/*
 * QEMU virtual device model for Hilscher netX PCI device (uio_netx driver)
 * Based on driver source: uio_netx.c
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/module.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_device.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"

#define TYPE_PCIBASE_DEVICE "netx_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI_VENDOR_ID_NETX 0x15CF
#define PCI_DEVICE_ID_NETX 0x0000
#define PCI_CLASS_NETX 0x00ff

/* Register offsets as seen in driver */
#define DPM_HOST_INT_EN0     0xfff0
#define DPM_HOST_INT_STAT0   0xffe0
#define DPM_HOST_INT_MASK    0xe600ffff
#define DPM_HOST_INT_GLOBAL_EN 0x80000000

#define BAR0_SIZE 0x10000

struct PCIBaseState {
    PCIDevice parent_obj;
    MemoryRegion mmio;
    uint32_t intr_status;
    uint32_t intr_enable;
};

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case DPM_HOST_INT_STAT0:
        val = s->intr_status;
        break;
    case DPM_HOST_INT_EN0:
        val = s->intr_enable;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "MMIO read [0x%"HWADDR_PRIx"] size %u\n", addr, size);
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case DPM_HOST_INT_EN0:
        s->intr_enable = val;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "MMIO write [0x%"HWADDR_PRIx"] = 0x%"PRIx64" size %u\n", addr, val, size);
        break;
    }
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
    s->intr_status = 0;
    s->intr_enable = 0;
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_NETX);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_NETX);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETX);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    memory_region_init_io(&s->mmio, OBJECT(s), &pcibase_mmio_ops, s, "netx-mmio", BAR0_SIZE);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mmio);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    /* Nothing to clean up */
}

static const VMStateDescription vmstate_pcibase = {
    .name = "netx_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(intr_status, PCIBaseState),
        VMSTATE_UINT32(intr_enable, PCIBaseState),
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
