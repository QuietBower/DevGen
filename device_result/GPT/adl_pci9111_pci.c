/*
 * QEMU PCI device model for ADLINK PCI-9111 (adl_pci9111)
 * Behavioral model sufficient for Linux comedi driver probing and basic I/O.
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

#define TYPE_PCIBASE_DEVICE "adl_pci9111_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define ADL_PCI9111_VENDOR_ID  0x144a
#define ADL_PCI9111_DEVICE_ID  0x9111
#define ADL_PCI9111_CLASS_ID   0xff

#define PCI9111_FIFO_HALF_SIZE                512
#define PCI9111_AI_ACQUISITION_PERIOD_MIN_NS  10000
#define PCI9111_RANGE_SETTING_DELAY           10
#define PCI9111_AI_INSTANT_READ_UDELAY_US     2

#define PCI9111_AI_FIFO_REG           0x00
#define PCI9111_AO_REG                0x00
#define PCI9111_DIO_REG               0x02
#define PCI9111_EDIO_REG              0x04
#define PCI9111_AI_CHANNEL_REG        0x06
#define PCI9111_AI_RANGE_STAT_REG     0x08
#define PCI9111_AI_STAT_AD_BUSY       (1U << 7)
#define PCI9111_AI_STAT_FF_FF         (1U << 6)
#define PCI9111_AI_STAT_FF_HF         (1U << 5)
#define PCI9111_AI_STAT_FF_EF         (1U << 4)
#define PCI9111_AI_RANGE(x)           (((x) & 0x7) << 0)
#define PCI9111_AI_RANGE_MASK         PCI9111_AI_RANGE(7)

#define PCI9111_AI_TRIG_CTRL_REG      0x0a
#define PCI9111_AI_TRIG_CTRL_TRGEVENT (1U << 5)
#define PCI9111_AI_TRIG_CTRL_POTRG    (1U << 4)
#define PCI9111_AI_TRIG_CTRL_PTRG     (1U << 3)
#define PCI9111_AI_TRIG_CTRL_ETIS     (1U << 2)
#define PCI9111_AI_TRIG_CTRL_TPST     (1U << 1)
#define PCI9111_AI_TRIG_CTRL_ASCAN    (1U << 0)

#define PCI9111_INT_CTRL_REG          0x0c
#define PCI9111_INT_CTRL_ISC2         (1U << 3)
#define PCI9111_INT_CTRL_FFEN         (1U << 2)
#define PCI9111_INT_CTRL_ISC1         (1U << 1)
#define PCI9111_INT_CTRL_ISC0         (1U << 0)

#define PCI9111_SOFT_TRIG_REG         0x0e
#define PCI9111_8254_BASE_REG         0x40
#define PCI9111_INT_CLR_REG           0x48

#define PLX9052_INTCSR_LI1STAT        (1U << 2)
#define PLX9052_INTCSR_LI1ENAB        (1U << 0)
#define PLX9052_INTCSR_LI2STAT        (1U << 5)
#define PLX9052_INTCSR_LI2ENAB        (1U << 3)
#define PLX9052_INTCSR_LI2POL         (1U << 4)
#define PLX9052_INTCSR                0x4c
#define PLX9052_INTCSR_PCIENAB        (1U << 6)
#define PLX9052_INTCSR_LI1POL         (1U << 1)

#define PCI9111_LI1_ACTIVE (PLX9052_INTCSR_LI1ENAB | PLX9052_INTCSR_LI1STAT)
#define PCI9111_LI2_ACTIVE (PLX9052_INTCSR_LI2ENAB | PLX9052_INTCSR_LI2STAT)

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

    /* Simple register shadows and state */
    uint16_t ai_fifo[PCI9111_FIFO_HALF_SIZE * 2]; /* simple FIFO buffer */
    unsigned int fifo_head;
    unsigned int fifo_tail;
    uint8_t ai_channel_reg;
    uint8_t ai_range_stat_reg;   /* low bits = range, high bits = status flags */
    uint8_t ai_trig_ctrl_reg;
    uint8_t int_ctrl_reg;
    uint8_t soft_trig_reg;
    uint8_t int_clr_reg;
    uint16_t ao_reg;
    uint16_t dio_reg;
    uint8_t plx_intcsr;

    bool running;          /* AI command running */
    bool fifo_half_event;  /* internal flag for fifo half-full event */
};

static inline bool pcibase_msi_enabled(PCIBaseState *s)
{
    return msi_enabled(PCI_DEVICE(s));
}

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* Interrupt is signaled when:
     * - PLX PCI interrupt enable is set, and
     * - local interrupt 1 is enabled and its status bit is set.
     * We model only LI1 (fifo half-full) as used by the driver.
     */
    bool li1_en = (s->plx_intcsr & PLX9052_INTCSR_LI1ENAB) != 0;
    bool li1_stat = (s->plx_intcsr & PLX9052_INTCSR_LI1STAT) != 0;
    bool pci_en = (s->plx_intcsr & PLX9052_INTCSR_PCIENAB) != 0;

    bool assert = pci_en && li1_en && li1_stat;

    if (pcibase_msi_enabled(s)) {
        if (assert) {
            msi_notify(pdev, 0);
        }
    } else {
        pci_set_irq(pdev, assert ? 1 : 0);
    }
}

/* Simple helper to push sample into FIFO and set status flags. */
static void pcibase_fifo_push_sample(PCIBaseState *s, uint16_t sample)
{
    unsigned int next_head;
    unsigned int fifo_size = PCI9111_FIFO_HALF_SIZE * 2; /* total entries */

    next_head = (s->fifo_head + 1) % fifo_size;
    if (next_head == s->fifo_tail) {
        /* FIFO full; model overflow by not writing new sample */
        s->ai_range_stat_reg &= ~PCI9111_AI_STAT_FF_FF; /* 0 => full / overflow */
        return;
    }

    s->ai_fifo[s->fifo_head] = sample;
    s->fifo_head = next_head;

    /* Update flags for empty/full/half full. We model half-full threshold
     * at PCI9111_FIFO_HALF_SIZE samples.
     */
    unsigned int count = (s->fifo_head + fifo_size - s->fifo_tail) % fifo_size;

    if (count == 0) {
        s->ai_range_stat_reg |= PCI9111_AI_STAT_FF_EF; /* empty */
        s->ai_range_stat_reg |= PCI9111_AI_STAT_FF_FF; /* not full */
        s->ai_range_stat_reg |= PCI9111_AI_STAT_FF_HF; /* not half-full */
    } else if (count >= fifo_size) {
        s->ai_range_stat_reg &= ~PCI9111_AI_STAT_FF_FF; /* full */
        s->ai_range_stat_reg &= ~PCI9111_AI_STAT_FF_EF; /* not empty */
        s->ai_range_stat_reg &= ~PCI9111_AI_STAT_FF_HF; /* half/full flag clear */
    } else {
        s->ai_range_stat_reg &= ~PCI9111_AI_STAT_FF_EF; /* not empty */
        s->ai_range_stat_reg |= PCI9111_AI_STAT_FF_FF;  /* not full */
        if (count >= PCI9111_FIFO_HALF_SIZE) {
            s->ai_range_stat_reg &= ~PCI9111_AI_STAT_FF_HF; /* 0 => half-full */
        } else {
            s->ai_range_stat_reg |= PCI9111_AI_STAT_FF_HF; /* 1 => not half-full */
        }
    }

    /* Generate fifo half-full interrupt when enabled. */
    if (count >= PCI9111_FIFO_HALF_SIZE) {
        s->plx_intcsr |= PLX9052_INTCSR_LI1STAT;
        pcibase_update_irq(s);
    }
}

/* Pop sample from FIFO, update flags. */
static uint16_t pcibase_fifo_pop_sample(PCIBaseState *s)
{
    unsigned int fifo_size = PCI9111_FIFO_HALF_SIZE * 2;

    if (s->fifo_head == s->fifo_tail) {
        /* Empty, return mid-scale sample. Keep empty flag set. */
        s->ai_range_stat_reg |= PCI9111_AI_STAT_FF_EF;
        return 0x8000;
    }

    uint16_t v = s->ai_fifo[s->fifo_tail];
    s->fifo_tail = (s->fifo_tail + 1) % fifo_size;

    unsigned int count = (s->fifo_head + fifo_size - s->fifo_tail) % fifo_size;
    if (count == 0) {
        s->ai_range_stat_reg |= PCI9111_AI_STAT_FF_EF; /* empty */
        s->ai_range_stat_reg |= PCI9111_AI_STAT_FF_FF; /* not full */
        s->ai_range_stat_reg |= PCI9111_AI_STAT_FF_HF; /* not half-full */
        s->plx_intcsr &= ~PLX9052_INTCSR_LI1STAT;
        pcibase_update_irq(s);
    } else {
        s->ai_range_stat_reg &= ~PCI9111_AI_STAT_FF_EF; /* not empty */
        s->ai_range_stat_reg |= PCI9111_AI_STAT_FF_FF;  /* not full */
        if (count >= PCI9111_FIFO_HALF_SIZE) {
            s->ai_range_stat_reg &= ~PCI9111_AI_STAT_FF_HF; /* half-full */
        } else {
            s->ai_range_stat_reg |= PCI9111_AI_STAT_FF_HF; /* not half-full */
            /* clear li1 status when dropping below half */
            s->plx_intcsr &= ~PLX9052_INTCSR_LI1STAT;
            pcibase_update_irq(s);
        }
    }

    return v;
}

/* Device-initiated DMA logic not used by this driver; left unimplemented. */

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case PCI9111_AI_FIFO_REG: /* 0x00, 16-bit read */
        if (size == 2) {
            val = pcibase_fifo_pop_sample(s);
        }
        break;
    case PCI9111_DIO_REG: /* 0x02 */
        if (size == 2) {
            val = s->dio_reg;
        }
        break;
    case PCI9111_EDIO_REG: /* 0x04 */
        if (size == 2) {
            val = 0x0000; /* not used explicitly by driver */
        }
        break;
    case PCI9111_AI_CHANNEL_REG: /* 0x06 */
        if (size == 1) {
            val = s->ai_channel_reg;
        }
        break;
    case PCI9111_AI_RANGE_STAT_REG: /* 0x08 */
        if (size == 1) {
            val = s->ai_range_stat_reg;
        }
        break;
    case PCI9111_AI_TRIG_CTRL_REG: /* 0x0a */
        if (size == 1) {
            val = s->ai_trig_ctrl_reg;
        }
        break;
    case PCI9111_INT_CTRL_REG: /* 0x0c */
        if (size == 1) {
            val = s->int_ctrl_reg;
        }
        break;
    case PCI9111_SOFT_TRIG_REG: /* 0x0e */
        if (size == 1) {
            val = s->soft_trig_reg;
        }
        break;
    case PCI9111_8254_BASE_REG: /* 0x40 timer base - driver only uses via comedi_8254, not direct */
        val = 0;
        break;
    case PCI9111_INT_CLR_REG: /* 0x48 */
        if (size == 1) {
            val = s->int_clr_reg;
        }
        break;
    case PLX9052_INTCSR: /* 0x4c PLX interrupt control/status */
        if (size == 1) {
            val = s->plx_intcsr;
        }
        break;
    default:
        /* unmapped register, return 0 */
        val = 0;
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case PCI9111_AO_REG: /* 0x00, 16-bit */
        if (size == 2) {
            s->ao_reg = (uint16_t)val;
        }
        break;
    case PCI9111_DIO_REG: /* 0x02, 16-bit */
        if (size == 2) {
            s->dio_reg = (uint16_t)val;
        }
        break;
    case PCI9111_EDIO_REG: /* 0x04 */
        /* driver does not explicitly use */
        break;
    case PCI9111_AI_CHANNEL_REG: /* 0x06, 8-bit */
        if (size == 1) {
            s->ai_channel_reg = (uint8_t)val;
        }
        break;
    case PCI9111_AI_RANGE_STAT_REG: /* 0x08, 8-bit write for range */
        if (size == 1) {
            /* preserve status bits, update only range bits. */
            s->ai_range_stat_reg &= ~PCI9111_AI_RANGE_MASK;
            s->ai_range_stat_reg |= (uint8_t)(val & PCI9111_AI_RANGE_MASK);
        }
        break;
    case PCI9111_AI_TRIG_CTRL_REG: /* 0x0a */
        if (size == 1) {
            s->ai_trig_ctrl_reg = (uint8_t)val;
            /* Starting/stopping acquisition is controlled here. We model
             * only basic state: when non-zero with TPST or ETIS, mark running.
             */
            if (s->ai_trig_ctrl_reg & (PCI9111_AI_TRIG_CTRL_TPST | PCI9111_AI_TRIG_CTRL_ETIS)) {
                s->running = true;
            } else {
                s->running = false;
            }
        }
        break;
    case PCI9111_INT_CTRL_REG: /* 0x0c */
        if (size == 1) {
            /* FFEN sequence 0->1->0 resets the FIFO in driver, so if FFEN
             * bit set on a non-zero write, and then cleared, the driver calls
             * pci9111_fifo_reset(). Here we just store the value; actual
             * fifo reset is invoked by writes that match the driver's calls.
             */
            s->int_ctrl_reg = (uint8_t)val;
        }
        break;
    case PCI9111_SOFT_TRIG_REG: /* 0x0e */
        if (size == 1) {
            s->soft_trig_reg = (uint8_t)val;
            /* For instantaneous read, driver writes 0 then polls EF bit
             * and finally reads a sample from FIFO. Emulate by generating
             * one sample and putting it into FIFO.
             */
            uint16_t sample = 0x8000; /* midscale value */
            pcibase_fifo_push_sample(s, sample);
            /* flags are already updated by pcibase_fifo_push_sample() */
        }
        break;
    case PCI9111_INT_CLR_REG: /* 0x48, 8-bit */
        if (size == 1) {
            /* Writing 0 clears interrupt sources in driver. We clear LI1STAT. */
            s->int_clr_reg = (uint8_t)val;
            s->plx_intcsr &= ~PLX9052_INTCSR_LI1STAT;
            pcibase_update_irq(s);
        }
        break;
    case PLX9052_INTCSR: /* 0x4c, 8-bit */
        if (size == 1) {
            /* Driver writes flags calculated in plx9050_interrupt_control().
             * Bits: enables and polarity. We simply store them and use
             * LI1ENAB and PCIENAB, LI1STAT is managed internally.
             */
            uint8_t new_val = (uint8_t)val;
            /* Preserve LI1STAT bit when writing (status is W1C in real HW,
             * but driver never writes it directly). We'll keep our own.
             */
            uint8_t keep = s->plx_intcsr & PLX9052_INTCSR_LI1STAT;
            s->plx_intcsr = (new_val & ~PLX9052_INTCSR_LI1STAT) | keep;
            pcibase_update_irq(s);
        }
        break;
    default:
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* This device does not use a separate PIO BAR in the provided driver; all
     * accesses are via dev->iobase which is modeled as MMIO BAR2.
     */
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* No separate PIO behavior needed. */
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
    PCIDevice *pdev = PCI_DEVICE(dev);
    pci_device_reset(pdev);

    s->fifo_head = 0;
    s->fifo_tail = 0;
    s->ai_channel_reg = 0;
    /* default range 0, and FIFO empty+not full+not half-full */
    s->ai_range_stat_reg = PCI9111_AI_STAT_FF_EF | PCI9111_AI_STAT_FF_FF | PCI9111_AI_STAT_FF_HF;
    s->ai_trig_ctrl_reg = 0;
    s->int_ctrl_reg = 0;
    s->soft_trig_reg = 0;
    s->int_clr_reg = 0;
    s->ao_reg = 0;
    s->dio_reg = 0;
    s->plx_intcsr = 0;
    s->running = false;
    s->fifo_half_event = false;

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

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  ADL_PCI9111_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  ADL_PCI9111_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, ADL_PCI9111_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Enable legacy INTx; MSI/MSI-X are not used by this driver. */
    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR layout: follows driver usage:
     *  BAR1: PLX local configuration registers (we emulate only INTCSR at 0x4c)
     *  BAR2: main device registers (AI/DI/DO/AO, etc.)
     */
    s->num_bars = 2;

    s->bar_info[0].index = 0;
    s->bar_info[0].type  = BAR_TYPE_NONE;
    s->bar_info[0].size  = 0;
    s->bar_info[0].name  = "unused";

    s->bar_info[1].index = 1;
    s->bar_info[1].type  = BAR_TYPE_MMIO;
    s->bar_info[1].size  = 0x100; /* small PLX area; we use only INTCSR at 0x4c */
    s->bar_info[1].name  = "adl_pci9111_plx";

    s->bar_info[2].index = 2;
    s->bar_info[2].type  = BAR_TYPE_MMIO;
    s->bar_info[2].size  = 0x100; /* main registers up to INTCSR */
    s->bar_info[2].name  = "adl_pci9111_regs";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i + 1], errp);
    }

    s->has_msi = false;
    s->has_msix = false;

    pcibase_reset(DEVICE(pdev));
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
    .name = "adl_pci9111_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT16_ARRAY(ai_fifo, PCIBaseState, PCI9111_FIFO_HALF_SIZE * 2),
        VMSTATE_UINT32(fifo_head, PCIBaseState),
        VMSTATE_UINT32(fifo_tail, PCIBaseState),
        VMSTATE_UINT8(ai_channel_reg, PCIBaseState),
        VMSTATE_UINT8(ai_range_stat_reg, PCIBaseState),
        VMSTATE_UINT8(ai_trig_ctrl_reg, PCIBaseState),
        VMSTATE_UINT8(int_ctrl_reg, PCIBaseState),
        VMSTATE_UINT8(soft_trig_reg, PCIBaseState),
        VMSTATE_UINT8(int_clr_reg, PCIBaseState),
        VMSTATE_UINT16(ao_reg, PCIBaseState),
        VMSTATE_UINT16(dio_reg, PCIBaseState),
        VMSTATE_UINT8(plx_intcsr, PCIBaseState),
        VMSTATE_BOOL(running, PCIBaseState),
        VMSTATE_BOOL(fifo_half_event, PCIBaseState),
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

type_init(pcibase_register_types)

