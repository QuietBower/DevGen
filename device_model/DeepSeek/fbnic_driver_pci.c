/* QEMU 8.2.10 PCI Device Model for fbnic driver - Phase 2 */
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
#include "hw/pci/pci_ids.h"

#define TYPE_PCIBASE_DEVICE "fbnic_driver_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define FBNIC_VENDOR_ID  0x0000  /* TODO - actual VID needed */
#define FBNIC_DEVICE_ID  0x0000  /* TODO - actual DID needed */
#define FBNIC_CLASS_ID   PCI_CLASS_NETWORK_ETHERNET

/* Common bit manipulation macros (QEMU-compatible) */
#define CSR_BIT(nr)          (1u << (nr))
#define CSR_GENMASK(h, l)    (((1u << ((h) - (l) + 1)) - 1) << (l))
#define DESC_BIT(nr)         BIT_ULL(nr)
#define DESC_GENMASK(h, l)   GENMASK_ULL(h, l)
#define BIT_ULL(nr)          (1ULL << (nr))
#define GENMASK_ULL(h, l)    (((1ULL << ((h) - (l) + 1)) - 1) << (l))

/* Driver register offset defines (word offsets) */
#define FBNIC_MASTER_SPARE_0		0x0C41B
#define FBNIC_FW_ZERO_REG	FBNIC_IPC_MBX(0, 0)
#define FBNIC_MAC_PS_TO_DEFAULT_MS	500
#define FBNIC_MAX_QUEUES			128
#define FBNIC_MBX_CMPL_SLOTS		4
#define FBNIC_RPC_TCAM_MACDA_NUM_ENTRIES	32
#define FBNIC_MAX_NAPI_VECTORS		128u
#define FBNIC_RPC_TCAM_IP_ADDR_NUM_ENTRIES	8
#define FBNIC_RPC_TCAM_ACT_NUM_ENTRIES		64
#define FBNIC_IPC_MBX(mbx_idx, desc_idx)	\
	((((mbx_idx) * FBNIC_IPC_MBX_DESC_LEN + (desc_idx)) * 2) + 0x6000)
#define FBNIC_MAX_TXQS				128u
#define FBNIC_RPC_RSS_TBL_COUNT			2
#define FBNIC_MAX_XDPQS			128u
#define FBNIC_RPC_RSS_KEY_DWORD_LEN \
	DIV_ROUND_UP(FBNIC_RPC_RSS_KEY_BIT_LEN, 32)
#define FBNIC_RPC_RSS_TBL_SIZE			256
#define FBNIC_MAX_RXQS			128u
#define FBNIC_INTR_SET(n)		(0x00010 + (n))
#define FBNIC_MAX_MSIX_VECS		256U
#define FBNIC_RPC_RSS_TBL(n, m) \
	(0x08d20 + 0x100 * (n) + (m))
#define FBNIC_RPC_RMI_CONFIG		0x08400
#define FBNIC_RPC_RMI_CONFIG_OH_BYTES		CSR_GENMASK(4, 0)
#define FBNIC_RPC_ACT_TBL1_DEFAULT	0x0840b
#define FBNIC_RPC_ACT_TBL0_DEFAULT	0x0840a
#define FBNIC_RPC_RMI_CONFIG_MTU		CSR_GENMASK(31, 16)
#define FBNIC_RPC_ACT_TBL0_DROP			CSR_BIT(0)
#define FBNIC_RPC_RSS_KEY(n)		(0x0840c + (n))
#define FBNIC_RPC_RMI_CONFIG_ENABLE		CSR_BIT(12)
#define FBNIC_MAX_JUMBO_FRAME_SIZE	9742
#define FBNIC_RPC_ACT_TBL0(n)		(0x08800 + (n))
#define FBNIC_RPC_ACT_TBL1(n)		(0x08840 + (n))
#define FBNIC_RPC_ACT_TBL0_DEST_MASK		CSR_GENMASK(3, 1)
#define FBNIC_MAC_ADDR_T_HOST_START	FBNIC_MAC_ADDR_T_BROADCAST
#define FBNIC_MAC_ADDR_T_HOST_LEN \
	(FBNIC_MAC_ADDR_T_HOST_LAST - FBNIC_MAC_ADDR_T_HOST_START)
#define FBNIC_QM_TCQ_IDLE(n)		(0x00821 + (n))
#define FBNIC_QM_TWQ_IDLE(n)		(0x00800 + (n))
#define FBNIC_QM_TQS_IDLE_CNT			8
#define FBNIC_QM_TDE_IDLE(n)		(0x00853 + (n))
#define FBNIC_QM_TCQ_IDLE_CNT			4
#define FBNIC_QM_HPQ_IDLE(n)		(0x00c0f + (n))
#define FBNIC_QM_RCQ_IDLE(n)		(0x00c00 + (n))
#define FBNIC_QM_TWQ_IDLE_CNT			8
#define FBNIC_QM_TQS_IDLE(n)		(0x00830 + (n))
#define FBNIC_QM_HPQ_IDLE_CNT			4
#define FBNIC_QM_PPQ_IDLE_CNT			4
#define FBNIC_QM_PPQ_IDLE(n)		(0x00c13 + (n))
#define FBNIC_QM_TDE_IDLE_CNT			8
#define FBNIC_QM_RCQ_IDLE_CNT			4
#define FBNIC_IPC_MBX_DESC_LEN	16
#define FBNIC_FW_LOG_MAX_SIZE			256
#define FBNIC_MAC_COMMAND_CONFIG	0x11002
#define FBNIC_MAC_COMMAND_CONFIG_TX_PAUSE_DIS	CSR_BIT(28)
#define FBNIC_RXB_ERR_INTR_STS_PS		CSR_GENMASK(15, 12)
#define FBNIC_RXB_ERR_INTR_STS		0x08050
#define FBNIC_INTR_MASK_CLEAR(n)	(0x00038 + (n))
#define FW_HEARTBEAT_PERIOD		(10 * HZ)
#define MIN_FW_VER_CODE_HIST		FW_VER_CODE(25, 5, 7, 0)
#define FBNIC_RPC_TCAM_MACDA_DEFAULT_BOUNDARY	24
#define FBNIC_HDS_THRESH_DEFAULT \
	(1536 - FBNIC_RX_PAD)
#define FBNIC_RX_USECS_DEFAULT		30
#define FBNIC_TX_USECS_DEFAULT		35
#define FBNIC_PPQ_SIZE_DEFAULT		256
#define FBNIC_HPQ_SIZE_DEFAULT		256
#define FBNIC_TXQ_SIZE_DEFAULT		1024
#define FBNIC_RX_FRAMES_DEFAULT		0
#define FBNIC_RCQ_SIZE_DEFAULT		1024
#define FBNIC_FW_LOG_SIZE	(512 * 1024)
#define FBNIC_RPC_TCAM_MACDA_WORD_LEN		3
#define FBNIC_RPC_TCAM_ACT_WORD_LEN		11
#define FBNIC_RPC_RSS_KEY_BIT_LEN		425
#define FBNIC_QUEUE_STRIDE		0x400
#define FBNIC_QUEUE(n)\
	(0x40000 + FBNIC_QUEUE_STRIDE * (n))
#define FBNIC_INTR_CQ_REARM_INTR_UNMASK		CSR_BIT(31)
#define FBNIC_INTR_MASK_SET(n)		(0x00030 + (n))
#define FBNIC_RING_F_DISABLED		BIT(0)
#define FBNIC_TMI_DROP_CTRL		0x04401
#define FBNIC_TMI_DROP_CTRL_EN			CSR_BIT(0)
#define FBNIC_NS_OTP_STATUS		0x0021d
#define FBNIC_RXB_PAUSE_STORM_THLD_TIME		CSR_GENMASK(19, 0)
#define FBNIC_RXB_PAUSE_STORM_FORCE_NORMAL	CSR_BIT(20)
#define FBNIC_RXB_PAUSE_STORM_UNIT_WR	0x0801d
#define FBNIC_RXB_PAUSE_STORM(n)	(0x08019 + (n))
#define FBNIC_MAC_RXB_PS_TO(ms)		((ms) * 100)
#define FBNIC_RXB_PS_CLK_DIV		0x1770
#define FW_RPC_MAC_SYNC_MC_ARRAY_SIZE		8
#define FW_RPC_MAC_SYNC_RX_FLAGS_BROADCAST	4
#define FBNIC_RPC_TCAM_MACDA_BROADCAST_IDX	4
#define FW_RPC_MAC_SYNC_UC_ARRAY_SIZE		8
#define FW_RPC_MAC_SYNC_RX_FLAGS_PROMISC	1
#define FBNIC_RPC_TCAM_MACDA_PROMISC_IDX	31
#define FW_RPC_MAC_SYNC_RX_FLAGS_ALLMULTI	2
#define FBNIC_RPC_TCAM_ACT1_L2_MACDA_VALID	CSR_BIT(10)
#define FBNIC_RPC_TCAM_ACT1_L2_MACDA_IDX	CSR_GENMASK(9, 5)
#define FBNIC_RPC_TCAM_MACDA_BMC_ADDR_IDX	0
#define FBNIC_RPC_ACT_TBL_BMC_OFFSET		0
#define FBNIC_PTP_ADJUST		0x04801
#define FBNIC_PTP_CTRL_TQS_OUT_EN		CSR_BIT(8)
#define FBNIC_PTP_CTRL_EN			CSR_BIT(0)
#define FBNIC_PTP_CTRL			0x04800
#define FBNIC_PTP_CTRL_TICK_IVAL		CSR_GENMASK(23, 20)
#define FBNIC_PTP_CTRL_MAC_OUT_IVAL		CSR_GENMASK(16, 12)
#define FBNIC_PTP_ADJUST_INIT			CSR_BIT(0)
#define FBNIC_PTP_SPARE			0x0480d
#define FBNIC_PTP_INIT_LO		0x04803
#define FBNIC_PTP_INIT_HI		0x04802
#define FBNIC_CLOCK_FREQ	(600 * (1000 * 1000))
#define MIN_FW_VER_CODE_LOG			FW_VER_CODE(0, 12, 9, 0)
#define FBNIC_FW_LOG_VERSION			1
#define FW_VER_CODE(_major, _minor, _patch, _build) (		      \
		FIELD_PREP(FBNIC_FW_CAP_RESP_VERSION_MAJOR, _major) | \
		FIELD_PREP(FBNIC_FW_CAP_RESP_VERSION_MINOR, _minor) | \
		FIELD_PREP(FBNIC_FW_CAP_RESP_VERSION_PATCH, _patch) | \
		FIELD_PREP(FBNIC_FW_CAP_RESP_VERSION_BUILD, _build))
#define FBNIC_RX_PAD			0
#define FBNIC_RPC_RSS_KEY_LAST_MASK \
	CSR_GENMASK(31, \
		    FBNIC_RPC_RSS_KEY_DWORD_LEN * 32 - \
		    FBNIC_RPC_RSS_KEY_BIT_LEN)
#define FBNIC_RPC_RSS_KEY_LAST_IDX \
	(FBNIC_RPC_RSS_KEY_DWORD_LEN - 1)
#define FBNIC_TMI_BAD_PTP_TS		0x0440b
#define FBNIC_TMI_DROP_BYTE_L		0x04403
#define FBNIC_TMI_ILLEGAL_PTP_REQS	0x04409
#define FBNIC_TMI_DROP_PKTS		0x04402
#define FBNIC_TMI_GOOD_PTP_TS		0x0440a
#define FBNIC_QUEUE_RDE_CQ_DROP_CNT	0x2a4
#define FBNIC_QUEUE_RDE_BDQ_DROP_CNT	0x2a5
#define FBNIC_QUEUE_RDE_PKT_ERR_CNT	0x2a3
#define FBNIC_TCE_TBI_DROP_PKTS		0x04044
#define FBNIC_TCE_TTI_CM_DROP_PKTS	0x0403e
#define FBNIC_TCE_TTI_FRAME_DROP_BYTE_L	0x04042
#define FBNIC_TCE_TTI_FRAME_DROP_PKTS	0x04041
#define FBNIC_TCE_TTI_CM_DROP_BYTE_L	0x0403f
#define FBNIC_TCE_TBI_DROP_BYTE_L	0x04045
#define FBNIC_PUL_USER_OB_CPL_TLP_CNT_31_0 \
					0x31076
#define FBNIC_PUL_USER_OB_RD_TLP_CNT_31_0 \
					0x3106e
#define FBNIC_PUL_USER_OB_RD_DWORD_CNT_31_0 \
					0x31070
#define FBNIC_PUL_USER_OB_CPL_DWORD_CNT_31_0 \
					0x31078
#define FBNIC_PUL_USER_OB_WR_DWORD_CNT_31_0 \
					0x31074
#define FBNIC_PUL_USER_OB_WR_TLP_CNT_31_0 \
					0x31072
#define FBNIC_PUL_USER_OB_RD_DBG_CNT_NP_CRED_31_0 \
					0x3107e
#define FBNIC_PUL_USER_OB_RD_DBG_CNT_TAG_31_0 \
					0x3107c
#define FBNIC_PUL_USER_OB_RD_DBG_CNT_CPL_CRED_31_0 \
					0x3107a
#define FBNIC_RPC_CNTR_UNKN_ETYPE	0x0849f
#define FBNIC_RPC_CNTR_OUT_OF_HDR_ERR	0x084a5
#define FBNIC_RPC_CNTR_TCP_OPT_ERR	0x0849e
#define FBNIC_RPC_CNTR_UNKN_EXT_HDR	0x084a4
#define FBNIC_RPC_CNTR_IPV4_FRAG	0x084a0
#define FBNIC_RPC_CNTR_IPV6_FRAG	0x084a1
#define FBNIC_RPC_CNTR_IPV6_ESP		0x084a3
#define FBNIC_RPC_CNTR_OVR_SIZE_ERR	0x084a6
#define FBNIC_RPC_CNTR_IPV4_ESP		0x084a2
#define FBNIC_PCS_MAX_LANES			4
#define FBNIC_INTR_CQ_REARM(n) \
				(0x00400 + 4 * (n))
#define FBNIC_INTR_CQ_REARM_RCQ_TIMEOUT		CSR_GENMASK(13, 0)
#define FBNIC_INTR_CQ_REARM_RCQ_TIMEOUT_UPD_EN	CSR_BIT(14)
#define FBNIC_INTR_CQ_REARM_TCQ_TIMEOUT		CSR_GENMASK(28, 15)
#define FBNIC_INTR_CQ_REARM_TCQ_TIMEOUT_UPD_EN	CSR_BIT(29)
#define FBNIC_BD_FRAG_COUNT \
	(PAGE_SIZE / FBNIC_BD_FRAG_SIZE)
#define FBNIC_RPC_TCAM_MACDA(m, n) \
	(0x08b80 + 0x20 * (n) + (m))
#define FBNIC_RPC_TCAM_ACT(m, n) \
	(0x08880 + 0x40 * (n) + (m))
#define FBNIC_RPC_TCAM_MACDA_MASK		CSR_GENMASK(31, 16)
#define FBNIC_RPC_TCAM_VALIDATE			CSR_BIT(31)
#define FBNIC_RPC_TCAM_MACDA_VALUE		CSR_GENMASK(15, 0)
#define FBNIC_TX_DESC_WAKEUP	(FBNIC_MAX_SKB_DESC * 2)
#define FBNIC_TWD_TYPE_MASK			DESC_GENMASK(47, 46)
#define FBNIC_TWD_LEN_MASK			DESC_GENMASK(63, 48)
#define FBNIC_IPC_MBX_DESC_HOST_CMPL	DESC_BIT(0)
#define FBNIC_IPC_MBX_DESC_EOM		DESC_BIT(46)
#define FBNIC_IPC_MBX_DESC_ADDR_MASK	DESC_GENMASK(45, 3)
#define FBNIC_IPC_MBX_DESC_LEN_MASK	DESC_GENMASK(63, 48)
#define FBNIC_FW_CAP_RESP_VERSION_BUILD		CSR_GENMASK(7, 0)
#define FBNIC_FW_CAP_RESP_VERSION_MAJOR		CSR_GENMASK(31, 24)
#define FBNIC_FW_CAP_RESP_VERSION_PATCH		CSR_GENMASK(15, 8)
#define FBNIC_FW_CAP_RESP_VERSION_MINOR		CSR_GENMASK(23, 16)
#define FBNIC_PCS_PAGE(n)	(0x10000 + 0x400 * (n))
#define FBNIC_AUI_MODE_R2	(FBNIC_AUI_LAUI2)
#define FBNIC_TWD_L3_IHLEN_MASK			DESC_GENMASK(23, 16)
#define FBNIC_TWD_L2_HLEN_MASK			DESC_GENMASK(5, 0)
#define FBNIC_TWD_CSUM_OFFSET_MASK		DESC_GENMASK(27, 24)
#define FBNIC_RSFEC_NCCW_LO(n)	(0x10804 + 8 * (n))
#define FBNIC_RSFEC_CCW_LO(n)	(0x10802 + 8 * (n))
#define FBNIC_SIG_MAC_IN0_RESET_TX_CLK		CSR_BIT(12)
#define FBNIC_SIG_MAC_IN0_RESET_FF_TX_CLK	CSR_BIT(14)
#define FBNIC_SIG_MAC_IN0		0x11800
#define FBNIC_SIG_MAC_IN0_RESET_FF_RX_CLK	CSR_BIT(13)
#define FBNIC_SIG_MAC_IN0_RESET_RX_CLK		CSR_BIT(11)
#define FBNIC_MAC_COMMAND_CONFIG_RX_ENA		CSR_BIT(1)
#define FBNIC_MAC_COMMAND_CONFIG_TX_ENA		CSR_BIT(0)
#define FBNIC_RXB_INTR_PS_COUNT(n)	(0x080e9 + (n))
#define FBNIC_PCS_SYMBLERR_LO(n) \
				(0x10880 + 2 * (n))
#define FBNIC_SIG_PCS_INTR_LINK_UP		CSR_BIT(0)
#define FBNIC_SIG_PCS_INTR_LINK_DOWN		CSR_BIT(1)
#define FBNIC_SIG_PCS_INTR_STS		0x11814
#define FBNIC_SIG_PCS_INTR_MASK		0x11816
#define FBNIC_SIG_PCS_OUT1		0x11809
#define FBNIC_SIG_PCS_OUT0_AMPS_LOCK		CSR_GENMASK(4, 1)
#define FBNIC_SIG_PCS_OUT1_FCFEC_LOCK		CSR_GENMASK(11, 8)
#define FBNIC_SIG_PCS_OUT0_LINK			CSR_BIT(27)
#define FBNIC_SIG_PCS_OUT0_BLOCK_LOCK		CSR_GENMASK(24, 5)
#define FBNIC_SIG_PCS_OUT0		0x11808
#define FBNIC_RXB_INTF_BYTE_CNT_DST_L(n) \
					(0x08109 + (n))
#define FBNIC_RXB_INTF_FRM_CNT_DST(n)	(0x08105 + (n))
#define FBNIC_RXB_PBUF_FRM_CNT_DST(n)	(0x08111 + (n))
#define FBNIC_RXB_PBUF_BYTE_CNT_DST_L(n) \
					(0x08115 + (n))
#define FBNIC_RXB_DROP_FRMS_STS(n)	(0x08057 + (n))
#define FBNIC_RXB_TRUN_FRMS_STS(n)	(0x08091 + (n))
#define FBNIC_RXB_TRANS_DROP_STS(n)	(0x080d9 + (n))
#define FBNIC_RXB_TRANS_ECN_STS(n)	(0x080e1 + (n))
#define FBNIC_RXB_TRUN_BYTES_STS_L(n) \
				(0x080c0 + 2 * (n))
#define FBNIC_RXB_DROP_BYTES_STS_L(n) \
				(0x08080 + 2 * (n))
#define FBNIC_RXB_DRBO_BYTE_CNT_SRC_L(n) \
					(0x080fd + (n))
#define FBNIC_RXB_PARSER_ERR(n)		(0x08137 + (n))
#define FBNIC_RXB_FRM_ERR(n)		(0x0813b + (n))
#define FBNIC_RXB_INTEGRITY_ERR(n)	(0x0812f + (n))
#define FBNIC_RXB_MAC_ERR(n)		(0x08133 + (n))
#define FBNIC_RXB_DRBO_FRM_CNT_SRC(n)	(0x080f9 + (n))
#define FBNIC_FW_VER_MAX_SIZE			32
#define FBNIC_BD_PAGE_ID_MASK \
	(FBNIC_BD_DESC_ID_MASK & ~FBNIC_BD_FRAG_ID_MASK)
#define FBNIC_BD_PAGE_ADDR_MASK \
	(FBNIC_BD_DESC_ADDR_MASK & ~FBNIC_BD_FRAG_ADDR_MASK)
#define FBNIC_BD_DESC_ID_MASK			DESC_GENMASK(63, 48)
#define FBNIC_BD_DESC_ADDR_MASK			DESC_GENMASK(45, 12)
#define FBNIC_BD_FRAG_SIZE \
	(FBNIC_BD_DESC_ADDR_MASK & ~(FBNIC_BD_DESC_ADDR_MASK - 1))
#define FBNIC_PAGECNT_BIAS_MAX	PAGE_SIZE
#define FBNIC_TWD_ADDR_MASK			DESC_GENMASK(45, 0)
#define FBNIC_MAX_SKB_DESC	(MAX_SKB_FRAGS + 10)
#define FBNIC_RPC_ACT_TBL_NUM_ENTRIES		64
#define FBNIC_RPC_ACT_TBL0_TS_ENA		CSR_BIT(28)
#define FBNIC_RPC_ACT_TBL0_DMA_HINT		CSR_GENMASK(24, 16)
#define FBNIC_RSS_EN_NUM_UNICAST FBNIC_RSS_EN_XCAST_UDP6
#define FBNIC_RPC_ACT_TBL_RSS_OFFSET \
	(FBNIC_RPC_ACT_TBL_NUM_ENTRIES - FBNIC_RSS_EN_NUM_ENTRIES)
#define FBNIC_TWD_FLAG_DEST_MAC			DESC_BIT(43)
#define FBNIC_INTR_MSIX_CTRL(n)		(0x00040 + (n))
#define FBNIC_QUEUE_SIZE_MAX		SZ_64K
#define FBNIC_HDS_THRESH_MAX \
	(4096 - FBNIC_RX_HROOM - FBNIC_RX_TROOM - FBNIC_RX_PAD)
#define FBNIC_QUEUE_RIM_THRESHOLD_RCD_MASK	CSR_GENMASK(14, 0)
#define FBNIC_MIN_RXD_PER_FRAME		2
#define FBNIC_L4_HASH_OPT FBNIC_TCP4_HASH_OPT
#define FBNIC_IP_HASH_OPT FBNIC_IPV4_HASH_OPT
#define FBNIC_TX_DESC_MIN	roundup_pow_of_two(FBNIC_TX_DESC_WAKEUP)
#define FBNIC_QUEUE_SIZE_MIN		64u
#define FBNIC_RX_DESC_MIN	roundup_pow_of_two(FBNIC_MAX_RX_PKT_DESC * 2)
#define FBNIC_RPC_RSS_KEY_BYTE_LEN \
	DIV_ROUND_UP(FBNIC_RPC_RSS_KEY_BIT_LEN, 8)
#define FBNIC_AUI_MODE_PAM4	(FBNIC_AUI_50GAUI1)
#define FBNIC_RXB_PBUF_BASE_ADDR		CSR_GENMASK(12, 0)
#define FBNIC_RXB_CT_SIZE(n)		(0x08000 + (n))
#define FBNIC_RXB_DROP_THLD_OFF			CSR_GENMASK(25, 13)
#define FBNIC_RXB_PAUSE_DROP_CTRL_DROP_ENABLE	CSR_GENMASK(7, 0)
#define FBNIC_RXB_CT_SIZE_HEADER		CSR_GENMASK(5, 0)
#define FBNIC_RXB_DWRR_RDE_WEIGHT0_EXT	0x08143
#define FBNIC_RXB_PBUF_CREDIT_MASK		CSR_GENMASK(13, 0)
#define FBNIC_RXB_PAUSE_THLD_ON			CSR_GENMASK(12, 0)
#define FBNIC_RXB_DWRR_RDE_WEIGHT1_QUANTUM4	CSR_GENMASK(7, 0)
#define FBNIC_RXB_PBUF_CREDIT(n)	(0x08047 + (n))
#define FBNIC_RXB_DROP_THLD(n)		(0x08011 + (n))
#define FBNIC_RXB_CT_SIZE_PAYLOAD		CSR_GENMASK(11, 6)
#define FBNIC_RXB_INTF_CREDIT_MASK1		CSR_GENMASK(7, 4)
#define FBNIC_RXB_PAUSE_THLD(n)		(0x08009 + (n))
#define FBNIC_RXB_PBUF_CFG(n)		(0x08027 + (n))
#define FBNIC_RXB_INTF_CREDIT		0x0804f
#define FBNIC_RXB_ECN_THLD_OFF			CSR_GENMASK(25, 13)
#define FBNIC_RXB_ECN_THLD(n)		(0x0801e + (n))
#define FBNIC_RXB_PBUF_SIZE			CSR_GENMASK(21, 13)
#define FBNIC_RXB_DWRR_RDE_WEIGHT0	0x0802f
#define FBNIC_RXB_DWRR_RDE_WEIGHT1_EXT	0x08144
#define FBNIC_RXB_ECN_THLD_ON			CSR_GENMASK(12, 0)
#define FBNIC_RXB_DWRR_RDE_WEIGHT0_QUANTUM2	CSR_GENMASK(23, 16)
#define FBNIC_RXB_PAUSE_THLD_OFF		CSR_GENMASK(25, 13)
#define FBNIC_RXB_INTF_CREDIT_MASK3		CSR_GENMASK(15, 12)
#define FBNIC_RXB_PAUSE_DROP_CTRL	0x08008
#define FBNIC_RXB_INTF_CREDIT_MASK0		CSR_GENMASK(3, 0)
#define FBNIC_RXB_DROP_THLD_ON			CSR_GENMASK(12, 0)
#define FBNIC_RXB_DWRR_RDE_WEIGHT1	0x08030
#define FBNIC_RXB_DWRR_RDE_WEIGHT0_QUANTUM0	CSR_GENMASK(7, 0)
#define FBNIC_RXB_PAUSE_DROP_CTRL_ECN_ENABLE	CSR_GENMASK(23, 16)
#define FBNIC_RXB_INTF_CREDIT_MASK2		CSR_GENMASK(11, 8)
#define FBNIC_RXB_ENDIAN_FCS		0x08044
#define FBNIC_RXB_CLDR_PRIO_CFG(n)	(0x8034 + (n))
#define FBNIC_QM_TCQ_CTL0		0x0082d
#define FBNIC_FAB_AXI4_AR_SPACER_MASK		CSR_BIT(16)
#define FBNIC_QM_TQS_MTU_CTL0		0x0081d
#define FBNIC_QM_TQS_EDT_TS_RANGE	0x00849
#define FBNIC_QM_TCQ_CTL0_TICK_CYCLES		CSR_GENMASK(26, 16)
#define FBNIC_QM_TQS_CTL0_PREFETCH_THRESH	CSR_GENMASK(7, 1)
#define FBNIC_QM_RCQ_CTL0_TICK_CYCLES		CSR_GENMASK(26, 16)
#define FBNIC_QM_TQS_MTU_CTL1		0x0081e
#define FBNIC_QM_RCQ_CTL0_COAL_WAIT		CSR_GENMASK(15, 0)
#define FBNIC_QM_RCQ_CTL0		0x00c0c
#define FBNIC_QM_TQS_CTL0_LSO_TS_MASK	CSR_BIT(0)
#define FBNIC_FAB_AXI4_AR_SPACER_2_CFG		0x0C005
#define FBNIC_QM_TQS_MTU_CTL1_BULK		CSR_GENMASK(13, 0)
#define FBNIC_FAB_AXI4_AR_SPACER_THREADSHOLD	CSR_GENMASK(15, 0)
#define FBNIC_QM_TQS_CTL0		0x0081b
#define FBNIC_TWD_FLAG_REQ_COMPLETION		DESC_BIT(37)
#define FBNIC_QM_TWQ_DEFAULT_META_H	0x00819
#define FBNIC_QM_TWQ_DEFAULT_META_L	0x00818
#define FBNIC_QM_TCQ_CTL0_COAL_WAIT		CSR_GENMASK(15, 0)
#define FBNIC_QM_TQS_CTL1_MC_MAX_CREDITS	CSR_GENMASK(7, 0)
#define FBNIC_TCE_TXB_ENQ_WRR_CTRL	0x04003
#define FBNIC_TCE_TXB_TX_BMC_Q_CTRL	0x0404B
#define FBNIC_TCE_LSO_CTRL_IPID_MODE_INC	CSR_BIT(27)
#define FBNIC_TCE_LSO_CTRL_TCPF_CLR_1ST		CSR_GENMASK(8, 0)
#define FBNIC_TCE_CSO_CTRL		0x04001
#define FBNIC_TCE_LSO_CTRL_TCPF_CLR_MID		CSR_GENMASK(17, 9)
#define FBNIC_TCE_TXB_TEI_DWRR_CTRL	0x04009
#define FBNIC_TCE_TXB_NTWRK_DWRR_CTRL	0x0400a
#define FBNIC_TCE_TXB_CLDR_SLOT_CFG(n)	(0x0400c + (n))
#define FBNIC_TCE_TXB_ENQ_WRR_CTRL_WEIGHT0	CSR_GENMASK(7, 0)
#define FBNIC_TCE_TXB_Q_CTRL_SIZE		CSR_GENMASK(22, 11)
#define FBNIC_TCE_TXB_NTWRK_DWRR_CTRL_QUANTUM1	CSR_GENMASK(15, 8)
#define FBNIC_TCE_TXB_CTRL_TCAM_ENABLE		CSR_BIT(1)
#define FBNIC_TCE_MC_MAX_PKTSZ		0x0403b
#define FBNIC_TCE_TXB_MC_Q_CTRL		0x04006
#define FBNIC_TCE_TXB_TEI_Q0_CTRL	0x04004
#define FBNIC_TCE_TXB_CTRL		0x04002
#define FBNIC_TCE_TXB_BMC_DWRR_CTRL_EXT	0x0404F
#define FBNIC_TCE_BMC_MAX_PKTSZ_TX		CSR_GENMASK(13, 0)
#define FBNIC_TCE_TXB_RX_BMC_Q_CTRL	0x04008
#define FBNIC_TCE_TXB_NTWRK_DWRR_CTRL_EXT \
					0x0404E
#define FBNIC_TCE_LSO_CTRL		0x04000
#define FBNIC_TCE_MC_MAX_PKTSZ_TMI		CSR_GENMASK(13, 0)
#define FBNIC_TMI_SOP_PROT_CTRL		0x04400
#define FBNIC_QM_TQS_CTL1_BULK_MAX_CREDITS	CSR_GENMASK(15, 8)
#define FBNIC_TCE_TXB_CTRL_LOAD			CSR_BIT(0)
#define FBNIC_TCE_TXB_NTWRK_DWRR_CTRL_QUANTUM2	CSR_GENMASK(23, 16)
#define FBNIC_QM_TQS_CTL1		0x0081c
#define FBNIC_TCE_TXB_TEI_DWRR_CTRL_EXT	0x0404D
#define FBNIC_TCE_TXB_BMC_DWRR_CTRL_QUANTUM0	CSR_GENMASK(7, 0)
#define FBNIC_TCE_TXB_ENQ_WRR_CTRL_WEIGHT2	CSR_GENMASK(23, 16)
#define FBNIC_TCE_TXB_RX_TEI_Q_CTRL	0x04007
#define FBNIC_TCE_TXB_BMC_DWRR_CTRL	0x0404C
#define FBNIC_TCE_TXB_BMC_DWRR_CTRL_QUANTUM1	CSR_GENMASK(15, 8)
#define FBNIC_TCE_SOP_PROT_CTRL_TBI		CSR_GENMASK(7, 0)
#define FBNIC_TCE_TXB_Q_CTRL_START		CSR_GENMASK(10, 0)
#define FBNIC_TCE_SOP_PROT_CTRL_TTI_FRM		CSR_GENMASK(14, 8)
#define FBNIC_TCE_SOP_PROT_CTRL_TTI_CM		CSR_GENMASK(18, 15)
#define FBNIC_TCE_BMC_MAX_PKTSZ		0x0403a
#define FBNIC_TCE_SOP_PROT_CTRL		0x0403c
#define FBNIC_TCE_LSO_CTRL_TCPF_CLR_END		CSR_GENMASK(26, 18)
#define FBNIC_TCE_BMC_MAX_PKTSZ_RX		CSR_GENMASK(27, 14)
#define FBNIC_QM_TNI_TDE_CTL		0x0086d
#define FBNIC_QM_TNI_TCM_CTL		0x0086e
#define FBNIC_QM_RNI_RBP_CTL		0x00c2d
#define FBNIC_QM_TNI_TDE_CTL_MRRS		CSR_GENMASK(1, 0)
#define FBNIC_QM_RNI_RCM_CTL		0x00c2f
#define FBNIC_QM_TNI_TDF_CTL		0x0086c
#define FBNIC_QM_TNI_TDE_CTL_MAX_OB		CSR_GENMASK(24, 12)
#define FBNIC_QM_TNI_TDE_CTL_MRRS_1K		CSR_BIT(25)
#define FBNIC_QM_RNI_RDE_CTL		0x00c2e
#define FBNIC_QM_TNI_TDE_CTL_CLS		CSR_GENMASK(3, 2)
#define FBNIC_QM_TNI_TDE_CTL_MAX_OT		CSR_GENMASK(11, 4)
#define FBNIC_RXB_PAUSE_DROP_CTRL_PS_ENABLE	CSR_GENMASK(27, 24)
#define FBNIC_RXB_ERR_INTR_MASK		0x08052
#define FBNIC_RXB_PAUSE_DROP_CTRL_PAUSE_ENABLE	CSR_GENMASK(15, 8)
#define FBNIC_IPC_MBX_DESC_FW_CMPL	DESC_BIT(1)
#define FBNIC_BD_FRAG_ID_MASK \
	(FBNIC_BD_DESC_ID_MASK & \
	 ~(FBNIC_BD_DESC_ID_MASK * FBNIC_BD_FRAG_COUNT))
#define FBNIC_BD_FRAG_ADDR_MASK \
	(FBNIC_BD_DESC_ADDR_MASK & \
	 ~(FBNIC_BD_DESC_ADDR_MASK * FBNIC_BD_FRAG_COUNT))
#define FBNIC_RPC_TCAM_ACT_VALUE		CSR_GENMASK(15, 0)
#define FBNIC_RPC_TCAM_ACT_MASK			CSR_GENMASK(31, 16)
#define FBNIC_RPC_TCAM_ACT1_L4_VALID		CSR_BIT(15)
#define FBNIC_RPC_TCAM_ACT1_IP_VALID		CSR_BIT(12)
#define FBNIC_RPC_TCAM_ACT1_IP_IS_V6		CSR_BIT(11)
#define FBNIC_RPC_TCAM_ACT1_L4_IS_UDP		CSR_BIT(14)
#define FBNIC_TWD_FLAG_REQ_CSO			DESC_BIT(36)
#define FBNIC_TWD_FLAG_REQ_TS			DESC_BIT(34)
#define FBNIC_RX_TROOM \
	SKB_DATA_ALIGN(sizeof(struct skb_shared_info))
#define FBNIC_RX_HROOM \
	(ALIGN(FBNIC_RX_TROOM + FBNIC_RX_HROOM_PAD, 128) - FBNIC_RX_TROOM)
#define FBNIC_MBX_RX_TO_SEC			10
#define FBNIC_MAX_RX_PKT_DESC	7
#define FBNIC_RPC_TCAM_ACT1_OUTER_IP_VALID	CSR_BIT(13)
#define FBNIC_RPC_TCAM_ACT0_OUTER_IPDST_IDX	CSR_GENMASK(14, 12)
#define FBNIC_RPC_ACT_TBL_NFC_ENTRIES \
	(FBNIC_RPC_ACT_TBL_RSS_OFFSET - FBNIC_RPC_ACT_TBL_NFC_OFFSET)
#define FBNIC_RPC_TCAM_ACT0_IPSRC_IDX		CSR_GENMASK(2, 0)
#define FBNIC_RPC_ACT_TBL_NFC_OFFSET		2
#define FBNIC_RPC_TCAM_ACT0_OUTER_IPSRC_IDX	CSR_GENMASK(10, 8)
#define FBNIC_RPC_ACT_TBL0_Q_ID			CSR_GENMASK(15, 8)
#define FBNIC_RPC_TCAM_ACT0_IPDST_IDX		CSR_GENMASK(6, 4)
#define FBNIC_RPC_ACT_TBL0_RSS_CTXT_ID		CSR_BIT(30)
#define FBNIC_RPC_ACT_TBL0_Q_SEL		CSR_BIT(4)
#define FBNIC_RPC_TCAM_ACT0_IPSRC_VALID		CSR_BIT(3)
#define FBNIC_RPC_TCAM_ACT0_OUTER_IPDST_VALID	CSR_BIT(15)
#define FBNIC_RPC_TCAM_ACT0_IPDST_VALID		CSR_BIT(7)
#define FBNIC_RPC_TCAM_ACT0_OUTER_IPSRC_VALID	CSR_BIT(11)
#define FBNIC_MAC_PS_TO_MAX_MS \
	FBNIC_MAC_RXB_PS_TO_MS(FIELD_MAX(FBNIC_RXB_PAUSE_STORM_THLD_TIME))
#define FBNIC_QM_TNI_TCM_CTL_CLS		CSR_GENMASK(3, 2)
#define FBNIC_QM_TNI_TCM_CTL_MPS		CSR_GENMASK(1, 0)
#define FBNIC_QM_TNI_TDF_CTL_CLS		CSR_GENMASK(3, 2)
#define FBNIC_QM_TNI_TDF_CTL_MAX_OT		CSR_GENMASK(11, 4)
#define FBNIC_QM_TNI_TDF_CTL_MAX_OB		CSR_GENMASK(23, 12)
#define FBNIC_QM_TNI_TDF_CTL_MRRS		CSR_GENMASK(1, 0)
#define FBNIC_PUL_OB_TLP_HDR_AR_CFG	0x3103e
#define FBNIC_PUL_OB_TLP_HDR_AR_CFG_FLUSH	CSR_BIT(19)
#define FBNIC_PUL_OB_TLP_HDR_AW_CFG_FLUSH	CSR_BIT(19)
#define FBNIC_PUL_OB_TLP_HDR_AW_CFG	0x3103d
#define FBNIC_TWD_L3_OHLEN_MASK			DESC_GENMASK(15, 8)
#define FBNIC_TWD_MSS_MASK			DESC_GENMASK(61, 48)
#define FBNIC_TWD_L4_HLEN_MASK			DESC_GENMASK(31, 28)
#define FBNIC_TWD_L4_TYPE_MASK			DESC_GENMASK(33, 32)
#define FBNIC_TWD_L3_TYPE_MASK			DESC_GENMASK(7, 6)
#define FBNIC_TWD_FLAG_REQ_LSO			DESC_BIT(35)
#define FBNIC_RING_F_STATS		BIT(2)
#define FBNIC_RX_HROOM_PAD		128
#define FBNIC_RPC_TCAM_OUTER_IPSRC(m, n)\
	(0x08c00 + 0x08 * (n) + (m))
#define FBNIC_CSR_END_RPC_RAM		0x08f1f
#define FBNIC_RPC_TCAM_OUTER_IPDST(m, n)\
	(0x08c48 + 0x08 * (n) + (m))
#define FBNIC_CSR_START_RPC_RAM		0x08800
#define FBNIC_RPC_TCAM_IPDST(m, n)\
	(0x08cd8 + 0x08 * (n) + (m))
#define FBNIC_RPC_TCAM_IPSRC(m, n)\
	(0x08c90 + 0x08 * (n) + (m))
#define FBNIC_QUEUE_RIM_THRESHOLD	0x2c1
#define FBNIC_QUEUE_RIM_CTL		0x2c0
#define FBNIC_QUEUE_BDQ_PPQ_TAIL	0x242
#define FBNIC_QUEUE_TWQ0_TAIL		0x002
#define FBNIC_RING_F_CTX		BIT(1)
#define FBNIC_QUEUE_TCQ_HEAD		0x081
#define FBNIC_QUEUE_RCQ_HEAD		0x201
#define FBNIC_QUEUE_BDQ_HPQ_TAIL	0x241
#define FBNIC_QUEUE_TWQ1_TAIL		0x003
#define FBNIC_MAC_RXB_PS_TO_MS(ps)	((ps) / 100)
#define FBNIC_INTR_STATUS(n)		(0x00000 + (n))
#define FBNIC_INTR_MASK(n)		(0x00008 + (n))
#define FBNIC_INTR_CLEAR(n)		(0x00018 + (n))
#define FBNIC_RXB_PBUF_FIFO_LEVEL(n)	(0x0811d + (n))
#define FBNIC_RPC_TCAM_IP_ADDR_VALUE		CSR_GENMASK(15, 0)
#define FBNIC_RPC_TCAM_IP_ADDR_WORD_LEN		8
#define FBNIC_RPC_TCAM_IP_ADDR_MASK		CSR_GENMASK(31, 16)
#define FBNIC_QUEUE_BDQ_CTL_RESET		CSR_BIT(0)
#define FBNIC_QUEUE_TCQ_CTL_RESET		CSR_BIT(0)
#define FBNIC_QUEUE_BAL_MASK			CSR_GENMASK(31, 7)
#define FBNIC_QUEUE_TCQ_SIZE_MASK		CSR_GENMASK(3, 0)
#define FBNIC_QUEUE_RCQ_CTL_RESET		CSR_BIT(0)
#define FBNIC_QUEUE_RCQ_SIZE_MASK		CSR_GENMASK(3, 0)
#define FBNIC_QUEUE_BDQ_SIZE_MASK		CSR_GENMASK(3, 0)
#define FBNIC_QUEUE_TWQ_SIZE_MASK		CSR_GENMASK(3, 0)
#define FBNIC_QUEUE_TWQ_CTL_RESET		CSR_BIT(0)

/* BAR sizes (power-of-two aligned) */
#define FBNIC_BAR0_SIZE  0x200000  /* 2 MiB, covers all queues and registers */
#define FBNIC_BAR4_SIZE  0x20000   /* 128 KiB, covers mailbox and MSI-X */

/* MSI-X table/PBA offsets within BAR4 */
#define FBNIC_MSIX_TABLE_OFFSET  0x0
#define FBNIC_MSIX_PBA_OFFSET    0x1000

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
    uint32_t intr_status[FBNIC_MAX_MSIX_VECS];
    uint32_t intr_mask[FBNIC_MAX_MSIX_VECS];

    uint32_t status;      /* Operational status flags */
};

/* Internal helper for status-triggered signaling. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    int i;
    for (i = 0; i < FBNIC_MAX_MSIX_VECS; i++) {
        if ((s->intr_status[i] & ~s->intr_mask[i]) != 0) {
            msix_notify(pdev, i);
        }
    }
}

/* Device-initiated DMA logic (not implemented - details missing) */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /* No DMA logic extractable from provided driver source */
    return;
}

/* MMIO Handlers for BAR0 */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    uint32_t word_idx = addr >> 2; /* Convert byte offset to word index */

    if (size != 4) return 0;

    /* FBNIC_MASTER_SPARE_0 - returns value with some bits clear */
    if (word_idx == 0x0C41B) {
        return 0x00000000;
    }

    /* Interrupt status registers */
    if (word_idx >= 0x00000 && word_idx < 0x00000 + FBNIC_MAX_MSIX_VECS) {
        int n = word_idx - 0x00000;
        return s->intr_status[n];
    }

    /* Interrupt mask registers */
    if (word_idx >= 0x00008 && word_idx < 0x00008 + FBNIC_MAX_MSIX_VECS) {
        int n = word_idx - 0x00008;
        return s->intr_mask[n];
    }

    /* Default: return 0 for unhandled registers */
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t word_idx = addr >> 2;

    if (size != 4) return;

    /* INTR_CLEAR: write-1-to-clear */
    if (word_idx >= 0x00018 && word_idx < 0x00018 + FBNIC_MAX_MSIX_VECS) {
        int n = word_idx - 0x00018;
        s->intr_status[n] &= ~(uint32_t)val;
        pcibase_update_irq(s);
        return;
    }

    /* INTR_SET: set bits */
    if (word_idx >= 0x00010 && word_idx < 0x00010 + FBNIC_MAX_MSIX_VECS) {
        int n = word_idx - 0x00010;
        s->intr_status[n] |= (uint32_t)val;
        pcibase_update_irq(s);
        return;
    }

    /* INTR_MASK_SET: set mask bits */
    if (word_idx >= 0x00030 && word_idx < 0x00030 + FBNIC_MAX_MSIX_VECS) {
        int n = word_idx - 0x00030;
        s->intr_mask[n] |= (uint32_t)val;
        pcibase_update_irq(s);
        return;
    }

    /* INTR_MASK_CLEAR: clear mask bits */
    if (word_idx >= 0x00038 && word_idx < 0x00038 + FBNIC_MAX_MSIX_VECS) {
        int n = word_idx - 0x00038;
        s->intr_mask[n] &= ~(uint32_t)val;
        pcibase_update_irq(s);
        return;
    }

    /* INTR_MASK direct write */
    if (word_idx >= 0x00008 && word_idx < 0x00008 + FBNIC_MAX_MSIX_VECS) {
        int n = word_idx - 0x00008;
        s->intr_mask[n] = (uint32_t)val;
        pcibase_update_irq(s);
        return;
    }

    /* Unhandled writes are ignored */
}

/* BAR4 (Firmware) MMIO handlers */
static uint64_t pcibase_fw_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    uint32_t word_idx = addr >> 2;
    if (size != 4) return 0;

    if (word_idx == 0x6000) { /* FBNIC_FW_ZERO_REG */
        return 0x00000000;
    }
    return 0;
}

static void pcibase_fw_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* No known firmware registers to handle writes */
}

/* PIO handlers (unused) */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static const MemoryRegionOps pcibase_fw_mmio_ops = {
    .read = pcibase_fw_mmio_read,
    .write = pcibase_fw_mmio_write,
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

    /* Set registers to power-on defaults */
    memset(s->intr_status, 0, sizeof(s->intr_status));
    memset(s->intr_mask, 0, sizeof(s->intr_mask));
    /* No other register defaults known from provided source */
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
        if (bi->index == 0) {
            memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, aligned_size);
        } else { /* bar 4 */
            memory_region_init_io(mr, OBJECT(s), &pcibase_fw_mmio_ops, s, bi->name, aligned_size);
        }
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        /* Not used */
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  FBNIC_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  FBNIC_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, FBNIC_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 2;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = FBNIC_BAR0_SIZE, .name = "fbnic-bar0" };
    s->bar_info[4] = (BARInfo){ .index = 4, .type = BAR_TYPE_MMIO, .size = FBNIC_BAR4_SIZE, .name = "fbnic-bar4" };
    for (int i = 0; i < 6; i++) {
        if (s->bar_info[i].type != BAR_TYPE_NONE) {
            pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
        }
    }

    s->has_msix = true;
    msix_init(pdev, FBNIC_MAX_MSIX_VECS, &s->bar_regions[4], 4, FBNIC_MSIX_TABLE_OFFSET,
              &s->bar_regions[4], 4, FBNIC_MSIX_PBA_OFFSET, 0, errp);
    if (errp) {
        return;
    }

    /* No DMA configuration in provided source */
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

    /* No additional cleanup from provided source */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "fbnic_driver_pci",
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
