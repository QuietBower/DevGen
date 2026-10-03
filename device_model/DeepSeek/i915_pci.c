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

#define TYPE_PCIBASE_DEVICE "i915_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x8086
/* DEVICE_ID: filled from first entry of INTEL_I830_IDS */
#define DEVICE_ID 0x3577
#define CLASS_ID 0x030000
#define GEN2_MMADR_BAR 1

// Begin register offset definitions from driver source
#define DEVEN                    0x54
#define DEVEN_MCHBAR_EN          (1 << 28)
#define GU_CNTL                  _MMIO(0x101010)
#define VLV_DISPLAY_BASE         0x180000
#define SWSCI                    0xe8
#define ASLE                     0xe4
#define ASLS                     0xfc
#define VLV_GTLC_MEDIA_CTX_EXISTS        (1 << 24)
#define VLV_GTLC_WAKE_CTRL           _MMIO(0x130090)
#define VLV_GTLC_RENDER_CTX_EXISTS       (1 << 25)
#define HSW_EDRAM_CAP               _MMIO(0x120010)
#define EDRAM_ENABLED           0x1
#define GEN2_ERROR_REGS      I915_ERROR_REGS(EMR, EIR)
#define GEN2_IRQ_REGS        I915_IRQ_REGS(GEN2_IMR, \
                          GEN2_IER, \
                          GEN2_IIR)
#define GEN8_PCU_IRQ_REGS        I915_IRQ_REGS(GEN8_PCU_IMR, \
                          GEN8_PCU_IER, \
                          GEN8_PCU_IIR)
#define VLV_MASTER_IER           _MMIO(0x4400c)
#define GMD_ID_MEDIA             _MMIO(MTL_MEDIA_GSI_BASE + 0xd8c)
#define GMD_ID_GRAPHICS             _MMIO(0xd8c)
#define MCHBAR_SIZE             (4 * 4096)
#define MCHBAR_I965              0x48
#define MCHBAR_I915              0x44
#define GEN10_MIRROR_FUSE3           _MMIO(0x9118)
#define XEHP_FUSE4              _MMIO(0x9114)
#define MTL_GT_L3_EXC_MASK           REG_GENMASK(5, 3)
#define MTL_GT_ACTIVITY_FACTOR           _MMIO(0x138010)
#define GT_L3_EXC_MASK          REG_GENMASK(6, 4)
#define GEN10_L3BANK_MASK           0x0F
#define GEN_DSS_PER_MSLICE  8
#define GEN12_MEML3_EN_MASK           REG_GENMASK(3, 0)
#define FORCEWAKE_MT               _MMIO(0xa188)
#define GEN2_IER    _MMIO(0x20a0)
#define GEN2_IIR   _MMIO(0x20a4)
#define GEN2_IMR   _MMIO(0x20a8)
#define GEN11_VCS2_VCS3_INTR_MASK       _MMIO(0x1900ac)
#define GEN11_CRYPTO_RSVD_INTR_MASK     _MMIO(0x1900f0)
#define GEN11_RCS0_RSVD_INTR_MASK       _MMIO(0x190090)
#define GEN12_CCS2_CCS3_INTR_MASK       _MMIO(0x190104)
#define GEN12_VCS6_VCS7_INTR_MASK       _MMIO(0x1900b4)
#define GEN11_GPM_WGBOXPERF_INTR_ENABLE     _MMIO(0x19003c)
#define GEN12_VECS2_VECS3_INTR_MASK     _MMIO(0x1900d4)
#define GEN12_CCS0_CCS1_INTR_MASK       _MMIO(0x190100)
#define GEN11_VECS0_VECS1_INTR_MASK     _MMIO(0x1900d0)
#define GEN11_CRYPTO_RSVD_INTR_ENABLE       _MMIO(0x190040)
#define GEN11_GUNIT_CSME_INTR_MASK      _MMIO(0x1900f4)
#define XEHPC_BCS7_BCS8_INTR_MASK        _MMIO(0x19011c)
#define CCS_MASK(gt) \
   ENGINE_INSTANCES_MASK(gt, CCS0, I915_MAX_CCS)
#define GEN12_CCS_RSVD_INTR_ENABLE      _MMIO(0x190048)
#define GEN11_VCS_VECS_INTR_ENABLE      _MMIO(0x190034)
#define XEHPC_BCS5_BCS6_INTR_MASK       _MMIO(0x190118)
#define GEN11_RENDER_COPY_INTR_ENABLE       _MMIO(0x190030)
#define GEN11_GUNIT_CSME_INTR_ENABLE        _MMIO(0x190044)
#define GEN11_VCS0_VCS1_INTR_MASK       _MMIO(0x1900a8)
#define GEN11_GUC_SG_INTR_ENABLE        _MMIO(0x190038)
#define GEN11_GPM_WGBOXPERF_INTR_MASK       _MMIO(0x1900ec)
#define GEN11_GUC_SG_INTR_MASK          _MMIO(0x1900e8)
#define XEHPC_BCS3_BCS4_INTR_MASK       _MMIO(0x190114)
#define GEN12_VCS4_VCS5_INTR_MASK       _MMIO(0x1900b0)
#define GEN11_BCS_RSVD_INTR_MASK        _MMIO(0x1900a0)
#define XEHPC_BCS1_BCS2_INTR_MASK       _MMIO(0x190110)
#define GEN8_PCU_IIR _MMIO(0x444e8)
#define GEN8_PCU_IER _MMIO(0x444ec)
#define GEN8_PCU_IMR _MMIO(0x444e4)
#define GT_IRQ_REGS      I915_IRQ_REGS(GTIMR, \
                          GTIER, \
                          GTIIR)
#define GEN6_PM_IRQ_REGS          I915_IRQ_REGS(GEN6_PMIMR, \
                          GEN6_PMIER, \
                          GEN6_PMIIR)
#define GEN8_GT_IRQ_REGS(which)      I915_IRQ_REGS(GEN8_GT_IMR(which), \
                          GEN8_GT_IER(which), \
                          GEN8_GT_IIR(which))
#define GMD_ID_ARCH_MASK            REG_GENMASK(31, 22)
#define GMD_ID_RELEASE_MASK         REG_GENMASK(21, 14)
#define GMD_ID_STEP                REG_GENMASK(5, 0)
#define XEHP_VEBOX4_RING_BASE        0x1f8000
#define GEN11_BSD2_RING_BASE    0x1c4000
#define XEHP_BSD7_RING_BASE    0x1f0000
#define XEHPC_BCS7_RING_BASE   0x3ec000
#define XEHPC_BCS8_RING_BASE   0x3ee000
#define GEN8_BSD2_RING_BASE    0x1c000
#define XEHPC_BCS4_RING_BASE   0x3e6000
#define XEHPC_BCS6_RING_BASE   0x3ea000
#define RENDER_RING_BASE      0x02000
#define GEN11_BSD4_RING_BASE   0x1d4000
#define XEHP_BSD6_RING_BASE   0x1e4000
#define VEBOX_RING_BASE        0x1a000
#define GEN12_COMPUTE0_RING_BASE    0x1a000
#define GEN11_VEBOX2_RING_BASE       0x1d8000
#define GEN6_BSD_RING_BASE    0x12000
#define MTL_GSC_RING_BASE     0x11a000
#define XEHP_BSD8_RING_BASE   0x1f4000
#define GEN11_BSD_RING_BASE   0x1c0000
#define XEHPC_BCS2_RING_BASE   0x3e2000
#define XEHP_BSD5_RING_BASE   0x1e0000
#define GEN12_COMPUTE2_RING_BASE    0x1e000
#define GEN11_VEBOX_RING_BASE       0x1c8000
#define BLT_RING_BASE       0x22000
#define GEN11_BSD3_RING_BASE   0x1d0000
#define OTHER_GSC_INSTANCE          6
#define GEN12_COMPUTE1_RING_BASE    0x1c000
#define XEHPC_BCS5_RING_BASE   0x3e8000
#define XEHP_VEBOX3_RING_BASE       0x1e8000
#define GEN12_COMPUTE3_RING_BASE    0x26000
#define XEHPC_BCS3_RING_BASE   0x3e4000
#define BSD_RING_BASE       0x04000
#define XEHPC_BCS1_RING_BASE   0x3e0000
#define I915_MAX_VECS  4
#define RCS_MASK(gt) \
   ENGINE_INSTANCES_MASK(gt, RCS0, I915_MAX_RCS)
#define CHV_FGT_DISABLE_SS1           REG_BIT(11)
#define CHV_FGT_EU_DIS_SS0_R1_MASK        REG_GENMASK(23, 20)
#define CHV_FGT_EU_DIS_SS1_R0_MASK        REG_GENMASK(27, 24)
#define CHV_FGT_DISABLE_SS0            REG_BIT(10)
#define CHV_FGT_EU_DIS_SS1_R1_MASK        REG_GENMASK(31, 28)
#define CHV_FGT_EU_DIS_SS0_R0_MASK        REG_GENMASK(19, 16)
#define CHV_FUSE_GT                _MMIO(VLV_GUNIT_BASE + 0x2168)
#define GEN9_EU_DISABLE(slice)           _MMIO(0x9134 + (slice) * 0x4)
#define GEN8_FUSE2              _MMIO(0x9120)
#define GEN9_F2_SS_DIS_MASK            REG_GENMASK(23, 20)
#define GEN8_F2_S_ENA_MASK          REG_GENMASK(27, 25)
#define HSW_F1_EU_DIS_8EUS           1
#define HSW_F1_EU_DIS_MASK          REG_GENMASK(17, 16)
#define HSW_F1_EU_DIS_10EUS          0
#define HSW_PAVP_FUSE1             _MMIO(0x911c)
#define HSW_F1_EU_DIS_6EUS           2
#define GEN11_EU_DIS_MASK           REG_GENMASK(7, 0)
#define GEN11_EU_DISABLE            _MMIO(0x9134)
#define GEN11_GT_S_ENA_MASK         REG_GENMASK(7, 0)
#define GEN11_GT_SLICE_ENABLE          _MMIO(0x9138)
#define GEN11_GT_SUBSLICE_DISABLE     _MMIO(0x913c)
#define GEN12_GT_GEOMETRY_DSS_ENABLE     _MMIO(0x913c)
#define GEN8_EU_DIS2_S2_MASK         REG_GENMASK(7, 0)
#define GEN8_EU_DIS0_S0_MASK         REG_GENMASK(23, 0)
#define GEN8_EU_DIS0_S1_MASK         REG_GENMASK(31, 24)
#define GEN8_EU_DIS1_S2_MASK         REG_GENMASK(31, 16)
#define GEN8_F2_SS_DIS_MASK          REG_GENMASK(23, 21)
#define GEN8_EU_DISABLE2            _MMIO(0x913c)
#define GEN8_EU_DISABLE1            _MMIO(0x9138)
#define GEN8_EU_DIS1_S1_MASK         REG_GENMASK(15, 0)
#define GEN8_EU_DISABLE0            _MMIO(0x9134)
#define GEN12_GT_COMPUTE_DSS_ENABLE      _MMIO(0x9144)
#define XEHP_EU_ENA_MASK            REG_GENMASK(7, 0)
#define XEHP_EU_ENABLE             _MMIO(0x9134)
#define XEHPC_GT_COMPUTE_DSS_ENABLE_EXT     _MMIO(0x9148)
#define FW_REG_READ  (1)
#define GT_FIFO_CTL_BLOCK_ALL_POLICY_STALL     (1 << 12)
#define GT_FIFO_CTL_RC6_POLICY_STALL        (1 << 11)
#define FORCEWAKE_RENDER_GEN9          _MMIO(0xa278)
#define FORCEWAKE_GT_GEN9           _MMIO(0xa188)
#define FORCEWAKE_VLV              _MMIO(0x1300b0)
#define FORCEWAKE_MEDIA_VDBOX_GEN11(n)      _MMIO(0xa540 + (n) * 4)
#define FORCEWAKE_ACK_GT_GEN9          _MMIO(0x130044)
#define FORCEWAKE_ACK_MEDIA_GEN9       _MMIO(0xd88)
#define FORCEWAKE_MT_ENABLE         (1 << 5)
#define FORCEWAKE_ACK_MEDIA_VDBOX_GEN11(n)  _MMIO(0xd50 + (n) * 4)
#define FORCEWAKE_ACK_VLV           _MMIO(0x1300b4)
#define FORCEWAKE_MEDIA_VEBOX_GEN11(n)      _MMIO(0xa560 + (n) * 4)
#define FORCEWAKE_ACK_HSW           _MMIO(0x130044)
#define FORCEWAKE_MT_ACK            _MMIO(0x130040)
#define FORCEWAKE_ACK_GT_MTL            _MMIO(0xdfc)
#define FORCEWAKE_MEDIA_GEN9            _MMIO(0xa270)
#define FORCEWAKE_MEDIA_VLV          _MMIO(0x1300b8)
#define FORCEWAKE               _MMIO(0xa18c)
#define FORCEWAKE_REQ_GSC            _MMIO(0xa618)
#define FORCEWAKE_ACK_MEDIA_VEBOX_GEN11(n)  _MMIO(0xd70 + (n) * 4)
#define FORCEWAKE_ACK_MEDIA_VLV           _MMIO(0x1300bc)
#define FORCEWAKE_ACK_RENDER_GEN9      _MMIO(0xd84)
#define FORCEWAKE_ACK_GSC           _MMIO(0xdf8)
#define FORCEWAKE_ACK              _MMIO(0x130090)
#define GEN6_RC_STATE              _MMIO(0xa094)
#define GEN6_PM_RP_UP_EI_EXPIRED       (1 << 2)
#define ARAT_EXPIRED_INTRMSK          (1 << 9)
#define GEN8_PMINTR_DISABLE_REDIRECT_TO_GUC  (1 << 31)
#define HSW_IDICR              _MMIO(0x9008)
#define IDIHASHMSK(x)              (((x) & 0x3f) << 16)
#define HSW_MI_PREDICATE_RESULT_2      _MMIO(0x2214)
#define GEN6_RC_CTL_RC6_ENABLE     (1 << 18)
#define RING_FAULT_GTTSEL_MASK      REG_BIT(11)
#define GEN6_RING_FAULT_REG_READ(engine__) \
   intel_uncore_read((engine__)->uncore, RING_FAULT_REG(engine__))
#define RING_FAULT_VALID           REG_BIT(0)
#define RING_FAULT_VADDR_MASK         REG_GENMASK(31, 12)
#define RING_FAULT_SRCID_MASK         REG_GENMASK(10, 3)
#define RING_FAULT_FAULT_TYPE_MASK     REG_GENMASK(2, 1)
#define XEHP_RING_FAULT_REG           MCR_REG(0xcec4)
#define XEHP_FAULT_TLB_DATA0          MCR_REG(0xceb8)
#define XEHP_FAULT_TLB_DATA1          MCR_REG(0xcebc)
#define GEN8_RING_FAULT_REG           _MMIO(0x4094)
#define GEN12_RING_FAULT_REG          _MMIO(0xcec4)
#define PGTBL_ER  _MMIO(0x02024)
#define IPEIR_I965             _MMIO(0x2064)
#define XELPMP_RING_FAULT_REG         _MMIO(0xcec4)
#define GEN12_FAULT_TLB_DATA1         _MMIO(0xcebc)
#define GEN12_FAULT_TLB_DATA0         _MMIO(0xceb8)
#define GEN8_FAULT_TLB_DATA0          _MMIO(0x4b10)
#define GEN8_FAULT_TLB_DATA1          _MMIO(0x4b14)
#define I915_MAX_CCS  4
#define MTL_PPAT_L4_CACHE_POLICY_MASK     REG_GENMASK(3, 2)
#define MTL_PAT_INDEX_COH_MODE_MASK   REG_GENMASK(1, 0)
#define MTL_MCR_SELECTOR          _MMIO(0xfd4)
#define GEN11_MCR_MULTICAST           REG_BIT(31)
#define MCR_REG(offset)   ((const i915_mcr_reg_t){ .reg = (offset) })
#define MTL_STEER_SEMAPHORE          _MMIO(0xfd0)
#define MTL_PCODE_STOLEN_ACCESS          _MMIO(0x138914)
#define STOLEN_ACCESS_ALLOWED           0x1
#define GMS_MASK           REG_GENMASK(15, 8)
#define GGMS_MASK          REG_GENMASK(7, 6)
#define GEN4_GMADR_BAR              2
#define GT_CS_MASTER_ERROR_INTERRUPT      REG_BIT(3)
#define GEN8_VECS_IRQ_SHIFT 0
#define GEN8_BCS_IRQ_SHIFT 16
#define GEN8_RCS_IRQ_SHIFT 0
#define GEN8_VCS1_IRQ_SHIFT 16
#define GT_CONTEXT_SWITCH_INTERRUPT       (1 <<  8)
#define GT_RENDER_USER_INTERRUPT       (1 <<  0)
#define GT_WAIT_SEMAPHORE_INTERRUPT       REG_BIT(11)
#define GEN8_VCS0_IRQ_SHIFT 0
#define GEN12_HECI2_RSVD_INTR_MASK        _MMIO(0x1900e4)
#define ENGINE1_MASK             REG_GENMASK(31, 16)
#define ENGINE0_MASK             REG_GENMASK(15, 0)
#define MTL_GUC_MGUC_INTR_MASK          _MMIO(0x1900e8)
#define GSC_IRQ_INTF(_x)  BIT(15 - (_x))
#define GT_PARITY_ERROR(dev_priv) \
   (GT_RENDER_L3_PARITY_ERROR_INTERRUPT | \
    (IS_HASWELL(dev_priv) ? GT_RENDER_L3_PARITY_ERROR_INTERRUPT_S1 : 0))
#define PM_VEBOX_USER_INTERRUPT            (1 << 10)
#define GT_BSD_USER_INTERRUPT           (1 << 12)
#define ILK_BSD_USER_INTERRUPT               (1 << 5)
#define GT_BLT_USER_INTERRUPT           (1 << 22)
#define DG1_MSTR_IRQ           REG_BIT(31)
#define I915_ERROR_MEMORY_REFRESH        (1 << 1)
#define I915_ERROR_PAGE_TABLE             (1 << 4)
#define GM45_ERROR_CP_PRIV                (1 << 3)
#define GM45_ERROR_MEM_PRIV               (1 << 4)
#define GM45_ERROR_PAGE_TABLE             (1 << 5)
#define CLAIM_ER_CTR_MASK    REG_GENMASK(15, 0)
#define CLAIM_ER      _MMIO(VLV_DISPLAY_BASE + 0x2028)
#define CLAIM_ER_CLR     REG_BIT(31)
#define CLAIM_ER_OVERFLOW    REG_BIT(16)
#define FPGA_DBG      _MMIO(0x42300)
#define FPGA_DBG_RM_NOCLAIM   REG_BIT(31)
#define CACHELINE_BYTES 64
#define GEN6_RP_CONTROL             _MMIO(0xa024)
#define MEMINT_EVAL_CHG_EN          (1 << 4)
#define MEMINT_EVAL_CHG          (1 << 4)
#define MEMINTRSTS              _MMIO(0x11184)
#define MEMINTREN              _MMIO(0x11180)
#define MEMSWCTL              _MMIO(0x11170)
#define MEMCTL_CMD_STS          (1 << 12)
#define GTIIR   _MMIO(0x44018)
#define GEN6_PMIIR             _MMIO(0x44028)
#define GEN8_GT_IER(which) _MMIO(0x4430c + (0x10 * (which)))
#define GEN8_GT_IMR(which) _MMIO(0x44304 + (0x10 * (which)))
#define GEN8_GT_IIR(which) _MMIO(0x44308 + (0x10 * (which)))
#define GC_DISPLAY_CLOCK_MASK           (7 << 4)
#define GCFGC                  0xf0
#define GC_DISPLAY_CLOCK_333_320_MHZ       (4 << 4)
#define GC_LOW_FREQUENCY_ENABLE        (1 << 7)
#define GC_DISPLAY_CLOCK_190_200_MHZ      (0 << 4)
#define GC_CLOCK_100_200           (1 << 0)
#define GC_CLOCK_133_266_2         (5 << 0)
#define GC_CLOCK_166_250           (7 << 0)
#define GC_CLOCK_166_266           (6 << 0)
#define GC_CLOCK_133_200_2          (4 << 0)
#define GC_CLOCK_CONTROL_MASK          (0x7 << 0)
#define GC_CLOCK_133_200          (0 << 0)
#define HPLLCC                 0xc0
#define GC_CLOCK_133_266           (3 << 0)
#define GC_CLOCK_100_133           (2 << 0)
#define GC_DISPLAY_CLOCK_167_MHZ_PNV        (7 << 4)
#define GC_DISPLAY_CLOCK_200_MHZ_PNV        (5 << 4)
#define GC_DISPLAY_CLOCK_444_MHZ_PNV        (2 << 4)
#define GC_DISPLAY_CLOCK_267_MHZ_PNV        (0 << 4)
#define GC_DISPLAY_CLOCK_133_MHZ_PNV        (6 << 4)
#define GC_DISPLAY_CLOCK_333_MHZ_PNV        (1 << 4)
#define HUC_LOAD_SUCCESSFUL       (1 << 0)
#define HECI1_FWSTS5_HUC_AUTH_DONE    (1 << 19)
#define MTL_GSC_HECI1_BASE    0x00116000
#define GEN11_HUC_KERNEL_LOAD_INFO    _MMIO(0xC1DC)
#define HECI_FWSTS(base, x) _MMIO((base) + _PICK(x, -(base), \
                        HECI_FWSTS1, \
                        HECI_FWSTS2, \
                        HECI_FWSTS3, \
                        HECI_FWSTS4, \
                        HECI_FWSTS5, \
                        HECI_FWSTS6))
#define HUC_FW_VERIFIED       (1<<7)
#define HUC_STATUS2             _MMIO(0xD3B0)
#define GEN11_SOFT_SCRATCH(n)       _MMIO(0x190240 + (n) * 4)
#define SOFT_SCRATCH(n)           _MMIO(0xc180 + (n) * 4)
#define SOFT_SCRATCH_COUNT        16
#define GEN11_SOFT_SCRATCH_COUNT  4
#define MEDIA_GUC_HOST_INTERRUPT    _MMIO(0x190304)
#define GEN11_GUC_HOST_INTERRUPT    _MMIO(0x1901f0)
#define GUC_SEND_INTERRUPT        _MMIO(0xc4c8)
#define MEDIA_SOFT_SCRATCH(n)        _MMIO(0x190310 + (n) * 4)
#define ENABLE_GUC_LOAD_HUC        BIT(1)
#define ENABLE_GUC_SUBMISSION      BIT(0)
#define CTC_SOURCE_PARAMETER_MASK     REG_BIT(0)
#define CTC_SOURCE_DIVIDE_LOGIC      REG_FIELD_PREP(CTC_SOURCE_PARAMETER_MASK, 1)
#define CTC_MODE             _MMIO(0xa26c)
#define RPM_CONFIG0               _MMIO(0xd00)
#define GEN10_RPM_CONFIG0_CTC_SHIFT_PARAMETER_MASK    REG_GENMASK(2, 1)
#define CTC_SHIFT_PARAMETER_MASK       REG_GENMASK(2, 1)
#define GEN11_GT_VDBOX_DISABLE_MASK     REG_GENMASK(7, 0)
#define GEN11_GT_VEBOX_VDBOX_DISABLE     _MMIO(0x9140)
#define VEBOX_MASK(gt) \
   ENGINE_INSTANCES_MASK(gt, VECS0, I915_MAX_VECS)
#define XEHP_SFC_ENABLE_MASK          REG_GENMASK(27, 24)
#define GEN11_GT_VEBOX_DISABLE_MASK     REG_GENMASK(19, 16)
#define I915_MAX_RCS  1
#define CXT_SIZE              _MMIO(0x21a0)
#define GEN7_CXT_SIZE             _MMIO(0x21a8)
#define GEN6_CXT_TOTAL_SIZE(cxt_reg)      (GEN6_CXT_RING_SIZE(cxt_reg) + \
                        GEN6_CXT_EXTENDED_SIZE(cxt_reg) + \
                        GEN6_CXT_PIPELINE_SIZE(cxt_reg))
#define GEN7_CXT_TOTAL_SIZE(ctx_reg)      (GEN7_CXT_EXTENDED_SIZE(ctx_reg) + \
                         GEN7_CXT_VFSTATE_SIZE(ctx_reg))
#define GEN11_GRDOM_MEDIA3            REG_BIT(7)
#define GEN11_GRDOM_MEDIA8            REG_BIT(12)
#define XEHPC_GRDOM_BLT3            REG_BIT(26)
#define GEN11_GRDOM_RENDER           GEN6_GRDOM_RENDER
#define GEN11_GRDOM_MEDIA6            REG_BIT(10)
#define XEHPC_GRDOM_BLT1            REG_BIT(24)
#define GEN6_GRDOM_BLT          (1 << 3)
#define GEN11_GRDOM_MEDIA5            REG_BIT(9)
#define GEN11_GRDOM_MEDIA2            REG_BIT(6)
#define XEHPC_GRDOM_BLT8            REG_BIT(31)
#define XEHPC_GRDOM_BLT4            REG_BIT(27)
#define GEN11_GRDOM_MEDIA            REG_BIT(5)
#define GEN11_GRDOM_MEDIA4            REG_BIT(8)
#define GEN6_GRDOM_VECS          (1 << 4)
#define GEN6_GRDOM_RENDER           (1 << 1)
#define GEN6_GRDOM_MEDIA           (1 << 2)
#define GEN8_GRDOM_MEDIA2           (1 << 7)
#define XEHPC_GRDOM_BLT7            REG_BIT(30)
#define GEN11_GRDOM_VECS2           REG_BIT(14)
#define GEN11_GRDOM_VECS3           REG_BIT(15)
#define GEN11_GRDOM_VECS4           REG_BIT(16)
#define GEN11_GRDOM_BLT          REG_BIT(2)
#define GEN11_GRDOM_VECS           REG_BIT(13)
#define XEHPC_GRDOM_BLT2            REG_BIT(25)
#define GEN12_GRDOM_GSC          REG_BIT(21)
#define XEHPC_GRDOM_BLT5            REG_BIT(28)
#define XEHPC_GRDOM_BLT6            REG_BIT(29)
#define GEN11_GRDOM_MEDIA7            REG_BIT(11)
#define VLV_GUNIT_BASE           0x180000
#define GTFIFODBG             _MMIO(0x120000)
#define GT_FIFO_FREE_ENTRIES_MASK       0x7f
#define GEN11_KCR               (19)
#define GEN12_VE_TLB_INV_CR           _MMIO(0xcee0)
#define XEHP_BLT_TLB_INV_CR           MCR_REG(0xcee4)
#define GEN12_BLT_TLB_INV_CR          _MMIO(0xcee4)
#define XEHP_VE_TLB_INV_CR           MCR_REG(0xcee0)
#define GEN8_BTCR              _MMIO(0x426c)
#define XEHP_COMPCTX_TLB_INV_CR           MCR_REG(0xcf04)
#define GEN8_VTCR              _MMIO(0x4270)
#define GEN8_M2TCR             _MMIO(0x4268)
#define XELPMP_GSC_TLB_INV_CR          _MMIO(0xcf04)
#define GEN8_RTCR              _MMIO(0x4260)
#define GEN12_COMPCTX_TLB_INV_CR        _MMIO(0xcf04)
#define GEN8_M1TCR             _MMIO(0x4264)
#define GEN12_VD_TLB_INV_CR            _MMIO(0xcedc)
#define GEN12_GFX_TLB_INV_CR           _MMIO(0xced8)
#define XEHP_GFX_TLB_INV_CR           MCR_REG(0xced8)
#define XEHP_VD_TLB_INV_CR           MCR_REG(0xcedc)
#define MEMMODE_FSTART_MASK           0x00000f00
#define MEMMODE_FMAX_MASK           0x000000f0
#define MEMMODECTL             _MMIO(0x11190)
#define MEMMODE_FMIN_MASK           0x0000000f
#define MEMMODE_FMAX_SHIFT          4
#define MEMMODE_FSTART_SHIFT          8
#define GEN6_GT_GFX_RC6             _MMIO(0x138108)
#define MTL_MEDIA_MC6             _MMIO(0x138048)
#define GEN6_GT_GFX_RC6_LOCKED          _MMIO(0x138104)
#define SRB3_BASE   (0x2130 - 0x30)
#define SRB1_BASE   (0x2110 - 0x30)
#define PRB1_BASE   (0x2040 - 0x30)
#define PRB2_BASE   (0x2050 - 0x30)
#define SRB2_BASE   (0x2120 - 0x30)
#define SRB0_BASE   (0x2100 - 0x30)
#define GEN6_RC_CTL_EI_MODE(x)      ((x) << 27)
#define GEN9_MEDIA_PG_IDLE_HYSTERESIS     _MMIO(0xa0c4)
#define GEN6_RC_EVALUATION_INTERVAL       _MMIO(0xa0a8)
#define GEN9_MEDIA_PG_ENABLE          REG_BIT(1)
#define GEN6_RC6_WAKE_RATE_LIMIT        _MMIO(0xa09c)
#define GEN9_RENDER_PG_IDLE_HYSTERESIS        _MMIO(0xa0c8)
#define GEN6_RC_SLEEP              _MMIO(0xa0b0)
#define GEN6_RC6_THRESHOLD          _MMIO(0xa0b8)
#define GEN6_RC_CTL_HW_ENABLE         (1 << 31)
#define GEN10_MEDIA_WAKE_RATE_LIMIT       _MMIO(0xa0a0)
#define GUC_MAX_IDLE_COUNT       _MMIO(0xC3E4)
#define GEN6_RC_IDLE_HYSTERSIS         _MMIO(0xa0ac)
#define VLV_COUNTER_CONTROL           _MMIO(0x138104)
#define VLV_RENDER_RC6_COUNT_EN       (1 << 0)
#define VLV_MEDIA_RC6_COUNT_EN       (1 << 1)
#define VLV_COUNT_RANGE_HIGH          (1 << 15)
#define GEN7_RC_CTL_TO_MODE          (1 << 28)
#define VLV_RENDER_RC0_COUNT_EN      (1 << 4)
#define VLV_MEDIA_RC0_COUNT_EN       (1 << 5)
#define VLV_RC_CTL_CTX_RST_PARALLEL      (1 << 24)
#define GEN8_RC6_CTX_INFO          _MMIO(0x8504)
#define GEN6_RC1_WAKE_RATE_LIMIT       _MMIO(0xa098)
#define VDN_MFX_POWERGATE_ENABLE(n)      REG_BIT(4 + 2 * (n))
#define GEN11_MEDIA_SAMPLER_PG_ENABLE      REG_BIT(2)
#define VDN_HCP_POWERGATE_ENABLE(n)      REG_BIT(3 + 2 * (n))
#define GEN6_PCODE_FREQ_IA_RATIO_SHIFT   8
#define GEN6_PCODE_FREQ_RING_RATIO_SHIFT  16
#define GEN6_RP_ENABLE          (1 << 7)
#define GEN6_RP_MEDIA_HW_NORMAL_MODE        (2 << 9)
#define GEN6_RP_IDLE_HYSTERSIS         _MMIO(0xa070)
#define GEN6_RP_DOWN_IDLE_AVG          (0x2 << 0)
#define GEN6_PM_RP_DOWN_THRESHOLD      (1 << 4)
#define GEN6_PM_RP_DOWN_TIMEOUT        (1 << 6)
#define GEN6_RP_DOWN_EI              _MMIO(0xa06c)
#define GEN6_RP_DOWN_THRESHOLD          _MMIO(0xa030)
#define GEN6_RP_UP_EI               _MMIO(0xa068)
#define GEN6_RP_UP_BUSY_AVG          (0x2 << 3)
#define GEN6_PM_RP_UP_THRESHOLD       (1 << 5)
#define GEN6_RP_MEDIA_IS_GFX           (1 << 8)
#define GEN6_RP_UP_THRESHOLD           _MMIO(0xa02c)
#define GEN6_RC_VIDEO_FREQ           _MMIO(0xa00c)
#define HSW_FREQUENCY(x)           ((x) << 24)
#define GEN6_RP_MEDIA_TURBO          (1 << 11)
#define GEN6_RP_DOWN_IDLE_CONT        (0x1 << 0)
#define GEN9_FREQUENCY(x)          ((x) << 23)
#define RCDNEI                 _MMIO(0x111b4)
#define RCBMAXAVG              _MMIO(0x1119c)
#define MCPPCE_EN              (1 << 0)
#define GFXEC                  _MMIO(0x112f4)
#define PXVFREQ_PX_MASK           0x7f000000
#define DMIEC                  _MMIO(0x112e4)
#define VIDSTART               _MMIO(0x111cc)
#define RCBMINAVG              _MMIO(0x111a0)
#define PXVFREQ(fstart)            _MMIO(0x11110 + (fstart) * 4)
#define DDREC                  _MMIO(0x112e8)
#define MEMIHYST               _MMIO(0x1117c)
#define MEMINT_CX_SUPR_EN          (1 << 7)
#define RCUPEI                 _MMIO(0x111b0)
#define CSIEC                  _MMIO(0x112e0)
#define PMMISC                 _MMIO(0x11214)
#define PXVFREQ_PX_SHIFT           24
#define MEMMODE_SWMODE_EN          (1 << 14)
#define BLEND_FILL_CACHING_OPT_DIS      REG_BIT(3)
#define XEHP_L3SCQREG7             MCR_REG(0xb188)
#define XEHP_SQCM              MCR_REG(0x8724)
#define EN_32B_ACCESS              REG_BIT(30)
#define FORCE_MISS_FTLB          REG_BIT(3)
#define XELPMP_VDBX_MOD_CTRL          _MMIO(0xcf34)
#define GEN12_STRICT_RAR_ENABLE      REG_BIT(23)
#define XELPMP_GSC_MOD_CTRL          _MMIO(0xcf30)
#define CG3DDISCFEG_CLKGATE_DIS      REG_BIT(17)
#define SARB_CHICKEN1              MCR_REG(0xe90c)
#define XEHP_LNESPARE              REG_BIT(19)
#define COMP_CKN_IN                REG_GENMASK(30, 29)
#define RENDER_MOD_CTRL             MCR_REG(0xcf2c)
#define COMP_MOD_CTRL             MCR_REG(0xcf30)
#define XEHP_L3NODEARBCFG           MCR_REG(0xb0b4)
#define DSS_ROUTER_CLKGATE_DIS      REG_BIT(28)
#define INVALIDATION_BROADCAST_MODE_DIS     REG_BIT(12)
#define GEN11_SUBSLICE_UNIT_LEVEL_CLKGATE   MCR_REG(0x9524)
#define XEHP_VEBX_MOD_CTRL           MCR_REG(0xcf38)
#define XEHP_VDBX_MOD_CTRL           MCR_REG(0xcf34)
#define GEN12_DOP_CLOCK_GATE_RENDER_ENABLE    REG_BIT(1)
#define XEHP_GAMCNTRL_CTRL           MCR_REG(0xcf54)
#define UNSLICE_UNIT_LEVEL_CLKGATE       _MMIO(0x9434)
#define GLOBAL_INVALIDATION_MODE        REG_BIT(2)
#define VLV_B0_WA_L3SQCREG1_VALUE      0x00D30000
#define GEN7_L3SQCREG4             _MMIO(0xb034)
#define GEN7_L3SQCREG1             _MMIO(0xb010)
#define L3SQ_URB_READ_CAM_MATCH_DISABLE   (1 << 27)
#define GAMT_ECO_ENABLE_IN_PLACE_DECOMPRESS   (1 << 18)
#define GEN8_EU_GAUNIT_CLOCK_GATE_DISABLE   (1 << 14)
#define GEN7_UCGCTL4                _MMIO(0x940c)
#define GEN9_GAMT_ECO_REG_RW_IA          _MMIO(0x4ab0)
#define CACHE_MODE_0              _MMIO(0x2120)
#define CM0_PIPELINED_RENDER_FLUSH_DISABLE   (1 << 8)
#define MMCD_PCLA               (1 << 31)
#define BDW_DISABLE_HDC_INVALIDATION      (1 << 25)
#define MMCD_MISC_CTRL              _MMIO(0x4ddc)
#define MMCD_HOTSPOT_EN          (1 << 27)
#define ECOCHK_DIS_TLB          (1 << 8)
#define GAMT_CHKN_DISABLE_DYNAMIC_CREDIT_SHARING   (1 << 28)
#define GAMT_CHKN_BIT_REG           _MMIO(0x4ab8)
#define HSW_SCRATCH1_L3_DATA_ATOMICS_DISABLE    (1 << 27)
#define HSW_SCRATCH1              _MMIO(0xb038)
#define GEN7_FF_VS_REF_CNT_FFME    (1 << 15)
#define GEN7_FF_THREAD_MODE        _MMIO(0x20a0)
#define HSW_ROW_CHICKEN3_L3_GLOBAL_ATOMICS_DISABLE   (1 << 6)
#define HSW_ROW_CHICKEN3            _MMIO(0xe49c)
#define GEN11_HASH_CTRL_BIT0          (1 << 0)
#define GEN11_SLICE_UNIT_LEVEL_CLKGATE     _MMIO(0x94d4)
#define DFR_DISABLE               (1 << 9)
#define GEN11_LSN_UNSLCVC_GAFS_HALF_CL2_MAXALLOC   (1 << 9)
#define GEN11_GACB_PERF_CTRL          _MMIO(0x4b80)
#define GEN11_HASH_CTRL_MASK          (0x3 << 12 | 0xf << 0)
#define UNSLICE_UNIT_LEVEL_CLKGATE2        _MMIO(0x94e4)
#define PSDUNIT_CLKGATE_DIS            REG_BIT(5)
#define VSUNIT_CLKGATE_DIS           REG_BIT(3)
#define GEN11_LSN_UNSLCVC_GAFS_HALF_SF_MAXALLOC    (1 << 7)
#define GEN8_GAMW_ECO_DEV_RW_IA          _MMIO(0x4080)
#define GEN11_HASH_CTRL_BIT4          (1 << 12)
#define HSUNIT_CLKGATE_DIS           REG_BIT(8)
#define L3_CLKGATE_DIS          REG_BIT(16)
#define L3_CR2X_CLKGATE_DIS            REG_BIT(17)
#define GAMW_ECO_DEV_CTX_RELOAD_DISABLE    (1 << 7)
#define GAMT_CHKN_DISABLE_L3_COH_PIPE       (1 << 31)
#define GEN11_LSN_UNSLCVC           _MMIO(0xb43c)
#define GWUNIT_CLKGATE_DIS           REG_BIT(16)
#define GEN10_DFR_RATIO_EN_AND_CHICKEN      MCR_REG(0x9550)
#define GEN7_L3CNTLREG1             _MMIO(0xb01c)
#define GEN7_L3_CHICKEN_MODE_REGISTER       _MMIO(0xb030)
#define GEN7_COMMON_SLICE_CHICKEN1        _MMIO(0x7010)
#define GEN7_CSC1_RHWO_OPT_DISABLE_IN_RCC  (1 << 10)
#define GEN7_WA_L3_CHICKEN_MODE        0x20000000
#define GEN7_WA_FOR_GEN7_L3_CONTROL        0x3C47FF8C
#define SUBSLICE_UNIT_LEVEL_CLKGATE2       MCR_REG(0x9528)
#define VSUNIT_CLKGATE_DIS_TGL      REG_BIT(19)
#define CPSSUNIT_CLKGATE_DIS          REG_BIT(9)
#define RC_OP_FLUSH_ENABLE          (1 << 0)
#define HECI1_FWSTS1_INIT_COMPLETE         REG_BIT(9)
#define RING_FAULT_REG(engine)          _MMIO(_PICK_EVEN((engine)->class, \
                                 _RING_FAULT_REG_RCS, \
                                 _RING_FAULT_REG_VCS))
#define RING_FAULT_ENGINE_ID_MASK        REG_GENMASK(16, 12)
#define FAULT_VA_HIGH_BITS            REG_GENMASK(3, 0)
#define FAULT_GTT_SEL              REG_BIT(4)
#define GEN6_RING_FAULT_REG_RMW(engine__, clear__, set__) \
({ \
   u32 __val; \
\
   __val = intel_uncore_read((engine__)->uncore, \
                 RING_FAULT_REG(engine__)); \
   __val &= ~(clear__); \
   __val |= (set__); \
   intel_uncore_write((engine__)->uncore, RING_FAULT_REG(engine__), \
              __val); \
})
#define GEN6_RING_FAULT_REG_POSTING_READ(engine__) \
   intel_uncore_posting_read((engine__)->uncore, RING_FAULT_REG(engine__))
#define I830_FENCE_TILING_Y_SHIFT    12
#define I830_FENCE_PITCH_SHIFT   4
#define FENCE_REG(i)          _MMIO(0x2000 + (((i) & 8) << 9) + ((i) & 7) * 4)
#define I830_FENCE_REG_VALID     (1 << 0)
#define I830_FENCE_SIZE_BITS(size)    ((ffs((size) >> 19) - 1) << 8)
#define I915_FENCE_SIZE_BITS(size)    ((ffs((size) >> 20) - 1) << 8)
#define FENCE_REG_965_LO(i)       _MMIO(0x03000 + (i) * 8)
#define FENCE_REG_GEN6_LO(i)     _MMIO(0x100000 + (i) * 8)
#define I965_FENCE_PITCH_SHIFT   2
#define GEN6_FENCE_PITCH_SHIFT   32
#define I965_FENCE_REG_VALID        (1 << 0)
#define FENCE_REG_GEN6_HI(i)     _MMIO(0x100000 + (i) * 8 + 4)
#define I965_FENCE_TILING_Y_SHIFT   1
#define FENCE_REG_965_HI(i)       _MMIO(0x03000 + (i) * 8 + 4)
#define GT0_PERF_LIMIT_REASONS        _MMIO(0x1381a8)
#define MTL_MEDIA_PERF_LIMIT_REASONS  _MMIO(0x138030)
#define DG2_GSC_HECI2_BASE   0x00374000
#define DG2_GSC_HECI1_BASE   0x00373000
#define DG1_GSC_HECI2_BASE   0x00259000
#define GEN8_L3_LRA_1_GPGPU         _MMIO(0x4dd4)
#define GEN8_PCU_ISR _MMIO(0x444e0)
#define GEN6_MBCUNIT_SNPCR          _MMIO(0x900c)
#define HSW_GTT_CACHE_EN    _MMIO(0x4024)
#define GEN8_GT_ISR(which) _MMIO(0x44300 + (0x10 * (which)))
#define GEN8_UCGCTL6             _MMIO(0x9430)
#define GEN7_SC_INSTDONE          _MMIO(0x7100)
#define HALF_SLICE_CHICKEN2           MCR_REG(0xe180)
#define HDC_CHICKEN0              _MMIO(0x7300)
#define GEN9_SCRATCH_LNCF1          _MMIO(0xb008)
#define GEN8_L3SQCREG4             MCR_REG(0xb118)
#define GEN8_PUSHBUS_ENABLE          _MMIO(0xa250)
#define GEN7_SAMPLER_INSTDONE          _MMIO(0xe160)
#define DONE_REG               _MMIO(0x40b0)
#define RC6_CTX_BASE              _MMIO(0xd48)
#define GEN9_CTX_PREEMPT_REG           _MMIO(0x2248)
#define GEN8_L3SQCREG1             MCR_REG(0xb100)
#define ERROR_GEN6             _MMIO(0x40a0)
#define GEN8_L3CNTLREG             _MMIO(0x7034)
#define GEN8_PUSHBUS_SHIFT          _MMIO(0xa25c)
#define GEN8_PUSHBUS_CONTROL          _MMIO(0xa248)
#define BXT_RP_STATE_CAP        _MMIO(0x138170)
#define GEN7_ROW_INSTDONE          _MMIO(0xe164)
#define RC6_LOCATION              _MMIO(0xd40)
#define GEN8_HDC_CHICKEN1          _MMIO(0x7304)
#define GEN7_L3CNTLREG2             _MMIO(0xb020)
#define GEN9_CS_DEBUG_MODE1           _MMIO(0x20ec)
#define BDW_SCRATCH1              MCR_REG(0xb11c)
#define DMA_CTRL          _MMIO(0xc314)
#define GEN7_FF_SLICE_CS_CHICKEN1      _MMIO(0x20e0)
#define GEN8_GARBCNTL              _MMIO(0xb004)
#define GEN6_STOLEN_RESERVED       _MMIO(0x1082C0)
#define GEN9_GFX_MOCS(i)          _MMIO(__GEN9_RCS0_MOCS0 + (i) * 4)
#define GEN9_WM_CHICKEN3          _MMIO(0x5588)
#define HSW_HALF_SLICE_CHICKEN3           _MMIO(0xe184)
#define GEN6_PMINTRMSK             _MMIO(0xa168)
#define ILK_DISPLAY_CHICKEN1    _MMIO(0x42000)
#define GEN6_RPNSWREQ             _MMIO(0xa008)
#define COMMON_SLICE_CHICKEN2           _MMIO(0x7014)
#define ILK_DSPCLK_GATE_D   _MMIO(0x42020)
#define GEN6_RP_PREV_UP             _MMIO(0xa058)
#define GEN6_RP_CUR_UP_EI           _MMIO(0xa050)
#define GS_INVOCATION_COUNT           _MMIO(0x2328)
#define VEBOX_HWS_PGA_GEN7          _MMIO(0x4380)
#define CACHE_MODE_0_GEN7          _MMIO(0x7000)
#define CL_PRIMITIVES_COUNT           _MMIO(0x2340)
#define HS_INVOCATION_COUNT           _MMIO(0x2300)
#define CACHE_MODE_1              _MMIO(0x7004)
#define GEN6_GT_THREAD_STATUS_REG       _MMIO(0x13805c)
#define GEN6_RP_PREV_DOWN          _MMIO(0xa064)
#define BCS_SWCTRL             _MMIO(0x22200)
#define GEN6_RPSTAT1             _MMIO(0xa01c)
#define GEN6_RP_CUR_DOWN_EI          _MMIO(0xa05c)
#define CL_INVOCATION_COUNT           _MMIO(0x2338)
#define GFX_FLSH_CNTL_GEN6          _MMIO(0x101008)
#define GEN6_GT_CORE_STATUS           _MMIO(0x138060)
#define GEN6_RC_CONTROL             _MMIO(0xa090)
#define GEN6_RP_CUR_DOWN           _MMIO(0xa060)
#define FF_SLICE_CS_CHICKEN2          _MMIO(0x20e4)
#define RSTDBYCTL             _MMIO(0x111b8)
#define ILK_DISPLAY_CHICKEN2    _MMIO(0x42004)
#define DS_INVOCATION_COUNT           _MMIO(0x2308)
#define GUC_STATUS          _MMIO(0xc000)
#define IA_PRIMITIVES_COUNT           _MMIO(0x2318)
#define IA_VERTICES_COUNT          _MMIO(0x2310)
#define VS_INVOCATION_COUNT           _MMIO(0x2320)
#define GEN6_UCGCTL2              _MMIO(0x9404)
#define GEN6_PCODE_DATA             _MMIO(0x138128)
#define GEN6_RP_INTERRUPT_LIMITS        _MMIO(0xa014)
#define PS_DEPTH_COUNT             _MMIO(0x2350)
#define GAC_ECO_BITS              _MMIO(0x14090)
#define PS_INVOCATION_COUNT           _MMIO(0x2348)
#define GEN6_GDRST             _MMIO(0x941c)
#define GS_PRIMITIVES_COUNT           _MMIO(0x2330)
#define GAB_CTL                _MMIO(0x24000)
#define GEN7_HALF_SLICE_CHICKEN1      _MMIO(0xe100)
#define GEN6_RP_CUR_UP             _MMIO(0xa054)
#define GEN7_GT_MODE              _MMIO(0x7008)
#define GEN2_ISR  _MMIO(0x20ac)
#define DSPCLK_GATE_D          _MMIO(0x6200)
#define XEHP_CCS_BASE_SHIFT           8
#define GEN6_GSMBASE          _MMIO(0x108100)
#define XEHP_FLAT_CCS_BASE_ADDR          MCR_REG(0x4910)
#define GEN_DSS_PER_GSLICE  4
#define GEN8_MCR_SELECTOR           _MMIO(0xfdc)
#define GEN8_PAGE_PRESENT        BIT_ULL(0)
#define MTL_GGTT_PTE_PAT1        BIT_ULL(53)
#define MTL_GGTT_PTE_PAT0        BIT_ULL(52)
#define GEN12_GGTT_PTE_LM        BIT_ULL(1)
#define GEN12_GGTT_PTE_ADDR_MASK     GENMASK_ULL(45, 12)
#define GFX_FLSH_CNTL_EN         (1 << 0)
#define GEN12_GUC_TLB_INV_CR_INVALIDATE    (1 << 0)
#define GEN12_GUC_TLB_INV_CR      _MMIO(0xcee8)
#define GEN8_GTCR_INVALIDATE       (1<<0)
#define GEN8_GTCR          _MMIO(0x4274)
#define GEN6_PTE_VALID          REG_BIT(0)
#define GEN6_PTE_UNCACHED       (1 << 1)
#define GEN6_PTE_CACHE_LLC       (2 << 1)
#define GEN6_PTE_ADDR_ENCODE(addr)      GEN6_GTT_ADDR_ENCODE(addr)
#define GEN7_PTE_CACHE_L3_LLC        (3 << 1)
#define HSW_WB_ELLC_LLC_AGE3        HSW_CACHEABILITY_CONTROL(0x8)
#define HSW_PTE_ADDR_ENCODE(addr)    HSW_GTT_ADDR_ENCODE(addr)
#define HSW_WT_ELLC_LLC_AGE3        HSW_CACHEABILITY_CONTROL(0x7)
#define BYT_PTE_SNOOPED_BY_CPU_CACHES      REG_BIT(2)
#define BYT_PTE_WRITEABLE       REG_BIT(1)
#define HSW_WB_LLC_AGE3         HSW_CACHEABILITY_CONTROL(0x2)
#define GT_RENDER_L3_PARITY_ERROR_INTERRUPT_S1     (1 << 11)
#define GT_RENDER_L3_PARITY_ERROR_INTERRUPT        (1 <<  5)
#define FORCEWAKE_KERNEL          BIT(0)
#define GEN6_PM_RPS_EVENTS           (GEN6_PM_RP_UP_EI_EXPIRED   | \
                         GEN6_PM_RP_UP_THRESHOLD    | \
                         GEN6_PM_RP_DOWN_EI_EXPIRED | \
                         GEN6_PM_RP_DOWN_THRESHOLD  | \
                         GEN6_PM_RP_DOWN_TIMEOUT)
#define HECI_FWSTS1              0xc40
#define HECI_FWSTS3              0xc60
#define HECI_FWSTS4              0xc64
#define HECI_FWSTS2              0xc48
#define HECI_FWSTS6              0xc6c
#define HECI_FWSTS5              0xc68
#define GEN11_RPM_CONFIG0_CRYSTAL_CLOCK_FREQ_MASK     REG_GENMASK(5, 3)
#define GEN11_RPM_CONFIG0_CRYSTAL_CLOCK_FREQ_25_MHZ   REG_FIELD_PREP(GEN11_RPM_CONFIG0_CRYSTAL_CLOCK_FREQ_MASK, 3)
#define GEN11_RPM_CONFIG0_CRYSTAL_CLOCK_FREQ_24_MHZ    REG_FIELD_PREP(GEN11_RPM_CONFIG0_CRYSTAL_CLOCK_FREQ_MASK, 0)
#define GEN11_RPM_CONFIG0_CRYSTAL_CLOCK_FREQ_38_4_MHZ   REG_FIELD_PREP(GEN11_RPM_CONFIG0_CRYSTAL_CLOCK_FREQ_MASK, 2)
#define GEN11_RPM_CONFIG0_CRYSTAL_CLOCK_FREQ_19_2_MHZ   REG_FIELD_PREP(GEN11_RPM_CONFIG0_CRYSTAL_CLOCK_FREQ_MASK, 1)
#define GEN9_TIMESTAMP_OVERRIDE_US_COUNTER_DIVIDER_MASK  0x3ff
#define GEN9_TIMESTAMP_OVERRIDE_US_COUNTER_DIVIDER_SHIFT  0
#define GEN9_TIMESTAMP_OVERRIDE_US_COUNTER_DENOMINATOR_MASK  (0xf << 12)
#define GEN9_TIMESTAMP_OVERRIDE_US_COUNTER_DENOMINATOR_SHIFT  12
#define GEN9_TIMESTAMP_OVERRIDE             _MMIO(0x44074)
#define GEN6_CXT_EXTENDED_SIZE(cxt_reg)   (((cxt_reg) >> 6) & 0x3f)
#define GEN6_CXT_RING_SIZE(cxt_reg)       (((cxt_reg) >> 18) & 0x3f)
#define GEN6_CXT_PIPELINE_SIZE(cxt_reg)   (((cxt_reg) >> 0) & 0x3f)
#define GEN7_CXT_VFSTATE_SIZE(ctx_reg)    (((ctx_reg) >> 0) & 0x3f)
#define GEN7_CXT_EXTENDED_SIZE(ctx_reg)   (((ctx_reg) >> 9) & 0x7f)
#define GUC_RENDER_CLASS        0
#define GUC_COMPUTE_CLASS        4
#define GUC_GSC_OTHER_CLASS       5
#define GUC_VIDEO_CLASS           1
#define GUC_BLITTER_CLASS         3
#define GUC_VIDEOENHANCE_CLASS      2
#define GEN11_GT_INTR_DW(x)          _MMIO(0x190018 + ((x) * 4))
#define XEHP_COMMON_SLICE_CHICKEN3      MCR_REG(0x7304)
#define GEN10_SAMPLER_MODE           MCR_REG(0xe18c)
#define GEN9_SLICE_COMMON_ECO_CHICKEN1      _MMIO(0x731c)
#define GEN9_HALF_SLICE_CHICKEN7        MCR_REG(0xe194)
#define HIZ_CHICKEN             _MMIO(0x7018)
#define GEN11_COMMON_SLICE_CHICKEN3        _MMIO(0x7304)
#define PWRCTX_MAXCNT_GSCCS     _MMIO(0x11a054)
#define RC_PSMI_CTRL_GSCCS     _MMIO(0x11a050)
#define IDLE_MSG_DISABLE        REG_BIT(0)
#define RC_SW_TARGET_STATE_SHIFT      16
#define RC_SW_TARGET_STATE_MASK        (7 << RC_SW_TARGET_STATE_SHIFT)
#define RC6_CTX_BASE_MASK           0xFFFFFFF0
#define RC6_CTX_IN_DRAM          (1 << 0)
#define GEN8_MISC_CTRL0             _MMIO(0xa180)
#define ECOCHK_PPGTT_WB_HSW           (0x3 << 3)
#define ECOCHK_PPGTT_LLC_IVB          (0x1 << 3)
#define ECOCHK_PPGTT_GFDT_IVB         (0x1 << 4)
#define ECOBITS_PPGTT_CACHE64B     (3 << 8)
#define ECOBITS_SNB_BIT          (1 << 13)
#define GFX_MODE              _MMIO(0x2520)
#define GAB_CTL_CONT_AFTER_PAGEFAULT       (1 << 8)
#define ECOCHK_PPGTT_CACHE64B          (0x3 << 3)
#define ECOCHK_SNB_BIT          (1 << 10)
#define GEN8_L3_LRA_1_GPGPU_DEFAULT_VALUE_BDW   0x67F1427F
#define GTT_CACHE_EN_ALL    0xF0007FFF
#define GAMW_ECO_ENABLE_64K_IPS_FIELD       0xF
#define GEN8_L3_LRA_1_GPGPU_DEFAULT_VALUE_CHV   0x5FF101FF
#define GEN9_L3_LRA_1_GPGPU_DEFAULT_VALUE_BXT   0x5FF101FF
#define GEN9_L3_LRA_1_GPGPU_DEFAULT_VALUE_SKL   0x67F1427F
#define GEN12_GLOBAL_MOCS(i)          _MMIO(0x4000 + (i) * 4)
#define XEHP_LNCFCMOCS(i)            MCR_REG(0xb020 + (i) * 4)
#define GEN9_LNCFCMOCS(i)          _MMIO(0xb020 + (i) * 4)
#define ECR                  _MMIO(0x11600)
#define PXWL(i)                 _MMIO(0x11680 + (i) * 8)
#define EG1                  _MMIO(0x11614)
#define SDEW                  _MMIO(0x1124c)
#define EG6                  _MMIO(0x11628)
#define LCFUSE02               _MMIO(0x116c0)
#define CSIEW0                 _MMIO(0x11250)
#define EG4                  _MMIO(0x11620)
#define EG2                  _MMIO(0x11618)
#define OGW0                  _MMIO(0x11608)
#define EG7                  _MMIO(0x1162c)
#define CSIEW1                 _MMIO(0x11254)
#define EG5                  _MMIO(0x11624)
#define LCFUSE_HIV_MASK           0x000000ff
#define PEW(i)                 _MMIO(0x1125c + (i) * 4)
#define OGW1                  _MMIO(0x1160c)
#define EG3                  _MMIO(0x1161c)
#define DEW(i)                 _MMIO(0x11270 + (i) * 4)
#define EG0                  _MMIO(0x11610)
#define CSIEW2                 _MMIO(0x11258)
#define PXW(i)                 _MMIO(0x11664 + (i) * 4)
#define SF_MCR_SELECTOR             _MMIO(0xfd8)
#define GAM_MCR_SELECTOR          _MMIO(0xfe0)
#define MCFG_MCR_SELECTOR         _MMIO(0xfd0)
#define GEN12_MAX_MSLICES          4
#define GEN8_MCR_SUBSLICE_MASK      GEN8_MCR_SUBSLICE(3)
#define GEN8_MCR_SLICE(slice)         (((slice) & 3) << 26)
#define GEN8_MCR_SLICE_MASK         GEN8_MCR_SLICE(3)
#define GEN8_MCR_SUBSLICE(subslice)      (((subslice) & 3) << 24)
#define PM_VEBOX_CS_ERROR_INTERRUPT      (1 << 12)
#define GT_BSD_CS_ERROR_INTERRUPT       (1 << 15)
#define GT_BLT_CS_ERROR_INTERRUPT       (1 << 25)
#define OVRUNIT_CLOCK_GATE_DISABLE      (1 << 3)
#define I830_L2_CACHE_CLOCK_GATE_DISABLE   (1 << 2)
#define I830_CLOCK_GATE              0xc8
#define GEN11_MCR_SLICE_MASK         GEN11_MCR_SLICE(0xf)
#define MTL_MCR_GROUPID          REG_GENMASK(11, 8)
#define GEN11_MCR_SLICE(slice)        (((slice) & 0xf) << 27)
#define GEN11_MCR_SUBSLICE(subslice)      (((subslice) & 0x7) << 24)
#define MTL_MCR_INSTANCEID           REG_GENMASK(3, 0)
#define GEN11_MCR_SUBSLICE_MASK      GEN11_MCR_SUBSLICE(0x7)
#define GEN6_GTT_ADDR_ENCODE(addr)    ((addr) | (((addr) >> 28) & 0xff0))
#define HSW_CACHEABILITY_CONTROL(bits)    ((((bits) & 0x7) << 1) | \
                         (((bits) & 0x8) << (11 - 3)))
#define HSW_GTT_ADDR_ENCODE(addr)    ((addr) | (((addr) >> 28) & 0x7f0))
#define I915_GTT_PAGE_MASK -I915_GTT_PAGE_SIZE
#define GEN6_PM_RP_DOWN_EI_EXPIRED      (1 << 1)
#define GUC_SEND_TRIGGER       (1<<0)
#define DMA_GUC_WOPCM_OFFSET       _MMIO(0xc340)
#define GUC_WOPCM_SIZE          _MMIO(0xc050)
#define GUC_WOPCM_OFFSET_VALID    (1<<0)
#define GUC_WOPCM_SIZE_LOCKED       (1<<0)
#define HUC_UKERNEL            (1<<9)
#define GEN9_IGNORE_SLICE_RATIO        (0 << 0)
#define GEN9_SW_REQ_UNSLICE_RATIO_SHIFT   23
#define GUC_WOPCM_OFFSET_MASK        (0x3ffff << GUC_WOPCM_OFFSET_SHIFT)
#define HUC_LOADING_AGENT_GUC        (1<<1)
#define UOS_MOVE              (1<<4)
#define GEN11_INTR_IDENTITY_REG(x)        _MMIO(0x190060 + ((x) * 4))
#define GEN11_IIR_REG_SELECTOR(x)      _MMIO(0x190070 + ((x) * 4))
#define GEN11_INTR_DATA_VALID          (1 << 31)
#define GEN11_ENABLE_32_PLANE_MODE      (1 << 7)
#define SC_DISABLE_POWER_OPTIMIZATION_EBB    REG_BIT(9)
#define HIZ_RAW_STALL_OPT_DISABLE       (1 << 2)
#define GEN7_FF_DS_SCHED_HW      (0x0 << 4)
#define GEN12_FF_TESSELATION_DOP_GATE_DISABLE BIT(19)
#define GEN12_DISABLE_READ_SUPPRESSION    REG_BIT(15)
#define FF_DOP_CLOCK_GATE_DISABLE      REG_BIT(1)
#define HSW_SAMPLE_C_PERFORMANCE        (1 << 9)
#define GEN9_LNCF_NONIA_COHERENT_ATOMICS_ENABLE    REG_BIT(0)
#define GEN7_FF_VS_SCHED_HW        (0x0 << 12)
#define GEN11_HASH_CTRL_EXCL_MASK        REG_GENMASK(6, 0)
#define GEN7_PSD_SINGLE_PORT_DISPATCH_ENABLE   (1 << 3)
#define EVICTION_PERF_FIX_ENABLE        REG_BIT(8)
#define GEN9_CSFE_CHICKEN1_RCS          _MMIO(0x20d4)
#define GEN7_FF_SCHED_MASK        0x0077070
#define GEN9_SCRATCH1             MCR_REG(0xb11c)
#define GEN9_POOLED_EU_LOAD_BALANCING_FIX_DISABLE    (1 << 10)
#define GEN8_ROW_CHICKEN2           MCR_REG(0xe4f4)
#define DIS_ATOMIC_CHAINING_TYPED_WRITES   REG_BIT(3)
#define GEN9_PREEMPT_GPGPU_SYNC_SWITCH_DISABLE    (1 << 2)
#define GEN11_COHERENT_PARTIAL_WRITE_MERGE_ENABLE   (1 << 19)
#define GEN8_LQSC_FLUSH_COHERENT_LINES   (1 << 21)
#define GEN11_BANK_HASH_ADDR_EXCL_BIT0   (1 << 5)
#define L3_HIGH_PRIO_CREDITS(x)      (((x) >> 1) << 14)
#define GEN7_DISABLE_SAMPLER_PREFETCH      (1 << 30)
#define GEN11_SCRATCH2             MCR_REG(0xb140)
#define GEN9_LBS_SLA_RETRY_TIMER_DECREMENT_ENABLE   (1 << 2)
#define GEN10_CACHE_MODE_SS           MCR_REG(0xe420)
#define GEN9_GAPS_TSV_CREDIT_DISABLE      REG_BIT(7)
#define CM0_STC_EVICT_DISABLE_LRA_SNB      (1 << 5)
#define GEN11_GLBLINVL             _MMIO(0xb404)
#define GEN11_LQSC_CLEAN_EVICT_DISABLE   (1 << 6)
#define GEN11_ARBITRATION_PRIO_ORDER_MASK    REG_GENMASK(27, 22)
#define GEN8_LQSQ_NONIA_COHERENT_ATOMICS_ENABLE   REG_BIT(22)
#define LSC_L1_FLUSH_CTL_3D_DATAPORT_FLUSH_EVENTS_MASK    REG_GENMASK(13, 11)
#define GEN9_FFSC_PERCTX_PREEMPT_CTRL      (1 << 14)
#define GEN7_SARCHKMD             _MMIO(0xb000)
#define GEN6_WIZ_HASHING_MASK          GEN6_WIZ_HASHING(1, 1)
#define GEN12_DISABLE_TDL_PUSH       REG_BIT(9)
#define GEN11_BANK_HASH_ADDR_EXCL_MASK   (0x7f << 5)
#define GEN7_FF_TS_SCHED_HW        (0x0 << 16)
#define XEHP_HDC_CHICKEN0          MCR_REG(0xe5f0)
#define GEN9_ROW_CHICKEN4          MCR_REG(0xe48c)
#define L3_PRIO_CREDITS_MASK          ((0x1f << 19) | (0x1f << 14))
#define GEN7_MAX_PS_THREAD_DEP       (8 << 12)
#define L3_GENERAL_PRIO_CREDITS(x)      (((x) >> 1) << 19)
#define ENABLE_EU_COUNT_FOR_TDL_FLUSH      REG_BIT(10)
#define GEN11_HASH_CTRL_EXCL_BIT0      REG_FIELD_PREP(GEN11_HASH_CTRL_EXCL_MASK, 0x1)
#define GEN12_PUSH_CONST_DEREF_HOLD_DIS    REG_BIT(8)
#define GEN6_GT_MODE              _MMIO(0x20d0)
#define ENABLE_SMALLPL          REG_BIT(15)
#define GEN12_DISABLE_EARLY_READ      REG_BIT(14)
#define GEN11_INDIRECT_STATE_BASE_ADDR_OVERRIDE    REG_BIT(0)
#define MAXREQS_PER_BANK          REG_GENMASK(39 - 32, 37 - 32)
#define GEN9_ROW_CHICKEN3          MCR_REG(0xe49c)
#define DISABLE_PREFETCH_INTO_IC        REG_BIT(3)
#define DIS_CHAIN_2XSIMD8          REG_BIT(55 - 32)
#define XEHP_DIS_BBL_SYSPIPE          REG_BIT(11)
#define LSC_CHICKEN_BIT_0_UDW          MCR_REG(0xe7c8 + 4)
#define POLYGON_TRIFAN_LINELOOP_DISABLE    REG_BIT(4)
#define XELPG_DISABLE_TDL_SVHS_GATING      REG_BIT(1)
#define LSC_CHICKEN_BIT_0          MCR_REG(0xe7c8)
#define DISABLE_D8_D16_COASLESCE       REG_BIT(30)
#define VFG_PREEMPTION_CHICKEN          _MMIO(0x83b4)
#define MTL_DISABLE_SAMPLER_SC_OOO      REG_BIT(3)
#define UGM_FRAGMENT_THRESHOLD_TO_3      REG_BIT(58 - 32)
#define DISABLE_128B_EVICTION_COMMAND_UDW    REG_BIT(36 - 32)
#define ENABLE_PREFETCH_INTO_IC        REG_BIT(3)
#define FORCE_1_SUB_MESSAGE_PER_FRAGMENT    REG_BIT(15)
#define MTL_DISABLE_FIX_FOR_EOT_FLUSH      REG_BIT(9)
#define XEHP_RCU_MODE_FIXED_SLICE_CCS_MODE    REG_BIT(1)
#define XEHP_CCS_MODE             _MMIO(0x14804)
#define GEN12_RCU_MODE            _MMIO(0x14800)
#define GEN8_CS_CHICKEN1          _MMIO(0x2580)
#define GEN7_3DPRIM_END_OFFSET          _MMIO(0x2420)
#define GPGPU_THREADS_DISPATCHED        _MMIO(0x2290)
#define GEN7_GPGPU_DISPATCHDIMX          _MMIO(0x2500)
#define GEN7_3DPRIM_VERTEX_COUNT        _MMIO(0x2434)
#define GEN7_3DPRIM_START_INSTANCE        _MMIO(0x243c)
#define GEN7_L3CNTLREG3             _MMIO(0xb024)
#define GEN7_SO_NUM_PRIMS_WRITTEN(n)        _MMIO(0x5200 + (n) * 8)
#define GEN7_SO_PRIM_STORAGE_NEEDED(n)      _MMIO(0x5240 + (n) * 8)
#define GEN7_3DPRIM_START_VERTEX        _MMIO(0x2430)
#define GEN7_GPGPU_DISPATCHDIMY          _MMIO(0x2504)
#define GEN7_GPGPU_DISPATCHDIMZ          _MMIO(0x2508)
#define GEN7_3DPRIM_INSTANCE_COUNT       _MMIO(0x2438)
#define GEN7_3DPRIM_BASE_VERTEX          _MMIO(0x2440)
#define GEN7_SO_WRITE_OFFSET(n)        _MMIO(0x5280 + (n) * 4)
#define MTL_MPE_FREQUENCY      _MMIO(0x13802c)
#define MTL_RPE_MASK        REG_GENMASK(8, 0)
#define MTL_RP0_CAP_MASK    REG_GENMASK(8, 0)
#define MTL_RPN_CAP_MASK    REG_GENMASK(24, 16)
#define MTL_MEDIAP_STATE_CAP     _MMIO(0x138020)
#define MTL_GT_RPE_FREQUENCY     _MMIO(0x13800c)
#define MTL_RP_STATE_CAP        _MMIO(0x138000)
#define DPIO_DEVFN           0
#define IOSF_PORT_PUNIT            0x04
#define IOSF_PORT_BUNIT           0x03
#define IOSF_PORT_FLISDSI           0x1b
#define IOSF_PORT_DPIO_2           0x1a
#define IOSF_PORT_NC              0x11
#define IOSF_PORT_DPIO           0x12
#define IOSF_PORT_CCU              0xa9
#define IOSF_PORT_CCK              0x14
#define IOSF_DEVFN_SHIFT           24
#define IOSF_OPCODE_SHIFT          16
#define IOSF_PORT_SHIFT           8
#define VLV_IOSF_ADDR             _MMIO(VLV_DISPLAY_BASE + 0x2108)
#define IOSF_BYTE_ENABLES_SHIFT        4
#define IOSF_BAR_SHIFT           1
#define VLV_IOSF_DATA             _MMIO(VLV_DISPLAY_BASE + 0x2104)
#define VLV_IOSF_DOORBELL_REQ          _MMIO(VLV_DISPLAY_BASE + 0x2100)
#define IOSF_SB_BUSY              (1 << 0)
#define LBPC                  0xf4
#define GEN11_GTPM              (16)
#define GEN9_RPSWCTL_ENABLE          (0x2 << GEN6_RPSWCTL_SHIFT)
#define GEN9_RPSWCTL_DISABLE         (0x0 << GEN6_RPSWCTL_SHIFT)
#define FORCEWAKE_KERNEL_FALLBACK        BIT(15)
#define GEN6_WIZ_HASHING(hi, lo)       (((hi) << 9) | ((lo) << 7))
#define THREAD_EX_ARB_MODE           REG_GENMASK(3, 2)
#define GEN12_BUS_HASH_CTL_BIT_EXC        REG_BIT(7)
#define THREAD_EX_ARB_MODE_RR_AFTER_DEP    REG_FIELD_PREP(THREAD_EX_ARB_MODE, 0x2)
#define RT_CTRL                 MCR_REG(0xe530)
#define STACKID_CTRL             REG_GENMASK(6, 5)
#define STACKID_CTRL_512           REG_FIELD_PREP(STACKID_CTRL, 0x2)
#define XEHP_CCS_MODE_CSLICE(cslice, ccs)  (ccs << (cslice * XEHP_CCS_MODE_CSLICE_WIDTH))
#define XEHP_CCS_MODE_CSLICE_MASK       REG_GENMASK(2, 0)
#define GEN6_OFFSET(x)          ((x) << 19)
#define GEN6_AGGRESSIVE_TURBO          (0 << 15)
#define GEN6_FREQUENCY(x)          ((x) << 25)
#define _MMIO(r) ((const i915_reg_t){ .reg = (r) })
#define INTEL_GUC_TLB_INVAL_TYPE_MASK   REG_GENMASK(7, 0)
#define G2H_LEN_DW_INVALIDATE_TLB      1
#define INTEL_GUC_TLB_INVAL_FLUSH_CACHE REG_BIT(31)
#define INTEL_GUC_TLB_INVAL_MODE_MASK    REG_GENMASK(11, 8)
#define SLPC_MAX_FREQ_MHZ 4250
#define GEN6_RPSWCTL_SHIFT           9
#define XEHP_CCS_MODE_CSLICE_WIDTH        ilog2(XEHP_CCS_MODE_CSLICE_MASK + 1)
#define I915_PRIORITY_UNPREEMPTABLE INT_MAX
#define MAKE_SEND_FLAGS(len) ({ \
   typeof(len) len_ = (len); \
   GEM_BUG_ON(!FIELD_FIT(INTEL_GUC_CT_SEND_G2H_DW_MASK, len_)); \
   (FIELD_PREP(INTEL_GUC_CT_SEND_G2H_DW_MASK, len_) | INTEL_GUC_CT_SEND_NB); \
})
#define LNCFCMOCS_REG_COUNT           32
#define INTEL_GUC_CT_SEND_NB       BIT(31)
#define INTEL_GUC_CT_SEND_G2H_DW_MASK      (0xff << INTEL_GUC_CT_SEND_G2H_DW_SHIFT)
#define GUC_CTB_STATUS_UNUSED               BIT(3)
#define GUC_CLIENT_PRIORITY_HIGH    1
#define GUC_CLIENT_PRIORITY_KMD_HIGH   0
#define GUC_CLIENT_PRIORITY_KMD_NORMAL 2
#define GUC_CLIENT_PRIORITY_NORMAL   3
#define INTEL_GUC_CT_SEND_G2H_DW_SHIFT    0
#define CONTEXT_REGISTRATION_FLAG_KMD    BIT(0)
#define CONTEXT_POLICY_FLAG_PREEMPT_TO_IDLE_V69   BIT(0)
#define INTEL_GUC_STATE_CAPTURE_EVENT_STATUS_MASK      0x000000FF
#define CORE_DUMP_FLAG_IS_GUC_CAPTURE BIT(0)
#define GUC_LAST_ENGINE_CLASS       GUC_GSC_OTHER_CLASS
#define GUC_ID_TO_ENGINE_INSTANCE(guc_id) \
   (((guc_id) & GUC_ENGINE_INSTANCE_MASK) >> GUC_ENGINE_INSTANCE_SHIFT)
#define GUC_ID_TO_ENGINE_CLASS(guc_id) \
   (((guc_id) & GUC_ENGINE_CLASS_MASK) >> GUC_ENGINE_CLASS_SHIFT)
#define GUC_ENGINE_INSTANCE_MASK    (0xf << GUC_ENGINE_INSTANCE_SHIFT)
#define GUC_ENGINE_CLASS_MASK       (0x7 << GUC_ENGINE_CLASS_SHIFT)
#define I915_MAX_NUM_FENCES 32
#define I915_MAX_SFC    (I915_MAX_VCS / 2)
#define GEN_MAX_GSLICES      (I915_MAX_SS_FUSE_BITS / GEN_DSS_PER_GSLICE)
#define I915_MAX_SUBSLICES 8

// End of register offset definitions

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
    /* Struct or array holding values for Version, ID, and Control regs */
    /* (empty in Phase 1) */

    /* DMA Context - not used, placeholder deleted */

    /* Operational status flags - not used, placeholder deleted */
    /* State used to handle reset sequences - not used, placeholder deleted */
    /* Power management state - not used, placeholder deleted */
    /* Other additions - not used, placeholder deleted */
};

/* Other additions defines - not used, placeholder deleted */

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Log and return 0 for all MMIO reads */
    qemu_log_mask(LOG_GUEST_ERROR,
                  "%s: MMIO read at addr 0x%" HWADDR_PRIx " size %u\n",
                  __func__, addr, size);

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* Log and ignore all MMIO writes */
    qemu_log_mask(LOG_GUEST_ERROR,
                  "%s: MMIO write at addr 0x%" HWADDR_PRIx " val 0x%" PRIx64 " size %u\n",
                  __func__, addr, val, size);
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

    /* Placeholder: Revert registers to power-on defaults */
    /* No specific reset behavior derived from driver source */
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
        /* PIO not implemented */
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, 0x8086);
    pci_set_word(pci_conf + PCI_DEVICE_ID, 0x3577);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x030000);
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
    s->bar_info[0].index = 1; /* GEN2_MMADR_BAR */
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x200000; /* 2 MB MMIO region, covers all known register offsets */
    s->bar_info[0].name = "i915_mmio";
    
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No MSI or MSI-X init */

    /* No DMA config */

    /* No timer config */

    /* No field init */
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

    /* Placeholder: uninit (no operations needed currently) */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "i915_pci",
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
