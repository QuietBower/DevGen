/*
 * QEMU PCI serial device model for Linux 8250_pci driver
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
#include "hw/irq.h"

#define TYPE_PCIBASE_DEVICE "serial_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCIBASE_VENDOR_ID  0x13fe
#define PCIBASE_DEVICE_ID  0x1600
#define PCIBASE_CLASS_ID   0x0700

/*
 * This model emulates a very simple 16550-compatible UART exposed
 * through a single IO-space BAR of 8 bytes, sufficient for the
 * generic 8250_pci driver to probe and register a single port.
 */

typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM
} BARType;

typedef struct {
    int     index;
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

    /* UART register shadows (NS16550-like) */
    uint8_t rbr_thr;   /* 0: RX buffer / TX holding */
    uint8_t ier;       /* 1: Interrupt Enable */
    uint8_t iir;       /* 2: Interrupt Identification (RO) */
    uint8_t fcr;       /* 2: FIFO Control (WO) - store last value */
    uint8_t lcr;       /* 3: Line Control */
    uint8_t mcr;       /* 4: Modem Control */
    uint8_t lsr;       /* 5: Line Status */
    uint8_t msr;       /* 6: Modem Status */
    uint8_t scr;       /* 7: Scratch */

    /* Divisor latch */
    uint8_t dll;       /* 0 with DLAB=1 */
    uint8_t dlm;       /* 1 with DLAB=1 */

    /* Simple RX holding register to simulate data availability */
    uint8_t rx_data;
    bool rx_has_data;

    /* IRQ line level */
    bool irq_level;
};

static void pcibase_update_iir(PCIBaseState *s)
{
    /* IIR priority: THRE (0x02) then RDA (0x04). Bit 0 == 0 when pending */
    if ((s->lsr & 0x20) && (s->ier & 0x02)) {
        s->iir = 0x02; /* THR empty */
    } else if ((s->lsr & 0x01) && (s->ier & 0x01)) {
        s->iir = 0x04; /* Received data available */
    } else {
        s->iir = 0x01; /* No interrupt pending, bit0=1 */
    }
}

/* Internal helper for status-triggered signaling. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    pcibase_update_iir(s);

    /* Any interrupt pending when IIR bit0 == 0 */
    bool pending = ((s->iir & 0x01) == 0);

    if (pending && !s->irq_level) {
        s->irq_level = true;
        pci_set_irq(pdev, 1);
    } else if (!pending && s->irq_level) {
        s->irq_level = false;
        pci_set_irq(pdev, 0);
    }
}

/* No DMA is modeled for this very simple UART. */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    (void)s;
    (void)is_write;
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0xff;

    /* This device uses only IO-space BAR; no MMIO registers. */
    (void)s;
    (void)addr;
    (void)size;

    return val;
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
    uint64_t val = 0xff;

    /* Only byte accesses are meaningful for UART regs */
    if (size != 1) {
        return 0xff;
    }

    switch (addr & 7) {
    case 0: /* RBR / DLL */
        if (s->lcr & 0x80) {
            /* DLAB=1: DLL */
            val = s->dll;
        } else {
            if (s->rx_has_data) {
                val = s->rx_data;
                s->rx_has_data = false;
                s->lsr &= ~0x01; /* clear Data Ready */
            } else {
                val = s->rbr_thr;
            }
        }
        break;
    case 1: /* IER / DLM */
        if (s->lcr & 0x80) {
            val = s->dlm;
        } else {
            val = s->ier;
        }
        break;
    case 2: /* IIR (RO) */
        val = s->iir | 0xC0; /* top bits as 1 like some UARTs */
        break;
    case 3: /* LCR */
        val = s->lcr;
        break;
    case 4: /* MCR */
        val = s->mcr;
        break;
    case 5: /* LSR */
        val = s->lsr;
        break;
    case 6: /* MSR */
        val = s->msr;
        break;
    case 7: /* SCR */
        val = s->scr;
        break;
    default:
        val = 0xff;
        break;
    }

    pcibase_update_irq(s);

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint8_t v = (uint8_t)val;

    if (size != 1) {
        return;
    }

    switch (addr & 7) {
    case 0: /* THR / DLL */
        if (s->lcr & 0x80) {
            /* DLAB=1: DLL */
            s->dll = v;
        } else {
            /* Transmit holding register write */
            s->rbr_thr = v;
            /* THR becomes empty immediately */
            s->lsr |= 0x20; /* THR empty */
        }
        break;
    case 1: /* IER / DLM */
        if (s->lcr & 0x80) {
            s->dlm = v;
        } else {
            s->ier = v & 0x0F; /* typical UART uses lower 4 bits */
        }
        break;
    case 2: /* FCR (WO) */
        s->fcr = v;
        break;
    case 3: /* LCR */
        s->lcr = v;
        break;
    case 4: /* MCR */
        s->mcr = v;
        break;
    case 5: /* LSR is RO; ignore writes */
        break;
    case 6: /* MSR is RO; ignore writes */
        break;
    case 7: /* SCR */
        s->scr = v;
        break;
    default:
        break;
    }

    pcibase_update_irq(s);
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

    s->rbr_thr = 0x00;
    s->ier = 0x00;
    s->iir = 0x01;  /* no interrupt pending */
    s->fcr = 0x00;
    s->lcr = 0x00;
    s->mcr = 0x00;
    /* LSR: THR empty and transmitter empty bits set after reset */
    s->lsr = 0x60;
    s->msr = 0x00;
    s->scr = 0x00;
    s->dll = 0x00;
    s->dlm = 0x00;
    s->rx_data = 0x00;
    s->rx_has_data = false;
    s->irq_level = false;

    pcibase_update_irq(s);
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    hwaddr aligned_size = pow2ceil(bi->size ? bi->size : 1);
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCIBASE_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCIBASE_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCIBASE_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* Single IO-space BAR of 8 bytes for 16550 registers */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 8;
    s->bar_info[0].name = "serial-pio-bar0";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = "";
    }
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize UART state */
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

    (void)s;
}

static const VMStateDescription vmstate_pcibase = {
    .name = "serial_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT8(rbr_thr, PCIBaseState),
        VMSTATE_UINT8(ier, PCIBaseState),
        VMSTATE_UINT8(iir, PCIBaseState),
        VMSTATE_UINT8(fcr, PCIBaseState),
        VMSTATE_UINT8(lcr, PCIBaseState),
        VMSTATE_UINT8(mcr, PCIBaseState),
        VMSTATE_UINT8(lsr, PCIBaseState),
        VMSTATE_UINT8(msr, PCIBaseState),
        VMSTATE_UINT8(scr, PCIBaseState),
        VMSTATE_UINT8(dll, PCIBaseState),
        VMSTATE_UINT8(dlm, PCIBaseState),
        VMSTATE_UINT8(rx_data, PCIBaseState),
        VMSTATE_BOOL(rx_has_data, PCIBaseState),
        VMSTATE_BOOL(irq_level, PCIBaseState),
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
