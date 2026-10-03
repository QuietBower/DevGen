/*
 * This template provides a robust skeleton for hardware emulation.
 * Integrated QEMU PCI device template (QEMU 8.2.10).
 * Replace #PLACEHOLDER# blocks with driver-specific definitions.
 * Designed for register-level modeling and PCIe driver probing.
 * NOTE: Do not directly fill this file with the Linux kernel source code.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "qemu/host-utils.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "sysemu/dma.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/pci/pcie.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-core.h"
#include "migration/vmstate.h"

#define TYPE_PCIBASE_DEVICE "cdns3_pci_usbss_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
/* PCI Vendor and Device IDs from cdns3_pci_ids table: first entry */
#define CDNS3_PCI_VENDOR_ID     0x17cd
#define CDNS3_PCI_DEVICE_ID     0x0100
/* PCI class: USB3 Controller */
#define CDNS3_PCI_CLASS_ID      0x0c0330

/* BAR indices and sizes (from driver: PCI_BAR_HOST=0, PCI_BAR_DEV=2, PCI_BAR_OTG=0) */
#define PCI_BAR_DEV             2
#define PCI_BAR_HOST            0
#define PCI_BAR_OTG             0
#define PCI_DEV_FN_HOST_DEVICE  0
#define PCI_DEV_FN_OTG          1

#define CDNS3_BAR0_SIZE         0x1000
#define CDNS3_BAR2_SIZE         0x1000

typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM 
} BARType;

typedef struct {
    int     index;    /* BAR index 0-5 */
    BARType type;
    hwaddr  size;
    const char *name;
} BARInfo;

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;
};

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }
    
    /* CRITICAL: PCI requires BAR sizes to be a power of 2. Prevent QEMU assert crash. */
    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;
    int fn = PCI_FUNC(pdev->devfn);

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  CDNS3_PCI_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  CDNS3_PCI_DEVICE_ID);
    /* Set USB3 Controller class code: base 0x0C, sub 0x03, prog-if 0x30 */
    pci_set_byte(pci_conf + PCI_CLASS_PROG, 0x30);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0C03);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization based on function number */
    if (fn == 0) {
        /* Function 0: host (BAR0) + device (BAR2) */
        s->num_bars = 2;
        s->bar_info[0].index = PCI_BAR_HOST;
        s->bar_info[0].type = BAR_TYPE_MMIO;
        s->bar_info[0].size = CDNS3_BAR0_SIZE;
        s->bar_info[0].name = "bar0";
        s->bar_info[1].index = PCI_BAR_DEV;
        s->bar_info[1].type = BAR_TYPE_MMIO;
        s->bar_info[1].size = CDNS3_BAR2_SIZE;
        s->bar_info[1].name = "bar2";
    } else if (fn == 1) {
        /* Function 1: OTG (BAR0 only) */
        s->num_bars = 1;
        s->bar_info[0].index = PCI_BAR_OTG;
        s->bar_info[0].type = BAR_TYPE_MMIO;
        s->bar_info[0].size = CDNS3_BAR0_SIZE;
        s->bar_info[0].name = "bar0";
    } else {
        error_setg(errp, "Invalid function number %d", fn);
        return;
    }

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Create second function if this is function 0 (multifunction device) */
    if (fn == 0) {
        /* Set multifunction bit in header type (already set by property, but ensure) */
        pci_set_byte(pdev->config + PCI_HEADER_TYPE,
                     pci_get_byte(pdev->config + PCI_HEADER_TYPE) | PCI_HEADER_TYPE_MULTI_FUNCTION);

        /* Create and realize the OTG function (fn=1) on the same slot */
        PCIDevice *dev1 = PCI_DEVICE(object_new(TYPE_PCIBASE_DEVICE));
        qdev_prop_set_int32(DEVICE(dev1), "addr", PCI_DEVFN(PCI_SLOT(pdev->devfn), 1));
        if (!qdev_realize(DEVICE(dev1), BUS(pci_get_bus(pdev)), &error_abort)) {
            error_setg(errp, "Failed to realize secondary function");
            return;
        }
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "cdns3_pci_usbss_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_END_OF_LIST()
    }
};

static void pcibase_instance_init(Object *obj)
{
    /* Ensure the device is recognized as multifunction so second function is visible */
    object_property_set_bool(obj, "multifunction", true, &error_abort);
}

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
        { INTERFACE_PCIE_DEVICE },
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    };

    static const TypeInfo pcibase_info = {
        .name = TYPE_PCIBASE_DEVICE,
        .parent = TYPE_PCI_DEVICE,
        .instance_size = sizeof(PCIBaseState),
        .instance_init = pcibase_instance_init,
        .class_init = pcibase_class_init,
        .interfaces = interfaces,
    };

    type_register_static(&pcibase_info);
}

type_init(pcibase_register_types);
