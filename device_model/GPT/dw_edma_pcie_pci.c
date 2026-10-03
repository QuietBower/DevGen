/*
 * QEMU PCI device model skeleton for DesignWare eDMA PCIe
 * Generated for driver: drivers/dma/dw-edma/dw-edma-pcie.c
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
#include "hw/pci/pci_ids.h"

#define TYPE_PCIBASE_DEVICE "dw_edma_pcie_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define DW_PCIE_VSEC_DMA_ID            0x6
#define DW_PCIE_VSEC_DMA_BAR           GENMASK(10, 8)
#define DW_PCIE_VSEC_DMA_MAP           GENMASK(2, 0)
#define DW_PCIE_VSEC_DMA_WR_CH         GENMASK(9, 0)
#define DW_PCIE_VSEC_DMA_RD_CH         GENMASK(25, 16)
#define EDMA_LL_SZ                     24

/* We provide a minimal Vendor-Specific Extended Capability (VSEC)
 * block sufficient for dw_edma_pcie_get_vsec_dma_data() to find
 * and parse it. The driver requires:
 *  - Vendor ID = PCI_VENDOR_ID_SYNOPSYS
 *  - ID        = DW_PCIE_VSEC_DMA_ID
 *  - REV       = 0x00
 *  - LEN       = 0x18
 *  - At offset vsec+0x8: BAR and MAP (format)
 *  - At offset vsec+0xc: WR/RD channel counts
 *  - At offsets vsec+0x10/0x14: 64-bit register offset
 */

#define PCIBASE_VSEC_OFFSET     0x100
#define PCIBASE_VSEC_LENGTH     0x18

/* BAR indices used by the driver */
/* The driver defines enum pci_barno { NO_BAR=-1, BAR_0, BAR_1, BAR_2, BAR_3, BAR_4, BAR_5 } */

#define EDMA_MAX_WR_CH                 8
#define EDMA_MAX_RD_CH                 8

enum dw_edma_map_format {
    EDMA_MF_EDMA_LEGACY = 0x0,
    EDMA_MF_EDMA_UNROLL = 0x1,
    EDMA_MF_HDMA_COMPAT = 0x5,
    EDMA_MF_HDMA_NATIVE = 0x7,
};

/* Local copy of enum pci_barno from the driver, to avoid depending on kernel headers */
enum pci_barno {
    NO_BAR = -1,
    BAR_0,
    BAR_1,
    BAR_2,
    BAR_3,
    BAR_4,
    BAR_5,
};

/*
 * The Linux driver uses PCI_DEVICE_DATA(SYNOPSYS, EDDA, &snps_edda_data)
 * for the first entry in its pci_device_id table. The supplementary
 * source provides the explicit numeric Vendor/Device IDs here.
 */
#define PCIBASE_VENDOR_ID  0x16c3
#define PCIBASE_DEVICE_ID  0xedda
#define PCIBASE_CLASS_ID   PCI_CLASS_SYSTEM_DMA

/*
 * Structures mirroring the driver static hardware description.
 * These are structural only; no runtime logic is implemented here.
 */
struct dw_edma_block {
    enum pci_barno bar;
    off_t          off;
    size_t         sz;
};

struct dw_edma_pcie_data {
    /* eDMA registers location */
    struct dw_edma_block   rg;
    /* eDMA memory linked list location */
    struct dw_edma_block   ll_wr[EDMA_MAX_WR_CH];
    struct dw_edma_block   ll_rd[EDMA_MAX_RD_CH];
    /* eDMA memory data location */
    struct dw_edma_block   dt_wr[EDMA_MAX_WR_CH];
    struct dw_edma_block   dt_rd[EDMA_MAX_RD_CH];
    /* Other */
    enum dw_edma_map_format mf;
    uint8_t                 irqs;
    uint16_t                wr_ch_cnt;
    uint16_t                rd_ch_cnt;
};

struct device; /* forward declaration matching driver usage */

typedef uint64_t phys_addr_t; /* minimal typedef to satisfy compiler; no functional use here */

struct dw_edma_plat_ops {
    int (*irq_vector)(struct device *dev, unsigned int nr);
    uint64_t (*pci_address)(struct device *dev, phys_addr_t cpu_addr);
};

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

    /* Simple MMIO register backing storage for BAR0 (regs) and BAR2 (others) */
    /* We model the register block for BAR0; BAR2 will be a plain MMIO window. */
    uint8_t *bar0_mem;
    hwaddr   bar0_size;

    /* Cached VSEC values (mirrors snps_edda_data defaults) */
    struct dw_edma_pcie_data vsec_cfg;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    /* The provided driver snippet does not yet show any interrupt/status
     * register usage for the eDMA core, so we intentionally leave this
     * empty. IRQ plumbing (MSI/MSI-X vector allocation) is handled by
     * the guest via pci_alloc_irq_vectors(), and QEMU will advertise
     * MSI capability via msi_init(). There are no MMIO-visible IRQ
     * status bits implemented here.
     */
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /* No dw-edma-core DMA register interface was provided, so we cannot
     * safely implement any DMA engine behavior. The generic dw-edma core
     * will only program the engine via MMIO registers which are not
     * specified here, so they are not modeled.
     */
}

/* Helpers for BAR0-backed MMIO register space */
G_GNUC_UNUSED static inline uint32_t pcibase_bar0_readl(PCIBaseState *s, hwaddr addr)
{
    if (!s->bar0_mem || addr + sizeof(uint32_t) > s->bar0_size) {
        return 0;
    }
    return ldl_le_p((void *)(s->bar0_mem + addr));
}

G_GNUC_UNUSED static inline void pcibase_bar0_writel(PCIBaseState *s, hwaddr addr, uint32_t val)
{
    if (!s->bar0_mem || addr + sizeof(uint32_t) > s->bar0_size) {
        return;
    }
    stl_le_p((void *)(s->bar0_mem + addr), val);
}

/* MMIO/PIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* The driver uses pcim_iomap_regions() on multiple BARs, but does not
     * access any specific eDMA registers in the provided snippet. We map
     * BAR0 as a generic MMIO space for the eDMA registers and BAR2 as an
     * MMIO window for linked list/data areas. Since no register offsets
     * are given, all reads return the content of backing storage (which
     * is zeroed on reset) or zero if out of range.
     */

    switch (size) {
    case 1:
    case 2:
    case 4:
    case 8:
        break;
    default:
        return 0;
    }

    /* Only BAR0 is backed by bar0_mem; other BARs are plain, read-as-zero
     * windows from QEMU's point of view.
     */

    if (addr < s->bar0_size && s->bar0_mem) {
        if (size == 1) {
            val = s->bar0_mem[addr];
        } else if (size == 2) {
            val = lduw_le_p((void *)(s->bar0_mem + addr));
        } else if (size == 4) {
            val = ldl_le_p((void *)(s->bar0_mem + addr));
        } else if (size == 8) {
            val = ldq_le_p((void *)(s->bar0_mem + addr));
        }
    } else {
        /* BAR2 and others: nothing modeled, read returns 0. */
        val = 0;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (size) {
    case 1:
    case 2:
    case 4:
    case 8:
        break;
    default:
        return;
    }

    if (addr < s->bar0_size && s->bar0_mem) {
        if (size == 1) {
            s->bar0_mem[addr] = (uint8_t)val;
        } else if (size == 2) {
            stw_le_p((void *)(s->bar0_mem + addr), (uint16_t)val);
        } else if (size == 4) {
            stl_le_p((void *)(s->bar0_mem + addr), (uint32_t)val);
        } else if (size == 8) {
            stq_le_p((void *)(s->bar0_mem + addr), (uint64_t)val);
        }
    } else {
        /* Writes to unmodeled BAR regions are ignored. */
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* The driver never uses I/O port space, so treat all PIO reads as zero. */
    PCIBaseState *s = opaque;
    (void)s;
    (void)addr;
    (void)size;

    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* The driver never uses I/O port space; ignore writes. */
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

    pci_device_reset(PCI_DEVICE(dev));

    /* Clear BAR0 backing storage to zero so that any eDMA core registers
     * start from a known reset state. We do not implement specific
     * register semantics as they are not present in the provided code.
     */
    if (s->bar0_mem && s->bar0_size) {
        memset(s->bar0_mem, 0, s->bar0_size);
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
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

static uint32_t pcibase_build_vsec_header(void)
{
    /* Build the Vendor-Specific Extended Capability header dword
     * as seen by pci_read_config_dword(pdev, vsec + PCI_VNDR_HEADER, &val)
     * in dw_edma_pcie_get_vsec_dma_data(). The macros PCI_VNDR_HEADER_* are
     * kernel macros; we cannot reuse them here, so we hardcode the bits.
     *
     * Layout of the first dword (PCIe spec):
     *  31:20  Next Capability Offset (0 for end of list)
     *  19:16  Capability Version (0)
     *  15:0   PCI Extended Capability ID = PCI_EXT_CAP_ID_VNDR (0x0B)
     */
    const uint16_t cap_id = PCI_EXT_CAP_ID_VNDR; /* 0x0B */
    uint32_t hdr = 0;

    hdr |= cap_id;                         /* bits 15:0 */
    hdr |= (0u << 16);                     /* version 0 */
    hdr |= (0u << 20);                     /* next cap pointer = 0 */

    return hdr;
}

static uint32_t pcibase_build_vsec_dw1(void)
{
    /* Second dword of VSEC header:
     *  31:16  Vendor ID (PCI_VENDOR_ID_SYNOPSYS)
     *  15:0   VSEC ID (DW_PCIE_VSEC_DMA_ID)
     */
    uint32_t val = 0;

    val |= (uint32_t)DW_PCIE_VSEC_DMA_ID; /* VSEC ID */
    val |= ((uint32_t)PCI_VENDOR_ID_SYNOPSYS) << 16;

    return val;
}

static uint32_t pcibase_build_vsec_dw2(void)
{
    /* Third dword of VSEC header:
     *  31:20  VSEC Length in dwords or bytes depending on spec
     *  19:16  VSEC Revision
     *  15:0   VSEC specific information (not used here)
     *
     * The Linux helper macros expect PCI_VNDR_HEADER_REV(val) == 0x00 and
     * PCI_VNDR_HEADER_LEN(val) == 0x18 (24 bytes). In the PCIe extended
     * capability space, this 16-bit length is stored starting at bit 4
     * (in dwords). However, the driver macro PCI_VNDR_HEADER_LEN(val)
     * simply extracts a byte/word, so we can place 0x18 directly into
     * bits 0-15 and 0 revision into (19:16).
     */
    uint32_t val = 0;

    val |= (0x18u & 0xFFFFu);  /* LEN = 0x18 */
    val |= (0u << 16);         /* REV = 0 */

    return val;
}

static uint32_t pcibase_build_vsec_map(const struct dw_edma_pcie_data *cfg)
{
    uint32_t val = 0;

    /* DW_PCIE_VSEC_DMA_BAR: GENMASK(10, 8) => bits 10:8 */
    uint32_t bar = (uint32_t)cfg->rg.bar & 0x7u;
    val |= (bar << 8);

    /* DW_PCIE_VSEC_DMA_MAP: GENMASK(2, 0) => bits 2:0 */
    val |= ((uint32_t)cfg->mf & 0x7u);

    return val;
}

static uint32_t pcibase_build_vsec_chcnt(const struct dw_edma_pcie_data *cfg)
{
    uint32_t val = 0;

    /* DW_PCIE_VSEC_DMA_WR_CH: GENMASK(9, 0) => bits 9:0 */
    val |= ((uint32_t)cfg->wr_ch_cnt & 0x3FFu);

    /* DW_PCIE_VSEC_DMA_RD_CH: GENMASK(25, 16) => bits 25:16 */
    val |= ((uint32_t)cfg->rd_ch_cnt & 0x3FFu) << 16;

    return val;
}

static void pcibase_populate_vsec(PCIBaseState *s, PCIDevice *pdev)
{
    uint8_t *cfg = pdev->config;
    uint32_t val;

    /* vsec offset is fixed at PCIBASE_VSEC_OFFSET */
    hwaddr off = PCIBASE_VSEC_OFFSET;

    /* Header dword 0 */
    val = pcibase_build_vsec_header();
    pci_set_long(cfg + off, val);

    /* Header dword 1 (Vendor + ID) */
    val = pcibase_build_vsec_dw1();
    pci_set_long(cfg + off + 4, val);

    /* Header dword 2 (REV/LEN) */
    val = pcibase_build_vsec_dw2();
    pci_set_long(cfg + off + 8, val);

    /* At offset vsec + 0x8 (which is off+8) the driver reads "val" and
     * applies FIELD_GET(DW_PCIE_VSEC_DMA_MAP, val) and
     * FIELD_GET(DW_PCIE_VSEC_DMA_BAR, val). We have already filled this in
     * header dword 2. However, in the Linux code snippet, the first
     * pci_read_config_dword after the header uses vsec + 0x8 again. To
     * satisfy that, we overwrite dword 2 with the MAP/BAR encoding.
     */
    val = pcibase_build_vsec_map(&s->vsec_cfg);
    pci_set_long(cfg + off + 8, val);

    /* At offset vsec + 0x0c: WR/RD channel counts */
    val = pcibase_build_vsec_chcnt(&s->vsec_cfg);
    pci_set_long(cfg + off + 0x0c, val);

    /* At offsets vsec + 0x10 and vsec + 0x14: lower and upper 32 bits
     * of the 64-bit register offset (pdata->rg.off).
     */
    uint64_t off64 = (uint64_t)s->vsec_cfg.rg.off;
    uint32_t lo = (uint32_t)(off64 & 0xFFFFFFFFu);
    uint32_t hi = (uint32_t)(off64 >> 32);

    pci_set_long(cfg + off + 0x10, lo);
    pci_set_long(cfg + off + 0x14, hi);
}

static void pcibase_init_vsec_defaults(PCIBaseState *s)
{
    struct dw_edma_pcie_data *cfg = &s->vsec_cfg;

    /* Defaults mirror snps_edda_data from the driver snippet. */

    /* eDMA registers location */
    cfg->rg.bar = BAR_0;
    cfg->rg.off = 0x00001000; /* 4 Kbytes */
    cfg->rg.sz  = 0x00002000; /* 8 Kbytes */

    /* eDMA memory linked list location (we only need BAR indices and offsets) */
    memset(cfg->ll_wr, 0, sizeof(cfg->ll_wr));
    memset(cfg->ll_rd, 0, sizeof(cfg->ll_rd));
    memset(cfg->dt_wr, 0, sizeof(cfg->dt_wr));
    memset(cfg->dt_rd, 0, sizeof(cfg->dt_rd));

    /* Channel 0 WR */
    cfg->ll_wr[0].bar = BAR_2;
    cfg->ll_wr[0].off = 0x00000000;
    cfg->ll_wr[0].sz  = 0x00000800;
    /* Channel 1 WR */
    cfg->ll_wr[1].bar = BAR_2;
    cfg->ll_wr[1].off = 0x00200000;
    cfg->ll_wr[1].sz  = 0x00000800;

    /* Channel 0 RD */
    cfg->ll_rd[0].bar = BAR_2;
    cfg->ll_rd[0].off = 0x00400000;
    cfg->ll_rd[0].sz  = 0x00000800;
    /* Channel 1 RD */
    cfg->ll_rd[1].bar = BAR_2;
    cfg->ll_rd[1].off = 0x00600000;
    cfg->ll_rd[1].sz  = 0x00000800;

    /* Data WR */
    cfg->dt_wr[0].bar = BAR_2;
    cfg->dt_wr[0].off = 0x00800000;
    cfg->dt_wr[0].sz  = 0x00000800;

    cfg->dt_wr[1].bar = BAR_2;
    cfg->dt_wr[1].off = 0x00900000;
    cfg->dt_wr[1].sz  = 0x00000800;

    /* Data RD */
    cfg->dt_rd[0].bar = BAR_2;
    cfg->dt_rd[0].off = 0x00a00000;
    cfg->dt_rd[0].sz  = 0x00000800;

    cfg->dt_rd[1].bar = BAR_2;
    cfg->dt_rd[1].off = 0x00b00000;
    cfg->dt_rd[1].sz  = 0x00000800;

    /* Other fields */
    cfg->mf        = EDMA_MF_EDMA_UNROLL;
    cfg->irqs      = 1;
    cfg->wr_ch_cnt = 2;
    cfg->rd_ch_cnt = 2;
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Initialize default VSEC configuration mirroring snps_edda_data. */
    pcibase_init_vsec_defaults(s);

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCIBASE_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCIBASE_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCIBASE_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Advertise PCIe capability. */
    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* Add Vendor-Specific Extended Capability for DMA. */
    pcibase_populate_vsec(s, pdev);

    /* BAR Initialization */
    /* The driver maps BAR0 (regs) and BAR2 (LL/DT windows) via pcim_iomap_regions().
     * We therefore create BAR0 and BAR2 as MMIO regions. BAR0 must be at least as
     * large as rg.off + rg.sz so that reg_base = pcim_iomap_table()[BAR_0] + off
     * is valid. For simplicity we create:
     *   BAR0: 64 KiB MMIO (includes reg block at 0x1000 size 0x2000)
     *   BAR2: 32 MiB MMIO (covers ll/dt offsets up to 0x00b00000)
     */

    s->num_bars = 3;

    /* BAR0: MMIO */
    s->bar_info[0].index = 0;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    s->bar_info[0].size  = 0x10000; /* 64 KiB */
    s->bar_info[0].name  = "dw-edma-pcie-bar0";

    /* BAR1: not used by the driver */
    s->bar_info[1].index = 1;
    s->bar_info[1].type  = BAR_TYPE_NONE;
    s->bar_info[1].size  = 0;
    s->bar_info[1].name  = "unused";

    /* BAR2: MMIO window for LL/DT memory */
    s->bar_info[2].index = 2;
    s->bar_info[2].type  = BAR_TYPE_MMIO;
    s->bar_info[2].size  = 0x02000000; /* 32 MiB, covers up to 0x00b00000 + 0x800 */
    s->bar_info[2].name  = "dw-edma-pcie-bar2";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Allocate bar0 backing storage for MMIO register emulation. */
    s->bar0_size = s->bar_info[0].size;
    if (s->bar0_size) {
        s->bar0_mem = g_malloc0(s->bar0_size);
    }

    /* Initialize MSI support since the driver uses pci_alloc_irq_vectors()
     * with PCI_IRQ_MSI | PCI_IRQ_MSIX. We provide MSI with a single vector.
     */
    if (msi_init(pdev, 0, s->vsec_cfg.irqs, true, false, errp) == 0) {
        s->has_msi = true;
    }

    /* MSI-X is not strictly required; we can skip explicit MSI-X table
     * creation and let the guest fall back to MSI if MSI-X is unavailable.
     */
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

    if (s->bar0_mem) {
        g_free(s->bar0_mem);
        s->bar0_mem = NULL;
        s->bar0_size = 0;
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "dw_edma_pcie_pci",
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
