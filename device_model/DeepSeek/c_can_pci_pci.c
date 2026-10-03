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

#define TYPE_PCIBASE_DEVICE "c_can_pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Vendor ID, Device ID, Class ID, and register offsets */
#define PCI_VENDOR_ID_STMICRO 0x104A
#define PCI_DEVICE_ID_STMICRO_CAN 0xCC11
#define PCI_CLASS_CAN 0x0280

/* Register offsets from reg_map_c_can (C_CAN) */
#define C_CAN_CTRL_REG		0x00
#define C_CAN_STS_REG		0x02
#define C_CAN_ERR_CNT_REG	0x04
#define C_CAN_BTR_REG		0x06
#define C_CAN_INT_REG		0x08
#define C_CAN_TEST_REG		0x0A
#define C_CAN_BRPEXT_REG	0x0C
#define C_CAN_IF1_COMREQ_REG	0x10
#define C_CAN_IF1_COMMSK_REG	0x12
#define C_CAN_IF1_MASK1_REG	0x14
#define C_CAN_IF1_MASK2_REG	0x16
#define C_CAN_IF1_ARB1_REG	0x18
#define C_CAN_IF1_ARB2_REG	0x1A
#define C_CAN_IF1_MSGCTRL_REG	0x1C
#define C_CAN_IF1_DATA1_REG	0x1E
#define C_CAN_IF1_DATA2_REG	0x20
#define C_CAN_IF1_DATA3_REG	0x22
#define C_CAN_IF1_DATA4_REG	0x24
#define C_CAN_IF2_COMREQ_REG	0x40
#define C_CAN_IF2_COMMSK_REG	0x42
#define C_CAN_IF2_MASK1_REG	0x44
#define C_CAN_IF2_MASK2_REG	0x46
#define C_CAN_IF2_ARB1_REG	0x48
#define C_CAN_IF2_ARB2_REG	0x4A
#define C_CAN_IF2_MSGCTRL_REG	0x4C
#define C_CAN_IF2_DATA1_REG	0x4E
#define C_CAN_IF2_DATA2_REG	0x50
#define C_CAN_IF2_DATA3_REG	0x52
#define C_CAN_IF2_DATA4_REG	0x54
#define C_CAN_TXRQST1_REG	0x80
#define C_CAN_TXRQST2_REG	0x82
#define C_CAN_NEWDAT1_REG	0x90
#define C_CAN_NEWDAT2_REG	0x92
#define C_CAN_INTPND1_REG	0xA0
#define C_CAN_INTPND2_REG	0xA2
#define C_CAN_INTPND3_REG	0xA4
#define C_CAN_MSGVAL1_REG	0xB0
#define C_CAN_MSGVAL2_REG	0xB2
#define C_CAN_FUNCTION_REG	0xC0

#define REG_BYTE(reg) (2 * (reg))

typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM 
} BARType;

typedef struct {
    int     index;
    BARType type;
    hwaddr  size;
    const char *name;
} BARInfo;

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;
    uint32_t int_reg;
    uint32_t int_mask;

    uint8_t regs[0x1000];

    uint8_t power_state;
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    uint16_t ctrl = lduw_le_p(s->regs + REG_BYTE(C_CAN_CTRL_REG));
    uint16_t int_val = lduw_le_p(s->regs + REG_BYTE(C_CAN_INT_REG));
    uint16_t intpnd1 = lduw_le_p(s->regs + REG_BYTE(C_CAN_INTPND1_REG));
    uint16_t intpnd2 = lduw_le_p(s->regs + REG_BYTE(C_CAN_INTPND2_REG));
    uint16_t intpnd3 = lduw_le_p(s->regs + REG_BYTE(C_CAN_INTPND3_REG));

    bool irq = (ctrl & 0x02) && (int_val || intpnd1 || intpnd2 || intpnd3);

    if (msi_enabled(&s->parent_obj)) {
        if (irq) {
            msi_notify(&s->parent_obj, 0);
        }
    } else {
        pci_set_irq(&s->parent_obj, irq ? 1 : 0);
    }
}

/* MMIO read/write handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    if (size != 2) {
        qemu_log_mask(LOG_UNIMP, "c_can_pci: unsupported read size %u at 0x%"HWADDR_PRIx"\n", size, addr);
        return 0;
    }
    return lduw_le_p(s->regs + addr);
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    if (size != 2) {
        qemu_log_mask(LOG_UNIMP, "c_can_pci: unsupported write size %u at 0x%"HWADDR_PRIx"\n", size, addr);
        return;
    }
    uint16_t v = (uint16_t)val;
    uint16_t *reg = (uint16_t *)(s->regs + addr);

    switch (addr >> 1) {
    case C_CAN_INT_REG:
        *reg &= ~v; /* write 1 to clear */
        break;
    case C_CAN_INTPND1_REG:
    case C_CAN_INTPND2_REG:
    case C_CAN_INTPND3_REG:
        *reg &= ~v;
        break;
    case C_CAN_STS_REG:
    case C_CAN_ERR_CNT_REG:
        /* read-only, ignore write */
        break;
    default:
        *reg = v;
        break;
    }

    pcibase_update_irq(s);
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
    memset(s->regs, 0, sizeof(s->regs));
    s->int_reg = 0;
    s->power_state = 0;
    /* Put controller into initialization mode */
    stw_le_p(s->regs + REG_BYTE(C_CAN_CTRL_REG), 0x0001);
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }
    
    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_STMICRO);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_STMICRO_CAN);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_CAN);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    s->num_bars = 1;
    s->bar_info[0] = (BARInfo) {
        .index = 0,
        .type = BAR_TYPE_MMIO,
        .size = 0x1000,
        .name = "bar0",
    };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    if (msi_init(pdev, 0, 1, true, false, errp)) {
        /* MSI init failed, continue with INTx */
        s->has_msi = false;
    } else {
        s->has_msi = true;
    }
    s->has_msix = false;
    s->power_state = 0;
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
}

static const VMStateDescription vmstate_pcibase = {
    .name = "c_can_pci_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
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