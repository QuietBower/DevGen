/* Minimal QEMU PCI device model for SiS 300/305 framebuffer */
#include "qemu/osdep.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_device.h"
#include "exec/memory.h"
#include "qemu/module.h"

#define PCI_VENDOR_ID_SI    0x1039
#define PCI_DEVICE_ID_SI_300 0x0300

#define TYPE_PCIBASE_DEVICE "sisfb"
#define PCIBASE_DEVICE(obj) OBJECT_CHECK(PCIBaseState, (obj), TYPE_PCIBASE_DEVICE)

typedef struct {
    PCIDevice pdev;
    MemoryRegion mmio;
    /* Add other state as needed */
} PCIBaseState;

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_SI);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_SI_300);
    /* Additional initialization would go here */
}

static void pcibase_uninit(PCIDevice *pdev)
{
    /* Cleanup: removed unused variable s */
}

static void pcibase_class_init(ObjectClass *klass, void *data)
{
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    k->realize = pcibase_realize;
    k->exit = pcibase_uninit;
    k->vendor_id = PCI_VENDOR_ID_SI;
    k->device_id = PCI_DEVICE_ID_SI_300;
    k->class_id = PCI_CLASS_DISPLAY_OTHER;
    k->revision = 0;
    k->is_express = false;
    k->is_bridge = false;
}

static const TypeInfo pcibase_info = {
    .name = TYPE_PCIBASE_DEVICE,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIBaseState),
    .class_init = pcibase_class_init,
};

static void pcibase_register_types(void)
{
    type_register_static(&pcibase_info);
}
type_init(pcibase_register_types)
