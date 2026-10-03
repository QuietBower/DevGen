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

#define TYPE_PCIBASE_DEVICE "rtl8821ae_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#ifndef PCI_VENDOR_ID_REALTEK
#define PCI_VENDOR_ID_REALTEK 0x10ec
#endif
#define PCI_DEVICE_ID_REALTEK_8821 0x8821
#define PCI_CLASS_NETWORK_OTHER 0x0280

#define REG_MCUTST_WOWLAN 0x01C7
#define REG_AMPDU_MAX_LENGTH_8812 0x0458
#define REG_CCK_CHECK 0x0454
#define REG_OPT_CTRL 0x0074
#define DM_REG_IGI_A_11AC 0xC50
#define DM_REG_IGI_B_11AC 0xE50
#define RA_RSSI_DUMP 0XBF0
#define RB_CFO_LONG_DUMP 0XBEE
#define RA_CFO_SHORT_DUMP 0XBF8
#define RB_RX_SNR_DUMP 0XBF7
#define RB_CFO_SHORT_DUMP 0XBFA
#define RS1_RX_EVM_DUMP 0XBF4
#define RB_RSSI_DUMP 0XBF1
#define RA_RX_SNR_DUMP 0XBF6
#define RS2_RX_EVM_DUMP 0XBF5
#define RA_CFO_LONG_DUMP 0XBEC
#define RB_PIREAD_8821A 0xd44
#define RHSSIREAD_8821AE 0x8b0
#define RA_LSSIWRITE_8821A 0xc90
#define RA_PIREAD_8821A 0xd04
#define RB_LSSIWRITE_8821A 0xe90
#define RA_SIREAD_8821A 0xd08
#define RB_SIREAD_8821A 0xd48
#define ODM_REG_CCK_RPT_FORMAT_11AC 0x804
#define DM_REG_CCK_CCA_11AC 0xA0A
#define RFC_AREA 0x860
#define RCCK_RX 0xa04
#define RB_TXSCALE 0xe1c
#define ROFDMCCKEN 0x808
#define RTXPATH 0x80c
#define RA_RFE_PINMUX 0xcb0
#define RA_TXSCALE 0xc1c
#define RL1PEAKTH 0x848
#define BCCK_SYSTEM 0x10
#define RCCAONSEC 0x838
#define RCCK_SYSTEM 0xa00
#define RADC_BUF_CLK 0x8c4
#define RRFMOD 0x8ac
#define ODM_REG_CCK_CCA_11AC 0xA0A
#define ODM_REG_OFDM_FA_RST_11AC 0x9A4
#define ODM_REG_BB_RX_PATH_11AC 0x808
#define ODM_REG_OFDM_FA_11AC 0xF48
#define ODM_REG_CCK_FA_11AC 0xA5C
#define ODM_REG_CCK_FA_RST_11AC 0xA2C

#define REG_SYS_ISO_CTRL 0x0000
#define REG_SYS_FUNC_EN 0x0002
#define REG_SYS_CLKR 0x0008
#define AM BIT(2)
#define AB BIT(3)
#define ACRC32 BIT(8)
#define ACF BIT(12)
#define AAP BIT(0)
#define REG_HIMR 0x0120
#define REG_HIMRE 0x0128
#define REG_EFUSE_ACCESS 0x00CF
#define REG_EFUSE_TEST 0x0034
#define REG_EFUSE_CTRL 0x0030
#define PWC_EV12V BIT(15)
#define FEN_ELDR BIT(12)
#define LOADER_CLK_EN BIT(5)
#define ANA8M BIT(1)
#define HWSET_MAX_SIZE 128
#define EFUSE_MAX_SECTION 16
#define EFUSE_REAL_CONTENT_LEN 512
#define EFUSE_OOB_PROTECT_BYTES 15
#define REG_CAMCMD 0x0670
#define REG_CAMWRITE 0x0674
#define REG_CAMREAD 0x0678
#define REG_CAMDBG 0x067C
#define REG_SECCFG 0x0680
#define CAM_NONE 0x0
#define CAM_WEP40 0x01
#define CAM_TKIP 0x02
#define CAM_AES 0x04
#define CAM_WEP104 0x05
#define IMR_BCNDMAINT6 BIT(31)
#define IMR_BCNDMAINT5 BIT(30)
#define IMR_BCNDMAINT4 BIT(29)
#define IMR_BCNDMAINT3 BIT(28)
#define IMR_BCNDMAINT2 BIT(27)
#define IMR_BCNDMAINT1 BIT(26)
#define IMR_BCNDOK7 BIT(24)
#define IMR_BCNDOK6 BIT(23)
#define IMR_BCNDOK5 BIT(22)
#define IMR_BCNDOK4 BIT(21)
#define IMR_BCNDOK3 BIT(20)
#define IMR_BCNDOK2 BIT(19)
#define IMR_BCNDOK1 BIT(18)
#define IMR_TXFOVW BIT(15)
#define IMR_PSTIMEOUT BIT(14)
#define IMR_BCNDMAINT0 BIT(20)
#define IMR_RXFOVW BIT(12)
#define IMR_RDU BIT(11)
#define IMR_ATIMEND BIT(10)
#define IMR_BCNDOK0 BIT(16)
#define IMR_MGNTDOK BIT(6)
#define IMR_TBDER BIT(5)
#define IMR_HIGHDOK BIT(8)
#define IMR_TBDOK BIT(7)
#define IMR_BKDOK BIT(4)
#define IMR_BEDOK BIT(3)
#define IMR_VIDOK BIT(2)
#define IMR_VODOK BIT(1)
#define IMR_ROK BIT(0)
#define DESC_RATE1M 0x00
#define DESC_RATE2M 0x01
#define DESC_RATE5_5M 0x02
#define DESC_RATE11M 0x03
#define DESC_RATE6M 0x04
#define DESC_RATE9M 0x05
#define DESC_RATE12M 0x06
#define DESC_RATE18M 0x07
#define DESC_RATE24M 0x08
#define DESC_RATE36M 0x09
#define DESC_RATE48M 0x0a
#define DESC_RATE54M 0x0b
#define DESC_RATEMCS7 0x13

#define REG_CR 0x00
#define REG_SYS_CFG 0x00F0
#define REG_WOW_CTRL 0x0690
#define REG_RXDMA_CONTROL 0x0286
#define REG_PCIE_CTRL_REG 0x0300
#define REG_HISRE 0x012C
#define REG_HSIMR 0x0058
#define REG_MACID 0x0610
#define REG_BSSID 0x0618
#define MSR (REG_CR + 2)
#define REG_BCN_INTERVAL 0x0554
#define REG_ATIMWND 0x055A
#define REG_RCR 0x0608
#define REG_TSFTR 0x0560
#define REG_RRSR 0x0440
#define REG_SIFS_CTX 0x0514
#define REG_SIFS_TRX 0x0516
#define REG_SPEC_SIFS 0x0428
#define REG_MAC_SPEC_SIFS 0x063A
#define REG_RESP_SIFS_OFDM 0x063E
#define REG_SLOT 0x051B
#define REG_TRXPTCL_CTL 0x0668
#define REG_AMPDU_MIN_SPACE 0x045C
#define REG_ACMHWCTRL 0x05C0
#define REG_RL 0x042A
#define REG_DUAL_TSF_RST 0x0553
#define REG_PCIE_HRPWM 0x0361
#define REG_BCN_PSR_RPT 0x06A8
#define REG_NAV_UPPER 0x0652
#define REG_9346CR 0x000A

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
    uint32_t irq_mask[4];
    uint32_t sys_irq_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t reg_874;
    uint32_t reg_c70;
    uint32_t reg_85c;
    uint32_t reg_a74;
    uint32_t reg_bcn_ctrl_val;
    uint32_t reg_ampdu_max_length;
    uint8_t  reg_seccfg;

    uint32_t reg_cr;
    uint32_t reg_sys_cfg;
    uint32_t reg_wow_ctrl;
    uint32_t reg_rxdma_control;
    uint32_t reg_pcie_ctrl_reg;
    uint32_t reg_hisre;
    uint32_t reg_hsimr;
    uint32_t reg_macid;
    uint32_t reg_bssid;
    uint32_t reg_msr;
    uint32_t reg_bcn_interval;
    uint32_t reg_atimwnd;
    uint32_t reg_rcr;
    uint32_t reg_tsftr;
    uint32_t reg_rrsr;
    uint32_t reg_sifs_ctx;
    uint32_t reg_sifs_trx;
    uint32_t reg_spec_sifs;
    uint32_t reg_mac_spec_sifs;
    uint32_t reg_resp_sifs_ofdm;
    uint32_t reg_slot;
    uint32_t reg_trxptcl_ctl;
    uint32_t reg_ampdu_min_space;
    uint32_t reg_acmhwctrl;
    uint32_t reg_rl;
    uint32_t reg_dual_tsf_rst;
    uint32_t reg_pcie_hrpwm;
    uint32_t reg_bcn_psr_rpt;
    uint32_t reg_nav_upper;
    uint32_t reg_9346cr;

    /* DMA Context */
    dma_addr_t tx_ring_dma[9];
    dma_addr_t rx_ring_dma[2];

    bool hwradiooff;
    bool swrf_processing;
    bool first_init;
    bool init_ready;
    uint8_t fw_ps_state;
    uint32_t cur_ps_level;
    uint8_t mac_addr[6];
};

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case REG_HIMR:
        val = s->irq_mask[0];
        break;
    case REG_HIMRE:
        val = s->irq_mask[1];
        break;
    case REG_SECCFG:
        val = s->reg_seccfg;
        break;
    case REG_AMPDU_MAX_LENGTH_8812:
        val = s->reg_ampdu_max_length;
        break;
    case REG_CR:
        val = s->reg_cr;
        break;
    case REG_SYS_CFG:
        val = s->reg_sys_cfg;
        break;
    case REG_WOW_CTRL:
        val = s->reg_wow_ctrl;
        break;
    case REG_RXDMA_CONTROL:
        val = s->reg_rxdma_control;
        break;
    case REG_PCIE_CTRL_REG:
        val = s->reg_pcie_ctrl_reg;
        break;
    case REG_HISRE:
        val = s->reg_hisre;
        break;
    case REG_HSIMR:
        val = s->reg_hsimr;
        break;
    case REG_MACID:
        val = s->reg_macid;
        break;
    case REG_BSSID:
        val = s->reg_bssid;
        break;
    case MSR:
        val = s->reg_msr;
        break;
    case REG_BCN_INTERVAL:
        val = s->reg_bcn_interval;
        break;
    case REG_ATIMWND:
        val = s->reg_atimwnd;
        break;
    case REG_RCR:
        val = s->reg_rcr;
        break;
    case REG_TSFTR:
        val = s->reg_tsftr;
        break;
    case REG_RRSR:
        val = s->reg_rrsr;
        break;
    case REG_SIFS_CTX:
        val = s->reg_sifs_ctx;
        break;
    case REG_SIFS_TRX:
        val = s->reg_sifs_trx;
        break;
    case REG_SPEC_SIFS:
        val = s->reg_spec_sifs;
        break;
    case REG_MAC_SPEC_SIFS:
        val = s->reg_mac_spec_sifs;
        break;
    case REG_RESP_SIFS_OFDM:
        val = s->reg_resp_sifs_ofdm;
        break;
    case REG_SLOT:
        val = s->reg_slot;
        break;
    case REG_TRXPTCL_CTL:
        val = s->reg_trxptcl_ctl;
        break;
    case REG_AMPDU_MIN_SPACE:
        val = s->reg_ampdu_min_space;
        break;
    case REG_ACMHWCTRL:
        val = s->reg_acmhwctrl;
        break;
    case REG_RL:
        val = s->reg_rl;
        break;
    case REG_DUAL_TSF_RST:
        val = s->reg_dual_tsf_rst;
        break;
    case REG_PCIE_HRPWM:
        val = s->reg_pcie_hrpwm;
        break;
    case REG_BCN_PSR_RPT:
        val = s->reg_bcn_psr_rpt;
        break;
    case REG_NAV_UPPER:
        val = s->reg_nav_upper;
        break;
    case REG_9346CR:
        val = s->reg_9346cr;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented read from 0x%" HWADDR_PRIx "\n", __func__, addr);
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case REG_HIMR:
        s->irq_mask[0] = val;
        break;
    case REG_HIMRE:
        s->irq_mask[1] = val;
        break;
    case REG_SECCFG:
        s->reg_seccfg = val & 0xFF;
        break;
    case REG_AMPDU_MAX_LENGTH_8812:
        s->reg_ampdu_max_length = val;
        break;
    case REG_CR:
        s->reg_cr = val;
        break;
    case REG_SYS_CFG:
        s->reg_sys_cfg = val;
        break;
    case REG_WOW_CTRL:
        s->reg_wow_ctrl = val;
        break;
    case REG_RXDMA_CONTROL:
        s->reg_rxdma_control = val;
        break;
    case REG_PCIE_CTRL_REG:
        s->reg_pcie_ctrl_reg = val;
        break;
    case REG_HISRE:
        s->reg_hisre = val;
        break;
    case REG_HSIMR:
        s->reg_hsimr = val;
        break;
    case REG_MACID:
        s->reg_macid = val;
        break;
    case REG_BSSID:
        s->reg_bssid = val;
        break;
    case MSR:
        s->reg_msr = val;
        break;
    case REG_BCN_INTERVAL:
        s->reg_bcn_interval = val;
        break;
    case REG_ATIMWND:
        s->reg_atimwnd = val;
        break;
    case REG_RCR:
        s->reg_rcr = val;
        break;
    case REG_TSFTR:
        s->reg_tsftr = val;
        break;
    case REG_RRSR:
        s->reg_rrsr = val;
        break;
    case REG_SIFS_CTX:
        s->reg_sifs_ctx = val;
        break;
    case REG_SIFS_TRX:
        s->reg_sifs_trx = val;
        break;
    case REG_SPEC_SIFS:
        s->reg_spec_sifs = val;
        break;
    case REG_MAC_SPEC_SIFS:
        s->reg_mac_spec_sifs = val;
        break;
    case REG_RESP_SIFS_OFDM:
        s->reg_resp_sifs_ofdm = val;
        break;
    case REG_SLOT:
        s->reg_slot = val;
        break;
    case REG_TRXPTCL_CTL:
        s->reg_trxptcl_ctl = val;
        break;
    case REG_AMPDU_MIN_SPACE:
        s->reg_ampdu_min_space = val;
        break;
    case REG_ACMHWCTRL:
        s->reg_acmhwctrl = val;
        break;
    case REG_RL:
        s->reg_rl = val;
        break;
    case REG_DUAL_TSF_RST:
        s->reg_dual_tsf_rst = val;
        break;
    case REG_PCIE_HRPWM:
        s->reg_pcie_hrpwm = val;
        break;
    case REG_BCN_PSR_RPT:
        s->reg_bcn_psr_rpt = val;
        break;
    case REG_NAV_UPPER:
        s->reg_nav_upper = val;
        break;
    case REG_9346CR:
        s->reg_9346cr = val;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented write to 0x%" HWADDR_PRIx " val 0x%" PRIx64 "\n", __func__, addr, val);
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
    
    s->irq_mask[0] = 0;
    s->irq_mask[1] = 0;
    s->reg_seccfg = 0;
    s->reg_ampdu_max_length = 0;
    s->reg_cr = 0;
    s->reg_sys_cfg = 0;
    s->reg_wow_ctrl = 0;
    s->reg_rxdma_control = 0;
    s->reg_pcie_ctrl_reg = 0;
    s->reg_hisre = 0;
    s->reg_hsimr = 0;
    s->reg_macid = 0;
    s->reg_bssid = 0;
    s->reg_msr = 0;
    s->reg_bcn_interval = 0;
    s->reg_atimwnd = 0;
    s->reg_rcr = 0;
    s->reg_tsftr = 0;
    s->reg_rrsr = 0;
    s->reg_sifs_ctx = 0;
    s->reg_sifs_trx = 0;
    s->reg_spec_sifs = 0;
    s->reg_mac_spec_sifs = 0;
    s->reg_resp_sifs_ofdm = 0;
    s->reg_slot = 0;
    s->reg_trxptcl_ctl = 0;
    s->reg_ampdu_min_space = 0;
    s->reg_acmhwctrl = 0;
    s->reg_rl = 0;
    s->reg_dual_tsf_rst = 0;
    s->reg_pcie_hrpwm = 0;
    s->reg_bcn_psr_rpt = 0;
    s->reg_nav_upper = 0;
    s->reg_9346cr = 0;
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
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY | PCI_BASE_ADDRESS_MEM_TYPE_64, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY | PCI_BASE_ADDRESS_MEM_TYPE_64, mr);
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_REALTEK );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_REALTEK_8821 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_OTHER );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    
    /* The driver hardcodes PMCSR at 0x44, so PM cap must be at 0x40 */
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0x40, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* Initialize BARs */
    s->num_bars = 2;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 64 * 1024;
    s->bar_info[0].name = "rtl8821ae-mmio0";

    s->bar_info[1].index = 2;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = 64 * 1024;
    s->bar_info[1].name = "rtl8821ae-mmio2";

    /* BAR Initialization */
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    if (msi_init(pdev, 0, 1, true, false, errp) == 0) {
        s->has_msi = true;
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

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "rtl8821ae_pci",
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
