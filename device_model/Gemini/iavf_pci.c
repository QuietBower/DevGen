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

#define TYPE_PCIBASE_DEVICE "iavf_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_INTEL 0x8086
#define IAVF_DEV_ID_VF 0x154C

#define IAVF_MASK(mask, shift) ((uint32_t)(mask) << (shift))

#define IAVF_VFGEN_RSTAT_VFR_STATE_SHIFT 0
#define IAVF_VF_ARQLEN1_ARQENABLE_SHIFT 31
#define IAVF_VF_ARQLEN1_ARQVFE_SHIFT 28
#define IAVF_VF_ARQLEN1_ARQOVFL_SHIFT 29
#define IAVF_VF_ARQLEN1_ARQCRIT_SHIFT 30
#define IAVF_VF_ATQLEN1_ATQVFE_SHIFT 28
#define IAVF_VF_ATQLEN1_ATQOVFL_SHIFT 29
#define IAVF_VF_ATQLEN1_ATQCRIT_SHIFT 30
#define IAVF_VFINT_DYN_CTL01_INTENA_SHIFT 0
#define IAVF_VFINT_DYN_CTL01_ITR_INDX_SHIFT 3
#define IAVF_VFINT_ICR0_ENA1_ADMINQ_SHIFT 30
#define IAVF_VFINT_DYN_CTLN1_INTENA_SHIFT 0
#define IAVF_VFINT_DYN_CTLN1_ITR_INDX_SHIFT 3
#define IAVF_ITR_DYNAMIC	0x8000

#define IAVF_VFGEN_RSTAT 0x00008800
#define IAVF_VFINT_DYN_CTL01 0x00005C00
#define IAVF_VFINT_ICR0_ENA1 0x00005000
#define IAVF_VFINT_ICR01 0x00004800
#define IAVF_VF_ARQLEN1 0x00008000
#define IAVF_VF_ATQLEN1 0x00006800
#define IAVF_VF_ATQH1 0x00006400
#define IAVF_VF_ARQH1 0x00007400
#define IAVF_VF_ARQT1 0x00007000
#define IAVF_VF_ATQBAL1 0x00007C00
#define IAVF_VF_ATQBAH1 0x00007800
#define IAVF_VF_ATQT1 0x00008400
#define IAVF_VF_ARQBAL1 0x00006C00
#define IAVF_VF_ARQBAH1 0x00006000

#define IAVF_MAX_AQ_BUF_SIZE 4096

enum virtchnl_ops {
	VIRTCHNL_OP_UNKNOWN = 0,
	VIRTCHNL_OP_VERSION = 1,
	VIRTCHNL_OP_RESET_VF = 2,
	VIRTCHNL_OP_GET_VF_RESOURCES = 3,
	VIRTCHNL_OP_CONFIG_TX_QUEUE = 4,
	VIRTCHNL_OP_CONFIG_RX_QUEUE = 5,
	VIRTCHNL_OP_CONFIG_VSI_QUEUES = 6,
	VIRTCHNL_OP_CONFIG_IRQ_MAP = 7,
	VIRTCHNL_OP_ENABLE_QUEUES = 8,
	VIRTCHNL_OP_DISABLE_QUEUES = 9,
	VIRTCHNL_OP_ADD_ETH_ADDR = 10,
	VIRTCHNL_OP_DEL_ETH_ADDR = 11,
	VIRTCHNL_OP_ADD_VLAN = 12,
	VIRTCHNL_OP_DEL_VLAN = 13,
	VIRTCHNL_OP_CONFIG_PROMISCUOUS_MODE = 14,
	VIRTCHNL_OP_GET_STATS = 15,
	VIRTCHNL_OP_RSVD = 16,
	VIRTCHNL_OP_EVENT = 17,
	VIRTCHNL_OP_CONFIG_RSS_HFUNC = 18,
	VIRTCHNL_OP_IWARP = 20,
	VIRTCHNL_OP_RDMA = VIRTCHNL_OP_IWARP,
	VIRTCHNL_OP_CONFIG_IWARP_IRQ_MAP = 21,
	VIRTCHNL_OP_CONFIG_RDMA_IRQ_MAP = VIRTCHNL_OP_CONFIG_IWARP_IRQ_MAP,
	VIRTCHNL_OP_RELEASE_IWARP_IRQ_MAP = 22,
	VIRTCHNL_OP_RELEASE_RDMA_IRQ_MAP = VIRTCHNL_OP_RELEASE_IWARP_IRQ_MAP,
	VIRTCHNL_OP_CONFIG_RSS_KEY = 23,
	VIRTCHNL_OP_CONFIG_RSS_LUT = 24,
	VIRTCHNL_OP_GET_RSS_HASHCFG_CAPS = 25,
	VIRTCHNL_OP_SET_RSS_HASHCFG = 26,
	VIRTCHNL_OP_ENABLE_VLAN_STRIPPING = 27,
	VIRTCHNL_OP_DISABLE_VLAN_STRIPPING = 28,
	VIRTCHNL_OP_REQUEST_QUEUES = 29,
	VIRTCHNL_OP_ENABLE_CHANNELS = 30,
	VIRTCHNL_OP_DISABLE_CHANNELS = 31,
	VIRTCHNL_OP_ADD_CLOUD_FILTER = 32,
	VIRTCHNL_OP_DEL_CLOUD_FILTER = 33,
	VIRTCHNL_OP_GET_SUPPORTED_RXDIDS = 44,
	VIRTCHNL_OP_ADD_RSS_CFG = 45,
	VIRTCHNL_OP_DEL_RSS_CFG = 46,
	VIRTCHNL_OP_ADD_FDIR_FILTER = 47,
	VIRTCHNL_OP_DEL_FDIR_FILTER = 48,
	VIRTCHNL_OP_GET_OFFLOAD_VLAN_V2_CAPS = 51,
	VIRTCHNL_OP_ADD_VLAN_V2 = 52,
	VIRTCHNL_OP_DEL_VLAN_V2 = 53,
	VIRTCHNL_OP_ENABLE_VLAN_STRIPPING_V2 = 54,
	VIRTCHNL_OP_DISABLE_VLAN_STRIPPING_V2 = 55,
	VIRTCHNL_OP_ENABLE_VLAN_INSERTION_V2 = 56,
	VIRTCHNL_OP_DISABLE_VLAN_INSERTION_V2 = 57,
	VIRTCHNL_OP_1588_PTP_GET_CAPS = 60,
	VIRTCHNL_OP_1588_PTP_GET_TIME = 61,
	VIRTCHNL_OP_GET_QOS_CAPS = 66,
	VIRTCHNL_OP_CONFIG_QUEUE_BW = 112,
	VIRTCHNL_OP_CONFIG_QUANTA = 113,
	VIRTCHNL_OP_MAX,
};

enum iavf_status {
	IAVF_SUCCESS				= 0,
	IAVF_ERR_NVM				= -1,
	IAVF_ERR_NVM_CHECKSUM			= -2,
	IAVF_ERR_PHY				= -3,
	IAVF_ERR_CONFIG				= -4,
	IAVF_ERR_PARAM				= -5,
	IAVF_ERR_MAC_TYPE			= -6,
	IAVF_ERR_UNKNOWN_PHY			= -7,
	IAVF_ERR_LINK_SETUP			= -8,
	IAVF_ERR_ADAPTER_STOPPED		= -9,
	IAVF_ERR_INVALID_MAC_ADDR		= -10,
	IAVF_ERR_DEVICE_NOT_SUPPORTED		= -11,
	IAVF_ERR_PRIMARY_REQUESTS_PENDING	= -12,
	IAVF_ERR_INVALID_LINK_SETTINGS		= -13,
	IAVF_ERR_AUTONEG_NOT_COMPLETE		= -14,
	IAVF_ERR_RESET_FAILED			= -15,
	IAVF_ERR_SWFW_SYNC			= -16,
	IAVF_ERR_NO_AVAILABLE_VSI		= -17,
	IAVF_ERR_NO_MEMORY			= -18,
	IAVF_ERR_BAD_PTR			= -19,
	IAVF_ERR_RING_FULL			= -20,
	IAVF_ERR_INVALID_PD_ID			= -21,
	IAVF_ERR_INVALID_QP_ID			= -22,
	IAVF_ERR_INVALID_CQ_ID			= -23,
	IAVF_ERR_INVALID_CEQ_ID			= -24,
	IAVF_ERR_INVALID_AEQ_ID			= -25,
	IAVF_ERR_INVALID_SIZE			= -26,
	IAVF_ERR_INVALID_ARP_INDEX		= -27,
	IAVF_ERR_INVALID_FPM_FUNC_ID		= -28,
	IAVF_ERR_QP_INVALID_MSG_SIZE		= -29,
	IAVF_ERR_QP_TOOMANY_WRS_POSTED		= -30,
	IAVF_ERR_INVALID_FRAG_COUNT		= -31,
	IAVF_ERR_QUEUE_EMPTY			= -32,
	IAVF_ERR_INVALID_ALIGNMENT		= -33,
	IAVF_ERR_FLUSHED_QUEUE			= -34,
	IAVF_ERR_INVALID_PUSH_PAGE_INDEX	= -35,
	IAVF_ERR_INVALID_IMM_DATA_SIZE		= -36,
	IAVF_ERR_TIMEOUT			= -37,
	IAVF_ERR_OPCODE_MISMATCH		= -38,
	IAVF_ERR_CQP_COMPL_ERROR		= -39,
	IAVF_ERR_INVALID_VF_ID			= -40,
	IAVF_ERR_INVALID_HMCFN_ID		= -41,
	IAVF_ERR_BACKING_PAGE_ERROR		= -42,
	IAVF_ERR_NO_PBLCHUNKS_AVAILABLE		= -43,
	IAVF_ERR_INVALID_PBLE_INDEX		= -44,
	IAVF_ERR_INVALID_SD_INDEX		= -45,
	IAVF_ERR_INVALID_PAGE_DESC_INDEX	= -46,
	IAVF_ERR_INVALID_SD_TYPE		= -47,
	IAVF_ERR_MEMCPY_FAILED			= -48,
	IAVF_ERR_INVALID_HMC_OBJ_INDEX		= -49,
	IAVF_ERR_INVALID_HMC_OBJ_COUNT		= -50,
	IAVF_ERR_INVALID_SRQ_ARM_LIMIT		= -51,
	IAVF_ERR_SRQ_ENABLED			= -52,
	IAVF_ERR_ADMIN_QUEUE_ERROR		= -53,
	IAVF_ERR_ADMIN_QUEUE_TIMEOUT		= -54,
	IAVF_ERR_BUF_TOO_SHORT			= -55,
	IAVF_ERR_ADMIN_QUEUE_FULL		= -56,
	IAVF_ERR_ADMIN_QUEUE_NO_WORK		= -57,
	IAVF_ERR_BAD_RDMA_CQE			= -58,
	IAVF_ERR_NVM_BLANK_MODE			= -59,
	IAVF_ERR_NOT_IMPLEMENTED		= -60,
	IAVF_ERR_PE_DOORBELL_NOT_ENABLED	= -61,
	IAVF_ERR_DIAG_TEST_FAILED		= -62,
	IAVF_ERR_NOT_READY			= -63,
	IAVF_NOT_SUPPORTED			= -64,
	IAVF_ERR_FIRMWARE_API_VERSION		= -65,
	IAVF_ERR_ADMIN_QUEUE_CRITICAL_ERROR	= -66,
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

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t vfgen_rstat;
    uint32_t vfint_dyn_ctl01;
    uint32_t vfint_icr0_ena1;
    uint32_t vfint_icr01;
    uint32_t vf_arqlen1;
    uint32_t vf_atqlen1;
    uint32_t vf_atqh1;
    uint32_t vf_arqh1;
    uint32_t vf_arqt1;
    uint32_t vf_atqt1;
    uint32_t vf_atqbal1;
    uint32_t vf_atqbah1;
    uint32_t vf_arqbal1;
    uint32_t vf_arqbah1;

    uint32_t vfint_dyn_ctln1[64];
    uint32_t vfint_itrn1[3][64];
    uint32_t qtx_tail1[64];
    uint32_t qrx_tail1[64];
    uint32_t vfqf_hkey[13];
    uint32_t vfqf_hlut[16];
    uint32_t vfqf_hena[2];

    /* DMA Context */
    dma_addr_t arq_base;
    dma_addr_t atq_base;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->vfint_icr01 & s->vfint_icr0_ena1) {
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

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= 0x00003800 && addr < 0x00003800 + 64 * 4) {
        int idx = (addr - 0x00003800) / 4;
        return s->vfint_dyn_ctln1[idx];
    }
    if (addr >= 0x00002800 && addr < 0x00002800 + 3 * 64) {
        int offset = addr - 0x00002800;
        int i = offset / 64;
        int intvf = (offset % 64) / 4;
        return s->vfint_itrn1[i][intvf];
    }
    if (addr >= 0x00000000 && addr < 0x00000000 + 64 * 4) {
        int idx = (addr - 0x00000000) / 4;
        return s->qtx_tail1[idx];
    }
    if (addr >= 0x00002000 && addr < 0x00002000 + 64 * 4) {
        int idx = (addr - 0x00002000) / 4;
        return s->qrx_tail1[idx];
    }
    if (addr >= 0x0000CC00 && addr < 0x0000CC00 + 13 * 4) {
        int idx = (addr - 0x0000CC00) / 4;
        return s->vfqf_hkey[idx];
    }
    if (addr >= 0x0000D000 && addr < 0x0000D000 + 16 * 4) {
        int idx = (addr - 0x0000D000) / 4;
        return s->vfqf_hlut[idx];
    }
    if (addr >= 0x0000C400 && addr < 0x0000C400 + 2 * 4) {
        int idx = (addr - 0x0000C400) / 4;
        return s->vfqf_hena[idx];
    }

    switch (addr) {
    case IAVF_VFGEN_RSTAT:
        val = s->vfgen_rstat;
        break;
    case IAVF_VFINT_DYN_CTL01:
        val = s->vfint_dyn_ctl01;
        break;
    case IAVF_VFINT_ICR0_ENA1:
        val = s->vfint_icr0_ena1;
        s->vfint_icr0_ena1 = 0; /* Clear on read */
        break;
    case IAVF_VFINT_ICR01:
        val = s->vfint_icr01;
        s->vfint_icr01 = 0; /* Clear on read */
        break;
    case IAVF_VF_ARQLEN1:
        val = s->vf_arqlen1;
        break;
    case IAVF_VF_ATQLEN1:
        val = s->vf_atqlen1;
        break;
    case IAVF_VF_ATQH1:
        val = s->vf_atqh1;
        break;
    case IAVF_VF_ARQH1:
        val = s->vf_arqh1;
        break;
    case IAVF_VF_ARQT1:
        val = s->vf_arqt1;
        break;
    case IAVF_VF_ATQT1:
        val = s->vf_atqt1;
        break;
    case IAVF_VF_ATQBAL1:
        val = s->vf_atqbal1;
        break;
    case IAVF_VF_ATQBAH1:
        val = s->vf_atqbah1;
        break;
    case IAVF_VF_ARQBAL1:
        val = s->vf_arqbal1;
        break;
    case IAVF_VF_ARQBAH1:
        val = s->vf_arqbah1;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: Unimplemented read at 0x%" HWADDR_PRIx "\n", __func__, addr);
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= 0x00003800 && addr < 0x00003800 + 64 * 4) {
        int idx = (addr - 0x00003800) / 4;
        s->vfint_dyn_ctln1[idx] = val;
        return;
    }
    if (addr >= 0x00002800 && addr < 0x00002800 + 3 * 64) {
        int offset = addr - 0x00002800;
        int i = offset / 64;
        int intvf = (offset % 64) / 4;
        s->vfint_itrn1[i][intvf] = val;
        return;
    }
    if (addr >= 0x00000000 && addr < 0x00000000 + 64 * 4) {
        int idx = (addr - 0x00000000) / 4;
        s->qtx_tail1[idx] = val;
        return;
    }
    if (addr >= 0x00002000 && addr < 0x00002000 + 64 * 4) {
        int idx = (addr - 0x00002000) / 4;
        s->qrx_tail1[idx] = val;
        return;
    }
    if (addr >= 0x0000CC00 && addr < 0x0000CC00 + 13 * 4) {
        int idx = (addr - 0x0000CC00) / 4;
        s->vfqf_hkey[idx] = val;
        return;
    }
    if (addr >= 0x0000D000 && addr < 0x0000D000 + 16 * 4) {
        int idx = (addr - 0x0000D000) / 4;
        s->vfqf_hlut[idx] = val;
        return;
    }
    if (addr >= 0x0000C400 && addr < 0x0000C400 + 2 * 4) {
        int idx = (addr - 0x0000C400) / 4;
        s->vfqf_hena[idx] = val;
        return;
    }

    switch (addr) {
    case IAVF_VFGEN_RSTAT:
        s->vfgen_rstat = val;
        break;
    case IAVF_VFINT_DYN_CTL01:
        s->vfint_dyn_ctl01 = val;
        break;
    case IAVF_VFINT_ICR0_ENA1:
        s->vfint_icr0_ena1 = val;
        break;
    case IAVF_VFINT_ICR01:
        s->vfint_icr01 = val;
        break;
    case IAVF_VF_ARQLEN1:
        s->vf_arqlen1 = val;
        break;
    case IAVF_VF_ATQLEN1:
        s->vf_atqlen1 = val;
        break;
    case IAVF_VF_ATQH1:
        s->vf_atqh1 = val;
        break;
    case IAVF_VF_ARQH1:
        s->vf_arqh1 = val;
        break;
    case IAVF_VF_ARQT1:
        s->vf_arqt1 = val;
        break;
    case IAVF_VF_ATQT1:
        s->vf_atqt1 = val;
        break;
    case IAVF_VF_ATQBAL1:
        s->vf_atqbal1 = val;
        break;
    case IAVF_VF_ATQBAH1:
        s->vf_atqbah1 = val;
        break;
    case IAVF_VF_ARQBAL1:
        s->vf_arqbal1 = val;
        break;
    case IAVF_VF_ARQBAH1:
        s->vf_arqbah1 = val;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: Unimplemented write at 0x%" HWADDR_PRIx " val 0x%" PRIx64 "\n", __func__, addr, val);
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
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
    
    s->vfgen_rstat = 0;
    s->vfint_dyn_ctl01 = 0;
    s->vfint_icr0_ena1 = 0;
    s->vfint_icr01 = 0;
    s->vf_arqlen1 = 0;
    s->vf_atqlen1 = 0;
    s->vf_atqh1 = 0;
    s->vf_arqh1 = 0;
    s->vf_arqt1 = 0;
    s->vf_atqt1 = 0;
    s->vf_atqbal1 = 0;
    s->vf_atqbah1 = 0;
    s->vf_arqbal1 = 0;
    s->vf_arqbah1 = 0;

    memset(s->vfint_dyn_ctln1, 0, sizeof(s->vfint_dyn_ctln1));
    memset(s->vfint_itrn1, 0, sizeof(s->vfint_itrn1));
    memset(s->qtx_tail1, 0, sizeof(s->qtx_tail1));
    memset(s->qrx_tail1, 0, sizeof(s->qrx_tail1));
    memset(s->vfqf_hkey, 0, sizeof(s->vfqf_hkey));
    memset(s->vfqf_hlut, 0, sizeof(s->vfqf_hlut));
    memset(s->vfqf_hena, 0, sizeof(s->vfqf_hena));
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x8086 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x154C );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0200 );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 1;  
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 65536; /* Default safe size, exact size pending driver macro */
    s->bar_info[0].name = "iavf-bar0";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msix = true;  
    if (s->has_msix) {
        msix_init(pdev, 16, &s->bar_regions[0], 0, 0x8000, &s->bar_regions[0], 0, 0x9000, 0, errp);
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, &s->bar_regions[0], &s->bar_regions[0]);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "iavf_pci",
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
