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


#define TYPE_PCIBASE_DEVICE "fsl_enetc_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */

/* Generic helpers */
#define GENMASK(h, l) (((1UL << ((h)-(l)+1))-1) << (l))

/* PCI IDs (first entry from enetc_pf_id_table) */
#define PCI_VENDOR_ID_FREESCALE 0x1957
#define ENETC_DEV_ID_PF  0xe100

/* Register offsets and bitfields from driver source */
#define ENETC_PSIPMAR1(n)    (0x0104 + (n) * 0x8)
#define ENETC_PSIPMAR0(n)    (0x0100 + (n) * 0x8)
#define ENETC_VLAN_PROMISC_MAP_ALL   0x7
#define ENETC_PSIPVMR        0x001c
#define ENETC_PSIPVMR_SET_VP(simap)   ((simap) & 0x7)
#define ENETC_PSIVLAN_SET_QOS(val)    ((u32)(val) << 12)
#define ENETC_PSIVLANR(n)     (0x0240 + (n) * 4)
#define ENETC_PSIVLAN_EN      BIT(31)
#define ENETC_PSIMMHFR0(n, err)   (((err) ? 0x1d00 : 0x1d08) + (n) * 0x10)
#define ENETC_PSIUMHFR1(n)    (0x1d04 + (n) * 0x10)
#define ENETC_PSIMMHFR1(n)    (0x1d0c + (n) * 0x10)
#define ENETC_PSIUMHFR0(n, err)   (((err) ? 0x1d08 : 0x1d00) + (n) * 0x10)
#define EMETC_MAC_ADDR_FILT_RES   3
#define ENETC_PSIPMR          0x0018
#define ENETC_PSIPMR_SET_MP(n)   BIT((n) + 16)
#define ENETC_PSIPMR_SET_UP(n)   BIT(n)
#define ENETC_PM0_CMD_XGLP     BIT(10)
#define ENETC_PM0_IF_MODE      0x8300
#define ENETC_PM0_IFM_RLP      (BIT(5) | BIT(11))
#define ENETC_PM0_IFM_RG       BIT(2)
#define ENETC_PM0_CMD_CFG      0x8008
#define ENETC_PM0_CMD_PHY_TX_EN   BIT(15)
#define ENETC_PSICFGR0(n)     (0x0940 + (n) * 0xc)
#define ENETC_PSICFGR0_ASE     BIT(15)
#define ENETC_PRFSCAPR_GET_NUM_RFS(val)   ((((val) & 0xf) + 1) * 16)
#define ENETC_PRFSCAPR        0x1804
#define ENETC_PSIRFSCFGR(n)   (0x1814 + (n) * 4)
#define ENETC_PRFSMR_RFSE      BIT(31)
#define ENETC_PRFSMR          0x1800
#define ENETC_PCAPR0_PSFP      BIT(9)
#define ENETC_PCAPR0          0x0900
#define ENETC_SI_F_QBV  BIT(1)
#define ENETC_PCAPR0_QBV       BIT(4)
#define ENETC_SI_F_PSFP BIT(0)
#define ENETC_SI_F_QBU  BIT(2)
#define ENETC_PCAPR0_QBU       BIT(3)
#define ENETC_PSICFGR0_SIVIE   BIT(14)
#define ENETC_PCAPR0_TXBDR(val)   (((val) >> 16) & 0xff)
#define ENETC_PSICFGR0_SIVC(bmp)   (((bmp) & 0xff) << 24)
#define ENETC_PSICFGR0_SET_TXBDR(val)   ((val) & 0xff)
#define ENETC_PSICFGR0_SET_RXBDR(val)   (((val) & 0xff) << 16)
#define ENETC_PF_NUM_RINGS     8
#define ENETC_PVCLCTR_OVTPIDL(bmp)    ((bmp) & 0xff)
#define ENETC_PSIVLANFMR_VS     BIT(0)
#define ENETC_PSICFGR0_VTE      BIT(12)
#define ENETC_PVCLCTR          0x0208
#define ENETC_PCAPR0_RXBDR(val)   ((val) >> 24)
#define ENETC_VLAN_TYPE_C       BIT(0)
#define ENETC_PSIVLANFMR       0x1700
#define ENETC_VLAN_TYPE_S       BIT(1)
#define ENETC_MAC_MAXFRM_SIZE   9600
#define ENETC_PTCMSDUR(n)      (0x2020 + (n) * 4)
#define ENETC_PM0_PROMISC       BIT(4)
#define ENETC_PM0_RX_FIFO_VAL   1
#define ENETC_RX_MAXFRM_SIZE    ENETC_MAC_MAXFRM_SIZE
#define ENETC_PM0_RX_FIFO      0x801c
#define ENETC_SET_MAXFRM(val)   ((val) & 0xffff)
#define ENETC_PM0_CMD_TXP       BIT(11)
#define ENETC_PM0_MAXFRM       0x8014
#define ENETC_PM0_IFM_IFMODE_MASK GENMASK(1, 0)
#define ENETC_PM0_IFM_FULL_DPX  BIT(12)
#define ENETC_PM0_IFM_EN_AUTO   BIT(15)
#define ENETC_PM0_IFM_IFMODE_XGMII 0
#define ENETC_PM0_IFM_IFMODE_GMII 2
#define ENETC_PM0_TX_EN         BIT(0)
#define ENETC_PM0_RX_EN         BIT(1)
#define ENETC_PMR              0x0000
#define ENETC_PMR_EN    GENMASK(18, 16)
#define ENETC_PM0_IFM_SSP_100    (0 << 13)
#define ENETC_PM0_IFM_SSP_1000  (2 << 13)
#define ENETC_PM0_IFM_SSP_10    (1 << 13)
#define ENETC_PM0_IFM_SSP_MASK  GENMASK(14, 13)
#define ENETC_PPAUOFFTR        0x0414
#define ENETC_PM0_PAUSE_THRESH  0x8064
#define ENETC_PM0_PAUSE_QUANTA  0x8054
#define ENETC_RBMR  0
#define ENETC_PM0_PAUSE_IGN     BIT(8)
#define ENETC_PPAUONTR         0x0410
#define ENETC_RBMR_CM   BIT(4)
#define ENETC_PRSSCAPR         0x1404
#define ENETC_PRSSCAPR_GET_NUM_RSS(val)   (BIT((val) & 0xf) * 32)
#define ENETC_CBDR_DEFAULT_SIZE 64
#define ENETC_MAX_NUM_TXQS     8
#define ENETC_DEV_ID_PF        0xe100
#define ENETC_MAX_NUM_MAC_FLT  ((ENETC_MAX_NUM_VFS + 1) * MADDR_TYPE)
#define ENETC_MAX_NUM_VFS      2
#define ENETC_INT_NAME_MAX     (IFNAMSIZ + 8)
#define ENETC_VLAN_HT_SIZE     64
#define ENETC_MADDR_HASH_TBL_SZ   64
#define ENETC_CBD_FLAGS_SF      BIT(7)
#define ENETC_SI_ALIGN  32
#define ENETC_MAX_BDR_INT      6
#define ENETC_RSSHASH_KEY_SIZE 40
#define ENETC_SI_INT_IDX       0
#define ENETC_SIMSIVR  0xa30
#define ENETC_PTGCR            0x11a00
#define ENETC_PTGCR_TGE        BIT(31)
#define ENETC_TSDE             BIT(31)
#define ENETC_PTCTSDR(n)       (0x1210 + 4 * (n))
#define ENETC_CBSE             BIT(31)
#define ENETC_PTCCBSR1(n)      (0x1114 + (n) * 8)
#define ENETC_PTCCBSR0(n)      (0x1110 + (n) * 8)
#define ENETC_PMR_PSPEED_100M  BIT(8)
#define ENETC_PMR_PSPEED_MASK GENMASK(11, 8)
#define ENETC_PMR_PSPEED_2500M BIT(10)
#define ENETC_PMR_PSPEED_1000M BIT(9)
#define ENETC_PMR_PSPEED_10M   0
#define ENETC_MMCSR_ME         BIT(16)
#define ENETC_MMCSR_LINK_FAIL  BIT(31)
#define ENETC_MMCSR           0x1f00
#define ENETC_SICAR_RD_COHERENT   0x2b2b0000
#define ENETC_SICBDRBAR1       0x814
#define ENETC_SICAR2   0x48
#define ENETC_SICBDRBAR0       0x810
#define ENETC_SICBDRPIR        0x818
#define ENETC_SICBDRCIR        0x81c
#define ENETC_RTBLENR_LEN(n)   ((n) & ~0x7)
#define ENETC_SICBDRLENR       0x820
#define ENETC_SICBDRMR         0x800
#define ENETC_SICAR_WR_COHERENT   0x00006727
#define EIPBRR0_REVISION       GENMASK(15, 0)
#define ENETC_G_EIPBRR0        0x0bf8
#define ENETC_GLOBAL_BASE      0x20000
#define ENETC_BAR_REGS 0
#define ENETC_PORT_BASE        0x10000
#define ENETC_SIRFSCAPR_GET_NUM_RFS(val) ((val) & 0x7f)
#define ENETC_SIRSSCAPR        0x1600
#define ENETC_SIPCAPR0_LSO     BIT(1)
#define ENETC_SI_F_LSO BIT(3)
#define ENETC_SIRFSCAPR        0x1200
#define ENETC_SIPCAPR0_RFS     BIT(2)
#define ENETC_SIRSSCAPR_GET_NUM_RSS(val) (BIT((val) & 0xf) * 32)
#define ENETC_SICAPR0  0x900
#define ENETC_SIPCAPR0_RSS     BIT(8)
#define ENETC_SIPCAPR0 0x20
#define ENETC_SIMR_EN   BIT(31)
#define ENETC_SIMR      0
#define ENETC_SICAR1   0x44
#define ENETC_SICAR0   0x40
#define ENETC_SICAR_MSI        0x00300030
#define ENETC_BDR_INT_BASE_IDX 1
#define ENETC_RX_RING_DEFAULT_SIZE     2048
#define ENETC_TX_RING_DEFAULT_SIZE     2048
#define ENETC_PPSFPMR_PVC BIT(2)
#define ENETC_PPSFPMR_PSFPEN BIT(0)
#define ENETC_PPSFPMR_PVZC BIT(3)
#define ENETC_PPSFPMR_VS BIT(1)
#define ENETC_PPSFPMR 0x11b00
#define ENETC_CBDR_TIMEOUT     1000
#define ENETC_SI_F_PPM BIT(4)
#define ENETC_PSIVMSGRCVAR1(n) (0x214 + (n) * 0x8)
#define ENETC_PSIVMSGRCVAR0(n) (0x210 + (n) * 0x8)
#define ENETC_PSIMR_MASK(n)   \
    ({ typeof(n) _n = (n); (_n) ? GENMASK((_n), 1) : 0; })
#define ENETC_PSIIER   0xa00
#define ENETC_DEFAULT_MSG_SIZE  1024
#define ENETC_SIRBGCR   0x38
#define ENETC_SIMR_RSSE BIT(0)
#define ENETC_TBMR_SET_PRIO(val)    ((val) & ENETC_TBMR_PRIO_MASK)
#define ENETC_TBMR_PRIO_MASK        GENMASK(2, 0)
#define ENETC_TBMR    0
#define ENETC_MMCSR_VDIS BIT(17)
#define ENETC_MMCSR_GET_VT(x)   (((x) & ENETC_MMCSR_VT_MASK) >> 23)
#define ENETC_BDR(t, i, r)      (0x8000 + (t) * 0x100 + ENETC_BDR_OFF(i) + (r))
#define ENETC_CBD_DATA_MEM_ALIGN 64
#define ENETC_REV1      0x1
#define ENETC_REV_4_3           0x0403
#define NXP_ENETC_PPM_DEV_ID            0xe110
#define ENETC_DEV_ID_VF         0xef00
#define NXP_ENETC_PF_DEV_ID             0xe101
#define ENETC_REV_1_0           0x0100
#define ENETC_REV_4_1           0X0401
#define ENETC4_SILSOSFMR1              0x1304
#define ENETC4_SILSOSFMR0              0x1300
#define SILSOSFMR0_VAL_SET(first, mid)  (FIELD_PREP(SILSOSFMR0_TCP_MID_SEG, mid) | \
                                         FIELD_PREP(SILSOSFMR0_TCP_1ST_SEG, first))
#define ENETC4_TCP_NL_SEG_FLAGS_DMASK   (ENETC4_TCP_FLAGS_FIN | \
                                         ENETC4_TCP_FLAGS_RST | ENETC4_TCP_FLAGS_PSH)
#define ENETC_EMDIO_BASE        0x1c00
#define ENETC4_EMDIO_BASE               0x5c00
#define ENETC4_PM_IMDIO_BASE            0x5030
#define ENETC_PM_IMDIO_BASE     0x8030
#define ENETC_RXB_TRUESIZE      (PAGE_SIZE >> 1)
#define ENETC_RXB_PAD           NET_SKB_PAD
#define ENETC_TXBDS_NEEDED(val) ((val) + 2)
#define ENETC_TXBDS_MAX_NEEDED(x)       ENETC_TXBDS_NEEDED((x) + 1)
#define ENETC_LSO_MAX_DATA_LEN          SZ_256K
#define ENETC_PSIVHFR0(n)       (0x1e00 + (n) * 8)
#define ENETC_PSIVHFR1(n)       (0x1e04 + (n) * 8)
#define ENETC4_PSIVHFR0(a)              ((a) * 0x80 + 0x2060)
#define ENETC4_PSIVHFR1(a)              ((a) * 0x80 + 0x2064)
#define ENETC_TBIER    0xa0
#define ENETC_RBICR1   0xac
#define ENETC_RBIER    0xa0
#define ENETC_SIMSITRV(n) (0xB00 + (n) * 0x4)
#define ENETC_SIMSIRRV(n) (0xB80 + (n) * 0x4)
#define ENETC_PSIDCAPR_MSK      GENMASK(15, 0)
#define ENETC_PFMCAPR           0x1b38
#define ENETC_PFMCAPR_MSK       GENMASK(15, 0)
#define ENETC_PSGCAPR_SGIT_MSK  GENMASK(15, 0)
#define ENETC_PSIDCAPR          0x1b08
#define ENETC_PSFCAPR           0x1b18
#define ENETC_PSFCAPR_MSK       GENMASK(15, 0)
#define ENETC_PSGCAPR_GCL_MSK   GENMASK(18, 16)
#define ENETC_PSGCAPR           0x1b28
#define ENETC_CBD_STATUS_MASK   0xf
#define ENETC_PRSSK(n)          (0x1410 + (n) * 4)
#define ENETC4_PRSSKR(n)                ((n) * 0x4 + 0x250)
#define ENETC_RBMR_VTE  BIT(5)
#define ENETC_TBMR_VIH  BIT(9)
#define ENETC_MMCSR_GET_VSTS(x) (((x) & ENETC_MMCSR_VSTS_MASK) >> 18)
#define ENETC_MM_VERIFY_RETRIES         3
#define ENETC_MM_VERIFY_SLEEP_US        USEC_PER_MSEC
#define ENETC_MMCSR_VT_MASK     GENMASK(29, 23)
#define ENETC_PTCFPR(n)                 (0x1910 + (n) * 4)
#define ENETC_PTCFPR_FPE                BIT(31)
#define ENETC_BDR_OFF(i)        ((i) * 0x200)
#define ENETC_CLK_400M          400000000ULL
#define ENETC_PMAC_OFFSET       0x1000
#define ENETC_CLK_333M          333000000ULL
#define ENETC4_PMAC_OFFSET              0x400
#define ENETC_SIPMAR0   0x80
#define ENETC_SIPMAR1   0x84
#define SILSOSFMR0_TCP_1ST_SEG          GENMASK(11, 0)
#define SILSOSFMR0_TCP_MID_SEG          GENMASK(27, 16)
#define ENETC4_TCP_FLAGS_RST           BIT(2)
#define ENETC4_TCP_FLAGS_PSH           BIT(3)
#define ENETC4_TCP_FLAGS_FIN           BIT(0)
#define ENETC_TX_BD_L3_HDR_LEN  GENMASK(6, 0)
#define ENETC_TX_BD_L4T         GENMASK(7, 5)
#define ENETC_TX_BD_L3T         BIT(7)
#define ENETC_TXBD_E_FLAGS_VLAN_INS     BIT(0)
#define ENETC_TXBD_E_FLAGS_TWO_STEP_PTP BIT(2)
#define ENETC_TXBD_L4T_TCP      2
#define ENETC_TXBD_E_FLAGS_ONE_STEP_PTP BIT(1)
#define ENETC_TXBD_L4T_UDP      1
#define ENETC_TX_BD_L3_START    GENMASK(6, 0)
#define ENETC_TX_BD_IPCS        BIT(7)
#define ENETC_TBLENR    0x20
#define ENETC_TBICR0    0xa8
#define ENETC_SITXIDR   0xa18
#define ENETC_TBBAR1    0x14
#define ENETC_TBICR0_ICEN                BIT(31)
#define ENETC_TBCIR     0x1c
#define ENETC_TBBAR0    0x10
#define ENETC_TBPIR     0x18
#define ENETC_RBICR0_ICEN                BIT(31)
#define ENETC_RBBAR0    0x10
#define ENETC_RXB_DMA_SIZE      \
    min(SKB_WITH_OVERHEAD(ENETC_RXB_TRUESIZE) - ENETC_RXB_PAD, 0xffff)
#define ENETC_RBBAR1    0x14
#define ENETC_RBLENR    0x20
#define ENETC_RBBSR     0x8
#define ENETC_SIRXIDR   0xa28
#define ENETC_RXB_DMA_SIZE_XDP  \
    min(SKB_WITH_OVERHEAD(ENETC_RXB_TRUESIZE) - XDP_PACKET_HEADROOM, 0xffff)
#define ENETC_RBCIR     0xc
#define ENETC_RBICR0    0xa8
#define ENETC_RBPIR     0x18
#define ENETC_RBMR_BDS   BIT(2)
#define ENETC_RXIC_PKTTHR        min_t(u32, 256, ENETC_RX_RING_DEFAULT_SIZE / 2)
#define ENETC_TBIER_TXTIE       BIT(0)
#define ENETC_TXIC_PKTTHR        min_t(u32, 128, ENETC_TX_RING_DEFAULT_SIZE / 2)
#define ENETC_RBIER_RXTIE       BIT(0)
#define ENETC_RBICR0_SET_ICPT(n)        ((n) & ENETC_RBICR0_ICPT_MASK)
#define ENETC_TBICR1    0xac
#define ENETC_TBICR0_SET_ICPT(n) ((ilog2(n) + 1) & ENETC_TBICR0_ICPT_MASK)
#define ENETC_PTGCAPR_MAX_GCL_LEN_MASK  GENMASK(15, 0)
#define ENETC_PTGCAPR                    0x11a08
#define ENETC_MMCSR_VSTS_MASK   GENMASK(20, 18)
#define ENETC_TXBD_FLAGS_OFFSET 24
#define ENETC_TXBD_TXSTART_MASK GENMASK(24, 0)
#define ENETC_TXBD_TSTAMP      GENMASK(29, 0)
#define ENETC_SICTR0   0x18
#define ENETC_SICTR1   0x1c
#define ENETC_TPID_8021Q       0
#define ENETC_TBSR     0x4
#define ENETC_TBSR_BUSY        BIT(0)
#define ENETC_TBMR_EN  BIT(31)
#define ENETC_RBICR0_ICPT_MASK          0x1ff
#define ENETC_TBICR0_ICPT_MASK          0xf
#define ENETC_RBMR_EN  BIT(31)
#define ENETC_PFPMR_PMACE      BIT(1)
#define ENETC_MMCSR_RAFS_MASK  GENMASK(9, 8)
#define ENETC_MMCSR_VT(x)      (((x) << 23) & ENETC_MMCSR_VT_MASK)
#define ENETC_PFPMR            0x1900
#define ENETC_MMCSR_RAFS(x)    (((x) << 8) & ENETC_MMCSR_RAFS_MASK)
#define ENETC_MMCSR_LPE                BIT(1)
#define ENETC_MMCSR_GET_RAFS(x)        (((x) & ENETC_MMCSR_RAFS_MASK) >> 8)
#define ENETC_MMCSR_GET_LAFS(x)        (((x) & ENETC_MMCSR_LAFS_MASK) >> 3)
#define ENETC_RBDCR(n)  (0x8180 + (n) * 0x200)
#define ENETC_MMFCRXR           0x1f14
#define ENETC_MMFCTXR           0x1f18
#define ENETC_MMHCR             0x1f1c
#define ENETC_MMFAECR           0x1f08
#define ENETC_MMFAOCR           0x1f10
#define ENETC_MMFSECR           0x1f0c
#define ENETC4_PM_SINGLE_STEP(mac)      (0x50c0 + (mac) * 0x400)
#define PM_SINGLE_STEP_CH               BIT(6)
#define PM_SINGLE_STEP_OFFSET_SET(o)    FIELD_PREP(PM_SINGLE_STEP_OFFSET, o)
#define PM_SINGLE_STEP_EN               BIT(31)
#define ENETC_PM0_SINGLE_STEP_EN        BIT(31)
#define ENETC_PM0_SINGLE_STEP           0x80c0
#define ENETC_PM0_SINGLE_STEP_CH        BIT(7)
#define ENETC_SET_SINGLE_STEP_OFFSET(v) (((v) & 0xff) << 8)
#define ENETC4_PM_TCNP(mac)             (0x52c0 + (mac) * 0x400)
#define ENETC_PM_RCNP(mac)      (0x81C0 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_REV4      0x4
#define ENETC4_PM_RCNP(mac)             (0x51c0 + (mac) * 0x400)
#define ENETC_PM_TCNP(mac)      (0x82C0 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_SITFRM    0x328
#define ENETC_SITUCA    0x330
#define ENETC_SIRUCA    0x310
#define ENETC_SIROCT    0x300
#define ENETC_SITOCT    0x320
#define ENETC_SITMCA    0x338
#define ENETC_SIRMCA    0x318
#define ENETC_SIRFRM    0x308
#define ENETC_PM_RDRP(mac)      (0x8158 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_TUCA(mac)      (0x8240 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_RERR(mac)      (0x8138 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_TUND(mac)      (0x8268 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_TVLAN(mac)     (0x8230 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_RUCA(mac)      (0x8140 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_TOCT(mac)      (0x8208 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_TPKT(mac)      (0x8260 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_TFCS(mac)      (0x8228 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_RPKT(mac)      (0x8160 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_RVLAN(mac)     (0x8130 + ENETC_PMAC_OFFSET * (mac))
#define ENETC4_PM_TUCA(mac)             (0x5240 + (mac) * 0x400)
#define ENETC4_PM_RERR(mac)             (0x5138 + (mac) * 0x400)
#define ENETC4_PM_TVLAN(mac)            (0x5230 + (mac) * 0x400)
#define ENETC4_PM_TUND(mac)             (0x5268 + (mac) * 0x400)
#define ENETC4_PM_RUCA(mac)             (0x5140 + (mac) * 0x400)
#define ENETC4_PM_TIOCT(mac)            (0x52f8 + (mac) * 0x400)
#define ENETC4_PM_TPKT(mac)             (0x5260 + (mac) * 0x400)
#define ENETC4_PM_TOCT(mac)             (0x5208 + (mac) * 0x400)
#define ENETC4_PM_RVLAN(mac)            (0x5130 + (mac) * 0x400)
#define ENETC4_PM_RDRP(mac)             (0x5158 + (mac) * 0x400)
#define ENETC4_PM_RPKT(mac)             (0x5160 + (mac) * 0x400)
#define ENETC4_PM_ROCT(mac)             (0x5108 + (mac) * 0x400)
#define ENETC4_PM_TFCS(mac)             (0x5228 + (mac) * 0x400)
#define ENETC4_PUFDVFR                  0x2d0
#define ENETC4_PBFDSIR                  0x208
#define ENETC4_PRXDCR                   0x41c0
#define ENETC4_PMFDMFR                  0x288
#define ENETC4_PICDRDCR(a)              ((a) * 0x10 + 0x140)
#define ENETC4_PBFDVFR                  0x2d8
#define ENETC4_PFDMSAPR                 0x20c
#define ENETC4_PUFDMFR                  0x284
#define ENETC4_PRXDCRR1                 0x41cc
#define ENETC4_PMFDVFR                  0x2d4
#define ENETC4_PRXDCRRR                 0x41c4
#define ENETC4_PRXDCRR0                 0x41c8
#define ENETC_UFDMF             0x1680
#define ENETC_PICDR(n)          (0x0700 + (n) * 8)
#define ENETC_MFDMF             0x1684
#define ENETC_PBFDSIR           0x0810
#define ENETC_PUFDVFR           0x1780
#define ENETC_PBFDVFR           0x1788
#define ENETC_PMFDVFR           0x1784
#define ENETC_PFDMSAPR          0x0814
#define ENETC_MMCSR_LAFS_MASK   GENMASK(4, 3)
#define ENETC_PSR               0x0004
#define ENETC_PTXMBAR           0x0608
#define ENETC_PCAPR1            0x0904
#define ENETC_SICAPR1   0x904
#define ENETC_SICBDRSR          0x804
#define ENETC_SIUEFDCR  0xe28
#define ENETC_RBSR     0x4
#define ENETC4_PM_TXPF(mac)             (0x5218 + (mac) * 0x400)
#define ENETC_PM_RXPF(mac)      (0x8118 + ENETC_PMAC_OFFSET * (mac))
#define ENETC4_PM_RXPF(mac)             (0x5118 + (mac) * 0x400)
#define ENETC_PM_TXPF(mac)      (0x8218 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_RFSE_EN   BIT(15)
#define ENETC_RFSE_MODE_BD      2
#define ENETC4_PPMTUFCR                 0x50c8
#define ENETC4_PPMRBFCR                 0x5098
#define ENETC4_PPMTMFCR                 0x50d0
#define ENETC4_PPMROCR                  0x5080
#define ENETC4_PPMRUFCR                 0x5088
#define ENETC4_PPMTBFCR                 0x50d8
#define ENETC4_PPMRMFCR                 0x5090
#define ENETC4_PPMTOCR                  0x50c0
#define PM_SINGLE_STEP_OFFSET           GENMASK(15, 7)
#define ENETC_PM_RMCA(mac)      (0x8148 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_RFRM(mac)      (0x8120 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_TEOCT(mac)     (0x8200 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_TLCOL(mac)     (0x82E8 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_TCRSE(mac)     (0x8210 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_TMCA(mac)      (0x8248 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_TBCA(mac)      (0x8250 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_TFRM(mac)      (0x8220 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_TERR(mac)      (0x8238 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_TDFR(mac)      (0x82D0 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_TECOL(mac)     (0x82F0 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_TSCOL(mac)     (0x82E0 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_RFCS(mac)      (0x8128 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_REOCT(mac)     (0x8100 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_RBCA(mac)      (0x8150 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_RALN(mac)      (0x8110 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_RDRNTP(mac)    (0x81C8 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_TMCOL(mac)     (0x82D8 + ENETC_PMAC_OFFSET * (mac))
#define ENETC4_PM_TSCOL(mac)            (0x52e0 + (mac) * 0x400)
#define ENETC4_PM_TFRM(mac)             (0x5220 + (mac) * 0x400)
#define ENETC4_PM_RDRNTP(mac)           (0x51c8 + (mac) * 0x400)
#define ENETC4_PM_RBCA(mac)             (0x5150 + (mac) * 0x400)
#define ENETC4_PM_RFRM(mac)             (0x5120 + (mac) * 0x400)
#define ENETC4_PM_TECOL(mac)            (0x52f0 + (mac) * 0x400)
#define ENETC4_PM_TMCA(mac)             (0x5248 + (mac) * 0x400)
#define ENETC4_PM_TBCA(mac)             (0x5250 + (mac) * 0x400)
#define ENETC4_PM_TMCOL(mac)            (0x52d8 + (mac) * 0x400)
#define ENETC4_PM_TDFR(mac)             (0x52d0 + (mac) * 0x400)
#define ENETC4_PM_RALN(mac)             (0x5110 + (mac) * 0x400)
#define ENETC4_PM_TEOCT(mac)            (0x5200 + (mac) * 0x400)
#define ENETC4_PM_TERR(mac)             (0x5238 + (mac) * 0x400)
#define ENETC4_PM_RMCA(mac)             (0x5148 + (mac) * 0x400)
#define ENETC4_PM_TLCOL(mac)            (0x52e8 + (mac) * 0x400)
#define ENETC4_PM_REOCT(mac)            (0x5100 + (mac) * 0x400)
#define ENETC4_PM_RFCS(mac)             (0x5128 + (mac) * 0x400)
#define ENETC4_PM_R255(mac)             (0x5180 + (mac) * 0x400)
#define ENETC4_PM_R1522(mac)            (0x5198 + (mac) * 0x400)
#define ENETC4_PM_T511(mac)             (0x5288 + (mac) * 0x400)
#define ENETC4_PM_T1523X(mac)           (0x52a0 + (mac) * 0x400)
#define ENETC4_PM_T1023(mac)            (0x5290 + (mac) * 0x400)
#define ENETC4_PM_R1523X(mac)           (0x51a0 + (mac) * 0x400)
#define ENETC4_PM_ROVR(mac)             (0x51a8 + (mac) * 0x400)
#define ENETC4_PM_R64(mac)              (0x5170 + (mac) * 0x400)
#define ENETC4_PM_RFRG(mac)             (0x51b8 + (mac) * 0x400)
#define ENETC4_PM_R1023(mac)            (0x5190 + (mac) * 0x400)
#define ENETC4_PM_T64(mac)              (0x5270 + (mac) * 0x400)
#define ENETC4_PM_T1522(mac)            (0x5298 + (mac) * 0x400)
#define ENETC4_PM_RJBR(mac)             (0x51b0 + (mac) * 0x400)
#define ENETC4_PM_T255(mac)             (0x5280 + (mac) * 0x400)
#define ENETC4_PM_R127(mac)             (0x5178 + (mac) * 0x400)
#define ENETC4_PM_T127(mac)             (0x5278 + (mac) * 0x400)
#define ENETC4_PM_RUND(mac)             (0x5168 + (mac) * 0x400)
#define ENETC4_PM_R511(mac)             (0x5188 + (mac) * 0x400)
#define ENETC_PM_RJBR(mac)      (0x81B0 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_R127(mac)      (0x8178 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_RUND(mac)      (0x8168 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_T1522(mac)     (0x8298 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_R511(mac)      (0x8188 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_ROVR(mac)      (0x81A8 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_R1523X(mac)    (0x81A0 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_T1023(mac)     (0x8290 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_R1522(mac)     (0x8198 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_T127(mac)      (0x8278 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_R1023(mac)     (0x8190 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_T64(mac)       (0x8270 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_T511(mac)      (0x8288 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_T255(mac)      (0x8280 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_T1523X(mac)    (0x82A0 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_R64(mac)       (0x8170 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_RFRG(mac)      (0x81B8 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_R255(mac)      (0x8180 + ENETC_PMAC_OFFSET * (mac))

/* Placeholder for BAR0 size (needs extraction from hw spec) */
#ifndef BAR0_SIZE
#define BAR0_SIZE 0x40000 /* guessed */
#endif

/* Placeholder for number of MSI-X vectors */
#ifndef ENETC_MSIX_NUM_VECTORS
#define ENETC_MSIX_NUM_VECTORS 2
#endif

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
    uint32_t irq_status;      /* Interrupt status placeholder */
    uint32_t irq_mask;        /* Interrupt mask placeholder */

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint8_t mmio_data[BAR0_SIZE];  /* Flat memory for MMIO region */

    /* DMA Context - will be modeled later */
    void *dma_placeholder;

    /* Operational status flags - not used yet */
    /* State used to handle reset sequences - not used yet */
    /* Power management state (D0-D3) - not used yet */
    /* Other addition info - not used yet */
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    /* IRQ update logic will be implemented in Phase 2 */
    /* Currently unused; placeholder for future interrupt signaling */
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    /* DMA logic will be implemented in Phase 2 */
    /* Currently unused; placeholder for future DMA transfers */
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= BAR0_SIZE) {
        return ~0ULL;
    }

    switch (size) {
    case 1:
        val = s->mmio_data[addr];
        break;
    case 2:
        val = lduw_le_p(&s->mmio_data[addr]);
        break;
    case 4:
        val = ldl_le_p(&s->mmio_data[addr]);
        break;
    case 8:
        val = ldq_le_p(&s->mmio_data[addr]);
        break;
    default:
        val = 0;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= BAR0_SIZE) {
        return;
    }

    switch (size) {
    case 1:
        s->mmio_data[addr] = (uint8_t)val;
        break;
    case 2:
        stw_le_p(&s->mmio_data[addr], (uint16_t)val);
        break;
    case 4:
        stl_le_p(&s->mmio_data[addr], (uint32_t)val);
        break;
    case 8:
        stq_le_p(&s->mmio_data[addr], val);
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

    /* Reset MMIO data to zero */
    memset(s->mmio_data, 0, BAR0_SIZE);

    /* Set initial register values required for driver probe */
    stl_le_p(&s->mmio_data[ENETC_PCAPR0], 0x08080000); /* 8 TXBDR, 8 RXBDR */
    stl_le_p(&s->mmio_data[ENETC_PRFSCAPR], 0);        /* No RFS entries -> (0+1)*16=16 */
    stl_le_p(&s->mmio_data[ENETC_PRSSCAPR], 0);        /* No RSS entries -> BIT(0)*32=32 */
    /* CRITICAL FIX: Set revision to a supported value (4.3) to pass driver validation */
    stl_le_p(&s->mmio_data[ENETC_G_EIPBRR0], ENETC_REV_4_3);

    /* Set SI-level capability registers to non-zero to satisfy enetc_get_driver_data */
    stl_le_p(&s->mmio_data[ENETC_SIPCAPR0], ENETC_SIPCAPR0_RSS | ENETC_SIPCAPR0_RFS | ENETC_SIPCAPR0_LSO);
    stl_le_p(&s->mmio_data[ENETC_SIRFSCAPR], 0x7F);    /* Max RFS entries for SI */
    stl_le_p(&s->mmio_data[ENETC_SIRSSCAPR], 0x7);     /* RSS: BIT(7)*32 = 4096 */

    /* Set MSI-related capability registers */
    stl_le_p(&s->mmio_data[ENETC_SICAR0], ENETC_SICAR_MSI);
    stl_le_p(&s->mmio_data[ENETC_SICAR1], ENETC_SICAR_MSI);

    /* Set PSI-level capability registers to indicate full features */
    stl_le_p(&s->mmio_data[ENETC_PSIDCAPR], 0xFFFF);   /* All PSI ID capabilities */
    stl_le_p(&s->mmio_data[ENETC_PSFCAPR], 0xFFFF);    /* All PSI filter capabilities */
    stl_le_p(&s->mmio_data[ENETC_PSGCAPR], 0x0007FFFF); /* GCL: 3 bits, SGIT: 16 bits */
    stl_le_p(&s->mmio_data[ENETC_PFMCAPR], 0xFFFF);    /* All PF management capabilities */
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_FREESCALE );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  ENETC_DEV_ID_PF );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0200 );
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
    s->bar_info[0].name = "enetc-mmio";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI-X initialization using exclusive BAR to prevent memory assertion crashes */
    if (msix_init_exclusive_bar(pdev, ENETC_MSIX_NUM_VECTORS, 1, errp)) {
        error_propagate(errp, *errp);
        return;
    }
    s->has_msix = true;

    /* Initialize register file with reset values */
    pcibase_reset(DEVICE(s));
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (msix_enabled(pdev)) {
        msix_uninit_exclusive_bar(pdev);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }

    /* Free buffers, stop timers, etc. */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "fsl_enetc_pci",
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