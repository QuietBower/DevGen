#include "qemu/osdep.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_device.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"
#include "qemu/module.h"

#define TYPE_PCIBASE_DEVICE "cb7210_pci_driver_pci"
#define PCIBASE_DEVICE(obj) \
    OBJECT_CHECK(PCIBaseDeviceState, (obj), TYPE_PCIBASE_DEVICE)

typedef struct PCIBaseDeviceState {
    PCIDevice parent_obj;
    MemoryRegion mmio;
} PCIBaseDeviceState;

static uint64_t pci_base_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pci_base_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static const MemoryRegionOps pci_base_ops = {
    .read = pci_base_read,
    .write = pci_base_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static void pci_base_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseDeviceState *s = PCIBASE_DEVICE(pdev);

    pci_config_set_interrupt_pin(pdev->config, 1);

    memory_region_init_io(&s->mmio, OBJECT(s), &pci_base_ops, s,
                          "pci-base-mmio", 0x1000);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mmio);
}

static void pci_base_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(class);

    k->realize = pci_base_realize;
    k->vendor_id = 0x1307;
    k->device_id = 0x0001;
    k->revision = 0x00;
    k->class_id = PCI_CLASS_OTHERS;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo pci_base_info = {
    .name          = TYPE_PCIBASE_DEVICE,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIBaseDeviceState),
    .class_init    = pci_base_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void pci_base_register_types(void)
{
    type_register_static(&pci_base_info);
}

type_init(pci_base_register_types)
