/*
 * This file is part of the QEMU device model for Freescale ENETC VF
 * Based on Linux driver enetc_vf.c (LSN 7.1).
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
#include "qemu/bitops.h"

#define TYPE_PCIBASE_DEVICE "fsl_enetc_vf_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI IDs from driver */
#define PCI_VENDOR_ID_FREESCALE     0x1957
#define PCI_DEVICE_ID_ENETC_VF      0xef00
#define PCI_CLASS_NETWORK_ETHERNET  0x0200

/* Register offsets copied from driver headers */
#define ENETC_VSIMSGSNDAR0    0x210
#define ENETC_VSIMSGSNDAR1    0x214
#define ENETC_SIMSGSR_GET_MC(val) ((val) >> 16)
#define ENETC_VSIMSGSR_MS     BIT(1)
#define ENETC_VSIMSGSR_MB     BIT(0)
#define ENETC_VSIMSGSR        0x204
#define ENETC_REV_1_0         0x0100
#define ENETC_DEV_ID_VF       0xef00
#define ENETC_SIRSSCAPR       0x1600
#define ENETC_SIPCAPR0_LSO    BIT(1)
#define ENETC_SI_F_LSO        BIT(3)
#define ENETC_SIRFSCAPR       0x1200
#define ENETC_SIPCAPR0_RFS    BIT(2)
#define ENETC_SIRSSCAPR_GET_NUM_RSS(val) (BIT((val) & 0xf) * 32)
#define ENETC_SICAPR0         0x900
#define ENETC_SIPCAPR0_RSS    BIT(8)
#define ENETC_SIPCAPR0        0x20
#define ENETC_SICAR_RD_COHERENT 0x2b2b0000
#define ENETC_SICBDRBAR1      0x814
#define ENETC_SICAR2          0x48
#define ENETC_SICBDRBAR0      0x810
#define ENETC_SICBDRPIR       0x818
#define ENETC_SICBDRCIR       0x81c
#define ENETC_RTBLENR_LEN(n)  ((n) & ~0x7)
#define ENETC_SICBDRLENR      0x820
#define ENETC_SICBDRMR        0x800
#define ENETC_SICAR_WR_COHERENT 0x00006727
#define ENETC_SIMR_EN          BIT(31)
#define ENETC_SIMR             0
#define ENETC_SICAR1           0x44
#define ENETC_SICAR0           0x40
#define ENETC_SICAR_MSI        0x00300030
#define ENETC_SI_ALIGN         32
#define ENETC_GLOBAL_BASE      0x20000
#define ENETC_BAR_REGS         0
#define ENETC_PORT_BASE        0x10000
#define ENETC_SIRBGCR          0x38
#define ENETC_SIMR_RSSE        BIT(0)
#define ENETC_TBMR_PRIO_MASK   GENMASK(2, 0)
#define ENETC_TBMR             0
#define ENETC_SIPMAR0          0x80
#define ENETC_SIPMAR1          0x84
#define ENETC_REV1             0x1
#define ENETC4_SILSOSFMR1      0x1304
#define ENETC4_SILSOSFMR0      0x1300
#define SILSOSFMR0_VAL_SET(first, mid) (FIELD_PREP(SILSOSFMR0_TCP_MID_SEG, mid) | FIELD_PREP(SILSOSFMR0_TCP_1ST_SEG, first))
#define ENETC4_TCP_FLAGS_RST    BIT(2)
#define ENETC4_TCP_FLAGS_PSH    BIT(3)
#define ENETC4_TCP_FLAGS_FIN    BIT(0)
#define ENETC_REV_4_3          0x0403
#define ENETC_REV_4_1          0X0401
#define ENETC_TBIER           0xa0
#define ENETC_RBICR1          0xac
#define ENETC_RBIER           0xa0
#define ENETC_SIMSITRV(n)     (0xB00 + (n) * 0x4)
#define ENETC_SIMSIRRV(n)     (0xB80 + (n) * 0x4)
#define ENETC_BDR(t, i, r)    (0x8000 + (t) * 0x100 + ENETC_BDR_OFF(i) + (r))
#define ENETC_SI_F_PPM        BIT(4)
#define ENETC_RBMR_VTE        BIT(5)
#define ENETC_RBMR           0
#define ENETC_TBMR_VIH        BIT(9)
#define ENETC_MMCSR_ME        BIT(16)
#define ENETC_MMCSR_VDIS      BIT(17)
#define ENETC_MMCSR_GET_VT(x) (((x) & ENETC_MMCSR_VT_MASK) >> 23)
#define ENETC_MMCSR          0x1f00
#define ENETC_BDR_OFF(i)     ((i) * 0x200)
#define ENETC_RBMR_BDS       BIT(2)
#define ENETC_TBIER_TXTIE    BIT(0)
#define ENETC_RBIER_RXTIE    BIT(0)
#define ENETC_RBICR0_SET_ICPT(n) ((n) & ENETC_RBICR0_ICPT_MASK)
#define ENETC_TBICR1         0xac
#define ENETC_TBICR0_SET_ICPT(n) ((ilog2(n) + 1) & ENETC_TBICR0_ICPT_MASK)
#define ENETC_TBMR_EN        BIT(31)
#define ENETC_RBICR0_ICPT_MASK 0x1ff
#define ENETC_TBICR0_ICPT_MASK 0xf
#define ENETC_RBMR_EN        BIT(31)
#define ENETC_TXBD_FLAGS_OFFSET 24
#define ENETC_TXBD_TXSTART_MASK GENMASK(24, 0)
#define ENETC_TXBD_TSTAMP    GENMASK(29, 0)
#define ENETC_SICTR0         0x18
#define ENETC_SICTR1         0x1c
#define ENETC_TBSR           0x4
#define ENETC_TBSR_BUSY      BIT(0)
#define ENETC_CBD_STATUS_MASK 0xf
#define ENETC_MMCSR_VT_MASK  GENMASK(29, 23)
#define ENETC_PFPMR_PMACE    BIT(1)
#define ENETC_MMCSR_RAFS_MASK GENMASK(9, 8)
#define ENETC_MMCSR_VT(x)    (((x) << 23) & ENETC_MMCSR_VT_MASK)
#define ENETC_PFPMR          0x1900
#define ENETC_MMCSR_LINK_FAIL BIT(31)
#define ENETC_MMCSR_RAFS(x)  (((x) << 8) & ENETC_MMCSR_RAFS_MASK)
#define ENETC_MMCSR_LPE      BIT(1)
#define ENETC_MMCSR_GET_RAFS(x) (((x) & ENETC_MMCSR_RAFS_MASK) >> 8)
#define ENETC_RSSHASH_KEY_SIZE 40
#define ENETC_RBDCR(n)        (0x8180 + (n) * 0x200)
#define ENETC_MMFCRXR         0x1f14
#define ENETC_MMFCTXR         0x1f18
#define ENETC_MMHCR           0x1f1c
#define ENETC_MMFAECR         0x1f08
#define ENETC_MMFAOCR         0x1f10
#define ENETC_MMFSECR         0x1f0c
#define ENETC4_PM_SINGLE_STEP(mac) (0x50c0 + (mac) * 0x400)
#define PM_SINGLE_STEP_CH      BIT(6)
#define PM_SINGLE_STEP_OFFSET_SET(o) FIELD_PREP(PM_SINGLE_STEP_OFFSET, o)
#define PM_SINGLE_STEP_EN      BIT(31)
#define ENETC_PM0_SINGLE_STEP_EN BIT(31)
#define ENETC_PM0_SINGLE_STEP  0x80c0
#define ENETC_PM0_SINGLE_STEP_CH BIT(7)
#define ENETC_SET_SINGLE_STEP_OFFSET(v) (((v) & 0xff) << 8)
#define ENETC4_PM_TCNP(mac)    (0x52c0 + (mac) * 0x400)
#define ENETC_PM_RCNP(mac)     (0x81C0 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_REV4            0x4
#define ENETC4_PM_RCNP(mac)    (0x51c0 + (mac) * 0x400)
#define ENETC_PM_TCNP(mac)     (0x82C0 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM0_CMD_CFG      0x8008
#define ENETC_PM0_RX_EN        BIT(1)
#define ENETC_SITFRM           0x328
#define ENETC_SITUCA           0x330
#define ENETC_SIRUCA           0x310
#define ENETC_SIROCT           0x300
#define ENETC_SITOCT           0x320
#define ENETC_SITMCA           0x338
#define ENETC_SIRMCA           0x318
#define ENETC_SIRFRM           0x308
#define ENETC_PM_RDRP(mac)     (0x8158 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_TUCA(mac)     (0x8240 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_RERR(mac)     (0x8138 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_TUND(mac)     (0x8268 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_TVLAN(mac)    (0x8230 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_RUCA(mac)     (0x8140 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_TOCT(mac)     (0x8208 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_TPKT(mac)     (0x8260 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_TFCS(mac)     (0x8228 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_RPKT(mac)     (0x8160 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_RVLAN(mac)    (0x8130 + ENETC_PMAC_OFFSET * (mac))
#define ENETC4_PM_TUCA(mac)    (0x5240 + (mac) * 0x400)
#define ENETC4_PM_RERR(mac)    (0x5138 + (mac) * 0x400)
#define ENETC4_PM_TVLAN(mac)   (0x5230 + (mac) * 0x400)
#define ENETC4_PM_TUND(mac)    (0x5268 + (mac) * 0x400)
#define ENETC4_PM_RUCA(mac)    (0x5140 + (mac) * 0x400)
#define ENETC4_PM_TIOCT(mac)   (0x52f8 + (mac) * 0x400)
#define ENETC4_PM_TPKT(mac)    (0x5260 + (mac) * 0x400)
#define ENETC4_PM_TOCT(mac)    (0x5208 + (mac) * 0x400)
#define ENETC4_PM_RVLAN(mac)   (0x5130 + (mac) * 0x400)
#define ENETC4_PM_RDRP(mac)    (0x5158 + (mac) * 0x400)
#define ENETC4_PM_RPKT(mac)    (0x5160 + (mac) * 0x400)
#define ENETC4_PM_ROCT(mac)    (0x5108 + (mac) * 0x400)
#define ENETC4_PM_TFCS(mac)    (0x5228 + (mac) * 0x400)
#define ENETC4_PUFDVFR         0x2d0
#define ENETC4_PBFDSIR         0x208
#define ENETC4_PRXDCR          0x41c0
#define ENETC4_PICDRDCR(a)     ((a) * 0x10 + 0x140)
#define ENETC4_PBFDVFR         0x2d8
#define ENETC4_PFDMSAPR        0x20c
#define ENETC4_PUFDMFR         0x284
#define ENETC4_PRXDCRR1        0x41cc
#define ENETC4_PMFDVFR         0x2d4
#define ENETC4_PRXDCRRR        0x41c4
#define ENETC4_PRXDCRR0        0x41c8
#define ENETC_UFDMF           0x1680
#define ENETC_PICDR(n)         (0x0700 + (n) * 8)
#define ENETC_MFDMF           0x1684
#define ENETC_PBFDSIR         0x0810
#define ENETC_PUFDVFR         0x1780
#define ENETC_PBFDVFR         0x1788
#define ENETC_PMFDVFR         0x1784
#define ENETC_PFDMSAPR        0x0814
#define ENETC_MMCSR_LAFS_MASK  GENMASK(4, 3)
#define ENETC_PSR             0x0004
#define ENETC_PCAPR0          0x0900
#define ENETC_PRFSCAPR        0x1804
#define ENETC_PSIPMAR1(n)     (0x0104 + (n) * 0x8)
#define ENETC_PMR             0x0000
#define ENETC_PTXMBAR         0x0608
#define ENETC_PSICFGR0(n)     (0x0940 + (n) * 0xc)
#define ENETC_PSIPMR          0x0018
#define ENETC_PCAPR1          0x0904
#define ENETC_PM0_MAXFRM      0x8014
#define ENETC_PM0_IF_MODE     0x8300
#define ENETC_PTCMSDUR(n)     (0x2020 + (n) * 4)
#define ENETC_PSIPMAR0(n)     (0x0100 + (n) * 0x8)
#define ENETC_SICAPR1         0x904
#define ENETC_SICBDRSR        0x804
#define ENETC_SIUEFDCR        0xe28
#define ENETC_RBSR            0x4
#define ENETC4_PM_TXPF(mac)   (0x5218 + (mac) * 0x400)
#define ENETC_PM_RXPF(mac)    (0x8118 + ENETC_PMAC_OFFSET * (mac))
#define ENETC4_PM_RXPF(mac)   (0x5118 + (mac) * 0x400)
#define ENETC_PM_TXPF(mac)    (0x8218 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_RFSE_EN         BIT(15)
#define ENETC_RFSE_MODE_BD    2
#define ENETC4_PPMTUFCR       0x50c8
#define ENETC4_PPMRBFCR       0x5098
#define ENETC4_PPMTMFCR       0x50d0
#define ENETC4_PPMROCR        0x5080
#define ENETC4_PPMRUFCR       0x5088
#define ENETC4_PPMTBFCR       0x50d8
#define ENETC4_PPMRMFCR       0x5090
#define ENETC4_PPMTOCR        0x50c0
#define PM_SINGLE_STEP_OFFSET  GENMASK(15, 7)
#define ENETC_PRSSK(n)        (0x1410 + (n) * 4)
#define ENETC4_PRSSKR(n)      ((n) * 0x4 + 0x250)
#define ENETC_PM_RMCA(mac)    (0x8148 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_RFRM(mac)    (0x8120 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_TEOCT(mac)   (0x8200 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_TLCOL(mac)   (0x82E8 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_TCRSE(mac)   (0x8210 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_TMCA(mac)    (0x8248 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_TBCA(mac)    (0x8250 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_TFRM(mac)    (0x8220 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_TERR(mac)    (0x8238 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_TDFR(mac)    (0x82D0 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_TECOL(mac)   (0x82F0 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_TSCOL(mac)   (0x82E0 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_RFCS(mac)    (0x8128 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_REOCT(mac)   (0x8100 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_RBCA(mac)    (0x8150 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_RALN(mac)    (0x8110 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_RDRNTP(mac)  (0x81C8 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_TMCOL(mac)   (0x82D8 + ENETC_PMAC_OFFSET * (mac))
#define ENETC4_PM_TSCOL(mac)  (0x52e0 + (mac) * 0x400)
#define ENETC4_PM_TFRM(mac)   (0x5220 + (mac) * 0x400)
#define ENETC4_PM_RDRNTP(mac) (0x51c8 + (mac) * 0x400)
#define ENETC4_PM_RBCA(mac)   (0x5150 + (mac) * 0x400)
#define ENETC4_PM_RFRM(mac)   (0x5120 + (mac) * 0x400)
#define ENETC4_PM_TECOL(mac)  (0x52f0 + (mac) * 0x400)
#define ENETC4_PM_TMCA(mac)   (0x5248 + (mac) * 0x400)
#define ENETC4_PM_TBCA(mac)   (0x5250 + (mac) * 0x400)
#define ENETC4_PM_TMCOL(mac)  (0x52d8 + (mac) * 0x400)
#define ENETC4_PM_TDFR(mac)   (0x52d0 + (mac) * 0x400)
#define ENETC4_PM_RALN(mac)   (0x5110 + (mac) * 0x400)
#define ENETC4_PM_TEOCT(mac)  (0x5200 + (mac) * 0x400)
#define ENETC4_PM_TERR(mac)   (0x5238 + (mac) * 0x400)
#define ENETC4_PM_RMCA(mac)   (0x5148 + (mac) * 0x400)
#define ENETC4_PM_TLCOL(mac)  (0x52e8 + (mac) * 0x400)
#define ENETC4_PM_REOCT(mac)  (0x5100 + (mac) * 0x400)
#define ENETC4_PM_RFCS(mac)   (0x5128 + (mac) * 0x400)
#define ENETC4_PM_R255(mac)   (0x5180 + (mac) * 0x400)
#define ENETC4_PM_R1522(mac)  (0x5198 + (mac) * 0x400)
#define ENETC4_PM_T511(mac)   (0x5288 + (mac) * 0x400)
#define ENETC4_PM_T1523X(mac) (0x52a0 + (mac) * 0x400)
#define ENETC4_PM_T1023(mac)  (0x5290 + (mac) * 0x400)
#define ENETC4_PM_R1523X(mac) (0x51a0 + (mac) * 0x400)
#define ENETC4_PM_ROVR(mac)   (0x51a8 + (mac) * 0x400)
#define ENETC4_PM_R64(mac)    (0x5170 + (mac) * 0x400)
#define ENETC4_PM_RFRG(mac)   (0x51b8 + (mac) * 0x400)
#define ENETC4_PM_R1023(mac)  (0x5190 + (mac) * 0x400)
#define ENETC4_PM_T64(mac)    (0x5270 + (mac) * 0x400)
#define ENETC4_PM_T1522(mac)  (0x5298 + (mac) * 0x400)
#define ENETC4_PM_RJBR(mac)   (0x51b0 + (mac) * 0x400)
#define ENETC4_PM_T255(mac)   (0x5280 + (mac) * 0x400)
#define ENETC4_PM_R127(mac)   (0x5178 + (mac) * 0x400)
#define ENETC4_PM_T127(mac)   (0x5278 + (mac) * 0x400)
#define ENETC4_PM_RUND(mac)   (0x5168 + (mac) * 0x400)
#define ENETC4_PM_R511(mac)   (0x5188 + (mac) * 0x400)
#define ENETC_PM_RJBR(mac)    (0x81B0 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_R127(mac)    (0x8178 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_RUND(mac)    (0x8168 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_T1522(mac)   (0x8298 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_R511(mac)    (0x8188 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_ROVR(mac)    (0x81A8 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_R1523X(mac)  (0x81A0 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_T1023(mac)   (0x8290 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_R1522(mac)   (0x8198 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_T127(mac)    (0x8278 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_R1023(mac)   (0x8190 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_T64(mac)     (0x8270 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_T511(mac)    (0x8288 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_T255(mac)    (0x8280 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_T1523X(mac)  (0x82A0 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_R64(mac)     (0x8170 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_RFRG(mac)    (0x81B8 + ENETC_PMAC_OFFSET * (mac))
#define ENETC_PM_R255(mac)    (0x8180 + ENETC_PMAC_OFFSET * (mac))

/* QEMU device state */
#define ENETC_REG_SIZE 0x100000
#define ENETC_REG_COUNT (ENETC_REG_SIZE / 4)

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[2]; /* BAR0: registers, BAR1: MSI-X */
    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;

    uint32_t regs[ENETC_REG_COUNT]; /* hardware register space */
};

/* PIO functions not used; driver uses MMIO exclusively. */

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= ENETC_REG_SIZE) {
        /* out of bounds */
        return ~0ULL;
    }
    if (addr & 3) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: unaligned read at 0x%"PRIx64" size %u\n",
                      __func__, addr, size);
        return ~0ULL;
    }
    uint32_t reg_idx = addr >> 2;
    val = s->regs[reg_idx];

    switch (addr) {
    case ENETC_VSIMSGSR:
        /* Provide current status */
        break;
    default:
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= ENETC_REG_SIZE) {
        return;
    }
    if (addr & 3) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: unaligned write at 0x%"PRIx64" size %u value %"PRIx64"\n",
                      __func__, addr, size, val);
        return;
    }
    uint32_t reg_idx = addr >> 2;

    switch (addr) {
    case ENETC_VSIMSGSNDAR0:
        s->regs[reg_idx] = val;
        break;
    case ENETC_VSIMSGSNDAR1:
        s->regs[reg_idx] = val;
        break;
    case ENETC_VSIMSGSR:
        /* The driver writes to MB and MS bits? Actually driver reads only. So ignore write. */
        break;
    default:
        s->regs[reg_idx] = val;
        break;
    }

    /* Handle mailbox command completion: when both SNDAR0 and SNDAR1 are written? */
    /* The driver writes SNDAR1 then SNDAR0, so after SNDAR0 write, we can trigger completion. */
    if (addr == ENETC_VSIMSGSNDAR0) {
        /* Simulate immediate command completion: clear MB, set MC to success (0) */
        uint32_t vsimsgsr = 0; /* MB=0, MS=0, MC=0 */
        s->regs[ENETC_VSIMSGSR >> 2] = vsimsgsr;
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 }, /* driver uses 32-bit access */
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Initialize registers to default values */
    memset(s->regs, 0, sizeof(s->regs));

    /* Set capability registers to plausible values */
    /* SICAPR0: indicate RSS present, some RX/TX rings */
    s->regs[ENETC_SICAPR0 >> 2] = 0x00010001; /* 1 RX ring, 1 TX ring? */
    s->regs[ENETC_SICAPR1 >> 2] = 0x00000000;
    s->regs[ENETC_SIPCAPR0 >> 2] = ENETC_SIPCAPR0_RSS; /* RSS supported */
    s->regs[ENETC_SIRSSCAPR >> 2] = 0x00000004; /* 4? gives RSS entries = BIT(4)*32 = 16*32 = 512? Actually formula: BIT(val&0xf)*32. We set 4 -> 16*32=512. But driver will use. */
    /* Primary MAC address: set a valid unicast MAC */
    s->regs[ENETC_SIPMAR0 >> 2] = 0x78563412; /* low 32 bits (bytes 0-3) */
    s->regs[ENETC_SIPMAR1 >> 2] = 0x0000009a; /* high 16 bits (bytes 4-5) plus other? Need ETH_ALEN=6. Actually lower register holds bytes 0-3, upper holds bytes 4-5 in lower 16 bits. 0x9a is byte 5, 0x00 is byte 4. But typical MAC is 00:12:34:56:78:9A -> sipmar0 = 0x78563412, sipmar1 = 0x00009a00? */
    /* Correct: to get 00:12:34:56:78:9A -> driver does put_unaligned_le32(upper, addr); addr[0]=12, addr[1]=34, addr[2]=56, addr[3]=78? Actually le32: upper as bytes: 78 56 34 12. So upper = 0x78563412. Then lower = 0x9a00? put_unaligned_le16(lower, addr+4) sets addr[4]=0x9a, addr[5]=0x00. So MAC = 12:34:56:78:9a:00? Not standard. Better set a common MAC: 00:12:34:56:78:9a. Then upper = 0x56341200? Let's do: desired MAC bytes: 00 12 34 56 78 9a. Then le32 at bytes 0-3: 00 12 34 56 -> 0x56341200? Actually little-endian: byte0 = 0x56, byte1=0x34, byte2=0x12, byte3=0x00 => 0x00341256? I'm messing up. Let's just set sipmar0 = 0x00000000, sipmar1 = 0x00000000 and allow driver to set later. But probe reads MAC to set ndev->dev_addr. A zero MAC might cause registration failure. So let's set a valid one: 00:11:22:33:44:55. Then sipmar0 (bytes 0-3) = 0x33221100? No, little-endian: first byte of MAC is 00, second 11, third 22, fourth 33. So 32-bit value read from sipmar0 will be a little-endian word: bytes: 00 11 22 33 -> 0x33221100. So we set s->regs[ENETC_SIPMAR0>>2] = 0x33221100. sipmar1 for bytes 4-5: 44 55 -> low 16 bits: 0x5544? Actually driver reads 32-bit register, then extracts lower 16 bits: lower = readl(ENETC_SIPMAR1). So the register value should have the two bytes in the lower half: byte4=44, byte5=55. So we set s->regs[ENETC_SIPMAR1>>2] = 0x00005544. That should yield MAC 00:11:22:33:44:55. Good. */
    s->regs[ENETC_SIPMAR0 >> 2] = 0x33221100;
    s->regs[ENETC_SIPMAR1 >> 2] = 0x00005544;

    /* Other registers remain 0. */
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_FREESCALE);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_ENETC_VF);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_ETHERNET);
    pci_set_byte(pci_conf + PCI_REVISION_ID, ENETC_REV1);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR0: MMIO for SI registers, size 1MB */
    memory_region_init_io(&s->bar_regions[0], OBJECT(s), &pcibase_mmio_ops, s, "enetc-regs", ENETC_REG_SIZE);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_regions[0]);

    /* BAR1: MMIO for MSI-X */
    memory_region_init(&s->bar_regions[1], OBJECT(s), "enetc-msix", 0x1000);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_regions[1]);

    /* MSI-X initialization */
    s->has_msi = false;
    s->has_msix = true;
    if (msix_init(pdev, 4, &s->bar_regions[1], 1, 0, &s->bar_regions[1], 1, 0x800, 0, errp)) {
        return;
    }

    /* Initialize registers after BAR allocation, done in reset. */
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
    .name = "fsl_enetc_vf_pci",
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
