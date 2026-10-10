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

#define TYPE_PCIBASE_DEVICE "et131x_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_ATT 0x11c1
#define ET131X_PCI_DEVICE_ID_GIG 0xED00
#define ET131X_PCI_DEVICE_ID_FAST 0xED01
#define HZ 100

#define MII_BMCR            0x00
#define MII_BMSR            0x01
#define MII_PHYSID1         0x02
#define MII_PHYSID2         0x03
#define BMSR_LSTATUS        0x0004
#define BMSR_ANEGCAPABLE    0x0008
#define BMSR_100FULL        0x4000
#define BMSR_100HALF        0x2000

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

struct global_regs {
    uint32_t txq_start_addr;
    uint32_t txq_end_addr;
    uint32_t rxq_start_addr;
    uint32_t rxq_end_addr;
    uint32_t pm_csr;
    uint32_t unused;
    uint32_t int_status;
    uint32_t int_mask;
    uint32_t int_alias_clr_en;
    uint32_t int_status_alias;
    uint32_t sw_reset;
    uint32_t slv_timer;
    uint32_t msi_config;
    uint32_t loopback;
    uint32_t watchdog_timer;
};

struct rxdma_regs {
    uint32_t csr;
    uint32_t dma_wb_base_lo;
    uint32_t dma_wb_base_hi;
    uint32_t num_pkt_done;
    uint32_t max_pkt_time;
    uint32_t rxq_rd_addr;
    uint32_t rxq_rd_addr_ext;
    uint32_t rxq_wr_addr;
    uint32_t psr_base_lo;
    uint32_t psr_base_hi;
    uint32_t psr_num_des;
    uint32_t psr_avail_offset;
    uint32_t psr_full_offset;
    uint32_t psr_access_index;
    uint32_t psr_min_des;
    uint32_t fbr0_base_lo;
    uint32_t fbr0_base_hi;
    uint32_t fbr0_num_des;
    uint32_t fbr0_avail_offset;
    uint32_t fbr0_full_offset;
    uint32_t fbr0_rd_index;
    uint32_t fbr0_min_des;
    uint32_t fbr1_base_lo;
    uint32_t fbr1_base_hi;
    uint32_t fbr1_num_des;
    uint32_t fbr1_avail_offset;
    uint32_t fbr1_full_offset;
    uint32_t fbr1_rd_index;
    uint32_t fbr1_min_des;
};

struct txdma_regs {
    uint32_t csr;
    uint32_t pr_base_hi;
    uint32_t pr_base_lo;
    uint32_t pr_num_des;
    uint32_t txq_wr_addr;
    uint32_t txq_wr_addr_ext;
    uint32_t txq_rd_addr;
    uint32_t dma_wb_base_hi;
    uint32_t dma_wb_base_lo;
    uint32_t service_request;
    uint32_t service_complete;
    uint32_t cache_rd_index;
    uint32_t cache_wr_index;
    uint32_t tx_dma_error;
    uint32_t desc_abort_cnt;
    uint32_t payload_abort_cnt;
    uint32_t writeback_abort_cnt;
    uint32_t desc_timeout_cnt;
    uint32_t payload_timeout_cnt;
    uint32_t writeback_timeout_cnt;
    uint32_t desc_error_cnt;
    uint32_t payload_error_cnt;
    uint32_t writeback_error_cnt;
    uint32_t dropped_tlp_cnt;
    uint32_t new_service_complete;
    uint32_t ethernet_packet_cnt;
};

struct txmac_regs {
    uint32_t ctl;
    uint32_t shadow_ptr;
    uint32_t err_cnt;
    uint32_t max_fill;
    uint32_t cf_param;
    uint32_t tx_test;
    uint32_t err;
    uint32_t err_int;
    uint32_t bp_ctrl;
};

struct rxmac_regs {
    uint32_t ctrl;
    uint32_t crc0;
    uint32_t crc12;
    uint32_t crc34;
    uint32_t sa_lo;
    uint32_t sa_hi;
    uint32_t mask0_word0;
    uint32_t mask0_word1;
    uint32_t mask0_word2;
    uint32_t mask0_word3;
    uint32_t mask1_word0;
    uint32_t mask1_word1;
    uint32_t mask1_word2;
    uint32_t mask1_word3;
    uint32_t mask2_word0;
    uint32_t mask2_word1;
    uint32_t mask2_word2;
    uint32_t mask2_word3;
    uint32_t mask3_word0;
    uint32_t mask3_word1;
    uint32_t mask3_word2;
    uint32_t mask3_word3;
    uint32_t mask4_word0;
    uint32_t mask4_word1;
    uint32_t mask4_word2;
    uint32_t mask4_word3;
    uint32_t uni_pf_addr1;
    uint32_t uni_pf_addr2;
    uint32_t uni_pf_addr3;
    uint32_t multi_hash1;
    uint32_t multi_hash2;
    uint32_t multi_hash3;
    uint32_t multi_hash4;
    uint32_t pf_ctrl;
    uint32_t mcif_ctrl_max_seg;
    uint32_t mcif_water_mark;
    uint32_t rxq_diag;
    uint32_t space_avail;
    uint32_t mif_ctrl;
    uint32_t err_reg;
};

struct mac_regs {
    uint32_t cfg1;
    uint32_t cfg2;
    uint32_t ipg;
    uint32_t hfdp;
    uint32_t max_fm_len;
    uint32_t rsv1;
    uint32_t rsv2;
    uint32_t mac_test;
    uint32_t mii_mgmt_cfg;
    uint32_t mii_mgmt_cmd;
    uint32_t mii_mgmt_addr;
    uint32_t mii_mgmt_ctrl;
    uint32_t mii_mgmt_stat;
    uint32_t mii_mgmt_indicator;
    uint32_t if_ctrl;
    uint32_t if_stat;
    uint32_t station_addr_1;
    uint32_t station_addr_2;
};

struct macstat_regs {
    uint32_t pad[32];
    uint32_t txrx_0_64_byte_frames;
    uint32_t txrx_65_127_byte_frames;
    uint32_t txrx_128_255_byte_frames;
    uint32_t txrx_256_511_byte_frames;
    uint32_t txrx_512_1023_byte_frames;
    uint32_t txrx_1024_1518_byte_frames;
    uint32_t txrx_1519_1522_gvln_frames;
    uint32_t rx_bytes;
    uint32_t rx_packets;
    uint32_t rx_fcs_errs;
    uint32_t rx_multicast_packets;
    uint32_t rx_broadcast_packets;
    uint32_t rx_control_frames;
    uint32_t rx_pause_frames;
    uint32_t rx_unknown_opcodes;
    uint32_t rx_align_errs;
    uint32_t rx_frame_len_errs;
    uint32_t rx_code_errs;
    uint32_t rx_carrier_sense_errs;
    uint32_t rx_undersize_packets;
    uint32_t rx_oversize_packets;
    uint32_t rx_fragment_packets;
    uint32_t rx_jabbers;
    uint32_t rx_drops;
    uint32_t tx_bytes;
    uint32_t tx_packets;
    uint32_t tx_multicast_packets;
    uint32_t tx_broadcast_packets;
    uint32_t tx_pause_frames;
    uint32_t tx_deferred;
    uint32_t tx_excessive_deferred;
    uint32_t tx_single_collisions;
    uint32_t tx_multiple_collisions;
    uint32_t tx_late_collisions;
    uint32_t tx_excessive_collisions;
    uint32_t tx_total_collisions;
    uint32_t tx_pause_honored_frames;
    uint32_t tx_drops;
    uint32_t tx_jabbers;
    uint32_t tx_fcs_errs;
    uint32_t tx_control_frames;
    uint32_t tx_oversize_frames;
    uint32_t tx_undersize_frames;
    uint32_t tx_fragments;
    uint32_t carry_reg1;
    uint32_t carry_reg2;
    uint32_t carry_reg1_mask;
    uint32_t carry_reg2_mask;
};

struct mmc_regs {
    uint32_t mmc_ctrl;
    uint32_t sram_access;
    uint32_t sram_word1;
    uint32_t sram_word2;
    uint32_t sram_word3;
    uint32_t sram_word4;
};

struct address_map {
    struct global_regs global;
    uint8_t unused_global[4096 - sizeof(struct global_regs)];
    struct txdma_regs txdma;
    uint8_t unused_txdma[4096 - sizeof(struct txdma_regs)];
    struct rxdma_regs rxdma;
    uint8_t unused_rxdma[4096 - sizeof(struct rxdma_regs)];
    struct txmac_regs txmac;
    uint8_t unused_txmac[4096 - sizeof(struct txmac_regs)];
    struct rxmac_regs rxmac;
    uint8_t unused_rxmac[4096 - sizeof(struct rxmac_regs)];
    struct mac_regs mac;
    uint8_t unused_mac[4096 - sizeof(struct mac_regs)];
    struct macstat_regs macstat;
    uint8_t unused_mac_stat[4096 - sizeof(struct macstat_regs)];
    struct mmc_regs mmc;
    uint8_t unused_mmc[4096 - sizeof(struct mmc_regs)];
    uint8_t unused_[1015808];
    uint8_t unused_exp_rom[4096];
    uint8_t unused__[524288];
};

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;
    uint32_t int_status;
    uint32_t int_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    struct address_map regs;

    /* DMA Context */
    struct tx_desc {
        uint32_t addr_hi;
        uint32_t addr_lo;
        uint32_t len_vlan;
        uint32_t flags;
    } tx_desc;
    struct fbr_desc {
        uint32_t addr_lo;
        uint32_t addr_hi;
        uint32_t word2;
    } fbr_desc;
    struct pkt_stat_desc {
        uint32_t word0;
        uint32_t word1;
    } pkt_stat_desc;
    struct rx_status_block {
        uint32_t word0;
        uint32_t word1;
    } rx_status_block;

};

#define MAX_NUM_REGISTER_POLLS 1000
#define MAX_NUM_WRITE_RETRIES 2
#define COUNTER_WRAP_16_BIT 0x10000
#define COUNTER_WRAP_12_BIT 0x1000
#define INTERNAL_MEM_SIZE 0x400
#define INTERNAL_MEM_RX_OFFSET 0x1FF
#define INT_MASK_DISABLE 0xffffffff
#define INT_MASK_ENABLE 0xfffebf17
#define INT_MASK_ENABLE_NO_FLOW 0xfffebfd7
#define NIC_MIN_PACKET_SIZE 60
#define NIC_MAX_MCAST_LIST 128
#define ET131X_PACKET_TYPE_DIRECTED 0x0001
#define ET131X_PACKET_TYPE_MULTICAST 0x0002
#define ET131X_PACKET_TYPE_BROADCAST 0x0004
#define ET131X_PACKET_TYPE_PROMISCUOUS 0x0008
#define ET131X_PACKET_TYPE_ALL_MULTICAST 0x0010
#define ET131X_TX_TIMEOUT (1 * HZ)
#define NIC_SEND_HANG_THRESHOLD 0
#define FMP_ADAPTER_INTERRUPT_IN_USE 0x00000008
#define FMP_ADAPTER_LOWER_POWER 0x00200000
#define FMP_ADAPTER_NON_RECOVER_ERROR 0x00800000
#define FMP_ADAPTER_HARDWARE_ERROR 0x04000000
#define FMP_ADAPTER_FAIL_SEND_MASK 0x3ff00000
#define ET1310_PCI_MAC_ADDRESS 0xA4
#define ET1310_PCI_EEPROM_STATUS 0xB2
#define ET1310_PCI_ACK_NACK 0xC0
#define ET1310_PCI_REPLAY 0xC2
#define ET1310_PCI_L0L1LATENCY 0xCF
#define NANO_IN_A_MICRO 1000
#define PARM_RX_NUM_BUFS_DEF 4
#define PARM_RX_TIME_INT_DEF 10
#define PARM_RX_MEM_END_DEF 0x2bc
#define PARM_TX_TIME_INT_DEF 40
#define PARM_TX_NUM_BUFS_DEF 4
#define PARM_DMA_CACHE_DEF 0
#define FBR_CHUNKS 32
#define MAX_DESC_PER_RING_RX 1024
#define RFD_LOW_WATER_MARK 40
#define NIC_DEFAULT_NUM_RFD 1024
#define NUM_FBRS 2
#define MAX_PACKETS_HANDLED 256
#define ET131X_MIN_MTU 64
#define ET131X_MAX_MTU 9216
#define ALCATEL_MULTICAST_PKT 0x01000000
#define ALCATEL_BROADCAST_PKT 0x02000000
#define TXDESC_FLAG_LASTPKT 0x0001
#define TXDESC_FLAG_FIRSTPKT 0x0002
#define TXDESC_FLAG_INTPROC 0x0004
#define NUM_DESC_PER_RING_TX 512
#define NUM_TCB 64
#define TX_ERROR_PERIOD 1000
#define LO_MARK_PERCENT_FOR_PSR 15
#define LO_MARK_PERCENT_FOR_RX 15
#define FLOW_BOTH 0
#define FLOW_TXONLY 1
#define FLOW_RXONLY 2
#define FLOW_NONE 3
#define MAX_TX_DESC_PER_PKT 24
#define ET131X_REGS_LEN 256
#define LBCIF_CONTROL_LBCIF_ENABLE 0x80
#define LBCIF_STATUS_GENERAL_ERROR 0x08
#define LBCIF_CONTROL_REGISTER 0xB1
#define LBCIF_ADDRESS_REGISTER 0xAC
#define LBCIF_CONTROL_I2C_WRITE 0x40
#define LBCIF_DATA_REGISTER 0xB0
#define LBCIF_STATUS_ACK_ERROR 0x04
#define LBCIF_DWORD1_GROUP 0xB0
#define ET_RXDMA_CSR_FBR0_SIZE_LO 0x0100
#define ET_RXDMA_CSR_FBR1_SIZE_LO 0x0800
#define ET_RXDMA_CSR_HALT_STATUS 0x00020000
#define ET_RXDMA_CSR_FBR0_SIZE_HI 0x0200
#define ET_RXDMA_CSR_FBR1_SIZE_HI 0x1000
#define ET_RXDMA_CSR_FBR1_ENABLE 0x2000
#define ET_RXDMA_CSR_FBR0_ENABLE 0x0400
#define ET_RXDMA_CSR_HALT 0x0001
#define ET_TXDMA_CACHE_SHIFT 4
#define ET_TXDMA_SNGL_EPKT 0x00000100
#define ET_DMA10_WRAP 0x0400
#define INDEX10(x) ((x) & ET_DMA10_MASK)
#define ET_DMA12_WRAP 0x1000
#define INDEX12(x) ((x) & ET_DMA12_MASK)
#define ET_MAC_STATION_ADDR1_OC4_SHIFT 8
#define ET_MAC_CFG1_RESET_RXFUNC 0x00020000
#define ET_MAC_CFG1_SIM_RESET 0x40000000
#define ET_MAC_CFG1_RESET_TXMC 0x00040000
#define ET_MAC_STATION_ADDR2_OC2_SHIFT 24
#define ET_MAC_CFG1_SOFT_RESET 0x80000000
#define ET_MAC_STATION_ADDR1_OC6_SHIFT 24
#define ET_MAC_STATION_ADDR1_OC5_SHIFT 16
#define ET_MAC_MIIMGMT_CLK_RST 0x0007
#define ET_MAC_STATION_ADDR2_OC1_SHIFT 16
#define ET_MAC_CFG1_RESET_RXMC 0x00080000
#define ET_MAC_CFG1_RESET_TXFUNC 0x00010000
#define ET_MAC_CFG1_TX_ENABLE 0x00000001
#define ET_MAC_CFG2_IFMODE_PAD_CRC 0x0004
#define ET_MAC_CFG2_IFMODE_HUGE_FRAME 0x0020
#define ET_MAC_CFG2_IFMODE_MASK 0x0300
#define ET_MAC_CFG1_LOOPBACK 0x00000100
#define ET_MAC_CFG1_TX_FLOW 0x00000010
#define ET_MAC_CFG2_IFMODE_FULL_DPLX 0x0001
#define ET_MAC_CFG2_PREAMBLE_SHIFT 12
#define ET_MAC_CFG1_RX_FLOW 0x00000020
#define ET_MAC_CFG2_IFMODE_LEN_CHECK 0x0010
#define ET_MAC_CFG1_WAIT 0x0000000A
#define ET_MAC_CFG2_IFMODE_CRC_ENABLE 0x0002
#define ET_MAC_IFCTRL_GHDMODE (1 << 26)
#define ET_TX_CTRL_TXMAC_ENABLE 0x0001
#define ET_TX_CTRL_FC_DISABLE 0x0008
#define ET_MAC_IFCTRL_PHYMODE (1 << 24)
#define ET_MAC_CFG2_IFMODE_1000 0x0200
#define ET_MAC_CFG2_IFMODE_100 0x0100
#define ET_MAC_CFG1_RX_ENABLE 0x00000004
#define ET_PM_PHY_SW_COMA 0x40
#define ET_RX_UNI_PF_ADDR1_1_SHIFT 8
#define ET_RX_UNI_PF_ADDR1_5_SHIFT 8
#define ET_RX_UNI_PF_ADDR1_3_SHIFT 24
#define ET_RX_UNI_PF_ADDR2_2_SHIFT 16
#define ET_RX_UNI_PF_ADDR2_4_SHIFT 16
#define ET_RX_UNI_PF_ADDR2_3_SHIFT 24
#define ET_RX_UNI_PF_ADDR1_4_SHIFT 16
#define ET_RX_UNI_PF_ADDR2_5_SHIFT 8
#define ET_RX_UNI_PF_ADDR2_1_SHIFT 24
#define ET_RX_CTRL_RXMAC_ENABLE 0x0001
#define ET_RX_CTRL_WOL_DISABLE 0x0008
#define ET_RX_WOL_LO_SA4_SHIFT 16
#define ET_RX_WOL_LO_SA3_SHIFT 24
#define ET_RX_PFCTRL_UNICST_FILTER_ENABLE 0x0004
#define ET_RX_PFCTRL_MLTCST_FILTER_ENABLE 0x0002
#define ET_RX_WOL_HI_SA1_SHIFT 8
#define ET_RX_WOL_LO_SA5_SHIFT 8
#define ET_RX_PFCTRL_MIN_PKT_SZ_SHIFT 16
#define ET_RX_PFCTRL_FRAG_FILTER_ENABLE 0x0008
#define ET_MAC_MIIMGMT_STAT_PHYCRTL_MASK 0xFFFF
#define ET_MAC_MII_ADDR(phy, reg) ((phy) << 8 | (reg))
#define ET_MAC_MGMT_WAIT 0x00000005
#define ET_MAC_MGMT_BUSY 0x00000001
#define LED_TXRX_SHIFT 8
#define ET_LED2_LED_1000T 0x000F
#define PHY_LED_2 0x1C
#define LED_VAL_LINKON_ACTIVE 0xA
#define LED_LINK_SHIFT 12
#define ET_LED2_LED_100TX 0x00F0
#define LED_VAL_1000BT_100BTX 0x3
#define LED_VAL_LINKON 0x4
#define ET_RXDMA_PSR_NUM_DES_MASK 0xFFF
#define ET_MMC_ENABLE 1
#define ET_RESET_ALL 0x007F
#define ET_TXDMA_CSR_HALT 0x00000001
#define ET_PMCSR_INIT 0x38
#define ET_DMA10_MASK 0x03FF
#define PHY_INTERRUPT_STATUS 0x19
#define PHY_CONFIG 0x16
#define PHY_INDEX_REG 0x10
#define PHY_PHY_CONTROL 0x17
#define PHY_LOOPBACK_CONTROL 0x13
#define PHY_DATA_REG 0x11
#define PHY_MPHY_CONTROL_REG 0x12
#define PHY_LED_1 0x1B
#define PHY_REGISTER_MGMT_CONTROL 0x15
#define PHY_INTERRUPT_MASK 0x18
#define PHY_PHY_STATUS 0x1A
#define ET_PHY_CONFIG_FIFO_DEPTH_32 0x2000
#define ET_PHY_CONFIG_TX_FIFO_DEPTH 0x3000
#define ET_INTR_RXDMA_FB_R0_LOW 0x00000040
#define ET_INTR_SLV_TIMEOUT 0x00100000
#define ET_INTR_TXDMA_ERR 0x00000010
#define ET_INTR_RXDMA_STAT_LOW 0x00000100
#define ET_INTR_TXMAC 0x00020000
#define ET_INTR_WOL 0x00008000
#define ET_INTR_RXDMA_FB_R1_LOW 0x00000080
#define ET_INTR_RXDMA_ERR 0x00000200
#define ET_INTR_RXMAC 0x00040000
#define ET_INTR_WATCHDOG 0x00004000
#define ET_INTR_TXDMA_ISR 0x00000008
#define ET_INTR_RXDMA_XFR_DONE 0x00000020
#define ET_INTR_MAC_STAT 0x00080000
#define ET_DMA12_MASK 0x0FFF

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t status = s->regs.global.int_status & s->regs.global.int_mask;
    
    if (status) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    if (is_write) {
        s->regs.txdma.new_service_complete = s->regs.txdma.service_request;
        s->regs.global.int_status |= ET_INTR_TXDMA_ISR;
        pcibase_update_irq(s);
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = 0;

    if (addr < sizeof(s->regs)) {
        if (size == 4) {
            val = *(uint32_t *)((uint8_t *)&s->regs + addr);
        } else if (size == 2) {
            val = *(uint16_t *)((uint8_t *)&s->regs + addr);
        } else if (size == 1) {
            val = *(uint8_t *)((uint8_t *)&s->regs + addr);
        }
    }

    if (addr == offsetof(struct address_map, mac.mii_mgmt_indicator)) {
        val = 0;
    } else if (addr == offsetof(struct address_map, mac.cfg1)) {
        val |= ET_MAC_CFG1_WAIT;
    } else if (addr == offsetof(struct address_map, rxdma.csr)) {
        if (s->regs.rxdma.csr & ET_RXDMA_CSR_HALT) {
            val |= ET_RXDMA_CSR_HALT_STATUS;
        } else {
            val &= ~ET_RXDMA_CSR_HALT_STATUS;
        }
    } else if (addr == offsetof(struct address_map, global.int_status)) {
        uint32_t ret = s->regs.global.int_status;
        s->regs.global.int_status = 0;
        pcibase_update_irq(s);
        return ret;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < sizeof(s->regs)) {
        if (size == 4) {
            *(uint32_t *)((uint8_t *)&s->regs + addr) = val;
        } else if (size == 2) {
            *(uint16_t *)((uint8_t *)&s->regs + addr) = val;
        } else if (size == 1) {
            *(uint8_t *)((uint8_t *)&s->regs + addr) = val;
        }
    }

    if (addr == offsetof(struct address_map, mac.mii_mgmt_cmd)) {
        if (val == 1) {
            uint32_t mii_addr = s->regs.mac.mii_mgmt_addr;
            uint8_t phy_reg = mii_addr & 0xFF;
            if (phy_reg == MII_PHYSID1) {
                s->regs.mac.mii_mgmt_stat = 0x0040;
            } else if (phy_reg == MII_PHYSID2) {
                s->regs.mac.mii_mgmt_stat = 0x61E0;
            } else if (phy_reg == MII_BMSR) {
                s->regs.mac.mii_mgmt_stat = BMSR_LSTATUS | BMSR_ANEGCAPABLE | BMSR_100FULL | BMSR_100HALF;
            } else {
                s->regs.mac.mii_mgmt_stat = 0;
            }
            s->regs.mac.mii_mgmt_indicator = 0;
        }
    } else if (addr == offsetof(struct address_map, mac.mii_mgmt_ctrl)) {
        s->regs.mac.mii_mgmt_indicator = 0;
    } else if (addr == offsetof(struct address_map, txdma.service_request)) {
        pcibase_do_dma(s, true);
    } else if (addr == offsetof(struct address_map, macstat.carry_reg1)) {
        s->regs.macstat.carry_reg1 &= ~val;
    } else if (addr == offsetof(struct address_map, macstat.carry_reg2)) {
        s->regs.macstat.carry_reg2 &= ~val;
    } else if (addr == offsetof(struct address_map, global.int_mask)) {
        pcibase_update_irq(s);
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
    memset(&s->regs, 0, sizeof(s->regs));
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
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_ATT );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  ET131X_PCI_DEVICE_ID_GIG );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_ETHERNET );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* EEPROM dummy data */
    pci_set_long(pci_conf + 0xB0, 0x0001F0CD);

    /* MAC address dummy data */
    pci_set_byte(pci_conf + 0xA4, 0x00);
    pci_set_byte(pci_conf + 0xA5, 0x11);
    pci_set_byte(pci_conf + 0xA6, 0x22);
    pci_set_byte(pci_conf + 0xA7, 0x33);
    pci_set_byte(pci_conf + 0xA8, 0x44);
    pci_set_byte(pci_conf + 0xA9, 0x55);

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
    s->bar_info[0].size = sizeof(struct address_map);
    s->bar_info[0].name = "et131x-mmio";  
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

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

}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "et131x_pci",
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
