/*
 * QEMU PCI device model for Intel QAT 4xxx (minimal probe-enabling stub)
 *
 * This implements only the behavior explicitly visible in the provided
 * adf_drv.c probe/remove/shutdown path and supplementary 4xxx hw-data
 * initialization code. No hidden or speculative hardware behavior is modeled.
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

#define TYPE_PCIBASE_DEVICE "4xxx_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCIBASE_VENDOR_ID 0x8086
#define PCIBASE_DEVICE_ID 0x4940
#define PCIBASE_CLASS_ID  PCI_CLASS_OTHERS

#define HZ_PER_MHZ                1000000UL
#define BIT_(nr)                  (1UL << (nr))
#define PCI_DEVICE_ID_INTEL_QAT_4XXX 0x4940
#define PCI_DEVICE_ID_INTEL_QAT_401XX 0x4942
#define PCI_DEVICE_ID_INTEL_QAT_402XX 0x4944
#define ADF_4XXX_DEVICE_NAME "4xxx"
#define ADF_4XXX_AE_FREQ                (1000 * HZ_PER_MHZ)
#define ADF_4XXX_MAX_ACCELENGINES       9
#define ADF_4XXX_FW             "qat_4xxx.bin"
#define ADF_4XXX_MMP            "qat_4xxx_mmp.bin"
#define ADF_402XX_MMP           "qat_402xx_mmp.bin"
#define ADF_402XX_FW            "qat_402xx.bin"
#define ADF_4XXX_ADMIN_AE_MASK          (0x100)
#define ADF_4XXX_ACCELENGINES_MASK      (0x1FF)
#define ADF_4XXX_PARITYERRORMASK_ATH_CPH_MASK  0xF000F
#define ADF_4XXX_HICPPAGENTCMDPARERRLOG_MASK    0x1F
#define ADF_4XXX_PARITYERRORMASK_CPR_XLT_MASK   0x10001
#define ADF_4XXX_PARITYERRORMASK_PKE_MASK       0x3F
#define ADF_4XXX_PARITYERRORMASK_DCPR_UCS_MASK  0x30007
#define ADF_4XXX_SSMFEATREN_MASK \
    (BIT_(4) | BIT_(12) | BIT_(16) | BIT_(17) | BIT_(18) | \
     BIT_(19) | BIT_(20) | BIT_(21) | BIT_(22) | BIT_(23))
#define ADF_4XXX_RL_PCIE_SCALE_FACTOR_MUL       102
#define ADF_4XXX_RL_MAX_TP_SYM                  95000UL
#define ADF_4XXX_RL_DCPR_CORRECTION             1
#define ADF_4XXX_RL_SLICE_REF                   1000UL
#define ADF_4XXX_RL_SCANS_PER_SEC               954
#define ADF_4XXX_RL_PCIE_SCALE_FACTOR_DIV       100
#define ADF_4XXX_RL_MAX_TP_ASYM                 173750UL
#define ADF_4XXX_RL_MAX_TP_DC                   45000UL
#define ADF_402XX_ASYM_OBJ      "qat_402xx_asym.bin"
#define ADF_402XX_DC_OBJ        "qat_402xx_dc.bin"
#define ADF_402XX_SYM_OBJ       "qat_402xx_sym.bin"
#define ADF_402XX_ADMIN_OBJ     "qat_402xx_admin.bin"
#define ADF_4XXX_DC_OBJ         "qat_4xxx_dc.bin"
#define ADF_4XXX_ADMIN_OBJ      "qat_4xxx_admin.bin"
#define ADF_4XXX_SYM_OBJ        "qat_4xxx_sym.bin"
#define ADF_4XXX_ASYM_OBJ       "qat_4xxx_asym.bin"

#define ADF_GEN4_FUSECTL4_OFFSET    0x2D8
#define ADF_GEN4_BAR_MASK  (BIT_(0) | BIT_(2) | BIT_(4))

/* Forward declarations of driver-specific types referenced in probe path */
struct adf_accel_dev;
struct adf_accel_pci;
struct adf_hw_device_data;
struct adf_cfg_device_data;
struct adf_fw_loader_data;
struct adf_admin_comms;
struct adf_telemetry;
struct adf_dc_data;
struct adf_timer;
struct adf_heartbeat;
struct adf_rl;
struct adf_sysfs;
struct adf_accel_vf_info;
struct adf_error_counters;

/* PMISC BAR register offsets from driver source */
#define ADF_GEN4_ERRMSK2                 0x41A218
#define ADF_GEN4_PM_SOU                  (1U << 18)
#define ADF_GEN4_PM_INTERRUPT            0x50A028
#define ADF_GEN4_PM_DRV_ACTIVE           (1U << 20)
#define ADF_GEN4_PM_INIT_STATE           (1U << 21)
#define ADF_GEN4_PM_POLL_DELAY_US        20
#define ADF_GEN4_PM_POLL_TIMEOUT_US      1000000
#define ADF_GEN4_PM_STATUS               0x50A00C
#define ADF_GEN4_SMIAPF_RP_X0_MASK_OFFSET 0x41A040
#define ADF_GEN4_SMIAPF_RP_X1_MASK_OFFSET 0x41A044
#define ADF_GEN4_SMIAPF_MASK_OFFSET      0x41A084
#define ADF_GEN4_ERRMSK3                 0x41A21C
#define ADF_GEN4_VFLNOTIFY               (1U << 7)
#define ADF_GEN4_ERRSOU2                 0x41A208
/* Default PM interrupt enable and status masks are defined in other headers */
#define ADF_GEN4_PM_INT_EN_DEFAULT       0x0
#define ADF_GEN4_PM_INT_STS_MASK         0x0

#define ADF_GEN4_MAILBOX_BASE_OFFSET     0x600970
#define ADF_GEN4_ADMINMSGUR_OFFSET       0x500574
#define ADF_GEN4_ADMINMSGLR_OFFSET       0x500578
#define ADF_GEN4_MSIX_RTTABLE_OFFSET(i)  (0x409000 + ((i) * 0x04))

#define ADF_SSM_WDT_PKE_DEFAULT_VALUE    0x2000000ULL
#define ADF_SSM_WDT_DEFAULT_VALUE        0x200000ULL
#define ADF_SSMWDTL_OFFSET               0x54
#define ADF_SSMWDTH_OFFSET               0x5C
#define ADF_SSMWDTPKEL_OFFSET            0x58
#define ADF_SSMWDTPKEH_OFFSET            0x60

/* BAR indices for SRAM / ETR / PMISC */
#define ADF_GEN4_SRAM_BAR  0
#define ADF_GEN4_ETR_BAR   2
#define ADF_GEN4_PMISC_BAR 1

/* ETR / bank limits */
#define ADF_GEN4_ETR_MAX_BANKS           64

/* Basic BAR description used by QEMU model */
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
    /* MMIO register space backing store: only locations touched by driver */
    uint32_t fusectl4;

    /* PMISC BAR register shadows that the driver manipulates */
    uint32_t pm_errmsk2;
    uint32_t pm_interrupt;
    uint32_t pm_status;
    uint32_t errmsk3;
    uint32_t errsou2;
    uint32_t smiapf_rp_x0_mask;
    uint32_t smiapf_rp_x1_mask;
    uint32_t smiapf_mask;

    /* Admin mailbox / message registers in PMISC */
    uint32_t adminmsg_ur;
    uint32_t adminmsg_lr;
    uint64_t mailbox_dma_addr;

    /* MSI-X redirection table shadow for adf_gen4_set_msix_default_rttable */
    uint32_t msix_rttable[ADF_GEN4_ETR_MAX_BANKS + 1];

    /* Watchdog timer register shadows */
    uint64_t ssm_wdt_val;
    uint64_t ssm_wdt_pke_val;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    /*
     * The provided driver source does not show any direct ISR path that
     * inspects PMISC status/err registers in a way we can emulate without
     * additional context. Therefore, we keep interrupts inert for now.
     */
    (void)s;
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /*
     * The provided snippets do not program DMA descriptors or initiate
     * device DMA, so no DMA logic is implemented here.
     */
    (void)s;
    (void)is_write;
}

/* Helpers to route PMISC MMIO accesses to shadows. In this minimal model we
 * only support 32-bit accesses to a small set of registers.
 */
static uint32_t pcibase_pmisc_readl(PCIBaseState *s, hwaddr off)
{
    if (off == ADF_GEN4_ERRMSK2) {
        return s->pm_errmsk2;
    } else if (off == ADF_GEN4_PM_INTERRUPT) {
        return s->pm_interrupt;
    } else if (off == ADF_GEN4_PM_STATUS) {
        return s->pm_status;
    } else if (off == ADF_GEN4_ERRMSK3) {
        return s->errmsk3;
    } else if (off == ADF_GEN4_ERRSOU2) {
        return s->errsou2;
    } else if (off == ADF_GEN4_SMIAPF_RP_X0_MASK_OFFSET) {
        return s->smiapf_rp_x0_mask;
    } else if (off == ADF_GEN4_SMIAPF_RP_X1_MASK_OFFSET) {
        return s->smiapf_rp_x1_mask;
    } else if (off == ADF_GEN4_SMIAPF_MASK_OFFSET) {
        return s->smiapf_mask;
    } else if (off == ADF_GEN4_ADMINMSGUR_OFFSET) {
        return s->adminmsg_ur;
    } else if (off == ADF_GEN4_ADMINMSGLR_OFFSET) {
        return s->adminmsg_lr;
    } else if (off == ADF_SSMWDTL_OFFSET) {
        /* lower 32 bits of symmetric/asymmetric watchdog value */
        return (uint32_t)(s->ssm_wdt_val & 0xFFFFFFFFULL);
    } else if (off == ADF_SSMWDTH_OFFSET) {
        /* upper 32 bits of symmetric/asymmetric watchdog value */
        return (uint32_t)((s->ssm_wdt_val >> 32) & 0xFFFFFFFFULL);
    } else if (off == ADF_SSMWDTPKEL_OFFSET) {
        /* lower 32 bits of PKE watchdog value */
        return (uint32_t)(s->ssm_wdt_pke_val & 0xFFFFFFFFULL);
    } else if (off == ADF_SSMWDTPKEH_OFFSET) {
        /* upper 32 bits of PKE watchdog value */
        return (uint32_t)((s->ssm_wdt_pke_val >> 32) & 0xFFFFFFFFULL);
    }

    /* MSIX redirection table */
    if (off >= ADF_GEN4_MSIX_RTTABLE_OFFSET(0) &&
        off <= ADF_GEN4_MSIX_RTTABLE_OFFSET(ADF_GEN4_ETR_MAX_BANKS)) {
        unsigned index = 0;
        if (ADF_GEN4_MSIX_RTTABLE_OFFSET(1) != ADF_GEN4_MSIX_RTTABLE_OFFSET(0)) {
            index = (off - ADF_GEN4_MSIX_RTTABLE_OFFSET(0)) /
                    (ADF_GEN4_MSIX_RTTABLE_OFFSET(1) - ADF_GEN4_MSIX_RTTABLE_OFFSET(0));
        }
        if (index <= ADF_GEN4_ETR_MAX_BANKS) {
            return s->msix_rttable[index];
        }
    }

    return 0;
}

static void pcibase_pmisc_writel(PCIBaseState *s, hwaddr off, uint32_t val)
{
    if (off == ADF_GEN4_ERRMSK2) {
        s->pm_errmsk2 = val;
        return;
    } else if (off == ADF_GEN4_PM_INTERRUPT) {
        /* PM interrupt register has both control and W1C bits; we simply
         * store the written value for the driver's readback. Any W1C
         * semantics are ignored because there is no external event source.
         */
        s->pm_interrupt = val;
        return;
    } else if (off == ADF_GEN4_ERRMSK3) {
        s->errmsk3 = val;
        return;
    } else if (off == ADF_GEN4_ERRSOU2) {
        s->errsou2 = val;
        return;
    } else if (off == ADF_GEN4_SMIAPF_RP_X0_MASK_OFFSET) {
        s->smiapf_rp_x0_mask = val;
        return;
    } else if (off == ADF_GEN4_SMIAPF_RP_X1_MASK_OFFSET) {
        s->smiapf_rp_x1_mask = val;
        return;
    } else if (off == ADF_GEN4_SMIAPF_MASK_OFFSET) {
        s->smiapf_mask = val;
        return;
    } else if (off == ADF_GEN4_ADMINMSGUR_OFFSET) {
        s->adminmsg_ur = val;
        /* upper 32 bits of admin DMA address */
        s->mailbox_dma_addr &= 0x00000000FFFFFFFFULL;
        s->mailbox_dma_addr |= ((uint64_t)val) << 32;
        return;
    } else if (off == ADF_GEN4_ADMINMSGLR_OFFSET) {
        s->adminmsg_lr = val;
        /* lower 32 bits of admin DMA address */
        s->mailbox_dma_addr &= 0xFFFFFFFF00000000ULL;
        s->mailbox_dma_addr |= (uint64_t)val;
        return;
    } else if (off == ADF_SSMWDTL_OFFSET) {
        /* lower 32 bits of symmetric/asymmetric watchdog value */
        s->ssm_wdt_val &= 0xFFFFFFFF00000000ULL;
        s->ssm_wdt_val |= (uint64_t)val;
        return;
    } else if (off == ADF_SSMWDTH_OFFSET) {
        /* upper 32 bits of symmetric/asymmetric watchdog value */
        s->ssm_wdt_val &= 0x00000000FFFFFFFFULL;
        s->ssm_wdt_val |= ((uint64_t)val) << 32;
        return;
    } else if (off == ADF_SSMWDTPKEL_OFFSET) {
        /* lower 32 bits of PKE watchdog value */
        s->ssm_wdt_pke_val &= 0xFFFFFFFF00000000ULL;
        s->ssm_wdt_pke_val |= (uint64_t)val;
        return;
    } else if (off == ADF_SSMWDTPKEH_OFFSET) {
        /* upper 32 bits of PKE watchdog value */
        s->ssm_wdt_pke_val &= 0x00000000FFFFFFFFULL;
        s->ssm_wdt_pke_val |= ((uint64_t)val) << 32;
        return;
    }

    /* MSIX redirection table programming */
    if (off >= ADF_GEN4_MSIX_RTTABLE_OFFSET(0) &&
        off <= ADF_GEN4_MSIX_RTTABLE_OFFSET(ADF_GEN4_ETR_MAX_BANKS)) {
        unsigned index = 0;
        if (ADF_GEN4_MSIX_RTTABLE_OFFSET(1) != ADF_GEN4_MSIX_RTTABLE_OFFSET(0)) {
            index = (off - ADF_GEN4_MSIX_RTTABLE_OFFSET(0)) /
                    (ADF_GEN4_MSIX_RTTABLE_OFFSET(1) - ADF_GEN4_MSIX_RTTABLE_OFFSET(0));
        }
        if (index <= ADF_GEN4_ETR_MAX_BANKS) {
            s->msix_rttable[index] = val;
        }
        return;
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /*
     * BAR index selection: QEMU passes BAR-local addresses to this callback.
     * Our model uses the same ops for all MMIO BARs; only the PMISC BAR has
     * meaningful semantics based on the driver snippets. SRAM/ETR remain inert.
     */

    if (size == 4) {
        val = pcibase_pmisc_readl(s, addr);
    } else {
        /* Other access sizes are not used by the driver snippets; return 0. */
        val = 0;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size == 4) {
        pcibase_pmisc_writel(s, addr, (uint32_t)val);
        pcibase_update_irq(s);
    } else {
        /* Ignore non-32-bit accesses; not used by driver snippets. */
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* No PIO usage is present in the provided driver snippets. */
    (void)s;
    (void)addr;
    (void)size;
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    /* No PIO usage is present in the provided driver snippets. */
    (void)s;
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

    /* Reset shadow registers to power-on defaults used by probe path. */
    s->fusectl4 = 0;

    s->pm_errmsk2 = 0;
    s->pm_interrupt = 0;
    s->pm_status = 0;
    s->errmsk3 = 0;
    s->errsou2 = 0;
    s->smiapf_rp_x0_mask = 0;
    s->smiapf_rp_x1_mask = 0;
    s->smiapf_mask = 0;

    s->adminmsg_ur = 0;
    s->adminmsg_lr = 0;
    s->mailbox_dma_addr = 0;

    memset(s->msix_rttable, 0, sizeof(s->msix_rttable));

    s->ssm_wdt_val = ADF_SSM_WDT_DEFAULT_VALUE;
    s->ssm_wdt_pke_val = ADF_SSM_WDT_PKE_DEFAULT_VALUE;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCIBASE_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCIBASE_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCIBASE_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /*
     * The driver calls pci_read_config_dword(pdev, ADF_GEN4_FUSECTL4_OFFSET,...).
     * This is in PCI config space. We expose a 32-bit config register at
     * that offset which returns s->fusectl4. Currently we keep fusectl4 at 0;
     * additional semantics depend on helper functions not provided here.
     */
    if (ADF_GEN4_FUSECTL4_OFFSET + 4 <= PCI_CFG_SPACE_SIZE) {
        pci_set_long(pci_conf + ADF_GEN4_FUSECTL4_OFFSET, s->fusectl4);
    }

    /* BAR Initialization: the probe uses pci_select_bars(IORESOURCE_MEM)
     * with ADF_GEN4_BAR_MASK (bits 0,2,4). We expose three 1 MiB MMIO BARs
     * at indices 0,2,4 so that pcim_iomap() succeeds. Their internal
     * register maps are modeled minimally for PMISC behavior as above.
     */
    s->num_bars = 3;
    for (int i = 0; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = "";
    }

    s->bar_info[0].index = 0;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    s->bar_info[0].size  = 1 * MiB;
    s->bar_info[0].name  = "qat4xxx-bar0";

    s->bar_info[1].index = 2;
    s->bar_info[1].type  = BAR_TYPE_MMIO;
    s->bar_info[1].size  = 1 * MiB;
    s->bar_info[1].name  = "qat4xxx-bar2";

    s->bar_info[2].index = 4;
    s->bar_info[2].type  = BAR_TYPE_MMIO;
    s->bar_info[2].size  = 1 * MiB;
    s->bar_info[2].name  = "qat4xxx-bar4";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No MSI/MSI-X explicitly referenced in provided snippets for PF probe
     * path; interrupt resources are handled via generic QAT helper code
     * not visible here.
     */
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
    .name = "4xxx_pci",
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
