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

#define TYPE_PCIBASE_DEVICE "rtl8188ee_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define RTL8188EE_VENDOR_ID 0x10ec
#define RTL8188EE_DEVICE_ID 0x8179
#define RTL8188EE_CLASS_ID  0x0280 /* PCI_CLASS_NETWORK_OTHER */

#define REG_SYS_ISO_CTRL 0x0000
#define REG_SYS_FUNC_EN 0x0002
#define REG_SYS_CLKR 0x0008
#define REG_HIMR 0x0120
#define REG_HIMRE 0x0128
#define REG_HSISR 0x005c
#define REG_EFUSE_ACCESS 0x00CF
#define REG_EFUSE_TEST 0x0034
#define REG_EFUSE_CTRL 0x0030
#define REG_CAMCMD 0x0670
#define REG_CAMWRITE 0x0674
#define REG_CAMREAD 0x0678
#define REG_CAMDBG 0x067C
#define REG_SECCFG 0x0680

#define REG_CR				0x00
#define REG_RCR					0x0608
#define REG_HISRE				0x012C
#define REG_MACID				0x0610
#define REG_RRSR				0x0440
#define REG_INIRTS_RATE_SEL			0x0480
#define REG_BSSID				0x0618
#define REG_SIFS_CTX				0x0514
#define REG_SIFS_TRX				0x0516
#define REG_SPEC_SIFS				0x0428
#define REG_MAC_SPEC_SIFS			0x063A
#define REG_RESP_SIFS_OFDM			0x063E
#define REG_SLOT				0x051B
#define REG_TRXPTCL_CTL				0x0668
#define REG_AMPDU_MIN_SPACE			0x045C
#define REG_AGGLEN_LMT				0x0458
#define REG_ACMHWCTRL				0x05C0
#define REG_RL					0x042A
#define REG_DUAL_TSF_RST			0x0553
#define REG_PCIE_HRPWM				0x0361
#define REG_FWHW_TXQ_CTRL			0x0420
#define REG_TDECTRL				0x0208
#define REG_BCN_PSR_RPT				0x06A8
#define REG_TSFTR				0x0560
#define REG_NAV_CTRL				0x0650

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
    uint32_t sys_iso_ctrl;
    uint32_t sys_func_en;
    uint32_t sys_clk;
    uint32_t mac_himr;
    uint32_t mac_hsisr;
    uint32_t sec_cfg;
    uint32_t reg_16;

    uint32_t reg_rcr;
    uint32_t reg_hisre;
    uint32_t reg_macid;
    uint32_t reg_rrsr;
    uint32_t reg_inirts_rate_sel;
    uint32_t reg_bssid;
    uint32_t reg_sifs_ctx;
    uint32_t reg_sifs_trx;
    uint32_t reg_spec_sifs;
    uint32_t reg_mac_spec_sifs;
    uint32_t reg_resp_sifs_ofdm;
    uint32_t reg_slot;
    uint32_t reg_trxptcl_ctl;
    uint32_t reg_ampdu_min_space;
    uint32_t reg_agglen_lmt;
    uint32_t reg_acmhwctrl;
    uint32_t reg_rl;
    uint32_t reg_dual_tsf_rst;
    uint32_t reg_pcie_hrpwm;
    uint32_t reg_fwhw_txq_ctrl;
    uint32_t reg_tdectrl;
    uint32_t reg_bcn_psr_rpt;
    uint32_t reg_tsftr;
    uint32_t reg_nav_ctrl;

    /* DMA Context */
    struct {
        dma_addr_t dma;
        unsigned int idx;
        unsigned int entries;
    } tx_ring[9]; /* RTL_PCI_MAX_TX_QUEUE_COUNT */

    struct {
        dma_addr_t dma;
        unsigned int idx;
    } rx_ring[2]; /* RTL_PCI_MAX_RX_QUEUE */

    bool irq_enabled;
    bool msi_support;
    bool using_msi;
    bool first_init;
    bool being_init_adapter;
    uint8_t const_pci_aspm;
    bool support_aspm;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool level = (s->mac_hsisr & s->mac_himr) != 0;

    if (s->msi_support && msi_enabled(pdev)) {
        if (level) {
            msi_notify(pdev, 0);
        }
    } else {
        pci_set_irq(pdev, level);
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case REG_SYS_ISO_CTRL: /* Also REG_CR */
        val = s->sys_iso_ctrl;
        break;
    case REG_SYS_FUNC_EN:
        val = s->sys_func_en;
        break;
    case REG_SYS_CLKR:
        val = s->sys_clk;
        break;
    case REG_SYS_CLKR + 1:
        val = (s->sys_clk >> 8) & 0xFF;
        break;
    case REG_HIMR:
        val = s->mac_himr;
        break;
    case REG_HSISR:
        val = s->mac_hsisr;
        break;
    case REG_SECCFG:
        val = s->sec_cfg;
        break;
    case 0x16:
        val = s->reg_16;
        break;
    case REG_RCR:
        val = s->reg_rcr;
        break;
    case REG_HISRE:
        val = s->reg_hisre;
        break;
    case REG_MACID:
        val = s->reg_macid;
        break;
    case REG_RRSR:
        val = s->reg_rrsr;
        break;
    case REG_INIRTS_RATE_SEL:
        val = s->reg_inirts_rate_sel;
        break;
    case REG_BSSID:
        val = s->reg_bssid;
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
    case REG_AGGLEN_LMT:
        val = s->reg_agglen_lmt;
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
    case REG_FWHW_TXQ_CTRL:
        val = s->reg_fwhw_txq_ctrl;
        break;
    case REG_TDECTRL:
        val = s->reg_tdectrl;
        break;
    case REG_BCN_PSR_RPT:
        val = s->reg_bcn_psr_rpt;
        break;
    case REG_TSFTR:
        val = s->reg_tsftr;
        break;
    case REG_NAV_CTRL:
        val = s->reg_nav_ctrl;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: Unimplemented read at offset 0x%" HWADDR_PRIx "\n", __func__, addr);
        break;
    }
    
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case REG_SYS_ISO_CTRL: /* Also REG_CR */
        s->sys_iso_ctrl = val;
        break;
    case REG_SYS_FUNC_EN:
        s->sys_func_en = val;
        break;
    case REG_SYS_CLKR:
        s->sys_clk = val;
        break;
    case REG_SYS_CLKR + 1:
        s->sys_clk = (s->sys_clk & 0xFFFF00FF) | ((val & 0xFF) << 8);
        break;
    case REG_HIMR:
        s->mac_himr = val;
        pcibase_update_irq(s);
        break;
    case REG_HSISR:
        /* Assuming Write-1-to-Clear (W1C) based on typical interrupt status register behavior */
        s->mac_hsisr &= ~val;
        pcibase_update_irq(s);
        break;
    case REG_SECCFG:
        s->sec_cfg = val;
        break;
    case 0x16:
        s->reg_16 = val;
        break;
    case REG_RCR:
        s->reg_rcr = val;
        break;
    case REG_HISRE:
        s->reg_hisre = val;
        break;
    case REG_MACID:
        s->reg_macid = val;
        break;
    case REG_RRSR:
        s->reg_rrsr = val;
        break;
    case REG_INIRTS_RATE_SEL:
        s->reg_inirts_rate_sel = val;
        break;
    case REG_BSSID:
        s->reg_bssid = val;
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
    case REG_AGGLEN_LMT:
        s->reg_agglen_lmt = val;
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
    case REG_FWHW_TXQ_CTRL:
        s->reg_fwhw_txq_ctrl = val;
        break;
    case REG_TDECTRL:
        s->reg_tdectrl = val;
        break;
    case REG_BCN_PSR_RPT:
        s->reg_bcn_psr_rpt = val;
        break;
    case REG_TSFTR:
        s->reg_tsftr = val;
        break;
    case REG_NAV_CTRL:
        s->reg_nav_ctrl = val;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: Unimplemented write at offset 0x%" HWADDR_PRIx " val 0x%" PRIx64 "\n", __func__, addr, val);
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    qemu_log_mask(LOG_UNIMP, "%s: Unimplemented PIO read at offset 0x%" HWADDR_PRIx "\n", __func__, addr);
    
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    qemu_log_mask(LOG_UNIMP, "%s: Unimplemented PIO write at offset 0x%" HWADDR_PRIx " val 0x%" PRIx64 "\n", __func__, addr, val);
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

    s->sys_iso_ctrl = 0;
    s->sys_func_en = 0;
    s->sys_clk = 0;
    s->mac_himr = 0;
    s->mac_hsisr = 0;
    s->sec_cfg = 0;
    s->reg_16 = 0;
    s->irq_enabled = false;

    s->reg_rcr = 0;
    s->reg_hisre = 0;
    s->reg_macid = 0;
    s->reg_rrsr = 0;
    s->reg_inirts_rate_sel = 0;
    s->reg_bssid = 0;
    s->reg_sifs_ctx = 0;
    s->reg_sifs_trx = 0;
    s->reg_spec_sifs = 0;
    s->reg_mac_spec_sifs = 0;
    s->reg_resp_sifs_ofdm = 0;
    s->reg_slot = 0;
    s->reg_trxptcl_ctl = 0;
    s->reg_ampdu_min_space = 0;
    s->reg_agglen_lmt = 0;
    s->reg_acmhwctrl = 0;
    s->reg_rl = 0;
    s->reg_dual_tsf_rst = 0;
    s->reg_pcie_hrpwm = 0;
    s->reg_fwhw_txq_ctrl = 0;
    s->reg_tdectrl = 0;
    s->reg_bcn_psr_rpt = 0;
    s->reg_tsftr = 0;
    s->reg_nav_ctrl = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  RTL8188EE_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  RTL8188EE_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, RTL8188EE_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 2;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 256;
    s->bar_info[0].name = "rtl8188ee-pio";

    s->bar_info[1].index = 2;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = 16384;
    s->bar_info[1].name = "rtl8188ee-mmio";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->msi_support = true; /* Default from rtl88ee_mod_params */   
    if (s->msi_support) {
        msi_init(pdev, 0, 1, true, false, errp);
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
    .name = "rtl8188ee_pci",
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
