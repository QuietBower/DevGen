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

/* Additional include files retrieved from driver context */
/* HeadFile */

#define TYPE_PCIBASE_DEVICE "bnxt_en_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
/* Related_Config_Info */
#define PCI_VENDOR_ID_BROADCOM 0x14E4
#define DEVICE_ID_BNXT_FIRST   0x1604
#define PCI_CLASS_NETWORK_ETHERNET 0x020000

/* Chip number constants */
#define CHIP_NUM_57301   0x16c8
#define CHIP_NUM_57302   0x16c9
#define CHIP_NUM_57304   0x16ca
#define CHIP_NUM_58700   0x16cd
#define CHIP_NUM_57311   0x16ce
#define CHIP_NUM_57312   0x16cf
#define CHIP_NUM_57402   0x16d0
#define CHIP_NUM_57404   0x16d1
#define CHIP_NUM_57406   0x16d2
#define CHIP_NUM_57407   0x16d5
#define CHIP_NUM_57412   0x16d6
#define CHIP_NUM_57414   0x16d7
#define CHIP_NUM_57416   0x16d8
#define CHIP_NUM_57417   0x16d9
#define CHIP_NUM_57412L  0x16da
#define CHIP_NUM_57414L  0x16db
#define CHIP_NUM_5745X   0xd730
#define CHIP_NUM_57452   0xc452
#define CHIP_NUM_57454   0xc454
#define CHIP_NUM_57508   0x1750
#define CHIP_NUM_57504   0x1751
#define CHIP_NUM_57502   0x1752
#define CHIP_NUM_57608   0x1760
#define CHIP_NUM_58802   0xd802
#define CHIP_NUM_58804   0xd804
#define CHIP_NUM_58808   0xd808

/* Register offsets */
#define BNXT_GRC_REG_CHIP_NUM          0x48
#define BNXT_GRC_REG_BASE              0x260000
#define BNXT_GRCPF_REG_WINDOW_BASE_OUT 0x400
#define BNXT_GRC_REG_STATUS_P5         0x520
#define BNXT_FW_HEALTH_WIN_BASE        0x3000
#define BNXT_FW_HEALTH_WIN_MAP_OFF     8
#define BNXT_PTP_GRC_WIN_BASE          0x6000
#define BNXT_TS_REG_TIMESYNC_TS0_LOWER 0x640180c
#define BNXT_TS_REG_TIMESYNC_TS0_UPPER 0x6401810
#define DB_PF_OFFSET_P5   0x10000
#define DB_VF_OFFSET_P5   0x4000
#define HCOMM_STATUS_SIGNATURE_VAL   0x48435300UL
#define HCOMM_STATUS_VER_LATEST      1
#define BNXT_FW_STATUS_HEALTHY       0x8000
#define BNXT_HCOMM_STATUS_LOC        0x31001F0
#define BNXT_HEALTH_BAR0_OFFSET      0x4000
/* End of Related_Config_Info */

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
    /* Interrupt_Stru */
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    /* Reg_Stru */
    struct {
        uint32_t chip_num;  /* 0x48 */
        uint32_t grc_status; /* 0x520 */
    } regs;

    /* Window base for GRC window at offset 0x3000 */
    uint64_t grc_window_base;

    /* DMA Context - not used in this static phase */
};

/* Other_Addition_Info_Defin: none */

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool active = (s->intr_status & s->intr_mask) != 0;
    if (active) {
        if (msix_enabled(pdev)) {
            msix_notify(pdev, 0);
        } else if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* GRC window reads */
    if (addr >= BNXT_FW_HEALTH_WIN_BASE && addr < (BNXT_FW_HEALTH_WIN_BASE + 0x1000)) {
        hwaddr phys_addr = s->grc_window_base + (addr - BNXT_FW_HEALTH_WIN_BASE);
        if (phys_addr == (BNXT_GRC_REG_BASE + BNXT_GRC_REG_CHIP_NUM)) {
            val = s->regs.chip_num;
        } else {
            /* unknown GRC register - return 0 */
            val = 0;
        }
        return val;
    }

    /* Direct BAR0 register reads */
    switch (addr) {
    case BNXT_GRC_REG_STATUS_P5: /* 0x520 */
        val = s->regs.grc_status;
        break;
    case BNXT_HCOMM_STATUS_LOC:
        val = HCOMM_STATUS_SIGNATURE_VAL | HCOMM_STATUS_VER_LATEST;
        break;
    case BNXT_HCOMM_STATUS_LOC + 4:
        val = (BNXT_HEALTH_BAR0_OFFSET & 0xfffffffc) | 2; /* BAR0, offset 0x4000 */
        break;
    case BNXT_HEALTH_BAR0_OFFSET:
        val = BNXT_FW_STATUS_HEALTHY;
        break;
    default:
        val = 0;
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* GRC window base write */
    if (addr == (BNXT_GRCPF_REG_WINDOW_BASE_OUT + BNXT_FW_HEALTH_WIN_MAP_OFF)) {
        s->grc_window_base = val;
        return;
    }

    /* Other writes are ignored */
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* PIO_Read_Func: not used, stub */
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* PIO_Write_Func: not used, stub */
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Reset_Func: set chip number to match the first PCI entry (BCM5745X) */
    s->regs.chip_num = CHIP_NUM_5745X;
    s->regs.grc_status = 0;
    s->grc_window_base = 0;
    s->intr_status = 0;
    s->intr_mask = 0;
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
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s, bi->name, aligned_size);
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

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x14E4 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x1604 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x020000 );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    /* BAR0: Control registers, 256 MB */
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x10000000;
    s->bar_info[0].name = "bar0";
    /* BAR2: Doorbell, 128 KB */
    s->bar_info[1].index = 2;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = 0x20000;
    s->bar_info[1].name = "bar1";
    /* BAR4: Second control region, 4 KB */
    s->bar_info[2].index = 4;
    s->bar_info[2].type = BAR_TYPE_MMIO;
    s->bar_info[2].size = 0x1000;
    s->bar_info[2].name = "bar2";
    s->num_bars = 3;
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI_OR_MSIX_INIT: initialize MSI-X with 64 vectors */
    msix_init_exclusive_bar(pdev, 64, 0, errp);
    /* DMA_Config_Real: DMA mask set by driver (no QEMU action needed) */
    /* Timer_Config_Real: none */
    /* Field_Init_Real: none */
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

    /* Uninit_Func: free any allocated resources (none yet) */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "bnxt_en_pci",
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

type_init(pcibase_register_types)
