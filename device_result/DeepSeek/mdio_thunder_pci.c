/*
 * QEMU PCI Device Model for MDIO Thunder interface
 * Based on driver: mdio-thunder.c
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

#define TYPE_PCIBASE_DEVICE "mdio_thunder_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_CAVIUM 0x177d   /* from pci_ids.h */
#define THUNDER_MDIO_DEVICE_ID 0xa02b
/* CLASS_ID not found in driver, placeholder */
#define THUNDER_MDIO_CLASS_ID 0x000000

/* Register offsets for one bus (relative to bus base) */
#define SMI_CMD     0x0
#define SMI_WR_DAT  0x8
#define SMI_RD_DAT  0x10
#define SMI_CLK     0x18
#define SMI_EN      0x20

/* Number of MDIO buses */
#define MAX_BUSES   4

/* Macro to define bitfield structures */
#define OCT_MDIO_BITFIELD_FIELD(type, name, width) type name : width;

/* Hardware register definitions from driver (converted to stdint types) */
typedef union {
    uint64_t u64;
    struct {
        uint64_t reserved_1_63 : 63;
        uint64_t en : 1;
    } s;
} cvmx_smix_en_t;

typedef union {
    uint64_t u64;
    struct {
        uint64_t reserved_18_63 : 46;
        uint64_t phy_op : 2;
        uint64_t reserved_13_15 : 3;
        uint64_t phy_adr : 5;
        uint64_t reserved_5_7 : 3;
        uint64_t reg_adr : 5;
    } s;
} cvmx_smix_cmd_t;

typedef union {
    uint64_t u64;
    struct {
        uint64_t reserved_18_63 : 46;
        uint64_t pending : 1;
        uint64_t val : 1;
        uint64_t dat : 16;
    } s;
} cvmx_smix_wr_dat_t;

typedef union {
    uint64_t u64;
    struct {
        uint64_t reserved_18_63 : 46;
        uint64_t pending : 1;
        uint64_t val : 1;
        uint64_t dat : 16;
    } s;
} cvmx_smix_rd_dat_t;

typedef union {
    uint64_t u64;
    struct {
        uint64_t reserved_25_63 : 39;
        uint64_t mode : 1;
        uint64_t reserved_21_23 : 3;
        uint64_t sample_hi : 5;
        uint64_t sample_mode : 1;
        uint64_t reserved_14_14 : 1;
        uint64_t clk_idle : 1;
        uint64_t preamble : 1;
        uint64_t sample : 4;
        uint64_t phase : 8;
    } s;
} cvmx_smix_clk_t;

/* Per-bus register state */
typedef struct {
    cvmx_smix_cmd_t cmd;
    cvmx_smix_wr_dat_t wr_dat;
    cvmx_smix_rd_dat_t rd_dat;
    cvmx_smix_clk_t clk;
    cvmx_smix_en_t en;
} ThunderMdioBusRegs;

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

/* BAR0 size - set to a reasonable value to allow bus sub-regions */
#define BAR0_SIZE 0x100000

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;

    /* Hardware Register Shadows */
    ThunderMdioBusRegs bus_regs[MAX_BUSES];
    /* Offsets within BAR0 for each bus's register base: not provided, needed */
    hwaddr bus_base_offsets[MAX_BUSES];

    /* Flat register file for MMIO accesses */
    uint8_t *regs;
};

/* MMIO Read Handler - returns stored value from flat register file */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr + size > s->bar_info[0].size) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: read out of bounds at addr=0x%"PRIx64" size=%u\n",
                      __func__, addr, size);
        return 0;
    }
    memcpy(&val, s->regs + addr, size);
    return val;
}

/* MMIO Write Handler - stores value to flat register file */
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr + size > s->bar_info[0].size) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: write out of bounds at addr=0x%"PRIx64" size=%u val=0x%"PRIx64"\n",
                      __func__, addr, size, val);
        return;
    }
    memcpy(s->regs + addr, &val, size);
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
    /* Reset register file to zero */
    if (s->regs) {
        memset(s->regs, 0, s->bar_info[0].size);
    }
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
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_CAVIUM );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  THUNDER_MDIO_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, THUNDER_MDIO_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* Allocate flat register file */
    s->regs = g_new0(uint8_t, BAR0_SIZE);

    /* BAR0 Initialization */
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo) {
        .index = 0,
        .type = BAR_TYPE_MMIO,
        .size = BAR0_SIZE,
        .name = "bar0"
    };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
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
    g_free(s->regs);
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "mdio_thunder_pci",
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
