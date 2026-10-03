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

#define TYPE_PCIBASE_DEVICE "igb_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
/* Define VENDOR_ID, DEVICE_ID, CLASS_ID, and register offsets here */
#define VENDOR_ID 0x8086
#define DEVICE_ID 0x1F40
#define CLASS_ID  0x0200  /* Ethernet Controller */
#define BAR0_SIZE 0x20000

/* Register Offsets */
#define E1000_CTRL         0x00000
#define E1000_STATUS       0x00008
#define E1000_CTRL_EXT     0x00018
#define E1000_ICR          0x000C0
#define E1000_RCTL         0x00100
#define E1000_TCTL         0x00400
#define E1000_TDFH         0x03410
#define E1000_TDFT         0x03418
#define E1000_TDFHS        0x03420
#define E1000_TDFPC        0x03430
#define E1000_EECD         0x00010
#define E1000_EERD         0x00014
#define E1000_FLA          0x0001C
#define E1000_MDIC         0x00020
#define E1000_SCTL         0x00024
#define E1000_FCAL         0x00028
#define E1000_FCAH         0x0002C
#define E1000_FCT          0x00030
#define E1000_CONNSW       0x00034
#define E1000_VET          0x00038
#define E1000_TSSDP        0x0003C
#define E1000_TIPG         0x00410
#define E1000_I2CCMD       0x01028
#define E1000_I2CPARAMS    0x0102C
#define E1000_PBA          0x01000
#define E1000_PBS          0x01008
#define E1000_EEC          0x00010
#define E1000_EERD         0x00014
#define E1000_FLA          0x0001C
#define E1000_MDIC         0x00020
#define E1000_SCTL         0x00024
#define E1000_FCAL         0x00028
#define E1000_FCAH         0x0002C
#define E1000_FCT          0x00030
#define E1000_CONNSW       0x00034
#define E1000_VET          0x00038
#define E1000_TSSDP        0x0003C
#define E1000_TIPG         0x00410
#define E1000_I2CCMD       0x01028
#define E1000_I2CPARAMS    0x0102C
#define E1000_PBA          0x01000
#define E1000_PBS          0x01008
#define E1000_IMS          0x000D0
#define E1000_IMC          0x000D8
#define E1000_IAM          0x000E0
#define E1000_EIMS         0x01524
#define E1000_EIMC         0x01528
#define E1000_EIAC         0x0152C
#define E1000_EIAM         0x01530
#define E1000_ICS          0x000C8
#define E1000_EICS         0x01520
#define E1000_EITR(_n)     (0x01680 + (0x4 * (_n)))
#define E1000_IVAR0        0x01700
#define E1000_IVAR_MISC    0x01740
#define E1000_GPIE         0x01514
#define E1000_MSIXBM(_i)    (0x01600 + ((_i) * 4))
#define E1000_RDBAL(_n)   ((_n) < 4 ? (0x02800 + ((_n) * 0x100)) : (0x0C000 + ((_n) * 0x40)))
#define E1000_RDBAH(_n)   ((_n) < 4 ? (0x02804 + ((_n) * 0x100)) : (0x0C004 + ((_n) * 0x40)))
#define E1000_RDLEN(_n)   ((_n) < 4 ? (0x02808 + ((_n) * 0x100)) : (0x0C008 + ((_n) * 0x40)))
#define E1000_RDH(_n)     ((_n) < 4 ? (0x02810 + ((_n) * 0x100)) : (0x0C010 + ((_n) * 0x40)))
#define E1000_RDT(_n)     ((_n) < 4 ? (0x02818 + ((_n) * 0x100)) : (0x0C018 + ((_n) * 0x40)))
#define E1000_RXDCTL(_n)  ((_n) < 4 ? (0x02828 + ((_n) * 0x100)) : (0x0C028 + ((_n) * 0x40)))
#define E1000_TDBAL(_n)   ((_n) < 4 ? (0x03800 + ((_n) * 0x100)) : (0x0E000 + ((_n) * 0x40)))
#define E1000_TDBAH(_n)   ((_n) < 4 ? (0x03804 + ((_n) * 0x100)) : (0x0E004 + ((_n) * 0x40)))
#define E1000_TDLEN(_n)   ((_n) < 4 ? (0x03808 + ((_n) * 0x100)) : (0x0E008 + ((_n) * 0x40)))
#define E1000_TDH(_n)     ((_n) < 4 ? (0x03810 + ((_n) * 0x100)) : (0x0E010 + ((_n) * 0x40)))
#define E1000_TDT(_n)     ((_n) < 4 ? (0x03818 + ((_n) * 0x100)) : (0x0E018 + ((_n) * 0x40)))
#define E1000_TXDCTL(_n)  ((_n) < 4 ? (0x03828 + ((_n) * 0x100)) : (0x0E028 + ((_n) * 0x40)))
#define E1000_TXCTL(_n)   ((_n) < 4 ? (0x03814 + ((_n) * 0x100)) : (0x0E014 + ((_n) * 0x40)))
#define E1000_RXCTL(_n)   ((_n) < 4 ? (0x02814 + ((_n) * 0x100)) : (0x0C014 + ((_n) * 0x40)))
#define E1000_TDWBAL(_n)  ((_n) < 4 ? (0x03838 + ((_n) * 0x100)) : (0x0E038 + ((_n) * 0x40)))
#define E1000_TDWBAH(_n)  ((_n) < 4 ? (0x0383C + ((_n) * 0x100)) : (0x0E03C + ((_n) * 0x40)))
#define E1000_RXCSUM       0x05000
#define E1000_RFCTL        0x05008
#define E1000_SRRCTL(_n)  ((_n) < 4 ? (0x0280C + ((_n) * 0x100)) : (0x0C00C + ((_n) * 0x40)))
#define E1000_RLPML        0x05004
#define E1000_VT_CTL       0x0581C
#define E1000_MRQC         0x05818
#define E1000_RSSRK(_i)    (0x05C80 + ((_i) * 4))
#define E1000_RETA(_i)     (0x05C00 + ((_i) * 4))
#define E1000_VFTA         0x00C90
#define E1000_VFTE         0x00C90
#define E1000_UTA          0x0A000
#define E1000_WUC           0x05800
#define E1000_WUFC          0x05808
#define E1000_WUS           0x05810
#define E1000_WUPL          0x05900
#define E1000_WUPM_REG(_i)  (0x05A00 + ((_i) * 4))
#define E1000_MTA           0x05200
#define E1000_RAL(_i)       (((_i) <= 15) ? (0x05400 + ((_i) * 8)) : (0x054E0 + ((_i - 16) * 8)))
#define E1000_RAH(_i)       (((_i) <= 15) ? (0x05404 + ((_i) * 8)) : (0x054E4 + ((_i - 16) * 8)))
#define E1000_PSRTYPE(_i)   (0x05480 + ((_i) * 4))
#define E1000_IMIR(_i)      (0x05A80 + ((_i) * 4))
#define E1000_IMIREXT(_i)   (0x05AA0 + ((_i) * 4))
#define E1000_IMIRVP        0x05AC0
#define E1000_VMVIR(_n)     (0x03700 + (4 * (_n)))
#define E1000_VMOLR(_n)     (0x05AD0 + (4 * (_n)))
#define E1000_DVMOLR(_n)    (0x0C038 + (64 * (_n)))
#define E1000_VLVF(_n)      (0x05D00 + (4 * (_n)))
#define E1000_IP4AT_REG(_i) (0x05840 + ((_i) * 8))
#define E1000_IP6AT_REG(_i) (0x05880 + ((_i) * 4))
#define E1000_FFMT_REG(_i)  (0x09000 + ((_i) * 8))
#define E1000_FFVT_REG(_i)  (0x09800 + ((_i) * 8))
#define E1000_FFLT_REG(_i)  (0x05F00 + ((_i) * 8))
#define E1000_ETQF(_n)      (0x05CB0 + (4 * (_n)))
#define E1000_FTQF(_n)      (0x59E0 + 4 * (_n))
#define E1000_SPQF(_n)      (0x59C0 + 4 * (_n))
#define E1000_VLAPQF        0x055B0
#define E1000_TCPTIMER      0x0104C
#define E1000_FRTIMER       0x01048
#define E1000_DMACR         0x02508
#define E1000_DMCTLX        0x02514
#define E1000_DMCTXTH       0x03550
#define E1000_DMCRTRH       0x05DD0
#define E1000_FCRTC         0x02170
#define E1000_DTXSWC        0x03500
#define E1000_DTXCTL        0x03590
#define E1000_RPLOLR        0x05AF0
#define E1000_RXPBS         0x02404
#define E1000_TXPBS         0x03404
#define E1000_FCRTL         0x02160
#define E1000_FCRTH         0x02168
#define E1000_FCTTV         0x00170
#define E1000_FCRTV         0x02460
#define E1000_QDE           0x02408
#define E1000_IOVCTL        0x05BBC
#define E1000_PCIEMISC      0x05BB8
#define E1000_DCA_CTRL      0x05B74
#define E1000_DCA_TXCTRL(_n) E1000_TXCTL(_n)
#define E1000_DCA_RXCTRL(_n) E1000_RXCTL(_n)
#define E1000_TSYNCRXCTL     0x0B620
#define E1000_TSYNCTXCTL     0x0B614
#define E1000_TSYNCRXCFG     0x05F50
#define E1000_SYSTIML        0x0B600
#define E1000_SYSTIMR        0x0B6F8
#define E1000_SYSTIMH        0x0B604
#define E1000_TIMINCA        0x0B608
#define E1000_TSIM           0x0B674
#define E1000_TSICR          0x0B66C
#define E1000_TRGTTIML0      0x0B644
#define E1000_TRGTTIMH0      0x0B648
#define E1000_TRGTTIML1      0x0B64C
#define E1000_TRGTTIMH1      0x0B650
#define E1000_AUXSTMPL0      0x0B65C
#define E1000_AUXSTMPH0      0x0B660
#define E1000_AUXSTMPL1      0x0B664
#define E1000_AUXSTMPH1      0x0B668
#define E1000_FREQOUT0       0x0B654
#define E1000_FREQOUT1       0x0B658
#define E1000_TXSTMPL        0x0B618
#define E1000_TXSTMPH        0x0B61C
#define E1000_RXSTMPL        0x0B624
#define E1000_RXSTMPH        0x0B628
#define E1000_TSAUXC         0x0B640
#define E1000_PCS_CFG0       0x04200
#define E1000_PCS_LCTL       0x04208
#define E1000_PCS_LSTAT      0x0420C
#define E1000_PCS_ANADV      0x04218
#define E1000_PCS_LPAB       0x0421C
#define E1000_PCS_NPTX       0x04220
#define E1000_PCS_LPABNP     0x04224
#define E1000_MPHY_ADDR_CTL  0x0024
#define E1000_MPHY_DATA      0x0E10
#define E1000_I210_TQAVCTRL  0x3570
#define E1000_I210_TQAVCC(_n) (0x3004 + ((_n) * 0x40))
#define E1000_I210_TQAVHC(_n) (0x300C + ((_n) * 0x40))
#define E1000_I210_DTXMXPKTSZ 0x355C
#define E1000_I210_RR2DCDELAY 0x5BF4
#define E1000_EEER            0x0E30
#define E1000_EEE_SU          0x0E34
#define E1000_IPCNFG          0x0E38
#define E1000_CONNSW_ENRGSRC  0x4
#define E1000_CONNSW_PHYSD    0x400
#define E1000_CONNSW_SERDESD  0x200
#define E1000_CONNSW_AUTOSENSE_EN  0x1
#define E1000_CONNSW_AUTOSENSE_CONF 0x2
#define E1000_CONNSW_PHY_PDN       0x800
#define E1000_FWSM           0x05B54
#define E1000_FACTPS         0x05B30
#define E1000_MANC           0x05820
#define E1000_MANC_ASF_EN    0x00000002
#define E1000_MANC_SMBUS_EN  0x00000001
#define E1000_MANC_EN_BMC2OS 0x10000000
#define E1000_MANC_RCV_TCO_EN 0x00020000
#define E1000_MANC_BLK_PHY_RST_ON_IDE 0x00040000
#define E1000_MBVFIMR         0x00C84
#define E1000_VFRE            0x00C8C
#define E1000_VFMAILBOX_SIZE  16
#define E1000_VMVIR_VLANA_DEFAULT 0x40000000
#define E1000_VF_SET_MAC_ADDR 0x02
#define E1000_VF_SET_MULTICAST 0x03
#define E1000_VF_SET_VLAN     0x04
#define E1000_VF_SET_LPE      0x05
#define E1000_VF_SET_PROMISC  0x06
#define E1000_VF_RESET        0x01
#define E1000_VT_MSGTYPE_CTS  0x20000000
#define E1000_VT_MSGTYPE_ACK  0x80000000
#define E1000_VT_MSGTYPE_NACK 0x40000000
#define E1000_VT_MSGINFO_SHIFT 16
#define E1000_VT_MSGINFO_MASK  (0xFF << E1000_VT_MSGINFO_SHIFT)
#define E1000_PF_CONTROL_MSG  0x0100
#define E1000_VF_MAC_FILTER_ADD (0x02 << E1000_VT_MSGINFO_SHIFT)
#define E1000_VF_MAC_FILTER_CLR (0x01 << E1000_VT_MSGINFO_SHIFT)
#define E1000_VF_SET_PROMISC_MULTICAST (0x02 << E1000_VT_MSGINFO_SHIFT)
#define E1000_VLVF_ARRAY_SIZE 32
#define E1000_VLVF_POOLSEL_SHIFT 12
#define E1000_VLVF_VLANID_ENABLE 0x80000000
#define E1000_VLVF_POOLSEL_MASK (0xFF << E1000_VLVF_POOLSEL_SHIFT)
#define E1000_VLVF_VLANID_MASK 0x00000FFF
#define E1000_VT_CTL_DEFAULT_POOL_SHIFT 7
#define E1000_VT_CTL_DEFAULT_POOL_MASK (0x7 << E1000_VT_CTL_DEFAULT_POOL_SHIFT)
#define E1000_VT_CTL_VM_REPL_EN BIT(30)
#define E1000_VT_CTL_DISABLE_DEF_POOL BIT(29)

/* Bit definitions */
#define E1000_CTRL_FD       0x00000001
#define E1000_CTRL_LRST     0x00000008
#define E1000_CTRL_SLU      0x00000040
#define E1000_CTRL_ILOS     0x00000080
#define E1000_CTRL_FRCSPD   0x00000800
#define E1000_CTRL_FRCDPX   0x00001000
#define E1000_CTRL_VME      0x40000000
#define E1000_CTRL_ADVD3WUC 0x00100000
#define E1000_STATUS_LU     0x00000002
#define E1000_STATUS_TXOFF  0x00000010
#define E1000_STATUS_FD     0x00000001
#define E1000_STATUS_SPEED_100   0x00000040
#define E1000_STATUS_SPEED_1000  0x00000080
#define E1000_STATUS_2P5_SKU     0x00001000
#define E1000_STATUS_2P5_SKU_OVER 0x00002000
#define E1000_CTRL_EXT_EIAME      0x01000000
#define E1000_CTRL_EXT_PBA_CLR    0x80000000
#define E1000_CTRL_EXT_IRCA       0x00000001
#define E1000_CTRL_EXT_DRV_LOAD   0x10000000
#define E1000_CTRL_EXT_PFRSTD     0x00004000
#define E1000_CTRL_EXT_SDP3_DATA  0x00000080
#define E1000_CTRL_EXT_SDP3_DIR   0x00000800
#define E1000_ICR_TXDW            0x00000001
#define E1000_ICR_RXT0            0x00000080
#define E1000_ICR_RXSEQ           0x00000008
#define E1000_ICR_RXDMT0          0x00000010
#define E1000_ICR_LSC             0x00000004
#define E1000_ICR_TS              0x00080000
#define E1000_ICR_VMMB            0x00000100
#define E1000_ICR_DOUTSYNC        0x10000000
#define E1000_ICR_DRSTA           0x40000000
#define E1000_ICR_INT_ASSERTED    0x80000000
#define E1000_EICR_RX_QUEUE0      0x00000001
#define E1000_EICR_TX_QUEUE0      0x00000100
#define E1000_EICR_OTHER          0x80000000
#define E1000_IMS_RXT0             E1000_ICR_RXT0
#define E1000_IMS_RXDMT0           E1000_ICR_RXDMT0
#define E1000_IMS_TXDW             E1000_ICR_TXDW
#define E1000_IMS_RXSEQ            E1000_ICR_RXSEQ
#define E1000_IMS_LSC              E1000_ICR_LSC
#define E1000_IMS_DOUTSYNC          E1000_ICR_DOUTSYNC
#define E1000_IMS_DRSTA            E1000_ICR_DRSTA
#define E1000_IMS_TS               E1000_ICR_TS
#define E1000_RCTL_EN              0x00000002
#define E1000_RCTL_BAM             0x00008000
#define E1000_RCTL_UPE             0x00000008
#define E1000_RCTL_MPE             0x00000010
#define E1000_RCTL_VFE             0x00040000
#define E1000_RCTL_LPE             0x00000020
#define E1000_RCTL_DPF             0x00400000
#define E1000_RCTL_SECRC           0x04000000
#define E1000_RCTL_PMCF            0x00800000
#define E1000_RCTL_SBP             0x00000004
#define E1000_RCTL_SZ_256          0x00030000
#define E1000_RCTL_CFIEN           0x00080000
#define E1000_RCTL_MO_SHIFT        12
#define E1000_TCTL_EN              0x00000002
#define E1000_TCTL_CT              0x00000ff0
#define E1000_TCTL_RTLC            0x01000000
#define E1000_TCTL_PSP             0x00000008
#define E1000_TCTL_COLD            0x003ff000
#define E1000_TCTL_CT_SHIFT        4
#define E1000_TCTL_COLD_SHIFT      12
#define E1000_TXDCTL_QUEUE_ENABLE  0x02000000
#define E1000_RXDCTL_QUEUE_ENABLE  0x02000000
#define E1000_SRRCTL_BSIZEPKT_SHIFT 10
#define E1000_SRRCTL_DROP_EN        0x80000000
#define E1000_SRRCTL_DESCTYPE_ADV_ONEBUF 0x02000000
#define E1000_SRRCTL_BSIZEHDRSIZE_SHIFT  2
#define E1000_SRRCTL_TIMESTAMP      0x40000000
#define E1000_ADVTXD_DTYP_CTXT     0x00200000
#define E1000_ADVTXD_DTYP_DATA     0x00300000
#define E1000_ADVTXD_TUCMD_L4T_TCP 0x00000800
#define E1000_ADVTXD_TUCMD_L4T_UDP 0x00000000
#define E1000_ADVTXD_TUCMD_L4T_SCTP 0x00001000
#define E1000_ADVTXD_TUCMD_IPV4    0x00000400
#define E1000_ADVTXD_MSS_SHIFT      16
#define E1000_ADVTXD_L4LEN_SHIFT     8
#define E1000_ADVTXD_MACLEN_SHIFT    9
#define E1000_ADVTXD_PAYLEN_SHIFT    14
#define E1000_ADVTXD_DCMD_VLE       0x40000000
#define E1000_ADVTXD_DCMD_IFCS      0x02000000
#define E1000_ADVTXD_DCMD_DEXT      0x20000000
#define E1000_ADVTXD_DCMD_TSE       0x80000000
#define E1000_ADVTXD_DCMD_RS        0x08000000
#define E1000_ADVTXD_DCMD_EOP       0x01000000
#define E1000_ADVTXD_MAC_TSTAMP     0x00080000
#define E1000_TXD_CMD_DEXT          0x20000000
#define E1000_TXD_STAT_DD           0x00000001
#define E1000_RXD_STAT_DD           0x01
#define E1000_RXD_STAT_EOP          0x02
#define E1000_RXD_STAT_IXSM         0x04
#define E1000_RXD_STAT_VP           0x08
#define E1000_RXD_STAT_TCPCS        0x20
#define E1000_RXD_STAT_UDPCS        0x10
#define E1000_RXDEXT_STATERR_CE     0x01000000
#define E1000_RXDEXT_STATERR_SE     0x02000000
#define E1000_RXDEXT_STATERR_SEQ    0x04000000
#define E1000_RXDEXT_STATERR_CXE    0x10000000
#define E1000_RXDEXT_STATERR_RXE    0x80000000
#define E1000_RXDEXT_STATERR_TCPE   0x20000000
#define E1000_RXDEXT_STATERR_IPE    0x40000000
#define E1000_RXDEXT_ERR_FRAME_ERR_MASK (E1000_RXDEXT_STATERR_CE | E1000_RXDEXT_STATERR_SE | E1000_RXDEXT_STATERR_SEQ | E1000_RXDEXT_STATERR_CXE | E1000_RXDEXT_STATERR_RXE)
#define E1000_RXCSUM_CRCOFL         0x00000800
#define E1000_RXCSUM_PCSD           0x00002000
#define E1000_RFCTL_IPV6_EX_DIS     0x00010000
#define E1000_RFCTL_LEF             0x00040000
#define E1000_IVAR_VALID            0x80
#define E1000_GPIE_NSICR            0x00000001
#define E1000_GPIE_EIAME            0x40000000
#define E1000_GPIE_MSIX_MODE        0x00000010
#define E1000_GPIE_PBA              0x80000000
#define E1000_EITR_CNT_IGNR         0x80000000
#define E1000_WUFC_MAG              0x00000002
#define E1000_WUFC_MC               0x00000008
#define E1000_WUFC_BC               0x00000010
#define E1000_WUFC_EX               0x00000004
#define E1000_WUFC_LNKC             0x00000001
#define E1000_WUC_PME_EN            0x00000002
#define E1000_RAH_AV                0x80000000
#define E1000_RAH_POOL_1            0x00040000
#define E1000_RAH_QSEL_ENABLE       0x10000000
#define E1000_RAH_ASEL_SRC_ADDR     0x00010000
#define E1000_VMOLR_RLPML_MASK      0x00003FFF
#define E1000_VMOLR_LPE             0x00010000
#define E1000_VMOLR_STRVLAN         0x40000000
#define E1000_VMOLR_BAM             0x08000000
#define E1000_VMOLR_AUPE            0x01000000
#define E1000_VMOLR_RSSE            0x00020000
#define E1000_VMOLR_ROPE            0x04000000
#define E1000_VMOLR_ROMPE           0x02000000
#define E1000_VMOLR_MPME            0x10000000
#define E1000_DTXSWC_VMDQ_LOOPBACK_EN BIT(31)
#define E1000_DTXSWC_MAC_SPOOF_MASK 0x000000FF
#define E1000_DTXSWC_VLAN_SPOOF_MASK 0x0000FF00
#define E1000_DTXSWC_VLAN_SPOOF_SHIFT 8
#define E1000_DTXCTL_VLAN_ADDED      0x0008
#define E1000_RPLOLR_STRVLAN         0x40000000
#define E1000_DMACR_DMAC_EN          0x80000000
#define E1000_DMACR_DC_BMC2OSW_EN    0x00008000
#define E1000_DMACR_DMACTHR_MASK     0x00FF0000
#define E1000_FCRTC_RTH_COAL_MASK    0x0003FFF0
#define E1000_PCIEMISC_LX_DECISION   0x00000080
#define E1000_TSYNCRXCTL_ENABLED     0x00000010
#define E1000_TSYNCTXCTL_ENABLED     0x00000010
#define E1000_TSYNCRXCTL_VALID       0x00000001
#define E1000_TSYNCRXCTL_TYPE_MASK   0x0000000E
#define E1000_TSYNCRXCTL_TYPE_EVENT_V2 0x0A
#define E1000_TSYNCRXCTL_TYPE_ALL    0x08
#define E1000_TSYNCRXCTL_TYPE_L4_V1  0x02
#define E1000_TSYNCRXCFG_PTP_V1_SYNC_MESSAGE  0x00
#define E1000_TSYNCRXCFG_PTP_V1_DELAY_REQ_MESSAGE 0x01
#define E1000_ETQF_1588             BIT(30)
#define E1000_ETQF_FILTER_ENABLE    BIT(26)
#define E1000_ETQF_QUEUE_MASK       0x00070000
#define E1000_ETQF_QUEUE_ENABLE     BIT(31)
#define E1000_ETQF_ETYPE_MASK       0x0000FFFF
#define E1000_FTQF_MASK             0xF0000000
#define E1000_FTQF_1588_TIME_STAMP  0x08000000
#define E1000_FTQF_MASK_PROTO_BP    0x10000000
#define E1000_FTQF_MASK_SOURCE_PORT_BP 0x80000000
#define E1000_FTQF_VF_BP            0x00008000
#define E1000_RXPBS_CFG_TS_EN       0x80000000
#define E1000_TIMINCA_16NS_SHIFT    24
#define E1000_EEER_RX_LPI_EN        0x00020000
#define E1000_EEER_TX_LPI_EN       0x00010000
#define E1000_EEER_LPI_FC           0x00040000
#define E1000_EEER_EEE_NEG          0x20000000
#define E1000_IPCNFG_EEE_100M_AN    0x00000004
#define E1000_IPCNFG_EEE_1G_AN      0x00000008
#define E1000_EEE_ADV_100_SUPPORTED  BIT(1)
#define E1000_EEE_ADV_1000_SUPPORTED BIT(2)
#define E1000_FCRTL_XONE            0x80000000
#define E1000_DCA_CTRL_DCA_MODE_DISABLE 0x01
#define E1000_DCA_CTRL_DCA_MODE_CB2     0x02
#define E1000_PCS_LCTL_AN_ENABLE        0x10000
#define E1000_PCS_LCTL_FORCE_LINK       0x20
#define E1000_PCS_LCTL_FLV_LINK_UP      1
#define E1000_PCS_LCTL_FDV_FULL         8
#define E1000_PCS_CFG_PCS_EN            8
#define E1000_MPHY_PCS_CLK_REG_DIGINELBEN 0x10
#define E1000_MRQC_ENABLE_RSS_MQ        0x00000002
#define E1000_MRQC_ENABLE_VMDQ          0x00000003
#define E1000_MRQC_ENABLE_VMDQ_RSS_MQ   0x00000005
#define E1000_MRQC_RSS_FIELD_IPV4_TCP   0x00010000
#define E1000_MRQC_RSS_FIELD_IPV6_TCP   0x00200000
#define E1000_MRQC_RSS_FIELD_IPV4       0x00020000
#define E1000_MRQC_RSS_FIELD_IPV6       0x00100000
#define E1000_MRQC_RSS_FIELD_IPV4_UDP   0x00400000
#define E1000_MRQC_RSS_FIELD_IPV6_UDP   0x00800000
#define E1000_MRQC_RSS_FIELD_IPV6_TCP_EX 0x00040000
#define E1000_LEDCTL_MODE_LED_ON        0xE
#define E1000_LEDCTL_MODE_LED_OFF       0xF
#define E1000_I2C_DATA_IN         0x00001000
#define E1000_I2C_DATA_OUT        0x00000400
#define E1000_I2C_DATA_OE_N       0x00000800
#define E1000_I2C_CLK_OUT         0x00000200
#define E1000_I2C_CLK_OE_N        0x00002000
#define E1000_I2C_CLK_IN          0x00004000
#define E1000_I2CBB_EN            0x00000100
#define E1000_CTRL_I2C_ENA        0x02000000
#define E1000_I2CCMD_ERROR        0x80000000
#define E1000_I2CCMD_READY        0x20000000
#define E1000_I2CCMD_OPCODE_READ  0x08000000
#define E1000_I2CCMD_PHY_ADDR_SHIFT 24
#define E1000_I2CCMD_REG_ADDR_SHIFT 16

/* MDIC (MDIO Control) specific fields */
#define E1000_MDIC_REG_MASK  0x001F0000
#define E1000_MDIC_REG_SHIFT 16
#define E1000_MDIC_PHY_MASK  0x03E00000
#define E1000_MDIC_PHY_SHIFT 21
#define E1000_MDIC_OP_WRITE  0x04000000
#define E1000_MDIC_OP_READ   0x08000000
#define E1000_MDIC_READY     0x10000000
#define E1000_MDIC_ERROR     0x80000000

/* Enums */
typedef enum { BAR_TYPE_NONE = 0, BAR_TYPE_MMIO, BAR_TYPE_PIO, BAR_TYPE_RAM } BARType;

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
    uint32_t regs[BAR0_SIZE / 4];

    /* DMA Context */
    void *dma_ctx;

    uint32_t status;
    bool reset_needed;
    uint8_t power_state;
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->intr_status & s->intr_mask) {
        if (msix_enabled(pdev)) {
            msix_notify(pdev, 0);
        }
    }
}

static void igb_set_initial_regs(PCIBaseState *s)
{
    s->regs[E1000_STATUS / 4] = E1000_STATUS_LU | E1000_STATUS_FD | E1000_STATUS_SPEED_1000;
    s->regs[E1000_EEC / 4] = 0x01; /* EEPROM present */
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= BAR0_SIZE) {
        return 0xFFFFFFFFFFFFFFFFULL;
    }

    switch (addr) {
    case E1000_ICR:
        /* ICR: clear on read */
        val = s->regs[addr / 4];
        s->regs[addr / 4] = 0;
        break;
    default:
        val = s->regs[addr / 4];
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= BAR0_SIZE) {
        return;
    }

    switch (addr) {
    case E1000_CTRL:
        s->regs[addr / 4] = val;
        if (val & E1000_CTRL_LRST) {
            /* Perform device reset, then clear LRST bit */
            memset(s->regs, 0, sizeof(s->regs));
            s->intr_mask = 0;
            s->intr_status = 0;
            igb_set_initial_regs(s);
            s->regs[E1000_CTRL / 4] = val & ~E1000_CTRL_LRST;
        }
        break;
    case E1000_IMS:
        /* Set bits in mask */
        s->intr_mask |= val;
        s->regs[addr / 4] = s->intr_mask;
        break;
    case E1000_IMC:
        /* Clear bits in mask */
        s->intr_mask &= ~val;
        s->regs[addr / 4] = s->intr_mask;
        break;
    case E1000_ICS:
        /* Write-1-to-set: set intr_status and trigger interrupt if unmasked */
        s->intr_status |= val;
        s->regs[addr / 4] = s->intr_status;
        if (s->intr_status & s->intr_mask) {
            pcibase_update_irq(s);
        }
        break;
    case E1000_MDIC:
    {
        uint32_t phy_addr = (val & E1000_MDIC_PHY_MASK) >> E1000_MDIC_PHY_SHIFT;
        uint32_t reg_addr = (val & E1000_MDIC_REG_MASK) >> E1000_MDIC_REG_SHIFT;
        if (val & E1000_MDIC_OP_READ) {
            uint32_t phy_data = 0;
            switch (reg_addr) {
            case 0: /* PHY Control */
                phy_data = 0x1000; /* Auto-neg enabled, default */
                break;
            case 1: /* PHY Status */
                phy_data = 0x796d; /* Link up, 1000Mbps capable, etc */
                break;
            case 2: /* PHY ID1 */
                phy_data = 0x02A8; /* Intel PHY ID1 for I210 */
                break;
            case 3: /* PHY ID2 */
                phy_data = 0x0000;
                break;
            default:
                phy_data = 0;
                break;
            }
            s->regs[addr / 4] = (phy_data & 0xFFFF) | E1000_MDIC_READY;
        } else if (val & E1000_MDIC_OP_WRITE) {
            /* Acknowledge write */
            s->regs[addr / 4] = E1000_MDIC_READY;
        }
        break;
    }
    default:
        s->regs[addr / 4] = val;
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* PIO not used by driver */
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* PIO not used */
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

    /* Reset all registers to zero */
    memset(s->regs, 0, sizeof(s->regs));
    s->intr_mask = 0;
    s->intr_status = 0;
    igb_set_initial_regs(s);
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
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = BAR0_SIZE;
    s->bar_info[0].name = "bar0";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Init MSI-X exclusive BAR for interrupt vectors */
    msix_init_exclusive_bar(pdev, 3, 1, errp);

    /* DMA, Timers, and Field init not needed */
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit_exclusive_bar(pdev);
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "igb_pci",
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
