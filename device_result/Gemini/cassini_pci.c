#include "qemu/osdep.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_device.h"
#include "hw/qdev-properties.h"
#include "qom/object.h"
#include "qemu/module.h"

#define TYPE_CASSINI_PCI "cassini_pci"
OBJECT_DECLARE_SIMPLE_TYPE(CassiniState, CASSINI_PCI)

#define COMP_RING_I_TO_S(x)  (128*(1 << (x)))
#define RX_COMP_RING_INDEX 4
#define RX_COMP_RING_SIZE COMP_RING_I_TO_S(RX_COMP_RING_INDEX)

#define DESC_RING_I_TO_S(x)  (32*(1 << (x)))
#define RX_DESC_RING_INDEX 4
#define RX_DESC_RING_SIZE DESC_RING_I_TO_S(RX_DESC_RING_INDEX)

#define TX_DESC_RING_INDEX 4
#define TX_DESC_RING_SIZE DESC_RING_I_TO_S(TX_DESC_RING_INDEX)

struct CassiniState {
    PCIDevice parent_obj;
    MemoryRegion mmio;
};

static uint64_t cassini_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void cassini_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static const MemoryRegionOps cassini_mmio_ops = {
    .read = cassini_mmio_read,
    .write = cassini_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void cassini_pci_realize(PCIDevice *pdev, Error **errp)
{
    CassiniState *s = CASSINI_PCI(pdev);

    memory_region_init_io(&s->mmio, OBJECT(s), &cassini_mmio_ops, s,
                          "cassini-mmio", 0x100000);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mmio);
}

static void cassini_pci_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(class);

    k->realize = cassini_pci_realize;
    k->vendor_id = 0x100b;
    k->device_id = 0x0035;
    k->revision = 0x00;
    k->class_id = PCI_CLASS_NETWORK_ETHERNET;
    dc->desc = "Sun Cassini Gigabit Ethernet";
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
}

static const TypeInfo cassini_pci_info = {
    .name          = TYPE_CASSINI_PCI,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(CassiniState),
    .class_init    = cassini_pci_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void cassini_pci_register_types(void)
{
    type_register_static(&cassini_pci_info);
}

type_init(cassini_pci_register_types)
