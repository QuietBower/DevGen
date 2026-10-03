#include "qemu/osdep.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/qdev-properties.h"
#include "hw/registerfields.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "sysemu/dma.h"
#include "migration/vmstate.h"

#define TYPE_PCIBASE_DEVICE "pcibase"
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI Vendor & Device IDs - first entry from driver */
#define PCI_VENDOR_ID_ATH10K              0x168c
#define PCI_DEVICE_ID_ATH10K_QCA6174      0x003e

/* Register base addresses (example values for QCA6174) */
#define SOC_CORE_BASE_ADDRESS             0x00080000
#define RTC_SOC_BASE_ADDRESS              0x00040000
#define CE_WRAPPER_BASE_ADDRESS           0x00057000
#define FW_INDICATOR_ADDRESS              0x0003a010

/* Register offsets within blocks */
#define CORE_CTRL_ADDRESS                 0x00000000
#define SOC_CHIP_ID_ADDRESS               0x000000A0
#define SOC_RESET_CONTROL_ADDRESS         0x00000004
#define PCIE_BAR_REG_ADDRESS              0x40030

/* BAR indices */
#define BAR0   0

/* MMIO region size for BAR0 */
#define BAR0_SIZE   0x100000  /* 1MB */

struct PCIBaseState {
    PCIDevice parent_obj;
    MemoryRegion mmio;
    MemoryRegion bar0;
};

static uint64_t pcibase_mmio_read(void *opaque, hwaddr offset, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    (void)s;

    switch (offset) {
    case SOC_CORE_BASE_ADDRESS + CORE_CTRL_ADDRESS:
        val = 0x00000001;
        break;
    case RTC_SOC_BASE_ADDRESS + SOC_CHIP_ID_ADDRESS:
        val = 0x003e0000;
        break;
    case RTC_SOC_BASE_ADDRESS + SOC_RESET_CONTROL_ADDRESS:
        val = 0x00000000;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "pcibase: MMIO read offset 0x%" PRIx64 "\n", offset);
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr offset, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;

    switch (offset) {
    case SOC_CORE_BASE_ADDRESS + CORE_CTRL_ADDRESS:
        break;
    case RTC_SOC_BASE_ADDRESS + SOC_RESET_CONTROL_ADDRESS:
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "pcibase: MMIO write offset 0x%" PRIx64 " val 0x%" PRIx64 "\n", offset, val);
        break;
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    pci_set_byte(pdev->config + PCI_INTERRUPT_PIN, 1);

    memory_region_init_io(&s->mmio, OBJECT(s), &pcibase_mmio_ops, s,
                          "pcibase-mmio", BAR0_SIZE);
    pci_register_bar(pdev, BAR0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mmio);

    /* No MSI/MSI-X support per driver */
}

static void pcibase_exit(PCIDevice *pdev)
{
    (void)pdev;
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    (void)s;
}

static const VMStateDescription vmstate_pcibase = {
    .name = "pcibase",
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
    k->exit = pcibase_exit;
    k->vendor_id = PCI_VENDOR_ID_ATH10K;
    k->device_id = PCI_DEVICE_ID_ATH10K_QCA6174;
    k->revision = 0;
    k->class_id = PCI_CLASS_NETWORK_OTHER;
    dc->reset = pcibase_reset;
    dc->vmsd = &vmstate_pcibase;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo pcibase_info = {
    .name = TYPE_PCIBASE_DEVICE,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIBaseState),
    .class_init = pcibase_class_init,
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
