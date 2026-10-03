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

#define TYPE_PCIBASE_DEVICE "crypto_safexcel_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x10ee
#define DEVICE_ID 0x9038

/* BAR sizes inferred from address range usage */
#define BAR0_SIZE 0x100000   /* 1MB, covers all EIP197 registers up to 0xFFFFx */
#define BAR2_SIZE 0x10000    /* 64KB, covers Xilinx VIP IRQ block */

/* Register offsets and bitfield definitions from safexcel.c */
#define EIP197_FLUE_CACHEBASE_HI(n)		(0xf6004 + (32 * (n)))
#define EIP197_FLUE_OFFSETS			0xf6808
#define EIP197_FLUE_CACHEBASE_LO(n)		(0xf6000 + (32 * (n)))
#define EIP197_FLUE_CONFIG(n)			(0xf6010 + (32 * (n)))
#define EIP197_FLUE_IFC_LUT(n)			(0xf6820 + (4 * (n)))
#define EIP197_FLUE_CONFIG_MAGIC		0xc7000004
#define EIP197_FLUE_ARC4_OFFSET			0xf680c
#define EIP197_CS_BANKSEL_MASK			GENMASK(14, 12)
#define EIP197_CS_RAM_CTRL			0xf7ff0
#define EIP197_CS_BANKSEL_OFS			12
#define EIP197_CLASSIFICATION_RAMS		0xe0000
#define EIP197_CS_RC_PREV(x)			((x) << 10)
#define EIP197_RC_NULL				0x3ff
#define EIP197_CS_RC_NEXT(x)			(x)
#define EIP197_CS_RC_SIZE			(4 * sizeof(u32))
#define EIP197_MIN_ASIZE			8
#define EIP197_TRC_PARAMS_BLK_TIMER_SPEED(x)	((x) << 10)
#define EIP197_MIN_DSIZE			1024
#define EIP197_TRC_PARAMS_DATA_ACCESS		BIT(2)
#define EIP197_TRC_PARAMS_HTABLE_SZ(x)		((x) << 4)
#define EIP197_TRC_PARAMS			0xf0820
#define EIP197_TRC_FREECHAIN			0xf0824
#define EIP197_TRC_FREECHAIN_TAIL_PTR(p)	((p) << 16)
#define EIP197_TRC_PARAMS2_HTABLE_PTR(p)	(p)
#define EIP197_TRC_ENABLE_MASK			GENMASK(6, 4)
#define EIP197_TRC_PARAMS_RC_SZ_LARGE(n)	((n) << 18)
#define EIP197_TRC_ECCCTRL			0xf0830
#define EIP197_TRC_PARAMS2			0xf0828
#define EIP197_TRC_ENABLE_0			BIT(4)
#define EIP197_TRC_PARAMS_SW_RESET		BIT(0)
#define EIP197_CS_TRC_REC_WC			64
#define EIP197_TRC_PARAMS2_RC_SZ_SMALL(n)	((n) << 18)
#define EIP197_TRC_FREECHAIN_HEAD_PTR(p)	(p)
#define EIP197_PE_ICE_PPTF_CTRL(n)		(0x0e00 + (0x2000 * (n)))
#define EIP197_PE_ICE_PUE_CTRL(n)		(0x0c80 + (0x2000 * (n)))
#define EIP197_NUM_OF_SCRATCH_BLOCKS		32
#define EIP197_PE_ICE_SCRATCH_RAM(n)		(0x0800 + (0x2000 * (n)))
#define EIP197_PE_ICE_SCRATCH_CTRL_CHANGE_TIMER		BIT(2)
#define EIP197_PE_ICE_SCRATCH_CTRL_TIMER_EN		BIT(3)
#define EIP197_PE_ICE_SCRATCH_CTRL_SCRATCH_ACCESS	BIT(25)
#define EIP197_PE_ICE_SCRATCH_CTRL(n)		(0x0d04 + (0x2000 * (n)))
#define EIP197_PE_ICE_SCRATCH_CTRL_CHANGE_ACCESS	BIT(24)
#define EIP197_DEBUG_OCE_BYPASS			BIT(1)
#define EIP197_PE_ICE_PUTF_CTRL(n)		(0x0d00 + (0x2000 * (n)))
#define EIP197_PE_ICE_RAM_CTRL(n)		(0x0ff0 + (0x2000 * (n)))
#define EIP197_PE(priv)		((priv)->base + (priv)->offsets.pe)
#define EIP197_PE_ICE_RAM_CTRL_FPP_PROG_EN	BIT(1)
#define EIP197_PE_ICE_FPP_CTRL(n)		(0x0d80 + (0x2000 * (n)))
#define EIP197_PE_DEBUG(n)			(0x1ff4 + (0x2000 * (n)))
#define EIP197_FW_TERMINAL_NOPS		2
#define EIP197_FW_START_POLLCNT		16
#define EIP197_FW_FPP_READY		0x18
#define EIP197_FW_PUE_READY		0x14
#define EIP197_PE_ICE_UENG_DEBUG_RESET		BIT(3)
#define EIP197_PE_ICE_UENG_INIT_ALIGN_MASK	0x7ff0
#define EIP197_PE_ICE_UENG_START_OFFSET(n)	((n) << 16)
#define EIP197_PE_ICE_RAM_CTRL_PUE_PROG_EN	BIT(0)
#define RD_CACHE_3BITS				0x5
#define EIP197_FETCH_DEPTH			2
#define EIP197_HIA_xDR_CFG_WR_CACHE(n)		(((n) & 0x7) << 25)
#define EIP197_CDR_DESC_MODE_ADCP		BIT(30)
#define WR_CACHE_3BITS				0x3
#define EIP197_HIA_xDR_CFG_RD_CACHE(n)		(((n) & 0x7) << 29)
#define EIP197_HIA_CDR(priv, r)			(EIP197_HIA_xDR_OFF(priv, r))
#define EIP197_RD64_FETCH_SIZE		(sizeof(struct safexcel_result_desc) /\
					 sizeof(u32))
#define EIP197_HIA_AIC_R(priv)		((priv)->base + (priv)->offsets.hia_aic_r)
#define EIP197_HIA_RDR(priv, r)			(EIP197_HIA_xDR_OFF(priv, r) + 0x800)
#define EIP197_HIA_AIC_R_ENABLE_CTRL(r)		(0xe008 - EIP197_HIA_AIC_R_OFF(r))
#define EIP197_RDR_IRQ(n)			BIT((n) * 2 + 1)
#define EIP197_STRC_CONFIG			0xf43f0
#define EIP197_FUNCTION_ALL			0xffffffff
#define EIP197_HIA_DSE_THR_CTRL(n)		(0x0000 + (128 * (n)))
#define EIP197_HIA_RA_PE_CTRL_EN		BIT(30)
#define EIP197_STRC_CONFIG_SMALL_REC(s)		(s<<0)
#define EIP197_PE_OUT_DBUF_THRES_MAX(n)		((n) << 4)
#define EIP197_HIA_DxE_CFG_MIN_CTRL_SIZE(n)	((n) << 16)
#define EIP197_HIA_GEN_CFG(priv)	((priv)->base + (priv)->offsets.hia_gen_cfg)
#define EIP197_HIA_DxE_CFG_DATA_CACHE_CTRL(n)	(((n) & 0x7) << 4)
#define EIP197_HIA_DFE(priv)		((priv)->base + (priv)->offsets.hia_dfe)
#define EIP197_PE_EIP96_FUNCTION_EN(n)		(0x1004 + (0x2000 * (n)))
#define EIP197_MST_CTRL				0xfff4
#define EIP197_PE_OUT_DBUF_THRES(n)		(0x1c00 + (0x2000 * (n)))
#define EIP197_PE_IN_xBUF_THRES_MIN(n)		((n) << 8)
#define EIP197_HIA_DxE_CFG_MAX_DATA_SIZE(n)	((n) << 8)
#define EIP197_HIA_RA_PE_CTRL(n)		(0x0010 + (8   * (n)))
#define EIP197_HIA_AIC_R_ENABLE_CLR(r)		(0xe014 - EIP197_HIA_AIC_R_OFF(r))
#define EIP197_HIA_DFE_CFG(n)			(0x0000 + (128 * (n)))
#define EIP197_HIA_DFE_THR_CTRL(n)		(0x0000 + (128 * (n)))
#define EIP197_HIA_AIC(priv)		((priv)->base + (priv)->offsets.hia_aic)
#define EIP197_HIA_AIC_G_ENABLE_CTRL		0xf808
#define EIP197_HIA_DFE_CFG_DIS_DEBUG		GENMASK(31, 29)
#define EIP197_HIA_DxE_CFG_MAX_CTRL_SIZE(n)	((n) << 24)
#define EIP197_PE_EIP96_TOKEN_CTRL_ENABLE_TIMEOUT	BIT(22)
#define EIP197_HIA_DSE_CFG_EN_SINGLE_WR		BIT(29)
#define EIP197_PE_EIP96_FUNCTION2_EN(n)		(0x1030 + (0x2000 * (n)))
#define EIP197_PE_EIP96_TOKEN_CTRL_CTX_UPDATES		BIT(16)
#define EIP197_HIA_DSE_CFG_ALWAYS_BUFFERABLE	GENMASK(15, 14)
#define EIP197_HIA_DSE_THR_STAT(n)		(0x0004 + (128 * (n)))
#define EIP197_HIA_DSE(priv)		((priv)->base + (priv)->offsets.hia_dse)
#define EIP197_MST_CTRL_RD_CACHE(n)		(((n) & 0xf) << 0)
#define EIP197_HIA_AIC_G_ACK			0xf810
#define EIP197_PE_EIP96_TOKEN_CTRL2(n)		(0x102c + (0x2000 * (n)))
#define EIP197_HIA_RA_PE_CTRL_RESET		BIT(31)
#define EIP197_MST_CTRL_WD_CACHE(n)		(((n) & 0xf) << 4)
#define RD_CACHE_4BITS				(RD_CACHE_3BITS << 1 | BIT(0))
#define EIP197_HIA_DSE_THR(priv)	((priv)->base + (priv)->offsets.hia_dse_thr)
#define EIP197_PE_OUT_DBUF_THRES_MIN(n)		((n) << 0)
#define EIP197_HIA_DFE_THR(priv)	((priv)->base + (priv)->offsets.hia_dfe_thr)
#define WR_CACHE_4BITS				(WR_CACHE_3BITS << 1 | BIT(0))
#define EIP197_STRC_CONFIG_INIT			BIT(31)
#define EIP197_PE_EIP96_TOKEN_CTRL(n)		(0x1000 + (0x2000 * (n)))
#define EIP197_MST_CTRL_TX_MAX_CMD(n)		(((n) & 0xf) << 20)
#define EIP197_PE_IN_xBUF_THRES_MAX(n)		((n) << 12)
#define EIP197_HIA_DSE_CFG(n)			(0x0000 + (128 * (n)))
#define EIP197_HIA_DSE_CFG_DIS_DEBUG		GENMASK(31, 30)
#define EIP197_HIA_AIC_G(priv)		((priv)->base + (priv)->offsets.hia_aic_g)
#define EIP197_STRC_CONFIG_LARGE_REC(s)		(s<<8)
#define EIP197_HIA_MST_CTRL			0xfff4
#define EIP197_PE_IN_TBUF_THRES(n)		(0x0100 + (0x2000 * (n)))
#define EIP197_PE_EIP96_TOKEN_CTRL2_CTX_DONE	BIT(3)
#define EIP197_PE_IN_DBUF_THRES(n)		(0x0000 + (0x2000 * (n)))
#define EIP197_PE_EIP96_TOKEN_CTRL_NO_TOKEN_WAIT	BIT(17)
#define EIP197_HIA_DxE_CFG_MIN_DATA_SIZE(n)	((n) << 0)
#define EIP197_HIA_DxE_CFG_CTRL_CACHE_CTRL(n)	(((n) & 0x7) << 20)
#define EIP197_DEFAULT_RING_SIZE		400
#define EIP197_MAX_BATCH_SZ			64
#define EIP197_HIA_RDR_THRESH_PKT_MODE		BIT(23)
#define EIP197_HIA_RDR_THRESH_PROC_PKT(n)	(n)
#define CONTEXT_CONTROL_INV_TR			(0x6 << 24)
#define EIP197_TYPE_EXTENDED		0x3
#define EIP197_CONTEXT_SIZE_MASK	0x3
#define EIP197_xDR_PROC_xD_PKT(n)		((n) << 24)
#define EIP197_HIA_AIC_R_ACK(r)			(0xe010 - EIP197_HIA_AIC_R_OFF(r))
#define EIP197_HIA_AIC_R_ENABLED_STAT(r)	(0xe010 - EIP197_HIA_AIC_R_OFF(r))
#define EIP197_CD64_FETCH_SIZE		(sizeof(struct safexcel_command_desc) /\
					sizeof(u32))
#define EIP197_MAX_TOKENS			16
#define EIP197_RD64_RESULT_SIZE		(sizeof(struct result_data_desc) /\
					 sizeof(u32))
#define EIP197_HIA_AIC_R_BASE		0x90800
#define EIP197_HIA_DFE_BASE		0x8c000
#define EIP97_HIA_AIC_BASE		0x0
#define EIP97_HIA_DFE_THR_BASE		0xf200
#define EIP97_PE_BASE			0x10000
#define EIP197_GLOBAL_BASE		0xf0000
#define EIP197_HIA_GEN_CFG_BASE		0xf0000
#define EIP197_HIA_AIC_xDR_BASE		0x80000
#define EIP197_HIA_DFE_THR_BASE		0x8c040
#define EIP97_HIA_AIC_xDR_BASE		0x0
#define EIP197_HIA_AIC_BASE		0x90000
#define EIP197_HIA_AIC_G_BASE		0x90000
#define EIP97_HIA_DFE_BASE		0xf000
#define EIP97_HIA_DSE_BASE		0xf400
#define EIP197_PE_BASE			0xa0000
#define EIP197_HIA_DSE_THR_BASE		0x8d040
#define EIP97_GLOBAL_BASE		0x10000
#define EIP97_HIA_AIC_R_BASE		0x0
#define EIP97_HIA_GEN_CFG_BASE		0x10000
#define EIP97_HIA_DSE_THR_BASE		0xf600
#define EIP197_HIA_DSE_BASE		0x8d000
#define EIP197_REG_HI16(reg)			((reg >> 16) & 0xffff)
#define EIP197_VERSION_MASK(reg)		((reg >> 16) & 0xfff)
#define EIP197_N_RINGS_OFFSET			0
#define EIP197_REG_LO16(reg)			(reg & 0xffff)
#define EIP197_RFSIZE_ADJUST			4
#define EIP197_GLOBAL(priv)		((priv)->base + (priv)->offsets.global)
#define EIP197_N_RINGS_MASK			GENMASK(3, 0)
#define EIP207_VERSION_LE			0x30cf
#define EIP197_VERSION_SWAP(reg)		(((reg & 0xf0) << 4) | \
						((reg >> 4) & 0xf0) | \
						((reg >> 12) & 0xf))
#define EIP201_VERSION_LE			0x36c9
#define EIP97_VERSION_LE			0x9e61
#define EIP197_N_PES_OFFSET			4
#define EIP197_PE_VERSION(n)			(0x1ffc + (0x2000 * (n)))
#define EIP197_PE_OPTIONS(n)			(0x1ff8 + (0x2000 * (n)))
#define EIP96_VERSION_LE			0x9f60
#define EIP197_PE_ICE_VERSION(n)		(0x0ffc + (0x2000 * (n)))
#define EIP197_VERSION				0xfffc
#define EIP206_OPT_ICE_TYPE(n)			((n>>8)&3)
#define EIP197_CFSIZE_ADJUST			4
#define EIP197_HIA_AIC_R_VERSION(r)		(0xe01c - EIP197_HIA_AIC_R_OFF(r))
#define EIP197_HIA_OPTIONS			0xfff8
#define EIP197_MAX_RING_AIC			14
#define EIP97_HWDATAW_MASK			GENMASK(2, 0)
#define EIP197_PE_EIP96_VERSION(n)		(0x13fc + (0x2000 * (n)))
#define EIP196_VERSION_LE			0x3bc4
#define EIP197_HWDATAW_MASK			GENMASK(3, 0)
#define EIP197_HIA_OPT_HAS_PE_ARB		BIT(29)
#define EIP197_PE_EIP96_OPTIONS(n)		(0x13f8 + (0x2000 * (n)))
#define EIP197_MST_CTRL_BYTE_SWAP_BITS          GENMASK(25, 24)
#define EIP197_CFSIZE_OFFSET			9
#define EIP197_CFSIZE_MASK			GENMASK(2, 0)
#define EIP206_VERSION_LE			0x31ce
#define EIP197_OPT_HAS_TRC			BIT(31)
#define EIP206_OPT_OCE_TYPE(n)			((n>>10)&3)
#define EIP197_IRQ_NUMBER(i, is_pci)	(i + is_pci)
#define EIP97_RFSIZE_OFFSET			12
#define EIP197_HWDATAW_OFFSET			25
#define EIP197_RFSIZE_MASK			GENMASK(2, 0)
#define EIP197_HIA_VERSION_LE			0x35ca
#define EIP197_HIA_VERSION_BE			0xca35
#define EIP97_CFSIZE_MASK			GENMASK(3, 0)
#define EIP197_HIA_VERSION			0xfffc
#define EIP197_PE_PSE_VERSION(n)		(0x1efc + (0x2000 * (n)))
#define EIP197_N_PES_MASK			GENMASK(4, 0)
#define EIP197_RFSIZE_OFFSET			12
#define EIP97_RFSIZE_MASK			GENMASK(3, 0)
#define EIP97_CFSIZE_OFFSET			8
#define EIP197_VERSION_LE			0x3ac5
#define EIP197_OPTIONS				0xfff8

/* Xilinx VIP IRQ block (BAR2) */
#define EIP197_XLX_USER_INT_ENB_MSK	0x2004
#define EIP197_XLX_USER_VECT_LUT3_IDENT	0x0f0e0d0c
#define EIP197_XLX_USER_VECT_LUT1_IDENT	0x07060504
#define EIP197_XLX_GPIO_BASE		0x200000
#define EIP197_XLX_USER_VECT_LUT0_ADDR	0x2080
#define EIP197_XLX_USER_VECT_LUT3_ADDR	0x208c
#define EIP197_XLX_IRQ_BLOCK_ID_VALUE	0x1fc2
#define EIP197_XLX_USER_VECT_LUT0_IDENT	0x03020100
#define EIP197_XLX_USER_VECT_LUT2_IDENT	0x0b0a0908
#define EIP197_XLX_USER_VECT_LUT1_ADDR	0x2084
#define EIP197_XLX_IRQ_BLOCK_ID_ADDR	0x2000
#define EIP197_XLX_USER_VECT_LUT2_ADDR	0x2088

#define EIP197_HIA_xDR_OFF(priv, r)		(EIP197_HIA_AIC_xDR(priv) + (r) * 0x1000)
#define EIP197_HIA_AIC_R_OFF(r)			((r) * 0x1000)

#define EIP197_OPTION_64BIT_CTX		BIT(1)
#define EIP197_OPTION_MAGIC_VALUE	BIT(0)
#define EIP197_CONTEXT_SMALL		0x2
#define EIP197_OPTION_RC_AUTO		(0x2 << 3)
#define EIP197_OPTION_CTX_CTRL_IN_CMD	BIT(8)
#define EIP197_TYPE_BCLA		0x0

#define EIP197_HIA_AIC_xDR(priv)	((priv)->base + (priv)->offsets.hia_aic_xdr)
#define EIP197_EMB_TOKENS			4

/* Enums from driver */
enum safexcel_eip_version {
	EIP97IES_MRVL,
	EIP197B_MRVL,
	EIP197D_MRVL,
	EIP197_DEVBRD,
	EIP197C_MXL,
};
enum safexcel_alg_type {
	SAFEXCEL_ALG_TYPE_SKCIPHER,
	SAFEXCEL_ALG_TYPE_AEAD,
	SAFEXCEL_ALG_TYPE_AHASH,
};
enum safexcel_eip_algorithms {
	SAFEXCEL_ALG_BC0      = BIT(5),
	SAFEXCEL_ALG_SM4      = BIT(6),
	SAFEXCEL_ALG_SM3      = BIT(7),
	SAFEXCEL_ALG_CHACHA20 = BIT(8),
	SAFEXCEL_ALG_POLY1305 = BIT(9),
	SAFEXCEL_SEQMASK_256   = BIT(10),
	SAFEXCEL_SEQMASK_384   = BIT(11),
	SAFEXCEL_ALG_AES      = BIT(12),
	SAFEXCEL_ALG_AES_XFB  = BIT(13),
	SAFEXCEL_ALG_DES      = BIT(15),
	SAFEXCEL_ALG_DES_XFB  = BIT(16),
	SAFEXCEL_ALG_ARC4     = BIT(18),
	SAFEXCEL_ALG_AES_XTS  = BIT(20),
	SAFEXCEL_ALG_WIRELESS = BIT(21),
	SAFEXCEL_ALG_MD5      = BIT(22),
	SAFEXCEL_ALG_SHA1     = BIT(23),
	SAFEXCEL_ALG_SHA2_256 = BIT(25),
	SAFEXCEL_ALG_SHA2_512 = BIT(26),
	SAFEXCEL_ALG_XCBC_MAC = BIT(27),
	SAFEXCEL_ALG_CBC_MAC_ALL = BIT(29),
	SAFEXCEL_ALG_GHASH    = BIT(30),
	SAFEXCEL_ALG_SHA3     = BIT(31),
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

#define MAX_RINGS 4

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;

    uint32_t regs[BAR0_SIZE / sizeof(uint32_t)];

    /* DMA structures */
    struct dma_ring {
        void *base;
        dma_addr_t base_dma;
        uint32_t write;
        uint32_t read;
    } cdr, rdr;

    uint32_t status;
    bool reset;

    /* MSI-X BAR region */
    MemoryRegion msix_bar;

    /* EIP197 emulation state */
    uint32_t mst_ctrl;
    uint32_t aic_g_enable;
    uint32_t aic_r_enable[MAX_RINGS];
    uint32_t aic_r_raw[MAX_RINGS];
    bool fw_fpp_ready;
    bool fw_pue_ready;
};

G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    /* Minimal: raise IRQ if any ring raw status is non-zero and enabled */
    uint32_t pending = 0;
    for (int i = 0; i < MAX_RINGS; i++) {
        pending |= (s->aic_r_raw[i] & s->aic_r_enable[i]);
    }
    if (pending) {
        if (msix_enabled(pdev)) {
            msix_notify(pdev, 0);  /* vector 0 for all rings, simplistic */
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

/* DMA not needed for probe; removed placeholder */

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = ~0ULL;

    if (size != 4) {
        return val;
    }

    /* HIA version (EIP197) at base + 0x9fffc */
    if (addr == 0x9fffc) {
        val = EIP197_HIA_VERSION_LE;
    }
    /* Global version at base + 0xffffc */
    else if (addr == 0xffffc) {
        val = EIP197_VERSION_LE;
    }
    /* PE version at base + 0xa1ffc */
    else if (addr == 0xa1ffc) {
        val = EIP206_VERSION_LE;
    }
    /* EIP96 version at base + 0xa13fc */
    else if (addr == 0xa13fc) {
        val = EIP96_VERSION_LE;
    }
    /* Global options at base + 0xffff8 */
    else if (addr == 0xffff8) {
        val = 0;  /* no TRC */
    }
    /* HIA options at base + 0x9fff8 */
    else if (addr == 0x9fff8) {
        val = 0x4001211;  /* 1 ring, 1 PE, hwdataw=2, cfsize=1, rfsize=1 */
    }
    /* PE options at base + 0xa1ff8 */
    else if (addr == 0xa1ff8) {
        val = 0;  /* no ICE, no OCE */
    }
    /* Ring AIC registers: base HIA_AIC_R = 0x90800, ring 0 */
    else if (addr >= 0x90800 && addr < 0x90800 + 0x1000) {
        hwaddr ring_base = 0x90800;
        uint32_t ring = (addr - ring_base) / 0x1000;  /* ring index */
        hwaddr offset = addr - ring_base - ring * 0x1000;
        if (ring >= MAX_RINGS) return 0;
        
        /* Version register */
        if (offset == (0xe01c - ring * 0x1000)) {
            val = (ring == 0) ? EIP201_VERSION_LE : 0;
        }
        /* Enabled status */
        else if (offset == (0xe010 - ring * 0x1000)) {
            val = s->aic_r_raw[ring] & s->aic_r_enable[ring];
        }
        /* Enable control */
        else if (offset == (0xe008 - ring * 0x1000)) {
            val = s->aic_r_enable[ring];
        }
        /* ACK (read as raw status) */
        else if (offset == (0xe010 - ring * 0x1000)) {
            val = s->aic_r_raw[ring];
        }
        else {
            val = 0;
        }
    }
    /* MST_CTRL at 0x9fff4 */
    else if (addr == 0x9fff4) {
        val = s->mst_ctrl;
    }
    /* Global MST_CTRL at 0xffff4 (same register? but driver uses 0x9fff4) */
    else if (addr == 0xffff4) {
        val = s->mst_ctrl;
    }
    /* AIC_G_ENABLE_CTRL at 0x9f808 */
    else if (addr == 0x9f808) {
        val = s->aic_g_enable;
    }
    /* PE scratchpad firmware ready polls */
    else if (addr == 0xa0818) {
        val = s->fw_fpp_ready ? 1 : 0;
    }
    else if (addr == 0xa0814) {
        val = s->fw_pue_ready ? 1 : 0;
    }
    else {
        val = 0;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    if (size != 4) return;

    /* MST_CTRL */
    if (addr == 0x9fff4 || addr == 0xffff4) {
        s->mst_ctrl = (uint32_t)val;
    }
    /* AIC_G_ENABLE_CTRL */
    else if (addr == 0x9f808) {
        s->aic_g_enable = (uint32_t)val;
    }
    /* AIC_G_ACK (clear) */
    else if (addr == 0x9f810) {
        /* NOP for now */
    }
    /* Ring AIC registers */
    else if (addr >= 0x90800 && addr < 0x90800 + 0x1000) {
        hwaddr ring_base = 0x90800;
        uint32_t ring = (addr - ring_base) / 0x1000;
        hwaddr offset = addr - ring_base - ring * 0x1000;
        if (ring >= MAX_RINGS) return;
        
        if (offset == (0xe008 - ring * 0x1000)) {
            s->aic_r_enable[ring] = (uint32_t)val;
        } else if (offset == (0xe010 - ring * 0x1000)) {
            s->aic_r_raw[ring] &= ~((uint32_t)val);
        } else if (offset == (0xe014 - ring * 0x1000)) {
            s->aic_r_enable[ring] &= ~((uint32_t)val);
        }
    }
    /* PE FPP_CTRL write */
    else if (addr == 0xa0d80) {
        s->fw_fpp_ready = true;
    }
    /* PE PUE_CTRL write */
    else if (addr == 0xa0c80) {
        s->fw_pue_ready = true;
    }
    /* All other writes: accept without side effect */
}

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
    s->mst_ctrl = 0;
    s->aic_g_enable = 0;
    for (int i = 0; i < MAX_RINGS; i++) {
        s->aic_r_enable[i] = 0;
        s->aic_r_raw[i] = 0;
    }
    s->fw_fpp_ready = false;
    s->fw_pue_ready = false;
    s->intr_status = 0;
    s->intr_mask = 0;
    s->status = 0;
    s->reset = false;
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_config_set_class(pci_conf, PCI_CLASS_CRYPT_OTHER);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);

    s->num_bars = 2;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].name = "bar0";
    s->bar_info[0].size = BAR0_SIZE;

    s->bar_info[2].index = 2;
    s->bar_info[2].type = BAR_TYPE_MMIO;
    s->bar_info[2].name = "bar2";
    s->bar_info[2].size = BAR2_SIZE;

    for (int i = 0; i < s->num_bars; i++) {
        if (i == 0) {
            pcibase_register_bar(pdev, s, &s->bar_info[0], errp);
        } else if (i == 1) {
            /* BAR1 not used */
            continue;
        } else if (i == 2) {
            pcibase_register_bar(pdev, s, &s->bar_info[2], errp);
        }
    }

    /* MSI-X: BAR4 */
    memory_region_init(&s->msix_bar, OBJECT(s), "msix-bar", 0x1000);
    pci_register_bar(pdev, 4, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->msix_bar);
    msix_init(pdev, 16, &s->msix_bar, 4, 0, &s->msix_bar, 4, 0x800, 0x80, errp);

    /* Initialize state */
    memset(s->regs, 0, sizeof(s->regs));
    s->mst_ctrl = 0;
    s->aic_g_enable = 0;
    for (int i = 0; i < MAX_RINGS; i++) {
        s->aic_r_enable[i] = 0;
        s->aic_r_raw[i] = 0;
    }
    s->fw_fpp_ready = false;
    s->fw_pue_ready = false;
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
    /* no DMA resources to free */
}

static const VMStateDescription vmstate_pcibase = {
    .name = "crypto_safexcel_pci",
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
