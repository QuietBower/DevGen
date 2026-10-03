/*
 * QEMU PCI device model for MediaTek mt76x0e (minimal behavioral model)
 * Generated for driver probing and basic initialization.
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

/* Removed non-existent kernel header; define needed vendor ID locally */
#define PCI_VENDOR_ID_MEDIATEK 0x14c3

#define TYPE_PCIBASE_DEVICE "mt76x0e_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define MT76X0E_PCI_VENDOR_ID   PCI_VENDOR_ID_MEDIATEK
#define MT76X0E_PCI_DEVICE_ID   0x7610
#define MT76X0E_PCI_CLASS_ID    PCI_CLASS_NETWORK_OTHER

#define MT_ASIC_VERSION                 0x0000
#define MT_COEXCFG0                     0x0040
#define MT_CMB_CTRL                     0x0020
#define MT_EFUSE_CTRL                   0x0024
#define MT_WLAN_FUN_CTRL                0x0080
#define MT_CSR_EE_CFG1                  0x0104
#define MT_XO_CTRL7                     0x011c
#define MT_WPDMA_GLO_CFG                0x0208
#define MT_INT_MASK_CSR                 0x0204
#define MT_WMM_AIFSN                    0x0214
#define MT_WMM_CWMIN                    0x0218
#define MT_WMM_CWMAX                    0x021c
#define MT_WMM_TXOP_BASE                0x0220
#define MT_WMM_CTRL                     0x0230
#define MT_WMM_CWMIN_MASK               GENMASK(3, 0)
#define MT_WMM_CWMAX_MASK               GENMASK(3, 0)
#define MT_WMM_AIFSN_MASK               GENMASK(3, 0)
#define MT_WPDMA_RST_IDX                0x020c
#define MT_WMM_TXOP_MASK                GENMASK(15, 0)
#define MT_WLAN_FUN_CTRL_WLAN_EN        BIT(0)
#define MT_WLAN_FUN_CTRL_WLAN_CLK_EN    BIT(1)
#define MT_WLAN_FUN_CTRL_WLAN_RESET_RF  BIT(2)
#define MT_WLAN_FUN_CTRL_WLAN_RESET     BIT(3)
#define MT_WLAN_FUN_CTRL_FRC_WL_ANT_SEL BIT(5)
#define MT_WLAN_FUN_CTRL_GPIO_OUT_EN    GENMASK(31, 24)
#define MT_CMB_CTRL_XTAL_RDY            BIT(22)
#define MT_CMB_CTRL_PLL_LD              BIT(23)
#define MT_MAC_SYS_CTRL                 0x1004
#define MT_MAC_ADDR_DW0                 0x1008
#define MT_MAC_ADDR_DW1                 0x100c
#define MT_MAC_BSSID_DW0                0x1010
#define MT_MAC_BSSID_DW1                0x1014
#define MT_MAX_LEN_CFG                  0x1018
#define MT_XIFS_TIME_CFG                0x1100
#define MT_BKOFF_SLOT_CFG               0x1104
#define MT_CH_TIME_CFG                  0x110c
#define MT_BEACON_TIME_CFG              0x1114
#define MT_INT_TIMER_CFG                0x1128
#define MT_INT_TIMER_EN                 0x112c
#define MT_CH_IDLE                      0x1130
#define MT_CH_BUSY                      0x1134
#define MT_ED_CCA_TIMER                 0x1140
#define MT_MAC_STATUS                   0x1200
#define MT_TX_PROT_CFG6                 0x13e0
#define MT_TX_ALC_CFG_0                 0x13b0
#define MT_TX_ALC_CFG_1                 0x13b4
#define MT_TX_ALC_VGA3                  0x13c8
#define MT_TX0_RF_GAIN_CORR             0x13a0
#define MT_TX0_RF_GAIN_ATTEN            0x13a8
#define MT_TX_BAND_CFG                  0x132c
#define MT_TX_PIN_CFG                   0x1328
#define MT_TX_PWR_CFG_0                 0x1314
#define MT_TX_PWR_CFG_1                 0x1318
#define MT_TX_PWR_CFG_2                 0x131c
#define MT_TX_PWR_CFG_3                 0x1320
#define MT_TX_PWR_CFG_4                 0x1324
#define MT_TX_PWR_CFG_7                 0x13d4
#define MT_TX_PWR_CFG_8                 0x13d8
#define MT_TX_PWR_CFG_9                 0x13dc
#define MT_TX_RTS_CFG                   0x1344
#define MT_TX_TIMEOUT_CFG               0x1348
#define MT_TX_LINK_CFG                  0x1350
#define MT_CCK_PROT_CFG                 0x1364
#define MT_OFDM_PROT_CFG                0x1368
#define MT_AUTO_RSP_CFG                 0x1404
#define MT_RX_FILTR_CFG                 0x1400
#define MT_EXT_CCA_CFG                  0x141c
#define MT_RX_STAT_0                    0x1700
#define MT_RX_STAT_1                    0x1704
#define MT_RX_STAT_2                    0x1708
#define MT_TX_STA_0                     0x170c
#define MT_TX_STA_1                     0x1710
#define MT_TX_STA_2                     0x1714
#define MT_TX_STAT_FIFO                 0x1718
#define MT_TX_STAT_FIFO_EXT             0x1798
#define MT_RXQ_STA                      0x0430
#define MT_RX_RING_BASE                 0x03c0
#define MT_RING_SIZE                    0x10
#define MT_MAC_APC_BSSID_BASE           0x1090
#define MT_BCN_OFFSET_BASE              0x041c
#define MT_BCN_BYPASS_MASK              0x108c
#define MT_WCID_ADDR_BASE               0x1800
#define MT_WCID_ATTR_BASE               0xa800
#define MT_WCID_IV_BASE                 0xa000
#define MT_WCID_KEY_BASE                0x8000
#define MT_WCID_DROP_BASE               0x106c
#define MT_LED_CTRL                     0x0770
#define MT_LED_S0_BASE                  0x077C
#define MT_LED_S1_BASE                  0x0780
#define MT_MCU_INT_LEVEL                0x0718
#define MT_MCU_COM_REG0                 0x0730
#define MT_MCU_RESET_CTL                0x070C
#define MT_MCU_SEMAPHORE_00             0x07B0
#define MT_FCE_L2_STUFF                 0x080c
#define MT_RF_CSR_CFG                   0x0500
#define MT_RF_BYPASS_0                  0x0504
#define MT_RF_SETTING_0                 0x050c
#define MT_RF_MISC                      0x0518
#define MT_EFUSE_DATA_BASE              0x0028
#define MT_EFUSE_DATA(_n)               (MT_EFUSE_DATA_BASE + ((_n) << 2))

#define MT_WPDMA_GLO_CFG_TX_DMA_EN      BIT(0)
#define MT_WPDMA_GLO_CFG_TX_DMA_BUSY    BIT(1)
#define MT_WPDMA_GLO_CFG_RX_DMA_EN      BIT(2)
#define MT_WPDMA_GLO_CFG_RX_DMA_BUSY    BIT(3)
#define MT_WPDMA_GLO_CFG_DMA_BURST_SIZE GENMASK(5, 4)
#define MT_WPDMA_GLO_CFG_TX_WRITEBACK_DONE BIT(6)
#define MT_WPDMA_GLO_CFG_BIG_ENDIAN     BIT(7)
#define MT_WPDMA_GLO_CFG_HDR_SEG_LEN    GENMASK(15, 8)

#define MT_INT_RX_DONE(_n)              BIT(_n)
#define MT_INT_TX_DONE(_n)              BIT((_n) + 4)
#define MT_INT_RX_DONE_ALL              GENMASK(1, 0)
#define MT_INT_TX_DONE_ALL              GENMASK(13, 4)
#define MT_INT_TX_STAT                  BIT(22)
#define MT_INT_PRE_TBTT                 BIT(21)
#define MT_INT_TBTT                     BIT(20)
#define MT_INT_GPTIMER                  BIT(24)

#define MT_INT_TIMER_CFG_PRE_TBTT       GENMASK(15, 0)
#define MT_INT_TIMER_CFG_GP_TIMER       GENMASK(31, 16)
#define MT_INT_TIMER_EN_PRE_TBTT_EN     BIT(0)
#define MT_INT_TIMER_EN_GP_TIMER_EN     BIT(1)

#define MT_MAC_SYS_CTRL_ENABLE_TX       BIT(2)
#define MT_MAC_SYS_CTRL_ENABLE_RX       BIT(3)
#define MT_MAC_SYS_CTRL_RESET_CSR       BIT(0)
#define MT_MAC_SYS_CTRL_RESET_BBP       BIT(1)

#define MT_MAC_STATUS_TX                BIT(0)
#define MT_MAC_STATUS_RX                BIT(1)

#define MT_BEACON_TIME_CFG_INTVAL       GENMASK(15, 0)
#define MT_BEACON_TIME_CFG_TIMER_EN     BIT(16)
#define MT_BEACON_TIME_CFG_TBTT_EN      BIT(19)
#define MT_BEACON_TIME_CFG_BEACON_TX    BIT(20)
#define MT_BEACON_TIME_CFG_SYNC_MODE    GENMASK(18, 17)

#define MT_RX_FILTR_CFG_CRC_ERR         BIT(0)
#define MT_RX_FILTR_CFG_PHY_ERR         BIT(1)
#define MT_RX_FILTR_CFG_PROMISC         BIT(2)
#define MT_RX_FILTR_CFG_OTHER_BSS       BIT(3)
#define MT_RX_FILTR_CFG_BA              BIT(14)

#define MT_TX_RTS_CFG_THRESH            GENMASK(23, 8)
#define MT_PROT_CFG_CTRL                GENMASK(17, 16)
#define MT_PROT_CFG_RTS_THRESH          BIT(26)
#define MT_PROT_CFG_RATE                GENMASK(15, 0)

#define MT_TX_TIMEOUT_CFG_ACKTO         GENMASK(15, 8)

#define MT_TX_PIN_CFG_TXANT             GENMASK(3, 0)
#define MT_TX_PIN_CFG_RXANT             GENMASK(11, 8)
#define MT_TX_PIN_RFTR_EN               BIT(16)
#define MT_TX_PIN_TRSW_EN               BIT(18)

#define MT_TX_BAND_CFG_5G               BIT(1)
#define MT_TX_BAND_CFG_2G               BIT(2)
#define MT_TX_BAND_CFG_UPPER_40M        BIT(0)

#define MT_LED_CTRL_REPLAY(_n)          BIT(0 + (8 * (_n)))
#define MT_LED_CTRL_POLARITY(_n)        BIT(1 + (8 * (_n)))
#define MT_LED_CTRL_KICK(_n)            BIT(7 + (8 * (_n)))
#define MT_LED_STATUS_ON                GENMASK(23, 16)
#define MT_LED_STATUS_OFF               GENMASK(31, 24)
#define MT_LED_STATUS_DURATION          GENMASK(15, 8)

#define MT_TX_STAT_FIFO_VALID           BIT(0)
#define MT_TX_STAT_FIFO_SUCCESS         BIT(5)
#define MT_TX_STAT_FIFO_AGGR            BIT(6)
#define MT_TX_STAT_FIFO_ACKREQ          BIT(7)
#define MT_TX_STAT_FIFO_WCID            GENMASK(15, 8)
#define MT_TX_STAT_FIFO_EXT_PKTID       GENMASK(15, 8)
#define MT_TX_STAT_FIFO_RATE            GENMASK(31, 16)
#define MT_TX_STAT_FIFO_EXT_RETRY       GENMASK(7, 0)

#define MT_CH_TIME_CFG_TIMER_EN         BIT(0)
#define MT_CH_TIME_CFG_TX_AS_BUSY       BIT(1)
#define MT_CH_TIME_CFG_RX_AS_BUSY       BIT(2)
#define MT_CH_TIME_CFG_NAV_AS_BUSY      BIT(3)
#define MT_CH_TIME_CFG_EIFS_AS_BUSY     BIT(4)
#define MT_CH_TIME_CFG_CH_TIMER_CLR     GENMASK(9, 8)
#define MT_CH_CCA_RC_EN                 BIT(6)

#define MT_WMM_TXOP(_n)                 (MT_WMM_TXOP_BASE + (((_n) / 2) << 2))
#define MT_WMM_CWMIN_SHIFT(_n)          ((_n) * 4)
#define MT_WMM_CWMAX_SHIFT(_n)          ((_n) * 4)
#define MT_WMM_AIFSN_SHIFT(_n)          ((_n) * 4)
#define MT_WMM_TXOP_SHIFT(_n)           (((_n) & 1) * 16)

#define MT_EDCA_CFG_BASE                0x1300
#define MT_EDCA_CFG_AC(_n)              (MT_EDCA_CFG_BASE + ((_n) << 2))
#define MT_EDCA_CFG_AIFSN               GENMASK(11, 8)
#define MT_EDCA_CFG_CWMIN               GENMASK(15, 12)
#define MT_EDCA_CFG_CWMAX               GENMASK(19, 16)
#define MT_EDCA_CFG_TXOP                GENMASK(7, 0)

#define MT_MCU_MEMMAP_WLAN              0x410000
#define MT_MCU_MEMMAP_RF                0x80000000
#define MT_MCU_ILM_ADDR                 0x80000

#define MT_MCU_PCIE_REMAP_BASE4         0x074C

#define MT_MCU_MSG_LEN                  GENMASK(15, 0)
#define MT_MCU_MSG_CMD_SEQ              GENMASK(19, 16)
#define MT_MCU_MSG_CMD_TYPE             GENMASK(26, 20)
#define MT_MCU_MSG_PORT                 GENMASK(29, 27)
#define MT_MCU_MSG_TYPE_CMD             BIT(30)

#define MT_MCU_IVB_SIZE                 0x40

#define MT7610E_FIRMWARE                "mediatek/mt7610e.bin"
#define MT7650E_FIRMWARE                "mediatek/mt7650e.bin"

#define MT76X0_EEPROM_SIZE              512

#define MT_EE_NIC_CONF_0_TX_PATH        GENMASK(7, 4)
#define MT_EE_NIC_CONF_0_RX_PATH        GENMASK(3, 0)
#define MT_EE_NIC_CONF_0_BOARD_TYPE     GENMASK(13, 12)
#define MT_EE_NIC_CONF_0_PA_INT_2G      BIT(8)
#define MT_EE_NIC_CONF_0_PA_INT_5G      BIT(9)
#define MT_EE_NIC_CONF_0_PA_IO_CURRENT  BIT(10)
#define MT_EE_ANTENNA_DUAL              BIT(15)
#define MT_EE_NIC_CONF_1_HW_RF_CTRL     BIT(0)
#define MT_EE_NIC_CONF_1_TX_ALC_EN      BIT(13)
#define MT_EE_NIC_CONF_2_ANT_OPT        BIT(3)
#define MT_EE_NIC_CONF_2_ANT_DIV        BIT(4)

#define MT_EFUSE_CTRL_MODE              GENMASK(7, 6)
#define MT_EFUSE_CTRL_AOUT              GENMASK(5, 0)
#define MT_EFUSE_CTRL_AIN               GENMASK(25, 16)
#define MT_EFUSE_CTRL_KICK              BIT(30)

#define MT_RF_CSR_CFG_DATA              GENMASK(7, 0)
#define MT_RF_CSR_CFG_REG_ID            GENMASK(14, 8)
#define MT_RF_CSR_CFG_REG_BANK          GENMASK(17, 15)
#define MT_RF_CSR_CFG_WR                BIT(30)
#define MT_RF_CSR_CFG_KICK              BIT(31)

#define MT_TXOP_CTRL_CFG                0x1340
#define MT_TXOP_ED_CCA_EN               BIT(20)

#define MT_EXT_CCA_CFG_CCA0             GENMASK(1, 0)
#define MT_EXT_CCA_CFG_CCA1             GENMASK(3, 2)
#define MT_EXT_CCA_CFG_CCA2             GENMASK(5, 4)
#define MT_EXT_CCA_CFG_CCA3             GENMASK(7, 6)
#define MT_EXT_CCA_CFG_CCA_MASK         GENMASK(11, 8)

#define MT_AUTO_RSP_EN                  BIT(0)
#define MT_AUTO_RSP_PREAMB_SHORT        BIT(4)

#define MT_PROT_RATE_OFDM_24            0x2004
#define MT_PROT_RATE_DUP_OFDM_24        0x2084
#define MT_PROT_RATE_CCK_11             0x0003
#define MT_PROT_RATE_SGI_OFDM_24        0x2104
#define MT_PROT_CTRL_RTS_CTS            BIT(16)
#define MT_PROT_CTRL_CTS2SELF           BIT(17)

#define MT_MAC_BSSID_DW1_MBSS_MODE      GENMASK(17, 16)
#define MT_MAC_BSSID_DW1_MBEACON_N      GENMASK(20, 18)
#define MT_MAC_BSSID_DW1_MBSS_LOCAL_BIT BIT(21)
#define MT_MAC_ADDR_DW1_U2ME_MASK       GENMASK(23, 16)

#define MT_RXWI_RATE_INDEX              GENMASK(5, 0)
#define MT_RXWI_RATE_PHY                GENMASK(15, 13)
#define MT_RXWI_RATE_SGI                BIT(9)
#define MT_RXWI_RATE_STBC               BIT(10)
#define MT_RXWI_RATE_LDPC               BIT(6)
#define MT_RXWI_RATE_BW                 GENMASK(8, 7)

#define MT_RATE_INDEX_VHT_IDX           GENMASK(3, 0)
#define MT_RATE_INDEX_VHT_NSS           GENMASK(5, 4)

#define MT_RXINFO_BA                    BIT(0)
#define MT_RXINFO_CRC_ERR               BIT(0)
#define MT_RXINFO_UNICAST               BIT(4)
#define MT_RXINFO_NULL                  BIT(2)
#define MT_RXINFO_FRAG                  BIT(3)
#define MT_RXINFO_L2PAD                 BIT(14)
#define MT_RXINFO_RSSI                  BIT(13)
#define MT_RXINFO_AMPDU                 BIT(15)
#define MT_RXINFO_DECRYPT               BIT(16)
#define MT_RXINFO_PN_LEN                GENMASK(21, 19)

#define MT_RXWI_CTL_WCID                GENMASK(7, 0)
#define MT_RXWI_CTL_MPDU_LEN            GENMASK(29, 16)

#define MT_RXWI_SN                      GENMASK(15, 4)

#define MT_TXWI_ACK_CTL_REQ             BIT(0)
#define MT_TXWI_ACK_CTL_NSEQ            BIT(1)
#define MT_TXWI_ACK_CTL_BA_WINDOW       GENMASK(7, 2)

#define MT_TXWI_FLAGS_MMPS              BIT(1)
#define MT_TXWI_FLAGS_MPDU_DENSITY      GENMASK(7, 5)
#define MT_TXWI_FLAGS_AMPDU             BIT(4)
#define MT_TXWI_FLAGS_TS                BIT(3)

#define MT_WCID_TX_INFO_RATE            GENMASK(15, 0)
#define MT_WCID_TX_INFO_NSS             GENMASK(17, 16)
#define MT_WCID_TX_INFO_TXPWR_ADJ       GENMASK(25, 18)
#define MT_WCID_TX_INFO_SET             BIT(31)

#define MT_TX_ALC_CFG_0_CH_INIT_0       GENMASK(5, 0)
#define MT_TX_ALC_CFG_0_CH_INIT_1       GENMASK(13, 8)

#define MT_TX_PWR_ADJ                   GENMASK(3, 0)

#define MT_TX_SW_CFG0                   0x1330

#define MT_TX_RTS_CFG                   0x1344

#define MT_TXQ_FREE_THR                 32

#define MT_TXQ_VO                       IEEE80211_AC_VO
#define MT_TXQ_VI                       IEEE80211_AC_VI
#define MT_TXQ_BE                       IEEE80211_AC_BE
#define MT_TXQ_BK                       IEEE80211_AC_BK

#define MT_TX_HW_QUEUE_MCU              8
#define MT_TX_HW_QUEUE_MGMT             9

#define MT_RX_BUF_SIZE                  2048
#define MT76X02_RX_RING_SIZE            256
#define MT_MCU_RING_SIZE                32
#define MT_RX_HEADROOM                  32
#define MT_DMA_HDR_LEN                  4
#define MT_RING_SIZE_VAL                0x10

#define MT76_N_WCIDS                    1088

#define MT_WLAN_FUN_CTRL_FLAGS ( \
    MT_WLAN_FUN_CTRL_WLAN_EN | \
    MT_WLAN_FUN_CTRL_WLAN_CLK_EN | \
    MT_WLAN_FUN_CTRL_WLAN_RESET | \
    MT_WLAN_FUN_CTRL_WLAN_RESET_RF )

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
    uint32_t intr_status; /* pending interrupts */
    uint32_t intr_mask;   /* enabled interrupts (shadow of MT_INT_MASK_CSR) */

    /* Hardware Register Shadows (The 'Identity' of the device) */
    struct {
        uint32_t asic_version;
        uint32_t mac_sys_ctrl;
        uint32_t int_mask_csr;
        uint32_t wpdma_glo_cfg;
        uint32_t wlan_fun_ctrl;
        uint32_t cmb_ctrl;
        uint32_t mac_status;
    } regs;

    /* Simple register backing store for MMIO space we care about */
    uint32_t mac_csr0;         /* 0x1000, used by mt76x02_wait_for_mac */
};


/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* Interrupt is raised when any enabled status bit is set. */
    uint32_t pending = s->intr_status & s->intr_mask;

    if (pending) {
        if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!msi_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

/* Device-initiated DMA logic based on driver access patterns
 *
 * The provided driver source only uses CPU-initiated MMIO accesses and
 * does not program any explicit DMA descriptors in this snippet, so we
 * do not implement active bus mastering here.
 */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    (void)s;
    (void)is_write;
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = 0;

    /* Only little-endian 32-bit accesses are expected by the driver */
    if (size != 4) {
        return 0;
    }

    switch (addr) {
    case MT_ASIC_VERSION:
        /* Driver reads ASIC revision for identification */
        val = s->regs.asic_version;
        break;
    case 0x1000: /* MAC_CSR0 used by mt76x02_wait_for_mac */
        val = s->mac_csr0;
        break;
    case MT_MAC_SYS_CTRL:
        val = s->regs.mac_sys_ctrl;
        break;
    case MT_INT_MASK_CSR:
        val = s->regs.int_mask_csr;
        break;
    case MT_WPDMA_GLO_CFG:
        val = s->regs.wpdma_glo_cfg;
        break;
    case MT_WLAN_FUN_CTRL:
        val = s->regs.wlan_fun_ctrl;
        break;
    case MT_CMB_CTRL:
        val = s->regs.cmb_ctrl;
        break;
    case MT_MAC_STATUS:
        val = s->regs.mac_status;
        break;
    default:
        /* For all other registers within BAR we just return 0 */
        val = 0;
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        return;
    }

    switch (addr) {
    case MT_INT_MASK_CSR:
        /* Driver programs interrupt mask via mt76_wr / mt76_set_irq_mask */
        s->regs.int_mask_csr = (uint32_t)val;
        s->intr_mask = (uint32_t)val;
        pcibase_update_irq(s);
        break;
    case MT_WPDMA_GLO_CFG:
        /* Driver sets / clears TX/RX DMA enable and polls busy bits. */
        s->regs.wpdma_glo_cfg = (uint32_t)val;
        break;
    case MT_MAC_SYS_CTRL:
        /* Driver enables/disables MAC TX/RX and may assert resets. */
        s->regs.mac_sys_ctrl = (uint32_t)val;
        break;
    case MT_WLAN_FUN_CTRL:
        /* Chip on/off controls. We just record the value. */
        s->regs.wlan_fun_ctrl = (uint32_t)val;
        break;
    case MT_CMB_CTRL:
        /* Clock/PLL ready bits etc. */
        s->regs.cmb_ctrl = (uint32_t)val;
        break;
    case 0x1000: /* MAC_CSR0 */
        s->mac_csr0 = (uint32_t)val;
        break;
    case MT_MAC_STATUS:
        /* Status register is read-only from device side in real HW; here
         * we allow writes to let the driver clear bits if it ever does so.
         */
        s->regs.mac_status = (uint32_t)val;
        break;
    default:
        /* All other registers are currently not modeled; ignore writes. */
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    (void)s;
    (void)addr;
    (void)size;
    return val;
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

    /* Initialize register defaults visible to the driver */
    s->regs.asic_version = 0x76100000; /* corresponds to chip 0x7610 */
    s->mac_csr0 = 0x1;                 /* non-zero, non-~0 value to satisfy wait_for_mac */
    s->regs.mac_sys_ctrl = 0;
    s->regs.int_mask_csr = 0;
    s->regs.wpdma_glo_cfg = 0;
    s->regs.wlan_fun_ctrl = 0;
    s->regs.cmb_ctrl = 0;
    s->regs.mac_status = 0;

    s->intr_status = 0;
    s->intr_mask = 0;

    pcibase_update_irq(s);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  MT76X0E_PCI_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  MT76X0E_PCI_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, MT76X0E_PCI_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization: driver maps BAR0 via pcim_iomap_regions */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x10000;
    s->bar_info[0].name = "mt76x0e-mmio";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = "";
    }
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = false;
    s->has_msix = false;

    /* Initialize internal state */
    pcibase_reset(DEVICE(pdev));
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

    (void)s;
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "mt76x0e_pci",
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
