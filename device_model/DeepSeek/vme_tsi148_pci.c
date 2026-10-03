/*
 * QEMU model for TSI148 VME bridge
 * Generated from Linux driver: drivers/staging/vme_user/vme_tsi148.c
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
/* PCI Identification */
#define PCI_VENDOR_ID_TUNDRA 0x10e3
#define PCI_DEVICE_ID_TUNDRA_TSI148 0x148

/* Register offsets from Linux driver (TSI148 LCSR and GCSR) */
#define TSI148_LCSR_INTS_DMA0S         BIT(24)
#define TSI148_LCSR_INTC_DMA0C         BIT(24)
#define TSI148_LCSR_INTS_DMA1S         BIT(25)
#define TSI148_LCSR_INTC_DMA1C         BIT(25)
#define TSI148_LCSR_EDPXS	0x27C
#define TSI148_LCSR_INTC_PERRC         BIT(13)
#define TSI148_LCSR_EDPAL	0x274
#define TSI148_LCSR_EDPXA	0x278
#define TSI148_LCSR_EDPAU	0x270
#define TSI148_LCSR_EDPAT_EDPCL        BIT(29)
#define TSI148_LCSR_EDPAT	0x280
#define TSI148_LCSR_VEAT_VEOF          BIT(30)
#define TSI148_LCSR_VEAT	0x268
#define TSI148_LCSR_INTC_VERRC         BIT(12)
#define TSI148_LCSR_VEAT_AM_M          (0x3F << 8)
#define TSI148_LCSR_VEAU	0x260
#define TSI148_LCSR_VEAT_VESCL         BIT(29)
#define TSI148_LCSR_VEAL	0x264
#define TSI148_LCSR_INTC_IACKC         BIT(10)
#define TSI148_LCSR_INTS_LM2S          BIT(22)
#define TSI148_LCSR_INTS_IRQ3S         BIT(3)
#define TSI148_LCSR_INTS_IRQ7S         BIT(7)
#define TSI148_LCSR_INTS_IRQ4S         BIT(4)
#define TSI148_LCSR_INTS_MB1S          BIT(17)
#define TSI148_LCSR_INTS_IRQ1S         BIT(1)
#define TSI148_LCSR_INTS_LM1S          BIT(21)
#define TSI148_LCSR_INTC	0x454
#define TSI148_LCSR_INTS	0x450
#define TSI148_LCSR_INTS_IRQ2S         BIT(2)
#define TSI148_LCSR_INTS_MB3S          BIT(19)
#define TSI148_LCSR_INTS_MB2S          BIT(18)
#define TSI148_LCSR_INTS_LM0S          BIT(20)
#define TSI148_LCSR_INTS_PERRS         BIT(13)
#define TSI148_LCSR_INTS_VERRS         BIT(12)
#define TSI148_LCSR_INTS_MB0S          BIT(16)
#define TSI148_LCSR_INTS_IRQ5S         BIT(5)
#define TSI148_LCSR_INTS_IACKS         BIT(10)
#define TSI148_LCSR_INTS_IRQ6S         BIT(6)
#define TSI148_LCSR_INTS_LM3S          BIT(23)
#define TSI148_LCSR_INTEO	0x44C
#define TSI148_LCSR_INTEO_DMA0EO       BIT(24)
#define TSI148_LCSR_INTEO_MB1EO        BIT(17)
#define TSI148_LCSR_INTEO_VERREO       BIT(12)
#define TSI148_LCSR_INTEO_IACKEO       BIT(10)
#define TSI148_LCSR_INTEO_MB2EO        BIT(18)
#define TSI148_LCSR_INTEO_MB0EO        BIT(16)
#define TSI148_LCSR_INTEO_DMA1EO       BIT(25)
#define TSI148_LCSR_INTEO_MB3EO        BIT(19)
#define TSI148_LCSR_INTEO_PERREO       BIT(13)
#define TSI148_LCSR_INTEN	0x448
#define TSI148_LCSR_VICR	0x440
#define TSI148_LCSR_VICR_IRQS          BIT(11)
#define TSI148_LCSR_VICR_STID_M        (0xFF << 0)
#define TSI148_LCSR_ITAT_AS_A16        (0 << 4)
#define TSI148_LCSR_ITAT_AS_A24        BIT(4)
#define TSI148_LCSR_ITAT_AS_A32        (2 << 4)
#define TSI148_LCSR_ITAT_MBLT          BIT(8)
#define TSI148_LCSR_OFFSET_ITEAU	0x8
#define TSI148_LCSR_OFFSET_ITOFL	0x14
#define VME_PROG	0x4000
#define TSI148_LCSR_ITAT_NPRIV         BIT(2)
#define TSI148_LCSR_ITAT_AS_M          (7 << 4)
#define TSI148_LCSR_ITAT_AS_A64        (4 << 4)
#define TSI148_LCSR_ITAT_PGM           BIT(1)
#define TSI148_LCSR_ITAT_BLT           BIT(7)
#define VME_A24		0x2
#define VME_SUPER	0x1000
#define VME_A64		0x8
#define TSI148_LCSR_ITAT_EN            BIT(31)
#define TSI148_LCSR_OFFSET_ITEAL	0xC
#define TSI148_LCSR_OFFSET_ITSAL	0x4
#define TSI148_LCSR_OFFSET_ITOFU	0x10
#define VME_DATA	0x8000
#define VME_MBLT	0x4
#define TSI148_LCSR_ITAT_SUPR          BIT(3)
#define VME_USER	0x2000
#define VME_A16		0x1
#define TSI148_LCSR_ITAT_DATA          BIT(0)
#define TSI148_LCSR_OFFSET_ITAT		0x18
#define VME_BLT		0x2
#define VME_A32		0x4
#define TSI148_LCSR_OFFSET_ITSAU	0x0
#define VMENAMSIZ 16
#define TSI148_LCSR_OFFSET_OTEAU	0x8
#define TSI148_LCSR_OTAT_AMODE_USER4   (11 << 0)
#define TSI148_LCSR_OTAT_EN            BIT(31)
#define TSI148_LCSR_OTAT_DBW_M         (3 << 6)
#define TSI148_LCSR_OTAT_DBW_16        (0 << 6)
#define TSI148_LCSR_OTAT_SUP           BIT(5)
#define TSI148_LCSR_OFFSET_OTOFL	0x14
#define VME_USER1	0x20
#define VME_USER2	0x40
#define TSI148_LCSR_OTAT_TM_MBLT       (2 << 8)
#define TSI148_LCSR_OTAT_TM_BLT        BIT(8)
#define TSI148_LCSR_OFFSET_OTOFU	0x10
#define VME_USER3	0x80
#define VME_D16		0x2
#define TSI148_LCSR_OTAT_TM_M          (7 << 8)
#define TSI148_LCSR_OTAT_AMODE_M       (0xf << 0)
#define VME_CRCSR	0x10
#define TSI148_LCSR_OTAT_PGM           BIT(4)
#define TSI148_LCSR_OTAT_AMODE_USER2   (9 << 0)
#define VME_USER4	0x100
#define TSI148_LCSR_OTAT_AMODE_CRCSR   (5 << 0)
#define TSI148_LCSR_OTAT_AMODE_USER1   (8 << 0)
#define TSI148_LCSR_OFFSET_OTEAL	0xC
#define TSI148_LCSR_OFFSET_OTSAL	0x4
#define TSI148_LCSR_OTAT_AMODE_A32     (2 << 0)
#define TSI148_LCSR_OTAT_AMODE_A16     (0 << 0)
#define TSI148_LCSR_OTAT_AMODE_A24     BIT(0)
#define TSI148_LCSR_OTAT_AMODE_USER3   (10 << 0)
#define TSI148_LCSR_OFFSET_OTSAU	0x0
#define TSI148_LCSR_OTAT_DBW_32        BIT(6)
#define TSI148_LCSR_OFFSET_OTAT		0x1C
#define VME_D32		0x4
#define TSI148_LCSR_OTAT_AMODE_A64     (4 << 0)
#define VME_SCT		0x1
#define TSI148_LCSR_OTAT_TM_SCT        (0 << 8)
#define TSI148_LCSR_RMWAL	0x224
#define TSI148_LCSR_RMWEN	0x228
#define TSI148_LCSR_RMWAU	0x220
#define TSI148_LCSR_RMWS	0x230
#define TSI148_LCSR_VMCTRL_RMWEN       BIT(20)
#define TSI148_LCSR_RMWC	0x22C
#define TSI148_LCSR_VMCTRL	0x234
#define TSI148_LCSR_DSAT_DBW_16        (0 << 6)
#define TSI148_LCSR_DSAT_AMODE_USER3   (0xa << 0)
#define TSI148_LCSR_DSAT_DBW_32        BIT(6)
#define TSI148_LCSR_DSAT_AMODE_USER2   (9 << 0)
#define TSI148_LCSR_DSAT_TM_BLT        BIT(8)
#define TSI148_LCSR_DSAT_TM_MBLT       (2 << 8)
#define TSI148_LCSR_DSAT_AMODE_A16     (0 << 0)
#define TSI148_LCSR_DSAT_AMODE_A24     BIT(0)
#define TSI148_LCSR_DSAT_AMODE_USER1   (8 << 0)
#define TSI148_LCSR_DSAT_PGM           BIT(4)
#define TSI148_LCSR_DSAT_AMODE_USER4   (0xb << 0)
#define TSI148_LCSR_DSAT_TM_SCT        (0 << 8)
#define TSI148_LCSR_DSAT_AMODE_A32     (2 << 0)
#define TSI148_LCSR_DSAT_AMODE_A64     (4 << 0)
#define TSI148_LCSR_DSAT_SUP           BIT(5)
#define TSI148_LCSR_DSAT_AMODE_CRCSR   (5 << 0)
#define TSI148_LCSR_DDAT_DBW_32        BIT(6)
#define TSI148_LCSR_DDAT_PGM           BIT(4)
#define TSI148_LCSR_DDAT_AMODE_A16      (0 << 0)
#define TSI148_LCSR_DDAT_AMODE_USER4   (0xb << 0)
#define TSI148_LCSR_DDAT_AMODE_USER2   (9 << 0)
#define TSI148_LCSR_DDAT_AMODE_USER1   (8 << 0)
#define TSI148_LCSR_DDAT_AMODE_CRCSR   (5 << 0)
#define TSI148_LCSR_DDAT_TM_MBLT       (2 << 8)
#define TSI148_LCSR_DDAT_AMODE_USER3   (0xa << 0)
#define TSI148_LCSR_DDAT_TM_BLT        BIT(8)
#define TSI148_LCSR_DDAT_TM_SCT        (0 << 8)
#define TSI148_LCSR_DDAT_AMODE_A32      (2 << 0)
#define TSI148_LCSR_DDAT_SUP           BIT(5)
#define TSI148_LCSR_DDAT_AMODE_A24      BIT(0)
#define TSI148_LCSR_DDAT_AMODE_A64      (4 << 0)
#define TSI148_LCSR_DDAT_DBW_16        (0 << 6)
#define TSI148_LCSR_DDAT_TYP_VME       BIT(28)
#define TSI148_LCSR_DSAT_NIN           BIT(24)
#define TSI148_LCSR_DSAT_TYP_PCI       (0 << 28)
#define VME_DMA_PATTERN_INCREMENT	BIT(2)
#define TSI148_LCSR_DNLAL_LLA          BIT(0)
#define VME_DMA_PATTERN_BYTE		BIT(0)
#define VME_DMA_VME			BIT(2)
#define TSI148_LCSR_DSAT_TYP_VME       BIT(28)
#define VME_DMA_PCI			BIT(1)
#define TSI148_LCSR_DDAT_TYP_PCI       (0 << 28)
#define VME_DMA_PATTERN		BIT(0)
#define TSI148_LCSR_DSAT_PSZ           BIT(25)
#define TSI148_LCSR_DSAT_TYP_PAT       (2 << 28)
#define TSI148_LCSR_OFFSET_DSTA		0x4
#define TSI148_LCSR_DSTA_BSY           BIT(24)
#define TSI148_LCSR_OFFSET_DCTL		0x0
#define TSI148_LCSR_DCTL_DGO           BIT(25)
#define TSI148_LCSR_OFFSET_DNLAU	0x38
#define TSI148_LCSR_DSTA_VBE           BIT(28)
#define TSI148_LCSR_OFFSET_DNLAL	0x3C
#define TSI148_LCSR_DCTL_ABT           BIT(27)
#define TSI148_LCSR_LMAT_AS_A64        (4 << 4)
#define TSI148_LCSR_LMAT	0x42C
#define TSI148_LCSR_LMBAU	0x424
#define TSI148_LCSR_LMAT_AS_A16        (0 << 4)
#define TSI148_LCSR_LMAT_AS_A32        (2 << 4)
#define TSI148_LCSR_LMAT_NPRIV         BIT(2)
#define TSI148_LCSR_LMAT_DATA          BIT(0)
#define TSI148_LCSR_LMBAL	0x428
#define TSI148_LCSR_LMAT_AS_A24        BIT(4)
#define TSI148_LCSR_LMAT_SUPR          BIT(3)
#define TSI148_LCSR_LMAT_PGM           BIT(1)
#define TSI148_LCSR_LMAT_EN            BIT(7)
#define TSI148_LCSR_LMAT_AS_M          (7 << 4)
#define TSI148_LCSR_VSTAT	0x23C
#define TSI148_LCSR_VSTAT_GA_M         (0x1F << 0)
#define TSI148_CRCSR_CBAR_M            (0x1F << 3)
#define VME_CRCSR_BUF_SIZE (508 * 1024)
#define TSI148_LCSR_CRAT	0x420
#define TSI148_LCSR_CROL	0x41C
#define TSI148_CBAR	0xFFC
#define TSI148_LCSR_CRAT_EN            BIT(7)
#define TSI148_LCSR_CROU	0x418
#define TSI148_MAX_DMA			2
#define VME_DMA_MEM_TO_MEM		BIT(3)
#define VME_DMA_PATTERN_TO_MEM		BIT(5)
#define TSI148_LCSR_VSTAT_SCONS        BIT(8)
#define TSI148_LCSR_VSTAT_CPURST       BIT(15)
#define TSI148_MAX_MASTER		8
#define TSI148_MAX_SLAVE		8
#define VME_DMA_VME_TO_MEM		BIT(0)
#define VME_MAX_SLOTS		32
#define VME_DMA_MEM_TO_VME		BIT(1)
#define TSI148_LCSR_VSTAT_BRDFL        BIT(14)
#define PCI_VENDOR_ID_TUNDRA 0x10e3
#define VME_DMA_PATTERN_TO_VME		BIT(4)
#define VME_DMA_VME_TO_VME		BIT(2)
#define TSI148_PCFS_ID			0x0
#define TSI148_LCSR_CSRAT	0x414
#define TSI148_LCSR_INTM1	0x458
#define TSI148_LCSR_INTM2	0x45C
#define TSI148_LCSR_PSTAT	0x240
#define PCI_DEVICE_ID_TUNDRA_TSI148 0x148
#define TSI148_LCSR_INTC_LM2C          BIT(22)
#define TSI148_LCSR_INTC_LM0C          BIT(20)
#define TSI148_LCSR_INTC_LM1C          BIT(21)
#define TSI148_LCSR_INTC_LM3C          BIT(23)
#define TSI148_GCSR_MBOX3	0x61C
#define TSI148_GCSR_MBOX2	0x618
#define TSI148_GCSR_MBOX0	0x610
#define TSI148_GCSR_MBOX1	0x614
#define TSI148_LCSR_INTC_MB3C          BIT(19)
#define TSI148_LCSR_INTC_MB0C          BIT(16)
#define TSI148_LCSR_INTC_MB1C          BIT(17)
#define TSI148_LCSR_INTC_MB2C          BIT(18)
#define TSI148_LCSR_VIACK3	0x20C
#define TSI148_LCSR_VIACK7	0x21C
#define TSI148_LCSR_VIACK6	0x218
#define TSI148_LCSR_VIACK1	0x204
#define TSI148_LCSR_VIACK5	0x214
#define TSI148_LCSR_VIACK2	0x208
#define TSI148_LCSR_VIACK4	0x210
#define TSI148_LCSR_INTEN_IRQ2EN       BIT(2)
#define TSI148_LCSR_INTEN_IRQ3EN       BIT(3)
#define TSI148_LCSR_INTEN_IRQ4EN       BIT(4)
#define TSI148_LCSR_INTEN_IRQ6EN       BIT(6)
#define TSI148_LCSR_INTEN_IRQ1EN       BIT(1)
#define TSI148_LCSR_INTEN_IRQ5EN       BIT(5)
#define TSI148_LCSR_INTEN_IRQ7EN       BIT(7)
#define TSI148_LCSR_INTEO_IRQ1EO       BIT(1)
#define TSI148_LCSR_INTEO_IRQ4EO       BIT(4)
#define TSI148_LCSR_INTEO_IRQ2EO       BIT(2)
#define TSI148_LCSR_INTEO_IRQ3EO       BIT(3)
#define TSI148_LCSR_INTEO_IRQ5EO       BIT(5)
#define TSI148_LCSR_INTEO_IRQ7EO       BIT(7)
#define TSI148_LCSR_INTEO_IRQ6EO       BIT(6)
#define TSI148_LCSR_VICR_IRQL_1        BIT(8)
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
#define TSI148_LCSR_INTEO_LM0EO        BIT(20)
#define TSI148_LCSR_INTEO_LM1EO        BIT(21)
#define TSI148_LCSR_INTEO_LM3EO        BIT(23)
#define TSI148_LCSR_INTEO_LM2EO        BIT(22)
#define TSI148_LCSR_INTEN_LM0EN        BIT(20)
#define TSI148_LCSR_INTEN_LM3EN        BIT(23)
#define TSI148_LCSR_INTEN_LM2EN        BIT(22)
#define TSI148_LCSR_INTEN_LM1EN        BIT(21)
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
    uint32_t lcsr_ints;   /* Interrupt Status */
    uint32_t lcsr_intc;   /* Interrupt Clear */
    uint32_t lcsr_inten;  /* Interrupt Enable */
    uint32_t lcsr_inteo;  /* Interrupt Enable Output */
    uint32_t lcsr_intm1;
    uint32_t lcsr_intm2;

    /* Additional hardware register shadows for probe-critical registers */
    uint32_t vstat;       /* VSTAT register shadow */
    uint32_t cbar;        /* CBAR register */
    uint32_t crat;        /* CRAT register */
    uint32_t crou;        /* CROU register */
    uint32_t crol;        /* CROL register */

    uint8_t regs[4096];   /* placeholder for full register space */

    /* DMA Context */
    struct {
        bool busy;
        uint32_t src_attr;
        uint32_t dst_attr;
        hwaddr src_addr;
        hwaddr dst_addr;
        uint32_t count;
        uint32_t dst_la;
        uint32_t src_la;
    } dma_chan[2];  /* TSI148 has two DMA channels */

    /* Operational status flags */
    bool soft_reset;
    /* Power management state (D0-D3) */
    /* Not used by driver */
    /* Other addition info */
    /* Not used */
};



/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* IRQ asserted if any enabled interrupt source is active */
    if (s->lcsr_ints & s->lcsr_inteo) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Most driver accesses are 32-bit big-endian (ioread32be) */
    if (size == 4) {
        switch (addr) {
        case 0x0000: /* TSI148_PCFS_ID */
            /* This register is read with ioread32 (little-endian) */
            /* Return pre-swapped value so that lower 16 bits appear as 0x10E3 */
            val = 0xE3100000;
            break;
        case 0x023C: /* TSI148_LCSR_VSTAT */
            val = s->vstat;
            break;
        case 0x0440: /* TSI148_LCSR_VICR */
            val = 0; /* No VME IRQ active */
            break;
        case 0x0448: /* TSI148_LCSR_INTEN */
            val = s->lcsr_inten;
            break;
        case 0x044C: /* TSI148_LCSR_INTEO */
            val = s->lcsr_inteo;
            break;
        case 0x0450: /* TSI148_LCSR_INTS */
            val = s->lcsr_ints;
            break;
        case 0x0454: /* TSI148_LCSR_INTC */
            val = s->lcsr_intc;
            break;
        case 0x0418: /* TSI148_LCSR_CROU */
            val = s->crou;
            break;
        case 0x041C: /* TSI148_LCSR_CROL */
            val = s->crol;
            break;
        case 0x0420: /* TSI148_LCSR_CRAT */
            val = s->crat;
            break;
        case 0x0FFC: /* TSI148_CBAR */
            val = s->cbar;
            break;
        default:
            /* Unimplemented registers return 0 */
            val = 0;
            break;
        }
    } else if (size == 1) {
        /* Allow 8-bit reads (e.g., IACK) */
        if (addr >= 0x204 && addr <= 0x21C) {
            /* VIACK registers - return 0 for now */
            val = 0;
        } else {
            /* For other 1-byte reads, return 0 */
            val = 0;
        }
    } else {
        /* Unsupported sizes */
        val = 0;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size == 4) {
        switch (addr) {
        case 0x023C: /* TSI148_LCSR_VSTAT */
            /* Allow writes to BRDFL (bit14) and CPURST (bit15); preserve others */
            s->vstat = (s->vstat & ~(TSI148_LCSR_VSTAT_BRDFL | TSI148_LCSR_VSTAT_CPURST)) |
                       (val & (TSI148_LCSR_VSTAT_BRDFL | TSI148_LCSR_VSTAT_CPURST));
            break;
        case 0x0448: /* TSI148_LCSR_INTEN */
            s->lcsr_inten = val;
            pcibase_update_irq(s);
            break;
        case 0x044C: /* TSI148_LCSR_INTEO */
            s->lcsr_inteo = val;
            pcibase_update_irq(s);
            break;
        case 0x0454: /* TSI148_LCSR_INTC (W1C) */
            s->lcsr_ints &= ~val;
            pcibase_update_irq(s);
            break;
        case 0x0418: /* TSI148_LCSR_CROU */
            s->crou = val;
            break;
        case 0x041C: /* TSI148_LCSR_CROL */
            s->crol = val;
            break;
        case 0x0420: /* TSI148_LCSR_CRAT */
            s->crat = val;
            break;
        case 0x0FFC: /* TSI148_CBAR */
            s->cbar = val;
            break;
        default:
            /* Ignore writes to unimplemented registers */
            break;
        }
    }
    /* Ignore writes of other sizes */
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* PIO not used by this driver */
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* PIO not used by this driver */
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_BIG_ENDIAN,  /* Driver uses ioread32be */
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
    s->lcsr_ints = 0;
    s->lcsr_intc = 0;
    s->lcsr_inten = 0;
    s->lcsr_inteo = 0;
    s->vstat = TSI148_LCSR_VSTAT_SCONS; /* System controller, GA=0 */
    s->cbar = 0;
    s->crat = 0;
    s->crou = 0;
    s->crol = 0;
    s->soft_reset = false;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x10e3 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x0148 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0680 ); /* bridge */
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
    s->bar_info[0].size = 0x1000;  /* 4096 bytes */
    s->bar_info[0].name = "bar0";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize register values */
    s->vstat = TSI148_LCSR_VSTAT_SCONS; /* system controller, GA=0 */
    s->cbar = 0;
    s->crat = 0;
    s->crou = 0;
    s->crol = 0;
    s->lcsr_ints = 0;
    s->lcsr_intc = 0;
    s->lcsr_inten = 0;
    s->lcsr_inteo = 0;
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }

    /* Free buffers, stop timers, etc. (none needed currently) */
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
