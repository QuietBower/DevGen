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

/* Additional definitions for missing PCI IDs and IEEE802.11 AC constants */
#ifndef PCI_VENDOR_ID_MEDIATEK
#define PCI_VENDOR_ID_MEDIATEK 0x14c3
#endif

#define IEEE80211_AC_VO 0
#define IEEE80211_AC_VI 1
#define IEEE80211_AC_BE 2
#define IEEE80211_AC_BK 3

#define TYPE_PCIBASE_DEVICE "mt7603e_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define DRV_VENDOR_ID PCI_VENDOR_ID_MEDIATEK
#define DRV_DEVICE_ID 0x7603
#define DRV_CLASS_ID  PCI_CLASS_NETWORK_OTHER

/* All register offset macros from driver source */
#define MT_HW_CHIPID			0x70010200
#define MT_HW_REV			0x70010204
#define MT_INT_MASK_CSR			0x0204
#define MT_MMIO_SIZE			0x80000000
#define MT76_N_WCIDS 1088
#define mt76_hw(dev) (dev)->mphy.hw
#define mt76_dereference(p, dev) \
	rcu_dereference_protected(p, lockdep_is_held(&(dev)->mutex))
#define MT76_RNR_SCAN_MAX_BSSIDS       16
#define MT_DRV_SW_RX_AIRTIME		BIT(2)
#define MT_DRV_IGNORE_TXS_FAILED	BIT(6)
#define MT_PSE_RTA			MT_PSE(0x194)
#define MT_PSE_RTA_WRITE		BIT(16)
#define MT_PSE_RTA_BUSY			BIT(31)
#define MT_PSE_RTA_TAG_ID		GENMASK(15, 8)
#define MT7603_WTBL_SIZE	128
#define mt76_is_mmio(dev) ((dev)->bus->type == MT76_BUS_MMIO)
#define MT_PACKET_ID_HAS_RATE		BIT(7)
#define MT_PACKET_ID_NO_SKB		1
#define MT_PSE(ofs)			(MT_PSE_BASE + (ofs))
#define MT7603_EEPROM_SIZE	1024
#define MT_MCU_RING_SIZE	32
#define MT7603_RX_RING_SIZE     128
#define MT_TX_HW_QUEUE_BMC		8
#define MT_RX_BUF_SIZE		2048
#define MT_DELAY_INT_CFG		MT_HIF(0x210)
#define MT7603_TX_RING_SIZE	256
#define mt76_init_queues(dev, ...)		(dev)->mt76.queue_ops->init(&((dev)->mt76), __VA_ARGS__)
#define MT7603_MCU_RX_RING_SIZE	64
#define MT7603_PSD_RING_SIZE	128
#define MT_TX_HW_QUEUE_BCN		7
#define MT_TXTIME_THRESH(n)		(MT_TXTIME_THRESH_BASE + ((n) * 4))
#define MT_SCH_1			MT_HIF(0x588)
#define MT_GROUP_THRESH(n)		(MT_GROUP_THRESH_BASE + ((n) * 4))
#define MT_HIGH_PRIORITY_2		MT_HIF(0x5c0)
#define mt76xx_rev(dev) mt76_rev(&((dev)->mt76))
#define MT_PSE_FRP			MT_PSE(0x138)
#define MT_QUEUE_PRIORITY_1		MT_HIF(0x580)
#define MT_PSE_FC_P0			MT_PSE(0x120)
#define MT_PSE_FRP_P1			GENMASK(5, 3)
#define MT_PSE_FRP_P2_RQ2		GENMASK(14, 12)
#define MT_SCH_2			MT_HIF(0x58c)
#define MT_BMAP_1			MT_HIF(0x5b4)
#define MT_RSV_MAX_THRESH		MT_HIF(0x5c8)
#define MT_PSE_FC_P0_MAX_QUOTA		GENMASK(27, 16)
#define mt76_get_field(_dev, _reg, _field)		\
	FIELD_GET(_field, mt76_rr(dev, _reg))
#define MT_PAGE_COUNT(n)		(MT_PAGE_COUNT_BASE + ((n) * 4))
#define MT_PSE_FRP_P0			GENMASK(2, 0)
#define MT_SCH_4			MT_HIF(0x594)
#define MT_BMAP_0			MT_HIF(0x5b0)
#define MT_HIGH_PRIORITY_1		MT_HIF(0x5bc)
#define MT_PRIORITY_MASK		MT_HIF(0x5c4)
#define MT_QUEUE_PRIORITY_2		MT_HIF(0x584)

#define mt76_rmw_field(_dev, _reg, _field, _val)	\
	mt76_rmw(_dev, _reg, _field, FIELD_PREP(_field, _val))
#define MT_AGG_ARUCR			MT_WF_AGG(0x014)
#define MT_RXREQ			MT_WF_TMAC(0x0a0)
#define MT7603_WTBL_RESERVED	(MT7603_WTBL_SIZE - 1)
#define MT_DMA_TMCFR0			MT_WF_DMA(0x088)
#define MT_AGG_RETRY_CONTROL		MT_WF_AGG(0x0f4)
#define MT_WTBL_RMVTCR_RX_MV_MODE	BIT(23)
#define MT_CLIENT_RXINF_RXSH_GROUPS	GENMASK(2, 0)
#define MT_AGG_LIMIT_1			MT_WF_AGG(0x044)
#define MT_AGG_ARCR			MT_WF_AGG(0x010)
#define MT_AGG_ARCR_RTS_RATE_THR	GENMASK(12, 8)
#define MT_TMAC_PCR			MT_WF_TMAC(0x0b4)
#define MT_AGG_BA_SIZE_LIMIT_0		MT_WF_AGG(0x048)
#define MT_PSE_WTBL_2_PHYS_ADDR		0xa5000000
#define MT_AGG_PCR_RTS			MT_WF_AGG(0x054)
#define MT_DMA_TCFR0			MT_WF_DMA(0x080)
#define MT_AGG_PCR_RTS_PKT_THR		GENMASK(31, 25)
#define MT_WTBL_RMVTCR			MT_WTBL_OFF(0x008)
#define MT_CLIENT_BASE_PHYS_ADDR	0x800c0000
#define MT_TXREQ			MT_WF_TMAC(0x09c)
#define MT_DMA_VCFR0			MT_WF_DMA(0x07c)
#define MT_DMA_RCFR0			MT_WF_DMA(0x070)
#define MT_AGG_LIMIT			MT_WF_AGG(0x040)
#define MT_DMA_DCR1			MT_WF_DMA(0x004)
#define MT_AGG_ARDCR			MT_WF_AGG(0x018)
#define MT_SEC_SCR			MT_WF_SEC(0x004)
#define MT_LPON_BTEIR			MT_LPON(0x020)
#define MT_AGG_CONTROL			MT_WF_AGG(0x070)
#define MT_WF_RMACDR_MBSSID_MASK	GENMASK(25, 24)
#define MT_AGG_ARCR_RATE_DOWN_RATIO	GENMASK(17, 16)
#define MT_DMA_TCFR_TXS_AGGR_TIMEOUT	GENMASK(27, 16)
#define MT_DMA_DCR0_RX_VEC_DROP		BIT(17)
#define MT_LPON_SBTOR_TIME_OFFSET	GENMASK(19, 0)
#define MT_AGG_SIZE_LIMIT(_n)		(((_n) + 1) * 4)
#define MT_AGG_LIMIT_AC(_n)		GENMASK(((_n) + 1) * 8 - 1, (_n) * 8)
#define MT_WF_RMAC_MAXMINLEN		MT_WF_RMAC(0x098)
#define MT_TXREQ_CCA_SRC_SEL		GENMASK(31, 30)
#define MT_DMA_TCFR1			MT_WF_DMA(0x084)
#define MT_CLIENT_RXINF			0x068
#define MT_AGG_RETRY_CONTROL_RTS_LIMIT	GENMASK(11, 7)
#define MT7603_RATE_RETRY	2
#define MT_WF_RMAC_TMR_PA		MT_WF_RMAC(0x0e0)
#define MT_WF_RMACDR			MT_WF_RMAC(0x078)
#define MT_AGG_CONTROL_BAR_RATE		GENMASK(31, 20)
#define MT_LPON_BTEIR_MBSS_MODE		GENMASK(31, 29)
#define MT_AGG_CONTROL_CFEND_RATE	GENMASK(15, 4)
#define MT_WF_RMACDR_MAXLEN_20BIT	BIT(30)
#define MT_TMAC_TCR_TXOP_BURST_STOP	BIT(26)
#define MT_TMAC_TCR_RX_RIFS_MODE	BIT(23)
#define MT_TMAC_PCR_SPE_EN		BIT(23)
#define MT_AGG_BA_SIZE_LIMIT_1		MT_WF_AGG(0x04c)
#define MT_AGG_RETRY_CONTROL_BAR_LIMIT	GENMASK(15, 12)
#define MT_MCU_PCIE_REMAP_1		MT_MCU(0x500)
#define MT_AGG_ARCR_RATE_UP_EXTRA_TH	GENMASK(22, 20)
#define MT_AGG_CONTROL_NO_BA_AR_RULE	BIT(1)
#define MT_LPON_SBTOR(n)		MT_LPON(0x0a0)
#define MT_DMA_TCFR_TXS_AGGR_COUNT	GENMASK(12, 8)
#define MT_AGG_BA_SIZE_LIMIT_SHIFT	8
#define MT_SEC_SCR_MASK_ORDER		GENMASK(1, 0)
#define MT_AGG_ARxCR_LIMIT(_n)		GENMASK(2 + \
					MT_AGG_ARxCR_LIMIT_SHIFT(_n), \
					MT_AGG_ARxCR_LIMIT_SHIFT(_n))
#define MT_DMA_TCFR_TXS_BIT_MAP		GENMASK(6, 0)
#define MT_AGG_ARCR_RATE_DOWN_RATIO_EN	BIT(19)
#define MT_AGG_TMP			MT_WF_AGG(0x0d8)
#define MT_LED_STATUS_1(_n)		MT_LED_PHYS(0x14 + ((_n) * 8))
#define MT_LED_STATUS_0(_n)		MT_LED_PHYS(0x10 + ((_n) * 8))
#define mt76xx_chip(dev) mt76_chip(&((dev)->mt76))
#define MT_DRV_AMSDU_OFFLOAD		BIT(5)
#define mt76_for_each_q_rx(dev, i)	\
	for (i = 0; i < ARRAY_SIZE((dev)->q_rx); i++)		\
		if ((dev)->q_rx[i].ndesc)
#define mt76_wcid_ptr(dev, idx) __mt76_wcid_ptr(&(dev)->mt76, idx)
#define MT_PACKET_ID_NO_ACK		0
#define mt76_is_usb(dev) ((dev)->bus->type == MT76_BUS_USB)

#define mt76_wr_copy(dev, ...)	(dev)->mt76.bus->write_copy(&((dev)->mt76), __VA_ARGS__)
#define MT_WCID_TX_INFO_RATE		GENMASK(15, 0)
#define MT_WCID_TX_INFO_TXPWR_ADJ	GENMASK(25, 18)
#define MT_WCID_TX_INFO_NSS		GENMASK(17, 16)
#define MT_PACKET_ID_FIRST		3
#define MT_PACKET_ID_WED		2
#define MT_PACKET_ID_MASK		GENMASK(6, 0)
#define MT_WF_ARB_TX_START_0		MT_WF_ARB(0x100)
#define MT_WF_ARB_RQCR			MT_WF_ARB(0x070)
#define MT_WF_ARB_RQCR_RX_START		BIT(0)
#define MT_SCH_4_FORCE_QID		GENMASK(4, 0)
#define MT_TOP_MISC2			0x1134
#define MT7603_FIRMWARE_E1	"mt7603_e1.bin"
#define MT7603_FIRMWARE_E2	"mt7603_e2.bin"
#define MT_SCH_4_BYPASS			BIT(5)
#define MT7628_FIRMWARE_E1	"mt7628_e1.bin"
#define MT7628_FIRMWARE_E2	"mt7628_e2.bin"
#define mt76_tx_queue_skb_raw(dev, ...)	(dev)->mt76.queue_ops->tx_queue_skb_raw(&((dev)->mt76), __VA_ARGS__)
#define MT7603_WATCHDOG_TIMEOUT	10
#define MT_EFUSE_BASE			0x81070000
#define MT_HIF(ofs)			(MT_HIF_BASE + (ofs))
#define mt76_queue_alloc(dev, ...)	(dev)->mt76.queue_ops->alloc(&((dev)->mt76), __VA_ARGS__)
#define MT_WPDMA_GLO_CFG_FORCE_TX_EOF	BIT(25)
#define MT_CLIENT_RESET_TX_R_E_1_S	BIT(20)
#define MT_CLIENT_RESET_TX		0x070
#define MT_CLIENT_RESET_TX_R_E_2	BIT(17)
#define MT_WPDMA_GLO_CFG_SW_RESET	BIT(24)
#define MT_CLIENT_RESET_TX_R_E_1	BIT(16)
#define MT_CLIENT_RESET_TX_R_E_2_S	BIT(21)
#define MT_TXTIME_THRESH_BASE		MT_HIF(0x500)
#define MT_GROUP_THRESH_BASE		MT_HIF(0x598)
#define MT_PAGE_COUNT_BASE		MT_HIF(0x540)
#define MT_AGC_BASE			MT_WF_PHY(0x500)
#define MT_CLIENT_TMAC_INFO_TEMPLATE	0x040
#define MT_WTBL3_SIZE			(16 * 4)
#define MT_WTBL1_W2_ADMISSION_CONTROL	BIT(30)
#define MT_WTBL_UPDATE_ADM_COUNT_CLEAR	BIT(12)
#define MT_WTBL1_W0_ADDR_HI		GENMASK(15, 0)
#define MT_WTBL1_W0_RX_CHECK_A1		BIT(22)
#define MT_WTBL_UPDATE			MT_WTBL_OFF(0x000)
#define MT_WTBL4_SIZE			(8 * 4)
#define MT_WTBL1_W0_MUAR_IDX		GENMASK(21, 16)
#define MT_WTBL1_W1_ADDR_LO		GENMASK(31, 0)
#define MT_WTBL_UPDATE_WTBL2		BIT(11)
#define MT_WTBL2_SIZE			(16 * 4)
#define MT_WTBL_OFF(n)			(MT_WTBL_OFF_BASE + (n))
#define MT_WF_SEC(ofs)			(MT_WF_SEC_BASE + (ofs))
#define MT_LPON(n)			(MT_LPON_BASE + (n))
#define MT_WTBL1_W3_WTBL2_FRAME_ID	GENMASK(10, 0)
#define MT_WTBL1_W4_WTBL4_ENTRY_ID	GENMASK(22, 17)
#define MT_WTBL1_W4_WTBL3_FRAME_ID	GENMASK(10, 0)
#define MT_WTBL1_BASE			0x28000
#define MT_WTBL1_W3_WTBL2_ENTRY_ID	GENMASK(15, 11)
#define MT_WTBL_UPDATE_TX_COUNT_CLEAR	BIT(14)
#define MT_WTBL_UPDATE_RX_COUNT_CLEAR	BIT(15)
#define MT_WTBL1_SIZE			(8 * 4)
#define MT_WTBL1_W3_I_PSM		BIT(29)
#define MT_WTBL1_W3_WTBL4_FRAME_ID	GENMASK(26, 16)
#define MT_WTBL1_OR			(MT_WTBL1_BASE + 0x2300)
#define MT_WTBL3_OFFSET			(MT7603_WTBL_SIZE * MT_WTBL2_SIZE)
#define MT_WTBL1_W3_KEEP_I_PSM		BIT(28)
#define MT_WTBL1_OR_PSM_WRITE		BIT(31)
#define MT_WTBL1_W4_WTBL3_ENTRY_ID	GENMASK(16, 11)
#define MT_WTBL4_OFFSET			(MT7603_WTBL_SIZE * MT_WTBL3_SIZE + \
					 MT_WTBL3_OFFSET)
#define MT_WTBL1_W0_RX_VALID		BIT(28)
#define MT_WTBL1_W0_RX_CHECK_A2		BIT(29)
#define MT_MCU_PCIE_REMAP_2_BASE	GENMASK(31, 19)
#define MT_MCU_PCIE_REMAP_2		MT_MCU(0x504)
#define MT_MCU_PCIE_REMAP_2_OFFSET	GENMASK(18, 0)
#define MT_PCIE_REMAP_BASE_2		0x80000
#define MT_MCU(ofs)			(MT_MCU_BASE + (ofs))
#define MT_AGG_ARxCR_LIMIT_SHIFT(_n)	(4 * (_n))
#define MT_LED_PHYS(_n)			(MT_LED_BASE_PHYS + (_n))
#define MT_TX_CB_DMA_DONE		BIT(0)
#define MT_TX_CB_TXS_DONE		BIT(1)
#define MT_TX_STATUS_SKB_TIMEOUT	(HZ / 4)
#define MT_TX_CB_TXS_FAILED		BIT(2)
#define MT_TX_HW_QUEUE_PHY		GENMASK(3, 2)
#define MT_DRV_TXWI_NO_FREE		BIT(0)
#define MT_VEND_TYPE_MASK	(MT_VEND_TYPE_EEPROM | MT_VEND_TYPE_CFG)
#define MT_VEND_TYPE_CFG	BIT(30)
#define MT_VEND_TYPE_EEPROM	BIT(31)
#define MT_EFUSE_BASE_CTRL_EMPTY	BIT(30)
#define MT_EFUSE_BASE_CTRL		0x000
#define MT_HIF_BASE			0x4000
#define MT_WF_PHY_BASE			0x10000
#define MT_PCIE_REMAP_BASE_1		0x40000
#define MT_WF_ARB_TX_STOP_0		MT_WF_ARB(0x110)
#define MT_WTBL_UPDATE_WLAN_IDX		GENMASK(7, 0)
#define MT_WTBL_OFF_BASE		0x23000
#define MT_WF_SEC_BASE			0x21a00
#define MT_LPON_BASE			0x24000
#define MT_MCU_BASE			0x2000
#define MT_LED_BASE_PHYS		0x80024000
#define MT_QFLAG_WED		BIT(5)
#define MT_QFLAG_WED_RRO_EN	BIT(7)
#define MT_QFLAG_WED_RRO	BIT(6)
#define MT_QFLAG_WED_TYPE	GENMASK(4, 2)
#define RRO_IND_DATA1_MAGIC_CNT_MASK	GENMASK(31, 29)
#define MT_QFLAG_NPU		BIT(9)
#define MT_DRV_HW_MGMT_TXQ		BIT(4)
#define MT_MAX_NON_AQL_PKT	16
#define MT_TXQ_FREE_THR		32
#define mt76_wr_rp(dev, ...)	(dev)->mt76.bus->wr_rp(&((dev)->mt76), __VA_ARGS__)
#define mt76_is_sdio(dev) ((dev)->bus->type == MT76_BUS_SDIO)
#define MT_EFUSE_RDATA(_i)		(0x030 + ((_i) * 4))
#define MT_EFUSE_CTRL_VALID		BIT(29)
#define MT_DRV_TX_ALIGNED4_SKBS		BIT(1)
#define MT_WF_PHY_BASE			0x10000
#define MT_QFLAG_EMI_EN		BIT(8)
#define MT_WCID_TX_INFO_SET		BIT(31)
#define mt76_rd_rp(dev, ...)	(dev)->mt76.bus->rd_rp(&((dev)->mt76), __VA_ARGS__)
#define MT_QFLAG_WED_RING	GENMASK(1, 0)

/* Supplementary base address macros from supplementary driver source */
/* Band-aware base definitions */
#define MT_WF_TMAC_BASE(_band)		((_band) ? 0x820f4000 : 0x820e4000)
#define MT_WF_AGG_BASE(_band)		((_band) ? 0x820f2000 : 0x820e2000)
#define MT_WF_DMA_BASE(_band)		((_band) ? 0x820f7000 : 0x820e7000)
#define MT_WF_RMAC_BASE(_band)		((_band) ? 0x820f5000 : 0x820e5000)
#define MT_WF_ARB_BASE(_band)		((_band) ? 0x820f3000 : 0x820e3000)

/* The band-aware register offset macros (MT_WF_TMAC, MT_WF_AGG, etc.) are missing; 
   they are listed in needed_sources. Without them, the following register offset 
   macros that depend on them are disabled. */
#if 0 /* Requires band-aware macros */
#define MT_AGG_ARUCR			MT_WF_AGG(0x014)
#define MT_RXREQ			MT_WF_TMAC(0x0a0)
#define MT_DMA_TMCFR0			MT_WF_DMA(0x088)
#define MT_AGG_RETRY_CONTROL		MT_WF_AGG(0x0f4)
#define MT_AGG_LIMIT_1			MT_WF_AGG(0x044)
#define MT_AGG_ARCR			MT_WF_AGG(0x010)
#define MT_TMAC_PCR			MT_WF_TMAC(0x0b4)
#define MT_AGG_BA_SIZE_LIMIT_0		MT_WF_AGG(0x048)
#define MT_AGG_PCR_RTS			MT_WF_AGG(0x054)
#define MT_DMA_TCFR0			MT_WF_DMA(0x080)
#define MT_TXREQ			MT_WF_TMAC(0x09c)
#define MT_DMA_VCFR0			MT_WF_DMA(0x07c)
#define MT_DMA_RCFR0			MT_WF_DMA(0x070)
#define MT_AGG_LIMIT			MT_WF_AGG(0x040)
#define MT_DMA_DCR1			MT_WF_DMA(0x004)
#define MT_AGG_ARDCR			MT_WF_AGG(0x018)
#define MT_AGG_CONTROL			MT_WF_AGG(0x070)
#define MT_AGG_ARCR_RATE_DOWN_RATIO	GENMASK(17, 16)
#define MT_DMA_TCFR_TXS_AGGR_TIMEOUT	GENMASK(27, 16)
#define MT_AGG_ARCR_RATE_DOWN_RATIO_EN	BIT(19)
#define MT_AGG_TMP			MT_WF_AGG(0x0d8)
#define MT_WF_RMAC_MAXMINLEN		MT_WF_RMAC(0x098)
#define MT_DMA_TCFR1			MT_WF_DMA(0x084)
#define MT_WF_RMAC_TMR_PA		MT_WF_RMAC(0x0e0)
#define MT_WF_RMACDR			MT_WF_RMAC(0x078)
#define MT_AGG_CONTROL_BAR_RATE		GENMASK(31, 20)
#define MT_AGG_CONTROL_CFEND_RATE	GENMASK(15, 4)
#define MT_AGG_BA_SIZE_LIMIT_1		MT_WF_AGG(0x04c)
#define MT_AGG_CONTROL_NO_BA_AR_RULE	BIT(1)
#define MT_WF_ARB_TX_START_0		MT_WF_ARB(0x100)
#define MT_WF_ARB_RQCR			MT_WF_ARB(0x070)
#define MT_WF_ARB_TX_STOP_0		MT_WF_ARB(0x110)
#endif

/* Enum definitions */
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
    uint32_t reg_chip_id; /* read from MT_HW_CHIPID, write ignored */
    uint32_t reg_rev;     /* read from MT_HW_REV, write ignored */
    uint32_t int_mask_csr; /* interrupt mask register */
};

/* Additional enum definitions from driver */
enum mt76_hwrro_mode {
	MT76_HWRRO_OFF,
	MT76_HWRRO_V3,
	MT76_HWRRO_V3_1,
};
enum mt76_bus_type {
	MT76_BUS_MMIO,
	MT76_BUS_USB,
	MT76_BUS_SDIO,
};
enum mt76_testmode_state {
	MT76_TM_STATE_OFF,
	MT76_TM_STATE_IDLE,
	MT76_TM_STATE_TX_FRAMES,
	MT76_TM_STATE_RX_FRAMES,
	MT76_TM_STATE_TX_CONT,
	MT76_TM_STATE_ON,
	/* keep last */
	NUM_MT76_TM_STATES,
	MT76_TM_STATE_MAX = NUM_MT76_TM_STATES - 1,
};
enum mt76_txq_id {
	MT_TXQ_VO = IEEE80211_AC_VO,
	MT_TXQ_VI = IEEE80211_AC_VI,
	MT_TXQ_BE = IEEE80211_AC_BE,
	MT_TXQ_BK = IEEE80211_AC_BK,
	MT_TXQ_PSD,
	MT_TXQ_BEACON,
	MT_TXQ_CAB,
	__MT_TXQ_MAX
};
enum mt76_rxq_id {
	MT_RXQ_MAIN,
	MT_RXQ_MCU,
	MT_RXQ_MCU_WA,
	MT_RXQ_BAND1,
	MT_RXQ_BAND1_WA,
	MT_RXQ_MAIN_WA,
	MT_RXQ_BAND2,
	MT_RXQ_BAND2_WA,
	MT_RXQ_RRO_BAND0,
	MT_RXQ_RRO_BAND1,
	MT_RXQ_RRO_BAND2,
	MT_RXQ_MSDU_PAGE_BAND0,
	MT_RXQ_MSDU_PAGE_BAND1,
	MT_RXQ_MSDU_PAGE_BAND2,
	MT_RXQ_TXFREE_BAND0,
	MT_RXQ_TXFREE_BAND1,
	MT_RXQ_TXFREE_BAND2,
	MT_RXQ_RRO_IND,
	MT_RXQ_RRO_RXDMAD_C,
	MT_RXQ_NPU0,
	MT_RXQ_NPU1,
	__MT_RXQ_MAX
};
enum mt76_sta_event {
	MT76_STA_EVENT_ASSOC,
	MT76_STA_EVENT_AUTHORIZE,
	MT76_STA_EVENT_DISASSOC,
};
enum mt76_dfs_state {
	MT_DFS_STATE_UNKNOWN,
	MT_DFS_STATE_DISABLED,
	MT_DFS_STATE_CAC,
	MT_DFS_STATE_ACTIVE,
};

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* MMIO read: return shadow register values for known addresses */
    switch (addr) {
    case MT_HW_CHIPID:
        val = s->reg_chip_id;
        break;
    case MT_HW_REV:
        val = s->reg_rev;
        break;
    case MT_INT_MASK_CSR:
        val = s->int_mask_csr;
        break;
    default:
        /* For all other registers, return 0 */
        val = 0;
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* MMIO write: handle known registers, ignore others */
    switch (addr) {
    case MT_HW_CHIPID:
    case MT_HW_REV:
        /* read-only, ignore writes */
        break;
    case MT_INT_MASK_CSR:
        s->int_mask_csr = val;
        break;
    default:
        /* Silently ignore writes to unknown registers */
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    uint64_t val = 0;
    /* PIO read not used */
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* PIO write not used */
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
    /* Reset registers to defaults */
    s->reg_chip_id = 0x7603; /* MT7603 chip ID */
    s->reg_rev = 0x00000001; /* revision */
    s->int_mask_csr = 0;
    s->intr_status = 0;
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

    pci_set_word(pci_conf + PCI_VENDOR_ID, DRV_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, DRV_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, DRV_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = MT_MMIO_SIZE;
    s->bar_info[0].name = "mt7603-mmio";
    s->num_bars = 1;
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Set initial register states */
    s->reg_chip_id = 0x7603;
    s->reg_rev = 0x00000001;
    s->int_mask_csr = 0;
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
    /* No additional cleanup */
}

static const VMStateDescription vmstate_pcibase = {
    .name = "mt7603e_pci",
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
    dc->categories = DEVICE_CATEGORY_MISC;
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
