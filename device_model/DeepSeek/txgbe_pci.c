/*
 * QEMU device model for Wangxun TXGBE Ethernet controller.
 * Based on driver source: txgbe_main.c
 * This implements register-level MMIO access, MSI-X interrupts,
 * and basic reset/initialization to allow driver probe.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "qemu/host-utils.h"
#include "qemu/bitops.h"
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
/* No additional include files */

#define TYPE_PCIBASE_DEVICE "txgbe_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */

/* Temporary placeholder: need actual vendor ID from WANGXUN define */
#define WANGXUN_VENDOR_ID 0xFFFF

#define TXGBE_DEV_ID_SP1000 0x1001

/* CLASS_ID: Network controller (Ethernet) */
#define PCI_CLASS_NETWORK_ETHERNET 0x0200

/* Register offsets (from driver) */
#define WX_MIS_PWR                   0x10000
#define WX_MIS_RST                   0x1000C
#define WX_MIS_RST_SW_RST            BIT(0)
#define WX_MIS_RST_LAN_RST(_i)       BIT((_i) + 1)
#define WX_MIS_RST_ST                0x10030
#define WX_MIS_RST_ST_RST_INIT       (0xFF << WX_MIS_RST_ST_RST_INI_SHIFT)
#define WX_MIS_RST_ST_RST_INI_SHIFT  8
#define WX_MIS_ST                    0x10028
#define WX_MIS_ST_MNG_INIT_DN        BIT(0)
#define WX_PX_IC(_i)                 (0x120 + (_i) * 4)
#define WX_PX_TR_CFG(_i)             (0x03010 + ((_i) * 0x40))
#define WX_PX_TR_CFG_SWFLSH          BIT(26)
#define WX_CFG_PORT_CTL              0x14400
#define WX_CFG_PORT_CTL_PFRSTD       BIT(14)
#define WX_CFG_PORT_CTL_DRV_LOAD     BIT(3)
#define WX_GPIO_DR                   0x14800
#define TXGBE_GPIOBIT_1                         BIT(1)
#define TXGBE_GPIOBIT_2                         BIT(2)
#define TXGBE_GPIOBIT_4                         BIT(4)
#define WX_GPIO_EXT                  0x14850
#define WX_GPIO_INTSTATUS            0x14844
#define WX_GPIO_EOI                  0x1484C
#define WX_GPIO_INTEN                0x14830
#define WX_GPIO_INTTYPE_LEVEL        0x14838
#define WX_GPIO_DDR                  0x14804

#define WX_MAC_TX_CFG                0x11000
#define WX_MAC_TX_CFG_TE             BIT(0)
#define WX_MAC_RX_CFG                0x11004
#define WX_MAC_RX_CFG_RE             BIT(0)
#define WX_MAC_RX_CFG_JE             BIT(8)

#define WX_TDM_CTL                   0x18000
#define WX_TDM_CTL_TE                BIT(0)

#define WX_PX_ITRSEL                 0x180
#define WX_PX_ITR(_i)                (0x200 + (_i) * 4)

#define TXGBE_MIS_PRB_CTL                       0x10010
#define TXGBE_MIS_PRB_CTL_LAN_UP(_i)            BIT(1 - (_i))

#define WX_PX_MPRC(_i)               (0x01020 + ((_i) * 0x40))

#define WX_RX_UNDERSIZE_FRAMES_GOOD  0x11938
#define WX_RX_MC_FRAMES_GOOD_L       0x11920
#define WX_TDM_OS2BMC_CNT            0x18314
#define WX_RX_FRAME_CNT_GOOD_BAD_L   0x11900
#define WX_RDB_LXOFFTXC              0x19218
#define WX_RX_OVERSIZE_FRAMES_GOOD   0x1193C
#define WX_TX_FRAME_CNT_GOOD_BAD_L   0x1181C
#define WX_TX_MC_FRAMES_GOOD_L       0x1182C
#define WX_RDB_FDIR_MATCH            0x19558
#define WX_RDM_BYTE_CNT_LSB          0x12508
#define WX_RX_LEN_ERROR_FRAMES_L     0x11978
#define WX_RX_CRC_ERROR_FRAMES_L     0x11928
#define WX_RDB_LXONTXC               0x1921C
#define WX_RX_BC_FRAMES_GOOD_L       0x11918
#define WX_TX_BC_FRAMES_GOOD_L       0x11824
#define WX_MNG_BMC2OS_CNT            0x1E090
#define WX_RDB_FDIR_MISS             0x1955C
#define WX_RDM_BMC2OS_CNT            0x12510
#define WX_TDM_PKT_CNT               0x18308
#define WX_RDM_PKT_CNT               0x12504
#define WX_MNG_OS2BMC_CNT            0x1E094
#define WX_MAC_LXONRXC_AML           0x11F84
#define WX_RDM_DRP_PKT               0x12500
#define WX_TDM_BYTE_CNT_LSB          0x1830C
#define WX_MAC_LXONRXC               0x11E0C

#define WX_PX_GPIE                   0x118
#define WX_PX_GPIE_MODEL             BIT(0)

#define TXGBE_INTR_MISC(A)    BIT((A)->num_q_vectors)
#define TXGBE_INTR_QALL(A)    (TXGBE_INTR_MISC(A) - 1)

#define WX_PX_MISC_IEN               0x108
#define TXGBE_PX_MISC_IEN_MASK                            \
	(TXGBE_PX_MISC_ETH_LKDN | TXGBE_PX_MISC_DEV_RST | \
	 TXGBE_PX_MISC_ETH_EVENT | TXGBE_PX_MISC_ETH_LK | \
	 TXGBE_PX_MISC_ETH_AN | TXGBE_PX_MISC_INT_ERR | \
	 TXGBE_PX_MISC_IC_VF_MBOX | TXGBE_PX_MISC_IC_TIMESYNC)

#define WX_PX_MISC_IXC               0x100
#define TXGBE_PX_MISC_GPIO                      BIT(26)
#define WX_PX_MISC_IC                0x100
#define TXGBE_PX_MISC_ETH_LKDN                  BIT(8)
#define TXGBE_PX_MISC_DEV_RST                   BIT(10)
#define TXGBE_PX_MISC_INT_ERR                   BIT(20)
#define TXGBE_PX_MISC_ETH_EVENT                 BIT(17)
#define TXGBE_PX_MISC_ETH_LK                    BIT(18)
#define TXGBE_PX_MISC_ETH_AN                    BIT(19)

#define WX_PX_IMS(_i)                (0x140 + (_i) * 4)
#define WX_PX_IMC(_i)                (0x150 + (_i) * 4)

#define WX_PX_IVAR_ALLOC_VAL         0x80
#define WX_PX_MISC_IVAR              0x4FC
#define WX_PX_IVAR(_i)               (0x500 + (_i) * 4)

#define WX_PSR_MAC_SWC_IDX           0x16210
#define WX_PSR_MAC_SWC_AD_H_ADTYPE(v)   FIELD_PREP(BIT(30), v)
#define WX_PSR_MAC_SWC_AD_H          0x16204
#define WX_PSR_MAC_SWC_VM_L          0x16208
#define WX_PSR_MAC_SWC_AD_H_AD(v)       FIELD_PREP(U16_MAX, v)
#define WX_PSR_MAC_SWC_AD_L          0x16200
#define WX_PSR_MAC_SWC_VM_H          0x1620C

#define WX_PSR_CTL                   0x15000
#define WX_PSR_CTL_SW_EN             BIT(18)
#define WX_RDB_PB_CTL                0x19000
#define WX_RDB_PB_CTL_RXEN           BIT(31)

#define WX_RSS_FIELD_IPV4          BIT(1)
#define WX_RSS_FIELD_IPV6          BIT(5)
#define WX_RSS_FIELD_IPV4_TCP      BIT(0)
#define WX_RSS_FIELD_IPV6_TCP      BIT(4)
#define WX_RSS_FIELD_IPV6_UDP      BIT(7)
#define WX_RSS_FIELD_IPV6_SCTP     BIT(3)
#define WX_RSS_FIELD_IPV4_SCTP     BIT(2)
#define WX_RSS_FIELD_IPV4_UDP      BIT(6)

#define WX_CFG_PORT_ST_LANID         GENMASK(9, 8)
#define WX_CFG_PORT_ST               0x14404

#define WX_SPI_STATUS                0x1010C
#define WX_SPI_ILDR_STATUS           0x10120
#define WX_SPI_ILDR_STATUS_PWRRST            BIT(1)
#define WX_SPI_ILDR_STATUS_PERST             BIT(0)
#define WX_SPI_ILDR_STATUS_LAN_SW_RST(_i)    BIT((_i) + 9)
#define WX_SPI_STATUS_FLASH_BYPASS   BIT(31)

#define WX_HI_COMMAND_TIMEOUT        1000

#define WX_MNG_SWFW_SYNC_SW_FLASH    BIT(3)

#define TXGBE_EEPROM_CHECKSUM                   0x2F

#define WX_MIS_ST_MNG_INIT_DN        BIT(0)

#define WX_MIN_RSC_ITR               24

#define WX_PSR_CTL_UPE               BIT(9)
#define WX_PSR_VM_L2CTL(_i)          (0x15600 + ((_i) * 4))
#define WX_PSR_VM_L2CTL_ROMPE        BIT(9)
#define WX_PSR_VM_L2CTL_BAM          BIT(11)
#define WX_PSR_VM_L2CTL_UPE          BIT(4)
#define WX_RSC_CTL_SAVE_MAC_ERR      BIT(6)
#define WX_PSR_VM_L2CTL_MPE          BIT(12)
#define WX_RSC_CTL                   0x17000
#define WX_PSR_VLAN_CTL_VFE          BIT(30)
#define WX_PSR_VM_L2CTL_ROPE         BIT(10)
#define WX_PSR_CTL_BAM               BIT(10)
#define WX_PSR_CTL_MFE               BIT(7)
#define WX_PSR_VM_L2CTL_VACC         BIT(6)
#define WX_PSR_VLAN_CTL_CFIEN        BIT(29)
#define WX_PSR_CTL_MPE               BIT(8)
#define WX_PSR_VLAN_CTL              0x15088
#define WX_PSR_VM_L2CTL_AUPE         BIT(8)

#define WX_MAX_VF_MC_ENTRIES                    30

#define WX_MAC_LXOFFRXC              0x11988
#define WX_MAC_LXOFFRXC_AML          0x11F80

#define WX_AML_MAX_EITR              0x00000FFFU
#define WX_SP_MAX_EITR               0x00000FF8U
#define WX_PX_ITR_CNT_WDIS           BIT(31)
#define WX_EM_MAX_EITR               0x00007FFCU

#define WX_PX_IVAR_ALLOC_VAL         0x80

#define WX_GPIO_INTSTATUS            0x14844
#define WX_GPIO_EOI                  0x1484C
#define WX_GPIO_INTEN                0x14830
#define WX_GPIO_INTTYPE_LEVEL        0x14838

#define TXGBE_PX_MISC_ETH_EVENT                 BIT(17)
#define TXGBE_PX_MISC_DEV_RST                   BIT(10)
#define TXGBE_PX_MISC_INT_ERR                   BIT(20)
#define TXGBE_PX_MISC_ETH_LKDN                  BIT(8)
#define TXGBE_PX_MISC_ETH_AN                    BIT(19)
#define TXGBE_PX_MISC_ETH_LK                    BIT(18)
#define TXGBE_PX_MISC_IC_TIMESYNC               BIT(11)
#define TXGBE_PX_MISC_IC_VF_MBOX                BIT(23)

#define WX_PX_IMC(_i)                (0x150 + (_i) * 4)

#define WX_PSR_MAC_SWC_IDX           0x16210
#define WX_PSR_MAC_SWC_AD_H_ADTYPE(v)   FIELD_PREP(BIT(30), v)
#define WX_PSR_MAC_SWC_AD_H          0x16204
#define WX_PSR_MAC_SWC_VM_L          0x16208
#define WX_PSR_MAC_SWC_AD_H_AD(v)       FIELD_PREP(U16_MAX, v)
#define WX_PSR_MAC_SWC_AD_L          0x16200
#define WX_PSR_MAC_SWC_VM_H          0x1620C

#define WX_PSR_ETYPE_SWC_FILTER_EN   BIT(31)
#define WX_PSR_1588_CTL_TYPE_MASK       GENMASK(3, 1)
#define WX_PSR_ETYPE_SWC_1588        BIT(30)
#define WX_PSR_1588_CTL_ENABLED         BIT(4)
#define WX_PSR_1588_MSG_V1_SYNC         FIELD_PREP(GENMASK(7, 0), 0)
#define WX_TSC_1588_CTL              0x11F00
#define WX_TSC_1588_CTL_ENABLED      BIT(4)
#define WX_PSR_ETYPE_SWC_FILTER_1588 3
#define WX_PSR_ETYPE_SWC(_i)         (0x15128 + ((_i) * 4))
#define WX_PSR_1588_CTL                 0x15188
#define WX_PSR_1588_MSG                 0x15120
#define WX_PSR_1588_STMPH               0x151A4
#define WX_PSR_1588_CTL_TYPE_EVENT_V2   FIELD_PREP(GENMASK(3, 1), 5)
#define WX_PSR_1588_MSG_V1_DELAY_REQ    FIELD_PREP(GENMASK(7, 0), 1)
#define WX_PSR_1588_CTL_TYPE_L4_V1      FIELD_PREP(GENMASK(3, 1), 1)
#define WX_TSC_1588_INC              0x11F14

#define WX_BME_CTL                   0x12020
#define WX_PX_TR_CFG_ENABLE          BIT(0)

#define WX_PSR_CTL_MO                (0x3 << WX_PSR_CTL_MO_SHIFT)
#define WX_CLEAR_VMDQ_ALL            0xFFFFFFFFU

#define WX_PSR_MC_TBL(_i)            (0x15200  + ((_i) * 4))
#define WX_PSR_CTL_MO_SHIFT          5

#define WX_PSR_VLAN_SWC_IDX          0x16230
#define WX_PSR_VLAN_SWC_VM_H         0x16228
#define WX_PSR_VLAN_SWC_VM_L         0x16224
#define WX_PSR_VLAN_SWC_ENTRIES      64
#define WX_PSR_VLAN_TBL(_i)          (0x16000 + ((_i) * 4))
#define WX_PSR_VLAN_SWC              0x16220

#define WX_PX_IMS(_i)                (0x140 + (_i) * 4)

#define WX_TSC_BUF_AE_THR            GENMASK(9, 0)
#define WX_TSC_BUF_AE                0x1D00C
#define WX_TSC_CTL_TX_DIS            BIT(1)
#define WX_TSC_CTL_TSEC_DIS          BIT(0)
#define WX_TSC_CTL                   0x1D000

#define WX_CFG_PORT_CTL_NUM_VT_MASK  GENMASK(13, 12)
#define WX_CFG_PORT_CTL_QINQ         BIT(2)
#define WX_CFG_PORT_CTL_NUM_VT_32    FIELD_PREP(GENMASK(13, 12), 2)
#define WX_CFG_PORT_CTL_NUM_VT_NONE  0
#define WX_CFG_PORT_CTL_NUM_VT_64    FIELD_PREP(GENMASK(13, 12), 3)
#define WX_CFG_PORT_CTL_NUM_VT_8     FIELD_PREP(GENMASK(13, 12), 1)
#define WX_CFG_TAG_TPID(_i)          (0x14430 + ((_i) * 4))
#define WX_CFG_PORT_CTL_D_VLAN       BIT(0)

#define WX_PX_ISB_ADDR_H             0x164
#define WX_PX_ISB_ADDR_L             0x160

#define WX_TDB_PB_SZ(_i)             (0x1CC00 + ((_i) * 4))
#define WX_TDM_PB_THRE(_i)           (0x18020 + ((_i) * 4))
#define WX_RDB_PB_SZ_SHIFT           10
#define WX_TXPKT_SIZE_MAX            0xA
#define WX_RDB_PB_SZ(_i)             (0x19020 + ((_i) * 4))

#define WX_RDM_RSC_CTL               0x1200C
#define WX_PSR_CTL_RSC_DIS           BIT(16)
#define WX_PSR_CTL_RSC_ACK           BIT(17)
#define WX_RDM_DCACHE_CTL_EN         BIT(0)
#define WX_RDM_DCACHE_CTL            0x120A8
#define WX_RSC_CTL_CRC_STRIP         BIT(2)
#define WX_RDM_RSC_CTL_FREE_CNT_DIS  BIT(8)
#define WX_RDM_RSC_CTL_FREE_CTL      BIT(7)

#define WX_VF_REG_OFFSET(_v)         FIELD_GET(GENMASK(15, 5), (_v))
#define WX_PSR_VM_CTL_REPLEN         BIT(30)
#define WX_VF_IND_SHIFT(_v)          FIELD_GET(GENMASK(4, 0), (_v))
#define WX_TDM_VF_TE(_i)             (0x18004 + ((_i) * 4))
#define WX_PSR_VM_CTL                0x151B0
#define WX_RDM_VF_RE(_i)             (0x12004 + ((_i) * 4))
#define WX_PSR_VM_CTL_POOL_MASK      GENMASK(12, 7)

#define WX_RDB_PFCMACDAL             0x19210
#define WX_RDB_PFCMACDAH             0x19214

#define WX_RSC_ST_RSEC_RDY           BIT(0)
#define WX_RSC_ST                    0x17004
#define WX_RSC_CTL_RX_DIS            BIT(1)

#define TXGBE_RDB_FDIR_HASH_BUCKET_VALID        BIT(15)
#define TXGBE_RDB_FDIR_CMD                      0x1952C
#define TXGBE_RDB_FDIR_CMD_LAST                 BIT(11)
#define TXGBE_RDB_FDIR_CMD_FLOW_TYPE(v)         FIELD_PREP(GENMASK(6, 5), v)
#define TXGBE_RDB_FDIR_CMD_CMD_ADD_FLOW         TXGBE_RDB_FDIR_CMD_CMD(1)
#define TXGBE_RDB_FDIR_CMD_QUEUE_EN             BIT(15)
#define TXGBE_RDB_FDIR_CMD_RX_QUEUE(v)          FIELD_PREP(GENMASK(22, 16), v)
#define TXGBE_RDB_FDIR_HASH                     0x19528
#define TXGBE_RDB_FDIR_CMD_FILTER_UPDATE        BIT(3)

#define TXGBE_RDB_FDIR_FLEX_CFG_BASE_MAC        FIELD_PREP(GENMASK(1, 0), 0)
#define TXGBE_RDB_FDIR_FLEX_CFG_OFST(v)         FIELD_PREP(GENMASK(7, 3), v)
#define TXGBE_RDB_FDIR_CTL_MAX_LENGTH(v)        FIELD_PREP(GENMASK(27, 24), v)
#define TXGBE_RDB_FDIR_FLEX_CFG_FIELD0          GENMASK(7, 0)
#define TXGBE_RDB_FDIR_CTL_HASH_BITS(v)         FIELD_PREP(GENMASK(23, 20), v)
#define TXGBE_RDB_FDIR_FLEX_CFG(_i)             (0x19580 + ((_i) * 4))
#define TXGBE_RDB_FDIR_CTL_FULL_THRESH(v)       FIELD_PREP(GENMASK(31, 28), v)
#define TXGBE_RDB_FDIR_DROP_QUEUE               127
#define TXGBE_RDB_FDIR_CTL_PERFECT_MATCH        BIT(4)
#define TXGBE_RDB_FDIR_CTL_DROP_Q(v)            FIELD_PREP(GENMASK(14, 8), v)

#define WX_TSC_1588_STMPH            0x11F08

#define WX_HI_MAX_BLOCK_BYTE_LENGTH  256
#define WX_FW2SW_MBOX                0x1E300
#define FW_DEFAULT_CHECKSUM          0xFF
#define FW_NVM_DATA_OFFSET           3
#define FW_READ_SHADOW_RAM_CMD       0x31
#define FW_READ_SHADOW_RAM_LEN       0x6
#define WX_MNG_MBOX                  0x1E100
#define WX_MNG_SWFW_SYNC             0x1E008
#define TXGBE_EEPROM_I2C_END_PTR                0x800
#define TXGBE_EEPROM_I2C_SRART_PTR              0x580
#define TXGBE_EEPROM_SUM                        0xBABA
#define TXGBE_EEPROM_LAST_WORD                  0x800

#define WX_TX_FLAGS_VLAN_SHIFT			16

#define WX_RXBUFFER_2K       2048
#define WX_RXBUFFER_3K       3072
#define WX_PSR_MAX_SZ                0x15020
#define WX_RDB_PL_CFG(_i)            (0x19300 + ((_i) * 4))
#define WX_RDB_RA_CTL                0x194F4
#define WX_RDB_RA_CTL_RSS_EN         BIT(2)
#define WX_RDB_PL_CFG_RSS_EN         BIT(24)
#define WX_PSR_VLAN_SWC_VM(_i)       (0x16224 + ((_i) * 4))

#define WX_PX_RR_CFG_VLAN            BIT(31)
#define WX_VF_ENABLE                    BIT(31)

#define WX_TDM_VFTE_CLR(_i)          (0x180A0 + ((_i) * 4))
#define WX_RDM_VFRE_CLR(_i)          (0x120A0 + ((_i) * 4))

#define WX_TS_INT_EN                 0x10314
#define TXGBE_TS_CTL_EVAL_MD                    BIT(31)
#define TXGBE_TS_CTL                            0x10300
#define WX_TS_INT_EN_DALARM_INT_EN   BIT(1)
#define WX_TS_INT_EN_ALARM_INT_EN    BIT(0)
#define WX_TS_DALARM_THRE            0x10310
#define WX_TS_EN_ENA                 BIT(0)
#define WX_TS_ALARM_THRE             0x1030C
#define WX_TS_EN                     0x10304

#define WX_PSR_LAN_FLEX_DW_L(_i)     (0x15C00 + ((_i) * 16))
#define WX_PSR_MNG_FLEX_DW_H(_i)     (0x15A04 + ((_i) * 16))
#define WX_PSR_LAN_FLEX_MSK(_i)      (0x15C08 + ((_i) * 16))
#define WX_PSR_MNG_FLEX_SEL          0x1582C
#define WX_PSR_MNG_FLEX_MSK(_i)      (0x15A08 + ((_i) * 16))
#define WX_RDB_PFCMACDAL             0x19210
#define WX_MIS_RST_ST_RST_INIT       (0xFF << WX_MIS_RST_ST_RST_INI_SHIFT)
#define WX_RDB_PFCMACDAH             0x19214
#define WX_PSR_MNG_FLEX_DW_L(_i)     (0x15A00 + ((_i) * 16))
#define WX_MIS_RST_ST                0x10030
#define WX_PSR_LAN_FLEX_DW_H(_i)     (0x15C04 + ((_i) * 16))
#define WX_PSR_LAN_FLEX_SEL          0x15B8C
#define WX_PSR_UC_TBL(_i)            (0x15400 + ((_i) * 4))

#define WX_TSC_1588_AUX_CTL_EN_TT1   BIT(2)
#define WX_TSC_1588_SDP_OUT_LEVEL_H  FIELD_PREP(BIT(4), 0)
#define WX_TSC_1588_INT_EN           0x11F24
#define WX_TSC_1588_AUX_CTL_PLSG     BIT(1)
#define WX_TSC_1588_SDP(i)           (0x11F5C + ((i) * 4))
#define WX_TSC_1588_TRGT_H(i)        (0x11F30 + ((_i) * 8))
#define WX_TSC_1588_SDP_FUN_SEL_TS0  FIELD_PREP(WX_TSC_1588_SDP_FUN_SEL_MASK, 5)
#define WX_TSC_1588_SDP_FUN_SEL_TT0  FIELD_PREP(WX_TSC_1588_SDP_FUN_SEL_MASK, 1)
#define WX_TSC_1588_TRGT_L(i)        (0x11F2C + ((_i) * 8))
#define WX_TSC_1588_INT_EN_TT1       BIT(5)
#define WX_TSC_1588_AUX_CTL          0x11F28
#define WX_TSC_1588_AUX_CTL_EN_TS0   BIT(8)
#define WX_TSC_1588_AUX_CTL_EN_TT0   BIT(0)

#define WX_PX_TR_WP(_i)              (0x03008 + ((_i) * 0x40))
#define WX_PX_TR_RP(_i)              (0x0300C + ((_i) * 0x40))
#define WX_PX_TR_BAH(_i)             (0x03004 + ((_i) * 0x40))
#define WX_PX_TR_CFG_HEAD_WB         BIT(27)
#define WX_PX_TR_CFG_WTHRESH_SHIFT   16
#define WX_PX_TR_BAL(_i)             (0x03000 + ((_i) * 0x40))
#define WX_MAX_TXD                   8192
#define WX_PX_TR_HEAD_ADDRH(_i)      (0x0302C + ((_i) * 0x40))
#define WX_PX_TR_CFG_TR_SIZE_SHIFT   1
#define WX_PX_TR_HEAD_ADDRL(_i)      (0x03028 + ((_i) * 0x40))

#define WX_RDB_PL_CFG_L4HDR          BIT(1)
#define WX_RDB_PL_CFG_L2HDR          BIT(3)
#define WX_RDB_PL_CFG_TUN_TUNHDR     BIT(4)
#define WX_RDB_PL_CFG_L3HDR          BIT(2)
#define WX_RDB_PL_CFG_TUN_OUTL2HDR   BIT(5)

#define WX_PX_RR_BAL(_i)             (0x01000 + ((_i) * 0x40))
#define WX_PX_RR_CFG_RR_THER_SHIFT   16
#define WX_PX_RR_RP(_i)              (0x0100C + ((_i) * 0x40))
#define WX_PX_RR_CFG_DESC_MERGE      BIT(19)
#define WX_PX_RR_CFG_RR_SIZE_SHIFT   1
#define WX_MAX_RXD                   8192
#define WX_PX_RR_BAH(_i)             (0x01004 + ((_i) * 0x40))
#define WX_PX_RR_WP(_i)              (0x01008 + ((_i) * 0x40))

#define WX_PSR_CTL_PCSD              BIT(13)

#define WX_TDM_MAC_AS(_i)            (0x18060 + ((_i) * 4))
#define WX_TDM_VLAN_AS(_i)           (0x18070 + ((_i) * 4))
#define WX_TDM_ETYPE_AS(_i)          (0x18058 + ((_i) * 4))

#define WX_SPI_CMD_CLK(_v)           FIELD_PREP(GENMASK(27, 25), _v)
#define WX_SPI_CLK_DIV               0x3
#define WX_SPI_CMD                   0x10104
#define WX_SPI_CMD_CMD(_v)           FIELD_PREP(GENMASK(30, 28), _v)

/* Removed broken WX_PTT macro because it's not needed for this device and caused syntax errors */

#define TXGBE_ATR_HASH_MASK                     0x7fff
#define TXGBE_RDB_FDIR_CMD_CMD(v)               FIELD_PREP(GENMASK(1, 0), v)
#define TXGBE_RDB_FDIR_CMD_CMD_MASK             GENMASK(1, 0)
#define TXGBE_ATR_BUCKET_HASH_KEY               0x3DAD14E2
#define TXGBE_RDB_FDIR_SKEY                     0x1956C
#define TXGBE_RDB_FDIR_CTL                      0x19500
#define TXGBE_ATR_SIGNATURE_HASH_KEY            0x174D3614
#define TXGBE_RDB_FDIR_HKEY                     0x19568
#define TXGBE_RDB_FDIR_CTL_INIT_DONE            BIT(3)
#define TXGBE_RDB_FDIR_PORT_DESTINATION_SHIFT   16
#define TXGBE_RDB_FDIR_FLEX_FLEX_SHIFT          16
#define TXGBE_RDB_FDIR_HASH_SIG_SW_INDEX(v)     FIELD_PREP(GENMASK(31, 16), v)
#define TXGBE_RDB_FDIR_PORT                     0x19520
#define TXGBE_RDB_FDIR_CMD_DROP                 BIT(9)
#define TXGBE_RDB_FDIR_IP6(_i)                  (0x1950C + ((_i) * 4))
#define TXGBE_RDB_FDIR_FLEX                     0x19524
#define TXGBE_RDB_FDIR_SA                       0x19518
#define TXGBE_RDB_FDIR_CMD_VT_POOL(v)           FIELD_PREP(GENMASK(29, 24), v)
#define TXGBE_RDB_FDIR_DA                       0x1951C
#define TXGBE_RDB_FDIR_SCTP_MSK                 0x19560
#define TXGBE_RDB_FDIR_OTHER_MSK_POOL           BIT(2)
#define TXGBE_RDB_FDIR_TCP_MSK                  0x19544
#define TXGBE_RDB_FDIR_OTHER_MSK                0x19570
#define TXGBE_RDB_FDIR_FLEX_CFG_MSK             BIT(2)
#define TXGBE_RDB_FDIR_DA4_MSK                  0x1953C
#define TXGBE_RDB_FDIR_OTHER_MSK_L4P            BIT(3)
#define TXGBE_RDB_FDIR_SA4_MSK                  0x19540
#define TXGBE_RDB_FDIR_UDP_MSK                  0x19548

#define TXGBE_ATR_L4TYPE_MASK                   0x3
#define TXGBE_ATR_L4TYPE_IPV6_MASK              0x4

#define WX_RSS_8Q_MASK               0x7
#define WX_RSS_64Q_MASK              0x3F
#define WX_RSS_2Q_MASK               0x1
#define WX_RSS_DISABLED_MASK         0x0
#define WX_RSS_4Q_MASK               0x3

#define WX_VMDQ_2Q_MASK              0x7E
#define WX_VMDQ_4Q_MASK              0x7C

#define WX_7K_ITR                    595
#define WX_12K_ITR                   336
#define WX_20K_ITR                   200

#define WX_REQ_RX_DESCRIPTOR_MULTIPLE   128
#define WX_REQ_TX_DESCRIPTOR_MULTIPLE   128
#define WX_MIN_RXD                   128
#define WX_MIN_TXD                   128

#define WX_MDIO_CLAUSE_SELECT        0x11220

#define TXGBE_XPCS_IDA_ADDR                     0x13000
#define TXGBE_XPCS_IDA_DATA                     0x13004

#define WX_MNG_MBOX_CTL              0x1E044
#define WX_MNG_SWFW_SYNC_SW_MB       BIT(2)
#define WX_MNG_MBOX_CTL_FWRDY        BIT(2)
#define WX_MNG_MBOX_CTL_SWRDY        BIT(0)
#define WX_SW2FW_MBOX_CMD            0x1E0A0
#define WX_HIC_HDR_INDEX_MAX         255
#define WX_SW2FW_MBOX_CMD_VLD        BIT(31)
#define WX_SW2FW_MBOX                0x1E200
#define WX_SW_REGION_PTR             0x1C

#define FW_MAX_READ_BUFFER_SIZE      244

#define WX_TXD_RS                    BIT(27)
#define WX_MAX_DATA_PER_TXD  BIT(14)
#define WX_TXD_EOP                   BIT(24)
#define WX_TXD_MACLEN_SHIFT          9
#define WX_TXD_TAG_TPID_SEL_SHIFT    11
#define WX_TXD_TUNNEL_GRE            FIELD_PREP(BIT(WX_TXD_TUNNEL_TYPE_SHIFT), 1)
#define WX_TXD_TUNNEL_UDP            FIELD_PREP(BIT(WX_TXD_TUNNEL_TYPE_SHIFT), 0)
#define WX_TXD_OUTER_IPLEN_SHIFT     12
#define WX_TXD_L4LEN_SHIFT           8
#define WX_TXD_TUNNEL_LEN_SHIFT      21
#define WX_TXD_MSS_SHIFT             16
#define WX_TXD_IIPCS                 BIT(10)
#define WX_TXD_PAYLEN_SHIFT          13
#define WX_TXD_CC                    BIT(7)
#define WX_TXD_L4CS                  BIT(9)
#define WX_TXD_EIPCS                 BIT(11)
#define WX_TXD_IPSEC                 BIT(8)
#define WX_TXD_MAC_TSTAMP            BIT(19)
#define WX_TXD_IFCS                  BIT(25)
#define WX_TXD_LINKSEC               BIT(26)
#define WX_TXD_DTYP_DATA             0
#define WX_TXD_VLE                   BIT(30)
#define WX_TXD_TSE                   BIT(31)
#define WX_TXD_TUNNEL_TYPE_SHIFT     11
#define WX_TXD_DTYP_CTXT             BIT(20)

#define WX_MBVFICR_VFREQ_MASK GENMASK(15, 0)
#define WX_PXMAILBOX_PFU     BIT(3)
#define WX_MBVFICR_VFACK_MASK GENMASK(31, 16)

#define WX_TSC_1588_STMPL            0x11F04

#define WX_PHY_D     12800
#define WX_XAUI_D    (2 * 1024)
#define WX_MAC_D     4096

#define WX_MBVFICR(i)         (0x480 + (4 * (i)))

#define TXGBE_RDB_FDIR_CMD_FILTER_VALID         BIT(2)
#define TXGBE_RDB_FDIR_CMD_CMD_QUERY_REM_FILT   TXGBE_RDB_FDIR_CMD_CMD(3)
#define TXGBE_RDB_FDIR_CMD_CMD_REMOVE_FLOW      TXGBE_RDB_FDIR_CMD_CMD(2)

#define WX_RDB_RSSTBL(_i)            (0x19400 + ((_i) * 4))
#define WX_RDB_VMRSSTBL(_i, _p)      (0x1B000 + ((_i) * 4) + ((_p) * 0x40))
#define WX_RDB_RSSRK(_i)             (0x19480 + ((_i) * 4))
#define WX_RDB_VMRSSRK(_i, _p)       (0x1A000 + ((_i) * 4) + ((_p) * 0x40))

#define WX_RDB_PL_CFG_RSS_MASK       GENMASK(23, 16)
#define WX_RDB_RA_CTL_MULTI_RSS      BIT(0)
#define WX_RDB_RA_CTL_RSS_MASK       GENMASK(23, 16)

#define WX_PX_RR_CFG_BHDRSIZE_SHIFT  6
#define WX_PX_RR_CFG_SPLIT_MODE      BIT(26)
#define WX_PX_RR_CFG_BSIZEPKT_SHIFT  2
#define WX_RXBUFFER_256      256
#define WX_PX_RR_CFG_RR_BUF_SZ       GENMASK(11, 8)
#define WX_PX_RR_CFG_RR_HDR_SZ       GENMASK(15, 12)
#define WX_PX_RR_CFG_RSC             BIT(29)
#define WX_PX_RR_CFG_MAX_RSCBUF_16   FIELD_PREP(GENMASK(24, 23), 3)

#define WX_RXBUFFER_256      256
#define WX_PX_RR_CFG_RR_BUF_SZ       GENMASK(11, 8)
#define WX_PX_RR_CFG_RR_HDR_SZ       GENMASK(15, 12)
#define WX_PX_RR_CFG_RSC             BIT(29)
#define WX_PX_RR_CFG_MAX_RSCBUF_16   FIELD_PREP(GENMASK(24, 23), 3)

#define WX_MIS_RST_ST_RST_INI_SHIFT  8
#define WX_MAC_RX_FLOW_CTRL          0x11090
#define WX_MAC_PKT_FLT_PR            BIT(0)
#define WX_MMC_CONTROL_RSTONRD       BIT(2)
#define WX_MAC_RX_FLOW_CTRL_RFE      BIT(0)
#define WX_MMC_CONTROL               0x11800
#define WX_MAC_PKT_FLT               0x11008

#define WX_PSR_1588_CTL_VALID           BIT(0)
#define WX_TSC_1588_CTL_VALID        BIT(0)

#define FW_PPS_SET_CMD               0xF6
#define FW_PPS_SET_LEN               0x14

#define WX_TSC_1588_SDP_FUN_SEL_MASK GENMASK(2, 0)

#define WX_PCI_DELAY 10000
#define WX_ID        (WX_MAC_D + WX_XAUI_D + WX_PHY_D)
#define WX_PFC_D     672
#define WX_HD        6144
#define WX_CABLE_DC  5556

/* Added missing WX_* aliases for existing TXGBE_* definitions */
#define WX_RDB_FDIR_CTL            TXGBE_RDB_FDIR_CTL
#define WX_RDB_FDIR_CMD            TXGBE_RDB_FDIR_CMD
#define WX_RDB_FDIR_HASH           TXGBE_RDB_FDIR_HASH
#define WX_RDB_FDIR_SA             TXGBE_RDB_FDIR_SA
#define WX_RDB_FDIR_DA             TXGBE_RDB_FDIR_DA
#define WX_RDB_FDIR_PORT           TXGBE_RDB_FDIR_PORT
#define WX_RDB_FDIR_FLEX           TXGBE_RDB_FDIR_FLEX
#define WX_RDB_FDIR_SKEY           TXGBE_RDB_FDIR_SKEY
#define WX_RDB_FDIR_HKEY           TXGBE_RDB_FDIR_HKEY
#define WX_RDB_FDIR_TCP_MSK        TXGBE_RDB_FDIR_TCP_MSK
#define WX_RDB_FDIR_UDP_MSK        TXGBE_RDB_FDIR_UDP_MSK
#define WX_RDB_FDIR_SCTP_MSK       TXGBE_RDB_FDIR_SCTP_MSK
#define WX_RDB_FDIR_OTHER_MSK      TXGBE_RDB_FDIR_OTHER_MSK
#define WX_RDB_FDIR_FLEX_CFG(_i)   TXGBE_RDB_FDIR_FLEX_CFG(_i)
#define WX_TS_CTL                  TXGBE_TS_CTL

/* Enum definitions extracted from driver */
typedef enum {
    em_mac_type_unknown = 0,
    em_mac_type_mdi,
    em_mac_type_rgmii
} enum_em_mac_type;

typedef enum {
    wx_media_unknown = 0,
    wx_media_fiber,
    wx_media_copper,
    wx_media_backplane
} enum_wx_media_type;

typedef enum {
    WX_LAN_RESET = 0,
    WX_SW_RESET,
    WX_GLOBAL_RESET
} enum_wx_reset_type;

typedef enum {
    wx_eeprom_uninitialized = 0,
    wx_eeprom_spi,
    wx_flash,
    wx_eeprom_none
} enum_wx_eeprom_type;

typedef enum {
    wx_fc_none = 0,
    wx_fc_rx_pause,
    wx_fc_tx_pause,
    wx_fc_full
} enum_wx_fc_mode;

typedef enum {
    wx_mac_unknown = 0,
    wx_mac_sp,
    wx_mac_em,
    wx_mac_aml,
    wx_mac_aml40
} enum_wx_mac_type;

/* BAR information structure */
typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM
} bar_type_t;

typedef struct BARInfo {
    int index;
    bar_type_t type;
    hwaddr size;
    const char *name;
} BARInfo;

/* Device state structure */
typedef struct PCIBaseState {
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
    /* We'll store some typically used register values */
    uint32_t mis_rst;
    uint32_t mis_st;
    uint32_t mis_rst_st;        /* added missing field */
    uint32_t px_gpie;
    uint32_t px_misc_ien;
    uint32_t px_itr[128]; /* interrupt throttle rate */
    uint32_t px_ic[128];
    uint32_t px_ims[128];
    uint32_t px_imc[128];
    uint32_t px_ivar[64];
    uint32_t psr_ctl;
    uint32_t psr_mac_swc_idx;
    uint32_t psr_mac_swc_ad_l;
    uint32_t psr_mac_swc_ad_h;
    uint32_t psr_mac_swc_vm_l;
    uint32_t psr_mac_swc_vm_h;
    uint32_t rd_pb_ctl;
    uint32_t rsc_ctl;
    uint32_t mac_tx_cfg;
    uint32_t mac_rx_cfg;
    uint32_t tdm_ctl;
    uint32_t cfg_port_ctl;
    uint32_t cfg_port_st;
    uint32_t spi_status;
    uint32_t spi_ildr_status;
    uint32_t gpio_dr;
    uint32_t gpio_ddr;
    uint32_t gpio_inten;
    uint32_t gpio_intstatus;
    uint32_t gpio_inttype_level;
    uint32_t px_isb_addr_l;
    uint32_t px_isb_addr_h;
    uint32_t px_tr_bal[128];
    uint32_t px_tr_bah[128];
    uint32_t px_tr_wp[128];
    uint32_t px_tr_rp[128];
    uint32_t px_tr_cfg[128];
    uint32_t px_tr_head_addrl[128];
    uint32_t px_tr_head_addrh[128];
    uint32_t px_rr_bal[128];
    uint32_t px_rr_bah[128];
    uint32_t px_rr_wp[128];
    uint32_t px_rr_rp[128];
    // WX_PX_RR_CFG is not yet defined; omitting storage for now
    uint32_t misc_ivar;
    uint32_t px_ivars[64];
    uint32_t mng_mbox_ctl;
    uint32_t mng_swfw_sync;
    uint32_t sw2fw_mbox;
    uint32_t sw2fw_mbox_cmd;
    uint32_t fw2sw_mbox;
    uint32_t mng_mbox;
    uint32_t tsc_ctl;
    uint32_t tsc_ctr_1588_ctl;
    uint32_t tsc_1588_stmph;
    uint32_t tsc_1588_stmpl;
    uint32_t tsc_1588_inc;
    uint32_t tsc_1588_aux_ctl;
    uint32_t tsc_1588_int_en;
    uint32_t tsc_1588_sdp[8];
    uint32_t tsc_1588_trgt_l[8];
    uint32_t tsc_1588_trgt_h[8];
    uint32_t bme_ctl;
    uint32_t ts_en;
    uint32_t ts_alarm_thre;
    uint32_t ts_dalarm_thre;
    uint32_t ts_int_en;
    uint32_t psr_1588_ctl;
    uint32_t psr_1588_msg;
    uint32_t psr_1588_stmph;
    uint32_t psr_vlan_ctl;
    uint32_t psr_vlan_tbl[WX_PSR_VLAN_SWC_ENTRIES];
    uint32_t psr_uc_tbl[4]; /* size? */
    uint32_t psr_mc_tbl[128];
    uint32_t psr_vm_ctl;
    uint32_t psr_vm_l2ctl[128];
    uint32_t rd_rsc_ctl;
    uint32_t rd_rsc_st;
    uint32_t rd_ra_ctl;
    uint32_t rd_fdir_ctl;
    uint32_t rd_fdir_cmd;
    uint32_t rd_fdir_hash;
    uint32_t rd_fdir_sa;
    uint32_t rd_fdir_da;
    uint32_t rd_fdir_port;
    uint32_t rd_fdir_flex;
    uint32_t rd_fdir_skey;
    uint32_t rd_fdir_hkey;
    uint32_t rd_fdir_tcp_msk;
    uint32_t rd_fdir_udp_msk;
    uint32_t rd_fdir_sctp_msk;
    uint32_t rd_fdir_other_msk;
    uint32_t rd_fdir_flex_cfg[64];
    uint32_t rd_rsstbl[64];
    uint32_t rd_rssrk[10];
    uint32_t rd_pb_sz[8];
    uint32_t td_pb_sz[8];
    uint32_t tdm_pb_thre[8];
    uint32_t rd_pl_cfg[128];
    uint32_t mac_lxonrxc;
    uint32_t mac_lxoffrxc;
    uint32_t mac_lxonrxc_aml;
    uint32_t mac_lxoffrxc_aml;
    uint32_t mac_pkt_flt;
    uint32_t mmc_control;
    uint32_t mac_rx_flow_ctrl;
    uint32_t px_mpc[128];
    uint32_t vf_enable;
    uint32_t tdm_vfte_clr[128];
    uint32_t rdm_vfre_clr[128];
    uint32_t mbvficr[64];
    uint32_t pxmailbox[64];
    uint32_t pxmbmem[64][16]; /* 64 entries each 64 bytes */
    uint32_t spi_cmd;
    uint32_t spi_data;
    uint32_t mscc;
    uint32_t msca;
    /* DMA Context */
    dma_addr_t isb_dma;
    uint32_t isb_mem_offset; /* not actual memory */

    /* Operational status flags */
    uint32_t status_flags;

    /* State used to handle reset sequences */
    bool reset_pending;

    /* Power management state (D0-D3) */
    uint8_t power_state;

    /* Additional fields */
    uint8_t mac_addr[6];
    uint32_t rss_key[10]; /* 40 bytes */
    uint32_t rss_indir_tbl[128];
} PCIBaseState;

/* Update interrupt status and raise/lower IRQ */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    int vector;

    /* Check if any interrupt cause is active and its mask is set */
    if (msix_enabled(pdev)) {
        /* MSI-X: for each vector, check if interrupt pending */
        for (vector = 0; vector < msix_nr_vectors_allocated(pdev); vector++) {
            /* For simplicity, we aggregate all interrupts to vector 0 */
            if (s->intr_status & s->intr_mask) {
                msix_notify(pdev, vector);
            }
        }
    } else if (msi_enabled(pdev)) {
        if (s->intr_status & s->intr_mask) {
            msi_notify(pdev, 0);
        }
    } else {
        pci_set_irq(pdev, !! (s->intr_status & s->intr_mask));
    }
}

/* MMIO read handler - rewritten to avoid overlapping case ranges */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = PCIBASE_DEVICE(opaque);
    uint32_t val = 0;
    int idx;

    /* Handle all register reads with an if-else chain, ensuring non-overlapping conditions */
    if (addr == WX_MIS_RST) {
        val = s->mis_rst;
    } else if (addr == WX_MIS_RST_ST) {
        val = s->mis_rst_st;
    } else if (addr == WX_MIS_ST) {
        val = s->mis_st;
    } else if (addr == WX_PX_GPIE) {
        val = s->px_gpie;
    } else if (addr == WX_PX_MISC_IEN) {
        val = s->px_misc_ien;
    } else if (addr == WX_PX_MISC_IC) {
        val = s->px_ic[0];
    } else if (addr >= WX_PX_IMS(0) && addr <= WX_PX_IMS(127) && ((addr - WX_PX_IMS(0)) % 4 == 0)) {
        idx = (addr - WX_PX_IMS(0)) / 4;
        val = s->px_ims[idx];
    } else if (addr >= WX_PX_IMC(0) && addr <= WX_PX_IMC(127) && ((addr - WX_PX_IMC(0)) % 4 == 0)) {
        idx = (addr - WX_PX_IMC(0)) / 4;
        val = s->px_imc[idx];
    } else if (addr >= WX_PX_IC(0) && addr <= WX_PX_IC(127) && ((addr - WX_PX_IC(0)) % 4 == 0)) {
        idx = (addr - WX_PX_IC(0)) / 4;
        val = s->px_ic[idx];
    } else if (addr >= WX_PX_ITR(0) && addr <= WX_PX_ITR(127) && ((addr - WX_PX_ITR(0)) % 4 == 0)) {
        idx = (addr - WX_PX_ITR(0)) / 4;
        val = s->px_itr[idx];
    } else if (addr >= WX_PX_IVAR(0) && addr <= WX_PX_IVAR(63) && ((addr - WX_PX_IVAR(0)) % 4 == 0)) {
        idx = (addr - WX_PX_IVAR(0)) / 4;
        val = s->px_ivar[idx];
    } else if (addr == WX_PSR_CTL) {
        val = s->psr_ctl;
    } else if (addr == WX_PSR_MAC_SWC_IDX) {
        val = s->psr_mac_swc_idx;
    } else if (addr == WX_PSR_MAC_SWC_AD_L) {
        val = s->psr_mac_swc_ad_l;
    } else if (addr == WX_PSR_MAC_SWC_AD_H) {
        val = s->psr_mac_swc_ad_h;
    } else if (addr == WX_PSR_MAC_SWC_VM_L) {
        val = s->psr_mac_swc_vm_l;
    } else if (addr == WX_PSR_MAC_SWC_VM_H) {
        val = s->psr_mac_swc_vm_h;
    } else if (addr == WX_RDB_PB_CTL) {
        val = s->rd_pb_ctl;
    } else if (addr == WX_RSC_CTL) {
        val = s->rsc_ctl;
    } else if (addr == WX_MAC_TX_CFG) {
        val = s->mac_tx_cfg;
    } else if (addr == WX_MAC_RX_CFG) {
        val = s->mac_rx_cfg;
    } else if (addr == WX_TDM_CTL) {
        val = s->tdm_ctl;
    } else if (addr == WX_CFG_PORT_CTL) {
        val = s->cfg_port_ctl;
    } else if (addr == WX_CFG_PORT_ST) {
        val = s->cfg_port_st;
    } else if (addr == WX_SPI_STATUS) {
        val = s->spi_status;
    } else if (addr == WX_SPI_ILDR_STATUS) {
        val = s->spi_ildr_status;
    } else if (addr == WX_GPIO_DR) {
        val = s->gpio_dr;
    } else if (addr == WX_GPIO_DDR) {
        val = s->gpio_ddr;
    } else if (addr == WX_GPIO_INTEN) {
        val = s->gpio_inten;
    } else if (addr == WX_GPIO_INTSTATUS) {
        val = s->gpio_intstatus;
    } else if (addr == WX_GPIO_INTTYPE_LEVEL) {
        val = s->gpio_inttype_level;
    } else if (addr == WX_GPIO_EXT) {
        val = 0;
    } else if (addr == WX_GPIO_EOI) {
        val = 0;
    } else if (addr == WX_PX_ISB_ADDR_L) {
        val = s->px_isb_addr_l;
    } else if (addr == WX_PX_ISB_ADDR_H) {
        val = s->px_isb_addr_h;
    } else if (addr >= WX_PX_TR_BAL(0) && addr <= WX_PX_TR_BAL(127) && ((addr - WX_PX_TR_BAL(0)) % 0x40 == 0)) {
        idx = (addr - WX_PX_TR_BAL(0)) / 0x40;
        val = s->px_tr_bal[idx];
    } else if (addr >= WX_PX_TR_BAH(0) && addr <= WX_PX_TR_BAH(127) && ((addr - WX_PX_TR_BAH(0)) % 0x40 == 0)) {
        idx = (addr - WX_PX_TR_BAH(0)) / 0x40;
        val = s->px_tr_bah[idx];
    } else if (addr >= WX_PX_TR_WP(0) && addr <= WX_PX_TR_WP(127) && ((addr - WX_PX_TR_WP(0)) % 0x40 == 0)) {
        idx = (addr - WX_PX_TR_WP(0)) / 0x40;
        val = s->px_tr_wp[idx];
    } else if (addr >= WX_PX_TR_RP(0) && addr <= WX_PX_TR_RP(127) && ((addr - WX_PX_TR_RP(0)) % 0x40 == 0)) {
        idx = (addr - WX_PX_TR_RP(0)) / 0x40;
        val = s->px_tr_rp[idx];
    } else if (addr >= WX_PX_TR_CFG(0) && addr <= WX_PX_TR_CFG(127) && ((addr - WX_PX_TR_CFG(0)) % 0x40 == 0)) {
        idx = (addr - WX_PX_TR_CFG(0)) / 0x40;
        val = s->px_tr_cfg[idx];
    } else if (addr >= WX_PX_TR_HEAD_ADDRL(0) && addr <= WX_PX_TR_HEAD_ADDRL(127) && ((addr - WX_PX_TR_HEAD_ADDRL(0)) % 0x40 == 0)) {
        idx = (addr - WX_PX_TR_HEAD_ADDRL(0)) / 0x40;
        val = s->px_tr_head_addrl[idx];
    } else if (addr >= WX_PX_TR_HEAD_ADDRH(0) && addr <= WX_PX_TR_HEAD_ADDRH(127) && ((addr - WX_PX_TR_HEAD_ADDRH(0)) % 0x40 == 0)) {
        idx = (addr - WX_PX_TR_HEAD_ADDRH(0)) / 0x40;
        val = s->px_tr_head_addrh[idx];
    } else if (addr >= WX_PX_RR_BAL(0) && addr <= WX_PX_RR_BAL(127) && ((addr - WX_PX_RR_BAL(0)) % 0x40 == 0)) {
        idx = (addr - WX_PX_RR_BAL(0)) / 0x40;
        val = s->px_rr_bal[idx];
    } else if (addr >= WX_PX_RR_BAH(0) && addr <= WX_PX_RR_BAH(127) && ((addr - WX_PX_RR_BAH(0)) % 0x40 == 0)) {
        idx = (addr - WX_PX_RR_BAH(0)) / 0x40;
        val = s->px_rr_bah[idx];
    } else if (addr >= WX_PX_RR_WP(0) && addr <= WX_PX_RR_WP(127) && ((addr - WX_PX_RR_WP(0)) % 0x40 == 0)) {
        idx = (addr - WX_PX_RR_WP(0)) / 0x40;
        val = s->px_rr_wp[idx];
    } else if (addr >= WX_PX_RR_RP(0) && addr <= WX_PX_RR_RP(127) && ((addr - WX_PX_RR_RP(0)) % 0x40 == 0)) {
        idx = (addr - WX_PX_RR_RP(0)) / 0x40;
        val = s->px_rr_rp[idx];
    /* WX_PX_RR_CFG not defined yet; commented out to compile
     * else if (addr >= WX_PX_RR_CFG(0) && addr <= WX_PX_RR_CFG(127) && ...)
     */
    } else if (addr == WX_PX_MISC_IVAR) {
        val = s->misc_ivar;
    } else if (addr >= WX_PX_IVAR(0) && addr <= WX_PX_IVAR(63) && ((addr - WX_PX_IVAR(0)) % 4 == 0)) {
        idx = (addr - WX_PX_IVAR(0)) / 4;
        val = s->px_ivars[idx];
    } else if (addr == WX_MNG_MBOX_CTL) {
        val = s->mng_mbox_ctl;
    } else if (addr == WX_MNG_SWFW_SYNC) {
        val = s->mng_swfw_sync;
    } else if (addr == WX_SW2FW_MBOX) {
        val = s->sw2fw_mbox;
    } else if (addr == WX_SW2FW_MBOX_CMD) {
        val = s->sw2fw_mbox_cmd;
    } else if (addr == WX_FW2SW_MBOX) {
        val = s->fw2sw_mbox;
    } else if (addr == WX_MNG_MBOX) {
        val = s->mng_mbox;
    } else if (addr == WX_TSC_CTL) {
        val = s->tsc_ctl;
    } else if (addr == WX_TSC_1588_CTL) {
        val = s->tsc_ctr_1588_ctl;
    } else if (addr == WX_TSC_1588_STMPH) {
        val = s->tsc_1588_stmph;
    } else if (addr == WX_TSC_1588_STMPL) {
        val = s->tsc_1588_stmpl;
    } else if (addr == WX_TSC_1588_INC) {
        val = s->tsc_1588_inc;
    } else if (addr == WX_TSC_1588_AUX_CTL) {
        val = s->tsc_1588_aux_ctl;
    } else if (addr == WX_TSC_1588_INT_EN) {
        val = s->tsc_1588_int_en;
    } else if (addr >= WX_TSC_1588_SDP(0) && addr <= WX_TSC_1588_SDP(7) && ((addr - WX_TSC_1588_SDP(0)) % 4 == 0)) {
        idx = (addr - WX_TSC_1588_SDP(0)) / 4;
        val = s->tsc_1588_sdp[idx];
    } else if (addr >= WX_TSC_1588_TRGT_L(0) && addr <= WX_TSC_1588_TRGT_L(7) && ((addr - WX_TSC_1588_TRGT_L(0)) % 8 == 0)) {
        idx = (addr - WX_TSC_1588_TRGT_L(0)) / 8;
        val = s->tsc_1588_trgt_l[idx];
    } else if (addr >= WX_TSC_1588_TRGT_H(0) && addr <= WX_TSC_1588_TRGT_H(7) && ((addr - WX_TSC_1588_TRGT_H(0)) % 8 == 0)) {
        idx = (addr - WX_TSC_1588_TRGT_H(0)) / 8;
        val = s->tsc_1588_trgt_h[idx];
    } else if (addr == WX_BME_CTL) {
        val = s->bme_ctl;
    } else if (addr == WX_TS_EN) {
        val = s->ts_en;
    } else if (addr == WX_TS_ALARM_THRE) {
        val = s->ts_alarm_thre;
    } else if (addr == WX_TS_DALARM_THRE) {
        val = s->ts_dalarm_thre;
    } else if (addr == WX_TS_INT_EN) {
        val = s->ts_int_en;
    } else if (addr == WX_PSR_1588_CTL) {
        val = s->psr_1588_ctl;
    } else if (addr == WX_PSR_1588_MSG) {
        val = s->psr_1588_msg;
    } else if (addr == WX_PSR_1588_STMPH) {
        val = s->psr_1588_stmph;
    } else if (addr == WX_PSR_VLAN_CTL) {
        val = s->psr_vlan_ctl;
    } else if (addr >= WX_PSR_VLAN_TBL(0) && addr <= WX_PSR_VLAN_TBL(63) && ((addr - WX_PSR_VLAN_TBL(0)) % 4 == 0)) {
        idx = (addr - WX_PSR_VLAN_TBL(0)) / 4;
        val = (idx < WX_PSR_VLAN_SWC_ENTRIES) ? s->psr_vlan_tbl[idx] : 0;
    } else if (addr >= WX_PSR_UC_TBL(0) && addr <= WX_PSR_UC_TBL(3) && ((addr - WX_PSR_UC_TBL(0)) % 4 == 0)) {
        idx = (addr - WX_PSR_UC_TBL(0)) / 4;
        val = s->psr_uc_tbl[idx];
    } else if (addr >= WX_PSR_MC_TBL(0) && addr <= WX_PSR_MC_TBL(127) && ((addr - WX_PSR_MC_TBL(0)) % 4 == 0)) {
        idx = (addr - WX_PSR_MC_TBL(0)) / 4;
        val = s->psr_mc_tbl[idx];
    } else if (addr == WX_PSR_VM_CTL) {
        val = s->psr_vm_ctl;
    } else if (addr >= WX_PSR_VM_L2CTL(0) && addr <= WX_PSR_VM_L2CTL(127) && ((addr - WX_PSR_VM_L2CTL(0)) % 4 == 0)) {
        idx = (addr - WX_PSR_VM_L2CTL(0)) / 4;
        val = s->psr_vm_l2ctl[idx];
    } else if (addr == WX_RDM_RSC_CTL) {
        val = s->rd_rsc_ctl;
    } else if (addr == WX_RSC_ST) {
        val = s->rd_rsc_st;
    } else if (addr == WX_RDB_RA_CTL) {
        val = s->rd_ra_ctl;
    } else if (addr == WX_RDB_FDIR_CTL) {
        val = s->rd_fdir_ctl;
    } else if (addr == WX_RDB_FDIR_CMD) {
        val = s->rd_fdir_cmd;
    } else if (addr == WX_RDB_FDIR_HASH) {
        val = s->rd_fdir_hash;
    } else if (addr == WX_RDB_FDIR_SA) {
        val = s->rd_fdir_sa;
    } else if (addr == WX_RDB_FDIR_DA) {
        val = s->rd_fdir_da;
    } else if (addr == WX_RDB_FDIR_PORT) {
        val = s->rd_fdir_port;
    } else if (addr == WX_RDB_FDIR_FLEX) {
        val = s->rd_fdir_flex;
    } else if (addr == WX_RDB_FDIR_SKEY) {
        val = s->rd_fdir_skey;
    } else if (addr == WX_RDB_FDIR_HKEY) {
        val = s->rd_fdir_hkey;
    } else if (addr == WX_RDB_FDIR_TCP_MSK) {
        val = s->rd_fdir_tcp_msk;
    } else if (addr == WX_RDB_FDIR_UDP_MSK) {
        val = s->rd_fdir_udp_msk;
    } else if (addr == WX_RDB_FDIR_SCTP_MSK) {
        val = s->rd_fdir_sctp_msk;
    } else if (addr == WX_RDB_FDIR_OTHER_MSK) {
        val = s->rd_fdir_other_msk;
    } else if (addr >= WX_RDB_FDIR_FLEX_CFG(0) && addr <= WX_RDB_FDIR_FLEX_CFG(63) && ((addr - WX_RDB_FDIR_FLEX_CFG(0)) % 4 == 0)) {
        idx = (addr - WX_RDB_FDIR_FLEX_CFG(0)) / 4;
        val = s->rd_fdir_flex_cfg[idx];
    } else if (addr >= WX_RDB_RSSTBL(0) && addr <= WX_RDB_RSSTBL(63) && ((addr - WX_RDB_RSSTBL(0)) % 4 == 0)) {
        idx = (addr - WX_RDB_RSSTBL(0)) / 4;
        val = s->rd_rsstbl[idx];
    } else if (addr >= WX_RDB_RSSRK(0) && addr <= WX_RDB_RSSRK(9) && ((addr - WX_RDB_RSSRK(0)) % 4 == 0)) {
        idx = (addr - WX_RDB_RSSRK(0)) / 4;
        val = s->rd_rssrk[idx];
    } else if (addr >= WX_RDB_PB_SZ(0) && addr <= WX_RDB_PB_SZ(7) && ((addr - WX_RDB_PB_SZ(0)) % 4 == 0)) {
        idx = (addr - WX_RDB_PB_SZ(0)) / 4;
        val = s->rd_pb_sz[idx];
    } else if (addr >= WX_TDB_PB_SZ(0) && addr <= WX_TDB_PB_SZ(7) && ((addr - WX_TDB_PB_SZ(0)) % 4 == 0)) {
        idx = (addr - WX_TDB_PB_SZ(0)) / 4;
        val = s->td_pb_sz[idx];
    } else if (addr >= WX_TDM_PB_THRE(0) && addr <= WX_TDM_PB_THRE(7) && ((addr - WX_TDM_PB_THRE(0)) % 4 == 0)) {
        idx = (addr - WX_TDM_PB_THRE(0)) / 4;
        val = s->tdm_pb_thre[idx];
    } else if (addr >= WX_RDB_PL_CFG(0) && addr <= WX_RDB_PL_CFG(127) && ((addr - WX_RDB_PL_CFG(0)) % 4 == 0)) {
        idx = (addr - WX_RDB_PL_CFG(0)) / 4;
        val = s->rd_pl_cfg[idx];
    } else if (addr == WX_MAC_LXONRXC) {
        val = s->mac_lxonrxc;
    } else if (addr == WX_MAC_LXOFFRXC) {
        val = s->mac_lxoffrxc;
    } else if (addr == WX_MAC_LXONRXC_AML) {
        val = s->mac_lxonrxc_aml;
    } else if (addr == WX_MAC_LXOFFRXC_AML) {
        val = s->mac_lxoffrxc_aml;
    } else if (addr == WX_MAC_PKT_FLT) {
        val = s->mac_pkt_flt;
    } else if (addr == WX_MMC_CONTROL) {
        val = s->mmc_control;
    } else if (addr == WX_MAC_RX_FLOW_CTRL) {
        val = s->mac_rx_flow_ctrl;
    } else if (addr >= WX_PX_MPRC(0) && addr <= WX_PX_MPRC(127) && ((addr - WX_PX_MPRC(0)) % 0x40 == 0)) {
        idx = (addr - WX_PX_MPRC(0)) / 0x40;
        val = s->px_mpc[idx];
    } else if (addr == WX_PX_ITRSEL) {
        val = 0;
    } else if (addr == WX_MDIO_CLAUSE_SELECT) {
        val = 0;
    } else if (addr == TXGBE_XPCS_IDA_ADDR) {
        val = 0;
    } else if (addr == TXGBE_XPCS_IDA_DATA) {
        val = 0;
    } else if (addr >= WX_TDM_MAC_AS(0) && addr <= WX_TDM_MAC_AS(127) && ((addr - WX_TDM_MAC_AS(0)) % 4 == 0)) {
        idx = (addr - WX_TDM_MAC_AS(0)) / 4;
        val = 0;
    } else if (addr >= WX_TDM_VLAN_AS(0) && addr <= WX_TDM_VLAN_AS(127) && ((addr - WX_TDM_VLAN_AS(0)) % 4 == 0)) {
        idx = (addr - WX_TDM_VLAN_AS(0)) / 4;
        val = 0;
    } else if (addr >= WX_TDM_ETYPE_AS(0) && addr <= WX_TDM_ETYPE_AS(127) && ((addr - WX_TDM_ETYPE_AS(0)) % 4 == 0)) {
        idx = (addr - WX_TDM_ETYPE_AS(0)) / 4;
        val = 0;
    } else if (addr == WX_SPI_CMD) {
        val = s->spi_cmd;
    } else if (addr == WX_SPI_STATUS) {
        val = s->spi_status;
    } else if (addr == WX_RX_UNDERSIZE_FRAMES_GOOD ||
               addr == WX_RX_MC_FRAMES_GOOD_L ||
               addr == WX_TDM_OS2BMC_CNT ||
               addr == WX_RX_FRAME_CNT_GOOD_BAD_L ||
               addr == WX_RDB_LXOFFTXC ||
               addr == WX_RX_OVERSIZE_FRAMES_GOOD ||
               addr == WX_TX_FRAME_CNT_GOOD_BAD_L ||
               addr == WX_TX_MC_FRAMES_GOOD_L ||
               addr == WX_RDB_FDIR_MATCH ||
               addr == WX_RDM_BYTE_CNT_LSB ||
               addr == WX_RX_LEN_ERROR_FRAMES_L ||
               addr == WX_RX_CRC_ERROR_FRAMES_L ||
               addr == WX_RDB_LXONTXC ||
               addr == WX_RX_BC_FRAMES_GOOD_L ||
               addr == WX_TX_BC_FRAMES_GOOD_L ||
               addr == WX_MNG_BMC2OS_CNT ||
               addr == WX_RDB_FDIR_MISS ||
               addr == WX_RDM_BMC2OS_CNT ||
               addr == WX_TDM_PKT_CNT ||
               addr == WX_RDM_PKT_CNT ||
               addr == WX_MNG_OS2BMC_CNT ||
               addr == WX_RDM_DRP_PKT ||
               addr == WX_TDM_BYTE_CNT_LSB ||
               addr == WX_TS_CTL) {
        val = 0;
    } else {
        val = 0;
    }
    return val;
}

/* MMIO write handler - rewritten */
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = PCIBASE_DEVICE(opaque);
    int idx;

    if (addr == WX_MIS_RST) {
        s->mis_rst = val;
    } else if (addr == WX_MIS_ST) {
        s->mis_st = val;
    } else if (addr == WX_PX_GPIE) {
        s->px_gpie = val;
    } else if (addr == WX_PX_MISC_IEN) {
        s->px_misc_ien = val;
        pcibase_update_irq(s);
    } else if (addr == WX_PX_MISC_IC) {
        s->px_ic[0] = val;
        pcibase_update_irq(s);
    } else if (addr >= WX_PX_IMS(0) && addr <= WX_PX_IMS(127) && ((addr - WX_PX_IMS(0)) % 4 == 0)) {
        idx = (addr - WX_PX_IMS(0)) / 4;
        if (idx < 128) {
            s->px_ims[idx] = val;
            pcibase_update_irq(s);
        }
    } else if (addr >= WX_PX_IMC(0) && addr <= WX_PX_IMC(127) && ((addr - WX_PX_IMC(0)) % 4 == 0)) {
        idx = (addr - WX_PX_IMC(0)) / 4;
        if (idx < 128) {
            s->px_imc[idx] = val;
            pcibase_update_irq(s);
        }
    } else if (addr >= WX_PX_IC(0) && addr <= WX_PX_IC(127) && ((addr - WX_PX_IC(0)) % 4 == 0)) {
        idx = (addr - WX_PX_IC(0)) / 4;
        if (idx < 128) s->px_ic[idx] = val;
    } else if (addr >= WX_PX_ITR(0) && addr <= WX_PX_ITR(127) && ((addr - WX_PX_ITR(0)) % 4 == 0)) {
        idx = (addr - WX_PX_ITR(0)) / 4;
        if (idx < 128) s->px_itr[idx] = val;
    } else if (addr >= WX_PX_IVAR(0) && addr <= WX_PX_IVAR(63) && ((addr - WX_PX_IVAR(0)) % 4 == 0)) {
        idx = (addr - WX_PX_IVAR(0)) / 4;
        if (idx < 64) s->px_ivar[idx] = val;
    } else if (addr == WX_PSR_CTL) {
        s->psr_ctl = val;
    } else if (addr == WX_PSR_MAC_SWC_IDX) {
        s->psr_mac_swc_idx = val;
    } else if (addr == WX_PSR_MAC_SWC_AD_L) {
        s->psr_mac_swc_ad_l = val;
    } else if (addr == WX_PSR_MAC_SWC_AD_H) {
        s->psr_mac_swc_ad_h = val;
    } else if (addr == WX_PSR_MAC_SWC_VM_L) {
        s->psr_mac_swc_vm_l = val;
    } else if (addr == WX_PSR_MAC_SWC_VM_H) {
        s->psr_mac_swc_vm_h = val;
    } else if (addr == WX_RDB_PB_CTL) {
        s->rd_pb_ctl = val;
    } else if (addr == WX_RSC_CTL) {
        s->rsc_ctl = val;
    } else if (addr == WX_MAC_TX_CFG) {
        s->mac_tx_cfg = val;
    } else if (addr == WX_MAC_RX_CFG) {
        s->mac_rx_cfg = val;
    } else if (addr == WX_TDM_CTL) {
        s->tdm_ctl = val;
    } else if (addr == WX_CFG_PORT_CTL) {
        s->cfg_port_ctl = val;
    } else if (addr == WX_CFG_PORT_ST) {
        s->cfg_port_st = val;
    } else if (addr == WX_SPI_STATUS) {
        s->spi_status = val;
    } else if (addr == WX_SPI_ILDR_STATUS) {
        s->spi_ildr_status = val;
    } else if (addr == WX_GPIO_DR) {
        s->gpio_dr = val;
    } else if (addr == WX_GPIO_DDR) {
        s->gpio_ddr = val;
    } else if (addr == WX_GPIO_INTEN) {
        s->gpio_inten = val;
    } else if (addr == WX_GPIO_INTSTATUS) {
        s->gpio_intstatus = val;
    } else if (addr == WX_GPIO_INTTYPE_LEVEL) {
        s->gpio_inttype_level = val;
    } else if (addr == WX_GPIO_EXT) {
        /* read-only */
    } else if (addr == WX_GPIO_EOI) {
        /* write-only, ignore */
    } else if (addr == WX_PX_ISB_ADDR_L) {
        s->px_isb_addr_l = val;
    } else if (addr == WX_PX_ISB_ADDR_H) {
        s->px_isb_addr_h = val;
    } else if (addr >= WX_PX_TR_BAL(0) && addr <= WX_PX_TR_BAL(127) && ((addr - WX_PX_TR_BAL(0)) % 0x40 == 0)) {
        idx = (addr - WX_PX_TR_BAL(0)) / 0x40;
        if (idx < 128) s->px_tr_bal[idx] = val;
    } else if (addr >= WX_PX_TR_BAH(0) && addr <= WX_PX_TR_BAH(127) && ((addr - WX_PX_TR_BAH(0)) % 0x40 == 0)) {
        idx = (addr - WX_PX_TR_BAH(0)) / 0x40;
        if (idx < 128) s->px_tr_bah[idx] = val;
    } else if (addr >= WX_PX_TR_WP(0) && addr <= WX_PX_TR_WP(127) && ((addr - WX_PX_TR_WP(0)) % 0x40 == 0)) {
        idx = (addr - WX_PX_TR_WP(0)) / 0x40;
        if (idx < 128) s->px_tr_wp[idx] = val;
    } else if (addr >= WX_PX_TR_RP(0) && addr <= WX_PX_TR_RP(127) && ((addr - WX_PX_TR_RP(0)) % 0x40 == 0)) {
        idx = (addr - WX_PX_TR_RP(0)) / 0x40;
        if (idx < 128) s->px_tr_rp[idx] = val;
    } else if (addr >= WX_PX_TR_CFG(0) && addr <= WX_PX_TR_CFG(127) && ((addr - WX_PX_TR_CFG(0)) % 0x40 == 0)) {
        idx = (addr - WX_PX_TR_CFG(0)) / 0x40;
        if (idx < 128) s->px_tr_cfg[idx] = val;
    } else if (addr >= WX_PX_TR_HEAD_ADDRL(0) && addr <= WX_PX_TR_HEAD_ADDRL(127) && ((addr - WX_PX_TR_HEAD_ADDRL(0)) % 0x40 == 0)) {
        idx = (addr - WX_PX_TR_HEAD_ADDRL(0)) / 0x40;
        if (idx < 128) s->px_tr_head_addrl[idx] = val;
    } else if (addr >= WX_PX_TR_HEAD_ADDRH(0) && addr <= WX_PX_TR_HEAD_ADDRH(127) && ((addr - WX_PX_TR_HEAD_ADDRH(0)) % 0x40 == 0)) {
        idx = (addr - WX_PX_TR_HEAD_ADDRH(0)) / 0x40;
        if (idx < 128) s->px_tr_head_addrh[idx] = val;
    } else if (addr >= WX_PX_RR_BAL(0) && addr <= WX_PX_RR_BAL(127) && ((addr - WX_PX_RR_BAL(0)) % 0x40 == 0)) {
        idx = (addr - WX_PX_RR_BAL(0)) / 0x40;
        if (idx < 128) s->px_rr_bal[idx] = val;
    } else if (addr >= WX_PX_RR_BAH(0) && addr <= WX_PX_RR_BAH(127) && ((addr - WX_PX_RR_BAH(0)) % 0x40 == 0)) {
        idx = (addr - WX_PX_RR_BAH(0)) / 0x40;
        if (idx < 128) s->px_rr_bah[idx] = val;
    } else if (addr >= WX_PX_RR_WP(0) && addr <= WX_PX_RR_WP(127) && ((addr - WX_PX_RR_WP(0)) % 0x40 == 0)) {
        idx = (addr - WX_PX_RR_WP(0)) / 0x40;
        if (idx < 128) s->px_rr_wp[idx] = val;
    } else if (addr >= WX_PX_RR_RP(0) && addr <= WX_PX_RR_RP(127) && ((addr - WX_PX_RR_RP(0)) % 0x40 == 0)) {
        idx = (addr - WX_PX_RR_RP(0)) / 0x40;
        if (idx < 128) s->px_rr_rp[idx] = val;
    /* WX_PX_RR_CFG not defined yet; commented out */
    } else if (addr == WX_PX_MISC_IVAR) {
        s->misc_ivar = val;
    } else if (addr >= WX_PX_IVAR(0) && addr <= WX_PX_IVAR(63) && ((addr - WX_PX_IVAR(0)) % 4 == 0)) {
        idx = (addr - WX_PX_IVAR(0)) / 4;
        if (idx < 64) s->px_ivars[idx] = val                  {
        s->mng_mbox_ctl = val;
    } else if (addr == WX_MNG_SWFW_SYNC) {
        s->mng_swfw_sync = val;
    } else if (addr == WX_SW2FW_MBOX) {
        s->sw2fw_mbox = val;
    } else if (addr == WX_SW2FW_MBOX_CMD) {
        s->sw2fw_mbox_cmd = val;
    } else if (addr == WX_FW2SW_MBOX) {
        s->fw2sw_mbox = val;
    } else if (addr == WX_MNG_MBOX) {
        s->mng_mbox = val;
    } else if (addr == WX_TSC_CTL) {
        s->tsc_ctl = val;
    } else if (addr == WX_TSC_1588_CTL) {
        s->tsc_ctr_1588_ctl = val;
    } else if (addr == WX_TSC_1588_STMPH) {
        s->tsc_1588_stmph = val;
    } else if (addr == WX_TSC_1588_STMPL) {
        s->tsc_1588_stmpl = val;
    } else if (addr == WX_TSC_1588_INC) {
        s->tsc_1588_inc = val;
    } else if (addr == WX_TSC_1588_AUX_CTL) {
        s->tsc_1588_aux_ctl = val;
    } else if (addr == WX_TSC_1588_INT_EN) {
        s->tsc_1588_int_en = val;
    } else if (addr >= WX_TSC_1588_SDP(0) && addr <= WX_TSC_1588_SDP(7) && ((addr - WX_TSC_1588_SDP(0)) % 4 == 0)) {
        idx = (addr - WX_TSC_1588_SDP(0)) / 4;
        if (idx < 8) s->tsc_1588_sdp[idx] = val;
    } else if (addr >= WX_TSC_1588_TRGT_L(0) && addr <= WX_TSC_1588_TRGT_L(7) && ((addr - WX_TSC_1588_TRGT_L(0)) % 8 == 0)) {
        idx = (addr - WX_TSC_1588_TRGT_L(0)) / 8;
        if (idx < 8) s->tsc_1588_trgt_l[idx] = val;
    } else if (addr >= WX_TSC_1588_TRGT_H(0) && addr <= WX_TSC_1588_TRGT_H(7) && ((addr - WX_TSC_1588_TRGT_H(0)) % 8 == 0)) {
        idx = (addr - WX_TSC_1588_TRGT_H(0)) / 8;
        if (idx < 8) s->tsc_1588_trgt_h[idx] = val;
    } else if (addr == WX_BME_CTL) {
        s->bme_ctl = val;
    } else if (addr == WX_TS_EN) {
        s->ts_en = val;
    } else if (addr == WX_TS_ALARM_THRE) {
        s->ts_alarm_thre = val;
    } else if (addr == WX_TS_DALARM_THRE) {
        s->ts_dalarm_thre = val;
    } else if (addr == WX_TS_INT_EN) {
        s->ts_int_en = val;
    } else if (addr == WX_PSR_1588_CTL) {
        s->psr_1588_ctl = val;
    } else if (addr == WX_PSR_1588_MSG) {
        s->psr_1588_msg = val;
    } else if (addr == WX_PSR_1588_STMPH) {
        s->psr_1588_stmph = val;
    } else if (addr == WX_PSR_VLAN_CTL) {
        s->psr_vlan_ctl = val;
    } else if (addr >= WX_PSR_VLAN_TBL(0) && addr <= WX_PSR_VLAN_TBL(63) && ((addr - WX_PSR_VLAN_TBL(0)) % 4 == 0)) {
        idx = (addr - WX_PSR_VLAN_TBL(0)) / 4;
        if (idx < WX_PSR_VLAN_SWC_ENTRIES) s->psr_vlan_tbl[idx] = val;
    } else if (addr >= WX_PSR_UC_TBL(0) && addr <= WX_PSR_UC_TBL(3) && ((addr - WX_PSR_UC_TBL(0)) % 4 == 0)) {
        idx = (addr - WX_PSR_UC_TBL(0)) / 4;
        if (idx < 4) s->psr_uc_tbl[idx] = val;
    } else if (addr >= WX_PSR_MC_TBL(0) && addr <= WX_PSR_MC_TBL(127) && ((addr - WX_PSR_MC_TBL(0)) % 4 == 0)) {
        idx = (addr - WX_PSR_MC_TBL(0)) / 4;
        if (idx < 128) s->psr_mc_tbl[idx] = val;
    } else if (addr == WX_PSR_VM_CTL) {
        s->psr_vm_ctl = val;
    } else if (addr >= WX_PSR_VM_L2CTL(0) && addr <= WX_PSR_VM_L2CTL(127) && ((addr - WX_PSR_VM_L2CTL(0)) % 4 == 0)) {
        idx = (addr - WX_PSR_VM_L2CTL(0)) / 4;
        if (idx < 128) s->psr_vm_l2ctl[idx] = val;
    } else if (addr == WX_RDM_RSC_CTL) {
        s->rd_rsc_ctl = val;
    } else if (addr == WX_RSC_ST) {
        s->rd_rsc_st = val;
    } else if (addr == WX_RDB_RA_CTL) {
        s->rd_ra_ctl = val;
    } else if (addr == WX_RDB_FDIR_CTL) {
        s->rd_fdir_ctl = val;
    } else if (addr == WX_RDB_FDIR_CMD) {
        s->rd_fdir_cmd = val;
    } else if (addr == WX_RDB_FDIR_HASH) {
        s->rd_fdir_hash = val;
    } else if (addr == WX_RDB_FDIR_SA) {
        s->rd_fdir_sa = val;
    } else if (addr == WX_RDB_FDIR_DA) {
        s->rd_fdir_da = val;
    } else if (addr == WX_RDB_FDIR_PORT) {
        s->rd_fdir_port = val;
    } else if (addr == WX_RDB_FDIR_FLEX) {
        s->rd_fdir_flex = val;
    } else if (addr == WX_RDB_FDIR_SKEY) {
        s->rd_fdir_skey = val;
    } else if (addr == WX_RDB_FDIR_HKEY) {
        s->rd_fdir_hkey = val;
    } else if (addr == WX_RDB_FDIR_TCP_MSK) {
        s->rd_fdir_tcp_msk = val;
    } else if (addr == WX_RDB_FDIR_UDP_MSK) {
        s->rd_fdir_udp_msk = val;
    } else if (addr == WX_RDB_FDIR_SCTP_MSK) {
        s->rd_fdir_sctp_msk = val;
    } else if (addr == WX_RDB_FDIR_OTHER_MSK) {
        s->rd_fdir_other_msk = val;
    } else if (addr >= WX_RDB_FDIR_FLEX_CFG(0) && addr <= WX_RDB_FDIR_FLEX_CFG(63) && ((addr - WX_RDB_FDIR_FLEX_CFG(0)) % 4 == 0)) {
        idx = (addr - WX_RDB_FDIR_FLEX_CFG(0)) / 4;
        if (idx < 64) s->rd_fdir_flex_cfg[idx] = val;
    } else if (addr >= WX_RDB_RSSTBL(0) && addr <= WX_RDB_RSSTBL(63) && ((addr - WX_RDB_RSSTBL(0)) % 4 == 0)) {
        idx = (addr - WX_RDB_RSSTBL(0)) / 4;
        if (idx < 64) s->rd_rsstbl[idx] = val;
    } else if (addr >= WX_RDB_RSSRK(0) && addr <= WX_RDB_RSSRK(9) && ((addr - WX_RDB_RSSRK(0)) % 4 == 0)) {
        idx = (addr - WX_RDB_RSSRK(0)) / 4;
        if (idx < 10) s->rd_rssrk[idx] = val;
    } else if (addr >= WX_RDB_PB_SZ(0) && addr <= WX_RDB_PB_SZ(7) && ((addr - WX_RDB_PB_SZ(0)) % 4 == 0)) {
        idx = (addr - WX_RDB_PB_SZ(0)) / 4;
        if (idx < 8) s->rd_pb_sz[idx] = val;
    } else if (addr >= WX_TDB_PB_SZ(0) && addr <= WX_TDB_PB_SZ(7) && ((addr - WX_TDB_PB_SZ(0)) % 4 == 0)) {
        idx = (addr - WX_TDB_PB_SZ(0)) / 4;
        if (idx < 8) s->td_pb_sz[idx] = val;
    } else if (addr >= WX_TDM_PB_THRE(0) && addr <= WX_TDM_PB_THRE(7) && ((addr - WX_TDM_PB_THRE(0)) % 4 == 0)) {
        idx = (addr - WX_TDM_PB_THRE(0)) / 4;
        if (idx < 8) s->tdm_pb_thre[idx] = val;
    } else if (addr >= WX_RDB_PL_CFG(0) && addr <= WX_RDB_PL_CFG(127) && ((addr - WX_RDB_PL_CFG(0)) % 4 == 0)) {
        idx = (addr - WX_RDB_PL_CFG(0)) / 4;
        if (idx < 128) s->rd_pl_cfg[idx] = val;
    } else if (addr == WX_MAC_LXONRXC) {
        s->mac_lxonrxc = val;
    } else if (addr == WX_MAC_LXOFFRXC) {
        s->mac_lxoffrxc = val;
    } else if (addr == WX_MAC_LXONRXC_AML) {
        s->mac_lxonrxc_aml = val;
    } else if (addr == WX_MAC_LXOFFRXC_AML) {
        s->mac_lxoffrxc_aml = val;
    } else if (addr == WX_MAC_PKT_FLT) {
        s->mac_pkt_flt = val;
    } else if (addr == WX_MMC_CONTROL) {
        s->mmc_control = val;
    } else if (addr == WX_MAC_RX_FLOW_CTRL) {
        s->mac_rx_flow_ctrl = val;
    } else if (addr >= WX_PX_MPRC(0) && addr <= WX_PX_MPRC(127) && ((addr - WX_PX_MPRC(0)) % 0x40 == 0)) {
        idx = (addr - WX_PX_MPRC(0)) / 0x40;
        if (idx < 128) s->px_mpc[idx] = val;
    } else if (addr == WX_PX_ITRSEL) {
        /* may be writable */
    } else if (addr == WX_MDIO_CLAUSE_SELECT) {
    } else if (addr == TXGBE_XPCS_IDA_ADDR) {
    } else if (addr == TXGBE_XPCS_IDA_DATA) {
    } else if (addr == WX_SPI_CMD) {
        s->spi_cmd = val;
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Reset all registers to power-on defaults.
     * Values chosen to allow driver probe to succeed: */
    s->mis_st = WX_MIS_ST_MNG_INIT_DN; /* management initialization done */
    s->spi_ildr_status = WX_SPI_ILDR_STATUS_PERST | WX_SPI_ILDR_STATUS_PWRRST; /* flash load done */
    s->cfg_port_st = 0; /* default LAN ID? */
    /* Other defaults: many registers are 0 after reset. */
    memset(s->px_itr, 0, sizeof(s->px_itr));
    memset(s->px_ic, 0, sizeof(s->px_ic));
    memset(s->px_ims, 0, sizeof(s->px_ims));
    memset(s->px_imc, 0, sizeof(s->px_imc));
    memset(s->px_tr_bal, 0, sizeof(s->px_tr_bal));
    memset(s->px_tr_bah, 0, sizeof(s->px_tr_bah));
    memset(s->px_tr_wp, 0, sizeof(s->px_tr_wp));
    memset(s->px_tr_rp, 0, sizeof(s->px_tr_rp));
    memset(s->px_tr_cfg, 0, sizeof(s->px_tr_cfg));
    memset(s->px_rr_bal, 0, sizeof(s->px_rr_bal));
    memset(s->px_rr_bah, 0, sizeof(s->px_rr_bah));
    memset(s->px_rr_wp, 0, sizeof(s->px_rr_wp));
    memset(s->px_rr_rp, 0, sizeof(s->px_rr_rp));
    /* MAC address: set a valid dummy address */
    s->mac_addr[0] = 0x00;
    s->mac_addr[1] = 0x11;
    s->mac_addr[2] = 0x22;
    s->mac_addr[3] = 0x33;
    s->mac_addr[4] = 0x44;
    s->mac_addr[5] = 0x55;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  WANGXUN_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  TXGBE_DEV_ID_SP1000 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_ETHERNET );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* MSI-X capability */
    int msix_bar_idx = 0; /* Use BAR0 for MSI-X table */
    int msix_table_offset = 0x8000; /* After register space, needs to be large enough */
    int msix_pba_offset = msix_table_offset + 0x1000 * msix_nr_vectors_allocated(pdev); /* adjust */
    /* Allocate some MSI-X vectors; driver expects up to 64 maybe */
    if (msix_init(pdev, 64, &s->bar_regions[msix_bar_idx], msix_bar_idx, msix_table_offset,
                  &s->bar_regions[msix_bar_idx], msix_bar_idx, msix_pba_offset, 0, errp)) {
        return;
    }

    /* BAR Initialization */
    /* BAR0 is MMIO as indicated by pci_resource_start(pdev, 0) in driver.
     * Size: make it large enough to cover all registers and MSI-X (e.g., 1 MB) */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x100000; /* 1 MB */
    s->bar_info[0].name = "txgbe-mmio";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Final state initialization placeholder */
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

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "txgbe_pci",
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
