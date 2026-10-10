/*
 * IGEN6 EDAC PCI device QEMU model - Phase 2 (Behavioral Implementation)
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

#define TYPE_PCIBASE_DEVICE "igen6_edac_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define IGEN6_PCI_VENDOR_ID 0x8086
#define IGEN6_PCI_DEVICE_ID 0x4514
#ifndef PCI_CLASS_MEMORY_EDAC
#define PCI_CLASS_MEMORY_EDAC 0x0508
#endif
#define IGEN6_PCI_CLASS_ID  PCI_CLASS_MEMORY_EDAC

#define IGEN6_REVISION "v2.5.1"
#define EDAC_MOD_STR "igen6_edac"
#define IGEN6_NMI_NAME "igen6_ibecc"
#define NUM_IMC                2
#define NUM_CHANNELS           2
#define NUM_DIMMS              2
#define TOM_OFFSET             0xa0
#define TOLUD_OFFSET           0xbc
#define CAPID_C_OFFSET         0xec
#define CAPID_C_IBECC          (1UL << 15)
#define CAPID_E_OFFSET         0xf0
#define CAPID_E_IBECC          (1UL << 12)
#define CAPID_E_IBECC_BIT18    (1UL << 18)
#define ERRSTS_OFFSET          0xc8
#define ERRSTS_CE              (1ULL << 6)
#define ERRSTS_UE              (1ULL << 7)
#define ERRCMD_OFFSET          0xca
#define ERRCMD_CE              (1ULL << 6)
#define ERRCMD_UE              (1ULL << 7)
#define ECC_ERROR_LOG_CE       (1ULL << 62)
#define ECC_ERROR_LOG_UE       (1ULL << 63)
#define ECC_ERROR_LOG_ADDR_SHIFT 5
#define MCHBAR_OFFSET          0x48
#define MCHBAR_EN              (1ULL << 0)
#define MCHBAR_SIZE            0x10000
#define MAD_INTRA_CH0_OFFSET   0x5004
#define MAD_DIMM_CH0_OFFSET    0x500c
#define MAD_MC_HASH_OFFSET     0x51b8
#define CHANNEL_HASH_OFFSET    0x5024
#define CHANNEL_EHASH_OFFSET   0x5028
#define ECCLOG_POOL_SIZE       (1UL << CONFIG_PAGE_SHIFT)
#define DID_EHL_SKU5           0x4514

/* Some helper masks taken from driver macros usage */
#define GENMASK(h, l)   (((~0U)  - (1U << (l)) + 1) & \
                        (~0U >> (31 - (h))))
#define GENMASK_ULL(h, l) (((~0ULL) - (1ULL << (l)) + 1) & \
                           (~0ULL >> (63 - (h))))

/* For simplicity, assume 4KB page shift when building standalone */
#ifndef CONFIG_PAGE_SHIFT
#define CONFIG_PAGE_SHIFT 12
#endif

/* The driver defines MCHBAR_BASE(v) as GET_BITFIELD(v,16,38)<<16.
 * We do not know GET_BITFIELD's literal implementation here, but we
 * can mirror the effective mask/shift directly when translating the
 * config-space MCHBAR value into the guest-physical base address.
 */
#define MCHBAR_BASE_FROM_VALUE(v) \
    ((((v) >> 16) & GENMASK_ULL(38 - 16, 0)) << 16)

/* BAR types description */
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
    /* Config space shadows for fields accessed via pci_read_config_* */
    uint32_t reg_tolud;      /* 32-bit at TOLUD_OFFSET */

    uint64_t reg_tom;        /* 64-bit at TOM_OFFSET (low/high dwords) */

    uint32_t reg_capid_c;    /* 32-bit at CAPID_C_OFFSET */
    uint32_t reg_capid_e;    /* 32-bit at CAPID_E_OFFSET */

    uint16_t reg_errsts;     /* 16-bit at ERRSTS_OFFSET */
    uint16_t reg_errcmd;     /* 16-bit at ERRCMD_OFFSET */

    uint64_t reg_mchbar;     /* 64-bit at MCHBAR_OFFSET (low/high) */

    /* MCHBAR MMIO window (system memory) */
    MemoryRegion mchbar_region;

    /* Internal ECC error log per IMC (one per MCHBAR window) */
    uint64_t ecc_error_log[NUM_IMC];

    /* Simple flag to simulate that IBECC is present */
    bool ibecc_present;
};


/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* The driver for this device uses NMI / machine check to report errors
     * and does not interact with legacy INTx/MSI directly. To keep things
     * simple and within the provided driver scope, we do not model any
     * IRQ behavior here. This function is kept for potential future
     * extensions but intentionally left empty.
     */
    (void)pdev;
}

/* Device-initiated DMA logic based on driver access patterns
 * The driver never programs any DMA engine in the device, nor does it
 * perform pci_{dma,alloc}_* on this PCI function. All memory accesses
 * are CPU-initiated via ioremap() of the MCHBAR window.
 * Therefore no device-initiated DMA is implemented here.
 */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    (void)s;
    (void)pdev;
    (void)is_write;
}

/* Helper to read 32-bit from MCHBAR shadow */
static uint32_t pcibase_mchbar_readl(PCIBaseState *s, hwaddr offset)
{
    /* The driver only reads a finite set of offsets; return 0 for others. */
    switch (offset) {
    case CHANNEL_HASH_OFFSET:
        /* CHANNEL_HASH register */
        return 0;
    case CHANNEL_EHASH_OFFSET:
        /* CHANNEL_EHASH register */
        return 0;
    case MAD_INTRA_CH0_OFFSET:
    case MAD_INTRA_CH0_OFFSET + 4:
        /* MAD_INTRA_CH registers per channel */
        return 0;
    case MAD_DIMM_CH0_OFFSET:
    case MAD_DIMM_CH0_OFFSET + 4:
        /* MAD_DIMM_CH registers per channel */
        return 0;
    case MAD_MC_HASH_OFFSET:
        /* MAD_MC_HASH register */
        return 0;
    default:
        return 0;
    }
}

/* Helper to read 64-bit from MCHBAR shadow */
static uint64_t pcibase_mchbar_readq(PCIBaseState *s, hwaddr offset)
{
    /* Known 64-bit register: ECC_ERROR_LOG at per-IMC offset. The exact
     * base offset within the MCHBAR window (ECC_ERROR_LOG_OFFSET) is not
     * explicitly defined in the provided snippet, but it is referenced
     * symbolically. Without its literal numeric value, we cannot map it
     * correctly, so we simply return the stored value for each IMC based
     * on the offset alignment within MCHBAR_SIZE.
     */

    /* Determine which IMC window based on offset/MCHBAR_SIZE */
    int mc = offset / MCHBAR_SIZE;
    hwaddr off_in_mc = offset % MCHBAR_SIZE;

    if (mc < 0 || mc >= NUM_IMC) {
        return 0;
    }

    /* We only emulate ECC_ERROR_LOG at a single offset within each IMC
     * window if the offset matches ECC_ERROR_LOG_OFFSET.
     */
    if (off_in_mc == 0) {
        /* Generic 0 offset: not used by driver for 64-bit read */
        return 0;
    }

    /* Without the literal ECC_ERROR_LOG_OFFSET value, we cannot do a
     * precise comparison. Return stored ecc_error_log only when the
     * driver later injects errors via debugfs and uses CPU helpers,
     * not via direct MMIO, so practical effect is minimal.
     */

    (void)off_in_mc;
    return s->ecc_error_log[mc];
}

/* Helper to write 64-bit into MCHBAR shadow */
static void pcibase_mchbar_writeq(PCIBaseState *s, hwaddr offset, uint64_t val)
{
    int mc = offset / MCHBAR_SIZE;
    hwaddr off_in_mc = offset % MCHBAR_SIZE;

    if (mc < 0 || mc >= NUM_IMC) {
        return;
    }

    /* As with readq, we only have semantic knowledge for the ECC_ERROR_LOG
     * register in terms of driver behavior: the driver writes the same
     * value back to clear CE/UE bits (W1C semantics). However, the device
     * model does not need to generate these errors. Implementing W1C here
     * would require knowing the exact offset. Since that's not provided as
     * a literal, we do not modify ecc_error_log[] here.
     */

    (void)off_in_mc;
    (void)val;
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    /* MCHBAR region is modeled as a simple memory-mapped window representing
     * host bridge registers. The driver uses ioremap(MCHBAR_BASE(reg_mchbar))
     * and then performs 32-bit or 64-bit accesses at specific offsets.
     *
     * Our mchbar_region is created with size MCHBAR_SIZE * NUM_IMC and uses
     * this callback. Here, 'addr' is already relative to the base of
     * mchbar_region (i.e., guest-physical address minus MCHBAR_BASE).
     */

    if (size == 4) {
        return pcibase_mchbar_readl(s, addr);
    } else if (size == 8) {
        return pcibase_mchbar_readq(s, addr);
    }

    /* For any unexpected access sizes, return 0. */
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size == 8) {
        pcibase_mchbar_writeq(s, addr, val);
    }
    /* The driver is not known to perform 32-bit writes to MCHBAR registers
     * we model, so 32-bit writes are ignored for now.
     */
    (void)s;
    (void)addr;
    (void)val;
    (void)size;
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    (void)s;
    (void)addr;
    (void)size;

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
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
    PCIDevice *pdev = PCI_DEVICE(dev);
    uint8_t *pci_conf = pdev->config;

    pci_device_reset(PCI_DEVICE(dev));

    /* Reinitialize config space shadows to a safe state expected by
     * the driver. These values are chosen to satisfy logical checks
     * in the driver, based only on masks and bit tests performed.
     */

    /* TOLUD: mask with GENMASK(31,20) in driver, any aligned value < 4GB. */
    s->reg_tolud = 0x80000000U & GENMASK(31, 20);
    pci_set_long(pci_conf + TOLUD_OFFSET, s->reg_tolud);

    /* TOM: 64-bit physical address of top-of-memory, masked with
     * GENMASK_ULL(38,20). Choose 8GB for simplicity.
     */
    s->reg_tom = (uint64_t)0x0000000200000000ULL & GENMASK_ULL(38, 20);
    pci_set_long(pci_conf + TOM_OFFSET, (uint32_t)(s->reg_tom & 0xffffffffULL));
    pci_set_long(pci_conf + TOM_OFFSET + 4, (uint32_t)(s->reg_tom >> 32));

    /* CAPID_C / CAPID_E: used only to check IBECC availability. We
     * set them so that ehl_ibecc_available() and tgl_ibecc_available()
     * style functions would report IBECC is present for at least one
     * configuration. For the selected PCI ID (DID_EHL_SKU5), the
     * driver will pick ehl_cfg, which uses ehl_ibecc_available() and
     * checks (CAPID_C_IBECC & v). Return bit set => available.
     */
    s->reg_capid_c = CAPID_C_IBECC;  /* ensures ehl_ibecc_available() == true */
    pci_set_long(pci_conf + CAPID_C_OFFSET, s->reg_capid_c);

    /* CAPID_E only used on some platforms; keep 0 so their
     * !CAPID_E_IBECC tests evaluate as needed. Not relevant for EHL.
     */
    s->reg_capid_e = 0;
    pci_set_long(pci_conf + CAPID_E_OFFSET, s->reg_capid_e);

    /* ERRSTS: initially no errors */
    s->reg_errsts = 0;
    pci_set_word(pci_conf + ERRSTS_OFFSET, s->reg_errsts);

    /* ERRCMD: error reporting disabled at reset; driver enables it */
    s->reg_errcmd = 0;
    pci_set_word(pci_conf + ERRCMD_OFFSET, s->reg_errcmd);

    /* MCHBAR: driver requires MCHBAR_EN and base address non-zero. We
     * choose an arbitrary aligned base within 64-bit space. The driver
     * masks and uses MCHBAR_BASE(u.v). We keep MCHBAR_EN set.
     */
    s->reg_mchbar = ((uint64_t)0x00000000FEDC0000ULL) | MCHBAR_EN;
    pci_set_long(pci_conf + MCHBAR_OFFSET, (uint32_t)(s->reg_mchbar & 0xffffffffULL));
    pci_set_long(pci_conf + MCHBAR_OFFSET + 4, (uint32_t)(s->reg_mchbar >> 32));

    /* Initialize internal ECC logs to zero */
    for (int i = 0; i < NUM_IMC; i++) {
        s->ecc_error_log[i] = 0;
    }

    /* Mark IBECC present */
    s->ibecc_present = true;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  IGEN6_PCI_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  IGEN6_PCI_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, IGEN6_PCI_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* Initialize register shadows to defaults before exposing BARs */
    pcibase_reset(DEVICE(pdev));

    /* BAR Initialization: by default, expose no additional BARs; the
     * driver does not rely on them for this function (it uses config
     * space only). Keep num_bars at 0 unless configured by board code.
     */
    s->num_bars = 0;
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Create an internal MCHBAR MMIO region and map it into the guest
     * physical address space at the base derived from reg_mchbar.
     * The driver uses MCHBAR_BASE(reg_mchbar) to compute this base.
     */
    memory_region_init_io(&s->mchbar_region, OBJECT(s), &pcibase_mmio_ops, s,
                          "igen6-edac-mchbar", MCHBAR_SIZE * NUM_IMC);

    uint64_t mchbar_base = MCHBAR_BASE_FROM_VALUE(s->reg_mchbar);
    MemoryRegion *sysmem = get_system_memory();
    memory_region_add_subregion(sysmem, mchbar_base, &s->mchbar_region);
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

    /* No additional cleanup needed for simple shadows */
    (void)s;
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "igen6_edac_pci",
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

