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

#define TYPE_PCIBASE_DEVICE "vme_tsi148_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_TUNDRA 0x10e3
#define PCI_DEVICE_ID_TUNDRA_TSI148 0x148

#define TSI148_LCSR_INTS_DMA0S         (1 << 24)
#define TSI148_LCSR_INTC_DMA0C         (1 << 24)
#define TSI148_LCSR_INTS_DMA1S         (1 << 25)
#define TSI148_LCSR_INTC_DMA1C         (1 << 25)
#define TSI148_LCSR_EDPXS	0x27C
#define TSI148_LCSR_INTC_PERRC         (1 << 13)
#define TSI148_LCSR_EDPAL	0x274
#define TSI148_LCSR_EDPXA	0x278
#define TSI148_LCSR_EDPAU	0x270
#define TSI148_LCSR_EDPAT_EDPCL        (1 << 29)
#define TSI148_LCSR_EDPAT	0x280
#define TSI148_LCSR_VEAT_VEOF          (1 << 30)
#define TSI148_LCSR_VEAT	0x268
#define TSI148_LCSR_INTC_VERRC         (1 << 12)
#define TSI148_LCSR_VEAT_AM_M          (0x3F << 8)
#define TSI148_LCSR_VEAU	0x260
#define TSI148_LCSR_VEAT_VESCL         (1 << 29)
#define TSI148_LCSR_VEAL	0x264
#define TSI148_LCSR_INTC_IACKC         (1 << 10)
#define TSI148_LCSR_INTS_LM2S          (1 << 22)
#define TSI148_LCSR_INTS_IRQ3S         (1 << 3)
#define TSI148_LCSR_INTS_IRQ7S         (1 << 7)
#define TSI148_LCSR_INTS_IRQ4S         (1 << 4)
#define TSI148_LCSR_INTS_MB1S          (1 << 17)
#define TSI148_LCSR_INTS_IRQ1S         (1 << 1)
#define TSI148_LCSR_INTS_LM1S          (1 << 21)
#define TSI148_LCSR_INTC	0x454
#define TSI148_LCSR_INTS	0x450
#define TSI148_LCSR_INTS_IRQ2S         (1 << 2)
#define TSI148_LCSR_INTS_MB3S          (1 << 19)
#define TSI148_LCSR_INTS_MB2S          (1 << 18)
#define TSI148_LCSR_INTS_LM0S          (1 << 20)
#define TSI148_LCSR_INTS_PERRS         (1 << 13)
#define TSI148_LCSR_INTS_VERRS         (1 << 12)
#define TSI148_LCSR_INTS_MB0S          (1 << 16)
#define TSI148_LCSR_INTS_IRQ5S         (1 << 5)
#define TSI148_LCSR_INTS_IACKS         (1 << 10)
#define TSI148_LCSR_INTS_IRQ6S         (1 << 6)
#define TSI148_LCSR_INTS_LM3S          (1 << 23)
#define TSI148_LCSR_INTEO	0x44C
#define TSI148_LCSR_INTEO_DMA0EO       (1 << 24)
#define TSI148_LCSR_INTEO_MB1EO        (1 << 17)
#define TSI148_LCSR_INTEO_VERREO       (1 << 12)
#define TSI148_LCSR_INTEO_IACKEO       (1 << 10)
#define TSI148_LCSR_INTEO_MB2EO        (1 << 18)
#define TSI148_LCSR_INTEO_MB0EO        (1 << 16)
#define TSI148_LCSR_INTEO_DMA1EO       (1 << 25)
#define TSI148_LCSR_INTEO_MB3EO        (1 << 19)
#define TSI148_LCSR_INTEO_PERREO       (1 << 13)
#define TSI148_LCSR_INTEN	0x448
#define TSI148_LCSR_VICR	0x440
#define TSI148_LCSR_VICR_IRQS          (1 << 11)
#define TSI148_LCSR_VICR_STID_M        (0xFF << 0)
#define TSI148_LCSR_ITAT_AS_A16        (0 << 4)
#define TSI148_LCSR_ITAT_AS_A24        (1 << 4)
#define TSI148_LCSR_ITAT_AS_A32        (2 << 4)
#define TSI148_LCSR_ITAT_MBLT          (1 << 8)
#define TSI148_LCSR_OFFSET_ITEAU	0x8
#define TSI148_LCSR_OFFSET_ITOFL	0x14
#define VME_PROG	0x4000
#define TSI148_LCSR_ITAT_NPRIV         (1 << 2)
#define TSI148_LCSR_ITAT_AS_M          (7 << 4)
#define TSI148_LCSR_ITAT_AS_A64        (4 << 4)
#define TSI148_LCSR_ITAT_PGM           (1 << 1)
#define TSI148_LCSR_ITAT_BLT           (1 << 7)
#define VME_A24		0x2
#define VME_SUPER	0x1000
#define VME_A64		0x8
#define TSI148_LCSR_ITAT_EN            (1 << 31)
#define TSI148_LCSR_OFFSET_ITEAL	0xC
#define TSI148_LCSR_OFFSET_ITSAL	0x4
#define TSI148_LCSR_OFFSET_ITOFU	0x10
#define VME_DATA	0x8000
#define VME_MBLT	0x4
#define TSI148_LCSR_ITAT_SUPR          (1 << 3)
#define VME_USER	0x2000
#define VME_A16		0x1
#define TSI148_LCSR_ITAT_DATA          (1 << 0)
#define TSI148_LCSR_OFFSET_ITAT		0x18
#define VME_BLT		0x2
#define VME_A32		0x4
#define TSI148_LCSR_OFFSET_ITSAU	0x0
#define VMENAMSIZ 16
#define TSI148_LCSR_OFFSET_OTEAU	0x8
#define TSI148_LCSR_OTAT_AMODE_USER4   (11 << 0)
#define TSI148_LCSR_OTAT_EN            (1 << 31)
#define TSI148_LCSR_OTAT_DBW_M         (3 << 6)
#define TSI148_LCSR_OTAT_DBW_16        (0 << 6)
#define TSI148_LCSR_OTAT_SUP           (1 << 5)
#define TSI148_LCSR_OFFSET_OTOFL	0x14
#define VME_USER1	0x20
#define VME_USER2	0x40
#define TSI148_LCSR_OTAT_TM_MBLT       (2 << 8)
#define TSI148_LCSR_OTAT_TM_BLT        (1 << 8)
#define TSI148_LCSR_OFFSET_OTOFU	0x10
#define VME_USER3	0x80
#define VME_D16		0x2
#define TSI148_LCSR_OTAT_TM_M          (7 << 8)
#define TSI148_LCSR_OTAT_AMODE_M       (0xf << 0)
#define VME_CRCSR	0x10
#define TSI148_LCSR_OTAT_PGM           (1 << 4)
#define TSI148_LCSR_OTAT_AMODE_USER2   (9 << 0)
#define VME_USER4	0x100
#define TSI148_LCSR_OTAT_AMODE_CRCSR   (5 << 0)
#define TSI148_LCSR_OTAT_AMODE_USER1   (8 << 0)
#define TSI148_LCSR_OFFSET_OTEAL	0xC
#define TSI148_LCSR_OFFSET_OTSAL	0x4
#define TSI148_LCSR_OTAT_AMODE_A32     (2 << 0)
#define TSI148_LCSR_OTAT_AMODE_A16     (0 << 0)
#define TSI148_LCSR_OTAT_AMODE_A24     (1 << 0)
#define TSI148_LCSR_OTAT_AMODE_USER3   (10 << 0)
#define TSI148_LCSR_OFFSET_OTSAU	0x0
#define TSI148_LCSR_OTAT_DBW_32        (1 << 6)
#define TSI148_LCSR_OFFSET_OTAT		0x1C
#define VME_D32		0x4
#define TSI148_LCSR_OTAT_AMODE_A64     (4 << 0)
#define VME_SCT		0x1
#define TSI148_LCSR_OTAT_TM_SCT        (0 << 8)
#define TSI148_LCSR_RMWAL	0x224
#define TSI148_LCSR_RMWEN	0x228
#define TSI148_LCSR_RMWAU	0x220
#define TSI148_LCSR_RMWS	0x230
#define TSI148_LCSR_VMCTRL_RMWEN       (1 << 20)
#define TSI148_LCSR_RMWC	0x22C
#define TSI148_LCSR_VMCTRL	0x234
#define TSI148_LCSR_DSAT_DBW_16        (0 << 6)
#define TSI148_LCSR_DSAT_AMODE_USER3   (0xa << 0)
#define TSI148_LCSR_DSAT_DBW_32        (1 << 6)
#define TSI148_LCSR_DSAT_AMODE_USER2   (9 << 0)
#define TSI148_LCSR_DSAT_TM_BLT        (1 << 8)
#define TSI148_LCSR_DSAT_TM_MBLT       (2 << 8)
#define TSI148_LCSR_DSAT_AMODE_A16     (0 << 0)
#define TSI148_LCSR_DSAT_AMODE_A24     (1 << 0)
#define TSI148_LCSR_DSAT_AMODE_USER1   (8 << 0)
#define TSI148_LCSR_DSAT_PGM           (1 << 4)
#define TSI148_LCSR_DSAT_AMODE_USER4   (0xb << 0)
#define TSI148_LCSR_DSAT_TM_SCT        (0 << 8)
#define TSI148_LCSR_DSAT_AMODE_A32     (2 << 0)
#define TSI148_LCSR_DSAT_AMODE_A64     (4 << 0)
#define TSI148_LCSR_DSAT_SUP           (1 << 5)
#define TSI148_LCSR_DSAT_AMODE_CRCSR   (5 << 0)
#define TSI148_LCSR_DDAT_DBW_32        (1 << 6)
#define TSI148_LCSR_DDAT_PGM           (1 << 4)
#define TSI148_LCSR_DDAT_AMODE_A16      (0 << 0)
#define TSI148_LCSR_DDAT_AMODE_USER4   (0xb << 0)
#define TSI148_LCSR_DDAT_AMODE_USER2   (9 << 0)
#define TSI148_LCSR_DDAT_AMODE_USER1   (8 << 0)
#define TSI148_LCSR_DDAT_AMODE_CRCSR   (5 << 0)
#define TSI148_LCSR_DDAT_TM_MBLT       (2 << 8)
#define TSI148_LCSR_DDAT_AMODE_USER3   (0xa << 0)
#define TSI148_LCSR_DDAT_TM_BLT        (1 << 8)
#define TSI148_LCSR_DDAT_TM_SCT        (0 << 8)
#define TSI148_LCSR_DDAT_AMODE_A32      (2 << 0)
#define TSI148_LCSR_DDAT_SUP           (1 << 5)
#define TSI148_LCSR_DDAT_AMODE_A24      (1 << 0)
#define TSI148_LCSR_DDAT_AMODE_A64      (4 << 0)
#define TSI148_LCSR_DDAT_DBW_16        (0 << 6)
#define TSI148_LCSR_DDAT_TYP_VME       (1 << 28)
#define TSI148_LCSR_DSAT_NIN           (1 << 24)
#define TSI148_LCSR_DSAT_TYP_PCI       (0 << 28)
#define VME_DMA_PATTERN_INCREMENT	(1 << 2)
#define TSI148_LCSR_DNLAL_LLA          (1 << 0)
#define VME_DMA_PATTERN_BYTE		(1 << 0)
#define VME_DMA_VME			(1 << 2)
#define TSI148_LCSR_DSAT_TYP_VME       (1 << 28)
#define VME_DMA_PCI			(1 << 1)
#define TSI148_LCSR_DDAT_TYP_PCI       (0 << 28)
#define VME_DMA_PATTERN		(1 << 0)
#define TSI148_LCSR_DSAT_PSZ           (1 << 25)
#define TSI148_LCSR_DSAT_TYP_PAT       (2 << 28)
#define TSI148_LCSR_OFFSET_DSTA		0x4
#define TSI148_LCSR_DSTA_BSY           (1 << 24)
#define TSI148_LCSR_OFFSET_DCTL		0x0
#define TSI148_LCSR_DCTL_DGO           (1 << 25)
#define TSI148_LCSR_OFFSET_DNLAU	0x38
#define TSI148_LCSR_DSTA_VBE           (1 << 28)
#define TSI148_LCSR_OFFSET_DNLAL	0x3C
#define TSI148_LCSR_DCTL_ABT           (1 << 27)
#define TSI148_LCSR_LMAT_AS_A64        (4 << 4)
#define TSI148_LCSR_LMAT	0x42C
#define TSI148_LCSR_LMBAU	0x424
#define TSI148_LCSR_LMAT_AS_A16        (0 << 4)
#define TSI148_LCSR_LMAT_AS_A32        (2 << 4)
#define TSI148_LCSR_LMAT_NPRIV         (1 << 2)
#define TSI148_LCSR_LMAT_DATA          (1 << 0)
#define TSI148_LCSR_LMBAL	0x428
#define TSI148_LCSR_LMAT_AS_A24        (1 << 4)
#define TSI148_LCSR_LMAT_SUPR          (1 << 3)
#define TSI148_LCSR_LMAT_PGM           (1 << 1)
#define TSI148_LCSR_LMAT_EN            (1 << 7)
#define TSI148_LCSR_LMAT_AS_M          (7 << 4)
#define TSI148_LCSR_VSTAT	0x23C
#define TSI148_LCSR_VSTAT_GA_M         (0x1F << 0)
#define TSI148_CRCSR_CBAR_M            (0x1F << 3)
#define VME_CRCSR_BUF_SIZE (508 * 1024)
#define TSI148_LCSR_CRAT	0x420
#define TSI148_LCSR_CROL	0x41C
#define TSI148_CBAR	0xFFC
#define TSI148_LCSR_CRAT_EN            (1 << 7)
#define TSI148_LCSR_CROU	0x418
#define TSI148_MAX_DMA			2
#define VME_DMA_MEM_TO_MEM		(1 << 3)
#define VME_DMA_PATTERN_TO_MEM		(1 << 5)
#define TSI148_LCSR_VSTAT_SCONS        (1 << 8)
#define TSI148_LCSR_VSTAT_CPURST       (1 << 15)
#define TSI148_MAX_MASTER		8
#define TSI148_MAX_SLAVE		8
#define VME_DMA_VME_TO_MEM		(1 << 0)
#define VME_MAX_SLOTS		32
#define VME_DMA_MEM_TO_VME		(1 << 1)
#define TSI148_LCSR_VSTAT_BRDFL        (1 << 14)
#define VME_DMA_PATTERN_TO_VME		(1 << 4)
#define VME_DMA_VME_TO_VME		(1 << 2)
#define TSI148_PCFS_ID			0x0
#define TSI148_LCSR_CSRAT	0x414
#define TSI148_LCSR_INTM1	0x458
#define TSI148_LCSR_INTM2	0x45C
#define TSI148_LCSR_PSTAT	0x240
#define TSI148_LCSR_INTC_LM2C          (1 << 22)
#define TSI148_LCSR_INTC_LM0C          (1 << 20)
#define TSI148_LCSR_INTC_LM1C          (1 << 21)
#define TSI148_LCSR_INTC_LM3C          (1 << 23)
#define TSI148_GCSR_MBOX3	0x61C
#define TSI148_GCSR_MBOX2	0x618
#define TSI148_GCSR_MBOX0	0x610
#define TSI148_GCSR_MBOX1	0x614
#define TSI148_LCSR_INTC_MB3C          (1 << 19)
#define TSI148_LCSR_INTC_MB0C          (1 << 16)
#define TSI148_LCSR_INTC_MB1C          (1 << 17)
#define TSI148_LCSR_INTC_MB2C          (1 << 18)
#define TSI148_LCSR_VIACK3	0x20C
#define TSI148_LCSR_VIACK7	0x21C
#define TSI148_LCSR_VIACK6	0x218
#define TSI148_LCSR_VIACK1	0x204
#define TSI148_LCSR_VIACK5	0x214
#define TSI148_LCSR_VIACK2	0x208
#define TSI148_LCSR_VIACK4	0x210
#define TSI148_LCSR_INTEN_IRQ2EN       (1 << 2)
#define TSI148_LCSR_INTEN_IRQ3EN       (1 << 3)
#define TSI148_LCSR_INTEN_IRQ4EN       (1 << 4)
#define TSI148_LCSR_INTEN_IRQ6EN       (1 << 6)
#define TSI148_LCSR_INTEN_IRQ1EN       (1 << 1)
#define TSI148_LCSR_INTEN_IRQ5EN       (1 << 5)
#define TSI148_LCSR_INTEN_IRQ7EN       (1 << 7)
#define TSI148_LCSR_INTEO_IRQ1EO       (1 << 1)
#define TSI148_LCSR_INTEO_IRQ4EO       (1 << 4)
#define TSI148_LCSR_INTEO_IRQ2EO       (1 << 2)
#define TSI148_LCSR_INTEO_IRQ3EO       (1 << 3)
#define TSI148_LCSR_INTEO_IRQ5EO       (1 << 5)
#define TSI148_LCSR_INTEO_IRQ7EO       (1 << 7)
#define TSI148_LCSR_INTEO_IRQ6EO       (1 << 6)
#define TSI148_LCSR_VICR_IRQL_1        (1 << 8)
#define TSI148_LCSR_VICR_IRQL_4        (4 << 8)
#define TSI148_LCSR_VICR_IRQL_5        (5 << 8)
#define TSI148_LCSR_VICR_IRQL_7        (7 << 8)
#define TSI148_LCSR_VICR_IRQL_6        (6 << 8)
#define TSI148_LCSR_VICR_IRQL_3        (3 << 8)
#define TSI148_LCSR_VICR_IRQL_2        (2 << 8)
#define TSI148_LCSR_IT5		0x3A0
#define TSI148_LCSR_IT6		0x3C0
#define TSI148_LCSR_IT1		0x320
#define TSI148_LCSR_IT3		0x360
#define TSI148_LCSR_IT4		0x380
#define TSI148_LCSR_IT0		0x300
#define TSI148_LCSR_IT7		0x3E0
#define TSI148_LCSR_IT2		0x340
#define TSI148_LCSR_OT3		0x160
#define TSI148_LCSR_OT0		0x100
#define TSI148_LCSR_OT4		0x180
#define TSI148_LCSR_OT6		0x1C0
#define TSI148_LCSR_OT2		0x140
#define TSI148_LCSR_OT7		0x1E0
#define TSI148_LCSR_OT5		0x1A0
#define TSI148_LCSR_OT1		0x120
#define TSI148_LCSR_DMA1	0x580
#define TSI148_LCSR_DMA0	0x500
#define TSI148_LCSR_INTEO_LM0EO        (1 << 20)
#define TSI148_LCSR_INTEO_LM1EO        (1 << 21)
#define TSI148_LCSR_INTEO_LM3EO        (1 << 23)
#define TSI148_LCSR_INTEO_LM2EO        (1 << 22)
#define TSI148_LCSR_INTEN_LM0EN        (1 << 20)
#define TSI148_LCSR_INTEN_LM3EN        (1 << 23)
#define TSI148_LCSR_INTEN_LM2EN        (1 << 22)
#define TSI148_LCSR_INTEN_LM1EN        (1 << 21)
#define VME_NUM_STATUSID	256

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
    uint32_t lcsr_ints;
    uint32_t lcsr_intc;
    uint32_t lcsr_inteo;
    uint32_t lcsr_inten;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t regs[0x1000 / 4];

    /* DMA Context */
    uint32_t dma_dctls[2];
    uint32_t dma_dstas[2];
    uint32_t dma_dnlau[2];
    uint32_t dma_dnlal[2];

};

struct tsi148_dma_descriptor {
    uint32_t dsau;
    uint32_t dsal;
    uint32_t ddau;
    uint32_t ddal;
    uint32_t dsat;
    uint32_t ddat;
    uint32_t dnlau;
    uint32_t dnlal;
    uint32_t dcnt;
    uint32_t ddbs;
};

/* Internal helper for status-triggered signaling. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool active = (s->lcsr_ints & s->lcsr_inteo) != 0;
    pci_set_irq(pdev, active ? 1 : 0);
}

/* Device-initiated DMA logic based on driver access patterns */
static void pcibase_do_dma(PCIBaseState *s, int channel)
{
    uint32_t dma_base = channel == 0 ? 0x500 : 0x580;
    
    /* Clear DGO and ABT */
    s->regs[(dma_base + TSI148_LCSR_OFFSET_DCTL) / 4] &= ~(TSI148_LCSR_DCTL_DGO | TSI148_LCSR_DCTL_ABT);
    
    /* Clear BSY in DSTA */
    s->regs[(dma_base + TSI148_LCSR_OFFSET_DSTA) / 4] &= ~TSI148_LCSR_DSTA_BSY;
    
    /* Set DMAxS in INTS */
    if (channel == 0) {
        s->lcsr_ints |= TSI148_LCSR_INTS_DMA0S;
    } else {
        s->lcsr_ints |= TSI148_LCSR_INTS_DMA1S;
    }
    
    pcibase_update_irq(s);
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = 0;

    if (addr >= 0x1000) {
        return 0;
    }

    switch (addr) {
    case TSI148_PCFS_ID: /* 0x0 */
        /* Driver uses ioread32 (LE) and expects 0x10e3. 
         * Since region is BE, we return bswap32(0x014810e3) = 0xe3104801 */
        val = 0xe3104801;
        break;
    case TSI148_LCSR_INTS: /* 0x450 */
        val = s->lcsr_ints;
        break;
    case TSI148_LCSR_INTEN: /* 0x448 */
        val = s->lcsr_inten;
        break;
    case TSI148_LCSR_INTEO: /* 0x44C */
        val = s->lcsr_inteo;
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

    if (addr >= 0x1000) {
        return;
    }

    switch (addr) {
    case TSI148_LCSR_INTC: /* 0x454 */
        s->lcsr_ints &= ~val;
        pcibase_update_irq(s);
        break;
    case TSI148_LCSR_INTEN: /* 0x448 */
        s->lcsr_inten = val;
        pcibase_update_irq(s);
        break;
    case TSI148_LCSR_INTEO: /* 0x44C */
        s->lcsr_inteo = val;
        pcibase_update_irq(s);
        break;
    case TSI148_LCSR_VICR: /* 0x440 */
        s->regs[addr / 4] = val;
        /* If driver is generating a VME IRQ, simulate immediate IACK */
        if (val & 0x0700) { /* IRQL mask */
            s->regs[addr / 4] &= ~TSI148_LCSR_VICR_IRQS;
            s->lcsr_ints |= TSI148_LCSR_INTS_IACKS;
            pcibase_update_irq(s);
        }
        break;
    case TSI148_LCSR_DMA0 + TSI148_LCSR_OFFSET_DCTL: /* 0x500 */
        s->regs[addr / 4] = val;
        if (val & TSI148_LCSR_DCTL_DGO) {
            pcibase_do_dma(s, 0);
        }
        break;
    case TSI148_LCSR_DMA1 + TSI148_LCSR_OFFSET_DCTL: /* 0x580 */
        s->regs[addr / 4] = val;
        if (val & TSI148_LCSR_DCTL_DGO) {
            pcibase_do_dma(s, 1);
        }
        break;
    default:
        s->regs[addr / 4] = val;
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
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
    s->lcsr_ints = 0;
    s->lcsr_intc = 0;
    s->lcsr_inteo = 0;
    s->lcsr_inten = 0;

    /* Set VSTAT to indicate System Controller */
    s->regs[0x23C / 4] = TSI148_LCSR_VSTAT_SCONS;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_TUNDRA );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_TUNDRA_TSI148 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_BRIDGE_OTHER );
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
    s->bar_info[0].size = 0x1000;
    s->bar_info[0].name = "tsi148-bar0";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
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

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "vme_tsi148_pci",
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
