/*
 * QEMU PCI device model for amplc_pci224 (for Linux comedi driver)
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

#define TYPE_PCIBASE_DEVICE "amplc_pci224_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Define missing log mask used with qemu_log_mask() */
#ifndef LOG_GUEST
#define LOG_GUEST LOG_UNIMP
#endif

#define CLK_EXT                7
#define CLK_CLK                0
#define CLK_10MHZ              1
#define CLK_1MHZ               2
#define CLK_100KHZ             3
#define CLK_10KHZ              4
#define CLK_1KHZ               5
#define CLK_OUTNM1             6

#define GAT_VCC                0
#define GAT_GND                1
#define GAT_EXT                2
#define GAT_NOUTNM2            3

#define PCI224_Z2_BASE         0x14
#define PCI224_ZCLK_SCE        0x1A
#define PCI224_ZGAT_SCE        0x1D
#define PCI224_INT_SCE         0x1E

#define PCI224_DACDATA         0x00
#define PCI224_SOFTTRIG        0x00
#define PCI224_DACCON          0x02
#define PCI224_FIFOSIZ         0x04
#define PCI224_DACCEN          0x06

#define PCI224_DACCON_TRIG(x)          (((x) & 0x7) << 0)
#define PCI224_DACCON_TRIG_MASK        PCI224_DACCON_TRIG(7)
#define PCI224_DACCON_TRIG_NONE        PCI224_DACCON_TRIG(0)
#define PCI224_DACCON_TRIG_SW          PCI224_DACCON_TRIG(1)
#define PCI224_DACCON_TRIG_EXTP        PCI224_DACCON_TRIG(2)
#define PCI224_DACCON_TRIG_EXTN        PCI224_DACCON_TRIG(3)
#define PCI224_DACCON_TRIG_Z2CT0       PCI224_DACCON_TRIG(4)
#define PCI224_DACCON_TRIG_Z2CT1       PCI224_DACCON_TRIG(5)
#define PCI224_DACCON_TRIG_Z2CT2       PCI224_DACCON_TRIG(6)

#define PCI224_DACCON_POLAR(x)         (((x) & 0x1) << 3)
#define PCI224_DACCON_POLAR_MASK       PCI224_DACCON_POLAR(1)
#define PCI224_DACCON_POLAR_UNI        PCI224_DACCON_POLAR(0)
#define PCI224_DACCON_POLAR_BI         PCI224_DACCON_POLAR(1)

#define PCI224_DACCON_VREF(x)          (((x) & 0x3) << 4)
#define PCI224_DACCON_VREF_MASK        PCI224_DACCON_VREF(3)
#define PCI224_DACCON_VREF_1_25        PCI224_DACCON_VREF(0)
#define PCI224_DACCON_VREF_2_5         PCI224_DACCON_VREF(1)
#define PCI224_DACCON_VREF_5           PCI224_DACCON_VREF(2)
#define PCI224_DACCON_VREF_10          PCI224_DACCON_VREF(3)

#define PCI224_DACCON_FIFOWRAP         (1U << 7)
#define PCI224_DACCON_FIFOENAB         (1U << 8)

#define PCI224_DACCON_FIFOINTR(x)      (((x) & 0x7) << 9)
#define PCI224_DACCON_FIFOINTR_MASK    PCI224_DACCON_FIFOINTR(7)
#define PCI224_DACCON_FIFOINTR_EMPTY   PCI224_DACCON_FIFOINTR(0)
#define PCI224_DACCON_FIFOINTR_NEMPTY  PCI224_DACCON_FIFOINTR(1)
#define PCI224_DACCON_FIFOINTR_NHALF   PCI224_DACCON_FIFOINTR(2)
#define PCI224_DACCON_FIFOINTR_HALF    PCI224_DACCON_FIFOINTR(3)
#define PCI224_DACCON_FIFOINTR_NFULL   PCI224_DACCON_FIFOINTR(4)
#define PCI224_DACCON_FIFOINTR_FULL    PCI224_DACCON_FIFOINTR(5)

#define PCI224_DACCON_FIFOFL(x)        (((x) & 0x7) << 12)
#define PCI224_DACCON_FIFOFL_MASK      PCI224_DACCON_FIFOFL(7)
#define PCI224_DACCON_FIFOFL_EMPTY     PCI224_DACCON_FIFOFL(1)
#define PCI224_DACCON_FIFOFL_ONETOHALF PCI224_DACCON_FIFOFL(0)
#define PCI224_DACCON_FIFOFL_HALFTOFULL PCI224_DACCON_FIFOFL(4)
#define PCI224_DACCON_FIFOFL_FULL      PCI224_DACCON_FIFOFL(6)

#define PCI224_DACCON_BUSY             (1U << 15)
#define PCI224_DACCON_FIFORESET        (1U << 12)
#define PCI224_DACCON_GLOBALRESET      (1U << 13)

#define PCI224_FIFO_SIZE               4096
#define PCI224_FIFO_ROOM_EMPTY         PCI224_FIFO_SIZE
#define PCI224_FIFO_ROOM_ONETOHALF     (PCI224_FIFO_SIZE / 2)
#define PCI224_FIFO_ROOM_HALFTOFULL    1
#define PCI224_FIFO_ROOM_FULL          0

#define PCI224_INTR_EXT                0x01
#define PCI224_INTR_DAC                0x04
#define PCI224_INTR_Z2CT1              0x20
#define PCI224_INTR_EDGE_BITS          (PCI224_INTR_EXT | PCI224_INTR_Z2CT1)
#define PCI224_INTR_LEVEL_BITS         PCI224_INTR_DAC

#define AO_CMD_STARTED                 0

#define MAX_SCAN_PERIOD                0xFFFFFFFFU
#define MIN_SCAN_PERIOD                2500
#define CONVERT_PERIOD                 625

#define PCI224_VENDOR_ID               0x14DC
#define PCI224_DEVICE_ID               0x0007
#define PCI224_CLASS_ID                PCI_CLASS_OTHERS

/* ------------------------------------------------------------------ */
/* BAR metadata definition                                             */
/* ------------------------------------------------------------------ */
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

    MemoryRegion bar_regions[6];

    uint8_t *mmio_backing;
    size_t mmio_backing_size;

    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;

    /* PCI224 register shadows / state */
    uint16_t daccon;
    uint16_t daccen;
    uint16_t fifosiz;

    uint16_t fifo_level;      /* number of samples currently in FIFO */

    uint8_t intsce;           /* interrupt enable shadow (INT_SCE) */
    uint8_t intstat;          /* latched interrupt status bits */

    /* simple 8254-like timer config registers (only what driver writes) */
    uint8_t zclk_sce_chan[4];
    uint8_t zgat_sce_chan[4];

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
/* Helper: register a BAR                                             */
/* ------------------------------------------------------------------ */
static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, bi->size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

/* ------------------------------------------------------------------ */
/* Interrupt helper                                                    */
/* ------------------------------------------------------------------ */
static void pci224_update_irq(PCIBaseState *s)
{
    uint8_t pending = s->intstat & s->intsce;
    if (pending) {
        qemu_set_irq(s->irq, 1);
    } else {
        qemu_set_irq(s->irq, 0);
    }
}

/* ------------------------------------------------------------------ */
/* MMIO / PIO handlers                                                 */
/* ------------------------------------------------------------------ */

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    /* Only 16-bit accesses are used by driver (inw/outw). */
    if (size != 2) {
        return 0xFFFF;
    }

    switch (addr) {
    case PCI224_DACDATA:
        /* FIFO is write-only for this driver; return 0 */
        return 0;
    case PCI224_DACCON:
        /* Return daccon with FIFO level encoded in FIFOFL bits. */
    {
        uint16_t val = s->daccon & ~(PCI224_DACCON_FIFOFL_MASK);
        /* Map fifo_level to one of the four documented ranges. */
        if (s->fifo_level == 0) {
            val |= PCI224_DACCON_FIFOFL_EMPTY;
        } else if (s->fifo_level < PCI224_FIFO_ROOM_ONETOHALF) {
            val |= PCI224_DACCON_FIFOFL_ONETOHALF;
        } else if (s->fifo_level < PCI224_FIFO_SIZE) {
            val |= PCI224_DACCON_FIFOFL_HALFTOFULL;
        } else {
            val |= PCI224_DACCON_FIFOFL_FULL;
        }
        return val;
    }
    case PCI224_FIFOSIZ:
        return s->fifosiz;
    case PCI224_DACCEN:
        return s->daccen;
    default:
        return 0;
    }
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 2) {
        return;
    }

    uint16_t v = (uint16_t)val;

    switch (addr) {
    case PCI224_DACDATA:
        /* FIFO write: increase level up to capacity */
        if (s->fifo_level < PCI224_FIFO_SIZE) {
            s->fifo_level++;
        }
        /* DAC interrupt is level-sensitive to FIFO status according to driver.
         * The driver enables PCI224_INTR_DAC and then expects interrupts when
         * FIFO crosses configured threshold. We model simply: whenever data is
         * written and DAC interrupt is enabled, assert DAC interrupt bit.
         */
        s->intstat |= PCI224_INTR_DAC;
        pci224_update_irq(s);
        break;
    case PCI224_DACCON:
        /* Handle special control bits: GLOBALRESET, FIFORESET. */
        if (v & PCI224_DACCON_GLOBALRESET) {
            s->daccon = 0;
            s->daccen = 0;
            s->fifosiz = 0;
            s->fifo_level = 0;
        } else {
            s->daccon = (v & ~(PCI224_DACCON_FIFORESET));
        }
        if (v & PCI224_DACCON_FIFORESET) {
            s->fifo_level = 0;
        }
        break;
    case PCI224_FIFOSIZ:
        /* Driver writes 0 during init; just store value. */
        s->fifosiz = v;
        break;
    case PCI224_DACCEN:
        s->daccen = v;
        break;
    default:
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    /* iobase1 port space: only 8-bit accesses (inb/outb) */
    if (size != 1) {
        return 0xFF;
    }

    switch (addr) {
    case PCI224_INT_SCE:
        /* Driver uses inb(INT_SCE) to read interrupt status. We model
         * a simple latched status register: return current status bits.
         */
        return s->intstat & 0x3F;
    case PCI224_ZCLK_SCE:
        /* No reads done by driver; return last written value on channel 0. */
        return s->zclk_sce_chan[0];
    case PCI224_ZGAT_SCE:
        return s->zgat_sce_chan[0];
    default:
        return 0x00;
    }
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 1) {
        return;
    }

    uint8_t v = (uint8_t)val;

    switch (addr) {
    case PCI224_INT_SCE:
        /* Interrupt enable register; driver writes mask values here. */
        s->intsce = v & 0x3F;
        /* Clear any bits in intstat that have been disabled, then update irq. */
        if (!(s->intsce & PCI224_INTR_DAC)) {
            s->intstat &= ~PCI224_INTR_DAC;
        }
        if (!(s->intsce & PCI224_INTR_EXT)) {
            s->intstat &= ~PCI224_INTR_EXT;
        }
        if (!(s->intsce & PCI224_INTR_Z2CT1)) {
            s->intstat &= ~PCI224_INTR_Z2CT1;
        }
        pci224_update_irq(s);
        break;
    case PCI224_ZCLK_SCE:
    {
        unsigned int chan = (v >> 3) & 0x3;
        if (chan < 4) {
            s->zclk_sce_chan[chan] = v;
        }
        break;
    }
    case PCI224_ZGAT_SCE:
    {
        unsigned int chan = (v >> 3) & 0x3;
        if (chan < 4) {
            s->zgat_sce_chan[chan] = v;
        }
        break;
    }
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

    s->daccon = 0;
    s->daccen = 0;
    s->fifosiz = 0;
    s->fifo_level = 0;

    s->intsce = 0;
    s->intstat = 0;

    memset(s->zclk_sce_chan, 0, sizeof(s->zclk_sce_chan));
    memset(s->zgat_sce_chan, 0, sizeof(s->zgat_sce_chan));

    if (s->mmio_backing && s->mmio_backing_size) {
        memset(s->mmio_backing, 0, s->mmio_backing_size);
    }

    pci224_update_irq(s);
}

/* ------------------------------------------------------------------ */
/* DMA initialize (unused by this driver)                             */
/* ------------------------------------------------------------------ */
static void pcibase_dma_device_realize(PCIDevice *pdev, Error **errp)
{
    (void)pdev;
    (void)errp;
}

/* ------------------------------------------------------------------ */
/* PCI config space access                                             */
/* ------------------------------------------------------------------ */
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    uint32_t val = pci_default_read_config(pdev, addr, len);

    switch (len) {
    case 1:
        val &= 0xFF;
        break;
    case 2:
        val &= 0xFFFF;
        break;
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
/* Realize (device init)                                              */
/* ------------------------------------------------------------------ */
static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI224_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI224_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI224_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Setup BARs according to linux driver usage:
     *  BAR2 -> iobase1 (PIO)
     *  BAR3 -> dev->iobase (MMIO)
     */
    s->num_bars = 2;
    s->bar_info[0].index = 2;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 0x20; /* covers INT_SCE, Z2_BASE, ZCLK_SCE, ZGAT_SCE */
    s->bar_info[0].name = "pci224-io1";
    s->bar_info[0].sparse = false;

    s->bar_info[1].index = 3;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = 0x10; /* covers DACDATA..DACCEN */
    s->bar_info[1].name = "pci224-mmio";
    s->bar_info[1].sparse = false;

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
        if (errp && *errp) {
            return;
        }
    }

    /* Legacy INTx */
    s->irq = pci_allocate_irq(pdev);

    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) {
        return;
    }

    qemu_log_mask(LOG_GUEST, "[%s] device realized\n", TYPE_PCIBASE_DEVICE);
}

/* ------------------------------------------------------------------ */
/* Uninit/cleanup                                                     */
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
    }

    qemu_log_mask(LOG_GUEST, "[%s] device uninit\n", TYPE_PCIBASE_DEVICE);
}

/* ------------------------------------------------------------------ */
/* Class init / type registration                                     */
/* ------------------------------------------------------------------ */
static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->config_read  = pcibase_config_read;
    k->config_write = pcibase_config_write;

    k->realize = pcibase_realize;
    k->exit    = pcibase_uninit;
    dc->reset  = pcibase_reset;

    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void pcibase_register_types(void)
{
    static InterfaceInfo interfaces[] = {
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

