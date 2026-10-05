/*
 * QEMU ML-IOH GPIO PCI Device Emulation
 *
 * Based on Linux driver: /linux-6.18/drivers/gpio/gpio-ml-ioh.c
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

#define TYPE_PCIBASE_DEVICE "ml_ioh_gpio_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Related register/macros/constants from driver (Stage1) */
#define IOH_EDGE_FALLING	0
#define IOH_EDGE_RISING		BIT(0)
#define IOH_LEVEL_L		BIT(1)
#define IOH_LEVEL_H		(BIT(0) | BIT(1))
#define IOH_EDGE_BOTH		BIT(2)
#define IOH_IM_MASK		(BIT(0) | BIT(1) | BIT(2))
#define IOH_IRQ_BASE		0

#define PCI_VENDOR_ID_ROHM	0x10DB
#define VENDOR_ID		PCI_VENDOR_ID_ROHM
#define DEVICE_ID		0x802E
/* PCI_CLASS_SYSTEM_GPIO is not defined in QEMU headers, use raw value */
#define CLASS_ID		0x0C00 /* PCI_CLASS_SYSTEM_GPIO = 0x0C00 */

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

/* Define ioh_reg_comn before using it in arrays */
struct ioh_reg_comn {
    uint32_t ien;
    uint32_t istatus;
    uint32_t idisp;
    uint32_t iclr;
    uint32_t imask;
    uint32_t imaskclr;
    uint32_t po;
    uint32_t pi;
    uint32_t pm;
    uint32_t im_0;
    uint32_t im_1;
    uint32_t reserved;
};

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
    
    /* Register shadows */
    struct ioh_regs {
        struct ioh_reg_comn regs[8];
        uint32_t reserve1[16];
        uint32_t ioh_sel_reg[4];
        uint32_t reserve2[11];
        uint32_t srst;
    } reg;

    /* Status fields */
    struct ioh_reg_comn regs[8];

    /* reset/probe state */
    int ch;
    int irq_base;

    /* power mgmt */
    
    /* other fields */
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

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR, "[%s] invalid read size %d at addr=%" PRIx64 "\n", TYPE_PCIBASE_DEVICE, size, (uint64_t)addr);
        return 0;
    }

    /* Determine which channel and register */
    int channel = addr / 0x40;
    if (channel >= 8) {
        /* Check for special registers */
        if (addr >= 0x200 && addr < 0x210) {
            int sel_idx = (addr - 0x200) / 4;
            if (sel_idx < 4) {
                val = s->reg.ioh_sel_reg[sel_idx];
            }
        } else if (addr == 0x23c) {
            val = s->reg.srst;
        }
        return val;
    }

    hwaddr offset = addr % 0x40;
    switch (offset) {
    case 0x00: /* ien */
        val = s->regs[channel].ien;
        break;
    case 0x04: /* istatus */
        val = s->regs[channel].istatus;
        break;
    case 0x08: /* idisp */
        val = s->regs[channel].idisp;
        break;
    case 0x0c: /* iclr */
        val = s->regs[channel].iclr;
        break;
    case 0x10: /* imask */
        val = s->regs[channel].imask;
        break;
    case 0x14: /* imaskclr */
        val = s->regs[channel].imaskclr;
        break;
    case 0x18: /* po */
        val = s->regs[channel].po;
        break;
    case 0x1c: /* pi */
        val = s->regs[channel].pi;
        break;
    case 0x20: /* pm */
        val = s->regs[channel].pm;
        break;
    case 0x24: /* im_0 */
        val = s->regs[channel].im_0;
        break;
    case 0x28: /* im_1 */
        val = s->regs[channel].im_1;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "[%s] unimplemented read at addr=%" PRIx64 "\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr);
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR, "[%s] invalid write size %d at addr=%" PRIx64 "\n", TYPE_PCIBASE_DEVICE, size, (uint64_t)addr);
        return;
    }

    /* Determine which channel and register */
    int channel = addr / 0x40;
    if (channel >= 8) {
        /* Check for special registers */
        if (addr >= 0x200 && addr < 0x210) {
            int sel_idx = (addr - 0x200) / 4;
            if (sel_idx < 4) {
                s->reg.ioh_sel_reg[sel_idx] = val;
            }
        } else if (addr == 0x23c) {
            s->reg.srst = val;
            if (val == 0x01) {
                /* Reset all registers */
                for (int i = 0; i < 8; i++) {
                    memset(&s->regs[i], 0, sizeof(s->regs[i]));
                }
                memset(s->reg.ioh_sel_reg, 0, sizeof(s->reg.ioh_sel_reg));
            }
        }
        return;
    }

    hwaddr offset = addr % 0x40;
    switch (offset) {
    case 0x00: /* ien */
        s->regs[channel].ien = val;
        break;
    case 0x04: /* istatus */
        s->regs[channel].istatus = val;
        break;
    case 0x08: /* idisp */
        s->regs[channel].idisp = val;
        break;
    case 0x0c: /* iclr */
        /* Clear interrupt status bits */
        s->regs[channel].istatus &= ~val;
        break;
    case 0x10: /* imask */
        s->regs[channel].imask = val;
        break;
    case 0x14: /* imaskclr */
        /* Clear mask bits */
        s->regs[channel].imask &= ~val;
        break;
    case 0x18: /* po */
        s->regs[channel].po = val;
        break;
    case 0x1c: /* pi */
        s->regs[channel].pi = val;
        break;
    case 0x20: /* pm */
        s->regs[channel].pm = val;
        break;
    case 0x24: /* im_0 */
        s->regs[channel].im_0 = val;
        break;
    case 0x28: /* im_1 */
        s->regs[channel].im_1 = val;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "[%s] unimplemented write at addr=%" PRIx64 " val=%" PRIx64 "\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, (uint64_t)val);
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* Remove unused variable to fix warning */
    qemu_log_mask(LOG_UNIMP, "[%s] pio_read addr=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Remove unused variable to fix warning */
    qemu_log_mask(LOG_UNIMP, "[%s] pio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, (uint64_t)val, size);
}

/* ------------------------------------------------------------------ */
/* Reset       */
/* ------------------------------------------------------------------ */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    /* core resets */
    pci_device_reset(pdev);

    if (s->mmio_backing && s->mmio_backing_size)
        memset(s->mmio_backing, 0, s->mmio_backing_size);

    /* Reset all registers */
    for (int i = 0; i < 8; i++) {
        memset(&s->regs[i], 0, sizeof(s->regs[i]));
    }
    memset(s->reg.ioh_sel_reg, 0, sizeof(s->reg.ioh_sel_reg));
    s->reg.srst = 0;
}

/* ------------------------------------------------------------------ */
/* DMA initialize (called from realize)                                */
/* ------------------------------------------------------------------ */
static void pcibase_dma_device_realize(PCIDevice *pdev, Error **errp)
{
    /* Remove unused variables to fix warnings */
    (void)pdev;
    (void)errp;
}

/* ------------------------------------------------------------------ */
/* PCI config space access (class-level hooks)                         */
/* ------------------------------------------------------------------ */
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    uint32_t val = pci_default_read_config(pdev, addr, len);
    /* optional: override certain config offsets if needed */

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
    /* Call default handler for other addresses */
    pci_default_write_config(pdev, addr, val, len);
}

/* ------------------------------------------------------------------ */
/* Realize (device init)                                                */
/* ------------------------------------------------------------------ */
static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);
    
    s->irq_base = IOH_IRQ_BASE;
    s->ch = 0;

    /* Set up BAR1 as MMIO with size rounded up to next power of 2 (0x240 -> 0x400) */
    s->bar_info[0].index = 1;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x400;  /* Next power of 2 >= 0x240 */
    s->bar_info[0].name = "ml_ioh_gpio-mmio";
    s->num_bars = 1;

    /* register BARs */
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Interrupt config (placeholders) */

    /* DMA init (called here so device can set DMA masks/etc) */
    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) return;

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
