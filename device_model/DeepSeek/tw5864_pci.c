/*
 * QEMU TW5864 PCI device model
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "hw/qdev-core.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_device.h"
#include "hw/pci/pcie.h"
#include "migration/vmstate.h"

#define TYPE_PCIBASE_DEVICE "tw5864_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI_VENDOR_ID_TECHWELL 0x1797
#define PCI_DEVICE_ID_TECHWELL_5864 0x5864
#define BAR0_SIZE (128 * 1024)

/* Register offsets from driver */
#define TW5864_HW_VERSION             0xb004
#define TW5864_UNDECLARED_H264REV_PART2 0x0008
#define TW5864_H264REV                0x0000
#define TW5864_INTR_ENABLE_H          0x880c
#define TW5864_INTR_ENABLE_L          0x8808
#define TW5864_INTR_STATUS_L          0x8838
#define TW5864_INTR_STATUS_H          0x883c
#define TW5864_INTR_CLR_H             0x8814
#define TW5864_INTR_CLR_L             0x8810
#define TW5864_DSP                    0x002c
#define TW5864_VLC_LENGTH             0x18024
#define TW5864_PCI_INTR_STATUS        0x18000
#define TW5864_VLC_CRC_REG            0x1801c
#define TW5864_VLC_DSP_INTR           0x1014
#define TW5864_VLC_STREAM_BASE_ADDR   0x18080
#define TW5864_MV_STREAM_BASE_ADDR    0x18084
#define TW5864_ENC_BUF_PTR_REC1       0x0010
#define TW5864_SENIF_ORG_FRM_PTR1     0x0038

struct PCIBaseState {
    PCIDevice parent_obj;
    MemoryRegion mmio;
    uint32_t mmio_regs[BAR0_SIZE / sizeof(uint32_t)];
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t en_l = s->mmio_regs[TW5864_INTR_ENABLE_L >> 2];
    uint32_t en_h = s->mmio_regs[TW5864_INTR_ENABLE_H >> 2];
    uint32_t st_l = s->mmio_regs[TW5864_INTR_STATUS_L >> 2];
    uint32_t st_h = s->mmio_regs[TW5864_INTR_STATUS_H >> 2];
    int irq = (st_l & en_l) || (st_h & en_h) ? 1 : 0;
    pci_set_irq(pdev, irq);
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr >= BAR0_SIZE || size != 4) {
        return 0;
    }
    return s->mmio_regs[addr >> 2];
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr >= BAR0_SIZE || size != 4) {
        return;
    }

    switch (addr) {
    case TW5864_INTR_CLR_L:
        s->mmio_regs[addr >> 2] &= ~((uint32_t)val);
        pcibase_update_irq(s);
        break;
    case TW5864_INTR_CLR_H:
        s->mmio_regs[addr >> 2] &= ~((uint32_t)val);
        pcibase_update_irq(s);
        break;
    case TW5864_PCI_INTR_STATUS:
        s->mmio_regs[addr >> 2] &= ~((uint32_t)val);
        pcibase_update_irq(s);
        break;
    case TW5864_VLC_DSP_INTR:
        s->mmio_regs[addr >> 2] = (uint32_t)val;
        pcibase_update_irq(s);
        break;
    default:
        s->mmio_regs[addr >> 2] = (uint32_t)val;
        if (addr == TW5864_INTR_ENABLE_L || addr == TW5864_INTR_ENABLE_H) {
            pcibase_update_irq(s);
        }
        break;
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    memset(s->mmio_regs, 0, sizeof(s->mmio_regs));
    /* Hardware version registers */
    s->mmio_regs[TW5864_HW_VERSION >> 2] = 0x0001;
    s->mmio_regs[TW5864_H264REV >> 2] = 0x0002;
    s->mmio_regs[TW5864_UNDECLARED_H264REV_PART2 >> 2] = 0x0003;
    pcibase_update_irq(s);
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_TECHWELL);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_TECHWELL_5864);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0400);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    memory_region_init_io(&s->mmio, OBJECT(s), &pcibase_mmio_ops, s, "tw5864-mmio", BAR0_SIZE);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mmio);
}

static void pcibase_uninit(PCIDevice *pdev)
{
}

static const VMStateDescription vmstate_pcibase = {
    .name = "tw5864_pci",
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
        { INTERFACE_PCIE_DEVICE },
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
