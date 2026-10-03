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

#define TYPE_PCIBASE_DEVICE "Marvell_CGX_RPM_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
/* === PCI IDs === */
#ifndef PCI_VENDOR_ID_CAVIUM
#define PCI_VENDOR_ID_CAVIUM 0x177d
#endif
#define PCI_DEVID_OCTEONTX2_CGX    0xA059

/* Subsystem device IDs */
#define PCI_SUBSYS_DEVID_CN10K_A   0xB900

/* === Bit operation macros (QEMU-style) === */
#define BIT_ULL(n)                  (1ULL << (n))
#define GENMASK_ULL(h, l)           (((1ULL << ((h) - (l) + 1)) - 1) << (l))

/* === CGX/RPM Register Offsets and Bitmasks === */
#define CGXX_CMRX_CFG               0x00
#define CGXX_CMR_GLOBAL_CONFIG      0x08
#define CGXX_CMRX_INT               0x040
#define CGXX_CMRX_INT_ENA_W1S       0x058
#define CGXX_CMRX_RX_STAT0          0x070
#define CGXX_CMRX_TX_STAT0          0x700
#define CGXX_CMRX_RX_DMAC_CTL0      0x1F8
#define CGXX_CMRX_RX_DMAC_CAM0      0x200
#define CGXX_CMRX_RX_LOGL_XON       0x100
#define CGXX_CMRX_RX_LMACS          0x128
#define RPM2_CMRX_RX_LMACS          0x100
#define CGXX_SCRATCH0_REG           0x1050
#define CGXX_SCRATCH1_REG           0x1058
#define CGXX_CMR_RX_OVR_BP          0x130
#define CGXX_CONST                  0x2000
#define CGXX_GMP_PCS_MRX_CTL        0x30000
#define CGXX_SPUX_CONTROL1          0x10000
#define CGXX_SPUX_RSFEC_CORR        0x10088
#define CGXX_SPUX_RSFEC_UNCORR      0x10090
#define CGXX_SPUX_LNX_FEC_CORR_BLOCKS  0x10700
#define CGXX_SPUX_LNX_FEC_UNCORR_BLOCKS 0x10800
#define CGXX_SMUX_RX_FRM_CTL        0x20020
#define CGXX_SMUX_TX_CTL            0x20178
#define CGXX_SMUX_SMAC              0x20108
#define CGXX_SMUX_TX_PAUSE_PKT_TIME 0x20110
#define CGXX_SMUX_TX_PAUSE_PKT_INTERVAL 0x20120
#define CGXX_SMUX_CBFC_CTL          0x20218
#define CGXX_GMP_GMI_RXX_FRM_CTL    0x38028
#define CGXX_GMP_GMI_TX_PAUSE_PKT_TIME      0x38230
#define CGXX_GMP_GMI_TX_PAUSE_PKT_INTERVAL  0x38248

/* Interrupt bits */
#define FW_CGX_INT                  BIT_ULL(1)

/* Global config bits */
#define CGX_NSCI_DROP               BIT_ULL(9)

/* Reset bits */
#define CGX_NIX1_RESET              BIT_ULL(3)
#define CGX_NIX0_RESET              BIT_ULL(2)

/* DMAC control and filter bits */
#define CGX_DMAC_BCAST_MODE         BIT_ULL(0)
#define CGX_DMAC_MCAST_MODE         BIT_ULL(1)
#define CGX_DMAC_MCAST_MODE_CAM     BIT_ULL(2)
#define CGX_DMAC_CAM_ACCEPT         BIT_ULL(3)
#define CGX_DMAC_CTL0_CAM_ENABLE    BIT_ULL(3)
#define CGX_DMAC_CAM_ADDR_ENABLE    BIT_ULL(48)
#define CGX_RX_DMAC_ADR_MASK        GENMASK_ULL(47, 0)

/* LMAC type extraction */
#define CGX_LMAC_TYPE_MASK          0xF
#define CGX_LMAC_TYPE_SHIFT         40

/* PCS loopback */
#define CGXX_GMP_PCS_MRX_CTL_LBK    BIT_ULL(14)
#define CGXX_SPUX_CONTROL1_LBK      BIT_ULL(14)

/* SMUX control bits */
#define CGX_SMUX_RX_FRM_CTL_CTL_BCK BIT_ULL(3)
#define CGX_SMUX_TX_CTL_L2P_BP_CONV BIT_ULL(7)
#define CGX_SMUX_RX_FRM_CTL_PTP_MODE  BIT_ULL(12)
#define CGXX_SMUX_CBFC_CTL_BCK_EN   BIT_ULL(3)
#define CGXX_SMUX_CBFC_CTL_RX_EN    BIT_ULL(0)
#define CGXX_SMUX_CBFC_CTL_TX_EN    BIT_ULL(1)
#define CGXX_SMUX_CBFC_CTL_DRP_EN   BIT_ULL(2)

/* GMP bits */
#define CGX_GMP_GMI_RXX_FRM_CTL_CTL_BCK BIT_ULL(3)
#define CGX_GMP_GMI_RXX_FRM_CTL_PTP_MODE BIT_ULL(12)

/* Backpressure bits */
#define CGX_CMR_RX_OVR_BP_BP(X)     BIT_ULL(((X) + 4))
#define CGX_CMR_RX_OVR_BP_EN(X)     BIT_ULL(((X) + 8))

/* Pause frame defaults */
#define DEFAULT_PAUSE_TIME           0x7FF

/* Packet enable bits */
#define DATA_PKT_RX_EN              BIT_ULL(54)
#define DATA_PKT_TX_EN              BIT_ULL(53)

/* PFC mask */
#define CGX_PFC_CLASS_MASK          GENMASK_ULL(47, 32)

/* Command register definitions */
#define CGX_COMMAND_REG             CGXX_SCRATCH1_REG
#define CMDREG_ID                   GENMASK_ULL(7, 2)
#define CMDREG_OWN                  BIT_ULL(0)
#define CGX_CMD_TIMEOUT             5000

/* Event register definitions */
#define CGX_EVENT_REG               CGXX_SCRATCH0_REG
#define EVTREG_STAT                 BIT_ULL(2)
#define EVTREG_ID                   GENMASK_ULL(8, 3)
#define EVTREG_EVT_TYPE             BIT_ULL(1)
#define EVTREG_ACK                  BIT_ULL(0)

/* Response bitmasks */
#define RESP_LINKSTAT_FDUPLEX       GENMASK_ULL(10, 10)
#define RESP_LINKSTAT_AN            GENMASK_ULL(25, 25)
#define RESP_LINKSTAT_LMAC_TYPE     GENMASK_ULL(35, 28)
#define RESP_LINKSTAT_FEC           GENMASK_ULL(27, 26)
#define RESP_LINKSTAT_SPEED         GENMASK_ULL(14, 11)
#define RESP_LINKSTAT_UP            GENMASK_ULL(9, 9)
#define RESP_LINKSTAT_ERRTYPE       GENMASK_ULL(24, 15)
#define RESP_FWD_BASE               GENMASK_ULL(56, 9)

/* Mode change command fields */
#define CMDMODECHANGE_SPEED         GENMASK_ULL(11, 8)
#define CMDMODECHANGE_DUPLEX        GENMASK_ULL(12, 12)
#define CMDMODECHANGE_MODE_BASEIDX  GENMASK_ULL(21, 20)
#define CMDMODECHANGE_AN            GENMASK_ULL(13, 13)
#define CMDMODECHANGE_FLAGS         GENMASK_ULL(63, 22)

/* FEC command fields */
#define CMDSETFEC                   GENMASK_ULL(9, 8)

/* Link config timeout */
#define LINKCFG_TIMEOUT             GENMASK_ULL(21, 8)

/* Firmware version fields */
#define RESP_MAJOR_VER              GENMASK_ULL(12, 9)
#define RESP_MINOR_VER              GENMASK_ULL(16, 13)
#define CGX_FIRMWARE_MAJOR_VER      1

/* Features */
#define RVU_LMAC_FEAT_HIGIG2        BIT_ULL(1)
#define RVU_LMAC_FEAT_PTP           BIT_ULL(2)
#define RVU_LMAC_FEAT_FC            BIT_ULL(0)
#define RVU_LMAC_FEAT_DMACF         BIT_ULL(3)
#define RVU_MAC_RPM                 BIT_ULL(6)

/* CONST register fields */
#define CGX_CONST_RXFIFO_SIZE       GENMASK_ULL(55, 32)
#define CGX_CONST_MAX_LMACS         GENMASK_ULL(31, 24)

/* Misc */
#define CGX_ID_MASK                 0xF
#define MAX_LMAC_COUNT              8
#define LMACTYPE_STR_LEN            16
#define PCI_CFG_REG_BAR_NUM         0

/* Additional local defines */
#define MAX_LMACS                   4
#define CGX_CMD_OWN_NS              0
#define CGX_CMD_OWN_FIRMWARE        1
#define CGX_EVT_CMD_RESP            2
#define CGX_CMD_GET_FW_VER          1
#define CGX_FW_RESP_MAJOR_VER       1
#define CGX_FW_RESP_SUCCESS         (BIT_ULL(0) | BIT_ULL(1) | (CGX_FW_RESP_MAJOR_VER << 9) | BIT_ULL(2))
#define CGX_DEFAULT_FIFO_LEN        0x10000
#define CGX_DEFAULT_MAX_LMACS       1

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

typedef struct {
    uint64_t cfg;
    uint64_t global_config;
    uint64_t int_status;
    uint64_t int_ena;
    uint64_t rx_stats[18];
    uint64_t tx_stats[18];
    uint64_t rx_dmac_ctl0;
    uint64_t rx_dmac_cam[32];
    uint64_t rx_logl_xon;
    uint64_t rx_lmacs;
    uint64_t event_reg;
    uint64_t command_reg;
    uint64_t cmr_rx_ovr_bp;
    uint64_t cgx_const;
    uint64_t gmp_pcs_mrx_ctl;
    uint64_t spux_control1;
    uint64_t spux_rsfec_corr;
    uint64_t spux_rsfec_uncorr;
    uint64_t spux_lnx_fec_corr_blocks;
    uint64_t spux_lnx_fec_uncorr_blocks;
    uint64_t smux_rx_frm_ctl;
    uint64_t smux_tx_ctl;
    uint64_t smux_smac;
    uint64_t smux_tx_pause_pkt_time;
    uint64_t smux_tx_pause_pkt_interval;
    uint64_t smux_cbfc_ctl;
    uint64_t gmp_gmi_rxx_frm_ctl;
    uint64_t gmp_gmi_tx_pause_pkt_time;
    uint64_t gmp_gmi_tx_pause_pkt_interval;
} LmacState;

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];
    MemoryRegion bar0_regs;
    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;

    LmacState lmac[MAX_LMACS];
    int max_lmacs;
    unsigned int msix_entries;
};

G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s, int lmac_idx)
{
    if (lmac_idx >= s->max_lmacs) {
        return;
    }
    LmacState *lmac = &s->lmac[lmac_idx];
    PCIDevice *pdev = PCI_DEVICE(s);
    bool active = (lmac->int_status & lmac->int_ena) & FW_CGX_INT;
    if (active) {
        if (msix_enabled(pdev)) {
            msix_notify(pdev, lmac_idx);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (msix_enabled(pdev)) {
        } else {
            pci_set_irq(pdev, 0);
        }
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    int lmac_idx = (addr >> 18) & 0x7;
    hwaddr offset = addr & 0x3FFFF;

    if (lmac_idx >= s->max_lmacs) {
        return 0;
    }
    LmacState *lmac = &s->lmac[lmac_idx];

    switch (offset) {
    case 0x00:      val = lmac->cfg; break;
    case 0x08:      val = lmac->global_config; break;
    case 0x40:      val = lmac->int_status; break;
    case 0x58:      val = lmac->int_ena; break;
    case 0x70 ... 0xF8: {
        int idx = (offset - 0x70) / 8;
        if (idx < 18) val = lmac->rx_stats[idx];
        break;
    }
    case 0x700 ... 0x788: {
        int idx = (offset - 0x700) / 8;
        if (idx < 18) val = lmac->tx_stats[idx];
        break;
    }
    case 0x1F8:     val = lmac->rx_dmac_ctl0; break;
    case 0x200 ... 0x2F8: {
        int idx = (offset - 0x200) / 8;
        if (idx < 32) val = lmac->rx_dmac_cam[idx];
        break;
    }
    case 0x100:     val = lmac->rx_logl_xon; break;
    case 0x128:     val = lmac->rx_lmacs; break;
    case 0x1050:    val = lmac->event_reg; break;
    case 0x1058:    val = lmac->command_reg; break;
    case 0x130:     val = lmac->cmr_rx_ovr_bp; break;
    case 0x2000:    val = lmac->cgx_const; break;
    case 0x30000:   val = lmac->gmp_pcs_mrx_ctl; break;
    case 0x10000:   val = lmac->spux_control1; break;
    case 0x10088:   val = lmac->spux_rsfec_corr; break;
    case 0x10090:   val = lmac->spux_rsfec_uncorr; break;
    case 0x10700:   val = lmac->spux_lnx_fec_corr_blocks; break;
    case 0x10800:   val = lmac->spux_lnx_fec_uncorr_blocks; break;
    case 0x20020:   val = lmac->smux_rx_frm_ctl; break;
    case 0x20178:   val = lmac->smux_tx_ctl; break;
    case 0x20108:   val = lmac->smux_smac; break;
    case 0x20110:   val = lmac->smux_tx_pause_pkt_time; break;
    case 0x20120:   val = lmac->smux_tx_pause_pkt_interval; break;
    case 0x20218:   val = lmac->smux_cbfc_ctl; break;
    case 0x38028:   val = lmac->gmp_gmi_rxx_frm_ctl; break;
    case 0x38230:   val = lmac->gmp_gmi_tx_pause_pkt_time; break;
    case 0x38248:   val = lmac->gmp_gmi_tx_pause_pkt_interval; break;
    default:        val = 0; break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    int lmac_idx = (addr >> 18) & 0x7;
    hwaddr offset = addr & 0x3FFFF;

    if (lmac_idx >= s->max_lmacs) {
        return;
    }
    LmacState *lmac = &s->lmac[lmac_idx];

    switch (offset) {
    case 0x00:      lmac->cfg = val; break;
    case 0x08:      lmac->global_config = val; break;
    case 0x40:
        if (val & FW_CGX_INT) {
            lmac->int_status &= ~FW_CGX_INT;
            pcibase_update_irq(s, lmac_idx);
        }
        break;
    case 0x58:
        if (val & FW_CGX_INT) {
            lmac->int_ena |= FW_CGX_INT;
        }
        break;
    case 0x70 ... 0xF8: {
        int idx = (offset - 0x70) / 8;
        if (idx < 18) lmac->rx_stats[idx] = val;
        break;
    }
    case 0x700 ... 0x788: {
        int idx = (offset - 0x700) / 8;
        if (idx < 18) lmac->tx_stats[idx] = val;
        break;
    }
    case 0x1F8:     lmac->rx_dmac_ctl0 = val; break;
    case 0x200 ... 0x2F8: {
        int idx = (offset - 0x200) / 8;
        if (idx < 32) lmac->rx_dmac_cam[idx] = val;
        break;
    }
    case 0x100:     lmac->rx_logl_xon = val; break;
    case 0x128:     lmac->rx_lmacs = val; break;
    case 0x1050:    lmac->event_reg = val; break;
    case 0x1058:
        lmac->command_reg = val;
        if (val & BIT_ULL(0)) {
            lmac->event_reg = CGX_FW_RESP_SUCCESS;
            lmac->int_status |= FW_CGX_INT;
            pcibase_update_irq(s, lmac_idx);
        }
        break;
    case 0x130:     lmac->cmr_rx_ovr_bp = val; break;
    case 0x2000:    lmac->cgx_const = val; break;
    case 0x30000:   lmac->gmp_pcs_mrx_ctl = val; break;
    case 0x10000:   lmac->spux_control1 = val; break;
    case 0x10088:   lmac->spux_rsfec_corr = val; break;
    case 0x10090:   lmac->spux_rsfec_uncorr = val; break;
    case 0x10700:   lmac->spux_lnx_fec_corr_blocks = val; break;
    case 0x10800:   lmac->spux_lnx_fec_uncorr_blocks = val; break;
    case 0x20020:   lmac->smux_rx_frm_ctl = val; break;
    case 0x20178:   lmac->smux_tx_ctl = val; break;
    case 0x20108:   lmac->smux_smac = val; break;
    case 0x20110:   lmac->smux_tx_pause_pkt_time = val; break;
    case 0x20120:   lmac->smux_tx_pause_pkt_interval = val; break;
    case 0x20218:   lmac->smux_cbfc_ctl = val; break;
    case 0x38028:   lmac->gmp_gmi_rxx_frm_ctl = val; break;
    case 0x38230:   lmac->gmp_gmi_tx_pause_pkt_time = val; break;
    case 0x38248:   lmac->gmp_gmi_tx_pause_pkt_interval = val; break;
    default: break;
    }
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
    memset(s->lmac, 0, sizeof(s->lmac));
    s->max_lmacs = CGX_DEFAULT_MAX_LMACS;
    s->lmac[0].rx_lmacs = 1;
    s->lmac[0].cgx_const = ((uint64_t)CGX_DEFAULT_FIFO_LEN << 32) |
                           ((uint64_t)CGX_DEFAULT_MAX_LMACS << 24);
    s->lmac[0].cfg = 0;
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }
    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr;

    if (bi->type == BAR_TYPE_MMIO) {
        if (bi->index == 0) {
            /* BAR0 uses a container with subregions for registers and MSI-X */
            memory_region_init(&s->bar_regions[0], OBJECT(s), "bar0", aligned_size);
            memory_region_init_io(&s->bar0_regs, OBJECT(s), &pcibase_mmio_ops, s,
                                  "bar0-regs", 0x40000);
            memory_region_add_subregion(&s->bar_regions[0], 0, &s->bar0_regs);
            pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_regions[0]);
        } else {
            memory_region_init_io(&s->bar_regions[bi->index], OBJECT(s),
                                  &pcibase_mmio_ops, s, bi->name, aligned_size);
            pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY,
                             &s->bar_regions[bi->index]);
        }
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(&s->bar_regions[bi->index], OBJECT(s),
                              &pcibase_pio_ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO,
                         &s->bar_regions[bi->index]);
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(&s->bar_regions[bi->index], OBJECT(s),
                               bi->name, aligned_size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY,
                         &s->bar_regions[bi->index]);
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_CAVIUM);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVID_OCTEONTX2_CGX);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_ETHERNET);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pci_set_word(pci_conf + PCI_SUBSYSTEM_VENDOR_ID, PCI_VENDOR_ID_CAVIUM);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_ID, PCI_SUBSYS_DEVID_CN10K_A);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);

    /* BAR0: MMIO registers + MSI-X inside a single BAR */
    s->num_bars = 0;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x200000; /* 2 MB */
    s->bar_info[0].name = "bar0";
    s->num_bars++;

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Place MSI-X inside BAR0 at offset 0x100000 (1MB) */
    int ret = msix_init(pdev, MAX_LMACS,
                        &s->bar_regions[0], 0, 0x100000,
                        &s->bar_regions[0], 0, 0x100800,
                        0, errp);
    if (ret < 0) {
        error_setg(errp, "msix_init failed");
        return;
    }

    pcibase_reset(DEVICE(s));
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    msix_uninit(pdev, &s->bar_regions[0], &s->bar_regions[0]);
}

static const VMStateDescription vmstate_pcibase = {
    .name = "Marvell_CGX_RPM_pci",
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

type_init(pcibase_register_types)
