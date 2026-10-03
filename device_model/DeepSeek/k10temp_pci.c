/*
 * Integrated QEMU PCI device model for AMD Family 10h/11h/12h/15h/16h/17h/18h/19h/1a 
 * Northbridge Thermal Sensors (k10temp).
 * Device ID: 0x1203 (PCI_DEVICE_ID_AMD_10H_NB_MISC)
 * This device presents only PCI configuration space; no MMIO/PIO BARs.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "qemu/host-utils.h"
#include "qemu/bitops.h"
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

/* Driver-extracted register offsets and hardware identifiers */
#define PCI_VENDOR_ID_AMD 0x1022
#define PCI_DEVICE_ID_AMD_10H_NB_MISC 0x1203
#define DEVICE_ID PCI_DEVICE_ID_AMD_10H_NB_MISC
#define CLASS_ID 0x0600   /* Host bridge (used by northbridge functions) */

#define REG_DCT0_CONFIG_HIGH               0x094
#define DDR3_MODE                          BIT(8)
#define REG_HARDWARE_THERMAL_CONTROL       0x64
#define HTC_ENABLE                         BIT(0)
#define REG_REPORTED_TEMPERATURE           0xa4
#define REG_NORTHBRIDGE_CAPABILITIES       0xe8
#define NB_CAP_HTC                         BIT(10)
#define F15H_M60H_HARDWARE_TEMP_CTRL_OFFSET 0xd8200c64
#define F15H_M60H_REPORTED_TEMP_CTRL_OFFSET 0xd8200ca4
#define ZEN_REPORTED_TEMP_CTRL_BASE         0x00059800
#define ZEN_CCD_TEMP(offset, x)             (ZEN_REPORTED_TEMP_CTRL_BASE + \
                                             (offset) + ((x) * 4))
#define ZEN_CCD_TEMP_VALID                  BIT(11)
#define ZEN_CCD_TEMP_MASK                   GENMASK(10, 0)
#define ZEN_CUR_TEMP_SHIFT                  21
#define ZEN_CUR_TEMP_RANGE_SEL_MASK         BIT(19)
#define ZEN_CUR_TEMP_TJ_SEL_MASK            GENMASK(17, 16)

#define TYPE_PCIBASE_DEVICE "k10temp_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

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

    /* No hardware register shadows needed; this device uses config space only */

    /* No DMA context */

    /* No operational status */

    /* No reset-specific state */

    /* No power management state beyond standard PM CAP */
};

/* Device operates entirely via PCI config space; no IRQ, DMA, or BAR handlers. */

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* No driver-specific reset logic */
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_AMD);
    pci_set_word(pci_conf + PCI_DEVICE_ID, DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* Set initial values for temperature/control registers */
    pci_set_long(pci_conf + REG_HARDWARE_THERMAL_CONTROL, 0x1F);   /* HTC_ENABLE=1, some limits */
    pci_set_long(pci_conf + REG_REPORTED_TEMPERATURE, 0x0);        /* Report 0 temperature */
    pci_set_long(pci_conf + REG_NORTHBRIDGE_CAPABILITIES, NB_CAP_HTC); /* Capable of HTC */

    /* No BARs needed; all data accessed via PCI configuration space */
    s->num_bars = 0;

    /* MSI/MSI-X not used by driver */
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

    /* No additional cleanup needed */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "k10temp_pci",
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
        { INTERFACE_PCIE_DEVICE },
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
