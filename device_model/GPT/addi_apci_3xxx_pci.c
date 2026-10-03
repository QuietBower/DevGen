/*
 * QEMU PCI device model for ADDI-DATA APCI-3xxx family
 *
 * This file is auto-generated from the Linux comedi driver
 * drivers/comedi/drivers/addi_apci_3xxx.c
 *
 * Target QEMU: 8.2.10
 */

#include "qemu/osdep.h"
#include <inttypes.h>
#include <string.h>
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "sysemu/dma.h"
#include "sysemu/reset.h"
#include "hw/irq.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/pci/pcie.h"
#include "qom/object.h"
#include "qapi/visitor.h"
#include "hw/qdev-properties.h"

#define TYPE_PCIBASE_DEVICE "addi_apci_3xxx_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define APCI3XXX_VENDOR_ID 0x15B8
#define APCI3XXX_DEVICE_ID 0x3010
#define APCI3XXX_CLASS_ID  PCI_CLASS_OTHERS

#define PCI_CLASS_OTHERS 0xff

/* BAR indices used by the Linux driver:
 *  - BAR2: I/O port region (dev->iobase, inl/outl)
 *  - BAR3: MMIO region   (dev->mmio,  readl/writel)
 */

/* Simple BAR type enum */
typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM
} BARType;

typedef struct {
    int index;
    BARType type;
    hwaddr size;
    const char *name;
    bool sparse;
} BARInfo;

/* ------------------------------------------------------------------ */
/* Device State                                                        */
/* ------------------------------------------------------------------ */
struct PCIBaseState {
    PCIDevice parent_obj;

    /* BAR memory regions (MMIO/PIO) */
    MemoryRegion bar_regions[6];

    /* optional linear backing for MMIO (not strictly needed here) */
    uint8_t *mmio_backing;
    size_t mmio_backing_size;

    /* BAR table */
    BARInfo bar_info[6];
    int num_bars;

    /* interrupt feature flags */
    bool has_msi;
    bool has_msix;

    /* simple register shadows for MMIO BAR3 */
    uint32_t reg_cfg0;      /* offset 0  - AI config/chan select */
    uint32_t reg_delay;     /* offset 4  - delay/mode */
    uint32_t reg_start;     /* offset 8  - start/convert control */
    uint32_t reg_fifo_ctrl; /* offset 12 - FIFO clear */
    uint32_t reg_irq_status;/* offset 16 - IRQ status */
    uint32_t reg_eos;       /* offset 20 - EOS status */
    uint32_t reg_fifo_data; /* offset 28 - AI data */
    uint32_t reg_conv_timer;/* offset 32 - convert timer */
    uint32_t reg_conv_unit; /* offset 36 - convert unit */
    uint32_t reg_seq_cnt;   /* offset 48 - sequence count */

    /* AO registers */
    uint32_t reg_ao_status; /* offset 96 - AO status/range */
    uint32_t reg_ao_data;   /* offset 100 - AO data/chan */

    /* Simple DI/DO/DIO state for I/O BAR2 */
    uint32_t io_di_reg32;   /* offset 32 - 4-bit DI */
    uint32_t io_do_reg48;   /* offset 48 - 4-bit DO */

    uint32_t io_dio_out80;  /* offset 80 - TTL port0 out */
    uint32_t io_dio_in64;   /* offset 64 - TTL port1 in */
    uint32_t io_dio_out112; /* offset 112 - TTL port2 out */
    uint32_t io_dio_in96;   /* offset 96 - TTL port2 in (when input) */
    uint32_t io_dio_dir224; /* offset 224 - TTL port2 direction */

    /* FIFO buffer for AI values (very simple depth-16 buffer) */
    uint32_t fifo[16];
    int fifo_head;
    int fifo_tail;
    int fifo_count;

    /* whether an AI conversion is currently running (bit 0x80000 of reg_start) */
    bool ai_running;

    /* QEMU IRQ line */
    qemu_irq irq;
};

/* ------------------------------------------------------------------ */
/* Forward declarations                                                */
/* ------------------------------------------------------------------ */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len);
static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len);
static void pcibase_reset(DeviceState *dev);
static void pcibase_realize(PCIDevice *pdev, Error **errp);
static void pcibase_uninit(PCIDevice *pdev);

/* ------------------------------------------------------------------ */
/* MemoryRegionOps                                                     */
/* ------------------------------------------------------------------ */
static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* ------------------------------------------------------------------ */
/* BAR registration helper                                             */
/* ------------------------------------------------------------------ */
static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    MemoryRegion *mr;

    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    mr = &s->bar_regions[bi->index];

    switch (bi->type) {
    case BAR_TYPE_MMIO:
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s,
                              bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
        break;
    case BAR_TYPE_PIO:
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s,
                              bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
        break;
    case BAR_TYPE_RAM:
        memory_region_init_ram(mr, OBJECT(s), bi->name, bi->size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* FIFO helpers                                                        */
/* ------------------------------------------------------------------ */
static void apci3xxx_fifo_reset(PCIBaseState *s)
{
    s->fifo_head = 0;
    s->fifo_tail = 0;
    s->fifo_count = 0;
}

static void apci3xxx_fifo_push(PCIBaseState *s, uint32_t v)
{
    if (s->fifo_count >= 16) {
        return;
    }
    s->fifo[s->fifo_head] = v;
    s->fifo_head = (s->fifo_head + 1) & 0x0f;
    s->fifo_count++;
}

static uint32_t apci3xxx_fifo_pop(PCIBaseState *s)
{
    uint32_t v = 0;
    if (s->fifo_count <= 0) {
        return 0;
    }
    v = s->fifo[s->fifo_tail];
    s->fifo_tail = (s->fifo_tail + 1) & 0x0f;
    s->fifo_count--;
    return v;
}

/* ------------------------------------------------------------------ */
/* IRQ helpers                                                         */
/* ------------------------------------------------------------------ */
static void apci3xxx_update_irq(PCIBaseState *s)
{
    /* Linux handler only checks bit 0x2 in reg_irq_status. Raise INTx when set. */
    if (s->reg_irq_status & 0x2) {
        pci_irq_assert(PCI_DEVICE(s));
    } else {
        pci_irq_deassert(PCI_DEVICE(s));
    }
}

/* ------------------------------------------------------------------ */
/* MMIO handlers (BAR3)                                                */
/* ------------------------------------------------------------------ */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = 0;

    switch (addr) {
    case 0: /* config/chan register */
        val = s->reg_cfg0;
        break;
    case 4: /* delay/mode */
        val = s->reg_delay;
        break;
    case 8: /* start/convert control */
        val = s->reg_start;
        break;
    case 12: /* FIFO control */
        val = s->reg_fifo_ctrl;
        break;
    case 16: /* IRQ status */
        val = s->reg_irq_status;
        break;
    case 20: /* EOS status */
        val = s->reg_eos;
        break;
    case 28: /* FIFO data */
        if (s->fifo_count > 0) {
            val = apci3xxx_fifo_pop(s);
        } else {
            val = s->reg_fifo_data;
        }
        s->reg_fifo_data = val;
        /* reading data clears EOS flag bit0 */
        s->reg_eos &= ~0x1u;
        break;
    case 32: /* convert timer */
        val = s->reg_conv_timer;
        break;
    case 36: /* convert unit */
        val = s->reg_conv_unit;
        break;
    case 48: /* sequence count */
        val = s->reg_seq_cnt;
        break;
    case 96: /* AO status / range */
        val = s->reg_ao_status;
        break;
    case 100: /* AO data */
        val = s->reg_ao_data;
        break;
    default:
        val = 0;
        break;
    }

    switch (size) {
    case 1:
        return val & 0xffu;
    case 2:
        return val & 0xffffu;
    default:
        return val;
    }
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t value, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = (uint32_t)value;

    switch (addr) {
    case 0: /* config/chan */
        s->reg_cfg0 = val;
        break;
    case 4: /* delay/mode */
        /* Driver uses read-modify-write with mask 0xfffffef0 and bit 0x100
         * for channel selection. Preserve lower and mode bits as written. */
        s->reg_delay = val;
        break;
    case 8: /* start/convert control */
        s->reg_start = val;
        if (val & 0x80000u) {
            /* single conversion start (insn) */
            s->ai_running = true;
            /* Simulate end of scan: set EOS and IRQ status bit used by ISR */
            s->reg_eos |= 0x1u; /* EOS */
            s->reg_irq_status |= 0x2u; /* interrupt status bit checked by driver */
            /* push one sample into FIFO */
            apci3xxx_fifo_push(s, s->reg_fifo_data);
            apci3xxx_update_irq(s);
        }
        if (val & 0x180000u) {
            /* command-mode start: also mark running and EOS/IRQ */
            s->ai_running = true;
            s->reg_eos |= 0x1u;
            s->reg_irq_status |= 0x2u;
            apci3xxx_fifo_push(s, s->reg_fifo_data);
            apci3xxx_update_irq(s);
        }
        break;
    case 12: /* FIFO control */
        s->reg_fifo_ctrl = val;
        if (val & 0x10000u) {
            /* Clear the FIFO as driver writes 0x10000 to dev->mmio + 12 */
            apci3xxx_fifo_reset(s);
        }
        break;
    case 16: /* IRQ status (write-to-clear) */
        /* Linux writes back the read value to clear interrupt */
        s->reg_irq_status &= ~val;
        apci3xxx_update_irq(s);
        break;
    case 20: /* EOS status */
        /* Driver only reads this for EOC; writes are not used in snippets,
         * but treat as plain write for completeness. */
        s->reg_eos = val;
        break;
    case 28: /* FIFO data: not used by driver as write, accept and store */
        s->reg_fifo_data = val;
        break;
    case 32: /* convert timer */
        s->reg_conv_timer = val;
        break;
    case 36: /* convert unit */
        s->reg_conv_unit = val;
        break;
    case 48: /* sequence count */
        s->reg_seq_cnt = val;
        break;
    case 96: /* AO status / range and EOC bit */
        /* driver uses this for range and EOC bit 0x100; treat as RW */
        s->reg_ao_status = val;
        break;
    case 100: {/* AO data write triggers EOC completion */
        s->reg_ao_data = val;
        /* after write, set EOC bit (0x100) so ao_eoc returns 0 */
        s->reg_ao_status |= 0x100u;
        break;
    }
    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* PIO handlers (BAR2)                                                 */
/* ------------------------------------------------------------------ */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = 0;

    switch (addr) {
    case 32: /* DI */
        /* apci3xxx_di_insn_bits: inl(dev->iobase + 32) & 0xf */
        val = s->io_di_reg32 & 0x0f;
        break;
    case 48: /* DO state */
        /* apci3xxx_do_insn_bits reads current state from dev->iobase + 48 */
        val = s->io_do_reg48 & 0x0f;
        break;
    case 64: /* DIO port1 input */
        /* Used as input in apci3xxx_dio_insn_bits: inl(dev->iobase + 64) */
        val = s->io_dio_in64 & 0xff;
        break;
    case 80: /* DIO port0 output readback */
        /* apci3xxx_dio_insn_bits reads dev->iobase + 80 */
        val = s->io_dio_out80 & 0xff;
        break;
    case 96: /* DIO port2 input when configured input */
        /* apci3xxx_dio_insn_bits may read dev->iobase + 96 */
        val = s->io_dio_in96 & 0xff;
        break;
    case 112: /* DIO port2 output readback */
        /* apci3xxx_dio_insn_bits may read dev->iobase + 112 */
        val = s->io_dio_out112 & 0xff;
        break;
    case 224: /* DIO port2 direction */
        /* apci3xxx_dio_insn_config writes dev->iobase + 224, readback here */
        val = s->io_dio_dir224 & 0xff;
        break;
    default:
        val = 0;
        break;
    }

    switch (size) {
    case 1:
        return val & 0xffu;
    case 2:
        return val & 0xffffu;
    default:
        return val;
    }
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t value, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = (uint32_t)value;

    switch (addr) {
    case 48: /* DO */
        /* apci3xxx_do_insn_bits writes new DO state to dev->iobase + 48 */
        s->io_do_reg48 = val & 0x0f;
        break;
    case 80: /* DIO port0 output */
        /* apci3xxx_dio_insn_bits may write dev->iobase + 80 */
        s->io_dio_out80 = val & 0xff;
        break;
    case 96: /* alt input shadow; driver does not normally write here */
        s->io_dio_in96 = val & 0xff;
        break;
    case 112: /* DIO port2 output */
        /* apci3xxx_dio_insn_bits may write dev->iobase + 112 */
        s->io_dio_out112 = val & 0xff;
        break;
    case 224: /* DIO port2 direction */
        /* apci3xxx_dio_insn_config writes (s->io_bits >> 24) & 0xff */
        s->io_dio_dir224 = val & 0xff;
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Reset                                                              */
/* ------------------------------------------------------------------ */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    pci_device_reset(pdev);

    s->reg_cfg0 = 0;
    s->reg_delay = 0;
    s->reg_start = 0;
    s->reg_fifo_ctrl = 0;
    s->reg_irq_status = 0;
    s->reg_eos = 0;
    s->reg_fifo_data = 0;
    s->reg_conv_timer = 0;
    s->reg_conv_unit = 0;
    s->reg_seq_cnt = 0;
    s->reg_ao_status = 0;
    s->reg_ao_data = 0;

    s->io_di_reg32 = 0;
    s->io_do_reg48 = 0;
    s->io_dio_out80 = 0;
    s->io_dio_in64 = 0;
    s->io_dio_out112 = 0;
    s->io_dio_in96 = 0;
    s->io_dio_dir224 = 0;

    apci3xxx_fifo_reset(s);
    s->ai_running = false;

    if (s->mmio_backing && s->mmio_backing_size) {
        memset(s->mmio_backing, 0, s->mmio_backing_size);
    }

    apci3xxx_update_irq(s);
}

/* ------------------------------------------------------------------ */
/* DMA initialize (unused here, but kept for structure)                */
/* ------------------------------------------------------------------ */
static void pcibase_dma_device_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    (void)s;
    (void)errp;
}

/* ------------------------------------------------------------------ */
/* PCI config space hooks                                              */
/* ------------------------------------------------------------------ */
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    uint32_t val = pci_default_read_config(pdev, addr, len);

    switch (len) {
    case 1:
        val &= 0xffu;
        break;
    case 2:
        val &= 0xffffu;
        break;
    case 4:
    default:
        break;
    }
    return val;
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    if (addr >= PCI_BASE_ADDRESS_0 && addr <= PCI_BASE_ADDRESS_5) {
        pci_default_write_config(pdev, addr, val, len);
        return;
    }
    pci_default_write_config(pdev, addr, val, len);
}

/* ------------------------------------------------------------------ */
/* Realize                                                             */
/* ------------------------------------------------------------------ */
static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;
    int i;

    /* PCI IDs */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  APCI3XXX_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  APCI3XXX_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, APCI3XXX_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Define BAR layout according to linux driver usage */
    s->num_bars = 0;
    for (i = 0; i < 6; ++i) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = "";
        s->bar_info[i].sparse = false;
    }

    /* BAR2: I/O space (at least up to offset 224, use 0x100) */
    s->bar_info[s->num_bars].index = 2;
    s->bar_info[s->num_bars].type  = BAR_TYPE_PIO;
    s->bar_info[s->num_bars].size  = 0x100;
    s->bar_info[s->num_bars].name  = "apci3xxx-io";
    s->num_bars++;

    /* BAR3: MMIO space (at least up to offset 100, use 0x100) */
    s->bar_info[s->num_bars].index = 3;
    s->bar_info[s->num_bars].type  = BAR_TYPE_MMIO;
    s->bar_info[s->num_bars].size  = 0x100;
    s->bar_info[s->num_bars].name  = "apci3xxx-mmio";
    s->num_bars++;

    /* Register BARs */
    for (i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
        if (errp && *errp) {
            return;
        }
    }

    /* Allocate optional backing for debug / safety (size of largest MMIO) */
    s->mmio_backing_size = 0x100;
    s->mmio_backing = g_malloc0(s->mmio_backing_size);

    /* Legacy INTx only; Linux uses request_irq on pcidev->irq */
    s->irq = pci_allocate_irq(pdev);

    /* No MSI/MSI-X advertised or enabled by driver */
    s->has_msi = false;
    s->has_msix = false;

    /* DMA not used by driver */
    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) {
        return;
    }

    /* Initialize runtime state */
    pcibase_reset(DEVICE(pdev));
}

/* ------------------------------------------------------------------ */
/* Uninit                                                              */
/* ------------------------------------------------------------------ */
static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (s->has_msix) {
        msix_uninit(pdev, NULL, 0);
        s->has_msix = false;
    }
    if (s->has_msi) {
        msi_uninit(pdev);
        s->has_msi = false;
    }

    if (s->mmio_backing) {
        g_free(s->mmio_backing);
        s->mmio_backing = NULL;
        s->mmio_backing_size = 0;
    }
}

/* ------------------------------------------------------------------ */
/* Class init / type registration                                      */
/* ------------------------------------------------------------------ */
static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->config_read  = pcibase_config_read;
    k->config_write = pcibase_config_write;
    k->realize      = pcibase_realize;
    k->exit         = pcibase_uninit;

    dc->reset       = pcibase_reset;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void pcibase_register_types(void)
{
    static InterfaceInfo interfaces[] = {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    };

    static const TypeInfo pcibase_info = {
        .name          = TYPE_PCIBASE_DEVICE,
        .parent        = TYPE_PCI_DEVICE,
        .instance_size = sizeof(PCIBaseState),
        .class_init    = pcibase_class_init,
        .interfaces    = interfaces,
    };

    type_register_static(&pcibase_info);
}

type_init(pcibase_register_types)

