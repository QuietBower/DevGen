/*
 * QEMU PCI device model for ml_ioh_gpio (based strictly on gpio-ml-ioh.c)
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

#define BIT_Q(nr) (1U << (nr))
#define IOH_EDGE_FALLING    0
#define IOH_EDGE_RISING     BIT_Q(0)
#define IOH_LEVEL_L         BIT_Q(1)
#define IOH_LEVEL_H         (BIT_Q(0) | BIT_Q(1))
#define IOH_EDGE_BOTH       BIT_Q(2)
#define IOH_IM_MASK         (BIT_Q(0) | BIT_Q(1) | BIT_Q(2))
#define IOH_IRQ_BASE        0
/* Fallback definition for vendor ID if PCI_VENDOR_ID_ROHM is not available */
#ifndef PCI_VENDOR_ID_ROHM
#define PCI_VENDOR_ID_ROHM 0x10DB
#endif
#define ML_IOH_GPIO_PCI_VENDOR_ID PCI_VENDOR_ID_ROHM
#define ML_IOH_GPIO_PCI_DEVICE_ID 0x802E

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
    bool sparse;
} BARInfo;

/* We infer minimal register layout from access patterns in the driver. */

typedef struct IOHChRegs {
    uint32_t pi;        /* input */
    uint32_t po;        /* output */
    uint32_t pm;        /* direction */
    uint32_t ien;       /* interrupt enable */
    uint32_t imask;     /* interrupt mask */
    uint32_t imaskclr;  /* interrupt mask clear */
    uint32_t im_0;      /* interrupt mode 0 */
    uint32_t im_1;      /* interrupt mode 1 */
    uint32_t iclr;      /* interrupt clear */
    uint32_t istatus;   /* interrupt status */
} IOHChRegs;

/* Matches struct ioh_regs layout used by the driver */
typedef struct IOHRegs {
    IOHChRegs regs[8];
    uint32_t reserve1[16];
    uint32_t ioh_sel_reg[4];
    uint32_t reserve2[11];
    uint32_t srst;
} IOHRegs;

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];

    uint8_t *mmio_backing;
    size_t mmio_backing_size;

    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;

    /* GPIO/IRQ state as seen by driver */
    IOHRegs regs;
};

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len);
static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len);
static void pcibase_reset(DeviceState *dev);
static void pcibase_realize(PCIDevice *pdev, Error **errp);
static void pcibase_uninit(PCIDevice *pdev);

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, bi->size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

/* Helpers to map driver-style structure field access to offsets */

static hwaddr ioh_ch_base(hwaddr base, int ch)
{
    /* sizeof(struct ioh_reg_comn) == sizeof(IOHChRegs) == 10 * 4 bytes */
    return base + ch * (10 * 4);
}

static hwaddr ioh_pi_off(hwaddr base, int ch)
{
    return ioh_ch_base(base, ch) + 0 * 4;
}

static hwaddr ioh_po_off(hwaddr base, int ch)
{
    return ioh_ch_base(base, ch) + 1 * 4;
}

static hwaddr ioh_pm_off(hwaddr base, int ch)
{
    return ioh_ch_base(base, ch) + 2 * 4;
}

static hwaddr ioh_ien_off(hwaddr base, int ch)
{
    return ioh_ch_base(base, ch) + 3 * 4;
}

static hwaddr ioh_imask_off(hwaddr base, int ch)
{
    return ioh_ch_base(base, ch) + 4 * 4;
}

static hwaddr ioh_imaskclr_off(hwaddr base, int ch)
{
    return ioh_ch_base(base, ch) + 5 * 4;
}

static hwaddr ioh_im0_off(hwaddr base, int ch)
{
    return ioh_ch_base(base, ch) + 6 * 4;
}

static hwaddr ioh_im1_off(hwaddr base, int ch)
{
    return ioh_ch_base(base, ch) + 7 * 4;
}

static hwaddr ioh_iclr_off(hwaddr base, int ch)
{
    return ioh_ch_base(base, ch) + 8 * 4;
}

static hwaddr ioh_istatus_off(hwaddr base, int ch)
{
    return ioh_ch_base(base, ch) + 9 * 4;
}

static hwaddr ioh_sel_reg_off(hwaddr base, int idx)
{
    /* regs[8] + reserve1[16] sit before ioh_sel_reg[4] */
    return base + (8 * (10 * 4)) + (16 * 4) + idx * 4;
}

static hwaddr ioh_srst_off(hwaddr base)
{
    /* srst is after regs[8], reserve1[16], ioh_sel_reg[4], reserve2[11] */
    return base + (8 * (10 * 4)) + (16 * 4) + (4 * 4) + (11 * 4);
}

static uint32_t ioh_mmio_read32(PCIBaseState *s, hwaddr addr)
{
    hwaddr base = 0;
    int ch;

    /* per-channel registers */
    for (ch = 0; ch < 8; ch++) {
        if (addr == ioh_pi_off(base, ch)) {
            return s->regs.regs[ch].pi;
        }
        if (addr == ioh_po_off(base, ch)) {
            return s->regs.regs[ch].po;
        }
        if (addr == ioh_pm_off(base, ch)) {
            return s->regs.regs[ch].pm;
        }
        if (addr == ioh_ien_off(base, ch)) {
            return s->regs.regs[ch].ien;
        }
        if (addr == ioh_imask_off(base, ch)) {
            return s->regs.regs[ch].imask;
        }
        if (addr == ioh_imaskclr_off(base, ch)) {
            /* readback: mask clear register is write-only in driver, return 0 */
            return 0;
        }
        if (addr == ioh_im0_off(base, ch)) {
            return s->regs.regs[ch].im_0;
        }
        if (addr == ioh_im1_off(base, ch)) {
            return s->regs.regs[ch].im_1;
        }
        if (addr == ioh_iclr_off(base, ch)) {
            /* write-only in driver */
            return 0;
        }
        if (addr == ioh_istatus_off(base, ch)) {
            return s->regs.regs[ch].istatus;
        }
    }

    /* ioh_sel_reg[0..3] */
    if (addr == ioh_sel_reg_off(base, 0)) {
        return s->regs.ioh_sel_reg[0];
    }
    if (addr == ioh_sel_reg_off(base, 1)) {
        return s->regs.ioh_sel_reg[1];
    }
    if (addr == ioh_sel_reg_off(base, 2)) {
        return s->regs.ioh_sel_reg[2];
    }
    if (addr == ioh_sel_reg_off(base, 3)) {
        return s->regs.ioh_sel_reg[3];
    }

    /* srst */
    if (addr == ioh_srst_off(base)) {
        return s->regs.srst;
    }

    return 0;
}

static void ioh_mmio_write32(PCIBaseState *s, hwaddr addr, uint32_t val)
{
    hwaddr base = 0;
    int ch;

    for (ch = 0; ch < 8; ch++) {
        if (addr == ioh_pi_off(base, ch)) {
            /* driver never writes pi */
            return;
        }
        if (addr == ioh_po_off(base, ch)) {
            s->regs.regs[ch].po = val;
            return;
        }
        if (addr == ioh_pm_off(base, ch)) {
            s->regs.regs[ch].pm = val;
            return;
        }
        if (addr == ioh_ien_off(base, ch)) {
            s->regs.regs[ch].ien = val;
            return;
        }
        if (addr == ioh_imask_off(base, ch)) {
            s->regs.regs[ch].imask = val;
            return;
        }
        if (addr == ioh_imaskclr_off(base, ch)) {
            /* clear bits in imask when written */
            s->regs.regs[ch].imask &= ~val;
            return;
        }
        if (addr == ioh_im0_off(base, ch)) {
            s->regs.regs[ch].im_0 = val;
            return;
        }
        if (addr == ioh_im1_off(base, ch)) {
            s->regs.regs[ch].im_1 = val;
            return;
        }
        if (addr == ioh_iclr_off(base, ch)) {
            /* clear interrupt status bits specified by val */
            s->regs.regs[ch].istatus &= ~val;
            return;
        }
        if (addr == ioh_istatus_off(base, ch)) {
            /* driver never writes istatus */
            return;
        }
    }

    if (addr == ioh_sel_reg_off(base, 0)) {
        s->regs.ioh_sel_reg[0] = val;
        return;
    }
    if (addr == ioh_sel_reg_off(base, 1)) {
        s->regs.ioh_sel_reg[1] = val;
        return;
    }
    if (addr == ioh_sel_reg_off(base, 2)) {
        s->regs.ioh_sel_reg[2] = val;
        return;
    }
    if (addr == ioh_sel_reg_off(base, 3)) {
        s->regs.ioh_sel_reg[3] = val;
        return;
    }

    if (addr == ioh_srst_off(base)) {
        /* driver writes 0x01 then 0x00 on resume */
        s->regs.srst = val;
        if (val == 0x01) {
            /* soft reset: clear all internal registers */
            memset(&s->regs, 0, sizeof(s->regs));
            s->regs.srst = 0x01;
        }
        if (val == 0x00) {
            s->regs.srst = 0x00;
        }
        return;
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR, "[%s] bad mmio_read size=%u addr=%" PRIx64 "\n", TYPE_PCIBASE_DEVICE, size, (uint64_t)addr);
        return 0;
    }

    return ioh_mmio_read32(s, addr);
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR, "[%s] bad mmio_write size=%u addr=%" PRIx64 "\n", TYPE_PCIBASE_DEVICE, size, (uint64_t)addr);
        return;
    }

    ioh_mmio_write32(s, addr, (uint32_t)val);
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* driver never uses PIO */
    qemu_log_mask(LOG_UNIMP, "[%s] pio_read addr=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* driver never uses PIO */
    qemu_log_mask(LOG_UNIMP, "[%s] pio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, (uint64_t)val, size);
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    pci_device_reset(pdev);

    memset(&s->regs, 0, sizeof(s->regs));

    if (s->mmio_backing && s->mmio_backing_size) {
        memset(s->mmio_backing, 0, s->mmio_backing_size);
    }
}

static void pcibase_dma_device_realize(PCIDevice *pdev, Error **errp)
{
    (void)pdev;
    (void)errp;
}

static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    uint32_t val = pci_default_read_config(pdev, addr, len);

    switch (len) {
    case 1:
        val &= 0xFF;
        break;
    case 2:
        val &= 0xFFFF;
        break;
    case 4:
    default:
        break;
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

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, ML_IOH_GPIO_PCI_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, ML_IOH_GPIO_PCI_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_OTHERS);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* one MMIO BAR at index 1 as used by the driver (pcim_iomap_regions BIT(1)) */
    s->num_bars = 1;
    s->bar_info[0].index = 1;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    /* size large enough to contain struct ioh_regs; must be power of two for pci_register_bar() */
    s->bar_info[0].size = 4096;
    s->bar_info[0].name = "ml-ioh-gpio-mmio";
    s->bar_info[0].sparse = false;

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
        if (errp && *errp) {
            return;
        }
    }

    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) {
        return;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] device realized\n", TYPE_PCIBASE_DEVICE);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (s->has_msix) {
        msix_uninit(pdev, NULL, 0);
        s->has_msix = false;
    }
    if (s->has_msi) {
        msi_uninit(pdev);
        s->has_msi = false;
    }

    if (s->mmio_backing) {
        g_free(s->mmio_backing);
        s->mmio_backing = NULL;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] device uninit\n", TYPE_PCIBASE_DEVICE);
}

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

