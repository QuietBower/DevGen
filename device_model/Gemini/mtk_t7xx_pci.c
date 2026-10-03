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
#include "hw/pci/pci_ids.h"
#include "qemu/bitops.h"

#ifndef GENMASK
#define GENMASK(h, l) MAKE_64BIT_MASK(l, (h) - (l) + 1)
#endif
#ifndef GENMASK_ULL
#define GENMASK_ULL(h, l) MAKE_64BIT_MASK(l, (h) - (l) + 1)
#endif

#define TYPE_PCIBASE_DEVICE "mtk_t7xx_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#ifndef PCI_VENDOR_ID_MEDIATEK
#define PCI_VENDOR_ID_MEDIATEK 0x14c3
#endif

#define DRIVER_NAME "mtk_t7xx"
#define T7XX_PCI_IREG_BASE		0
#define T7XX_PCI_EREG_BASE		2
#define T7XX_INIT_TIMEOUT		20
#define PM_SLEEP_DIS_TIMEOUT_MS		20
#define PM_ACK_TIMEOUT_MS		1500
#define PM_AUTOSUSPEND_MS		5000
#define PM_RESOURCE_POLL_TIMEOUT_US	10000
#define PM_RESOURCE_POLL_STEP_US	100
#define IREG_BASE(t7xx_dev)	((t7xx_dev)->base_addr.pcie_mac_ireg_base)
#define T7XX_PCIE_MISC_MAC_SLEEP_DIS		BIT(7)
#define T7XX_PCIE_MISC_CTRL			0x0348
#define T7XX_PCIE_RESOURCE_STS_MSK		GENMASK(4, 0)
#define T7XX_PCIE_RESOURCE_STATUS		0x0d28
#define T7XX_L1_BIT(i)				BIT((i) * 4 + 1)
#define T7XX_L1_1_BIT(i)			BIT((i) * 4 + 2)
#define T7XX_L1_2_BIT(i)			BIT((i) * 4 + 3)
#define DISABLE_ASPM_LOWPWR			0x0e50
#define D2H_INT_RESUME_ACK_AP			BIT(14)
#define D2H_INT_SUSPEND_ACK_AP			BIT(13)
#define D2H_INT_DS_LOCK_ACK			BIT(0)
#define D2H_INT_RESUME_ACK			BIT(12)
#define ENABLE_ASPM_LOWPWR			0x0e54
#define D2H_INT_SUSPEND_ACK			BIT(11)
#define H2D_CH_DS_LOCK				3
#define H2D_CH_SUSPEND_REQ			9
#define H2D_CH_SUSPEND_REQ_AP			11
#define H2D_CH_RESUME_REQ			10
#define EXT_INT_NUM				8
#define MSIX_MSK_SET_ALL			GENMASK(31, 24)
#define IMASK_HOST_MSIX_CLR_GRP0_0		0x3080
#define FSM_CMD_FLAG_WAIT_FOR_COMPLETION	BIT(0)
#define T7XX_PCIE_PM_RESUME_STATE		0x0d0c
#define D2H_INT_EXCEPTION_ALLQ_RESET		BIT(4)
#define ATR_PCIE_WIN0_T0_ATR_PARAM_SRC_ADDR	0x0600
#define ATR_SRC_ADDR_INVALID			0x007f
#define D2H_INT_EXCEPTION_CLEARQ_DONE		BIT(3)
#define H2D_CH_RESUME_REQ_AP			12
#define D2H_INT_EXCEPTION_INIT			BIT(1)
#define D2H_INT_PORT_ENUM			BIT(5)
#define D2H_INT_EXCEPTION_INIT_DONE		BIT(2)
#define INFRACFG_AO_DEV_CHIP			0x10001000
#define H2D_CH_DEVICE_RESET			13
#define REG_EP2RC_SW_INT_EAP_MASK_CLR		0x40
#define REG_RC2EP_SW_TCHNUM			0x0c
#define REG_RC2EP_SW_BSY			0x04
#define T7XX_PCIE_CFG_MSIX			0x03ec
#define T7XX_PCIE_REG_SIZE_CHIP			0x00400000
#define T7XX_PCIE_REG_TRSL_ADDR_CHIP		0x10000000
#define MHCCIF_RC_DEV_BASE			0x10024000
#define FSM_CMD_FLAG_IN_INTERRUPT		BIT(2)
#define MSIX_ISTAT_HST_GRP0_0			0x0f00
#define EXT_INT_START				24
#define T7XX_PCIE_MISC_DEV_STATUS		0x0d1c
#define HOST_EVENT_MASK				GENMASK(31, 28)
#define MTK_QUEUES		16
#define PORT_CH_ID_MASK		GENMASK(7, 0)
#define IMASK_HOST_MSIX_SET_GRP0_0		0x3000
#define ISTAT_HST_CTRL				0x01ac
#define ISTAT_HST_CTRL_DIS			BIT(0)
#define ATR_PCIE_WIN0_ADDR_ALGMT		GENMASK_ULL(63, 12)
#define ATR_PCIE_WIN0_T0_TRSL_ADDR		0x0608
#define ATR_PORT_OFFSET				0x100
#define ATR_TRANSPARENT_SIZE			0x3f
#define ATR_TABLE_OFFSET			0x20
#define ATR_PCIE_WIN0_T0_TRSL_PARAM		0x0610
#define ATR_TABLE_NUM_PER_ATR			8
#define TOPRGU_CH_PCIE_IRQ_STA			0x1000790c
#define D2H_SW_INT_MASK (D2H_INT_EXCEPTION_INIT | \
			 D2H_INT_EXCEPTION_INIT_DONE | \
			 D2H_INT_EXCEPTION_CLEARQ_DONE | \
			 D2H_INT_EXCEPTION_ALLQ_RESET | \
			 D2H_INT_PORT_ENUM | \
			 D2H_INT_ASYNC_AP_HK | \
			 D2H_INT_ASYNC_MD_HK)
#define CLDMA_RXQ_NUM			8
#define CLDMA_TXQ_NUM			8
#define FEATURE_COUNT		64
#define CLDMA_L2TISAR0_ALL_INT_MASK	GENMASK(15, 0)
#define CLDMA_L2RISAR0_ALL_INT_MASK	GENMASK(15, 0)
#define D2H_INT_ASYNC_AP_HK			BIT(15)
#define D2H_INT_ASYNC_MD_HK			BIT(16)
#define REG_EP2RC_SW_INT_EAP_MASK_SET		0x30
#define CLDMA0_PD_BASE			0x1021d000
#define CLDMA1_AO_BASE			0x1004b000
#define CLDMA1_PD_BASE			0x1021f000
#define CLDMA0_AO_BASE			0x10049000
#define REG_CLDMA_L2TISAR0		0x0810
#define REG_CLDMA_L2RISAR0		0x0850
#define EMPTY_STATUS_BITMASK		GENMASK(15, 8)
#define REG_CLDMA_L2TIMSR0		0x0828
#define REG_CLDMA_L2RIMSR0		0x0868
#define TXRX_STATUS_BITMASK		GENMASK(7, 0)
#define REG_CLDMA_DL_STOP_CMD		0x05c4
#define CLDMA_ALL_Q			GENMASK(7, 0)
#define REG_CLDMA_UL_STOP_CMD		0x0090
#define EQ_STA_BIT_OFFSET		8
#define REG_CLDMA_IP_BUSY		0x08b4
#define IP_BUSY_WAKEUP			BIT(0)
#define REG_CLDMA_L2TIMCR0		0x0820
#define REG_CLDMA_L2RIMCR0		0x0860
#define UL_CFG_BIT_MODE_MASK		GENMASK(7, 5)
#define REG_CLDMA_UL_CFG		0x0098
#define DL_MEM_CHECK_DIS		BIT(0)
#define REG_CLDMA_DL_MEM		0x0508
#define UL_CFG_BIT_MODE_36		BIT(5)
#define REG_CLDMA_UL_MEM		0x009c
#define UL_MEM_CHECK_DIS		BIT(0)
#define UL_CFG_BIT_MODE_64		BIT(7)
#define UL_CFG_BIT_MODE_40		BIT(6)
#define REG_CLDMA_DL_START_CMD		0x05bc
#define REG_CLDMA_UL_START_CMD		0x0088
#define REG_CLDMA_DL_START_ADDRL_0	0x0478
#define REG_CLDMA_UL_START_ADDRL_0	0x0004
#define CLDMA_Q_IDX_DUMP	1
#define DPMAIF_UL_CHK_BUSY			(BASE_DPMAIF_UL + 0x88)
#define DPMAIF_AO_DL_PIT_WR_IDX			(BASE_DPMAIF_PD_SRAM_DL + 0x60)
#define DPMAIF_DL_CHK_BUSY			(BASE_DPMAIF_DL + 0xb4)
#define DPMAIF_AO_DL_PIT_RD_IDX			(BASE_DPMAIF_PD_SRAM_DL + 0xec)
#define DPMAIF_DL_RD_WR_IDX_MSK			GENMASK(17, 0)
#define DPMAIF_AP_APDL_ALL_L2TISAR0_MASK	GENMASK(31, 0)
#define DPMAIF_AP_APDL_L2TISAR0			(BASE_DPMAIF_AP_MISC + 0x50)
#define DPMAIF_AP_L2TISAR0			(BASE_DPMAIF_AP_MISC + 0x00)
#define DPMAIF_AP_ALL_L2TISAR0_MASK		GENMASK(31, 0)
#define DPMAIF_DL_ADD_COUNT_MASK		GENMASK(15, 0)
#define DPMAIF_DL_ADD_UPDATE			BIT(31)
#define DPMAIF_DL_BAT_ADD			(BASE_DPMAIF_DL + 0x04)
#define DPMAIF_DL_FRG_ADD_UPDATE		BIT(16)
#define BASE_DPMAIF_UL				DPMAIF_PD_BASE
#define DPMAIF_UL_ALL_QUE_ARB_EN		GENMASK(11, 8)
#define DPMAIF_AO_UL_CHNL_ARB0			(BASE_DPMAIF_AO_UL + 0x1c)
#define DPMAIF_UL_IDLE_STS			BIT(11)
#define BASE_DPMAIF_PD_SRAM_DL			(DPMAIF_PD_BASE + 0xc00)
#define DPMAIF_DL_IDLE_STS			BIT(23)
#define BASE_DPMAIF_DL				(DPMAIF_PD_BASE + 0x100)
#define DPMAIF_DL_BAT_INIT_CON1			(BASE_DPMAIF_DL + 0x0c)
#define DPMAIF_BAT_EN_MSK			BIT(16)
#define DPMAIF_DL_BAT_INIT_ONLY_ENABLE_BIT	0
#define DPMAIF_DL_BAT_INIT_EN			BIT(31)
#define DPMAIF_DL_BAT_INIT_NOT_READY		BIT(31)
#define DPMAIF_DL_BAT_INIT			(BASE_DPMAIF_DL + 0x00)
#define DPMAIF_DL_INT_DLQ1_QDONE		BIT(9)
#define DPMAIF_DL_INT_DLQ0_QDONE		BIT(8)
#define DPMAIF_AO_UL_APDL_L2TIMCR0		(BASE_DPMAIF_AO_UL + 0x94)
#define RX_QUEUE_MAXLEN		32
#define BASE_DPMAIF_AP_MISC			(DPMAIF_PD_BASE + 0x400)
#define DPMAIF_AO_UL_AP_L2TIMSR0		(BASE_DPMAIF_AO_UL + 0x88)
#define DPMAIF_UDL_IP_BUSY			BIT(0)
#define DPMAIF_DL_INT_DLQ1_PITCNT_LEN		BIT(11)
#define DPMAIF_AO_AP_DLUL_IP_BUSY_MASK		(BASE_DPMAIF_AO_UL + 0x9c)
#define DPMAIF_DL_INT_Q2TOQ1			BIT(24)
#define DPMAIF_AO_UL_AP_L2TIMR0			(BASE_DPMAIF_AO_UL + 0x80)
#define DPMAIF_DL_INT_Q2APTOP			BIT(25)
#define DPMAIF_AO_UL_AP_L2TIMCR0			(BASE_DPMAIF_AO_UL + 0x84)
#define DPMAIF_DL_INT_DLQ0_PITCNT_LEN		BIT(10)
#define DPMAIF_AO_UL_APDL_L2TIMR0		(BASE_DPMAIF_AO_UL + 0x90)
#define DPMAIF_AP_IP_BUSY			(BASE_DPMAIF_AP_MISC + 0x60)
#define DPMAIF_AP_IP_BUSY_MASK			GENMASK(31, 0)
#define DPMAIF_AO_UL_APDL_L2TIMSR0		(BASE_DPMAIF_AO_UL + 0x98)
#define DPMAIF_HPC_INTR_MASK			(BASE_DPMAIF_MMW_HPC + 0x0f4)
#define DPMAIF_AO_UL_AP_L1TIMR0			(BASE_DPMAIF_AO_UL + 0x8c)
#define DPMA_HPC_ALL_INT_MASK			GENMASK(15, 0)
#define DPMAIF_UL_RESERVE_AO_RW			(BASE_DPMAIF_UL + 0xac)
#define DPMAIF_PCIE_MODE_SET_VALUE		0x55
#define DPMAIF_CG_EN				0x7f
#define DPMAIF_PORT_MODE_PCIE			BIT(30)
#define DPMAIF_AP_CG_EN				(BASE_DPMAIF_AP_MISC + 0x68)
#define DPMAIF_AO_DL_RDY_CHK_THRES		(BASE_DPMAIF_PD_SRAM_DL + 0x0c)
#define DPMAIF_DL_BAT_CACHE_PRI			BIT(22)
#define DPMAIF_DL_BURST_PIT_EN			BIT(13)
#define DPMAIF_AP_OVERWRITE_CFG			(BASE_DPMAIF_AP_MISC + 0x90)
#define DPMAIF_DL_INIT_DONE			BIT(0)
#define DPMAIF_AO_DL_INIT_SET			(BASE_DPMAIF_AO_DL + 0x00)
#define DPMAIF_AO_UL_INIT_SET			(BASE_DPMAIF_AO_UL + 0x0)
#define DPMAIF_UL_INIT_DONE			BIT(0)
#define DPMAIF_SRAM_SYNC			BIT(0)
#define DPMAIF_AO_DL_BAT_WR_IDX			(BASE_DPMAIF_PD_SRAM_DL + 0xdc)
#define DPMAIF_DL_ADD_NOT_READY			BIT(31)
#define DPMAIF_PD_BASE				0x1022d000
#define BASE_DPMAIF_AO_UL			DPMAIF_AO_BASE
#define BASE_DPMAIF_MMW_HPC			(DPMAIF_PD_BASE + 0x600)
#define DPMAIF_MEM_CLR				BIT(0)
#define DPMAIF_AP_MEM_CLR			(BASE_DPMAIF_AP_MISC + 0x94)
#define DPMAIF_AP_AO_RGU_ASSERT			0x10001140
#define DPMAIF_AP_RST_BIT			BIT(2)
#define DPMAIF_AP_AO_RGU_DEASSERT		0x10001144
#define DPMAIF_AP_RGU_ASSERT			0x10001150
#define DPMAIF_AP_AO_RST_BIT			BIT(6)
#define DPMAIF_AP_RGU_DEASSERT			0x10001154
#define DPMAIF_DL_PIT_SEQ_MSK			GENMASK(7, 0)
#define DPMAIF_AO_DL_PIT_SEQ_END		(BASE_DPMAIF_PD_SRAM_DL + 0x40)
#define DPMAIF_BAT_SIZE_MSK			GENMASK(15, 0)
#define DPMAIF_FRG_CHECK_THRES_MSK		GENMASK(7, 0)
#define DPMAIF_AO_DL_RDY_CHK_FRG_THRES		(BASE_DPMAIF_PD_SRAM_DL + 0x10)
#define DPMAIF_PIT_CHK_NUM_MSK			GENMASK(31, 24)
#define DPMAIF_AO_DL_PKTINFO_CON2		(BASE_DPMAIF_PD_SRAM_DL + 0x08)
#define DPMAIF_FRG_EN_MSK			BIT(28)
#define DPMAIF_AO_DL_PKTINFO_CON0		(BASE_DPMAIF_PD_SRAM_DL + 0x00)
#define DPMAIF_BAT_REMAIN_MINSZ_MSK		GENMASK(15, 8)
#define DPMAIF_BAT_REMAIN_SZ_BASE		16
#define DPMAIF_DL_BAT_INIT_CON0			(BASE_DPMAIF_DL + 0x08)
#define DPMAIF_DL_BAT_INIT_CON3			(BASE_DPMAIF_DL + 0x50)
#define DPMAIF_BAT_BUFFER_SZ_BASE		128
#define DPMAIF_BAT_BUF_SZ_MSK			GENMASK(16, 8)
#define DPMAIF_BAT_CHECK_THRES_MSK		GENMASK(21, 16)
#define DPMAIF_FRG_BUF_SZ_MSK			GENMASK(16, 8)
#define DPMAIF_FRG_BUFFER_SZ_BASE		128
#define DPMAIF_DL_BAT_INIT_ALLSET		BIT(0)
#define DPMAIF_DL_BAT_FRG_INIT			BIT(16)
#define DPMAIF_DL_PKT_CHECKSUM_EN		BIT(31)
#define DPMAIF_BAT_BID_MAXCNT_MSK		GENMASK(31, 16)
#define DPMAIF_AO_DL_PKTINFO_CON1		(BASE_DPMAIF_PD_SRAM_DL + 0x04)
#define DPMAIF_BAT_RSV_LEN_MSK			GENMASK(7, 0)
#define DPMAIF_PKT_ALIGN_EN			BIT(23)
#define DPMAIF_PKT_ALIGN_MSK			GENMASK(23, 22)
#define DPMAIF_ULQSAR_n(q)			(DPMAIF_AO_UL_CHNL0_CON0 + 0x10 * (q))
#define DPMAIF_UL_DRB_ADDRH_n(q)		(DPMAIF_AO_UL_CHNL0_CON2 + 0x10 * (q))
#define DPMAIF_DRB_SIZE_MSK			GENMASK(15, 0)
#define DPMAIF_UL_DRBSIZE_ADDRH_n(q)		(DPMAIF_AO_UL_CHNL0_CON1 + 0x10 * (q))
#define BASE_DPMAIF_AO_DL			(DPMAIF_AO_BASE + 0x400)
#define DPMAIF_AO_BASE				0x10014000
#define DPMAIF_AO_DL_DLQPIT_TIMEOUT0		(BASE_DPMAIF_PD_SRAM_DL + 0x24)
#define DPMAIF_MID_TIMEOUT_THRES_DF		100
#define DPMAIF_AO_DL_DLQPIT_INIT_CON5		(BASE_DPMAIF_AO_DL + 0x28)
#define DPMAIF_DLQ_HASH_BIT_CHOOSE_DF		0
#define DPMAIF_HPC_ADD_MODE_DF			0
#define DPMAIF_HASH_PRIME_DF			13
#define DPMAIF_AO_DL_HPC_CNTL			(BASE_DPMAIF_PD_SRAM_DL + 0x38)
#define DPMAIF_HPC_TOTAL_NUM			8
#define DPMAIF_HPC_DLQ_PATH_MODE		3
#define DPMAIF_AGG_MAX_LEN_DF			65535
#define DPMAIF_AGG_TBL_ENT_NUM_DF		50
#define DPMAIF_AO_DL_DLQ_AGG_CFG		(BASE_DPMAIF_PD_SRAM_DL + 0x20)
#define DPMAIF_AO_DL_DLQPIT_TRIG_THRES		(BASE_DPMAIF_AO_DL + 0x34)
#define DPMAIF_DLQ_PRS_THRES_DF			10
#define DPMAIF_HPC_MAX_TOTAL_NUM		8
#define DPMAIF_DLQ_TIMEOUT_THRES_DF		100
#define DPMAIF_DLQ_HIGH_TIMEOUT_THRES_MSK	GENMASK(31, 16)
#define DPMAIF_DLQ_LOW_TIMEOUT_THRES_MKS	GENMASK(15, 0)
#define DPMAIF_AO_DL_DLQPIT_TIMEOUT1		(BASE_DPMAIF_PD_SRAM_DL + 0x28)
#define DPMAIF_AO_UL_CHNL0_CON0			(BASE_DPMAIF_PD_SRAM_UL + 0x10)
#define DPMAIF_AO_UL_CHNL0_CON2			(BASE_DPMAIF_PD_SRAM_UL + 0x18)
#define DPMAIF_AO_UL_CHNL0_CON1			(BASE_DPMAIF_PD_SRAM_UL + 0x14)
#define DPMAIF_DL_DLQPIT_INIT_CON3		(BASE_DPMAIF_DL_DLQ_REMOVEAO_IDX + 0x20)
#define DPMAIF_DLQPIT_EN_MSK			BIT(20)
#define DPMAIF_DL_DLQPIT_INIT_CON2		(BASE_DPMAIF_DL_DLQ_REMOVEAO_IDX + 0x1c)
#define DPMAIF_DL_DLQPIT_INIT_CON1		(BASE_DPMAIF_DL_DLQ_REMOVEAO_IDX + 0x18)
#define DPMAIF_PIT_SIZE_MSK			GENMASK(17, 0)
#define DPMAIF_DL_DLQPIT_INIT_CON6		(BASE_DPMAIF_DL_DLQ_REMOVEAO_IDX + 0x2c)
#define DPMAIF_DL_DLQPIT_INIT_CON5		(BASE_DPMAIF_DL_DLQ_REMOVEAO_IDX + 0x28)
#define DPMAIF_DL_PIT_INIT_EN			BIT(31)
#define DPMAIF_DL_DLQPIT_INIT			(BASE_DPMAIF_DL_DLQ_REMOVEAO_IDX + 0x00)
#define DPMAIF_DL_PIT_INIT_NOT_READY		BIT(31)
#define DPMAIF_DLQPIT_CHAN_OFS			16
#define DPMAIF_DL_PIT_INIT_ALLSET		BIT(0)
#define DPMAIF_DL_DLQPIT_INIT_CON4		(BASE_DPMAIF_DL_DLQ_REMOVEAO_IDX + 0x24)
#define DPMAIF_DL_DLQPIT_INIT_CON0		(BASE_DPMAIF_DL_DLQ_REMOVEAO_IDX + 0x14)
#define BASE_DPMAIF_PD_SRAM_UL			(DPMAIF_PD_BASE + 0xd00)
#define BASE_DPMAIF_DL_DLQ_REMOVEAO_IDX		(DPMAIF_PD_BASE + 0x900)

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
    uint32_t ireg[T7XX_PCIE_REG_SIZE_CHIP / 4];
    uint32_t ereg[T7XX_PCIE_REG_SIZE_CHIP / 4];

    /* DMA Context */
    uint64_t cldma_dl_start_addr[8];
    uint64_t cldma_ul_start_addr[8];

    uint32_t misc_dev_status;
    uint32_t pcie_resource_status;
    uint32_t reset_state;
    uint32_t pm_resume_state;
};

enum t7xx_pm_state {
	MTK_PM_EXCEPTION,
	MTK_PM_INIT,
	MTK_PM_SUSPENDED,
	MTK_PM_RESUMED,
};
enum t7xx_mode {
	T7XX_UNKNOWN,
	T7XX_READY,
	T7XX_RESET,
	T7XX_FASTBOOT_SWITCHING,
	T7XX_FASTBOOT_DOWNLOAD,
	T7XX_FASTBOOT_DUMP,
	T7XX_MODE_LAST,
};
enum t7xx_pm_id {
	PM_ENTITY_ID_CTRL1,
	PM_ENTITY_ID_CTRL2,
	PM_ENTITY_ID_DATA,
	PM_ENTITY_ID_INVALID
};
enum reset_type {
	FLDR,
	PLDR,
	FASTBOOT,
};
enum t7xx_int {
	DPMAIF_INT,
	CLDMA0_INT,
	CLDMA1_INT,
	CLDMA2_INT,
	MHCCIF_INT,
	DPMAIF2_INT,
	SAP_RGU_INT,
	CLDMA3_INT,
};
enum t7xx_fsm_cmd_state {
	FSM_CMD_INVALID,
	FSM_CMD_START,
	FSM_CMD_EXCEPTION,
	FSM_CMD_PRE_STOP,
	FSM_CMD_STOP,
};
enum md_state {
	MD_STATE_INVALID,
	MD_STATE_WAITING_FOR_HS1,
	MD_STATE_WAITING_FOR_HS2,
	MD_STATE_READY,
	MD_STATE_EXCEPTION,
	MD_STATE_WAITING_TO_STOP,
	MD_STATE_STOPPED,
};
enum cldma_id {
	CLDMA_ID_MD,
	CLDMA_ID_AP,
	CLDMA_NUM
};
enum port_ch {
	PORT_CH_AP_CONTROL_RX = 0x1000,
	PORT_CH_AP_CONTROL_TX = 0x1001,
	PORT_CH_AP_ADB_RX = 0x100a,
	PORT_CH_AP_ADB_TX = 0x100b,
	PORT_CH_CONTROL_RX = 0x2000,
	PORT_CH_CONTROL_TX = 0x2001,
	PORT_CH_UART1_RX = 0x2006,
	PORT_CH_UART1_TX = 0x2008,
	PORT_CH_UART2_RX = 0x200a,
	PORT_CH_UART2_TX = 0x200c,
	PORT_CH_MD_LOG_RX = 0x202a,
	PORT_CH_MD_LOG_TX = 0x202b,
	PORT_CH_LB_IT_RX = 0x203e,
	PORT_CH_LB_IT_TX = 0x203f,
	PORT_CH_STATUS_RX = 0x2043,
	PORT_CH_MIPC_RX = 0x20ce,
	PORT_CH_MIPC_TX = 0x20cf,
	PORT_CH_MBIM_RX = 0x20d0,
	PORT_CH_MBIM_TX = 0x20d1,
	PORT_CH_DSS0_RX = 0x20d2,
	PORT_CH_DSS0_TX = 0x20d3,
	PORT_CH_DSS1_RX = 0x20d4,
	PORT_CH_DSS1_TX = 0x20d5,
	PORT_CH_DSS2_RX = 0x20d6,
	PORT_CH_DSS2_TX = 0x20d7,
	PORT_CH_DSS3_RX = 0x20d8,
	PORT_CH_DSS3_TX = 0x20d9,
	PORT_CH_DSS4_RX = 0x20da,
	PORT_CH_DSS4_TX = 0x20db,
	PORT_CH_DSS5_RX = 0x20dc,
	PORT_CH_DSS5_TX = 0x20dd,
	PORT_CH_DSS6_RX = 0x20de,
	PORT_CH_DSS6_TX = 0x20df,
	PORT_CH_DSS7_RX = 0x20e0,
	PORT_CH_DSS7_TX = 0x20e1,
	PORT_CH_UNIMPORTANT = 0xffff,
};
enum port_cfg_id {
	PORT_CFG_ID_INVALID,
	PORT_CFG_ID_NORMAL,
	PORT_CFG_ID_EARLY,
};
enum mtk_txrx {
	MTK_TX,
	MTK_RX,
};
enum t7xx_hw_mode {
	MODE_BIT_32,
	MODE_BIT_36,
	MODE_BIT_40,
	MODE_BIT_64,
};
enum t7xx_fsm_event_state {
	FSM_EVENT_INVALID,
	FSM_EVENT_MD_HS2,
	FSM_EVENT_AP_HS2,
	FSM_EVENT_MD_EX,
	FSM_EVENT_MD_EX_REC_OK,
	FSM_EVENT_MD_EX_PASS,
	FSM_EVENT_MD_HS2_EXIT,
	FSM_EVENT_AP_HS2_EXIT,
	FSM_EVENT_MAX
};
struct cldma_gpd {
	uint8_t flags;
	uint8_t not_used1;
	uint16_t rx_data_allow_len;
	uint32_t next_gpd_ptr_h;
	uint32_t next_gpd_ptr_l;
	uint32_t data_buff_bd_ptr_h;
	uint32_t data_buff_bd_ptr_l;
	uint16_t data_buff_len;
	uint16_t not_used2;
};
struct ccci_header {
	uint32_t packet_header;
	uint32_t packet_len;
	uint32_t status;
	uint32_t ex_msg;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    if (s->intr_status & ~s->intr_mask) {
        if (s->has_msix && msix_enabled(pdev)) {
            msix_notify(pdev, 0);
        } else if (s->has_msi && msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        pci_set_irq(pdev, 0);
    }
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    memset(s->ireg, 0, sizeof(s->ireg));
    memset(s->ereg, 0, sizeof(s->ereg));
    s->pm_resume_state = 0;
    s->intr_status = 0;
    s->intr_mask = 0xFFFFFFFF;
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_ireg_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr == T7XX_PCIE_RESOURCE_STATUS) {
        val = T7XX_PCIE_RESOURCE_STS_MSK;
    } else if (addr == T7XX_PCIE_PM_RESUME_STATE) {
        val = s->pm_resume_state;
    } else if (addr < T7XX_PCIE_REG_SIZE_CHIP) {
        val = s->ireg[addr / 4];
    }

    return val;
}

static void pcibase_ireg_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < T7XX_PCIE_REG_SIZE_CHIP) {
        s->ireg[addr / 4] = val;
    }

    if (addr == MSIX_ISTAT_HST_GRP0_0) {
        s->intr_status &= ~val;
        pcibase_update_irq(s);
    } else if (addr == IMASK_HOST_MSIX_CLR_GRP0_0) {
        s->intr_mask &= ~val;
        pcibase_update_irq(s);
    } else if (addr == IMASK_HOST_MSIX_SET_GRP0_0) {
        s->intr_mask |= val;
        pcibase_update_irq(s);
    }
}

static const MemoryRegionOps pcibase_ireg_ops = {
    .read = pcibase_ireg_read,
    .write = pcibase_ireg_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static uint64_t pcibase_ereg_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr < T7XX_PCIE_REG_SIZE_CHIP) {
        val = s->ereg[addr / 4];
    }

    return val;
}

static void pcibase_ereg_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < T7XX_PCIE_REG_SIZE_CHIP) {
        s->ereg[addr / 4] = val;
    }

    /* MHCCIF is at offset 0x24000 in EREG */
    if (addr == 0x24000 + REG_EP2RC_SW_INT_EAP_MASK_CLR) {
        s->intr_mask &= ~val;
        pcibase_update_irq(s);
    } else if (addr == 0x24000 + REG_EP2RC_SW_INT_EAP_MASK_SET) {
        s->intr_mask |= val;
        pcibase_update_irq(s);
    } else if (addr == 0x24000 + REG_RC2EP_SW_TCHNUM) {
        if (val == H2D_CH_DEVICE_RESET) {
            /* Trigger device reset */
            pcibase_reset(DEVICE(s));
        }
    } else if (addr == TOPRGU_CH_PCIE_IRQ_STA - T7XX_PCIE_REG_TRSL_ADDR_CHIP) {
        s->intr_status = 0;
        pcibase_update_irq(s);
    }
}

static const MemoryRegionOps pcibase_ereg_ops = {
    .read = pcibase_ereg_read,
    .write = pcibase_ereg_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }
    
    /* CRITICAL: PCI requires BAR sizes to be a power of 2. Prevent QEMU assert crash. */
    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        const MemoryRegionOps *ops = (bi->index == 0) ? &pcibase_ireg_ops : &pcibase_ereg_ops;
        memory_region_init_io(mr, OBJECT(s), ops, s, bi->name, aligned_size);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_MEDIATEK );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x4d75 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_COMMUNICATION_OTHER );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 6;
    for (int i = 0; i < 6; i++) {
        s->bar_info[i].type = BAR_TYPE_NONE;
    }
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = T7XX_PCIE_REG_SIZE_CHIP;
    s->bar_info[0].name = "t7xx-ireg";

    s->bar_info[2].index = 2;
    s->bar_info[2].type = BAR_TYPE_MMIO;
    s->bar_info[2].size = T7XX_PCIE_REG_SIZE_CHIP;
    s->bar_info[2].name = "t7xx-ereg";  
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msix = true;
    msix_init(pdev, EXT_INT_NUM, &s->bar_regions[0], 0, 0x3F0000, &s->bar_regions[0], 0, 0x3F1000, 0, errp);  
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
    .name = "mtk_t7xx_pci",
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
