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
/* #HeadFile#: none */

#define TYPE_PCIBASE_DEVICE "rtw89_8922ae_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
/* #Related_Config_Info# */
/* Vendor and Device IDs from rtw89_8922ae_id_table first entry */
#define VENDOR_ID 0x10EC  /* PCI_VENDOR_ID_REALTEK */
#define DEVICE_ID 0x8922
/* Class ID set to Network Other (0x0280) */
#define CLASS_ID 0x0280

/* Hardware Register Offsets (extracted from driver source) */
#define R_BE_HAXI_INIT_CFG1 0xB000
#define R_BE_HAXI_EXP_CTRL_V1 0xB020
#define R_BE_TXBD_RWPTR_CLR1 0xB014
#define R_BE_RXBD_RWPTR_CLR1_V1 0xB018
#define R_BE_HAXI_DMA_BUSY1 0xB01C
#define R_BE_HAXI_DMA_STOP1 0xB010
#define R_BE_PCIE_HRPWM 0x30C0
#define R_BE_PCIE_CRPWM 0x30C4
#define R_BE_PCIE_MIT_CH_EN 0x3338
#define R_BE_WP_ADDR_H_SEL0_3_V1 0xB420
#define R_BE_PCIE_HIMR0 0x30B0
#define R_BE_PCIE_HISR 0x30B4
#define R_BE_HAXI_HIMR00 0xB0B0
#define R_BE_HAXI_HISR00 0xB0B4
#define R_BE_PCIE_DMA_IMR_0_V1 0x30B8
#define R_BE_PCIE_DMA_ISR 0x30BC
#define R_BE_HCI_OPT_CTRL 0x0074
#define R_BE_CLK_PM_EN BIT(0)
#define R_BE_IC_PWR_STATE 0x03F0
#define R_BE_WLMAC_PWR_STE_MASK GENMASK(9, 8)
#define R_BE_PCIE_FRZ_CLK 0x3004
#define R_BE_PCIE_DIS_L2__CTRL_LDO_HCI BIT(15)
#define R_BE_PCIE_DIS_L2_RTK_PERST BIT(14)
#define R_BE_L1_2_CTRL_HCILDO 0x3110
#define R_BE_PCIE_DIS_L1_2_CTRL_HCILDO BIT(0)
#define R_BE_EFUSE_CTRL_1_V1 0x0034
#define R_BE_EFUSE_CTRL_2_V1 0x00A4
#define R_BE_WL_BT_PWR_CTRL 0x0068
#define R_BE_BT_DISN_EN BIT(16)
#define R_BE_WHOLE_SYS_PWR_STE_MASK GENMASK(25, 16)
#define R_BE_SYS_WL_EFUSE_CTRL 0x000A
#define R_BE_AUTOLOAD_SUS BIT(5)
#define R_BE_SYS_CLK_CTRL 0x0008
#define R_BE_SYS_ISO_CTRL 0x0000
#define R_BE_SYS_PW_CTRL 0x0004
#define R_BE_SYS_ADIE_PAD_PWR_CTRL 0x0018
#define R_BE_ANAPAR_POW_MAC 0x0016
#define R_BE_WLLPS_CTRL 0x0090
#define R_BE_WLRESUME_CTRL 0x0094
#define R_BE_AFE_ON_CTRL1 0x0244
#define R_BE_PMC_DBG_CTRL2 0x00CC
#define R_BE_RSV_CTRL 0x001C
#define R_BE_SYS_SDIO_CTRL 0x0070
#define R_BE_GPIO_EXT_CTRL 0x0060
#define R_BE_GPIO8_15_FUNC_SEL 0x02D4
#define R_BE_WLAN_XTAL_SI_CTRL 0x0270
#define R_BE_EFUSE_CTRL 0x0030
#define R_BE_EF_ADDR_MASK GENMASK(15, 0)
#define R_BE_EF_RDY BIT(29)
#define R_BE_SYS_CHIPINFO 0x00FC
#define R_AX_SYS_CFG1 0x00F0
#define R_AX_GPIO0_7_FUNC_SEL 0x02D0
#define R_BE_HW_ID_MASK GENMASK(7, 0)
#define B_AX_CHIP_VER_MASK GENMASK(15, 12)
#define R_BE_SYS_PAGE_CLK_GATED 0x000C
#define R_BE_FEN_RST_ENABLE 0x0084
#define R_BE_DMAC_FUNC_EN 0x8400
#define R_BE_CMAC_SHARE_FUNC_EN 0x0E000
#define R_BE_PLATFORM_ENABLE 0x0088
#define R_BE_CMAC_FUNC_EN 0x10000
#define R_BE_CMAC_FUNC_EN_C1 0x14000
#define CMAC1_START_ADDR_BE 0x14000
#define CMAC1_END_ADDR_BE 0x17FFF
#define R_BE_DMAC_TABLE_CTRL 0x8420
#define R_BE_DMAC_SYS_CR32B 0x842C
#define R_BE_MEM_PWR_CTRL 0x00D0
#define R_BE_DMAC_CLK_EN 0x8404
#define R_BE_CK_EN 0x10004
#define R_BE_HAXI_MST_WDT_TIMEOUT_SEL_V1 0xB02C
#define R_BE_AXIDMA_WDT_TMR 0x3060
#define R_BE_WDT_R 0x3168
#define R_BE_AON_WDT_ENABLE BIT(0)
#define R_BE_WDT_W_ENABLE BIT(0)
#define R_BE_WDT_AR_ENABLE BIT(0)
#define R_BE_WDT_B_ENABLE BIT(0)
#define R_BE_AON_WDT_TMR 0x306C
#define R_BE_LA_MODE_WDT_ENABLE BIT(0)
#define R_BE_WDT_AR_TMR 0x3144
#define R_BE_AON_WDT 0x3068
#define R_BE_AXIDMA_WDT_ENABLE BIT(0)
#define R_BE_WLAN_WDT_ENABLE BIT(0)
#define R_BE_WDT_W 0x3158
#define R_BE_WDT_B_TMR 0x3164
#define R_BE_LOCAL_WDT 0x3080
#define R_BE_MDIO_WDT_TMR 0x3090
#define R_BE_LA_MODE_WDT 0x3098
#define R_BE_WDT_AW_TMR 0x3150
#define R_BE_WDT_B 0x3160
#define R_BE_WDT_AW_ENABLE BIT(0)
#define R_BE_MDIO_WDT_ENABLE BIT(0)
#define R_BE_WDT_AR 0x3140
#define R_BE_WDT_AW 0x314C
#define R_BE_LOCAL_WDT_ENABLE BIT(0)
#define R_BE_LOCAL_WDT_TMR 0x3084
#define R_BE_MDIO_WDT 0x308C
#define R_BE_WDT_R_ENABLE BIT(0)
#define R_BE_WDT_W_TMR 0x315C
#define R_BE_AXIDMA_WDT 0x305C
#define R_BE_LA_MODE_WDT_TMR 0x309C
#define R_BE_WLAN_WDT_TMR 0x3054
#define R_BE_WLAN_WDT 0x3050
#define R_BE_WDT_R_TMR 0x316C
#define R_BE_HAXI_IDCT_MSK 0xB0B8
#define R_BE_HAXI_IDCT 0xB0BC
#define R_BE_PCIE_SER_DBG 0x02FC
#define R_BE_SER_PL1_CTRL 0x34A8
#define R_BE_REG_PL1_ISR 0x34B4
#define R_BE_REG_PL1_MASK 0x34B0
#define R_BE_PL1_DBG_INFO 0x3120
#define R_BE_DLE_EMPTY0 0x8430
#define R_BE_DLE_EMPTY1 0x8434
#define R_BE_HALT_C2H 0x016C
#define R_BE_HALT_H2C 0x0168
#define R_BE_HALT_C2H_CTRL 0x0164
#define R_BE_HALT_H2C_CTRL 0x0160
#define R_BE_BOOT_REASON 0x01E6
#define R_BE_BOOT_DBG 0x78F0
#define R_BE_SYS_CFG5 0x0170
#define R_BE_SECURE_BOOT_MALLOC_INFO 0x0184
#define R_BE_GPIO_MUXCFG 0x0040
#define R_BE_WCPU_FW_CTRL 0x01E0
#define R_BE_DCPU_PLATFORM_ENABLE 0x0888
#define R_BE_UDM0 0x01F0
#define R_BE_UDM1 0x01F4
#define R_BE_UDM2 0x01F8
#define R_BE_SCOREBOARD 0x00AC
#define R_BE_SYSON_FSM_MON 0x00A0
#define R_BE_INTERRUPT_STS_REG 0xA3F4
#define R_BE_INTERRUPT_MASK_REG 0xA3F0
#define R_BE_PCIE_MIX_CFG 0x300C
#define R_BE_PCIE_PS_CTRL 0x3008
#define R_BE_LTR_CTRL_0 0x8410
#define R_BE_LTR_CFG_0 0x8414
#define R_BE_LTR_CFG_1 0x8418
#define R_BE_LTR_DECISION_CTRL_V1 0x3610
#define R_BE_LTR_LATENCY_IDX0_V1 0x3614
#define R_BE_LTR_LATENCY_IDX1_V1 0x3618
#define R_BE_LTR_LATENCY_IDX3_V1 0x3620
#define R_BE_PCIE_LAT_CTRL 0x3044
#define R_BE_L1_CLK_CTRL 0x3010
#define R_BE_HCI_FC_CTRL 0xB700
#define R_BE_CH_PAGE_CTRL 0xB704
#define R_BE_CH0_PAGE_CTRL 0xB718
#define R_BE_CH0_PAGE_INFO 0xB750
#define R_BE_PUB_PAGE_CTRL1 0xB790
#define R_BE_PUB_PAGE_CTRL2 0xB794
#define R_BE_PUB_PAGE_INFO1 0xB79C
#define R_BE_PUB_PAGE_INFO2 0xB7A0
#define R_BE_PUB_PAGE_INFO3 0xB78C
#define R_BE_WP_PAGE_CTRL1 0xB7A4
#define R_BE_WP_PAGE_CTRL2 0xB7A8
#define R_BE_WP_PAGE_INFO1 0xB7AC
#define R_BE_PLE_PKTBUF_CFG 0x9008
#define R_BE_WDE_PKTBUF_CFG 0x8C08
#define R_BE_PLE_BUFMGN_CTL 0x9010
#define R_BE_WDE_BUFMGN_CTL 0x8C10
#define R_BE_WD_CPUQ_OP_0 0x9810
#define R_BE_WD_CPUQ_OP_1 0x9814
#define R_BE_WD_CPUQ_OP_2 0x9818
#define R_BE_WD_CPUQ_OP_3 0x981C
#define R_BE_WD_CPUQ_OP_STATUS 0x9820
#define R_BE_PL_CPUQ_OP_0 0x9850
#define R_BE_PL_CPUQ_OP_1 0x9854
#define R_BE_PL_CPUQ_OP_2 0x9858
#define R_BE_PL_CPUQ_OP_3 0x985C
#define R_BE_PL_CPUQ_OP_STATUS 0x9860
#define R_BE_WD_BUF_REQ 0x9800
#define R_BE_WD_BUF_STATUS 0x9804
#define R_BE_PL_BUF_REQ 0x9840
#define R_BE_PL_BUF_STATUS 0x9844
#define R_BE_RXQ0_RXBD_IDX_V1 0xB160
#define R_BE_RPQ0_RXBD_IDX_V1 0xB164
#define R_BE_TX_SUB_BAND_VALUE 0x10088
#define R_BE_PREBKF_CFG_0 0x10338
#define R_BE_PREBKF_CFG_1 0x1033C
#define R_BE_SIFS_SETTING 0x10824
#define R_BE_TXRATE_CHK 0x10828
#define R_BE_TXCNT 0x1082C
#define R_BE_AGG_BK_0 0x10804
#define R_BE_AMPDU_AGG_LIMIT 0x10810
#define R_BE_AGG_LEN_HT_0 0x10814
#define R_BE_RCR 0x11400
#define R_BE_DRV_INFO_OPTION 0x11470
#define R_BE_RX_APPEND_MODE 0x8920
#define R_BE_RX_STOP 0x8914
#define R_BE_FWD_ERR 0x9C10
#define R_BE_FWD_ACTN0 0x9C14
#define R_BE_FWD_ACTN1 0x9C18
#define R_BE_FWD_ACTN2 0x9C1C
#define R_BE_FWD_TF0 0x9C20
#define R_BE_FWD_TF1 0x9C24
#define R_BE_DBG_WOW 0x0504
#define R_BE_DBG_WOW_READY 0x815E
#define R_BE_WOW_CTRL 0x9CB8
#define R_BE_PPDU_STAT 0x11440
#define R_BE_HW_PPDU_STATUS 0x9C30
#define R_BE_BT_PLT 0x1087C
#define R_BE_RX_PLCP_EXT_OPTION_1 0x11514
#define R_BE_RX_PLCP_EXT_OPTION_2 0x11518
#define R_BE_RX_FLTR_OPT 0x11420
#define R_BE_RX_FLTR_OPT_C1 0x15420
#define R_BE_MBSSID_DROP_0 0x1083C
#define R_BE_DTIM_CTRL_P0 0x10426
#define R_BE_BCN_ERR_FLAG_P0 0x10424
#define R_BE_P0MB_HGQ_WINDOW_CFG_0 0x10590
#define R_BE_BCN_DROP_ALL0 0x10560
#define R_BE_MBSSID_CTRL 0x10568
#define R_BE_BCN_PSR_RPT_P0 0x11484
#define R_BE_WMTX_MOREDATA_TSFT_STMP_CTL 0x10E08
#define R_BE_TSFTR_HIGH_P0 0x1043C
#define R_BE_BCN_FORCETX_P0 0x10418
#define R_BE_BCN_ERR_CNT_P0 0x10420
#define R_BE_PORT_CFG_P0 0x10400
#define R_BE_TSFTR_LOW_P0 0x10438
#define R_BE_PORT_HGQ_WINDOW_CFG 0x105A0
#define R_BE_TBTT_PROHIB_P0 0x10404
#define R_BE_TBTTERLYINT_CFG_P0 0x1040E
#define R_BE_BCN_CNT_TMR_P0 0x10434
#define R_BE_PTCL_BSS_COLOR_0 0x108A0
#define R_BE_BCN_SPACE_CFG_P0 0x10414
#define R_BE_TBTT_AGG_P0 0x10412
#define R_BE_PTCL_DBG_INFO 0x108F0
#define R_BE_PTCL_DBG 0x108F4
#define R_BE_TBTT_SHIFT_P0 0x10428
#define R_BE_BCNERLYINT_CFG_P0 0x1040C
#define R_BE_BCN_AREA_P0 0x10408
#define R_BE_PORT_0_TSF_SYNC 0x102A0
#define R_BE_FWS0ISR 0x0194
#define R_BE_FWS0IMR 0x0190
#define R_BE_MAC_LOOPBACK 0x11020
#define R_BE_RX_PLCP_HEADR 0x11404
#define R_BE_RX_SR_CTRL 0x1144A
#define R_BE_BSSID_SRC_CTRL 0x1144B
#define R_BE_ADDR_CAM_CTRL 0x11434
#define R_BE_RESPBA_CAM_CTRL 0x1143C
#define R_BE_PTCL_TX_CTN_SEL 0x108EC
#define R_BE_WMAC_NAV_CTL 0x11080
#define R_BE_SPECIAL_TX_SETTING 0x10820
#define R_BE_RX_CTRL_1 0x10C0C
#define R_BE_CCA_CFG_0 0x10340
#define R_BE_HE_SIFS_CHK_CCA_NAV 0x103B4
#define R_BE_HE_CTN_CHK_CCA_NAV 0x103C4
#define R_BE_TB_CHK_CCA_NAV 0x103AC
#define R_BE_SCH_EDCA_RST_CFG 0x102E4
#define R_BE_SCH_EXT_CTRL 0x103FC
#define R_BE_EDCA_BCNQ_PARAM 0x10324
#define R_BE_RLSRPT0_CFG0 0x9440
#define R_BE_RLSRPT0_CFG1 0x9444
#define R_BE_WDRLS_CFG 0x9408
#define R_BE_CMAC_ERR_IMR 0x10160
#define R_BE_CMAC_ERR_IMR_C1 0x14160
#define R_BE_DMAC_ERR_IMR 0x8520
#define R_BE_TXPKTCTL_B0_PRELD_CFG0 0x9F48
#define R_BE_TXPKTCTL_B0_PRELD_CFG1 0x9F4C
#define R_BE_TXPKTCTL_B1_PRELD_CFG0 0x9F88
#define R_BE_TXPKTCTL_B1_PRELD_CFG1 0x9F8C
#define R_BE_TXPKTCTL_MPDUINFO_CFG 0x9F10
#define R_BE_CTN_DRV_TXEN 0x10398
#define R_BE_PWR_COEX_CTRL 0x11A54
#define R_BE_PWR_REG_CTRL 0x11A50
#define R_BE_PWR_RATE_CTRL 0x11A2C
#define R_BE_PWR_BOOST 0x11A40
#define R_BE_PWR_RATE_OFST_CTRL 0x11A30
#define R_BE_PWR_MACID_LMT_BASE 0x0ED00
#define R_BE_PWR_MACID_PATH_BASE 0x0E500
#define R_BE_PWR_LMT 0x11FAC
#define R_BE_PWR_BY_RATE 0x11E00
#define R_BE_PWR_RU_LMT 0x12048
#define R_BE_PWR_REF_CTRL 0x11A20
#define P0_TXPWRB_BE 0xE61C
#define P1_TXPWRB_BE 0xE71C
#define R_BE_PWR_MODULE 0x11900
#define R_BE_GID_POSITION_EN0 0x10080
#define R_BE_GID_POSITION_EN1 0x10084
#define R_BE_GID_POSITION0 0x10070
#define R_BE_GID_POSITION1 0x10074
#define R_BE_GID_POSITION2 0x10078
#define R_BE_GID_POSITION3 0x1007C
#define R_BE_PTCL_IMR0 0x108C0
#define R_BE_PTCL_IMR1 0x108C8
#define R_BE_PTCL_IMR_2 0x108B8
#define R_BE_RX_ERR_IMR 0x114F8
#define R_BE_RX_ERROR_FLAG_IMR 0x10C04
#define R_BE_RX_ERROR_FLAG_IMR_1 0x10C88
#define R_BE_TX_ERROR_FLAG_IMR 0x10C70
#define R_BE_RESP_IMR 0x11884
#define R_BE_TRXPTCL_ERROR_INDICA_MASK 0x110BC
#define R_BE_SCHEDULE_ERR_IMR 0x103E8
#define R_BE_PHYINFO_ERR_IMR_V1 0x110F8
#define R_BE_C0_TXPWR_IMR 0x128E0
#define R_BE_PCIE_MIT0_CNT 0x3334
#define R_BE_PCIE_MIT0_TMR 0x3330
#define R_BE_PCIE_LAT_CTRL 0x3044
#define R_BE_H2CREG_CTRL 0x7160
#define R_BE_H2CREG_DATA0 0x7140
#define R_BE_H2CREG_DATA1 0x7144
#define R_BE_H2CREG_DATA2 0x7148
#define R_BE_H2CREG_DATA3 0x714C
#define R_BE_C2HREG_CTRL 0x7164
#define R_BE_C2HREG_DATA0 0x7150
#define R_BE_C2HREG_DATA1 0x7154
#define R_BE_C2HREG_DATA2 0x7158
#define R_BE_C2HREG_DATA3 0x715C
#define R_AX_C2HREG_DATA3_V1 0x715C
#define R_BE_HCI_FUNC_EN 0x7880
#define R_BSS_CLR_VLD_V2 0x4EBC
#define R_BSS_CLR_MAP_V2 0x4EB0
#define R_BE_FILTER_MODEL_ADDR 0x0C04
#define R_BE_BT_BREAK_TABLE 0x0E344
#define R_BE_GNT_SW_CTRL 0x0E348
#define R_BE_PTA_GNT_SW_CTRL 0x0E348
#define R_BE_BTC_COEX_WL_REQ_BE 0xE324
#define R_BE_PCIE_SER_DBG 0x02FC
#define R_BE_SER_PL1_CTRL 0x34A8
#define R_BE_REG_PL1_ISR 0x34B4
#define R_BE_REG_PL1_MASK 0x34B0
#define R_BE_SER_L1_DBG_CNT_0 0x8440
#define R_BE_SER_L1_DBG_CNT_1 0x8444
#define R_BE_SER_L1_DBG_CNT_2 0x8448
#define R_BE_SER_L1_DBG_CNT_3 0x844C
#define R_BE_SER_L1_DBG_CNT_4 0x8450
#define R_BE_SER_L1_DBG_CNT_5 0x8454
#define R_BE_SER_L1_DBG_CNT_6 0x8458
#define R_BE_SER_L1_DBG_CNT_7 0x845C
#define R_BE_SER_L0_DBG_CNT 0x10170
#define R_BE_SER_L0_DBG_CNT1 0x10174
#define R_BE_SER_L0_DBG_CNT2 0x10178
#define R_BE_SER_L0_DBG_CNT3 0x1017C
#define R_BE_SER_L0_DBG_CNT_C1 0x14170
#define R_BE_SER_L0_DBG_CNT1_C1 0x14174
#define R_BE_SER_DBG_INFO 0x8424
#define R_BE_MPDU_PROC 0x9C00
#define R_BE_CUT_AMSDU_CTRL 0x9C94
#define R_BE_RX_HDRTRNS 0x9CC0
#define R_BE_HDR_SHCUT_SETTING 0x9B00
#define R_BE_SS_CTRL 0xA310
#define R_BE_SS_CTRL_V1 0xA610
#define R_BE_MLO_INIT_CTL 0xA114
#define R_BE_CMAC_SHARE_ACQCHK_CFG_0 0x0E010
#define R_BE_NO_RX_ERR_CFG 0x841C
#define R_BE_DISP_FWD_WLAN_0 0x8938
#define R_BE_WDRLS_ERR_IMR 0x9430
#define R_BE_PLRLS_ERR_IMR 0xA218
#define R_BE_LA_ERRFLAG_IMR 0x9668
#define R_BE_BBRPT_COM_ERR_IMR 0x9608
#define R_BE_BBRPT_CHINFO_ERR_IMR 0x9628
#define R_BE_BBRPT_DFS_ERR_IMR 0x9638
#define R_BE_DISP_CPU_IMR 0x8878
#define R_BE_DISP_HOST_IMR 0x8874
#define R_BE_DISP_OTHER_IMR 0x8870
#define R_BE_PKTIN_ERR_IMR 0x9A20
#define R_BE_SEC_ERROR_IMR 0x9D2C
#define R_BE_MPDU_TX_ERR_IMR 0x9BF4
#define R_BE_MPDU_RX_ERR_IMR 0x9CF4
#define R_BE_CH_INFO_DBGFLAG_IMR 0x9688
#define R_BE_PLE_ERR_IMR 0x9038
#define R_BE_PLE_ERRFLAG1_IMR 0x90C0
#define R_BE_WDE_ERR_IMR 0x8C38
#define R_BE_WDE_ERR1_IMR 0x8CC0
#define R_BE_CPUIO_ERR_IMR 0x9888
#define R_BE_TXPKTCTL_B0_ERRFLAG_IMR 0x9F78
#define R_BE_TXPKTCTL_B1_ERRFLAG_IMR 0x9FB8
#define R_BE_MLO_ERR_IDCT_IMR 0xA128
#define R_BE_PL_AXIDMA_IDCT_MSK 0x0910
#define R_BE_PL_AXIDMA_IDCT 0x0914
#define R_BE_DISP_ERROR_ISR0 0x8804
#define R_BE_DISP_ERROR_ISR1 0x8808
#define R_BE_DISP_ERROR_ISR2 0x880C
#define R_BE_SER_DBG_INFO 0x8424
#define R_BE_RX_ERROR_FLAG 0x10C00
#define R_BE_TX_ERROR_FLAG 0x10C6C
#define R_BE_RX_ERROR_FLAG_1 0x10C84
#define R_BE_PTCL_ISR0 0x108C4
#define R_BE_PTCL_ISR1 0x108CC
#define R_BE_RX_ERR_ISR 0x114F4
#define R_BE_CMAC_ERR_ISR 0x10164
#define R_BE_TXPWR_ERR_FLAG 0x128E4
#define R_BE_SCHEDULE_ERR_ISR 0x103EC
#define R_BE_TRXPTCL_ERROR_INDICA 0x110C0
#define R_BE_PHYINFO_ERR_ISR 0x110FC
#define R_BE_TXPWR_ERR_IMR 0x128E0
#define R_BE_DBGSEL_TRXPTCL 0x110F4
#define R_AX_DMAC_ERR_ISR 0x8524
#define R_AX_HOST_DISPATCHER_ERR_IMR 0x8850
#define R_AX_SEC_ERROR_FLAG_IMR 0x9D2C
#define R_AX_TXPKTCTL_B0_ERRFLAG_ISR 0x9F7C
#define R_AX_RX_CTRL0 0xC808
#define R_AX_RX_CTRL1 0xC80C
#define R_AX_RX_CTRL2 0xC810
#define R_AX_RXDMA_PKT_INFO_0 0xC814
#define R_AX_RXDMA_PKT_INFO_1 0xC818
#define R_AX_RXDMA_PKT_INFO_2 0xC81C
#define R_AX_PLE_ERR_FLAG_ISR 0x903C
#define R_AX_RPQ_RXBD_IDX_V1 0x121C
#define R_AX_BBRPT_DFS_ERR_ISR 0x963C
#define R_AX_BBRPT_COM_ERR_IMR_ISR 0x960C
#define R_AX_SEC_ENG_CTRL 0x9D00
#define R_AX_PLE_ERR_FLAG_CFG_NUM1 0x9034
#define R_AX_LA_ERRFLAG_ISR 0x966C
#define R_AX_WDE_ERR_FLAG_CFG_NUM1 0x8C34
#define R_AX_CPU_DISPATCHER_ERR_ISR 0x880C
#define R_AX_SEC_ERROR_FLAG 0x9D30
#define R_AX_OTHER_DISPATCHER_ERR_ISR 0x8804
#define R_AX_MPDU_TX_ERR_IMR 0x9BF4
#define R_AX_MPDU_RX_ERR_ISR 0x9CF0
#define R_AX_SEC_TRX_PKT_CNT 0x9D28
#define R_AX_PLE_ERR_IMR 0x9038
#define R_AX_SYS_STATUS1 0x00F4
#define R_AX_SEC_RX_DEBUG 0x9D24
#define R_AX_HAXI_IDCT_MSK 0x10B8
#define R_AX_HAXI_IDCT 0x10BC
#define R_AX_PLE_DBGERR_LOCKEN 0x9020
#define R_AX_CPU_DISPATCHER_ERR_IMR 0x8854
#define R_AX_OTHER_DISPATCHER_ERR_IMR 0x8858
#define R_AX_BBRPT_COM_ERR_ISR 0x960C
#define R_AX_WDRLS_ERR_IMR 0x9430
#define R_AX_SEC_CAM_RDATA 0x9D14
#define R_AX_SEC_CAM_WDATA 0x9D18
#define R_AX_SEC_CAM_ACCESS 0x9D10
#define R_AX_WD_CPUQ_OP_0 0x9810
#define R_AX_WD_CPUQ_OP_1 0x9814
#define R_AX_WD_CPUQ_OP_2 0x9818
#define R_AX_WD_CPUQ_OP_STATUS 0x981C
#define R_AX_PL_CPUQ_OP_0 0x9830
#define R_AX_PL_CPUQ_OP_1 0x9834
#define R_AX_PL_CPUQ_OP_2 0x9838
#define R_AX_PL_CPUQ_OP_STATUS 0x983C
#define R_AX_WDE_ERR_ISR 0x8C3C
#define R_AX_PKTIN_ERR_ISR 0x9A24
#define R_AX_SEC_ERR_IMR_ISR 0x991C
#define R_AX_WDRLS_ERR_ISR 0x9434
#define R_AX_PKTIN_ERR_IMR 0x9A20
#define R_AX_BBRPT_COM_ERR_IMR 0x9608
#define R_AX_PLE_ERRFLAG_MSG 0x9030
#define R_AX_SEC_TRX_BLK_CNT 0x9D2C
#define R_AX_PLE_DBGERR_STS 0x9024
#define R_AX_MPDU_TX_ERR_ISR 0x9BF0
#define R_AX_BBRPT_CHINFO_ERR_IMR 0x9628
#define R_AX_BBRPT_CHINFO_ERR_ISR 0x962C
#define R_AX_STA_SCHEDULER_ERR_IMR 0x9EF0
#define R_AX_SEC_ERROR_FLAG 0x9D30
#define R_AX_HOST_DISPATCHER_ERR_ISR 0x8808
#define R_AX_SEC_DEBUG 0x9D1C
#define R_AX_SEC_MPDU_PROC 0x9D04
#define R_AX_DBG_CTRL 0x0058
#define R_AX_DMAC_ERR_IMR 0x8520
#define R_AX_TXPKTCTL_B0_ERRFLAG_IMR 0x9F78
#define R_AX_TXPKTCTL_B1_ERRFLAG_IMR 0x9FB8
#define R_AX_TXPKTCTL_B1_ERRFLAG_ISR 0x9FBC
#define R_AX_DBG_SEL0 GENMASK(7, 0)
#define R_AX_DBG_SEL1 GENMASK(23, 16)
#define R_AX_SEL_0XC0_MASK GENMASK(17, 16)
#define R_AX_CMAC_REG_START 0xC000
#define R_AX_CMAC_REG_END 0xFFFF
#define R_AX_CK_EN 0xC004
#define R_AX_SER_DBG_INFO 0x8424
#define R_AX_PLE_DBG_FUN_INTF_CTL 0x9110
#define R_AX_PLE_DBG_FUN_INTF_DATA 0x9114
#define R_AX_WDE_DBG_FUN_INTF_CTL 0x8D10
#define R_AX_WDE_DBG_FUN_INTF_DATA 0x8D14

/* Newly defined registers */
#define R_BE_HIMR0 0x01A0
#define R_BE_HISR0 0x01A4

/* Bit definitions, masks, and enums extracted from driver source */
#define B_BE_TXDMA_EN BIT(4)
#define B_BE_RXDMA_EN BIT(5)
#define B_BE_RXQ_RXBD_MODE_MASK GENMASK(12, 11)
#define B_BE_MAX_TAG_NUM_MASK GENMASK(3, 0)
#define B_BE_STOP_AXI_MST BIT(7)
#define DMA_BUSY1_CHECK_BE (B_BE_CH0_BUSY | B_BE_CH1_BUSY | B_BE_CH2_BUSY | \
                            B_BE_CH3_BUSY | B_BE_CH4_BUSY | B_BE_CH5_BUSY | \
                            B_BE_CH6_BUSY | B_BE_CH7_BUSY | B_BE_CH8_BUSY | \
                            B_BE_CH9_BUSY | B_BE_CH10_BUSY | B_BE_CH11_BUSY | \
                            B_BE_CH12_BUSY | B_BE_CH13_BUSY | B_BE_CH14_BUSY)
#define B_BE_CH0_BUSY BIT(0)
#define B_BE_CH1_BUSY BIT(1)
#define B_BE_CH2_BUSY BIT(2)
#define B_BE_CH3_BUSY BIT(3)
#define B_BE_CH4_BUSY BIT(4)
#define B_BE_CH5_BUSY BIT(5)
#define B_BE_CH6_BUSY BIT(6)
#define B_BE_CH7_BUSY BIT(7)
#define B_BE_CH8_BUSY BIT(8)
#define B_BE_CH9_BUSY BIT(9)
#define B_BE_CH10_BUSY BIT(10)
#define B_BE_CH11_BUSY BIT(11)
#define B_BE_CH12_BUSY BIT(12)
#define B_BE_CH13_BUSY BIT(13)
#define B_BE_CH14_BUSY BIT(14)
#define B_BE_TX_STOP1_MASK (B_BE_STOP_CH0 | B_BE_STOP_CH1 | \
                            B_BE_STOP_CH2 | B_BE_STOP_CH3 | \
                            B_BE_STOP_CH4 | B_BE_STOP_CH5 | \
                            B_BE_STOP_CH6 | B_BE_STOP_CH7 | \
                            B_BE_STOP_CH8 | B_BE_STOP_CH9 | \
                            B_BE_STOP_CH10 | B_BE_STOP_CH11 | \
                            B_BE_STOP_CH12 | B_BE_STOP_CH13 | \
                            B_BE_STOP_CH14)
#define B_BE_STOP_CH0 BIT(0)
#define B_BE_STOP_CH1 BIT(1)
#define B_BE_STOP_CH2 BIT(2)
#define B_BE_STOP_CH3 BIT(3)
#define B_BE_STOP_CH4 BIT(4)
#define B_BE_STOP_CH5 BIT(5)
#define B_BE_STOP_CH6 BIT(6)
#define B_BE_STOP_CH7 BIT(7)
#define B_BE_STOP_CH8 BIT(8)
#define B_BE_STOP_CH9 BIT(9)
#define B_BE_STOP_CH10 BIT(10)
#define B_BE_STOP_CH11 BIT(11)
#define B_BE_STOP_CH12 BIT(12)
#define B_BE_STOP_CH13 BIT(13)
#define B_BE_STOP_CH14 BIT(14)
#define B_BE_TX_STOP1_MASK_V1 (B_BE_STOP_CH0 | B_BE_STOP_CH2 | \
                               B_BE_STOP_CH4 | B_BE_STOP_CH6 | \
                               B_BE_STOP_CH8 | B_BE_STOP_CH10 | \
                               B_BE_STOP_CH12)

/* BAR Information */
#define BAR0_SIZE 0x100000  /* 1 MiB, estimated from driver register usage */
#define BAR2_SIZE 0x1000    /* 4 KB for additional MMIO region */

/* Interrupt and DMA placeholders kept as feature referenced */

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
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t regs[0x100000 / 4]; /* 1 MiB of registers */

    /* DMA Context */
    /* #DMA_Info_Stru# */
    struct {
        uint64_t tx_ring_addr;
        uint64_t rx_ring_addr;
        /* etc. */
    } dma;

    /* Operational status flags */
    uint32_t status;

    /* State used to handle reset sequences */
    /* #Probe_Reset_Stru# */

    /* Power management state (D0-D3) */
    uint8_t power_state;

    /* #Other_Addition_Info_Stru# */
};

/* #Other_Addition_Info_Defin# */

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t top_status = s->regs[R_BE_PCIE_HISR / 4];
    uint32_t top_mask = s->regs[R_BE_PCIE_HIMR0 / 4];

    if (top_status & top_mask) {
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

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr < 0x100000) {
        unsigned index = addr / 4;
        uint32_t reg = s->regs[index];
        switch (size) {
        case 1:
            val = (reg >> ((addr & 3) * 8)) & 0xff;
            break;
        case 2:
            val = (reg >> ((addr & 2) ? 16 : 0)) & 0xffff;
            break;
        case 4:
            val = reg;
            break;
        case 8:
            if (index + 1 < ARRAY_SIZE(s->regs)) {
                val = (uint64_t)reg | ((uint64_t)s->regs[index + 1] << 32);
            }
            break;
        default:
            break;
        }
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* Handle special registers with side effects */
    if (size == 4) {
        switch (addr) {
        case R_BE_PCIE_HISR:
            s->regs[addr / 4] &= ~((uint32_t)val);
            pcibase_update_irq(s);
            return;
        case R_BE_PCIE_HIMR0:
            s->regs[addr / 4] = (uint32_t)val;
            pcibase_update_irq(s);
            return;
        case R_BE_HAXI_HISR00:
            s->regs[addr / 4] &= ~((uint32_t)val);
            return;
        case R_BE_HAXI_HIMR00:
            s->regs[addr / 4] = (uint32_t)val;
            return;
        case R_BE_PCIE_DMA_ISR:
            s->regs[addr / 4] &= ~((uint32_t)val);
            return;
        case R_BE_PCIE_DMA_IMR_0_V1:
            s->regs[addr / 4] = (uint32_t)val;
            return;
        case R_BE_HIMR0:
            s->regs[addr / 4] = (uint32_t)val;
            return;
        case R_BE_HISR0:
            s->regs[addr / 4] &= ~((uint32_t)val);
            return;
        default:
            break;
        }
    }

    /* Generic handling for all other registers */
    if (addr < 0x100000) {
        unsigned index = addr / 4;
        switch (size) {
        case 1: {
            unsigned shift = (addr & 3) * 8;
            uint32_t mask = 0xff << shift;
            s->regs[index] = (s->regs[index] & ~mask) | ((uint32_t)(val & 0xff) << shift);
            break;
        }
        case 2: {
            unsigned shift = (addr & 2) ? 16 : 0;
            uint32_t mask = 0xffff << shift;
            s->regs[index] = (s->regs[index] & ~mask) | ((uint32_t)(val & 0xffff) << shift);
            break;
        }
        case 4:
            s->regs[index] = (uint32_t)val;
            break;
        case 8:
            if (index + 1 < ARRAY_SIZE(s->regs)) {
                s->regs[index] = val & 0xffffffff;
                s->regs[index + 1] = (val >> 32) & 0xffffffff;
            }
            break;
        default:
            break;
        }
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

    memset(s->regs, 0, sizeof(s->regs));
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

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_VENDOR_ID, VENDOR_ID);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_ID, DEVICE_ID);
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
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = BAR0_SIZE;
    s->bar_info[0].name = "bar0";
    s->bar_info[1].index = 2;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = BAR2_SIZE;
    s->bar_info[1].name = "bar2";
    
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI or MSI-X initialization */
    s->has_msi = true;
    if (msi_init(pdev, 0, 1, true, false, errp)) {
        return;
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

static const VMStateDescription vmstate_pcibase = {
    .name = "rtw89_8922ae_pci",
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
