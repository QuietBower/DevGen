/*
 * QEMU PCI device model for Synopsys DWC XLGMAC PCI
 * Phase 2: Functional behavior for basic driver bring-up.
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
/* Removed placeholder include that caused compile issues */

#define TYPE_PCIBASE_DEVICE "dwc_xlgmac_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define XLGMAC_DRV_NAME             "dwc-xlgmac"
#define XLGMAC_MAX_DMA_CHANNELS     16
#define MAC_VR                      0x0110
#define MAC_HWF0R                   0x011c
#define MAC_HWF1R                   0x0120
#define MAC_HWF2R                   0x0124
#define MAC_TCR                     0x0000
#define MAC_RCR                     0x0004
#define MAC_PFR                     0x0008
#define MAC_VLANTR                  0x0050
#define MAC_VLANHTR                 0x0058
#define MAC_VLANIR                  0x0060
#define MAC_Q0TFCR                  0x0070
#define MAC_RFCR                    0x0090
#define MAC_RQC0R                   0x00a0
#define MAC_RQC2R                   0x00a8
#define MAC_IER                     0x00b4
#define MAC_HTR0                    0x0010
#define MAC_MACA1HR                 0x0308
#define MAC_MACA_INC                4
#define MAC_HTR_INC                 4
#define MAC_QTFCR_INC               4
#define MAC_RSSCR                   0x0c80
#define MAC_RSSAR                   0x0c88
#define MAC_RSSDR                   0x0c8c
#define MAC_VLANTR_VL_POS           0
#define MAC_VLANTR_VL_LEN           16
#define MAC_VLANTR_VTIM_POS         17
#define MAC_VLANTR_VTIM_LEN         1
#define MAC_VLANTR_ETV_POS          16
#define MAC_VLANTR_ETV_LEN          1
#define MAC_VLANTR_VTHM_POS         25
#define MAC_VLANTR_VTHM_LEN         1
#define MAC_VLANTR_EVLS_POS         21
#define MAC_VLANTR_EVLS_LEN         2
#define MAC_VLANTR_DOVLTC_POS       20
#define MAC_VLANTR_DOVLTC_LEN       1
#define MAC_VLANTR_ESVL_POS         18
#define MAC_VLANTR_ESVL_LEN         1
#define MAC_VLANTR_ERSVLM_POS       19
#define MAC_VLANTR_ERSVLM_LEN       1
#define MAC_VLANTR_EVLRXS_POS       24
#define MAC_VLANTR_EVLRXS_LEN       1
#define MAC_PFR_PM_POS              4
#define MAC_PFR_PM_LEN              1
#define MAC_PFR_PR_POS              0
#define MAC_PFR_PR_LEN              1
#define MAC_PFR_VTFE_POS            16
#define MAC_PFR_VTFE_LEN            1
#define MAC_PFR_HMC_POS             2
#define MAC_PFR_HMC_LEN             1
#define MAC_PFR_HUC_POS             1
#define MAC_PFR_HUC_LEN             1
#define MAC_PFR_HPF_POS             10
#define MAC_PFR_HPF_LEN             1
#define MAC_RCR_RE_POS              0
#define MAC_RCR_RE_LEN              1
#define MAC_RCR_ACS_POS             1
#define MAC_RCR_ACS_LEN             1
#define MAC_RCR_CST_POS             2
#define MAC_RCR_CST_LEN             1
#define MAC_RCR_DCRCC_POS           3
#define MAC_RCR_DCRCC_LEN           1
#define MAC_RCR_JE_POS              8
#define MAC_RCR_JE_LEN              1
#define MAC_RCR_IPC_POS             9
#define MAC_RCR_IPC_LEN             1
#define MAC_RCR_HDSMS_POS           12
#define MAC_RCR_HDSMS_LEN           3
#define MAC_TCR_SS_POS              28
#define MAC_TCR_SS_LEN              3
#define MAC_TCR_TE_POS              0
#define MAC_TCR_TE_LEN              1
#define MAC_IER_TSIE_POS            12
#define MAC_IER_TSIE_LEN            1
#define DMA_MR                      0x3000
#define DMA_MR_SWR_POS              0
#define DMA_MR_SWR_LEN              1
#define DMA_SBMR                    0x3004
#define DMA_SBMR_EAME_POS           11
#define DMA_SBMR_EAME_LEN           1
#define DMA_SBMR_BLEN_256_POS       7
#define DMA_SBMR_BLEN_256_LEN       1
#define DMA_SBMR_UNDEF_POS          0
#define DMA_SBMR_UNDEF_LEN          1
#define DMA_DSR0                    0x3020
#define DMA_DSR1                    0x3024
#define DMA_DSR_RPS_LEN             4
#define DMA_DSR_TPS_LEN             4
#define DMA_DSR0_TPS_START          12
#define DMA_DSRX_FIRST_QUEUE        3
#define DMA_DSRX_TPS_START          4
#define DMA_DSRX_QPR                4
#define DMA_DSRX_INC                4
#define DMA_TPS_STOPPED             0x00
#define DMA_TPS_SUSPENDED           0x06
#define DMA_CH_BASE                 0x3100
#define DMA_CH_INC                  0x80
#define DMA_CH_CR                   0x00
#define DMA_CH_TCR                  0x04
#define DMA_CH_RCR                  0x08
#define DMA_CH_TDLR_HI              0x10
#define DMA_CH_TDLR_LO              0x14
#define DMA_CH_RDLR_HI              0x18
#define DMA_CH_RDLR_LO              0x1c
#define DMA_CH_TDTR_LO              0x24
#define DMA_CH_RDTR_LO              0x2c
#define DMA_CH_TDRLR                0x30
#define DMA_CH_RDRLR                0x34
#define DMA_CH_IER                  0x38
#define DMA_CH_RIWT                 0x3c
#define DMA_CH_SR                   0x60
#define DMA_CH_CR_SPH_POS           24
#define DMA_CH_CR_SPH_LEN           1
#define DMA_CH_TCR_ST_POS           0
#define DMA_CH_TCR_ST_LEN           1
#define DMA_CH_TCR_TSE_POS          12
#define DMA_CH_TCR_TSE_LEN          1
#define DMA_CH_TCR_OSP_POS          4
#define DMA_CH_TCR_OSP_LEN          1
#define DMA_CH_TCR_PBL_POS          16
#define DMA_CH_TCR_PBL_LEN          6
#define DMA_CH_RCR_SR_POS           0
#define DMA_CH_RCR_SR_LEN           1
#define DMA_CH_RCR_RBSZ_POS         1
#define DMA_CH_RCR_RBSZ_LEN         14
#define DMA_CH_RCR_PBL_POS          16
#define DMA_CH_RCR_PBL_LEN          6
#define DMA_CH_CR_PBLX8_POS         16
#define DMA_CH_CR_PBLX8_LEN         1
#define DMA_CH_IER_TIE_POS          0
#define DMA_CH_IER_TIE_LEN          1
#define DMA_CH_IER_TXSE_POS         1
#define DMA_CH_IER_TXSE_LEN         1
#define DMA_CH_IER_TBUE_POS         2
#define DMA_CH_IER_TBUE_LEN         1
#define DMA_CH_IER_RIE_POS          6
#define DMA_CH_IER_RIE_LEN          1
#define DMA_CH_IER_RBUE_POS         7
#define DMA_CH_IER_RBUE_LEN         1
#define DMA_CH_IER_RSE_POS          8
#define DMA_CH_IER_RSE_LEN          1
#define DMA_CH_IER_FBEE_POS         12
#define DMA_CH_IER_FBEE_LEN         1
#define DMA_CH_IER_AIE_POS          15
#define DMA_CH_IER_AIE_LEN          1
#define DMA_CH_IER_NIE_POS          16
#define DMA_CH_IER_NIE_LEN          1
#define DMA_CH_RIWT_RWT_POS         0
#define DMA_CH_RIWT_RWT_LEN         8
#define XLGMAC_DMA_INTERRUPT_MASK   0x31c7
#define MTL_Q_BASE                  0x1100
#define MTL_Q_INC                   0x80
#define MTL_OMR                     0x1000
#define MTL_OMR_RAA_POS             2
#define MTL_OMR_RAA_LEN             1
#define MTL_OMR_ETSALG_POS          5
#define MTL_OMR_ETSALG_LEN          2
#define MTL_ETSALG_WRR              0x00
#define MTL_RAA_SP                  0x00
#define MTL_TC_ETSCR                0x10
#define MTL_TC_ETSCR_TSA_POS        0
#define MTL_TC_ETSCR_TSA_LEN        2
#define MTL_TSA_ETS                 0x02
#define MTL_TC_QWR                  0x18
#define MTL_TC_QWR_QW_POS           0
#define MTL_TC_QWR_QW_LEN           21
#define MTL_Q_TQOMR                 0x00
#define MTL_Q_TQOMR_FTQ_POS         0
#define MTL_Q_TQOMR_FTQ_LEN         1
#define MTL_Q_TQOMR_TSF_POS         1
#define MTL_Q_TQOMR_TSF_LEN         1
#define MTL_Q_TQOMR_TXQEN_POS       2
#define MTL_Q_TQOMR_TXQEN_LEN       2
#define MTL_Q_TQOMR_TTC_POS         4
#define MTL_Q_TQOMR_TTC_LEN         3
#define MTL_Q_TQOMR_TQS_POS         16
#define MTL_Q_TQOMR_TQS_LEN         10
#define MTL_Q_TQOMR_Q2TCMAP_POS     8
#define MTL_Q_TQOMR_Q2TCMAP_LEN     3
#define MTL_Q_ENABLED               0x02
#define MTL_Q_RQOMR                 0x40
#define MTL_Q_RQOMR_RTC_POS         0
#define MTL_Q_RQOMR_RTC_LEN         2
#define MTL_Q_RQOMR_FUP_POS         3
#define MTL_Q_RQOMR_FUP_LEN         1
#define MTL_Q_RQOMR_FEP_POS         4
#define MTL_Q_RQOMR_FEP_LEN         1
#define MTL_Q_RQOMR_RSF_POS         5
#define MTL_Q_RQOMR_RSF_LEN         1
#define MTL_Q_RQOMR_EHFC_POS        7
#define MTL_Q_RQOMR_EHFC_LEN        1
#define MTL_Q_RQOMR_RQS_POS         16
#define MTL_Q_RQOMR_RQS_LEN         9
#define MTL_Q_RQDR                  0x48
#define MTL_Q_RQDR_PRXQ_POS         16
#define MTL_Q_RQDR_PRXQ_LEN         14
#define MTL_Q_RQDR_RXQSTS_POS       4
#define MTL_Q_RQDR_RXQSTS_LEN       2
#define MTL_Q_RQFCR                 0x50
#define MTL_Q_RQFCR_RFA_POS         1
#define MTL_Q_RQFCR_RFA_LEN         6
#define MTL_Q_RQFCR_RFD_POS         17
#define MTL_Q_RQFCR_RFD_LEN         6
#define MTL_Q_ISR                   0x74
#define MTL_Q_IER                   0x70
#define MTL_RQDCM0R                 0x1030
#define MTL_RQDCM_INC               4
#define MTL_RQDCM0R_Q0MDMACH        0x0
#define MTL_RQDCM0R_Q1MDMACH        0x00000100
#define MTL_RQDCM0R_Q2MDMACH        0x00020000
#define MTL_RQDCM0R_Q3MDMACH        0x03000000
#define MTL_RQDCM1R_Q4MDMACH        0x00000004
#define MTL_RQDCM1R_Q5MDMACH        0x00000500
#define MTL_RQDCM1R_Q6MDMACH        0x00060000
#define MTL_RQDCM1R_Q7MDMACH        0x07000000
#define MTL_RQDCM2R_Q8MDMACH        0x00000008
#define MTL_RQDCM2R_Q9MDMACH        0x00000900
#define MTL_RQDCM2R_Q10MDMACH       0x000A0000
#define MTL_RQDCM2R_Q11MDMACH       0x0B000000
#define MAC_RSSCR_RSSE_POS          0
#define MAC_RSSCR_RSSE_LEN          1
#define MAC_RSSCR_UDP4TE_POS        3
#define MAC_RSSCR_UDP4TE_LEN        1
#define MAC_RSSCR_TCP4TE_POS        2
#define MAC_RSSCR_TCP4TE_LEN        1
#define MAC_RSSCR_IP2TE_POS         1
#define MAC_RSSCR_IP2TE_LEN         1
#define MAC_RSSDR_DMCH_POS          0
#define MAC_RSSDR_DMCH_LEN          4
#define MAC_RSSAR_OB_POS            0
#define MAC_RSSAR_OB_LEN            1
#define MAC_RSSAR_CT_POS            1
#define MAC_RSSAR_CT_LEN            1
#define MAC_RSSAR_ADDRT_POS         2
#define MAC_RSSAR_ADDRT_LEN         1
#define MAC_RSSAR_RSSIA_POS         8
#define MAC_RSSAR_RSSIA_LEN         8
#define MAC_VLANHTR_VLHT_POS        0
#define MAC_VLANHTR_VLHT_LEN        16
#define MAC_VLANIR_CSVL_POS         19
#define MAC_VLANIR_CSVL_LEN         1
#define MAC_VLANIR_VLTI_POS         20
#define MAC_VLANIR_VLTI_LEN         1
#define MAC_RFCR_RFE_POS            0
#define MAC_RFCR_RFE_LEN            1
#define MAC_MACA1HR_AE_POS          31
#define MAC_MACA1HR_AE_LEN          1
#define XLGMAC_MAC_HASH_TABLE_SIZE  8
#define XLGMAC_MAX_FIFO             81920
#define XLGMAC_SYSCLOCK             125000000
#define XLGMAC_MAX_FLOW_CONTROL_QUEUES 8
#define XLGMAC_RSS_HASH_KEY_SIZE    40
#define XLGMAC_RSS_MAX_TABLE_SIZE   256
#define XLGMAC_TX_DESC_CNT          1024
#define XLGMAC_RX_DESC_CNT          1024
#define XLGMAC_TX_MAX_BUF_SIZE      (0x3fff & ~(64 - 1))
#define XLGMAC_STD_PACKET_MTU       1500
#define XLGMAC_SKB_ALLOC_SIZE       512
#define XLGMAC_INIT_DMA_TX_FRAMES   25
#define XLGMAC_INIT_DMA_RX_FRAMES   25
#define XLGMAC_INIT_DMA_TX_USECS    1000
#define XLGMAC_INIT_DMA_RX_USECS    30
#define XLGMAC_DRV_VERSION          "1.0.0"

/* Define MMC_CR used in mmio access switch statements */
#define MMC_CR                      0x0808

/* PCI identification from xlgmac_pci_tbl: first entry only */
#define XLGMAC_PCI_VENDOR_ID        PCI_VENDOR_ID_SYNOPSYS
#define XLGMAC_PCI_DEVICE_ID        0x7302

/* Network class code */
#define XLGMAC_PCI_CLASS_ID         PCI_CLASS_NETWORK_ETHERNET


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

    /* Hardware Register Shadows */
    uint32_t mac_vr;
    uint32_t mac_hwf0r;
    uint32_t mac_hwf1r;
    uint32_t mac_hwf2r;

    uint32_t dma_mr;
    uint32_t dma_sbmr;

    uint32_t mac_tcr;
    uint32_t mac_rcr;
    uint32_t mac_pfr;
    uint32_t mac_vlantr;
    uint32_t mac_vlanir;
    uint32_t mac_rfcr;

    uint32_t mac_rsscr;
    uint32_t mac_rssar;
    uint32_t mac_rssdr;

    uint32_t mmc_cr;

    /* Minimal additional register shadows used by driver */
    uint32_t mac_rqc0r;
    uint32_t mac_rqc2r;
    uint32_t mac_ier;

    /* A tiny RSS write busy emulation */
    bool rss_op_busy;

    /* MMC interrupt enable registers used in enable_mac_interrupts */
    uint32_t mmc_rier;
    uint32_t mmc_tier;

    /* Simple DMA channel register windows (we only implement ch0) */
    uint32_t dma_ch_cr[XLGMAC_MAX_DMA_CHANNELS];
    uint32_t dma_ch_tcr[XLGMAC_MAX_DMA_CHANNELS];
    uint32_t dma_ch_rcr[XLGMAC_MAX_DMA_CHANNELS];
    uint32_t dma_ch_tdrlr[XLGMAC_MAX_DMA_CHANNELS];
    uint32_t dma_ch_rdrlr[XLGMAC_MAX_DMA_CHANNELS];
    uint32_t dma_ch_tdlr_hi[XLGMAC_MAX_DMA_CHANNELS];
    uint32_t dma_ch_tdlr_lo[XLGMAC_MAX_DMA_CHANNELS];
    uint32_t dma_ch_rdlr_hi[XLGMAC_MAX_DMA_CHANNELS];
    uint32_t dma_ch_rdlr_lo[XLGMAC_MAX_DMA_CHANNELS];
    uint32_t dma_ch_tdtr_lo[XLGMAC_MAX_DMA_CHANNELS];
    uint32_t dma_ch_rdtr_lo[XLGMAC_MAX_DMA_CHANNELS];
    uint32_t dma_ch_ier[XLGMAC_MAX_DMA_CHANNELS];
    uint32_t dma_ch_riwt[XLGMAC_MAX_DMA_CHANNELS];
    uint32_t dma_ch_sr[XLGMAC_MAX_DMA_CHANNELS];

    /* Some DSR registers used by prepare_tx_stop */
    uint32_t dma_dsr0;
    uint32_t dma_dsr1;

    /* MTL global and per-queue registers (we emulate a few queues) */
    uint32_t mtl_omr;
    uint32_t mtl_tc_etscr[XLGMAC_MAX_DMA_CHANNELS];
    uint32_t mtl_tc_qwr[XLGMAC_MAX_DMA_CHANNELS];
    uint32_t mtl_q_tqomr[XLGMAC_MAX_DMA_CHANNELS];
    uint32_t mtl_q_rqomr[XLGMAC_MAX_DMA_CHANNELS];
    uint32_t mtl_q_rqdr[XLGMAC_MAX_DMA_CHANNELS];
    uint32_t mtl_q_rqfcr[XLGMAC_MAX_DMA_CHANNELS];
    uint32_t mtl_q_isr[XLGMAC_MAX_DMA_CHANNELS];
    uint32_t mtl_q_ier[XLGMAC_MAX_DMA_CHANNELS];
    uint32_t mtl_rqdcm[3];

    /* MMC status/interrupt status registers used in stats paths */
    uint32_t mmc_risr;
    uint32_t mmc_tisr;

    /* MAC hash table and additional address storage is not needed
     * for probe; just keep minimal shadows if written. */
    uint32_t mac_htr[XLGMAC_MAC_HASH_TABLE_SIZE];

    /* MSI/MSI-X not used by driver snippet; just PCI INTx */
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    /* The provided driver snippet never reads/writes a global interrupt
     * status register or clears device IRQs. For basic bring-up we just
     * keep the line deasserted. Implementing real IRQ behavior will
     * require additional driver sources describing ISR. */
    pci_set_irq(pdev, 0);
}

/* Device-initiated DMA logic based on driver access patterns.
 * The driver programs descriptor rings and expects the hardware
 * DMA engine to operate asynchronously, but the probe path only
 * verifies that MMIO accesses succeed and that the software reset
 * bit in DMA_MR clears. We therefore do not implement real DMA
 * transfers at this stage.
 */
static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    (void)s;
    (void)is_write;
}

/* Helpers to map MMIO addr to channel/queue indices */
static inline int pcibase_dma_ch_index(hwaddr addr)
{
    if (addr < DMA_CH_BASE) {
        return -1;
    }
    int idx = (addr - DMA_CH_BASE) / DMA_CH_INC;
    if (idx < 0 || idx >= XLGMAC_MAX_DMA_CHANNELS) {
        return -1;
    }
    return idx;
}

static inline hwaddr pcibase_dma_ch_offset(hwaddr addr)
{
    return (addr - DMA_CH_BASE) % DMA_CH_INC;
}

static inline int pcibase_mtl_q_index(hwaddr addr)
{
    if (addr < MTL_Q_BASE) {
        return -1;
    }
    int idx = (addr - MTL_Q_BASE) / MTL_Q_INC;
    if (idx < 0 || idx >= XLGMAC_MAX_DMA_CHANNELS) {
        return -1;
    }
    return idx;
}

static inline hwaddr pcibase_mtl_q_offset(hwaddr addr)
{
    return (addr - MTL_Q_BASE) % MTL_Q_INC;
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val32 = 0;

    /* All registers are 32-bit from driver perspective; return 0 on
     * unexpected sizes to avoid crashing the guest. */
    if (size != 4) {
        return 0;
    }

    switch (addr) {
    case MAC_TCR:
        val32 = s->mac_tcr;
        break;
    case MAC_RCR:
        val32 = s->mac_rcr;
        break;
    case MAC_PFR:
        val32 = s->mac_pfr;
        break;
    case MAC_VLANTR:
        val32 = s->mac_vlantr;
        break;
    case MAC_VLANHTR:
        /* VLAN hash table shadow is in mac_htr[0] according to MAC_HTR0 */
        val32 = s->mac_htr[0];
        break;
    case MAC_VLANIR:
        val32 = s->mac_vlanir;
        break;
    case MAC_Q0TFCR:
        /* Only queue 0 TFCR is used by flow control helpers */
        /* Shadow not explicitly stored; returns 0 unless written via
         * some future implementation. */
        val32 = 0;
        break;
    case MAC_RFCR:
        val32 = s->mac_rfcr;
        break;
    case MAC_RQC0R:
        val32 = s->mac_rqc0r;
        break;
    case MAC_RQC2R:
        val32 = s->mac_rqc2r;
        break;
    case MAC_IER:
        val32 = s->mac_ier;
        break;
    case MAC_VR:
        /* Hardware version register; choose a fixed non-zero version
         * compatible with driver logic (translation is purely numeric). */
        val32 = s->mac_vr;
        break;
    case MAC_HWF0R:
        val32 = s->mac_hwf0r;
        break;
    case MAC_HWF1R:
        val32 = s->mac_hwf1r;
        break;
    case MAC_HWF2R:
        val32 = s->mac_hwf2r;
        break;
    case MAC_RSSCR:
        val32 = s->mac_rsscr;
        break;
    case MAC_RSSAR:
        val32 = s->mac_rssar;
        break;
    case MAC_RSSDR:
        val32 = s->mac_rssdr;
        break;
    case MMC_CR:
        val32 = s->mmc_cr;
        break;
    /* MMC interrupt status registers used by statistics code */
    case 0x0800: /* MMC_RISR placeholder offset */
        val32 = s->mmc_risr;
        break;
    case 0x0804: /* MMC_TISR placeholder offset */
        val32 = s->mmc_tisr;
        break;
    /* DMA global */
    case DMA_MR:
        val32 = s->dma_mr;
        break;
    case DMA_SBMR:
        val32 = s->dma_sbmr;
        break;
    case DMA_DSR0:
        val32 = s->dma_dsr0;
        break;
    case DMA_DSR1:
        val32 = s->dma_dsr1;
        break;
    /* MTL global */
    case MTL_OMR:
        val32 = s->mtl_omr;
        break;
    default:
        break;
    }

    /* DMA channel window */
    if (val32 == 0) {
        int ch = pcibase_dma_ch_index(addr);
        if (ch >= 0) {
            hwaddr off = pcibase_dma_ch_offset(addr);
            switch (off) {
            case DMA_CH_CR:
                val32 = s->dma_ch_cr[ch];
                break;
            case DMA_CH_TCR:
                val32 = s->dma_ch_tcr[ch];
                break;
            case DMA_CH_RCR:
                val32 = s->dma_ch_rcr[ch];
                break;
            case DMA_CH_TDRLR:
                val32 = s->dma_ch_tdrlr[ch];
                break;
            case DMA_CH_RDRLR:
                val32 = s->dma_ch_rdrlr[ch];
                break;
            case DMA_CH_TDLR_HI:
                val32 = s->dma_ch_tdlr_hi[ch];
                break;
            case DMA_CH_TDLR_LO:
                val32 = s->dma_ch_tdlr_lo[ch];
                break;
            case DMA_CH_RDLR_HI:
                val32 = s->dma_ch_rdlr_hi[ch];
                break;
            case DMA_CH_RDLR_LO:
                val32 = s->dma_ch_rdlr_lo[ch];
                break;
            case DMA_CH_TDTR_LO:
                val32 = s->dma_ch_tdtr_lo[ch];
                break;
            case DMA_CH_RDTR_LO:
                val32 = s->dma_ch_rdtr_lo[ch];
                break;
            case DMA_CH_IER:
                val32 = s->dma_ch_ier[ch];
                break;
            case DMA_CH_RIWT:
                val32 = s->dma_ch_riwt[ch];
                break;
            case DMA_CH_SR:
                val32 = s->dma_ch_sr[ch];
                break;
            default:
                break;
            }
        }
    }

    /* MTL queue window */
    if (val32 == 0) {
        int q = pcibase_mtl_q_index(addr);
        if (q >= 0) {
            hwaddr off = pcibase_mtl_q_offset(addr);
            switch (off) {
            case MTL_Q_TQOMR:
                val32 = s->mtl_q_tqomr[q];
                break;
            case MTL_Q_RQOMR:
                val32 = s->mtl_q_rqomr[q];
                break;
            case MTL_Q_RQDR:
                val32 = s->mtl_q_rqdr[q];
                break;
            case MTL_Q_RQFCR:
                val32 = s->mtl_q_rqfcr[q];
                break;
            case MTL_Q_ISR:
                val32 = s->mtl_q_isr[q];
                break;
            case MTL_Q_IER:
                val32 = s->mtl_q_ier[q];
                break;
            default:
                break;
            }
        }
    }

    return val32;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        return;
    }

    uint32_t v = (uint32_t)val;

    switch (addr) {
    case MAC_TCR:
        s->mac_tcr = v;
        break;
    case MAC_RCR:
        s->mac_rcr = v;
        break;
    case MAC_PFR:
        s->mac_pfr = v;
        break;
    case MAC_VLANTR:
        s->mac_vlantr = v;
        break;
    case MAC_VLANHTR:
        s->mac_htr[0] = v;
        break;
    case MAC_VLANIR:
        s->mac_vlanir = v;
        break;
    case MAC_Q0TFCR:
        /* Flow control per-queue register; ignore content. */
        break;
    case MAC_RFCR:
        s->mac_rfcr = v;
        break;
    case MAC_RQC0R:
        s->mac_rqc0r = v;
        break;
    case MAC_RQC2R:
        s->mac_rqc2r = v;
        break;
    case MAC_IER:
        s->mac_ier = v;
        break;
    case MAC_VR:
        s->mac_vr = v;
        break;
    case MAC_HWF0R:
        s->mac_hwf0r = v;
        break;
    case MAC_HWF1R:
        s->mac_hwf1r = v;
        break;
    case MAC_HWF2R:
        s->mac_hwf2r = v;
        break;
    case MAC_RSSCR:
        s->mac_rsscr = v;
        break;
    case MAC_RSSAR:
        s->mac_rssar = v;
        break;
    case MAC_RSSDR:
        s->mac_rssdr = v;
        break;
    case MMC_CR:
        s->mmc_cr = v;
        break;
    /* MMC interrupt enable registers: offsets are not in macro list, but
     * driver uses MMC_RIER and MMC_TIER; we map them to fixed offsets here
     * consistent with our read side. */
    case 0x0810: /* MMC_RIER placeholder */
        s->mmc_rier = v;
        break;
    case 0x0814: /* MMC_TIER placeholder */
        s->mmc_tier = v;
        break;
    case DMA_MR:
        /* Software reset bit: driver sets and then polls until cleared. */
        s->dma_mr = v & ~(1u << DMA_MR_SWR_POS);
        break;
    case DMA_SBMR:
        s->dma_sbmr = v;
        break;
    case DMA_DSR0:
        s->dma_dsr0 = v;
        break;
    case DMA_DSR1:
        s->dma_dsr1 = v;
        break;
    case MTL_OMR:
        s->mtl_omr = v;
        break;
    default:
        break;
    }

    /* Handle DMA channel window writes */
    int ch = pcibase_dma_ch_index(addr);
    if (ch >= 0) {
        hwaddr off = pcibase_dma_ch_offset(addr);
        switch (off) {
        case DMA_CH_CR:
            s->dma_ch_cr[ch] = v;
            break;
        case DMA_CH_TCR:
            s->dma_ch_tcr[ch] = v;
            break;
        case DMA_CH_RCR:
            s->dma_ch_rcr[ch] = v;
            break;
        case DMA_CH_TDRLR:
            s->dma_ch_tdrlr[ch] = v;
            break;
        case DMA_CH_RDRLR:
            s->dma_ch_rdrlr[ch] = v;
            break;
        case DMA_CH_TDLR_HI:
            s->dma_ch_tdlr_hi[ch] = v;
            break;
        case DMA_CH_TDLR_LO:
            s->dma_ch_tdlr_lo[ch] = v;
            break;
        case DMA_CH_RDLR_HI:
            s->dma_ch_rdlr_hi[ch] = v;
            break;
        case DMA_CH_RDLR_LO:
            s->dma_ch_rdlr_lo[ch] = v;
            break;
        case DMA_CH_TDTR_LO:
            s->dma_ch_tdtr_lo[ch] = v;
            /* DMA doorbell; could trigger pcibase_do_dma later */
            pcibase_do_dma(s, false);
            break;
        case DMA_CH_RDTR_LO:
            s->dma_ch_rdtr_lo[ch] = v;
            break;
        case DMA_CH_IER:
            s->dma_ch_ier[ch] = v;
            break;
        case DMA_CH_RIWT:
            s->dma_ch_riwt[ch] = v;
            break;
        case DMA_CH_SR:
            s->dma_ch_sr[ch] = v;
            break;
        default:
            break;
        }
    }

    /* Handle MTL queue window writes */
    int q = pcibase_mtl_q_index(addr);
    if (q >= 0) {
        hwaddr offq = pcibase_mtl_q_offset(addr);
        switch (offq) {
        case MTL_Q_TQOMR:
            s->mtl_q_tqomr[q] = v;
            break;
        case MTL_Q_RQOMR:
            s->mtl_q_rqomr[q] = v;
            break;
        case MTL_Q_RQDR:
            s->mtl_q_rqdr[q] = v;
            break;
        case MTL_Q_RQFCR:
            s->mtl_q_rqfcr[q] = v;
            break;
        case MTL_Q_ISR:
            /* Status register; driver clears by writing value back. */
            s->mtl_q_isr[q] &= ~v;
            break;
        case MTL_Q_IER:
            s->mtl_q_ier[q] = v;
            break;
        default:
            break;
        }
    }

    pcibase_update_irq(s);
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;
    (void)addr;
    (void)size;
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
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

    s->mac_vr = 0x00010000;  /* arbitrary non-zero version */
    s->mac_hwf0r = 0;
    s->mac_hwf1r = 0;
    s->mac_hwf2r = 0;
    s->dma_mr = 0;
    s->dma_sbmr = 0;
    s->mac_tcr = 0;
    s->mac_rcr = 0;
    s->mac_pfr = 0;
    s->mac_vlantr = 0;
    s->mac_vlanir = 0;
    s->mac_rfcr = 0;
    s->mac_rsscr = 0;
    s->mac_rssar = 0;
    s->mac_rssdr = 0;
    s->mmc_cr = 0;
    s->mac_rqc0r = 0;
    s->mac_rqc2r = 0;
    s->mac_ier = 0;
    s->rss_op_busy = false;
    s->mmc_rier = 0;
    s->mmc_tier = 0;
    s->mmc_risr = 0;
    s->mmc_tisr = 0;
    s->dma_dsr0 = 0;
    s->dma_dsr1 = 0;
    s->mtl_omr = 0;
    memset(s->mac_htr, 0, sizeof(s->mac_htr));
    memset(s->dma_ch_cr, 0, sizeof(s->dma_ch_cr));
    memset(s->dma_ch_tcr, 0, sizeof(s->dma_ch_tcr));
    memset(s->dma_ch_rcr, 0, sizeof(s->dma_ch_rcr));
    memset(s->dma_ch_tdrlr, 0, sizeof(s->dma_ch_tdrlr));
    memset(s->dma_ch_rdrlr, 0, sizeof(s->dma_ch_rdrlr));
    memset(s->dma_ch_tdlr_hi, 0, sizeof(s->dma_ch_tdlr_hi));
    memset(s->dma_ch_tdlr_lo, 0, sizeof(s->dma_ch_tdlr_lo));
    memset(s->dma_ch_rdlr_hi, 0, sizeof(s->dma_ch_rdlr_hi));
    memset(s->dma_ch_rdlr_lo, 0, sizeof(s->dma_ch_rdlr_lo));
    memset(s->dma_ch_tdtr_lo, 0, sizeof(s->dma_ch_tdtr_lo));
    memset(s->dma_ch_rdtr_lo, 0, sizeof(s->dma_ch_rdtr_lo));
    memset(s->dma_ch_ier, 0, sizeof(s->dma_ch_ier));
    memset(s->dma_ch_riwt, 0, sizeof(s->dma_ch_riwt));
    memset(s->dma_ch_sr, 0, sizeof(s->dma_ch_sr));
    memset(s->mtl_tc_etscr, 0, sizeof(s->mtl_tc_etscr));
    memset(s->mtl_tc_qwr, 0, sizeof(s->mtl_tc_qwr));
    memset(s->mtl_q_tqomr, 0, sizeof(s->mtl_q_tqomr));
    memset(s->mtl_q_rqomr, 0, sizeof(s->mtl_q_rqomr));
    memset(s->mtl_q_rqdr, 0, sizeof(s->mtl_q_rqdr));
    memset(s->mtl_q_rqfcr, 0, sizeof(s->mtl_q_rqfcr));
    memset(s->mtl_q_isr, 0, sizeof(s->mtl_q_isr));
    memset(s->mtl_q_ier, 0, sizeof(s->mtl_q_ier));
    memset(s->mtl_rqdcm, 0, sizeof(s->mtl_rqdcm));

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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  XLGMAC_PCI_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  XLGMAC_PCI_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, XLGMAC_PCI_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    s->bar_info[0].size  = 1 * MiB;
    s->bar_info[0].name  = "dwc_xlgmac-mmio";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type  = BAR_TYPE_NONE;
        s->bar_info[i].size  = 0;
        s->bar_info[i].name  = NULL;
    }

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
    .name = "dwc_xlgmac_pci",
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

