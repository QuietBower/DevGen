/*
 * QEMU PCI device model for KSZ884x Ethernet controller
 * Based on Linux driver: drivers/net/ethernet/micrel/ksz884x.c
 * Vendor 0x16C6, Device 0x8841
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

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

#define TYPE_PCIBASE_DEVICE "ksz884xp_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI_VENDOR_ID_MICREL_KS 0x16c6
#define PCI_DEVICE_ID_KSZ8841   0x8841
#define PCI_CLASS_NETWORK_ETHERNET 0x0200

#define BAR0_SIZE 0x1000

#define PORT_LED_OFF			0x8000
#define DRV_NAME		"KSZ884X PCI"
#define DEVICE_NAME		"KSZ884x PCI"
#define DRV_VERSION		"1.0.0"
#define HOST_MASK		(1 << SWITCH_PORT_NUM)
#define DRV_RELDATE		"Feb 8, 2010"
#define HOST_PORT		SWITCH_PORT_NUM
#define PORT_TX_ENABLE			0x0400
#define PORT_RX_ENABLE			0x0200
#define PORT_LEARN_DISABLE		0x0100
#define BROADCAST_STORM_VALUE		9969
#define BROADCAST_STORM_RATE_HI		0x0007
#define BROADCAST_STORM_RATE_LO		0xFF00
#define BROADCAST_STORM_RATE		0x07FF
#define MULTICAST_STORM_DISABLE		0x0040
#define FAIR_FLOW_CTRL			0x0010
#define NO_EXC_COLLISION_DROP		0x0008
#define UNICAST_VLAN_BOUNDARY		0x0080
#define PORT_CTRL_ADDR(port, addr)		\
	(addr = KS8842_PORT_1_CTRL_1 + (port) *		\
		(KS8842_PORT_2_CTRL_1 - KS8842_PORT_1_CTRL_1))
#define PORT_TX_FLOW_CTRL		0x1000
#define PORT_RX_FLOW_CTRL		0x0800
#define PORT_AUTO_NEG_ENABLE		0x0080
#define PORT_POWER_DOWN			0x0800
#define PORT_AUTO_NEG_RESTART		0x2000
#define PORT_REMOTE_FAULT		0x0100
#define PORT_AUTO_NEG_SYM_PAUSE		0x0010
#define PORT_AUTO_NEG_100BTX_FD		0x0008
#define PORT_AUTO_NEG_100BTX		0x0004
#define PORT_AUTO_NEG_10BT_FD		0x0002
#define PORT_AUTO_NEG_10BT		0x0001
#define PORT_REMOTE_SYM_PAUSE		0x0010
#define PORT_REMOTE_100BTX_FD		0x0008
#define PORT_REMOTE_100BTX		0x0004
#define PORT_REMOTE_10BT_FD		0x0002
#define PORT_REMOTE_10BT		0x0001
#define PORT_START_CABLE_DIAG		0x1000
#define PORT_TX_DISABLE			0x4000
#define PORT_CABLE_STAT_NORMAL		0x0000
#define PORT_CABLE_STAT_OPEN		0x2000
#define PORT_CABLE_STAT_SHORT		0x4000
#define PORT_CABLE_STAT_FAILED		0x6000
#define PORT_CABLE_FAULT_COUNTER	0x01FF
#define PORT_BROADCAST_STORM		0x0080
#define PORT_BACK_PRESSURE		0x0800
#define PORT_COUNT_IFG			0x0002
#define PORT_COUNT_PREAMBLE		0x0001
#define PORT_MIRROR_RX			0x0040
#define PORT_MIRROR_TX			0x0020
#define PORT_MIRROR_SNIFFER		0x0080
#define PORT_DISCARD_NON_VID		0x2000
#define PRIO_QUEUES			4
#define TOTAL_PORT_NUM			(SWITCH_PORT_NUM + 1)
#define PORT_DIFFSERV_ENABLE		0x0040
#define PORT_802_1P_ENABLE		0x0020
#define PORT_INSERT_TAG			0x0004
#define PORT_REMOVE_TAG			0x0002
#define PORT_VLAN_MEMBERSHIP		0x0007
#define PORT_FORCE_FLOW_CTRL		0x1000
#define PORT_DEFAULT_VID		0x0001
#define PORT_HP_MDIX			0x8000
#define PORT_REVERSED_POLARITY		0x2000
#define PORT_CABLE_10M_SHORT		0x8000
#define PORT_FORCE_LINK			0x0800
#define PORT_PHY_REMOTE_LOOPBACK	0x0200
#define PORT_FORCE_100_MBIT		0x0040
#define PORT_FORCE_FULL_DUPLEX		0x0020
#define PORT_AUTO_MDIX_DISABLE		0x0400
#define PORT_FORCE_MDIX			0x0200
#define PORT_MDIX_STATUS		0x0080
#define PORT_AUTO_NEG_COMPLETE		0x0040
#define TABLE_READ			0x10
#define PHY_START_CABLE_DIAG		0x8000
#define PHY_CABLE_DIAG_RESULT		0x6000
#define PHY_CABLE_STAT_NORMAL		0x0000
#define PHY_CABLE_STAT_OPEN		0x2000
#define PHY_CABLE_STAT_SHORT		0x4000
#define PHY_CABLE_STAT_FAILED		0x6000
#define PHY_CABLE_10M_SHORT		0x1000
#define PHY_STAT_REVERSED_POLARITY	0x0020
#define PHY_STAT_MDIX			0x0010
#define PHY_FORCE_LINK			0x0008
#define PHY_REMOTE_LOOPBACK		0x0002
#define MIB_COUNTER_VALUE		0x3FFFFFFF
#define MIB_PACKET_DROPPED		0x0000FFFF
#define PORT_MASK			7
#define MAX_ETHERNET_PACKET_SIZE	\
	(MAX_ETHERNET_BODY_SIZE + ETHERNET_HEADER_SIZE)
#define EEPROM_SIZE			0x40
#define EEPROM_MAGIC			0x10A18842
#define DMA_START			0x00000001
#define MAX_RX_BUF_SIZE			(1912 + 4)
#define KS_DMA_TX_CTRL			0x0000
#define DMA_TX_ENABLE			0x00000001
#define DMA_TX_CRC_ENABLE		0x00000002
#define DMA_TX_PAD_ENABLE		0x00000004
#define DMA_TX_LOOPBACK			0x00000100
#define DMA_TX_FLOW_ENABLE		0x00000200
#define DMA_TX_CSUM_IP			0x00010000
#define DMA_TX_CSUM_TCP			0x00020000
#define DMA_TX_CSUM_UDP			0x00040000
#define DMA_TX_BURST_SIZE		0x3F000000
#define KS_DMA_RX_CTRL			0x0004
#define DMA_RX_ENABLE			0x00000001
#define KS884X_DMA_RX_MULTICAST		0x00000002
#define DMA_RX_PROMISCUOUS		0x00000004
#define DMA_RX_ERROR			0x00000008
#define DMA_RX_UNICAST			0x00000010
#define DMA_RX_ALL_MULTICAST		0x00000020
#define DMA_RX_BROADCAST		0x00000040
#define DMA_RX_FLOW_ENABLE		0x00000200
#define DMA_RX_CSUM_IP			0x00010000
#define DMA_RX_CSUM_TCP			0x00020000
#define DMA_RX_CSUM_UDP			0x00040000
#define DMA_RX_BURST_SIZE		0x3F000000
#define DMA_BURST_SHIFT			24
#define DMA_BURST_DEFAULT		8
#define KS_DMA_TX_START			0x0008
#define KS_DMA_RX_START			0x000C
#define KS_DMA_TX_ADDR			0x0010
#define KS_DMA_RX_ADDR			0x0014
#define DMA_ADDR_LIST_MASK		0xFFFFFFFC
#define DMA_ADDR_LIST_SHIFT		2
#define KS884X_MULTICAST_0_OFFSET	0x0020
#define KS884X_MULTICAST_1_OFFSET	0x0021
#define KS884X_MULTICAST_2_OFFSET	0x0022
#define KS884x_MULTICAST_3_OFFSET	0x0023
#define KS884X_MULTICAST_4_OFFSET	0x0024
#define KS884X_MULTICAST_5_OFFSET	0x0025
#define KS884X_MULTICAST_6_OFFSET	0x0026
#define KS884X_MULTICAST_7_OFFSET	0x0027
#define KS884X_INTERRUPTS_ENABLE	0x0028
#define KS884X_INTERRUPTS_STATUS	0x002C
#define KS884X_INT_RX_STOPPED		0x02000000
#define KS884X_INT_TX_STOPPED		0x04000000
#define KS884X_INT_RX_OVERRUN		0x08000000
#define KS884X_INT_TX_EMPTY		0x10000000
#define KS884X_INT_RX			0x20000000
#define KS884X_INT_TX			0x40000000
#define KS884X_INT_PHY			0x80000000
#define KS884X_INT_RX_MASK		\
	(KS884X_INT_RX | KS884X_INT_RX_OVERRUN)
#define KS884X_INT_TX_MASK		\
	(KS884X_INT_TX | KS884X_INT_TX_EMPTY)
#define KS884X_INT_MASK	(KS884X_INT_RX | KS884X_INT_TX | KS884X_INT_PHY)
#define KS_ADD_ADDR_0_LO		0x0080
#define KS_ADD_ADDR_0_HI		0x0084
#define KS_ADD_ADDR_1_LO		0x0088
#define KS_ADD_ADDR_1_HI		0x008C
#define KS_ADD_ADDR_2_LO		0x0090
#define KS_ADD_ADDR_2_HI		0x0094
#define KS_ADD_ADDR_3_LO		0x0098
#define KS_ADD_ADDR_3_HI		0x009C
#define KS_ADD_ADDR_4_LO		0x00A0
#define KS_ADD_ADDR_4_HI		0x00A4
#define KS_ADD_ADDR_5_LO		0x00A8
#define KS_ADD_ADDR_5_HI		0x00AC
#define KS_ADD_ADDR_6_LO		0x00B0
#define KS_ADD_ADDR_6_HI		0x00B4
#define KS_ADD_ADDR_7_LO		0x00B8
#define KS_ADD_ADDR_7_HI		0x00BC
#define KS_ADD_ADDR_8_LO		0x00C0
#define KS_ADD_ADDR_8_HI		0x00C4
#define KS_ADD_ADDR_9_LO		0x00C8
#define KS_ADD_ADDR_9_HI		0x00CC
#define KS_ADD_ADDR_A_LO		0x00D0
#define KS_ADD_ADDR_A_HI		0x00D4
#define KS_ADD_ADDR_B_LO		0x00D8
#define KS_ADD_ADDR_B_HI		0x00DC
#define KS_ADD_ADDR_C_LO		0x00E0
#define KS_ADD_ADDR_C_HI		0x00E4
#define KS_ADD_ADDR_D_LO		0x00E8
#define KS_ADD_ADDR_D_HI		0x00EC
#define KS_ADD_ADDR_E_LO		0x00F0
#define KS_ADD_ADDR_E_HI		0x00F4
#define KS_ADD_ADDR_F_LO		0x00F8
#define KS_ADD_ADDR_F_HI		0x00FC
#define ADD_ADDR_HI_MASK		0x0000FFFF
#define ADD_ADDR_ENABLE			0x80000000
#define ADD_ADDR_INCR			8
#define KS884X_ADDR_0_OFFSET		0x0200
#define KS884X_ADDR_1_OFFSET		0x0201
#define KS884X_ADDR_2_OFFSET		0x0202
#define KS884X_ADDR_3_OFFSET		0x0203
#define KS884X_ADDR_4_OFFSET		0x0204
#define KS884X_ADDR_5_OFFSET		0x0205
#define KS884X_BUS_CTRL_OFFSET		0x0210
#define BUS_SPEED_125_MHZ		0x0000
#define BUS_SPEED_62_5_MHZ		0x0001
#define BUS_SPEED_41_66_MHZ		0x0002
#define BUS_SPEED_25_MHZ		0x0003
#define KS884X_EEPROM_CTRL_OFFSET	0x0212
#define EEPROM_CHIP_SELECT		0x0001
#define EEPROM_SERIAL_CLOCK		0x0002
#define EEPROM_DATA_OUT			0x0004
#define EEPROM_DATA_IN			0x0008
#define EEPROM_ACCESS_ENABLE		0x0010
#define KS884X_MEM_INFO_OFFSET		0x0214
#define RX_MEM_TEST_FAILED		0x0008
#define RX_MEM_TEST_FINISHED		0x0010
#define TX_MEM_TEST_FAILED		0x0800
#define TX_MEM_TEST_FINISHED		0x1000
#define KS884X_GLOBAL_CTRL_OFFSET	0x0216
#define GLOBAL_SOFTWARE_RESET		0x0001
#define KS8841_POWER_MANAGE_OFFSET	0x0218
#define KS8841_WOL_CTRL_OFFSET		0x021A
#define KS8841_WOL_MAGIC_ENABLE		0x0080
#define KS8841_WOL_FRAME3_ENABLE	0x0008
#define KS8841_WOL_FRAME2_ENABLE	0x0004
#define KS8841_WOL_FRAME1_ENABLE	0x0002
#define KS8841_WOL_FRAME0_ENABLE	0x0001
#define KS8841_WOL_FRAME_CRC_OFFSET	0x0220
#define KS8841_WOL_FRAME_BYTE0_OFFSET	0x0224
#define KS8841_WOL_FRAME_BYTE2_OFFSET	0x0228
#define KS884X_IACR_P			0x04A0
#define KS884X_IACR_OFFSET		KS884X_IACR_P
#define KS884X_IADR1_P			0x04A2
#define KS884X_IADR2_P			0x04A4
#define KS884X_IADR3_P			0x04A6
#define KS884X_IADR4_P			0x04A8
#define KS884X_IADR5_P			0x04AA
#define KS884X_ACC_CTRL_SEL_OFFSET	KS884X_IACR_P
#define KS884X_ACC_CTRL_INDEX_OFFSET	(KS884X_ACC_CTRL_SEL_OFFSET + 1)
#define KS884X_ACC_DATA_0_OFFSET	KS884X_IADR4_P
#define KS884X_ACC_DATA_1_OFFSET	(KS884X_ACC_DATA_0_OFFSET + 1)
#define KS884X_ACC_DATA_2_OFFSET	KS884X_IADR5_P
#define KS884X_ACC_DATA_3_OFFSET	(KS884X_ACC_DATA_2_OFFSET + 1)
#define KS884X_ACC_DATA_4_OFFSET	KS884X_IADR2_P
#define KS884X_ACC_DATA_5_OFFSET	(KS884X_ACC_DATA_4_OFFSET + 1)
#define KS884X_ACC_DATA_6_OFFSET	KS884X_IADR3_P
#define KS884X_ACC_DATA_7_OFFSET	(KS884X_ACC_DATA_6_OFFSET + 1)
#define KS884X_ACC_DATA_8_OFFSET	KS884X_IADR1_P
#define KS884X_P1MBCR_P			0x04D0
#define KS884X_P1MBSR_P			0x04D2
#define KS884X_PHY1ILR_P		0x04D4
#define KS884X_PHY1IHR_P		0x04D6
#define KS884X_P1ANAR_P			0x04D8
#define KS884X_P1ANLPR_P		0x04DA
#define KS884X_P2MBCR_P			0x04E0
#define KS884X_P2MBSR_P			0x04E2
#define KS884X_PHY2ILR_P		0x04E4
#define KS884X_PHY2IHR_P		0x04E6
#define KS884X_P2ANAR_P			0x04E8
#define KS884X_P2ANLPR_P		0x04EA
#define KS884X_PHY_1_CTRL_OFFSET	KS884X_P1MBCR_P
#define PHY_CTRL_INTERVAL		(KS884X_P2MBCR_P - KS884X_P1MBCR_P)
#define KS884X_PHY_CTRL_OFFSET		0x00
#define KS884X_PHY_STATUS_OFFSET	0x02
#define KS884X_PHY_ID_1_OFFSET		0x04
#define KS884X_PHY_ID_2_OFFSET		0x06
#define KS884X_PHY_AUTO_NEG_OFFSET	0x08
#define KS884X_PHY_REMOTE_CAP_OFFSET	0x0A
#define KS884X_P1VCT_P			0x04F0
#define KS884X_P1PHYCTRL_P		0x04F2
#define KS884X_P2VCT_P			0x04F4
#define KS884X_P2PHYCTRL_P		0x04F6
#define KS884X_PHY_SPECIAL_OFFSET	KS884X_P1VCT_P
#define PHY_SPECIAL_INTERVAL		(KS884X_P2VCT_P - KS884X_P1VCT_P)
#define KS884X_PHY_LINK_MD_OFFSET	0x00
#define PHY_CABLE_FAULT_COUNTER		0x01FF
#define KS884X_PHY_PHY_CTRL_OFFSET	0x02
#define PHY_POWER_SAVING_DISABLE	0x0004
#define KS884X_SIDER_P			0x0400
#define KS884X_CHIP_ID_OFFSET		KS884X_SIDER_P
#define KS884X_FAMILY_ID_OFFSET		(KS884X_CHIP_ID_OFFSET + 1)
#define REG_FAMILY_ID			0x88
#define REG_CHIP_ID_41			0x8810
#define REG_CHIP_ID_42			0x8800
#define KS884X_CHIP_ID_MASK_41		0xFF10
#define KS884X_CHIP_ID_MASK		0xFFF0
#define KS884X_CHIP_ID_SHIFT		4
#define KS884X_REVISION_MASK		0x000E
#define KS884X_REVISION_SHIFT		1
#define KS8842_START			0x0001
#define CHIP_IP_41_M			0x8810
#define CHIP_IP_42_M			0x8800
#define CHIP_IP_61_M			0x8890
#define CHIP_IP_62_M			0x8880
#define CHIP_IP_41_P			0x8850
#define CHIP_IP_42_P			0x8840
#define CHIP_IP_61_P			0x88D0
#define CHIP_IP_62_P			0x88C0
#define KS8842_SGCR1_P			0x0402
#define KS8842_SWITCH_CTRL_1_OFFSET	KS8842_SGCR1_P
#define SWITCH_PASS_ALL			0x8000
#define SWITCH_TX_FLOW_CTRL		0x2000
#define SWITCH_RX_FLOW_CTRL		0x1000
#define SWITCH_CHECK_LENGTH		0x0800
#define SWITCH_AGING_ENABLE		0x0400
#define SWITCH_FAST_AGING		0x0200
#define SWITCH_AGGR_BACKOFF		0x0100
#define SWITCH_PASS_PAUSE		0x0008
#define SWITCH_LINK_AUTO_AGING		0x0001
#define KS8842_SGCR2_P			0x0404
#define KS8842_SWITCH_CTRL_2_OFFSET	KS8842_SGCR2_P
#define SWITCH_VLAN_ENABLE		0x8000
#define SWITCH_IGMP_SNOOP		0x4000
#define IPV6_MLD_SNOOP_ENABLE		0x2000
#define IPV6_MLD_SNOOP_OPTION		0x1000
#define PRIORITY_SCHEME_SELECT		0x0800
#define SWITCH_MIRROR_RX_TX		0x0100
#define SWITCH_BACK_PRESSURE		0x0020
#define SWITCH_HUGE_PACKET		0x0004
#define SWITCH_LEGAL_PACKET		0x0002
#define SWITCH_BUF_RESERVE		0x0001
#define KS8842_SGCR3_P			0x0406
#define KS8842_SWITCH_CTRL_3_OFFSET	KS8842_SGCR3_P
#define SWITCH_REPEATER			0x0080
#define SWITCH_HALF_DUPLEX		0x0040
#define SWITCH_FLOW_CTRL		0x0020
#define SWITCH_10_MBIT			0x0010
#define SWITCH_REPLACE_NULL_VID		0x0008
#define KS8842_SGCR4_P			0x0408
#define KS8842_SGCR5_P			0x040A
#define KS8842_SWITCH_CTRL_5_OFFSET	KS8842_SGCR5_P
#define LED_MODE			0x8200
#define LED_SPEED_DUPLEX_ACT		0x0000
#define LED_SPEED_DUPLEX_LINK_ACT	0x8000
#define LED_DUPLEX_10_100		0x0200
#define KS8842_SGCR6_P			0x0410
#define KS8842_SWITCH_CTRL_6_OFFSET	KS8842_SGCR6_P
#define KS8842_PRIORITY_MASK		3
#define KS8842_PRIORITY_SHIFT		2
#define KS8842_SGCR7_P			0x0412
#define KS8842_SWITCH_CTRL_7_OFFSET	KS8842_SGCR7_P
#define SWITCH_UNK_DEF_PORT_ENABLE	0x0008
#define SWITCH_UNK_DEF_PORT_3		0x0004
#define SWITCH_UNK_DEF_PORT_2		0x0002
#define SWITCH_UNK_DEF_PORT_1		0x0001
#define KS8842_MACAR1_P			0x0470
#define KS8842_MACAR2_P			0x0472
#define KS8842_MACAR3_P			0x0474
#define KS8842_MAC_ADDR_1_OFFSET	KS8842_MACAR1_P
#define KS8842_MAC_ADDR_0_OFFSET	(KS8842_MAC_ADDR_1_OFFSET + 1)
#define KS8842_MAC_ADDR_3_OFFSET	KS8842_MACAR2_P
#define KS8842_MAC_ADDR_2_OFFSET	(KS8842_MAC_ADDR_3_OFFSET + 1)
#define KS8842_MAC_ADDR_5_OFFSET	KS8842_MACAR3_P
#define KS8842_MAC_ADDR_4_OFFSET	(KS8842_MAC_ADDR_5_OFFSET + 1)
#define KS8842_TOSR1_P			0x0480
#define KS8842_TOSR2_P			0x0482
#define KS8842_TOSR3_P			0x0484
#define KS8842_TOSR4_P			0x0486
#define KS8842_TOSR5_P			0x0488
#define KS8842_TOSR6_P			0x048A
#define KS8842_TOSR7_P			0x0490
#define KS8842_TOSR8_P			0x0492
#define KS8842_TOS_1_OFFSET		KS8842_TOSR1_P
#define KS8842_TOS_2_OFFSET		KS8842_TOSR2_P
#define KS8842_TOS_3_OFFSET		KS8842_TOSR3_P
#define KS8842_TOS_4_OFFSET		KS8842_TOSR4_P
#define KS8842_TOS_5_OFFSET		KS8842_TOSR5_P
#define KS8842_TOS_6_OFFSET		KS8842_TOSR6_P
#define KS8842_TOS_7_OFFSET		KS8842_TOSR7_P
#define KS8842_TOS_8_OFFSET		KS8842_TOSR8_P
#define KS8842_P1CR1_P			0x0500
#define KS8842_P1CR2_P			0x0502
#define KS8842_P1VIDR_P			0x0504
#define KS8842_P1CR3_P			0x0506
#define KS8842_P1IRCR_P			0x0508
#define KS8842_P1ERCR_P			0x050A
#define KS884X_P1SCSLMD_P		0x0510
#define KS884X_P1CR4_P			0x0512
#define KS884X_P1SR_P			0x0514
#define KS8842_P2CR1_P			0x0520
#define KS8842_P2CR2_P			0x0522
#define KS8842_P2VIDR_P			0x0524
#define KS8842_P2CR3_P			0x0526
#define KS8842_P2IRCR_P			0x0528
#define KS8842_P2ERCR_P			0x052A
#define KS884X_P2SCSLMD_P		0x0530
#define KS884X_P2CR4_P			0x0532
#define KS884X_P2SR_P			0x0534
#define KS8842_P3CR1_P			0x0540
#define KS8842_P3CR2_P			0x0542
#define KS8842_P3VIDR_P			0x0544
#define KS8842_P3CR3_P			0x0546
#define KS8842_P3IRCR_P			0x0548
#define KS8842_P3ERCR_P			0x054A
#define KS8842_PORT_1_CTRL_1		KS8842_P1CR1_P
#define KS8842_PORT_2_CTRL_1		KS8842_P2CR1_P
#define KS8842_PORT_3_CTRL_1		KS8842_P3CR1_P
#define KS8842_PORT_CTRL_1_OFFSET	0x00
#define PORT_BASED_PRIORITY_MASK	0x0018
#define PORT_BASED_PRIORITY_BASE	0x0003
#define PORT_BASED_PRIORITY_SHIFT	3
#define PORT_BASED_PRIORITY_0		0x0000
#define PORT_BASED_PRIORITY_1		0x0008
#define PORT_BASED_PRIORITY_2		0x0010
#define PORT_BASED_PRIORITY_3		0x0018
#define PORT_PRIO_QUEUE_ENABLE		0x0001
#define KS8842_PORT_CTRL_2_OFFSET	0x02
#define PORT_INGRESS_VLAN_FILTER	0x4000
#define PORT_USER_PRIORITY_CEILING	0x0008
#define KS8842_PORT_CTRL_VID_OFFSET	0x04
#define KS8842_PORT_CTRL_3_OFFSET	0x06
#define PORT_INGRESS_LIMIT_MODE		0x000C
#define PORT_INGRESS_ALL		0x0000
#define PORT_INGRESS_UNICAST		0x0004
#define PORT_INGRESS_MULTICAST		0x0008
#define PORT_INGRESS_BROADCAST		0x000C
#define KS8842_PORT_IN_RATE_OFFSET	0x08
#define KS8842_PORT_OUT_RATE_OFFSET	0x0A
#define PORT_PRIORITY_RATE		0x0F
#define PORT_PRIORITY_RATE_SHIFT	4
#define KS884X_PORT_LINK_MD		0x10
#define PORT_CABLE_DIAG_RESULT		0x6000
#define PORT_POWER_SAVING_DISABLE	0x0400
#define KS884X_PORT_CTRL_4_OFFSET	0x12
#define PORT_REMOTE_FAULT_DISABLE	0x1000
#define PORT_LOOPBACK			0x0100
#define KS884X_PORT_STATUS_OFFSET	0x14
#define PORT_STATUS_SPEED_100MBIT	0x0400
#define PORT_STATUS_FULL_DUPLEX		0x0200
#define PORT_STATUS_LINK_GOOD		0x0020
#define STATIC_MAC_TABLE_ADDR		0x0000FFFF
#define STATIC_MAC_TABLE_FWD_PORTS	0x00070000
#define STATIC_MAC_TABLE_VALID		0x00080000
#define STATIC_MAC_TABLE_OVERRIDE	0x00100000
#define STATIC_MAC_TABLE_USE_FID	0x00200000
#define STATIC_MAC_TABLE_FID		0x03C00000
#define STATIC_MAC_FWD_PORTS_SHIFT	16
#define STATIC_MAC_FID_SHIFT		22
#define VLAN_TABLE_VID			0x00000FFF
#define VLAN_TABLE_FID			0x0000F000
#define VLAN_TABLE_MEMBERSHIP		0x00070000
#define VLAN_TABLE_VALID		0x00080000
#define VLAN_TABLE_FID_SHIFT		12
#define VLAN_TABLE_MEMBERSHIP_SHIFT	16
#define DYNAMIC_MAC_TABLE_ADDR		0x0000FFFF
#define DYNAMIC_MAC_TABLE_FID		0x000F0000
#define DYNAMIC_MAC_TABLE_SRC_PORT	0x00300000
#define DYNAMIC_MAC_TABLE_TIMESTAMP	0x00C00000
#define DYNAMIC_MAC_TABLE_ENTRIES	0xFF000000
#define DYNAMIC_MAC_TABLE_ENTRIES_H	0x03
#define DYNAMIC_MAC_TABLE_MAC_EMPTY	0x04
#define DYNAMIC_MAC_TABLE_RESERVED	0x78
#define DYNAMIC_MAC_TABLE_NOT_READY	0x80
#define DYNAMIC_MAC_FID_SHIFT		16
#define DYNAMIC_MAC_SRC_PORT_SHIFT	20
#define DYNAMIC_MAC_TIMESTAMP_SHIFT	22
#define DYNAMIC_MAC_ENTRIES_SHIFT	24
#define DYNAMIC_MAC_ENTRIES_H_SHIFT	8
#define MIB_COUNTER_VALID		0x40000000
#define MIB_COUNTER_OVERFLOW		0x80000000
#define KS_MIB_PACKET_DROPPED_TX_0	0x100
#define KS_MIB_PACKET_DROPPED_TX_1	0x101
#define KS_MIB_PACKET_DROPPED_TX	0x102
#define KS_MIB_PACKET_DROPPED_RX_0	0x103
#define KS_MIB_PACKET_DROPPED_RX_1	0x104
#define KS_MIB_PACKET_DROPPED_RX	0x105
#define SET_DEFAULT_LED			LED_SPEED_DUPLEX_ACT
#define MAC_ADDR_ORDER(i)		(ETH_ALEN - 1 - (i))
#define MAX_ETHERNET_BODY_SIZE		1500
#define ETHERNET_HEADER_SIZE		(14 + VLAN_HLEN)
#define REGULAR_RX_BUF_SIZE		(MAX_ETHERNET_PACKET_SIZE + 4)
#define ADDITIONAL_ENTRIES		16
#define MAX_MULTICAST_LIST		32
#define HW_MULTICAST_SIZE		8
#define HW_TO_DEV_PORT(port)		(port - 1)
#define DESC_ALIGNMENT			16
#define BUFFER_ALIGNMENT		8
#define NUM_OF_RX_DESC			64
#define NUM_OF_TX_DESC			64
#define KS_DESC_RX_FRAME_LEN		0x000007FF
#define KS_DESC_RX_FRAME_TYPE		0x00008000
#define KS_DESC_RX_ERROR_CRC		0x00010000
#define KS_DESC_RX_ERROR_RUNT		0x00020000
#define KS_DESC_RX_ERROR_TOO_LONG	0x00040000
#define KS_DESC_RX_ERROR_PHY		0x00080000
#define KS884X_DESC_RX_PORT_MASK	0x00300000
#define KS_DESC_RX_MULTICAST		0x01000000
#define KS_DESC_RX_ERROR		0x02000000
#define KS_DESC_RX_ERROR_CSUM_UDP	0x04000000
#define KS_DESC_RX_ERROR_CSUM_TCP	0x08000000
#define KS_DESC_RX_ERROR_CSUM_IP	0x10000000
#define KS_DESC_RX_LAST			0x20000000
#define KS_DESC_RX_FIRST		0x40000000
#define KS_DESC_RX_ERROR_COND		\
	(KS_DESC_RX_ERROR_CRC |		\
	KS_DESC_RX_ERROR_RUNT |		\
	KS_DESC_RX_ERROR_PHY |		\
	KS_DESC_RX_ERROR_TOO_LONG)
#define KS_DESC_HW_OWNED		0x80000000
#define KS_DESC_BUF_SIZE		0x000007FF
#define KS884X_DESC_TX_PORT_MASK	0x00300000
#define KS_DESC_END_OF_RING		0x02000000
#define KS_DESC_TX_CSUM_GEN_UDP		0x04000000
#define KS_DESC_TX_CSUM_GEN_TCP		0x08000000
#define KS_DESC_TX_CSUM_GEN_IP		0x10000000
#define KS_DESC_TX_LAST			0x20000000
#define KS_DESC_TX_FIRST		0x40000000
#define KS_DESC_TX_INTERRUPT		0x80000000
#define KS_DESC_PORT_SHIFT		20
#define KS_DESC_RX_MASK			(KS_DESC_BUF_SIZE)
#define KS_DESC_TX_MASK			\
	(KS_DESC_TX_INTERRUPT |		\
	KS_DESC_TX_FIRST |		\
	KS_DESC_TX_LAST |		\
	KS_DESC_TX_CSUM_GEN_IP |	\
	KS_DESC_TX_CSUM_GEN_TCP |	\
	KS_DESC_TX_CSUM_GEN_UDP |	\
	KS_DESC_BUF_SIZE)
#define DMA_BUFFER(desc)  ((struct ksz_dma_buf *)(&(desc)->dma_buf))
#define LEARNED_MAC_TABLE_ENTRIES	1024
#define STATIC_MAC_TABLE_ENTRIES	8
#define VLAN_TABLE_ENTRIES		16
#define DIFFSERV_ENTRIES		64
#define PRIO_802_1P_ENTRIES		8
#define SWITCH_PORT_NUM			2
#define MAIN_PORT			0
#define OTHER_PORT			1
#define PORT_COUNTER_NUM		0x20
#define TOTAL_PORT_COUNTER_NUM		(PORT_COUNTER_NUM + 2)
#define MIB_COUNTER_RX_LO_PRIORITY	0x00
#define MIB_COUNTER_RX_HI_PRIORITY	0x01
#define MIB_COUNTER_RX_UNDERSIZE	0x02
#define MIB_COUNTER_RX_FRAGMENT		0x03
#define MIB_COUNTER_RX_OVERSIZE		0x04
#define MIB_COUNTER_RX_JABBER		0x05
#define MIB_COUNTER_RX_SYMBOL_ERR	0x06
#define MIB_COUNTER_RX_CRC_ERR		0x07
#define MIB_COUNTER_RX_ALIGNMENT_ERR	0x08
#define MIB_COUNTER_RX_CTRL_8808	0x09
#define MIB_COUNTER_RX_PAUSE		0x0A
#define MIB_COUNTER_RX_BROADCAST	0x0B
#define MIB_COUNTER_RX_MULTICAST	0x0C
#define MIB_COUNTER_RX_UNICAST		0x0D
#define MIB_COUNTER_RX_OCTET_64		0x0E
#define MIB_COUNTER_RX_OCTET_65_127	0x0F
#define MIB_COUNTER_RX_OCTET_128_255	0x10
#define MIB_COUNTER_RX_OCTET_256_511	0x11
#define MIB_COUNTER_RX_OCTET_512_1023	0x12
#define MIB_COUNTER_RX_OCTET_1024_1522	0x13
#define MIB_COUNTER_TX_LO_PRIORITY	0x14
#define MIB_COUNTER_TX_HI_PRIORITY	0x15
#define MIB_COUNTER_TX_LATE_COLLISION	0x16
#define MIB_COUNTER_TX_PAUSE		0x17
#define MIB_COUNTER_TX_BROADCAST	0x18
#define MIB_COUNTER_TX_MULTICAST	0x19
#define MIB_COUNTER_TX_UNICAST		0x1A
#define MIB_COUNTER_TX_DEFERRED		0x1B
#define MIB_COUNTER_TX_TOTAL_COLLISION	0x1C
#define MIB_COUNTER_TX_EXCESS_COLLISION	0x1D
#define MIB_COUNTER_TX_SINGLE_COLLISION	0x1E
#define MIB_COUNTER_TX_MULTI_COLLISION	0x1F
#define MIB_COUNTER_RX_DROPPED_PACKET	0x20
#define MIB_COUNTER_TX_DROPPED_PACKET	0x21
#define TX_RATE_UNIT			10000
#define MAX_TX_HELD_SIZE		52000
#define LINK_INT_WORKING		(1 << 0)
#define SMALL_PACKET_TX_BUG		(1 << 1)
#define HALF_DUPLEX_SIGNAL_BUG		(1 << 2)
#define RX_HUGE_FRAME			(1 << 4)
#define STP_SUPPORT			(1 << 8)
#define PAUSE_FLOW_CTRL			(1 << 0)
#define FAST_AGING			(1 << 1)
#define TABLE_SEL_SHIFT			2
#define HW_DELAY(hw, reg)			\
	do {					\
		readw(hw->io + reg);		\
	} while (0)
#define BROADCAST_STORM_PROTECTION_RATE	10
#define STP_ENTRY			0
#define BROADCAST_ENTRY			1
#define BRIDGE_ADDR_ENTRY		2
#define IPV6_ADDR_ENTRY			3
#define AT93C_CODE			0
#define AT93C_WR_OFF			0x00
#define AT93C_WR_ALL			0x10
#define AT93C_ER_ALL			0x20
#define AT93C_WR_ON			0x30
#define AT93C_WRITE			1
#define AT93C_READ			2
#define AT93C_ERASE			3
#define EEPROM_DELAY			4
#define EEPROM_DATA_RESERVED		0
#define EEPROM_DATA_MAC_ADDR_0		1
#define EEPROM_DATA_MAC_ADDR_1		2
#define EEPROM_DATA_MAC_ADDR_2		3
#define EEPROM_DATA_SUBSYS_ID		4
#define EEPROM_DATA_SUBSYS_VEN_ID	5
#define EEPROM_DATA_PM_CAP		6
#define EEPROM_DATA_OTHER_MAC_ADDR	9
#define PHY_RESET_TIMEOUT		10
#define MIN_DESC_SHIFT  2
#define ADVERTISED_ALL			\
	(ADVERTISED_10baseT_Half |	\
	ADVERTISED_10baseT_Full |	\
	ADVERTISED_100baseT_Half |	\
	ADVERTISED_100baseT_Full)
#define WOL_SUPPORT			\
	(WAKE_PHY | WAKE_MAGIC |	\
	WAKE_UCAST | WAKE_MCAST |	\
	WAKE_BCAST | WAKE_ARP)
#define STATS_LEN			(TOTAL_PORT_COUNTER_NUM)
#define KS884X_DMA_MASK			(~0x0UL)

struct ksz_desc_rx_stat {
#ifdef __BIG_ENDIAN_BITFIELD
	u32 hw_owned:1;
	u32 first_desc:1;
	u32 last_desc:1;
	u32 csum_err_ip:1;
	u32 csum_err_tcp:1;
	u32 csum_err_udp:1;
	u32 error:1;
	u32 multicast:1;
	u32 src_port:4;
	u32 err_phy:1;
	u32 err_too_long:1;
	u32 err_runt:1;
	u32 err_crc:1;
	u32 frame_type:1;
	u32 reserved1:4;
	u32 frame_len:11;
#else
	u32 frame_len:11;
	u32 reserved1:4;
	u32 frame_type:1;
	u32 err_crc:1;
	u32 err_runt:1;
	u32 err_too_long:1;
	u32 err_phy:1;
	u32 src_port:4;
	u32 multicast:1;
	u32 error:1;
	u32 csum_err_udp:1;
	u32 csum_err_tcp:1;
	u32 csum_err_ip:1;
	u32 last_desc:1;
	u32 first_desc:1;
	u32 hw_owned:1;
#endif
};

struct ksz_desc_tx_stat {
#ifdef __BIG_ENDIAN_BITFIELD
	u32 hw_owned:1;
	u32 reserved1:31;
#else
	u32 reserved1:31;
	u32 hw_owned:1;
#endif
};

struct ksz_desc_rx_buf {
#ifdef __BIG_ENDIAN_BITFIELD
	u32 reserved4:6;
	u32 end_of_ring:1;
	u32 reserved3:14;
	u32 buf_size:11;
#else
	u32 buf_size:11;
	u32 reserved3:14;
	u32 end_of_ring:1;
	u32 reserved4:6;
#endif
};

struct ksz_desc_tx_buf {
#ifdef __BIG_ENDIAN_BITFIELD
	u32 intr:1;
	u32 first_seg:1;
	u32 last_seg:1;
	u32 csum_gen_ip:1;
	u32 csum_gen_tcp:1;
	u32 csum_gen_udp:1;
	u32 end_of_ring:1;
	u32 reserved4:1;
	u32 dest_port:4;
	u32 reserved3:9;
	u32 buf_size:11;
#else
	u32 buf_size:11;
	u32 reserved3:9;
	u32 dest_port:4;
	u32 reserved4:1;
	u32 end_of_ring:1;
	u32 csum_gen_udp:1;
	u32 csum_gen_tcp:1;
	u32 csum_gen_ip:1;
	u32 last_seg:1;
	u32 first_seg:1;
	u32 intr:1;
#endif
};

union desc_stat {
	struct ksz_desc_rx_stat rx;
	struct ksz_desc_tx_stat tx;
	u32 data;
};

union desc_buf {
	struct ksz_desc_rx_buf rx;
	struct ksz_desc_tx_buf tx;
	u32 data;
};

struct ksz_hw_desc {
	union desc_stat ctrl;
	union desc_buf buf;
	u32 addr;
	u32 next;
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
    uint16_t chip_id;
    uint16_t family_id;

    /* DMA Context */
    uint32_t dma_tx_ctrl;
    uint32_t dma_rx_ctrl;
    uint32_t dma_tx_addr;
    uint32_t dma_rx_addr;
    uint32_t dma_tx_start;
    uint32_t dma_rx_start;

    uint32_t status;      /* Operational status flags */
    bool in_reset;      /* State used to handle reset sequences */
    uint16_t power_mgmt;     /* Power management state (D0-D3) */

    /* Additional shadow registers for functional emulation */
    uint8_t mac_addr[6];     /* Node MAC address at 0x0200-0x0205 */
    uint16_t bus_ctrl;       /* KS884X_BUS_CTRL_OFFSET */
    uint16_t global_ctrl;    /* KS884X_GLOBAL_CTRL_OFFSET */
    uint32_t intr_enable;    /* KS884X_INTERRUPTS_ENABLE */
};

G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
}

G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case 0x0000: /* KS_DMA_TX_CTRL */
        if (size == 4) return s->dma_tx_ctrl;
        break;
    case 0x0004: /* KS_DMA_RX_CTRL */
        if (size == 4) return s->dma_rx_ctrl;
        break;
    case 0x0008: /* KS_DMA_TX_START */
        if (size == 4) return 0; /* Write-only, driver never reads */
        break;
    case 0x000C: /* KS_DMA_RX_START */
        if (size == 4) return 0;
        break;
    case 0x0010: /* KS_DMA_TX_ADDR */
        if (size == 4) return s->dma_tx_addr;
        break;
    case 0x0014: /* KS_DMA_RX_ADDR */
        if (size == 4) return s->dma_rx_addr;
        break;
    case 0x0028: /* KS884X_INTERRUPTS_ENABLE */
        if (size == 4) return s->intr_enable;
        break;
    case 0x002C: /* KS884X_INTERRUPTS_STATUS */
        if (size == 4) return s->intr_status;
        break;
    case 0x0200: /* KS884X_ADDR_0_OFFSET ... 0x0205 */
    case 0x0201:
    case 0x0202:
    case 0x0203:
    case 0x0204:
    case 0x0205:
        if (size == 1) {
            return s->mac_addr[addr - 0x0200];
        }
        break;
    case 0x0210: /* KS884X_BUS_CTRL_OFFSET */
        if (size == 2) return s->bus_ctrl;
        break;
    case 0x0212: /* KS884X_EEPROM_CTRL_OFFSET, simple return 0 */
        if (size == 2) return 0;
        break;
    case 0x0214: /* KS884X_MEM_INFO_OFFSET */
        if (size == 2) return 0;
        break;
    case 0x0216: /* KS884X_GLOBAL_CTRL_OFFSET */
        if (size == 2) return s->global_ctrl;
        break;
    case 0x0218: /* KS8841_POWER_MANAGE_OFFSET */
        if (size == 2) return s->power_mgmt;
        break;
    case 0x0400: /* KS884X_CHIP_ID_OFFSET */
        if (size == 2) return s->chip_id;
        break;
    case 0x0401: /* KS884X_FAMILY_ID_OFFSET */
        if (size == 1) return s->family_id;
        break;
    default:
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case 0x0000: /* KS_DMA_TX_CTRL */
        if (size == 4) s->dma_tx_ctrl = val;
        break;
    case 0x0004: /* KS_DMA_RX_CTRL */
        if (size == 4) s->dma_rx_ctrl = val;
        break;
    case 0x0008: /* KS_DMA_TX_START, start TX, ignore */
        if (size == 4) { /* nothing */ }
        break;
    case 0x000C: /* KS_DMA_RX_START, start RX, ignore */
        if (size == 4) { /* nothing */ }
        break;
    case 0x0010: /* KS_DMA_TX_ADDR */
        if (size == 4) s->dma_tx_addr = val;
        break;
    case 0x0014: /* KS_DMA_RX_ADDR */
        if (size == 4) s->dma_rx_addr = val;
        break;
    case 0x0028: /* KS884X_INTERRUPTS_ENABLE */
        if (size == 4) s->intr_enable = val;
        break;
    case 0x002C: /* KS884X_INTERRUPTS_STATUS, write-1-to-clear */
        if (size == 4) s->intr_status &= ~val;
        break;
    case 0x0200:
    case 0x0201:
    case 0x0202:
    case 0x0203:
    case 0x0204:
    case 0x0205:
        if (size == 1) s->mac_addr[addr - 0x0200] = val;
        break;
    case 0x0210: /* KS884X_BUS_CTRL_OFFSET */
        if (size == 2) s->bus_ctrl = val;
        break;
    case 0x0212: /* KS884X_EEPROM_CTRL_OFFSET, ignore */
        if (size == 2) { /* ignore */ }
        break;
    case 0x0214: /* KS884X_MEM_INFO_OFFSET, ignore */
        break;
    case 0x0216: /* KS884X_GLOBAL_CTRL_OFFSET */
        if (size == 2) s->global_ctrl = val;
        break;
    case 0x0218: /* KS8841_POWER_MANAGE_OFFSET */
        if (size == 2) s->power_mgmt = val;
        break;
    case 0x0400: /* KS884X_CHIP_ID_OFFSET, also used for switch start, ignore */
        if (size == 2) { /* nothing */ }
        break;
    case 0x0401: /* KS884X_FAMILY_ID_OFFSET, read-only */
        break;
    default:
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    return 0;
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

    /* Initialize hardware identity */
    s->chip_id = 0x8810;   /* KSZ8841 */
    s->family_id = REG_FAMILY_ID;

    /* Set a default MAC address */
    s->mac_addr[0] = 0x00;
    s->mac_addr[1] = 0x12;
    s->mac_addr[2] = 0x34;
    s->mac_addr[3] = 0x56;
    s->mac_addr[4] = 0x78;
    s->mac_addr[5] = 0x9A;

    /* Clear all other registers */
    s->dma_tx_ctrl = 0;
    s->dma_rx_ctrl = 0;
    s->dma_tx_addr = 0;
    s->dma_rx_addr = 0;
    s->dma_tx_start = 0;
    s->dma_rx_start = 0;
    s->intr_enable = 0;
    s->intr_status = 0;
    s->bus_ctrl = 0;
    s->global_ctrl = 0;
    s->power_mgmt = 0;
    s->in_reset = false;
    s->status = 0;
    s->intr_mask = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x16C6);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x8841);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0200);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo) {
        .index = 0,
        .type = BAR_TYPE_MMIO,
        .size = BAR0_SIZE,
        .name = "bar0"
    };
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

static const VMStateDescription vmstate_pcibase = {
    .name = "ksz884xp_pci",
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
