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

#define TYPE_PCIBASE_DEVICE "rtl8723be_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define VENDOR_ID 0x10EC
#define DEVICE_ID 0xB723
#define CLASS_ID  0x0280

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

struct tx_desc_8723be {
    uint32_t pktsize:16;
    uint32_t offset:8;
    uint32_t bmc:1;
    uint32_t htc:1;
    uint32_t lastseg:1;
    uint32_t firstseg:1;
    uint32_t linip:1;
    uint32_t noacm:1;
    uint32_t gf:1;
    uint32_t own:1;

    uint32_t macid:6;
    uint32_t rsvd0:2;
    uint32_t queuesel:5;
    uint32_t rd_nav_ext:1;
    uint32_t lsig_txop_en:1;
    uint32_t pifs:1;
    uint32_t rateid:4;
    uint32_t nav_usehdr:1;
    uint32_t en_descid:1;
    uint32_t sectype:2;
    uint32_t pktoffset:8;

    uint32_t rts_rc:6;
    uint32_t data_rc:6;
    uint32_t agg_en:1;
    uint32_t rdg_en:1;
    uint32_t bar_retryht:2;
    uint32_t agg_break:1;
    uint32_t morefrag:1;
    uint32_t raw:1;
    uint32_t ccx:1;
    uint32_t ampdudensity:3;
    uint32_t bt_int:1;
    uint32_t ant_sela:1;
    uint32_t ant_selb:1;
    uint32_t txant_cck:2;
    uint32_t txant_l:2;
    uint32_t txant_ht:2;

    uint32_t nextheadpage:8;
    uint32_t tailpage:8;
    uint32_t seq:12;
    uint32_t cpu_handle:1;
    uint32_t tag1:1;
    uint32_t trigger_int:1;
    uint32_t hwseq_en:1;

    uint32_t rtsrate:5;
    uint32_t apdcfe:1;
    uint32_t qos:1;
    uint32_t hwseq_ssn:1;
    uint32_t userrate:1;
    uint32_t dis_rtsfb:1;
    uint32_t dis_datafb:1;
    uint32_t cts2self:1;
    uint32_t rts_en:1;
    uint32_t hwrts_en:1;
    uint32_t portid:1;
    uint32_t pwr_status:3;
    uint32_t waitdcts:1;
    uint32_t cts2ap_en:1;
    uint32_t txsc:2;
    uint32_t stbc:2;
    uint32_t txshort:1;
    uint32_t txbw:1;
    uint32_t rtsshort:1;
    uint32_t rtsbw:1;
    uint32_t rtssc:2;
    uint32_t rtsstbc:2;

    uint32_t txrate:6;
    uint32_t shortgi:1;
    uint32_t ccxt:1;
    uint32_t txrate_fb_lmt:5;
    uint32_t rtsrate_fb_lmt:4;
    uint32_t retrylmt_en:1;
    uint32_t txretrylmt:6;
    uint32_t usb_txaggnum:8;

    uint32_t txagca:5;
    uint32_t txagcb:5;
    uint32_t usemaxlen:1;
    uint32_t maxaggnum:5;
    uint32_t mcsg1maxlen:4;
    uint32_t mcsg2maxlen:4;
    uint32_t mcsg3maxlen:4;
    uint32_t mcs7sgimaxlen:4;

    uint32_t txbuffersize:16;
    uint32_t sw_offset30:8;
    uint32_t sw_offset31:4;
    uint32_t rsvd1:1;
    uint32_t antsel_c:1;
    uint32_t null_0:1;
    uint32_t null_1:1;

    uint32_t txbuffaddr;
    uint32_t txbufferaddr64;
    uint32_t nextdescaddress;
    uint32_t nextdescaddress64;

    uint32_t reserve_pass_pcie_mm_limit[4];
};

struct rx_fwinfo_8723be {
    uint8_t gain_trsw[2];
    uint16_t chl_num:10;
    uint16_t sub_chnl:4;
    uint16_t r_rfmod:2;
    uint8_t pwdb_all;
    uint8_t cfosho[4];
    uint8_t cfotail[4];
    int8_t rxevm[2];
    int8_t rxsnr[2];
    uint8_t pcts_msk_rpt[2];
    uint8_t pdsnr[2];
    uint8_t csi_current[2];
    uint8_t rx_gain_c;
    uint8_t rx_gain_d;
    uint8_t sigevm;
    uint8_t resvd_0;
    uint8_t antidx_anta:3;
    uint8_t antidx_antb:3;
    uint8_t resvd_1:2;
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
    uint32_t irq_mask[4];
    uint32_t sys_irq_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t reg_trxptcl_ctl;
    uint32_t reg_rxdma_control;
    uint32_t reg_nav_upper;
    uint32_t reg_sys_cfg1;
    uint32_t reg_sys_iso_ctrl;
    uint32_t reg_sys_func_en;
    uint32_t reg_sys_clkr;
    uint32_t reg_himr;
    uint32_t reg_himre;
    uint32_t reg_hsisr;
    uint32_t reg_efuse_access;
    uint32_t reg_camcmd;
    uint32_t reg_seccfg;
    
    /* New Registers from Iteration */
    uint32_t reg_9346cr;
    uint32_t reg_hisre;
    uint32_t reg_cr;
    uint32_t reg_sys_cfg;
    uint32_t reg_rcr;
    uint32_t reg_pcie_ctrl_reg;
    uint32_t reg_hsimr;

    /* DMA Context */
    uint64_t tx_ring_dma[9];
    uint64_t rx_ring_dma[2];

    bool hwradiooff;
    bool first_init;
    bool support_aspm;
    bool msi_support;
};

#define REG_TRXPTCL_CTL 0x0668
#define REG_RXDMA_CONTROL 0x0286
#define REG_NAV_UPPER 0x0652
#define REG_SYS_CFG1 0x00FC
#define DM_REG_OFDM_FA_RSTC_11N 0xC0C
#define DM_REG_OFDM_FA_RSTD_11N 0xD00
#define DM_REG_OFDM_FA_TYPE4_11N 0xDA8
#define DM_REG_CCK_FA_MSB_11N 0xA58
#define DM_REG_OFDM_FA_TYPE3_11N 0xDA4
#define DM_REG_OFDM_FA_TYPE2_11N 0xDA0
#define DM_REG_OFDM_FA_HOLDC_11N 0xC00
#define DM_REG_CCK_CCA_CNT_11N 0xA60
#define DM_REG_OFDM_FA_TYPE1_11N 0xCF0
#define DM_REG_CCK_FA_RST_11N 0xA2C
#define REG_SYS_ISO_CTRL 0x0000
#define REG_SYS_FUNC_EN 0x0002
#define REG_SYS_CLKR 0x0008
#define REG_HIMR 0x0120
#define REG_HIMRE 0x0128
#define REG_HSISR 0x005c
#define REG_EFUSE_ACCESS 0x00CF
#define REG_CAMCMD 0x0670
#define REG_SECCFG 0x0680

/* New Registers */
#define REG_9346CR 0x000A
#define REG_HISRE 0x012C
#define REG_CR 0x00
#define REG_SYS_CFG 0x00F0
#define REG_RCR 0x0608
#define REG_PCIE_CTRL_REG 0x0300
#define REG_HSIMR 0x0058
#define REG_EDCA_BK_PARAM 0x050C
#define REG_EDCA_VI_PARAM 0x0504
#define REG_EDCA_VO_PARAM 0x0500
#define REG_ATIMWND 0x055A
#define REG_BCN_INTERVAL 0x0554
#define REG_BCNTCFG 0x0510
#define REG_RXTSF_OFFSET_CCK 0x055E
#define REG_RXTSF_OFFSET_OFDM 0x055F
#define REG_TSFTR 0x0560
#define REG_MACID 0x0610
#define REG_RRSR 0x0440
#define REG_INIRTS_RATE_SEL 0x0480
#define REG_BSSID 0x0618
#define REG_SIFS_CTX 0x0514
#define REG_SIFS_TRX 0x0516
#define REG_SPEC_SIFS 0x0428
#define REG_MAC_SPEC_SIFS 0x063A
#define REG_RESP_SIFS_OFDM 0x063E
#define REG_SLOT 0x051B
#define REG_AMPDU_MIN_SPACE 0x045C
#define REG_AGGLEN_LMT 0x0458
#define REG_ACMHWCTRL 0x05C0
#define REG_RL 0x042A
#define REG_DUAL_TSF_RST 0x0553
#define REG_PCIE_HRPWM 0x0361
#define REG_BCN_PSR_RPT 0x06A8
#define REG_GPIO_IO_SEL_2 0x0062
#define REG_GPIO_PIN_CTRL_2 0x0060
#define IMR_DISABLED 0x0

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    /* Stub until ISR is known */
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
    case REG_CR:
        val = s->reg_cr;
        break;
    case REG_9346CR:
        val = s->reg_9346cr;
        break;
    case REG_SYS_CFG:
        val = s->reg_sys_cfg;
        break;
    case REG_PCIE_CTRL_REG:
        val = s->reg_pcie_ctrl_reg;
        break;
    case REG_HSIMR:
        val = s->reg_hsimr;
        break;
    case REG_RCR:
        val = s->reg_rcr;
        break;
    case REG_RXDMA_CONTROL:
        val = s->reg_rxdma_control;
        break;
    case REG_NAV_UPPER:
        val = s->reg_nav_upper;
        break;
    case REG_HIMR:
        val = s->reg_himr;
        break;
    case REG_HIMRE:
        val = s->reg_himre;
        break;
    case REG_HISRE:
        val = s->reg_hisre;
        break;
    case REG_SECCFG:
        val = s->reg_seccfg;
        break;
    case REG_TRXPTCL_CTL ... REG_TRXPTCL_CTL + 3:
        val = (s->reg_trxptcl_ctl >> ((addr - REG_TRXPTCL_CTL) * 8)) & ((1ULL << (size * 8)) - 1);
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
    case REG_CR:
        s->reg_cr = val;
        break;
    case REG_9346CR:
        s->reg_9346cr = val;
        break;
    case REG_SYS_CFG:
        s->reg_sys_cfg = val;
        break;
    case REG_PCIE_CTRL_REG:
        s->reg_pcie_ctrl_reg = val;
        break;
    case REG_HSIMR:
        s->reg_hsimr = val;
        break;
    case REG_RCR:
        s->reg_rcr = val;
        break;
    case REG_RXDMA_CONTROL:
        s->reg_rxdma_control = val;
        break;
    case REG_NAV_UPPER:
        s->reg_nav_upper = val;
        break;
    case REG_HIMR:
        s->reg_himr = val;
        pcibase_update_irq(s);
        break;
    case REG_HIMRE:
        s->reg_himre = val;
        pcibase_update_irq(s);
        break;
    case REG_HISRE:
        s->reg_hisre = val;
        pcibase_update_irq(s);
        break;
    case REG_SECCFG:
        s->reg_seccfg = val;
        break;
    case REG_TRXPTCL_CTL ... REG_TRXPTCL_CTL + 3:
        {
            uint32_t shift = (addr - REG_TRXPTCL_CTL) * 8;
            uint32_t mask = ((1ULL << (size * 8)) - 1) << shift;
            s->reg_trxptcl_ctl = (s->reg_trxptcl_ctl & ~mask) | ((val << shift) & mask);
        }
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
    
    s->reg_trxptcl_ctl = 0;
    s->reg_rxdma_control = 0;
    s->reg_nav_upper = 0;
    s->reg_himr = 0;
    s->reg_himre = 0;
    s->reg_seccfg = 0;
    
    s->reg_9346cr = 0;
    s->reg_hisre = 0;
    s->reg_cr = 0;
    s->reg_sys_cfg = 0;
    s->reg_rcr = 0;
    s->reg_pcie_ctrl_reg = 0;
    s->reg_hsimr = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    
    /* Place PM capability at 0x40 to match driver's hardcoded write to 0x44 (PMCSR) */
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0x40, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 2;
    
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 256;
    s->bar_info[0].name = "rtl8723be-pio";

    s->bar_info[1].index = 2;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = 16384;
    s->bar_info[1].name = "rtl8723be-mmio";
  
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = true;
    if (s->has_msi) {
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
    .name = "rtl8723be_pci",
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
