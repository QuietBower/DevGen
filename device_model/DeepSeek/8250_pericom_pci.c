/* Complete QEMU C device model for Pericom serial controller */
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

/* Pericom UART state per port */
typedef struct {
    /* Divisor latch bytes */
    uint8_t dll;        /* Divisor Latch Low (DLAB=1, offset 0) */
    uint8_t dlm;        /* Divisor Latch High (DLAB=1, offset 1) */
    /* Standard registers */
    uint8_t ier;        /* Interrupt Enable Register (DLAB=0, offset 1) */
    uint8_t fcr;        /* FIFO Control Register (DLAB=0, offset 2 write) */
    uint8_t lcr;        /* Line Control Register (offset 3) */
    uint8_t mcr;        /* Modem Control Register (offset 4) */
    uint8_t lsr;        /* Line Status Register (offset 5) */
    uint8_t msr;        /* Modem Status Register (offset 6) */
    uint8_t scr;        /* Scratch Register (offset 7) */

    /* Receiver/Transmitter buffer */
    uint8_t thr;        /* Transmitter Holding Register (DLAB=0, offset 0 write) */
    uint8_t rbr;        /* Receiver Buffer Register (DLAB=0, offset 0 read) */

    /* Custom prescaler register (offset 2 when DLAB=1) */
    uint8_t ps;         /* Prescaler value written by driver */

    bool fifo_enabled;  /* FIFO enabled */

    /* Interrupt state */
    bool irq_pending;   /* whether this port has an unmasked pending interrupt */
    uint8_t iir;        /* cached IIR value for read */
} PericomUART;

#define MAX_UART_PORTS 4

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    PericomUART uart[MAX_UART_PORTS];
    int nr_ports;       /* actual number of ports (from PCI device ID) */
};

/* Helper to update interrupt line after any change */
static void pericom_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    int i;
    bool irq = false;

    for (i = 0; i < s->nr_ports; i++) {
        PericomUART *u = &s->uart[i];
        if (u->irq_pending && (u->mcr & 0x08)) { /* OUT2 enables interrupts */
            irq = true;
            break;
        }
    }
    pci_set_irq(pdev, irq);
}

/* Update interrupt status for a single port: recompute pending conditions and IIR */
static void pericom_port_update_irq(PCIBaseState *s, int port_idx)
{
    PericomUART *u = &s->uart[port_idx];
    uint8_t iir = 0x01; /* default: no interrupt */

    /* Check potential interrupt sources: RDI, THRE, RLSI, MSI in priority order */
    /* THRE: THR empty and IER bit0 */
    if ((u->ier & 0x02) && (u->lsr & 0x20)) { /* THR empty and IER_THRI */
        iir = 0x02; /* THRE interrupt ID */
    }
    /* RDI: data ready and IER bit0 */
    if ((u->ier & 0x01) && (u->lsr & 0x01)) { /* data ready and IER_RDI */
        iir = 0x04; /* RDI interrupt ID */
    }
    /* RLSI: overrun, parity, framing, break and IER bit2 */
    if ((u->ier & 0x04) && (u->lsr & 0x1E)) { /* any line status error and IER_RLSI */
        iir = 0x06; /* RLSI interrupt ID */
    }
    /* MSI: modem status delta and IER bit3 */
    if ((u->ier & 0x08) && (u->msr & 0x0F)) { /* any modem delta and IER_MSI */
        iir = 0x00; /* MSI interrupt ID */
    }

    if (u->fifo_enabled) {
        /* Add FIFO identification bits: 64-byte FIFO -> bits 7-6 = 11 */
        iir |= 0xC0;
        /* FIFO enabled: bit 0 clear when interrupt pending */
        /* If a pending interrupt, bit 0 must be 0; we already set iir to non-zero if pending */
        if (iir != 0x01) {
            iir &= ~0x01;  /* clear bit 0 to indicate interrupt */
        }
    } else {
        /* FIFO disabled: bits 7-6 = 00 */
        if (iir != 0x01) {
            iir &= ~0x01;  /* interrupt present if ID not 0x01 */
        }
    }

    u->iir = iir;
    u->irq_pending = (iir & 0x01) == 0; /* bit 0 clear means interrupt */
}

/* Update all ports and then global IRQ */
static void pericom_update_all_irqs(PCIBaseState *s)
{
    int i;
    for (i = 0; i < s->nr_ports; i++) {
        pericom_port_update_irq(s, i);
    }
    pericom_update_irq(s);
}

static const hwaddr port_offsets_map[MAX_UART_PORTS] = {
    0x00, 0x08, 0x10, 0x38
};

static int pericom_port_from_addr(hwaddr addr, hwaddr *port_offset)
{
    int i;
    for (i = 0; i < MAX_UART_PORTS; i++) {
        if (addr >= port_offsets_map[i] && addr < port_offsets_map[i] + 8) {
            *port_offset = port_offsets_map[i];
            return i;
        }
    }
    return -1;
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = ~0ULL;
    hwaddr port_base;
    int port_idx = pericom_port_from_addr(addr, &port_base);
    if (port_idx < 0 || port_idx >= s->nr_ports) {
        return val;
    }

    PericomUART *u = &s->uart[port_idx];
    unsigned reg = addr - port_base;
    bool dlab = u->lcr & 0x80;

    if (size != 1) {
        /* The 8250 core uses byte accesses only; for multi-byte, return 0xFFFFFFFF */
        return val;
    }

    switch (reg) {
    case 0:
        if (dlab) {
            val = u->dll;
        } else {
            val = u->rbr;  /* reading RBR */
            /* After reading, clear data ready */
            u->lsr &= ~0x01;
            pericom_port_update_irq(s, port_idx);
        }
        break;
    case 1:
        if (dlab) {
            val = u->dlm;
        } else {
            val = u->ier;
        }
        break;
    case 2:
        if (dlab) {
            val = u->ps;   /* Prescaler register */
        } else {
            val = u->iir;  /* Read IIR */
            /* Reading IIR clears the THRE interrupt condition */
            if ((u->iir & 0x0E) == 0x02) {  /* If the reported interrupt was THRE */
                /* The THRE condition is cleared by read; we assume the condition goes away temporarily */
                /* We defer re-evaluation; update to re-arm if still true */
            }
        }
        break;
    case 3:
        val = u->lcr;
        break;
    case 4:
        val = u->mcr;
        break;
    case 5:
        val = u->lsr;
        break;
    case 6:
        val = u->msr;
        /* Reading MSR clears modem delta bits */
        u->msr &= 0xF0;
        pericom_port_update_irq(s, port_idx);
        break;
    case 7:
        val = u->scr;
        break;
    default:
        val = 0;
        break;
    }

    pericom_update_all_irqs(s);
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    hwaddr port_base;
    int port_idx = pericom_port_from_addr(addr, &port_base);
    if (port_idx < 0 || port_idx >= s->nr_ports || size != 1) {
        return;
    }

    PericomUART *u = &s->uart[port_idx];
    unsigned reg = addr - port_base;
    bool dlab = u->lcr & 0x80;

    switch (reg) {
    case 0:
        if (dlab) {
            u->dll = val & 0xFF;
        } else {
            /* Write to THR */
            u->thr = val & 0xFF;
            /* In loopback mode, copy to RBR and set data ready */
            if (u->mcr & 0x10) {
                u->rbr = u->thr;
                u->lsr |= 0x01;  /* Data ready */
            }
            /* THR empty: we set it always high; the write does not clear it in our model */
            pericom_port_update_irq(s, port_idx);
        }
        break;
    case 1:
        if (dlab) {
            u->dlm = val & 0xFF;
        } else {
            u->ier = val & 0xFF;
            pericom_port_update_irq(s, port_idx);
        }
        break;
    case 2:
        if (dlab) {
            u->ps = val & 0xFF;   /* Prescaler register */
        } else {
            u->fcr = val & 0xFF;
            u->fifo_enabled = u->fcr & 0x01;
            /* If FCR bit1 (RCVR FIFO reset) set, clear receiver FIFO (not modeled) */
            /* If FCR bit2 (XMTR FIFO reset) set, clear transmitter FIFO */
            pericom_port_update_irq(s, port_idx);
        }
        break;
    case 3:
        u->lcr = val & 0xFF;
        break;
    case 4:
        u->mcr = val & 0xFF;
        /* Loopback mode: if LOOP set, connect MCR outputs to MSR inputs */
        if (u->mcr & 0x10) {
            /* Update MSR bits 4-7 based on MCR bits 0-3 */
            u->msr = (u->msr & 0x0F) | /* preserve delta bits */
                     ((u->mcr & 0x01) ? 0x10 : 0) | /* CTS <- RTS */
                     ((u->mcr & 0x02) ? 0x20 : 0) | /* DSR <- DTR */
                     ((u->mcr & 0x04) ? 0x40 : 0) | /* RI  <- OUT1 */
                     ((u->mcr & 0x08) ? 0x80 : 0);  /* DCD <- OUT2 */
        }
        /* OUT2 change may affect IRQ */
        pericom_port_update_irq(s, port_idx);
        break;
    case 5:
        /* LSR is read-only; ignore writes */
        break;
    case 6:
        /* MSR is read-only; ignore writes */
        break;
    case 7:
        u->scr = val & 0xFF;
        break;
    default:
        break;
    }
    pericom_update_all_irqs(s);
}

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 1 },
    .impl  = { .min_access_size = 1, .max_access_size = 1 },
};

static void pericom_port_reset(PericomUART *u)
{
    u->dll = 0;
    u->dlm = 0;
    u->ier = 0;
    u->fcr = 0;
    u->lcr = 0;
    u->mcr = 0;
    u->lsr = 0x60;  /* THR empty and TEMT */
    u->msr = 0;
    u->scr = 0;
    u->thr = 0;
    u->rbr = 0;
    u->ps = 0;
    u->fifo_enabled = false;
    u->irq_pending = false;
    u->iir = 0x01;
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    int i;
    pci_device_reset(PCI_DEVICE(dev));
    for (i = 0; i < s->nr_ports; i++) {
        pericom_port_reset(&s->uart[i]);
    }
    pericom_update_all_irqs(s);
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, 0x12D8);
    pci_set_word(pci_conf + PCI_DEVICE_ID, 0x7951);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_COMMUNICATION_SERIAL);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);

    s->nr_ports = pci_get_word(pci_conf + PCI_DEVICE_ID) & 0x0f;
    if (s->nr_ports < 1) s->nr_ports = 1;
    if (s->nr_ports > MAX_UART_PORTS) s->nr_ports = MAX_UART_PORTS;

    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 0x40;
    s->bar_info[0].name = "pericom-pio";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
}

static const VMStateDescription vmstate_pericom_uart = {
    .name = "pericom_uart",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(dll, PericomUART),
        VMSTATE_UINT8(dlm, PericomUART),
        VMSTATE_UINT8(ier, PericomUART),
        VMSTATE_UINT8(fcr, PericomUART),
        VMSTATE_UINT8(lcr, PericomUART),
        VMSTATE_UINT8(mcr, PericomUART),
        VMSTATE_UINT8(lsr, PericomUART),
        VMSTATE_UINT8(msr, PericomUART),
        VMSTATE_UINT8(scr, PericomUART),
        VMSTATE_UINT8(thr, PericomUART),
        VMSTATE_UINT8(rbr, PericomUART),
        VMSTATE_UINT8(ps, PericomUART),
        VMSTATE_BOOL(fifo_enabled, PericomUART),
        VMSTATE_UINT8(iir, PericomUART),
        VMSTATE_BOOL(irq_pending, PericomUART),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_pcibase = {
    .name = "8250_pericom_pci",
    .version_id = 2,
    .minimum_version_id = 2,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_INT32(nr_ports, PCIBaseState),
        VMSTATE_STRUCT_ARRAY(uart, PCIBaseState, MAX_UART_PORTS, 0, vmstate_pericom_uart, PericomUART),
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
