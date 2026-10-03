#include "qemu/osdep.h"
#include "hw/pci/pci_device.h"
#include "hw/pci/msi.h"
#include "qemu/log.h"

#define TYPE_QLA3XXX_DEVICE "qla3xxx_pci"
#define QLA3XXX_DEVICE(obj) OBJECT_CHECK(QLA3XXXState, (obj), TYPE_QLA3XXX_DEVICE)

#define FM93C56A_READ 0x2

typedef struct {
    PCIDevice pdev;
    MemoryRegion mmio;
} QLA3XXXState;

static void qla3xxx_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size) {
}

static uint64_t qla3xxx_mmio_read(void *opaque, hwaddr addr, unsigned size) {
    return 0;
}

static const MemoryRegionOps qla3xxx_mmio_ops = {
    .read = qla3xxx_mmio_read,
    .write = qla3xxx_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void qla3xxx_realize(PCIDevice *pdev, Error **errp) {
    QLA3XXXState *s = QLA3XXX_DEVICE(pdev);
    memory_region_init_io(&s->mmio, OBJECT(s), &qla3xxx_mmio_ops, s,
                          "qla3xxx-mmio", 0x1000);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mmio);
}

static void qla3xxx_class_init(ObjectClass *klass, void *data) {
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    k->realize = qla3xxx_realize;
    k->vendor_id = 0x1077;
    k->device_id = 0x3010;
    k->revision = 0;
    k->class_id = PCI_CLASS_NETWORK_ETHERNET;
}

static const TypeInfo qla3xxx_info = {
    .name = TYPE_QLA3XXX_DEVICE,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(QLA3XXXState),
    .class_init = qla3xxx_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void qla3xxx_register_types(void) {
    type_register_static(&qla3xxx_info);
}

type_init(qla3xxx_register_types);
