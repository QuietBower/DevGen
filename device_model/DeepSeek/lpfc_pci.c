#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/module.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"

#define TYPE_PCIBASE_DEVICE "lpfc_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register offsets (from driver) - kept for reference */
#define LPFC_SLI_INTF                  0x0058
#define LPFC_SLI_INTF_FAMILY_G6        0xc

/* PCI IDs */
#define PCI_VENDOR_ID_EMULEX           0x10df
#define PCI_DEVICE_ID_VIPER            0xfb00

/* BAR sizes */
#define BAR0_SIZE (256 * 1024)  /* SLIM space */
#define BAR2_SIZE (4 * 1024)    /* control registers */

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar0;
    MemoryRegion bar2;
};

/* BAR0 MMIO handlers */
static uint64_t pcibase_bar0_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* Return 0 for all reads */
    return 0;
}

static void pcibase_bar0_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Accept all writes */
}

static const MemoryRegionOps pcibase_bar0_mmio_ops = {
    .read = pcibase_bar0_mmio_read,
    .write = pcibase_bar0_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl = { .min_access_size = 1, .max_access_size = 4 },
};

/* BAR2 MMIO handlers */
static uint64_t pcibase_bar2_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* Return 0 for all reads */
    return 0;
}

static void pcibase_bar2_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Accept all writes */
}

static const MemoryRegionOps pcibase_bar2_mmio_ops = {
    .read = pcibase_bar2_mmio_read,
    .write = pcibase_bar2_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl = { .min_access_size = 1, .max_access_size = 4 },
};

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_EMULEX);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_VIPER);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0c04); /* Fibre Channel */
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);

    /* Set SLI_INTF to indicate valid SLI-3 device */
    pci_set_long(pci_conf + LPFC_SLI_INTF, 0x0000000B);

    /* Configure interrupt pin (INTx) */
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* BAR0: SLIM memory */
    memory_region_init_io(&s->bar0, OBJECT(s), &pcibase_bar0_mmio_ops, s,
                          "lpfc-bar0", BAR0_SIZE);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar0);

    /* BAR2: control registers */
    memory_region_init_io(&s->bar2, OBJECT(s), &pcibase_bar2_mmio_ops, s,
                          "lpfc-bar2", BAR2_SIZE);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar2);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    /* Nothing to clean up */
}

static void pcibase_reset(DeviceState *dev)
{
    pci_device_reset(PCI_DEVICE(dev));
    /* No hardware state to reset */
}

static const VMStateDescription vmstate_pcibase = {
    .name = "lpfc_pci",
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
    k->exit = pcibase_uninit;
    dc->reset = pcibase_reset;
    dc->vmsd = &vmstate_pcibase;
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
