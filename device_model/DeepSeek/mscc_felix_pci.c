/*
 * QEMU model of VSC9959 (Felix) switch PCI device
 * Based on Linux driver felix_vsc9959.c
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
#include "hw/pci/pci_ids.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/pci/pcie.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-core.h"
#include "migration/vmstate.h"

#define TYPE_PCIBASE_DEVICE "mscc_felix_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VSC9959_VENDOR_ID PCI_VENDOR_ID_FREESCALE
#define VSC9959_DEVICE_ID 0xEEF0
#define VSC9959_CLASS_ID 0x00000000

#define VSC9959_IMDIO_PCI_BAR 0
#define VSC9959_SWITCH_PCI_BAR 4

/* Global base offsets within switch BAR for each target (from resource definitions) */
#define VSC9959_TARGET_SYS_BASE   0x0010000
#define VSC9959_TARGET_REW_BASE   0x0030000
#define VSC9959_TARGET_S0_BASE    0x0040000
#define VSC9959_TARGET_S1_BASE    0x0050000
#define VSC9959_TARGET_S2_BASE    0x0060000
#define VSC9959_TARGET_GCB_BASE   0x0070000
#define VSC9959_TARGET_QS_BASE    0x0080000
#define VSC9959_TARGET_PTP_BASE   0x0090000
#define VSC9959_TARGET_PORT0_BASE 0x0100000
#define VSC9959_TARGET_PORT1_BASE 0x0110000
#define VSC9959_TARGET_PORT2_BASE 0x0120000
#define VSC9959_TARGET_PORT3_BASE 0x0130000
#define VSC9959_TARGET_PORT4_BASE 0x0140000
#define VSC9959_TARGET_PORT5_BASE 0x0150000
#define VSC9959_TARGET_QSYS_BASE  0x0200000
#define VSC9959_TARGET_ANA_BASE   0x0280000

/* Register offsets from regmaps (combined with target base) */
/* GCB target */
#define GCB_SOFT_RST             0x000004
#define GCB_CHIP_ID              0x000000
/* SYS target */
#define SYS_RAM_INIT             0x000f24
#define SYS_RESET_CFG            0x000e00

/* Full offsets */
#define GCB_SOFT_RST_FULL  (VSC9959_TARGET_GCB_BASE + GCB_SOFT_RST)
#define GCB_CHIP_ID_FULL   (VSC9959_TARGET_GCB_BASE + GCB_CHIP_ID)
#define SYS_RAM_INIT_FULL  (VSC9959_TARGET_SYS_BASE + SYS_RAM_INIT)
#define SYS_RESET_CFG_FULL (VSC9959_TARGET_SYS_BASE + SYS_RESET_CFG)

/* Chip ID value for VSC9959 (driver expects this specific value) */
#define VSC9959_CHIP_ID_VALUE 0x09950000

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware Register Shadows - only specific registers we emulate */
    uint32_t gcb_soft_rst;
    uint32_t sys_ram_init;

    uint32_t status;      /* Operational status flags */
    int reset_state;      /* State used to handle reset sequences */
};

/* MMIO read/write handlers for the IMDIO BAR */
static uint64_t imdio_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void imdio_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Nothing to do */
}

static const MemoryRegionOps imdio_mmio_ops = {
    .read = imdio_mmio_read,
    .write = imdio_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

/* MMIO read/write handlers for the Switch BAR */
static uint64_t switch_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case GCB_CHIP_ID_FULL:
        /* Return the chip ID expected by the driver to pass probe */
        return VSC9959_CHIP_ID_VALUE;
    case GCB_SOFT_RST_FULL:
        /* After reset, hardware clears this bit; return 0 to indicate done */
        return 0;
    case SYS_RAM_INIT_FULL:
        return 0;
    case SYS_RESET_CFG_FULL:
        /* Return 0 for now */
        return 0;
    default:
        /* Unimplemented register reads return 0 */
        qemu_log_mask(LOG_UNIMP,
                      "mscc_felix_pci: unimplemented switch read offset=0x%"HWADDR_PRIx"\n",
                      addr);
        return 0;
    }
}

static void switch_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case GCB_CHIP_ID_FULL:
        /* Chip ID is read-only, ignore writes */
        break;
    case GCB_SOFT_RST_FULL:
        /* Writing 1 to SWC_RST triggers reset; we accept but ignore */
        s->gcb_soft_rst = val;
        break;
    case SYS_RAM_INIT_FULL:
        s->sys_ram_init = val;
        /* RAM initialization completes immediately */
        break;
    case SYS_RESET_CFG_FULL:
        /* Accept CORE_ENA write, no actual effect */
        break;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "mscc_felix_pci: unimplemented switch write offset=0x%"HWADDR_PRIx" val=0x%"PRIx64"\n",
                      addr, val);
        break;
    }
}

static const MemoryRegionOps switch_mmio_ops = {
    .read = switch_mmio_read,
    .write = switch_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Clear our shadow state */
    s->gcb_soft_rst = 0;
    s->sys_ram_init = 0;
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;
    Error *local_err = NULL;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  VSC9959_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  VSC9959_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, VSC9959_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* Initialize MSI to allow interrupt allocation in the driver */
    if (msi_init(pdev, 0, 1, true, false, &local_err) < 0) {
        error_propagate(errp, local_err);
        return;
    }
    s->has_msi = true;

    /* BAR0: IMDIO */
    memory_region_init_io(&s->bar_regions[0], OBJECT(s), &imdio_mmio_ops, s,
                          "imdio", 0x1000);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_regions[0]);

    /* BAR4: Switch */
    memory_region_init_io(&s->bar_regions[4], OBJECT(s), &switch_mmio_ops, s,
                          "switch", 0x400000);
    pci_register_bar(pdev, 4, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_regions[4]);

    s->num_bars = 2;
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
    .name = "mscc_felix_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(gcb_soft_rst, PCIBaseState),
        VMSTATE_UINT32(sys_ram_init, PCIBaseState),
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
