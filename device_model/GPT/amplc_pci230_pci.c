/*
 * QEMU PCI device model for amplc_pci230
 * Phase 2: functional behavior (minimal needed for probe/insn IO)
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

#include "hw/pci/pci_ids.h"

#define TYPE_PCIBASE_DEVICE "amplc_pci230_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI230_PPI_X_BASE      0x00
#define PCI230_PPI_X_A         0x00
#define PCI230_PPI_X_B         0x01
#define PCI230_PPI_X_C         0x02
#define PCI230_PPI_X_CMD       0x03
#define PCI230_Z2_CT_BASE      0x14
#define PCI230_ZCLK_SCE        0x1A
#define PCI230_ZGAT_SCE        0x1D
#define PCI230_INT_SCE         0x1E
#define PCI230_INT_STAT        0x1E
#define PCI230_DACCON          0x00
#define PCI230_DACOUT1         0x02
#define PCI230_DACOUT2         0x04
#define PCI230_ADCDATA         0x08
#define PCI230_ADCSWTRIG       0x08
#define PCI230_ADCCON          0x0A
#define PCI230_ADCEN           0x0C
#define PCI230_ADCG            0x0E
#define PCI230P_ADCTRIG        0x10
#define PCI230P_ADCTH          0x12
#define PCI230P_ADCFFTH        0x14
#define PCI230P_ADCFFLEV       0x16
#define PCI230P_ADCPTSC        0x18
#define PCI230P_ADCHYST        0x1A
#define PCI230P_EXTFUNC        0x1C
#define PCI230P_HWVER          0x1E
#define PCI230P2_DACDATA       0x06
#define PCI230P2_DACSWTRIG     0x02
#define PCI230P2_DACEN         0x06

#define PCI230_DAC_OR(x)               (((x) & 0x1) << 0)
#define PCI230_DAC_OR_UNI              PCI230_DAC_OR(0)
#define PCI230_DAC_OR_BIP              PCI230_DAC_OR(1)
#define PCI230_DAC_OR_MASK             PCI230_DAC_OR(1)
#define PCI230P2_DAC_FIFO_EN           (1U << 8)
#define PCI230P2_DAC_TRIG(x)           (((x) & 0x7) << 2)
#define PCI230P2_DAC_TRIG_NONE         PCI230P2_DAC_TRIG(0)
#define PCI230P2_DAC_TRIG_SW           PCI230P2_DAC_TRIG(1)
#define PCI230P2_DAC_TRIG_EXTP         PCI230P2_DAC_TRIG(2)
#define PCI230P2_DAC_TRIG_EXTN         PCI230P2_DAC_TRIG(3)
#define PCI230P2_DAC_TRIG_Z2CT0        PCI230P2_DAC_TRIG(4)
#define PCI230P2_DAC_TRIG_Z2CT1        PCI230P2_DAC_TRIG(5)
#define PCI230P2_DAC_TRIG_Z2CT2        PCI230P2_DAC_TRIG(6)
#define PCI230P2_DAC_TRIG_MASK         PCI230P2_DAC_TRIG(7)
#define PCI230P2_DAC_FIFO_WRAP         (1U << 7)
#define PCI230P2_DAC_INT_FIFO(x)       (((x) & 7) << 9)
#define PCI230P2_DAC_INT_FIFO_EMPTY    PCI230P2_DAC_INT_FIFO(0)
#define PCI230P2_DAC_INT_FIFO_NEMPTY   PCI230P2_DAC_INT_FIFO(1)
#define PCI230P2_DAC_INT_FIFO_NHALF    PCI230P2_DAC_INT_FIFO(2)
#define PCI230P2_DAC_INT_FIFO_HALF     PCI230P2_DAC_INT_FIFO(3)
#define PCI230P2_DAC_INT_FIFO_NFULL    PCI230P2_DAC_INT_FIFO(4)
#define PCI230P2_DAC_INT_FIFO_FULL     PCI230P2_DAC_INT_FIFO(5)
#define PCI230P2_DAC_INT_FIFO_MASK     PCI230P2_DAC_INT_FIFO(7)
#define PCI230_DAC_BUSY                (1U << 1)
#define PCI230P2_DAC_FIFO_UNDERRUN_LATCHED (1U << 5)
#define PCI230P2_DAC_FIFO_EMPTY        (1U << 13)
#define PCI230P2_DAC_FIFO_FULL         (1U << 14)
#define PCI230P2_DAC_FIFO_HALF         (1U << 15)
#define PCI230P2_DAC_FIFO_UNDERRUN_CLEAR   (1U << 5)
#define PCI230P2_DAC_FIFO_RESET        (1U << 12)
#define PCI230P2_DAC_FIFOLEVEL_HALF    512
#define PCI230P2_DAC_FIFOLEVEL_FULL    1024
#define PCI230P2_DAC_FIFOROOM_EMPTY        PCI230P2_DAC_FIFOLEVEL_FULL
#define PCI230P2_DAC_FIFOROOM_ONETOHALF    (PCI230P2_DAC_FIFOLEVEL_FULL - PCI230P2_DAC_FIFOLEVEL_HALF)
#define PCI230P2_DAC_FIFOROOM_HALFTOFULL   1
#define PCI230P2_DAC_FIFOROOM_FULL         0

#define PCI230_ADC_TRIG(x)             (((x) & 0x7) << 0)
#define PCI230_ADC_TRIG_NONE           PCI230_ADC_TRIG(0)
#define PCI230_ADC_TRIG_SW             PCI230_ADC_TRIG(1)
#define PCI230_ADC_TRIG_EXTP           PCI230_ADC_TRIG(2)
#define PCI230_ADC_TRIG_EXTN           PCI230_ADC_TRIG(3)
#define PCI230_ADC_TRIG_Z2CT0          PCI230_ADC_TRIG(4)
#define PCI230_ADC_TRIG_Z2CT1          PCI230_ADC_TRIG(5)
#define PCI230_ADC_TRIG_Z2CT2          PCI230_ADC_TRIG(6)
#define PCI230_ADC_TRIG_MASK           PCI230_ADC_TRIG(7)
#define PCI230_ADC_IR(x)               (((x) & 0x1) << 3)
#define PCI230_ADC_IR_UNI              PCI230_ADC_IR(0)
#define PCI230_ADC_IR_BIP              PCI230_ADC_IR(1)
#define PCI230_ADC_IR_MASK             PCI230_ADC_IR(1)
#define PCI230_ADC_IM(x)               (((x) & 0x1) << 4)
#define PCI230_ADC_IM_SE               PCI230_ADC_IM(0)
#define PCI230_ADC_IM_DIF              PCI230_ADC_IM(1)
#define PCI230_ADC_IM_MASK             PCI230_ADC_IM(1)
#define PCI230_ADC_FIFO_EN             (1U << 8)
#define PCI230_ADC_INT_FIFO(x)         (((x) & 0x7) << 9)
#define PCI230_ADC_INT_FIFO_EMPTY      PCI230_ADC_INT_FIFO(0)
#define PCI230_ADC_INT_FIFO_NEMPTY     PCI230_ADC_INT_FIFO(1)
#define PCI230_ADC_INT_FIFO_NHALF      PCI230_ADC_INT_FIFO(2)
#define PCI230_ADC_INT_FIFO_HALF       PCI230_ADC_INT_FIFO(3)
#define PCI230_ADC_INT_FIFO_NFULL      PCI230_ADC_INT_FIFO(4)
#define PCI230_ADC_INT_FIFO_FULL       PCI230_ADC_INT_FIFO(5)
#define PCI230P_ADC_INT_FIFO_THRESH    PCI230_ADC_INT_FIFO(7)
#define PCI230_ADC_INT_FIFO_MASK       PCI230_ADC_INT_FIFO(7)
#define PCI230_ADC_FIFO_RESET          (1U << 12)
#define PCI230_ADC_GLOB_RESET          (1U << 13)
#define PCI230_ADC_BUSY                (1U << 15)
#define PCI230_ADC_FIFO_EMPTY          (1U << 12)
#define PCI230_ADC_FIFO_FULL           (1U << 13)
#define PCI230_ADC_FIFO_HALF           (1U << 14)
#define PCI230_ADC_FIFO_FULL_LATCHED   (1U << 5)
#define PCI230_ADC_FIFOLEVEL_HALFFULL  2049
#define PCI230_ADC_FIFOLEVEL_FULL      4096

#define PCI230P_EXTFUNC_GAT_EXTTRIG    (1U << 0)
#define PCI230P2_EXTFUNC_DACFIFO       (1U << 1)

#define PCI230_INT_DISABLE             0
#define PCI230_INT_PPI_C0              (1U << 0)
#define PCI230_INT_PPI_C3              (1U << 1)
#define PCI230_INT_ADC                 (1U << 2)
#define PCI230_INT_ZCLK_CT1            (1U << 5)
#define PCI230P2_INT_DAC               (1U << 4)

#define PCI_VENDOR_ID_AMPLICON        0x14dc
#define PCI_CLASS_OTHERS              0xff
#define I8254_OSC_BASE_10MHZ  100
#define I8254_OSC_BASE_1MHZ   1000
#define I8254_OSC_BASE_100KHZ 10000
#define I8254_OSC_BASE_10KHZ  100000
#define I8254_OSC_BASE_1KHZ   1000000

#define PCI_DEVICE_ID_PCI230   0x0000
#define PCI_DEVICE_ID_PCI260   0x0006

#define PCIBASE_VENDOR_ID      PCI_VENDOR_ID_AMPLICON
#define PCIBASE_DEVICE_ID      PCI_DEVICE_ID_PCI230
#define PCIBASE_CLASS_ID       PCI_CLASS_OTHERS

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

    /* Hardware Register Shadows */
    /* BAR2: PLX / base I/O (we model only INT_SCE/STAT at 0x1e and timer control regs) */
    uint8_t bar2_regs[0x20];

    /* BAR3: DAQ I/O region (16-bit regs) */
    uint16_t daccon;
    uint16_t dacout1;
    uint16_t dacout2;

    uint16_t adcen;
    uint16_t adcg;
    uint16_t adccon;

    uint16_t adc_fifo[4096];
    uint32_t adc_fifo_head;
    uint32_t adc_fifo_tail;
    uint16_t adcfifothresh;

    uint16_t extfunc;
    uint16_t hwver;

    uint16_t dacen;

    /* Simple IRQ state */
    uint8_t ier;      /* interrupt enable mirror (INT_SCE writes) */
    uint8_t istatus;  /* interrupt status (INT_STAT reads) */
};

/* Internal helper for status-triggered signaling. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    if (s->istatus & s->ier) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* Device-initiated DMA logic - not used by this driver, keep empty */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    (void)s;
    (void)is_write;
}

static inline uint16_t pcibase_adc_fifo_level(PCIBaseState *s)
{
    uint32_t level;
    if (s->adc_fifo_head >= s->adc_fifo_tail) {
        level = s->adc_fifo_head - s->adc_fifo_tail;
    } else {
        level = 4096 - (s->adc_fifo_tail - s->adc_fifo_head);
    }
    return (uint16_t)level;
}

static inline uint16_t pcibase_adc_status_bits(PCIBaseState *s)
{
    uint16_t level = pcibase_adc_fifo_level(s);
    uint16_t st = 0;

    if (level == 0) {
        st |= PCI230_ADC_FIFO_EMPTY;
    }
    if (level >= PCI230_ADC_FIFOLEVEL_FULL) {
        st |= PCI230_ADC_FIFO_FULL;
        st |= PCI230_ADC_FIFO_FULL_LATCHED;
    }
    if (level >= PCI230_ADC_FIFOLEVEL_HALFFULL) {
        st |= PCI230_ADC_FIFO_HALF;
    }
    return st;
}

static inline uint16_t pcibase_dac_status_bits(PCIBaseState *s)
{
    /* Very simple DAC FIFO model: always empty when enabled, no underrun */
    uint16_t st = s->daccon & (PCI230_DAC_OR_MASK | PCI230P2_DAC_FIFO_EN |
                               PCI230P2_DAC_TRIG_MASK |
                               PCI230P2_DAC_INT_FIFO_MASK);
    if (s->daccon & PCI230P2_DAC_FIFO_EN) {
        /* pretend FIFO always half/empty, never full */
        st |= PCI230P2_DAC_FIFO_EMPTY;
        st |= PCI230P2_DAC_FIFO_HALF;
    }
    return st;
}

G_GNUC_UNUSED static void pcibase_adc_fifo_push(PCIBaseState *s, uint16_t v)
{
    uint32_t next = (s->adc_fifo_head + 1) & (4096 - 1);
    if (next == s->adc_fifo_tail) {
        /* FIFO overrun: set FULL_LATCHED and drop sample */
        s->adccon |= PCI230_ADC_FIFO_FULL_LATCHED;
        return;
    }
    s->adc_fifo[s->adc_fifo_head] = v;
    s->adc_fifo_head = next;
}

static bool pcibase_adc_fifo_pop(PCIBaseState *s, uint16_t *v)
{
    if (s->adc_fifo_head == s->adc_fifo_tail) {
        return false;
    }
    *v = s->adc_fifo[s->adc_fifo_tail];
    s->adc_fifo_tail = (s->adc_fifo_tail + 1) & (4096 - 1);
    return true;
}

/* MMIO/PIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    /* BAR0/1 are not used by driver in provided snippet */
    (void)s;
    (void)addr;
    (void)size;
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    (void)s;
    (void)addr;
    (void)val;
    (void)size;
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    /* We model BAR2 (index 2) and BAR3 (index 3) in same handler;
     * QEMU passes addr relative to each region separately. We only
     * use offsets that driver actually reads.
     */

    if (size == 1) {
        /* 8-bit accesses: used for INT_STAT/INT_SCE and ZCLK/ZGAT and PPI */
        switch (addr) {
        case PCI230_INT_STAT:
            /* return current pending interrupts (read-only) */
            return s->istatus;
        case PCI230_ZCLK_SCE:
        case PCI230_ZGAT_SCE:
        case PCI230_PPI_X_A:
        case PCI230_PPI_X_B:
        case PCI230_PPI_X_C:
        case PCI230_PPI_X_CMD:
            /* simple shadow in bar2_regs */
            return s->bar2_regs[addr & 0x1f];
        default:
            return 0xff;
        }
    } else if (size == 2) {
        /* 16-bit accesses: DAQ IO (BAR3) */
        uint16_t ret = 0xffff;
        switch (addr) {
        case PCI230_DACCON:
            ret = pcibase_dac_status_bits(s);
            break;
        case PCI230_DACOUT1:
            ret = s->dacout1;
            break;
        case PCI230_DACOUT2:
            ret = s->dacout2;
            break;
        case PCI230_ADCDATA: {
            uint16_t v;
            if (!pcibase_adc_fifo_pop(s, &v)) {
                /* If FIFO empty, just return a stable mid-scale value */
                v = 0x8000;
            }
            ret = v;
            break;
        }
        case PCI230_ADCCON: {
            uint16_t st = s->adccon;
            st |= pcibase_adc_status_bits(s);
            ret = st;
            break;
        }
        case PCI230_ADCEN:
            ret = s->adcen;
            break;
        case PCI230_ADCG:
            ret = s->adcg;
            break;
        case PCI230P_ADCFFTH:
            ret = s->adcfifothresh;
            break;
        case PCI230P_ADCFFLEV:
            ret = pcibase_adc_fifo_level(s);
            break;
        case PCI230P_EXTFUNC:
            ret = s->extfunc;
            break;
        case PCI230P_HWVER:
            ret = s->hwver;
            break;
        case PCI230P2_DACEN:
            ret = s->dacen;
            break;
        default:
            ret = 0xffff;
            break;
        }
        return ret;
    }

    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size == 1) {
        uint8_t v = (uint8_t)val;
        switch (addr) {
        case PCI230_INT_SCE:
            /* Interrupt enable register mirror */
            s->ier = v;
            pcibase_update_irq(s);
            break;
        case PCI230_ZCLK_SCE:
        case PCI230_ZGAT_SCE:
        case PCI230_PPI_X_A:
        case PCI230_PPI_X_B:
        case PCI230_PPI_X_C:
        case PCI230_PPI_X_CMD:
            s->bar2_regs[addr & 0x1f] = v;
            break;
        default:
            break;
        }
        return;
    }

    if (size == 2) {
        uint16_t v = (uint16_t)val;
        switch (addr) {
        case PCI230_DACCON:
            /* handle FIFO reset and underrun clear without inventing timing */
            if (v & PCI230P2_DAC_FIFO_RESET) {
                /* no internal FIFO; nothing to do other than clear flags */
            }
            if (v & PCI230P2_DAC_FIFO_UNDERRUN_CLEAR) {
                s->daccon &= ~PCI230P2_DAC_FIFO_UNDERRUN_LATCHED;
            }
            s->daccon = v & ~(PCI230P2_DAC_FIFO_UNDERRUN_CLEAR | PCI230P2_DAC_FIFO_RESET);
            break;
        case PCI230_DACOUT1:
            s->dacout1 = v;
            break;
        case PCI230_DACOUT2:
            s->dacout2 = v;
            break;
        case PCI230P2_DACDATA:
            /* accept written data, do not store */
            (void)v;
            break;
        /* Removed duplicate case label that conflicted with PCI230P2_DACDATA */
        case PCI230_ADCEN:
            s->adcen = v;
            break;
        case PCI230_ADCG:
            s->adcg = v;
            break;
        case PCI230_ADCCON: {
            /* Handle FIFO reset and start conv source config; no timing */
            if (v & PCI230_ADC_FIFO_RESET) {
                s->adc_fifo_head = s->adc_fifo_tail = 0;
                s->adccon &= ~PCI230_ADC_FIFO_FULL_LATCHED;
            }
            s->adccon = v & ~PCI230_ADC_FIFO_RESET;
            break;
        }
        case PCI230P_ADCFFTH:
            s->adcfifothresh = v;
            break;
        case PCI230P_EXTFUNC:
            s->extfunc = v;
            break;
        default:
            break;
        }
    }
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

    memset(s->bar2_regs, 0, sizeof(s->bar2_regs));

    s->daccon = PCI230_DAC_OR_UNI;
    s->dacout1 = 0;
    s->dacout2 = 0;

    s->adcen = 0;
    s->adcg = 0;
    s->adccon = PCI230_ADC_TRIG_NONE | PCI230_ADC_IM_SE | PCI230_ADC_IR_BIP;
    s->adcfifothresh = 0;

    memset(s->adc_fifo, 0, sizeof(s->adc_fifo));
    s->adc_fifo_head = s->adc_fifo_tail = 0;

    s->extfunc = 0;
    /* emulate a + board with FIFO and DIO available: set hwver >= 2 */
    s->hwver = 2;

    s->dacen = 0;

    s->ier = 0;
    s->istatus = 0;

    pcibase_update_irq(s);
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCIBASE_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCIBASE_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCIBASE_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR Initialization: two I/O regions, like driver expects: index 2 and 3 */
    s->num_bars = 0;

    /* BAR2: PLX / base I/O, at least 0x20 bytes */
    s->bar_info[s->num_bars].index = 2;
    s->bar_info[s->num_bars].type  = BAR_TYPE_PIO;
    s->bar_info[s->num_bars].size  = 0x20;
    s->bar_info[s->num_bars].name  = "pci230-bar2";
    s->num_bars++;

    /* BAR3: DAQ I/O, at least 0x20 bytes (driver checks >=32 for plus model) */
    s->bar_info[s->num_bars].index = 3;
    s->bar_info[s->num_bars].type  = BAR_TYPE_PIO;
    s->bar_info[s->num_bars].size  = 0x20;
    s->bar_info[s->num_bars].name  = "pci230-bar3";
    s->num_bars++;

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize internal state */
    pcibase_reset(DEVICE(pdev));

    (void)errp;
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

    (void)s;
}

static const VMStateDescription vmstate_pcibase = {
    .name = "amplc_pci230_pci",
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

