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

/* Additional include files retrieved from driver context */

#define TYPE_PCIBASE_DEVICE "cassini_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */

#define PCI_VENDOR_ID_SUN         0x108e
#define PCI_DEVICE_ID_SUN_CASSINI 0xabba
#define PCI_CLASS_NETWORK_ETHERNET 0x0200

/* Register Offsets */
#define REG_CAWR                   0x0004
#define REG_INF_BURST              0x0008
#define REG_INTR_STATUS            0x000C
#define REG_INTR_MASK              0x0010
#define REG_ALIAS_CLEAR            0x0014
#define REG_INTR_STATUS_ALIAS      0x001C
#define REG_BIM_CFG                0x1008
#define REG_BIM_DIAG               0x100C
#define REG_PCI_ERR_STATUS         0x1000
#define REG_PCI_ERR_STATUS_MASK    0x1004
#define REG_SW_RESET               0x1010
#define REG_BIM_LOCAL_DEV_EN       0x1020
#define REG_PLUS_INTR_MASK_1       0x1038
#define REG_SATURN_PCFG            0x106c
#define REG_EXPANSION_ROM_RUN_START 0x100000
#define REG_TX_CFG                 0x2004
#define REG_TX_FIFO_WRITE_PTR      0x2014
#define REG_TX_FIFO_READ_PTR       0x201C
#define REG_TX_FIFO_PKT_CNT        0x2024
#define REG_TX_SM_1                0x2028
#define REG_TX_SM_2                0x202C
#define REG_TX_KICK0               0x2038
#define REG_TX_COMP0               0x2048
#define REG_TX_COMPWB_DB_LOW       0x2058
#define REG_TX_COMPWB_DB_HI        0x205C
#define REG_TX_DB0_LOW             0x2060
#define REG_TX_DB0_HI              0x2064
#define REG_TX_MAXBURST_0          0x2080
#define REG_TX_MAXBURST_1          0x2084
#define REG_TX_MAXBURST_2          0x2088
#define REG_TX_MAXBURST_3          0x208C
#define REG_TX_FIFO_SIZE           0x2118
#define REG_RX_CFG                 0x4000
#define REG_RX_PAGE_SIZE           0x4004
#define REG_RX_PAUSE_THRESH        0x4020
#define REG_RX_KICK                0x4024
#define REG_RX_DB_LOW              0x4028
#define REG_RX_DB_HI               0x402C
#define REG_RX_CB_LOW              0x4030
#define REG_RX_CB_HI               0x4034
#define REG_RX_COMP_HEAD           0x403C
#define REG_RX_COMP_TAIL           0x4040
#define REG_RX_BLANK               0x4044
#define REG_RX_AE_THRESH           0x4048
#define REG_RX_RED                 0x404C
#define REG_RX_TABLE_ADDR          0x4128
#define REG_RX_TABLE_DATA_LOW      0x412C
#define REG_RX_TABLE_DATA_MID      0x4130
#define REG_RX_TABLE_DATA_HI       0x4134
#define REG_RX_CTRL_FIFO_ADDR      0x4094
#define REG_RX_IPP_FIFO_ADDR       0x4104
#define REG_HP_CFG                 0x4140
#define REG_HP_INSTR_RAM_ADDR      0x4144
#define REG_HP_INSTR_RAM_DATA_LOW  0x4148
#define REG_HP_INSTR_RAM_DATA_MID  0x414C
#define REG_HP_INSTR_RAM_DATA_HI   0x4150
#define REG_HP_STATE_MACHINE       0x418C
#define REG_HP_STATUS0             0x4190
#define REG_HP_STATUS1             0x4194
#define REG_HP_STATUS2             0x4198
#define REG_PLUS_RX_DB1_LOW        0x4200
#define REG_PLUS_RX_DB1_HI         0x4204
#define REG_PLUS_RX_CB1_LOW        0x4208
#define REG_PLUS_RX_CB1_HI         0x420C
#define REG_PLUS_RX_KICK1          0x4220
#define REG_PLUS_RX_COMP1_TAIL     0x422C
#define REG_PLUS_RX_AE1_THRESH     0x4240
#define REG_MAC_TX_RESET           0x6000
#define REG_MAC_RX_RESET           0x6004
#define REG_MAC_SEND_PAUSE         0x6008
#define REG_MAC_TX_STATUS          0x6010
#define REG_MAC_RX_STATUS          0x6014
#define REG_MAC_CTRL_STATUS        0x6018
#define REG_MAC_TX_MASK            0x6020
#define REG_MAC_RX_MASK            0x6024
#define REG_MAC_CTRL_MASK          0x6028
#define REG_MAC_TX_CFG             0x6030
#define REG_MAC_RX_CFG             0x6034
#define REG_MAC_CTRL_CFG           0x6038
#define REG_MAC_XIF_CFG            0x603C
#define REG_MAC_IPG0               0x6040
#define REG_MAC_IPG1               0x6044
#define REG_MAC_IPG2               0x6048
#define REG_MAC_SLOT_TIME          0x604C
#define REG_MAC_FRAMESIZE_MIN      0x6050
#define REG_MAC_FRAMESIZE_MAX      0x6054
#define REG_MAC_PA_SIZE            0x6058
#define REG_MAC_JAM_SIZE           0x605C
#define REG_MAC_ATTEMPT_LIMIT      0x6060
#define REG_MAC_CTRL_TYPE          0x6064
#define REG_MAC_ADDR0              0x6080
#define REG_MAC_ADDR_FILTER0       0x614C
#define REG_MAC_ADDR_FILTER1       0x6150
#define REG_MAC_ADDR_FILTER2       0x6154
#define REG_MAC_ADDR_FILTER2_1_MASK 0x6158
#define REG_MAC_ADDR_FILTER0_MASK  0x615C
#define REG_MAC_HASH_TABLE0        0x6160
#define REG_MAC_RECV_FRAME         0x61B8
#define REG_MAC_COLL_NORMAL        0x61A0
#define REG_MAC_COLL_FIRST         0x61A4
#define REG_MAC_COLL_EXCESS        0x61A8
#define REG_MAC_COLL_LATE          0x61AC
#define REG_MAC_TIMER_DEFER        0x61B0
#define REG_MAC_ATTEMPTS_PEAK      0x61B4
#define REG_MAC_LEN_ERR            0x61BC
#define REG_MAC_ALIGN_ERR          0x61C0
#define REG_MAC_FCS_ERR            0x61C4
#define REG_MAC_RX_CODE_ERR        0x61C8
#define REG_MAC_RANDOM_SEED        0x61CC
#define REG_MAC_STATE_MACHINE      0x61D0
#define REG_MIF_FRAME              0x620C
#define REG_MIF_CFG                0x6210
#define REG_MIF_MASK               0x6214
#define REG_MIF_STATUS             0x6218
#define REG_MIF_STATE_MACHINE      0x621C
#define REG_PCS_MII_CTRL           0x9000
#define REG_PCS_MII_STATUS         0x9004
#define REG_PCS_MII_ADVERT         0x9008
#define REG_PCS_MII_LPA            0x900C
#define REG_PCS_CFG                0x9010
#define REG_PCS_STATE_MACHINE      0x9014
#define REG_PCS_INTR_STATUS        0x9018
#define REG_PCS_DATAPATH_MODE      0x9050
#define REG_PCS_SERDES_CTRL        0x9054
#define REG_PCS_SERDES_STATE       0x905C
#define REG_SECOND_LOCALBUS_START  0x180000
#define REG_ENTROPY_START          0x180000
#define REG_ENTROPY_IV             0x180008
#define REG_ENTROPY_RAND_REG       0x180006
#define REG_ENTROPY_RESET          0x180007

/* Register bit fields and masks */
#define INTR_TX_INTME              0x00000001
#define INTR_TX_ALL                0x00000002
#define INTR_TX_DONE               0x00000004
#define INTR_RX_DONE               0x00000010
#define INTR_RX_BUF_UNAVAIL        0x00000020
#define INTR_RX_COMP_FULL          0x00000080
#define INTR_RX_COMP_AF            0x00000200
#define INTR_RX_BUF_AE             0x00000100
#define INTR_RX_TAG_ERROR          0x00000040
#define INTR_RX_LEN_MISMATCH       0x00000400
#define INTR_TX_MAC_STATUS         0x00004000
#define INTR_RX_MAC_STATUS         0x00008000
#define INTR_MAC_CTRL_STATUS       0x00010000
#define INTR_MIF_STATUS            0x00020000
#define INTR_PCI_ERROR_STATUS      0x00040000
#define INTR_PCS_STATUS            0x00002000
#define INTR_TX_TAG_ERROR          0x00000008

#define INTRN_MASK_CLEAR_ALL       (INTR_RX_DONE_ALT | INTR_RX_COMP_FULL_ALT | INTR_RX_COMP_AF_ALT | INTR_RX_BUF_UNAVAIL_1 | INTR_RX_BUF_AE_1)
#define INTRN_MASK_RX_EN           0x80

#define SW_RESET_TX                0x00000001
#define SW_RESET_RX                0x00000002
#define SW_RESET_BLOCK_PCS_SLINK   0x00000008

#define TX_CFG_DMA_EN              0x00000001
#define RX_CFG_DMA_EN              0x00000001

#define MAC_TX_CFG_EN                 0x0001
#define MAC_RX_CFG_EN                 0x0001

#define HP_CFG_PARSE_EN               0x00000001
#define HP_CFG_SYN_INC_MASK           0x00000100

#define BIM_LOCAL_DEV_PAD             0x01
#define BIM_LOCAL_DEV_PROM            0x02
#define BIM_LOCAL_DEV_EXT             0x04

#define ENTROPY_RESET_STC_MODE        0x02

#define MIF_FRAME_ST                  0x40000000
#define MIF_FRAME_OP_READ             0x20000000
#define MIF_FRAME_OP_WRITE            0x10000000
#define MIF_FRAME_TURN_AROUND_LSB     0x00010000
#define MIF_FRAME_TURN_AROUND_MSB     0x00020000
#define MIF_FRAME_DATA_MASK           0x0000FFFF

#define PCS_MII_RESET                 0x8000
#define PCS_MII_CTRL_DUPLEX           0x0100
#define PCS_MII_AUTONEG_EN            0x1000
#define PCS_MII_RESTART_AUTONEG       0x0200
#define PCS_MII_ADVERT_FD             0x0020
#define PCS_MII_ADVERT_HD             0x0040
#define PCS_MII_ADVERT_SYM_PAUSE      0x0080
#define PCS_MII_ADVERT_ASYM_PAUSE     0x0100
#define PCS_MII_LPA_FD                0x0020
#define PCS_MII_LPA_SYM_PAUSE         0x0080
#define PCS_MII_LPA_ASYM_PAUSE        0x0100
#define PCS_MII_STATUS_LINK_STATUS    0x0004
#define PCS_MII_STATUS_REMOTE_FAULT   0x0010
#define PCS_MII_STATUS_AUTONEG_COMP   0x0020
#define PCS_CFG_EN                    0x01
#define PCS_DATAPATH_MODE_MII         0x00
#define PCS_DATAPATH_MODE_SERDES      0x02
#define PCS_SERDES_CTRL_SYNCD_EN      0x02
#define PCS_SM_WORD_SYNC_STATE_MASK   0x00000700
#define SM_LINK_STATE_UP              0x00016000
#define PCS_SM_LINK_STATE_MASK        0x0001E000
#define PCS_INTR_STATUS_LINK_CHANGE   0x04

#define SATURN_PCFG_FSI               0x00000200

#define MAC_TX_MAX_PACKET_ERR          0x0004
#define MAC_TX_UNDERRUN                0x0002
#define MAC_TX_COLL_EXCESS             0x0010
#define MAC_TX_COLL_LATE               0x0020
#define MAC_TX_DEFER_TIMER             0x0080
#define MAC_TX_COLL_NORMAL             0x0008
#define MAC_CTRL_PAUSE_RECEIVED        0x00000001
#define MAC_CTRL_PAUSE_STATE           0x00000002
#define MAC_RX_CRC_ERR                 0x0010
#define MAC_RX_LEN_ERR                 0x0020
#define MAC_RX_ALIGN_ERR               0x0008
#define MAC_RX_OVERFLOW                0x0002

#define TX_DESC_INTME                  0x0000000100000000ULL
#define TX_DESC_SOF                    0x0000000080000000ULL
#define TX_DESC_EOF                    0x0000000040000000ULL
#define TX_DESC_CSUM_EN                0x0000000020000000ULL

#define RX_COMP1_SPLIT_PKT             0x0400000000000000ULL
#define RX_COMP1_RELEASE_FLOW          0x0800000000000000ULL
#define RX_COMP1_RELEASE_DATA          0x1000000000000000ULL
#define RX_COMP1_RELEASE_HDR           0x2000000000000000ULL
#define RX_COMP1_RELEASE_NEXT          0x0200000000000000ULL
#define RX_COMP3_SMALL_PKT             0x0000000000000001ULL
#define RX_COMP4_LEN_MISMATCH          0x8000000000000000ULL
#define RX_COMP4_BAD                   0x4000000000000000ULL
#define RX_COMP4_ZERO                  0x0000080000000000ULL

/* DMA descriptor structures */
struct cas_rx_desc {
    __le64     index;
    __le64     buffer;
};

struct cas_rx_comp {
    __le64     word1;
    __le64     word2;
    __le64     word3;
    __le64     word4;
};

struct cas_tx_desc {
    __le64     control;
    __le64     buffer;
};

/* TX descriptor ring configuration register bits */
#define TX_CFG_DESC_RING0_MASK         0x0000003C
#define TX_CFG_DESC_RING0_SHIFT        2
#define TX_CFG_COMPWB_Q1               0x02000000
#define TX_CFG_COMPWB_Q2               0x04000000
#define TX_CFG_COMPWB_Q3               0x08000000
#define TX_CFG_COMPWB_Q4               0x10000000
#define TX_CFG_INTR_COMPWB_DIS         0x20000000
#define TX_CFG_DMA_RDPIPE_DIS          0x01000000
#define TX_CFG_PACED_MODE              0x00100000

#define RX_PAUSE_THRESH_QUANTUM        64
#define RX_BLANK_INTR_PKT_VAL          0x05
#define RX_BLANK_INTR_TIME_VAL         0x0F
#define RX_AE_COMP_VAL                 (/* to be defined */)
#define RX_AE_FREEN_VAL(x)             (/* to be defined */)

#define BIM_CFG_66MHZ                  0x008
#define BIM_CFG_32BIT                  0x010
#define BIM_CFG_RMA_INTR_ENABLE        0x040
#define BIM_CFG_DPAR_INTR_ENABLE       0x020
#define BIM_CFG_RTA_INTR_ENABLE        0x080

#define CAS_FLAG_1000MB_CAP            0x00000001
#define CAS_FLAG_REG_PLUS              0x00000002
#define CAS_FLAG_TARGET_ABORT          0x00000004
#define CAS_FLAG_SATURN                0x00000008
#define CAS_FLAG_RXD_POST_MASK         0x000000F0
#define CAS_FLAG_RXD_POST_SHIFT        4
#define CAS_FLAG_ENTROPY_DEV           0x00000100
#define CAS_FLAG_NO_HW_CSUM            0x00000200

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
    /* TX registers */
    uint32_t reg_tx_cfg;
    uint32_t reg_tx_fifo_write_ptr;
    uint32_t reg_tx_fifo_read_ptr;
    uint32_t reg_tx_fifo_pkt_cnt;
    uint32_t reg_tx_sm_1;
    uint32_t reg_tx_sm_2;
    uint64_t reg_tx_db[4]; /* 4 rings: low and high pairs */
    uint32_t reg_tx_kick[4];
    uint32_t reg_tx_comp[4];
    uint32_t reg_tx_compwb_db_low;
    uint32_t reg_tx_compwb_db_hi;
    uint32_t reg_tx_maxburst[4];
    uint32_t reg_tx_fifo_size;

    /* RX registers */
    uint32_t reg_rx_cfg;
    uint32_t reg_rx_page_size;
    uint32_t reg_rx_pause_thresh;
    uint32_t reg_rx_kick;
    uint64_t reg_rx_db; /* combined low/high */
    uint64_t reg_rx_cb; /* combined low/high */
    uint32_t reg_rx_comp_head;
    uint32_t reg_rx_comp_tail;
    uint32_t reg_rx_blank;
    uint32_t reg_rx_ae_thresh;
    uint32_t reg_rx_red;
    uint32_t reg_rx_table_addr;
    uint32_t reg_rx_table_data[3]; /* 96-bit: low, mid, high */
    uint32_t reg_rx_ctrl_fifo_addr;
    uint32_t reg_rx_ipp_fifo_addr;
    /* HP (Header Parser) */
    uint32_t reg_hp_cfg;
    uint32_t reg_hp_instr_ram_addr;
    uint32_t reg_hp_instr_ram_data_low;
    uint32_t reg_hp_instr_ram_data_mid;
    uint32_t reg_hp_instr_ram_data_hi;
    uint32_t reg_hp_state_machine;
    uint32_t reg_hp_status0;
    uint32_t reg_hp_status1;
    uint32_t reg_hp_status2;
    /* Plus (enhanced) RX registers */
    uint64_t reg_plus_rx_db1; /* combined low/high */
    uint64_t reg_plus_rx_cb1;
    uint32_t reg_plus_rx_kick1;
    uint32_t reg_plus_rx_comp1_tail;
    uint32_t reg_plus_rx_ae1_thresh;
    /* MAC */
    uint32_t reg_mac_tx_reset;
    uint32_t reg_mac_rx_reset;
    uint32_t reg_mac_send_pause;
    uint32_t reg_mac_tx_status;
    uint32_t reg_mac_rx_status;
    uint32_t reg_mac_ctrl_status;
    uint32_t reg_mac_tx_mask;
    uint32_t reg_mac_rx_mask;
    uint32_t reg_mac_ctrl_mask;
    uint32_t reg_mac_tx_cfg;
    uint32_t reg_mac_rx_cfg;
    uint32_t reg_mac_ctrl_cfg;
    uint32_t reg_mac_xif_cfg;
    uint32_t reg_mac_ipg0;
    uint32_t reg_mac_ipg1;
    uint32_t reg_mac_ipg2;
    uint32_t reg_mac_slot_time;
    uint32_t reg_mac_framesize_min;
    uint32_t reg_mac_framesize_max;
    uint32_t reg_mac_pa_size;
    uint32_t reg_mac_jam_size;
    uint32_t reg_mac_attempt_limit;
    uint32_t reg_mac_ctrl_type;
    uint32_t reg_mac_addr[3]; /* 3 words for MAC addr */
    uint32_t reg_mac_addr_filter0;
    uint32_t reg_mac_addr_filter1;
    uint32_t reg_mac_addr_filter2;
    uint32_t reg_mac_addr_filter2_1_mask;
    uint32_t reg_mac_addr_filter0_mask;
    uint32_t reg_mac_hash_table[2];
    uint32_t reg_mac_recv_frame;
    uint32_t reg_mac_coll_normal;
    uint32_t reg_mac_coll_first;
    uint32_t reg_mac_coll_excess;
    uint32_t reg_mac_coll_late;
    uint32_t reg_mac_timer_defer;
    uint32_t reg_mac_attempts_peak;
    uint32_t reg_mac_len_err;
    uint32_t reg_mac_align_err;
    uint32_t reg_mac_fcs_err;
    uint32_t reg_mac_rx_code_err;
    uint32_t reg_mac_random_seed;
    uint32_t reg_mac_state_machine;
    /* MIF */
    uint32_t reg_mif_frame;
    uint32_t reg_mif_cfg;
    uint32_t reg_mif_mask;
    uint32_t reg_mif_status;
    uint32_t reg_mif_state_machine;
    /* PCS */
    uint32_t reg_pcs_mii_ctrl;
    uint32_t reg_pcs_mii_status;
    uint32_t reg_pcs_mii_advert;
    uint32_t reg_pcs_mii_lpa;
    uint32_t reg_pcs_cfg;
    uint32_t reg_pcs_state_machine;
    uint32_t reg_pcs_intr_status;
    uint32_t reg_pcs_datapath_mode;
    uint32_t reg_pcs_serdes_ctrl;
    uint32_t reg_pcs_serdes_state;
    /* Global/BIM */
    uint32_t reg_bim_cfg;
    uint32_t reg_bim_diag;
    uint32_t reg_pci_err_status;
    uint32_t reg_pci_err_status_mask;
    uint32_t reg_sw_reset;
    uint32_t reg_bim_local_dev_en;
    uint32_t reg_saturn_pcfg;
    uint32_t reg_cawr;
    uint32_t reg_inf_burst;
    uint32_t reg_intr_status;
    uint32_t reg_intr_mask;
    uint32_t reg_alias_clear;
    uint32_t reg_intr_status_alias;
    uint32_t reg_plus_intr_mask_1;
    /* Entropy */
    uint32_t reg_entropy_iv;
    uint32_t reg_entropy_rand_reg;
    uint32_t reg_entropy_reset;
    /* Expansion ROM start address (not implemented as real ROM) */
    uint32_t reg_expansion_rom_run_start;

    /* DMA Context */
    struct {
        /* TX rings: each ring has a base address and current pointer */
        dma_addr_t tx_ring_base[4];
        uint32_t   tx_ring_kick[4];
        uint32_t   tx_ring_comp[4];
        dma_addr_t tx_compwb_base;
        /* RX descriptor ring */
        dma_addr_t rx_desc_base;
        dma_addr_t rx_desc_kick;
        dma_addr_t rx_desc_cb;
        /* RX completion ring */
        dma_addr_t rx_comp_base;
        dma_addr_t rx_comp_head;
        dma_addr_t rx_comp_tail;
        /* Plus (second) RX rings */
        dma_addr_t rx_plus_desc_base;
        dma_addr_t rx_plus_comp_tail;
        /* Status */
        bool dma_enabled;
    } dma;

    /* Operational status flags */
    uint32_t status_bits;

    /* State used to handle reset sequences */
    bool in_reset;

    /* MII PHY registers */
    uint16_t phy_regs[32];
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    int level = 0;
    if (s->intr_status & ~s->intr_mask) {
        level = 1;
    }
    pci_set_irq(pdev, level);
}

/* MMIO/PIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Expansion ROM area: return 0 */
    if (addr >= 0x100000 && addr < 0x200000) {
        return 0;
    }

    switch (addr) {
    case REG_CAWR:
        val = s->reg_cawr;
        break;
    case REG_INF_BURST:
        val = s->reg_inf_burst;
        break;
    case REG_INTR_STATUS:
        val = s->reg_intr_status;
        break;
    case REG_INTR_MASK:
        val = s->reg_intr_mask;
        break;
    case REG_ALIAS_CLEAR:
        val = s->reg_alias_clear;
        break;
    case REG_INTR_STATUS_ALIAS:
        val = s->reg_intr_status_alias;
        break;
    case REG_BIM_CFG:
        val = s->reg_bim_cfg;
        break;
    case REG_BIM_DIAG:
        val = s->reg_bim_diag;
        break;
    case REG_PCI_ERR_STATUS:
        val = s->reg_pci_err_status;
        break;
    case REG_PCI_ERR_STATUS_MASK:
        val = s->reg_pci_err_status_mask;
        break;
    case REG_SW_RESET:
        val = s->reg_sw_reset;
        break;
    case REG_BIM_LOCAL_DEV_EN:
        val = s->reg_bim_local_dev_en;
        break;
    case REG_PLUS_INTR_MASK_1:
        val = s->reg_plus_intr_mask_1;
        break;
    case REG_SATURN_PCFG:
        val = s->reg_saturn_pcfg;
        break;
    case REG_TX_CFG:
        val = s->reg_tx_cfg;
        break;
    case REG_TX_FIFO_WRITE_PTR:
        val = s->reg_tx_fifo_write_ptr;
        break;
    case REG_TX_FIFO_READ_PTR:
        val = s->reg_tx_fifo_read_ptr;
        break;
    case REG_TX_FIFO_PKT_CNT:
        val = s->reg_tx_fifo_pkt_cnt;
        break;
    case REG_TX_SM_1:
        val = s->reg_tx_sm_1;
        break;
    case REG_TX_SM_2:
        val = s->reg_tx_sm_2;
        break;
    case REG_TX_KICK0:
        val = s->reg_tx_kick[0];
        break;
    case REG_TX_COMP0:
        val = s->reg_tx_comp[0];
        break;
    case REG_TX_COMPWB_DB_LOW:
        val = s->reg_tx_compwb_db_low;
        break;
    case REG_TX_COMPWB_DB_HI:
        val = s->reg_tx_compwb_db_hi;
        break;
    case REG_TX_DB0_LOW:
        val = (uint32_t)(s->reg_tx_db[0] & 0xFFFFFFFF);
        break;
    case REG_TX_DB0_HI:
        val = (uint32_t)((s->reg_tx_db[0] >> 32) & 0xFFFFFFFF);
        break;
    case REG_TX_MAXBURST_0:
        val = s->reg_tx_maxburst[0];
        break;
    case REG_TX_MAXBURST_1:
        val = s->reg_tx_maxburst[1];
        break;
    case REG_TX_MAXBURST_2:
        val = s->reg_tx_maxburst[2];
        break;
    case REG_TX_MAXBURST_3:
        val = s->reg_tx_maxburst[3];
        break;
    case REG_TX_FIFO_SIZE:
        val = s->reg_tx_fifo_size;
        break;
    case REG_RX_CFG:
        val = s->reg_rx_cfg;
        break;
    case REG_RX_PAGE_SIZE:
        val = s->reg_rx_page_size;
        break;
    case REG_RX_PAUSE_THRESH:
        val = s->reg_rx_pause_thresh;
        break;
    case REG_RX_KICK:
        val = s->reg_rx_kick;
        break;
    case REG_RX_DB_LOW:
        val = (uint32_t)(s->reg_rx_db & 0xFFFFFFFF);
        break;
    case REG_RX_DB_HI:
        val = (uint32_t)((s->reg_rx_db >> 32) & 0xFFFFFFFF);
        break;
    case REG_RX_CB_LOW:
        val = (uint32_t)(s->reg_rx_cb & 0xFFFFFFFF);
        break;
    case REG_RX_CB_HI:
        val = (uint32_t)((s->reg_rx_cb >> 32) & 0xFFFFFFFF);
        break;
    case REG_RX_COMP_HEAD:
        val = s->reg_rx_comp_head;
        break;
    case REG_RX_COMP_TAIL:
        val = s->reg_rx_comp_tail;
        break;
    case REG_RX_BLANK:
        val = s->reg_rx_blank;
        break;
    case REG_RX_AE_THRESH:
        val = s->reg_rx_ae_thresh;
        break;
    case REG_RX_RED:
        val = s->reg_rx_red;
        break;
    case REG_RX_TABLE_ADDR:
        val = s->reg_rx_table_addr;
        break;
    case REG_RX_TABLE_DATA_LOW:
        val = s->reg_rx_table_data[0];
        break;
    case REG_RX_TABLE_DATA_MID:
        val = s->reg_rx_table_data[1];
        break;
    case REG_RX_TABLE_DATA_HI:
        val = s->reg_rx_table_data[2];
        break;
    case REG_RX_CTRL_FIFO_ADDR:
        val = s->reg_rx_ctrl_fifo_addr;
        break;
    case REG_RX_IPP_FIFO_ADDR:
        val = s->reg_rx_ipp_fifo_addr;
        break;
    case REG_HP_CFG:
        val = s->reg_hp_cfg;
        break;
    case REG_HP_INSTR_RAM_ADDR:
        val = s->reg_hp_instr_ram_addr;
        break;
    case REG_HP_INSTR_RAM_DATA_LOW:
        val = s->reg_hp_instr_ram_data_low;
        break;
    case REG_HP_INSTR_RAM_DATA_MID:
        val = s->reg_hp_instr_ram_data_mid;
        break;
    case REG_HP_INSTR_RAM_DATA_HI:
        val = s->reg_hp_instr_ram_data_hi;
        break;
    case REG_HP_STATE_MACHINE:
        val = s->reg_hp_state_machine;
        break;
    case REG_HP_STATUS0:
        val = s->reg_hp_status0;
        break;
    case REG_HP_STATUS1:
        val = s->reg_hp_status1;
        break;
    case REG_HP_STATUS2:
        val = s->reg_hp_status2;
        break;
    case REG_PLUS_RX_DB1_LOW:
        val = (uint32_t)(s->reg_plus_rx_db1 & 0xFFFFFFFF);
        break;
    case REG_PLUS_RX_DB1_HI:
        val = (uint32_t)((s->reg_plus_rx_db1 >> 32) & 0xFFFFFFFF);
        break;
    case REG_PLUS_RX_CB1_LOW:
        val = (uint32_t)(s->reg_plus_rx_cb1 & 0xFFFFFFFF);
        break;
    case REG_PLUS_RX_CB1_HI:
        val = (uint32_t)((s->reg_plus_rx_cb1 >> 32) & 0xFFFFFFFF);
        break;
    case REG_PLUS_RX_KICK1:
        val = s->reg_plus_rx_kick1;
        break;
    case REG_PLUS_RX_COMP1_TAIL:
        val = s->reg_plus_rx_comp1_tail;
        break;
    case REG_PLUS_RX_AE1_THRESH:
        val = s->reg_plus_rx_ae1_thresh;
        break;
    case REG_MAC_TX_RESET:
        val = s->reg_mac_tx_reset;
        break;
    case REG_MAC_RX_RESET:
        val = s->reg_mac_rx_reset;
        break;
    case REG_MAC_SEND_PAUSE:
        val = s->reg_mac_send_pause;
        break;
    case REG_MAC_TX_STATUS:
        val = s->reg_mac_tx_status;
        break;
    case REG_MAC_RX_STATUS:
        val = s->reg_mac_rx_status;
        break;
    case REG_MAC_CTRL_STATUS:
        val = s->reg_mac_ctrl_status;
        break;
    case REG_MAC_TX_MASK:
        val = s->reg_mac_tx_mask;
        break;
    case REG_MAC_RX_MASK:
        val = s->reg_mac_rx_mask;
        break;
    case REG_MAC_CTRL_MASK:
        val = s->reg_mac_ctrl_mask;
        break;
    case REG_MAC_TX_CFG:
        val = s->reg_mac_tx_cfg;
        break;
    case REG_MAC_RX_CFG:
        val = s->reg_mac_rx_cfg;
        break;
    case REG_MAC_CTRL_CFG:
        val = s->reg_mac_ctrl_cfg;
        break;
    case REG_MAC_XIF_CFG:
        val = s->reg_mac_xif_cfg;
        break;
    case REG_MAC_IPG0:
        val = s->reg_mac_ipg0;
        break;
    case REG_MAC_IPG1:
        val = s->reg_mac_ipg1;
        break;
    case REG_MAC_IPG2:
        val = s->reg_mac_ipg2;
        break;
    case REG_MAC_SLOT_TIME:
        val = s->reg_mac_slot_time;
        break;
    case REG_MAC_FRAMESIZE_MIN:
        val = s->reg_mac_framesize_min;
        break;
    case REG_MAC_FRAMESIZE_MAX:
        val = s->reg_mac_framesize_max;
        break;
    case REG_MAC_PA_SIZE:
        val = s->reg_mac_pa_size;
        break;
    case REG_MAC_JAM_SIZE:
        val = s->reg_mac_jam_size;
        break;
    case REG_MAC_ATTEMPT_LIMIT:
        val = s->reg_mac_attempt_limit;
        break;
    case REG_MAC_CTRL_TYPE:
        val = s->reg_mac_ctrl_type;
        break;
    case REG_MAC_ADDR0:
        val = s->reg_mac_addr[0];
        break;
    case REG_MAC_ADDR_FILTER0:
        val = s->reg_mac_addr_filter0;
        break;
    case REG_MAC_ADDR_FILTER1:
        val = s->reg_mac_addr_filter1;
        break;
    case REG_MAC_ADDR_FILTER2:
        val = s->reg_mac_addr_filter2;
        break;
    case REG_MAC_ADDR_FILTER2_1_MASK:
        val = s->reg_mac_addr_filter2_1_mask;
        break;
    case REG_MAC_ADDR_FILTER0_MASK:
        val = s->reg_mac_addr_filter0_mask;
        break;
    case REG_MAC_HASH_TABLE0:
        val = s->reg_mac_hash_table[0];
        break;
    case REG_MAC_RECV_FRAME:
        val = s->reg_mac_recv_frame;
        break;
    case REG_MAC_COLL_NORMAL:
        val = s->reg_mac_coll_normal;
        break;
    case REG_MAC_COLL_FIRST:
        val = s->reg_mac_coll_first;
        break;
    case REG_MAC_COLL_EXCESS:
        val = s->reg_mac_coll_excess;
        break;
    case REG_MAC_COLL_LATE:
        val = s->reg_mac_coll_late;
        break;
    case REG_MAC_TIMER_DEFER:
        val = s->reg_mac_timer_defer;
        break;
    case REG_MAC_ATTEMPTS_PEAK:
        val = s->reg_mac_attempts_peak;
        break;
    case REG_MAC_LEN_ERR:
        val = s->reg_mac_len_err;
        break;
    case REG_MAC_ALIGN_ERR:
        val = s->reg_mac_align_err;
        break;
    case REG_MAC_FCS_ERR:
        val = s->reg_mac_fcs_err;
        break;
    case REG_MAC_RX_CODE_ERR:
        val = s->reg_mac_rx_code_err;
        break;
    case REG_MAC_RANDOM_SEED:
        val = s->reg_mac_random_seed;
        break;
    case REG_MAC_STATE_MACHINE:
        val = s->reg_mac_state_machine;
        break;
    case REG_MIF_FRAME:
        val = s->reg_mif_frame;
        break;
    case REG_MIF_CFG:
        val = s->reg_mif_cfg;
        break;
    case REG_MIF_MASK:
        val = s->reg_mif_mask;
        break;
    case REG_MIF_STATUS:
        val = s->reg_mif_status;
        break;
    case REG_MIF_STATE_MACHINE:
        val = s->reg_mif_state_machine;
        break;
    case REG_PCS_MII_CTRL:
        val = s->reg_pcs_mii_ctrl;
        break;
    case REG_PCS_MII_STATUS:
        val = s->reg_pcs_mii_status;
        break;
    case REG_PCS_MII_ADVERT:
        val = s->reg_pcs_mii_advert;
        break;
    case REG_PCS_MII_LPA:
        val = s->reg_pcs_mii_lpa;
        break;
    case REG_PCS_CFG:
        val = s->reg_pcs_cfg;
        break;
    case REG_PCS_STATE_MACHINE:
        val = s->reg_pcs_state_machine;
        break;
    case REG_PCS_INTR_STATUS:
        val = s->reg_pcs_intr_status;
        break;
    case REG_PCS_DATAPATH_MODE:
        val = s->reg_pcs_datapath_mode;
        break;
    case REG_PCS_SERDES_CTRL:
        val = s->reg_pcs_serdes_ctrl;
        break;
    case REG_PCS_SERDES_STATE:
        val = s->reg_pcs_serdes_state;
        break;
    case REG_SECOND_LOCALBUS_START:
    case REG_ENTROPY_START:
        /* Both aliases at same address; return 0 */
        break;
    case REG_ENTROPY_IV:
        val = s->reg_entropy_iv;
        break;
    case REG_ENTROPY_RAND_REG:
        val = s->reg_entropy_rand_reg;
        break;
    case REG_ENTROPY_RESET:
        val = s->reg_entropy_reset;
        break;
    default:
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* Expansion ROM area: ignore writes */
    if (addr >= 0x100000 && addr < 0x200000) {
        return;
    }

    switch (addr) {
    case REG_CAWR:
        s->reg_cawr = val;
        break;
    case REG_INF_BURST:
        s->reg_inf_burst = val;
        break;
    case REG_INTR_STATUS:
        /* Usually W1C, clear bits */
        s->reg_intr_status &= ~val;
        pcibase_update_irq(s);
        break;
    case REG_INTR_MASK:
        s->reg_intr_mask = val;
        pcibase_update_irq(s);
        break;
    case REG_ALIAS_CLEAR:
        /* Clear the written bits from intr_status */
        s->reg_intr_status &= ~val;
        pcibase_update_irq(s);
        break;
    case REG_INTR_STATUS_ALIAS:
        /* Reads should clear, but write does nothing */
        break;
    case REG_BIM_CFG:
        s->reg_bim_cfg = val;
        break;
    case REG_BIM_DIAG:
        s->reg_bim_diag = val;
        break;
    case REG_PCI_ERR_STATUS:
        s->reg_pci_err_status = val;
        break;
    case REG_PCI_ERR_STATUS_MASK:
        s->reg_pci_err_status_mask = val;
        break;
    case REG_SW_RESET:
        /* Writing to SW_RESET starts reset; bits clear automatically */
        s->reg_sw_reset = 0;
        break;
    case REG_BIM_LOCAL_DEV_EN:
        s->reg_bim_local_dev_en = val;
        break;
    case REG_PLUS_INTR_MASK_1:
        s->reg_plus_intr_mask_1 = val;
        break;
    case REG_SATURN_PCFG:
        s->reg_saturn_pcfg = val;
        break;
    case REG_TX_CFG:
        s->reg_tx_cfg = val;
        break;
    case REG_TX_FIFO_WRITE_PTR:
        s->reg_tx_fifo_write_ptr = val;
        break;
    case REG_TX_FIFO_READ_PTR:
        s->reg_tx_fifo_read_ptr = val;
        break;
    case REG_TX_FIFO_PKT_CNT:
        s->reg_tx_fifo_pkt_cnt = val;
        break;
    case REG_TX_SM_1:
        s->reg_tx_sm_1 = val;
        break;
    case REG_TX_SM_2:
        s->reg_tx_sm_2 = val;
        break;
    case REG_TX_KICK0:
        s->reg_tx_kick[0] = val;
        break;
    case REG_TX_COMP0:
        s->reg_tx_comp[0] = val;
        break;
    case REG_TX_COMPWB_DB_LOW:
        s->reg_tx_compwb_db_low = val;
        break;
    case REG_TX_COMPWB_DB_HI:
        s->reg_tx_compwb_db_hi = val;
        break;
    case REG_TX_DB0_LOW:
        s->reg_tx_db[0] = (s->reg_tx_db[0] & ~0xFFFFFFFFULL) | (val & 0xFFFFFFFFULL);
        break;
    case REG_TX_DB0_HI:
        s->reg_tx_db[0] = (s->reg_tx_db[0] & 0xFFFFFFFFULL) | ((uint64_t)val << 32);
        break;
    case REG_TX_MAXBURST_0:
        s->reg_tx_maxburst[0] = val;
        break;
    case REG_TX_MAXBURST_1:
        s->reg_tx_maxburst[1] = val;
        break;
    case REG_TX_MAXBURST_2:
        s->reg_tx_maxburst[2] = val;
        break;
    case REG_TX_MAXBURST_3:
        s->reg_tx_maxburst[3] = val;
        break;
    case REG_TX_FIFO_SIZE:
        s->reg_tx_fifo_size = val;
        break;
    case REG_RX_CFG:
        s->reg_rx_cfg = val;
        break;
    case REG_RX_PAGE_SIZE:
        s->reg_rx_page_size = val;
        break;
    case REG_RX_PAUSE_THRESH:
        s->reg_rx_pause_thresh = val;
        break;
    case REG_RX_KICK:
        s->reg_rx_kick = val;
        break;
    case REG_RX_DB_LOW:
        s->reg_rx_db = (s->reg_rx_db & ~0xFFFFFFFFULL) | (val & 0xFFFFFFFFULL);
        break;
    case REG_RX_DB_HI:
        s->reg_rx_db = (s->reg_rx_db & 0xFFFFFFFFULL) | ((uint64_t)val << 32);
        break;
    case REG_RX_CB_LOW:
        s->reg_rx_cb = (s->reg_rx_cb & ~0xFFFFFFFFULL) | (val & 0xFFFFFFFFULL);
        break;
    case REG_RX_CB_HI:
        s->reg_rx_cb = (s->reg_rx_cb & 0xFFFFFFFFULL) | ((uint64_t)val << 32);
        break;
    case REG_RX_COMP_HEAD:
        s->reg_rx_comp_head = val;
        break;
    case REG_RX_COMP_TAIL:
        s->reg_rx_comp_tail = val;
        break;
    case REG_RX_BLANK:
        s->reg_rx_blank = val;
        break;
    case REG_RX_AE_THRESH:
        s->reg_rx_ae_thresh = val;
        break;
    case REG_RX_RED:
        s->reg_rx_red = val;
        break;
    case REG_RX_TABLE_ADDR:
        s->reg_rx_table_addr = val;
        break;
    case REG_RX_TABLE_DATA_LOW:
        s->reg_rx_table_data[0] = val;
        break;
    case REG_RX_TABLE_DATA_MID:
        s->reg_rx_table_data[1] = val;
        break;
    case REG_RX_TABLE_DATA_HI:
        s->reg_rx_table_data[2] = val;
        break;
    case REG_RX_CTRL_FIFO_ADDR:
        s->reg_rx_ctrl_fifo_addr = val;
        break;
    case REG_RX_IPP_FIFO_ADDR:
        s->reg_rx_ipp_fifo_addr = val;
        break;
    case REG_HP_CFG:
        s->reg_hp_cfg = val;
        break;
    case REG_HP_INSTR_RAM_ADDR:
        s->reg_hp_instr_ram_addr = val;
        break;
    case REG_HP_INSTR_RAM_DATA_LOW:
        s->reg_hp_instr_ram_data_low = val;
        break;
    case REG_HP_INSTR_RAM_DATA_MID:
        s->reg_hp_instr_ram_data_mid = val;
        break;
    case REG_HP_INSTR_RAM_DATA_HI:
        s->reg_hp_instr_ram_data_hi = val;
        break;
    case REG_HP_STATE_MACHINE:
        s->reg_hp_state_machine = val;
        break;
    case REG_HP_STATUS0:
        s->reg_hp_status0 = val;
        break;
    case REG_HP_STATUS1:
        s->reg_hp_status1 = val;
        break;
    case REG_HP_STATUS2:
        s->reg_hp_status2 = val;
        break;
    case REG_PLUS_RX_DB1_LOW:
        s->reg_plus_rx_db1 = (s->reg_plus_rx_db1 & ~0xFFFFFFFFULL) | (val & 0xFFFFFFFFULL);
        break;
    case REG_PLUS_RX_DB1_HI:
        s->reg_plus_rx_db1 = (s->reg_plus_rx_db1 & 0xFFFFFFFFULL) | ((uint64_t)val << 32);
        break;
    case REG_PLUS_RX_CB1_LOW:
        s->reg_plus_rx_cb1 = (s->reg_plus_rx_cb1 & ~0xFFFFFFFFULL) | (val & 0xFFFFFFFFULL);
        break;
    case REG_PLUS_RX_CB1_HI:
        s->reg_plus_rx_cb1 = (s->reg_plus_rx_cb1 & 0xFFFFFFFFULL) | ((uint64_t)val << 32);
        break;
    case REG_PLUS_RX_KICK1:
        s->reg_plus_rx_kick1 = val;
        break;
    case REG_PLUS_RX_COMP1_TAIL:
        s->reg_plus_rx_comp1_tail = val;
        break;
    case REG_PLUS_RX_AE1_THRESH:
        s->reg_plus_rx_ae1_thresh = val;
        break;
    case REG_MAC_TX_RESET:
        /* Writing 1 triggers reset; auto-clears */
        s->reg_mac_tx_reset = 0;
        break;
    case REG_MAC_RX_RESET:
        s->reg_mac_rx_reset = 0;
        break;
    case REG_MAC_SEND_PAUSE:
        s->reg_mac_send_pause = val;
        break;
    case REG_MAC_TX_STATUS:
        s->reg_mac_tx_status = val;
        break;
    case REG_MAC_RX_STATUS:
        s->reg_mac_rx_status = val;
        break;
    case REG_MAC_CTRL_STATUS:
        s->reg_mac_ctrl_status = val;
        break;
    case REG_MAC_TX_MASK:
        s->reg_mac_tx_mask = val;
        break;
    case REG_MAC_RX_MASK:
        s->reg_mac_rx_mask = val;
        break;
    case REG_MAC_CTRL_MASK:
        s->reg_mac_ctrl_mask = val;
        break;
    case REG_MAC_TX_CFG:
        s->reg_mac_tx_cfg = val;
        break;
    case REG_MAC_RX_CFG:
        s->reg_mac_rx_cfg = val;
        break;
    case REG_MAC_CTRL_CFG:
        s->reg_mac_ctrl_cfg = val;
        break;
    case REG_MAC_XIF_CFG:
        s->reg_mac_xif_cfg = val;
        break;
    case REG_MAC_IPG0:
        s->reg_mac_ipg0 = val;
        break;
    case REG_MAC_IPG1:
        s->reg_mac_ipg1 = val;
        break;
    case REG_MAC_IPG2:
        s->reg_mac_ipg2 = val;
        break;
    case REG_MAC_SLOT_TIME:
        s->reg_mac_slot_time = val;
        break;
    case REG_MAC_FRAMESIZE_MIN:
        s->reg_mac_framesize_min = val;
        break;
    case REG_MAC_FRAMESIZE_MAX:
        s->reg_mac_framesize_max = val;
        break;
    case REG_MAC_PA_SIZE:
        s->reg_mac_pa_size = val;
        break;
    case REG_MAC_JAM_SIZE:
        s->reg_mac_jam_size = val;
        break;
    case REG_MAC_ATTEMPT_LIMIT:
        s->reg_mac_attempt_limit = val;
        break;
    case REG_MAC_CTRL_TYPE:
        s->reg_mac_ctrl_type = val;
        break;
    case REG_MAC_ADDR0:
        s->reg_mac_addr[0] = val;
        break;
    case REG_MAC_ADDR_FILTER0:
        s->reg_mac_addr_filter0 = val;
        break;
    case REG_MAC_ADDR_FILTER1:
        s->reg_mac_addr_filter1 = val;
        break;
    case REG_MAC_ADDR_FILTER2:
        s->reg_mac_addr_filter2 = val;
        break;
    case REG_MAC_ADDR_FILTER2_1_MASK:
        s->reg_mac_addr_filter2_1_mask = val;
        break;
    case REG_MAC_ADDR_FILTER0_MASK:
        s->reg_mac_addr_filter0_mask = val;
        break;
    case REG_MAC_HASH_TABLE0:
        s->reg_mac_hash_table[0] = val;
        break;
    case REG_MAC_RECV_FRAME:
        s->reg_mac_recv_frame = val;
        break;
    case REG_MAC_COLL_NORMAL:
        s->reg_mac_coll_normal = val;
        break;
    case REG_MAC_COLL_FIRST:
        s->reg_mac_coll_first = val;
        break;
    case REG_MAC_COLL_EXCESS:
        s->reg_mac_coll_excess = val;
        break;
    case REG_MAC_COLL_LATE:
        s->reg_mac_coll_late = val;
        break;
    case REG_MAC_TIMER_DEFER:
        s->reg_mac_timer_defer = val;
        break;
    case REG_MAC_ATTEMPTS_PEAK:
        s->reg_mac_attempts_peak = val;
        break;
    case REG_MAC_LEN_ERR:
        s->reg_mac_len_err = val;
        break;
    case REG_MAC_ALIGN_ERR:
        s->reg_mac_align_err = val;
        break;
    case REG_MAC_FCS_ERR:
        s->reg_mac_fcs_err = val;
        break;
    case REG_MAC_RX_CODE_ERR:
        s->reg_mac_rx_code_err = val;
        break;
    case REG_MAC_RANDOM_SEED:
        s->reg_mac_random_seed = val;
        break;
    case REG_MAC_STATE_MACHINE:
        s->reg_mac_state_machine = val;
        break;
    case REG_MIF_FRAME:
    {
        /* MII Management Interface Frame handling */
        uint32_t cmd = val;
        if (cmd & MIF_FRAME_ST) {
            if (cmd & MIF_FRAME_OP_READ) {
                uint8_t reg = (cmd >> 14) & 0x1F;
                uint16_t data = s->phy_regs[reg];
                /* Complete transaction: set TURN_AROUND_LSB and data */
                s->reg_mif_frame = (cmd & ~(MIF_FRAME_DATA_MASK | MIF_FRAME_TURN_AROUND_LSB))
                                   | MIF_FRAME_TURN_AROUND_LSB
                                   | data;
            } else if (cmd & MIF_FRAME_OP_WRITE) {
                uint8_t reg = (cmd >> 14) & 0x1F;
                uint16_t data = cmd & MIF_FRAME_DATA_MASK;
                s->phy_regs[reg] = data;
                /* Write complete: set TURN_AROUND_LSB */
                s->reg_mif_frame = MIF_FRAME_TURN_AROUND_LSB;
            } else {
                s->reg_mif_frame = cmd;
            }
        } else {
            s->reg_mif_frame = cmd;
        }
        break;
    }
    case REG_MIF_CFG:
        s->reg_mif_cfg = val;
        break;
    case REG_MIF_MASK:
        s->reg_mif_mask = val;
        break;
    case REG_MIF_STATUS:
        s->reg_mif_status = val;
        break;
    case REG_MIF_STATE_MACHINE:
        s->reg_mif_state_machine = val;
        break;
    case REG_PCS_MII_CTRL:
        s->reg_pcs_mii_ctrl = val;
        break;
    case REG_PCS_MII_STATUS:
        s->reg_pcs_mii_status = val;
        break;
    case REG_PCS_MII_ADVERT:
        s->reg_pcs_mii_advert = val;
        break;
    case REG_PCS_MII_LPA:
        s->reg_pcs_mii_lpa = val;
        break;
    case REG_PCS_CFG:
        s->reg_pcs_cfg = val;
        break;
    case REG_PCS_STATE_MACHINE:
        s->reg_pcs_state_machine = val;
        break;
    case REG_PCS_INTR_STATUS:
        s->reg_pcs_intr_status = val;
        break;
    case REG_PCS_DATAPATH_MODE:
        s->reg_pcs_datapath_mode = val;
        break;
    case REG_PCS_SERDES_CTRL:
        s->reg_pcs_serdes_ctrl = val;
        break;
    case REG_PCS_SERDES_STATE:
        s->reg_pcs_serdes_state = val;
        break;
    case REG_SECOND_LOCALBUS_START:
    case REG_ENTROPY_START:
        /* Ignore writes to the base address */
        break;
    case REG_ENTROPY_IV:
        s->reg_entropy_iv = val;
        break;
    case REG_ENTROPY_RAND_REG:
        s->reg_entropy_rand_reg = val;
        break;
    case REG_ENTROPY_RESET:
        s->reg_entropy_reset = val;
        break;
    default:
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

    /* Reset all registers to default values */
    memset(&s->reg_cawr, 0, (uintptr_t)&s->phy_regs[0] - (uintptr_t)&s->reg_cawr + sizeof(s->phy_regs));
    s->reg_tx_fifo_size = 0x10; /* 16 * 64 = 1024 bytes */
    s->reg_bim_cfg = 0x0; /* will be set by driver */
    s->reg_mif_cfg = 0x1; /* MIF_CFG_MDIO_0: MDIO0 line present */
    s->intr_status = 0;
    s->intr_mask = 0xFFFFFFFF;
    s->reg_sw_reset = 0;
    s->reg_mac_tx_reset = 0;
    s->reg_mac_rx_reset = 0;

    /* Initialize MII PHY registers */
    s->phy_regs[0] = 0x1000; /* BMCR: default */
    s->phy_regs[1] = 0x7869; /* BMSR: link up, autoneg complete, 1000MB capable */
    s->phy_regs[2] = 0x0141; /* PHYID1 */
    s->phy_regs[3] = 0x0e11; /* PHYID2 */
    /* Others zero */
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
        /* PIO not used */
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_SUN);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_SUN_CASSINI);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_ETHERNET);
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
    s->bar_info[0] = (BARInfo) {
        .index = 0,
        .type = BAR_TYPE_MMIO,
        .size = 0x200000,  /* 2 MB, covers all register offsets */
        .name = "cassini-mmio"
    };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI/MSI-X not used; legacy IRQ only */

    /* DMA configuration: base addresses will be set via registers */

    /* No hardware timers needed */

    /* Set initial register defaults */
    pcibase_reset(DEVICE(pdev));
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }

    /* No dynamic resources to free */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "cassini_pci",
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
