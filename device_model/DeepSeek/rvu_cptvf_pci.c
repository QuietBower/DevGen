/*
 * QEMU model for Marvell OcteonTX2 CPT VF (rvu_cptvf)
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "qemu/host-utils.h"
#include "qemu/bitops.h"
#include "qemu/cutils.h"
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
#include <stdint.h>

/* QEMU-compatible type aliases for kernel u* types */
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef uint64_t dma_addr_t;
typedef int spinlock_t;

#define TYPE_PCIBASE_DEVICE "rvu_cptvf_pci"
typedef struct PCIBaseState PCIBaseState;

/* Register offsets and macros from driver sources */
#define OTX2_CPTVF_DRV_NAME "rvu_cptvf"
#define OTX2_CPT_INST_QLEN_MSGS	((OTX2_CPT_SIZE_DIV40 - 1) * 40)
#define OTX2_RVU_VF_INT                 (0x20)
#define OTX2_RVU_VF_INT_ENA_W1S         (0x30)
#define OTX2_RVU_VF_INT_ENA_W1C         (0x38)
#define CN10K_CPT_VF_MBOX_REGION  (0xC0000)
#define CN10K_MBOX  0
#define OTX2_CPT_INVALID_CRYPTO_ENG_GRP 0xFF
#define OTX2_CPT_QUEUE_HI_PRIO  0x1
#define OTX2_CPT_PCI_VF_DEVICE_ID 0xA0FE
#define CN10K_CPT_PCI_VF_DEVICE_ID 0xA0F3
#define OTX2_CPT_MAX_LFS_NUM    64
#define OTX2_CPT_SIZE_DIV40 (OTX2_CPT_USER_REQUESTED_QLEN_MSGS/40)
#define OTX2_CPT_RVU_FUNC_ADDR_S(blk, slot, offs) \
		(((blk) << 20) | ((slot) << 12) | (offs))
#define OTX2_CPT_LF_MSIX_VECTORS 2
#define OTX2_CPT_RVU_PFFUNC(pdev, pf, func) rvu_make_pcifunc(pdev, pf, func)
#define MBOX_MSG_GET_KVF_LIMITS         0xBFC
#define MBOX_MSG_GET_ENG_GRP_NUM        0xBFF
#define OTX2_CPT_LF_NQX(a)              (0x400 | (a) << 3)
#define OTX2_CPT_LMT_LF_LMTLINEX(a)     (OTX2_CPT_LMT_LFBASE | 0x000 | \
				 (a) << 12)
#define CN10K_LMTST 1
#define LMTLINE_ALIGN 128
#define LMTLINE_SIZE  128
#define MBOX_MSG_GET_CAPS               0xBFD
#define OTX2_CPT_RES_ADDR_ALIGN		32
#define OTX2_CPT_DPTR_RPTR_ALIGN	8
#define OTX2_CPT_USER_REQUESTED_QLEN_MSGS 8200
#define OTX2_CPT_LF_MISC_INT_ENA_W1C    (0xe0)
#define OTX2_CPT_LF_MISC_INT_ENA_W1S    (0xd0)
#define DQPTR      GENMASK_ULL(19, 0)
#define XQ_XOR     GENMASK_ULL(63, 63)
#define INFLIGHT   GENMASK_ULL(8, 0)
#define OTX2_CPT_LF_Q_INST_PTR          (0x110)
#define GRB_CNT    GENMASK_ULL(39, 32)
#define NQPTR      GENMASK_ULL(51, 32)
#define OTX2_CPT_LF_Q_SIZE              (0x100)
#define OTX2_CPT_LF_CTL                 (0x10)
#define OTX2_CPT_LF_INPROG              (0x40)
#define OTX2_CPT_LF_DONE_INT_ENA_W1C    (0xa0)
#define OTX2_CPT_LF_DONE_INT_ENA_W1S    (0x90)
#define OTX2_CPT_Q_FC_LEN 128
#define OTX2_CPT_INST_QLEN_BYTES                                               \
		((OTX2_CPT_SIZE_DIV40 * 40 * OTX2_CPT_INST_SIZE) +             \
		OTX2_CPT_INST_QLEN_EXTRA_BYTES)
#define OTX2_CPT_INST_Q_ALIGNMENT  128
#define OTX2_CPT_INST_GRP_QLEN_BYTES                                           \
		((OTX2_CPT_SIZE_DIV40 + OTX2_CPT_EXTRA_SIZE_DIV40) * 16)
#define OTX2_CPT_LMT_LFBASE             BIT_ULL(OTX2_CPT_RVU_FUNC_BLKADDR_SHIFT)
#define OTX2_CPT_PCI_PF_DEVICE_ID 0xA0FD
#define SGV2_COMPS_MAX  3
#define SG_COMP_2    2
#define SG_COMP_1    1
#define OTX2_CPT_MAX_SG_OUT_CNT 50
#define OTX2_CPT_MAX_SG_IN_CNT  50
#define OTX2_CPT_INST_QLEN_EXTRA_BYTES  (320 * OTX2_CPT_INST_SIZE)
#define OTX2_CPT_INST_SIZE	64
#define OTX2_CPT_EXTRA_SIZE_DIV40       (320/40)
#define OTX2_CPT_LF_Q_BASE              (0xf0)
#define OTX2_CPT_RVU_FUNC_BLKADDR_SHIFT 20
#define SG_LIST_HDR_SIZE	8
#define OTX2_CPT_MAX_KEY_SIZE (OTX2_CPT_MAX_ENC_KEY_SIZE + \
			       OTX2_CPT_MAX_HASH_KEY_SIZE)
#define CPT_PCI_SUBSYS_DEVID_CN10K_B 0xBD00
#define OTX2_CPT_LF_CTX_CTL             (0x500)
#define OTX2_CPT_LF_DONE_WAIT           (0x30)
#define SG_COMP_3    3
#define SG_COMPS_MAX    4
#define OTX2_CPT_MAX_REQ_SIZE 65535
#define CN10K_CPT_HW_CTX_SIZE  256
#define OTX2_CPT_MAX_HASH_KEY_SIZE   64
#define OTX2_CPT_MAX_ENC_KEY_SIZE    32
#define CPT_PCI_SUBSYS_DEVID_CN10K_A 0xB900
#define OTX2_CPT_LF_CTX_FLUSH           (0x510)
#define OTX2_CPT_LF_CTX_ERR             (0x520)
#define OTX2_CPT_COMPLETION_CODE_INIT OTX2_CPT_COMP_E_NOTDONE
#define OTX2_CPT_DMA_MODE_SG     1
#define OTX2_CPT_FROM_CPTR 0
#define OTX2_CPT_FROM_DPTR 1

/* Additional register offsets derived from driver behavior */
#define OTX2_CPT_LF_MISC_INT            0xB0
#define OTX2_CPT_LF_DONE_ACK            0x60
#define OTX2_CPT_LF_DONE                0x50
#define RVU_VF_VFPF_MBOX1               0x0008

/* Message IDs from PF-VF mailbox */
#define MBOX_MSG_READY                  0x001
#define MBOX_MSG_ATTACH_RESOURCES       0x002
#define MBOX_MSG_DETACH_RESOURCES       0x003
#define MBOX_MSG_MSIX_OFFSET            0x004
#define MBOX_MSG_CPT_LF_RESET           0x005

#define MBOX_SIZE 4096

enum otx2_cpt_eng_type {
	OTX2_CPT_AE_TYPES = 1,
	OTX2_CPT_SE_TYPES = 2,
	OTX2_CPT_IE_TYPES = 3,
	OTX2_CPT_MAX_ENG_TYPES,
};

struct mbox_msghdr {
	u16 pcifunc;
	u16 id;
#define OTX2_MBOX_REQ_SIG (0xdead)
#define OTX2_MBOX_RSP_SIG (0xbeef)
	u16 sig;
	u16 ver;
	u16 next_msgoff;
	int rc;
};

struct otx2_cpt_kvf_limits_rsp {
	struct mbox_msghdr hdr;
	u8 kvf_limits;
};

struct otx2_cpt_egrp_num_msg {
	struct mbox_msghdr hdr;
	u8 eng_type;
};

struct otx2_cpt_egrp_num_rsp {
	struct mbox_msghdr hdr;
	u8 eng_type;
	u8 eng_grp_num;
};

union otx2_cpt_eng_caps {
	u64 u;
	struct {
		u64 reserved_0_4:5;
		u64 mul:1;
		u64 sha1_sha2:1;
		u64 chacha20:1;
		u64 zuc_snow3g:1;
		u64 sha3:1;
		u64 aes:1;
		u64 kasumi:1;
		u64 des:1;
		u64 crc:1;
		u64 mmul:1;
		u64 reserved_15_33:19;
		u64 pdcp_chain:1;
		u64 reserved_35_63:29;
	};
};

struct otx2_cpt_caps_rsp {
	struct mbox_msghdr hdr;
	u16 cpt_pf_drv_version;
	u8 cpt_revision;
	union otx2_cpt_eng_caps eng_caps[OTX2_CPT_MAX_ENG_TYPES];
};

struct msix_offset_rsp {
	struct mbox_msghdr hdr;
	u8 num_lf;
	u16 offsets[OTX2_CPT_MAX_LFS_NUM];
};

struct rsrc_attach {
	struct mbox_msghdr hdr;
	u8 cptlfs;
};

struct rsrc_detach {
	struct mbox_msghdr hdr;
	u8 cptlfs;
};

struct cpt_lf_rst_req {
	struct mbox_msghdr hdr;
	u8 slot;
};

struct ready_msg_rsp {
	struct mbox_msghdr hdr;
};

/* Structures used in PCIBaseState */
struct otx2_cpt_pending_entry {
	void *completion_addr;
	void *info;
	void (*callback)(int status, void *arg1, void *arg2);
	void *areq;
	u8 resume_sender;
	u8 busy;
};

struct otx2_cpt_inst_queue {
	u8 *vaddr;
	u8 *real_vaddr;
	dma_addr_t dma_addr;
	dma_addr_t real_dma_addr;
	u32 size;
};

struct otx2_cpt_pending_queue {
	struct otx2_cpt_pending_entry *head;
	u32 front;
	u32 rear;
	u32 pending_count;
	u32 qlen;
	spinlock_t lock;
};

union otx2_cptx_lf_q_base {
	u64 u;
	struct otx2_cptx_lf_q_base_s {
		u64 fault:1;
		u64 reserved_1_6:6;
		u64 addr:46;
		u64 reserved_53_63:11;
	} s;
};

union otx2_cptx_lf_q_size {
	u64 u;
	struct otx2_cptx_lf_q_size_s {
		u64 size_div40:15;
		u64 reserved_15_63:49;
	} s;
};

union otx2_cptx_lf_done_wait {
	u64 u;
	struct otx2_cptx_lf_done_wait_s {
		u64 num_wait:20;
		u64 reserved_20_31:12;
		u64 time_wait:16;
		u64 reserved_48_63:16;
	} s;
};

union otx2_cptx_lf_inprog {
	u64 u;
	struct otx2_cptx_lf_inprog_s {
		u64 inflight:9;
		u64 reserved_9_15:7;
		u64 eena:1;
		u64 grp_drp:1;
		u64 reserved_18_30:13;
		u64 grb_partial:1;
		u64 grb_cnt:8;
		u64 gwb_cnt:8;
		u64 reserved_48_63:16;
	} s;
};

union otx2_cptx_lf_ctl {
	u64 u;
	struct otx2_cptx_lf_ctl_s {
		u64 ena:1;
		u64 fc_ena:1;
		u64 fc_up_crossing:1;
		u64 reserved_3:1;
		u64 fc_hyst_bits:4;
		u64 reserved_8_63:56;
	} s;
};

typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM
} BARType;

typedef struct {
    int     index;
    BARType type;
    hwaddr  size;
    const char *name;
} BARInfo;

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];
    MemoryRegion mailbox_region;
    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;
    uint32_t vf_int_status;
    uint32_t vf_int_mask;
    uint64_t lf_misc_int;
    uint64_t lf_misc_int_ena;
    uint64_t lf_done_int_ena;
    uint32_t done_count;

    union otx2_cptx_lf_ctl lf_ctl;
    union otx2_cptx_lf_inprog lf_inprog;
    union otx2_cptx_lf_q_size lf_q_size;
    union otx2_cptx_lf_q_base lf_q_base;
    union otx2_cptx_lf_done_wait lf_done_wait;
    uint64_t lf_q_inst_ptr;
    uint64_t lf_nqx;
    uint64_t lf_ctx_ctl;
    uint64_t lf_ctx_flush;
    uint64_t lf_ctx_err;

    struct otx2_cpt_inst_queue iqueue;
    struct otx2_cpt_pending_queue pqueue;

    uint8_t mailbox_buf[MBOX_SIZE];
    bool mailbox_msg_pending;
};

OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    if (s->vf_int_status & s->vf_int_mask) {
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

    /* Per-vector MSI-X notifications for LF interrupts */
    if (msix_enabled(pdev)) {
        if (s->lf_done_int_ena && s->done_count > 0) {
            msix_notify(pdev, 0);
        }
        if (s->lf_misc_int_ena & s->lf_misc_int) {
            msix_notify(pdev, 1);
        }
    }
}

static void mailbox_process(PCIBaseState *s)
{
    struct mbox_msghdr *hdr = (struct mbox_msghdr *)s->mailbox_buf;
    uint16_t id = le16_to_cpu(hdr->id);

    /* Respond to known messages; for unknown, still respond success to avoid driver hang */
    switch (id) {
    case MBOX_MSG_READY:
        hdr->sig = cpu_to_le16(OTX2_MBOX_RSP_SIG);
        hdr->rc = 0;
        break;
    case MBOX_MSG_GET_CAPS:
        {
            struct otx2_cpt_caps_rsp *rsp = (void *)hdr;
            rsp->hdr.sig = cpu_to_le16(OTX2_MBOX_RSP_SIG);
            rsp->hdr.rc = 0;
            rsp->cpt_pf_drv_version = 1;
            rsp->cpt_revision = 0;
            rsp->eng_caps[OTX2_CPT_AE_TYPES].u = cpu_to_le64(0);
            rsp->eng_caps[OTX2_CPT_SE_TYPES].u = cpu_to_le64((1 << 5) | (1 << 6) | (1 << 8));
        }
        break;
    case MBOX_MSG_GET_ENG_GRP_NUM:
        {
            struct otx2_cpt_egrp_num_rsp *rsp = (void *)hdr;
            rsp->hdr.sig = cpu_to_le16(OTX2_MBOX_RSP_SIG);
            rsp->hdr.rc = 0;
            rsp->eng_type = ((struct otx2_cpt_egrp_num_msg *)hdr)->eng_type;
            rsp->eng_grp_num = 0;
        }
        break;
    case MBOX_MSG_GET_KVF_LIMITS:
        {
            struct otx2_cpt_kvf_limits_rsp *rsp = (void *)hdr;
            rsp->hdr.sig = cpu_to_le16(OTX2_MBOX_RSP_SIG);
            rsp->hdr.rc = 0;
            rsp->kvf_limits = 0;
        }
        break;
    case MBOX_MSG_MSIX_OFFSET:
        {
            struct msix_offset_rsp *rsp = (void *)hdr;
            rsp->hdr.sig = cpu_to_le16(OTX2_MBOX_RSP_SIG);
            rsp->hdr.rc = 0;
            rsp->num_lf = 1;
            rsp->offsets[0] = cpu_to_le16(0);
        }
        break;
    case MBOX_MSG_ATTACH_RESOURCES:
        hdr->sig = cpu_to_le16(OTX2_MBOX_RSP_SIG);
        hdr->rc = 0;
        break;
    case MBOX_MSG_DETACH_RESOURCES:
        hdr->sig = cpu_to_le16(OTX2_MBOX_RSP_SIG);
        hdr->rc = 0;
        break;
    case MBOX_MSG_CPT_LF_RESET:
        hdr->sig = cpu_to_le16(OTX2_MBOX_RSP_SIG);
        hdr->rc = 0;
        break;
    default:
        hdr->sig = cpu_to_le16(OTX2_MBOX_RSP_SIG);
        hdr->rc = 0;
        break;
    }

    /* Signal completion; driver may poll or receive interrupt */
    if (msix_enabled(&s->parent_obj)) {
        msix_notify(&s->parent_obj, 0);
    }
}

static uint64_t pcibase_mailbox_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    if (addr + size <= MBOX_SIZE) {
        memcpy(&val, s->mailbox_buf + addr, size);
    }
    return val;
}

static void pcibase_mailbox_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr + size <= MBOX_SIZE) {
        memcpy(s->mailbox_buf + addr, &val, size);
        if (addr <= 4 && addr + size > 4) {
            uint16_t sig = le16_to_cpu(*(uint16_t *)(s->mailbox_buf + 4));
            if (sig == OTX2_MBOX_REQ_SIG && !s->mailbox_msg_pending) {
                s->mailbox_msg_pending = true;
                mailbox_process(s);
                s->mailbox_msg_pending = false;
            }
        }
    }
}

static const MemoryRegionOps pcibase_mailbox_ops = {
    .read = pcibase_mailbox_read,
    .write = pcibase_mailbox_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    int blk = (addr >> 20) & 0xF;
    int slot = (addr >> 12) & 0xFF;
    int offs = addr & 0xFFF;

    if (blk == 0 && slot == 0) {
        switch (offs) {
        case OTX2_RVU_VF_INT:
            val = s->vf_int_status;
            break;
        case OTX2_RVU_VF_INT_ENA_W1S:
            val = s->vf_int_mask;
            break;
        case OTX2_RVU_VF_INT_ENA_W1C:
            val = s->vf_int_mask;
            break;
        default:
            break;
        }
    } else if (blk == 1 && slot == 0) {
        switch (offs) {
        case OTX2_CPT_LF_CTL:
            val = s->lf_ctl.u;
            break;
        case OTX2_CPT_LF_INPROG:
            val = s->lf_inprog.u;
            break;
        case OTX2_CPT_LF_Q_SIZE:
            val = s->lf_q_size.u;
            break;
        case OTX2_CPT_LF_Q_BASE:
            val = s->lf_q_base.u;
            break;
        case OTX2_CPT_LF_DONE_WAIT:
            val = s->lf_done_wait.u;
            break;
        case OTX2_CPT_LF_Q_INST_PTR:
            val = s->lf_q_inst_ptr;
            break;
        case OTX2_CPT_LF_CTX_CTL:
            val = s->lf_ctx_ctl;
            break;
        case OTX2_CPT_LF_CTX_FLUSH:
            val = s->lf_ctx_flush;
            break;
        case OTX2_CPT_LF_CTX_ERR:
            val = s->lf_ctx_err;
            break;
        case OTX2_CPT_LF_MISC_INT:
            val = s->lf_misc_int;
            break;
        case OTX2_CPT_LF_DONE:
            val = s->done_count;
            break;
        case OTX2_CPT_LF_MISC_INT_ENA_W1S:
            val = s->lf_misc_int_ena;
            break;
        case OTX2_CPT_LF_MISC_INT_ENA_W1C:
            val = s->lf_misc_int_ena;
            break;
        case OTX2_CPT_LF_DONE_INT_ENA_W1S:
            val = s->lf_done_int_ena;
            break;
        case OTX2_CPT_LF_DONE_INT_ENA_W1C:
            val = s->lf_done_int_ena;
            break;
        default:
            break;
        }
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    int blk = (addr >> 20) & 0xF;
    int slot = (addr >> 12) & 0xFF;
    int offs = addr & 0xFFF;

    if (blk == 0 && slot == 0) {
        switch (offs) {
        case OTX2_RVU_VF_INT:
            s->vf_int_status &= ~val;
            pcibase_update_irq(s);
            break;
        case OTX2_RVU_VF_INT_ENA_W1S:
            s->vf_int_mask |= val;
            pcibase_update_irq(s);
            break;
        case OTX2_RVU_VF_INT_ENA_W1C:
            s->vf_int_mask &= ~val;
            pcibase_update_irq(s);
            break;
        case RVU_VF_VFPF_MBOX1:
            /* Trigger mailbox processing on any write */
            mailbox_process(s);
            break;
        default:
            break;
        }
    } else if (blk == 1 && slot == 0) {
        switch (offs) {
        case OTX2_CPT_LF_CTL:
            s->lf_ctl.u = val;
            break;
        case OTX2_CPT_LF_INPROG:
            s->lf_inprog.u = val;
            break;
        case OTX2_CPT_LF_Q_SIZE:
            s->lf_q_size.u = val;
            break;
        case OTX2_CPT_LF_Q_BASE:
            s->lf_q_base.u = val;
            break;
        case OTX2_CPT_LF_DONE_WAIT:
            s->lf_done_wait.u = val;
            break;
        case OTX2_CPT_LF_Q_INST_PTR:
            s->lf_q_inst_ptr = val;
            break;
        case OTX2_CPT_LF_CTX_CTL:
            s->lf_ctx_ctl = val;
            break;
        case OTX2_CPT_LF_CTX_FLUSH:
            s->lf_ctx_flush = val;
            break;
        case OTX2_CPT_LF_CTX_ERR:
            s->lf_ctx_err = val;
            break;
        case OTX2_CPT_LF_MISC_INT:
            s->lf_misc_int &= ~val; /* write 1 to clear */
            pcibase_update_irq(s);
            break;
        case OTX2_CPT_LF_DONE_ACK:
            s->done_count -= val;
            pcibase_update_irq(s);
            break;
        case OTX2_CPT_LF_MISC_INT_ENA_W1S:
            s->lf_misc_int_ena |= val;
            pcibase_update_irq(s);
            break;
        case OTX2_CPT_LF_MISC_INT_ENA_W1C:
            s->lf_misc_int_ena &= ~val;
            pcibase_update_irq(s);
            break;
        case OTX2_CPT_LF_DONE_INT_ENA_W1S:
            s->lf_done_int_ena |= val;
            pcibase_update_irq(s);
            break;
        case OTX2_CPT_LF_DONE_INT_ENA_W1C:
            s->lf_done_int_ena &= ~val;
            pcibase_update_irq(s);
            break;
        default:
            break;
        }
    }
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

    s->vf_int_status = 0;
    s->vf_int_mask = 0;
    s->lf_misc_int = 0;
    s->lf_misc_int_ena = 0;
    s->lf_done_int_ena = 0;
    s->done_count = 0;
    s->lf_ctl.u = 0;
    s->lf_inprog.u = 0;
    s->lf_q_size.u = 0;
    s->lf_q_base.u = 0;
    s->lf_done_wait.u = 0;
    s->lf_q_inst_ptr = 0;
    s->lf_ctx_ctl = 0;
    s->lf_ctx_flush = 0;
    s->lf_ctx_err = 0;
    memset(&s->iqueue, 0, sizeof(s->iqueue));
    memset(&s->pqueue, 0, sizeof(s->pqueue));
    memset(s->mailbox_buf, 0, MBOX_SIZE);
    s->mailbox_msg_pending = false;

    pcibase_update_irq(s);
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }
    
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x177d);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0xA0FE);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0b40);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    s->num_bars = 1;
    s->bar_info[0] = (BARInfo){0, BAR_TYPE_MMIO, 2 * 1024 * 1024, "rvu_cptvf_bar0"};
    pcibase_register_bar(pdev, s, &s->bar_info[0], errp);

    /* Add mailbox subregion at offset CN10K_CPT_VF_MBOX_REGION */
    memory_region_init_io(&s->mailbox_region, OBJECT(s), &pcibase_mailbox_ops, s,
                          "rvu_cptvf_mbox", MBOX_SIZE);
    memory_region_add_subregion(&s->bar_regions[0], CN10K_CPT_VF_MBOX_REGION,
                                &s->mailbox_region);

    if (msix_init(pdev, 2, &s->bar_regions[0], 0, 0,
                  &s->bar_regions[0], 0, 0, 0xA0, errp)) {
        /* Error handling */
    }

    pcibase_reset(DEVICE(s));
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

static const VMStateDescription vmstate_pcibase = {
    .name = "rvu_cptvf_pci",
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
