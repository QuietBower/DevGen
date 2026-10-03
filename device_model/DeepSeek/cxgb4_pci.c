/*
 * QEMU Chelsio T5 Virtual PCI Device for cxgb4 driver (QEMU 8.2.10)
 * This device emulates enough of the hardware to let the driver probe successfully.
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

#define TYPE_PCIBASE_DEVICE "cxgb4_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* -- PCI IDs -- */
#define CHELSIO_VENDOR_ID           0x1425
#define CXGB4_DEVICE_ID             0x5401  /* T5 device */
#define PCI_CLASS_NETWORK_ETHERNET  0x0200

/* -- Register Offsets -- */
#define CIM_PF_MAILBOX_DATA_A       0x240
#define CIM_PF_MAILBOX_CTRL_A       0x280
#define PL_WHOAMI_A                 0x19400
#define PL_REV_A                    0x1943c
#define PCIE_FW_A                   0x30b8
#define SGE_CONTROL_A               0x1008
#define SGE_HOST_PAGE_SIZE_A        0x100c
#define SGE_EGRESS_QUEUES_PER_PAGE_PF_A 0x1010  /* invented offset, not exact */
#define SGE_CONM_CTRL_A             0x1094
#define SGE_DBVFIFO_BADDR_A         0x1138
#define SGE_DBVFIFO_SIZE_A          0x113c
#define SGE_DBQ_CTXT_BADDR_A        0x1080
#define SGE_DBFIFO_STATUS_A         0x109c
#define SGE_DBFIFO_STATUS2_A        0x10a0
#define SGE_TIMESTAMP_LO_A          0x1090
#define SGE_TIMESTAMP_HI_A          0x1094
#define MPS_CMN_CTL_A               0x9000
#define LE_DB_CONFIG_A              0x19c04
#define LE_DB_HASH_CONFIG_A         0x19c28
#define LE_DB_TID_HASHBASE_A        0x19c2c
#define LE_DB_ACTIVE_TABLE_START_INDEX_A 0x19c30
#define T6_LE_DB_HASH_TID_BASE_A    0x19c34  /* poss. */
#define TP_SHIFT_CNT_A              0x1a000
#define TP_INGRESS_CONFIG_A         0x1a008
#define TP_PIO_ADDR_A               0x1a010
#define TP_PIO_DATA_A               0x1a014
#define TP_TX_MOD_QUEUE_REQ_MAP_A   0x1a020
#define TP_TX_MOD_QUEUE_WEIGHT0_A   0x1a030
#define TP_TX_MOD_CHANNEL_WEIGHT_A  0x1a040
#define ULP_RX_TDDP_PSZ_A           0x1a800
#define MA_EDRAM0_BAR_A             0x100
#define MA_EDRAM1_BAR_A             0x104
#define MA_EXT_MEMORY0_BAR_A        0x108
#define MA_EXT_MEMORY1_BAR_A        0x10c
#define MA_TARGET_MEM_ENABLE_A      0x110
#define SGE_DOORBELL_CONTROL_A      0x10b0
#define SGE_INT_ENABLE3_A           0x10c0

/* -- Firmware opcodes (subset) -- */
#define FW_HELLO_CMD                0x01
#define FW_CAPS_CONFIG_CMD          0x08
#define FW_PARAMS_CMD               0x09
#define FW_PFVF_CMD                 0x0a

/* -- Mailbox constants -- */
#define MAILBOX_DATA_SIZE           64

/* -- Parameter mnemonics -- */
#define FW_PARAMS_MNEM_V(m)         ((m) << 16)
#define FW_PARAMS_PARAM_X_V(x)      ((x) << 8)
#define FW_PARAMS_PARAM_YZ_V(yz)    ((yz) << 0)

#define FW_PARAMS_MNEM_DEV          1
#define FW_PARAMS_MNEM_PFVF         2

/* -- DEV param X values -- */
#define FW_PARAMS_PARAM_DEV_PORTVEC            0x00
#define FW_PARAMS_PARAM_DEV_CF                 0x07
#define FW_PARAMS_PARAM_DEV_HMA_SIZE           0x0d
#define FW_PARAMS_PARAM_DEV_DBQ_TIMERTICK      0x10
#define FW_PARAMS_PARAM_DEV_ULPTX_MEMWRITE_DSGL 0x17
#define FW_PARAMS_PARAM_DEV_RI_FR_NSMR_TPTE_WR 0x18
#define FW_PARAMS_PARAM_DEV_FILTER2_WR         0x19
#define FW_PARAMS_PARAM_DEV_OPAQUE_VIID_SMT_EXTN 0x1a
#define FW_PARAMS_PARAM_DEV_NTID               0x1b
#define FW_PARAMS_PARAM_DEV_NUM_TM_CLASS       0x1c
#define FW_PARAMS_PARAM_DEV_MAXORDIRD_QP       0x1d
#define FW_PARAMS_PARAM_DEV_MAXIRD_ADAPTER     0x1e
#define FW_PARAMS_PARAM_DEV_FLOWC_BUFFIFO_SZ   0x1f
#define FW_PARAMS_PARAM_DEV_RDMA_WRITE_WITH_IMM 0x20
#define FW_PARAMS_PARAM_DEV_RI_WRITE_CMPL_WR   0x21
#define FW_PARAMS_PARAM_DEV_HPFILTER_REGION_SUPPORT 0x22
/* -- PFVF param X values -- */
#define FW_PARAMS_PARAM_PFVF_EQ_START          0x00
#define FW_PARAMS_PARAM_PFVF_L2T_START         0x01
#define FW_PARAMS_PARAM_PFVF_L2T_END           0x02
#define FW_PARAMS_PARAM_PFVF_FILTER_START      0x03
#define FW_PARAMS_PARAM_PFVF_FILTER_END        0x04
#define FW_PARAMS_PARAM_PFVF_IQLINT_START      0x05
#define FW_PARAMS_PARAM_PFVF_EQ_END            0x06
#define FW_PARAMS_PARAM_PFVF_IQLINT_END        0x07
#define FW_PARAMS_PARAM_PFVF_CLIP_START        0x08
#define FW_PARAMS_PARAM_PFVF_CLIP_END          0x09
#define FW_PARAMS_PARAM_PFVF_HPFILTER_START    0x0a
#define FW_PARAMS_PARAM_PFVF_HPFILTER_END      0x0b
#define FW_PARAMS_PARAM_PFVF_RAWF_START        0x0c
#define FW_PARAMS_PARAM_PFVF_RAWF_END          0x0d
#define FW_PARAMS_PARAM_PFVF_ACTIVE_FILTER_START 0x0e
#define FW_PARAMS_PARAM_PFVF_ACTIVE_FILTER_END   0x0f
#define FW_PARAMS_PARAM_PFVF_CPLFW4MSG_ENCAP   0x10
#define FW_PARAMS_PARAM_PFVF_ETHOFLD_START     0x11
#define FW_PARAMS_PARAM_PFVF_ETHOFLD_END       0x12
#define FW_PARAMS_PARAM_PFVF_SERVER_START      0x13
#define FW_PARAMS_PARAM_PFVF_SERVER_END        0x14
#define FW_PARAMS_PARAM_PFVF_TDDP_START        0x15
#define FW_PARAMS_PARAM_PFVF_TDDP_END          0x16
#define FW_PARAMS_PARAM_PFVF_STAG_START        0x17
#define FW_PARAMS_PARAM_PFVF_STAG_END          0x18
#define FW_PARAMS_PARAM_PFVF_RQ_START          0x19
#define FW_PARAMS_PARAM_PFVF_RQ_END            0x1a
#define FW_PARAMS_PARAM_PFVF_PBL_START         0x1b
#define FW_PARAMS_PARAM_PFVF_PBL_END           0x1c
#define FW_PARAMS_PARAM_PFVF_SRQ_START         0x1d
#define FW_PARAMS_PARAM_PFVF_SRQ_END           0x1e
#define FW_PARAMS_PARAM_PFVF_SQRQ_START        0x1f
#define FW_PARAMS_PARAM_PFVF_SQRQ_END          0x20
#define FW_PARAMS_PARAM_PFVF_CQ_START          0x21
#define FW_PARAMS_PARAM_PFVF_CQ_END            0x22
#define FW_PARAMS_PARAM_PFVF_OCQ_START         0x23
#define FW_PARAMS_PARAM_PFVF_OCQ_END           0x24
#define FW_PARAMS_PARAM_PFVF_ISCSI_START       0x25
#define FW_PARAMS_PARAM_PFVF_ISCSI_END         0x26
#define FW_PARAMS_PARAM_PFVF_PPOD_EDRAM_START  0x27
#define FW_PARAMS_PARAM_PFVF_PPOD_EDRAM_END    0x28
#define FW_PARAMS_PARAM_PFVF_NCRYPTO_LOOKASIDE 0x29
#define FW_PARAMS_PARAM_PFVF_TLS_START         0x2a
#define FW_PARAMS_PARAM_PFVF_TLS_END           0x2b
#define FW_PARAMS_PARAM_PFVF_HASHFILTER_WITH_OFLD 0x2c

/* -- Device state -- */
#define DEV_STATE_INIT  1

/* -- BAR sizes -- */
#define BAR0_SIZE                   0x200000
#define BAR2_SIZE                   0x200000

/* -- SOURCEPF mask (T5) -- */
#define SOURCEPF_G(x)               (((x) >> 0) & 0x7)

/* -- Misc driver constants (for parameter replies) -- */
#define CHELSIO_T4 4
#define CHELSIO_T5 5
#define CHELSIO_T6 6

/* ------------------------------------------------------------------------- */

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar0_mmio;
    MemoryRegion bar2_ram;

    /* Shadow registers */
    uint32_t pl_whomai;
    uint32_t pl_rev;
    uint32_t pcie_fw;
    uint32_t sge_control;
    uint32_t sge_host_page_size;
    uint32_t sge_egress_queues_per_page_pf;
    uint32_t sge_conm_ctrl;
    uint32_t sge_dbvfifo_baddr;
    uint32_t sge_dbvfifo_size;
    uint32_t sge_dbq_ctxt_baddr;
    uint32_t sge_doorbell_control;
    uint32_t sge_int_enable3;
    uint32_t le_db_config;
    uint32_t le_db_hash_config;
    uint32_t le_db_tid_hashbase;
    uint32_t le_db_active_table_start_index;
    uint32_t tp_shift_cnt;
    uint32_t tp_ingress_config;
    uint32_t tp_pio_addr;
    uint32_t tp_pio_data;
    uint32_t tp_tx_mod_queue_req_map;
    uint32_t tp_tx_mod_queue_weight0;
    uint32_t tp_tx_mod_channel_weight;
    uint32_t ulp_rx_tddp_psz;
    uint32_t mps_cmn_ctl;
    uint32_t ma_edram0_bar;
    uint32_t ma_edram1_bar;
    uint32_t ma_ext_memory0_bar;
    uint32_t ma_ext_memory1_bar;
    uint32_t ma_target_mem_enable;

    /* Mailbox data */
    uint32_t mailbox_data[MAILBOX_DATA_SIZE / 4];
    uint32_t mailbox_ctrl;

    /* Parameter table for FW_PARAMS_CMD queries */
    uint32_t dev_params[256];   /* index by param X */
    uint32_t pfvf_params[256];  /* index by param X */
};

/* -------------------------------------------------------------------------
 * Helper: process FW_PARAMS_CMD request / response
 */
static void handle_fw_params_cmd(PCIBaseState *s, uint32_t *cmd, int len_bytes)
{
    int i, num_params;
    uint32_t *p;
    uint32_t len16 = (cmd[1] >> 0) & 0xffff;  /* number of 16-byte units */
    num_params = (len16 * 16 - 8) / 8;        /* after 8-byte header */
    p = cmd + 2;                              /* start of param pairs */

    for (i = 0; i < num_params; i++) {
        uint32_t param = p[i*2];
        uint32_t mnem = (param >> 16) & 0xff;
        uint32_t x    = (param >> 8) & 0xff;

        if (mnem == FW_PARAMS_MNEM_DEV) {
            p[i*2 + 1] = s->dev_params[x];
        } else if (mnem == FW_PARAMS_MNEM_PFVF) {
            p[i*2 + 1] = s->pfvf_params[x];
        } else {
            p[i*2 + 1] = 0;
        }
    }
    cmd[1] = (len16 << 0) | (0 << 16);  /* retval = 0, success */
}

/* -------------------------------------------------------------------------
 * Helper: process FW_PFVF_CMD (resource allocation) request
 */
static void handle_fw_pfvf_cmd(PCIBaseState *s, uint32_t *cmd, int len_bytes)
{
    /* For a read (op_to_write & 0x10?), return pre-set resources.
     * The driver uses t4_get_pfres which does a read of PFVF_CMD.
     * The response structure: many fields, we fill default values.
     * Offsets within the cmd buffer after header:
     *   word[2] : type_to_neq? We'll write neq=128, niqflint=128 etc.
     * Simple: use good defaults to let cfg_queues succeed.
     */
    uint32_t *p = cmd + 2;

    /* The driver expects: neq, niqflint, etc. We'll set generous numbers. */
    p[0] = 0;                       /* type_to_neq: neq=0? Actually, FW_PFVF_CMD response has many */
    p[1] = 0;
    p[2] = 0;
    p[3] = 0;
    /* We'll use the known offsets from t4fw_interface.h: 
       after header, there is neq, niqflint, etc. Let's just set enough resources. */
    /* For simplicity, fill the whole rest of the 64-byte buffer with zeros, but set
       a few critical fields to non-zero. */
    memset(p, 0, MAILBOX_DATA_SIZE - 8);
    /* neq (offset 0x0) */
    p[0] = 512;                     /* neq = 512 */
    /* niqflint (offset 0x4) */
    p[1] = 512;                     /* niqflint = 512 */
    /* there are many more fields, but leaving them 0 is okay for probe. */

    cmd[1] = (4 << 0) | (0 << 16);  /* retval = 0, len16 = 4 (64 bytes) */
}

/* -------------------------------------------------------------------------
 * Process a mailbox command from the driver.
 */
static void pcibase_handle_mailbox(PCIBaseState *s)
{
    uint32_t opcode = s->mailbox_data[0] & 0xff;
    int len_bytes = ((s->mailbox_data[1] & 0xffff) * 16);
    uint32_t retval = 0;

    switch (opcode) {
    case FW_HELLO_CMD:
        /* Return success, state = DEV_STATE_INIT (already initialized). */
        s->mailbox_data[2] = DEV_STATE_INIT;  /* state in response */
        s->mailbox_data[1] = (4 << 0) | (0 << 16);  /* retval=0, len16=4 */
        break;

    case FW_CAPS_CONFIG_CMD:
        /* For read, return all capabilities 0 (no offload). */
        memset(&s->mailbox_data[2], 0, MAILBOX_DATA_SIZE - 8);
        s->mailbox_data[1] = (8 << 0) | (0 << 16);  /* retval=0, len16=8 */
        break;

    case FW_PARAMS_CMD:
        handle_fw_params_cmd(s, s->mailbox_data, len_bytes);
        break;

    case FW_PFVF_CMD:
        handle_fw_pfvf_cmd(s, s->mailbox_data, len_bytes);
        break;

    default:
        /* unknown command: return success with zero data */
        memset(&s->mailbox_data[2], 0, MAILBOX_DATA_SIZE - 8);
        s->mailbox_data[1] = (4 << 0) | (0 << 16);
        break;
    }
}

/* -------------------------------------------------------------------------
 * MMIO read handler for BAR0
 */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    uint32_t offset = addr;

    if (offset >= CIM_PF_MAILBOX_DATA_A && offset < CIM_PF_MAILBOX_DATA_A + MAILBOX_DATA_SIZE) {
        int idx = (offset - CIM_PF_MAILBOX_DATA_A) / 4;
        if (idx < MAILBOX_DATA_SIZE / 4) {
            val = s->mailbox_data[idx];
        }
        return val;
    }

    switch (offset) {
    case CIM_PF_MAILBOX_CTRL_A:
        val = s->mailbox_ctrl;
        break;
    case PL_WHOAMI_A:
        val = s->pl_whomai;
        break;
    case PL_REV_A:
        val = s->pl_rev;
        break;
    case PCIE_FW_A:
        val = s->pcie_fw;
        break;
    case SGE_CONTROL_A:
        val = s->sge_control;
        break;
    case SGE_HOST_PAGE_SIZE_A:
        val = s->sge_host_page_size;
        break;
    case SGE_EGRESS_QUEUES_PER_PAGE_PF_A:
        val = s->sge_egress_queues_per_page_pf;
        break;
    case SGE_CONM_CTRL_A:
        val = s->sge_conm_ctrl;
        break;
    case SGE_DBVFIFO_BADDR_A:
        val = s->sge_dbvfifo_baddr;
        break;
    case SGE_DBVFIFO_SIZE_A:
        val = s->sge_dbvfifo_size;
        break;
    case SGE_DBQ_CTXT_BADDR_A:
        val = s->sge_dbq_ctxt_baddr;
        break;
    case SGE_DOORBELL_CONTROL_A:
        val = s->sge_doorbell_control;
        break;
    case SGE_INT_ENABLE3_A:
        val = s->sge_int_enable3;
        break;
    case MPS_CMN_CTL_A:
        val = s->mps_cmn_ctl;
        break;
    case LE_DB_CONFIG_A:
        val = s->le_db_config;
        break;
    case LE_DB_HASH_CONFIG_A:
        val = s->le_db_hash_config;
        break;
    case LE_DB_TID_HASHBASE_A:
        val = s->le_db_tid_hashbase;
        break;
    case LE_DB_ACTIVE_TABLE_START_INDEX_A:
        val = s->le_db_active_table_start_index;
        break;
    case TP_SHIFT_CNT_A:
        val = s->tp_shift_cnt;
        break;
    case TP_INGRESS_CONFIG_A:
        val = s->tp_ingress_config;
        break;
    case TP_PIO_ADDR_A:
        val = s->tp_pio_addr;
        break;
    case TP_PIO_DATA_A:
        val = s->tp_pio_data;
        break;
    case TP_TX_MOD_QUEUE_REQ_MAP_A:
        val = s->tp_tx_mod_queue_req_map;
        break;
    case TP_TX_MOD_QUEUE_WEIGHT0_A:
        val = s->tp_tx_mod_queue_weight0;
        break;
    case TP_TX_MOD_CHANNEL_WEIGHT_A:
        val = s->tp_tx_mod_channel_weight;
        break;
    case ULP_RX_TDDP_PSZ_A:
        val = s->ulp_rx_tddp_psz;
        break;
    case MA_EDRAM0_BAR_A:
        val = s->ma_edram0_bar;
        break;
    case MA_EDRAM1_BAR_A:
        val = s->ma_edram1_bar;
        break;
    case MA_EXT_MEMORY0_BAR_A:
        val = s->ma_ext_memory0_bar;
        break;
    case MA_EXT_MEMORY1_BAR_A:
        val = s->ma_ext_memory1_bar;
        break;
    case MA_TARGET_MEM_ENABLE_A:
        val = s->ma_target_mem_enable;
        break;
    default:
        /* Return 0 for unknown registers */
        val = 0;
        break;
    }

    return val;
}

/* -------------------------------------------------------------------------
 * MMIO write handler for BAR0
 */
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t offset = addr;
    uint32_t v32 = (uint32_t)val;

    if (offset >= CIM_PF_MAILBOX_DATA_A && offset < CIM_PF_MAILBOX_DATA_A + MAILBOX_DATA_SIZE) {
        int idx = (offset - CIM_PF_MAILBOX_DATA_A) / 4;
        if (idx < MAILBOX_DATA_SIZE / 4) {
            s->mailbox_data[idx] = v32;
        }
        return;
    }

    switch (offset) {
    case CIM_PF_MAILBOX_CTRL_A:
        if (v32 != 0) {
            s->mailbox_ctrl = v32;
            pcibase_handle_mailbox(s);
            s->mailbox_ctrl &= ~1;  /* clear owner */
        } else {
            s->mailbox_ctrl = v32;
        }
        break;
    case PL_WHOAMI_A:
        s->pl_whomai = v32;
        break;
    case PL_REV_A:
        s->pl_rev = v32;
        break;
    case PCIE_FW_A:
        s->pcie_fw = v32;
        break;
    case SGE_CONTROL_A:
        s->sge_control = v32;
        break;
    case SGE_HOST_PAGE_SIZE_A:
        s->sge_host_page_size = v32;
        break;
    case SGE_EGRESS_QUEUES_PER_PAGE_PF_A:
        s->sge_egress_queues_per_page_pf = v32;
        break;
    case SGE_CONM_CTRL_A:
        s->sge_conm_ctrl = v32;
        break;
    case SGE_DBVFIFO_BADDR_A:
        s->sge_dbvfifo_baddr = v32;
        break;
    case SGE_DBVFIFO_SIZE_A:
        s->sge_dbvfifo_size = v32;
        break;
    case SGE_DBQ_CTXT_BADDR_A:
        s->sge_dbq_ctxt_baddr = v32;
        break;
    case SGE_DOORBELL_CONTROL_A:
        s->sge_doorbell_control = v32;
        break;
    case SGE_INT_ENABLE3_A:
        s->sge_int_enable3 = v32;
        break;
    case MPS_CMN_CTL_A:
        s->mps_cmn_ctl = v32;
        break;
    case LE_DB_CONFIG_A:
        s->le_db_config = v32;
        break;
    case LE_DB_HASH_CONFIG_A:
        s->le_db_hash_config = v32;
        break;
    case LE_DB_TID_HASHBASE_A:
        s->le_db_tid_hashbase = v32;
        break;
    case LE_DB_ACTIVE_TABLE_START_INDEX_A:
        s->le_db_active_table_start_index = v32;
        break;
    case TP_SHIFT_CNT_A:
        s->tp_shift_cnt = v32;
        break;
    case TP_INGRESS_CONFIG_A:
        s->tp_ingress_config = v32;
        break;
    case TP_PIO_ADDR_A:
        s->tp_pio_addr = v32;
        break;
    case TP_PIO_DATA_A:
        s->tp_pio_data = v32;
        break;
    case TP_TX_MOD_QUEUE_REQ_MAP_A:
        s->tp_tx_mod_queue_req_map = v32;
        break;
    case TP_TX_MOD_QUEUE_WEIGHT0_A:
        s->tp_tx_mod_queue_weight0 = v32;
        break;
    case TP_TX_MOD_CHANNEL_WEIGHT_A:
        s->tp_tx_mod_channel_weight = v32;
        break;
    case ULP_RX_TDDP_PSZ_A:
        s->ulp_rx_tddp_psz = v32;
        break;
    case MA_EDRAM0_BAR_A:
        s->ma_edram0_bar = v32;
        break;
    case MA_EDRAM1_BAR_A:
        s->ma_edram1_bar = v32;
        break;
    case MA_EXT_MEMORY0_BAR_A:
        s->ma_ext_memory0_bar = v32;
        break;
    case MA_EXT_MEMORY1_BAR_A:
        s->ma_ext_memory1_bar = v32;
        break;
    case MA_TARGET_MEM_ENABLE_A:
        s->ma_target_mem_enable = v32;
        break;
    default:
        /* ignore writes to unknown registers */
        break;
    }
}

/* -------------------------------------------------------------------------
 * Memory region ops for BAR0
 */
static const MemoryRegionOps pcibase_bar0_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

/* -------------------------------------------------------------------------
 * Reset handler
 */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    memset(&s->mailbox_data, 0, sizeof(s->mailbox_data));
    s->mailbox_ctrl = 0;

    /* Set PCIe FW ready */
    s->pcie_fw = 1 << 30;  /* PCIE_FW_INIT_S set */

    /* T5 A1: PL_WHOAMI (PF0, maybe sourcepf = 0) */
    s->pl_whomai = 0x00000000;  /* adjust if needed */
    s->pl_rev = 0x00010000;     /* rev = 1 (A1) */

    /* SGE defaults */
    s->sge_control          = 0;
    s->sge_host_page_size   = 0;    /* 4K page */
    s->sge_egress_queues_per_page_pf = 0;  /* qpp=1 */
    s->sge_conm_ctrl        = 0;
    s->sge_dbvfifo_baddr    = 0;
    s->sge_dbvfifo_size     = 0;
    s->sge_dbq_ctxt_baddr   = 0;
    s->sge_doorbell_control = 0;
    s->sge_int_enable3      = 0;

    /* MPS */
    s->mps_cmn_ctl = 0;

    /* LE DB */
    s->le_db_config         = 0;
    s->le_db_hash_config    = 0;
    s->le_db_tid_hashbase   = 0;
    s->le_db_active_table_start_index = 0;

    /* TP */
    s->tp_shift_cnt             = 0;
    s->tp_ingress_config        = 0;
    s->tp_pio_addr              = 0;
    s->tp_pio_data              = 0;
    s->tp_tx_mod_queue_req_map  = 0;
    s->tp_tx_mod_queue_weight0  = 0;
    s->tp_tx_mod_channel_weight = 0;

    /* ULP */
    s->ulp_rx_tddp_psz = 0;

    /* MA */
    s->ma_edram0_bar         = 0;
    s->ma_edram1_bar         = 0;
    s->ma_ext_memory0_bar    = 0;
    s->ma_ext_memory1_bar    = 0;
    s->ma_target_mem_enable  = 0;

    /* --- Parameter table defaults --- */
    memset(s->dev_params, 0, sizeof(s->dev_params));
    memset(s->pfvf_params, 0, sizeof(s->pfvf_params));

    /* DEV params needed by the driver */
    s->dev_params[FW_PARAMS_PARAM_DEV_PORTVEC] = 0x3;  /* two ports */
    s->dev_params[FW_PARAMS_PARAM_DEV_DBQ_TIMERTICK] = 0x10;
    s->dev_params[FW_PARAMS_PARAM_DEV_HMA_SIZE] = 0;
    s->dev_params[FW_PARAMS_PARAM_DEV_ULPTX_MEMWRITE_DSGL] = 0;
    s->dev_params[FW_PARAMS_PARAM_DEV_RI_FR_NSMR_TPTE_WR] = 0;
    s->dev_params[FW_PARAMS_PARAM_DEV_FILTER2_WR] = 0;
    s->dev_params[FW_PARAMS_PARAM_DEV_OPAQUE_VIID_SMT_EXTN] = 0;
    s->dev_params[FW_PARAMS_PARAM_DEV_NTID] = 1024;  /* ntids */
    s->dev_params[FW_PARAMS_PARAM_DEV_NUM_TM_CLASS] = 16;
    s->dev_params[FW_PARAMS_PARAM_DEV_MAXORDIRD_QP] = 8;
    s->dev_params[FW_PARAMS_PARAM_DEV_MAXIRD_ADAPTER] = 32 * 1024;
    s->dev_params[FW_PARAMS_PARAM_DEV_FLOWC_BUFFIFO_SZ] = 0;
    s->dev_params[FW_PARAMS_PARAM_DEV_RDMA_WRITE_WITH_IMM] = 0;
    s->dev_params[FW_PARAMS_PARAM_DEV_RI_WRITE_CMPL_WR] = 0;
    s->dev_params[FW_PARAMS_PARAM_DEV_HPFILTER_REGION_SUPPORT] = 0;

    /* PFVF params */
    s->pfvf_params[FW_PARAMS_PARAM_PFVF_EQ_START] = 0;
    s->pfvf_params[FW_PARAMS_PARAM_PFVF_L2T_START] = 2000;
    s->pfvf_params[FW_PARAMS_PARAM_PFVF_L2T_END]   = 3000;
    s->pfvf_params[FW_PARAMS_PARAM_PFVF_FILTER_START] = 3000;
    s->pfvf_params[FW_PARAMS_PARAM_PFVF_FILTER_END]   = 4000;
    s->pfvf_params[FW_PARAMS_PARAM_PFVF_IQLINT_START] = 0;  /* ingr_start */
    s->pfvf_params[FW_PARAMS_PARAM_PFVF_EQ_END]       = 100;
    s->pfvf_params[FW_PARAMS_PARAM_PFVF_IQLINT_END]   = 100;
    s->pfvf_params[FW_PARAMS_PARAM_PFVF_CLIP_START]   = 4096;
    s->pfvf_params[FW_PARAMS_PARAM_PFVF_CLIP_END]     = 8191;
    s->pfvf_params[FW_PARAMS_PARAM_PFVF_HPFILTER_START] = 0;
    s->pfvf_params[FW_PARAMS_PARAM_PFVF_HPFILTER_END]   = 0;
    s->pfvf_params[FW_PARAMS_PARAM_PFVF_RAWF_START]     = 0;
    s->pfvf_params[FW_PARAMS_PARAM_PFVF_RAWF_END]       = 0;
    s->pfvf_params[FW_PARAMS_PARAM_PFVF_ACTIVE_FILTER_START] = 0;
    s->pfvf_params[FW_PARAMS_PARAM_PFVF_ACTIVE_FILTER_END]   = 0;
    s->pfvf_params[FW_PARAMS_PARAM_PFVF_CPLFW4MSG_ENCAP] = 1;  /* enable */
    s->pfvf_params[FW_PARAMS_PARAM_PFVF_ETHOFLD_START]  = 0;
    s->pfvf_params[FW_PARAMS_PARAM_PFVF_ETHOFLD_END]    = 0;
    s->pfvf_params[FW_PARAMS_PARAM_PFVF_SERVER_START]   = 0;
    s->pfvf_params[FW_PARAMS_PARAM_PFVF_SERVER_END]     = 0;
    s->pfvf_params[FW_PARAMS_PARAM_PFVF_TDDP_START]     = 0;
    s->pfvf_params[FW_PARAMS_PARAM_PFVF_TDDP_END]       = 0;
    s->pfvf_params[FW_PARAMS_PARAM_PFVF_STAG_START]     = 0;
    s->pfvf_params[FW_PARAMS_PARAM_PFVF_STAG_END]       = 0;
    s->pfvf_params[FW_PARAMS_PARAM_PFVF_RQ_START]       = 0;
    s->pfvf_params[FW_PARAMS_PARAM_PFVF_RQ_END]         = 0;
    s->pfvf_params[FW_PARAMS_PARAM_PFVF_PBL_START]      = 0;
    s->pfvf_params[FW_PARAMS_PARAM_PFVF_PBL_END]        = 0;
    s->pfvf_params[FW_PARAMS_PARAM_PFVF_SRQ_START]      = 0;
    s->pfvf_params[FW_PARAMS_PARAM_PFVF_SRQ_END]        = 0;
    s->pfvf_params[FW_PARAMS_PARAM_PFVF_SQRQ_START]     = 0;
    s->pfvf_params[FW_PARAMS_PARAM_PFVF_SQRQ_END]       = 0;
    s->pfvf_params[FW_PARAMS_PARAM_PFVF_CQ_START]       = 0;
    s->pfvf_params[FW_PARAMS_PARAM_PFVF_CQ_END]         = 0;
    s->pfvf_params[FW_PARAMS_PARAM_PFVF_OCQ_START]      = 0;
    s->pfvf_params[FW_PARAMS_PARAM_PFVF_OCQ_END]        = 0;
    s->pfvf_params[FW_PARAMS_PARAM_PFVF_ISCSI_START]    = 0;
    s->pfvf_params[FW_PARAMS_PARAM_PFVF_ISCSI_END]      = 0;
    s->pfvf_params[FW_PARAMS_PARAM_PFVF_PPOD_EDRAM_START] = 0;
    s->pfvf_params[FW_PARAMS_PARAM_PFVF_PPOD_EDRAM_END]   = 0;
    s->pfvf_params[FW_PARAMS_PARAM_PFVF_NCRYPTO_LOOKASIDE] = 0;
    s->pfvf_params[FW_PARAMS_PARAM_PFVF_TLS_START]      = 0;
    s->pfvf_params[FW_PARAMS_PARAM_PFVF_TLS_END]        = 0;
    s->pfvf_params[FW_PARAMS_PARAM_PFVF_HASHFILTER_WITH_OFLD] = 0;
}

/* -------------------------------------------------------------------------
 * Realize: PCI initialization
 */
static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, CHELSIO_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, CXGB4_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_ETHERNET);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR0: main MMIO register space */
    memory_region_init_io(&s->bar0_mmio, OBJECT(s), &pcibase_bar0_ops, s,
                          "bar0", BAR0_SIZE);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar0_mmio);

    /* BAR2: doorbell / WC memory (required for T5) */
    memory_region_init_ram(&s->bar2_ram, OBJECT(s), "bar2", BAR2_SIZE, errp);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar2_ram);

    /* No MSI-X */
}

/* -------------------------------------------------------------------------
 * Uninit
 */
static void pcibase_uninit(PCIDevice *pdev)
{
    /* nothing extra */
}

/* -------------------------------------------------------------------------
 * VMState
 */
static const VMStateDescription vmstate_pcibase = {
    .name = "cxgb4_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_END_OF_LIST()
    }
};

/* -------------------------------------------------------------------------
 * Class init
 */
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

static const TypeInfo pcibase_info = {
    .name = TYPE_PCIBASE_DEVICE,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIBaseState),
    .class_init = pcibase_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_PCIE_DEVICE },
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void pcibase_register_types(void)
{
    type_register_static(&pcibase_info);
}

type_init(pcibase_register_types)
