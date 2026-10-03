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
#include "net/eth.h"

#define TYPE_PCIBASE_DEVICE "hibmcge_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define HBG_STATUS_ENABLE		0x1
#define HBG_STATUS_DISABLE		0x0
#define HBG_TX_TIMEOUT_BUF_LEN		1024
#define HBG_INT_MSK_TX_B			(1 << 1)
#define HBG_INT_MSK_RX_B			(1 << 0)
#define HBG_REG_SGMII_BASE			0x10000
#define HBG_REG_MDIO_BASE			0x8000

#define HBG_REG_CF_IND_RXINT_MSK_ADDR		(HBG_REG_SGMII_BASE + 0x06a0)
#define HBG_REG_CF_INTRPT_MSK_ADDR		(HBG_REG_SGMII_BASE + 0x042C)
#define HBG_REG_CF_IND_TXINT_MSK_ADDR		(HBG_REG_SGMII_BASE + 0x0694)
#define HBG_REG_PORT_ENABLE_ADDR		(HBG_REG_SGMII_BASE + 0x0044)
#define HBG_REG_EVENT_REQ_ADDR			0x0004
#define HBG_REG_STATION_ADDR_LOW_MSK_1		(HBG_REG_SGMII_BASE + 0x0238)
#define HBG_REG_REC_FILT_CTRL_ADDR		(HBG_REG_SGMII_BASE + 0x0064)
#define HBG_REG_STATION_ADDR_LOW_MSK_0		(HBG_REG_SGMII_BASE + 0x0230)
#define HBG_REG_STATION_ADDR_LOW_2_ADDR		(HBG_REG_SGMII_BASE + 0x0210)
#define HBG_REG_FD_FC_ADDR_LOW_ADDR		(HBG_REG_SGMII_BASE + 0x0020)
#define HBG_REG_BRUST_LENGTH_ADDR		(HBG_REG_SGMII_BASE + 0x04C4)
#define HBG_REG_CF_CFF_DATA_NUM_ADDR		(HBG_REG_SGMII_BASE + 0x045C)
#define HBG_REG_PUSH_REQ_ADDR			0x00F0
#define HBG_REG_PAUSE_ENABLE_ADDR		(HBG_REG_SGMII_BASE + 0x0048)
#define HBG_REG_MODE_CHANGE_EN_ADDR		(HBG_REG_SGMII_BASE + 0x01B4)
#define HBG_REG_BUS_CTRL_ADDR			(HBG_REG_SGMII_BASE + 0x04E8)
#define HBG_REG_MAX_FRAME_SIZE_ADDR		(HBG_REG_SGMII_BASE + 0x003C)
#define HBG_REG_MAX_FRAME_LEN_ADDR		(HBG_REG_SGMII_BASE + 0x0444)
#define HBG_REG_RX_UNKNOWN_MACCTL_FRAMCOUNTER_ADDR (HBG_REG_SGMII_BASE + 0x00CC)
#define HBG_REG_RX_PAUSE_MACCTL_FRAMCOUNTER_ADDR   (HBG_REG_SGMII_BASE + 0x00C8)
#define HBG_REG_TX_PAUSE_FRAMES_ADDR		(HBG_REG_SGMII_BASE + 0x015C)
#define HBG_REG_RX_BUFRQ_ERR_CNT_ADDR		(HBG_REG_SGMII_BASE + 0x058C)
#define HBG_REG_RX_FILT_PKT_CNT_ADDR		(HBG_REG_SGMII_BASE + 0x00E8)
#define HBG_REG_TX_CRC_ERROR_ADDR		(HBG_REG_SGMII_BASE + 0x0158)
#define HBG_REG_RX_SHORT_ERR_CNT_ADDR		(HBG_REG_SGMII_BASE + 0x00D8)
#define HBG_REG_RX_FAIL_COMMA_CNT_ADDR		(HBG_REG_SGMII_BASE + 0x01F8)
#define HBG_REG_RX_LONG_ERRORS_ADDR		(HBG_REG_SGMII_BASE + 0x00C0)
#define HBG_REG_RX_JABBER_ERRORS_ADDR		(HBG_REG_SGMII_BASE + 0x00C4)
#define HBG_REG_RX_TAGGED_ADDR			(HBG_REG_SGMII_BASE + 0x00B4)
#define HBG_REG_TX_UNDERRUN_ADDR		(HBG_REG_SGMII_BASE + 0x0150)
#define HBG_REG_TX_BUFRL_ERR_CNT_ADDR		(HBG_REG_SGMII_BASE + 0x0590)
#define HBG_REG_TX_EXCESSIVE_LENGTH_DROP_ADDR	(HBG_REG_SGMII_BASE + 0x014C)
#define HBG_REG_RX_OVERRUN_CNT_ADDR		(HBG_REG_SGMII_BASE + 0x01EC)
#define HBG_REG_OCTETS_TRANSMITTED_BAD_ADDR	(HBG_REG_SGMII_BASE + 0x0104)
#define HBG_REG_RX_DATA_ERR_ADDR		(HBG_REG_SGMII_BASE + 0x00B8)
#define HBG_REG_TX_CS_FAIL_CNT_ADDR		(HBG_REG_SGMII_BASE + 0x0460)
#define HBG_REG_RX_RUNT_ERR_CNT_ADDR		(HBG_REG_SGMII_BASE + 0x00D4)
#define HBG_REG_RX_LENGTHFIELD_ERR_CNT_ADDR	(HBG_REG_SGMII_BASE + 0x01F4)
#define HBG_REG_TX_DROP_CNT_ADDR		(HBG_REG_SGMII_BASE + 0x0448)
#define HBG_REG_RX_WE_ERR_CNT_ADDR		(HBG_REG_SGMII_BASE + 0x0594)
#define HBG_REG_RX_OCTETS_BAD_ADDR		(HBG_REG_SGMII_BASE + 0x0084)
#define HBG_REG_RX_VERY_LONG_ERR_CNT_ADDR	(HBG_REG_SGMII_BASE + 0x00D0)
#define HBG_REG_TX_TAGGED_ADDR			(HBG_REG_SGMII_BASE + 0x0154)
#define HBG_REG_RX_UC_PKTS_ADDR			(HBG_REG_SGMII_BASE + 0x0088)
#define HBG_REG_RX_OVER_FLOW_CNT_ADDR		(HBG_REG_SGMII_BASE + 0x044C)
#define HBG_REG_RX_OCTETS_TOTAL_FILT_ADDR	(HBG_REG_SGMII_BASE + 0x00EC)
#define HBG_REG_TX_UC_PKTS_ADDR			(HBG_REG_SGMII_BASE + 0x0108)
#define HBG_REG_RX_OCTETS_TOTAL_OK_ADDR		(HBG_REG_SGMII_BASE + 0x0080)
#define HBG_REG_RX_FCS_ERRORS_ADDR		(HBG_REG_SGMII_BASE + 0x00B0)
#define HBG_REG_RX_BC_PKTS_ADDR			(HBG_REG_SGMII_BASE + 0x0090)
#define HBG_REG_RX_TRANS_PKG_CNT_ADDR		(HBG_REG_SGMII_BASE + 0x0464)
#define HBG_REG_TX_MC_PKTS_ADDR			(HBG_REG_SGMII_BASE + 0x010C)
#define HBG_REG_TX_BC_PKTS_ADDR			(HBG_REG_SGMII_BASE + 0x0110)
#define HBG_REG_OCTETS_TRANSMITTED_OK_ADDR	(HBG_REG_SGMII_BASE + 0x0100)
#define HBG_REG_RX_MC_PKTS_ADDR			(HBG_REG_SGMII_BASE + 0x008C)
#define HBG_REG_RX_ALIGN_ERRORS_ADDR		(HBG_REG_SGMII_BASE + 0x00BC)
#define HBG_REG_TX_TRANS_PKG_CNT_ADDR		(HBG_REG_SGMII_BASE + 0x0468)
#define HBG_REG_RX_PKTS_65TO127OCTETS_ADDR	(HBG_REG_SGMII_BASE + 0x0098)
#define HBG_REG_TX_PKTS_1024TO1518OCTETS_ADDR	(HBG_REG_SGMII_BASE + 0x0128)
#define HBG_REG_RX_PKTS_1024TO1518OCTETS_ADDR	(HBG_REG_SGMII_BASE + 0x00A8)
#define HBG_REG_TX_PKTS_256TO511OCTETS_ADDR	(HBG_REG_SGMII_BASE + 0x0120)
#define HBG_REG_TX_PKTS_65TO127OCTETS_ADDR	(HBG_REG_SGMII_BASE + 0x0118)
#define HBG_REG_RX_PKTS_512TO1023OCTETS_ADDR	(HBG_REG_SGMII_BASE + 0x00A4)
#define HBG_REG_RX_PKTS_1519TOMAXOCTETS_ADDR	(HBG_REG_SGMII_BASE + 0x00AC)
#define HBG_REG_TX_PKTS_512TO1023OCTETS_ADDR	(HBG_REG_SGMII_BASE + 0x0124)
#define HBG_REG_TX_PKTS_1519TOMAXOCTETS_ADDR	(HBG_REG_SGMII_BASE + 0x012C)
#define HBG_REG_RX_PKTS_128TO255OCTETS_ADDR	(HBG_REG_SGMII_BASE + 0x009C)
#define HBG_REG_RX_PKTS_64OCTETS_ADDR		(HBG_REG_SGMII_BASE + 0x0094)
#define HBG_REG_TX_PKTS_128TO255OCTETS_ADDR	(HBG_REG_SGMII_BASE + 0x011C)
#define HBG_REG_RX_PKTS_256TO511OCTETS_ADDR	(HBG_REG_SGMII_BASE + 0x00A0)
#define HBG_REG_TX_PKTS_64OCTETS_ADDR		(HBG_REG_SGMII_BASE + 0x0114)
#define HBG_REG_AN_NEG_STATE_ADDR		(HBG_REG_SGMII_BASE + 0x0058)
#define HBG_REG_MAC_ADDR_ADDR			0x0010
#define HBG_REG_TX_FIFO_NUM_ADDR		0x0030
#define HBG_REG_MIN_MTU_ADDR			0x002C
#define HBG_REG_VLAN_LAYERS_ADDR		0x0038
#define HBG_REG_MAX_MTU_ADDR			0x0028
#define HBG_REG_RX_FIFO_NUM_ADDR		0x0034
#define HBG_REG_PHY_ID_ADDR			0x000C
#define HBG_REG_UC_MAC_NUM_ADDR			0x0018
#define HBG_REG_MDIO_FREQ_ADDR			0x0024
#define HBG_REG_MAC_ID_ADDR			0x0008
#define HBG_REG_CFG_FIFO_THRSLD_ADDR		(HBG_REG_SGMII_BASE + 0x0428)
#define HBG_REG_RX_BUF_SIZE_ADDR		(HBG_REG_SGMII_BASE + 0x04E4)
#define HBG_REG_CF_CRC_STRIP_ADDR		(HBG_REG_SGMII_BASE + 0x01B0)
#define HBG_REG_RX_PKT_MODE_ADDR		(HBG_REG_SGMII_BASE + 0x04F4)
#define HBG_REG_RECV_CTRL_ADDR			(HBG_REG_SGMII_BASE + 0x01E0)
#define HBG_REG_TRANSMIT_CTRL_ADDR		(HBG_REG_SGMII_BASE + 0x0060)
#define HBG_REG_TX_FIFO_THRSLD_ADDR		(HBG_REG_SGMII_BASE + 0x0420)
#define HBG_REG_RX_FIFO_THRSLD_ADDR		(HBG_REG_SGMII_BASE + 0x0424)
#define HBG_REG_TX_CFF_ADDR_2_ADDR		(HBG_REG_SGMII_BASE + 0x0490)
#define HBG_REG_TX_CFF_ADDR_3_ADDR		(HBG_REG_SGMII_BASE + 0x0494)
#define HBG_REG_TX_CFF_ADDR_0_ADDR		(HBG_REG_SGMII_BASE + 0x0488)
#define HBG_REG_TX_CFF_ADDR_1_ADDR		(HBG_REG_SGMII_BASE + 0x048C)
#define HBG_REG_SPEC_VALID_ADDR			0x0000
#define HBG_REG_RX_CTRL_ADDR			(HBG_REG_SGMII_BASE + 0x04F0)
#define HBG_REG_MDIO_WDATA_ADDR			(HBG_REG_MDIO_BASE + 0x0008)
#define HBG_REG_MDIO_RDATA_ADDR			(HBG_REG_MDIO_BASE + 0x000C)
#define HBG_REG_MDIO_COMMAND_ADDR		(HBG_REG_MDIO_BASE + 0x0000)
#define HBG_REG_MSG_HEADER_ADDR			0x00F4
#define HBG_REG_MSG_DATA_BASE_ADDR		0x0100
#define HBG_REG_MAC_ADDR_HIGH_ADDR		0x0014
#define HBG_REG_DUPLEX_TYPE_ADDR		(HBG_REG_SGMII_BASE + 0x0008)
#define HBG_REG_CF_INTRPT_CLR_ADDR		(HBG_REG_SGMII_BASE + 0x0438)
#define HBG_REG_STATION_ADDR_HIGH_1_ADDR	(HBG_REG_SGMII_BASE + 0x020C)
#define HBG_REG_STATION_ADDR_HIGH_2_ADDR	(HBG_REG_SGMII_BASE + 0x0214)
#define HBG_REG_CF_IND_TXINT_STAT_ADDR		(HBG_REG_SGMII_BASE + 0x0698)
#define HBG_REG_VLAN_CODE_ADDR			(HBG_REG_SGMII_BASE + 0x01E8)
#define HBG_REG_STATION_ADDR_HIGH_0_ADDR	(HBG_REG_SGMII_BASE + 0x0204)
#define HBG_REG_STATION_ADDR_LOW_5_ADDR		(HBG_REG_SGMII_BASE + 0x0228)
#define HBG_REG_FIFO_HIST_STATUS_ADDR		(HBG_REG_SGMII_BASE + 0x0458)
#define HBG_REG_CF_TX_PAUSE_ADDR		(HBG_REG_SGMII_BASE + 0x0470)
#define HBG_REG_RX_CFF_ADDR_ADDR		(HBG_REG_SGMII_BASE + 0x04A0)
#define HBG_REG_STATION_ADDR_HIGH_4_ADDR	(HBG_REG_SGMII_BASE + 0x0224)
#define HBG_REG_DBG_ST0_ADDR			(HBG_REG_SGMII_BASE + 0x05E4)
#define HBG_REG_PORT_MODE_ADDR			(HBG_REG_SGMII_BASE + 0x0040)
#define HBG_REG_STATION_ADDR_LOW_0_ADDR		(HBG_REG_SGMII_BASE + 0x0200)
#define HBG_REG_STATION_ADDR_LOW_1_ADDR		(HBG_REG_SGMII_BASE + 0x0208)
#define HBG_REG_RX_BUS_ERR_ADDR_ADDR		(HBG_REG_SGMII_BASE + 0x0440)
#define HBG_REG_MDIO_STA_ADDR			(HBG_REG_MDIO_BASE + 0x0010)
#define HBG_REG_LINE_LOOP_BACK_ADDR		(HBG_REG_SGMII_BASE + 0x01A8)
#define HBG_REG_FD_FC_TYPE_ADDR			(HBG_REG_SGMII_BASE + 0x000C)
#define HBG_REG_CF_INTRPT_STAT_ADDR		(HBG_REG_SGMII_BASE + 0x0434)
#define HBG_REG_STATION_ADDR_LOW_4_ADDR		(HBG_REG_SGMII_BASE + 0x0220)
#define HBG_REG_FD_FC_ADDR_HIGH_ADDR		(HBG_REG_SGMII_BASE + 0x0024)
#define HBG_REG_CF_IND_RXINT_STAT_ADDR		(HBG_REG_SGMII_BASE + 0x06a4)
#define HBG_REG_CF_IND_RXINT_CLR_ADDR		(HBG_REG_SGMII_BASE + 0x06a8)
#define HBG_REG_FC_TX_TIMER_ADDR		(HBG_REG_SGMII_BASE + 0x001C)
#define HBG_REG_STATION_ADDR_LOW_3_ADDR		(HBG_REG_SGMII_BASE + 0x0218)
#define HBG_REG_FIFO_CURR_STATUS_ADDR		(HBG_REG_SGMII_BASE + 0x0454)
#define HBG_REG_STATION_ADDR_HIGH_3_ADDR	(HBG_REG_SGMII_BASE + 0x021C)
#define HBG_REG_BUS_RST_EN_ADDR			(HBG_REG_SGMII_BASE + 0x0688)
#define HBG_REG_STATION_ADDR_HIGH_5_ADDR	(HBG_REG_SGMII_BASE + 0x022C)
#define HBG_REG_DBG_ST1_ADDR			(HBG_REG_SGMII_BASE + 0x05E8)
#define HBG_REG_DEBUG_ST_MCH_ADDR		(HBG_REG_SGMII_BASE + 0x0450)
#define HBG_REG_TX_BUS_ERR_ADDR_ADDR		(HBG_REG_SGMII_BASE + 0x043C)
#define HBG_REG_MDIO_ADDR_ADDR			(HBG_REG_MDIO_BASE + 0x0004)
#define HBG_REG_DBG_ST2_ADDR			(HBG_REG_SGMII_BASE + 0x05EC)
#define HBG_REG_LOOP_REG_ADDR			(HBG_REG_SGMII_BASE + 0x01DC)
#define HBG_REG_CF_IND_TXINT_CLR_ADDR		(HBG_REG_SGMII_BASE + 0x069C)

#define HBG_REG_BRUST_LENGTH_B			(1 << 29)
#define HBG_REG_PORT_ENABLE_TX_B		(1 << 2)
#define HBG_REG_PORT_ENABLE_RX_B		(1 << 1)
#define HBG_REG_MAX_FRAME_LEN_M			0xFFFF
#define HBG_REG_RX_PKT_MODE_PARSE_MODE_M	(3 << 21)
#define HBG_REG_RECV_CTRL_STRIP_PAD_EN_B	(1 << 3)
#define HBG_REG_RX_BUF_SIZE_M			0xFFFF
#define HBG_REG_CF_CRC_STRIP_B			(1 << 0)
#define HBG_REG_TRANSMIT_CTRL_AN_EN_B		(1 << 5)
#define HBG_REG_TRANSMIT_CTRL_CRC_ADD_B		(1 << 6)
#define HBG_REG_TRANSMIT_CTRL_PAD_EN_B		(1 << 7)
#define HBG_REG_FIFO_THRSLD_FULL_M		(0x3FF << 16)
#define HBG_REG_FIFO_THRSLD_EMPTY_M		0x3FF
#define HBG_REG_CFG_FIFO_THRSLD_TX_FULL_M	(0xFF << 24)
#define HBG_REG_CFG_FIFO_THRSLD_TX_EMPTY_M	(0xFF << 16)
#define HBG_REG_CFG_FIFO_THRSLD_RX_FULL_M	(0xFF << 8)
#define HBG_REG_CFG_FIFO_THRSLD_RX_EMPTY_M	0xFF
#define HBG_REG_AN_NEG_STATE_NP_LINK_OK_B	(1 << 15)
#define HBG_PCU_FRAME_LEN_PLUS 4
#define HBG_PCU_CACHE_LINE_SIZE		32
#define HBG_HW_EVENT_WAIT_INTERVAL_US	(10 * 1000)
#define HBG_HW_EVENT_WAIT_TIMEOUT_US	(2 * 1000 * 1000)

#define HBG_VECTOR_NUM			4

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
    uint32_t intr_mask;
    uint32_t intr_status;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t regs[0x20000 / 4];

    /* DMA Context */
    dma_addr_t tx_ring_dma;
    dma_addr_t rx_ring_dma;
    uint32_t tx_ring_len;
    uint32_t rx_ring_len;

    uint32_t status;
    uint32_t reset_type;
    
    struct hbg_stats {
        uint64_t rx_desc_drop;
        uint64_t rx_desc_l2_err_cnt;
        uint64_t rx_desc_pkt_len_err_cnt;
        uint64_t rx_desc_l3l4_err_cnt;
        uint64_t rx_desc_l3_wrong_head_cnt;
        uint64_t rx_desc_l3_csum_err_cnt;
        uint64_t rx_desc_l3_len_err_cnt;
        uint64_t rx_desc_l3_zero_ttl_cnt;
        uint64_t rx_desc_l3_other_cnt;
        uint64_t rx_desc_l4_err_cnt;
        uint64_t rx_desc_l4_wrong_head_cnt;
        uint64_t rx_desc_l4_len_err_cnt;
        uint64_t rx_desc_l4_csum_err_cnt;
        uint64_t rx_desc_l4_zero_port_num_cnt;
        uint64_t rx_desc_l4_other_cnt;
        uint64_t rx_desc_frag_cnt;
        uint64_t rx_desc_ip_ver_err_cnt;
        uint64_t rx_desc_ipv4_pkt_cnt;
        uint64_t rx_desc_ipv6_pkt_cnt;
        uint64_t rx_desc_no_ip_pkt_cnt;
        uint64_t rx_desc_ip_pkt_cnt;
        uint64_t rx_desc_tcp_pkt_cnt;
        uint64_t rx_desc_udp_pkt_cnt;
        uint64_t rx_desc_vlan_pkt_cnt;
        uint64_t rx_desc_icmp_pkt_cnt;
        uint64_t rx_desc_arp_pkt_cnt;
        uint64_t rx_desc_rarp_pkt_cnt;
        uint64_t rx_desc_multicast_pkt_cnt;
        uint64_t rx_desc_broadcast_pkt_cnt;
        uint64_t rx_desc_ipsec_pkt_cnt;
        uint64_t rx_desc_ip_opt_pkt_cnt;
        uint64_t rx_desc_key_not_match_cnt;
        uint64_t rx_octets_total_ok_cnt;
        uint64_t rx_uc_pkt_cnt;
        uint64_t rx_mc_pkt_cnt;
        uint64_t rx_bc_pkt_cnt;
        uint64_t rx_vlan_pkt_cnt;
        uint64_t rx_octets_bad_cnt;
        uint64_t rx_octets_total_filt_cnt;
        uint64_t rx_filt_pkt_cnt;
        uint64_t rx_trans_pkt_cnt;
        uint64_t rx_framesize_64;
        uint64_t rx_framesize_65_127;
        uint64_t rx_framesize_128_255;
        uint64_t rx_framesize_256_511;
        uint64_t rx_framesize_512_1023;
        uint64_t rx_framesize_1024_1518;
        uint64_t rx_framesize_bt_1518;
        uint64_t rx_fcs_error_cnt;
        uint64_t rx_data_error_cnt;
        uint64_t rx_align_error_cnt;
        uint64_t rx_pause_macctl_frame_cnt;
        uint64_t rx_unknown_macctl_frame_cnt;
        uint64_t rx_frame_long_err_cnt;
        uint64_t rx_jabber_err_cnt;
        uint64_t rx_frame_very_long_err_cnt;
        uint64_t rx_frame_runt_err_cnt;
        uint64_t rx_frame_short_err_cnt;
        uint64_t rx_overflow_cnt;
        uint64_t rx_overrun_cnt;
        uint64_t rx_bufrq_err_cnt;
        uint64_t rx_we_err_cnt;
        uint64_t rx_lengthfield_err_cnt;
        uint64_t rx_fail_comma_cnt;
        uint64_t rx_dma_err_cnt;
        uint64_t rx_fifo_less_empty_thrsld_cnt;
        uint64_t tx_octets_total_ok_cnt;
        uint64_t tx_uc_pkt_cnt;
        uint64_t tx_mc_pkt_cnt;
        uint64_t tx_bc_pkt_cnt;
        uint64_t tx_vlan_pkt_cnt;
        uint64_t tx_octets_bad_cnt;
        uint64_t tx_trans_pkt_cnt;
        uint64_t tx_pause_frame_cnt;
        uint64_t tx_framesize_64;
        uint64_t tx_framesize_65_127;
        uint64_t tx_framesize_128_255;
        uint64_t tx_framesize_256_511;
        uint64_t tx_framesize_512_1023;
        uint64_t tx_framesize_1024_1518;
        uint64_t tx_framesize_bt_1518;
        uint64_t tx_underrun_err_cnt;
        uint64_t tx_add_cs_fail_cnt;
        uint64_t tx_bufrl_err_cnt;
        uint64_t tx_crc_err_cnt;
        uint64_t tx_drop_cnt;
        uint64_t tx_excessive_length_drop_cnt;
        uint64_t tx_timeout_cnt;
        uint64_t tx_dma_err_cnt;
        uint64_t np_link_fail_cnt;
        uint64_t reset_fail_cnt;
    } stats;

    struct hbg_dev_specs {
        uint32_t mac_id;
        uint32_t phy_addr;
        uint32_t mdio_frequency;
        uint32_t rx_fifo_num;
        uint32_t tx_fifo_num;
        uint32_t vlan_layers;
        uint32_t max_mtu;
        uint32_t min_mtu;
        uint32_t uc_mac_num;
        uint32_t max_frame_len;
        uint32_t rx_buf_size;
    } dev_specs;
};

enum hbg_reset_type {
    HBG_RESET_TYPE_NONE = 0,
    HBG_RESET_TYPE_FLR,
    HBG_RESET_TYPE_FUNCTION,
};

enum hbg_hw_event_type {
    HBG_HW_EVENT_NONE = 0,
    HBG_HW_EVENT_INIT,
    HBG_HW_EVENT_RESET,
    HBG_HW_EVENT_CORE_RESET,
};

enum hbg_dir {
    HBG_DIR_TX = 1 << 0,
    HBG_DIR_RX = 1 << 1,
    HBG_DIR_TX_RX = HBG_DIR_TX | HBG_DIR_RX,
};

struct hbg_tx_desc {
    uint32_t word0;
    uint32_t word1;
    uint32_t word2;
    uint32_t word3;
};

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr < sizeof(s->regs)) {
        val = s->regs[addr >> 2];
        if (size == 8 && (addr + 4) < sizeof(s->regs)) {
            val |= ((uint64_t)s->regs[(addr >> 2) + 1]) << 32;
        }
    }
    
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < sizeof(s->regs)) {
        s->regs[addr >> 2] = (uint32_t)val;
        if (size == 8 && (addr + 4) < sizeof(s->regs)) {
            s->regs[(addr >> 2) + 1] = (uint32_t)(val >> 32);
        }
        
        if (addr <= HBG_REG_EVENT_REQ_ADDR && addr + size > HBG_REG_EVENT_REQ_ADDR) {
            /* Auto-clear event request to simulate immediate completion */
            s->regs[HBG_REG_EVENT_REQ_ADDR >> 2] = 0;
        }
        if (addr <= HBG_REG_MDIO_COMMAND_ADDR && addr + size > HBG_REG_MDIO_COMMAND_ADDR) {
            /* Auto-clear MDIO command to simulate immediate completion */
            s->regs[HBG_REG_MDIO_COMMAND_ADDR >> 2] = 0;
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

    memset(s->regs, 0, sizeof(s->regs));

    /* Initialize registers for hbg_hw_spec_is_valid */
    s->regs[HBG_REG_SPEC_VALID_ADDR >> 2] = 1;
    s->regs[HBG_REG_EVENT_REQ_ADDR >> 2] = 0;

    /* Initialize registers for hbg_hw_dev_specs_init */
    s->regs[HBG_REG_MAC_ID_ADDR >> 2] = 0x0;
    s->regs[HBG_REG_PHY_ID_ADDR >> 2] = 0x1;
    s->regs[HBG_REG_MDIO_FREQ_ADDR >> 2] = 0x0;
    s->regs[HBG_REG_MAX_MTU_ADDR >> 2] = 9728;
    s->regs[HBG_REG_MIN_MTU_ADDR >> 2] = 68;
    s->regs[HBG_REG_VLAN_LAYERS_ADDR >> 2] = 2;
    s->regs[HBG_REG_RX_FIFO_NUM_ADDR >> 2] = 4096;
    s->regs[HBG_REG_TX_FIFO_NUM_ADDR >> 2] = 4096;
    s->regs[HBG_REG_UC_MAC_NUM_ADDR >> 2] = 1;
    
    /* 64-bit MAC address (e.g., 12:34:56:78:9A:BC) */
    s->regs[HBG_REG_MAC_ADDR_ADDR >> 2] = 0x789ABC;
    s->regs[(HBG_REG_MAC_ADDR_ADDR >> 2) + 1] = 0x123456;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x19e5 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x3730 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_ETHERNET );
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
    s->bar_info[0].size = 0x20000;
    s->bar_info[0].name = "bar0";
      
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Interrupt Initialization */
    if (msix_init_exclusive_bar(pdev, HBG_VECTOR_NUM, 1, errp) < 0) {
        msi_init(pdev, 0, HBG_VECTOR_NUM, true, false, errp);
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit_exclusive_bar(pdev);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "hibmcge_pci",
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
