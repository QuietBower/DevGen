/*
 * Intel VPU PCI Device QEMU Model - Behavioral Implementation (Phase 2)
 * Generated based on Linux driver static information.
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

#define TYPE_PCIBASE_DEVICE "intel_vpu_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/*
 * From ivpu_drv.c:
 *   static struct pci_device_id ivpu_pci_ids[] = {
 *       { PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_MTL) },
 *       ...
 *   };
 * Rule: use FIRST entry: vendor = PCI_VENDOR_ID_INTEL, device = PCI_DEVICE_ID_MTL (0x7d1d).
 */
#define IVPU_PCI_VENDOR_ID   PCI_VENDOR_ID_INTEL
#define IVPU_PCI_DEVICE_ID   0x7d1d

/* PCI class explicitly defined in supplementary driver source. */
#define IVPU_PCI_CLASS_ID    0x0380

/* Minimal subset of register offsets explicitly defined in driver snippets.
 * These belong to BAR4 (regb) and BAR0 (regv) as per driver macros.
 */
#define VPU_HW_BTRS_MTL_VPU_STATUS              0x00000044u
#define VPU_HW_BTRS_LNL_VPU_STATUS              0x00000154u
#define VPU_HW_BTRS_MTL_VPU_TELEMETRY_OFFSET    0x00000080u
#define VPU_HW_BTRS_LNL_VPU_TELEMETRY_OFFSET    0x00000168u
#define VPU_HW_BTRS_MTL_VPU_TELEMETRY_ENABLE    0x00000088u
#define VPU_HW_BTRS_LNL_VPU_TELEMETRY_ENABLE    0x00000170u
#define VPU_HW_BTRS_MTL_VPU_TELEMETRY_SIZE      0x00000084u
#define VPU_HW_BTRS_LNL_VPU_TELEMETRY_SIZE      0x0000016cu

#define VPU_37XX_HOST_SS_TIM_IPC_FIFO           0x000200f4u
#define VPU_40XX_HOST_SS_TIM_IPC_FIFO           0x000200f4u

/* New explicit BAR4 / MMU / IRQ-related register offsets from supplementary source */
#define VPU_HW_BTRS_MTL_INTERRUPT_STAT          0x00000004u
#define VPU_HW_BTRS_LNL_INTERRUPT_STAT          0x00000000u
#define VPU_HW_BTRS_MTL_LOCAL_INT_MASK          0x00000030u
#define VPU_HW_BTRS_MTL_GLOBAL_INT_MASK         0x00000034u
#define VPU_HW_BTRS_LNL_LOCAL_INT_MASK          0x00000004u
#define VPU_HW_BTRS_LNL_GLOBAL_INT_MASK         0x00000008u
#define VPU_HW_BTRS_LNL_PORT_ARBITRATION_WEIGHTS        0x00000054u
#define VPU_HW_BTRS_LNL_PORT_ARBITRATION_WEIGHTS_ATS    0x00000024u

#define IVPU_MMU_REG_IDR0                       0x00200000u
#define IVPU_MMU_REG_IDR1                       0x00200004u
#define IVPU_MMU_REG_IDR3                       0x0020000cu
#define IVPU_MMU_REG_IDR5                       0x00200014u
#define IVPU_MMU_REG_EVTQ_PROD_SEC              (0x002000a8u + (64 * 1024))

#define VPU_37XX_HOST_SS_ICB_STATUS_0           0x00010210u
#define VPU_40XX_HOST_SS_ICB_STATUS_0           0x00010210u
#define VPU_37XX_HOST_SS_FW_SOC_IRQ_EN          0x00000170u
#define VPU_40XX_HOST_SS_FW_SOC_IRQ_EN          0x00000170u
#define VPU_37XX_HOST_SS_ICB_ENABLE_0           0x00010240u
#define VPU_40XX_HOST_SS_ICB_ENABLE_0           0x00010240u

/* SIMICS/MMU IDR reference values used in driver to identify platform */
#define IVPU_MMU_IDR0_REF               0x080f3e0f
#define IVPU_MMU_IDR0_REF_SIMICS        0x080f3e1f
#define IVPU_MMU_IDR1_REF               0x0e739d18
#define IVPU_MMU_IDR3_REF               0x0000003c
#define IVPU_MMU_IDR5_REF               0x00040070
#define IVPU_MMU_IDR5_REF_SIMICS        0x00000075
#define IVPU_MMU_IDR5_REF_FPGA          0x00800075

/* Simplified constants for generation selection based on device id,
 * mirroring ivpu_hw_btrs_gen() and ivpu_hw_ip_gen() logic.
 */
#define PCI_DEVICE_ID_MTL  0x7d1d
#define PCI_DEVICE_ID_ARL  0x7d20
#define PCI_DEVICE_ID_LNL  0x7750
#define PCI_DEVICE_ID_PTL_P 0x7d2a
#define PCI_DEVICE_ID_WCL  0x7d2b
#define PCI_DEVICE_ID_NVL  0x7d2c

#define IVPU_HW_BTRS_MTL 1
#define IVPU_HW_BTRS_LNL 2

#define IVPU_HW_IP_37XX 37
#define IVPU_HW_IP_40XX 40
#define IVPU_HW_IP_50XX 50
#define IVPU_HW_IP_60XX 60

/* IPC alignment requirement directly from driver define */
#define IVPU_IPC_ALIGNMENT 64

/* Helper enums/flags for VPU_STATUS fields are not provided; we keep raw
 * register semantics as opaque 32-bit values.
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

    /*
     * Very simple register storage backing BARs.
     * No real layout is known, so we implement a flat byte array
     * per BAR used by the driver (0 and 4).
     */
    uint8_t *bar_storage[6];

    /* Synthetic status for telemetry used via REGB/REGV accessors. */
    uint32_t btrs_status_mtl;
    uint32_t btrs_status_lnl;
    uint32_t telemetry_offset_mtl;
    uint32_t telemetry_offset_lnl;
    uint32_t telemetry_enable_mtl;
    uint32_t telemetry_enable_lnl;
    uint32_t telemetry_size_mtl;
    uint32_t telemetry_size_lnl;
    uint32_t ipc_rx_count;

    /* New synthetic registers / state derived from driver usage */
    uint32_t btrs_mtl_interrupt_stat;
    uint32_t btrs_lnl_interrupt_stat;
    uint32_t btrs_mtl_local_int_mask;
    uint32_t btrs_mtl_global_int_mask;
    uint32_t btrs_lnl_local_int_mask;
    uint32_t btrs_lnl_global_int_mask;
    uint32_t btrs_lnl_port_arb_weights;
    uint32_t btrs_lnl_port_arb_weights_ats;

    uint32_t mmu_idr0;
    uint32_t mmu_idr1;
    uint32_t mmu_idr3;
    uint32_t mmu_idr5;
    uint32_t mmu_evtq_prod_sec;

    uint32_t icb_status0;
    uint32_t fw_soc_irq_en;
    uint32_t icb_enable0;
};

/* Internal helper for status-triggered signaling. Currently minimal. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /*
     * The driver programs various interrupt mask registers but
     * the exact interrupt status generation is not specified in
     * provided sources. To keep behavior simple and deterministic
     * we do not autonomously generate interrupts here.
     */
    (void)pdev;
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /*
     * DMA is used extensively by the real device, but there are no
     * descriptor formats or DMA control registers in the snippets
     * provided so far. Without explicit register-level information we
     * cannot model any bus mastering. This helper remains a no-op.
     */
    (void)pdev;
    (void)is_write;
}

/* Helper: resolve BAR0 vs BAR4 from a global BAR address, matching the
 * simplified layout used in realize(): BAR0 at [0, size0), BAR4
 * immediately following at [size0, size0+size4).
 */
static inline bool pcibase_resolve_bar(PCIBaseState *s, hwaddr addr,
                                       int *bar_index, hwaddr *bar_offset)
{
    hwaddr size0 = s->bar_info[0].size;
    hwaddr size4 = s->bar_info[4].size;

    if (addr < size0) {
        *bar_index = 0;
        *bar_offset = addr;
        return true;
    }

    if (addr < size0 + size4 && size4) {
        *bar_index = 4;
        *bar_offset = addr - size0;
        return true;
    }

    return false;
}

/* MMIO/PIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    int bar = -1;
    hwaddr offset = 0;

    if (!pcibase_resolve_bar(s, addr, &bar, &offset)) {
        return 0;
    }

    uint8_t *storage = s->bar_storage[bar];
    if (!storage) {
        return 0;
    }

    if (offset + size > s->bar_info[bar].size) {
        return 0;
    }

    /* Implement synthetic behavior for a few registers that are
     * read by helper functions in the provided driver snippets.
     */
    if (bar == 4) {
        /* BAR4: buttress/telemetry/MMU/IRQ registers accessed via REGB_RD32. */
        switch (offset) {
        case VPU_HW_BTRS_MTL_VPU_STATUS:
            val = s->btrs_status_mtl;
            return val;
        case VPU_HW_BTRS_LNL_VPU_STATUS:
            val = s->btrs_status_lnl;
            return val;
        case VPU_HW_BTRS_MTL_VPU_TELEMETRY_OFFSET:
            val = s->telemetry_offset_mtl;
            return val;
        case VPU_HW_BTRS_LNL_VPU_TELEMETRY_OFFSET:
            val = s->telemetry_offset_lnl;
            return val;
        case VPU_HW_BTRS_MTL_VPU_TELEMETRY_ENABLE:
            val = s->telemetry_enable_mtl;
            return val;
        case VPU_HW_BTRS_LNL_VPU_TELEMETRY_ENABLE:
            val = s->telemetry_enable_lnl;
            return val;
        case VPU_HW_BTRS_MTL_VPU_TELEMETRY_SIZE:
            val = s->telemetry_size_mtl;
            return val;
        case VPU_HW_BTRS_LNL_VPU_TELEMETRY_SIZE:
            val = s->telemetry_size_lnl;
            return val;
        case VPU_HW_BTRS_MTL_INTERRUPT_STAT:
            /* driver reads after writing; return stored value */
            val = s->btrs_mtl_interrupt_stat;
            return val;
        case VPU_HW_BTRS_LNL_INTERRUPT_STAT:
            val = s->btrs_lnl_interrupt_stat;
            return val;
        case VPU_HW_BTRS_MTL_LOCAL_INT_MASK:
            val = s->btrs_mtl_local_int_mask;
            return val;
        case VPU_HW_BTRS_MTL_GLOBAL_INT_MASK:
            val = s->btrs_mtl_global_int_mask;
            return val;
        case IVPU_MMU_REG_IDR0:
            val = s->mmu_idr0;
            return val;
        case IVPU_MMU_REG_IDR1:
            val = s->mmu_idr1;
            return val;
        case IVPU_MMU_REG_IDR3:
            val = s->mmu_idr3;
            return val;
        case IVPU_MMU_REG_IDR5:
            val = s->mmu_idr5;
            return val;
        case IVPU_MMU_REG_EVTQ_PROD_SEC:
            val = s->mmu_evtq_prod_sec;
            return val;
        case VPU_37XX_HOST_SS_ICB_STATUS_0:
            val = s->icb_status0;
            return val;
        /*
         * NOTE: VPU_37XX_HOST_SS_FW_SOC_IRQ_EN and
         * VPU_HW_BTRS_LNL_LOCAL_INT_MASK share the same
         * numeric offset (0x00000170u vs 0x00000004u adjusted
         * by different base macros in real hardware). In this
         * simplified flat BAR model that does not incorporate
         * per-block base offsets, treating both as separate
         * switch cases would create duplicate case values.
         *
         * To preserve compile correctness without altering any
         * observable behavior (the driver does not rely on the
         * value read back from FW_SOC_IRQ_EN in the provided
         * context), we omit a dedicated case label for
         * VPU_37XX_HOST_SS_FW_SOC_IRQ_EN here and let it fall
         * through to the generic storage-backed read logic.
         */
        case VPU_37XX_HOST_SS_ICB_ENABLE_0:
            val = s->icb_enable0;
            return val;
        default:
            break;
        }
    } else if (bar == 0) {
        /* BAR0: IP side; explicit read side-effects are not shown for
         * TIM_IPC_FIFO or MMU IDR* registers beyond comparisons, so
         * we simply return the storage-backed values.
         */
    }

    switch (size) {
    case 1:
        val = storage[offset];
        break;
    case 2:
        val = le16_to_cpu(*(uint16_t *)(storage + offset));
        break;
    case 4:
        val = le32_to_cpu(*(uint32_t *)(storage + offset));
        break;
    case 8:
        val = le64_to_cpu(*(uint64_t *)(storage + offset));
        break;
    default:
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    int bar = -1;
    hwaddr offset = 0;

    if (!pcibase_resolve_bar(s, addr, &bar, &offset)) {
        return;
    }

    uint8_t *storage = s->bar_storage[bar];
    if (!storage) {
        return;
    }

    if (offset + size > s->bar_info[bar].size) {
        return;
    }

    /* Implement minimal semantics for explicitly written registers. */
    if (bar == 4) {
        switch (offset) {
        case VPU_HW_BTRS_MTL_VPU_STATUS:
            /* Opaque 32-bit register preserving writes. */
            s->btrs_status_mtl = (uint32_t)val;
            break;
        case VPU_HW_BTRS_LNL_VPU_STATUS:
            s->btrs_status_lnl = (uint32_t)val;
            break;
        case VPU_HW_BTRS_MTL_VPU_TELEMETRY_OFFSET:
            s->telemetry_offset_mtl = (uint32_t)val;
            break;
        case VPU_HW_BTRS_LNL_VPU_TELEMETRY_OFFSET:
            s->telemetry_offset_lnl = (uint32_t)val;
            break;
        case VPU_HW_BTRS_MTL_VPU_TELEMETRY_ENABLE:
            s->telemetry_enable_mtl = (uint32_t)val;
            break;
        case VPU_HW_BTRS_LNL_VPU_TELEMETRY_ENABLE:
            s->telemetry_enable_lnl = (uint32_t)val;
            break;
        case VPU_HW_BTRS_MTL_VPU_TELEMETRY_SIZE:
            s->telemetry_size_mtl = (uint32_t)val;
            break;
        case VPU_HW_BTRS_LNL_VPU_TELEMETRY_SIZE:
            s->telemetry_size_lnl = (uint32_t)val;
            break;
        case VPU_HW_BTRS_MTL_INTERRUPT_STAT:
            /* ivpu_hw_btrs_irqs_clear_with_0_mtl() tests behavior:
             * - first write BTRS_MTL_ALL_IRQ_MASK and read back; if
             *   register still equals mask then it concludes that
             *   writing ones does not clear and writes 0.
             * We therefore store the written value verbatim to make
             * the equality check succeed.
             */
            s->btrs_mtl_interrupt_stat = (uint32_t)val;
            break;
        case VPU_HW_BTRS_LNL_INTERRUPT_STAT:
            s->btrs_lnl_interrupt_stat = (uint32_t)val;
            break;
        case VPU_HW_BTRS_MTL_LOCAL_INT_MASK:
            s->btrs_mtl_local_int_mask = (uint32_t)val;
            break;
        case VPU_HW_BTRS_MTL_GLOBAL_INT_MASK:
            s->btrs_mtl_global_int_mask = (uint32_t)val;
            break;
        case IVPU_MMU_REG_IDR0:
            /* IDR registers are read-only in HW, but driver may not write them;
             * to be safe and simple, just store the value if writes occur.
             */
            s->mmu_idr0 = (uint32_t)val;
            break;
        case IVPU_MMU_REG_IDR1:
            s->mmu_idr1 = (uint32_t)val;
            break;
        case IVPU_MMU_REG_IDR3:
            s->mmu_idr3 = (uint32_t)val;
            break;
        case IVPU_MMU_REG_IDR5:
            s->mmu_idr5 = (uint32_t)val;
            break;
        case IVPU_MMU_REG_EVTQ_PROD_SEC:
            s->mmu_evtq_prod_sec = (uint32_t)val;
            break;
        case VPU_37XX_HOST_SS_ICB_STATUS_0:
            s->icb_status0 = (uint32_t)val;
            break;
        /* See read-side comment: to avoid duplicate case values and
         * maintain compile correctness without changing observable
         * behavior, we let writes to VPU_37XX_HOST_SS_FW_SOC_IRQ_EN
         * fall through to the generic storage-backed write path.
         */
        case VPU_37XX_HOST_SS_ICB_ENABLE_0:
            s->icb_enable0 = (uint32_t)val;
            break;
        default:
            break;
        }
    } else if (bar == 0) {
        /* TIM_IPC_FIFO write is used for IPC doorbells; driver does not
         * read it back or depend on side effects visible in MMIO, so we
         * just store the value for potential debug visibility.
         */
        if (offset == VPU_37XX_HOST_SS_TIM_IPC_FIFO ||
            offset == VPU_40XX_HOST_SS_TIM_IPC_FIFO) {
            /* No side effects implemented. */
        }
    }

    switch (size) {
    case 1:
        storage[offset] = (uint8_t)val;
        break;
    case 2:
        *(uint16_t *)(storage + offset) = cpu_to_le16((uint16_t)val);
        break;
    case 4:
        *(uint32_t *)(storage + offset) = cpu_to_le32((uint32_t)val);
        break;
    case 8:
        *(uint64_t *)(storage + offset) = cpu_to_le64((uint64_t)val);
        break;
    default:
        break;
    }

    /* No IRQ or DMA side-effects are implemented due to lack of details. */
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* No legacy PIO usage indicated in driver snippet. */
    (void)s;
    (void)addr;
    (void)size;

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* No legacy PIO usage indicated in driver snippet. */
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

    /* Clear BAR-backed storage on reset to power-on defaults (zeros). */
    for (int i = 0; i < 6; i++) {
        if (s->bar_storage[i]) {
            memset(s->bar_storage[i], 0, s->bar_info[i].size);
        }
    }

    /* Reset synthetic status fields as well. */
    s->btrs_status_mtl = 0;
    s->btrs_status_lnl = 0;
    s->telemetry_offset_mtl = 0;
    s->telemetry_offset_lnl = 0;
    s->telemetry_enable_mtl = 0;
    s->telemetry_enable_lnl = 0;
    s->telemetry_size_mtl = 0;
    s->telemetry_size_lnl = 0;
    s->ipc_rx_count = 0;

    s->btrs_mtl_interrupt_stat = 0;
    s->btrs_lnl_interrupt_stat = 0;
    s->btrs_mtl_local_int_mask = 0;
    s->btrs_mtl_global_int_mask = 0;
    s->btrs_lnl_local_int_mask = 0;
    s->btrs_lnl_global_int_mask = 0;
    s->btrs_lnl_port_arb_weights = 0;
    s->btrs_lnl_port_arb_weights_ats = 0;

    /* Default MMU IDR registers to reference values so that driver
     * comparisons and platform detection behave consistently.
     */
    s->mmu_idr0 = IVPU_MMU_IDR0_REF;
    s->mmu_idr1 = IVPU_MMU_IDR1_REF;
    s->mmu_idr3 = IVPU_MMU_IDR3_REF;
    s->mmu_idr5 = IVPU_MMU_IDR5_REF;

    s->mmu_evtq_prod_sec = 0;
    s->icb_status0 = 0;
    s->fw_soc_irq_en = 0;
    s->icb_enable0 = 0;
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
        /* Allocate backing storage for this BAR */
        s->bar_storage[bi->index] = g_malloc0(aligned_size);
        bi->size = aligned_size;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  IVPU_PCI_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  IVPU_PCI_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, IVPU_PCI_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Enable PCIe capability and basic PM capability as in skeleton */
    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR Initialization
     * Driver explicitly uses BAR0 (regv) and BAR4 (regb).
     * No exact sizes are provided; keep generic power-of-two sizes.
     */
    s->num_bars = 2;

    /* BAR0: main register window */
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 1 * MiB; /* Generic size; sufficient for register space */
    s->bar_info[0].name = "ivpu-bar0-regv";

    /* BAR4: secondary register window */
    s->bar_info[4].index = 4;
    s->bar_info[4].type = BAR_TYPE_MMIO;
    s->bar_info[4].size = 64 * KiB; /* Smaller generic size */
    s->bar_info[4].name = "ivpu-bar4-regb";

    for (int i = 0; i < 6; i++) {
        s->bar_storage[i] = NULL;
    }

    pcibase_register_bar(pdev, s, &s->bar_info[0], errp);
    pcibase_register_bar(pdev, s, &s->bar_info[4], errp);

    /* Initialize minimal synthetic HW status used by helper functions. */
    s->btrs_status_mtl = 0;
    s->btrs_status_lnl = 0;
    s->telemetry_offset_mtl = 0;
    s->telemetry_offset_lnl = 0;
    s->telemetry_enable_mtl = 0;
    s->telemetry_enable_lnl = 0;
    s->telemetry_size_mtl = 0;
    s->telemetry_size_lnl = 0;
    s->ipc_rx_count = 0;

    s->btrs_mtl_interrupt_stat = 0;
    s->btrs_lnl_interrupt_stat = 0;
    s->btrs_mtl_local_int_mask = 0;
    s->btrs_mtl_global_int_mask = 0;
    s->btrs_lnl_local_int_mask = 0;
    s->btrs_lnl_global_int_mask = 0;
    s->btrs_lnl_port_arb_weights = 0;
    s->btrs_lnl_port_arb_weights_ats = 0;

    /* Seed MMU IDR registers with reference values so that driver
     * validation logic using IVPU_MMU_IDR*_REF macros succeeds.
     */
    s->mmu_idr0 = IVPU_MMU_IDR0_REF;
    s->mmu_idr1 = IVPU_MMU_IDR1_REF;
    s->mmu_idr3 = IVPU_MMU_IDR3_REF;
    s->mmu_idr5 = IVPU_MMU_IDR5_REF;

    s->mmu_evtq_prod_sec = 0;
    s->icb_status0 = 0;
    s->fw_soc_irq_en = 0;
    s->icb_enable0 = 0;

    /* Seed BAR4 telemetry and related registers with zero; driver accepts
     * zeroed defaults and may configure them later via explicit writes.
     */
    if (s->bar_storage[4]) {
        memset(s->bar_storage[4], 0, s->bar_info[4].size);
    }

    /* MSI/MSI-X: driver calls pci_alloc_irq_vectors() with MSI | MSIX. */
    /* Enable a single MSI vector; MSIX left unimplemented. */
    Error *local_err = NULL;
    if (msi_init(pdev, 0, 1, true, false, &local_err) == 0) {
        s->has_msi = true;
    } else {
        s->has_msi = false;
        if (local_err) {
            error_propagate(errp, local_err);
        }
    }

    s->has_msix = false;
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

    for (int i = 0; i < 6; i++) {
        if (s->bar_storage[i]) {
            g_free(s->bar_storage[i]);
            s->bar_storage[i] = NULL;
        }
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "intel_vpu_pci",
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
