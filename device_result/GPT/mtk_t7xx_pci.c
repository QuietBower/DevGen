/*
 * QEMU PCI device model skeleton for MediaTek T7XX WWAN device
 * Phase 4: Debug & Update – fixed BAR layout for IREG/EREG and EREG size
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
#include "hw/pci/msi.h"

#define TYPE_PCIBASE_DEVICE "mtk_t7xx_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Static definitions from supplementary driver source */
#define PCI_VENDOR_ID_MEDIATEK        0x14c3
#define PCI_CLASS_OTHERS              0xff
#define BIT(nr)                       (1UL << (nr))

/* Simplified GENMASK helpers without compile-time assertions */
#define GENMASK(h, l)                 (((~0UL) - ((1UL << (l)) - 1)) & (~0UL >> (BITS_PER_LONG - 1 - (h))))
#define GENMASK_ULL(h, l)             (((~0ULL) - ((1ULL << (l)) - 1)) & (~0ULL >> (BITS_PER_LONG_LONG - 1 - (h))))

/* Register Layout and Hardware Identifiers extracted from driver source */
#define MTK_T7XX_VENDOR_ID        PCI_VENDOR_ID_MEDIATEK
#define MTK_T7XX_DEVICE_ID        0x4d75
#define MTK_T7XX_CLASS_ID         PCI_CLASS_OTHERS

#define T7XX_PCI_IREG_BASE                0
#define T7XX_PCI_EREG_BASE                2
#define T7XX_PCIE_MISC_CTRL               0x0348
#define T7XX_PCIE_MISC_MAC_SLEEP_DIS      BIT(7)
#define T7XX_PCIE_RESOURCE_STATUS         0x0d28
#define T7XX_PCIE_RESOURCE_STS_MSK        GENMASK(4, 0)
#define DISABLE_ASPM_LOWPWR               0x0e50
#define ENABLE_ASPM_LOWPWR                0x0e54
#define IMASK_HOST_MSIX_CLR_GRP0_0        0x3080
#define MSIX_MSK_SET_ALL                  GENMASK(31, 24)
#define IMASK_HOST_MSIX_SET_GRP0_0        0x3000
#define ISTAT_HST_CTRL                    0x01ac
#define ISTAT_HST_CTRL_DIS                BIT(0)
#define MSIX_ISTAT_HST_GRP0_0             0x0f00
#define T7XX_PCIE_PM_RESUME_STATE         0x0d0c
#define T7XX_PCIE_MISC_DEV_STATUS         0x0d1c
#define HOST_EVENT_MASK                   GENMASK(31, 28)
#define T7XX_PCIE_CFG_MSIX                0x03ec
#define T7XX_PCIE_REG_SIZE_CHIP           0x00400000
#define T7XX_PCIE_REG_TRSL_ADDR_CHIP      0x10000000
#define T7XX_PCIE_REG_BAR                 2
#define T7XX_PCIE_REG_PORT                ATR_SRC_PCI_WIN0
#define T7XX_PCIE_REG_TRSL_PORT           ATR_DST_AXIM_0
#define T7XX_PCIE_REG_TABLE_NUM           0
#define T7XX_PCIE_DEV_DMA_PORT_START      ATR_SRC_AXIS_0
#define T7XX_PCIE_DEV_DMA_PORT_END        ATR_SRC_AXIS_2
#define T7XX_PCIE_DEV_DMA_TABLE_NUM       0
#define T7XX_PCIE_DEV_DMA_TRANSPARENT     1
#define T7XX_PCIE_DEV_DMA_SRC_ADDR        0
#define T7XX_PCIE_DEV_DMA_TRSL_ADDR       0
#define T7XX_PCIE_DEV_DMA_SIZE            0
#define ATR_PCIE_WIN0_T0_ATR_PARAM_SRC_ADDR   0x0600
#define ATR_PCIE_WIN0_T0_TRSL_ADDR            0x0608
#define ATR_PCIE_WIN0_T0_TRSL_PARAM           0x0610
#define ATR_PCIE_WIN0_ADDR_ALGMT              GENMASK_ULL(63, 12)
#define ATR_TRANSPARENT_SIZE                  0x3f
#define ATR_PORT_OFFSET                       0x100
#define ATR_TABLE_NUM_PER_ATR                 8
#define ATR_TABLE_OFFSET                      0x20
#define ATR_SRC_ADDR_INVALID                  0x007f
#define MHCCIF_RC_DEV_BASE                0x10024000
#define INFRACFG_AO_DEV_CHIP              0x10001000
#define TOPRGU_CH_PCIE_IRQ_STA            0x1000790c
#define CLDMA1_AO_BASE                    0x1004b000
#define CLDMA1_PD_BASE                    0x1021f000
#define CLDMA0_AO_BASE                    0x10049000
#define CLDMA0_PD_BASE                    0x1021d000
#define DPMAIF_PD_BASE                    0x1022d000
#define DPMAIF_AO_BASE                    0x10014000
#define DPMAIF_AO_BASE_DL                 (DPMAIF_AO_BASE + 0x400)
#define DPMAIF_PD_SRAM_DL_BASE            (DPMAIF_PD_BASE + 0xc00)
#define DPMAIF_PD_SRAM_UL_BASE            (DPMAIF_PD_BASE + 0xd00)
#define DPMAIF_AP_MISC_BASE               (DPMAIF_PD_BASE + 0x400)
#define DPMAIF_MMW_HPC_BASE               (DPMAIF_PD_BASE + 0x600)

enum t7xx_atr_src_port {
    ATR_SRC_PCI_WIN0,
    ATR_SRC_PCI_WIN1,
    ATR_SRC_AXIS_0,
    ATR_SRC_AXIS_1,
    ATR_SRC_AXIS_2,
    ATR_SRC_AXIS_3,
};

enum t7xx_atr_dst_port {
    ATR_DST_PCI_TRX,
    ATR_DST_PCI_CONFIG,
    ATR_DST_AXIM_0 = 4,
    ATR_DST_AXIM_1,
    ATR_DST_AXIM_2,
    ATR_DST_AXIM_3,
};

/* Simple fixed definitions that driver uses but values are not important for probe */
#define EXT_INT_START 0
#define EXT_INT_NUM   8

/* PM resume state values are only compared, exact numbers don't matter as long as consistent */
#define PM_RESUME_REG_STATE_INIT   0
#define PM_RESUME_REG_STATE_L1     1
#define PM_RESUME_REG_STATE_L2     2
#define PM_RESUME_REG_STATE_L3     3
#define PM_RESUME_REG_STATE_EXP    4
#define PM_RESUME_REG_STATE_L2_EXP 5

/* Interrupt index placeholders used in driver */
#define MHCCIF_INT   0
#define SAP_RGU_INT  1


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

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t misc_ctrl;
    uint32_t resource_status;
    uint32_t disable_aspm;
    uint32_t enable_aspm;
    uint32_t imask_msix_clr;
    uint32_t imask_msix_set;
    uint32_t istat_hst_ctrl;
    uint32_t msix_istat_grp0;
    uint32_t pm_resume_state;
    uint32_t misc_dev_status;
    uint32_t cfg_msix;

    /* simple flag to indicate PCIe interrupts enabled */
    bool pcie_irqs_enabled;
};


/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    if (!s->pcie_irqs_enabled) {
        return;
    }

    /* Minimal model: trigger vector 0 whenever msix_istat_grp0 has any bit set */
    if (s->msix_istat_grp0) {
        if (msix_enabled(pdev)) {
            msix_notify(pdev, 0);
        } else if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!msix_enabled(pdev) && !msi_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

/* Device-initiated DMA logic based on driver access patterns
 * The provided driver code only uses DMA APIs on host side; it does not
 * program device DMA engines by writing MMIO registers in this file.
 * Therefore we do not implement any active DMA engine here.
 */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    (void)s;
    (void)is_write;
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case T7XX_PCIE_MISC_CTRL:
        val = s->misc_ctrl;
        break;
    case T7XX_PCIE_RESOURCE_STATUS:
        /* When driver waits for PM config, it expects all bits in mask to be set */
        val = s->resource_status;
        break;
    case DISABLE_ASPM_LOWPWR:
        val = s->disable_aspm;
        break;
    case ENABLE_ASPM_LOWPWR:
        val = s->enable_aspm;
        break;
    case IMASK_HOST_MSIX_CLR_GRP0_0:
        val = s->imask_msix_clr;
        break;
    case IMASK_HOST_MSIX_SET_GRP0_0:
        val = s->imask_msix_set;
        break;
    case ISTAT_HST_CTRL:
        val = s->istat_hst_ctrl;
        break;
    case MSIX_ISTAT_HST_GRP0_0:
        val = s->msix_istat_grp0;
        break;
    case T7XX_PCIE_PM_RESUME_STATE:
        val = s->pm_resume_state;
        break;
    case T7XX_PCIE_MISC_DEV_STATUS:
        val = s->misc_dev_status;
        break;
    case T7XX_PCIE_CFG_MSIX:
        val = s->cfg_msix;
        break;
    case ATR_PCIE_WIN0_T0_ATR_PARAM_SRC_ADDR:
        /* ATR source address register used in resume state detection */
        val = 0; /* any initialized value different from ATR_SRC_ADDR_INVALID */
        break;
    default:
        /* other registers default to 0 */
        val = 0;
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case T7XX_PCIE_MISC_CTRL:
        s->misc_ctrl = (uint32_t)val;
        break;
    case T7XX_PCIE_RESOURCE_STATUS:
        s->resource_status = (uint32_t)val;
        break;
    case DISABLE_ASPM_LOWPWR:
        s->disable_aspm = (uint32_t)val;
        /* when driver writes DISABLE_ASPM_LOWPWR it expects resource_status to settle */
        s->resource_status = T7XX_PCIE_RESOURCE_STS_MSK;
        break;
    case ENABLE_ASPM_LOWPWR:
        s->enable_aspm = (uint32_t)val;
        break;
    case IMASK_HOST_MSIX_CLR_GRP0_0:
        /* write-1-to-clear style mask clear */
        s->imask_msix_clr |= (uint32_t)val;
        break;
    case IMASK_HOST_MSIX_SET_GRP0_0:
        s->imask_msix_set |= (uint32_t)val;
        break;
    case ISTAT_HST_CTRL:
        s->istat_hst_ctrl = (uint32_t)val;
        /* bit 0 controls host interrupt enable */
        s->pcie_irqs_enabled = ((s->istat_hst_ctrl & ISTAT_HST_CTRL_DIS) == 0);
        break;
    case MSIX_ISTAT_HST_GRP0_0:
        /* driver writes BIT(EXT_INT_START + int_type) to clear status */
        s->msix_istat_grp0 &= ~((uint32_t)val);
        pcibase_update_irq(s);
        break;
    case T7XX_PCIE_PM_RESUME_STATE:
        s->pm_resume_state = (uint32_t)val;
        break;
    case T7XX_PCIE_MISC_DEV_STATUS:
        /* host_event_notify writes masked value with FIELD_PREP(HOST_EVENT_MASK, event_id) */
        s->misc_dev_status &= ~HOST_EVENT_MASK;
        s->misc_dev_status |= ((uint32_t)val & HOST_EVENT_MASK);
        break;
    case T7XX_PCIE_CFG_MSIX:
        s->cfg_msix = (uint32_t)val;
        break;
    default:
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    (void)opaque;
    (void)addr;
    (void)size;
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    (void)opaque;
    (void)addr;
    (void)val;
    (void)size;
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

    s->misc_ctrl = 0;
    s->resource_status = T7XX_PCIE_RESOURCE_STS_MSK;
    s->disable_aspm = 0;
    s->enable_aspm = 0;
    s->imask_msix_clr = 0;
    s->imask_msix_set = 0;
    s->istat_hst_ctrl = 0;
    s->msix_istat_grp0 = 0;
    s->pm_resume_state = PM_RESUME_REG_STATE_INIT;
    s->misc_dev_status = 0;
    s->cfg_msix = 0;
    s->pcie_irqs_enabled = true;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  MTK_T7XX_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  MTK_T7XX_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, MTK_T7XX_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /*
     * BAR Initialization:
     * Driver uses BAR 0 as IREG (T7XX_PCI_IREG_BASE = 0)
     * and BAR 2 as EREG (T7XX_PCI_EREG_BASE = 2).
     * We model IREG as a full chip register window and EREG as a smaller
     * 256-byte region, matching the driver's expectation for the EREG size
     * so that pcim_iomap_region() succeeds.
     *
     * NOTE: bar_info[] index is a simple list index; .index is the real PCI BAR number.
     */
    s->num_bars = 2;

    /* IREG BAR (BAR 0) */
    s->bar_info[0].index = 0;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    s->bar_info[0].size  = T7XX_PCIE_REG_SIZE_CHIP;
    s->bar_info[0].name  = "t7xx-ireg";

    /* EREG BAR (BAR 2) */
    s->bar_info[1].index = 2; /* PCI BAR number 2 */
    s->bar_info[1].type  = BAR_TYPE_MMIO;
    s->bar_info[1].size  = 0x100; /* 256 bytes, matches EREG window size */
    s->bar_info[1].name  = "t7xx-ereg";

    for (int i = 2; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type  = BAR_TYPE_NONE;
        s->bar_info[i].size  = 0;
        s->bar_info[i].name  = NULL;
    }

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = false;
    s->has_msix = false;

    /* Initialize register state consistent with reset */
    pcibase_reset(DEVICE(pdev));
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
    .name = "mtk_t7xx_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(misc_ctrl, PCIBaseState),
        VMSTATE_UINT32(resource_status, PCIBaseState),
        VMSTATE_UINT32(disable_aspm, PCIBaseState),
        VMSTATE_UINT32(enable_aspm, PCIBaseState),
        VMSTATE_UINT32(imask_msix_clr, PCIBaseState),
        VMSTATE_UINT32(imask_msix_set, PCIBaseState),
        VMSTATE_UINT32(istat_hst_ctrl, PCIBaseState),
        VMSTATE_UINT32(msix_istat_grp0, PCIBaseState),
        VMSTATE_UINT32(pm_resume_state, PCIBaseState),
        VMSTATE_UINT32(misc_dev_status, PCIBaseState),
        VMSTATE_UINT32(cfg_msix, PCIBaseState),
        VMSTATE_BOOL(pcie_irqs_enabled, PCIBaseState),
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

