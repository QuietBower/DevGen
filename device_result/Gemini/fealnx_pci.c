#include "qemu/osdep.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_device.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "qemu/log.h"

#define TYPE_FEALNX "fealnx_pci"
OBJECT_DECLARE_SIMPLE_TYPE(FealnxState, FEALNX)

struct FealnxState {
    PCIDevice parent_obj;
    MemoryRegion mmio;
    MemoryRegion io;
};

static uint64_t fealnx_io_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void fealnx_io_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static const MemoryRegionOps fealnx_io_ops = {
    .read = fealnx_io_read,
    .write = fealnx_io_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static void fealnx_realize(PCIDevice *pdev, Error **errp)
{
    FealnxState *s = FEALNX(pdev);

    memory_region_init_io(&s->io, OBJECT(s), &fealnx_io_ops, s, "fealnx-io", 256);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_IO, &s->io);

    memory_region_init_io(&s->mmio, OBJECT(s), &fealnx_io_ops, s, "fealnx-mmio", 256);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mmio);
}

static void fealnx_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = fealnx_realize;
    k->vendor_id = 0x125b;
    k->device_id = 0x1400;
    k->revision = 0x00;
    k->class_id = PCI_CLASS_NETWORK_ETHERNET;
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
}

static const TypeInfo fealnx_info = {
    .name          = TYPE_FEALNX,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(FealnxState),
    .class_init    = fealnx_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void fealnx_register_types(void)
{
    type_register_static(&fealnx_info);
}

type_init(fealnx_register_types)
