#include "qemu/osdep.h"
#include "hw/pci/pci.h"
#include "qapi/error.h"

#define TYPE_SATA_VSC "sata_vsc_pci"
#define SATA_VSC(obj) OBJECT_CHECK(SATA_VSCState, (obj), TYPE_SATA_VSC)

typedef struct SATA_VSCState {
    PCIDevice parent_obj;
    MemoryRegion io;
    uint8_t regs[0x20];
} SATA_VSCState;

static uint64_t sata_vsc_io_read(void *opaque, hwaddr addr, unsigned size)
{
    SATA_VSCState *s = opaque;
    switch (addr) {
    case 0x07:
        return 0x50;
    case 0x0e:
        return 0x50;
    case 0x14:
        return 0x00;
    default:
        return 0x00;
    }
}

static void sata_vsc_io_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    SATA_VSCState *s = opaque;
    if (addr < 0x20) {
        s->regs[addr] = val;
    }
}

static const MemoryRegionOps sata_vsc_io_ops = {
    .read = sata_vsc_io_read,
    .write = sata_vsc_io_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static void sata_vsc_realize(PCIDevice *pci_dev, Error **errp)
{
    SATA_VSCState *s = SATA_VSC(pci_dev);
    pci_config_set_vendor_id(pci_dev->config, 0x1725);
    pci_config_set_device_id(pci_dev->config, 0x7174);
    pci_set_word(pci_dev->config + PCI_CLASS_DEVICE, PCI_CLASS_STORAGE_IDE);
    pci_config_set_interrupt_pin(pci_dev->config, 1);
    memory_region_init_io(&s->io, OBJECT(s), &sata_vsc_io_ops, s, "sata_vsc_io", 0x20);
    pci_register_bar(pci_dev, 5, PCI_BASE_ADDRESS_SPACE_IO, &s->io);
}

static const VMStateDescription vmstate_sata_vsc = {
    .name = "sata_vsc",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, SATA_VSCState),
        VMSTATE_UINT8_ARRAY(regs, SATA_VSCState, 0x20),
        VMSTATE_END_OF_LIST()
    }
};

static void sata_vsc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    k->realize = sata_vsc_realize;
    k->vendor_id = 0x1725;
    k->device_id = 0x7174;
    k->class_id = PCI_CLASS_STORAGE_IDE;
    dc->vmsd = &vmstate_sata_vsc;
}

static const TypeInfo sata_vsc_info = {
    .name = TYPE_SATA_VSC,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(SATA_VSCState),
    .class_init = sata_vsc_class_init,
};

static void sata_vsc_register_types(void)
{
    type_register_static(&sata_vsc_info);
}
type_init(sata_vsc_register_types)
