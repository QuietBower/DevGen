/*
 * QAT 6xxx minimal PCI device model for driver probing
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
/* Removed unavailable linux/pci_ids.h include and provide local defines
 * to satisfy the compiler without changing runtime behavior beyond
 * what was already implied by PCIBASE_VENDOR_ID/DEVICE_ID usage.
 */

#define PCI_VENDOR_ID_INTEL       0x8086
#define PCI_DEVICE_ID_INTEL_QAT_6XXX 0x4940

#define TYPE_PCIBASE_DEVICE "6xxx_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCIBASE_VENDOR_ID          PCI_VENDOR_ID_INTEL
#define PCIBASE_DEVICE_ID          PCI_DEVICE_ID_INTEL_QAT_6XXX
#define PCIBASE_CLASS_ID           PCI_CLASS_OTHERS

#define ADF_GEN6_FUSECTL0_OFFSET   0x2C8
#define ADF_GEN6_ACCELERATORS_MASK 0x1
#define ADF_GEN6_FUSECTL4_OFFSET   0x2D8
#define ADF_GEN6_FUSECTL1_OFFSET   0x2CC
#define ADF_GEN6_RX_RINGS_OFFSET   1
#define ADF_GEN6_NUM_BANKS_PER_VF  4
#define ADF_6XXX_MMP               "qat_6xxx_mmp.bin"
#define ADF_NUM_THREADS_PER_AE     8
#define ADF_NUM_HB_CNT_PER_AE      ADF_NUM_THREADS_PER_AE
#define ADF_6XXX_MAX_ACCELENGINES  9
#define ADF_6XXX_ADMIN_AE_MASK     GENMASK(8, 8)
#define ADF_GEN6_TX_RINGS_MASK     0x1
#define ADF_GEN6_NUM_RINGS_PER_BANK 2
#define ADF_GEN6_ETR_MAX_BANKS     64
#define ADF_6XXX_FW                "qat_6xxx.bin"
#define ADF_GEN6_MAX_ACCELERATORS  1
#define ADF_6XXX_AE_FREQ           (1000 * HZ_PER_MHZ)
#define ADF_6XXX_ACCELENGINES_MASK GENMASK(8, 0)
#define ADF_GEN6_ETR_BAR           2
#define ADF_GEN6_MAILBOX_BASE_OFFSET 0x600970
#define ADF_GEN6_ADMINMSGUR_OFFSET 0x500574
#define ADF_GEN6_ADMINMSGLR_OFFSET 0x500578
#define ADF_GEN6_PMISC_BAR         1
#define ADF_GEN6_SRAM_BAR          0
#define ADF_GEN6_COUNTER_FREQ      (100 * HZ_PER_MHZ)
#define ADF_GEN6_ERRMSK2           0x41A218
#define ADF_GEN6_MSIX_RTTABLE_OFFSET(i) (0x409000 + ((i) * 4))
#define ADF_SSM_WDT_DEFAULT_VALUE  0x7000000ULL
#define ADF_SSMWDTUCSH_OFFSET      0x580C
#define ADF_SSMWDTDCPRH_OFFSET     0x5A0C
#define ADF_SSMWDTCNVH_OFFSET      0x540C
#define ADF_SSMWDTATHL_OFFSET      0x5208
#define ADF_SSMWDTPKEL_OFFSET      0x5E08
#define ADF_SSMWDTATHH_OFFSET      0x520C
#define ADF_SSMWDTCNVL_OFFSET      0x5408
#define ADF_SSM_WDT_PKE_DEFAULT_VALUE 0x8000000ULL
#define ADF_SSMWDTDCPRL_OFFSET     0x5A08
#define ADF_SSMWDTUCSL_OFFSET      0x5808
#define ADF_SSMWDTPKEH_OFFSET      0x5E0C
#define ADF_GEN6_RL_TOKEN_PCIEIN_BUCKET_OFFSET 0x508800
#define ADF_6XXX_RL_PCIE_SCALE_FACTOR_DIV 100
#define ADF_6XXX_RL_SLICE_REF      1000UL
#define ADF_GEN6_RL_L2C_OFFSET     0x509000
#define ADF_6XXX_RL_PCIE_SCALE_FACTOR_MUL 102
#define ADF_GEN6_RL_R2L_OFFSET     0x508000
#define ADF_6XXX_RL_MAX_TP_ASYM    173750UL
#define ADF_GEN6_RL_C2S_OFFSET     0x508818
#define ADF_6XXX_RL_SCANS_PER_SEC  954
#define ADF_6XXX_RL_MAX_TP_SYM     95000UL
#define ADF_6XXX_RL_MAX_TP_DECOMP  40000UL
#define ADF_6XXX_RL_MAX_TP_DC      40000UL
#define ADF_GEN6_RL_TOKEN_PCIEOUT_BUCKET_OFFSET 0x508804
#define ADF_GEN6_SMIAPF_RP_X0_MASK_OFFSET 0x41A040
#define ADF_GEN6_SMIAPF_RP_X1_MASK_OFFSET 0x41A044
#define ADF_GEN6_SMIAPF_MASK_OFFSET 0x41A084
#define ADF_GEN6_ERRMSK3           0x41A21C
#define ADF_GEN6_VFLNOTIFY         BIT(7)
#define ADF_GEN6_ARB_CONFIG        (BIT(31) | BIT(6) | BIT(0))
#define ADF_GEN6_ARB_WRK_2_SER_MAP_OFFSET 0x400
#define ADF_GEN6_ARB_OFFSET        0x000
#define ADF_WQM_CSR_RPRESETSTS(bank) (ADF_WQM_CSR_RPRESETCTL(bank) + 4)
#define ADF_WQM_CSR_RPRESETCTL(bank) (0x6000 + (bank) * 8)
#define ADF_RPRESET_POLL_TIMEOUT_US (5 * USEC_PER_SEC)
#define ADF_WQM_CSR_RPRESETSTS_STATUS BIT(0)
#define ADF_RPRESET_POLL_DELAY_US  20
#define ADF_WQM_CSR_RPRESETCTL_RESET BIT(0)
#define ADF_6XXX_DC_OBJ            "qat_6xxx_dc.bin"
#define ADF_6XXX_CY_OBJ            "qat_6xxx_cy.bin"
#define ADF_6XXX_ADMIN_OBJ         "qat_6xxx_admin.bin"
#define ADF_GEN6_RINGMODECTL_TC_MASK GENMASK(18, 16)
#define ADF_GEN6_RINGMODECTL_TC_EN_OP1 0x1
#define ADF_GEN6_RINGMODECTL_TC_EN_MASK GENMASK(20, 19)
#define ADF_GEN6_CSR_RINGMODECTL(bank) (0x9000 + (bank) * 4)
#define ADF_GEN6_RINGMODECTL_TC_DEFAULT 0x7
#define ADF_GEN6_PVC1CTL_VCEN_ON  0x1
#define ADF_GEN6_PVC0CTL_TCVCMAP_MASK GENMASK(7, 1)
#define ADF_GEN6_PVC0CTL_TCVCMAP_DEFAULT 0x3F
#define ADF_GEN6_PVC1CTL_VCEN_MASK BIT(31)
#define ADF_GEN6_PVC0CTL_OFFSET   0x204
#define ADF_GEN6_PVC1CTL_TCVCMAP_DEFAULT 0x40
#define ADF_GEN6_PVC1CTL_TCVCMAP_MASK GENMASK(7, 1)
#define ADF_GEN6_PVC1CTL_OFFSET   0x210

#define ADF_GEN6_SRAM_BAR         0
#define ADF_GEN6_PMISC_BAR        1
#define ADF_GEN6_ETR_BAR          2

/* adf_bar_map is unused in this QEMU model; remove to avoid warning
 * without affecting runtime behavior.
 */


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
    /* We model only PCI config DWORDs used by the driver: FUSECTL4/0/1. */
    uint32_t fusectl0;
    uint32_t fusectl1;
    uint32_t fusectl4;

    /* Simple PMISC BAR register backing store for CSR writes/reads */
    uint32_t pmisc_regs[0x100000 / 4];
};


/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    /* No interrupt behavior is exercised in the provided probe path. */
    (void)s;
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /* No device-initiated DMA visible in the provided driver snippet. */
    (void)s;
    (void)is_write;
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Implement only behaviors explicitly exercised by the driver. */

    /* PMISC BAR is registered as BAR index 2. The driver accesses PMISC
     * registers via helpers like adf_get_pmisc_base() and ADF_CSR_RD/WR,
     * with explicit offsets defined above (ADF_GEN6_ERRMSK2, etc.).
     * We simply back them with a flat uint32_t array.
     */

    if (size == 4) {
        hwaddr word_index = addr >> 2;
        if (word_index < (sizeof(s->pmisc_regs) / sizeof(s->pmisc_regs[0]))) {
            val = s->pmisc_regs[word_index];
        }
    } else if (size == 8) {
        /* 64-bit reads are used via ADF_CSR_WR64_LO_HI for writes; reads
         * are not shown in the supplied snippets. To stay conservative,
         * when 64-bit reads happen we compose them from two consecutive
         * 32-bit locations.
         */
        hwaddr word_index = addr >> 2;
        if (word_index + 1 < (sizeof(s->pmisc_regs) / sizeof(s->pmisc_regs[0]))) {
            uint64_t lo = s->pmisc_regs[word_index];
            uint64_t hi = s->pmisc_regs[word_index + 1];
            val = lo | (hi << 32);
        }
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* Implement only behaviors explicitly exercised by the driver. */

    if (size == 4) {
        hwaddr word_index = addr >> 2;
        if (word_index < (sizeof(s->pmisc_regs) / sizeof(s->pmisc_regs[0]))) {
            s->pmisc_regs[word_index] = (uint32_t)val;
        }
    } else if (size == 8) {
        /* Back 64-bit writes (ADF_CSR_WR64_LO_HI) by splitting into
         * two adjacent 32-bit registers.
         */
        hwaddr word_index = addr >> 2;
        if (word_index + 1 < (sizeof(s->pmisc_regs) / sizeof(s->pmisc_regs[0]))) {
            s->pmisc_regs[word_index] = (uint32_t)(val & 0xFFFFFFFFULL);
            s->pmisc_regs[word_index + 1] = (uint32_t)(val >> 32);
        }
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Logic for Port I/O (Legacy support) - not used by the driver. */
    (void)s;
    (void)addr;
    (void)size;
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    /* Logic for Port I/O (Legacy support) - not used by the driver. */
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

    /* Revert registers to power-on defaults that satisfy probe().
     * The probe reads three config DWORDs into hw_data->fuses[].
     * It then checks:
     *   if (!(fusectl1 & ICP_ACCEL_GEN6_MASK_WCP_WAT_SLICE))
     *     fail;
     * and later calls hw_data->get_accel_mask(), get_ae_mask(), etc.
     * Since their implementations are not visible, we only guarantee
     * that the wireless-mode check passes by setting fusectl1 bit(s)
     * corresponding to ICP_ACCEL_GEN6_MASK_WCP_WAT_SLICE.  Without
     * the actual define value we conservatively set all bits.
     */
    s->fusectl0 = 0x00000000;
    s->fusectl4 = 0x00000000;
    s->fusectl1 = 0xFFFFFFFFu;

    /* Program the config space shadows for FUSECTL registers so that
     * pci_read_config_dword() in the guest sees the desired values.
     */
    PCIDevice *pdev = PCI_DEVICE(dev);
    pci_set_long(pdev->config + ADF_GEN6_FUSECTL0_OFFSET, s->fusectl0);
    pci_set_long(pdev->config + ADF_GEN6_FUSECTL1_OFFSET, s->fusectl1);
    pci_set_long(pdev->config + ADF_GEN6_FUSECTL4_OFFSET, s->fusectl4);

    /* Clear PMISC backing store to known state. */
    memset(s->pmisc_regs, 0, sizeof(s->pmisc_regs));
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

    /* BAR Initialization */
    s->num_bars = 3;

    s->bar_info[0].index = 0;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    s->bar_info[0].size  = 1 * MiB;
    s->bar_info[0].name  = "qat6xxx-sram";

    s->bar_info[1].index = 2;
    s->bar_info[1].type  = BAR_TYPE_MMIO;
    s->bar_info[1].size  = 1 * MiB;
    s->bar_info[1].name  = "qat6xxx-pmisc";

    s->bar_info[2].index = 4;
    s->bar_info[2].type  = BAR_TYPE_MMIO;
    s->bar_info[2].size  = 1 * MiB;
    s->bar_info[2].name  = "qat6xxx-etr";
  
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* msi_init or msix_init calls left disabled for now; driver enables
     * MSI-X through its own flows which we are not modeling here.
     */
    s->has_msi = false;
    s->has_msix = false;

    /* Ensure reset defaults are applied now. */
    pcibase_reset(DEVICE(pdev));
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

    /* Free buffers, stop timers, etc. */
    (void)s;
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "6xxx_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(fusectl0, PCIBaseState),
        VMSTATE_UINT32(fusectl1, PCIBaseState),
        VMSTATE_UINT32(fusectl4, PCIBaseState),
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
