/*
 * QEMU PCI device model for Intel EG20T PCH UART (simplified)
 * Generated to satisfy Linux drivers/tty/serial/pch_uart.c expectations
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

#define TYPE_PCIBASE_DEVICE "pch_uart_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Basic IDs */
#define PCH_UART_VENDOR_ID PCI_VENDOR_ID_INTEL
#define PCH_UART_DEVICE_ID 0x8811
#define PCH_UART_CLASS_ID  PCI_CLASS_COMMUNICATION_SERIAL

/* The driver uses the standard 16550A layout via linux/serial_reg.h macros
 * UART_RBR 0, UART_THR 0, UART_IER 1, UART_IIR 2, UART_FCR 2,
 * UART_LCR 3, UART_MCR 4, UART_LSR 5, UART_MSR 6, UART_SCR 7,
 * UART_DLL 0, UART_DLM 1, etc.
 * It also uses additional EG20T-specific BRCSR at offset 0x0E.
 */

/* PCH-specific defines from the stage-1 template (kept for consistency) */
#define PCH_UART_RBR              0x00
#define PCH_UART_THR              0x00
#define PCH_UART_IER_ERBFI        0x00000001
#define PCH_UART_IER_ETBEI        0x00000002
#define PCH_UART_IER_ELSI         0x00000004
#define PCH_UART_IER_EDSSI        0x00000008
#define PCH_UART_IER_MASK         (PCH_UART_IER_ERBFI | PCH_UART_IER_ETBEI | \
                                   PCH_UART_IER_ELSI | PCH_UART_IER_EDSSI)
#define PCH_UART_IIR_IP           0x00000001
#define PCH_UART_IIR_IID          0x00000006
#define PCH_UART_IIR_MSI          0x00000000
#define PCH_UART_IIR_TRI          0x00000002
#define PCH_UART_IIR_RRI          0x00000004
#define PCH_UART_IIR_REI          0x00000006
#define PCH_UART_IIR_TOI          0x00000008
#define PCH_UART_IIR_FIFO256      0x00000020
#define PCH_UART_IIR_FIFO64       PCH_UART_IIR_FIFO256
#define PCH_UART_IIR_FE           0x000000C0
#define PCH_UART_FCR_FIFOE        0x00000001
#define PCH_UART_FCR_RFR          0x00000002
#define PCH_UART_FCR_TFR          0x00000004
#define PCH_UART_FCR_DMS          0x00000008
#define PCH_UART_FCR_FIFO256      0x00000020
#define PCH_UART_FCR_RFTL         0x000000C0
#define PCH_UART_FCR_RFTL_SHIFT   6
#define PCH_UART_LCR_WLS          0x00000003
#define PCH_UART_LCR_STB          0x00000004
#define PCH_UART_LCR_PEN          0x00000008
#define PCH_UART_LCR_EPS          0x00000010
#define PCH_UART_LCR_SP           0x00000020
#define PCH_UART_LCR_SB           0x00000040
#define PCH_UART_LCR_DLAB         0x00000080
#define PCH_UART_LCR_5BIT         0x00000000
#define PCH_UART_LCR_6BIT         0x00000001
#define PCH_UART_LCR_7BIT         0x00000002
#define PCH_UART_LCR_8BIT         0x00000003
#define PCH_UART_MCR_DTR          0x00000001
#define PCH_UART_MCR_RTS          0x00000002
#define PCH_UART_MCR_OUT          0x0000000C
#define PCH_UART_MCR_LOOP         0x00000010
#define PCH_UART_MCR_AFE          0x00000020
#define PCH_UART_LSR_DR           0x00000001
#define PCH_UART_LSR_ERR          (1 << 7)
#define PCH_UART_MSR_DCTS         0x00000001
#define PCH_UART_MSR_DDSR         0x00000002
#define PCH_UART_MSR_TERI         0x00000004
#define PCH_UART_MSR_DDCD         0x00000008
#define PCH_UART_MSR_CTS          0x00000010
#define PCH_UART_MSR_DSR          0x00000020
#define PCH_UART_MSR_RI           0x00000040
#define PCH_UART_MSR_DCD          0x00000080
#define PCH_UART_MSR_DELTA        (PCH_UART_MSR_DCTS | PCH_UART_MSR_DDSR | \
                                   PCH_UART_MSR_TERI | PCH_UART_MSR_DDCD)
#define PCH_UART_DLL              0x00
#define PCH_UART_DLM              0x01
#define PCH_UART_BRCSR            0x0E

#define PCH_REGS_BUFSIZE          1024

/* Standard UART register offsets (byte offsets) */
#define UART_RBR 0
#define UART_THR 0
#define UART_IER 1
#define UART_IIR 2
#define UART_FCR 2
#define UART_LCR 3
#define UART_MCR 4
#define UART_LSR 5
#define UART_MSR 6
#define UART_SCR 7
#define UART_DLL 0
#define UART_DLM 1

/* Bits for LSR, MSR as used by the driver via linux/serial_reg.h */
#define UART_LSR_THRE      0x20
#define UART_LSR_TEMT      0x40
#define UART_LSR_BI        0x10
#define UART_LSR_FE        0x08
#define UART_LSR_PE        0x04
#define UART_LSR_OE        0x02
#define UART_LSR_DR        0x01
#define UART_LSR_BOTH_EMPTY (UART_LSR_TEMT | UART_LSR_THRE)

#define UART_MSR_DCTS      0x01
#define UART_MSR_DDSR      0x02
#define UART_MSR_TERI      0x04
#define UART_MSR_DDCD      0x08
#define UART_MSR_CTS       0x10
#define UART_MSR_DSR       0x20
#define UART_MSR_RI        0x40
#define UART_MSR_DCD       0x80
#define UART_MSR_ANY_DELTA 0x0F

#define UART_MCR_DTR       0x01
#define UART_MCR_RTS       0x02
#define UART_MCR_OUT1      0x04
#define UART_MCR_OUT2      0x08
#define UART_MCR_LOOP      0x10
#define UART_MCR_AFE       0x20

/* Interrupt ID values as used by driver:
 * PCH_UART_IID_RLS, PCH_UART_IID_RDR, PCH_UART_IID_RDR_TO, PCH_UART_IID_THRE, PCH_UART_IID_MS
 * Not defined numerically in provided snippet; we keep only IID/IP bits behaviour.
 */

#define PCH_UART_NR               4

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

    /* Resources */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;

    /* Register shadows */
    uint32_t regs_buf[PCH_REGS_BUFSIZE / sizeof(uint32_t)];

    /* UART-specific state */
    uint8_t rbr;   /* receive buffer */
    uint8_t thr;   /* transmit holding */
    uint8_t ier;   /* interrupt enable */
    uint8_t iir;   /* interrupt identification (read-only) */
    uint8_t fcr;   /* fifo control (write-only) */
    uint8_t lcr;   /* line control */
    uint8_t mcr;   /* modem control */
    uint8_t lsr;   /* line status */
    uint8_t msr;   /* modem status */
    uint8_t scr;   /* scratch */
    uint8_t dll;
    uint8_t dlm;
    uint8_t brcsr;

    /* simple receive buffer for polled input */
    uint8_t rx_buf[16];
    int rx_buf_head;
    int rx_buf_tail;

    /* interrupt line state */
    bool irq_level;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* IIR bit0=1 => no interrupt pending; 0 => pending.
     * For simplification we trigger a generic RDR interrupt when DR set & enabled,
     * or THRE interrupt when THRE set & ETBEI enabled.
     */
    uint8_t iid = 0;
    bool pending = false;

    if ((s->lsr & UART_LSR_DR) && (s->ier & PCH_UART_IER_ERBFI)) {
        /* Received Data Ready */
        iid = PCH_UART_IIR_RRI; /* matches RDR in driver */
        pending = true;
    } else if ((s->lsr & UART_LSR_THRE) && (s->ier & PCH_UART_IER_ETBEI)) {
        /* THR empty */
        iid = PCH_UART_IIR_TRI; /* matches THRE in driver */
        pending = true;
    }

    if (pending) {
        s->iir = iid;          /* bit0 cleared implicitly (IP=0) */
    } else {
        s->iir = PCH_UART_IIR_IP; /* set IP bit => no interrupt */
    }

    bool level = pending;
    if (level != s->irq_level) {
        s->irq_level = level;
        pci_set_irq(pdev, level ? 1 : 0);
    }
}

/* No driver-visible device-initiated DMA over PCI; keep helper empty. */
__attribute__((unused)) static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    (void)s;
    (void)is_write;
}

static inline void uart_reset_state(PCIBaseState *s)
{
    s->rbr = 0;
    s->thr = 0;
    s->ier = 0;
    s->iir = PCH_UART_IIR_IP; /* no interrupt pending */
    s->fcr = 0;
    s->lcr = 0;
    s->mcr = 0;
    s->lsr = UART_LSR_TEMT | UART_LSR_THRE; /* empty TX */
    s->msr = 0;
    s->scr = 0;
    s->dll = 0;
    s->dlm = 0;
    s->brcsr = 0;
    s->rx_buf_head = s->rx_buf_tail = 0;
    s->irq_level = false;
}

static uint8_t uart_receive_pop(PCIBaseState *s)
{
    if (s->rx_buf_head == s->rx_buf_tail) {
        /* no data */
        s->lsr &= ~(UART_LSR_DR);
        pcibase_update_irq(s);
        return 0;
    }
    uint8_t ch = s->rx_buf[s->rx_buf_tail];
    s->rx_buf_tail = (s->rx_buf_tail + 1) % sizeof(s->rx_buf);
    if (s->rx_buf_head == s->rx_buf_tail) {
        s->lsr &= ~(UART_LSR_DR);
    }
    pcibase_update_irq(s);
    return ch;
}

static void uart_receive_push(PCIBaseState *s, uint8_t ch)
{
    int next = (s->rx_buf_head + 1) % sizeof(s->rx_buf);
    if (next == s->rx_buf_tail) {
        /* drop if full; set overrun error */
        s->lsr |= UART_LSR_OE;
        return;
    }
    s->rx_buf[s->rx_buf_head] = ch;
    s->rx_buf_head = next;
    s->lsr |= UART_LSR_DR;
    pcibase_update_irq(s);
}

/* MMIO/PIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint8_t val8 = 0xFF;

    if (size != 1) {
        /* driver only uses byte accessors */
        return 0xFF;
    }

    switch (addr & 0xFF) {
    case UART_RBR: /* 0 - RBR/THR/DLL */
        if (s->lcr & PCH_UART_LCR_DLAB) {
            val8 = s->dll;
        } else {
            val8 = uart_receive_pop(s);
            s->rbr = val8;
        }
        break;
    case UART_IER: /* 1 - IER/DLM */
        if (s->lcr & PCH_UART_LCR_DLAB) {
            val8 = s->dlm;
        } else {
            val8 = s->ier & 0x0F;
        }
        break;
    case UART_IIR: /* 2, read-only */
        val8 = s->iir;
        break;
    case UART_LCR: /* 3 */
        val8 = s->lcr;
        break;
    case UART_MCR: /* 4 */
        val8 = s->mcr;
        break;
    case UART_LSR: /* 5 */
        val8 = s->lsr;
        break;
    case UART_MSR: /* 6 */
        val8 = s->msr;
        /* clear delta bits on read */
        s->msr &= ~UART_MSR_ANY_DELTA;
        break;
    case UART_SCR: /* 7 */
        val8 = s->scr;
        break;
    case PCH_UART_BRCSR: /* 0x0E */
        val8 = s->brcsr;
        break;
    default:
        /* fall back to regs_buf shadow within first 1KB */
        if (addr < PCH_REGS_BUFSIZE) {
            uint32_t v = s->regs_buf[addr >> 2];
            int shift = (addr & 3) * 8;
            val8 = (v >> shift) & 0xFF;
        } else {
            val8 = 0xFF;
        }
        break;
    }

    return val8;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint8_t v = (uint8_t)val;

    if (size != 1) {
        return;
    }

    switch (addr & 0xFF) {
    case UART_THR: /* 0 - THR/DLL */
        if (s->lcr & PCH_UART_LCR_DLAB) {
            s->dll = v;
        } else {
            /* transmit character; we simply mark THR empty and TEMT */
            s->thr = v;
            s->lsr |= UART_LSR_THRE | UART_LSR_TEMT;
            /* For very simple loopback when LOOP set, push to RX */
            if (s->mcr & UART_MCR_LOOP) {
                uart_receive_push(s, v);
            }
            pcibase_update_irq(s);
        }
        break;
    case UART_IER: /* 1 - IER/DLM */
        if (s->lcr & PCH_UART_LCR_DLAB) {
            s->dlm = v;
        } else {
            s->ier = v & PCH_UART_IER_MASK;
            pcibase_update_irq(s);
        }
        break;
    case UART_FCR: /* 2 */
        /* store but only bits implemented are FIFO enable/reset & dma */
        s->fcr = v;
        /* reset FIFOs when RFR/TFR set */
        if (v & PCH_UART_FCR_RFR) {
            s->rx_buf_head = s->rx_buf_tail = 0;
            s->lsr &= ~(UART_LSR_DR | UART_LSR_OE | UART_LSR_FE | UART_LSR_PE);
        }
        if (v & PCH_UART_FCR_TFR) {
            s->lsr |= UART_LSR_THRE | UART_LSR_TEMT;
        }
        pcibase_update_irq(s);
        break;
    case UART_LCR: /* 3 */
        s->lcr = v;
        break;
    case UART_MCR: /* 4 */
        s->mcr = v;
        break;
    case UART_LSR: /* 5, write ignored */
        break;
    case UART_MSR: /* 6, write ignored */
        break;
    case UART_SCR: /* 7 */
        s->scr = v;
        break;
    case PCH_UART_BRCSR: /* 0x0E */
        s->brcsr = v;
        break;
    default:
        if (addr < PCH_REGS_BUFSIZE) {
            uint32_t *p = &s->regs_buf[addr >> 2];
            int shift = (addr & 3) * 8;
            uint32_t mask = 0xFFu << shift;
            *p = (*p & ~mask) | ((uint32_t)v << shift);
        }
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* The driver uses UPIO_PORT and ioread8 on membase from BAR1,
     * but in QEMU we only expose BAR0 MMIO of 1KB. Keep PIO handler
     * as alias of MMIO for completeness, though it will not normally
     * be used by this driver in a guest.
     */
    return pcibase_mmio_read(opaque, addr, size);
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    pcibase_mmio_write(opaque, addr, val, size);
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 1 },
    .impl  = { .min_access_size = 1, .max_access_size = 1 },
};

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 1 },
    .impl  = { .min_access_size = 1, .max_access_size = 1 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    uart_reset_state(s);
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCH_UART_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCH_UART_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCH_UART_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = PCH_REGS_BUFSIZE;
    s->bar_info[0].name = "pch-uart-bar0";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = NULL;
    }

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = false;
    s->has_msix = false;

    for (size_t i = 0; i < (PCH_REGS_BUFSIZE / sizeof(uint32_t)); i++) {
        s->regs_buf[i] = 0;
    }

    uart_reset_state(s);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, 0);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

static const VMStateDescription vmstate_pcibase = {
    .name = "pch_uart_pci",
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
