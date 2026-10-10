/*
 * This template provides a robust skeleton for hardware emulation.
 * Integrated QEMU PCI device template (QEMU 8.2.10).
 * Replace #PLACEHOLDER# blocks with driver-specific definitions.
 * Designed for register-level modeling and PCIe driver probing.
 * NOTE: Do not directly fill this file with the Linux kernel source code.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "qemu/host-utils.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "sysemu/dma.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-core.h"
#include "migration/vmstate.h"

#define TYPE_PCIBASE_DEVICE "pata_ns87415_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x100b
#define DEVICE_ID 0x0002
#define CLASS_ID  0x0000

/* ATA BMDMA constants */
#define ATA_DMA_START   0x01
#define ATA_DMA_ERR     0x02
#define ATA_DMA_INTR    0x04
#define ATA_DMA_WR      0x08

typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM 
} BARType;

typedef struct {
    int     index;    /* BAR index 0-5 */
    BARType type;
    hwaddr  size;
    const char *name;
} BARInfo;

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    /* BMDMA shadow registers */
    uint8_t bmdma_cmd;      /* Command register (offset 0) */
    uint8_t bmdma_status;   /* Status register (offset 2) */
    uint32_t bmdma_prd;     /* PRD table address (offset 4) */
    QEMUTimer dma_timer;    /* Timer to simulate DMA completion */

    /* Extra PCI config space (vendor-specific registers) */
    uint8_t extra_cfg[0x56]; /* offset 0x00-0x55, used for 0x42-0x55 */
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    int level = 0;
    if (s->bmdma_status & ATA_DMA_INTR) {
        level = 1;
    }
    pci_set_irq(pdev, level);
}

/* DMA timer callback: simulate DMA completion, set interrupt */
static void pcibase_dma_timer(void *opaque)
{
    PCIBaseState *s = opaque;
    s->bmdma_status |= ATA_DMA_INTR;  /* set interrupt */
    s->bmdma_cmd |= ATA_DMA_INTR;    /* status reflected in command */
    s->bmdma_status &= ~0x01;        /* clear BM active */
    s->bmdma_cmd &= ~ATA_DMA_START;  /* clear start bit */
    pcibase_update_irq(s);
}

/* PIO handlers for BMDMA BAR (BAR4) */
static uint64_t pcibase_bmdma_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case 0:
        val = s->bmdma_cmd;
        break;
    case 2:
        val = s->bmdma_status;
        break;
    case 4:
        if (size == 4) {
            val = s->bmdma_prd;
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: unsupported read size %d at addr 0x%" HWADDR_PRIx "\n",
                          __func__, size, addr);
        }
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: unexpected read at addr 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        break;
    }
    return val;
}

static void pcibase_bmdma_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint8_t old_cmd, new_val_byte;

    switch (addr) {
    case 0:
        if (size != 1) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: unsupported write size %d at addr 0x%" HWADDR_PRIx "\n",
                          __func__, size, addr);
            return;
        }
        new_val_byte = val & 0xFF;
        old_cmd = s->bmdma_cmd;

        /* W1C: clear bits 1 and 2 if set in val */
        s->bmdma_cmd &= ~(new_val_byte & (ATA_DMA_ERR | ATA_DMA_INTR));
        if (new_val_byte & ATA_DMA_ERR) {
            s->bmdma_status &= ~ATA_DMA_ERR;
        }
        if (new_val_byte & ATA_DMA_INTR) {
            s->bmdma_status &= ~ATA_DMA_INTR;
        }
        /* Set bits 0 (start) and 3 (direction) from val */
        s->bmdma_cmd = (s->bmdma_cmd & ~0x09) | (new_val_byte & 0x09);

        /* Handle start/stop transitions */
        if (!(old_cmd & ATA_DMA_START) && (new_val_byte & ATA_DMA_START)) {
            /* Start DMA */
            s->bmdma_status |= 0x01;  /* set BM active */
            timer_mod(&s->dma_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 10);
        } else if ((old_cmd & ATA_DMA_START) && !(new_val_byte & ATA_DMA_START)) {
            /* Stop DMA */
            s->bmdma_status &= ~0x01;
            timer_del(&s->dma_timer);
        }
        pcibase_update_irq(s);
        break;
    case 2:
        if (size != 1) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: unsupported write size %d at addr 0x%" HWADDR_PRIx "\n",
                          __func__, size, addr);
            return;
        }
        new_val_byte = val & 0xFF;
        /* W1C clear for status */
        s->bmdma_status &= ~(new_val_byte & (ATA_DMA_ERR | ATA_DMA_INTR));
        if (new_val_byte & ATA_DMA_ERR) {
            s->bmdma_cmd &= ~ATA_DMA_ERR;
        }
        if (new_val_byte & ATA_DMA_INTR) {
            s->bmdma_cmd &= ~ATA_DMA_INTR;
        }
        pcibase_update_irq(s);
        break;
    case 4:
        if (size == 4) {
            s->bmdma_prd = val & 0xFFFFFFFF;
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: unsupported write size %d at addr 0x%" HWADDR_PRIx "\n",
                          __func__, size, addr);
        }
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: unexpected write at addr 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        break;
    }
}

/* PIO handlers for IDE channel BARs (BAR0-3) - minimal, just return 0 */
static uint64_t pcibase_ide_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_ide_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* NOP */
}

static const MemoryRegionOps pcibase_bmdma_ops = {
    .read = pcibase_bmdma_read,
    .write = pcibase_bmdma_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static const MemoryRegionOps pcibase_ide_ops = {
    .read = pcibase_ide_read,
    .write = pcibase_ide_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* Unused MMIO ops, kept for template completeness */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Reset BMDMA state */
    s->bmdma_cmd = 0;
    s->bmdma_status = 0;
    s->bmdma_prd = 0;
    timer_del(&s->dma_timer);

    /* Clear extra PCI config registers */
    memset(s->extra_cfg, 0, sizeof(s->extra_cfg));
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }
    
    /* CRITICAL: PCI requires BAR sizes to be a power of 2. Prevent QEMU assert crash. */
    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        /* Select appropriate ops based on BAR index */
        const MemoryRegionOps *ops;
        if (bi->index == 4) {
            ops = &pcibase_bmdma_ops;
        } else {
            ops = &pcibase_ide_ops;
        }
        memory_region_init_io(mr, OBJECT(s), ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

/* Custom PCI config read/write to allow vendor-specific registers (0x42-0x55) */
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint32_t val = 0;

    if (addr >= 0x42 && addr + len <= 0x56) {
        memcpy(&val, s->extra_cfg + addr, len);
        return val;
    }
    return pci_default_read_config(pdev, addr, len);
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (addr >= 0x42 && addr + len <= 0x56) {
        memcpy(s->extra_cfg + addr, &val, len);
        return;
    }
    pci_default_write_config(pdev, addr, val, len);
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Override config read/write to handle extra registers */
    pdev->config_read = pcibase_config_read;
    pdev->config_write = pcibase_config_write;

    /* Initialize DMA timer */
    timer_init_ms(&s->dma_timer, QEMU_CLOCK_VIRTUAL, pcibase_dma_timer, s);

    /* Set up BAR information: 5 BARs (0-3 for IDE channels, 4 for BMDMA) */
    s->num_bars = 5;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_PIO, .size = 8, .name = "ide-pri-cmd" };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_PIO, .size = 4, .name = "ide-pri-ctl" };
    s->bar_info[2] = (BARInfo){ .index = 2, .type = BAR_TYPE_PIO, .size = 8, .name = "ide-sec-cmd" };
    s->bar_info[3] = (BARInfo){ .index = 3, .type = BAR_TYPE_PIO, .size = 4, .name = "ide-sec-ctl" };
    s->bar_info[4] = (BARInfo){ .index = 4, .type = BAR_TYPE_PIO, .size = 16, .name = "bmdma" };

    /* BAR Initialization */
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
    timer_del(&s->dma_timer);
}

/* VMState to support migration, include BMDMA state and extra config */
static const VMStateDescription vmstate_pcibase = {
    .name = "pata_ns87415_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT8(bmdma_cmd, PCIBaseState),
        VMSTATE_UINT8(bmdma_status, PCIBaseState),
        VMSTATE_UINT32(bmdma_prd, PCIBaseState),
        VMSTATE_BUFFER(extra_cfg, PCIBaseState),
        VMSTATE_END_OF_LIST()
    }
};

static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pcibase_realize;
    k->exit    = pcibase_uninit;
    dc->reset  = pcibase_reset;
    dc->vmsd   = &vmstate_pcibase;
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
