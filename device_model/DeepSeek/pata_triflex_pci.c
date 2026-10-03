/*
 * QEMU model for Compaq Triflex IDE PCI controller.
 * Generated from driver pata_triflex.c.
 */
#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/module.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "sysemu/dma.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/pci/pcie.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"

#define TYPE_PCIBASE_DEVICE "pata_triflex_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI_VENDOR_ID_COMPAQ 0x0e11
#define PCI_DEVICE_ID_COMPAQ_TRIFLEX_IDE 0xae33
#define PCI_CLASS_STORAGE_IDE 0x0101

#define TRIFLEX_CHAN0_TIMING 0x70
#define TRIFLEX_CHAN1_TIMING 0x74
#define TRIFLEX_ENABLE_CONFIG 0x80

struct PCIBaseState {
    PCIDevice parent_obj;
    MemoryRegion bar0_mr;   /* BAR0: primary command */
    MemoryRegion bar1_mr;   /* BAR1: primary control */
    MemoryRegion bar2_mr;   /* BAR2: secondary command */
    MemoryRegion bar3_mr;   /* BAR3: secondary control */
    MemoryRegion bmdma_mr;  /* BAR4: bus master DMA registers */
};

/* Minimal I/O read handler for IDE BARs */
static uint64_t pcibase_io_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

/* Minimal I/O write handler for IDE BARs */
static void pcibase_io_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* No actual DMA or IDE emulation; driver will probe and accept idle device */
}

static const MemoryRegionOps pcibase_io_ops = {
    .read = pcibase_io_read,
    .write = pcibase_io_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_COMPAQ);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_COMPAQ_TRIFLEX_IDE);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_STORAGE_IDE);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Initialize enable bits to allow both ports (0x80 bits 0 and 1) */
    pci_set_byte(pci_conf + TRIFLEX_ENABLE_CONFIG, 0x03);

    /* Register BAR0: primary IDE command block, I/O, size 8 */
    memory_region_init_io(&s->bar0_mr, OBJECT(s), &pcibase_io_ops, s,
                          "triflex-bar0", 8);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_IO, &s->bar0_mr);

    /* Register BAR1: primary IDE control block, I/O, size 4 */
    memory_region_init_io(&s->bar1_mr, OBJECT(s), &pcibase_io_ops, s,
                          "triflex-bar1", 4);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_IO, &s->bar1_mr);

    /* Register BAR2: secondary IDE command block, I/O, size 8 */
    memory_region_init_io(&s->bar2_mr, OBJECT(s), &pcibase_io_ops, s,
                          "triflex-bar2", 8);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_IO, &s->bar2_mr);

    /* Register BAR3: secondary IDE control block, I/O, size 4 */
    memory_region_init_io(&s->bar3_mr, OBJECT(s), &pcibase_io_ops, s,
                          "triflex-bar3", 4);
    pci_register_bar(pdev, 3, PCI_BASE_ADDRESS_SPACE_IO, &s->bar3_mr);

    /* Register BAR4: bus master DMA I/O, size 16 */
    memory_region_init_io(&s->bmdma_mr, OBJECT(s), &pcibase_io_ops, s,
                          "triflex-bmdma", 16);
    pci_register_bar(pdev, 4, PCI_BASE_ADDRESS_SPACE_IO, &s->bmdma_mr);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    /* Nothing extra to clean up */
}

static void pcibase_reset(DeviceState *dev)
{
    pci_device_reset(PCI_DEVICE(dev));
}

static const VMStateDescription vmstate_pcibase = {
    .name = "pata_triflex_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
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
