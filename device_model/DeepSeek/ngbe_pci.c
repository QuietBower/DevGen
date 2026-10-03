
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

#define TYPE_PCIBASE_DEVICE "ngbe_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */

/* Utility macros not present in QEMU */
#ifndef BIT
#define BIT(x) (1U << (x))
#endif
#ifndef GENMASK
#define GENMASK(high, low) (((1U << ((high) - (low) + 1)) - 1) << (low))
#endif
#ifndef FIELD_GET
#define FIELD_GET(_mask, _reg) (((_reg) & (_mask)) >> (__builtin_ctz(_mask)))
#endif
#ifndef FIELD_PREP
#define FIELD_PREP(_mask, _val) (((_val) << (__builtin_ctz(_mask))) & (_mask))
#endif

/* PCI IDs: first entry in ngbe_pci_tbl */
#define VENDOR_ID 0x0000 /* FIXME: need WANGXUN vendor ID */
#define DEVICE_ID 0x0100 /* NGBE_DEV_ID_EM_WX1860AL_W */
#define CLASS_ID  0x0200 /* PCI_CLASS_NETWORK_ETHERNET */

/* Hardware register offsets extracted from driver source defines */
#define WX_CFG_PORT_ST               0x14404
#define WX_GPIO_DDR                  0x14804
#define WX_GPIO_INTEN                0x14830
#define WX_GPIO_POLARITY             0x1483C
#define WX_GPIO_INTTYPE_LEVEL        0x14838
#define WX_PX_MISC_IEN               0x108
#define WX_PX_INTA                   0x110
#define WX_PX_MISC_IC                0x100
#define WX_PX_IC(_i)                 (0x120 + (_i) * 4)
#define WX_PX_IMS(_i)                (0x140 + (_i) * 4)
#define WX_PX_IMC(_i)                (0x150 + (_i) * 4)
#define WX_PX_ITR(_i)                (0x200 + (_i) * 4)
#define WX_PX_ITRSEL                 0x180
#define WX_PX_GPIE                   0x118
#define WX_PX_RR_CFG(_i)             (0x01010 + ((_i) * 0x40))
#define WX_PX_TR_CFG(_i)             (0x03010 + ((_i) * 0x40))
#define WX_PX_MPRC(_i)               (0x01020 + ((_i) * 0x40))
#define WX_PX_RR_CFG_RR_EN           BIT(0)
#define WX_CFG_PORT_CTL              0x14400
#define WX_CFG_PORT_CTL_PFRSTD       BIT(14)
#define WX_PSR_CTL                   0x15000
#define WX_PSR_CTL_SW_EN             BIT(18)
#define WX_RDB_PB_CTL                0x19000
#define WX_RDB_PB_CTL_RXEN           BIT(31)
#define WX_MAC_RX_CFG                0x11004
#define WX_MAC_RX_CFG_RE             BIT(0)
#define WX_PSR_MAC_SWC_AD_L          0x16200
#define WX_PSR_MAC_SWC_AD_H          0x16204
#define WX_PSR_MAC_SWC_IDX           0x16210
#define WX_PSR_MAC_SWC_VM_L          0x16208
#define WX_PSR_MAC_SWC_VM_H          0x1620C
#define WX_PSR_ETYPE_SWC(_i)         (0x15128 + ((_i) * 4))
#define WX_PSR_1588_CTL              0x15188
#define WX_PSR_1588_MSG              0x15120
#define WX_PSR_1588_STMPH            0x151A4
#define WX_TSC_1588_CTL              0x11F00
#define WX_TSC_1588_CTL_ENABLED      BIT(4)
#define WX_TSC_1588_AUX_CTL          0x11F28
#define WX_TSC_1588_AUX_CTL_EN_TS0   BIT(8)
#define WX_TSC_1588_AUX_CTL_EN_TT0   BIT(0)
#define WX_TSC_1588_AUX_CTL_EN_TT1   BIT(2)
#define WX_TSC_1588_AUX_CTL_PLSG     BIT(1)
#define WX_TSC_1588_INT_ST           0x11F20
#define WX_TSC_1588_INT_ST_TT1       BIT(5)
#define WX_TSC_1588_INT_EN           0x11F24
#define WX_TSC_1588_INT_EN_TT1       BIT(5)
#define WX_TSC_1588_TRGT_L(i)        (0x11F2C + ((i) * 8))
#define WX_TSC_1588_TRGT_H(i)        (0x11F30 + ((i) * 8))
#define WX_TSC_1588_SYSTIML          0x11F0C
#define WX_TSC_1588_SYSTIMH          0x11F10
#define WX_TSC_1588_STMPL            0x11F04
#define WX_TSC_1588_STMPH            0x11F08
#define WX_TSC_1588_INC              0x11F14
#define WX_MIS_RST                   0x1000C
#define WX_MIS_ST                    0x10028
#define WX_MIS_ST_MNG_INIT_DN        BIT(0)
#define WX_MIS_PWR                   0x10000
#define WX_SPI_STATUS                0x1010C
#define WX_SPI_STATUS_FLASH_BYPASS   BIT(31)
#define WX_SPI_ILDR_STATUS           0x10120
#define WX_SPI_CMD                   0x10104
#define WX_SPI_DATA                  0x10108
#define WX_SPI_CMD_READ_DWORD        0x1
#define WX_SPI_CMD_CLK(_v)           FIELD_PREP(GENMASK(27, 25), _v)
#define WX_SPI_CLK_DIV               0x3
#define WX_SPI_CMD_CMD(_v)           FIELD_PREP(GENMASK(30, 28), _v)
#define NGBE_CALSUM_CAP_STATUS       0x10224
#define NGBE_EEPROM_VERSION_STORE_REG 0x1022C
#define NGBE_EEPROM_VERSION_L        0x1D
#define NGBE_EEPROM_VERSION_H        0x1E
#define NGBE_SPI_ILDR_STATUS_PWRRST  BIT(1)
#define NGBE_SPI_ILDR_STATUS_PERST   BIT(0)
#define WX_MNG_MBOX                  0x1E100
#define WX_MNG_MBOX_CTL              0x1E044
#define WX_MNG_MBOX_CTL_FWRDY        BIT(2)
#define WX_MNG_MBOX_CTL_SWRDY        BIT(0)
#define WX_MNG_SWFW_SYNC             0x1E008
#define WX_MNG_SWFW_SYNC_SW_MB       BIT(2)
#define WX_MNG_SWFW_SYNC_SW_FLASH    BIT(3)
#define WX_FW2SW_MBOX                0x1E300
#define WX_SW2FW_MBOX                0x1E200
#define WX_SW2FW_MBOX_CMD            0x1E0A0
#define WX_SW2FW_MBOX_CMD_VLD        BIT(31)
#define WX_MNG_OS2BMC_CNT            0x1E094
#define WX_MNG_BMC2OS_CNT            0x1E090
#define WX_PSR_VLAN_TBL(_i)          (0x16000 + ((_i) * 4))
#define WX_PSR_VLAN_CTL              0x15088
#define WX_PSR_VLAN_CTL_VFE          BIT(30)
#define WX_PSR_VLAN_CTL_CFIEN        BIT(29)
#define WX_PSR_MC_TBL(_i)            (0x15200  + ((_i) * 4))
#define WX_PSR_UC_TBL(_i)            (0x15400 + ((_i) * 4))
#define WX_PSR_VM_L2CTL(_i)          (0x15600 + ((_i) * 4))
#define WX_PSR_VM_L2CTL_ROMPE        BIT(9)
#define WX_PSR_VM_L2CTL_BAM          BIT(11)
#define WX_PSR_VM_L2CTL_UPE          BIT(4)
#define WX_PSR_VM_L2CTL_MPE          BIT(12)
#define WX_PSR_VM_L2CTL_ROPE         BIT(10)
#define WX_PSR_VM_L2CTL_VACC         BIT(6)
#define WX_PSR_VM_L2CTL_AUPE         BIT(8)
#define WX_PSR_VM_L2CTL_VPE          BIT(7)
#define WX_PSR_VM_CTL                0x151B0
#define WX_PSR_VM_CTL_REPLEN         BIT(30)
#define WX_PSR_CTL_UPE               BIT(9)
#define WX_PSR_CTL_MFE               BIT(7)
#define WX_PSR_CTL_BAM               BIT(10)
#define WX_PSR_CTL_MPE               BIT(8)
#define WX_PSR_CTL_PCSD              BIT(13)
#define WX_PSR_CTL_MO_SHIFT          5
#define WX_PSR_CTL_MO                (0x3 << WX_PSR_CTL_MO_SHIFT)
#define WX_PSR_CTL_RSC_DIS           BIT(16)
#define WX_PSR_CTL_RSC_ACK           BIT(17)
#define WX_PSR_WKUP_CTL              0x15B80
#define WX_PSR_WKUP_CTL_MAG          BIT(1)
#define WX_PSR_MAX_SZ                0x15020
#define WX_PSR_LAN_FLEX_DW_L(_i)     (0x15C00 + ((_i) * 16))
#define WX_PSR_LAN_FLEX_DW_H(_i)     (0x15C04 + ((_i) * 16))
#define WX_PSR_MNG_FLEX_DW_L(_i)     (0x15A00 + ((_i) * 16))
#define WX_PSR_MNG_FLEX_DW_H(_i)     (0x15A04 + ((_i) * 16))
#define WX_PSR_LAN_FLEX_MSK(_i)      (0x15C08 + ((_i) * 16))
#define WX_PSR_MNG_FLEX_MSK(_i)      (0x15A08 + ((_i) * 16))
#define WX_PSR_LAN_FLEX_SEL          0x15B8C
#define WX_PSR_MNG_FLEX_SEL          0x1582C
#define WX_MAC_TX_CFG                0x11000
#define WX_MAC_TX_CFG_TE             BIT(0)
#define WX_MAC_RX_CFG_RE             BIT(0)
#define WX_MAC_RX_CFG_JE             BIT(8)
#define WX_MAC_PKT_FLT               0x11008
#define WX_MAC_PKT_FLT_PR            BIT(0)
#define WX_MAC_RX_FLOW_CTRL          0x11090
#define WX_MAC_RX_FLOW_CTRL_RFE      BIT(0)
#define WX_MMC_CONTROL               0x11800
#define WX_MMC_CONTROL_RSTONRD       BIT(2)
#define WX_MAC_WDG_TIMEOUT           0x1100C
#define WX_MAC_WDG_TIMEOUT_WTO_MASK  GENMASK(3, 0)
#define WX_MAC_WDG_TIMEOUT_WTO_DELTA 2
#define WX_MDIO_CLAUSE_SELECT        0x11220
#define WX_MSCA                      0x11200
#define WX_MSCC                      0x11204
#define WX_MSCA_PA(v)                FIELD_PREP(GENMASK(20, 16), v)
#define WX_MSCA_RA(v)                FIELD_PREP(U16_MAX, v)
#define WX_MSCA_DA(v)                FIELD_PREP(GENMASK(25, 21), v)
#define WX_MSCC_CMD(v)               FIELD_PREP(GENMASK(17, 16), v)
#define WX_MSCC_BUSY                 BIT(22)
#define WX_MDIO_CLK(v)               FIELD_PREP(GENMASK(21, 19), v)
#define WX_TDM_CTL                   0x18000
#define WX_TDM_CTL_TE                BIT(0)
#define WX_TSC_CTL                   0x1D000
#define WX_TSC_CTL_TSEC_DIS          BIT(0)
#define WX_TSC_CTL_TX_DIS            BIT(1)
#define WX_TSC_BUF_AE                0x1D00C
#define WX_TSC_BUF_AE_THR            GENMASK(9, 0)
#define WX_RDM_DCACHE_CTL            0x120A8
#define WX_RDM_DCACHE_CTL_EN         BIT(0)
#define WX_RDM_RSC_CTL               0x1200C
#define WX_RDM_RSC_CTL_FREE_CNT_DIS  BIT(8)
#define WX_RDM_RSC_CTL_FREE_CTL      BIT(7)
#define WX_RSC_CTL                   0x17000
#define WX_RSC_CTL_CRC_STRIP         BIT(2)
#define WX_RSC_CTL_SAVE_MAC_ERR      BIT(6)
#define WX_RSC_CTL_RX_DIS            BIT(1)
#define WX_RSC_ST                    0x17004
#define WX_RSC_ST_RSEC_RDY           BIT(0)
#define WX_BME_CTL                   0x12020
#define WX_RDB_PB_SZ(_i)             (0x19020 + ((_i) * 4))
#define WX_RDB_PB_SZ_SHIFT           10
#define WX_TDB_PB_SZ(_i)             (0x1CC00 + ((_i) * 4))
#define WX_TDM_PB_THRE(_i)           (0x18020 + ((_i) * 4))
#define WX_RDB_PL_CFG(_i)            (0x19300 + ((_i) * 4))
#define WX_RDB_PL_CFG_L4HDR          BIT(1)
#define WX_RDB_PL_CFG_L2HDR          BIT(3)
#define WX_RDB_PL_CFG_TUN_TUNHDR     BIT(4)
#define WX_RDB_PL_CFG_L3HDR          BIT(2)
#define WX_RDB_PL_CFG_TUN_OUTL2HDR   BIT(5)
#define WX_RDB_RA_CTL                0x194F4
#define WX_RDB_RA_CTL_RSS_EN         BIT(2)
#define WX_RDB_RA_CTL_MULTI_RSS      BIT(0)
#define WX_RDB_RA_CTL_RSS_MASK       GENMASK(23, 16)
#define WX_RDB_RSSTBL(_i)            (0x19400 + ((_i) * 4))
#define WX_RDB_VMRSSTBL(_i, _p)      (0x1B000 + ((_i) * 4) + ((_p) * 0x40))
#define WX_RDB_RSSRK(_i)             (0x19480 + ((_i) * 4))
#define WX_RDB_VMRSSRK(_i, _p)       (0x1A000 + ((_i) * 4) + ((_p) * 0x40))
#define WX_RDB_PFCMACDAL             0x19210
#define WX_RDB_PFCMACDAH             0x19214
#define WX_RDB_LXOFFTXC              0x19218
#define WX_RDB_LXONTXC               0x1921C
#define WX_RX_UNDERSIZE_FRAMES_GOOD  0x11938
#define WX_RX_MC_FRAMES_GOOD_L       0x11920
#define WX_RX_FRAME_CNT_GOOD_BAD_L   0x11900
#define WX_RX_OVERSIZE_FRAMES_GOOD   0x1193C
#define WX_TX_FRAME_CNT_GOOD_BAD_L   0x1181C
#define WX_TX_MC_FRAMES_GOOD_L       0x1182C
#define WX_RDM_BYTE_CNT_LSB          0x12508
#define WX_RX_LEN_ERROR_FRAMES_L     0x11978
#define WX_RX_CRC_ERROR_FRAMES_L     0x11928
#define WX_RX_BC_FRAMES_GOOD_L       0x11918
#define WX_TX_BC_FRAMES_GOOD_L       0x11824
#define WX_TDM_OS2BMC_CNT            0x18314
#define WX_RDB_FDIR_MATCH            0x19558
#define WX_RDB_FDIR_MISS             0x1955C
#define WX_RDM_BMC2OS_CNT            0x12510
#define WX_TDM_PKT_CNT               0x18308
#define WX_RDM_PKT_CNT               0x12504
#define WX_RDM_DRP_PKT               0x12500
#define WX_TDM_BYTE_CNT_LSB          0x1830C
#define WX_MAC_LXONRXC_AML           0x11F84
#define WX_MAC_LXONRXC               0x11E0C
#define WX_MAC_LXOFFRXC              0x11988
#define WX_MAC_LXOFFRXC_AML          0x11F80
#define WX_PX_ISB_ADDR_H             0x164
#define WX_PX_ISB_ADDR_L             0x160
#define WX_PX_RR_CFG_DESC_MERGE      BIT(19)
#define WX_PX_RR_CFG_RR_SIZE_SHIFT   1
#define WX_PX_RR_CFG_RR_THER_SHIFT   16
#define WX_PX_RR_CFG_VLAN            BIT(31)
#define WX_PX_RR_CFG_SPLIT_MODE      BIT(26)
#define WX_PX_RR_CFG_BHDRSIZE_SHIFT  6
#define WX_PX_RR_CFG_BSIZEPKT_SHIFT  2
#define WX_PX_RR_CFG_RR_BUF_SZ       GENMASK(11, 8)
#define WX_PX_RR_CFG_RR_HDR_SZ       GENMASK(15, 12)
#define WX_PX_RR_CFG_RSC             BIT(29)
#define WX_PX_RR_CFG_MAX_RSCBUF_16   FIELD_PREP(GENMASK(24, 23), 3)
#define WX_TSC_1588_SDP(i)           (0x11F5C + ((i) * 4))
#define WX_TSC_1588_SDP_FUN_SEL_MASK GENMASK(2, 0)
#define WX_TSC_1588_SDP_FUN_SEL_TS0  FIELD_PREP(WX_TSC_1588_SDP_FUN_SEL_MASK, 5)
#define WX_TSC_1588_SDP_FUN_SEL_TT0  FIELD_PREP(WX_TSC_1588_SDP_FUN_SEL_MASK, 1)
#define WX_TSC_1588_SDP_OUT_LEVEL_H  FIELD_PREP(BIT(4), 0)
#define WX_PX_IVAR(_i)               (0x500 + (_i) * 4)
#define WX_PX_MISC_IVAR              0x4FC
#define WX_PX_IVAR_ALLOC_VAL         0x80
#define WX_PCIE_MSIX_TBL_SZ_MASK     0x7FF
#define NGBE_MAX_MSIX_VECTORS        0x09
#define WX_PX_GPIE_MODEL             BIT(0)
#define WX_PX_ITRSEL                 0x180
#define WX_PX_TRANSACTION_PENDING    0x168
#define WX_PCI_MASTER_DISABLE_TIMEOUT        80000
#define NGBE_PHY_CONFIG(reg_offset)  (0x14000 + ((reg_offset) * 4))
#define WX_PSR_VLAN_SWC_IDX          0x16230
#define WX_PSR_VLAN_SWC              0x16220
#define WX_PSR_VLAN_SWC_VM_L         0x16224
#define WX_PSR_VLAN_SWC_VM_H         0x16228
#define WX_PSR_VLAN_SWC_VIEN         BIT(31)
#define WX_PSR_VLAN_SWC_VLANID_MASK  GENMASK(11, 0)
#define WX_TDM_VLAN_INS(_i)          (0x18100 + ((_i) * 4))
#define WX_TDM_VLAN_INS_VLANA_DEFAULT BIT(30)
#define WX_TDM_VF_TE(_i)             (0x18004 + ((_i) * 4))
#define WX_RDM_VF_RE(_i)             (0x12004 + ((_i) * 4))
#define WX_TDM_MAC_AS(_i)            (0x18060 + ((_i) * 4))
#define WX_TDM_VLAN_AS(_i)           (0x18070 + ((_i) * 4))
#define WX_TDM_ETYPE_AS(_i)          (0x18058 + ((_i) * 4))
#define WX_VFLREC(i)                 (0x4A8 + (4 * (i)))
#define WX_VFLRE(i)                  (0x4A0 + (4 * (i)))
#define WX_PXMAILBOX(i)              (0x600 + (4 * (i)))
#define WX_PXMBMEM(i)                (0x5000 + (64 * (i)))
#define WX_MBVFICR(i)                (0x480 + (4 * (i)))
#define WX_PXMAILBOX_STS             BIT(0)
#define WX_PXMAILBOX_ACK             BIT(1)
#define WX_PXMAILBOX_PFU             BIT(3)
#define WX_MIS_RST_LAN_RST(_i)       BIT((_i) + 1)
#define WX_MIS_RST_ST                0x10030
#define WX_MIS_RST_ST_RST_INI_SHIFT  8
#define WX_MIS_RST_ST_RST_INIT       (0xFF << WX_MIS_RST_ST_RST_INI_SHIFT)
#define NGBE_DEV_ID_EM_WX1860AL_W    0x0100
#define NGBE_DEV_ID_EM_WX1860A2      0x0101
#define NGBE_DEV_ID_EM_WX1860A2S     0x0102
#define NGBE_DEV_ID_EM_WX1860A4      0x0103
#define NGBE_DEV_ID_EM_WX1860A4S     0x0104
#define NGBE_DEV_ID_EM_WX1860AL2     0x0105
#define NGBE_DEV_ID_EM_WX1860AL2S    0x0106
#define NGBE_DEV_ID_EM_WX1860AL4     0x0107
#define NGBE_DEV_ID_EM_WX1860AL4S    0x0108
#define NGBE_DEV_ID_EM_WX1860LC      0x0109
#define NGBE_DEV_ID_EM_WX1860A1      0x010a
#define NGBE_DEV_ID_EM_WX1860A1L     0x010b
#define NGBE_PX_MISC_IEN_DEV_RST     BIT(10)
#define NGBE_PX_MISC_IEN_TIMESYNC    BIT(11)
#define NGBE_PX_MISC_IEN_GPIO        BIT(26)
#define NGBE_PX_MISC_IEN_INT_ERR     BIT(20)
#define NGBE_PX_MISC_IEN_ETH_LK      BIT(18)
#define NGBE_PX_MISC_IC_TIMESYNC     BIT(11)
#define NGBE_PX_MISC_IC_VF_MBOX      BIT(23)
#define NGBE_INTR_MISC(A)            BIT((A)->msix_entry->entry)
#define NGBE_INTR_ALL                0x1FF
#define WX_INTR_ALL                  (~0ULL)
#define WX_GPIO_INTEN_1              BIT(1)
#define WX_GPIO_INTEN_0              BIT(0)
#define WX_GPIO_DDR_0                BIT(0)
#define NGBE_GPIO_DR                 0x14800
#define NGBE_GPIO_DR_0               BIT(0)
#define WX_PSR_1588_CTL_VALID        BIT(0)
#define WX_TSC_1588_CTL_VALID        BIT(0)
#define WX_PSR_1588_CTL_TYPE_MASK    GENMASK(3, 1)
#define WX_PSR_1588_CTL_ENABLED      BIT(4)
#define WX_PSR_1588_MSG_V1_SYNC      FIELD_PREP(GENMASK(7, 0), 0)
#define WX_PSR_1588_MSG_V1_DELAY_REQ FIELD_PREP(GENMASK(7, 0), 1)
#define WX_PSR_1588_CTL_TYPE_L4_V1   FIELD_PREP(GENMASK(3, 1), 1)
#define WX_PSR_1588_CTL_TYPE_EVENT_V2 FIELD_PREP(GENMASK(3, 1), 5)
#define WX_PSR_ETYPE_SWC_FILTER_EN   BIT(31)
#define WX_PSR_ETYPE_SWC_1588        BIT(30)
#define WX_PSR_ETYPE_SWC_FILTER_1588 3
#define WX_TDM_VFTE_CLR(_i)          (0x180A0 + ((_i) * 4))
#define WX_RDM_VFRE_CLR(_i)          (0x120A0 + ((_i) * 4))
#define WX_PX_ITR(_i)                (0x200 + (_i) * 4)

/* Newly added defines from supplementary driver source */
#define NGBE_FW_CMD_ST_PASS             0x80658383
#define NGBE_FW_EEPROM_CHECKSUM_CMD     0xE9
#define NGBE_FW_CMD_DEFAULT_CHECKSUM    0xFF

/* End of register offsets */

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

#define BAR0_SIZE 0x20000
#define MSIX_BAR_INDEX 4
#define MSIX_BAR_SIZE 0x1000
#define EEPROM_SIZE 256

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
    uint32_t regs[BAR0_SIZE / 4];
    uint8_t eeprom[EEPROM_SIZE];

    /* Operational status flags */
    bool spi_busy;
    uint32_t spi_cmd;
    uint32_t spi_data;

    /* Timers for delayed hardware events */
    QEMUTimer *flash_timer;
    QEMUTimer *lan_rst_timer;

    /* State used to handle reset sequences */
    /* Other device-specific state */
};

/* Internal helper for status-triggered signaling */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    /* Interrupt logic: not needed for probe, but can be extended */
}

/* Timer callbacks */
static void flash_timer_cb(void *opaque)
{
    PCIBaseState *s = opaque;
    s->regs[WX_SPI_ILDR_STATUS >> 2] &= ~(NGBE_SPI_ILDR_STATUS_PERST | NGBE_SPI_ILDR_STATUS_PWRRST);
}

static void lan_rst_timer_cb(void *opaque)
{
    PCIBaseState *s = opaque;
    s->regs[WX_MIS_ST >> 2] &= ~BIT(9); /* Clear function 0 reset bit */
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= BAR0_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR, "ngbe: read out of bounds addr=0x%"HWADDR_PRIx"\n", addr);
        return 0xFFFFFFFFFFFFFFFFULL;
    }

    if (size != 4 && size != 8) {
        qemu_log_mask(LOG_GUEST_ERROR, "ngbe: unsupported read size %u at 0x%"HWADDR_PRIx"\n", size, addr);
        return 0;
    }

    /* Special handling for read-to-clear registers */
    if (addr == WX_PX_MISC_IC) {
        uint32_t ret = s->regs[addr >> 2];
        s->regs[addr >> 2] = 0; /* clear on read */
        return ret;
    }
    if (addr == WX_PX_IC(0)) {
        uint32_t ret = s->regs[addr >> 2];
        s->regs[addr >> 2] = 0;
        return ret;
    }

    if (size == 8) {
        /* 64-bit read: two consecutive 32-bit registers */
        uint32_t lo = s->regs[addr >> 2];
        uint32_t hi = s->regs[(addr >> 2) + 1];
        val = ((uint64_t)hi << 32) | lo;
    } else {
        val = s->regs[addr >> 2];
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= BAR0_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR, "ngbe: write out of bounds addr=0x%"HWADDR_PRIx"\n", addr);
        return;
    }

    if (size != 4 && size != 8) {
        qemu_log_mask(LOG_GUEST_ERROR, "ngbe: unsupported write size %u at 0x%"HWADDR_PRIx"\n", size, addr);
        return;
    }

    if (size == 8) {
        /* Split 8-byte write into two 4-byte writes */
        uint32_t lo = val & 0xffffffff;
        uint32_t hi = (val >> 32) & 0xffffffff;
        pcibase_mmio_write(opaque, addr, lo, 4);
        pcibase_mmio_write(opaque, addr + 4, hi, 4);
        return;
    }

    /* Handle special registers */
    switch (addr) {
    case WX_SPI_CMD:
        s->spi_cmd = val;
        /* Simulate immediate SPI operation completion */
        if ((val & GENMASK(30, 28)) == FIELD_PREP(GENMASK(30, 28), WX_SPI_CMD_READ_DWORD)) {
            uint32_t addr_offset = (val >> 0) & 0xFFFFFF; /* address extraction, adjust if needed */
            addr_offset &= (EEPROM_SIZE - 1);
            s->spi_data = *(uint32_t *)&s->eeprom[addr_offset];
        }
        s->spi_busy = false;
        break;
    case WX_SPI_DATA:
        s->spi_data = val;
        break;
    case WX_MIS_RST:
        s->regs[addr >> 2] = val;
        /* Simulate LAN reset completion: set reset-in-progress bit, then clear after delay */
        if (val & 0xFE) { /* LAN_RST bits for function 0 */
            s->regs[WX_MIS_ST >> 2] |= BIT(9); /* Set reset in progress for func 0 */
            timer_mod(s->lan_rst_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 10); /* 10ms delay */
        }
        break;
    case WX_SW2FW_MBOX_CMD:
        s->regs[addr >> 2] = val;
        if (val & WX_SW2FW_MBOX_CMD_VLD) {
            /* Simulate immediate firmware command completion */
            s->regs[(WX_MNG_MBOX >> 2) + 1] = NGBE_FW_CMD_ST_PASS; /* index 1 in MNG_MBOX array */
            s->regs[addr >> 2] &= ~WX_SW2FW_MBOX_CMD_VLD; /* Clear VLD bit */
        }
        break;
    default:
        s->regs[addr >> 2] = val;
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* Phase 2: PIO read handler (if needed) */
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Phase 2: PIO write handler (if needed) */
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 8 },
    .impl  = { .min_access_size = 4, .max_access_size = 8 },
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

    /* Set power-on default register values */
    memset(s->regs, 0, sizeof(s->regs));
    memset(s->eeprom, 0, sizeof(s->eeprom));

    s->regs[WX_SPI_ILDR_STATUS >> 2] = NGBE_SPI_ILDR_STATUS_PERST | NGBE_SPI_ILDR_STATUS_PWRRST;
    s->regs[WX_MNG_SWFW_SYNC >> 2] = WX_MNG_SWFW_SYNC_SW_MB; /* management present */
    s->regs[WX_MIS_ST >> 2] = WX_MIS_ST_MNG_INIT_DN;
    s->regs[WX_MIS_RST_ST >> 2] = WX_MIS_RST_ST_RST_INIT;
    s->regs[WX_SPI_STATUS >> 2] = 0; /* not busy */

    /* Preload a dummy MAC address into shadow registers */
    s->regs[WX_PSR_MAC_SWC_AD_L >> 2] = 0x33221100; /* 00:11:22:33 */
    s->regs[WX_PSR_MAC_SWC_AD_H >> 2] = 0x00005544; /* 44:55:00:00 */

    s->spi_busy = false;
    s->spi_cmd = 0;
    s->spi_data = 0;

    /* Start flash load simulation timer: clear ILDR status bits after 10ms */
    timer_mod(s->flash_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 10);
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
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = BAR0_SIZE, .name = "ngbe-mmio" };
    s->bar_info[4] = (BARInfo){ .index = MSIX_BAR_INDEX, .type = BAR_TYPE_MMIO, .size = MSIX_BAR_SIZE, .name = "ngbe-msix" };

    /* Register the two known BARs explicitly */
    pcibase_register_bar(pdev, s, &s->bar_info[0], errp);
    pcibase_register_bar(pdev, s, &s->bar_info[4], errp);

    /* Create timers */
    s->flash_timer = timer_new_ms(QEMU_CLOCK_VIRTUAL, flash_timer_cb, s);
    s->lan_rst_timer = timer_new_ms(QEMU_CLOCK_VIRTUAL, lan_rst_timer_cb, s);

    /* MSI-X initialization: table and PBA share BAR4 */
    msix_init(pdev, NGBE_MAX_MSIX_VECTORS,
              &s->bar_regions[MSIX_BAR_INDEX], MSIX_BAR_INDEX, 0,
              &s->bar_regions[MSIX_BAR_INDEX], MSIX_BAR_INDEX, 0x800,
              0, errp);

    /* Initialize register defaults */
    pcibase_reset(DEVICE(s));
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

    timer_del(s->flash_timer);
    timer_free(s->flash_timer);
    timer_del(s->lan_rst_timer);
    timer_free(s->lan_rst_timer);

    /* Free buffers, stop timers, etc. */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "ngbe_pci",
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
