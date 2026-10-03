/*
 * QEMU ath9k PCI device model (QEMU 8.2.10)
 * Based on Linux driver pci.c and register definitions.
 * This is a Phase 2 implementation.
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

#define TYPE_PCIBASE_DEVICE "ath9k_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_ATH9K 0x168C
#define PCI_DEVICE_ID_ATH9K 0x0023
#define PCI_CLASS_ATH9K PCI_CLASS_NETWORK_OTHER

/* Offsets from driver source */
#define AR_RTC_BASE             0x00020000
#define AR_CFG               0x0014
#define AR_CFG_HALT_REQ	     0x00000800
#define AR_CFG_HALT_ACK	     0x00001000
#define AR_CFG_SWRD          0x00000004
#define AR_CFG_SWRB          0x00000008
#define AR_CFG_SWRG          0x00000010
#define AR_CFG_SWTB          0x00000002
#define AR_CFG_SWTD          0x00000001
#define AR_CFG_AP_ADHOC_INDICATION 0x00000020
#define AR_CR                0x0008
#define AR_CR_RXE(_ah)       (AR_SREV_9300_20_OR_LATER(_ah) ? 0x0000000c : 0x00000004)
#define AR_CR_RXD            0x00000020
#define AR_RXDP              0x000C
#define AR_HP_RXDP           0x0074
#define AR_LP_RXDP           0x0078
#define AR_RXCFG             0x0034
#define AR_RXCFG_ZLFDMA      0x00000010
#define AR_RXCFG_DMASZ_MASK  0x00000007
#define AR_RXCFG_DMASZ_128B  5
#define AR_TXCFG             0x0030
#define AR_TXCFG_DMASZ_MASK  0x00000007
#define AR_TXCFG_DMASZ_128B  5
#define AR_TXCFG_ADHOC_BEACON_ATIM_TX_POLICY 0x00000800
#define AR_MIBC			0x0040
#define AR_MIBC_FMC		0x00000002
#define AR_MIBC_CMC		0x00000004
#define AR_MIBC_MCS		0x00000008
#define AR_MIBC_COW		0x00000001
#define AR_RCCNT		0x80f4
#define AR_CCCNT		0x80f8
#define AR_RFCNT		0x80f0
#define AR_TFCNT		0x80ec
#define AR_GTXTO    0x0064
#define AR_GTXTO_TIMEOUT_LIMIT      0xFFFF0000
#define AR_GEN_TIMERS(_i)                   (0x8200 + ((_i) << 2))
#define AR_NEXT_TBTT_TIMER                  AR_GEN_TIMERS(0)
#define AR_NEXT_DMA_BEACON_ALERT            AR_GEN_TIMERS(1)
#define AR_NEXT_SWBA                        AR_GEN_TIMERS(2)
#define AR_NEXT_TIM                         AR_GEN_TIMERS(4)
#define AR_NEXT_DTIM                        AR_GEN_TIMERS(5)
#define AR_NEXT_NDP_TIMER                   AR_GEN_TIMERS(7)
#define AR_BEACON_PERIOD                    AR_GEN_TIMERS(8)
#define AR_DMA_BEACON_PERIOD                AR_GEN_TIMERS(9)
#define AR_SWBA_PERIOD                      AR_GEN_TIMERS(10)
#define AR_TIM_PERIOD                       AR_GEN_TIMERS(12)
#define AR_DTIM_PERIOD                      AR_GEN_TIMERS(13)
#define AR_NDP_PERIOD                       AR_GEN_TIMERS(15)
#define AR_NDP2_PERIOD                      0x81a0
#define AR_NDP2_TIMER_MODE                  0x81c0
#define AR_SLP32_INC               0x824c
#define AR_TIMER_MODE                       0x8240
#define AR_FTRIG             0x000003F0
#define AR_IMR               0x00a0
#define AR_IMR_S0               0x00a4
#define AR_IMR_S1              0x00a8
#define AR_IMR_S2              0x00ac
#define AR_IMR_S5                   0x00b8
#define AR_IMR_RXOK          0x00000001
#define AR_IMR_RXDESC        0x00000002
#define AR_IMR_RXERR         0x00000004
#define AR_IMR_RXOK_HP	     0x00000001
#define AR_IMR_RXOK_LP	     0x00000002
#define AR_IMR_TXOK          0x00000040
#define AR_IMR_TXDESC        0x00000080
#define AR_IMR_TXERR         0x00000100
#define AR_IMR_TXURN         0x00000800
#define AR_IMR_RXORN         0x00000020
#define AR_IMR_TXMINTR       0x00080000
#define AR_IMR_RXMINTR       0x01000000
#define AR_IMR_BCNMISC       0x00800000
#define AR_IMR_TXEOL         0x00000400
#define AR_IMR_RXINTM        0x80000000
#define AR_IMR_TXINTM        0x40000000
#define AR_IMR_GENTMR        0x10000000
#define AR_IMR_S0_QCU_TXOK      0x000003FF
#define AR_IMR_S0_QCU_TXDESC    0x03FF0000
#define AR_IMR_S1_QCU_TXERR    0x000003FF
#define AR_IMR_S1_QCU_TXEOL    0x03FF0000
#define AR_IMR_S2_QCU_TXURN    0x000003FF
#define AR_IMR_S2_TIM          0x01000000
#define AR_IMR_S2_DTIM         0x20000000
#define AR_IMR_S2_DTIMSYNC     0x04000000
#define AR_IMR_S2_CABEND       0x02000000
#define AR_IMR_S2_CABTO        0x10000000
#define AR_IMR_S2_TSFOOR       0x40000000
#define AR_IMR_S2_GTT          0x00800000
#define AR_IMR_S2_CST          0x00400000
#define AR_IMR_S2_BB_WATCHDOG  0x00010000
#define AR_IMR_S5_GENTIMER_TRIG     0x0000FF80
#define AR_IMR_S5_GENTIMER_THRESH   0xFF800000
#define AR_IMR_S5_TIM_TIMER         0x00000010
#define AR_IER               0x0024
#define AR_IER_DISABLE       0x00000000
#define AR_ISR               0x0080
#define AR_INTCFG               0x005C
#define AR_INTCFG_MSI_RXINTM    0x00000004
#define AR_INTCFG_MSI_TXINTM    0x00000010
#define AR_INTCFG_MSI_TXOK      0x00000000
#define AR_INTCFG_MSI_RXOK      0x00000000
#define AR_INTCFG_MSI_TXMINTR   0x00000018
#define AR_INTCFG_MSI_RXMINTR   0x00000006
#define AR_RTC_RESET(_ah) \
	((AR_SREV_9100(_ah)) ? (AR_RTC_BASE + 0x0040) : 0x7040)
#define AR_RTC_STATUS(_ah) \
	((AR_SREV_9100(_ah)) ? (AR_RTC_BASE + 0x0044) : 0x7044)
#define AR_RTC_FORCE_WAKE(_ah) \
	((AR_SREV_9100(_ah)) ? (AR_RTC_BASE + 0x004c) : 0x704c)
#define AR_RTC_STATUS_M(_ah) \
	((AR_SREV_9100(_ah)) ? 0x0000003f : 0x0000000f)
#define AR_RTC_STATUS_ON        0x00000002
#define AR_RTC_STATUS_SHUTDOWN  0x00000001
#define AR_RTC_RESET_EN		(0x00000001)
#define AR_RTC_FORCE_WAKE_EN        0x00000001
#define AR_RTC_FORCE_WAKE_ON_INT    0x00000002
#define AR_RTC_RC(_ah) \
	((AR_SREV_9100(_ah)) ? (AR_RTC_BASE + 0x0000) : 0x7000)
#define AR_RTC_RC_M		0x00000003
#define AR_RTC_RC_MAC_COLD      0x00000002
#define AR_RTC_RC_MAC_WARM      0x00000001
#define AR_RTC_RC_COLD_RESET    0x00000004
#define AR_RTC_RC_WARM_RESET    0x00000008
#define AR_RTC_PLL_CONTROL(_ah) \
	((AR_SREV_9100(_ah)) ? (AR_RTC_BASE + 0x0014) : 0x7014)
#define AR_RTC_PLL_CONTROL2	0x703c
#define AR_RTC_SLEEP_CLK(_ah) \
	((AR_SREV_9100(_ah)) ? (AR_RTC_BASE + 0x0048) : 0x7048)
#define AR_RTC_DERIVED_CLK(_ah) \
	(AR_SREV_9100(_ah) ? (AR_RTC_BASE + 0x0038) : 0x7038)
#define AR_RTC_DERIVED_CLK_PERIOD    0x0000fffe
#define AR_RTC_FORCE_DERIVED_CLK    0x2
#define AR_RTC_REG_CONTROL0     0x7008
#define AR_RTC_REG_CONTROL1     0x700c
#define AR_RTC_REG_CONTROL1_SWREG_PROGRAM       0x00000001
#define AR_RTC_FORCE_SWREG_PRD      0x00000004
#define AR_RTC_9300_PLL_BYPASS       0x00010000
#define AR_RTC_9300_SOC_PLL_BYPASS           0x08000000
#define AR_INTR_SYNC_CAUSE(_ah)               (AR_SREV_9340(_ah) ? 0x4010 : 0x4028)
#define AR_INTR_SYNC_ENABLE(_ah)              (AR_SREV_9340(_ah) ? 0x4014 : 0x402c)
#define AR_INTR_ASYNC_ENABLE(_ah)                (AR_SREV_9340(_ah) ? 0x4024 : 0x403c)
#define AR_INTR_SYNC_MASK(_ah)                   (AR_SREV_9340(_ah) ? 0x401c : 0x4034)
#define AR_INTR_PRIO_SYNC_ENABLE(_ah)  (AR_SREV_9340(_ah) ? 0x4088 : 0x40c4)
#define AR_INTR_PRIO_ASYNC_ENABLE(_ah) (AR_SREV_9340(_ah) ? 0x4094 : 0x40d4)
#define AR_INTR_PRIO_SYNC_MASK(_ah)    (AR_SREV_9340(_ah) ? 0x4090 : 0x40cc)
#define AR_INTR_PRIO_ASYNC_MASK(_ah)   (AR_SREV_9340(_ah) ? 0x408c : 0x40c8)
#define AR_PCU_MISC                0x8120
#define AR_PCU_MISC_MODE2               0x8344
#define AR_PCU_MISC_MODE2_ENABLE_AGGWEP                0x00020000
#define AR_PCU_MISC_MODE2_NO_CRYPTO_FOR_NON_DATA_PKT   0x00000004
#define AR_PCU_MISC_MODE2_MGMT_CRYPTO_ENABLE           0x00000002
#define AR_PCU_MIC_NEW_LOC_ENA     0x00000004
#define AR_PCU_TX_ADD_TSF          0x00000008
#define AR_PCU_TXBUF_CTRL               0x8340
#define AR_PCU_TXBUF_CTRL_USABLE_SIZE   0x700
#define AR_9285_PCU_TXBUF_CTRL_USABLE_SIZE   0x380
#define AR_9340_PCU_TXBUF_CTRL_USABLE_SIZE   0x500
#define AR_PCU_CLEAR_VMF           0x01000000
#define AR_PCU_FORCE_QUIET_COLL    0x00040000
#define AR_PCU_BT_ANT_PREVENT_RX   0x00100000
#define AR_RX_FILTER        0x803C
#define AR_STA_ID0		0x8000
#define AR_STA_ID1		0x8004
#define AR_STA_ID1_SADH_MASK	0x0000ffff
#define AR_STA_ID1_PWR_SAV         0x00040000
#define AR_STA_ID1_ADHOC           0x00020000
#define AR_STA_ID1_STA_AP          0x00010000
#define AR_STA_ID1_KSRCH_MODE      0x10000000
#define AR_STA_ID1_PRESERVE_SEQNUM 0x20000000
#define AR_STA_ID1_BASE_RATE_11B   0x02000000
#define AR_STA_ID1_RTS_USE_DEF     0x00800000
#define AR_BSS_ID0          0x8008
#define AR_BSS_ID1          0x800C
#define AR_BSS_ID1_AID_S     16
#define AR_BSSMSKU		0x80e4
#define AR_BSSMSKL		0x80e0
#define AR_TSF_L32          0x804c
#define AR_TSF_U32          0x8050
#define AR_KEYTABLE_0           0x8800
#define AR_KEYTABLE(_n)         (AR_KEYTABLE_0 + ((_n)*32))
#define AR_KEYTABLE_TYPE(_n)    (AR_KEYTABLE(_n) + 20)
#define AR_KEYTABLE_TYPE_CLR    0x00000007
#define AR_KEYTABLE_TYPE_TKIP   0x00000004
#define AR_KEYTABLE_KEY0(_n)    (AR_KEYTABLE(_n) + 0)
#define AR_KEYTABLE_KEY1(_n)    (AR_KEYTABLE(_n) + 4)
#define AR_KEYTABLE_KEY2(_n)    (AR_KEYTABLE(_n) + 8)
#define AR_KEYTABLE_KEY3(_n)    (AR_KEYTABLE(_n) + 12)
#define AR_KEYTABLE_KEY4(_n)    (AR_KEYTABLE(_n) + 16)
#define AR_KEYTABLE_MAC0(_n)    (AR_KEYTABLE(_n) + 24)
#define AR_KEYTABLE_MAC1(_n)    (AR_KEYTABLE(_n) + 28)
#define AR_KEYTABLE_SIZE            128
#define AR_RSSI_THR          0x8018
#define AR_RSSI_THR_BM_THR   0x0000FF00
#define AR_DEF_ANTENNA      0x8058
#define AR_TIME_OUT         0x8014
#define AR_TIME_OUT_ACK      0x00003FFF
#define AR_TIME_OUT_CTS      0x3FFF0000
#define AR_TXSIFS              0x81d0
#define AR_TXSIFS_ACK_SHIFT    0x00007000
#define AR_TXSIFS_TIME         0x000000FF
#define AR_USEC              0x801c
#define AR_USEC_USEC         0x0000007F
#define AR_USEC_TX_LAT       0x007FC000
#define AR_USEC_RX_LAT       0x1F800000
#define AR_USEC_ASYNC_FIFO   0x12E00074
#define AR_D_GBL_IFS_EIFS         0x10b0
#define AR_D_GBL_IFS_EIFS_ASYNC_FIFO 363
#define AR_D_GBL_IFS_SIFS         0x1030
#define AR_D_GBL_IFS_SLOT         0x1070
#define AR_D_GBL_IFS_MISC        0x10f0
#define AR_D_GBL_IFS_MISC_IGNORE_BACKOFF        0x10000000
#define AR_D_LCL_IFS_CWMIN       0x000003FF
#define AR_D_LCL_IFS_CWMAX       0x000FFC00
#define AR_D_LCL_IFS_AIFS        0x0FF00000
#define AR_DLCL_IFS(_i)   (AR_D0_LCL_IFS + ((_i)<<2))
#define AR_D0_LCL_IFS     0x1040
#define AR_DRETRY_LIMIT(_i)   (AR_D0_RETRY_LIMIT + ((_i)<<2))
#define AR_D0_RETRY_LIMIT     0x1080
#define AR_D_RETRY_LIMIT_FR_SH       0x0000000F
#define AR_D_RETRY_LIMIT_STA_SH      0x00003F00
#define AR_D_RETRY_LIMIT_STA_LG      0x000FC000
#define AR_DCHNTIME(_i)   (AR_D0_CHNTIME + ((_i)<<2))
#define AR_D0_CHNTIME     0x10c0
#define AR_D_CHNTIME_DUR         0x000FFFFF
#define AR_D_CHNTIME_EN          0x00100000
#define AR_DMISC(_i)      (AR_D0_MISC + ((_i)<<2))
#define AR_D0_MISC        0x1100
#define AR_D_MISC_BEACON_USE          0x00010000
#define AR_D_MISC_CW_BKOFF_EN         0x00001000
#define AR_D_MISC_FRAG_WAIT_EN        0x00000100
#define AR_D_MISC_FRAG_BKOFF_EN       0x00000200
#define AR_D_MISC_ARB_LOCKOUT_CNTRL   0x00060000
#define AR_D_MISC_ARB_LOCKOUT_CNTRL_S 17
#define AR_D_MISC_ARB_LOCKOUT_CNTRL_GLOBAL   2
#define AR_D_MISC_POST_FR_BKOFF_DIS   0x00200000
#define AR_QMISC(_i)       (AR_Q0_MISC + ((_i)<<2))
#define AR_Q0_MISC         0x09c0
#define AR_Q_MISC_BEACON_USE              0x00000080
#define AR_Q_MISC_DCU_EARLY_TERM_REQ      0x00000800
#define AR_Q_MISC_RDYTIME_EXP_POLICY      0x00000200
#define AR_Q_MISC_CBR_INCR_DIS0           0x00000040
#define AR_Q_MISC_CBR_INCR_DIS1           0x00000020
#define AR_Q_MISC_CBR_EXP_CNTR_LIMIT_EN   0x00000100
#define AR_Q_MISC_FSP_CBR                 1
#define AR_Q_MISC_FSP_DBA_GATED           2
#define AR_QRDYTIMECFG(_i)       (AR_Q0_RDYTIMECFG + ((_i)<<2))
#define AR_Q0_RDYTIMECFG         0x0900
#define AR_Q_RDYTIMECFG_EN         0x01000000
#define AR_Q_RDYTIMECFG_DURATION   0x00FFFFFF
#define AR_QCBRCFG(_i)      (AR_Q0_CBRCFG + ((_i)<<2))
#define AR_Q0_CBRCFG         0x08c0
#define AR_Q_CBRCFG_INTERVAL     0x00FFFFFF
#define AR_Q_CBRCFG_OVF_THRESH   0xFF000000
#define AR_Q_TXE             0x0840
#define AR_Q_TXD             0x0880
#define AR_Q_TXD_M           0x000003FF
#define AR_QTXDP(_i)    (AR_Q0_TXDP + ((_i)<<2))
#define AR_Q0_TXDP           0x0800
#define AR_Q_STS_PEND_FR_CNT          0x00000003
#define AR_QSTS(_i)       (AR_Q0_STS + ((_i)<<2))
#define AR_Q0_STS         0x0a00
#define AR_Q_STATUS_RING_START	0x830
#define AR_Q_STATUS_RING_END	0x834
#define AR_Q_DESC_CRCCHK    0xa44
#define AR_Q_DESC_CRCCHK_EN 1
#define AR_DQCUMASK(_i)   (AR_D0_QCUMASK + ((_i)<<2))
#define AR_D0_QCUMASK     0x1000
#define AR_NUM_QCU      10
#define AR_NUM_DCU      10
#define AR_RXBP_THRESH       0x0018
#define AR_RXBP_THRESH_HP    0x0000000f
#define AR_RXBP_THRESH_LP    0x00003f00
#define AR_RXFIFO_CFG          0x8114
#define AR_PHY_ERR         0x810c
#define AR_PHY_ERR_1           0x812c
#define AR_PHY_ERR_2           0x8134
#define AR_PHY_ERR_MASK_1      0x8130
#define AR_PHY_ERR_MASK_2      0x8138
#define AR_PHY_ERR_OFDM_TIMING 0x00020000
#define AR_PHY_ERR_CCK_TIMING  0x02000000
#define AR_PHY_ERR_RADAR       0x00000020
#define AR_PHY_AGC_CONTROL(_ah)			(AR_SREV_9300_20_OR_LATER(_ah) ? AR9003_PHY_AGC_CONTROL : AR9002_PHY_AGC_CONTROL)
#define AR9002_PHY_AGC_CONTROL			0x9860
#define AR9003_PHY_AGC_CONTROL			AR9300_SM_BASE + 0xc4
#define AR_PHY_AGC_CONTROL_NF			0x00000002
#define AR_PHY_AGC_CONTROL_ENABLE_NF		0x00008000
#define AR_PHY_AGC_CONTROL_NO_UPDATE_NF		0x00020000
#define AR_PHY_PLL_CONTROL 0x16180
#define AR_PHY_PLL_MODE 0x16184
#define AR_PHY_CCA_FILTERWINDOW_LENGTH          5
#define AR_BTCOEX_CTRL                                  0x18ac
#define AR_BTCOEX_CTRL_MCI_MODE_EN                      0x00000004
#define AR_BTCOEX_CTRL_BT_OWN_SPDT_CTRL                 0x00000002
#define AR_BTCOEX_CTRL_SPDT_ENABLE                      0x00000001
#define AR_BTCOEX_CTRL_ONE_STEP_LOOK_AHEAD_EN           0x00000020
#define AR_BTCOEX_CTRL_TIME_TO_NEXT_BT_THRESH_EN        0x00000040
#define AR_BTCOEX_CTRL_AGGR_THRESH                      0x00007000
#define AR_BTCOEX_CTRL_REDUCE_TXPWR                     0x20000000
#define AR_BTCOEX_CTRL_NUM_ANTENNAS                     0x00000180
#define AR_BTCOEX_CTRL_PA_SHARED                        0x00000010
#define AR_BTCOEX_CTRL_LNA_SHARED                       0x00000008
#define AR_BTCOEX_CTRL_1_CHAIN_ACK                      0x00100000
#define AR_BTCOEX_CTRL_1_CHAIN_BCN                      0x00080000
#define AR_BTCOEX_CTRL_RX_CHAIN_MASK                    0x00000E00
#define AR_BTCOEX_CTRL_WBTIMER_EN                       0x00000002
#define AR_BTCOEX_CTRL_AR9462_MODE                      0x00000001
#define AR_BTCOEX_CTRL2                                 0x1948
#define AR_BTCOEX_CTRL2_MAC_BB_OBS_SEL                  0x01000000
#define AR_BTCOEX_CTRL2_GPIO_OBS_SEL                    0x00800000
#define AR_BTCOEX_CTRL2_TXPWR_THRESH                    0x0007F800
#define AR_BTCOEX_CTRL2_RX_DEWEIGHT                     0x00400000
#define AR_BTCOEX_CTRL2_DESC_BASED_TXPWR_ENABLE         0x02000000
#define AR_BTCOEX_CTRL2_TX_CHAIN_MASK                   0x00380000
#define AR_BTCOEX_CTRL3                                 0x1a60
#define AR_BTCOEX_CTRL3_CONT_INFO_TIMEOUT               0x00000fff
#define AR_BTCOEX_WL_LNA                                0x1940
#define AR_BTCOEX_WL_LNA_TIMEOUT                        0x003FFFFF
#define AR_BTCOEX_RC                                    0x194c
#define AR_BTCOEX_WL_WEIGHTS0                           0x18b0
#define AR_BTCOEX_WL_WEIGHTS1                           0x18b4
#define AR_BTCOEX_WL_WEIGHTS2                           0x18b8
#define AR_BTCOEX_WL_WEIGHTS3                           0x18bc
#define AR_BTCOEX_MAX_TXPWR(_x)                         (0x18c0 + ((_x) << 2))
#define AR_BT_COEX_MODE            0x8170
#define AR_BT_COEX_MODE2		0x817c
#define AR_BT_COEX_MODE3			0x81d4
#define AR_BT_MODE                 0x00000c00
#define AR_BT_QUIET                0x00001000
#define AR_BT_COEX_WEIGHT          0x8174
#define AR_BT_COEX_WL_WEIGHTS1     0x81c4
#define AR_BT_COEX_BT_WEIGHTS(_i)  (0x83ac + (_i << 2))
#define AR_BT_COEX_WGHT		   0xff55
#define AR_BTCOEX_WL_WGHT          0xffff0000
#define AR_BTCOEX_BT_WGHT          0x0000ffff
#define AR9300_BT_WGHT             0xcccc4444
#define AR_STOMP_ALL_WLAN_WGHT	   0xfcfc
#define AR_STOMP_LOW_WLAN_WGHT	   0xa8a8
#define AR_STOMP_NONE_WLAN_WGHT	   0x0000
#define AR_MCI_INTERRUPT_EN                             0x182c
#define AR_MCI_INTERRUPT_RAW                            0x1828
#define AR_MCI_INTERRUPT_RX_MSG_RAW                     0x1838
#define AR_MCI_INTERRUPT_SW_MSG_DONE                    0x00000001
#define AR_MCI_INTERRUPT_DEFAULT (AR_MCI_INTERRUPT_SW_MSG_DONE         | \
				  AR_MCI_INTERRUPT_RX_INVALID_HDR      | \
				  AR_MCI_INTERRUPT_RX_HW_MSG_FAIL      | \
				  AR_MCI_INTERRUPT_RX_SW_MSG_FAIL      | \
				  AR_MCI_INTERRUPT_TX_HW_MSG_FAIL      | \
				  AR_MCI_INTERRUPT_TX_SW_MSG_FAIL      | \
				  AR_MCI_INTERRUPT_RX_MSG              | \
				  AR_MCI_INTERRUPT_REMOTE_SLEEP_UPDATE | \
				  AR_MCI_INTERRUPT_CONT_INFO_TIMEOUT)
#define AR_MCI_INTERRUPT_RX_MSG_DEFAULT (AR_MCI_INTERRUPT_RX_MSG_GPM           | \
                                         AR_MCI_INTERRUPT_RX_MSG_REMOTE_RESET  | \
                                         AR_MCI_INTERRUPT_RX_MSG_SYS_WAKING    | \
                                         AR_MCI_INTERRUPT_RX_MSG_SYS_SLEEPING  | \
                                         AR_MCI_INTERRUPT_RX_MSG_REQ_WAKE)
#define AR_MCI_INTERRUPT_RX_MSG_SCHD_INFO               0x00000020
#define AR_MCI_INTERRUPT_RX_MSG_LNA_INFO                0x00000200
#define AR_MCI_INTERRUPT_RX_MSG_CONT_INFO               0x00000008
#define AR_MCI_INTERRUPT_RX_MSG_CONT_NACK               0x00000004
#define AR_MCI_INTERRUPT_RX_MSG_LNA_CONTROL             0x00000002
#define AR_MCI_INTERRUPT_RX_MSG_CONT_RST                0x00000010
#define AR_MCI_INTERRUPT_RX_MSG_GPM                     0x00000100
#define AR_MCI_INTERRUPT_RX_MSG_REMOTE_RESET            0x00000001
#define AR_MCI_INTERRUPT_RX_MSG_SYS_SLEEPING            0x00000400
#define AR_MCI_INTERRUPT_RX_MSG_SYS_WAKING              0x00000800
#define AR_MCI_INTERRUPT_RX_MSG_REQ_WAKE                0x00001000
#define AR_MCI_INTERRUPT_RX_INVALID_HDR                 0x00000008
#define AR_MCI_INTERRUPT_CONT_INFO_TIMEOUT              0x80000000
#define AR_MCI_INTERRUPT_BT_PRI                         0x07fff800
#define AR_MCI_INTERRUPT_REMOTE_SLEEP_UPDATE            0x00000400
#define AR_MCI_INTERRUPT_RX_HW_MSG_FAIL                 0x00000010
#define AR_MCI_INTERRUPT_RX_SW_MSG_FAIL                 0x00000020
#define AR_MCI_INTERRUPT_TX_HW_MSG_FAIL                 0x00000080
#define AR_MCI_INTERRUPT_TX_SW_MSG_FAIL                 0x00000100
#define AR_MCI_INTERRUPT_MSG_FAIL_MASK (AR_MCI_INTERRUPT_RX_HW_MSG_FAIL | \
                                        AR_MCI_INTERRUPT_RX_SW_MSG_FAIL | \
                                        AR_MCI_INTERRUPT_TX_HW_MSG_FAIL | \
                                        AR_MCI_INTERRUPT_TX_SW_MSG_FAIL)
#define AR_MCI_COMMAND0                                 0x1800
#define AR_MCI_COMMAND0_HEADER                          0xFF
#define AR_MCI_COMMAND0_LEN                             0x1f00
#define AR_MCI_COMMAND0_DISABLE_TIMESTAMP               0x2000
#define AR_MCI_COMMAND2                                 0x1808
#define AR_MCI_COMMAND2_RESET_TX                        0x01
#define AR_MCI_COMMAND2_RESET_RX                        0x02
#define AR_MCI_COMMAND2_RESET_REQ_WAKEUP                0x400
#define AR_MCI_TX_CTRL                                  0x1810
#define AR_MCI_TX_CTRL_CLK_DIV                          0x03
#define AR_MCI_TX_CTRL_DISABLE_LNA_UPDATE               0x04
#define AR_MCI_MSG_ATTRIBUTES_TABLE                     0x1814
#define AR_MCI_MSG_ATTRIBUTES_TABLE_CHECKSUM            0xFFFF
#define AR_MCI_MSG_ATTRIBUTES_TABLE_INVALID_HDR         0xFFFF0000
#define AR_MCI_SCHD_TABLE_0                             0x1818
#define AR_MCI_GPM_0                                    0x1820
#define AR_MCI_GPM_1                                    0x1824
#define AR_MCI_GPM_WRITE_PTR                            0xFFFF0000
#define AR_MCI_RX_STATUS                                0x1844
#define AR_MCI_RX_LAST_SCHD_MSG_INDEX                   0x00000F00
#define AR_MCI_RX_REMOTE_SLEEP                          0x00001000
#define AR_MCI_BT_PRI3                                  0x1858
#define AR_MCI_BT_PRI                                   0x185c
#define AR_MCI_BT_PRI0                                  0x184c
#define AR_MCI_BT_PRI1                                  0x1850
#define AR_MCI_BT_PRI2                                  0x1854
#define AR_MCI_MISC                                     0x1a74
#define AR_MCI_MISC_HW_FIX_EN                           0x00000001
#define AR_MCI_SCHD_TABLE_2                             0x1a5c
#define AR_MCI_SCHD_TABLE_2_MEM_BASED                   0x00000001
#define AR_MCI_SCHD_TABLE_2_HW_BASED                    0x00000002
#define AR_MCI_DBG_CNT_CTRL                             0x1a78
#define AR_MCI_DBG_CNT_CTRL_BT_LINKID                   0x000007f8
#define AR_MCI_DBG_CNT_CTRL_ENABLE                      0x00000001
#define AR_MCI_COEX_WL_WEIGHTS(_i) (0x18b0 + (_i << 2))
#define AR_MCI_TX_PAYLOAD0                              0x1898
#define AR_PCIE_MSI(_ah)                         (AR_SREV_9340(_ah) ? 0x40d8 : \
						  (AR_SREV_9300_20_OR_LATER(_ah) ? 0x40a4 : 0x4094))
#define AR_PCIE_MSI_HW_DBI_WR_EN                 0x02000000
#define AR_PCIE_MSI_HW_INT_PENDING_ADDR_MSI_64   0xFFA0C9FF
#define AR_WA(_ah)			(AR_SREV_9340(_ah) ? 0x40c4 : 0x4004)
#define AR_WA_D3_L1_DISABLE		(1 << 14)
#define AR_GPIO_IN_OUT(_ah)                      (AR_SREV_9340(_ah) ? 0x4028 : 0x4048)
#define AR_GPIO_OE_OUT(_ah)                      (AR_SREV_9340(_ah) ? 0x4030 : \
						  (AR_SREV_9300_20_OR_LATER(_ah) ? 0x4050 : 0x404c))
#define AR_GPIO_INPUT_MUX1(_ah)                  (AR_SREV_9340(_ah) ? 0x4040 : \
						  (AR_SREV_9300_20_OR_LATER(_ah) ? 0x4060 : 0x4058))
#define AR_GPIO_INPUT_EN_VAL(_ah)                (AR_SREV_9340(_ah) ? 0x403c : \
						  (AR_SREV_9300_20_OR_LATER(_ah) ? 0x405c : 0x4054))
#define AR_GPIO_OUTPUT_MUX1(_ah)                 (AR_SREV_9340(_ah) ? 0x4048 : \
						  (AR_SREV_9300_20_OR_LATER(_ah) ? 0x4068 : 0x4060))
#define AR_GPIO_OUTPUT_MUX2(_ah)                 (AR_SREV_9340(_ah) ? 0x404c : \
						  (AR_SREV_9300_20_OR_LATER(_ah) ? 0x406c : 0x4064))
#define AR_GPIO_OUTPUT_MUX3(_ah)                 (AR_SREV_9340(_ah) ? 0x4050 : \
						  (AR_SREV_9300_20_OR_LATER(_ah) ? 0x4070 : 0x4068))
#define AR_GPIO_OUTPUT_MUX_AS_OUTPUT             0
#define AR_GPIO_OUTPUT_MUX_AS_WL_IN_TX           0x14
#define AR_GPIO_OUTPUT_MUX_AS_WL_IN_RX           0x13
#define AR_GPIO_OUTPUT_MUX_AS_BT_IN_TX           9
#define AR_GPIO_OUTPUT_MUX_AS_BT_IN_RX           8
#define AR_GPIO_OUTPUT_MUX_AS_MCI_WLAN_CLK       0x17
#define AR_GPIO_OUTPUT_MUX_AS_MCI_WLAN_DATA      0x16
#define AR_GPIO_OUTPUT_MUX_AS_MCI_BT_CLK         0x19
#define AR_GPIO_OUTPUT_MUX_AS_MCI_BT_DATA        0x18
#define AR_GPIO_OUTPUT_MUX_AS_RX_CLEAR_EXTERNAL  4
#define AR_GPIO_OUTPUT_MUX_AS_TX_FRAME           3
#define AR_GPIO_INPUT_EN_VAL_BT_PRIORITY_BB      0x00000400
#define AR_GPIO_INPUT_EN_VAL_BT_ACTIVE_BB        0x00001000
#define AR_GPIO_INPUT_MUX1_BT_PRIORITY           0x00000f00
#define AR_GPIO_INPUT_MUX1_BT_ACTIVE             0x000f0000
#define AR_GPIO_INPUT_EN_VAL_BT_PRIORITY_DEF     0x00000004
#define AR_GPIO_INPUT_EN_VAL_BT_FREQUENCY_DEF    0x00000008
#define AR_GPIO_OE_OUT_DRV_NO                    0x0
#define AR_GPIO_OE_OUT_DRV                       0x3
#define AR_GPIO_OE_OUT_DRV_ALL                   0x3
#define AR_GPIO_JTAG_DISABLE                     0x00020000
#define AR7010_GPIO_OUT                          0x52008
#define AR7010_GPIO_OE                           0x52000
#define AR7010_GPIO_OE_AS_OUTPUT                 0x0
#define AR7010_GPIO_OE_AS_INPUT                  0x1
#define AR7010_GPIO_OE_MASK                      0x1
#define AR_MACMISC           0x0058
#define AR_MACMISC_MISC_OBS_BUS_LSB     0x00007000
#define AR_MACMISC_MISC_OBS_BUS_MSB     0x00038000
#define AR_OBS(_ah)             (AR_SREV_9340(_ah) ? 0x405c : \
				 (AR_SREV_9300_20_OR_LATER(_ah) ? 0x4088 : 0x4080))
#define AR_OBS_BUS_CTRL     0x8068
#define AR_DIAG_SW                  0x8048
#define AR_DIAG_RX_ABORT            0x02000000
#define AR_DIAG_RX_DIS              0x00000020
#define AR_DIAG_FORCE_CH_IDLE_HIGH  0x00400000
#define AR_DIAG_OBS_PT_SEL1         0x000C0000
#define AR_DIAG_OBS_PT_SEL2         0x08000000
#define AR_OBS_BUS_1               0x806c
#define AR_SELFGEN_MASK         0x832c
#define AR_GLB_WLAN_UART_INTF_EN                        0x00020000
#define AR_GLB_DS_JTAG_DISABLE                          0x00040000
#define AR_AHB_MODE                           0x4024
#define AR_AHB_PREFETCH_RD_EN                 0x00000004
#define AR_AHB_CUSTOM_BURST_EN                0x000000C0
#define AR_AHB_CUSTOM_BURST_ASYNC_FIFO_VAL    3
#define AR_MAC_PCU_LOGIC_ANALYZER               0x8264
#define AR_MAC_PCU_LOGIC_ANALYZER_DISBUG20768   0x20000000
#define AR_MAC_PCU_ASYNC_FIFO_REG3			0x8358
#define AR_MAC_PCU_ASYNC_FIFO_REG3_DATAPATH_SEL		0x00000400
#define AR_MAC_PCU_ASYNC_FIFO_REG3_SOFT_RESET		0x80000000
#define AR_MAC_PCU_GEN_TIMER_TSF_SEL			0x83d8
#define AR_TSFOOR_THRESHOLD       0x813c
#define AR_SLEEP1               0x80d4
#define AR_SLEEP1_CAB_TIMEOUT   0xFFE00000
#define AR_SLEEP1_ASSUME_DTIM   0x00080000
#define AR_SLEEP2                   0x80d8
#define AR_SLEEP2_BEACON_TIMEOUT    0xFFE00000
#define AR_TIMT              0x0028
#define AR_TIMT_FIRST        0xffff0000
#define AR_TIMT_LAST         0x0000ffff
#define AR_RIMT              0x002C
#define AR_RIMT_FIRST        0xffff0000
#define AR_RIMT_LAST         0x0000ffff
#define AR_DATABUF_SIZE		0x0060
#define AR_DATABUF_SIZE_MASK	0x00000FFF
#define AR_CH0_BB_DPLL1		 0x16180
#define AR_CH0_BB_DPLL1_NINI	 0x07FC0000
#define AR_CH0_BB_DPLL1_REFDIV	 0xF8000000
#define AR_CH0_BB_DPLL1_NFRAC	 0x0003FFFF
#define AR_CH0_BB_DPLL2		     0x16184
#define AR_CH0_BB_DPLL2_PLL_PWD	     0x00010000
#define AR_CH0_BB_DPLL2_EN_NEGTRIG   0x00040000
#define AR_CH0_BB_DPLL2_LOCAL_PLL       0x40000000
#define AR_CH0_BB_DPLL2_OUTDIV	     0x0000E000
#define AR_CH0_BB_DPLL3          0x16188
#define AR_CH0_BB_DPLL3_PHASE_SHIFT	0x3F800000
#define AR_CH0_DDR_DPLL2         0x16244
#define AR_CH0_DDR_DPLL3         0x16248
#define AR_CH0_DPLL2_KI              0x3C000000
#define AR_CH0_DPLL2_KD              0x03F80000
#define AR_CH0_DPLL3_PHASE_SHIFT     0x3F800000
#define AR_DIRECT_CONNECT                              0x83a0
#define AR_CFG_LED                     0x1f04
#define AR_CFG_LED_ASSOC_CTL           0x00000c00
#define AR_CFG_LED_BLINK_THRESH_SEL    0x00000070
#define AR_CFG_LED_BLINK_SLOW          0x00000008
#define AR_CFG_LED_MODE_SEL            0x00000380
#define AR_CFG_SCLK_32KHZ              0x00000003
#define AR_RESET_TSF        0x8020
#define AR_RESET_TSF2_ONCE  0x02000000
#define AR_BEACON_CNT       0x8098
#define AR_RTS_OK           0x8088
#define AR_FCS_FAIL         0x8094
#define AR_ACK_FAIL         0x8090
#define AR_RTS_FAIL         0x808c
#define AR_FILT_OFDM           0x8124
#define AR_FILT_CCK            0x8128
#define AR_MCAST_FIL0       0x8040
#define AR_MCAST_FIL1       0x8044
#define AR_QUIET1          0x80fc
#define AR_QUIET1_QUIET_ACK_CTS_ENABLE 0x00020000
#define AR_QOS_NO_ACK              0x8108
#define AR_QOS_NO_ACK_BIT_OFF      0x00000070
#define AR_QOS_NO_ACK_BYTE_OFF     0x00000180
#define AR_QOS_NO_ACK_TWO_BIT      0x0000000f
#define AR_MIC_QOS_CONTROL 0x8118
#define AR_MIC_QOS_SELECT  0x811c
#define AR_TXOP_0_3    0x81f0
#define AR_TXOP_4_7    0x81f4
#define AR_TXOP_8_11   0x81f8
#define AR_TXOP_12_15  0x81fc
#define AR_TXOP_X          0x81ec
#define AR_TXOP_X_VAL      0x000000FF
#define AR_AES_MUTE_MASK1       0x8060
#define AR_AES_MUTE_MASK1_FC_MGMT 0xFFFF0000
#define AR_ENT_OTP_MIN_PKT_SIZE_DISABLE		0x00800000
#define AR_DBA_TIMER_EN                     0x00000002
#define AR_SWBA_TIMER_EN                    0x00000004
#define AR_TIM_TIMER_EN                     0x00000010
#define AR_DTIM_TIMER_EN                    0x00000020
#define AR_TBTT_TIMER_EN                    0x00000001
#define AR_DC_AP_STA_EN                                0x00000001
#define AR_9285_PCU_TXBUF_CTRL_USABLE_SIZE   0x380
#define AR_9340_PCU_TXBUF_CTRL_USABLE_SIZE   0x500
#define AR_PCU_TXBUF_CTRL_USABLE_SIZE   0x700
#define AR_SREV_9100(_ah) \
	(((_ah)->hw_version.macVersion == AR_SREV_VERSION_9100))
#define AR_SREV_9160_10_OR_LATER(_ah) \
	(((_ah)->hw_version.macVersion >= AR_SREV_VERSION_9160))
#define AR_SREV_9280(_ah) \
	(((_ah)->hw_version.macVersion == AR_SREV_VERSION_9280))
#define AR_SREV_9280_20_OR_LATER(_ah) \
	(((_ah)->hw_version.macVersion >= AR_SREV_VERSION_9280))
#define AR_SREV_9285(_ah) \
	(((_ah)->hw_version.macVersion == AR_SREV_VERSION_9285))
#define AR_SREV_9285_12_OR_LATER(_ah) \
	(((_ah)->hw_version.macVersion >= AR_SREV_VERSION_9285))
#define AR_SREV_9287(_ah) \
	(((_ah)->hw_version.macVersion == AR_SREV_VERSION_9287))
#define AR_SREV_9287_13_OR_LATER(_ah) \
	(((_ah)->hw_version.macVersion > AR_SREV_VERSION_9287) || \
	 (((_ah)->hw_version.macVersion == AR_SREV_VERSION_9287) && \
	  ((_ah)->hw_version.macRev >= AR_SREV_REVISION_9287_13)))
#define AR_SREV_9300(_ah) \
	(((_ah)->hw_version.macVersion == AR_SREV_VERSION_9300))
#define AR_SREV_9300_20_OR_LATER(_ah) \
	((_ah)->hw_version.macVersion >= AR_SREV_VERSION_9300)
#define AR_SREV_9330(_ah) \
	(((_ah)->hw_version.macVersion == AR_SREV_VERSION_9330))
#define AR_SREV_9340(_ah) \
	(((_ah)->hw_version.macVersion == AR_SREV_VERSION_9340))
#define AR_SREV_9340_13(_ah) \
	(AR_SREV_9340((_ah)) && \
	 ((_ah)->hw_version.macRev == AR_SREV_REVISION_9340_13))
#define AR_SREV_9340_13_OR_LATER(_ah) \
	(AR_SREV_9340((_ah)) && \
	 ((_ah)->hw_version.macRev >= AR_SREV_REVISION_9340_13))
#define AR_SREV_9462(_ah) \
	(((_ah)->hw_version.macVersion == AR_SREV_VERSION_9462))
#define AR_SREV_9462_20_OR_LATER(_ah) \
	(AR_SREV_9462(_ah) && \
	 ((_ah)->hw_version.macRev >= AR_SREV_REVISION_9462_20))
#define AR_SREV_9485(_ah) \
	(((_ah)->hw_version.macVersion == AR_SREV_VERSION_9485))
#define AR_SREV_9531(_ah) \
	(((_ah)->hw_version.macVersion == AR_SREV_VERSION_9531))
#define AR_SREV_9550(_ah) \
	(((_ah)->hw_version.macVersion == AR_SREV_VERSION_9550))
#define AR_SREV_9561(_ah) \
	(((_ah)->hw_version.macVersion == AR_SREV_VERSION_9561))
#define AR_SREV_9565(_ah) \
	(((_ah)->hw_version.macVersion == AR_SREV_VERSION_9565))
#define AR_SREV_9565_11_OR_LATER(_ah) \
	(AR_SREV_9565(_ah) && \
	 ((_ah)->hw_version.macRev >= AR_SREV_REVISION_9565_11))
#define AR_SREV_9580(_ah) \
	(((_ah)->hw_version.macVersion == AR_SREV_VERSION_9580) && \
	((_ah)->hw_version.macRev >= AR_SREV_REVISION_9580_10))
#define AR_SREV_9580_10_OR_LATER(_ah) \
	(AR_SREV_9580(_ah))
#define AR_SREV_9271(_ah) \
    (((_ah))->hw_version.macVersion == AR_SREV_VERSION_9271)
#define AR_SREV_SOC(_ah) \
	(AR_SREV_9340(_ah) || AR_SREV_9531(_ah) || AR_SREV_9550(_ah) || \
	 AR_SREV_9561(_ah))
#define AR_SREV_VERSION_9100		0x14
#define AR_SREV_VERSION_9160		0x40
#define AR_SREV_VERSION_9280		0x80
#define AR_SREV_VERSION_9285		0xC0
#define AR_SREV_VERSION_9287		0x180
#define AR_SREV_VERSION_9300		0x1c0
#define AR_SREV_VERSION_9330		0x200
#define AR_SREV_VERSION_9340		0x300
#define AR_SREV_VERSION_9462		0x280
#define AR_SREV_VERSION_9485		0x240
#define AR_SREV_VERSION_9531            0x500
#define AR_SREV_VERSION_9550		0x400
#define AR_SREV_VERSION_9561            0x600
#define AR_SREV_VERSION_9565            0x2C0
#define AR_SREV_VERSION_9580		0x1C0
#define AR_SREV_VERSION_9271		0x140
#define AR_SREV_VERSION_5416_PCI	0xD
#define AR_SREV_VERSION_5416_PCIE	0xC
#define AR_SREV_REVISION_9340_13	3
#define AR_SREV_REVISION_9287_13	3
#define AR_SREV_REVISION_9462_20	2
#define AR_SREV_REVISION_9565_11        2
#define AR_SREV_REVISION_9580_10	4

/* Additional defines from supplementary source */
#define AR_SREV 0x4020
#define AR_PHY_CHIP_ID 0x9818

/* Additional defines from driver */
#define PCI_VENDOR_ID_ATHEROS 0x168C
#define ATH9K_PM_OPS	(&ath9k_pm_ops)
#define DEFAULT_CACHELINE       32
#define ATH9K_NUM_CHANCTX  2
#define ATH9K_NUM_CHANNELS	38
#define ATH_KEYMAX	        128
#define PAPRD_GAIN_TABLE_ENTRIES	32
#define ATH9K_NUM_TX_QUEUES 10
#define AR5416_MAX_CHAINS               3
#define AR9300_MAX_CHAINS            3

/* Enums from driver */
enum ath9k_int {
	ATH9K_INT_RX = 0x00000001,
	ATH9K_INT_RXDESC = 0x00000002,
	ATH9K_INT_RXHP = 0x00000001,
	ATH9K_INT_RXLP = 0x00000002,
	ATH9K_INT_RXNOFRM = 0x00000008,
	ATH9K_INT_RXEOL = 0x00000010,
	ATH9K_INT_RXORN = 0x00000020,
	ATH9K_INT_TX = 0x00000040,
	ATH9K_INT_TXDESC = 0x00000080,
	ATH9K_INT_TIM_TIMER = 0x00000100,
	ATH9K_INT_MCI = 0x00000200,
	ATH9K_INT_BB_WATCHDOG = 0x00000400,
	ATH9K_INT_TXURN = 0x00000800,
	ATH9K_INT_MIB = 0x00001000,
	ATH9K_INT_RXPHY = 0x00004000,
	ATH9K_INT_RXKCM = 0x00008000,
	ATH9K_INT_SWBA = 0x00010000,
	ATH9K_INT_BMISS = 0x00040000,
	ATH9K_INT_BNR = 0x00100000,
	ATH9K_INT_TIM = 0x00200000,
	ATH9K_INT_DTIM = 0x00400000,
	ATH9K_INT_DTIMSYNC = 0x00800000,
	ATH9K_INT_GPIO = 0x01000000,
	ATH9K_INT_CABEND = 0x02000000,
	ATH9K_INT_TSFOOR = 0x04000000,
	ATH9K_INT_GENTIMER = 0x08000000,
	ATH9K_INT_CST = 0x10000000,
	ATH9K_INT_GTT = 0x20000000,
	ATH9K_INT_FATAL = 0x40000000,
	ATH9K_INT_GLOBAL = 0x80000000,
	ATH9K_INT_BMISC = ATH9K_INT_TIM |
		ATH9K_INT_DTIM |
		ATH9K_INT_DTIMSYNC |
		ATH9K_INT_TSFOOR |
		ATH9K_INT_CABEND,
	ATH9K_INT_COMMON = ATH9K_INT_RXNOFRM |
		ATH9K_INT_RXDESC |
		ATH9K_INT_RXEOL |
		ATH9K_INT_RXORN |
		ATH9K_INT_TXURN |
		ATH9K_INT_TXDESC |
		ATH9K_INT_MIB |
		ATH9K_INT_RXPHY |
		ATH9K_INT_RXKCM |
		ATH9K_INT_SWBA |
		ATH9K_INT_BMISS |
		ATH9K_INT_GPIO,
	ATH9K_INT_NOCARD = 0xffffffff
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

/* EEPROM simulation definitions */
#define AR5416_EEPROM_OFFSET    0x2000
#define AR5416_EEPROM_S         2
/* AR_EEPROM_STATUS_DATA offset for AR5416 (non-9300/9340) is 0x407c */
#define AR_EEPROM_STATUS_DATA   0x407c
#define AR_EEPROM_STATUS_DATA_BUSY        0x00010000
#define AR_EEPROM_STATUS_DATA_PROT_ACCESS 0x00040000
#define AR_EEPROM_STATUS_DATA_VAL        0x0000ffff
#define AH_WAIT_TIMEOUT 1000

/* Minimal EEPROM image for AR5416 PCIe */
static const uint16_t eeprom_image[] = {
    0xa55a, /* magic */
    0x000b, /* length? */
    0x0400, /* version? */
    0x0000,
    0x0000,
    0x0000,
    0x0000,
    0x0000,
    0x0000,
    0x0000,
    0x0000,
    0x0000,
    0x0000,
    0x0000,
    0x0000,
    0x0000,
};
#define EEPROM_SIZE (sizeof(eeprom_image) / sizeof(eeprom_image[0]))

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware Register Shadows */
    uint32_t mac_version;
    uint32_t mac_rev;
    /* other key registers as needed */
    uint32_t mmio[0x20000 / 4];  /* shadow for entire BAR0 region */

    /* EEPROM state */
    uint32_t eeprom_offset;   /* stores the raw offset value written to AR5416_EEPROM_OFFSET */
    bool eeprom_busy;

    /* DMA Context */
    struct {
        dma_addr_t base;
        uint32_t count;
    } dma;

    uint32_t status;
    bool in_reset;
    uint32_t pm_state;
    unsigned long driver_data;
};

/* Internal helper for status-triggered signaling */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    if (s->intr_status & s->intr_mask) {
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

/* Device-initiated DMA logic based on driver access patterns (none observed in pci.c) */

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    uint32_t reg_offset = addr & ~3;

    /* Handle special registers */
    switch (reg_offset) {
    case AR_SREV:
        return 0x000C0002; /* Corrected: AR5416 PCIe: macVersion=0xC, macRev=2 */
    case AR_PHY_CHIP_ID:
        return 0x02020000; /* dummy PHY ID, non-zero to indicate valid chip */
    case AR_ISR:
        return s->intr_status;
    case AR_IMR:
        return s->intr_mask;
    case AR_IER:
        /* IER enable; return some default */
        return 0x00000000;
    case AR_EEPROM_STATUS_DATA: {
        /* Return EEPROM data with status bits cleared */
        uint16_t eedata = 0;
        uint32_t word_index = s->eeprom_offset >> 1;  /* each EEPROM word is 2 bytes */
        if (word_index < EEPROM_SIZE) {
            eedata = eeprom_image[word_index];
        }
        /* Status: not busy, not protected, valid data in lower 16 bits */
        return eedata; /* only lower 16 bits; upper bits are zero (no busy/prot) */
    }
    default:
        /* Generic read from shadow array */
        if ((addr + size) <= sizeof(s->mmio)) {
            if (size == 1) {
                val = *(uint8_t *)((uint8_t *)s->mmio + addr);
            } else if (size == 2) {
                val = lduw_le_p((uint8_t *)s->mmio + addr);
            } else if (size == 4) {
                val = ldl_le_p((uint8_t *)s->mmio + addr);
            } else if (size == 8) {
                val = ldq_le_p((uint8_t *)s->mmio + addr);
            }
        }
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t reg_offset = addr & ~3;

    /* Handle special registers */
    switch (reg_offset) {
    case AR_ISR:
        /* Write-1-to-clear */
        s->intr_status &= ~(uint32_t)val;
        pcibase_update_irq(s);
        break;
    case AR_IMR:
        s->intr_mask = val;
        pcibase_update_irq(s);
        break;
    case AR_IER:
        /* Just save; we don't use it */
        break;
    case AR5416_EEPROM_OFFSET:
        /* The driver writes the word offset shifted left by AR5416_EEPROM_S (2) */
        s->eeprom_offset = val;  /* store raw value (byte address) */
        s->eeprom_busy = false;
        break;
    default:
        /* Generic write to shadow array */
        if ((addr + size) <= sizeof(s->mmio)) {
            if (size == 1) {
                *(uint8_t *)((uint8_t *)s->mmio + addr) = val;
            } else if (size == 2) {
                stw_le_p((uint8_t *)s->mmio + addr, val);
            } else if (size == 4) {
                stl_le_p((uint8_t *)s->mmio + addr, val);
            } else if (size == 8) {
                stq_le_p((uint8_t *)s->mmio + addr, val);
            }
        }
        break;
    }

    /* Fallback: if any special register not caught, store in shadow */
    if ((addr + size) <= sizeof(s->mmio)) {
        if (size == 1) {
            *(uint8_t *)((uint8_t *)s->mmio + addr) = val;
        } else if (size == 2) {
            stw_le_p((uint8_t *)s->mmio + addr, val);
        } else if (size == 4) {
            stl_le_p((uint8_t *)s->mmio + addr, val);
        } else if (size == 8) {
            stq_le_p((uint8_t *)s->mmio + addr, val);
        }
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* Not used */
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Not used */
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

    /* Reset state */
    s->intr_status = 0;
    s->intr_mask = 0;
    s->eeprom_offset = 0;
    s->eeprom_busy = false;
    memset(s->mmio, 0, sizeof(s->mmio));
    /* Set default MAC version */
    s->mmio[AR_SREV / 4] = 0x000C0002; /* Corrected: AR5416 PCIe: macVersion=0xC, macRev=2 */
    s->mmio[AR_PHY_CHIP_ID / 4] = 0x02020000;
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

    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_ATH9K);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_ATH9K);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_ATH9K);
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
    s->bar_info[0] = (BARInfo){
        .index = 0,
        .type = BAR_TYPE_MMIO,
        .size = 0x20000,  /* Typical AR5416 register space size */
        .name = "ath9k-mmio"
    };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = true;
    s->has_msix = false;
    if (msi_init(pdev, 0, 1, true, false, errp)) {
        return;
    }

    /* DMA config not needed */
    /* Timer config not needed */

    /* Initialize hardware registers */
    s->intr_status = 0;
    s->intr_mask = 0;
    s->eeprom_offset = 0;
    s->eeprom_busy = false;
    memset(s->mmio, 0, sizeof(s->mmio));
    /* Set default MAC version */
    s->mmio[AR_SREV / 4] = 0x000C0002; /* Corrected: AR5416 PCIe: macVersion=0xC, macRev=2 */
    s->mmio[AR_PHY_CHIP_ID / 4] = 0x02020000;
    /* Set other defaults if needed */
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

static const VMStateDescription vmstate_pcibase = {
    .name = "ath9k_pci",
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
