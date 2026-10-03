/*
 * QEMU 8.2.10 PCI device model for rtl8192de
 * Phase 2: Functional behavior implementation based strictly on sw.c
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
/* #HeadFile# */

#define TYPE_PCIBASE_DEVICE "rtl8192de_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
/* #Related_Config_Info# */
#define PCIBASE_VENDOR_ID 0x10ec /* PCI_VENDOR_ID_REALTEK */
#define PCIBASE_DEVICE_ID 0x8193 /* from first rtl92de_pci_ids entry */
#define PCIBASE_CLASS_ID  0x0280 /* Network controller, other (generic WLAN) */

/* BAR index is provided by rtl92de_hal_cfg.bar_id = 2, BAR size is not
 * explicitly defined in sw.c; use a conservative 64 KiB window here. */
#define PCIBASE_BAR_INDEX 2
#define PCIBASE_BAR_SIZE  (64 * KiB)

/* Interrupt-related masks present in this translation unit */
#define PCIBASE_IMR_BDOK     (1U << 9)
#define PCIBASE_IMR_TIMEOUT1 (1U << 16)
#define PCIBASE_IMR_TIMEOUT2 (1U << 17)
#define PCIBASE_IMR_BCNINT   (1U << 13)
#define PCIBASE_IMR_BCNDOK8  (1U << 25)

/* Additional interrupt mask bits from supplementary driver source */
#define PCIBASE_IMR_BCNDMAINT6  (1U << 26)
#define PCIBASE_IMR_BCNDMAINT5  (1U << 25)
#define PCIBASE_IMR_BCNDMAINT4  (1U << 24)
#define PCIBASE_IMR_BCNDMAINT3  (1U << 23)
#define PCIBASE_IMR_BCNDMAINT2  (1U << 22)
#define PCIBASE_IMR_BCNDMAINT1  (1U << 21)
#define PCIBASE_IMR_BCNDOK7     (1U << 20)
#define PCIBASE_IMR_BCNDOK6     (1U << 19)
#define PCIBASE_IMR_BCNDOK5     (1U << 18)
#define PCIBASE_IMR_BCNDOK4     (1U << 17)
#define PCIBASE_IMR_BCNDOK3     (1U << 16)
#define PCIBASE_IMR_BCNDOK2     (1U << 15)
#define PCIBASE_IMR_BCNDOK1     (1U << 14)
#define PCIBASE_IMR_TXFOVW      (1U << 9)
#define PCIBASE_IMR_PSTIMEOUT   (1U << 29)
#define PCIBASE_IMR_RXFOVW      (1U << 8)
#define PCIBASE_IMR_RDU         (1U << 1)
#define PCIBASE_IMR_ATIMEND     (1U << 12)
#define PCIBASE_IMR_MGNTDOK     (1U << 6)
#define PCIBASE_IMR_TBDER       (1U << 26)
#define PCIBASE_IMR_HIGHDOK     (1U << 7)
#define PCIBASE_IMR_TBDOK       (1U << 25)
#define PCIBASE_IMR_BKDOK       (1U << 5)
#define PCIBASE_IMR_BEDOK       (1U << 4)
#define PCIBASE_IMR_VIDOK       (1U << 3)
#define PCIBASE_IMR_VODOK       (1U << 2)
#define PCIBASE_IMR_ROK         (1U << 0)

/* Example MAC/PHY control register used during init */
#define PCIBASE_REG_MAC_PHY_CTRL_NORMAL 0x00f8

/* DBI/PCIe configuration related registers referenced in sw.c */
#define PCIBASE_REG_MAC0                  0x0081
#define PCIBASE_REG_MAC1                  0x0053
#define PCIBASE_REG_POWER_OFF_IN_PROCESS  0x0017
#define PCIBASE_REG_DBI_RDATA             0x034c
#define PCIBASE_REG_DBI_CTRL              0x0350
#define PCIBASE_REG_DBI_FLAG              0x0352
#define PCIBASE_REG_DBI_WDATA             0x0348

/* MAC1/MAC0 firmware ready flags */
#define PCIBASE_FW_MAC1_READY 0x1a
#define PCIBASE_MAC1_READY    (1U << 0)
#define PCIBASE_FW_MAC0_READY 0x18
#define PCIBASE_MAC0_READY    (1U << 0)

/* System control and EFUSE related registers from supplementary source */
#define PCIBASE_REG_SYS_ISO_CTRL   0x0000
#define PCIBASE_REG_SYS_FUNC_EN    0x0002
#define PCIBASE_REG_SYS_CLKR       0x0008
#define PCIBASE_REG_EFUSE_CTRL     0x0030
#define PCIBASE_REG_EFUSE_TEST     0x0034

/* Receive Configuration Register (RCR) bits from supplementary source */
#define PCIBASE_RCR_AAP     (1U << 0)
#define PCIBASE_RCR_AM      (1U << 2)
#define PCIBASE_RCR_AB      (1U << 3)
#define PCIBASE_RCR_ACRC32  (1U << 8)
#define PCIBASE_RCR_ACF     (1U << 12)

/* Power Configuration bits */
#define PCIBASE_PWC_EV12V   (1U << 15)
#define PCIBASE_FEN_ELDR    (1U << 12)
#define PCIBASE_LOADER_CLK_EN (1U << 5)

/* EFUSE layout constants */
#define PCIBASE_HWSET_MAX_SIZE         512
#define PCIBASE_EFUSE_MAX_SECTION      64
#define PCIBASE_EFUSE_REAL_CONTENT_LEN 256

/* Security CAM related registers */
#define PCIBASE_REG_CAMCMD    0x0670
#define PCIBASE_REG_CAMWRITE  0x0674
#define PCIBASE_REG_CAMREAD   0x0678
#define PCIBASE_REG_CAMDBG    0x067C
#define PCIBASE_REG_SECCFG    0x0680

/* CAM entry key types */
#define PCIBASE_CAM_NONE   0x00
#define PCIBASE_CAM_WEP40  0x01
#define PCIBASE_CAM_TKIP   0x02
#define PCIBASE_CAM_AES    0x04
#define PCIBASE_CAM_WEP104 0x05

/* Descriptor rate definitions (static values only, no behavior) */
#define PCIBASE_DESC_RATE1M      0x00
#define PCIBASE_DESC_RATE2M      0x01
#define PCIBASE_DESC_RATE5_5M    0x02
#define PCIBASE_DESC_RATE11M     0x03
#define PCIBASE_DESC_RATE6M      0x04
#define PCIBASE_DESC_RATE9M      0x05
#define PCIBASE_DESC_RATE12M     0x06
#define PCIBASE_DESC_RATE18M     0x07
#define PCIBASE_DESC_RATE24M     0x08
#define PCIBASE_DESC_RATE36M     0x09
#define PCIBASE_DESC_RATE48M     0x0a
#define PCIBASE_DESC_RATE54M     0x0b
#define PCIBASE_DESC_RATEMCS7    0x13

/* Module parameters and specification bits (static only) */
#define PCIBASE_MODPARAM_DMA64_BIT   (1U << 0)
#define PCIBASE_SPEC_NEW_RATEID      (1U << 0)
#define PCIBASE_SPEC_SUPPORT_VHT     (1U << 1)
#define PCIBASE_SPEC_EXT_C2H         (1U << 2)

/* Interrupt-related registers actually used by the driver */
#define PCIBASE_REG_ISR    0x003c  /* ISR - Interrupt Status Register */
#define PCIBASE_REG_HIMR   0x00b0  /* Interrupt Mask Register */
#define PCIBASE_REG_HIMRE  0x00b4  /* Extended Interrupt Mask Register */
#define PCIBASE_REG_INT_MIG 0x0304 /* Interrupt migration */
#define PCIBASE_REG_PCIE_CTRL_REG 0x0300
#define PCIBASE_REG_HIMR2  PCIBASE_REG_HIMRE
#define PCIBASE_REG_CR     0x0100
#define PCIBASE_REG_MAC_PHY_CTRL_NORMAL_ALIAS PCIBASE_REG_MAC_PHY_CTRL_NORMAL

/* Beacon/TSF related registers */
#define PCIBASE_REG_ATIMWND           0x055a
#define PCIBASE_REG_BCN_INTERVAL      0x0554
#define PCIBASE_REG_BCNTCFG           0x0510
#define PCIBASE_REG_RXTSF_OFFSET_CCK  0x055e
#define PCIBASE_REG_RXTSF_OFFSET_OFDM 0x055f
#define PCIBASE_REG_TSFTR             0x0054 /* low dword; high at +4, reused */

/* ACM / QoS related */
#define PCIBASE_REG_ACMHWCTRL 0x05c0

/* APSD and power control */
#define PCIBASE_REG_APSD_CTRL 0x0600

/* Receive Configuration Register address */
#define PCIBASE_REG_RCR 0x00f0

/* Some driver constants, reflected here for completeness */
#define PCIBASE_IMR8190_DISABLED 0x0
#define PCIBASE_RT_AC_INT_MASKS (PCIBASE_IMR_VIDOK | PCIBASE_IMR_VODOK | PCIBASE_IMR_BEDOK | PCIBASE_IMR_BKDOK)


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
    uint32_t intr_status;   /* mirrors ISR */
    uint32_t intr_mask;     /* HIMR - primary mask */
    uint32_t intr_mask_ext; /* HIMRE - extended mask */

    /* Hardware Register Shadows (The 'Identity' of the device) */
    struct {
        uint32_t mac_phy_ctrl;
        uint32_t dbi_rdata;
        uint32_t dbi_ctrl;
        uint32_t dbi_flag;
        uint32_t dbi_wdata;
        uint8_t  mac0_reg;
        uint8_t  mac1_reg;
        uint8_t  power_off_in_process;
        uint8_t  fw_mac0_ready;
        uint8_t  fw_mac1_ready;
        /* Additional control and EFUSE related shadows */
        uint16_t sys_iso_ctrl;
        uint16_t sys_func_en;
        uint16_t sys_clkr;
        uint32_t efuse_ctrl;
        uint32_t efuse_test;
        uint32_t rcr;
        uint32_t camcmd;
        uint32_t camwrite;
        uint32_t camread;
        uint32_t camdbg;
        uint32_t seccfg;
        /* Interrupt and beacon related registers */
        uint32_t himr;
        uint32_t himre;
        uint32_t int_mig;
        uint16_t atimwnd;
        uint16_t bcn_interval;
        uint16_t bcntcfg;
        uint8_t  rxtsf_offset_cck;
        uint8_t  rxtsf_offset_ofdm;
        uint16_t pcie_ctrl_reg;
        uint8_t  cr;
        uint8_t  mac_phyctl_reg_shadow; /* used by suspend/resume path */
        uint8_t  acmhwctrl;
        uint8_t  apsd_ctrl;
    } regs;

    /* DMA Context */
    /* no explicit DMA control registers in sw.c */

    struct {
        bool initialized;
        bool fw_ready;
    } status;

    struct {
        bool in_reset;
    } reset_state;

    struct {
        uint32_t cur_ps_level;
    } pm_state;

    /* #Other_Addition_Info_Stru# */
};

/* #Other_Addition_Info_Defin# */

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* Interrupt is raised when any unmasked status bit is set. */
    uint32_t pending = s->intr_status & s->intr_mask;
    if (pending) {
        if (msix_enabled(pdev)) {
            msix_notify(pdev, 0);
        } else if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        /* Deassert legacy INTx if nothing pending. MSI/MSI-X are edge-based. */
        if (!msi_enabled(pdev) && !msix_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* No explicit DMA engine registers are described in sw.c; do nothing. */
    (void)s;
    (void)pdev;
    (void)is_write;
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Implement only explicitly referenced registers. Unmapped addresses read as 0. */
    switch (addr) {
    case PCIBASE_REG_MAC_PHY_CTRL_NORMAL:
        val = s->regs.mac_phy_ctrl;
        break;

    case PCIBASE_REG_DBI_RDATA:
        val = s->regs.dbi_rdata;
        break;
    case PCIBASE_REG_DBI_CTRL:
        val = s->regs.dbi_ctrl;
        break;
    case PCIBASE_REG_DBI_FLAG:
        val = s->regs.dbi_flag;
        break;
    case PCIBASE_REG_DBI_WDATA:
        val = s->regs.dbi_wdata;
        break;

    case PCIBASE_REG_SYS_ISO_CTRL:
        val = s->regs.sys_iso_ctrl;
        break;
    case PCIBASE_REG_SYS_FUNC_EN:
        val = s->regs.sys_func_en;
        break;
    case PCIBASE_REG_SYS_CLKR:
        val = s->regs.sys_clkr;
        break;

    case PCIBASE_REG_EFUSE_CTRL:
        val = s->regs.efuse_ctrl;
        break;
    case PCIBASE_REG_EFUSE_TEST:
        val = s->regs.efuse_test;
        break;

    case PCIBASE_REG_CAMCMD:
        val = s->regs.camcmd;
        break;
    case PCIBASE_REG_CAMWRITE:
        val = s->regs.camwrite;
        break;
    case PCIBASE_REG_CAMREAD:
        val = s->regs.camread;
        break;
    case PCIBASE_REG_CAMDBG:
        val = s->regs.camdbg;
        break;
    case PCIBASE_REG_SECCFG:
        val = s->regs.seccfg;
        break;

    case PCIBASE_REG_MAC0:
        val = s->regs.mac0_reg;
        break;
    case PCIBASE_REG_MAC1:
        val = s->regs.mac1_reg;
        break;
    case PCIBASE_REG_POWER_OFF_IN_PROCESS:
        val = s->regs.power_off_in_process;
        break;

    case PCIBASE_FW_MAC0_READY:
        val = s->regs.fw_mac0_ready;
        break;
    case PCIBASE_FW_MAC1_READY:
        val = s->regs.fw_mac1_ready;
        break;

    case PCIBASE_REG_RCR:
        val = s->regs.rcr;
        break;

    case PCIBASE_REG_HIMR:
        val = s->regs.himr;
        break;
    case PCIBASE_REG_HIMRE:
        val = s->regs.himre;
        break;

    case PCIBASE_REG_ISR:
        /* rtl92de_interrupt_recognized: rtl_read_dword(ISR) & irq_mask[0] */
        val = s->intr_status;
        break;

    case PCIBASE_REG_INT_MIG:
        val = s->regs.int_mig;
        break;

    case PCIBASE_REG_ATIMWND:
        val = s->regs.atimwnd;
        break;
    case PCIBASE_REG_BCN_INTERVAL:
        val = s->regs.bcn_interval;
        break;
    case PCIBASE_REG_BCNTCFG:
        val = s->regs.bcntcfg;
        break;
    case PCIBASE_REG_RXTSF_OFFSET_CCK:
        val = s->regs.rxtsf_offset_cck;
        break;
    case PCIBASE_REG_RXTSF_OFFSET_OFDM:
        val = s->regs.rxtsf_offset_ofdm;
        break;

    case PCIBASE_REG_PCIE_CTRL_REG:
        val = s->regs.pcie_ctrl_reg;
        break;

    case PCIBASE_REG_CR:
        val = s->regs.cr;
        break;

    case PCIBASE_REG_ACMHWCTRL:
        val = s->regs.acmhwctrl;
        break;

    case PCIBASE_REG_APSD_CTRL:
        val = s->regs.apsd_ctrl;
        break;

    default:
        /* For unhandled addresses, keep val = 0. */
        break;
    }

    /* Truncate to requested size */
    if (size == 1) {
        val &= 0xff;
    } else if (size == 2) {
        val &= 0xffff;
    } else if (size == 4) {
        val &= 0xffffffffU;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* Update only explicitly referenced registers. */
    switch (addr) {
    case PCIBASE_REG_MAC_PHY_CTRL_NORMAL:
        if (size == 1) {
            /* lower byte into shadow */
            s->regs.mac_phy_ctrl &= ~0xFFU;
            s->regs.mac_phy_ctrl |= (uint32_t)(val & 0xFFU);
        } else if (size == 2) {
            s->regs.mac_phy_ctrl &= ~0xFFFFU;
            s->regs.mac_phy_ctrl |= (uint32_t)(val & 0xFFFFU);
        } else if (size == 4) {
            s->regs.mac_phy_ctrl = (uint32_t)val;
        }
        /* rtl92de_suspend/rtl92de_resume use REG_MAC_PHY_CTRL_NORMAL via
         * rtlpriv->rtlhal.macphyctl_reg, which is stored separately in the
         * driver. We just maintain this shadow. */
        s->regs.mac_phyctl_reg_shadow = (uint8_t)(s->regs.mac_phy_ctrl & 0xff);
        break;

    case PCIBASE_REG_DBI_RDATA:
        if (size == 4) {
            s->regs.dbi_rdata = (uint32_t)val;
        }
        break;
    case PCIBASE_REG_DBI_CTRL:
        if (size == 4) {
            s->regs.dbi_ctrl = (uint32_t)val;
        }
        break;
    case PCIBASE_REG_DBI_FLAG:
        if (size == 2) {
            s->regs.dbi_flag = (s->regs.dbi_flag & 0xFFFF0000U) | (uint32_t)(val & 0xFFFFU);
        } else if (size == 4) {
            s->regs.dbi_flag = (uint32_t)val;
        }
        break;
    case PCIBASE_REG_DBI_WDATA:
        if (size == 4) {
            s->regs.dbi_wdata = (uint32_t)val;
        }
        break;

    case PCIBASE_REG_SYS_ISO_CTRL:
        if (size == 2) {
            s->regs.sys_iso_ctrl = (uint16_t)val;
        }
        break;
    case PCIBASE_REG_SYS_FUNC_EN:
        if (size == 1) {
            /* several paths write byte to REG_SYS_FUNC_EN */
            uint16_t tmp = s->regs.sys_func_en;
            tmp &= ~0x00FFU;
            tmp |= (uint16_t)(val & 0xFFU);
            s->regs.sys_func_en = tmp;
        } else if (size == 2) {
            s->regs.sys_func_en = (uint16_t)val;
        }
        break;
    case PCIBASE_REG_SYS_CLKR:
        if (size == 2) {
            s->regs.sys_clkr = (uint16_t)val;
        }
        break;

    case PCIBASE_REG_EFUSE_CTRL:
        if (size == 4) {
            s->regs.efuse_ctrl = (uint32_t)val;
        }
        break;
    case PCIBASE_REG_EFUSE_TEST:
        if (size == 4) {
            s->regs.efuse_test = (uint32_t)val;
        }
        break;

    case PCIBASE_REG_CAMCMD:
        if (size == 4) {
            s->regs.camcmd = (uint32_t)val;
        }
        break;
    case PCIBASE_REG_CAMWRITE:
        if (size == 4) {
            s->regs.camwrite = (uint32_t)val;
        }
        break;
    case PCIBASE_REG_CAMREAD:
        if (size == 4) {
            s->regs.camread = (uint32_t)val;
        }
        break;
    case PCIBASE_REG_CAMDBG:
        if (size == 4) {
            s->regs.camdbg = (uint32_t)val;
        }
        break;
    case PCIBASE_REG_SECCFG:
        if (size == 1) {
            s->regs.seccfg &= ~0xFFU;
            s->regs.seccfg |= (uint32_t)(val & 0xFFU);
        } else if (size == 2) {
            s->regs.seccfg &= ~0xFFFFU;
            s->regs.seccfg |= (uint32_t)(val & 0xFFFFU);
        } else if (size == 4) {
            s->regs.seccfg = (uint32_t)val;
        }
        break;

    case PCIBASE_REG_MAC0:
        if (size == 1) {
            s->regs.mac0_reg = (uint8_t)val;
        }
        break;
    case PCIBASE_REG_MAC1:
        if (size == 1) {
            s->regs.mac1_reg = (uint8_t)val;
        }
        break;
    case PCIBASE_REG_POWER_OFF_IN_PROCESS:
        if (size == 1) {
            s->regs.power_off_in_process = (uint8_t)val;
        }
        break;

    case PCIBASE_FW_MAC0_READY:
        if (size == 1) {
            s->regs.fw_mac0_ready = (uint8_t)val;
            if (s->regs.fw_mac0_ready & PCIBASE_MAC0_READY) {
                s->status.fw_ready = true;
            }
        }
        break;
    case PCIBASE_FW_MAC1_READY:
        if (size == 1) {
            s->regs.fw_mac1_ready = (uint8_t)val;
        }
        break;

    case PCIBASE_REG_RCR:
        if (size == 4) {
            s->regs.rcr = (uint32_t)val;
        } else if (size == 2) {
            uint32_t tmp = s->regs.rcr;
            tmp &= ~0x0000FFFFU;
            tmp |= (uint32_t)(val & 0xFFFFU);
            s->regs.rcr = tmp;
        }
        break;

    case PCIBASE_REG_HIMR:
        if (size == 4) {
            s->regs.himr = (uint32_t)val;
            s->intr_mask = s->regs.himr;
        }
        break;
    case PCIBASE_REG_HIMRE:
        if (size == 4) {
            s->regs.himre = (uint32_t)val;
            s->intr_mask_ext = s->regs.himre;
        }
        break;

    case PCIBASE_REG_ISR:
        if (size == 4) {
            /* rtl92de_interrupt_recognized: W1C of bits that were read */
            uint32_t clr = (uint32_t)val;
            s->intr_status &= ~clr;
            pcibase_update_irq(s);
        }
        break;

    case PCIBASE_REG_INT_MIG:
        if (size == 4) {
            s->regs.int_mig = (uint32_t)val;
        }
        break;

    case PCIBASE_REG_ATIMWND:
        if (size == 2) {
            s->regs.atimwnd = (uint16_t)val;
        }
        break;
    case PCIBASE_REG_BCN_INTERVAL:
        if (size == 2) {
            s->regs.bcn_interval = (uint16_t)val;
        }
        break;
    case PCIBASE_REG_BCNTCFG:
        if (size == 2) {
            s->regs.bcntcfg = (uint16_t)val;
        }
        break;
    case PCIBASE_REG_RXTSF_OFFSET_CCK:
        if (size == 1) {
            s->regs.rxtsf_offset_cck = (uint8_t)val;
        }
        break;
    case PCIBASE_REG_RXTSF_OFFSET_OFDM:
        if (size == 1) {
            s->regs.rxtsf_offset_ofdm = (uint8_t)val;
        }
        break;

    case PCIBASE_REG_PCIE_CTRL_REG:
        if (size == 2) {
            s->regs.pcie_ctrl_reg = (uint16_t)val;
        } else if (size == 4) {
            s->regs.pcie_ctrl_reg = (uint32_t)val;
        }
        break;
    case PCIBASE_REG_PCIE_CTRL_REG + 1:
        if (size == 1) {
            uint32_t tmp = s->regs.pcie_ctrl_reg;
            tmp &= ~0x0000FF00U;
            tmp |= ((uint32_t)(val & 0xFFU) << 8);
            s->regs.pcie_ctrl_reg = tmp;
        }
        break;

    case PCIBASE_REG_CR:
        if (size == 1) {
            s->regs.cr = (uint8_t)val;
        } else if (size == 2) {
            s->regs.cr = (uint8_t)(val & 0xFFU);
        } else if (size == 4) {
            s->regs.cr = (uint8_t)(val & 0xFFU);
        }
        break;
    case PCIBASE_REG_CR + 1:
        if (size == 1) {
            /* rtl92de_set_hw_reg modifies REG_CR + 1, but we only
             * store the low byte in cr. Keep it untouched here. */
        }
        break;

    case PCIBASE_REG_ACMHWCTRL:
        if (size == 1) {
            s->regs.acmhwctrl = (uint8_t)val;
        } else if (size == 4) {
            s->regs.acmhwctrl = (uint8_t)(val & 0xFFU);
        }
        break;

    case PCIBASE_REG_APSD_CTRL:
        if (size == 1) {
            s->regs.apsd_ctrl = (uint8_t)val;
        }
        break;

    default:
        /* Unhandled writes are ignored. */
        break;
    }

    /* Some register writes may influence interrupt state in real hardware,
     * but sw.c does not specify such behavior, so we do not modify intr_status
     * here.
     */
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* No explicit PIO accesses are shown in sw.c; return 0. */
    (void)s;
    (void)addr;
    (void)size;
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    /* No explicit PIO accesses are shown in sw.c; ignore writes. */
    (void)s;
    (void)addr;
    (void)val;
    (void)size;
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

    /* Reset all internal shadow registers to sensible defaults visible to driver. */
    s->intr_status = 0;
    s->intr_mask = 0;
    s->intr_mask_ext = 0;

    s->status.initialized = false;
    s->status.fw_ready = false;
    s->reset_state.in_reset = true;
    s->pm_state.cur_ps_level = 0;

    s->regs.mac_phy_ctrl = 0;
    s->regs.dbi_rdata = 0;
    s->regs.dbi_ctrl = 0;
    s->regs.dbi_flag = 0;
    s->regs.dbi_wdata = 0;
    s->regs.mac0_reg = 0;
    s->regs.mac1_reg = 0;
    s->regs.power_off_in_process = 0;
    s->regs.fw_mac0_ready = 0;
    s->regs.fw_mac1_ready = 0;
    s->regs.sys_iso_ctrl = 0;
    s->regs.sys_func_en = 0;
    s->regs.sys_clkr = 0;
    s->regs.efuse_ctrl = 0;
    s->regs.efuse_test = 0;
    s->regs.rcr = 0;
    s->regs.camcmd = 0;
    s->regs.camwrite = 0;
    s->regs.camread = 0;
    s->regs.camdbg = 0;
    s->regs.seccfg = 0;
    s->regs.himr = 0;
    s->regs.himre = 0;
    s->regs.int_mig = 0;
    s->regs.atimwnd = 0;
    s->regs.bcn_interval = 0;
    s->regs.bcntcfg = 0;
    s->regs.rxtsf_offset_cck = 0;
    s->regs.rxtsf_offset_ofdm = 0;
    s->regs.pcie_ctrl_reg = 0;
    s->regs.cr = 0;
    s->regs.mac_phyctl_reg_shadow = 0;
    s->regs.acmhwctrl = 0;
    s->regs.apsd_ctrl = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCIBASE_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCIBASE_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCIBASE_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    /* #BAR_CONFIG_INIT#  */
    s->num_bars = 1;
    for (int i = 0; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = "rtl8192de-bar";
    }
    s->bar_info[0].index = PCIBASE_BAR_INDEX;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = PCIBASE_BAR_SIZE;
    s->bar_info[0].name = "rtl8192de-mmio";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* #MSI_OR_MSIX_INIT#  */
    s->has_msi = false;
    s->has_msix = false;

    /* #DMA_Config_Real#   */
    /* No explicit DMA mask setup in sw.c; leave default. */

    /* #Timer_Config_Real# */
    /* No explicit hardware timers described in sw.c. */

    /* #Field_Init_Real#   */
    s->intr_status = 0;
    s->intr_mask = 0;
    s->intr_mask_ext = 0;
    s->status.initialized = false;
    s->status.fw_ready = false;
    s->reset_state.in_reset = false;
    s->pm_state.cur_ps_level = 0;
    s->regs.mac_phy_ctrl = 0;
    s->regs.dbi_rdata = 0;
    s->regs.dbi_ctrl = 0;
    s->regs.dbi_flag = 0;
    s->regs.dbi_wdata = 0;
    s->regs.mac0_reg = 0;
    s->regs.mac1_reg = 0;
    s->regs.power_off_in_process = 0;
    s->regs.fw_mac0_ready = 0;
    s->regs.fw_mac1_ready = 0;
    s->regs.sys_iso_ctrl = 0;
    s->regs.sys_func_en = 0;
    s->regs.sys_clkr = 0;
    s->regs.efuse_ctrl = 0;
    s->regs.efuse_test = 0;
    s->regs.rcr = 0;
    s->regs.camcmd = 0;
    s->regs.camwrite = 0;
    s->regs.camread = 0;
    s->regs.camdbg = 0;
    s->regs.seccfg = 0;
    s->regs.himr = 0;
    s->regs.himre = 0;
    s->regs.int_mig = 0;
    s->regs.atimwnd = 0;
    s->regs.bcn_interval = 0;
    s->regs.bcntcfg = 0;
    s->regs.rxtsf_offset_cck = 0;
    s->regs.rxtsf_offset_ofdm = 0;
    s->regs.pcie_ctrl_reg = 0;
    s->regs.cr = 0;
    s->regs.mac_phyctl_reg_shadow = 0;
    s->regs.acmhwctrl = 0;
    s->regs.apsd_ctrl = 0;
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

    /* #Uninit_Func# */
    (void)s;
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "rtl8192de_pci",
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
