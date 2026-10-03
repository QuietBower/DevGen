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
#include "qemu/bitops.h"

#define TYPE_PCIBASE_DEVICE "mt7996e_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */

/* PCI IDs */
#define PCI_VENDOR_ID_MEDIATEK 0x14c3
#define MT7996_DEVICE_ID 0x7990

/* Register base addresses */
#define MT_AFE_CTL_BASE				0x18043000
#define MT_WF_PHYRX_CSD_BASE			0x83000000
#define MT_WF_PHYRX_BAND_BASE			0x83080000
#define MT_RRO_TOP_BASE				0xA000

/* Derived register macros */
#define MT_AFE_CTL_BAND(_band, ofs)		(MT_AFE_CTL_BASE + ((_band) * 0x1000) + (ofs))
#define MT_WF_PHYRX_CSD(_band, _wf, ofs)	(MT_WF_PHYRX_CSD_BASE + ((_band) << 20) + ((_wf) << 16) + (ofs))
#define MT_WF_PHYRX_BAND(_band, ofs)		(MT_WF_PHYRX_BAND_BASE + ((_band) << 20) + (ofs))
#define MT_RRO_TOP(ofs)				(MT_RRO_TOP_BASE + (ofs))

/* Register offsets and bit definitions */
#define MT7990_DEVICE_ID_2		0x799b
#define MT7996_DEVICE_ID_2		0x7991
#define MT7992_DEVICE_ID_2		0x799a
#define MT7992_DEVICE_ID		0x7992
#define MT7990_DEVICE_ID		0x7993
#define MT7996_DEVICE_ID		0x7990
#define MT76_N_WCIDS 1088
#define MT_WF_L05_RST				0x70028550
#define MT_WF_SUBSYS_RST_WHOLE_PATH_RST		BIT(0)
#define MT_WF_SUBSYS_RST_BYPASS_WFDMA_SLP_PROT	BIT(6)
#define MT_WF_SUBSYS_RST_BYPASS_WFDMA2_SLP_PROT	BIT(16)
#define MT_WF_SUBSYS_RST_WHOLE_PATH_RST_REVERT_CYCLE	GENMASK(15, 8)
#define MT_WF_SUBSYS_RST_WHOLE_PATH_RST_REVERT	BIT(5)
#define MT_WF_L05_RST_WF_RST_MASK		GENMASK(4, 0)
#define MT_INT_RX_DONE_MSDU_PG_BAND0		BIT(18)
#define MT_INT_RX_DONE_WA_TRI			BIT(3)
#define MT_INT_RX_TXFREE_BAND1_EXT		BIT(19)
#define MT_INT_RX_DONE_RRO_BAND1		BIT(17)
#define MT_INT_TX_DONE_BAND2			BIT(15)
#define MT_INT_RX_DONE_MSDU_PG_BAND2		BIT(23)
#define MT_INT_RX_DONE_RRO_BAND0		BIT(16)
#define MT_INT_RX_TXFREE_EXT			BIT(26)
#define MT7996_HW_TOKEN_SIZE		8192
#define MT_INT_RX_DONE_MSDU_PG_BAND1		BIT(19)
#define MT_RX_BUF_SIZE		2048
#define MT_INT_RX_TXFREE_MAIN			BIT(17)
#define MT_INT_RX_DONE_BAND2			BIT(13)
#define MT7996_TOKEN_SIZE		16384
#define MT_TXD_SIZE				(8 * 4)
#define MT_DRV_TXWI_NO_FREE		BIT(0)
#define MT_DRV_AMSDU_OFFLOAD		BIT(5)
#define MT_DRV_HW_MGMT_TXQ		BIT(4)
#define MT7996_MAX_RADIOS		3
#define MT7996_MAX_QUEUE		(__MT_RXQ_MAX +	__MT_MCUQ_MAX + 3)
#define MT7996_RRO_BA_BITMAP_LEN	2
#define MT7996_NPU_TXD_SIZE		3
#define MT7996_RRO_MSDU_PG_HASH_SIZE	127
#define MT7996_RRO_MSDU_PG_CR_CNT	8
#define MT7996_RRO_ADDR_ELEM_LEN	128
#define MT_RXD0_SW_PKT_TYPE_MAP		0x380F
#define MT_TXS_HDR_SIZE			4
#define MT_RXD0_SW_PKT_TYPE_FRAME	0x3801
#define MT_TXS_SIZE			12
#define MT_RXD0_SW_PKT_TYPE_MASK	GENMASK(31, 16)
#define MT7996_WATCHDOG_TIME		(HZ / 10)
#define RRO_HIF_DATA4_RX_TOKEN_ID_MASK	GENMASK(15, 0)
#define RRO_HIF_DATA1_SDL_MASK		GENMASK(29, 16)
#define MT7996_RRO_MAX_SESSION		1024
#define MT7996_RRO_WINDOW_MAX_LEN	1024
#define RRO_IND_DATA0_START_SEQ_MASK	GENMASK(27, 16)
#define RRO_IND_DATA1_IND_COUNT_MASK	GENMASK(12, 0)
#define RRO_HIF_DATA1_LS_MASK		BIT(30)
#define MSDU_PAGE_INFO_PG_HIGH_MASK	GENMASK(3, 0)
#define MT_RRO_ACK_SN_CTRL_SESSION_MASK		GENMASK(11, 0)
#define RRO_IND_DATA0_IND_REASON_MASK	GENMASK(31, 28)
#define MT_RRO_ACK_SN_CTRL_SN_MASK		GENMASK(27, 16)
#define RRO_IND_DATA0_SEQ_ID_MASK	GENMASK(11, 0)
#define MT_RRO_ACK_SN_CTRL			MT_RRO_TOP(0x50)
#define WED_RRO_ADDR_COUNT_MASK		GENMASK(14, 4)
#define MT7996_MAX_HIF_RXD_IN_PG	5
#define WED_RRO_ADDR_SIGNATURE_MASK	GENMASK(31, 24)
#define WED_RRO_ADDR_HEAD_HIGH_MASK	GENMASK(3, 0)
#define MT_TXP_MAX_BUF_NUM		6
#define MT_TXP3_DMA_ADDR_H		GENMASK(13, 12)
#define MT_TXP_DMA_ADDR_H		GENMASK(15, 12)
#define MT_TXP_BUF_LEN			GENMASK(11, 0)
#define MT_PACKET_ID_FIRST		3
#define MT_TXP0_TOKEN_ID0		GENMASK(14, 0)
#define MT_TXD7_MAC_TXD			BIT(27)
#define MT_TXP0_TOKEN_ID0_VALID_MASK	BIT(15)
#define MT_TXP3_ML0_MASK		BIT(15)
#define MT_TXP1_TID_ADDBA		GENMASK(14, 12)
#define MSDU_PAGE_INFO_OWNER_MASK	BIT(31)
#define MT7996_BASIC_RATES_TBL		31
#define MT7996_WTBL_RESERVED		(mt7996_wtbl_size(dev) - 1)
#define MT_WCID_TX_INFO_SET		BIT(31)
#define MT7996_NPU_TX_RING_SIZE		1024
#define MT7996_TX_RING_SIZE		2048
#define MT7996_MAX_TEMP_IDX		1
#define MT7996_CRIT_TEMP		110
#define MT7996_CRIT_TEMP_IDX		0
#define MT7996_MAX_TEMP			120
#define MT_AFE_CTL_BAND_PLL_03(_band)		MT_AFE_CTL_BAND(_band, 0x2c)
#define MT_AFE_CTL_BAND_PLL_03_MSB_EN		BIT(1)
#define MT7996_RRO_BA_BITMAP_SESSION_SIZE	(MT7996_RRO_MAX_SESSION / MT7996_RRO_ADDR_ELEM_LEN)
#define MT_HW_TXP_MAX_BUF_NUM		4
#define MT_HW_TXP_MAX_MSDU_NUM		4
#define MT_TXD6_DAS			BIT(2)
#define MT7996_MAX_WMM_SETS		4
#define MT_TX_HW_QUEUE_PHY		GENMASK(3, 2)
#define MT_TXD6_MSDU_CNT		GENMASK(9, 4)
#define MT_TXD1_FIXED_RATE		BIT(31)
#define MT_TXD6_VTA			BIT(28)
#define MT_TXD6_MSDU_CNT_V2		GENMASK(15, 10)
#define MT_TXD6_DIS_MAT			BIT(3)
#define MT76_TOKEN_FREE_THR	64
#define MT_PACKET_ID_NO_ACK		0
#define MT_PACKET_ID_WED		2
#define MT_PACKET_ID_MASK		GENMASK(6, 0)
#define MT7996_MAX_INTERFACES		19
#define MT7996_WTBL_BMC_SIZE		64
#define MT_TXD9_WLAN_IDX		GENMASK(23, 8)
#define MT_TXS3_RATE_STBC		BIT(7)
#define MT_WF_PHYRX_CSD_BAND_RX_CTRL1(_band)	MT_WF_PHYRX_BAND(_band, 0x2004)
#define MT_WF_PHYRX_CSD_BAND_RX_CTRL1_STSCNT_EN	GENMASK(11, 9)
#define MT_RXD0_MHCP			BIT(19)
#define MT_RXD3_NORMAL_FCS_ERR		BIT(24)
#define MT_RXD0_MESH			BIT(18)
#define MT_RXD10_SEQ_CTRL		GENMASK(15, 0)
#define MT_RXD3_NORMAL_UDP_TCP_SUM	BIT(27)
#define MT_RXD8_FRAME_CONTROL		GENMASK(15, 0)
#define MT_RXD3_NORMAL_IP_SUM		BIT(26)
#define MT_RXD2_NORMAL_SEC_MODE		GENMASK(20, 16)
#define MT_RXD10_QOS_CTL		GENMASK(31, 16)
#define MT_DRV_SW_RX_AIRTIME		BIT(2)
#define MT_DRV_IGNORE_TXS_FAILED	BIT(6)
#define MT7996_MAX_STA_TWT_AGRT		8
#define MT_PAD_GPIO_ADIE_SINGLE		BIT(15)
#define MT_PAD_GPIO			0x700056f0
#define MT_PAD_GPIO_ADIE_COMB		GENMASK(16, 15)
#define MT_PAD_GPIO_2ADIE_TBTC		BIT(19)
#define MT7996_EEPROM_BLOCK_SIZE	16
#define MT7996_EXT_EEPROM_BLOCK_SIZE	1024
#define MT7996_EEPROM_SIZE		7680
#define MT_LED_CTRL_BLINK_BAND_SEL		BIT(4)
#define MT_WCID_TX_INFO_RATE		GENMASK(15, 0)
#define MT_WCID_TX_INFO_TXPWR_ADJ	GENMASK(25, 18)
#define MT_WCID_TX_INFO_NSS		GENMASK(17, 16)
#define MT7996_IBF_MAX_NC		2

/* Queue and interrupt routing macros (only those fully defined) */
#define MT_INT_RX_DONE_BAND2_EXT		BIT(23)
#define MT_INT_RX_TXFREE_BAND1_MT7990		BIT(15)
#define MT_INT_RX_DONE_RRO_RXDMAD_C		BIT(11)
#define MT_INT_RX_DONE_RRO_IND			BIT(11)
#define MT_INT_RX_TXFREE_TRI			BIT(15)
#define MT_INT_RX_TXFREE_BAND0_MT7990		BIT(14)
#define MT_RRO_3_1_GLOBAL_CONFIG		MT_RRO_TOP(0x604)
#define MT_RRO_BA_BITMAP_BASE1			MT_RRO_TOP(0xC)
#define MT_RRO_3_1_GLOBAL_CONFIG_RX_DIDX_WR_EN	BIT(2)
#define MT_RRO_3_1_GLOBAL_CONFIG_RXDMAD_SEL	BIT(6)
#define MT_RRO_3_0_EMU_CONF			MT_RRO_TOP(0x600)
#define MT_RRO_BA_BITMAP_BASE_EXT1		MT_RRO_TOP(0x74)
#define MT_RRO_HOST_INT_ENA			MT_RRO_TOP(0x204)
#define MT_RRO_ADDR_ELEM_SEG_ADDR0		MT_RRO_TOP(0x400)
#define MT_RRO_ADDR_ARRAY_BASE1			MT_RRO_TOP(0x34)
#define MT_RRO_BA_BITMAP_BASE_EXT0		MT_RRO_TOP(0x70)
#define MT_RRO_MSDU_PG_SEG_ADDR0		MT_RRO_TOP(0x620)
#define MT_RRO_ADDR_ARRAY_BASE0			MT_RRO_TOP(0x30)
#define MT_RRO_3_1_GLOBAL_CONFIG_RX_CIDX_RD_EN	BIT(3)
#define WF_RRO_AXI_MST_CFG_DIDX_OK		BIT(12)
#define MT_RRO_RX_RING_AP_DIDX_ADDR		MT_RRO_TOP(0x6f4)
#define MT_RRO_BA_BITMAP_BASE0			MT_RRO_TOP(0x8)
#define WF_RRO_AXI_MST_CFG			MT_RRO_TOP(0xB8)
#define MT_RRO_ADDR_ARRAY_ELEM_ADDR_SEG_MODE	BIT(31)
#define MT_RRO_HOST_INT_ENA_HOST_RRO_DONE_ENA   BIT(0)
#define MT_RRO_3_1_GLOBAL_CONFIG_INTERLEAVE_EN	BIT(0)
#define MT_RRO_RX_RING_AP_CIDX_ADDR		MT_RRO_TOP(0x6f0)
#define MT_RRO_3_0_EMU_CONF_EN_MASK		BIT(11)
#define MT_RXQ_RRO_IND_RING_BASE		MT_RRO_TOP(0x40)
#define MT_RXQ_RRO_AP_RING_BASE			MT_RRO_TOP(0x650)
#define MT_RRO_IND_CMD_SIGNATURE_BASE1		MT_RRO_TOP(0x3C)
#define MT_RRO_PARTICULAR_CFG1			MT_RRO_TOP(0x60)
#define MT_RRO_IND_CMD_SIGNATURE_BASE0		MT_RRO_TOP(0x38)
#define MT_RRO_PARTICULAR_SID			GENMASK(30, 16)
#define MT_RRO_PARTICULAR_CFG0			MT_RRO_TOP(0x5C)
#define MT_RRO_IND_CMD_SIGNATURE_BASE1_EN	BIT(31)
#define MT_RRO_PARTICULAR_CONFG_EN		BIT(31)
#define MT_TXD_LEN_MASK			GENMASK(11, 0)
#define MT_TXD_LEN_MSDU_LAST		BIT(14)
#define MT_TXD_LEN_LAST			BIT(15)
#define RRO_IND_DATA1_MAGIC_CNT_MASK	GENMASK(31, 29)
#define MT_QFLAG_WED		BIT(5)
#define MT_PRXV_HE_RU_ALLOC		GENMASK(30, 22)
#define MT_CRXV_HE_RU3_H		GENMASK(3, 0)
#define MT_CRXV_HE_RU3_L		GENMASK(31, 27)
#define MT_PRXV_HT_SHORT_GI		GENMASK(4, 3)
#define MT_QFLAG_WED_RRO_EN	BIT(7)
#define MT_QFLAG_WED_TYPE	GENMASK(4, 2)
#define MT_QFLAG_WED_RING	GENMASK(1, 0)
#define MT_QFLAG_EMI_EN		BIT(8)
#define MT_QFLAG_WED		BIT(5)
#define MT_QFLAG_WED_RRO	BIT(6)
#define MT_QFLAG_NPU		BIT(9)

/* New register offsets from supplementary source */
#define MT_PCIE_RECOG_ID		0xd7090
#define MT_PCIE_RECOG_ID_SEM		BIT(31)
#define MT_PCIE_RECOG_ID_MASK		GENMASK(30, 0)
#define MT_INT_MASK_CSR			0x0204
#define MT_PCIE_MAC_INT_ENABLE		0x74020188  /* MT_PCIE_MAC(0x188) base unknown, using literal */
#define MT_INT1_MASK_CSR		0x0         /* placeholder, __REG(INT1_MASK_CSR) undefined */
#define MT_PCIE1_MAC_INT_ENABLE		0x74020188
#define MT_HW_CHIPID			0x1008

/* Enum for hardware RRO mode */
enum mt76_hwrro_mode {
	MT76_HWRRO_OFF,
	MT76_HWRRO_V3,
	MT76_HWRRO_V3_1,
};

/* Queue index enums used by hardware */
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

enum mt76_txq_id {
	MT_TXQ_VO = 0,
	MT_TXQ_VI = 1,
	MT_TXQ_BE = 2,
	MT_TXQ_BK = 3,
	MT_TXQ_PSD,
	MT_TXQ_BEACON,
	MT_TXQ_CAB,
	__MT_TXQ_MAX
};

/* Hardware register layout structures */
struct mt76_queue_regs {
	uint32_t desc_base;
	uint32_t ring_size;
	uint32_t cpu_idx;
	uint32_t dma_idx;
};

/* Other hardware-related enums */
enum mt76_bus_type {
	MT76_BUS_MMIO,
	MT76_BUS_USB,
	MT76_BUS_SDIO,
};

enum mt76_band_id {
	MT_BAND0,
	MT_BAND1,
	MT_BAND2,
	__MT_MAX_BAND
};

enum mt76_mcuq_id {
	MT_MCUQ_WM,
	MT_MCUQ_WA,
	MT_MCUQ_FWDL,
	__MT_MCUQ_MAX
};

typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM 
} BARType;

typedef struct {
    int     index;
    BARType type;
    hwaddr  size;
    const char *name;
} BARInfo;

#define MMIO_REG_SIZE 0x100000  /* 1 MB */
#define MMIO_NUM_REGS (MMIO_REG_SIZE / sizeof(uint32_t))

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Generic MMIO register storage */
    uint32_t mmio_regs[MMIO_NUM_REGS];
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    /* Placeholder for future implementation */
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /* Placeholder for future implementation */
}

/* MMIO/PIO Handlers (behavioral logic will be added in Phase 2) */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t index = addr >> 2;
    uint64_t val = 0;

    if (index < MMIO_NUM_REGS) {
        val = s->mmio_regs[index];
        /* Special handling for registers that need side effects on read */
        /* Currently none */
    } else {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: out-of-bounds MMIO read at 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t index = addr >> 2;

    if (index < MMIO_NUM_REGS) {
        /* Store the value */
        s->mmio_regs[index] = (uint32_t)val;

        /* Special handling for registers with side effects */
        /* Example: handle reset registers */
        if (addr == MT_WF_L05_RST) {
            /* Simulate reset: write to this register triggers a subsystem reset.
             * For emulation, we just store the value; no actual reset needed for probe. */
            qemu_log_mask(LOG_GUEST_ERROR, "%s: WF_RESET written 0x%x\n", __func__, (uint32_t)val);
        }
    } else {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: out-of-bounds MMIO write at 0x%" HWADDR_PRIx ", val 0x%" PRIx64 "\n",
                      __func__, addr, val);
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* No PIO used */
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* No PIO used */
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

    /* Reset: clear MMIO registers */
    memset(s->mmio_regs, 0, sizeof(s->mmio_regs));

    /* Set any required default values that the driver expects */
    /* No known registers need initialization yet */
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

    pci_set_word(pci_conf + PCI_VENDOR_ID, 0x14c3);
    pci_set_word(pci_conf + PCI_DEVICE_ID, 0x7990);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x028000);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization: one MMIO BAR, size 1MB */
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = 0x100000, .name = "mt7996-bar0" };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize MMIO registers to zero */
    memset(s->mmio_regs, 0, sizeof(s->mmio_regs));

    /* No MSI/MSI-X init based on provided driver source */
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

static const VMStateDescription vmstate_pcibase = {
    .name = "mt7996e_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32_ARRAY(mmio_regs, PCIBaseState, MMIO_NUM_REGS),
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
