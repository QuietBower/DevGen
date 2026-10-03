/*
 * QEMU model for Marvell OcteonTX2 CPT PF device
 * Based on driver: otx2_cptpf_main.c
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

#define TYPE_PCIBASE_DEVICE "rvu_cptpf_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_CAVIUM 0x177d
#define PCI_DEVICE_ID_OTX2_CPT_PF 0xA0FD
#define PCI_CLASS_ID_CRYPT_OTHER 0x108000

/* Updated register offsets from new driver source */
#define BLKADDR_RVUM        0x0
#define BLKADDR_CPT0        0x1
#define BLKADDR_CPT1        0x2
#define RVU_PF_BLOCK_ADDRX_DISC(blk)    (0x200 | ((blk) << 3))
#define RVU_PF_INT                          (0xc20)
#define RVU_PF_INT_ENA_W1S                  (0xc30)
#define RVU_PF_INT_ENA_W1C                  (0xc38)
#define RVU_PF_VF_MBOX_ADDR                 (0xC40)
#define RVU_PF_VF_BAR4_ADDR                 (0x10)
#define RVU_PF_VFPF_MBOX_INTX(a)            (0x880 | (a) << 3)
#define RVU_PF_VFPF_MBOX_INT_ENA_W1SX(a)    (0x8C0 | (a) << 3)
#define RVU_PF_VFPF_MBOX_INT_ENA_W1CX(a)    (0x8E0 | (a) << 3)
#define RVU_PF_VFFLR_INTX(a)                (0x900 | (a) << 3)
#define RVU_PF_VFFLR_INT_ENA_W1SX(a)        (0x940 | (a) << 3)
#define RVU_PF_VFFLR_INT_ENA_W1CX(a)        (0x960 | (a) << 3)
#define RVU_PF_VFME_INTX(a)                 (0x980 | (a) << 3)
#define RVU_PF_VFME_INT_ENA_W1SX(a)         (0x9C0 | (a) << 3)
#define RVU_PF_VFME_INT_ENA_W1CX(a)         (0x9E0 | (a) << 3)
#define RVU_PF_VFTRPENDX(a)                 (0x820 | (a) << 3)

#define RVU_PF_INT_VEC_AFPF_MBOX  0
#define RVU_PF_INT_VEC_VFPF_MBOX0 1
#define RVU_PF_INT_VEC_VFFLR0     2
#define RVU_PF_INT_VEC_VFME0      3
#define RVU_PF_INT_VEC_VFPF_MBOX1 4
#define RVU_PF_INT_VEC_VFFLR1     5
#define RVU_PF_INT_VEC_VFME1      6
#define RVU_PF_INT_VEC_CNT        7

/* Bar numbers updated based on driver expected layout:
 *  BAR1: MSI-X table
 *  BAR2: PF registers (PCI_PF_REG_BAR_NUM)
 *  BAR4: Mailbox (PCI_MBOX_BAR_NUM)
 */
#define PCI_MSIX_BAR_NUM    1
#define PCI_PF_REG_BAR_NUM  2
#define PCI_MBOX_BAR_NUM    4
#define MBOX_SIZE           0xE000

/* OTX2 CPT RVU functional address composition */
#define OTX2_CPT_RVU_FUNC_BLKADDR_SHIFT 20
#define OTX2_CPT_RVU_FUNC_SLOT_SHIFT    12
#define OTX2_CPT_RVU_FUNC_ADDR_S(blk, slot, offs) \
    (((blk) << OTX2_CPT_RVU_FUNC_BLKADDR_SHIFT) | \
     ((slot) << OTX2_CPT_RVU_FUNC_SLOT_SHIFT) | \
     ((offs) & 0xFFF))

/* Register offset macros from driver */
#define OTX2_CPT_LF_CTL                         0x10
#define OTX2_CPT_LF_DONE_WAIT                   0x30
#define OTX2_CPT_LF_INPROG                      0x40
#define OTX2_CPT_LF_MISC_INT_ENA_W1S            0xd0
#define OTX2_CPT_LF_MISC_INT_ENA_W1C            0xe0
#define OTX2_CPT_LF_Q_BASE                      0xf0
#define OTX2_CPT_LF_Q_SIZE                      0x100
#define OTX2_CPT_LF_Q_INST_PTR                  0x110
#define OTX2_CPT_LF_CTX_CTL                     0x500

/* Other hardware constants */
#define OTX2_CPT_RVU_FUNC_BLKADDR_SHIFT         20
#define OTX2_CPT_LMT_LFBASE                     (1ULL << OTX2_CPT_RVU_FUNC_BLKADDR_SHIFT)
#define OTX2_CPT_MAX_LFS_NUM                    64
#define OTX2_CPT_LF_MSIX_VECTORS                7

/* Register union definitions extracted from driver headers */
typedef union {
    uint64_t u;
    struct {
        uint64_t pri:1;
        uint64_t reserved_1_8:8;
        uint64_t pf_func_inst:1;
        uint64_t cont_err:1;
        uint64_t reserved_11_15:5;
        uint64_t nixtx_en:1;
        uint64_t ctx_ilen:3;
        uint64_t reserved_17_47:28;
        uint64_t grp:8;
        uint64_t reserved_56_63:8;
    } s;
} otx2_cptx_af_lf_ctrl_t;

typedef union {
    uint64_t u;
    struct {
        uint64_t num_wait:20;
        uint64_t reserved_20_31:12;
        uint64_t time_wait:16;
        uint64_t reserved_48_63:16;
    } s;
} otx2_cptx_lf_done_wait_t;

typedef union {
    uint64_t u;
    struct {
        uint64_t inflight:9;
        uint64_t reserved_9_15:7;
        uint64_t eena:1;
        uint64_t grp_drp:1;
        uint64_t reserved_18_30:13;
        uint64_t grb_partial:1;
        uint64_t grb_cnt:8;
        uint64_t gwb_cnt:8;
        uint64_t reserved_48_63:16;
    } s;
} otx2_cptx_lf_inprog_t;

typedef union {
    uint64_t u;
    struct {
        uint64_t ena:1;
        uint64_t fc_ena:1;
        uint64_t fc_up_crossing:1;
        uint64_t reserved_3:1;
        uint64_t fc_hyst_bits:4;
        uint64_t reserved_8_63:56;
    } s;
} otx2_cptx_lf_ctl_t;

typedef union {
    uint64_t u;
    struct {
        uint64_t fault:1;
        uint64_t reserved_1_6:6;
        uint64_t addr:46;
        uint64_t reserved_53_63:11;
    } s;
} otx2_cptx_lf_q_base_t;

typedef union {
    uint64_t u;
    struct {
        uint64_t size_div40:15;
        uint64_t reserved_15_63:49;
    } s;
} otx2_cptx_lf_q_size_t;

typedef union {
    uint64_t u;
    struct {
        uint64_t reserved_0:1;
        uint64_t nqerr:1;
        uint64_t irde:1;
        uint64_t nwrp:1;
        uint64_t reserved_4:1;
        uint64_t hwerr:1;
        uint64_t fault:1;
        uint64_t reserved_7_63:57;
    } s;
} otx2_cptx_lf_misc_int_ena_w1s_t;

typedef union otx2_cpt_eng_caps {
    uint64_t u;
    struct {
        uint64_t reserved_0_4:5;
        uint64_t mul:1;
        uint64_t sha1_sha2:1;
        uint64_t chacha20:1;
        uint64_t zuc_snow3g:1;
        uint64_t sha3:1;
        uint64_t aes:1;
        uint64_t kasumi:1;
        uint64_t des:1;
        uint64_t crc:1;
        uint64_t mmul:1;
        uint64_t reserved_15_33:19;
        uint64_t pdcp_chain:1;
        uint64_t reserved_35_63:29;
    } s;
} otx2_cpt_eng_caps_t;

/* LF state per slot */
typedef struct LFState {
    uint64_t lf_ctl;
    uint64_t lf_done_wait;
    uint64_t lf_inprog;
    uint64_t lf_misc_int_ena_w1s;
    uint64_t lf_misc_int_ena_w1c;
    uint64_t lf_q_base;
    uint64_t lf_q_size;
    uint64_t lf_q_inst_ptr;
    uint64_t lf_ctx_ctl;
} LFState;

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
    BARInfo bar_info[6];
    int num_bars;

    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;

    uint64_t rvu_pf_int;
    uint64_t rvu_pf_int_ena;
    uint64_t rvu_pf_vf_mbox_addr;
    uint64_t rvu_pf_vf_bar4_addr;
    uint64_t rvu_vfpf_mbox_int[2];
    uint64_t rvu_vfpf_mbox_int_ena[2];
    uint64_t rvu_vfflr_int[2];
    uint64_t rvu_vfflr_int_ena[2];
    uint64_t rvu_vfme_int[2];
    uint64_t rvu_vfme_int_ena[2];
    uint64_t rvu_vftrpend[2];

    QEMUTimer *af_mbox_timer;
    bool af_mbox_pending;
    uint8_t mbox_buf[MBOX_SIZE];

    LFState lf_state[OTX2_CPT_MAX_LFS_NUM];
};

static void pcibase_update_irq(PCIBaseState *s);

static void pcibase_do_dma(PCIBaseState *s, bool is_write);

static void pcibase_af_mbox_timer_cb(void *opaque)
{
    PCIBaseState *s = opaque;
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t resp = 0x1;

    /* Write success response at offset 0 of mailbox memory */
    memcpy(s->mbox_buf, &resp, sizeof(resp));
    /* memory_region_set_dirty removed: not needed for MMIO region */

    s->af_mbox_pending = false;

    /* Set interrupt status bit for AF mailbox */
    s->rvu_pf_int |= (1 << RVU_PF_INT_VEC_AFPF_MBOX);

    /* Raise MSI-X vector for AF mailbox */
    if (msix_enabled(pdev)) {
        msix_notify(pdev, RVU_PF_INT_VEC_AFPF_MBOX);
    }
}

static void pcibase_handle_af_mbox(PCIBaseState *s)
{
    if (!s->af_mbox_pending) {
        s->af_mbox_pending = true;
        /* Schedule response immediately to avoid probe timeout */
        timer_mod(s->af_mbox_timer,
                  qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 0);
    }
}

static uint64_t pcibase_mbox_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr + size <= MBOX_SIZE) {
        memcpy(&val, s->mbox_buf + addr, MIN(size, sizeof(val)));
    }
    return val;
}

static void pcibase_mbox_write(void *opaque, hwaddr addr, uint64_t val,
                               unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr + size <= MBOX_SIZE) {
        memcpy(s->mbox_buf + addr, &val, MIN(size, 8));
    }

    /* Trigger mailbox processing on write to the first 8 bytes (message header) */
    if (addr == 0) {
        pcibase_handle_af_mbox(s);
    }
}

static const MemoryRegionOps pcibase_mbox_ops = {
    .read = pcibase_mbox_read,
    .write = pcibase_mbox_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static void pcibase_update_irq(PCIBaseState *s)
{
}

static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    uint64_t blk, slot, offs;

    blk = (addr >> OTX2_CPT_RVU_FUNC_BLKADDR_SHIFT) & 0x3F;
    slot = (addr >> OTX2_CPT_RVU_FUNC_SLOT_SHIFT) & 0xFF;
    offs = addr & 0xFFF;

    switch (blk) {
    case BLKADDR_RVUM:
        switch (offs) {
        case 0x200 ... 0x2FF:
            if (offs == RVU_PF_BLOCK_ADDRX_DISC(BLKADDR_RVUM)) {
                val = 0x1000;
            } else if (offs == RVU_PF_BLOCK_ADDRX_DISC(BLKADDR_CPT0)) {
                val = 0x1000;
            } else if (offs == RVU_PF_BLOCK_ADDRX_DISC(BLKADDR_CPT1)) {
                val = 0x1000;
            }
            break;
        case RVU_PF_INT:
            val = s->rvu_pf_int;
            break;
        case RVU_PF_INT_ENA_W1S:
            val = s->rvu_pf_int_ena;
            break;
        case RVU_PF_INT_ENA_W1C:
            val = s->rvu_pf_int_ena;
            break;
        case RVU_PF_VF_MBOX_ADDR:
            val = s->rvu_pf_vf_mbox_addr;
            break;
        case RVU_PF_VF_BAR4_ADDR:
            val = s->rvu_pf_vf_bar4_addr;
            break;
        case 0x880 ... 0x88F:
            val = s->rvu_vfpf_mbox_int[(offs - 0x880) >> 3];
            break;
        case 0x8C0 ... 0x8CF:
            val = s->rvu_vfpf_mbox_int_ena[(offs - 0x8C0) >> 3];
            break;
        case 0x8E0 ... 0x8EF:
            val = s->rvu_vfpf_mbox_int_ena[(offs - 0x8E0) >> 3];
            break;
        case 0x900 ... 0x90F:
            val = s->rvu_vfflr_int[(offs - 0x900) >> 3];
            break;
        case 0x940 ... 0x94F:
            val = s->rvu_vfflr_int_ena[(offs - 0x940) >> 3];
            break;
        case 0x960 ... 0x96F:
            val = s->rvu_vfflr_int_ena[(offs - 0x960) >> 3];
            break;
        case 0x980 ... 0x98F:
            val = s->rvu_vfme_int[(offs - 0x980) >> 3];
            break;
        case 0x9C0 ... 0x9CF:
            val = s->rvu_vfme_int_ena[(offs - 0x9C0) >> 3];
            break;
        case 0x9E0 ... 0x9EF:
            val = s->rvu_vfme_int_ena[(offs - 0x9E0) >> 3];
            break;
        case 0x820 ... 0x82F:
            val = s->rvu_vftrpend[(offs - 0x820) >> 3];
            break;
        default:
            qemu_log_mask(LOG_UNIMP, "%s: RVU read unimplemented offset 0x%"PRIx64"\n",
                          __func__, offs);
            break;
        }
        break;
    case BLKADDR_CPT0:
    case BLKADDR_CPT1:
        if (slot >= OTX2_CPT_MAX_LFS_NUM) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid slot %d\n", __func__, (int)slot);
            return 0;
        }
        switch (offs) {
        case OTX2_CPT_LF_CTL:
            val = s->lf_state[slot].lf_ctl;
            break;
        case OTX2_CPT_LF_DONE_WAIT:
            val = s->lf_state[slot].lf_done_wait;
            break;
        case OTX2_CPT_LF_INPROG:
            val = s->lf_state[slot].lf_inprog;
            break;
        case OTX2_CPT_LF_MISC_INT_ENA_W1S:
            val = s->lf_state[slot].lf_misc_int_ena_w1s;
            break;
        case OTX2_CPT_LF_MISC_INT_ENA_W1C:
            val = s->lf_state[slot].lf_misc_int_ena_w1c;
            break;
        case OTX2_CPT_LF_Q_BASE:
            val = s->lf_state[slot].lf_q_base;
            break;
        case OTX2_CPT_LF_Q_SIZE:
            val = s->lf_state[slot].lf_q_size;
            break;
        case OTX2_CPT_LF_Q_INST_PTR:
            val = s->lf_state[slot].lf_q_inst_ptr;
            break;
        case OTX2_CPT_LF_CTX_CTL:
            val = s->lf_state[slot].lf_ctx_ctl;
            break;
        default:
            qemu_log_mask(LOG_UNIMP, "%s: CPT LF unimplemented offset 0x%"PRIx64" slot %d\n",
                          __func__, offs, (int)slot);
            break;
        }
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented block %d\n", __func__, (int)blk);
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t blk, slot, offs;
    uint8_t reg_index;

    blk = (addr >> OTX2_CPT_RVU_FUNC_BLKADDR_SHIFT) & 0x3F;
    slot = (addr >> OTX2_CPT_RVU_FUNC_SLOT_SHIFT) & 0xFF;
    offs = addr & 0xFFF;

    switch (blk) {
    case BLKADDR_RVUM:
        switch (offs) {
        case RVU_PF_INT:
            s->rvu_pf_int &= ~val;
            pcibase_update_irq(s);
            break;
        case RVU_PF_INT_ENA_W1S:
            s->rvu_pf_int_ena |= val;
            break;
        case RVU_PF_INT_ENA_W1C:
            s->rvu_pf_int_ena &= ~val;
            break;
        case RVU_PF_VF_MBOX_ADDR:
            s->rvu_pf_vf_mbox_addr = val;
            break;
        case RVU_PF_VF_BAR4_ADDR:
            s->rvu_pf_vf_bar4_addr = val;
            break;
        case 0x880 ... 0x88F:
            reg_index = (offs - 0x880) >> 3;
            s->rvu_vfpf_mbox_int[reg_index] &= ~val;
            break;
        case 0x8C0 ... 0x8CF:
            reg_index = (offs - 0x8C0) >> 3;
            s->rvu_vfpf_mbox_int_ena[reg_index] |= val;
            break;
        case 0x8E0 ... 0x8EF:
            reg_index = (offs - 0x8E0) >> 3;
            s->rvu_vfpf_mbox_int_ena[reg_index] &= ~val;
            break;
        case 0x900 ... 0x90F:
            reg_index = (offs - 0x900) >> 3;
            s->rvu_vfflr_int[reg_index] &= ~val;
            break;
        case 0x940 ... 0x94F:
            reg_index = (offs - 0x940) >> 3;
            s->rvu_vfflr_int_ena[reg_index] |= val;
            break;
        case 0x960 ... 0x96F:
            reg_index = (offs - 0x960) >> 3;
            s->rvu_vfflr_int_ena[reg_index] &= ~val;
            break;
        case 0x980 ... 0x98F:
            reg_index = (offs - 0x980) >> 3;
            s->rvu_vfme_int[reg_index] &= ~val;
            break;
        case 0x9C0 ... 0x9CF:
            reg_index = (offs - 0x9C0) >> 3;
            s->rvu_vfme_int_ena[reg_index] |= val;
            break;
        case 0x9E0 ... 0x9EF:
            reg_index = (offs - 0x9E0) >> 3;
            s->rvu_vfme_int_ena[reg_index] &= ~val;
            break;
        case 0x820 ... 0x82F:
            reg_index = (offs - 0x820) >> 3;
            s->rvu_vftrpend[reg_index] = val;
            break;
        default:
            qemu_log_mask(LOG_UNIMP, "%s: RVU write unimplemented offset 0x%"PRIx64" val=0x%"PRIx64"\n",
                          __func__, offs, val);
            break;
        }
        break;
    case BLKADDR_CPT0:
    case BLKADDR_CPT1:
        if (slot >= OTX2_CPT_MAX_LFS_NUM) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid slot %d\n", __func__, (int)slot);
            return;
        }
        switch (offs) {
        case OTX2_CPT_LF_CTL:
            s->lf_state[slot].lf_ctl = val;
            break;
        case OTX2_CPT_LF_DONE_WAIT:
            s->lf_state[slot].lf_done_wait = val;
            break;
        case OTX2_CPT_LF_INPROG:
            s->lf_state[slot].lf_inprog = val;
            break;
        case OTX2_CPT_LF_MISC_INT_ENA_W1S:
            s->lf_state[slot].lf_misc_int_ena_w1s |= val;
            break;
        case OTX2_CPT_LF_MISC_INT_ENA_W1C:
            s->lf_state[slot].lf_misc_int_ena_w1c &= ~val;
            break;
        case OTX2_CPT_LF_Q_BASE:
            s->lf_state[slot].lf_q_base = val;
            break;
        case OTX2_CPT_LF_Q_SIZE:
            s->lf_state[slot].lf_q_size = val;
            break;
        case OTX2_CPT_LF_Q_INST_PTR:
            s->lf_state[slot].lf_q_inst_ptr = val;
            break;
        case OTX2_CPT_LF_CTX_CTL:
            s->lf_state[slot].lf_ctx_ctl = val;
            break;
        default:
            qemu_log_mask(LOG_UNIMP, "%s: CPT LF write unimplemented offset 0x%"PRIx64" val=0x%"PRIx64"\n",
                          __func__, offs, val);
            break;
        }
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: write to unimplemented block %d\n", __func__, (int)blk);
        break;
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

    s->rvu_pf_int = 0;
    s->rvu_pf_int_ena = 0;
    s->rvu_pf_vf_mbox_addr = 0xdeadbeef00000000ULL;
    s->rvu_pf_vf_bar4_addr = 0xdeadbeef00001000ULL;
    memset(s->rvu_vfpf_mbox_int, 0, sizeof(s->rvu_vfpf_mbox_int));
    memset(s->rvu_vfpf_mbox_int_ena, 0, sizeof(s->rvu_vfpf_mbox_int_ena));
    memset(s->rvu_vfflr_int, 0, sizeof(s->rvu_vfflr_int));
    memset(s->rvu_vfflr_int_ena, 0, sizeof(s->rvu_vfflr_int_ena));
    memset(s->rvu_vfme_int, 0, sizeof(s->rvu_vfme_int));
    memset(s->rvu_vfme_int_ena, 0, sizeof(s->rvu_vfme_int_ena));
    memset(s->rvu_vftrpend, 0, sizeof(s->rvu_vftrpend));

    memset(s->lf_state, 0, sizeof(s->lf_state));

    s->af_mbox_pending = false;
    memset(s->mbox_buf, 0, MBOX_SIZE);
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        if (bi->index == PCI_MBOX_BAR_NUM) {
            memory_region_init_io(mr, OBJECT(s), &pcibase_mbox_ops, s, bi->name, aligned_size);
        } else {
            memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, aligned_size);
        }
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
    BARInfo bar_info;

    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_CAVIUM);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_OTX2_CPT_PF);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x1080); /* Crypto: Other */
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* MSI-X initialization using exclusive bar on BAR1 */
    if (msix_init_exclusive_bar(pdev, OTX2_CPT_LF_MSIX_VECTORS, PCI_MSIX_BAR_NUM, errp) < 0) {
        return;
    }

    /* Initialize AF mailbox timer */
    s->af_mbox_timer = timer_new_ms(QEMU_CLOCK_VIRTUAL, pcibase_af_mbox_timer_cb, s);

    /* BAR2: Register space (MMIO) */
    bar_info.index = PCI_PF_REG_BAR_NUM;
    bar_info.type = BAR_TYPE_MMIO;
    bar_info.size = 16 * MiB;
    bar_info.name = "otx2-cpt-pf-regs";
    pcibase_register_bar(pdev, s, &bar_info, errp);

    /* BAR4: Mailbox (MMIO with side effects) */
    bar_info.index = PCI_MBOX_BAR_NUM;
    bar_info.type = BAR_TYPE_MMIO;
    bar_info.size = MBOX_SIZE;
    bar_info.name = "otx2-cpt-pf-mbox";
    pcibase_register_bar(pdev, s, &bar_info, errp);

    s->num_bars = 2; /* Only BAR2 and BAR4 are used for device logic; BAR1 is MSI-X */
    s->has_msix = true;
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (s->af_mbox_timer) {
        timer_del(s->af_mbox_timer);
        timer_free(s->af_mbox_timer);
    }
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
}

static const VMStateDescription vmstate_pcibase = {
    .name = "rvu_cptpf_pci",
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
