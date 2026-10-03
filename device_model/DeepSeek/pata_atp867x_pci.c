#include "qemu/osdep.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "migration/vmstate.h"

#define TYPE_PCIBASE_DEVICE "pcibase"
typedef struct PCIBaseState {
    PCIDevice parent_obj;
    MemoryRegion mmio;
} PCIBaseState;
#define PCIBASE_DEVICE(obj) OBJECT_CHECK(PCIBaseState, (obj), TYPE_PCIBASE_DEVICE)

/* Missing PCI vendor/device IDs from supplementary source */
#define PCI_VENDOR_ID_ARTOP    0x1191
#define PCI_DEVICE_ID_ARTOP_ATP867A  0x000A

/* Convenience macros */
#define VENDOR_ID PCI_VENDOR_ID_ARTOP
#define DEVICE_ID PCI_DEVICE_ID_ARTOP_ATP867A

static void pcibase_mmio_read(void *opaque, hwaddr addr, uint64_t *val, unsigned size) {
    *val = 0x0; /* default read; removed unused PCIBaseState *s */
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size) {
    /* removed unused PCIBaseState *s */
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static void pcibase_do_dma(PCIBaseState *s) {
    /* removed unused PCIDevice *pdev = PCI_DEVICE(s); */
}

static void pcibase_realize(PCIDevice *pdev, Error **errp) {
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_set_word(pci_conf + PCI_COMMAND,    0x0000 );
    pci_set_word(pci_conf + PCI_STATUS,     0x0000 );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01 );
    pci_set_byte(pci_conf + PCI_CLASS_PROG,  0x00 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0101 );
    pci_set_byte(pci_conf + PCI_HEADER_TYPE,  PCI_HEADER_TYPE_NORMAL );
    pci_set_byte(pci_conf + PCI_INTERRUPT_PIN, 1 );

    memory_region_init_io(&s->mmio, OBJECT(s), &pcibase_mmio_ops, s,
                          "pcibase-mmio", 0x100);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mmio);

    pcibase_do_dma(s);
}

static void pcibase_uninit(PCIDevice *pdev) {
    /* removed unused PCIBaseState *s = PCIBASE_DEVICE(pdev); */
}

static void pcibase_class_init(ObjectClass *klass, void *data) {
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pcibase_realize;
    k->exit = pcibase_uninit;
    dc->desc = "ATP867A PCI IDE Controller (virtual)";
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
}

static const TypeInfo pcibase_device_info = {
    .name          = TYPE_PCIBASE_DEVICE,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIBaseState),
    .class_init    = pcibase_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void pcibase_register_types(void) {
    type_register_static(&pcibase_device_info);
}

type_init(pcibase_register_types);
