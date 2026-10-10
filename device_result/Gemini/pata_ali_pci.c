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

#define TYPE_PCIBASE_DEVICE "pata_ali_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI_VENDOR_ID_AL        0x10b9
#define PCI_DEVICE_ID_AL_M5228  0x5228

/* ALi M5228 PCI Configuration Registers */
#define ALI_CABLE_DETECT_4A     0x4A
#define ALI_UDMA_CABLE_4B       0x4B
#define ALI_CDROM_FIFO_53       0x53
#define ALI_PIO_FIFO_54         0x54
#define ALI_UDMA_TIMING_56      0x56
#define ALI_CMD_SETUP_58        0x58
#define ALI_CMD_ACTREC_59       0x59
#define ALI_RW_TIMING_5A        0x5A

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
    pci_device_reset(PCI_DEVICE(dev));
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_AL );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_AL_M5228 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_STORAGE_IDE );
    pci_set_byte(pci_conf + PCI_CLASS_PROG, 0x8f); /* Native mode, Bus Master */
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0xC5);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* ALi M5228 specific configuration registers wmask */
    pdev->wmask[ALI_CABLE_DETECT_4A] = 0xFF;
    pdev->wmask[ALI_UDMA_CABLE_4B] = 0xFF;
    pdev->wmask[ALI_CDROM_FIFO_53] = 0xFF;
    
    /* Port 0 and 1 PIO FIFO */
    pdev->wmask[ALI_PIO_FIFO_54] = 0xFF;
    pdev->wmask[ALI_PIO_FIFO_54 + 1] = 0xFF;
    
    /* Port 0 and 1 UDMA Timing */
    pdev->wmask[ALI_UDMA_TIMING_56] = 0xFF;
    pdev->wmask[ALI_UDMA_TIMING_56 + 1] = 0xFF;
    
    /* Port 0 and 1 Command Setup */
    pdev->wmask[ALI_CMD_SETUP_58] = 0xFF;
    pdev->wmask[ALI_CMD_SETUP_58 + 4] = 0xFF;
    
    /* Port 0 and 1 Command Act/Rec */
    pdev->wmask[ALI_CMD_ACTREC_59] = 0xFF;
    pdev->wmask[ALI_CMD_ACTREC_59 + 4] = 0xFF;
    
    /* Port 0 and 1, Dev 0 and 1 R/W Timing */
    pdev->wmask[ALI_RW_TIMING_5A] = 0xFF;
    pdev->wmask[ALI_RW_TIMING_5A + 1] = 0xFF;
    pdev->wmask[ALI_RW_TIMING_5A + 4] = 0xFF;
    pdev->wmask[ALI_RW_TIMING_5A + 5] = 0xFF;

    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 8;
    s->bar_info[0].name = "ide-cmd0";

    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_PIO;
    s->bar_info[1].size = 4;
    s->bar_info[1].name = "ide-ctrl0";

    s->bar_info[2].index = 2;
    s->bar_info[2].type = BAR_TYPE_PIO;
    s->bar_info[2].size = 8;
    s->bar_info[2].name = "ide-cmd1";

    s->bar_info[3].index = 3;
    s->bar_info[3].type = BAR_TYPE_PIO;
    s->bar_info[3].size = 4;
    s->bar_info[3].name = "ide-ctrl1";

    s->bar_info[4].index = 4;
    s->bar_info[4].type = BAR_TYPE_PIO;
    s->bar_info[4].size = 16;
    s->bar_info[4].name = "bmdma";

    s->num_bars = 5;

    for (int i = 0; i < s->num_bars; i++) {
        memory_region_init_io(&s->bar_regions[i], OBJECT(s), &pio_ops, s,
                              s->bar_info[i].name, s->bar_info[i].size);
        pci_register_bar(pdev, s->bar_info[i].index,
                         PCI_BASE_ADDRESS_SPACE_IO, &s->bar_regions[i]);
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "pata_ali_pci",
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
