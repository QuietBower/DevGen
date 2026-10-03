/* QEMU model of Intel SCH IDE Controller */

#include "qemu/osdep.h"
#include "hw/pci/pci.h"
#include "hw/pci/pcie.h"
#include "hw/pci/msi.h"
#include "qemu/module.h"

#define PCI_DEVICE_ID_INTEL_SCH_IDE 0x811a

#define TYPE_PATA_SCH "pata_sch_pci"
OBJECT_DECLARE_SIMPLE_TYPE(PATASCHState, PATA_SCH)

struct PATASCHState {
    PCIDevice parent_obj;
    MemoryRegion bar0;  /* Primary command block (I/O) */
    MemoryRegion bar1;  /* Primary control block (I/O) */
    MemoryRegion bar4;  /* Bus master DMA (I/O) */
};

static uint64_t pata_sch_bar0_read(void *opaque, hwaddr addr, unsigned size)
{
    /* ATA primary command block register reads */
    switch (addr) {
    case 7: /* Status register */
        return 0x50; /* RDY (not busy) */
    default:
        return 0;
    }
}

static void pata_sch_bar0_write(void *opaque, hwaddr addr, uint64_t val,
                                unsigned size)
{
    /* ATA primary command block writes - ignore for now */
}

static const MemoryRegionOps pata_sch_bar0_ops = {
    .read = pata_sch_bar0_read,
    .write = pata_sch_bar0_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static uint64_t pata_sch_bar1_read(void *opaque, hwaddr addr, unsigned size)
{
    /* ATA primary control block register reads */
    return 0; /* Alternate status: no interrupt pending */
}

static void pata_sch_bar1_write(void *opaque, hwaddr addr, uint64_t val,
                                unsigned size)
{
    /* ATA primary control block writes - ignore for now */
}

static const MemoryRegionOps pata_sch_bar1_ops = {
    .read = pata_sch_bar1_read,
    .write = pata_sch_bar1_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static uint64_t pata_sch_bar4_read(void *opaque, hwaddr addr, unsigned size)
{
    /* BMDMA registers: base address +0: Bus Master Command/Status,
     * +4: PRD table pointer. We return 0 for both. */
    return 0;
}

static void pata_sch_bar4_write(void *opaque, hwaddr addr, uint64_t val,
                                unsigned size)
{
    /* BMDMA writes - ignore for now */
}

static const MemoryRegionOps pata_sch_bar4_ops = {
    .read = pata_sch_bar4_read,
    .write = pata_sch_bar4_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static void pata_sch_reset(DeviceState *dev)
{
    /* No controller state to reset */
}

static void pata_sch_realize(PCIDevice *pci_dev, Error **errp)
{
    PATASCHState *d = PATA_SCH(pci_dev);
    uint8_t *pci_conf = pci_dev->config;

    /* Set IDE class code with native mode programming interface (0x8a) */
    pci_config_set_class(pci_conf, 0x01018a);

    /* Set interrupt pin to INTA# */
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* BAR0: Primary command block I/O (size 8) */
    memory_region_init_io(&d->bar0, OBJECT(d), &pata_sch_bar0_ops, d,
                          "pata-sch-bar0", 8);
    pci_register_bar(pci_dev, 0, PCI_BASE_ADDRESS_SPACE_IO, &d->bar0);

    /* BAR1: Primary control block I/O (size 4) */
    memory_region_init_io(&d->bar1, OBJECT(d), &pata_sch_bar1_ops, d,
                          "pata-sch-bar1", 4);
    pci_register_bar(pci_dev, 1, PCI_BASE_ADDRESS_SPACE_IO, &d->bar1);

    /* BAR4: Bus master DMA I/O (size 16) */
    memory_region_init_io(&d->bar4, OBJECT(d), &pata_sch_bar4_ops, d,
                          "pata-sch-bar4", 16);
    pci_register_bar(pci_dev, 4, PCI_BASE_ADDRESS_SPACE_IO, &d->bar4);
}

static void pata_sch_exit(PCIDevice *pci_dev)
{
    /* Nothing to clean up */
}

static void pata_sch_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pata_sch_realize;
    k->exit = pata_sch_exit;
    k->vendor_id = 0x8086;
    k->device_id = PCI_DEVICE_ID_INTEL_SCH_IDE;
    k->revision = 0;
    k->class_id = PCI_CLASS_STORAGE_IDE;
    dc->reset = pata_sch_reset;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
}

static const TypeInfo pata_sch_info = {
    .name          = TYPE_PATA_SCH,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PATASCHState),
    .class_init    = pata_sch_class_init,
    .interfaces    = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void pata_sch_register_types(void)
{
    type_register_static(&pata_sch_info);
}
type_init(pata_sch_register_types)