#include "qemu/osdep.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_device.h"
#include "qapi/error.h"
#include "qemu/module.h"

#define TYPE_PCIBASE_DEVICE "jme_pci"
OBJECT_DECLARE_SIMPLE_TYPE(JMEState, PCIBASE_DEVICE)

struct JMEState {
    PCIDevice parent_obj;
    MemoryRegion mmio;
};

static uint64_t jme_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void jme_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static const MemoryRegionOps jme_mmio_ops = {
    .read = jme_mmio_read,
    .write = jme_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static void jme_pci_realize(PCIDevice *pdev, Error **errp)
{
    JMEState *s = PCIBASE_DEVICE(pdev);

    memory_region_init_io(&s->mmio, OBJECT(s), &jme_mmio_ops, s,
                          "jme-mmio", 0x4000);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mmio);
}

static void jme_pci_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(class);

    k->realize = jme_pci_realize;
    k->vendor_id = 0x197b;
    k->device_id = 0x0250;
    k->class_id = PCI_CLASS_NETWORK_ETHERNET;
    k->revision = 0x00;
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
}

static const TypeInfo jme_pci_info = {
    .name          = TYPE_PCIBASE_DEVICE,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(JMEState),
    .class_init    = jme_pci_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_PCIE_DEVICE },
        { }
    },
};

static void jme_pci_register_types(void)
{
    type_register_static(&jme_pci_info);
}

type_init(jme_pci_register_types)
