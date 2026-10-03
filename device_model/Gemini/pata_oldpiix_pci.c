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

#define TYPE_PCIBASE_DEVICE "pata_oldpiix_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define OLDPIIX_VENDOR_ID PCI_VENDOR_ID_INTEL
#define OLDPIIX_DEVICE_ID 0x1230
#define OLDPIIX_CLASS_ID  PCI_CLASS_STORAGE_IDE

#define OLDPIIX_IDETM_PORT0 0x40
#define OLDPIIX_ENABLE_PORT0 0x41
#define OLDPIIX_IDETM_PORT1 0x42
#define OLDPIIX_ENABLE_PORT1 0x43

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

    /* Hardware Register Shadows (The 'Identity' of the device) */
};

static uint64_t pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0xffffffffffffffffULL;
}

static void pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static const MemoryRegionOps pio_ops = {
    .read = pio_read,
    .write = pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    (void)s;
    pci_device_reset(PCI_DEVICE(dev));

    uint8_t *pci_conf = PCI_DEVICE(dev)->config;
    /* Set enable bits for port 0 and 1 so oldpiix_pre_reset passes */
    pci_conf[OLDPIIX_ENABLE_PORT0] = 0x80;
    pci_conf[OLDPIIX_ENABLE_PORT1] = 0x80;
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  OLDPIIX_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  OLDPIIX_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, OLDPIIX_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Set Prog IF to 0x8f: Master IDE, Native mode for both channels */
    pci_set_byte(pci_conf + PCI_CLASS_PROG, 0x8f);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* Allow driver to write to IDETM_PORT0 and IDETM_PORT1 */
    pci_set_word(pdev->wmask + OLDPIIX_IDETM_PORT0, 0xFFFF);
    pci_set_word(pdev->wmask + OLDPIIX_IDETM_PORT1, 0xFFFF);

    /* BAR Initialization */
    s->num_bars = 5;
    s->bar_info[0] = (BARInfo){0, BAR_TYPE_PIO, 8, "ide-cmd0"};
    s->bar_info[1] = (BARInfo){1, BAR_TYPE_PIO, 4, "ide-ctl0"};
    s->bar_info[2] = (BARInfo){2, BAR_TYPE_PIO, 8, "ide-cmd1"};
    s->bar_info[3] = (BARInfo){3, BAR_TYPE_PIO, 4, "ide-ctl1"};
    s->bar_info[4] = (BARInfo){4, BAR_TYPE_PIO, 16, "bmdma"};

    for (int i = 0; i < s->num_bars; i++) {
        memory_region_init_io(&s->bar_regions[i], OBJECT(s), &pio_ops, s,
                              s->bar_info[i].name, s->bar_info[i].size);
        pci_register_bar(pdev, s->bar_info[i].index, PCI_BASE_ADDRESS_SPACE_IO,
                         &s->bar_regions[i]);
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    (void)s;
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "pata_oldpiix_pci",
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
