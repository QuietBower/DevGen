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

#define TYPE_PCIBASE_DEVICE "ATL1E_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#ifndef PCI_VENDOR_ID_ATTANSIC
#define PCI_VENDOR_ID_ATTANSIC 0x1969
#endif
#define PCI_DEVICE_ID_ATTANSIC_L1E      0x1026

#define REG_PM_CTRLSTAT             0x44
#define REG_PCIE_CAP_LIST           0x58
#define REG_DEVICE_CTRL             0x60
#define REG_VPD_CAP                 0x6C
#define REG_VPD_DATA                0x70
#define REG_SPI_FLASH_CTRL          0x200
#define REG_SPI_FLASH_CONFIG        0x20C
#define REG_TWSI_CTRL               0x218
#define REG_PCIE_DEV_MISC_CTRL      0x21C
#define REG_PCIE_PHYMISC            0x1000
#define REG_MASTER_CTRL             0x1400
#define REG_MANUAL_TIMER_INIT       0x1404
#define REG_IRQ_MODU_TIMER_INIT     0x1408
#define REG_IRQ_MODU_TIMER2_INIT    0x140A
#define REG_GPHY_CTRL               0x140C
#define REG_CMBDISDMA_TIMER         0x140E
#define REG_IDLE_STATUS             0x1410
#define REG_MDIO_CTRL               0x1414
#define REG_PHY_STATUS              0x1418
#define REG_SERDES_LOCK             0x1424
#define REG_MAC_CTRL                0x1480
#define REG_MAC_IPG_IFG             0x1484
#define REG_MAC_STA_ADDR            0x1488
#define REG_RX_HASH_TABLE           0x1490
#define REG_MAC_HALF_DUPLX_CTRL     0x1498
#define REG_MTU                     0x149c
#define REG_WOL_CTRL                0x14a0
#define REG_SRAM_TRD_ADDR           0x1518
#define REG_SRAM_TRD_LEN            0x151C
#define REG_SRAM_RXF_ADDR           0x1520
#define REG_SRAM_RXF_LEN            0x1524
#define REG_SRAM_TXF_ADDR           0x1528
#define REG_SRAM_TXF_LEN            0x152C
#define REG_SRAM_TCPH_ADDR          0x1530
#define REG_SRAM_PKTH_ADDR          0x1532
#define REG_LOAD_PTR                0x1534
#define REG_RXF3_BASE_ADDR_HI       0x153C
#define REG_DESC_BASE_ADDR_HI       0x1540
#define REG_RXF0_BASE_ADDR_HI       0x1540
#define REG_HOST_RXF0_PAGE0_LO      0x1544
#define REG_HOST_RXF0_PAGE1_LO      0x1548
#define REG_TPD_BASE_ADDR_LO        0x154C
#define REG_RXF1_BASE_ADDR_HI       0x1550
#define REG_RXF2_BASE_ADDR_HI       0x1554
#define REG_HOST_RXFPAGE_SIZE       0x1558
#define REG_TPD_RING_SIZE           0x155C
#define REG_IDT_TABLE0              0x1560
#define REG_IDT_TABLE               REG_IDT_TABLE0
#define REG_BASE_CPU_NUMBER         0x157C
#define REG_TXQ_CTRL                0x1580
#define REG_TX_EARLY_TH             0x1584
#define REG_RXQ_CTRL                0x15A0
#define REG_RXQ_JMBOSZ_RRDTIM       0x15A4
#define REG_RXQ_RXF_PAUSE_THRESH    0x15A8
#define REG_DMA_CTRL                0x15C0
#define REG_SMB_STAT_TIMER          0x15C4
#define REG_TRIG_TPD_THRESH         0x15C8
#define REG_TRIG_RRD_THRESH         0x15CA
#define REG_TRIG_TXTIMER            0x15CC
#define REG_TRIG_RXTIMER            0x15CE
#define REG_HOST_RXF1_PAGE0_LO      0x15D0
#define REG_HOST_RXF1_PAGE1_LO      0x15D4
#define REG_HOST_RXF2_PAGE0_LO      0x15D8
#define REG_HOST_RXF2_PAGE1_LO      0x15DC
#define REG_HOST_RXF3_PAGE0_LO      0x15E0
#define REG_HOST_RXF3_PAGE1_LO      0x15E4
#define REG_MB_TPD_PROD_IDX         0x15F0
#define REG_HOST_RXF0_PAGE0_VLD     0x15F4
#define REG_HOST_RXF0_PAGE1_VLD     0x15F5
#define REG_HOST_RXF1_PAGE0_VLD     0x15F6
#define REG_HOST_RXF1_PAGE1_VLD     0x15F7
#define REG_HOST_RXF2_PAGE0_VLD     0x15F8
#define REG_HOST_RXF2_PAGE1_VLD     0x15F9
#define REG_HOST_RXF3_PAGE0_VLD     0x15FA
#define REG_HOST_RXF3_PAGE1_VLD     0x15FB
#define REG_ISR                     0x1600
#define REG_IMR                     0x1604
#define REG_MAC_RX_STATUS_BIN       0x1700
#define REG_MAC_RX_STATUS_END       0x175c
#define REG_MAC_TX_STATUS_BIN       0x1760
#define REG_MAC_TX_STATUS_END       0x17c0
#define REG_TPD_CONS_IDX            0x1804
#define REG_HOST_RXF0_MB0_LO        0x1820
#define REG_HOST_RXF0_MB1_LO        0x1824
#define REG_HOST_RXF1_MB0_LO        0x1828
#define REG_HOST_RXF1_MB1_LO        0x182C
#define REG_HOST_RXF2_MB0_LO        0x1830
#define REG_HOST_RXF2_MB1_LO        0x1834
#define REG_HOST_RXF3_MB0_LO        0x1838
#define REG_HOST_RXF3_MB1_LO        0x183C
#define REG_HOST_TX_CMB_LO          0x1840
#define REG_DEBUG_DATA0             0x1900

#define ISR_DIS_INT            0x80000000
#define ISR_GPHY               0x1000
#define ISR_PHY_LINKDOWN       0x10000000
#define ISR_DMAR_TO_RST        0x400
#define ISR_DMAW_TO_RST        0x800
#define ISR_SMB                1
#define ISR_MANUAL             4
#define ISR_TXF_UN             0x100
#define ISR_HW_RXF_OV          8
#define ISR_HOST_RXF0_OV       0x10
#define ISR_GPHY_LPW           0x4000
#define ISR_RX_PKT             0x10000
#define ISR_TX_PKT             0x20000

#define MASTER_CTRL_LED_MODE        0x200
#define MASTER_CTRL_ITIMER_EN       0x4
#define MASTER_CTRL_ITIMER2_EN      0x20
#define MASTER_CTRL_MANUAL_INT      0x8

#define MAC_CTRL_RX_EN              2
#define MAC_CTRL_TX_EN              1
#define MAC_CTRL_DUPLX              0x20
#define MAC_CTRL_SPEED_1000         2
#define MAC_CTRL_SPEED_10_100       1
#define MAC_CTRL_SPEED_SHIFT        20
#define MAC_CTRL_TX_FLOW            4
#define MAC_CTRL_RX_FLOW            8
#define MAC_CTRL_ADD_CRC            0x40
#define MAC_CTRL_PAD                0x80
#define MAC_CTRL_PRMLEN_MASK        0xf
#define MAC_CTRL_PRMLEN_SHIFT       10
#define MAC_CTRL_BC_EN              0x4000000
#define MAC_CTRL_PROMIS_EN          0x8000
#define MAC_CTRL_MC_ALL_EN          0x2000000
#define MAC_CTRL_DBG                0x8000000
#define MAC_CTRL_RMV_VLAN           0x4000

#define PHY_STATUS_100M           0x20000
#define PHY_STATUS_EMI_CA         0x40000

#define DEVICE_CTRL_MAX_PAYLOAD_SHIFT   5
#define DEVICE_CTRL_MAX_PAYLOAD_MASK    0x7
#define DEVICE_CTRL_MAX_RREQ_SZ_SHIFT   12
#define DEVICE_CTRL_MAX_RREQ_SZ_MASK    0x7

#define TXQ_CTRL_NUM_TPD_BURST_MASK     0xF
#define TXQ_CTRL_NUM_TPD_BURST_SHIFT    0
#define TXQ_CTRL_ENH_MODE               0x40
#define TXQ_CTRL_EN                     0x20

#define RXQ_JMBOSZ_TH_MASK      0x7ff
#define RXQ_JMBOSZ_TH_SHIFT         0
#define RXQ_JMBO_LKAH_MASK          0xf
#define RXQ_JMBO_LKAH_SHIFT         11
#define RXQ_RXF_PAUSE_TH_HI_MASK        0xfff
#define RXQ_RXF_PAUSE_TH_HI_SHIFT       0
#define RXQ_RXF_PAUSE_TH_LO_MASK        0xfff
#define RXQ_RXF_PAUSE_TH_LO_SHIFT       16
#define RXQ_CTRL_HASH_TYPE_IPV4                 0x10000
#define RXQ_CTRL_HASH_TYPE_IPV4_TCP             0x20000
#define RXQ_CTRL_HASH_TYPE_IPV6                 0x40000
#define RXQ_CTRL_HASH_TYPE_IPV6_TCP             0x80000
#define RXQ_CTRL_HASH_ENABLE                    0x20000000
#define RXQ_CTRL_RSS_MODE_MQUESINT              0x8000000
#define RXQ_CTRL_IPV6_XSUM_VERIFY_EN            0x80
#define RXQ_CTRL_PBA_ALIGN_32                   0
#define RXQ_CTRL_CUT_THRU_EN                    0x40000000
#define RXQ_CTRL_EN                             0x80000000

#define DMA_CTRL_RXCMB_EN               0x200000
#define DMA_CTRL_DMAR_BURST_LEN_MASK    7
#define DMA_CTRL_DMAR_BURST_LEN_SHIFT   4
#define DMA_CTRL_DMAW_BURST_LEN_MASK    7
#define DMA_CTRL_DMAW_BURST_LEN_SHIFT   7
#define DMA_CTRL_DMAR_REQ_PRI           0x400
#define DMA_CTRL_DMAR_OUT_ORDER         0x4
#define DMA_CTRL_DMAR_DLY_CNT_MASK      0x1F
#define DMA_CTRL_DMAR_DLY_CNT_SHIFT     11
#define DMA_CTRL_DMAW_DLY_CNT_MASK      0xF
#define DMA_CTRL_DMAW_DLY_CNT_SHIFT     16

#define WOL_MAGIC_EN                    0x00000004
#define WOL_MAGIC_PME_EN                0x00000008
#define WOL_LINK_CHG_EN                 0x00000010
#define WOL_LINK_CHG_PME_EN             0x00000020

#define PCIE_PHYMISC_FORCE_RCV_DET      0x4

#define AT_MAX_RECEIVE_QUEUE    4
#define AT_PAGE_NUM_PER_QUEUE   2
#define AT_DMA_HI_ADDR_MASK     0xffffffff00000000ULL
#define AT_DMA_LO_ADDR_MASK     0x00000000ffffffffULL

#define RRS_IS_IPV4             0x0400
#define RRS_IS_IPV6             0x0010
#define RRS_IS_TCP              0x1000
#define RRS_IS_UDP              0x0800
#define RRS_IS_802_3            0x0080
#define RRS_IS_IP_DF            0x0040
#define RRS_ERR_IP_CSUM         0x0040
#define RRS_ERR_L4_CSUM         0x0080
#define RRS_IS_ERR_FRAME        0x0200
#define RRS_ERR_BAD_CRC         0x0001
#define RRS_ERR_DRIBBLE         0x0004
#define RRS_ERR_CODE            0x0002
#define RRS_ERR_TRUNC           0x0020
#define RRS_PKT_SIZE_SHIFT      16
#define RRS_PKT_SIZE_MASK       0x3FFF
#define RRS_IS_VLAN_TAG         0x0100

#define TPD_BUFLEN_MASK         0x3FFF
#define TPD_BUFLEN_SHIFT        0
#define TPD_SEGMENT_EN_SHIFT    4
#define TPD_SEGMENT_EN_MASK     0x0001
#define TPD_V4_IPHL_SHIFT       10
#define TPD_TCPHDRLEN_MASK      0x000F
#define TPD_TCPHDRLEN_SHIFT     14
#define TPD_MSS_MASK            0x1FFF
#define TPD_MSS_SHIFT           19
#define TPD_PLOADOFFSET_MASK    0x00FF
#define TPD_PLOADOFFSET_SHIFT   16
#define TPD_CCSUMOFFSET_MASK    0x00FF
#define TPD_CCSUMOFFSET_SHIFT   24
#define TPD_CC_SEGMENT_EN_SHIFT 3
#define TPD_HDRFLAG_SHIFT       18
#define TPD_EOP_SHIFT           0
#define TPD_INS_VL_TAG_SHIFT    2
#define TPD_VLANTAG_MASK        0xFFFF
#define TPD_VLAN_SHIFT          16
#define TPD_VL_TAGGED_SHIFT     8
#define TPD_ETHTYPE_SHIFT       9

#define AT_VLAN_TAG_TO_TPD_TAG(_vlan, _tpd)    \
    _tpd = (((_vlan) << (4)) | (((_vlan) >> 13) & 7) |\
             (((_vlan) >> 9) & 8))

/* Hardware DMA Descriptor Structures */
struct atl1e_tpd_desc {
    uint64_t buffer_addr;
    uint32_t word2;
    uint32_t word3;
};

struct atl1e_recv_ret_status {
    uint16_t seq_num;
    uint16_t hash_lo;
    uint32_t word1;
    uint16_t pkt_flag;
    uint16_t err_flag;
    uint16_t hash_hi;
    uint16_t vtag;
};

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
    uint32_t isr;
    uint32_t imr;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t mac_ctrl;
    uint32_t master_ctrl;
    uint32_t dma_ctrl;
    uint32_t txq_ctrl;
    uint32_t rxq_ctrl;
    uint32_t device_ctrl;
    uint32_t mdio_ctrl;
    uint32_t gphy_ctrl;
    uint32_t twsi_ctrl;
    uint32_t spi_flash_ctrl;
    uint32_t spi_flash_config;
    uint32_t mac_half_duplx_ctrl;
    uint32_t serdes_lock;
    uint32_t mac_ipg_ifg;
    uint32_t pcie_dev_misc_ctrl;
    uint32_t pcie_phymisc;
    uint32_t wol_ctrl;
    uint32_t mtu;
    uint32_t sram_txf_len;
    uint32_t sram_trd_len;
    uint32_t sram_rxf_len;
    uint32_t sram_txf_addr;
    uint32_t sram_trd_addr;
    uint32_t sram_rxf_addr;
    uint32_t sram_pkth_addr;
    uint32_t sram_tcph_addr;
    uint32_t mac_sta_addr[2];

    /* DMA Context */
    uint32_t desc_base_addr_hi;
    uint32_t tpd_base_addr_lo;
    uint32_t tpd_ring_size;
    uint32_t host_tx_cmb_lo;
    uint32_t host_rxfpage_size;
    uint32_t rxf_base_addr_hi[4];
    uint32_t host_rxf_page_lo[4][2];
    uint32_t host_rxf_page_vld[4][2];
    uint32_t host_rxf_mb_lo[4][2];
    uint32_t tpd_cons_idx;
    uint32_t mb_tpd_prod_idx;

    uint32_t idle_status;
    uint32_t phy_status;
    
    uint32_t pm_ctrlstat;
    
    uint32_t smb_stat_timer;
    uint32_t irq_modu_timer_init;
    uint32_t irq_modu_timer2_init;
    uint32_t trig_rrd_thresh;
    uint32_t trig_tpd_thresh;
    uint32_t trig_rxtimer;
    uint32_t trig_txtimer;
    uint32_t cmbdisdma_timer;
    uint32_t manual_timer_init;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool level = (s->isr & s->imr) != 0;
    pci_set_irq(pdev, level);
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    /* DMA logic will be implemented when structures are available */
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case REG_PM_CTRLSTAT: val = s->pm_ctrlstat; break;
    case REG_PCIE_PHYMISC: val = s->pcie_phymisc; break;
    case REG_MASTER_CTRL: val = s->master_ctrl; break;
    case REG_MANUAL_TIMER_INIT: val = s->manual_timer_init; break;
    case REG_IRQ_MODU_TIMER_INIT: val = s->irq_modu_timer_init; break;
    case REG_IRQ_MODU_TIMER2_INIT: val = s->irq_modu_timer2_init; break;
    case REG_GPHY_CTRL: val = s->gphy_ctrl; break;
    case REG_CMBDISDMA_TIMER: val = s->cmbdisdma_timer; break;
    case REG_IDLE_STATUS: val = s->idle_status; break;
    case REG_MDIO_CTRL: val = s->mdio_ctrl; break;
    case REG_PHY_STATUS: val = s->phy_status; break;
    case REG_SERDES_LOCK: val = s->serdes_lock; break;
    case REG_MAC_CTRL: val = s->mac_ctrl; break;
    case REG_MAC_IPG_IFG: val = s->mac_ipg_ifg; break;
    case REG_MAC_STA_ADDR: val = s->mac_sta_addr[0]; break;
    case REG_MAC_STA_ADDR + 4: val = s->mac_sta_addr[1]; break;
    case REG_TWSI_CTRL: val = s->twsi_ctrl; break;
    case REG_MAC_HALF_DUPLX_CTRL: val = s->mac_half_duplx_ctrl; break;
    case REG_MTU: val = s->mtu; break;
    case REG_WOL_CTRL: val = s->wol_ctrl; break;
    case REG_SRAM_TRD_ADDR: val = s->sram_trd_addr; break;
    case REG_SRAM_TRD_LEN: val = s->sram_trd_len; break;
    case REG_SRAM_RXF_ADDR: val = s->sram_rxf_addr; break;
    case REG_SRAM_RXF_LEN: val = s->sram_rxf_len; break;
    case REG_SRAM_TXF_ADDR: val = s->sram_txf_addr; break;
    case REG_SRAM_TXF_LEN: val = s->sram_txf_len; break;
    case REG_SRAM_TCPH_ADDR: val = s->sram_tcph_addr; break;
    case REG_SRAM_PKTH_ADDR: val = s->sram_pkth_addr; break;
    case REG_DESC_BASE_ADDR_HI: val = s->desc_base_addr_hi; break;
    case REG_TPD_BASE_ADDR_LO: val = s->tpd_base_addr_lo; break;
    case REG_HOST_RXFPAGE_SIZE: val = s->host_rxfpage_size; break;
    case REG_TPD_RING_SIZE: val = s->tpd_ring_size; break;
    case REG_TXQ_CTRL: val = s->txq_ctrl; break;
    case REG_RXQ_CTRL: val = s->rxq_ctrl; break;
    case REG_DMA_CTRL: val = s->dma_ctrl; break;
    case REG_SMB_STAT_TIMER: val = s->smb_stat_timer; break;
    case REG_TRIG_TPD_THRESH: val = s->trig_tpd_thresh; break;
    case REG_TRIG_RRD_THRESH: val = s->trig_rrd_thresh; break;
    case REG_TRIG_TXTIMER: val = s->trig_txtimer; break;
    case REG_TRIG_RXTIMER: val = s->trig_rxtimer; break;
    case REG_MB_TPD_PROD_IDX: val = s->mb_tpd_prod_idx; break;
    case REG_ISR: val = s->isr; break;
    case REG_IMR: val = s->imr; break;
    case REG_TPD_CONS_IDX: val = s->tpd_cons_idx; break;
    case REG_HOST_TX_CMB_LO: val = s->host_tx_cmb_lo; break;
    case REG_DEVICE_CTRL: val = s->device_ctrl; break;
    case REG_HOST_RXF0_PAGE0_LO: val = s->host_rxf_page_lo[0][0]; break;
    case REG_HOST_RXF0_PAGE1_LO: val = s->host_rxf_page_lo[0][1]; break;
    case REG_RXF1_BASE_ADDR_HI: val = s->rxf_base_addr_hi[1]; break;
    case REG_RXF2_BASE_ADDR_HI: val = s->rxf_base_addr_hi[2]; break;
    case REG_RXF3_BASE_ADDR_HI: val = s->rxf_base_addr_hi[3]; break;
    case REG_HOST_RXF1_PAGE0_LO: val = s->host_rxf_page_lo[1][0]; break;
    case REG_HOST_RXF1_PAGE1_LO: val = s->host_rxf_page_lo[1][1]; break;
    case REG_HOST_RXF2_PAGE0_LO: val = s->host_rxf_page_lo[2][0]; break;
    case REG_HOST_RXF2_PAGE1_LO: val = s->host_rxf_page_lo[2][1]; break;
    case REG_HOST_RXF3_PAGE0_LO: val = s->host_rxf_page_lo[3][0]; break;
    case REG_HOST_RXF3_PAGE1_LO: val = s->host_rxf_page_lo[3][1]; break;
    case REG_HOST_RXF0_PAGE0_VLD: val = s->host_rxf_page_vld[0][0]; break;
    case REG_HOST_RXF0_PAGE1_VLD: val = s->host_rxf_page_vld[0][1]; break;
    case REG_HOST_RXF1_PAGE0_VLD: val = s->host_rxf_page_vld[1][0]; break;
    case REG_HOST_RXF1_PAGE1_VLD: val = s->host_rxf_page_vld[1][1]; break;
    case REG_HOST_RXF2_PAGE0_VLD: val = s->host_rxf_page_vld[2][0]; break;
    case REG_HOST_RXF2_PAGE1_VLD: val = s->host_rxf_page_vld[2][1]; break;
    case REG_HOST_RXF3_PAGE0_VLD: val = s->host_rxf_page_vld[3][0]; break;
    case REG_HOST_RXF3_PAGE1_VLD: val = s->host_rxf_page_vld[3][1]; break;
    case REG_HOST_RXF0_MB0_LO: val = s->host_rxf_mb_lo[0][0]; break;
    case REG_HOST_RXF0_MB1_LO: val = s->host_rxf_mb_lo[0][1]; break;
    case REG_HOST_RXF1_MB0_LO: val = s->host_rxf_mb_lo[1][0]; break;
    case REG_HOST_RXF1_MB1_LO: val = s->host_rxf_mb_lo[1][1]; break;
    case REG_HOST_RXF2_MB0_LO: val = s->host_rxf_mb_lo[2][0]; break;
    case REG_HOST_RXF2_MB1_LO: val = s->host_rxf_mb_lo[2][1]; break;
    case REG_HOST_RXF3_MB0_LO: val = s->host_rxf_mb_lo[3][0]; break;
    case REG_HOST_RXF3_MB1_LO: val = s->host_rxf_mb_lo[3][1]; break;
    default:
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case REG_PM_CTRLSTAT: s->pm_ctrlstat = val; break;
    case REG_PCIE_PHYMISC: s->pcie_phymisc = val; break;
    case REG_MASTER_CTRL: 
        s->master_ctrl = val; 
        if (val & MASTER_CTRL_MANUAL_INT) {
            s->isr |= ISR_MANUAL;
            pcibase_update_irq(s);
        }
        break;
    case REG_MANUAL_TIMER_INIT: s->manual_timer_init = val; break;
    case REG_IRQ_MODU_TIMER_INIT: s->irq_modu_timer_init = val; break;
    case REG_IRQ_MODU_TIMER2_INIT: s->irq_modu_timer2_init = val; break;
    case REG_GPHY_CTRL: s->gphy_ctrl = val; break;
    case REG_CMBDISDMA_TIMER: s->cmbdisdma_timer = val; break;
    case REG_MDIO_CTRL: s->mdio_ctrl = val; break;
    case REG_MAC_CTRL: s->mac_ctrl = val; break;
    case REG_MAC_IPG_IFG: s->mac_ipg_ifg = val; break;
    case REG_MAC_STA_ADDR: s->mac_sta_addr[0] = val; break;
    case REG_MAC_STA_ADDR + 4: s->mac_sta_addr[1] = val; break;
    case REG_TWSI_CTRL: s->twsi_ctrl = 0; break; /* Clear to indicate immediate completion */
    case REG_MAC_HALF_DUPLX_CTRL: s->mac_half_duplx_ctrl = val; break;
    case REG_MTU: s->mtu = val; break;
    case REG_WOL_CTRL: s->wol_ctrl = val; break;
    case REG_DESC_BASE_ADDR_HI: s->desc_base_addr_hi = val; break;
    case REG_TPD_BASE_ADDR_LO: s->tpd_base_addr_lo = val; break;
    case REG_HOST_RXFPAGE_SIZE: s->host_rxfpage_size = val; break;
    case REG_TPD_RING_SIZE: s->tpd_ring_size = val; break;
    case REG_TXQ_CTRL: s->txq_ctrl = val; break;
    case REG_RXQ_CTRL: s->rxq_ctrl = val; break;
    case REG_DMA_CTRL: s->dma_ctrl = val; break;
    case REG_SMB_STAT_TIMER: s->smb_stat_timer = val; break;
    case REG_TRIG_TPD_THRESH: s->trig_tpd_thresh = val; break;
    case REG_TRIG_RRD_THRESH: s->trig_rrd_thresh = val; break;
    case REG_TRIG_TXTIMER: s->trig_txtimer = val; break;
    case REG_TRIG_RXTIMER: s->trig_rxtimer = val; break;
    case REG_MB_TPD_PROD_IDX: 
        s->mb_tpd_prod_idx = val; 
        pcibase_do_dma(s, true);
        break;
    case REG_ISR: 
        s->isr &= ~val;
        pcibase_update_irq(s);
        break;
    case REG_IMR: 
        s->imr = val; 
        pcibase_update_irq(s);
        break;
    case REG_HOST_TX_CMB_LO: s->host_tx_cmb_lo = val; break;
    case REG_DEVICE_CTRL: s->device_ctrl = val; break;
    case REG_HOST_RXF0_PAGE0_LO: s->host_rxf_page_lo[0][0] = val; break;
    case REG_HOST_RXF0_PAGE1_LO: s->host_rxf_page_lo[0][1] = val; break;
    case REG_RXF1_BASE_ADDR_HI: s->rxf_base_addr_hi[1] = val; break;
    case REG_RXF2_BASE_ADDR_HI: s->rxf_base_addr_hi[2] = val; break;
    case REG_RXF3_BASE_ADDR_HI: s->rxf_base_addr_hi[3] = val; break;
    case REG_HOST_RXF1_PAGE0_LO: s->host_rxf_page_lo[1][0] = val; break;
    case REG_HOST_RXF1_PAGE1_LO: s->host_rxf_page_lo[1][1] = val; break;
    case REG_HOST_RXF2_PAGE0_LO: s->host_rxf_page_lo[2][0] = val; break;
    case REG_HOST_RXF2_PAGE1_LO: s->host_rxf_page_lo[2][1] = val; break;
    case REG_HOST_RXF3_PAGE0_LO: s->host_rxf_page_lo[3][0] = val; break;
    case REG_HOST_RXF3_PAGE1_LO: s->host_rxf_page_lo[3][1] = val; break;
    case REG_HOST_RXF0_PAGE0_VLD: s->host_rxf_page_vld[0][0] = val; break;
    case REG_HOST_RXF0_PAGE1_VLD: s->host_rxf_page_vld[0][1] = val; break;
    case REG_HOST_RXF1_PAGE0_VLD: s->host_rxf_page_vld[1][0] = val; break;
    case REG_HOST_RXF1_PAGE1_VLD: s->host_rxf_page_vld[1][1] = val; break;
    case REG_HOST_RXF2_PAGE0_VLD: s->host_rxf_page_vld[2][0] = val; break;
    case REG_HOST_RXF2_PAGE1_VLD: s->host_rxf_page_vld[2][1] = val; break;
    case REG_HOST_RXF3_PAGE0_VLD: s->host_rxf_page_vld[3][0] = val; break;
    case REG_HOST_RXF3_PAGE1_VLD: s->host_rxf_page_vld[3][1] = val; break;
    case REG_HOST_RXF0_MB0_LO: s->host_rxf_mb_lo[0][0] = val; break;
    case REG_HOST_RXF0_MB1_LO: s->host_rxf_mb_lo[0][1] = val; break;
    case REG_HOST_RXF1_MB0_LO: s->host_rxf_mb_lo[1][0] = val; break;
    case REG_HOST_RXF1_MB1_LO: s->host_rxf_mb_lo[1][1] = val; break;
    case REG_HOST_RXF2_MB0_LO: s->host_rxf_mb_lo[2][0] = val; break;
    case REG_HOST_RXF2_MB1_LO: s->host_rxf_mb_lo[2][1] = val; break;
    case REG_HOST_RXF3_MB0_LO: s->host_rxf_mb_lo[3][0] = val; break;
    case REG_HOST_RXF3_MB1_LO: s->host_rxf_mb_lo[3][1] = val; break;
    case REG_LOAD_PTR:
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

    s->isr = 0;
    s->imr = 0;
    s->mac_ctrl = 0;
    s->master_ctrl = 0;
    s->dma_ctrl = 0;
    s->txq_ctrl = 0;
    s->rxq_ctrl = 0;
    s->device_ctrl = 0;
    s->mdio_ctrl = 0;
    s->gphy_ctrl = 0;
    s->twsi_ctrl = 0;
    s->spi_flash_ctrl = 0;
    s->spi_flash_config = 0;
    s->mac_half_duplx_ctrl = 0;
    s->serdes_lock = 0;
    s->mac_ipg_ifg = 0;
    s->pcie_dev_misc_ctrl = 0;
    s->pcie_phymisc = 0;
    s->wol_ctrl = 0;
    s->mtu = 0;
    s->sram_txf_len = 0;
    s->sram_trd_len = 0;
    s->sram_rxf_len = 0;
    s->sram_txf_addr = 0;
    s->sram_trd_addr = 0;
    s->sram_rxf_addr = 0;
    s->sram_pkth_addr = 0;
    s->sram_tcph_addr = 0;
    s->desc_base_addr_hi = 0;
    s->tpd_base_addr_lo = 0;
    s->tpd_ring_size = 0;
    s->host_tx_cmb_lo = 0;
    s->host_rxfpage_size = 0;
    s->tpd_cons_idx = 0;
    s->mb_tpd_prod_idx = 0;
    s->idle_status = 0;
    s->phy_status = 0;
    s->pm_ctrlstat = 0;
    s->smb_stat_timer = 0;
    s->irq_modu_timer_init = 0;
    s->irq_modu_timer2_init = 0;
    s->trig_rrd_thresh = 0;
    s->trig_tpd_thresh = 0;
    s->trig_rxtimer = 0;
    s->trig_txtimer = 0;
    s->cmbdisdma_timer = 0;
    s->manual_timer_init = 0;
    
    /* Initialize with a valid unicast MAC address (52:54:00:12:34:56) */
    s->mac_sta_addr[0] = 0x00123456;
    s->mac_sta_addr[1] = 0x5254;
    
    for (int i = 0; i < 4; i++) {
        s->rxf_base_addr_hi[i] = 0;
        for (int j = 0; j < 2; j++) {
            s->host_rxf_page_lo[i][j] = 0;
            s->host_rxf_page_vld[i][j] = 0;
            s->host_rxf_mb_lo[i][j] = 0;
        }
    }
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_ATTANSIC );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_ATTANSIC_L1E );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_ETHERNET );
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
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x10000;
    s->bar_info[0].name = "atl1e-mmio";
  
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
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
    .name = "ATL1E_pci",
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
