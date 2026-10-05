/*
 * QEMU PCI device model for ROHM IOH GPIO Controller
 * Based on Linux driver: drivers/gpio/gpio-ml-ioh.c
 *
 * Copyright (c) 2024
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_device.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qom/object.h"

#define TYPE_PCIBASE_DEVICE "ml_ioh_gpio_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Driver Constants */
#define PCI_VENDOR_ID_ROHM 0x10DB
#define DEVICE_ID 0x802E
#define CLASS_ID PCI_CLASS_OTHERS

#define IOH_EDGE_FALLING    0
#define IOH_EDGE_RISING     BIT(0)
#define IOH_LEVEL_L         BIT(1)
#define IOH_LEVEL_H         (BIT(0) | BIT(1))
#define IOH_EDGE_BOTH       BIT(2)
#define IOH_IM_MASK         (BIT(0) | BIT(1) | BIT(2))

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

struct ioh_regs {
    struct ioh_reg_comn regs[8];
    uint32_t reserve1[16];
    uint32_t ioh_sel_reg[4];
    uint32_t reserve2[11];
    uint32_t srst;
};

/* BAR metadata definition */
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
} BARInfo;

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    struct ioh_regs regs;
    qemu_irq irq;
};

static const int num_ports[] = {6, 12, 16, 16, 15, 16, 16, 12};

static void pcibase_update_irq(PCIBaseState *s)
{
    int i, j;
    int level = 0;

    for (i = 0; i < 8; i++) {
        /* 
         * Interrupt triggers if status is set, enabled (ien), 
         * and NOT masked (imask).
         * Note: imask bit 1 means MASKED.
         */
        uint32_t pending = s->regs.regs[i].istatus & 
                           s->regs.regs[i].ien & 
                           ~s->regs.regs[i].imask;
        
        /* Check only valid ports */
        for (j = 0; j < num_ports[i]; j++) {
            if (pending & BIT(j)) {
                level = 1;
                break;
            }
        }
        if (level) break;
    }
    pci_set_irq(PCI_DEVICE(s), level);
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    int ch;
    uint32_t offset;

    /* Channel registers: 8 channels * 0x30 bytes = 0x180 */
    if (addr < 0x180) {
        ch = addr / 0x30;
        offset = addr % 0x30;
        struct ioh_reg_comn *r = &s->regs.regs[ch];

        switch (offset) {
        case 0x00: return r->ien;
        case 0x04: return r->istatus;
        case 0x08: return r->idisp;
        case 0x0c: return 0; /* iclr is Write-Only */
        case 0x10: return r->imask;
        case 0x14: return 0; /* imaskclr is Write-Only */
        case 0x18: return r->po;
        case 0x1c: return r->pi;
        case 0x20: return r->pm;
        case 0x24: return r->im_0;
        case 0x28: return r->im_1;
        default: return 0;
        }
    }

    /* ioh_sel_reg at 0x1C0 (size 16 bytes) */
    if (addr >= 0x1C0 && addr < 0x1D0) {
        int idx = (addr - 0x1C0) / 4;
        return s->regs.ioh_sel_reg[idx];
    }

    /* srst at 0x1FC */
    if (addr == 0x1FC) {
        return s->regs.srst;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] mmio_read unhandled addr=0x%" PRIx64 "\n", 
                  TYPE_PCIBASE_DEVICE, addr);
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    int ch;
    uint32_t offset;

    if (addr < 0x180) {
        ch = addr / 0x30;
        offset = addr % 0x30;
        struct ioh_reg_comn *r = &s->regs.regs[ch];

        switch (offset) {
        case 0x00: /* ien */
            r->ien = val;
            pcibase_update_irq(s);
            break;
        case 0x04: /* istatus is RO (cleared via iclr) */
            break;
        case 0x08: /* idisp */
            r->idisp = val;
            break;
        case 0x0c: /* iclr: Write 1 to Clear istatus */
            r->istatus &= ~val;
            pcibase_update_irq(s);
            break;
        case 0x10: /* imask: Write 1 to Set Mask */
            r->imask |= val;
            pcibase_update_irq(s);
            break;
        case 0x14: /* imaskclr: Write 1 to Clear Mask */
            r->imask &= ~val;
            pcibase_update_irq(s);
            break;
        case 0x18: /* po */
            r->po = val;
            break;
        case 0x1c: /* pi is RO */
            break;
        case 0x20: /* pm */
            r->pm = val;
            break;
        case 0x24: /* im_0 */
            r->im_0 = val;
            break;
        case 0x28: /* im_1 */
            r->im_1 = val;
            break;
        default:
            break;
        }
        return;
    }

    if (addr >= 0x1C0 && addr < 0x1D0) {
        int idx = (addr - 0x1C0) / 4;
        s->regs.ioh_sel_reg[idx] = val;
        return;
    }

    if (addr == 0x1FC) {
        s->regs.srst = val;
        /* Driver writes 0x1 then 0x0 to reset */
        if (val == 0x1) {
            /* Perform soft reset of registers */
            memset(&s->regs.regs, 0, sizeof(s->regs.regs));
            pcibase_update_irq(s);
        }
        return;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] mmio_write unhandled addr=0x%" PRIx64 " val=0x%" PRIx64 "\n", 
                  TYPE_PCIBASE_DEVICE, addr, val);
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE)
        return;

    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    
    memset(&s->regs, 0, sizeof(s->regs));
    pcibase_update_irq(s);
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_ROHM);
    pci_set_word(pci_conf + PCI_DEVICE_ID, DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* BAR 1 is used by the driver for MMIO */
    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = 0x200; /* Sufficient for 0x1FC + 4 */
    s->bar_info[1].name = "ioh-gpio-mmio";
    s->num_bars = 6;

    pcibase_register_bar(pdev, s, &s->bar_info[1], errp);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    /* No special cleanup required for this device */
}

static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pcibase_realize;
    k->exit = pcibase_uninit;
    k->vendor_id = PCI_VENDOR_ID_ROHM;
    k->device_id = DEVICE_ID;
    k->class_id = CLASS_ID;
    dc->reset = pcibase_reset;
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

type_init(pcibase_register_types)