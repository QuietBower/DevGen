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
#include "qemu/bitops.h"
#include "net/eth.h"

#define TYPE_PCIBASE_DEVICE "alx_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_ATTANSIC		0x1969
#define ALX_RFD_PIDX					0x15E0
#define ALX_DEFAULT_TX_WORK 128
#define RRD_ERR_RES_SHIFT	20
#define ALX_GET_FIELD(_data, _field)					\
	(((_data) >> _field ## _SHIFT) & _field ## _MASK)
#define ALX_RX_ALLOC_THRESH	32
#define RRD_UPDATED_SHIFT	31
#define RRD_PID_IPV6UDP		4
#define RRD_ERR_L4_SHIFT	14
#define RRD_ERR_LEN_SHIFT	30
#define RRD_PID_IPV4TCP		3
#define RRD_PID_IPV6TCP		2
#define RRD_PID_IPV4UDP		5
#define RRD_ERR_IPV4_SHIFT	15
#define ALX_IMR						0x1604
#define ALX_ISR_TX_Q0					BIT(15)
#define ALX_ISR_RX_Q0					BIT(16)
#define ALX_ISR_FATAL		(ALX_ISR_PCIE_LNKDOWN | \
				 ALX_ISR_DMAW | ALX_ISR_DMAR)
#define ALX_ISR_ALERT		(ALX_ISR_RXF_OV | \
				 ALX_ISR_TXF_UR | \
				 ALX_ISR_RFD_UR)
#define ALX_ISR_PHY					BIT(12)
#define ALX_ISR						0x1600
#define ALX_ISR_DIS					BIT(31)
#define ALX_ISR_ALL_QUEUES	(ALX_ISR_TX_Q0 | \
				 ALX_ISR_TX_Q1 | \
				 ALX_ISR_TX_Q2 | \
				 ALX_ISR_TX_Q3 | \
				 ALX_ISR_RX_Q0 | \
				 ALX_ISR_RX_Q1 | \
				 ALX_ISR_RX_Q2 | \
				 ALX_ISR_RX_Q3 | \
				 ALX_ISR_RX_Q4 | \
				 ALX_ISR_RX_Q5 | \
				 ALX_ISR_RX_Q6 | \
				 ALX_ISR_RX_Q7)
#define ALX_RX_BASE_ADDR_HI				0x1540
#define ALX_SRAM_LOAD_PTR				BIT(0)
#define ALX_RFD_RING_SZ					0x1560
#define ALX_RRD_ADDR_LO					0x1568
#define ALX_RFD_ADDR_LO					0x1550
#define ALX_TX_BASE_ADDR_HI				0x1544
#define ALX_SRAM9					0x1534
#define ALX_RRD_RING_SZ					0x1578
#define ALX_TPD_RING_SZ					0x1584
#define ALX_RFD_BUF_SZ					0x1564
#define ALX_HASH_TBL0					0x1490
#define ALX_HASH_TBL1					0x1494
#define ALX_MAC_CTRL_MULTIALL_EN			BIT(25)
#define ALX_MAC_CTRL					0x1480
#define ALX_MAC_CTRL_PROMISC_EN				BIT(15)
#define ALX_MSI_MAP_TBL1_RXQ0_SHIFT			0
#define ALX_MSI_MAP_TBL2				0x15D8
#define ALX_MSI_MAP_TBL1				0x15D0
#define ALX_MSI_ID_MAP					0x15D4
#define ALX_MAX_TX_QUEUES	4
#define ALX_MSI_MASK_SEL_LINE				BIT(16)
#define ALX_MSI_RETRANS_TM_SHIFT			0
#define ALX_MSI_RETRANS_TIMER				0x1920
#define ALX_REV_C0					3
#define ALX_REV_B0					2
#define ALX_MAC_CTRL_TXFC_EN				BIT(2)
#define ALX_MAC_CTRL_MHASH_ALG_HI5B			BIT(29)
#define ALX_MAC_CTRL_PCRCE				BIT(7)
#define ALX_FC_ANEG		0x04
#define ALX_DEV_ID_AR8161				0x1091
#define ALX_FC_TX		0x02
#define ALX_MAC_CTRL_RXFC_EN				BIT(3)
#define ALX_MAC_CTRL_BRD_EN				BIT(26)
#define ALX_MAX_FRAME_LEN(_mtu)	(ALIGN((ALX_RAW_MTU(_mtu) + ALX_FRAME_PAD), 8))
#define ALX_MAC_CTRL_PRMBLEN_SHIFT			10
#define ALX_ISR_MISC		(ALX_ISR_PCIE_LNKDOWN | \
				 ALX_ISR_DMAW | \
				 ALX_ISR_DMAR | \
				 ALX_ISR_SMB | \
				 ALX_ISR_MANU | \
				 ALX_ISR_TIMER)
#define ALX_MAC_CTRL_CRCE				BIT(6)
#define ALX_MAX_FRAME_SIZE	ALX_MAX_JUMBO_PKT_SIZE
#define ALX_FC_RX		0x01
#define ALX_MAC_CTRL_WOLSPED_SWEN			BIT(30)
#define ALX_MAX_TSO_PKT_SIZE	(7*1024)
#define ALX_DEF_RXBUF_SIZE	ALX_MAX_FRAME_LEN(1500)
#define TPD_CXSUMSTART_SHIFT		0
#define TPD_CXSUMOFFSET_SHIFT		18
#define TPD_CXSUM_EN_SHIFT		8
#define TPD_L4HDROFFSET_MASK		0x00FF
#define TPD_L4HDROFFSET_SHIFT		0
#define TPD_MSS_SHIFT			18
#define TPD_LSO_EN_SHIFT		12
#define TPD_IPV4_SHIFT			16
#define TPD_LSO_V2_SHIFT		13
#define TPD_MSS_MASK			0x1FFF
#define TPD_EOP_SHIFT			31
#define ALX_WATCHDOG_TIME   (5 * HZ)
#define ALX_TPD_PRI1_ADDR_LO				0x157C
#define ALX_TPD_PRI0_ADDR_LO				0x1580
#define ALX_TPD_PRI3_ADDR_LO				0x14E4
#define ALX_TPD_PRI2_ADDR_LO				0x14E0
#define ALX_TPD_PRI2_PIDX				0x161A
#define ALX_TPD_PRI3_PIDX				0x1618
#define ALX_TPD_PRI1_PIDX				0x15F0
#define ALX_TPD_PRI0_PIDX				0x15F2
#define ALX_TPD_PRI3_CIDX				0x161C
#define ALX_TPD_PRI2_CIDX				0x161E
#define ALX_TPD_PRI0_CIDX				0x15F6
#define ALX_TPD_PRI1_CIDX				0x15F4
#define ALX_ISR_TX_Q2					BIT(6)
#define ALX_ISR_TX_Q1					BIT(5)
#define ALX_ISR_TX_Q3					BIT(7)
#define ALX_ISR_RX_Q6					BIT(29)
#define ALX_ISR_RX_Q4					BIT(27)
#define ALX_ISR_RX_Q1					BIT(17)
#define ALX_ISR_RX_Q3					BIT(19)
#define ALX_ISR_RX_Q2					BIT(18)
#define ALX_ISR_RX_Q7					BIT(30)
#define ALX_ISR_RX_Q5					BIT(28)
#define ALX_MSI_MAP_TBL2_TXQ3_SHIFT			20
#define ALX_MSI_MAP_TBL1_TXQ0_SHIFT			16
#define ALX_MSI_MAP_TBL1_TXQ1_SHIFT			20
#define ALX_MSI_MAP_TBL2_TXQ2_SHIFT			16
#define ALX_DEV_ID_E2200				0xe091
#define ALX_DEV_ID_AR8162				0x1090
#define ALX_DEV_ID_E2400				0xe0a1
#define ALX_DEV_ID_E2500				0xe0b1
#define ALX_DEV_ID_AR8172				0x10A0
#define ALX_DEV_ID_AR8171				0x10A1
#define ALX_MAX_NAPIS 8
#define ALX_MSIX_ENTRY_BASE				0x2000
#define ALX_ISR_DMAR					BIT(9)
#define ALX_ISR_DMAW					BIT(10)
#define ALX_ISR_PCIE_LNKDOWN				BIT(26)
#define ALX_ISR_RFD_UR					BIT(4)
#define ALX_ISR_RXF_OV					BIT(3)
#define ALX_ISR_TXF_UR					BIT(8)
#define ALX_STAD1					0x148C
#define ALX_STAD0					0x1488
#define ALX_PCI_REVID_SHIFT				3
#define ALX_RAW_MTU(_mtu)	(_mtu + ETH_HLEN + ETH_FCS_LEN + VLAN_HLEN)
#define ALX_FRAME_PAD		16
#define ALX_ISR_SMB					BIT(0)
#define ALX_ISR_TIMER					BIT(1)
#define ALX_ISR_MANU					BIT(2)
#define ALX_MAX_JUMBO_PKT_SIZE	(9*1024)
#define ALX_MISC_INTNLOSC_OPEN				BIT(3)
#define ALX_PMCTRL_L1_EN				BIT(3)
#define ALX_MISC3_25M_NOTO_INTNL			BIT(0)
#define ALX_SERDES_PHYCLK_SLWDWN			BIT(18)
#define ALX_PMCTRL_L0S_EN				BIT(12)
#define ALX_MASTER					0x1400
#define ALX_MISC3_25M_BY_SW				BIT(1)
#define ALX_PMCTRL					0x12F8
#define ALX_MASTER_PCLKSEL_SRDS				BIT(12)
#define ALX_DMA_MAC_RST_TO				50
#define ALX_MISC3					0x19CC
#define ALX_MISC_ISO_EN					BIT(12)
#define ALX_SERDES_MACCLK_SLWDWN			BIT(17)
#define ALX_SERDES					0x1424
#define ALX_MASTER_OOB_DIS				BIT(6)
#define ALX_MSIX_MASK					0x0090
#define ALX_MASTER_DMA_MAC_RST				BIT(0)
#define ALX_MISC					0x19C0
#define ALX_PMCTRL_HOTRST_WTEN				BIT(31)
#define ALX_PMCTRL_RXL1_AFTER_L0S			BIT(11)
#define ALX_PMCTRL_L1_SRDS_EN				BIT(4)
#define ALX_PMCTRL_RCVR_WT_1US				BIT(15)
#define ALX_PMCTRL_L1_SRDSRX_PWD			BIT(6)
#define ALX_PMCTRL_LCKDET_TIMER_DEF			0xC
#define ALX_SET_FIELD(_data, _field, _value)	do {			\
		(_data) &= ~(_field ## _MASK << _field ## _SHIFT);	\
		(_data) |= ((_value) & _field ## _MASK) << _field ## _SHIFT;\
	} while (0)
#define ALX_PMCTRL_TXL1_AFTER_L0S			BIT(19)
#define ALX_PMCTRL_L1_BUFSRX_EN				BIT(7)
#define ALX_PMCTRL_L1_SRDSPLL_EN			BIT(5)
#define ALX_PMCTRL_L1_CLKSW_EN				BIT(13)
#define ALX_PMCTRL_L1REG_TO_DEF				0xF
#define ALX_PMCTRL_L1_TIMER_16US			4
#define ALX_PMCTRL_ASPM_FCEN				BIT(30)
#define ALX_PMCTRL_SADLY_EN				BIT(29)
#define ALX_RXQ0					0x15A0
#define ALX_RXQ0_RSS_HASH_EN				BIT(29)
#define ALX_DMA_RREQ_BLEN_SHIFT				4
#define ALX_MASTER_SYSALVTIMER_EN			BIT(7)
#define ALX_HQTPD_Q1_NUMPREF_SHIFT			0
#define ALX_MAC_CTRL_FAST_PAUSE				BIT(31)
#define ALX_DMA_RREQ_PRI_DATA				BIT(10)
#define ALX_IRQ_MODU_TIMER1_SHIFT			0
#define ALX_RXQ2					0x15A8
#define ALX_WRR_PRI_SHIFT				29
#define ALX_IDLE_DECISN_TIMER				0x1474
#define ALX_RXQ0_ASPM_THRESH_100M			3
#define ALX_RXQ0_NUM_RFD_PREF_DEF			8
#define ALX_DMA_RORDER_MODE_OUT				4
#define ALX_DMA_WDLY_CNT_SHIFT				16
#define ALX_WRR_PRI_RESTRICT_NONE			3
#define ALX_IRQ_MODU_TIMER				0x1408
#define ALX_MTU_JUMBO_TH				1514
#define ALX_MTU						0x149C
#define ALX_WRR_PRI0_SHIFT				0
#define ALX_RXQ0_NUM_RFD_PREF_SHIFT			20
#define ALX_DMA_RDLY_CNT_DEF				15
#define ALX_HQTPD_Q3_NUMPREF_SHIFT			8
#define ALX_TXQ_TPD_BURSTPREF_DEF			5
#define ALX_MTU_STD_ALGN				1536
#define ALX_DMA_RDLY_CNT_SHIFT				11
#define ALX_TXQ1					0x1594
#define ALX_TXQ_TXF_BURST_PREF_DEF			0x200
#define ALX_DMA						0x15C0
#define ALX_HQTPD_BURST_EN				BIT(31)
#define ALX_RXQ0_IDT_TBL_SIZE_SHIFT			8
#define ALX_WRR						0x1938
#define ALX_DMA_RCHNL_SEL_SHIFT				26
#define ALX_RXQ2_RXF_FLOW_CTRL_RSVD			3212
#define ALX_HQTPD_Q2_NUMPREF_SHIFT			4
#define ALX_RXQ0_IDT_TBL_SIZE_DEF			0x100
#define ALX_DEV_CTRL_MAXRRS_MIN				2
#define ALX_SRAM5					0x1524
#define ALX_RXQ2_RXF_XOFF_THRESH_SHIFT			16
#define ALX_MASTER_IRQMOD1_EN				BIT(10)
#define ALX_HQTPD					0x193C
#define ALX_RXQ0_RSS_MODE_SHIFT				26
#define ALX_INT_RETRIG_TO				20000
#define ALX_CLK_GATE					0x1814
#define ALX_IDLE_DECISN_TIMER_DEF			0x400
#define ALX_SMB_TIMER					0x15C4
#define ALX_SRAM_RXF_LEN_8K				(8*1024)
#define ALX_RXQ0_IPV6_PARSE_EN				BIT(7)
#define ALX_WRR_PRI3_SHIFT				24
#define ALX_TXQ0_TXF_BURST_PREF_SHIFT			16
#define ALX_TINT_TIMER					0x15CC
#define ALX_RXQ2_RXF_XON_THRESH_SHIFT			0
#define ALX_RXQ0_RSS_HSTYP_ALL		(ALX_RXQ0_RSS_HSTYP_IPV6_TCP_EN | \
					 ALX_RXQ0_RSS_HSTYP_IPV4_TCP_EN | \
					 ALX_RXQ0_RSS_HSTYP_IPV6_EN | \
					 ALX_RXQ0_RSS_HSTYP_IPV4_EN)
#define ALX_DMA_WDLY_CNT_DEF				4
#define ALX_INT_RETRIG					0x1608
#define ALX_TXQ0_SUPT_IPOPT				BIT(4)
#define ALX_RXQ0_RSS_MODE_DIS				0
#define ALX_TXQ0					0x1590
#define ALX_TXQ1_ERRLGPKT_DROP_EN			BIT(11)
#define ALX_TXQ0_LSO_8023_EN				BIT(7)
#define ALX_DMA_RORDER_MODE_SHIFT			0
#define ALX_TXQ0_TPD_BURSTPREF_SHIFT			0
#define ALX_TXQ0_MODE_ENHANCE				BIT(6)
#define ALX_CLK_GATE_ALL		(ALX_CLK_GATE_RXMAC | \
					 ALX_CLK_GATE_TXMAC | \
					 ALX_CLK_GATE_RXQ | \
					 ALX_CLK_GATE_TXQ | \
					 ALX_CLK_GATE_DMAR | \
					 ALX_CLK_GATE_DMAW)
#define ALX_WRR_PRI2_SHIFT				16
#define ALX_WRR_PRI1_SHIFT				8
#define ALX_TXQ1_JUMBO_TSO_TH				(7*1024)
#define ALX_TINT_TPD_THRSHLD				0x15C8
#define ALX_MASTER_IRQMOD2_EN				BIT(11)
#define ALX_MII_ISR					0x13
#define ALX_MAC_CTRL_SPEED_1000				2
#define ALX_MAC_CTRL_TX_EN				BIT(0)
#define ALX_TXQ0_EN					BIT(5)
#define ALX_MAC_CTRL_FULLD				BIT(5)
#define ALX_RXQ0_EN					BIT(31)
#define ALX_MAC_CTRL_SPEED_10_100			1
#define ALX_MAC_CTRL_RX_EN				BIT(1)
#define ALX_MIIDBG_AZ_ANADECT				0x15
#define ALX_MSE16DB_DOWN				0x02EA
#define ALX_MIIEXT_CLDCTRL6				0x8006
#define ALX_MIIEXT_ANEG					7
#define ALX_MSE16DB_UP					0x05EA
#define ALX_AGC_LONG1G_LIMT				40
#define ALX_AZ_ANADECT_LONG				0x3210
#define ALX_MIIEXT_AFE					0x801A
#define ALX_CLDCTRL6_CAB_LEN_SHORT100M			152
#define ALX_MIIEXT_PCS					3
#define ALX_AFE_10BT_100M_TH				0x0040
#define ALX_MSE20DB_TH_HI				0x54
#define ALX_AZ_ANADECT_DEF				0x3220
#define ALX_CLDCTRL6_CAB_LEN_SHORT1G			116
#define ALX_MIIDBG_MSE20DB				0x1C
#define ALX_MIIDBG_AGC					0x23
#define ALX_MSE20DB_TH_DEF				0x2E
#define ALX_AGC_LONG100M_LIMT				44
#define ALX_MIIDBG_MSE16DB				0x18
#define ALX_GIGA_PSSR_SPD_DPLX_RESOLVED			0x0800
#define ALX_GIGA_PSSR_DPLX				0x2000
#define ALX_GIGA_PSSR_SPEED				0xC000
#define ALX_GIGA_PSSR_100MBS				0x4000
#define ALX_GIGA_PSSR_10MBS				0x0000
#define ALX_GIGA_PSSR_1000MBS				0x8000
#define ALX_MII_GIGA_PSSR				0x11
#define ALX_MIB_TX_BCCNT				(ALX_MIB_BASE + 188)
#define ALX_MIB_RX_MCAST				(ALX_MIB_BASE + 8)
#define ALX_MIB_RX_PAUSE				(ALX_MIB_BASE + 12)
#define ALX_MIB_TX_SZ_MAX				(ALX_MIB_BASE + 152)
#define ALX_MIB_RX_CTRL					(ALX_MIB_BASE + 16)
#define ALX_MIB_RX_ALIGN_ERR				(ALX_MIB_BASE + 80)
#define ALX_MIB_RX_OK					(ALX_MIB_BASE + 0)
#define ALX_MIB_RX_RUNT					(ALX_MIB_BASE + 32)
#define ALX_MIB_TX_EXC_DEFER				(ALX_MIB_BASE + 112)
#define ALX_MIB_RX_SZ_127B				(ALX_MIB_BASE + 44)
#define ALX_MIB_TX_SINGLE_COL				(ALX_MIB_BASE + 156)
#define ALX_MIB_TX_OK					(ALX_MIB_BASE + 96)
#define ALX_MIB_TX_ABORT_COL				(ALX_MIB_BASE + 168)
#define ALX_MIB_RX_SZ_1518B				(ALX_MIB_BASE + 60)
#define ALX_MIB_TX_MCAST				(ALX_MIB_BASE + 104)
#define ALX_MIB_RX_SZ_511B				(ALX_MIB_BASE + 52)
#define ALX_MIB_RX_SZ_255B				(ALX_MIB_BASE + 48)
#define ALX_MIB_TX_SZ_1023B				(ALX_MIB_BASE + 144)
#define ALX_MIB_TX_SZ_255B				(ALX_MIB_BASE + 136)
#define ALX_MIB_RX_FCS_ERR				(ALX_MIB_BASE + 20)
#define ALX_MIB_RX_OV_RRD				(ALX_MIB_BASE + 76)
#define ALX_MIB_TX_SZ_127B				(ALX_MIB_BASE + 132)
#define ALX_MIB_TX_TRD_EOP				(ALX_MIB_BASE + 176)
#define ALX_MIB_TX_LEN_ERR				(ALX_MIB_BASE + 180)
#define ALX_MIB_TX_DEFER				(ALX_MIB_BASE + 120)
#define ALX_MIB_TX_BYTE_CNT				(ALX_MIB_BASE + 124)
#define ALX_MIB_TX_TRUNC				(ALX_MIB_BASE + 184)
#define ALX_MIB_RX_OV_SZ				(ALX_MIB_BASE + 68)
#define ALX_MIB_RX_BYTE_CNT				(ALX_MIB_BASE + 28)
#define ALX_MIB_TX_SZ_511B				(ALX_MIB_BASE + 140)
#define ALX_MIB_TX_MULTI_COL				(ALX_MIB_BASE + 160)
#define ALX_MIB_TX_CTRL					(ALX_MIB_BASE + 116)
#define ALX_MIB_RX_LEN_ERR				(ALX_MIB_BASE + 24)
#define ALX_MIB_TX_BCAST				(ALX_MIB_BASE + 100)
#define ALX_MIB_RX_MCCNT				(ALX_MIB_BASE + 88)
#define ALX_MIB_TX_PAUSE				(ALX_MIB_BASE + 108)
#define ALX_MIB_RX_BCAST				(ALX_MIB_BASE + 4)
#define ALX_MIB_TX_SZ_64B				(ALX_MIB_BASE + 128)
#define ALX_MIB_TX_MCCNT				(ALX_MIB_BASE + 192)
#define ALX_MIB_UPDATE					(ALX_MIB_BASE + 196)
#define ALX_MIB_RX_OV_RXF				(ALX_MIB_BASE + 72)
#define ALX_MIB_RX_FRAG					(ALX_MIB_BASE + 36)
#define ALX_MIB_RX_SZ_MAX				(ALX_MIB_BASE + 64)
#define ALX_MIB_RX_BCCNT				(ALX_MIB_BASE + 84)
#define ALX_MIB_RX_SZ_1023B				(ALX_MIB_BASE + 56)
#define ALX_MIB_TX_UNDERRUN				(ALX_MIB_BASE + 172)
#define ALX_MIB_RX_SZ_64B				(ALX_MIB_BASE + 40)
#define ALX_MIB_TX_SZ_1518B				(ALX_MIB_BASE + 148)
#define ALX_MIB_TX_LATE_COL				(ALX_MIB_BASE + 164)
#define ALX_MIB_RX_ERRADDR				(ALX_MIB_BASE + 92)
#define ALX_SLD						0x0218
#define ALX_EFLD_F_EXIST				BIT(10)
#define ALX_SLD_START					BIT(11)
#define ALX_EFLD					0x0204
#define ALX_EFLD_E_EXIST				BIT(9)
#define ALX_EFLD_STAT					BIT(5)
#define ALX_SLD_STAT					BIT(12)
#define ALX_EFLD_START					BIT(0)
#define ALX_TST10BTCFG_DEF				0x4C04
#define ALX_PHY_CTRL_DSPRST_OUT				BIT(0)
#define ALX_PHY_CTRL_CLS	(ALX_PHY_CTRL_LED_MODE | \
				 ALX_PHY_CTRL_100AB_EN | \
				 ALX_PHY_CTRL_PLL_ON)
#define ALX_MIIDBG_TST100BTCFG				0x36
#define ALX_LPI_CTRL					0x1440
#define ALX_MIIDBG_GREENCFG2				0x3D
#define ALX_MIIEXT_NLP78				0x8027
#define ALX_MIIDBG_LEGCYPS				0x29
#define ALX_ANACTRL_DEF					0x02EF
#define ALX_MIIEXT_VDRVBIAS				0x8062
#define ALX_LEGCYPS_DEF					0x129D
#define ALX_CLDCTRL3_BP_CABLE1TH_DET_GT			0x8000
#define ALX_IER_LINK_UP					0x0400
#define ALX_CLDCTRL5_BP_VD_HLFBIAS			0x4000
#define ALX_PHY_CTRL_IDDQ				BIT(7)
#define ALX_MIIEXT_S3DIG10_DEF				0
#define ALX_SYSMODCTRL_IECHOADJ_DEF			0xBB8B
#define ALX_PHY_CTRL					0x140C
#define ALX_MIIEXT_S3DIG10				0x8023
#define ALX_PHY_CTRL_GATE_25M				BIT(5)
#define ALX_VDRVBIAS_DEF				0x3
#define ALX_TST100BTCFG_DEF				0xE12C
#define ALX_MII_IER					0x12
#define ALX_PHY_CTRL_HIB_EN				BIT(10)
#define ALX_PHY_CTRL_DSPRST_TO				80
#define ALX_IER_LINK_DOWN				0x0800
#define ALX_MIIDBG_SYSMODCTRL				0x04
#define ALX_MIIDBG_ANACTRL				0x00
#define ALX_GREENCFG2_GATE_DFSE_EN			0x0080
#define ALX_GREENCFG2_BP_GREEN				0x8000
#define ALX_MIIEXT_CLDCTRL5				0x8005
#define ALX_PHY_CTRL_HIB_PULSE				BIT(11)
#define ALX_MIIDBG_SRDSYSMOD				0x05
#define ALX_MIIEXT_LOCAL_EEEADV				0x3C
#define ALX_MIIEXT_NLP78_120M_DEF			0x8A05
#define ALX_LPI_CTRL_EN					BIT(0)
#define ALX_MIIDBG_TST10BTCFG				0x12
#define ALX_MIIEXT_CLDCTRL3				0x8003
#define ALX_SRDSYSMOD_DEF				0x2C46
#define ALX_PHY_CTRL_RST_ANALOG				BIT(12)
#define ALX_PHY_CTRL_POWER_DOWN				BIT(14)
#define ALX_UE_SVRT_FCPROTERR				BIT(13)
#define ALX_PDLL_TRNS1					0x1104
#define ALX_WOL0					0x14A0
#define ALX_UE_SVRT_DLPROTERR				BIT(4)
#define ALX_PDLL_TRNS1_D3PLLOFF_EN			BIT(11)
#define ALX_UE_SVRT					0x010C
#define ALX_MASTER_WAKEN_25M				BIT(5)
#define ALX_DRV_PHY_UNKNOWN				0
#define ALX_DRV						0x1804
#define ALX_PHY_INITED					0x003F
#define ALX_MII_DBG_ADDR				0x1D
#define ALX_MAC_STS					0x1410
#define ALX_MAC_STS_IDLE	(ALX_MAC_STS_TXQ_BUSY | \
				 ALX_MAC_STS_RXQ_BUSY | \
				 ALX_MAC_STS_TXMAC_BUSY | \
				 ALX_MAC_STS_RXMAC_BUSY)
#define ALX_MISC_PSW_OCP_DEF				0x7
#define ALX_MSIC2_CALB_START				BIT(0)
#define ALX_MSIC2					0x19C8
#define ALX_REV_A1					1
#define ALX_REV_A0					0
#define ALX_RXQ0_RSS_HSTYP_IPV6_TCP_EN			BIT(5)
#define ALX_RXQ0_RSS_HSTYP_IPV4_TCP_EN			BIT(3)
#define ALX_RXQ0_RSS_HSTYP_IPV4_EN			BIT(2)
#define ALX_RXQ0_RSS_HSTYP_IPV6_EN			BIT(4)
#define ALX_CLK_GATE_RXQ				BIT(3)
#define ALX_CLK_GATE_DMAR				BIT(1)
#define ALX_CLK_GATE_TXMAC				BIT(4)
#define ALX_CLK_GATE_RXMAC				BIT(5)
#define ALX_CLK_GATE_DMAW				BIT(0)
#define ALX_CLK_GATE_TXQ				BIT(2)
#define ALX_MIB_BASE					0x1700
#define ALX_SLD_MAX_TO					100
#define ALX_PHY_CTRL_LED_MODE				BIT(2)
#define ALX_PHY_CTRL_PLL_ON				BIT(13)
#define ALX_PHY_CTRL_100AB_EN				BIT(17)
#define ALX_DRV_PHY_100					BIT(26)
#define ALX_DRV_PHY_1000				BIT(27)
#define ALX_DRV_PHY_10					BIT(25)
#define ALX_DRV_PHY_AUTO				BIT(28)
#define ALX_DRV_PHY_DUPLEX				BIT(24)
#define ALX_MAC_STS_RXMAC_BUSY				BIT(0)
#define ALX_MAC_STS_TXMAC_BUSY				BIT(1)
#define ALX_MAC_STS_TXQ_BUSY				BIT(3)
#define ALX_MAC_STS_RXQ_BUSY				BIT(2)

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
    uint32_t isr;
    uint32_t imr;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t regs[0x8000 / 4];

    /* DMA Context */
    uint64_t tpd_dma;
    uint64_t rrd_dma;
    uint64_t rfd_dma;

    uint32_t mac_sts;
    uint32_t pmctrl;
    uint8_t mac_addr[6];
};

struct alx_rfd {
    uint64_t addr;
};

struct alx_rrd {
    uint32_t word0;
    uint32_t rss_hash;
    uint32_t word2;
    uint32_t word3;
};

struct alx_txd {
    uint16_t len;
    uint16_t vlan_tag;
    uint32_t word1;
    union {
        uint64_t addr;
        struct {
            uint32_t pkt_len;
            uint32_t resvd;
        } l;
    } adrl;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    uint32_t isr = s->regs[ALX_ISR / 4];
    uint32_t imr = s->regs[ALX_IMR / 4];

    if (isr & imr) {
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
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr < sizeof(s->regs)) {
        if (size == 4) {
            val = s->regs[addr / 4];
        } else if (size == 2) {
            val = ((uint16_t *)s->regs)[addr / 2];
        } else if (size == 1) {
            val = ((uint8_t *)s->regs)[addr];
        }
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < sizeof(s->regs)) {
        if (size == 4) {
            if (addr == ALX_ISR) {
                s->regs[addr / 4] &= ~val;
                pcibase_update_irq(s);
            } else if (addr == ALX_MASTER) {
                s->regs[addr / 4] = val;
                if (val & ALX_MASTER_DMA_MAC_RST) {
                    s->regs[addr / 4] &= ~ALX_MASTER_DMA_MAC_RST;
                    s->regs[ALX_RFD_PIDX / 4] = 0;
                }
            } else if (addr == ALX_SLD) {
                s->regs[addr / 4] = val;
                if (val & ALX_SLD_START) {
                    s->regs[addr / 4] &= ~ALX_SLD_START;
                }
            } else if (addr == ALX_EFLD) {
                s->regs[addr / 4] = val;
                if (val & ALX_EFLD_START) {
                    s->regs[addr / 4] &= ~ALX_EFLD_START;
                }
            } else if (addr == ALX_MSIC2) {
                s->regs[addr / 4] = val;
                if (val & ALX_MSIC2_CALB_START) {
                    s->regs[addr / 4] &= ~ALX_MSIC2_CALB_START;
                }
            } else {
                s->regs[addr / 4] = val;
                if (addr == ALX_IMR) {
                    pcibase_update_irq(s);
                }
            }
        } else if (size == 2) {
            ((uint16_t *)s->regs)[addr / 2] = val;
        } else if (size == 1) {
            ((uint8_t *)s->regs)[addr] = val;
        }
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    uint64_t val = 0;

    
    return val;
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

    memset(s->regs, 0, sizeof(s->regs));
    
    /* Provide a dummy MAC address to satisfy alx_read_macaddr */
    s->regs[ALX_STAD0 / 4] = 0x12345678;
    s->regs[ALX_STAD1 / 4] = 0x00009ABC;

    /* Satisfy alx_get_phy_config */
    s->regs[ALX_PHY_CTRL / 4] |= ALX_PHY_CTRL_DSPRST_OUT;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_ATTANSIC );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  ALX_DEV_ID_AR8161 );
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
    s->bar_info[0].size = 0x8000;
    s->bar_info[0].name = "alx-mmio";
      
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    msi_init(pdev, 0, 1, true, false, errp);
    msix_init(pdev, ALX_MAX_NAPIS, &s->bar_regions[0], 0, ALX_MSIX_ENTRY_BASE, &s->bar_regions[0], 0, ALX_MSIX_ENTRY_BASE + 0x800, 0, errp);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }

    
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "alx_pci",
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
