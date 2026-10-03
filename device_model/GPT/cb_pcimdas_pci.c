/*
 * QEMU PCI device model for cb_pcimdas (simplified, register-level)
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

/* Define missing vendor ID locally (from driver context) */
#define PCI_VENDOR_ID_CB 0x1307

#define TYPE_PCIBASE_DEVICE "cb_pcimdas_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define CB_PCIMDAS_VENDOR_ID PCI_VENDOR_ID_CB
#define CB_PCIMDAS_DEVICE_ID 0x0056
#define CB_PCIMDAS_CLASS_ID  PCI_CLASS_OTHERS

#define PCIMDAS_AI_REG                  0x00
#define PCIMDAS_AI_SOFTTRIG_REG         0x00
#define PCIMDAS_AO_REG(x)               (0x02 + ((x) * 2))
#define PCIMDAS_MUX_REG                 0x00
#define PCIMDAS_MUX(_lo, _hi)           ((_lo) | ((_hi) << 4))
#define PCIMDAS_DI_DO_REG               0x01
#define PCIMDAS_STATUS_REG              0x02
#define PCIMDAS_STATUS_EOC              (1U << 7)
#define PCIMDAS_STATUS_UB               (1U << 6)
#define PCIMDAS_STATUS_MUX              (1U << 5)
#define PCIMDAS_STATUS_CLK              (1U << 4)
#define PCIMDAS_STATUS_TO_CURR_MUX(x)   ((x) & 0xf)
#define PCIMDAS_CONV_STATUS_REG         0x03
#define PCIMDAS_CONV_STATUS_EOC         (1U << 7)
#define PCIMDAS_CONV_STATUS_EOB         (1U << 6)
#define PCIMDAS_CONV_STATUS_EOA         (1U << 5)
#define PCIMDAS_CONV_STATUS_FNE         (1U << 4)
#define PCIMDAS_CONV_STATUS_FHF         (1U << 3)
#define PCIMDAS_CONV_STATUS_OVERRUN     (1U << 2)
#define PCIMDAS_IRQ_REG                 0x04
#define PCIMDAS_IRQ_INTE                (1U << 7)
#define PCIMDAS_IRQ_INT                 (1U << 6)
#define PCIMDAS_IRQ_OVERRUN             (1U << 4)
#define PCIMDAS_IRQ_EOA                 (1U << 3)
#define PCIMDAS_IRQ_EOA_INT_SEL         (1U << 2)
#define PCIMDAS_IRQ_INTSEL(x)           ((x) << 0)
#define PCIMDAS_IRQ_INTSEL_EOC          PCIMDAS_IRQ_INTSEL(0)
#define PCIMDAS_IRQ_INTSEL_FNE          PCIMDAS_IRQ_INTSEL(1)
#define PCIMDAS_IRQ_INTSEL_EOB          PCIMDAS_IRQ_INTSEL(2)
#define PCIMDAS_IRQ_INTSEL_FHF_EOA      PCIMDAS_IRQ_INTSEL(3)
#define PCIMDAS_PACER_REG               0x05
#define PCIMDAS_PACER_GATE_STATUS       (1U << 6)
#define PCIMDAS_PACER_GATE_POL          (1U << 5)
#define PCIMDAS_PACER_GATE_LATCH        (1U << 4)
#define PCIMDAS_PACER_GATE_EN           (1U << 3)
#define PCIMDAS_PACER_EXT_PACER_POL     (1U << 2)
#define PCIMDAS_PACER_SRC(x)            ((x) << 0)
#define PCIMDAS_PACER_SRC_POLLED        PCIMDAS_PACER_SRC(0)
#define PCIMDAS_PACER_SRC_EXT           PCIMDAS_PACER_SRC(2)
#define PCIMDAS_PACER_SRC_INT           PCIMDAS_PACER_SRC(3)
#define PCIMDAS_PACER_SRC_MASK          (3U << 0)
#define PCIMDAS_BURST_REG               0x06
#define PCIMDAS_BURST_BME               (1U << 1)
#define PCIMDAS_BURST_CONV_EN           (1U << 0)
#define PCIMDAS_GAIN_REG                0x07
#define PCIMDAS_8254_BASE               0x08
#define PCIMDAS_USER_CNTR_REG           0x0c
#define PCIMDAS_USER_CNTR_CTR1_CLK_SEL  (1U << 0)
#define PCIMDAS_RESIDUE_MSB_REG         0x0d
#define PCIMDAS_RESIDUE_LSB_REG         0x0e
#define PCIMDAS_8255_BASE               0x00

/* simple BAR definition based on driver usage:
 * BADR3 is used for inb/outb to the small register block (status, pacer,
 * DI/DO, 8254 etc.).
 * daqio is used for inw/outw to AI and AO registers.
 * The driver also sets dev->iobase to BAR4 for other comedi helpers;
 * we just expose another small IO BAR.
 */

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

    /* BADR3-like IO register shadow (8-bit accesses) */
    uint8_t status_reg;          /* PCIMDAS_STATUS_REG (0x02) */
    uint8_t conv_status_reg;     /* PCIMDAS_CONV_STATUS_REG (0x03) */
    uint8_t irq_reg;             /* PCIMDAS_IRQ_REG (0x04) */
    uint8_t pacer_reg;           /* PCIMDAS_PACER_REG (0x05) */
    uint8_t burst_reg;           /* PCIMDAS_BURST_REG (0x06) */
    uint8_t gain_reg;            /* PCIMDAS_GAIN_REG (0x07) */
    uint8_t di_do_reg;           /* PCIMDAS_DI_DO_REG (0x01) */
    uint8_t user_cntr_reg;       /* PCIMDAS_USER_CNTR_REG (0x0c) */
    uint8_t residue_msb;         /* PCIMDAS_RESIDUE_MSB_REG (0x0d) */
    uint8_t residue_lsb;         /* PCIMDAS_RESIDUE_LSB_REG (0x0e) */

    /* daqio-like region (16-bit accesses) */
    uint16_t ai_reg;             /* PCIMDAS_AI_REG (0x00) */
    uint16_t ao_reg[2];          /* PCIMDAS_AO_REG(0/1) */

    /* configuration from "board switches" as seen via STATUS_REG */
    bool ai_se;                  /* single-ended vs differential */
    bool ai_uni;                 /* unipolar vs bipolar */
    bool pacer_10mhz;            /* CLK bit */

    /* simple conversion timer */
    QEMUTimer *conv_timer;
    bool conv_in_progress;
};


/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* Driver code provided does not enable or handle interrupts for this
     * board, so we keep interrupts deasserted. Implementing a basic
     * INT bit in irq_reg for completeness if needed later.
     */
    if (s->irq_reg & PCIMDAS_IRQ_INT) {
        if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!msi_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

/* Device-initiated DMA logic based on driver access patterns
 * The provided driver code does not program any DMA engines for this
 * board, so no DMA behavior is implemented.
 */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    (void)s;
    (void)is_write;
}

/* conversion complete timer callback: clear EOC bit */
static void pcibase_conv_timer_cb(void *opaque)
{
    PCIBaseState *s = opaque;

    s->conv_in_progress = false;
    s->status_reg &= ~PCIMDAS_STATUS_EOC;
}

/* Helpers to map BAR index to register set semantics.
 * We decide:
 *   BAR2 (index 2): daqio (16-bit AI/AO) - PIO
 *   BAR3 (index 3): BADR3 block (8-bit control/status/8254 base) - PIO
 *   BAR4 (index 4): dummy IO for dev->iobase - PIO
 */

G_GNUC_UNUSED static bool pcibase_is_badr3_bar(PCIBaseState *s, MemoryRegion *mr)
{
    return mr == &s->bar_regions[3];
}

G_GNUC_UNUSED static bool pcibase_is_daqio_bar(PCIBaseState *s, MemoryRegion *mr)
{
    return mr == &s->bar_regions[2];
}

G_GNUC_UNUSED static bool pcibase_is_iobase_bar(PCIBaseState *s, MemoryRegion *mr)
{
    return mr == &s->bar_regions[4];
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    (void)opaque;
    (void)addr;
    uint64_t val = 0;

    /* This device model does not use MMIO BARs in the driver context.
     * Return 0 for all MMIO reads.
     */
    switch (size) {
    case 1:
        val = 0;
        break;
    case 2:
        val = 0;
        break;
    case 4:
        val = 0;
        break;
    default:
        val = 0;
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    (void)opaque;
    (void)addr;
    (void)val;
    (void)size;
    /* No MMIO functionality used by the driver. */
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Identify which BAR this IO access targets by checking addr ranges.
     * QEMU passes addr relative to the MemoryRegion, so we instead rely on
     * which ops structure is used. Since all PIO BARs share the same ops,
     * we must differentiate by address ranges. But QEMU separates regions,
     * so here 'addr' is per-region. We'll distinguish by region size
     * indirectly using known register maps:
     *  - For BAR2 (daqio): valid offsets are 0x00 (AI) and 0x02/0x04 (AO0/1),
     *    accessed with size=2.
     *  - For BAR3 (BADR3): small block up to 0x0e.
     *  - For BAR4: generic IO, we just return 0.
     *
     * Because this callback doesn't directly know the MemoryRegion, we rely
     * on QEMU creating separate MemoryRegionOps per BAR index. However, the
     * template uses a single ops instance. To still emulate driver-visible
     * behavior without modifying template, we simply assume:
     *  - daqio (BAR2) uses 16-bit accesses only to offsets 0,2,4.
     *  - BADR3 (BAR3) uses 8-bit accesses to small offsets including 0x02.
     *  - dev->iobase (BAR4) is not used directly by this driver code except
     *    through helper libraries that we cannot fully emulate, so we return 0.
     *
     * We approximate by using the access size: size==2 => daqio, size==1
     * and small offset => BADR3. This is safe because driver uses inw/outw
     * for daqio and inb/outb for BADR3.
     */

    if (size == 2) {
        /* Treat as daqio region */
        switch (addr) {
        case PCIMDAS_AI_REG: /* 0x00 */
            val = s->ai_reg;
            break;
        case PCIMDAS_AO_REG(0):
            val = s->ao_reg[0];
            break;
        case PCIMDAS_AO_REG(1):
            val = s->ao_reg[1];
            break;
        default:
            val = 0;
            break;
        }
        return val;
    }

    if (size == 4) {
        /* 8254 helper may use inl/outl via i8254_io32_cb on dev->pacer
         * with base BADR3 + 0x08, so handle 32-bit reads there by composing
         * bytes from the 8-bit/16-bit spaces. The driver does provide
         * implementations for i8254_io32_cb/inl/outl referencing iobase
         * given to comedi_8254_io_alloc, which is BADR3 + 0x08.
         * For simplicity, we just return 0 for 32-bit reads.
         */
        return 0;
    }

    /* size == 1 -> BADR3 or generic IO */
    /* We do not know which BAR this is, but driver only performs 8-bit
     * on BADR3, so we interpret all 8-bit PIO as BADR3 register block.
     */

    switch (addr) {
    case PCIMDAS_DI_DO_REG: /* 0x01 */
        val = s->di_do_reg;
        break;
    case PCIMDAS_STATUS_REG: /* 0x02 */
        val = s->status_reg;
        break;
    case PCIMDAS_CONV_STATUS_REG: /* 0x03 */
        val = s->conv_status_reg;
        break;
    case PCIMDAS_IRQ_REG: /* 0x04 */
        val = s->irq_reg;
        break;
    case PCIMDAS_PACER_REG: /* 0x05 */
        val = s->pacer_reg;
        break;
    case PCIMDAS_BURST_REG: /* 0x06 */
        val = s->burst_reg;
        break;
    case PCIMDAS_GAIN_REG: /* 0x07 */
        val = s->gain_reg;
        break;
    case PCIMDAS_8254_BASE + 0: /* counter 0 */
    case PCIMDAS_8254_BASE + 1: /* counter 1 */
    case PCIMDAS_8254_BASE + 2: /* counter 2 */
    case PCIMDAS_8254_BASE + 3: /* control */
        /* We do not emulate full 8254; just return 0xff */
        val = 0xff;
        break;
    case PCIMDAS_USER_CNTR_REG: /* 0x0c */
        val = s->user_cntr_reg;
        break;
    case PCIMDAS_RESIDUE_MSB_REG: /* 0x0d */
        val = s->residue_msb;
        break;
    case PCIMDAS_RESIDUE_LSB_REG: /* 0x0e */
        val = s->residue_lsb;
        break;
    default:
        val = 0;
        break;
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size == 2) {
        /* daqio region: 16-bit writes for AO, AI trigger */
        switch (addr) {
        case PCIMDAS_AI_SOFTTRIG_REG: /* 0x00 */
            /* Trigger a single conversion for software-initiated read.
             * Driver sequence:
             *   outw(0, daqio + AI_SOFTTRIG_REG);
             *   wait for EOC to clear via cb_pcimdas_ai_eoc which reads
             *   STATUS_REG and expects STATUS_EOC to be 0 when done.
             * We emulate by setting EOC, starting timer, and clearing EOC
             * after a very small delay. Also produce a dummy AI sample.
             */
            if (!s->conv_in_progress) {
                s->conv_in_progress = true;
                s->status_reg |= PCIMDAS_STATUS_EOC;
                /* simple deterministic sample; could depend on gain/mux */
                s->ai_reg = 0x8000;
                /* schedule completion shortly */
                timer_mod(s->conv_timer,
                          qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                          NANOSECONDS_PER_SECOND / 1000); /* 1us approx */
            }
            break;
        case PCIMDAS_AO_REG(0):
            s->ao_reg[0] = (uint16_t)val;
            break;
        case PCIMDAS_AO_REG(1):
            s->ao_reg[1] = (uint16_t)val;
            break;
        default:
            break;
        }
        return;
    }

    if (size == 4) {
        /* Used by i8254_io32_cb via outl; we ignore content. */
        return;
    }

    /* 8-bit writes -> BADR3 block */
    switch (addr) {
    case PCIMDAS_DI_DO_REG: /* 0x01 */
        /* DO subdevice: lower 4 bits are outputs; we store full byte */
        s->di_do_reg = (uint8_t)val;
        break;
    case PCIMDAS_STATUS_REG: /* 0x02 */
        /* STATUS is mostly read-only switches. Ignore writes. */
        break;
    case PCIMDAS_CONV_STATUS_REG: /* 0x03 */
        /* Not used by driver; ignore. */
        s->conv_status_reg = (uint8_t)val;
        break;
    case PCIMDAS_IRQ_REG: /* 0x04 */
        /* Interrupt configuration; driver does not appear to use it; we
         * just store the value and update IRQ line.
         */
        s->irq_reg = (uint8_t)val;
        pcibase_update_irq(s);
        break;
    case PCIMDAS_PACER_REG: /* 0x05 */
        /* Driver reads-modifies this to select PACER_SRC_POLLED. */
        s->pacer_reg = (uint8_t)val;
        break;
    case PCIMDAS_BURST_REG: /* 0x06 */
        /* Driver sets BURST_CONV_EN for conversions. */
        s->burst_reg = (uint8_t)val;
        break;
    case PCIMDAS_GAIN_REG: /* 0x07 */
        /* Range selection for AI */
        s->gain_reg = (uint8_t)val;
        break;
    case PCIMDAS_8254_BASE + 0:
    case PCIMDAS_8254_BASE + 1:
    case PCIMDAS_8254_BASE + 2:
    case PCIMDAS_8254_BASE + 3:
        /* Ignore writes to 8254 counters/control; comedi library handles
         * behavior, while here we just act as a dumb IO sink/source.
         */
        break;
    case PCIMDAS_USER_CNTR_REG: /* 0x0c */
        /* Only bit 0 used to select clock source for counter 1. */
        s->user_cntr_reg = (uint8_t)val;
        break;
    case PCIMDAS_RESIDUE_MSB_REG: /* 0x0d */
        s->residue_msb = (uint8_t)val;
        break;
    case PCIMDAS_RESIDUE_LSB_REG: /* 0x0e */
        s->residue_lsb = (uint8_t)val;
        break;
    default:
        break;
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

    /* Revert registers to power-on defaults consistent with driver
     * expectations for status bits representing board switches.
     */

    /* Set dummy switch positions: single-ended, bipolar, 1MHz clock. */
    s->ai_se = true;
    s->ai_uni = false;
    s->pacer_10mhz = false;

    s->status_reg = 0;
    if (s->ai_uni) {
        s->status_reg |= PCIMDAS_STATUS_UB;
    }
    if (s->ai_se) {
        s->status_reg |= PCIMDAS_STATUS_MUX;
    }
    if (s->pacer_10mhz) {
        s->status_reg |= PCIMDAS_STATUS_CLK;
    }

    s->status_reg &= ~PCIMDAS_STATUS_EOC;

    s->conv_status_reg = 0;
    s->irq_reg = 0;
    s->pacer_reg = PCIMDAS_PACER_SRC_POLLED;
    s->burst_reg = 0;
    s->gain_reg = 0;
    s->di_do_reg = 0; /* DI bits default 0, DO outputs low. */
    s->user_cntr_reg = 0; /* external clock by default */
    s->residue_msb = 0;
    s->residue_lsb = 0;

    s->ai_reg = 0;
    s->ao_reg[0] = 0;
    s->ao_reg[1] = 0;

    s->conv_in_progress = false;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  CB_PCIMDAS_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  CB_PCIMDAS_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CB_PCIMDAS_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization: set up three IO BARs to align with driver usage. */
    s->num_bars = 3;

    /* BAR2: daqio region (AI/AO, 16-bit). Size: small, cover 0-0x10. */
    s->bar_info[0].index = 2;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 0x10;
    s->bar_info[0].name = "cb_pcimdas_daqio";

    /* BAR3: BADR3 region (status, pacer, DI/DO, 8254 base etc.). */
    s->bar_info[1].index = 3;
    s->bar_info[1].type = BAR_TYPE_PIO;
    s->bar_info[1].size = 0x20;
    s->bar_info[1].name = "cb_pcimdas_badr3";

    /* BAR4: generic IO base used by comedi core as dev->iobase. */
    s->bar_info[2].index = 4;
    s->bar_info[2].type = BAR_TYPE_PIO;
    s->bar_info[2].size = 0x20;
    s->bar_info[2].name = "cb_pcimdas_iobase";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize conversion timer */
    s->conv_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, pcibase_conv_timer_cb, s);

    /* No MSI/MSI-X explicitly used in driver, so do not enable by default. */

    /* Final state initialization before the device is 'live' */
    pcibase_reset(DEVICE(pdev));
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

    if (s->conv_timer) {
        timer_del(s->conv_timer);
        timer_free(s->conv_timer);
        s->conv_timer = NULL;
    }

    /* Free buffers, stop timers, etc. */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "cb_pcimdas_pci",
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

