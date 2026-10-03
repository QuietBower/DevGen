/* This template provides a robust skeleton for hardware emulation.
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

#define TYPE_PCIBASE_DEVICE "ice_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */

/* Vendor and Device IDs from first entry in ice_pci_tbl[] */
#define VENDOR_ID          0x8086
#define DEVICE_ID          0x1591
#define CLASS_ID           PCI_CLASS_NETWORK_ETHERNET

/* BAR definitions */
#define ICE_BAR0           0
/* #define ICE_BAR0_SIZE   ? (needs definition from driver) */

/* Number of MSI-X vectors; actual number to be confirmed */
#define ICE_MSIX_VECTORS   65

/* Register offsets from ice driver */
#define PFGEN_CTRL                 0x00091000
#define PFGEN_STATE                0x00088000
#define GLGEN_RSTAT                0x000B8188
#define GLGEN_RSTCTL               0x000B8180
#define GLGEN_RTRIG                0x000B8190
#define GLGEN_STAT                 0x000B612C
#define GLGEN_CLKSTAT_SRC          0x000B826C
#define GLGEN_SWITCH_MODE_CONFIG   0x000B81E0
#define GLGEN_VFLRSTAT(_i)         (0x00093A04 + ((_i) * 4))
#define GLNVM_ULD                  0x000B6008
#define GLNVM_FLA                  0x000B6108
#define GLNVM_GENS                 0x000B6100
#define GL_MNG_FWSM                0x000B6134
#define GL_MDET_TX_TCLAN_BY_MAC(hw) ((hw)->mac_type == ICE_MAC_E830 ? \
                                     0x000FCCC0 : 0x000FC068)
#define GL_MDET_RX                 0x00294C00
#define GL_MDET_TX_PQM             0x002D2E00
#define GL_PWR_MODE_CTL            0x000B820C
#define GLPCI_CNF2                 0x000BE004
#define GLINT_CTL                  0x0016CC54
#define GLINT_DYN_CTL(_INT)        (0x00160000 + ((_INT) * 4))
#define GLINT_ITR(_i, _INT)        (0x00154000 + ((_i) * 8192 + (_INT) * 4))
#define GLINT_RATE(_INT)           (0x0015A000 + ((_INT) * 4))
#define GLINT_VECT2FUNC(_INT)      (0x00162000 + ((_INT) * 4))
#define PFINT_ALLOC                0x001D2600
#define PFINT_OICR                 0x0016CA00
#define PFINT_OICR_ENA             0x0016C900
#define PFINT_OICR_CTL             0x0016CA80
#define PFINT_FW_CTL               0x0016C800
#define PFINT_MBX_CTL              0x0016B280
#define PFINT_SB_CTL               0x0016B600
#define PFINT_TSYN_MSK             0x0016C980
#define PF_FUNC_RID                0x0009E880
#define PF_PCI_CIAA                0x0009E580
#define PF_PCI_CIAD                0x0009E500
#define PF_MDET_RX                 0x00294280
#define PF_MDET_TX_TCLAN_BY_MAC(hw) ((hw)->mac_type == ICE_MAC_E830 ? \
                                      0x000FCC00 : 0x000FC000)
#define PF_MDET_TX_PQM             0x002D2C80
#define PF_SB_ATQBAH               0x0022FC80
#define PF_SB_ATQBAL               0x0022FC00
#define PF_SB_REM_DEV_CTL          0x002300F0
#define PFQF_FD_ENA                0x0043A000
#define PFQF_FD_SIZE               0x00460100
#define PFPM_APM                   0x000B8080
#define PFPM_WUS                   0x0009DB80
#define PFPM_WUFC                  0x0009DC00
#define PRTGEN_STATUS              0x000B8100
#define PRTDCB_TUP2TC              0x001D26C0
#define PRTDCB_GENS                0x00083020
#define PRTDCB_GENC                0x00083000
#define PRTRPB_RDPC                0x000AC260
#define PFTSYN_SEM                 0x00088880
#define GLTSYN_ENA(_i)             (0x00088808 + ((_i) * 4))
#define GLTSYN_STAT(_i)            (0x000888C0 + ((_i) * 4))
#define GLTSYN_INCVAL_L(_i)        (0x00088918 + ((_i) * 4))
#define GLTSYN_INCVAL_H(_i)        (0x00088920 + ((_i) * 4))
#define GLTSYN_SHTIME_L(_i)        (0x000888E8 + ((_i) * 4))
#define GLTSYN_SHTIME_0(_i)        (0x000888E0 + ((_i) * 4))
#define GLTSYN_SHTIME_H(_i)        (0x000888F0 + ((_i) * 4))
#define GLTSYN_TIME_L(_i)          (0x000888D0 + ((_i) * 4))
#define GLTSYN_TIME_H(_i)          (0x000888D8 + ((_i) * 4))
#define GLTSYN_TIME_0(_i)          (0x000888C8 + ((_i) * 4))
#define GLTSYN_TGT_L(_chan, _idx)  (GLTSYN_TGT_L_0(_idx) + ((_chan) * 16))
#define GLTSYN_TGT_H(_chan, _idx)  (GLTSYN_TGT_H_0(_idx) + ((_chan) * 16))
#define GLTSYN_TGT_L_0(_i)         (0x00088928 + ((_i) * 4))
#define GLTSYN_TGT_H_0(_i)         (0x00088930 + ((_i) * 4))
#define GLTSYN_EVNT_L(_chan, _idx) (GLTSYN_EVNT_L_0(_idx) + ((_chan) * 16))
#define GLTSYN_EVNT_H(_chan, _idx) (GLTSYN_EVNT_H_0(_idx) + ((_chan) * 16))
#define GLTSYN_EVNT_L_0(_i)        (0x00088968 + ((_i) * 4))
#define GLTSYN_EVNT_H_0(_i)        (0x00088970 + ((_i) * 4))
#define GLTSYN_AUX_IN(_chan, _idx) (GLTSYN_AUX_IN_0(_idx) + ((_chan) * 8))
#define GLTSYN_AUX_IN_0(_i)        (0x000889D8 + ((_i) * 4))
#define GLTSYN_AUX_OUT(_chan, _idx) (GLTSYN_AUX_OUT_0(_idx) + ((_chan) * 8))
#define GLTSYN_AUX_OUT_0(_i)       (0x00088998 + ((_i) * 4))
#define GLTSYN_CLKO(_chan, _idx)   (GLTSYN_CLKO_0(_idx) + ((_chan) * 8))
#define GLTSYN_CLKO_0(_i)          (0x000889B8 + ((_i) * 4))
#define GLTSYN_SHADJ_L(_i)         (0x00088908 + ((_i) * 4))
#define GLTSYN_SHADJ_H(_i)         (0x00088910 + ((_i) * 4))
#define GLTSYN_CMD                 0x00088810
#define GLTSYN_SYNC_DLAY           0x00088818
#define GLGEN_GPIO_CTL(_i)         (0x000880C8 + ((_i) * 4))
#define GLDCB_RTCTQ_RXQNUM_M       ICE_M(0x7FF, 0)
#define QINT_RQCTL(_QRX)           (0x00150000 + ((_QRX) * 4))
#define QINT_TQCTL(_DBQM)          (0x00140000 + ((_DBQM) * 4))
#define QRX_CTRL(_QRX)             (0x00120000 + ((_QRX) * 4))
#define QRX_TAIL(_QRX)             (0x00290000 + ((_QRX) * 4))
#define QRX_ITR(_QRX)              (0x00292000 + ((_QRX) * 4))
#define QTX_COMM_HEAD(_DBQM)       (0x000E0000 + ((_DBQM) * 4))
#define QTX_COMM_DBELL(_DBQM)      (0x002C0000 + ((_DBQM) * 4))
#define QRXFLXP_CNTXT(_QRX)        (0x00480000 + ((_QRX) * 4))
#define VSIQF_FD_SIZE(_VSI)        (0x00462000 + ((_VSI) * 4))
#define VSIQF_FD_CNT(_VSI)         (0x00464000 + ((_VSI) * 4))
#define VPLAN_TX_QBASE(_VF)        (0x001D1800 + ((_VF) * 4))
#define VPLAN_RX_QBASE(_VF)        (0x00072000 + ((_VF) * 4))
#define VPLAN_RXQ_MAPENA(_VF)      (0x00073000 + ((_VF) * 4))
#define VPLAN_TXQ_MAPENA(_VF)      (0x00073800 + ((_VF) * 4))
#define VPINT_ALLOC(_VF)           (0x001D1000 + ((_VF) * 4))
#define VPINT_ALLOC_PCI(_VF)       (0x0009D000 + ((_VF) * 4))
#define VPINT_MBX_CTL(_VSI)        (0x0016A000 + ((_VSI) * 4))
#define VP_MDET_TX_TCLAN(_VF)      (0x000FB800 + ((_VF) * 4))
#define VP_MDET_TX_PQM(_VF)        (0x002D2000 + ((_VF) * 4))
#define VP_MDET_RX(_VF)            (0x00294400 + ((_VF) * 4))
#define VP_MDET_TX_TDPU(_VF)       (0x00040000 + ((_VF) * 4))
#define VFGEN_RSTAT(_VF)           (0x00074000 + ((_VF) * 4))
#define VPGEN_VFRTRIG(_VF)         (0x00090000 + ((_VF) * 4))
#define VPGEN_VFRSTAT(_VF)         (0x00090800 + ((_VF) * 4))
#define VF_MBX_ATQLEN(_VF)         (0x0022A800 + ((_VF) * 4))
#define VF_MBX_ARQLEN(_VF)         (0x0022BC00 + ((_VF) * 4))
#define E830_GL_MDET_TX_TCLAN      0x000FCCC0
#define E800_GL_MDET_TX_TCLAN      0x000FC068
#define E800_PF_MDET_TX_TCLAN      0x000FC000
#define E830_PF_MDET_TX_TCLAN      0x000FCC00
#define GL_MDCK_TX_TDPU            0x00049348
#define GL_PREEXT_L2_PMASK0(_i)    (0x0020F0FC + ((_i) * 4))
#define GL_PREEXT_L2_PMASK1(_i)    (0x0020F108 + ((_i) * 4))
#define GLPRT_BPRCL(_i)            (0x00381380 + ((_i) * 8))
#define GLPRT_MRFC(_i)             (0x00380080 + ((_i) * 8))
#define GLPRT_MPTCL(_i)            (0x00381200 + ((_i) * 8))
#define GLPRT_PTC64L(_i)           (0x00380B80 + ((_i) * 8))
#define GLPRT_MPRCL(_i)            (0x00381340 + ((_i) * 8))
#define GLSTAT_FD_CNT0L(_i)        (0x003A0000 + ((_i) * 8))
#define GLPRT_PTC255L(_i)          (0x00380C00 + ((_i) * 8))
#define GLPRT_LXONRXC(_i)          (0x00380280 + ((_i) * 8))
#define GLPRT_PTC1023L(_i)         (0x00380C80 + ((_i) * 8))
#define GLPRT_RUC(_i)              (0x00380200 + ((_i) * 8))
#define GLPRT_PRC511L(_i)          (0x003809C0 + ((_i) * 8))
#define GLPRT_LXOFFTXC(_i)         (0x00381180 + ((_i) * 8))
#define GLPRT_BPTCL(_i)            (0x00381240 + ((_i) * 8))
#define GLPRT_PRC64L(_i)           (0x00380900 + ((_i) * 8))
#define GLPRT_LXOFFRXC(_i)         (0x003802C0 + ((_i) * 8))
#define GLPRT_PTC9522L(_i)         (0x00380D00 + ((_i) * 8))
#define GLPRT_ROC(_i)              (0x00380240 + ((_i) * 8))
#define GLPRT_LXONTXC(_i)          (0x00381140 + ((_i) * 8))
#define GLPRT_PTC127L(_i)          (0x00380BC0 + ((_i) * 8))
#define GLPRT_PRC1522L(_i)         (0x00380A40 + ((_i) * 8))
#define GLPRT_PTC511L(_i)          (0x00380C40 + ((_i) * 8))
#define GLPRT_PRC255L(_i)          (0x00380980 + ((_i) * 8))
#define GLPRT_ILLERRC(_i)          (0x003801C0 + ((_i) * 8))
#define GLPRT_GORCL(_i)            (0x00380000 + ((_i) * 8))
#define GLPRT_UPTCL(_i)            (0x003811C0 + ((_i) * 8))
#define GLPRT_PRC127L(_i)          (0x00380940 + ((_i) * 8))
#define GLPRT_MLFC(_i)             (0x00380040 + ((_i) * 8))
#define GLPRT_PTC1522L(_i)         (0x00380CC0 + ((_i) * 8))
#define GLPRT_UPRCL(_i)            (0x00381300 + ((_i) * 8))
#define GLPRT_GOTCL(_i)            (0x00380B40 + ((_i) * 8))
#define GLPRT_PRC9522L(_i)         (0x00380A80 + ((_i) * 8))
#define GLPRT_TDOLD(_i)            (0x00381280 + ((_i) * 8))
#define GLPRT_CRCERRS(_i)          (0x00380100 + ((_i) * 8))
#define GLPRT_RFC(_i)              (0x00380AC0 + ((_i) * 8))
#define GLPRT_RLEC(_i)             (0x00380140 + ((_i) * 8))
#define GLPRT_RJC(_i)              (0x00380B00 + ((_i) * 8))
#define GLPRT_PRC1023L(_i)         (0x00380A00 + ((_i) * 8))
#define GLV_RDPC(_i)               (0x00294C04 + ((_i) * 4))
#define GLV_TEPC(_VSI)             (0x00312000 + ((_VSI) * 4))
#define GLV_BPRCL(_i)              (0x003B6000 + ((_i) * 8))
#define GLV_UPTCL(_i)              (0x0030A000 + ((_i) * 8))
#define GLV_MPRCL(_i)              (0x003B4000 + ((_i) * 8))
#define GLV_BPTCL(_i)              (0x0030E000 + ((_i) * 8))
#define GLV_GOTCL(_i)              (0x00300000 + ((_i) * 8))
#define GLV_GORCL(_i)              (0x003B0000 + ((_i) * 8))
#define GLV_UPRCL(_i)              (0x003B2000 + ((_i) * 8))
#define GLV_MPTCL(_i)              (0x0030C000 + ((_i) * 8))
#define QRX_CONTEXT(_i, _QRX)      (0x00280000 + ((_i) * 8192 + (_QRX) * 4))
#define GLCOMM_QUANTA_PROF(_i)     (0x002D2D68 + ((_i) * 4))
#define GLQF_FD_SIZE               0x00460010
#define GLQF_FD_CNT                0x00460018
#define GLQF_FDSWAP(_i, _j)        (0x00413000 + ((_i) * 4 + (_j) * 512))
#define GLQF_FDINSET(_i, _j)       (0x00412000 + ((_i) * 4 + (_j) * 512))
#define GLQF_HSYMM(_i, _j)         (0x0040F000 + ((_i) * 4 + (_j) * 512))
#define GLQF_HMASK(_i)             (0x0040FC00 + ((_i) * 4))
#define GLQF_FDMASK(_i)            (0x00410800 + ((_i) * 4))
#define GLQF_FDMASK_SEL(_i)        (0x00410400 + ((_i) * 4))
#define GLQF_HMASK_SEL(_i)         (0x00410000 + ((_i) * 4))
#define GLFLXP_RXDID_FLAGS(_i, _j) (0x0045D000 + ((_i) * 4 + (_j) * 256))
#define E830_VSIQF_FD_CNT_FD_BCNT_M  GENMASK(31, 16)
#define E800_VSIQF_FD_CNT_FD_BCNT_M  GENMASK(29, 16)
#define E830_VSIQF_FD_CNT_FD_GCNT_M  GENMASK(15, 0)
#define E800_VSIQF_FD_CNT_FD_GCNT_M  GENMASK(13, 0)
#define E830_GLQF_FD_SIZE_FD_BSIZE_M   GENMASK(31, 16)
#define E830_GLQF_FD_SIZE_FD_GSIZE_M   GENMASK(15, 0)
#define E800_GLQF_FD_SIZE_FD_GSIZE_M   GENMASK(14, 0)
#define E800_GLQF_FD_SIZE_FD_BSIZE_M   GENMASK(30, 16)
#define E830_GLQF_FD_CNT_FD_BCNT_M     GENMASK(31, 16)
#define E800_GLQF_FD_CNT_FD_BCNT_M     GENMASK(30, 16)
#define E830_MBX_VF_DEC_TRIG(_VF)   (0x00233800 + (_VF) * 4)
#define E830_MBX_VF_IN_FLIGHT_MSGS_AT_PF_CNT(_VF) (0x00233000 + (_VF) * 4)
#define E830_GLTSYN_TIME_L(_tmr_idx) (0x0008A000 + 0x1000 * (_tmr_idx))
#define E830_ETH_GLTSYN_CMD         0x00088814
#define E810_ETH_GLTSYN_CMD         0x03000344
#define ETH_GLTSYN_SHADJ_L(_i)      (0x03000378 + ((_i) * 32))
#define ETH_GLTSYN_SHADJ_H(_i)      (0x0300037C + ((_i) * 32))
#define ETH_GLTSYN_ENA(_i)          (0x03000348 + ((_i) * 4))
#define ETH_GLTSYN_SHTIME_L(i)      (0x0300036C + ((i) * 32))
#define ETH_GLTSYN_SHTIME_0(i)      (0x03000368 + ((i) * 32))
#define E830_PRTMAC_CL01_QNT_THR     0x001E3320
#define E830_PRTMAC_CL01_PS_QNT      0x001E32A0
#define E800_PRTMAC_HSEC_CTL_TX_PS_QNT(_i) (0x001E36E0 + ((_i) * 32))
#define E800_PRTMAC_HSEC_CTL_TX_PS_RFSH_TMR(_i) (0x001E3800 + ((_i) * 32))
#define E830_PRTTSYN_TXTIME_H(_i)    (0x001E5800 + ((_i) * 32))
#define E830_PRTTSYN_TXTIME_L(_i)    (0x001E5000 + ((_i) * 32))
#define E830_PRTMAC_TS_TX_MEM_VALID_H 0x001E2020
#define E830_PRTMAC_TS_TX_MEM_VALID_L 0x001E2000
#define E830_GLQTX_TXTIME_DBELL_LSB(_DBQM) (0x002E0000 + ((_DBQM) * 8))
#define E830_GLTXTIME_FETCH_PROFILE(_i, _j) (0x002D3500 + ((_i) * 4 + (_j) * 64))
#define E830_PFPTM_SEM               0x00088B00
#define E830_GLPTM_ART_CTL           0x00088B50
#define E830_GLPTM_ART_TIME_L        0x00088B58
#define E830_GLPTM_ART_TIME_H        0x00088B54
#define E830_GLTSYN_PTMTIME_L(_i)    (0x00088B40 + ((_i) * 4))
#define E830_GLTSYN_PTMTIME_H(_i)    (0x00088B48 + ((_i) * 4))
#define E830_GLTSYN_CMD              0x00088814
#define GLHH_ART_CTL                 0x000A41D4
#define GLHH_ART_TIME_L              0x000A41DC
#define GLHH_ART_TIME_H              0x000A41D8
#define PFHH_SEM                     0x000A4200
#define GLTSYN_HHTIME_L(_i)          (0x000888F8 + ((_i) * 4))
#define GLTSYN_HHTIME_H(_i)          (0x00088900 + ((_i) * 4))
#define ICE_M(m, s)                  ((m ## U) << (s))

/* Useful macros from driver */
#define BIT_ULL(nr)                   (1ULL << (nr))
#define GENMASK(h, l)                 (((~0UL) << (l)) & (~0UL >> (BITS_PER_LONG - 1 - (h))))

/* MSI-X related: driver uses MSI-X, so we enable it */

/* DMA support */

/* Power Management */

/* Timer support */

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
    /* We shadow a few key control/status registers */
    struct {
        uint32_t pfgen_ctrl;
        uint32_t pfgen_state;
        uint32_t glgen_rstat;
        uint32_t glgen_rstctl;
        uint32_t glgen_rtrig;
        uint32_t glgen_stat;
        uint32_t glgen_clkstat_src;
        uint32_t glgen_switch_mode_config;
        uint32_t glnvm_uld;
        uint32_t glnvm_fla;
        uint32_t glnvm_gens;
        uint32_t gl_mng_fwsm;
        uint32_t gl_mdet_rx;
        uint32_t gl_mdet_tx_pqm;
        uint32_t gl_pwr_mode_ctl;
        uint32_t glpci_cnf2;
        uint32_t glint_ctl;
        uint32_t pfint_alloc;
        uint32_t pfint_oicr;
        uint32_t pfint_oicr_ena;
        uint32_t pfint_oicr_ctl;
        uint32_t pfint_fw_ctl;
        uint32_t pfint_mbx_ctl;
        uint32_t pfint_sb_ctl;
        uint32_t pfint_tsyn_msk;
        uint32_t pf_func_rid;
        uint32_t pf_pci_ciaa;
        uint32_t pf_pci_ciad;
        uint32_t pf_mdet_rx;
        uint32_t pf_mdet_tx_tclan;
        uint32_t pf_mdet_tx_pqm;
        uint32_t pf_sb_atqbah;
        uint32_t pf_sb_atqbal;
        uint32_t pf_sb_rem_dev_ctl;
        uint32_t pfqf_fd_ena;
        uint32_t pfqf_fd_size;
        uint32_t pfpm_apm;
        uint32_t pfpm_wus;
        uint32_t pfpm_wufc;
        uint32_t prtgen_status;
        uint32_t prtdcb_tup2tc;
        uint32_t prtdcb_gens;
        uint32_t prtdcb_genc;
        uint32_t prtrpb_rdpc;
        uint32_t pftsyn_sem;
        uint32_t gltsyn_ena[4];
        uint32_t gltsyn_stat[4];
        uint32_t gltsyn_incval_l[4];
        uint32_t gltsyn_incval_h[4];
        uint32_t gltsyn_shtime_l[4];
        uint32_t gltsyn_shtime_0[4];
        uint32_t gltsyn_shtime_h[4];
        uint32_t gltsyn_time_l[4];
        uint32_t gltsyn_time_h[4];
        uint32_t gltsyn_time_0[4];
        uint32_t gltsyn_tgt_l[4][4];
        uint32_t gltsyn_tgt_h[4][4];
        uint32_t gltsyn_evnt_l[4][4];
        uint32_t gltsyn_evnt_h[4][4];
        uint32_t gltsyn_aux_in[4][4];
        uint32_t gltsyn_aux_out[4][4];
        uint32_t gltsyn_clko[4][4];
        uint32_t gltsyn_shadj_l[4];
        uint32_t gltsyn_shadj_h[4];
        uint32_t gltsyn_cmd;
        uint32_t gltsyn_sync_dlay;
        uint32_t glgen_gpio_ctl[4];
        uint32_t gldcb_rtctq;
        uint32_t qint_rqctl[2048];
        uint32_t qint_tqctl[2048];
        uint32_t qrx_ctrl[2048];
        uint32_t qrx_tail[2048];
        uint32_t qrx_itr[2048];
        uint32_t qtx_comm_head[2048];
        uint32_t qtx_comm_dbell[2048];
        uint32_t qrxfxp_cntxt[2048];
        uint32_t vsiqf_fd_size[768];
        uint32_t vsiqf_fd_cnt[768];
        uint32_t vplan_tx_qbase[256];
        uint32_t vplan_rx_qbase[256];
        uint32_t vplan_rxq_mapena[256];
        uint32_t vplan_txq_mapena[256];
        uint32_t vpint_alloc[256];
        uint32_t vpint_alloc_pci[256];
        uint32_t vpint_mbx_ctl[256];
        uint32_t vp_mdet_tx_tclan[256];
        uint32_t vp_mdet_tx_pqm[256];
        uint32_t vp_mdet_rx[256];
        uint32_t vp_mdet_tx_tdpu[256];
        uint32_t vfgen_rstat[256];
        uint32_t vpgen_vfrtrig[256];
        uint32_t vpgen_vfrstat[256];
        uint32_t vf_mbx_atqlen[256];
        uint32_t vf_mbx_arqlen[256];
        uint32_t gl_mdck_tx_tdpu;
        uint32_t gl_preext_l2_pmask0[4];
        uint32_t gl_preext_l2_pmask1[4];
        /* GLPRT and GLV statistics registers (many) omitted for brevity */
        uint32_t qrx_context[2048][8];
        uint32_t glcomm_quanta_prof[16];
        uint32_t glqf_fd_size2;
        uint32_t glqf_fd_cnt;
        uint32_t glqf_fdswap[64][512];
        uint32_t glqf_fdinset[64][512];
        uint32_t glqf_hsymm[64][512];
        uint32_t glqf_hmask[32];
        uint32_t glqf_fdmask[32];
        uint32_t glqf_fdmask_sel[32];
        uint32_t glqf_hmask_sel[32];
        uint32_t glflxp_rxdid_flags[64][256];
    } regs;

    /* DMA Context */
    /* DMA is used: keep placeholder */
    struct {
        dma_addr_t addr;
        uint32_t count;
        bool active;
    } dma;

    /* Operational status flags */
    uint32_t status;

    /* Reset state */
    bool reset_active;

    /* Power management state */
    uint8_t pm_state; /* D0-D3 */

    /* Timers */
    QEMUTimer *timer;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    if (s->intr_status & s->intr_mask) {
        if (s->has_msix) {
            msix_notify(pdev, 0);
        } else if (s->has_msi) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!s->has_msix && !s->has_msi) {
            pci_set_irq(pdev, 0);
        }
    }
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /* DMA implementation will go here */
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case PFGEN_CTRL:
        val = s->regs.pfgen_ctrl;
        break;
    case PFGEN_STATE:
        val = s->regs.pfgen_state;
        break;
    case GLGEN_RSTAT:
        val = s->regs.glgen_rstat;
        break;
    case GLGEN_RSTCTL:
        val = s->regs.glgen_rstctl;
        break;
    case GLGEN_RTRIG:
        val = s->regs.glgen_rtrig;
        break;
    case GLGEN_STAT:
        val = s->regs.glgen_stat;
        break;
    case GLGEN_CLKSTAT_SRC:
        val = s->regs.glgen_clkstat_src;
        break;
    case GLGEN_SWITCH_MODE_CONFIG:
        val = s->regs.glgen_switch_mode_config;
        break;
    case GLNVM_ULD:
        val = s->regs.glnvm_uld;
        break;
    case GLNVM_FLA:
        val = s->regs.glnvm_fla;
        break;
    case GLNVM_GENS:
        val = s->regs.glnvm_gens;
        break;
    case GL_MNG_FWSM:
        val = s->regs.gl_mng_fwsm;
        break;
    case GL_MDET_RX:
        val = s->regs.gl_mdet_rx;
        break;
    case GL_MDET_TX_PQM:
        val = s->regs.gl_mdet_tx_pqm;
        break;
    case GL_PWR_MODE_CTL:
        val = s->regs.gl_pwr_mode_ctl;
        break;
    case GLPCI_CNF2:
        val = s->regs.glpci_cnf2;
        break;
    case GLINT_CTL:
        val = s->regs.glint_ctl;
        break;
    case PFINT_ALLOC:
        val = s->regs.pfint_alloc;
        break;
    case PFINT_OICR:
        val = s->regs.pfint_oicr;
        break;
    case PFINT_OICR_ENA:
        val = s->regs.pfint_oicr_ena;
        break;
    case PFINT_OICR_CTL:
        val = s->regs.pfint_oicr_ctl;
        break;
    case PFINT_FW_CTL:
        val = s->regs.pfint_fw_ctl;
        break;
    case PFINT_MBX_CTL:
        val = s->regs.pfint_mbx_ctl;
        break;
    case PFINT_SB_CTL:
        val = s->regs.pfint_sb_ctl;
        break;
    case PFINT_TSYN_MSK:
        val = s->regs.pfint_tsyn_msk;
        break;
    case PF_FUNC_RID:
        val = s->regs.pf_func_rid;
        break;
    case PF_PCI_CIAA:
        val = s->regs.pf_pci_ciaa;
        break;
    case PF_PCI_CIAD:
        val = s->regs.pf_pci_ciad;
        break;
    case PF_MDET_RX:
        val = s->regs.pf_mdet_rx;
        break;
    case E800_PF_MDET_TX_TCLAN:
        val = s->regs.pf_mdet_tx_tclan;
        break;
    case PF_MDET_TX_PQM:
        val = s->regs.pf_mdet_tx_pqm;
        break;
    case PF_SB_ATQBAH:
        val = s->regs.pf_sb_atqbah;
        break;
    case PF_SB_ATQBAL:
        val = s->regs.pf_sb_atqbal;
        break;
    case PF_SB_REM_DEV_CTL:
        val = s->regs.pf_sb_rem_dev_ctl;
        break;
    case PFQF_FD_ENA:
        val = s->regs.pfqf_fd_ena;
        break;
    case PFQF_FD_SIZE:
        val = s->regs.pfqf_fd_size;
        break;
    case PFPM_APM:
        val = s->regs.pfpm_apm;
        break;
    case PFPM_WUS:
        val = s->regs.pfpm_wus;
        break;
    case PFPM_WUFC:
        val = s->regs.pfpm_wufc;
        break;
    case PRTGEN_STATUS:
        val = s->regs.prtgen_status;
        break;
    case PRTDCB_TUP2TC:
        val = s->regs.prtdcb_tup2tc;
        break;
    case PRTDCB_GENS:
        val = s->regs.prtdcb_gens;
        break;
    case PRTDCB_GENC:
        val = s->regs.prtdcb_genc;
        break;
    case PRTRPB_RDPC:
        val = s->regs.prtrpb_rdpc;
        break;
    case PFTSYN_SEM:
        val = s->regs.pftsyn_sem;
        break;
    case GLTSYN_CMD:
        val = s->regs.gltsyn_cmd;
        break;
    case GLTSYN_SYNC_DLAY:
        val = s->regs.gltsyn_sync_dlay;
        break;
    case GL_MDCK_TX_TDPU:
        val = s->regs.gl_mdck_tx_tdpu;
        break;
    case GLQF_FD_SIZE:
        val = s->regs.glqf_fd_size2;
        break;
    case GLQF_FD_CNT:
        val = s->regs.glqf_fd_cnt;
        break;
    default:
        if (addr >= GLINT_DYN_CTL(0) && addr < GLINT_DYN_CTL(2048)) {
            val = 0;
            break;
        }
        if (addr >= GLINT_ITR(0,0) && addr < GLINT_ITR(3,799)) {
            /* Not storing per-ITR; return 0 */
            break;
        }
        if (addr >= GLTSYN_ENA(0) && addr < GLTSYN_ENA(4)) {
            uint32_t i = (addr - GLTSYN_ENA(0)) / 4;
            val = s->regs.gltsyn_ena[i];
            break;
        }
        if (addr >= GLTSYN_STAT(0) && addr < GLTSYN_STAT(4)) {
            uint32_t i = (addr - GLTSYN_STAT(0)) / 4;
            val = s->regs.gltsyn_stat[i];
            break;
        }
        if (addr >= GLTSYN_TGT_L_0(0) && addr < GLTSYN_TGT_L_0(4)) {
            uint32_t i = (addr - GLTSYN_TGT_L_0(0)) / 4;
            if (i < 4) val = s->regs.gltsyn_tgt_l[0][i]; /* simplified: assuming chan=0 */
            break;
        }
        /* many more cases omitted for brevity; full implementation would map all */
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case PFGEN_CTRL:
        s->regs.pfgen_ctrl = val;
        break;
    case PFGEN_STATE:
        s->regs.pfgen_state = val;
        break;
    case GLGEN_RSTAT:
        s->regs.glgen_rstat = val;
        break;
    case GLGEN_RSTCTL:
        s->regs.glgen_rstctl = val;
        /* Simulate reset completion: set device state to ready */
        s->regs.glgen_rstat = 1; /* DEVICE_STATE_READY */
        s->regs.pfgen_state = 1;  /* PF_STATE_READY */
        s->regs.glnvm_uld = 1;   /* Simulate NVM loaded */
        s->regs.gl_mng_fwsm = 0x1; /* Firmware loaded indicator */
        break;
    case GLGEN_RTRIG:
        s->regs.glgen_rtrig = val;
        break;
    case GLGEN_STAT:
        s->regs.glgen_stat = val;
        break;
    case GLGEN_CLKSTAT_SRC:
        s->regs.glgen_clkstat_src = val;
        break;
    case GLGEN_SWITCH_MODE_CONFIG:
        s->regs.glgen_switch_mode_config = val;
        break;
    case GLNVM_ULD:
        s->regs.glnvm_uld = val;
        break;
    case GLNVM_FLA:
        s->regs.glnvm_fla = val;
        break;
    case GLNVM_GENS:
        s->regs.glnvm_gens = val;
        break;
    case GL_MNG_FWSM:
        s->regs.gl_mng_fwsm = val;
        break;
    case GL_MDET_RX:
        s->regs.gl_mdet_rx = val;
        break;
    case GL_MDET_TX_PQM:
        s->regs.gl_mdet_tx_pqm = val;
        break;
    case GL_PWR_MODE_CTL:
        s->regs.gl_pwr_mode_ctl = val;
        break;
    case GLPCI_CNF2:
        s->regs.glpci_cnf2 = val;
        break;
    case GLINT_CTL:
        s->regs.glint_ctl = val;
        break;
    case PFINT_ALLOC:
        s->regs.pfint_alloc = val;
        break;
    case PFINT_OICR:
        /* Write 1 to clear bits */
        s->regs.pfint_oicr &= ~val;
        break;
    case PFINT_OICR_ENA:
        s->regs.pfint_oicr_ena = val;
        s->intr_mask = val;
        pcibase_update_irq(s);
        break;
    case PFINT_OICR_CTL:
        s->regs.pfint_oicr_ctl = val;
        break;
    case PFINT_FW_CTL:
        s->regs.pfint_fw_ctl = val;
        break;
    case PFINT_MBX_CTL:
        s->regs.pfint_mbx_ctl = val;
        break;
    case PFINT_SB_CTL:
        s->regs.pfint_sb_ctl = val;
        break;
    case PFINT_TSYN_MSK:
        s->regs.pfint_tsyn_msk = val;
        break;
    case PF_FUNC_RID:
        s->regs.pf_func_rid = val;
        break;
    case PF_PCI_CIAA:
        s->regs.pf_pci_ciaa = val;
        break;
    case PF_PCI_CIAD:
        s->regs.pf_pci_ciad = val;
        break;
    case PF_MDET_RX:
        s->regs.pf_mdet_rx = val;
        break;
    case E800_PF_MDET_TX_TCLAN:
        s->regs.pf_mdet_tx_tclan = val;
        break;
    case PF_MDET_TX_PQM:
        s->regs.pf_mdet_tx_pqm = val;
        break;
    case PF_SB_ATQBAH:
        s->regs.pf_sb_atqbah = val;
        break;
    case PF_SB_ATQBAL:
        s->regs.pf_sb_atqbal = val;
        break;
    case PF_SB_REM_DEV_CTL:
        s->regs.pf_sb_rem_dev_ctl = val;
        break;
    case PFQF_FD_ENA:
        s->regs.pfqf_fd_ena = val;
        break;
    case PFQF_FD_SIZE:
        s->regs.pfqf_fd_size = val;
        break;
    case PFPM_APM:
        s->regs.pfpm_apm = val;
        break;
    case PFPM_WUS:
        s->regs.pfpm_wus = val;
        break;
    case PFPM_WUFC:
        s->regs.pfpm_wufc = val;
        break;
    case PRTGEN_STATUS:
        s->regs.prtgen_status = val;
        break;
    case PRTDCB_TUP2TC:
        s->regs.prtdcb_tup2tc = val;
        break;
    case PRTDCB_GENS:
        s->regs.prtdcb_gens = val;
        break;
    case PRTDCB_GENC:
        s->regs.prtdcb_genc = val;
        break;
    case PRTRPB_RDPC:
        s->regs.prtrpb_rdpc = val;
        break;
    case PFTSYN_SEM:
        s->regs.pftsyn_sem = val;
        break;
    case GLTSYN_CMD:
        s->regs.gltsyn_cmd = val;
        break;
    case GLTSYN_SYNC_DLAY:
        s->regs.gltsyn_sync_dlay = val;
        break;
    case GL_MDCK_TX_TDPU:
        s->regs.gl_mdck_tx_tdpu = val;
        break;
    case GLQF_FD_SIZE:
        s->regs.glqf_fd_size2 = val;
        break;
    case GLQF_FD_CNT:
        s->regs.glqf_fd_cnt = val;
        break;
    default:
        if (addr >= GLINT_DYN_CTL(0) && addr < GLINT_DYN_CTL(2048)) {
            /* store the value, but we may need to trigger interrupt enable */
            if (val & 1) { /* assuming INTENA is bit 0 */
                /* enable interrupt for vector i */
            }
            break;
        }
        if (addr >= GLINT_ITR(0,0) && addr < GLINT_ITR(3,799)) {
            /* store ITR values */
            break;
        }
        if (addr >= GLTSYN_ENA(0) && addr < GLTSYN_ENA(4)) {
            uint32_t i = (addr - GLTSYN_ENA(0)) / 4;
            s->regs.gltsyn_ena[i] = val;
            break;
        }
        if (addr >= GLTSYN_STAT(0) && addr < GLTSYN_STAT(4)) {
            uint32_t i = (addr - GLTSYN_STAT(0)) / 4;
            s->regs.gltsyn_stat[i] = val;
            break;
        }
        if (addr >= GLTSYN_TGT_L_0(0) && addr < GLTSYN_TGT_L_0(4)) {
            uint32_t i = (addr - GLTSYN_TGT_L_0(0)) / 4;
            if (i < 4) s->regs.gltsyn_tgt_l[0][i] = val;
            break;
        }
        /* many more cases omitted */
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    uint64_t val = 0;
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* No PIO */
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

    /* Reset registers to power-on defaults */
    memset(&s->regs, 0, sizeof(s->regs));
    s->intr_status = 0;
    s->intr_mask = 0;
    s->status = 0;
    s->reset_active = false;
    s->pm_state = 0; /* D0 */

    /* Initialize GLGEN_RSTAT to DEAD (0) to match hardware power-on */
    s->regs.glgen_rstat = 0; /* DEVICE_STATE_DEAD */
    s->regs.pfgen_state = 0; /* PF_STATE_DEAD */
    s->regs.glnvm_uld = 0;   /* NVM not loaded yet */
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
    s->bar_info[0] = (BARInfo){
        .index = 0,
        .type = BAR_TYPE_MMIO,
        .size = 0x1000000, /* 16 MB, placeholder: actual size from driver */
        .name = "ice-mmio"
    };

    /* Register BAR0 as MMIO region */
    pcibase_register_bar(pdev, s, &s->bar_info[0], errp);

    /* Setup MSI-X within BAR0 using exclusive bar */
    if (msix_init_exclusive_bar(pdev, ICE_MSIX_VECTORS, 0, errp)) {
        return;
    }
    s->has_msix = true;

    /* DMA: no specific init needed here, handled by driver via descriptors */

    /* Timer init */
    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, NULL, NULL);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, &s->bar_regions[0], &s->bar_regions[0]);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }

    timer_del(s->timer);
    timer_free(s->timer);
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "ice_pci",
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
