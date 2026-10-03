#include "qemu/osdep.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_device.h"
#include "qemu/log.h"

#define TYPE_PCIBASE_DEVICE "pcibase"
#define PCIBASE_DEVICE(obj) OBJECT_CHECK(PCIBaseState, (obj), TYPE_PCIBASE_DEVICE)

#define PCI_VENDOR_ID_CREATIVE           0x1102
#define PCI_DEVICE_ID_CREATIVE_20K1      0x0005

typedef struct PCIBaseState {
    PCIDevice pdev;
    MemoryRegion mmio;
} PCIBaseState;

static void pcibase_update_irq(PCIBaseState *s)
{
    /* Placeholder */
}

static void pcibase_do_dma(PCIBaseState *s)
{
    /* Placeholder */
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* Placeholder */
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Placeholder */
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* Placeholder */
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Placeholder */
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void pcibase_reset(DeviceState *dev)
{
    /* Placeholder */
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_CREATIVE);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_CREATIVE_20K1);
    pci_set_word(pci_conf + PCI_COMMAND, PCI_COMMAND_IO | PCI_COMMAND_MEMORY);
    pci_set_long(pci_conf + PCI_BASE_ADDRESS_0, 0x00000000);
    memory_region_init_io(&s->mmio, OBJECT(s), &pcibase_mmio_ops, s,
                          "pcibase-mmio", 0x1000);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mmio);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    /* Placeholder */
}

static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pcibase_realize;
    k->exit = pcibase_uninit;
    k->vendor_id = PCI_VENDOR_ID_CREATIVE;
    k->device_id = PCI_DEVICE_ID_CREATIVE_20K1;
    k->revision = 0;
    k->class_id = PCI_CLASS_MULTIMEDIA_AUDIO;
    dc->reset = pcibase_reset;
}

static const TypeInfo pcibase_info = {
    .name          = TYPE_PCIBASE_DEVICE,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIBaseState),
    .class_init    = pcibase_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void pcibase_register_types(void)
{
    type_register_static(&pcibase_info);
}

type_init(pcibase_register_types)
