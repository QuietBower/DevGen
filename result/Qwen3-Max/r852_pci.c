/*
 * QEMU R852 PCI NAND Controller Emulation
 *
 * Copyright (c) 2025 QEMU Team
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 */

#include "qemu/osdep.h"
#include <inttypes.h>
#include <string.h>
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "sysemu/dma.h"
#include "sysemu/reset.h"
#include "hw/irq.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/pci/pcie.h"
#include "qom/object.h"
#include "qapi/visitor.h"
#include "hw/qdev-properties.h"

#define TYPE_PCIBASE_DEVICE "r852_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Related register/macros/constants from driver (Stage1) */
#define PCI_VENDOR_ID_RICOH 0x1180
#define VENDOR_ID 0x1180
#define DEVICE_ID 0x0852
#define CLASS_ID   PCI_CLASS_OTHERS

/* Register offsets */
#define R852_DATALINE		0x00
#define R852_CTL		0x04
#define R852_CARD_STA		0x05
#define R852_CARD_IRQ_STA	0x06
#define R852_DMA_ADDR		0x0C
#define R852_DMA_SETTINGS	0x10
#define R852_DMA_IRQ_STA	0x14
#define R852_DMA_IRQ_ENABLE	0x18
#define R852_HW			0x08

/* Control register bits */
#define R852_CTL_WRITE		0x80
#define R852_CTL_ON		0x04
#define R852_CTL_COMMAND 	0x01
#define R852_CTL_DATA		0x02
#define R852_CTL_CARDENABLE	0x10
#define R852_CTL_ECC_ENABLE	0x20
#define R852_CTL_ECC_ACCESS	0x40
#define R852_CTL_RESET		0x08

/* Card status bits */
#define R852_CARD_STA_BUSY	0x80
#define R852_CARD_STA_PRESENT	0x04
#define R852_CARD_STA_RO	0x02
#define R852_CARD_STA_CD	0x01

/* Card IRQ bits */
#define R852_CARD_IRQ_REMOVE	0x04
#define R852_CARD_IRQ_GENABLE	0x80
#define R852_CARD_IRQ_INSERT	0x08
#define R852_CARD_IRQ_MASK	0x1D
#define R852_CARD_IRQ_ENABLE	0x07

/* DMA settings */
#define R852_DMA2		0x80
#define R852_DMA_CAP		0x09
#define R852_DMA1		0x40
#define R852_DMA_READ		0x02
#define R852_DMA_INTERNAL	0x04
#define R852_DMA_MEMORY		0x01
#define R852_DMA_LEN		512

/* DMA IRQ bits */
#define R852_DMA_IRQ_INTERNAL	0x04
#define R852_DMA_IRQ_ERROR	0x02
#define R852_DMA_IRQ_MEMORY	0x01
#define R852_DMA_IRQ_MASK	0x07

/* HW register bits */
#define R852_HW_ENABLED		0x01
#define R852_HW_UNKNOWN		0x80
#define R852_SMBIT		0x20

/* ECC and OOB related constants from new driver source */
#define R852_ECC_FAIL		0x40
#define R852_ECC_CORRECTABLE	0x20
#define R852_ECC_ERR_BIT_MSK	0x07
#define SM_OOB_SIZE		16

struct sm_oob {
	uint32_t reserved;
	uint8_t data_status;
	uint8_t block_status;
	uint8_t lba_copy1[2];
	uint8_t ecc2[3];
	uint8_t lba_copy2[2];
	uint8_t ecc1[3];
};

/* ------------------------------------------------------------------ */
/* BAR metadata definition                                             */
/* ------------------------------------------------------------------ */
typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM 
} BARType;

typedef struct {
    int index;
    BARType type;
    hwaddr size;
    const char *name;
    bool sparse;
} BARInfo;

/* ------------------------------------------------------------------ */
/* Device State                                                        */
/* ------------------------------------------------------------------ */
struct PCIBaseState {
    PCIDevice parent_obj;

    /* BAR memory regions (MMIO/PIO unified handling fix) */
    MemoryRegion bar_regions[6];

    /* optional linear backing */
    uint8_t *mmio_backing;
    size_t mmio_backing_size;

    /* BAR table */
    BARInfo bar_info[6];
    int num_bars;

    /* interrupt state feature flags */
    bool has_msi;
    bool has_msix;
    
    /* DMA info placeholder */
    bool dma_usable;
    int dma_state;

    /* Register shadows */
    uint8_t ctlreg;
    uint8_t card_status;
    uint8_t card_irq_status;
    uint8_t dma_irq_status;
    uint8_t hwreg;

    /* Status fields */
    bool card_detected;
    bool readonly;
    bool sm;

    /* reset/probe state */
    bool engine_enabled;

    /* power mgmt */
    
    /* other fields */
    QEMUTimer dma_timer;
    uint32_t dma_addr;
    uint32_t dma_settings;
    uint32_t dma_irq_enable;
    uint8_t data_buffer[512];
    int dma_stage;
    bool dma_dir;

    /* ECC/OOB fields */
    struct sm_oob oob_data;
};


/* ------------------------------------------------------------------ */
/* Forward declarations                                                */
/* ------------------------------------------------------------------ */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len);
static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len);
static void pcibase_reset(DeviceState *dev);
static void pcibase_realize(PCIDevice *pdev, Error **errp);
static void pcibase_uninit(PCIDevice *pdev);

/* ------------------------------------------------------------------ */
/* MemoryRegionOps                                                      */
/* ------------------------------------------------------------------ */
static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* ------------------------------------------------------------------ */
/* Helper: register a BAR (MMIO or PIO)                                 */
/* ------------------------------------------------------------------ */
static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE)
        return;

    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    }else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, bi->size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }

}

/* ------------------------------------------------------------------ */
/* MMIO / PIO handlers (device-specific code goes into placeholders)   */
/* ------------------------------------------------------------------ */

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case R852_DATALINE:
        if (size == 1) {
            val = s->data_buffer[0];
        } else if (size == 4) {
            val = ldl_le_p(s->data_buffer);
        }
        break;
    case R852_CTL:
        val = s->ctlreg;
        break;
    case R852_CARD_STA:
        val = s->card_status;
        break;
    case R852_CARD_IRQ_STA:
        val = s->card_irq_status;
        break;
    case R852_HW:
        val = s->hwreg;
        break;
    case R852_DMA_ADDR:
        if (size == 4) {
            val = s->dma_addr;
        }
        break;
    case R852_DMA_SETTINGS:
        if (size == 4) {
            val = s->dma_settings;
        }
        break;
    case R852_DMA_IRQ_STA:
        if (size == 4) {
            val = s->dma_irq_status;
        }
        break;
    case R852_DMA_IRQ_ENABLE:
        if (size == 4) {
            val = s->dma_irq_enable;
        }
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "[%s] unhandled mmio_read addr=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case R852_DATALINE:
        if (size == 1) {
            s->data_buffer[0] = val;
        } else if (size == 4) {
            stl_le_p(s->data_buffer, val);
        }
        break;
    case R852_CTL:
        if (size == 1) {
            s->ctlreg = val;
            if (val & R852_CTL_RESET) {
                s->engine_enabled = false;
                s->hwreg = 0;
            }
            if ((val & R852_CTL_ON) && (val & R852_CTL_CARDENABLE)) {
                s->engine_enabled = true;
            }
        }
        break;
    case R852_CARD_IRQ_STA:
        if (size == 1) {
            s->card_irq_status &= ~val;
        }
        break;
    case R852_HW:
        if (size == 4) {
            s->hwreg = val & 0xFF;
            if (val & R852_HW_ENABLED) {
                s->engine_enabled = true;
            }
        }
        break;
    case R852_DMA_ADDR:
        if (size == 4) {
            s->dma_addr = val;
        }
        break;
    case R852_DMA_SETTINGS:
        if (size == 4) {
            s->dma_settings = val;
            s->dma_dir = !!(val & R852_DMA_READ);
            s->dma_state = (val & R852_DMA_INTERNAL) ? 1 : 2; /* DMA_INTERNAL or DMA_MEMORY */
        }
        break;
    case R852_DMA_IRQ_STA:
        if (size == 4) {
            s->dma_irq_status &= ~val;
        }
        break;
    case R852_DMA_IRQ_ENABLE:
        if (size == 4) {
            s->dma_irq_enable = val;
        }
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "[%s] unhandled mmio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, (uint64_t)val, size);
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "[%s] pio_read addr=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "[%s] pio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, (uint64_t)val, size);
}

/* ------------------------------------------------------------------ */
/* Reset       */
/* ------------------------------------------------------------------ */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    pci_device_reset(pdev);

    s->ctlreg = 0;
    s->card_status = R852_CARD_STA_PRESENT | R852_CARD_STA_CD;
    s->card_irq_status = 0;
    s->dma_irq_status = 0;
    s->hwreg = R852_HW_UNKNOWN;
    s->dma_usable = true;
    s->dma_state = 1; /* DMA_INTERNAL */
    s->card_detected = true;
    s->readonly = false;
    s->sm = true;
    s->engine_enabled = false;
    s->dma_addr = 0;
    s->dma_settings = 0;
    s->dma_irq_enable = 0;
    s->dma_stage = 0;
    s->dma_dir = false;
    memset(s->data_buffer, 0, sizeof(s->data_buffer));
    memset(&s->oob_data, 0, sizeof(s->oob_data));
}

/* ------------------------------------------------------------------ */
/* DMA initialize (called from realize)                                */
/* ------------------------------------------------------------------ */
static void pcibase_dma_device_realize(PCIDevice *pdev, Error **errp)
{
    (void)pdev;
    (void)errp;
}

/* ------------------------------------------------------------------ */
/* PCI config space access (class-level hooks)                         */
/* ------------------------------------------------------------------ */
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    uint32_t val = pci_default_read_config(pdev, addr, len);

    switch (len) {
        case 1: val &= 0xFF; break;
        case 2: val &= 0xFFFF; break;
        case 4: break;
        default: break;
    }
    return val;
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    if (addr >= PCI_BASE_ADDRESS_0 && addr <= PCI_BASE_ADDRESS_5) {
        pci_default_write_config(pdev, addr, val, len);
        return;
    }
}

/* ------------------------------------------------------------------ */
/* Realize (device init)                                                */
/* ------------------------------------------------------------------ */
static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x1180 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x0852 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_OTHERS );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);
    
    /* Initialize BARs - only BAR0 is used as MMIO */
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x100; /* Based on register offsets up to 0x18 */
    s->bar_info[0].name = "r852-mmio";
    s->num_bars = 1;

    /* register BARs */
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Interrupt config (placeholders) */
    /* Legacy INTx only - no MSI/MSIX in driver */

    /* DMA init (called here so device can set DMA masks/etc) */
    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) return;

    /* DMA is usable by default */
    s->dma_usable = true;
    s->dma_state = 1; /* Internal DMA by default */

    /* Initialize timer for DMA simulation */
    timer_init_ms(&s->dma_timer, QEMU_CLOCK_VIRTUAL, NULL, s);

    qemu_log_mask(LOG_UNIMP, "[%s] device realized\n", TYPE_PCIBASE_DEVICE);
}

/* ------------------------------------------------------------------ */
/* Uninit/cleanup                                                      */
/* ------------------------------------------------------------------ */
static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (s->has_msix) {
        msix_uninit(pdev,NULL,0);
        s->has_msix = false;
    }
    if (s->has_msi) {
        msi_uninit(pdev);
        s->has_msi = false;
    }

    /* free mmio_backing or other allocations */

    if (s->mmio_backing) {
        g_free(s->mmio_backing);
        s->mmio_backing = NULL;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] device uninit\n", TYPE_PCIBASE_DEVICE);
}

/* ------------------------------------------------------------------ */
/* Class init / type registration                                      */
/* ------------------------------------------------------------------ */
static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->config_read  = pcibase_config_read;
    k->config_write = pcibase_config_write;

    k->realize = pcibase_realize;
    k->exit    = pcibase_uninit;
    dc->reset  = pcibase_reset;

    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void pcibase_register_types(void)
{
    static InterfaceInfo interfaces[] = {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    };

    static const TypeInfo pcibase_info = {
        .name = TYPE_PCIBASE_DEVICE,
        .parent = TYPE_PCI_DEVICE,
        .instance_size = sizeof(PCIBaseState),
        .class_init = pcibase_class_init,
        .interfaces = interfaces,
    };

    type_register_static(&pcibase_info);
}

type_init(pcibase_register_types);