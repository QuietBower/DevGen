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
#include "hw/net/mii.h"

#define TYPE_PCIBASE_DEVICE "bnx2_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_BROADCOM 0x14e4
#define PCI_DEVICE_ID_NX2_5706 0x164a

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
    uint32_t pcicfg_reg_window_address;
    uint32_t pcicfg_reg_window;
    uint32_t hc_command;
    uint32_t hc_config;
    uint32_t hc_attn_bits_enable;
    uint32_t hc_status_addr_l;
    uint32_t hc_status_addr_h;
    uint32_t hc_statistics_addr_l;
    uint32_t hc_statistics_addr_h;
    uint32_t hc_tx_quick_cons_trip;
    uint32_t hc_comp_prod_trip;
    uint32_t hc_rx_quick_cons_trip;
    uint32_t hc_rx_ticks;
    uint32_t hc_tx_ticks;
    uint32_t hc_com_ticks;
    uint32_t hc_cmd_ticks;
    uint32_t hc_stat_collect_ticks;
    uint32_t hc_stats_ticks;
    uint32_t hc_stats_interrupt_status;
    uint32_t misc_cfg;
    uint32_t misc_enable_set_bits;
    uint32_t misc_enable_clr_bits;
    uint32_t misc_command;
    uint32_t misc_id;
    uint32_t pcicfg_misc_config;
    uint32_t dma_config;
    uint32_t emac_mode;
    uint32_t emac_status;
    uint32_t emac_tx_mode;
    uint32_t emac_rx_mode;
    uint32_t emac_mdio_comm;
    uint32_t emac_mdio_mode;
    uint32_t pci_grc_window_addr;
    uint32_t pci_grc_window2_addr;
    uint32_t pci_grc_window3_addr;

    /* DMA Context */
    dma_addr_t status_blk_mapping;
    dma_addr_t stats_blk_mapping;
    dma_addr_t ctx_blk_mapping[4];
    dma_addr_t rx_desc_mapping[8];
    dma_addr_t tx_desc_mapping;

    uint32_t link_status;
};

#define BNX2_PCICFG_REG_WINDOW_ADDRESS 0x00000078
#define BNX2_PCICFG_REG_WINDOW 0x00000080
#define BNX2_CTX_CTX_DATA 0x00001020
#define BNX2_CTX_DATA_ADR 0x00001010
#define BNX2_CTX_DATA 0x00001014
#define BNX2_CTX_CTX_CTRL 0x0000101c
#define BNX2_EMAC_MDIO_COMM 0x000014ac
#define BNX2_EMAC_MDIO_MODE 0x000014b4
#define BNX2_HC_COMMAND 0x00006800
#define BNX2_LINK_STATUS 0x0000000c
#define BNX2_EMAC_STATUS 0x00001404
#define BNX2_EMAC_MODE 0x00001400
#define BNX2_EMAC_TX_MODE 0x000014bc
#define BNX2_EMAC_RX_MODE 0x000014c8
#define BNX2_MISC_GP_HW_CTL0 0x000008bc
#define BNX2_MISC_CFG 0x00000804
#define BNX2_MISC_ENABLE_SET_BITS 0x00000810
#define BNX2_MISC_ENABLE_CLR_BITS 0x00000814
#define BNX2_MISC_COMMAND 0x00000800
#define BNX2_MISC_ID 0x00000808
#define BNX2_PCICFG_MISC_CONFIG 0x00000068
#define BNX2_DMA_CONFIG 0x00000c08
#define BNX2_HC_CONFIG 0x00006808
#define BNX2_HC_STATUS_ADDR_L 0x00006810
#define BNX2_HC_STATUS_ADDR_H 0x00006814
#define BNX2_HC_STATISTICS_ADDR_L 0x00006818
#define BNX2_HC_STATISTICS_ADDR_H 0x0000681c
#define BNX2_HC_TX_QUICK_CONS_TRIP 0x00006820
#define BNX2_HC_COMP_PROD_TRIP 0x00006824
#define BNX2_HC_RX_QUICK_CONS_TRIP 0x00006828
#define BNX2_HC_RX_TICKS 0x0000682c
#define BNX2_HC_TX_TICKS 0x00006830
#define BNX2_HC_COM_TICKS 0x00006834
#define BNX2_HC_CMD_TICKS 0x00006838
#define BNX2_HC_STAT_COLLECT_TICKS 0x00006840
#define BNX2_HC_STATS_TICKS 0x00006844
#define BNX2_HC_STATS_INTERRUPT_STATUS 0x00006848
#define BNX2_HC_ATTN_BITS_ENABLE 0x0000680c
#define BNX2_PCI_GRC_WINDOW_ADDR 0x00000400
#define BNX2_PCI_GRC_WINDOW2_ADDR 0x00000614
#define BNX2_PCI_GRC_WINDOW3_ADDR 0x00000618
#define BNX2_PCI_MSIX_CONTROL 0x000004c0
#define BNX2_PCI_MSIX_TBL_OFF_BIR 0x000004c4
#define BNX2_PCI_MSIX_PBA_OFF_BIT 0x000004c8
#define BNX2_MSIX_TABLE_ADDR 0x318000
#define BNX2_MSIX_PBA_ADDR 0x31c000

/* Newly extracted macros */
#define BNX2_MCP_SCRATCH				0x00160000
#define BNX2_TX_DESC_CNT  (BNX2_PAGE_SIZE / sizeof(struct bnx2_tx_bd))
#define HZ __USER_HZ
#define MB_KERNEL_CTX_SHIFT         8

#define MB_GET_CID_ADDR(_cid)       (0x10000 + ((_cid) << MB_KERNEL_CTX_SHIFT))
#define TX_TSS_CID	32
#define TX_MAX_TSS_RINGS	7
#define BNX2_PCICFG_MISC_CONFIG_REG_WINDOW_ENA		 (1L<<7)
#define BNX2_PCICFG_MISC_CONFIG_TARGET_MB_WORD_SWAP	 (1L<<3)
#define BNX2_CHIP_5709			0x57090000
#define BNX2_CHIP_REV_Ax		0x00000000
#define BNX2_CHIP_ID_5706_A0		0x57060000
#define BNX2_CHIP_ID_5706_A1			0x57060010
#define BNX2_CHIP_5708			0x57080000
#define BNX2_SHM_HDR_SIGNATURE				BNX2_MCP_SCRATCH
#define BNX2_MCP_TOE_ID					0x001400a0
#define BNX2_SHM_HDR_ADDR_0				BNX2_MCP_SCRATCH + 4
#define BNX2_DEV_INFO_SIGNATURE			0x00000020
#define BNX2_DEV_INFO_BC_REV			0x0000004c
#define BNX2_PORT_FEATURE			0x000000d8
#define BNX2_BC_STATE_CONDITION			0x000001c8
#define BNX2_MFW_VER_PTR			0x0000014c
#define BNX2_PORT_HW_CFG_MAC_UPPER		0x00000050
#define BNX2_PORT_HW_CFG_MAC_LOWER		0x00000054
#define BNX2_SHARED_HW_CFG_CONFIG		0x0000003c
#define BNX2_PCI_CONFIG_3				0x0000040c
#define BNX2_MAX_MSIX_HW_VEC	9
#define BNX2_PCI_GRC_WINDOW2_BASE		 	 0xc000
#define BNX2_PCI_GRC_WINDOW3_BASE		 	 0xe000
#define BNX2_MAX_TX_DESC_CNT (BNX2_TX_DESC_CNT - 1)
#define BNX2_HC_STATS_TICKS_HC_STAT_TICKS		 (0xffffL<<8)
#define BNX2_TIMER_INTERVAL		HZ
#define BNX2_SHARED_HW_CFG_GIG_LINK_ON_VAUX	 0x8000
#define BNX2_SHARED_HW_CFG_PHY_2_5G		 0x20
#define BNX2_PCI_CONFIG_3_VAUX_PRESET			 (1L<<30)

#define BNX2_PCICFG_INT_ACK_CMD				0x00000084
#define BNX2_PCICFG_STATUS_BIT_SET_CMD			0x00000088
#define BNX2_PCICFG_STATUS_BIT_CLEAR_CMD		0x0000008c
#define BNX2_PCICFG_MISC_STATUS				0x0000006c
#define BNX2_PCICFG_MSI_CONTROL				0x00000058
#define BNX2_PCICFG_DEVICE_CONTROL			0x000000b4
#define BNX2_PCICFG_DEVICE_STATUS_NO_PEND		 ((1L<<5)<<16)
#define BNX2_PCICFG_MISC_CONFIG_CORE_RST_REQ		 (1L<<8)
#define BNX2_PCICFG_MISC_CONFIG_CORE_RST_BSY		 (1L<<9)
#define BNX2_PCICFG_MISC_STATUS_INTA_VALUE		 (1L<<0)
#define BNX2_PCICFG_INT_ACK_CMD_MASK_INT		 (1L<<18)
#define BNX2_PCICFG_INT_ACK_CMD_INDEX_VALID		 (1L<<16)
#define BNX2_PCICFG_INT_ACK_CMD_USE_INT_HC_PARAM	 (1L<<17)
#define BNX2_MISC_VREG_CONTROL				0x000008b4
#define BNX2_MISC_ECO_HW_CTL				0x000008cc
#define BNX2_MISC_ECO_HW_CTL_LARGE_GRC_TMOUT_EN		 (1L<<0)
#define BNX2_DMA_CONFIG_DATA_BYTE_SWAP			 (1L<<0)
#define BNX2_DMA_CONFIG_DATA_WORD_SWAP			 (1L<<1)
#define BNX2_DMA_CONFIG_CNTL_BYTE_SWAP			 (1L<<4)
#define BNX2_DMA_CONFIG_CNTL_WORD_SWAP			 (1L<<5)
#define BNX2_DMA_CONFIG_CNTL_PING_PONG_DMA		 (1L<<10)
#define BNX2_TDMA_CONFIG				0x00005c08
#define BNX2_TDMA_CONFIG_ONE_DMA			 (1L<<0)
#define BNX2_MISC_ENABLE_STATUS_BITS_RX_V2P_ENABLE	 (1L<<15)
#define BNX2_MISC_ENABLE_STATUS_BITS_CONTEXT_ENABLE	 (1L<<21)
#define BNX2_MQ_CONFIG					0x00003c08
#define BNX2_MQ_CONFIG_KNL_BYP_BLK_SIZE			 (0x7L<<4)
#define BNX2_MQ_CONFIG_KNL_BYP_BLK_SIZE_256		 (0L<<4)
#define BNX2_MQ_CONFIG_BIN_MQ_MODE			 (1L<<2)
#define BNX2_MQ_CONFIG_HALT_DIS				 (1L<<1)
#define BNX2_MQ_KNL_BYP_WIND_START			0x00003c1c
#define BNX2_MQ_KNL_WIND_END				0x00003c20
#define BNX2_RV2P_CONFIG				0x00002808
#define BNX2_TBDR_CONFIG				0x00005008
#define BNX2_TBDR_CONFIG_PAGE_SIZE			 (0xfL<<24)
#define BNX2_EMAC_BACKOFF_SEED				0x00001498
#define BNX2_EMAC_RX_MTU_SIZE				0x0000149c
#define BNX2_EMAC_RX_MTU_SIZE_JUMBO_ENA			 (1L<<31)
#define BNX2_RBUF_CONFIG				0x0020000c
#define BNX2_RBUF_CONFIG2				0x0020001c
#define BNX2_RBUF_CONFIG3				0x00200020
#define BNX2_HC_MSIX_BIT_VECTOR				0x00006918
#define BNX2_HC_MSIX_BIT_VECTOR_VAL			 (0x1ffL<<0)
#define BNX2_HC_CONFIG_SB_ADDR_INC_128B			 (1L<<24)
#define BNX2_HC_CONFIG_ONE_SHOT				 (1L<<17)
#define BNX2_HC_CONFIG_USE_INT_PARAM			 (1L<<18)
#define BNX2_FW_RX_LOW_LATENCY				 0x00120058
#define BNX2_HC_SB_CONFIG_2				0x00006a24
#define BNX2_HC_TX_QUICK_CONS_TRIP_1			0x00006a04
#define BNX2_HC_TX_TICKS_1				0x00006a14
#define BNX2_HC_RX_QUICK_CONS_TRIP_1			0x00006a0c
#define BNX2_HC_RX_TICKS_1				0x00006a10
#define BNX2_FW_CAP_BC_CAN_KEEP_VLAN		 0x00000010
#define BNX2_FW_CAP_MFW_CAN_KEEP_VLAN		 0x00000008
#define BNX2_HC_SB_CONFIG_SIZE	(BNX2_HC_SB_CONFIG_2 - BNX2_HC_SB_CONFIG_1)
#define BNX2_HC_SB_CONFIG_1				0x00006a00
#define BNX2_HC_SB_CONFIG_1_TX_TMR_MODE			 (1L<<2)
#define BNX2_HC_SB_CONFIG_1_RX_TMR_MODE			 (1L<<1)
#define BNX2_HC_SB_CONFIG_1_ONE_SHOT			 (1L<<17)
#define BNX2_HC_TX_QUICK_CONS_TRIP_OFF	(BNX2_HC_TX_QUICK_CONS_TRIP_1 -	\
					 BNX2_HC_SB_CONFIG_1)
#define BNX2_HC_TX_TICKS_OFF	(BNX2_HC_TX_TICKS_1 - BNX2_HC_SB_CONFIG_1)
#define BNX2_HC_RX_QUICK_CONS_TRIP_OFF	(BNX2_HC_RX_QUICK_CONS_TRIP_1 - \
					 BNX2_HC_SB_CONFIG_1)
#define BNX2_HC_RX_TICKS_OFF	(BNX2_HC_RX_TICKS_1 - BNX2_HC_SB_CONFIG_1)
#define BNX2_HC_COMMAND_CLR_STAT_NOW			 (1L<<21)
#define BNX2_MISC_NEW_CORE_CTL				0x000008c8
#define BNX2_MISC_NEW_CORE_CTL_DMA_ENABLE		 (1L<<16)
#define BNX2_MISC_ENABLE_DEFAULT	0x17ffffff
#define BNX2_TSCH_TSS_CFG				0x00004c1c
#define BNX2_RLUP_RSS_CONFIG				0x0000201c
#define BNX2_RXP_SCRATCH_RSS_TBL_SZ			 0x000e0038
#define BNX2_RLUP_RSS_DATA				0x0000204c
#define BNX2_RLUP_RSS_COMMAND				0x00002048
#define BNX2_RLUP_RSS_COMMAND_RSS_WRITE_MASK		 (0xffUL<<4)
#define BNX2_RLUP_RSS_COMMAND_WRITE			 (1UL<<12)
#define BNX2_RLUP_RSS_COMMAND_HASH_MASK			 (0x7UL<<14)
#define BNX2_RLUP_RSS_CONFIG_IPV4_RSS_TYPE_ALL_XI	 (1L<<0)
#define BNX2_RLUP_RSS_CONFIG_IPV6_RSS_TYPE_ALL_XI	 (1L<<2)
#define BNX2_PCI_SWAP_DIAG0				0x00000418
#define BNX2_MISC_ENABLE_CLR_BITS_TX_DMA_ENABLE		 (1L<<4)
#define BNX2_MISC_ENABLE_CLR_BITS_DMA_ENGINE_ENABLE	 (1L<<26)
#define BNX2_MISC_ENABLE_CLR_BITS_RX_DMA_ENABLE		 (1L<<17)
#define BNX2_MISC_ENABLE_CLR_BITS_HOST_COALESCE_ENABLE	 (1L<<19)
#define BNX2_MISC_COMMAND_SW_RESET			 (1L<<4)
#define BNX2_DRV_MSG_DATA_WAIT0			 0x00010000
#define BNX2_DRV_MSG_DATA_WAIT1			 0x00020000
#define BNX2_DRV_MSG_DATA_WAIT2			 0x00030000
#define BNX2_DRV_MSG_CODE_RESET			 0x01000000
#define BNX2_DRV_RESET_SIGNATURE		0x00000000
#define BNX2_DRV_RESET_SIGNATURE_MAGIC		 0x4841564b
#define BNX2_FW_CAP_MB				0x368
#define BNX2_FW_CAP_SIGNATURE_MASK		 0xffff0000
#define BNX2_FW_CAP_SIGNATURE			 0xaa550000
#define BNX2_FW_CAP_CAN_KEEP_VLAN	(BNX2_FW_CAP_BC_CAN_KEEP_VLAN | \
					 BNX2_FW_CAP_MFW_CAN_KEEP_VLAN)
#define BNX2_DRV_ACK_CAP_SIGNATURE		 0x35450000
#define BNX2_FW_CAP_REMOTE_PHY_CAPABLE		 0x00000001
#define BNX2_DRV_ACK_CAP_MB			0x364
#define BNX2_PCI_GRC_WINDOW_ADDR_SEP_WIN		 (1L<<31)
#define BNX2_NVM_CFG1					0x00006414
#define BNX2_NVM_CFG2					0x00006418
#define BNX2_NVM_CFG3					0x0000641c
#define BNX2_NVM_WRITE1					0x00006428
#define BNX2_NVM_SW_ARB					0x00006420
#define BNX2_NVM_SW_ARB_ARB_REQ_SET2			 (1L<<2)
#define BNX2_NVM_SW_ARB_ARB_ARB2			 (1L<<10)
#define BNX2_NVM_SW_ARB_ARB_REQ_CLR2			 (1L<<6)
#define BNX2_MISC_CFG_NVM_WR_EN_PCI			 (1L<<1)
#define BNX2_NVM_COMMAND				0x00006400
#define BNX2_NVM_COMMAND_DONE				 (1L<<3)
#define BNX2_NVM_COMMAND_WREN				 (1L<<16)
#define BNX2_NVM_COMMAND_DOIT				 (1L<<4)
#define BNX2_MISC_CFG_NVM_WR_EN				 (0x3L<<1)
#define BNX2_NVM_ACCESS_ENABLE				0x00006424
#define BNX2_NVM_ACCESS_ENABLE_EN			 (1L<<0)
#define BNX2_NVM_ACCESS_ENABLE_WR_EN			 (1L<<1)
#define BNX2_NVM_COMMAND_ERASE				 (1L<<6)
#define BNX2_NVM_COMMAND_WR				 (1L<<5)
#define BNX2_NVM_ADDR					0x0000640c
#define BNX2_NVM_ADDR_NVM_ADDR_VALUE			 (0xffffffL<<0)
#define BNX2_NVM_READ					0x00006410
#define BNX2_NVM_WRITE					0x00006408
#define BNX2_NVM_COMMAND_FIRST				 (1L<<7)
#define BNX2_NVM_COMMAND_LAST				 (1L<<8)
#define BNX2_SHARED_HW_CFG_CONFIG2		0x00000040
#define BNX2_SHARED_HW_CFG2_NVM_SIZE_MASK	 0x00fff000
#define BNX2_EMAC_ATTENTION_ENA				0x00001408
#define BNX2_EMAC_ATTENTION_ENA_LINK			 (1L<<11)
#define BNX2_HC_CONFIG_RX_TMR_MODE			 (1L<<1)
#define BNX2_HC_CONFIG_TX_TMR_MODE			 (1L<<2)
#define BNX2_HC_CONFIG_COLLECT_STATS			 (1L<<0)
#define BNX2_MISC_ENABLE_SET_BITS_HOST_COALESCE_ENABLE	 (1L<<19)
#define BNX2_CTX_COMMAND				0x00001000
#define BNX2_CTX_COMMAND_ENABLED			 (1L<<0)
#define BNX2_CTX_COMMAND_MEM_INIT			 (1L<<13)
#define BNX2_CTX_HOST_PAGE_TBL_DATA0			0x000010cc
#define BNX2_CTX_HOST_PAGE_TBL_DATA0_VALID		 (1L<<0)
#define BNX2_CTX_HOST_PAGE_TBL_DATA1			0x000010d0
#define BNX2_CTX_HOST_PAGE_TBL_CTRL			0x000010c8
#define BNX2_CTX_HOST_PAGE_TBL_CTRL_WRITE_REQ		 (1L<<30)
#define BNX2_CTX_VIRT_ADDR				0x00001008
#define BNX2_CTX_PAGE_TBL				0x0000100c
#define BNX2_RBUF_STATUS1				0x00200004
#define BNX2_RBUF_STATUS1_FREE_COUNT			 (0x3ffL<<0)
#define BNX2_RBUF_COMMAND				0x00200000
#define BNX2_RBUF_COMMAND_ALLOC_REQ			 (1L<<5)
#define BNX2_RBUF_FW_BUF_ALLOC				0x00200010
#define BNX2_RBUF_FW_BUF_ALLOC_VALUE			 (0x1ffL<<7)
#define BNX2_RBUF_FW_BUF_FREE				0x00200014
#define BNX2_MISC_ENABLE_SET_BITS_RX_MBUF_ENABLE	 (1L<<12)
#define BNX2_EMAC_MAC_MATCH0				0x00001410
#define BNX2_EMAC_MAC_MATCH1				0x00001414
#define BNX2_L2CTX_CTX_TYPE				0x00000000
#define BNX2_L2CTX_CTX_TYPE_CTX_BD_CHN_TYPE_VALUE	 (1<<28)
#define BNX2_L2CTX_CTX_TYPE_SIZE_L2			 ((0x20/20)<<16)
#define BNX2_L2CTX_FLOW_CTRL_ENABLE			 0x000000ff
#define BNX2_EMAC_TX_LENGTHS				0x000014c4
#define BNX2_EMAC_MODE_PORT				 (0x3L<<2)
#define BNX2_EMAC_MODE_HALF_DUPLEX			 (1L<<1)
#define BNX2_EMAC_MODE_MAC_LOOP				 (1L<<4)
#define BNX2_EMAC_MODE_FORCE_LINK			 (1L<<11)
#define BNX2_EMAC_MODE_25G_MODE				 (1L<<5)
#define BNX2_EMAC_MODE_PORT_MII_10M			 (3L<<2)
#define BNX2_EMAC_MODE_PORT_MII				 (1L<<2)
#define BNX2_EMAC_MODE_PORT_GMII			 (2L<<2)
#define BNX2_EMAC_RX_MODE_FLOW_EN			 (1L<<2)
#define BNX2_EMAC_TX_MODE_FLOW_EN			 (1L<<4)
#define BNX2_EMAC_STATUS_LINK_CHANGE			 (1L<<12)
#define BNX2_EMAC_MDIO_MODE_AUTO_POLL			 (1L<<4)
#define BNX2_EMAC_MDIO_COMM_COMMAND_READ		 (2L<<26)
#define BNX2_EMAC_MDIO_COMM_DISEXT			 (1L<<30)
#define BNX2_EMAC_MDIO_COMM_START_BUSY			 (1L<<29)
#define BNX2_EMAC_MDIO_COMM_DATA			 (0xffffL<<0)
#define BNX2_EMAC_MDIO_COMM_COMMAND_WRITE		 (1L<<26)
#define BNX2_FW_MAX_ISCSI_CONN				 0x001a0080
#define BNX2_CTX_CTX_CTRL_WRITE_REQ			 (1L<<30)
#define BNX2_DRV_MB				0x00000004
#define BNX2_FW_MB				0x00000008
#define BNX2_FW_MSG_ACK				 0x0000ffff
#define BNX2_DRV_MSG_SEQ				 0x0000ffff
#define BNX2_DRV_MSG_DATA				 0x00ff0000
#define BNX2_DRV_MSG_CODE				 0xff000000
#define BNX2_DRV_MSG_CODE_FW_TIMEOUT		 0x05000000
#define BNX2_FW_MSG_STATUS_MASK			 0x00ff0000
#define BNX2_FW_MSG_STATUS_OK			 0x00000000
#define BNX2_DRV_PULSE_MB			0x00000010
#define BNX2_BC_STATE_RESET_TYPE		0x000001c0
#define BNX2_BC_RESET_TYPE			0x000001c0
#define BNX2_MCP_STATE_P0				 0x0016fdc8
#define BNX2_MCP_STATE_P1				 0x0016f9c8
#define BNX2_MCP_STATE_P0_5708				 0x00169dc8
#define BNX2_MCP_STATE_P1_5708				 0x001699c8
#define BNX2_MCP_CPU_MODE				0x00145000
#define BNX2_MCP_CPU_STATE				0x00145004
#define BNX2_MCP_CPU_EVENT_MASK				0x00145008
#define BNX2_MCP_CPU_PROGRAM_COUNTER			0x0014501c
#define BNX2_MCP_CPU_INSTRUCTION			0x00145020
#define BNX2_RPHY_COPPER_LINK			0x378
#define BNX2_RPHY_SERDES_LINK			0x374
#define BNX2_NETLINK_SET_LINK_ENABLE_AUTONEG	 (1<<10)
#define BNX2_NETLINK_SET_LINK_SPEED_10HALF	 (1<<0)
#define BNX2_NETLINK_SET_LINK_SPEED_10FULL	 (1<<1)
#define BNX2_NETLINK_SET_LINK_SPEED_100HALF	 (1<<2)
#define BNX2_NETLINK_SET_LINK_SPEED_100FULL	 (1<<3)
#define BNX2_NETLINK_SET_LINK_SPEED_1GFULL	 (1<<5)
#define BNX2_NETLINK_SET_LINK_SPEED_2G5FULL	 (1<<7)
#define BNX2_NETLINK_SET_LINK_SPEED_10		 \
	(BNX2_NETLINK_SET_LINK_SPEED_10HALF |	 \
	 BNX2_NETLINK_SET_LINK_SPEED_10FULL)
#define BNX2_NETLINK_SET_LINK_SPEED_100		 \
	(BNX2_NETLINK_SET_LINK_SPEED_100HALF |	 \
	 BNX2_NETLINK_SET_LINK_SPEED_100FULL)
#define BNX2_DRV_MB_ARG0			0x00000014
#define BNX2_NETLINK_SET_LINK_FC_SYM_PAUSE	 (1<<12)
#define BNX2_NETLINK_SET_LINK_FC_ASYM_PAUSE	 (1<<13)
#define BNX2_NETLINK_SET_LINK_PHY_APP_REMOTE	 (1<<11)
#define BNX2_NETLINK_SET_LINK_ETH_AT_WIRESPEED	 (1<<14)
#define BNX2_DRV_MSG_CODE_CMD_SET_LINK		 0x10000000
#define BNX2_LINK_STATUS_10HALF			 (1<<1)
#define BNX2_LINK_STATUS_10FULL			 (2<<1)
#define BNX2_LINK_STATUS_100HALF			 (3<<1)
#define BNX2_LINK_STATUS_100FULL			 (5<<1)
#define BNX2_LINK_STATUS_1000HALF			 (6<<1)
#define BNX2_LINK_STATUS_1000FULL			 (7<<1)
#define BNX2_LINK_STATUS_2500HALF			 (8<<1)
#define BNX2_LINK_STATUS_2500FULL			 (9<<1)
#define BNX2_LINK_STATUS_LINK_UP			 0x1
#define BNX2_LINK_STATUS_AN_ENABLED			 (1<<5)
#define BNX2_LINK_STATUS_PARALLEL_DET		 (1<<7)
#define BNX2_LINK_STATUS_AN_COMPLETE		 (1<<6)
#define BNX2_LINK_STATUS_LINK_DOWN			 0x0
#define BNX2_LINK_STATUS_HEART_BEAT_EXPIRED	 (1<<31)
#define BNX2_LINK_STATUS_SPEED_MASK		 0x1e
#define BNX2_LINK_STATUS_100BASE_T4			 (4<<1)
#define BNX2_LINK_STATUS_TX_FC_ENABLED		 (1<<16)
#define BNX2_LINK_STATUS_RX_FC_ENABLED		 (1<<17)
#define BNX2_LINK_STATUS_SERDES_LINK		 (1<<20)
#define BNX2_FW_EVT_CODE_MB			0x354
#define BNX2_FW_EVT_CODE_LINK_EVENT		 0x00000001
#define BNX2_FW_EVT_CODE_SW_TIMER_EXPIRATION_EVENT 0x00000000
#define BNX2_PORT_HW_CFG_CONFIG			0x00000058
#define BNX2_PORT_HW_CFG_CFG_DFLT_LINK_MASK	 0x001f0000
#define BNX2_PORT_HW_CFG_CFG_DFLT_LINK_1G	 0x00030000
#define BNX2_EMAC_RX_MODE_PROMISCUOUS			 (1L<<8)
#define BNX2_EMAC_RX_MODE_KEEP_VLAN_TAG			 (1L<<10)
#define BNX2_RPM_SORT_USER0_BC_EN			 (1L<<16)
#define BNX2_RPM_SORT_USER0_PROM_EN			 (1L<<19)
#define BNX2_RPM_SORT_USER0_PROM_VLAN			 (1L<<24)
#define BNX2_EMAC_MULTICAST_HASH0			0x000014d0
#define BNX2_RPM_SORT_USER0_MC_EN			 (1L<<17)
#define BNX2_RPM_SORT_USER0_MC_HSH_EN			 (1L<<18)
#define BNX2_RPM_SORT_USER0				0x00001820
#define BNX2_RPM_SORT_USER0_ENA				 (1L<<31)
#define BNX2_RV2P_INSTR_HIGH				0x00002830
#define BNX2_RV2P_INSTR_LOW				0x00002834
#define BNX2_RV2P_PROC1_ADDR_CMD_RDWR			 (1L<<31)
#define BNX2_RV2P_PROC1_ADDR_CMD			0x00002838
#define BNX2_RV2P_PROC2_ADDR_CMD_RDWR			 (1L<<31)
#define BNX2_RV2P_PROC2_ADDR_CMD			0x0000283c
#define BNX2_RV2P_COMMAND				0x00002800
#define BNX2_RV2P_COMMAND_PROC1_RESET			 (1L<<16)
#define BNX2_RV2P_COMMAND_PROC2_RESET			 (1L<<17)
#define BNX2_EMAC_MODE_MPKT_RCVD			 (1L<<19)
#define BNX2_EMAC_MODE_ACPI_RCVD			 (1L<<20)
#define BNX2_EMAC_MODE_MPKT				 (1L<<18)
#define BNX2_EMAC_RX_MODE_SORT_MODE			 (1L<<12)
#define BNX2_MISC_ENABLE_SET_BITS_RX_PARSER_MAC_ENABLE	 (1L<<10)
#define BNX2_MISC_ENABLE_SET_BITS_TX_HEADER_Q_ENABLE	 (1L<<7)
#define BNX2_MISC_ENABLE_SET_BITS_EMAC_ENABLE		 (1L<<9)
#define BNX2_RPM_CONFIG					0x00001808
#define BNX2_RPM_CONFIG_ACPI_ENA			 (1L<<1)
#define BNX2_DRV_MSG_CODE_SUSPEND_WOL		 0x04000000
#define BNX2_DRV_MSG_CODE_SUSPEND_NO_WOL	 0x09000000
#define BNX2_DRV_MSG_DATA_WAIT3			 0x00040000
#define BNX2_PORT_FEATURE_ASF_ENABLED		 0x04000000
#define BNX2_CONDITION_PM_STATE_MASK		 0x00030000
#define BNX2_CONDITION_PM_STATE_UNPREP		 0x00010000
#define BNX2_DRV_MSG_CODE_UNLOAD_LNK_DN		 0x0b000000
#define BNX2_EMAC_LED					0x0000140c
#define BNX2_MISC_CFG_LEDMODE_MAC			 (0L<<8)
#define BNX2_EMAC_LED_OVERRIDE				 (1L<<0)
#define BNX2_EMAC_LED_1000MB_OVERRIDE			 (1L<<1)
#define BNX2_EMAC_LED_100MB_OVERRIDE			 (1L<<2)
#define BNX2_EMAC_LED_10MB_OVERRIDE			 (1L<<3)
#define BNX2_EMAC_LED_TRAFFIC_OVERRIDE			 (1L<<4)
#define BNX2_EMAC_LED_TRAFFIC				 (1L<<6)
#define BNX2_DRV_MSG_CODE_KEEP_VLAN_UPDATE	 0x0d000000
#define BNX2_MISC_DUAL_MEDIA_CTRL			0x000008ec
#define BNX2_MISC_DUAL_MEDIA_CTRL_BOND_ID		 (0xffL<<0)
#define BNX2_MISC_DUAL_MEDIA_CTRL_BOND_ID_C		 (3L<<0)
#define BNX2_MISC_DUAL_MEDIA_CTRL_BOND_ID_S		 (12L<<0)
#define BNX2_MISC_DUAL_MEDIA_CTRL_STRAP_OVERRIDE	 (1L<<25)
#define BNX2_MISC_DUAL_MEDIA_CTRL_PHY_CTRL		 (0x7L<<21)
#define BNX2_MISC_DUAL_MEDIA_CTRL_PHY_CTRL_STRAP	 (0x7L<<8)
#define BNX2_PCICFG_MISC_STATUS_PCIX_DET		 (1L<<3)
#define BNX2_PCICFG_PCI_CLOCK_CONTROL_BITS		0x00000070
#define BNX2_PCICFG_PCI_CLOCK_CONTROL_BITS_PCI_CLK_SPD_DET	 (0xfL<<0)
#define BNX2_PCICFG_PCI_CLOCK_CONTROL_BITS_PCI_CLK_SPD_DET_133MHZ	 (7L<<0)
#define BNX2_PCICFG_PCI_CLOCK_CONTROL_BITS_PCI_CLK_SPD_DET_95MHZ	 (6L<<0)
#define BNX2_PCICFG_PCI_CLOCK_CONTROL_BITS_PCI_CLK_SPD_DET_66MHZ	 (4L<<0)
#define BNX2_PCICFG_PCI_CLOCK_CONTROL_BITS_PCI_CLK_SPD_DET_80MHZ	 (5L<<0)
#define BNX2_PCICFG_PCI_CLOCK_CONTROL_BITS_PCI_CLK_SPD_DET_48MHZ	 (2L<<0)
#define BNX2_PCICFG_PCI_CLOCK_CONTROL_BITS_PCI_CLK_SPD_DET_55MHZ	 (3L<<0)
#define BNX2_PCICFG_PCI_CLOCK_CONTROL_BITS_PCI_CLK_SPD_DET_LOW	 (0xfL<<0)
#define BNX2_PCICFG_PCI_CLOCK_CONTROL_BITS_PCI_CLK_SPD_DET_32MHZ	 (0L<<0)
#define BNX2_PCICFG_PCI_CLOCK_CONTROL_BITS_PCI_CLK_SPD_DET_38MHZ	 (1L<<0)
#define BNX2_PCICFG_MISC_STATUS_M66EN			 (1L<<2)
#define BNX2_PCICFG_MISC_STATUS_32BIT_DET		 (1L<<1)
#define BNX2_SHM_HDR_SIGNATURE_SIG_MASK			 0xffff0000
#define BNX2_SHM_HDR_SIGNATURE_SIG			 0x53530000
#define BNX2_MCP_TOE_ID_FUNCTION_ID			 (1L<<31)
#define BNX2_DEV_INFO_SIGNATURE_MAGIC_MASK	 0xffffff00
#define BNX2_DEV_INFO_SIGNATURE_MAGIC		 0x44564900
#define BNX2_PORT_FEATURE_WOL_ENABLED		 0x01000000
#define BNX2_CONDITION_MFW_RUN_MASK		 0x0000e000
#define BNX2_CONDITION_MFW_RUN_UNKNOWN		 0x00000000
#define BNX2_CONDITION_MFW_RUN_NONE		 0x0000e000
#define BNX2_DRV_MSG_CODE_DIAG			 0x07000000
#define BNX2_FW_RX_DROP_COUNT				 0x00120084
#define BNX2_HC_COMMAND_STATS_NOW			 (1L<<18)
#define BNX2_HC_COMMAND_COAL_NOW			 (1L<<16)
#define BNX2_HC_COMMAND_COAL_NOW_WO_INT			 (1L<<17)
#define BNX2_TBDC_STATUS				0x5404
#define BNX2_TBDC_STATUS_FREE_CNT                        (0x3fUL<<0)
#define BNX2_TBDC_BD_ADDR                               0x5424
#define BNX2_TBDC_CAM_OPCODE                            0x5434
#define BNX2_TBDC_CAM_OPCODE_OPCODE_CAM_READ             (5UL<<0)
#define BNX2_TBDC_COMMAND                               0x5400
#define BNX2_TBDC_COMMAND_CMD_REG_ARB                    (1UL<<3)
#define BNX2_TBDC_CID                                   0x5430
#define BNX2_TBDC_BIDX                                  0x542c
#define BNX2_TBDC_BDIDX_BDIDX                            (0xffffUL<<0)
#define BNX2_TXP_CPU_MODE				0x00045000
#define BNX2_CP_CPU_MODE				0x00185000
#define BNX2_EMAC_TX_STATUS				0x000014c0
#define BNX2_EMAC_RX_STATUS				0x000014cc
#define BNX2_RPM_MGMT_PKT_CTRL				0x0000180c
#define BNX2_L2CTX_TYPE_XI				0x00000080
#define BNX2_L2CTX_CMD_TYPE_XI				0x00000240
#define BNX2_L2CTX_TBDR_BHADDR_HI_XI			0x00000258
#define BNX2_L2CTX_TBDR_BHADDR_LO_XI			0x0000025c
#define BNX2_L2CTX_TYPE					0x00000000
#define BNX2_L2CTX_CMD_TYPE				0x00000088
#define BNX2_L2CTX_TBDR_BHADDR_HI			0x000000a0
#define BNX2_L2CTX_TBDR_BHADDR_LO			0x000000a4
#define BNX2_L2CTX_TYPE_TYPE_L2				 (1<<28)
#define BNX2_L2CTX_TYPE_SIZE_L2				 ((0xc0/0x20)<<16)
#define BNX2_L2CTX_CMD_TYPE_TYPE_L2			 (0<<24)
#define BNX2_L2CTX_TX_HOST_BIDX				0x00000088
#define BNX2_L2CTX_TX_HOST_BSEQ				0x00000090
#define BNX2_MQ_MAP_L2_5				0x00003d34
#define BNX2_MQ_MAP_L2_5_ARM				 (0x3L<<26)
#define BNX2_L2CTX_PG_BUF_SIZE				0x00000048
#define BNX2_L2CTX_RBDC_KEY				0x0000004c
#define BNX2_L2CTX_RBDC_JUMBO_KEY			 0x3ffe
#define BNX2_L2CTX_NX_PG_BDHADDR_HI			0x00000050
#define BNX2_L2CTX_NX_PG_BDHADDR_LO			0x00000054
#define BNX2_MQ_MAP_L2_3				0x00003d2c
#define BNX2_MQ_MAP_L2_3_DEFAULT			 0x82004646
#define BNX2_L2CTX_NX_BDHADDR_HI			0x00000010
#define BNX2_L2CTX_NX_BDHADDR_LO			0x00000014
#define BNX2_L2CTX_HOST_BDIDX				0x00000004
#define BNX2_L2CTX_HOST_BSEQ				0x00000008
#define BNX2_L2CTX_HOST_PG_BDIDX			0x00000044
#define BNX2_ISCSI_INITIATOR			0x3dc
#define BNX2_ISCSI_INITIATOR_EN			 0x00080000
#define BNX2_ISCSI_MAX_CONN			0x3e4
#define BNX2_ISCSI_MAX_CONN_MASK		 0xffff0000
#define BNX2_ISCSI_MAX_CONN_SHIFT		 16

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->intr_status & s->intr_mask) {
        if (s->has_msix) {
            msix_notify(pdev, 0);
        } else if (s->has_msi) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case BNX2_PCICFG_REG_WINDOW_ADDRESS:
        val = s->pcicfg_reg_window_address;
        break;
    case BNX2_PCICFG_REG_WINDOW:
        switch (s->pcicfg_reg_window_address) {
        case 0x00160000: /* BNX2_SHM_HDR_SIGNATURE */
            val = BNX2_SHM_HDR_SIGNATURE_SIG;
            break;
        case 0x00160004: /* BNX2_SHM_HDR_ADDR_0 */
            val = 0x00160000;
            break;
        case 0x00160020: /* BNX2_DEV_INFO_SIGNATURE */
            val = BNX2_DEV_INFO_SIGNATURE_MAGIC;
            break;
        case 0x0016004c: /* BNX2_DEV_INFO_BC_REV */
            val = 0;
            break;
        case 0x001600d8: /* BNX2_PORT_FEATURE */
            val = 0;
            break;
        case 0x001601c8: /* BNX2_BC_STATE_CONDITION */
            val = BNX2_CONDITION_MFW_RUN_UNKNOWN;
            break;
        case 0x00160050: /* BNX2_PORT_HW_CFG_MAC_UPPER */
            val = 0x1234;
            break;
        case 0x00160054: /* BNX2_PORT_HW_CFG_MAC_LOWER */
            val = 0x56789abc;
            break;
        case 0x0016003c: /* BNX2_SHARED_HW_CFG_CONFIG */
            val = 0;
            break;
        case 0x00160368: /* BNX2_FW_CAP_MB */
            val = BNX2_FW_CAP_SIGNATURE;
            break;
        case BNX2_MCP_TOE_ID: /* 0x001400a0 */
            val = 0;
            break;
        default:
            val = s->pcicfg_reg_window;
            break;
        }
        break;
    case BNX2_MISC_ID:
        val = BNX2_CHIP_ID_5706_A0;
        break;
    case BNX2_PCI_CONFIG_3:
        val = BNX2_PCI_CONFIG_3_VAUX_PRESET;
        break;
    case BNX2_PCICFG_MISC_STATUS:
        val = BNX2_PCICFG_MISC_STATUS_PCIX_DET;
        break;
    case BNX2_PCICFG_PCI_CLOCK_CONTROL_BITS:
        val = BNX2_PCICFG_PCI_CLOCK_CONTROL_BITS_PCI_CLK_SPD_DET_133MHZ;
        break;
    case 0x04: /* PCI_COMMAND */
        val = 0;
        break;
    case BNX2_NVM_CFG1:
        val = 0;
        break;
    case BNX2_HC_COMMAND:
        val = s->hc_command;
        break;
    case BNX2_HC_CONFIG:
        val = s->hc_config;
        break;
    case BNX2_HC_ATTN_BITS_ENABLE:
        val = s->hc_attn_bits_enable;
        break;
    case BNX2_HC_STATUS_ADDR_L:
        val = s->hc_status_addr_l;
        break;
    case BNX2_HC_STATUS_ADDR_H:
        val = s->hc_status_addr_h;
        break;
    case BNX2_HC_STATISTICS_ADDR_L:
        val = s->hc_statistics_addr_l;
        break;
    case BNX2_HC_STATISTICS_ADDR_H:
        val = s->hc_statistics_addr_h;
        break;
    case BNX2_HC_TX_QUICK_CONS_TRIP:
        val = s->hc_tx_quick_cons_trip;
        break;
    case BNX2_HC_COMP_PROD_TRIP:
        val = s->hc_comp_prod_trip;
        break;
    case BNX2_HC_RX_QUICK_CONS_TRIP:
        val = s->hc_rx_quick_cons_trip;
        break;
    case BNX2_HC_RX_TICKS:
        val = s->hc_rx_ticks;
        break;
    case BNX2_HC_TX_TICKS:
        val = s->hc_tx_ticks;
        break;
    case BNX2_HC_COM_TICKS:
        val = s->hc_com_ticks;
        break;
    case BNX2_HC_CMD_TICKS:
        val = s->hc_cmd_ticks;
        break;
    case BNX2_HC_STAT_COLLECT_TICKS:
        val = s->hc_stat_collect_ticks;
        break;
    case BNX2_HC_STATS_TICKS:
        val = s->hc_stats_ticks;
        break;
    case BNX2_HC_STATS_INTERRUPT_STATUS:
        val = s->hc_stats_interrupt_status;
        break;
    case BNX2_MISC_CFG:
        val = s->misc_cfg;
        break;
    case BNX2_MISC_ENABLE_SET_BITS:
        val = s->misc_enable_set_bits;
        break;
    case BNX2_MISC_ENABLE_CLR_BITS:
        val = s->misc_enable_clr_bits;
        break;
    case BNX2_MISC_COMMAND:
        val = s->misc_command;
        break;
    case BNX2_PCICFG_MISC_CONFIG:
        val = s->pcicfg_misc_config;
        break;
    case BNX2_DMA_CONFIG:
        val = s->dma_config;
        break;
    case BNX2_EMAC_MODE:
        val = s->emac_mode;
        break;
    case BNX2_EMAC_STATUS:
        val = s->emac_status;
        break;
    case BNX2_EMAC_TX_MODE:
        val = s->emac_tx_mode;
        break;
    case BNX2_EMAC_RX_MODE:
        val = s->emac_rx_mode;
        break;
    case BNX2_EMAC_MDIO_COMM:
        val = s->emac_mdio_comm;
        break;
    case BNX2_EMAC_MDIO_MODE:
        val = s->emac_mdio_mode;
        break;
    case BNX2_PCI_GRC_WINDOW_ADDR:
        val = s->pci_grc_window_addr;
        break;
    case BNX2_PCI_GRC_WINDOW2_ADDR:
        val = s->pci_grc_window2_addr;
        break;
    case BNX2_PCI_GRC_WINDOW3_ADDR:
        val = s->pci_grc_window3_addr;
        break;
    case BNX2_LINK_STATUS:
        val = s->link_status;
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
    case BNX2_PCICFG_REG_WINDOW_ADDRESS:
        s->pcicfg_reg_window_address = val;
        break;
    case BNX2_PCICFG_REG_WINDOW:
        s->pcicfg_reg_window = val;
        break;
    case 0x04: /* PCI_COMMAND */
        break;
    case BNX2_HC_COMMAND:
        s->hc_command = val;
        break;
    case BNX2_HC_CONFIG:
        s->hc_config = val;
        break;
    case BNX2_HC_ATTN_BITS_ENABLE:
        s->hc_attn_bits_enable = val;
        break;
    case BNX2_HC_STATUS_ADDR_L:
        s->hc_status_addr_l = val;
        break;
    case BNX2_HC_STATUS_ADDR_H:
        s->hc_status_addr_h = val;
        break;
    case BNX2_HC_STATISTICS_ADDR_L:
        s->hc_statistics_addr_l = val;
        break;
    case BNX2_HC_STATISTICS_ADDR_H:
        s->hc_statistics_addr_h = val;
        break;
    case BNX2_HC_TX_QUICK_CONS_TRIP:
        s->hc_tx_quick_cons_trip = val;
        break;
    case BNX2_HC_COMP_PROD_TRIP:
        s->hc_comp_prod_trip = val;
        break;
    case BNX2_HC_RX_QUICK_CONS_TRIP:
        s->hc_rx_quick_cons_trip = val;
        break;
    case BNX2_HC_RX_TICKS:
        s->hc_rx_ticks = val;
        break;
    case BNX2_HC_TX_TICKS:
        s->hc_tx_ticks = val;
        break;
    case BNX2_HC_COM_TICKS:
        s->hc_com_ticks = val;
        break;
    case BNX2_HC_CMD_TICKS:
        s->hc_cmd_ticks = val;
        break;
    case BNX2_HC_STAT_COLLECT_TICKS:
        s->hc_stat_collect_ticks = val;
        break;
    case BNX2_HC_STATS_TICKS:
        s->hc_stats_ticks = val;
        break;
    case BNX2_HC_STATS_INTERRUPT_STATUS:
        s->hc_stats_interrupt_status = val;
        break;
    case BNX2_MISC_CFG:
        s->misc_cfg = val;
        break;
    case BNX2_MISC_ENABLE_SET_BITS:
        s->misc_enable_set_bits = val;
        break;
    case BNX2_MISC_ENABLE_CLR_BITS:
        s->misc_enable_clr_bits = val;
        break;
    case BNX2_MISC_COMMAND:
        s->misc_command = val;
        break;
    case BNX2_PCICFG_MISC_CONFIG:
        s->pcicfg_misc_config = val;
        break;
    case BNX2_DMA_CONFIG:
        s->dma_config = val;
        break;
    case BNX2_EMAC_MODE:
        s->emac_mode = val;
        break;
    case BNX2_EMAC_STATUS:
        s->emac_status = val;
        break;
    case BNX2_EMAC_TX_MODE:
        s->emac_tx_mode = val;
        break;
    case BNX2_EMAC_RX_MODE:
        s->emac_rx_mode = val;
        break;
    case BNX2_EMAC_MDIO_COMM:
        s->emac_mdio_comm = val;
        break;
    case BNX2_EMAC_MDIO_MODE:
        s->emac_mdio_mode = val;
        break;
    case BNX2_PCI_GRC_WINDOW_ADDR:
        s->pci_grc_window_addr = val;
        break;
    case BNX2_PCI_GRC_WINDOW2_ADDR:
        s->pci_grc_window2_addr = val;
        break;
    case BNX2_PCI_GRC_WINDOW3_ADDR:
        s->pci_grc_window3_addr = val;
        break;
    case BNX2_LINK_STATUS:
        s->link_status = val;
        break;
    default:
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    return val;
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

    s->pcicfg_reg_window_address = 0;
    s->pcicfg_reg_window = 0;
    s->hc_command = 0;
    s->hc_config = 0;
    s->hc_attn_bits_enable = 0;
    s->hc_status_addr_l = 0;
    s->hc_status_addr_h = 0;
    s->hc_statistics_addr_l = 0;
    s->hc_statistics_addr_h = 0;
    s->hc_tx_quick_cons_trip = 0;
    s->hc_comp_prod_trip = 0;
    s->hc_rx_quick_cons_trip = 0;
    s->hc_rx_ticks = 0;
    s->hc_tx_ticks = 0;
    s->hc_com_ticks = 0;
    s->hc_cmd_ticks = 0;
    s->hc_stat_collect_ticks = 0;
    s->hc_stats_ticks = 0;
    s->hc_stats_interrupt_status = 0;
    s->misc_cfg = 0;
    s->misc_enable_set_bits = 0;
    s->misc_enable_clr_bits = 0;
    s->misc_command = 0;
    s->misc_id = BNX2_CHIP_ID_5706_A0;
    s->pcicfg_misc_config = 0;
    s->dma_config = 0;
    s->emac_mode = 0;
    s->emac_status = 0;
    s->emac_tx_mode = 0;
    s->emac_rx_mode = 0;
    s->emac_mdio_comm = 0;
    s->emac_mdio_mode = 0;
    s->pci_grc_window_addr = 0;
    s->pci_grc_window2_addr = 0;
    s->pci_grc_window3_addr = 0;
    s->link_status = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_BROADCOM );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_NX2_5706 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_ETHERNET );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* Add PCI-X capability to satisfy driver probe for BCM5706 */
    pci_add_capability(pdev, PCI_CAP_ID_PCIX, 0, 0x18, errp);

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x2000000;
    s->bar_info[0].name = "bnx2-mmio";
    
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    if (msix_init_exclusive_bar(pdev, BNX2_MAX_MSIX_HW_VEC, 1, errp) == 0) {
        s->has_msix = true;
    } else if (msi_init(pdev, 0, 1, true, false, errp) == 0) {
        s->has_msi = true;
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (s->has_msix) {
        msix_uninit_exclusive_bar(pdev);
    }
    if (s->has_msi) {
        msi_uninit(pdev);
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "bnx2_pci",
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
