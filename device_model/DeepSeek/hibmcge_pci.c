/*
 * QEMU PCI device model for HiSilicon HiBMCGE network controller
 * Based on Linux driver: drivers/net/ethernet/hisilicon/hibmcge/hbg_main.c
 * Phase 2: Full behavioral implementation for probing and initialization.
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

#define TYPE_PCIBASE_DEVICE "hibmcge_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x19e5
#define DEVICE_ID 0x3730
#define CLASS_ID 0x0200

#define HBG_SUPPORT_FEATURES (NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM | \
			     NETIF_F_RXCSUM)
#define HBG_STATUS_ENABLE		0x1
#define HBG_STATUS_DISABLE		0x0
#define HBG_TX_TIMEOUT_BUF_LEN		1024
#define HBG_INT_MSK_TX_B			BIT(1)
#define HBG_REG_CF_IND_RXINT_MSK_ADDR		(HBG_REG_SGMII_BASE + 0x06a0)
#define HBG_REG_CF_INTRPT_MSK_ADDR		(HBG_REG_SGMII_BASE + 0x042C)
#define HBG_INT_MSK_RX_B			BIT(0)
#define HBG_REG_CF_IND_TXINT_MSK_ADDR		(HBG_REG_SGMII_BASE + 0x0694)
#define HBG_REG_PORT_ENABLE_ADDR		(HBG_REG_SGMII_BASE + 0x0044)
#define HBG_REG_PORT_ENABLE_RX_B		BIT(1)
#define HBG_REG_PORT_ENABLE_TX_B		BIT(2)
#define HBG_REG_EVENT_REQ_ADDR			0x0004
#define HBG_REG_STATION_ADDR_LOW_MSK_1		(HBG_REG_SGMII_BASE + 0x0238)
#define HBG_REG_REC_FILT_CTRL_ADDR		(HBG_REG_SGMII_BASE + 0x0064)
#define HBG_REG_STATION_ADDR_LOW_MSK_0		(HBG_REG_SGMII_BASE + 0x0230)
#define HBG_REG_REC_FILT_CTRL_UC_MATCH_EN_B	BIT(0)
#define HBG_REG_STATION_ADDR_LOW_2_ADDR		(HBG_REG_SGMII_BASE + 0x0210)
#define HBG_REG_FD_FC_ADDR_LOW_ADDR		(HBG_REG_SGMII_BASE + 0x0020)
#define HBG_REG_BRUST_LENGTH_B			BIT(29)
#define HBG_REG_BRUST_LENGTH_ADDR		(HBG_REG_SGMII_BASE + 0x04C4)
#define HBG_REG_CF_CFF_DATA_NUM_ADDR_RX_M	GENMASK(24, 16)
#define HBG_REG_CF_CFF_DATA_NUM_ADDR		(HBG_REG_SGMII_BASE + 0x045C)
#define HBG_REG_CF_CFF_DATA_NUM_ADDR_TX_M	GENMASK(8, 0)
#define HBG_REG_PUSH_REQ_ADDR			0x00F0
#define HBG_REG_PAUSE_ENABLE_RX_B		BIT(0)
#define HBG_REG_PAUSE_ENABLE_TX_B		BIT(1)
#define HBG_REG_PAUSE_ENABLE_ADDR		(HBG_REG_SGMII_BASE + 0x0048)
#define HBG_REG_MODE_CHANGE_EN_ADDR		(HBG_REG_SGMII_BASE + 0x01B4)
#define HBG_REG_MODE_CHANGE_EN_B		BIT(0)
#define HBG_REG_BUS_CTRL_ENDIAN_M		GENMASK(2, 1)
#define HBG_REG_BUS_CTRL_ADDR			(HBG_REG_SGMII_BASE + 0x04E8)
#define HBG_NO_PHY			0xFF
#define HBG_VECTOR_NUM			4
#define HBG_REG_SGMII_BASE			0x10000
#define HBG_REG_MAX_FRAME_LEN_M			GENMASK(15, 0)
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
#define HBG_REG_AN_NEG_STATE_NP_LINK_OK_B	BIT(15)
#define HBG_REG_AN_NEG_STATE_ADDR		(HBG_REG_SGMII_BASE + 0x0058)
#define HBG_REG_MAC_ADDR_ADDR			0x0010
#define HBG_PCU_CACHE_LINE_SIZE			32
#define HBG_REG_TX_FIFO_NUM_ADDR		0x0030
#define HBG_REG_MIN_MTU_ADDR			0x002C
#define HBG_REG_VLAN_LAYERS_ADDR		0x0038
#define HBG_REG_MAX_MTU_ADDR			0x0028
#define HBG_REG_RX_FIFO_NUM_ADDR		0x0034
#define HBG_REG_PHY_ID_ADDR			0x000C
#define HBG_REG_UC_MAC_NUM_ADDR			0x0018
#define HBG_REG_MDIO_FREQ_ADDR			0x0024
#define HBG_REG_MAC_ID_ADDR			0x0008
#define HBG_REG_CFG_FIFO_THRSLD_RX_FULL_M	GENMASK(15, 8)
#define HBG_REG_CFG_FIFO_THRSLD_TX_EMPTY_M	GENMASK(23, 16)
#define HBG_REG_CFG_FIFO_THRSLD_ADDR		(HBG_REG_SGMII_BASE + 0x0428)
#define HBG_REG_CFG_FIFO_THRSLD_TX_FULL_M	GENMASK(31, 24)
#define HBG_REG_CFG_FIFO_THRSLD_RX_EMPTY_M	GENMASK(7, 0)
#define HBG_REG_RECV_CTRL_STRIP_PAD_EN_B	BIT(3)
#define HBG_REG_RX_BUF_SIZE_M			GENMASK(15, 0)
#define HBG_REG_RX_BUF_SIZE_ADDR		(HBG_REG_SGMII_BASE + 0x04E4)
#define HBG_REG_CF_CRC_STRIP_ADDR		(HBG_REG_SGMII_BASE + 0x01B0)
#define HBG_REG_RX_PKT_MODE_PARSE_MODE_M	GENMASK(22, 21)
#define HBG_REG_RX_PKT_MODE_ADDR		(HBG_REG_SGMII_BASE + 0x04F4)
#define HBG_REG_RECV_CTRL_ADDR			(HBG_REG_SGMII_BASE + 0x01E0)
#define HBG_REG_CF_CRC_STRIP_B			BIT(0)
#define HBG_REG_TRANSMIT_CTRL_CRC_ADD_B		BIT(6)
#define HBG_REG_TRANSMIT_CTRL_PAD_EN_B		BIT(7)
#define HBG_REG_TRANSMIT_CTRL_ADDR		(HBG_REG_SGMII_BASE + 0x0060)
#define HBG_REG_TRANSMIT_CTRL_AN_EN_B		BIT(5)
#define HBG_REG_FIFO_THRSLD_EMPTY_M		GENMASK(9, 0)
#define HBG_REG_TX_FIFO_THRSLD_ADDR		(HBG_REG_SGMII_BASE + 0x0420)
#define HBG_REG_FIFO_THRSLD_FULL_M		GENMASK(25, 16)
#define HBG_REG_RX_FIFO_THRSLD_ADDR		(HBG_REG_SGMII_BASE + 0x0424)
#define HBG_REG_MDIO_COMMAND_CLK_SEL_B		BIT(15)
#define HBG_REG_MDIO_COMMAND_CLK_SEL_EXP_B	BIT(17)
#define HBG_REG_MDIO_COMMAND_AUTO_SCAN_B	BIT(16)
#define HBG_REG_MDIO_COMMAND_ST_M		GENMASK(13, 12)
#define HBG_TX_DESC_W1_SEND_LEN_M	GENMASK(19, 4)
#define HBG_TX_DESC_W0_IP_OFF_M		GENMASK(30, 26)
#define HBG_TX_DESC_W0_WB_B		BIT(1)
#define HBG_REG_TX_CFF_ADDR_2_ADDR		(HBG_REG_SGMII_BASE + 0x0490)
#define HBG_REG_TX_CFF_ADDR_3_ADDR		(HBG_REG_SGMII_BASE + 0x0494)
#define HBG_REG_TX_CFF_ADDR_0_ADDR		(HBG_REG_SGMII_BASE + 0x0488)
#define HBG_REG_TX_CFF_ADDR_1_ADDR		(HBG_REG_SGMII_BASE + 0x048C)
#define HBG_REG_REC_FILT_CTRL_PAUSE_FRM_PASS_B	BIT(4)
#define HBG_REG_SPEC_VALID_ADDR			0x0000
#define HBG_REG_RX_CTRL_PORT_NUM		GENMASK(16, 13)
#define HBG_REG_RX_CTRL_RX_GET_ADDR_MODE_B	BIT(12)
#define HBG_REG_RX_CTRL_TIME_INF_EN_B		BIT(23)
#define HBG_REG_RX_CTRL_RXBUF_1ST_SKIP_SIZE2_M	GENMASK(3, 0)
#define HBG_REG_RX_CTRL_RX_ALIGN_NUM_M		GENMASK(18, 17)
#define HBG_REG_RX_CTRL_ADDR			(HBG_REG_SGMII_BASE + 0x04F0)
#define HBG_REG_RX_CTRL_RXBUF_1ST_SKIP_SIZE_M	GENMASK(31, 28)
#define HBG_REG_MDIO_WDATA_M			GENMASK(15, 0)
#define HBG_REG_MDIO_WDATA_ADDR			(HBG_REG_MDIO_BASE + 0x0008)
#define HBG_REG_MDIO_COMMAND_PRTAD_M		GENMASK(9, 5)
#define HBG_REG_MDIO_COMMAND_DEVAD_M		GENMASK(4, 0)
#define HBG_REG_MDIO_COMMAND_START_B		BIT(14)
#define HBG_REG_MDIO_COMMAND_OP_M		GENMASK(11, 10)
#define HBG_REG_MDIO_RDATA_ADDR			(HBG_REG_MDIO_BASE + 0x000C)
#define HBG_REG_MDIO_COMMAND_ADDR		(HBG_REG_MDIO_BASE + 0x0000)
#define HBG_REG_MSG_HEADER_ADDR			0x00F4
#define HBG_REG_MSG_HEADER_RESP_CODE_M		GENMASK(27, 20)
#define HBG_REG_MSG_DATA_BASE_ADDR		0x0100
#define HBG_REG_MSG_HEADER_DATA_NUM_M		GENMASK(19, 12)
#define HBG_REG_MSG_HEADER_STATUS_M		GENMASK(11, 8)
#define HBG_REG_MSG_HEADER_OPCODE_M		GENMASK(7, 0)
#define HBG_REG_MDIO_BASE			0x8000
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

    /* Hardware Register Shadows */
    uint8_t *mmio;
    MemoryRegion msix_mr;
    uint16_t phy_regs[32];

    uint32_t status;
    enum hbg_reset_type reset_type;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t mask = ldl_le_p(&s->mmio[HBG_REG_CF_INTRPT_MSK_ADDR]);
    uint32_t stat = ldl_le_p(&s->mmio[HBG_REG_CF_INTRPT_STAT_ADDR]);

    if (mask & stat) {
        if (msix_enabled(pdev)) {
            msix_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!msix_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

/* Device-initiated DMA logic not used by driver – placeholder removed */

static void handle_mdio_command(PCIBaseState *s, uint32_t val)
{
    if (!(val & BIT(14))) {
        return;
    }

    uint32_t op = (val >> 10) & 3;
    uint32_t phy_addr = (val >> 5) & 0x1f;
    uint32_t reg_addr = val & 0x1f;

    if (op == 2) { /* Read */
        uint16_t rdata = 0xffff;
        if (phy_addr == 0 && reg_addr < 32) {
            rdata = s->phy_regs[reg_addr];
        }
        stw_le_p(&s->mmio[HBG_REG_MDIO_RDATA_ADDR], rdata);
        /* Update status: operation completed */
        stl_le_p(&s->mmio[HBG_REG_MDIO_STA_ADDR], 0x00000001);
    } else if (op == 1) { /* Write */
        uint16_t wdata = lduw_le_p(&s->mmio[HBG_REG_MDIO_WDATA_ADDR]);
        if (phy_addr == 0 && reg_addr < 32) {
            s->phy_regs[reg_addr] = wdata;
        }
        stl_le_p(&s->mmio[HBG_REG_MDIO_STA_ADDR], 0x00000001);
    }
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* Bounds check */
    if (addr + size > 0x20000) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds write at 0x%" HWADDR_PRIx " size %u\n",
                      __func__, addr, size);
        return;
    }

    /* Write value to flat MMIO buffer (little-endian) */
    switch (size) {
    case 1:
        s->mmio[addr] = val;
        break;
    case 2:
        stw_le_p(s->mmio + addr, val);
        break;
    case 4:
        stl_le_p(s->mmio + addr, val);
        break;
    case 8:
        stq_le_p(s->mmio + addr, val);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unhandled write size %u at 0x%" HWADDR_PRIx "\n",
                      __func__, size, addr);
        return;
    }

    /* Handle side effects */
    switch (addr) {
    case HBG_REG_CF_INTRPT_CLR_ADDR:
        /* Write-1-to-clear: clear only bits that were set */
        stl_le_p(&s->mmio[HBG_REG_CF_INTRPT_STAT_ADDR],
                 ldl_le_p(&s->mmio[HBG_REG_CF_INTRPT_STAT_ADDR]) & ~val);
        break;
    case HBG_REG_CF_IND_TXINT_CLR_ADDR:
        stl_le_p(&s->mmio[HBG_REG_CF_IND_TXINT_STAT_ADDR],
                 ldl_le_p(&s->mmio[HBG_REG_CF_IND_TXINT_STAT_ADDR]) & ~val);
        break;
    case HBG_REG_CF_IND_RXINT_CLR_ADDR:
        stl_le_p(&s->mmio[HBG_REG_CF_IND_RXINT_STAT_ADDR],
                 ldl_le_p(&s->mmio[HBG_REG_CF_IND_RXINT_STAT_ADDR]) & ~val);
        break;
    case HBG_REG_MDIO_COMMAND_ADDR:
        handle_mdio_command(s, val);
        break;
    case HBG_REG_EVENT_REQ_ADDR:
        if (val == HBG_HW_EVENT_RESET || val == HBG_HW_EVENT_CORE_RESET) {
            /* Soft reset: clear all MMIO and re-apply default values */
            memset(s->mmio, 0, 0x20000);
            /* Re-apply essential default register values */
            stl_le_p(&s->mmio[HBG_REG_SPEC_VALID_ADDR], 0x00000001);
            stl_le_p(&s->mmio[HBG_REG_PHY_ID_ADDR], 0x01410e9b);
            stl_le_p(&s->mmio[HBG_REG_UC_MAC_NUM_ADDR], 8);
            stl_le_p(&s->mmio[HBG_REG_TX_FIFO_NUM_ADDR], 256);
            stl_le_p(&s->mmio[HBG_REG_RX_FIFO_NUM_ADDR], 512);
            stl_le_p(&s->mmio[HBG_REG_MAX_MTU_ADDR], 9000);
            stl_le_p(&s->mmio[HBG_REG_MIN_MTU_ADDR], 60);
            stl_le_p(&s->mmio[HBG_REG_VLAN_LAYERS_ADDR], 0);
            stl_le_p(&s->mmio[HBG_REG_MDIO_FREQ_ADDR], 0);
        }
        break;
    default:
        break;
    }

    /* Update IRQ if any interrupt-related register was modified */
    if (addr == HBG_REG_CF_INTRPT_MSK_ADDR ||
        addr == HBG_REG_CF_INTRPT_CLR_ADDR ||
        addr == HBG_REG_CF_INTRPT_STAT_ADDR ||
        addr == HBG_REG_CF_IND_TXINT_MSK_ADDR ||
        addr == HBG_REG_CF_IND_TXINT_CLR_ADDR ||
        addr == HBG_REG_CF_IND_TXINT_STAT_ADDR ||
        addr == HBG_REG_CF_IND_RXINT_MSK_ADDR ||
        addr == HBG_REG_CF_IND_RXINT_CLR_ADDR ||
        addr == HBG_REG_CF_IND_RXINT_STAT_ADDR) {
        pcibase_update_irq(s);
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr + size > 0x20000) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds read at 0x%" HWADDR_PRIx " size %u\n",
                      __func__, addr, size);
        return ~0ULL;
    }

    uint64_t val = 0;
    switch (size) {
    case 1:
        val = s->mmio[addr];
        break;
    case 2:
        val = lduw_le_p(s->mmio + addr);
        break;
    case 4:
        val = ldl_le_p(s->mmio + addr);
        break;
    case 8:
        val = ldq_le_p(s->mmio + addr);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unhandled read size %u at 0x%" HWADDR_PRIx "\n",
                      __func__, size, addr);
        return ~0ULL;
    }
    return val;
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* PIO not used by driver */
    return ~0ULL;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* PIO not used by driver */
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

    /* Clear all MMIO and set hardware-defined defaults */
    memset(s->mmio, 0, 0x20000);

    /* Essential driver-discovery registers */
    stl_le_p(&s->mmio[HBG_REG_SPEC_VALID_ADDR], 0x00000001);
    stl_le_p(&s->mmio[HBG_REG_PHY_ID_ADDR], 0x01410e9b);
    stl_le_p(&s->mmio[HBG_REG_UC_MAC_NUM_ADDR], 8);
    stl_le_p(&s->mmio[HBG_REG_TX_FIFO_NUM_ADDR], 256);
    stl_le_p(&s->mmio[HBG_REG_RX_FIFO_NUM_ADDR], 512);
    stl_le_p(&s->mmio[HBG_REG_MAX_MTU_ADDR], 9000);
    stl_le_p(&s->mmio[HBG_REG_MIN_MTU_ADDR], 60);
    stl_le_p(&s->mmio[HBG_REG_VLAN_LAYERS_ADDR], 0);
    stl_le_p(&s->mmio[HBG_REG_MDIO_FREQ_ADDR], 0);

    /* Initialize PHY emulation */
    memset(s->phy_regs, 0, sizeof(s->phy_regs));
    s->phy_regs[0] = 0x3100;  /* Basic Control: default */
    s->phy_regs[1] = 0x782d;  /* Basic Status: link up, 10/100/1000, auto-neg complete */
    s->phy_regs[2] = 0x0141;  /* PHY ID 1 */
    s->phy_regs[3] = 0x0e9b;  /* PHY ID 2 */
    s->phy_regs[4] = 0x01e1;  /* Auto-Neg Advertisement */
    s->phy_regs[5] = 0xc5e1;  /* Link Partner Ability */
    s->phy_regs[6] = 0x0007;  /* Auto-Neg Expansion */
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* Allocate flat MMIO buffer */
    s->mmio = g_malloc0(0x20000);

    /* BAR 0: MMIO region */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x20000;
    s->bar_info[0].name = "hibmcge_mmio";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI-X initialization: 4 vectors, table and PBA within BAR0 */
    int msix_cap = pci_add_capability(pdev, PCI_CAP_ID_MSIX, 0, MSIX_CAP_LENGTH, errp);
    if (msix_init(pdev, HBG_VECTOR_NUM, &s->bar_regions[0], 0, 0x11000,
                  &s->bar_regions[0], 0, 0x12000, msix_cap, errp) != 0) {
        error_propagate(errp, NULL);
        return;
    }

    /* Perform initial reset to populate default register values */
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

    g_free(s->mmio);
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
