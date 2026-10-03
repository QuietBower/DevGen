#include "qemu/osdep.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_device.h"
#include "qom/object.h"

#define TYPE_PCIBASE_DEVICE "tifm_7xx1_pci"
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI_DEVICE_ID_TI_XX21_XX11_FM 0x8033
#define PCI_VENDOR_ID_TI 0x104c

/* Register offsets: combined to avoid duplicate case values */
#define FM_INTERRUPT_STATUS 0x00  /* all three registers map to same offset */

typedef struct PCIBaseState {
    PCIDevice pdev;
    MemoryRegion mmio;
    uint32_t intr_status;
} PCIBaseState;

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    switch (addr) {
    case FM_INTERRUPT_STATUS:
        return s->intr_status;
    default:
        return 0;
    }
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    switch (addr) {
    case FM_INTERRUPT_STATUS:
        /* write behavior: any write clears intr_status */
        s->intr_status = 0;
        break;
    default:
        break;
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_TI);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_TI_XX21_XX11_FM);
    memory_region_init_io(&s->mmio, OBJECT(s), &pcibase_mmio_ops, s, "tifm-mmio", 0x1000);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mmio);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    /* nothing to do */
}

static void pcibase_class_init(ObjectClass *klass, void *data)
{
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    k->realize = pcibase_realize;
    k->exit = pcibase_uninit;
    k->vendor_id = PCI_VENDOR_ID_TI;
    k->device_id = PCI_DEVICE_ID_TI_XX21_XX11_FM;
    k->class_id = PCI_CLASS_OTHERS;  /* added to fix assertion */
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
