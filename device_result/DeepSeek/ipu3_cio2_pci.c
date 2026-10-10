/* QEMU device model for Intel IPU3 CIO2 (ipu3-cio2) */

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

#define PAGE_SIZE 4096

#define TYPE_PCIBASE_DEVICE "ipu3_cio2_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define CIO2_PCI_ID		0x9d32
#define CIO2_PCI_BAR		0

#define CIO2_BAR0_SIZE		0x2000

#define CIO2_REG_GPREG_BASE		0x1000
#define CIO2_REG_CSIRX_BASE		0x000
#define CIO2_REG_MIPIBE_BASE		0x100
#define CIO2_REG_IRQCTRL_BASE		0x300

#define CIO2_REG_D0I3C			0x1408
#define CIO2_D0I3C_I3			BIT(2)
#define CIO2_D0I3C_RR			BIT(3)

#define CIO2_REG_CGC			0x1400
#define CIO2_CGC_XOSC_DCGE		BIT(13)
#define CIO2_CGC_CSI2_INTERFRAME_TGE	BIT(6)
#define CIO2_CGC_CSI2_PORT_DCGE		BIT(8)
#define CIO2_CGC_CSI_CLKGATE_HOLDOFF_SHIFT	24U
#define CIO2_CGC_CSI_CLKGATE_HOLDOFF		5U
#define CIO2_CGC_PRIM_TGE			BIT(1)
#define CIO2_CGC_SIDE_TGE			BIT(2)
#define CIO2_CGC_D3I3_TGE			BIT(5)
#define CIO2_CGC_ROSC_DCGE			BIT(12)
#define CIO2_CGC_SIDE_DCGE			BIT(10)
#define CIO2_CGC_PRIM_DCGE			BIT(11)
#define CIO2_CGC_CLKGATE_HOLDOFF		3U
#define CIO2_CGC_CLKGATE_HOLDOFF_SHIFT		20U

#define CIO2_REG_INT_EN				0x1420
#define CIO2_REG_INT_STS			0x1414
#define CIO2_REG_INT_STS_EXT_OE			0x1418
#define CIO2_REG_INT_EN_EXT_OE			0x1424
#define CIO2_REG_LTRCTRL			0x1480
#define CIO2_REG_LTRVAL01			0x1488
#define CIO2_REG_LTRVAL23			0x1484
#define CIO2_LTRVAL3_VAL			90U
#define CIO2_LTRVAL1_VAL			90U
#define CIO2_LTRVAL2_VAL			90U
#define CIO2_LTRVAL0_VAL			175U
#define CIO2_LTRVAL3_SCALE			2U
#define CIO2_LTRVAL2_SCALE			2U
#define CIO2_LTRVAL1_SCALE			2U
#define CIO2_LTRVAL0_SCALE			2U
#define CIO2_LTRVAL13_SCALE_SHIFT		26U
#define CIO2_LTRVAL13_VAL_SHIFT			16U
#define CIO2_LTRVAL02_VAL_SHIFT			0U
#define CIO2_LTRVAL02_SCALE_SHIFT		10U

#define CIO2_REG_PBM_WMCTRL1			0x1464
#define CIO2_REG_PBM_WMCTRL2			0x1468
#define CIO2_REG_PBM_ARB_CTRL			0x1460
#define CIO2_REG_PBM_FOPN_ABORT		0x1474

#define CIO2_PBM_ARB_CTRL_PLL_POST_SHTDN	2U
#define CIO2_PBM_ARB_CTRL_PLL_POST_SHTDN_SHIFT	8U
#define CIO2_PBM_ARB_CTRL_LANES_DIV		0U
#define CIO2_PBM_ARB_CTRL_LANES_DIV_SHIFT	0U
#define CIO2_PBM_ARB_CTRL_LE_EN			BIT(7)
#define CIO2_PBM_ARB_CTRL_PLL_AHD_WK_UP	480U
#define CIO2_PBM_ARB_CTRL_PLL_AHD_WK_UP_SHIFT	16U

#define CIO2_PBM_WMCTRL1_MIN_2CK		(4 << CIO2_PBM_WMCTRL1_MIN_2CK_SHIFT)
#define CIO2_PBM_WMCTRL1_MID1_2CK		(16 << CIO2_PBM_WMCTRL1_MID1_2CK_SHIFT)
#define CIO2_PBM_WMCTRL1_MID2_2CK		(21 << CIO2_PBM_WMCTRL1_MID2_2CK_SHIFT)
#define CIO2_PBM_WMCTRL1_MIN_2CK_SHIFT		0U
#define CIO2_PBM_WMCTRL1_MID1_2CK_SHIFT		8U
#define CIO2_PBM_WMCTRL1_MID2_2CK_SHIFT		16U

#define CIO2_PBM_WMCTRL2_HWM_2CK		40U
#define CIO2_PBM_WMCTRL2_LWM_2CK		22U
#define CIO2_PBM_WMCTRL2_OBFFWM_2CK		2U
#define CIO2_PBM_WMCTRL2_TRANSDYN		1U
#define CIO2_PBM_WMCTRL2_OBFF_MEM_EN		BIT(29)
#define CIO2_PBM_WMCTRL2_HWM_2CK_SHIFT		0U
#define CIO2_PBM_WMCTRL2_LWM_2CK_SHIFT		8U
#define CIO2_PBM_WMCTRL2_OBFFWM_2CK_SHIFT	16U
#define CIO2_PBM_WMCTRL2_TRANSDYN_SHIFT		24U

#define CIO2_REG_ISCLK_RATIO			(CIO2_REG_GPREG_BASE + 0xc)
#define CIO2_ISCLK_RATIO			0xc

#define CIO2_REG_CSIRX_ENABLE			(CIO2_REG_CSIRX_BASE + 0x0)
#define CIO2_REG_CSIRX_NOF_ENABLED_LANES	(CIO2_REG_CSIRX_BASE + 0x4)
#define CIO2_REG_CSIRX_STATUS_DLANE_LP		(CIO2_REG_CSIRX_BASE + 0x20)
#define CIO2_REG_CSIRX_STATUS_DLANE_HS		(CIO2_REG_CSIRX_BASE + 0x1c)
#define CIO2_REG_CSIRX_DLY_CNT_TERMEN(lane)	(CIO2_REG_CSIRX_BASE + 0x2c + 8 * (lane))
#define CIO2_REG_CSIRX_DLY_CNT_SETTLE(lane)	(CIO2_REG_CSIRX_BASE + 0x30 + 8 * (lane))

#define CIO2_REG_MIPIBE_ENABLE			(CIO2_REG_MIPIBE_BASE + 0x0)
#define CIO2_REG_MIPIBE_COMP_FORMAT(vc)		(CIO2_REG_MIPIBE_BASE + 0x8 + 0x4 * (vc))
#define CIO2_REG_MIPIBE_FORCE_RAW8		(CIO2_REG_MIPIBE_BASE + 0x20)
#define CIO2_REG_MIPIBE_SP_LUT_ENTRY(vc)	(CIO2_REG_MIPIBE_BASE + 0x74 + 4 * (vc))
#define CIO2_REG_MIPIBE_LP_LUT_ENTRY(m)		(CIO2_REG_MIPIBE_BASE + 0x84 + 4 * (m))
#define CIO2_REG_MIPIBE_GLOBAL_LUT_DISREGARD	(CIO2_REG_MIPIBE_BASE + 0x68)

#define CIO2_REG_IRQCTRL_EDGE			(CIO2_REG_IRQCTRL_BASE + 0x00)
#define CIO2_REG_IRQCTRL_MASK			(CIO2_REG_IRQCTRL_BASE + 0x04)
#define CIO2_REG_IRQCTRL_STATUS			(CIO2_REG_IRQCTRL_BASE + 0x08)
#define CIO2_REG_IRQCTRL_CLEAR			(CIO2_REG_IRQCTRL_BASE + 0x0c)
#define CIO2_REG_IRQCTRL_ENABLE			(CIO2_REG_IRQCTRL_BASE + 0x10)
#define CIO2_REG_IRQCTRL_LEVEL_NOT_PULSE	(CIO2_REG_IRQCTRL_BASE + 0x14)

#define CIO2_REG_FB_HPLL_FREQ			(CIO2_REG_GPREG_BASE + 0x08)

#define CIO2_REG_CDMAC0(n)			(0x1508 + 0x10 * (n))
#define CIO2_REG_CDMAC1(n)			(0x150c + 0x10 * (n))
#define CIO2_REG_CDMABA(n)			(0x1500 + 0x10 * (n))
#define CIO2_REG_CDMARI(n)			(0x1504 + 0x10 * (n))

#define CIO2_REG_PIPE_BASE(n)			((n) * 0x0400)

#define CIO2_REG_PXM_PXF_FMT_CFG0(n)		(0x1700 + 0x30 * (n))
#define CIO2_REG_PXM_SID2BID0(n)		(0x1724 + 0x30 * (n))
#define CIO2_REG_PXM_FRF_CFG(n)			(0x1720 + 0x30 * (n))

#define CIO2_NUM_DMA_CHAN			20U
#define CIO2_NUM_PORTS				4U
#define CIO2_QUEUES				CIO2_NUM_PORTS
#define CIO2_MAX_BUFFERS			(PAGE_SIZE / 16 / CIO2_MAX_LOPS)
#define CIO2_MAX_LOPS				8

#define CIO2_CDMAC0_DMA_INTR_ON_FS		BIT(26)
#define CIO2_CDMAC0_DMA_INTR_ON_FE		BIT(27)
#define CIO2_CDMAC0_FBPT_UPDATE_FIFO_FULL	BIT(28)
#define CIO2_CDMAC0_DMA_EN			BIT(30)
#define CIO2_CDMAC0_DMA_HALTED			BIT(31)
#define CIO2_CDMAC0_FBPT_LEN_SHIFT		0U
#define CIO2_CDMAC0_FBPT_WIDTH_SHIFT		8U
#define CIO2_CDMAC1_LINENUMUPDATE_SHIFT		16U

#define CIO2_CDMARI_FBPT_RP_MASK		0xff
#define CIO2_CDMARI_FBPT_RP_SHIFT		0U

#define CIO2_FBPT_CTRL_IOC			BIT(1)
#define CIO2_FBPT_CTRL_IOS			BIT(2)
#define CIO2_FBPT_CTRL_VALID			BIT(0)
#define CIO2_FBPT_SUBENTRY_UNIT		4

#define CIO2_INT_EN_EXT_IE			0x17e8
#define CIO2_INT_STS_EXT_IE			0x17e4
#define CIO2_INT_EN_EXT_OE			0x1424
#define CIO2_INT_STS_EXT_OE			0x1418

#define CIO2_INT_EXT_OE_OES_SHIFT		24U
#define CIO2_INT_EXT_OE_DMAOE_SHIFT		0U
#define CIO2_INT_EXT_OE_OES_MASK		(0xf << CIO2_INT_EXT_OE_OES_SHIFT)
#define CIO2_INT_EXT_OE_DMAOE_MASK		0x7ffff
#define CIO2_INT_EXT_IE_IRQ(n)			(0x80 << (8U * (n)))
#define CIO2_INT_IOOE				BIT(23)
#define CIO2_INT_IOIE				BIT(22)
#define CIO2_INT_IOIRQ				BIT(24)
#define CIO2_INT_IOC_SHIFT			0
#define CIO2_INT_IOC_MASK			(0x7ff << CIO2_INT_IOC_SHIFT)
#define CIO2_INT_IOS_IOLN_SHIFT			12
#define CIO2_INT_IOS_IOLN_MASK			(0x3ff << CIO2_INT_IOS_IOLN_SHIFT)
#define CIO2_REG_INT_EN_IRQ			(1 << 24)

#define CIO2_CGC_XOSC_TGE			BIT(3)

#define CIO2_REG_LTRVAL13_SCALE_SHIFT		26U
#define CIO2_REG_LTRVAL13_VAL_SHIFT		16U
#define CIO2_LTRVAL13_SCALE_SHIFT		26U
#define CIO2_LTRVAL13_VAL_SHIFT			16U

#define CIO2_PBM_FOPN_ABORT(n)			(0x1 << 8U * (n))

#define CIO2_PXM_FRF_CFG_MSK_ECC_DPHY_NE	BIT(10)
#define CIO2_PXM_FRF_CFG_CRC_TH			16
#define CIO2_PXM_FRF_CFG_MSK_ECC_DPHY_NR	BIT(8)
#define CIO2_PXM_FRF_CFG_MSK_ECC_RE		BIT(9)
#define CIO2_PXM_FRF_CFG_ABORT			BIT(2)

#define CIO2_CGC_CSI2_INTERFRAME_TGE		BIT(6)
#define CIO2_CGC_CSI2_PORT_DCGE			BIT(8)

#define CIO2_REG_PIPE_BASE(n)			((n) * 0x0400)

#define CIO2_REG_FB_HPLL_FREQ			(CIO2_REG_GPREG_BASE + 0x08)

#define CIO2_LTRCTRL_LTRDYNEN			BIT(16)

#define CIO2_CSIRX_DLY_CNT_TERMEN_DEFAULT	0x4
#define CIO2_CSIRX_DLY_CNT_SETTLE_DEFAULT	0x570
#define CIO2_CSIRX_DLY_CNT_TERMEN_DLANE_A	0
#define CIO2_CSIRX_DLY_CNT_SETTLE_CLANE_A	95
#define CIO2_CSIRX_DLY_CNT_SETTLE_CLANE_B	-8
#define CIO2_CSIRX_DLY_CNT_TERMEN_CLANE_A	0
#define CIO2_CSIRX_DLY_CNT_SETTLE_DLANE_A	85
#define CIO2_CSIRX_DLY_CNT_SETTLE_DLANE_B	-2
#define CIO2_CSIRX_DLY_CNT_TERMEN_DLANE_B	0
#define CIO2_CSIRX_DLY_CNT_TERMEN_CLANE_B	0
#define CIO2_CSIRX_DLY_CNT_CLANE_IDX		-1

#define CIO2_MIPIBE_GLOBAL_LUT_DISREGARD	1U
#define CIO2_MIPIBE_LP_LUT_ENTRY_DISREGARD	1U

#define CIO2_PBM_ARB_CTRL_LANES_DIV		0U
#define CIO2_PBM_ARB_CTRL_LANES_DIV_SHIFT	0U

#define CIO2_PBM_WMCTRL1_MIN_2CK_SHIFT		0U
#define CIO2_PBM_WMCTRL1_MID1_2CK_SHIFT		8U
#define CIO2_PBM_WMCTRL1_MID2_2CK_SHIFT		16U

#define CIO2_PBM_WMCTRL2_HWM_2CK_SHIFT		0U
#define CIO2_PBM_WMCTRL2_LWM_2CK_SHIFT		8U
#define CIO2_PBM_WMCTRL2_OBFFWM_2CK_SHIFT	16U
#define CIO2_PBM_WMCTRL2_TRANSDYN_SHIFT		24U

#define CIO2_FBPT_SIZE			(CIO2_MAX_BUFFERS * CIO2_MAX_LOPS * sizeof(struct cio2_fbpt_entry))

#define CIO2_DMA_MASK				DMA_BIT_MASK(39)

#define SENSOR_VIR_CH_DFLT			0
#define CIO2_IMAGE_MAX_HEIGHT			3136U
#define CIO2_IMAGE_MAX_WIDTH			4224U
#define CIO2_PAD_SINK				0U
#define CIO2_PAD_SOURCE				1U
#define CIO2_PADS				2U
#define CIO2_ENTITY_NAME			"ipu3-csi2"
#define CIO2_DEVICE_NAME			"Intel IPU3 CIO2"
#define CIO2_NAME				"ipu3-cio2"

#define CHUNK_SIZE(a) ((a)->end - (a)->begin + 1)

/* ---- Added missing register definitions for IE ---- */
#define CIO2_REG_INT_STS_EXT_IE      0x17e4
#define CIO2_REG_INT_EN_EXT_IE       0x17e8


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


#ifndef __packed
#define __packed __attribute__((__packed__))
#endif

/* Static struct definitions extracted from driver (hardware-facing) */
struct cio2_fbpt_entry {
    union {
        struct {
            uint32_t ctrl; /* status ctrl */
            uint16_t cur_line_num; /* current line # written to DDR */
            uint16_t frame_num; /* updated by DMA upon FE */
            uint32_t first_page_offset; /* offset for 1st page in LOP */
        } first_entry;
        /* Second entry per buffer */
        struct {
            uint32_t timestamp;
            uint32_t num_of_bytes;
            /* the number of bytes for write on last page */
            uint16_t last_page_available_bytes;
            /* the number of pages allocated for this buf */
            uint16_t num_of_pages;
        } second_entry;
    };
    uint32_t lop_page_addr;   /* Points to list of pointers (LOP) table */
} __packed;

struct cio2_csi2_timing {
    int32_t clk_termen;
    int32_t clk_settle;
    int32_t dat_termen;
    int32_t dat_settle;
};

struct csi2_bus_info {
    uint32_t port;
    uint32_t lanes;
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
    uint32_t irqctrl_edge;
    uint32_t irqctrl_mask;
    uint32_t irqctrl_status;
    uint32_t irqctrl_clear;
    uint32_t irqctrl_enable;
    uint32_t irqctrl_level_not_pulse;
    uint32_t int_en;
    uint32_t int_sts;
    uint32_t int_en_ext_oe;
    uint32_t int_sts_ext_oe;
    uint32_t int_en_ext_ie;
    uint32_t int_sts_ext_ie;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    /* We will map the entire MMIO region to a flat memory block for registers */
    uint8_t mmio[CIO2_BAR0_SIZE];

    /* DMA Context */
    uint64_t dma_mask;
    /* DMA channels state */
    struct {
        uint32_t cdmac0;
        uint32_t cdmac1;
        uint32_t cdmaba;
        uint32_t cdmari;
    } dma_chan[CIO2_NUM_DMA_CHAN];

    /* Frame Buffer Pointer Tables per queue (port) */
    struct cio2_fbpt_entry fbpt[CIO2_QUEUES][CIO2_MAX_BUFFERS * CIO2_MAX_LOPS];
    /* List of pointers (LOP) pages: dummy page for safety */
    uint32_t *dummy_lop[CIO2_QUEUES];
    dma_addr_t dummy_lop_bus_addr[CIO2_QUEUES];
    void *dummy_page[CIO2_QUEUES];
    dma_addr_t dummy_page_bus_addr[CIO2_QUEUES];

    /* Operational status flags */
    bool streaming;
    uint32_t cgc;

    /* Probe and Reset state */
    bool in_reset;

    /* Power management state */
    uint32_t d0i3c;
    bool in_d3;

    /* Other addition info */
};


/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    /* No interrupt generation needed for probe; placeholder empty. */
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /* DMA transfers are not emulated; placeholder empty. */
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size != 4 || (addr & 3)) {
        return 0;
    }
    addr &= ~3ULL;
    if (addr >= CIO2_BAR0_SIZE) {
        return 0;
    }
    uint32_t current = ldl_le_p(s->mmio + addr);

    switch (addr) {
    /* W1C registers: read returns current status */
    case CIO2_REG_INT_STS:
    case CIO2_REG_INT_STS_EXT_OE:
    case CIO2_REG_INT_STS_EXT_IE:
        val = current;
        break;
    /* Per-pipe IRQCTRL status (simplified global) */
    case CIO2_REG_IRQCTRL_STATUS: /* 0x308 */
        val = s->irqctrl_status;
        break;
    /* DMA channel registers: reflect stored values */
    default:
        if (addr >= 0x1500 && addr < 0x1500 + 0x10 * CIO2_NUM_DMA_CHAN) {
            int n = (addr - 0x1500) / 0x10;
            if (addr == CIO2_REG_CDMABA(n)) {
                val = s->dma_chan[n].cdmaba;
            } else if (addr == CIO2_REG_CDMARI(n)) {
                val = s->dma_chan[n].cdmari;
            } else if (addr == CIO2_REG_CDMAC0(n)) {
                val = s->dma_chan[n].cdmac0;
            } else if (addr == CIO2_REG_CDMAC1(n)) {
                val = s->dma_chan[n].cdmac1;
            } else {
                val = current;
            }
        } else {
            val = current;
        }
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4 || (addr & 3)) {
        return;
    }
    addr &= ~3ULL;
    if (addr >= CIO2_BAR0_SIZE) {
        return;
    }
    uint32_t write_val = (uint32_t)val;

    switch (addr) {
    /* W1C registers: clear bits that are set in write_val */
    case CIO2_REG_INT_STS:
    case CIO2_REG_INT_STS_EXT_OE:
    case CIO2_REG_INT_STS_EXT_IE:
        {
            uint32_t cur = ldl_le_p(s->mmio + addr);
            cur &= ~write_val;
            stl_le_p(s->mmio + addr, cur);
        }
        break;
    /* Global control registers */
    case CIO2_REG_CGC:
        s->cgc = write_val;
        stl_le_p(s->mmio + addr, write_val);
        break;
    case CIO2_REG_D0I3C:
        s->d0i3c = write_val;
        stl_le_p(s->mmio + addr, write_val);
        break;
    case CIO2_REG_INT_EN:
        s->int_en = write_val;
        stl_le_p(s->mmio + addr, write_val);
        break;
    case CIO2_REG_INT_EN_EXT_OE:
        s->int_en_ext_oe = write_val;
        stl_le_p(s->mmio + addr, write_val);
        break;
    case CIO2_REG_INT_EN_EXT_IE: /* 0x17e8 */
        s->int_en_ext_ie = write_val;
        stl_le_p(s->mmio + addr, write_val);
        break;
    default:
        /* Per-pipe IRQCTRL registers (simplified to global fields) */
        if (addr == (CIO2_REG_IRQCTRL_BASE + 0x04)) { /* 0x304 */
            s->irqctrl_mask = write_val;
            stl_le_p(s->mmio + addr, write_val);
        } else if (addr == (CIO2_REG_IRQCTRL_BASE + 0x08)) { /* 0x308 */
            s->irqctrl_status = write_val;
            stl_le_p(s->mmio + addr, write_val);
        } else if (addr == (CIO2_REG_IRQCTRL_BASE + 0x0c)) { /* 0x30c */
            s->irqctrl_status &= ~write_val;
            stl_le_p(s->mmio + addr, write_val);
        } else if (addr == (CIO2_REG_IRQCTRL_BASE + 0x10)) { /* 0x310 */
            s->irqctrl_enable = write_val;
            stl_le_p(s->mmio + addr, write_val);
        } else if (addr == (CIO2_REG_IRQCTRL_BASE + 0x00)) { /* 0x300 */
            s->irqctrl_edge = write_val;
            stl_le_p(s->mmio + addr, write_val);
        } else if (addr == (CIO2_REG_IRQCTRL_BASE + 0x14)) { /* 0x314 */
            s->irqctrl_level_not_pulse = write_val;
            stl_le_p(s->mmio + addr, write_val);
        }
        /* DMA channel registers */
        else if (addr >= 0x1500 && addr < 0x1500 + 0x10 * CIO2_NUM_DMA_CHAN) {
            int n = (addr - 0x1500) / 0x10;
            if (addr == CIO2_REG_CDMABA(n)) {
                s->dma_chan[n].cdmaba = write_val;
                stl_le_p(s->mmio + addr, write_val);
            } else if (addr == CIO2_REG_CDMARI(n)) {
                s->dma_chan[n].cdmari = write_val;
                stl_le_p(s->mmio + addr, write_val);
            } else if (addr == CIO2_REG_CDMAC0(n)) {
                s->dma_chan[n].cdmac0 = write_val;
                if (!(write_val & CIO2_CDMAC0_DMA_EN)) {
                    s->dma_chan[n].cdmac0 |= CIO2_CDMAC0_DMA_HALTED;
                }
                stl_le_p(s->mmio + addr, s->dma_chan[n].cdmac0);
            } else if (addr == CIO2_REG_CDMAC1(n)) {
                s->dma_chan[n].cdmac1 = write_val;
                stl_le_p(s->mmio + addr, write_val);
            } else {
                /* other addresses in the DMA range: just store */
                stl_le_p(s->mmio + addr, write_val);
            }
        }
        /* PXM registers (port-specific) */
        else if (addr >= 0x1700 && addr < 0x1700 + 0x30 * CIO2_NUM_PORTS) {
            /* just store; driver writes to PXM_PXF_FMT_CFG0(port), PXM_SID2BID0(port), PXM_FRF_CFG(port) */
            stl_le_p(s->mmio + addr, write_val);
        }
        else {
            /* Default: store write value */
            stl_le_p(s->mmio + addr, write_val);
        }
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    uint64_t val = 0;

    /* Legacy PIO not used by driver */
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Legacy PIO not used by driver */
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

    /* Reset register shadows to power-on defaults (all zeros). */
    memset(s->mmio, 0, sizeof(s->mmio));
    s->irqctrl_edge = 0;
    s->irqctrl_mask = 0;
    s->irqctrl_status = 0;
    s->irqctrl_clear = 0;
    s->irqctrl_enable = 0;
    s->irqctrl_level_not_pulse = 0;
    s->int_en = 0;
    s->int_sts = 0;
    s->int_en_ext_oe = 0;
    s->int_sts_ext_oe = 0;
    s->int_en_ext_ie = 0;
    s->int_sts_ext_ie = 0;
    s->d0i3c = 0;
    s->cgc = 0;
    for (int i = 0; i < CIO2_NUM_DMA_CHAN; i++) {
        s->dma_chan[i].cdmac0 = 0;
        s->dma_chan[i].cdmac1 = 0;
        s->dma_chan[i].cdmaba = 0;
        s->dma_chan[i].cdmari = 0;
    }
    s->streaming = false;
    s->in_d3 = false;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_INTEL);
    pci_set_word(pci_conf + PCI_DEVICE_ID, CIO2_PCI_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0480);
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
    s->bar_info[0] = (BARInfo) {
        .index = 0,
        .type = BAR_TYPE_MMIO,
        .size = CIO2_BAR0_SIZE,
        .name = "cio2-mmio"
    };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI initialization (driver uses pci_enable_msi) */
    if (msi_init(pdev, 0, 1, true, false, errp) < 0) {
        return;
    }

    /* No DMA configuration needed; driver sets mask via dma_set_mask(). */
    /* Field initialization: set default state. */
    memset(s->mmio, 0, sizeof(s->mmio));
    s->dma_mask = (1ULL << 39) - 1;
    s->irqctrl_edge = 0;
    s->irqctrl_mask = 0;
    s->irqctrl_status = 0;
    s->irqctrl_enable = 0;
    s->irqctrl_level_not_pulse = 0;
    s->int_en = 0;
    s->int_sts = 0;
    s->int_en_ext_oe = 0;
    s->int_sts_ext_oe = 0;
    s->int_en_ext_ie = 0;
    s->int_sts_ext_ie = 0;
    s->d0i3c = 0;
    s->cgc = 0;
    s->streaming = false;
    s->in_d3 = false;
    for (int i = 0; i < CIO2_NUM_DMA_CHAN; i++) {
        s->dma_chan[i].cdmac0 = 0;
        s->dma_chan[i].cdmac1 = 0;
        s->dma_chan[i].cdmaba = 0;
        s->dma_chan[i].cdmari = 0;
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }

    /* No special uninit needed; all memory owned by QEMU. */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "ipu3_cio2_pci",
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
