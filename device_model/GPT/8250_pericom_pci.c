/*
 * QEMU PCI device model for Pericom 8250 compatible serial controller
 * Minimal functional model sufficient for Linux 8250_pericom driver probe
 * This model exposes a single I/O BAR with 4 legacy 16550 UART ports
 * implemented via simple shadow registers. IRQ is raised on received data
 * and cleared on IIR read.
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

#define TYPE_PCIBASE_DEVICE "8250_pericom_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCIBASE_VENDOR_ID  0x12D8
#define PCIBASE_DEVICE_ID  0x7951
#define PCIBASE_CLASS_ID   0x0700

/* 16550 UART register offsets (8-bit) */
#define UART_RBR   0   /* In:  Receive buffer */
#define UART_THR   0   /* Out: Transmit holding */
#define UART_DLL   0   /* Out: Divisor Latch Low */
#define UART_IER   1   /* In/Out: Interrupt Enable */
#define UART_DLM   1   /* Out: Divisor Latch High */
#define UART_IIR   2   /* In: Interrupt ID */
#define UART_FCR   2   /* Out: FIFO Control */
#define UART_LCR   3   /* In/Out: Line Control */
#define UART_MCR   4   /* Out: Modem Control */
#define UART_LSR   5   /* In: Line Status */
#define UART_MSR   6   /* In: Modem Status */
#define UART_SCR   7   /* In/Out: Scratch */
#define UART_ICR   8   /* Extended Control Reg (used via SCR) */
#define UART_ACR   9

/* UART_LCR bits */
#define UART_LCR_DLAB 0x80

/* UART_IER bits */
#define UART_IER_RDI  0x01
#define UART_IER_THRI 0x02

/* UART_IIR bits */
#define UART_IIR_NO_INT   0x01
#define UART_IIR_RDI      0x04

/* UART_LSR bits */
#define UART_LSR_DR       0x01
#define UART_LSR_THRE     0x20
#define UART_LSR_TEMT     0x40

/* BAR layout: one I/O BAR with up to 4 ports spaced by 0x8, as driver expects */

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

typedef struct UARTPortState {
    uint8_t rbr;
    uint8_t thr;
    uint8_t dll;
    uint8_t dlm;
    uint8_t ier;
    uint8_t iir;
    uint8_t fcr;
    uint8_t lcr;
    uint8_t mcr;
    uint8_t lsr;
    uint8_t msr;
    uint8_t scr;
    uint8_t icr;
    uint8_t acr;
} UARTPortState;

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;

    /* UARTs */
    unsigned int nr_ports; /* how many ports this device exposes */
    UARTPortState ports[4];
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool pending = false;

    /* Simple model: if any port has DR set and RDI enabled, raise IRQ */
    for (unsigned int i = 0; i < s->nr_ports; i++) {
        UARTPortState *p = &s->ports[i];
        if ((p->lsr & UART_LSR_DR) && (p->ier & UART_IER_RDI)) {
            pending = true;
            break;
        }
    }

    if (pending) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* No device-initiated DMA used by this driver */

/* Helper to access a UART port given global I/O offset */
static UARTPortState *pcibase_get_port(PCIBaseState *s, hwaddr addr, hwaddr *port_off)
{
    /* Ports spaced every 0x8 bytes as per driver */
    unsigned int port = addr >> 3; /* addr / 8 */
    if (port >= s->nr_ports) {
        return NULL;
    }
    *port_off = addr & 0x7;
    return &s->ports[port];
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;
    (void)addr;
    (void)size;
    /* This device only exposes PIO; no MMIO */
    return 0xff;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;
    (void)addr;
    (void)val;
    (void)size;
    /* No MMIO */
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    hwaddr off;
    UARTPortState *p;
    uint8_t reg = 0xff;

    if (size != 1) {
        return 0xff;
    }

    p = pcibase_get_port(s, addr, &off);
    if (!p) {
        return 0xff;
    }

    switch (off) {
    case UART_RBR:
        if (p->lcr & UART_LCR_DLAB) {
            reg = p->dll;
        } else {
            /* Read received data; clear DR */
            reg = p->rbr;
            p->lsr &= ~UART_LSR_DR;
        }
        break;
    case UART_IER:
        if (p->lcr & UART_LCR_DLAB) {
            reg = p->dlm;
        } else {
            reg = p->ier;
        }
        break;
    case UART_IIR:
        /* Reading IIR clears pending interrupt in this simple model */
        if ((p->lsr & UART_LSR_DR) && (p->ier & UART_IER_RDI)) {
            reg = UART_IIR_RDI; /* data available interrupt */
        } else {
            reg = UART_IIR_NO_INT;
        }
        /* Clear interrupt by clearing DR */
        p->lsr &= ~UART_LSR_DR;
        break;
    case UART_LCR:
        reg = p->lcr;
        break;
    case UART_MCR:
        reg = p->mcr;
        break;
    case UART_LSR:
        reg = p->lsr;
        break;
    case UART_MSR:
        reg = p->msr;
        break;
    case UART_SCR:
        reg = p->scr;
        break;
    default:
        reg = 0xff;
        break;
    }

    pcibase_update_irq(s);
    return reg;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    hwaddr off;
    UARTPortState *p;
    uint8_t v = (uint8_t)val;

    if (size != 1) {
        return;
    }

    p = pcibase_get_port(s, addr, &off);
    if (!p) {
        return;
    }

    switch (off) {
    case UART_THR:
        if (p->lcr & UART_LCR_DLAB) {
            p->dll = v;
        } else {
            /* Transmit: we don't emulate actual data path, just mark THR empty */
            p->thr = v;
            p->lsr |= UART_LSR_THRE | UART_LSR_TEMT;
        }
        break;
    case UART_IER:
        if (p->lcr & UART_LCR_DLAB) {
            p->dlm = v;
        } else {
            p->ier = v;
        }
        break;
    case UART_FCR:
        p->fcr = v;
        break;
    case UART_LCR:
        p->lcr = v;
        break;
    case UART_MCR:
        p->mcr = v;
        break;
    case UART_SCR:
        p->scr = v;
        break;
    default:
        /* ignore writes to unknown/unused offsets */
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

    /* Initialize BAR and UART defaults */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_PIO;
    /* 4 ports * 8 bytes each = 32 bytes */
    s->bar_info[0].size = 0x20;
    s->bar_info[0].name = "pericom-io";

    s->nr_ports = 4;
    for (unsigned int i = 0; i < s->nr_ports; i++) {
        UARTPortState *p = &s->ports[i];
        memset(p, 0, sizeof(*p));
        p->lsr = UART_LSR_THRE | UART_LSR_TEMT; /* transmitter empty */
        p->iir = UART_IIR_NO_INT;
    }

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

    /* Ensure reset-initialized BARs and ports */
    pcibase_reset(DEVICE(pdev));

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

    (void)s;
}

static const VMStateDescription vmstate_pcibase = {
    .name = "8250_pericom_pci",
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
