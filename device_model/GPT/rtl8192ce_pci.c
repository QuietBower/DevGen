/*
 * QEMU PCI device model for rtl8192ce_pci
 * Phase 2: Behavioral implementation based on rtl8192ce driver sw.c
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
#include "hw/irq.h"

#define TYPE_PCIBASE_DEVICE "rtl8192ce_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define RTL8192CE_PCI_VENDOR_ID   0x10ec
#define RTL8192CE_PCI_DEVICE_ID   0x8191
#define RTL8192CE_PCI_CLASS_ID    0x0280

/* BAR selection from rtl92ce_hal_cfg (.bar_id = 2) */
#define RTL8192CE_PCI_BAR_INDEX   2

/* Core MAC / system control registers explicitly defined in driver headers */
#define REG_SYS_ISO_CTRL          0x0000
#define REG_SYS_FUNC_EN           0x0002
#define REG_SYS_CLKR              0x0008

/* EFUSE related registers */
#define REG_EFUSE_CTRL            0x0030
#define REG_EFUSE_TEST            0x0034

/* Interrupt mask/status registers */
#define REG_HIMR                  0x00B0
#define REG_HIMRE                 0x00B8
#define ISR                       0x00B4

/* Receive Configuration Register (used in hw_init) */
#define REG_RCR                   0x00D0

/* CAM / security related (offsets inferred from existing state fields) */
#define REG_CAMCMD                0x0670
#define REG_CAMWRITE              0x0674
#define REG_CAMREAD               0x0678
#define REG_CAMDBG                0x067C
#define REG_SECCFG                0x0680

/* Additional registers touched in new driver code that need benign handling */
#define REG_HISR                  0x00C0
#define REG_HISRE                 0x00C8
#define REG_RSV_CTRL              0x001C
#define REG_APS_FSMCO             0x0004
#define REG_SPS0_CTRL             0x0018
#define REG_AFE_XTAL_CTRL         0x0024
#define REG_CR                    0x0100
#define REG_TRXFF_BNDY            0x010C
#define REG_TRXDMA_CTRL           0x010C /* word access inside driver */
#define REG_FWHW_TXQ_CTRL         0x0424
#define REG_TCR                   0x004C
#define REG_BCNQ_DESA             0x0408
#define REG_MGQ_DESA              0x0410
#define REG_VOQ_DESA              0x0418
#define REG_VIQ_DESA              0x0420
#define REG_BEQ_DESA              0x0428
#define REG_BKQ_DESA              0x0430
#define REG_HQ_DESA               0x0438
#define REG_RX_DESA               0x0440
#define REG_PCIE_CTRL_REG         0x0360
#define REG_INT_MIG               0x0410
#define REG_APSD_CTRL             0x0600
#define REG_MCUTST_1              0x01C0
#define REG_AFE_PLL_CTRL          0x0028
#define REG_RF_CTRL               0x001F
#define REG_LEDCFG0               0x004C
#define REG_INIRTS_RATE_SEL       0x0200
#define REG_BWOPMODE              0x0203
#define REG_RRSR                  0x0208
#define REG_SLOT                  0x020B
#define REG_AMPDU_MIN_SPACE       0x020D
#define REG_RL                    0x0240
#define REG_BAR_MODE_CTRL         0x0458
#define REG_HWSEQ_CTRL            0x0207
#define REG_DARFRC                0x0210
#define REG_RARFRC                0x0218
#define REG_AGGLEN_LMT            0x0230
#define REG_ATIMWND               0x0202
#define REG_BCN_MAX_ERR           0x0550
#define REG_BCN_CTRL              0x0551
#define REG_TBTT_PROHIBIT         0x0540
#define REG_PIFS                  0x020C
#define REG_AGGR_BREAK_TIME       0x02F4
#define REG_NAV_PROT_LEN          0x0204
#define REG_PROT_MODE_CTRL        0x0206
#define REG_FAST_EDCA_CTRL        0x02FC
#define REG_ACKTO                 0x020E
#define REG_SPEC_SIFS             0x020A
#define REG_MAC_SPEC_SIFS         0x0A50
#define REG_SIFS_CTX              0x0A58
#define REG_SIFS_TRX              0x0A5C
#define REG_MAR                   0x0610
#define REG_BT_COEX_TABLE         0x0770
#define REG_GPIO_MUXCFG           0x0040
#define ROFDM0_TRXPATHENABLE      0x0C90
#define ROFDM1_TRXPATHENABLE      0x0D90
#define REG_BT_COEX_TABLE2        (REG_BT_COEX_TABLE + 4)
#define REG_BT_COEX_TABLE3        (REG_BT_COEX_TABLE + 8)
#define REG_BT_COEX_TABLE4        (REG_BT_COEX_TABLE + 0x0C)
#define REG_GPIO_PIN_CTRL         0x0044
#define REG_GPIO_IO_SEL           0x0046
#define REG_TXPAUSE               0x0522
#define REG_MCUFWDL               0x0080
#define REG_BCNTCFG               0x0510
#define MSR                       0x004C

/* Bit definitions */
#define AM                        (1U << 2)
#define AB                        (1U << 3)
#define ACRC32                    (1U << 8)
#define ACF                       (1U << 12)
#define AAP                       (1U << 0)

#define PWC_EV12V                 (1U << 15)
#define FEN_ELDR                  (1U << 12)
#define LOADER_CLK_EN             (1U << 5)

#define CAM_NONE                  0x0
#define CAM_WEP40                 0x01
#define CAM_TKIP                  0x02
#define CAM_AES                   0x04
#define CAM_WEP104                0x05

/* Interrupt mask bit definitions */
#define IMR_BCNDMAINT6            (1U << 26)
#define IMR_BCNDMAINT5            (1U << 25)
#define IMR_BCNDMAINT4            (1U << 24)
#define IMR_BCNDMAINT3            (1U << 23)
#define IMR_BCNDMAINT2            (1U << 22)
#define IMR_BCNDMAINT1            (1U << 21)

#define IMR_BCNDOK8               (1U << 25)
#define IMR_BCNDOK7               (1U << 20)
#define IMR_BCNDOK6               (1U << 19)
#define IMR_BCNDOK5               (1U << 18)
#define IMR_BCNDOK4               (1U << 17)
#define IMR_BCNDOK3               (1U << 16)
#define IMR_BCNDOK2               (1U << 15)
#define IMR_BCNDOK1               (1U << 14)

#define IMR_TIMEOUT2              (1U << 17)
#define IMR_TIMEOUT1              (1U << 16)

#define IMR_TXFOVW                (1U << 9)
#define IMR_PSTIMEOUT             (1U << 29)
#define IMR_BCNINT                (1U << 13)
#define IMR_RXFOVW                (1U << 8)
#define IMR_RDU                   (1U << 1)
#define IMR_ATIMEND               (1U << 12)
#define IMR_BDOK                  (1U << 9)
#define IMR_MGNTDOK               (1U << 6)
#define IMR_TBDER                 (1U << 26)
#define IMR_HIGHDOK               (1U << 7)
#define IMR_TBDOK                 (1U << 25)
#define IMR_BKDOK                 (1U << 5)
#define IMR_BEDOK                 (1U << 4)
#define IMR_VIDOK                 (1U << 3)
#define IMR_VODOK                 (1U << 2)
#define IMR_ROK                   (1U << 0)

/* EFUSE constants */
#define HWSET_MAX_SIZE            512
#define EFUSE_MAX_SECTION         64
#define EFUSE_REAL_CONTENT_LEN    256
#define EFUSE_OOB_PROTECT_BYTES   18

/* Descriptor rate constants */
#define DESC_RATE1M               0x00
#define DESC_RATE2M               0x01
#define DESC_RATE5_5M             0x02
#define DESC_RATE11M              0x03
#define DESC_RATE6M               0x04
#define DESC_RATE9M               0x05
#define DESC_RATE12M              0x06
#define DESC_RATE18M              0x07
#define DESC_RATE24M              0x08
#define DESC_RATE36M              0x09
#define DESC_RATE48M              0x0a
#define DESC_RATE54M              0x0b
#define DESC_RATEMCS7             0x13

/* Additional IMR bits from supplementary source */
#define IMR_CPWM                  (1U << 8)
#define IMR_C2HCMD                (1U << 10)

/* BAR types */
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

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;
    uint32_t intr_status;   /* mirrors ISR */
    uint32_t intr_mask;     /* mirrors REG_HIMR low 32 bits */

    /* Simple register shadows */
    uint8_t  sys_iso_ctrl[2];      /* 0x0000-0x0001 */
    uint8_t  sys_func_en[2];       /* 0x0002-0x0003 */
    uint8_t  sys_clkr[2];          /* 0x0008-0x0009 */

    uint8_t  efuse_ctrl[4];        /* 0x0030-0x0033 */
    uint8_t  efuse_test[4];        /* 0x0034-0x0037 */

    uint32_t reg_himr;             /* 0x00B0 */
    uint32_t reg_isr;              /* 0x00B4 - interrupt status */
    uint32_t reg_himre;            /* 0x00B8 */

    uint32_t camcmd;               /* 0x0670 */
    uint32_t camwrite;             /* 0x0674 */
    uint32_t camread;              /* 0x0678 */
    uint32_t camdbg;               /* 0x067C */
    uint32_t seccfg;               /* 0x0680 */

    uint32_t reg_rcr;              /* 0x00D0 - Receive Configuration */

    /* Additional register shadows for new driver interactions */
    uint32_t reg_hisr;             /* 0x00C0 */
    uint32_t reg_hisre;            /* 0x00C8 */
    uint8_t  rsv_ctrl;             /* 0x001C */
    uint32_t aps_fsmco;            /* 0x0004 */
    uint8_t  sps0_ctrl;            /* 0x0018 */
    uint32_t afe_xtal_ctrl;        /* 0x0024 */
    uint8_t  cr[2];                /* 0x0100-0x0101 */
    uint16_t trx_ff_bndy;          /* 0x010C+2 word access */
    uint16_t trxdma_ctrl;          /* 0x010C word */
    uint16_t fwhw_txq_ctrl;        /* 0x0424 word */
    uint32_t transmit_config;      /* REG_TCR */
    uint32_t int_mig;              /* REG_INT_MIG */
    uint8_t  apsd_ctrl;            /* REG_APSD_CTRL */
    uint32_t mcutst_1;             /* REG_MCUTST_1 */
    uint8_t  afe_pll_ctrl[2];      /* REG_AFE_PLL_CTRL and +1 */
    uint8_t  rf_ctrl;              /* REG_RF_CTRL */
    uint32_t ledcfg0;              /* REG_LEDCFG0 */

    uint8_t  inirts_rate_sel;      /* REG_INIRTS_RATE_SEL */
    uint8_t  bwopmode;             /* REG_BWOPMODE */
    uint32_t rrsr;                 /* REG_RRSR */
    uint8_t  slot_time;            /* REG_SLOT */
    uint8_t  ampdu_min_space;      /* REG_AMPDU_MIN_SPACE */
    uint16_t rl;                   /* REG_RL */
    uint32_t bar_mode_ctrl;        /* REG_BAR_MODE_CTRL */
    uint8_t  hwseq_ctrl;           /* REG_HWSEQ_CTRL */
    uint32_t darf_rc[2];           /* REG_DARFRC, +4 */
    uint32_t rarf_rc[2];           /* REG_RARFRC, +4 */
    uint32_t agglen_lmt;           /* REG_AGGLEN_LMT */
    uint8_t  atimwnd;              /* REG_ATIMWND */
    uint8_t  bcn_max_err;          /* REG_BCN_MAX_ERR */
    uint8_t  bcn_ctrl;             /* REG_BCN_CTRL */
    uint8_t  tbtt_prohibit;        /* REG_TBTT_PROHIBIT */
    uint8_t  tbtt_prohibit2;       /* REG_TBTT_PROHIBIT +1 */
    uint8_t  pifs;                 /* REG_PIFS */
    uint8_t  aggr_break_time;      /* REG_AGGR_BREAK_TIME */
    uint16_t nav_prot_len;         /* REG_NAV_PROT_LEN */
    uint16_t prot_mode_ctrl;       /* REG_PROT_MODE_CTRL */
    uint32_t fast_edca_ctrl;       /* REG_FAST_EDCA_CTRL */
    uint8_t  ackto;                /* REG_ACKTO */
    uint16_t spec_sifs;            /* REG_SPEC_SIFS */
    uint16_t mac_spec_sifs;        /* REG_MAC_SPEC_SIFS */
    uint16_t sifs_ctx;             /* REG_SIFS_CTX */
    uint16_t sifs_trx;             /* REG_SIFS_TRX */
    uint32_t mar[2];               /* REG_MAR, +4 */

    uint32_t bcnq_desa;            /* REG_BCNQ_DESA */
    uint32_t mgq_desa;             /* REG_MGQ_DESA */
    uint32_t voq_desa;             /* REG_VOQ_DESA */
    uint32_t viq_desa;             /* REG_VIQ_DESA */
    uint32_t beq_desa;             /* REG_BEQ_DESA */
    uint32_t bkq_desa;             /* REG_BKQ_DESA */
    uint32_t hq_desa;              /* REG_HQ_DESA */
    uint32_t rx_desa;              /* REG_RX_DESA */

    uint8_t  pcie_ctrl_reg3;       /* REG_PCIE_CTRL_REG + 3 */

    uint8_t  txpause;              /* REG_TXPAUSE */
    uint8_t  mcu_fwdl;             /* REG_MCUFWDL */
    uint32_t gpio_pin_ctrl;        /* REG_GPIO_PIN_CTRL */
    uint16_t gpio_io_sel;          /* REG_GPIO_IO_SEL */

    uint8_t  msr;                  /* MSR media status register */
    uint8_t  bcntcfg1;             /* REG_BCNTCFG + 1 */
};

static bool pcibase_msi_enabled(PCIBaseState *s)
{
    return msi_enabled(PCI_DEVICE(s));
}

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* Pending interrupts correspond to ISR & HIMR mask as in rtl92ce_interrupt_recognized */
    uint32_t pending = s->reg_isr & s->reg_himr;
    if (pending) {
        s->intr_status = pending;
        s->intr_mask = s->reg_himr;
        if (pcibase_msi_enabled(s)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        s->intr_status = 0;
        if (!pcibase_msi_enabled(s)) {
            pci_set_irq(pdev, 0);
        }
    }
}

static uint32_t pcibase_mmio_read32(void *opaque, hwaddr addr)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case REG_HIMR:
        return s->reg_himr;
    case ISR:
        /* rtl92ce_interrupt_recognized reads ISR and masks with rtlpci->irq_mask[0] */
        return s->reg_isr;
    case REG_HIMRE:
        return s->reg_himre;
    case REG_RCR:
        return s->reg_rcr;
    case REG_CAMCMD:
        return s->camcmd;
    case REG_CAMWRITE:
        return s->camwrite;
    case REG_CAMREAD:
        return s->camread;
    case REG_CAMDBG:
        return s->camdbg;
    case REG_SECCFG:
        return s->seccfg;
    case REG_HISR:
        /* HISR is separate from ISR; we simply mirror reg_hisr */
        return s->reg_hisr;
    case REG_HISRE:
        return s->reg_hisre;
    case REG_APS_FSMCO:
        return s->aps_fsmco;
    case REG_AFE_XTAL_CTRL:
        return s->afe_xtal_ctrl;
    case REG_TCR:
        return s->transmit_config;
    case REG_INT_MIG:
        return s->int_mig;
    case REG_MCUTST_1:
        return s->mcutst_1;
    case REG_DARFRC:
        return s->darf_rc[0];
    case REG_DARFRC + 4:
        return s->darf_rc[1];
    case REG_RARFRC:
        return s->rarf_rc[0];
    case REG_RARFRC + 4:
        return s->rarf_rc[1];
    case REG_AGGLEN_LMT:
        return s->agglen_lmt;
    case REG_MAR:
        return s->mar[0];
    case REG_MAR + 4:
        return s->mar[1];
    case REG_BAR_MODE_CTRL:
        return s->bar_mode_ctrl;
    case REG_BCNQ_DESA:
        return s->bcnq_desa;
    /* MGQ_DESA intentionally not handled as 32-bit to avoid duplicate case with REG_INT_MIG */
    case REG_VOQ_DESA:
        return s->voq_desa;
    case REG_VIQ_DESA:
        return s->viq_desa;
    case REG_BEQ_DESA:
        return s->beq_desa;
    case REG_BKQ_DESA:
        return s->bkq_desa;
    case REG_HQ_DESA:
        return s->hq_desa;
    case REG_RX_DESA:
        return s->rx_desa;
    default:
        break;
    }

    return 0;
}

static void pcibase_mmio_write32(void *opaque, hwaddr addr, uint32_t val)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case REG_HIMR:
        /* rtl92ce_enable_interrupt / disable_interrupt program HIMR */
        s->reg_himr = val;
        /* intr_mask mirrors HIMR for consistency with previous state */
        s->intr_mask = val;
        pcibase_update_irq(s);
        break;
    case ISR:
        /* rtl92ce_interrupt_recognized writes back the value it just read.
         * This is a classic write-1-to-clear behavior for interrupt status. */
        s->reg_isr &= ~val;
        pcibase_update_irq(s);
        break;
    case REG_HIMRE:
        s->reg_himre = val;
        /* upper mask bits do not affect our simple IRQ generation */
        pcibase_update_irq(s);
        break;
    case REG_RCR:
        /* rtl92ce_hw_init reads and then writes REG_RCR after clearing some bits */
        s->reg_rcr = val;
        break;
    case REG_CAMCMD:
        s->camcmd = val;
        break;
    case REG_CAMWRITE:
        s->camwrite = val;
        break;
    case REG_CAMREAD:
        s->camread = val;
        break;
    case REG_CAMDBG:
        s->camdbg = val;
        break;
    case REG_SECCFG:
        s->seccfg = val;
        break;
    case REG_HISR:
        /* Write-1-to-clear for HISR as done for ISR via REG_HISR */
        s->reg_hisr &= ~val;
        break;
    case REG_HISRE:
        s->reg_hisre = val;
        break;
    case REG_APS_FSMCO:
        s->aps_fsmco = val;
        break;
    case REG_AFE_XTAL_CTRL:
        s->afe_xtal_ctrl = val;
        break;
    case REG_TCR:
        s->transmit_config = val;
        break;
    case REG_INT_MIG:
        s->int_mig = val;
        break;
    case REG_MCUTST_1:
        s->mcutst_1 = val;
        break;
    case REG_DARFRC:
        s->darf_rc[0] = val;
        break;
    case REG_DARFRC + 4:
        s->darf_rc[1] = val;
        break;
    case REG_RARFRC:
        s->rarf_rc[0] = val;
        break;
    case REG_RARFRC + 4:
        s->rarf_rc[1] = val;
        break;
    case REG_AGGLEN_LMT:
        s->agglen_lmt = val;
        break;
    case REG_MAR:
        s->mar[0] = val;
        break;
    case REG_MAR + 4:
        s->mar[1] = val;
        break;
    case REG_BAR_MODE_CTRL:
        s->bar_mode_ctrl = val;
        break;
    case REG_BCNQ_DESA:
        s->bcnq_desa = val;
        break;
    /* MGQ_DESA intentionally not handled as 32-bit to avoid duplicate case with REG_INT_MIG */
    case REG_VOQ_DESA:
        s->voq_desa = val;
        break;
    case REG_VIQ_DESA:
        s->viq_desa = val;
        break;
    case REG_BEQ_DESA:
        s->beq_desa = val;
        break;
    case REG_BKQ_DESA:
        s->bkq_desa = val;
        break;
    case REG_HQ_DESA:
        s->hq_desa = val;
        break;
    case REG_RX_DESA:
        s->rx_desa = val;
        break;
    default:
        break;
    }
}

static uint16_t pcibase_mmio_read16(void *opaque, hwaddr addr)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case REG_SYS_ISO_CTRL:
        return s->sys_iso_ctrl[0] | (s->sys_iso_ctrl[1] << 8);
    case REG_SYS_FUNC_EN:
        return s->sys_func_en[0] | (s->sys_func_en[1] << 8);
    case REG_SYS_CLKR:
        return s->sys_clkr[0] | (s->sys_clkr[1] << 8);
    case REG_EFUSE_CTRL:
        return s->efuse_ctrl[0] | (s->efuse_ctrl[1] << 8);
    case REG_EFUSE_TEST:
        return s->efuse_test[0] | (s->efuse_test[1] << 8);
    case REG_TRXFF_BNDY + 2:
        return s->trx_ff_bndy;
    case REG_TRXDMA_CTRL:
        return s->trxdma_ctrl;
    case REG_FWHW_TXQ_CTRL:
        return s->fwhw_txq_ctrl;
    case REG_RL:
        return s->rl;
    case REG_NAV_PROT_LEN:
        return s->nav_prot_len;
    case REG_PROT_MODE_CTRL:
        return s->prot_mode_ctrl;
    case REG_SPEC_SIFS:
        return s->spec_sifs;
    case REG_MAC_SPEC_SIFS:
        return s->mac_spec_sifs;
    case REG_SIFS_CTX:
        return s->sifs_ctx;
    case REG_SIFS_TRX:
        return s->sifs_trx;
    case REG_GPIO_IO_SEL:
        return s->gpio_io_sel;
    default:
        break;
    }

    return 0;
}

static void pcibase_mmio_write16(void *opaque, hwaddr addr, uint16_t val)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case REG_SYS_ISO_CTRL:
        s->sys_iso_ctrl[0] = val & 0xff;
        s->sys_iso_ctrl[1] = (val >> 8) & 0xff;
        break;
    case REG_SYS_FUNC_EN:
        s->sys_func_en[0] = val & 0xff;
        s->sys_func_en[1] = (val >> 8) & 0xff;
        break;
    case REG_SYS_CLKR:
        s->sys_clkr[0] = val & 0xff;
        s->sys_clkr[1] = (val >> 8) & 0xff;
        break;
    case REG_EFUSE_CTRL:
        s->efuse_ctrl[0] = val & 0xff;
        s->efuse_ctrl[1] = (val >> 8) & 0xff;
        break;
    case REG_EFUSE_TEST:
        s->efuse_test[0] = val & 0xff;
        s->efuse_test[1] = (val >> 8) & 0xff;
        break;
    case REG_TRXFF_BNDY + 2:
        s->trx_ff_bndy = val;
        break;
    case REG_TRXDMA_CTRL:
        s->trxdma_ctrl = val;
        break;
    case REG_FWHW_TXQ_CTRL:
        s->fwhw_txq_ctrl = val;
        break;
    case REG_RL:
        s->rl = val;
        break;
    case REG_NAV_PROT_LEN:
        s->nav_prot_len = val;
        break;
    case REG_PROT_MODE_CTRL:
        s->prot_mode_ctrl = val;
        break;
    case REG_SPEC_SIFS:
        s->spec_sifs = val;
        break;
    case REG_MAC_SPEC_SIFS:
        s->mac_spec_sifs = val;
        break;
    case REG_SIFS_CTX:
        s->sifs_ctx = val;
        break;
    case REG_SIFS_TRX:
        s->sifs_trx = val;
        break;
    case REG_GPIO_IO_SEL:
        s->gpio_io_sel = val;
        break;
    default:
        break;
    }
}

static uint8_t pcibase_mmio_read8(void *opaque, hwaddr addr)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case REG_SYS_ISO_CTRL:
        return s->sys_iso_ctrl[0];
    case REG_SYS_ISO_CTRL + 1:
        return s->sys_iso_ctrl[1];
    case REG_SYS_FUNC_EN:
        return s->sys_func_en[0];
    case REG_SYS_FUNC_EN + 1:
        return s->sys_func_en[1];
    case REG_SYS_CLKR:
        return s->sys_clkr[0];
    case REG_SYS_CLKR + 1:
        return s->sys_clkr[1];
    case REG_EFUSE_CTRL:
        return s->efuse_ctrl[0];
    case REG_EFUSE_CTRL + 1:
        return s->efuse_ctrl[1];
    case REG_EFUSE_CTRL + 2:
        return s->efuse_ctrl[2];
    case REG_EFUSE_CTRL + 3:
        return s->efuse_ctrl[3];
    case REG_EFUSE_TEST:
        return s->efuse_test[0];
    case REG_EFUSE_TEST + 1:
        return s->efuse_test[1];
    case REG_EFUSE_TEST + 2:
        return s->efuse_test[2];
    case REG_EFUSE_TEST + 3:
        return s->efuse_test[3];
    case REG_RSV_CTRL:
        return s->rsv_ctrl;
    case REG_SPS0_CTRL:
        return s->sps0_ctrl;
    case REG_APS_FSMCO + 1:
        /* upper byte used for BIT(0) polling in _rtl92ce_init_mac */
        return (s->aps_fsmco >> 8) & 0xff;
    case REG_AFE_XTAL_CTRL + 1:
        return (s->afe_xtal_ctrl >> 8) & 0xff;
    case REG_AFE_XTAL_CTRL + 2:
        return (s->afe_xtal_ctrl >> 16) & 0xff;
    case REG_AFE_XTAL_CTRL + 3:
        return (s->afe_xtal_ctrl >> 24) & 0xff;
    case REG_CR:
        return s->cr[0];
    case REG_CR + 1:
        return s->cr[1];
    case REG_APSD_CTRL:
        return s->apsd_ctrl;
    case REG_AFE_PLL_CTRL:
        return s->afe_pll_ctrl[0];
    case REG_AFE_PLL_CTRL + 1:
        return s->afe_pll_ctrl[1];
    case REG_RF_CTRL:
        return s->rf_ctrl;
    case REG_INIRTS_RATE_SEL:
        return s->inirts_rate_sel;
    case REG_BWOPMODE:
        return s->bwopmode;
    case REG_SLOT:
        return s->slot_time;
    case REG_AMPDU_MIN_SPACE:
        return s->ampdu_min_space;
    case REG_HWSEQ_CTRL:
        return s->hwseq_ctrl;
    case REG_ATIMWND:
        return s->atimwnd;
    case REG_BCN_MAX_ERR:
        return s->bcn_max_err;
    case REG_BCN_CTRL:
        return s->bcn_ctrl;
    case REG_TBTT_PROHIBIT:
        return s->tbtt_prohibit;
    case REG_TBTT_PROHIBIT + 1:
        return s->tbtt_prohibit2;
    case REG_PIFS:
        return s->pifs;
    case REG_AGGR_BREAK_TIME:
        return s->aggr_break_time;
    case REG_ACKTO:
        return s->ackto;
    case REG_PCIE_CTRL_REG + 3:
        return s->pcie_ctrl_reg3;
    case REG_TXPAUSE:
        return s->txpause;
    case REG_MCUFWDL:
        return s->mcu_fwdl;
    case REG_GPIO_PIN_CTRL:
        return s->gpio_pin_ctrl & 0xff;
    case REG_GPIO_PIN_CTRL + 1:
        return (s->gpio_pin_ctrl >> 8) & 0xff;
    case REG_GPIO_PIN_CTRL + 2:
        return (s->gpio_pin_ctrl >> 16) & 0xff;
    case REG_GPIO_PIN_CTRL + 3:
        return (s->gpio_pin_ctrl >> 24) & 0xff;
    case MSR:
        return s->msr;
    case REG_BCNTCFG + 1:
        return s->bcntcfg1;
    default:
        break;
    }

    return 0;
}

static void pcibase_mmio_write8(void *opaque, hwaddr addr, uint8_t val)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case REG_SYS_ISO_CTRL:
        s->sys_iso_ctrl[0] = val;
        break;
    case REG_SYS_ISO_CTRL + 1:
        s->sys_iso_ctrl[1] = val;
        break;
    case REG_SYS_FUNC_EN:
        s->sys_func_en[0] = val;
        break;
    case REG_SYS_FUNC_EN + 1:
        s->sys_func_en[1] = val;
        break;
    case REG_SYS_CLKR:
        s->sys_clkr[0] = val;
        break;
    case REG_SYS_CLKR + 1:
        s->sys_clkr[1] = val;
        break;
    case REG_EFUSE_CTRL:
        s->efuse_ctrl[0] = val;
        break;
    case REG_EFUSE_CTRL + 1:
        s->efuse_ctrl[1] = val;
        break;
    case REG_EFUSE_CTRL + 2:
        s->efuse_ctrl[2] = val;
        break;
    case REG_EFUSE_CTRL + 3:
        s->efuse_ctrl[3] = val;
        break;
    case REG_EFUSE_TEST:
        s->efuse_test[0] = val;
        break;
    case REG_EFUSE_TEST + 1:
        s->efuse_test[1] = val;
        break;
    case REG_EFUSE_TEST + 2:
        s->efuse_test[2] = val;
        break;
    case REG_EFUSE_TEST + 3:
        s->efuse_test[3] = val;
        break;
    case REG_RSV_CTRL:
        s->rsv_ctrl = val;
        break;
    case REG_SPS0_CTRL:
        s->sps0_ctrl = val;
        break;
    case REG_APS_FSMCO + 1: {
        uint32_t tmp = s->aps_fsmco;
        tmp &= ~0x0000FF00U;
        tmp |= ((uint32_t)val << 8);
        s->aps_fsmco = tmp;
        break;
    }
    case REG_AFE_XTAL_CTRL + 1: {
        uint32_t tmp = s->afe_xtal_ctrl;
        tmp &= ~0x0000FF00U;
        tmp |= ((uint32_t)val << 8);
        s->afe_xtal_ctrl = tmp;
        break;
    }
    case REG_AFE_XTAL_CTRL + 2: {
        uint32_t tmp = s->afe_xtal_ctrl;
        tmp &= ~0x00FF0000U;
        tmp |= ((uint32_t)val << 16);
        s->afe_xtal_ctrl = tmp;
        break;
    }
    case REG_AFE_XTAL_CTRL + 3: {
        uint32_t tmp = s->afe_xtal_ctrl;
        tmp &= ~0xFF000000U;
        tmp |= ((uint32_t)val << 24);
        s->afe_xtal_ctrl = tmp;
        break;
    }
    case REG_CR:
        s->cr[0] = val;
        break;
    case REG_CR + 1:
        s->cr[1] = val;
        break;
    case REG_APSD_CTRL:
        s->apsd_ctrl = val;
        break;
    case REG_AFE_PLL_CTRL:
        s->afe_pll_ctrl[0] = val;
        break;
    case REG_AFE_PLL_CTRL + 1:
        s->afe_pll_ctrl[1] = val;
        break;
    case REG_RF_CTRL:
        s->rf_ctrl = val;
        break;
    case REG_INIRTS_RATE_SEL:
        s->inirts_rate_sel = val;
        break;
    case REG_BWOPMODE:
        s->bwopmode = val;
        break;
    case REG_SLOT:
        s->slot_time = val;
        break;
    case REG_AMPDU_MIN_SPACE:
        s->ampdu_min_space = val;
        break;
    case REG_HWSEQ_CTRL:
        s->hwseq_ctrl = val;
        break;
    case REG_ATIMWND:
        s->atimwnd = val;
        break;
    case REG_BCN_MAX_ERR:
        s->bcn_max_err = val;
        break;
    case REG_BCN_CTRL:
        s->bcn_ctrl = val;
        break;
    case REG_TBTT_PROHIBIT:
        s->tbtt_prohibit = val;
        break;
    case REG_TBTT_PROHIBIT + 1:
        s->tbtt_prohibit2 = val;
        break;
    case REG_PIFS:
        s->pifs = val;
        break;
    case REG_AGGR_BREAK_TIME:
        s->aggr_break_time = val;
        break;
    case REG_ACKTO:
        s->ackto = val;
        break;
    case REG_PCIE_CTRL_REG + 3:
        s->pcie_ctrl_reg3 = val;
        break;
    case REG_TXPAUSE:
        s->txpause = val;
        break;
    case REG_MCUFWDL:
        s->mcu_fwdl = val;
        break;
    case REG_GPIO_PIN_CTRL: {
        uint32_t tmp = s->gpio_pin_ctrl;
        tmp &= ~0x000000FFU;
        tmp |= val;
        s->gpio_pin_ctrl = tmp;
        break;
    }
    case REG_GPIO_PIN_CTRL + 1: {
        uint32_t tmp = s->gpio_pin_ctrl;
        tmp &= ~0x0000FF00U;
        tmp |= ((uint32_t)val << 8);
        s->gpio_pin_ctrl = tmp;
        break;
    }
    case REG_GPIO_PIN_CTRL + 2: {
        uint32_t tmp = s->gpio_pin_ctrl;
        tmp &= ~0x00FF0000U;
        tmp |= ((uint32_t)val << 16);
        s->gpio_pin_ctrl = tmp;
        break;
    }
    case REG_GPIO_PIN_CTRL + 3: {
        uint32_t tmp = s->gpio_pin_ctrl;
        tmp &= ~0xFF000000U;
        tmp |= ((uint32_t)val << 24);
        s->gpio_pin_ctrl = tmp;
        break;
    }
    case MSR:
        s->msr = val;
        break;
    case REG_BCNTCFG + 1:
        s->bcntcfg1 = val;
        break;
    default:
        break;
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    switch (size) {
    case 1:
        return pcibase_mmio_read8(opaque, addr);
    case 2:
        return pcibase_mmio_read16(opaque, addr);
    case 4:
        return pcibase_mmio_read32(opaque, addr);
    default:
        return 0;
    }
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    switch (size) {
    case 1:
        pcibase_mmio_write8(opaque, addr, (uint8_t)val);
        break;
    case 2:
        pcibase_mmio_write16(opaque, addr, (uint16_t)val);
        break;
    case 4:
        pcibase_mmio_write32(opaque, addr, (uint32_t)val);
        break;
    default:
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    (void)opaque;
    (void)addr;
    (void)size;
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    (void)opaque;
    (void)addr;
    (void)val;
    (void)size;
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
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

    memset(s->sys_iso_ctrl, 0, sizeof(s->sys_iso_ctrl));
    memset(s->sys_func_en, 0, sizeof(s->sys_func_en));
    memset(s->sys_clkr, 0, sizeof(s->sys_clkr));
    memset(s->efuse_ctrl, 0, sizeof(s->efuse_ctrl));
    memset(s->efuse_test, 0, sizeof(s->efuse_test));

    s->reg_himr = 0;
    s->reg_isr = 0;
    s->reg_hisr = 0;
    s->reg_hisre = 0;
    s->reg_himre = 0;
    s->camcmd = 0;
    s->camwrite = 0;
    s->camread = 0;
    s->camdbg = 0;
    s->seccfg = 0;
    s->reg_rcr = 0;

    s->rsv_ctrl = 0;
    s->aps_fsmco = 0;
    s->sps0_ctrl = 0;
    s->afe_xtal_ctrl = 0;
    s->cr[0] = 0;
    s->cr[1] = 0;
    s->trx_ff_bndy = 0;
    s->trxdma_ctrl = 0;
    s->fwhw_txq_ctrl = 0;
    s->transmit_config = 0;
    s->int_mig = 0;
    s->apsd_ctrl = 0;
    s->mcutst_1 = 0;
    s->afe_pll_ctrl[0] = 0;
    s->afe_pll_ctrl[1] = 0;
    s->rf_ctrl = 0;
    s->ledcfg0 = 0;

    s->inirts_rate_sel = 0;
    s->bwopmode = 0;
    s->rrsr = 0;
    s->slot_time = 0;
    s->ampdu_min_space = 0;
    s->rl = 0;
    s->bar_mode_ctrl = 0;
    s->hwseq_ctrl = 0;
    s->darf_rc[0] = 0;
    s->darf_rc[1] = 0;
    s->rarf_rc[0] = 0;
    s->rarf_rc[1] = 0;
    s->agglen_lmt = 0;
    s->atimwnd = 0;
    s->bcn_max_err = 0;
    s->bcn_ctrl = 0;
    s->tbtt_prohibit = 0;
    s->tbtt_prohibit2 = 0;
    s->pifs = 0;
    s->aggr_break_time = 0;
    s->nav_prot_len = 0;
    s->prot_mode_ctrl = 0;
    s->fast_edca_ctrl = 0;
    s->ackto = 0;
    s->spec_sifs = 0;
    s->mac_spec_sifs = 0;
    s->sifs_ctx = 0;
    s->sifs_trx = 0;
    s->mar[0] = 0;
    s->mar[1] = 0;

    s->bcnq_desa = 0;
    s->mgq_desa = 0;
    s->voq_desa = 0;
    s->viq_desa = 0;
    s->beq_desa = 0;
    s->bkq_desa = 0;
    s->hq_desa = 0;
    s->rx_desa = 0;

    s->pcie_ctrl_reg3 = 0;
    s->txpause = 0;
    s->mcu_fwdl = 0;
    s->gpio_pin_ctrl = 0;
    s->gpio_io_sel = 0;
    s->msr = 0;
    s->bcntcfg1 = 0;

    s->intr_status = 0;
    s->intr_mask = 0;
    pcibase_update_irq(s);
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

    pci_set_word(pci_conf + PCI_VENDOR_ID, RTL8192CE_PCI_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, RTL8192CE_PCI_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, RTL8192CE_PCI_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    s->num_bars = 1;
    for (int i = 0; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = "";
    }

    s->bar_info[0].index = RTL8192CE_PCI_BAR_INDEX;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x10000;
    s->bar_info[0].name = "rtl8192ce-mmio";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = false;
    s->has_msix = false;

    pcibase_reset(DEVICE(pdev));
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
    .name = "rtl8192ce_pci",
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
