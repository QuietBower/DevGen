#include "qemu/osdep.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "qemu/module.h"

#define TYPE_PCIBASE_DEVICE "s3fb"
#define PCIBASE_DEVICE(obj) OBJECT_CHECK(PCIBaseState, (obj), TYPE_PCIBASE_DEVICE)

#define PCI_VENDOR_ID_S3 0x5333
#define PCI_DEVICE_ID_S3_TRIO64V2 0x8811

typedef struct PCIBaseState {
    PCIDevice pdev;
    MemoryRegion bar0;
    MemoryRegion bar1;
} PCIBaseState;

static void pcibase_update_irq(PCIDevice *pdev)
{
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_S3);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_S3_TRIO64V2);

    memory_region_init_ram(&s->bar0, OBJECT(s), "s3fb-fb", 16 * 1024 * 1024, errp);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar0);

    memory_region_init_io(&s->bar1, OBJECT(s), &pcibase_mmio_ops, s, "s3fb-mmio", 0x1000);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar1);
}

static void pcibase_uninit(PCIDevice *pdev)
{
}

static void pcibase_class_init(ObjectClass *klass, void *data)
{
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    k->realize = pcibase_realize;
    k->exit = pcibase_uninit;
    k->vendor_id = PCI_VENDOR_ID_S3;
    k->device_id = PCI_DEVICE_ID_S3_TRIO64V2;
    k->revision = 0;
    k->class_id = PCI_CLASS_DISPLAY_VGA;
}

static const TypeInfo pcibase_info = {
    .name          = TYPE_PCIBASE_DEVICE,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIBaseState),
    .class_init    = pcibase_class_init,
};

static void pcibase_register_types(void)
{
    type_register_static(&pcibase_info);
}

type_init(pcibase_register_types);
