/* QEMU PCI device model for Mellanox Spectrum switch - Phase 3 compile fix */

#include "qemu/osdep.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_device.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "qapi/error.h"

/* Missing Mellanox PCI IDs from driver source */
#define PCI_VENDOR_ID_MELLANOX          0x15b3
#define PCI_DEVICE_ID_MELLANOX_SPECTRUM 0xcb84

/* Device type and macro from template, must not be changed */
#define TYPE_PCIBASE_DEVICE "pcibase"
#define PCIBASE_DEVICE(obj) OBJECT_CHECK(PCIBaseState, (obj), TYPE_PCIBASE_DEVICE)

/* Vendor/device ID aliases used in realize */
#define VENDOR_ID  PCI_VENDOR_ID_MELLANOX
#define DEVICE_ID  PCI_DEVICE_ID_MELLANOX_SPECTRUM

typedef struct PCIBaseState {
    PCIDevice pdev;
    MemoryRegion bar0;
} PCIBaseState;

typedef struct PCIBaseClass {
    PCIDeviceClass parent_class;
} PCIBaseClass;

#define DECLARE_PCIBASE_DEVICE(obj) \
    OBJECT_DECLARE_SIMPLE_TYPE(PCIBASEState, PCIBASE_DEVICE, TYPE_PCIBASE_DEVICE)

/* MMIO read handler */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    qemu_log_mask(LOG_GUEST_ERROR, "%s: unimplemented mmio read at offset 0x%" HWADDR_PRIx "\n", __func__, addr);
    return 0;
}

/* MMIO write handler */
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    qemu_log_mask(LOG_GUEST_ERROR, "%s: unimplemented mmio write at offset 0x%" HWADDR_PRIx " value 0x%" PRIx64 "\n", __func__, addr, val);
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

/* Reset callback */
static void pcibase_reset(DeviceState *dev)
{
    qemu_log_mask(LOG_GUEST_ERROR, "%s: reset\n", __func__);
}

/* Device realize */
static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID);
    pci_set_word(pci_conf + PCI_COMMAND, PCI_COMMAND_IO | PCI_COMMAND_MEMORY);
    pci_set_word(pci_conf + PCI_STATUS, PCI_STATUS_CAP_LIST);

    /* BAR0: 1MB MMIO region */
    memory_region_init_io(&s->bar0, OBJECT(s), &pcibase_mmio_ops, s,
                          "pcibase-bar0", 0x100000);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar0);
}

/* Uninit callback */
static void pcibase_uninit(PCIDevice *pdev)
{
}

/* Class initialization */
static void pcibase_class_init(ObjectClass *klass, void *data)
{
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    k->realize = pcibase_realize;
    k->exit = pcibase_uninit;
    dc->reset = pcibase_reset;
}

/* Type information */
static const TypeInfo pcibase_info = {
    .name          = TYPE_PCIBASE_DEVICE,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIBaseState),
    .class_init    = pcibase_class_init,
    .class_size    = sizeof(PCIBaseClass),
};

static void pcibase_register_types(void)
{
    type_register_static(&pcibase_info);
}

type_init(pcibase_register_types);
