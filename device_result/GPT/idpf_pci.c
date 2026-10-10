/*
 * QEMU PCI device model for Intel IDPF driver (behavioral implementation)
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

/* Removed include for missing stub header to allow compilation */

#define TYPE_PCIBASE_DEVICE "idpf_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define IDPF_NETWORK_ETHERNET_PROGIF 0x01
#define IDPF_CLASS_NETWORK_ETHERNET_PROGIF ((PCI_CLASS_NETWORK_ETHERNET << 8) | IDPF_NETWORK_ETHERNET_PROGIF)
#define IDPF_VF_TEST_VAL 0xfeed0000u
#define VF_BASE 0x00006000
#define VF_ARQBAL (VF_BASE + 0x0C00)
#define IDPF_DEV_ID_VF 0x145C
#define IDPF_DEV_ID_PF 0x1452
#define IDPF_VF_RSTAT_REGION_SZ 2048
#define IDPF_VF_MBX_REGION_SZ 10240
#define VFGEN_RSTAT 0x00008800
#define IDPF_PF_RSTAT_REGION_SZ 2048
#define IDPF_PF_MBX_REGION_SZ 4096
#define IDPF_VC_XN_RING_LEN U8_MAX
#define IDPF_DIM_PROFILE_SLOTS 5
#define IDPF_MMIO_REG_NUM_STATIC 2
#define IDPF_DFLT_MBX_Q_LEN 64
#define IDPF_VC_XN_DEFAULT_TIMEOUT_MSEC (60 * 1000)
#define IDPF_ITR_IDX_SPACING(spacing, dflt) (spacing ? spacing : dflt)
#define VF_INT_DYN_CTLN_SWINT_TRIG_M BIT(VF_INT_DYN_CTLN_SWINT_TRIG_S)
#define VF_INT_DYN_CTLN_ITR_INDX_M GENMASK(4, 3)
#define VF_INT_DYN_CTLN_INTERVAL_S 5
#define VF_INT_ITRN_ADDR(_ITR, _reg_start, _itrn_indx_spacing) ((_reg_start) + ((_ITR) * (_itrn_indx_spacing)))
#define VF_INT_DYN_CTLN_INTENA_M BIT(VF_INT_DYN_CTLN_INTENA_S)
#define VF_INT_DYN_CTLN_WB_ON_ITR_M BIT(VF_INT_DYN_CTLN_WB_ON_ITR_S)
#define IDPF_MBX_Q_VEC 1
#define VF_INT_DYN_CTLN_INTENA_MSK_M BIT(VF_INT_DYN_CTLN_INTENA_MSK_S)
#define IDPF_NO_ITR_UPDATE_IDX 3
#define VF_INT_DYN_CTLN_SW_ITR_INDX_ENA_M BIT(VF_INT_DYN_CTLN_SW_ITR_INDX_ENA_S)
#define VF_INT_DYN_CTLN_ITR_INDX_S 3
#define VFGEN_RSTAT_VFR_STATE_M GENMASK(1, 0)
#define VF_ATQT (VF_BASE + 0x2400)
#define VF_ARQLEN (VF_BASE + 0x2000)
#define VF_ATQH (VF_BASE + 0x0400)
#define VF_ATQBAL (VF_BASE + 0x1C00)
#define VF_ARQLEN_ARQENABLE_M BIT(VF_ARQLEN_ARQENABLE_S)
#define VF_ARQLEN_ARQLEN_M GENMASK(9, 0)
#define VF_ARQH (VF_BASE + 0x1400)
#define VF_ATQLEN_ATQLEN_M GENMASK(9, 0)
#define IDPF_NUM_DFLT_MBX_Q 2
#define VF_ARQBAH (VF_BASE)
#define VF_ATQLEN_ATQENABLE_M BIT(VF_ATQLEN_ATQENABLE_S)
#define VF_ATQH_ATQH_M GENMASK(9, 0)
#define VF_ATQLEN (VF_BASE + 0x0800)
#define VF_ATQBAH (VF_BASE + 0x1800)
#define VF_ARQH_ARQH_M GENMASK(12, 0)
#define VF_ARQT (VF_BASE + 0x1000)
#define VF_INT_ICR0_ENA1_ADMINQ_M BIT(VF_INT_ICR0_ENA1_ADMINQ_S)
#define VF_INT_DYN_CTL0_ITR_INDX_M GENMASK(4, 3)
#define VF_INT_DYN_CTL0_INTENA_M BIT(VF_INT_DYN_CTL0_INTENA_S)
#define VF_INT_ICR0_ENA1 0x00005000
#define IDPF_MAX_BUFQS_PER_RXQ_GRP 2
#define IDPF_CTLQ_FLAG_DD BIT(IDPF_CTLQ_FLAG_DD_S)
#define IDPF_VMVF_TYPE_VM 1
#define IDPF_HOST_ID_MASK 0x7
#define IDPF_INDIRECT_CTX_SIZE 8
#define IDPF_DIRECT_CTX_SIZE 16
#define IDPF_VMVF_TYPE_PF 2
#define IDPF_VMVF_TYPE_VF 0
#define IDPF_VC_XN_SALT_M GENMASK(15, 8)
#define IDPF_VC_XN_IDX_M GENMASK(7, 0)
#define VF_INT_DYN_CTLN_SWINT_TRIG_S 2
#define VF_INT_DYN_CTLN_INTENA_S 0
#define VF_INT_DYN_CTLN_WB_ON_ITR_S 30
#define VF_INT_DYN_CTLN_INTENA_MSK_S 31
#define VF_INT_DYN_CTLN_SW_ITR_INDX_ENA_S 24
#define VF_ARQLEN_ARQENABLE_S 31
#define VF_ATQLEN_ATQENABLE_S 31
#define IDPF_CTLQ_MAX_BUF_LEN 4096
#define VF_INT_ICR0_ENA1_ADMINQ_S 30
#define VF_INT_DYN_CTL0_INTENA_S 0
#define IDPF_LARGE_MAX_Q 256
#define IDPF_TX_CTX_L2TAG2_M GENMASK_ULL(47, 32)
#define IDPF_TX_CTX_DTYPE_M GENMASK_ULL(3, 0)
#define IDPF_TX_CTX_TSYN_REG_M GENMASK_ULL(47, 30)
#define IDPF_TX_CTX_MSS_M GENMASK_ULL(50, 63)
#define IDPF_TX_CTX_CMD_M GENMASK_ULL(15, 4)
#define IDPF_NO_FREE_SLOT 0xffff
#define IDPF_CTLQ_FLAG_DD_S 0
#define IDPF_CTLQ_DESC_PF_ID_M (0x1F << IDPF_CTLQ_DESC_PF_ID_S)
#define IDPF_CTLQ_DESC_VF_ID_M (0x7FF << IDPF_CTLQ_DESC_VF_ID_S)
#define IDPF_CTLQ_DESC_PF_ID_S 11
#define IDPF_CTLQ_DESC_VF_ID_S 0
#define IDPF_CTLQ_FLAG_HOST_ID_S 13
#define IDPF_CTLQ_FLAG_RD BIT(IDPF_CTLQ_FLAG_RD_S)
#define IDPF_CTLQ_FLAG_BUF BIT(IDPF_CTLQ_FLAG_BUF_S)
#define IDPF_NUM_FILTERS_PER_MSG 20
#define IDPF_NUMQ_PER_CHUNK 1
#define IDPF_WAIT_FOR_MARKER_TIMEO 500
#define IDPF_TXD_COMPLT_SW_MARKER 5
#define IDPF_TXD_COMPLQ_QID_M GENMASK_ULL(9, 0)
#define IDPF_TXD_COMPLQ_GEN_M BIT_ULL(IDPF_TXD_COMPLQ_GEN_S)
#define IDPF_TXD_COMPLQ_COMPL_TYPE_M GENMASK_ULL(13, 11)
#define IDPF_TXD_COMPLQ_GEN_S 15
#define IDPF_CTLQ_DESC_VF_ID_S 0
#define IDPF_CTLQ_DESC_VF_ID_M (0x7FF << IDPF_CTLQ_DESC_VF_ID_S)
#define IDPF_CTLQ_DESC_PF_ID_S 11
#define IDPF_CTLQ_DESC_PF_ID_M (0x1F << IDPF_CTLQ_DESC_PF_ID_S)
#define IDPF_CTLQ_FLAG_RD_S 10
#define IDPF_CTLQ_FLAG_BUF_S 12
#define IDPF_NUM_DFLT_MBX_Q 2
#define IDPF_MIN_Q_VEC 1
#define IDPF_CTLQ_MAX_BUF_LEN 4096
#define IDPF_TXD_FLEX_FLOW_RXR BIT(14)
#define IDPF_TXD_FLOW_SCH_HORIZON_OVERFLOW_M BIT(7)
#define IDPF_TXD_FLEX_FLOW_CMD_RE BIT(7)
#define IDPF_TXD_FLEX_FLOW_CMD_EOP BIT(5)
#define IDPF_TXD_FLEX_FLOW_CMD_CS_EN BIT(6)
#define IDPF_TXD_FLEX_FLOW_DTYPE_M GENMASK(4, 0)
#define IDPF_TXD_FLEX_FLOW_BUFSIZE_M GENMASK(13, 0)
#define IDPF_TXD_FLEX_TSO_CTX_FLEX_S 24
#define IDPF_TXD_FLEX_CTX_TLEN_M GENMASK(17, 0)
#define IDPF_TXD_FLEX_CTX_MSS_RT_M GENMASK(13, 0)
#define IDPF_FLEX_TXD_QW1_CMD_M GENMASK(15, 5)
#define IDPF_FLEX_TXD_QW1_DTYPE_M GENMASK(4, 0)
#define IDPF_FLEX_TXD_QW1_DTYPE_S 0
#define IDPF_FLEX_TXD_QW1_CMD_S 5

#define PCI_VENDOR_ID_INTEL 0x8086
#define PCI_CLASS_NETWORK_ETHERNET 0x0200
#define BIT(nr) (1UL << (nr))
#define GENMASK(h, l) (((~0UL) - (1UL << (l)) + 1) & (~0UL >> (BITS_PER_LONG - 1 - (h))))
#define GENMASK_ULL(h, l) (((~0ULL) - (1ULL << (l)) + 1) & (~0ULL >> (BITS_PER_LONG_LONG - 1 - (h))))
#define BIT_ULL(nr) (1ULL << (nr))
#define U8_MAX ((uint8_t)~0U)
/* BITS_PER_LONG is already defined by QEMU; do not redefine here */
#define BITS_PER_LONG_LONG 64

#define PCIBASE_VENDOR_ID PCI_VENDOR_ID_INTEL
#define PCIBASE_DEVICE_ID IDPF_DEV_ID_PF
#define PCIBASE_CLASS_ID IDPF_CLASS_NETWORK_ETHERNET_PROGIF


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
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    struct {
        uint32_t vfgen_rstat;
        uint32_t vf_arqbal;
        uint32_t vf_atqbal;
        uint32_t vf_arqbah;
        uint32_t vf_atqbah;
        uint32_t vf_arqlen;
        uint32_t vf_atqlen;
        uint32_t vf_arqh;
        uint32_t vf_atqh;
        uint32_t vf_arqt;
        uint32_t vf_atqt;
        uint32_t vf_int_icr0_ena1;
        uint32_t vf_int_dyn_ctl0;
    } regs;

    /* DMA Context */
    /* No explicit DMA register layout is defined in this file */

    /* Operational status flags */
    uint32_t status_flags;

    /* State used to handle reset sequences */
    uint32_t reset_state;

    /* Power management state (D0-D3) */
    uint8_t pm_state;

    /* Additional device information placeholder */
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* For now, we implement a simple level-triggered interrupt based on intr_status & ~intr_mask */
    uint32_t pending = s->intr_status & ~s->intr_mask;

    if (pending) {
        if (msix_enabled(pdev)) {
            /* Notify vector 0 by default; the real driver will set up MSIX later. */
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
 * Currently the provided driver snippet does not expose any explicit
 * device-initiated DMA register behavior, so this is left empty.
 */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    (void)pdev;
    (void)is_write;
}

/* Translate BAR0-relative MMIO offset into internal register shadows */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val32 = 0;

    /* Implement only 32-bit accesses for known registers; other sizes return 0 */
    if (size != 4) {
        return 0;
    }

    switch (addr) {
    case VF_ARQBAL:
        val32 = s->regs.vf_arqbal;
        break;
    case VF_ATQBAL:
        val32 = s->regs.vf_atqbal;
        break;
    case VF_ARQBAH:
        val32 = s->regs.vf_arqbah;
        break;
    case VF_ATQBAH:
        val32 = s->regs.vf_atqbah;
        break;
    case VF_ARQLEN:
        val32 = s->regs.vf_arqlen;
        break;
    case VF_ATQLEN:
        val32 = s->regs.vf_atqlen;
        break;
    case VF_ARQH:
        val32 = s->regs.vf_arqh;
        break;
    case VF_ATQH:
        val32 = s->regs.vf_atqh;
        break;
    case VF_ARQT:
        val32 = s->regs.vf_arqt;
        break;
    case VF_ATQT:
        val32 = s->regs.vf_atqt;
        break;
    case VF_INT_ICR0_ENA1:
        val32 = s->regs.vf_int_icr0_ena1;
        break;
    case VFGEN_RSTAT:
        /* Reset status region: expose shadow value */
        val32 = s->regs.vfgen_rstat;
        break;
    default:
        /* Unimplemented register; return 0 */
        val32 = 0;
        break;
    }

    return val32;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t v32 = (uint32_t)val;

    if (size != 4) {
        return;
    }

    switch (addr) {
    case VF_ARQBAL:
        s->regs.vf_arqbal = v32;
        break;
    case VF_ATQBAL:
        s->regs.vf_atqbal = v32;
        break;
    case VF_ARQBAH:
        s->regs.vf_arqbah = v32;
        break;
    case VF_ATQBAH:
        s->regs.vf_atqbah = v32;
        break;
    case VF_ARQLEN:
        s->regs.vf_arqlen = v32;
        break;
    case VF_ATQLEN:
        s->regs.vf_atqlen = v32;
        break;
    case VF_ARQH:
        s->regs.vf_arqh = v32;
        break;
    case VF_ATQH:
        s->regs.vf_atqh = v32;
        break;
    case VF_ARQT:
        s->regs.vf_arqt = v32;
        break;
    case VF_ATQT:
        s->regs.vf_atqt = v32;
        break;
    case VF_INT_ICR0_ENA1:
        /* Interrupt enable mask; store and possibly update irq level */
        s->regs.vf_int_icr0_ena1 = v32;
        /* For now tie intr_mask to complement of enable bits for adminq */
        if (v32 & VF_INT_ICR0_ENA1_ADMINQ_M) {
            s->intr_mask &= ~VF_INT_ICR0_ENA1_ADMINQ_M;
        } else {
            s->intr_mask |= VF_INT_ICR0_ENA1_ADMINQ_M;
        }
        pcibase_update_irq(s);
        break;
    default:
        /* For all other writes, just ignore */
        break;
    }
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
    pci_device_reset(PCI_DEVICE(dev));

    /* Revert registers to power-on defaults */
    memset(&s->regs, 0, sizeof(s->regs));
    s->intr_status = 0;
    s->intr_mask = 0;
    s->status_flags = 0;
    s->reset_state = 0;
    s->pm_state = 0;
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
    s->num_bars = 0;
    for (int i = 0; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = "";
    }

    /* The driver uses BAR0 for mailbox and rstat, and explicitly accesses
     * VF_ARQBAL (within BAR0). We therefore expose a single MMIO BAR0
     * large enough to cover at least the VF and rstat regions.
     * VF_ARQBAL is at 0x6000 + 0x0C00 = 0x6C00, and VFGEN_RSTAT is at 0x8800.
     * Use 64 KiB BAR0 to fully cover this range without guessing further.
     */
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x10000; /* 64 KiB */
    s->bar_info[0].name = "idpf-bar0";
    s->num_bars = 1;

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI/MSI-X presence is indicated in driver (num_avail_msix, etc.), but
     * initialization specifics are outside this file; do not enable here.
     */
    s->has_msi = false;
    s->has_msix = false;

    /* DMA configuration not specified here */

    /* Timer configuration not specified here */

    /* Final state initialization before the device is 'live' */
    memset(&s->regs, 0, sizeof(s->regs));
    s->intr_status = 0;
    s->intr_mask = 0;
    s->status_flags = 0;
    s->reset_state = 0;
    s->pm_state = 0;
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

    /* No dynamic allocations to free */
    (void)s;
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "idpf_pci",
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
