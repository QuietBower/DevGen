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

#define TYPE_PCIBASE_DEVICE "aectc_pci"

typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* ------------------------------------------------------------------ */
/* Device-specific macros                                              */
/* ------------------------------------------------------------------ */

#define PCI_VENDOR_ID_AEC 0xaecb
#define PCI_DEVICE_ID_AEC_VITCLTC 0x6250

#define INT_ENABLE              0x10
#define INT_DISABLE             0x0
#define INT_ENABLE_ADDR         0xFC
#define INT_MASK_ADDR           0x2E
#define INT_MASK_ALL            0x3F
#define INTA_DRVR_ADDR          0xFE
#define INTA_ENABLED_FLAG       0x08
#define INTA_FLAG               0x01
#define MAILBOX                 0x0F

/* ------------------------------------------------------------------ */
/* BAR metadata and device state definition                            */
/* ------------------------------------------------------------------ */

typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
} BARType;

typedef struct {
    int index;
    BARType type;
    hwaddr size;
    const char *name;
    bool sparse;
} BARInfo;

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Unified BAR memory regions for MMIO/PIO */
    MemoryRegion bar_regions[6];

    /* Optional linear backing for simple register arrays */
    uint8_t *mmio_backing;
    size_t mmio_backing_size;

    /* BAR table */
    BARInfo bar_info[6];
    int num_bars;

    /* Feature flags */
    bool has_msi;
    bool has_msix;

    /* Register shadows */
    uint32_t int_enable_reg;   /* at INT_ENABLE_ADDR (0xFC) */
    uint8_t  int_mask_reg;     /* at INT_MASK_ADDR (0x2E) */
    uint8_t  inta_status_reg;  /* at INTA_DRVR_ADDR (0xFE) */
    uint8_t  mailbox_reg;      /* at MAILBOX (0x0F) */

    /* Interrupt state */
    bool irq_asserted;
};


/* ------------------------------------------------------------------ */
/* Forward declarations                                                 */
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
/* Helper: IRQ handling                                                 */
/* ------------------------------------------------------------------ */

static void aectc_update_irq(PCIBaseState *s)
{
    bool want_irq = false;

    /* IRQ is requested if enable bit is set and the INTA flag is set */
    if ((s->int_enable_reg & INT_ENABLE) &&
        (s->inta_status_reg & INTA_ENABLED_FLAG) &&
        (s->inta_status_reg & INTA_FLAG)) {
        want_irq = true;
    }

    if (want_irq && !s->irq_asserted) {
        pci_set_irq(PCI_DEVICE(s), 1);
        s->irq_asserted = true;
    } else if (!want_irq && s->irq_asserted) {
        pci_set_irq(PCI_DEVICE(s), 0);
        s->irq_asserted = false;
    }
}

/* ------------------------------------------------------------------ */
/* MemoryRegionOps                                                       */
/* ------------------------------------------------------------------ */

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    /* MMIO BAR0 only. The Linux driver uses 8-bit and 32-bit accesses. */

    switch (addr) {
    case 0x00: /* vendor low byte (for print_board_data) */
        if (size == 1) {
            return PCI_VENDOR_ID_AEC & 0xff;
        }
        break;
    case 0x01: /* vendor high byte */
        if (size == 1) {
            return (PCI_VENDOR_ID_AEC >> 8) & 0xff;
        }
        break;
    case 0x02: /* device low byte */
        if (size == 1) {
            return PCI_DEVICE_ID_AEC_VITCLTC & 0xff;
        }
        break;
    case 0x03: /* device high byte */
        if (size == 1) {
            return (PCI_DEVICE_ID_AEC_VITCLTC >> 8) & 0xff;
        }
        break;
    case 0x06: /* revision low (print_board_data) */
        if (size == 1) {
            return 'A';
        }
        break;
    case 0x07: /* revision high (print_board_data) */
        if (size == 1) {
            return '0';
        }
        break;
    case MAILBOX: /* 0x0F */
        if (size == 1) {
            /* Reading mailbox is used by ISR and during remove(). */
            return s->mailbox_reg;
        }
        break;
    case INT_MASK_ADDR: /* 0x2E */
        if (size == 1) {
            return s->int_mask_reg;
        }
        break;
    case INT_ENABLE_ADDR: /* 0xFC */
        if (size == 4) {
            return s->int_enable_reg;
        }
        break;
    case INTA_DRVR_ADDR: /* 0xFE */
        if (size == 1) {
            /* Driver checks INTA_ENABLED_FLAG and INTA_FLAG */
            return s->inta_status_reg;
        }
        break;
    default:
        /* For undefined locations, return 0. */
        break;
    }

    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case MAILBOX: /* 0x0F */
        if (size == 1) {
            /* The driver does not write MAILBOX, but to be safe, store it. */
            s->mailbox_reg = (uint8_t)val;
        }
        break;
    case INT_MASK_ADDR: /* 0x2E */
        if (size == 1) {
            /* Mask register: driver writes INT_MASK_ALL on probe and
             * INT_DISABLE on remove. We simply store the value. */
            s->int_mask_reg = (uint8_t)val;
            /* Mask does not directly affect IRQ generation for the driver
             * paths we see; keep behaviour minimal. */
        }
        break;
    case INT_ENABLE_ADDR: /* 0xFC */
        if (size == 4) {
            /* Driver writes INT_ENABLE on probe and INT_DISABLE on remove. */
            s->int_enable_reg = (uint32_t)val;
            aectc_update_irq(s);
        }
        break;
    case INTA_DRVR_ADDR: /* 0xFE */
        if (size == 1) {
            /* Not used by the Linux driver, but allow software to clear flags
             * by writing. */
            s->inta_status_reg = (uint8_t)val;
            aectc_update_irq(s);
        }
        break;
    default:
        /* Ignore other writes, as the driver does not use them. */
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* The Linux driver does not use PIO; return 0. */
    (void)opaque;
    (void)addr;
    (void)size;
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* The Linux driver does not use PIO; ignore writes. */
    (void)opaque;
    (void)addr;
    (void)val;
    (void)size;
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 1 },
};

/* ------------------------------------------------------------------ */
/* Helper: register a BAR (MMIO or PIO)                                 */
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
    }

    (void)errp;
}

/* ------------------------------------------------------------------ */
/* Reset (clear state, IRQs, DMA)                                      */
/* ------------------------------------------------------------------ */

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    /* core QEMU resets */
    pci_device_reset(pdev);
    msi_reset(pdev);
    msix_reset(pdev);

    if (s->mmio_backing && s->mmio_backing_size) {
        memset(s->mmio_backing, 0, s->mmio_backing_size);
    }

    /* Reset device-visible registers to defaults that allow probe() to work. */
    s->int_enable_reg = 0;
    s->int_mask_reg = 0;
    /* After probe writes, driver expects INTA_ENABLED_FLAG set. We keep
     * it set by default so that the check passes once interrupts are enabled. */
    s->inta_status_reg = INTA_ENABLED_FLAG; /* INTA_FLAG will be 0 until we "fire" IRQs. */
    s->mailbox_reg = 0;

    if (s->irq_asserted) {
        pci_set_irq(pdev, 0);
        s->irq_asserted = false;
    }
}

/* ------------------------------------------------------------------ */
/* DMA initialization (called from realize)                            */
/* ------------------------------------------------------------------ */

static void pcibase_dma_device_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    (void)s;
    (void)errp;
}

/* ------------------------------------------------------------------ */
/* PCI config space access                                             */
/* ------------------------------------------------------------------ */

static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    uint32_t val = pci_default_read_config(pdev, addr, len);
    return val;
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    pci_default_write_config(pdev, addr, val, len);
}

/* ------------------------------------------------------------------ */
/* Core realize / initialize                                           */
/* ------------------------------------------------------------------ */

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Interrupt pin A for legacy INTx */
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Device uses legacy INTx only (no MSI/MSI-X in driver). */
    s->has_msi = false;
    s->has_msix = false;

    /* BAR0 used as memory-mapped I/O by the driver via pci_iomap. */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    /* Size: must cover all used registers up to 0xFE. Use 256 bytes. */
    s->bar_info[0].size = 0x100;
    s->bar_info[0].name = "aectc-bar0";
    s->bar_info[0].sparse = false;

    s->mmio_backing = NULL;
    s->mmio_backing_size = 0;

    /* Register BARs */
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
        if (errp && *errp) {
            return;
        }
    }

    /* DMA init (not used by this device) */
    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) {
        return;
    }

    /* Initialize internal state to match reset expectations */
    s->int_enable_reg = 0;
    s->int_mask_reg = 0;
    s->inta_status_reg = INTA_ENABLED_FLAG;
    s->mailbox_reg = 0;
    s->irq_asserted = false;

    
}

/* ------------------------------------------------------------------ */
/* Uninit / cleanup                                                    */
/* ------------------------------------------------------------------ */

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (s->has_msix) {
        msix_uninit(pdev, NULL, 0);
    }
    if (s->has_msi) {
        msi_uninit(pdev);
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

    /* Hook config read/write if device needs special handling */
    k->config_read  = pcibase_config_read;
    k->config_write = pcibase_config_write;

    k->realize = pcibase_realize;
    k->exit    = pcibase_uninit;
    dc->reset  = pcibase_reset;

    k->vendor_id = PCI_VENDOR_ID_AEC;
    k->device_id = PCI_DEVICE_ID_AEC_VITCLTC;
    k->class_id = PCI_CLASS_OTHERS;

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

static void pcibase_register_types_call(void)
{
    pcibase_register_types();
}

type_init(pcibase_register_types_call);
