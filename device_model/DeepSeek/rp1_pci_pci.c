#include "qemu/osdep.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_device.h"
#include "hw/pci/msix.h"
#include "exec/memory.h"

#define TYPE_PCIBASE_DEVICE "pcibase"

typedef struct PCIBaseState {
    PCIDevice pdev;
    MemoryRegion bar0;
} PCIBaseState;

#define PCIBASE_DEVICE(obj) OBJECT_CHECK(PCIBaseState, (obj), TYPE_PCIBASE_DEVICE)
#define RP1_MSIX_VECTORS 1

/* Supplementary defines from driver source */
#define PCI_VENDOR_ID_RPI           0x1de4
#define PCI_DEVICE_ID_RPI_RP1_C0    0x0001

/* Forward declarations */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size) {
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size) {
}

static void pcibase_realize(PCIDevice *pdev, Error **errp) {
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_RPI);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_RPI_RP1_C0);

    memory_region_init_io(&s->bar0, OBJECT(pdev), &pcibase_mmio_ops, s, "rp1-bar0", 0x1000);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar0);

    if (msix_init(pdev, RP1_MSIX_VECTORS,
                  &s->bar0, 0, 0,
                  &s->bar0, 0, 0,
                  0, errp)) {
        return;
    }
}

static void pcibase_uninit(PCIDevice *pdev) {
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    msix_uninit(pdev, &s->bar0, &s->bar0);
}

static void pcibase_class_init(ObjectClass *klass, void *data) {
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    k->realize = pcibase_realize;
    k->uninit = pcibase_uninit;
}

static const TypeInfo pcibase_info = {
    .name = TYPE_PCIBASE_DEVICE,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIBaseState),
    .class_init = pcibase_class_init,
};

static void pcibase_register_types(void) {
    type_register_static(&pcibase_info);
}

type_init(pcibase_register_types);
