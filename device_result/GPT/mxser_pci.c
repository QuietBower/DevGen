/*
 * QEMU PCI device model for Moxa mxser multiport serial card
 *
 * Implementation Phase (Stage 2)
 *
 * This model only implements behavior that is explicitly visible
 * in the Linux driver drivers/tty/mxser.c.
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

#define TYPE_PCIBASE_DEVICE "mxser_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* ------------------------------------------------------------------ */
/* Constants copied from Stage 1 (subset actually used in model)      */
/* ------------------------------------------------------------------ */
#define WAKEUP_CHARS        256
#define OP_MODE_MASK        3
#define RS232_MODE          0
#define MOXA            0x400
#define MOXA_SET_OP_MODE    (MOXA + 66)
#define MOXA_GET_OP_MODE    (MOXA + 67)
#define RS485_2WIRE_MODE    1
#define RS422_MODE      2
#define RS485_4WIRE_MODE    3
#define MOXA_MUST_ENTER_ENHANCED    0xBF
#define MOXA_MUST_GDL_REGISTER      0x07
#define MOXA_MUST_GDL_MASK      0x7F
#define MOXA_MUST_GDL_HAS_BAD_DATA  0x80
#define MOXA_MUST_LSR_RERR      0x80
#define MOXA_MUST_EFR_REGISTER      0x02
#define MOXA_MUST_EFR_EFRB_ENABLE   0x10
#define MOXA_MUST_EFR_BANK0     0x00
#define MOXA_MUST_EFR_BANK1     0x40
#define MOXA_MUST_EFR_BANK2     0x80
#define MOXA_MUST_EFR_BANK3     0xC0
#define MOXA_MUST_EFR_BANK_MASK     0xC0
#define MOXA_MUST_XON1_REGISTER     0x04
#define MOXA_MUST_XON2_REGISTER     0x05
#define MOXA_MUST_XOFF1_REGISTER    0x06
#define MOXA_MUST_XOFF2_REGISTER    0x07
#define MOXA_MUST_RBRTL_REGISTER    0x04
#define MOXA_MUST_RBRTH_REGISTER    0x05
#define MOXA_MUST_RBRTI_REGISTER    0x06
#define MOXA_MUST_THRTL_REGISTER    0x07
#define MOXA_MUST_ENUM_REGISTER     0x04
#define MOXA_MUST_HWID_REGISTER      0x05
#define MOXA_MUST_ECR_REGISTER      0x06
#define MOXA_MUST_CSR_REGISTER      0x07
#define MOXA_MUST_FCR_GDA_MODE_ENABLE   0x20
#define MOXA_MUST_FCR_GDA_ONLY_ENABLE   0x10
#define MOXA_MUST_IER_ECTSI      0x80
#define MOXA_MUST_IER_ERTSI      0x40
#define MOXA_MUST_IER_XINT       0x20
#define MOXA_MUST_IER_EGDAI      0x10
#define MOXA_MUST_RECV_ISR       (UART_IER_RDI | MOXA_MUST_IER_EGDAI)
#define MOXA_MUST_IIR_GDA        0x1C
#define MOXA_MUST_IIR_RDA        0x04
#define MOXA_MUST_IIR_RTO        0x0C
#define MOXA_MUST_IIR_LSR        0x06
#define MOXA_MUST_IIR_XSC        0x10
#define MOXA_MUST_IIR_RTSCTS     0x20
#define MOXA_MUST_IIR_MASK       0x3E
#define MOXA_MUST_MCR_XON_FLAG       0x40
#define MOXA_MUST_MCR_XON_ANY        0x80
#define MOXA_MUST_MCR_TX_XON     0x08
#define MOXA_MUST_EFR_SF_MASK        0x0F
#define MOXA_MUST_EFR_SF_TX1     0x08
#define MOXA_MUST_EFR_SF_TX2     0x04
#define MOXA_MUST_EFR_SF_TX12        0x0C
#define MOXA_MUST_EFR_SF_TX_NO       0x00
#define MOXA_MUST_EFR_SF_TX_MASK     0x0C
#define MOXA_MUST_EFR_SF_RX_NO       0x00
#define MOXA_MUST_EFR_SF_RX1     0x02
#define MOXA_MUST_EFR_SF_RX2     0x01
#define MOXA_MUST_EFR_SF_RX12        0x03
#define MOXA_MUST_EFR_SF_RX_MASK     0x03
#define MXSER_BOARDS        4
#define MXSER_PORTS_PER_BOARD    8
#define MXSER_PORTS        (MXSER_BOARDS * MXSER_PORTS_PER_BOARD)
#define MXSER_ISR_PASS_LIMIT 100
#define MXSER_BAUD_BASE     921600
#define PCI_DEVICE_ID_MOXA_RC7000   0x0001
#define PCI_VENDOR_ID_MOXA      0x1393
#define PCI_CLASS_COMMUNICATION_SERIAL  0x0700

#define MXSER_IO_BAR_INDEX      2
#define MXSER_VECTOR_BAR_INDEX  3

/* UART core register offsets (8250 style, only what driver uses) */
#define UART_RX              0   /* RBR */
#define UART_TX              0   /* THR */
#define UART_IER             1
#define UART_IIR             2
#define UART_FCR             2
#define UART_LCR             3
#define UART_MCR             4
#define UART_LSR             5
#define UART_MSR             6
#define UART_SCR             7
#define UART_DLL             0
#define UART_DLM             1

/* bits */
#define UART_IER_RDI        0x01
#define UART_IER_THRI       0x02
#define UART_IER_RLSI       0x04
#define UART_IER_MSI        0x08

#define UART_IIR_NO_INT     0x01

#define UART_FCR_CLEAR_RCVR  0x02
#define UART_FCR_CLEAR_XMIT  0x04

#define UART_MCR_DTR         0x01
#define UART_MCR_RTS         0x02

#define UART_LSR_DR          0x01
#define UART_LSR_OE          0x02
#define UART_LSR_PE          0x04
#define UART_LSR_FE          0x08
#define UART_LSR_BI          0x10
#define UART_LSR_THRE        0x20
#define UART_LSR_TEMT        0x40
#define UART_LSR_ERR         0x80

#define UART_MSR_DCTS        0x01
#define UART_MSR_DDSR        0x02
#define UART_MSR_TERI        0x04
#define UART_MSR_DDCD        0x08
#define UART_MSR_CTS         0x10
#define UART_MSR_DSR         0x20
#define UART_MSR_RI          0x40
#define UART_MSR_DCD         0x80

/* masking used in driver */
#define UART_MSR_ANY_DELTA (UART_MSR_DCTS|UART_MSR_DDSR|UART_MSR_TERI|UART_MSR_DDCD)
#define UART_LSR_BRK_ERROR_BITS (UART_LSR_BI|UART_LSR_FE|UART_LSR_PE|UART_LSR_OE)

#define UART_LCR_WLEN8       0x03
#define UART_LCR_DLAB        0x80
#define UART_LCR_SBC         0x40

#define MXSER_IO_STRIDE      8

/* Additional definitions from new driver fragment */
#define PORT_16550A    4
#define PORT_16450 2
#define PORT_8250  1
#define UART_LCR_WLEN(x)    ((x) - 5)
#define UART_LCR_STOP       0x04
#define UART_LCR_PARITY     0x08
#define UART_LCR_EPAR       0x10
#define UART_LCR_SPAR       0x20
#define UART_FCR_ENABLE_FIFO    0x01
#define UART_FCR_TRIGGER_1  0x00
#define UART_FCR_TRIGGER_4  0x40
#define UART_FCR_TRIGGER_8  0x80
#define UART_FCR_TRIGGER_14 0xC0
#define UART_MCR_AFE        0x20
/* UART_MSR_ANY_DELTA already defined above; keep original definition. */
#define BIT(nr)         (1UL << (nr))

/* BAR metadata definition */
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

/* Per-UART internal state (emulated) */
typedef struct MXUARTState {
    /* 8 basic registers, plus some MUST-specific shadow regs */
    uint8_t rbr;   /* receive buffer */
    uint8_t thr;   /* transmit holding */
    uint8_t ier;
    uint8_t iir;
    uint8_t fcr;
    uint8_t lcr;
    uint8_t mcr;
    uint8_t lsr;
    uint8_t msr;
    uint8_t scr;

    /* Divisor latches */
    uint8_t dll;
    uint8_t dlm;

    /* MUST enhanced feature registers (banked at EFR/XON/XOFF/etc) */
    uint8_t efr;
    uint8_t must_xon1;
    uint8_t must_xoff1;
    uint8_t must_rbrth;
    uint8_t must_rbrti;
    uint8_t must_rbrtl;
    uint8_t must_enum;
    uint8_t must_hwid;

    /* simple FIFO modeling: count of bytes available to read */
    uint8_t rx_count;
    uint8_t tx_count;

    /* interrupt pending flags */
    bool recv_irq_pending;
    bool xmit_irq_pending;
} MXUARTState;

/* Board state approximated from mxser_board */
typedef struct MXBoardState {
    unsigned short nports;
    int irq;                 /* PCI IRQ line */
    unsigned long vector_base; /* BAR3 base offset in our PIO region */
    uint8_t vector_bits;     /* current vector register value */

    uint8_t must_hwid;       /* enum mxser_must_hwid, simplified */
    MXUARTState ports[MXSER_PORTS_PER_BOARD];
} MXBoardState;

struct PCIBaseState {
    PCIDevice parent_obj;

    /* BAR memory regions (PIO only used) */
    MemoryRegion bar_regions[6];

    uint8_t *mmio_backing;
    size_t mmio_backing_size;

    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;

    /* emulated mxser board (single board instance) */
    MXBoardState board;
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
/* MemoryRegionOps                                                    */
/* ------------------------------------------------------------------ */
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
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* ------------------------------------------------------------------ */
/* Helper: raise/lower PCI interrupt based on board vector bits       */
/* ------------------------------------------------------------------ */
static void mxser_update_irq(PCIBaseState *s)
{
    /* Driver's ISR reads vector (BAR3) and expects bits cleared for
     * active ports. For simplicity we assert INTx when any port has
     * receive or transmit interrupt pending.
     */
    bool set = false;
    unsigned int i;

    for (i = 0; i < s->board.nports; i++) {
        MXUARTState *u = &s->board.ports[i];
        if ((u->ier & (UART_IER_RDI | UART_IER_RLSI)) && u->recv_irq_pending) {
            set = true;
            break;
        }
        if ((u->ier & UART_IER_THRI) && u->xmit_irq_pending) {
            set = true;
            break;
        }
    }

    pci_set_irq(PCI_DEVICE(s), set ? 1 : 0);
}

/* ------------------------------------------------------------------ */
/* Helper: recompute per-port IIR and board vector bits                */
/* ------------------------------------------------------------------ */
static void mxser_recalc_port_irq(PCIBaseState *s, unsigned int portno)
{
    MXUARTState *u;

    if (portno >= s->board.nports) {
        return;
    }

    u = &s->board.ports[portno];

    /* Default: no interrupt */
    u->iir = UART_IIR_NO_INT;

    /* Receive first */
    if ((u->ier & (UART_IER_RDI | UART_IER_RLSI)) && u->recv_irq_pending) {
        u->iir = 0x04; /* RDA */
    } else if ((u->ier & UART_IER_THRI) && u->xmit_irq_pending) {
        u->iir = 0x02; /* THRE */
    }

    /* Update vector bits: 0 means interrupt pending on that port.
     * Driver expects reading vector & mask, loop until all bits 1.
     */
    if (u->iir == UART_IIR_NO_INT) {
        s->board.vector_bits |= (1u << portno);
    } else {
        s->board.vector_bits &= ~(1u << portno);
    }

    mxser_update_irq(s);
}

/* ------------------------------------------------------------------ */
/* Helper: push one received byte into port, set LSR/IRQ              */
/* ------------------------------------------------------------------ */
__attribute__((unused)) static void mxser_rx_push_byte(PCIBaseState *s, unsigned int portno, uint8_t ch)
{
    MXUARTState *u;

    if (portno >= s->board.nports) {
        return;
    }

    u = &s->board.ports[portno];
    u->rbr = ch;
    u->lsr |= UART_LSR_DR;
    u->rx_count = 1;
    u->recv_irq_pending = true;
    mxser_recalc_port_irq(s, portno);
}

/* ------------------------------------------------------------------ */
/* Helper: model THR empty -> TX complete interrupt                   */
/* ------------------------------------------------------------------ */
static void mxser_tx_consume_fifo(PCIBaseState *s, unsigned int portno)
{
    MXUARTState *u;

    if (portno >= s->board.nports) {
        return;
    }

    u = &s->board.ports[portno];

    /* For simplicity, transmit completes immediately and sets THRE/TEMT
     * and raises TX interrupt if enabled.
     */
    u->tx_count = 0;
    u->lsr |= UART_LSR_THRE | UART_LSR_TEMT;
    u->xmit_irq_pending = true;
    mxser_recalc_port_irq(s, portno);
}

/* ------------------------------------------------------------------ */
/* Helper: register a BAR (MMIO or PIO)                               */
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
/* MMIO handlers                                                       */
/*
 * The mxser driver only uses BAR2 and BAR3 as I/O port BARs.  No MMIO
 * BARs are accessed, so MMIO handlers remain unimplemented (but present
 * to satisfy infrastructure).
 * ------------------------------------------------------------------ */

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "[%s] unexpected mmio_read addr=%" PRIx64 " size=%u\n",
                  TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
    return 0xff;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "[%s] unexpected mmio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n",
                  TYPE_PCIBASE_DEVICE, (uint64_t)addr, (uint64_t)val, size);
}

/* ------------------------------------------------------------------ */
/* PIO handlers                                                        */
/*
 * BAR2: classic UART channels, 8-byte stride per port.
 * BAR3: "vector" register used by mxser_interrupt to learn which
 *       channels have pending interrupts.
 *
 * The pci_resource_start() the driver sees for BAR2/BAR3 is the base
 * of our respective MemoryRegion, so addr here is the offset within
 * that BAR.
 * ------------------------------------------------------------------ */

static uint8_t mxser_read_uart_reg(PCIBaseState *s, unsigned int portno, unsigned int reg)
{
    MXUARTState *u;

    if (portno >= s->board.nports) {
        return 0xff;
    }

    u = &s->board.ports[portno];

    /* DLAB handling: when LCR.DLAB=1, offsets 0/1 are DLL/DLM */
    if (u->lcr & UART_LCR_DLAB) {
        if (reg == UART_DLL) {
            return u->dll;
        } else if (reg == UART_DLM) {
            return u->dlm;
        }
    }

    switch (reg) {
    case UART_RX:
        /* Data receive. Clear DR when read if we had data. */
        if (u->rx_count) {
            uint8_t ch = u->rbr;
            u->rx_count = 0;
            u->lsr &= ~UART_LSR_DR;
            u->recv_irq_pending = false;
            mxser_recalc_port_irq(s, portno);
            return ch;
        }
        return u->rbr;
    case UART_IER:
        return u->ier;
    case UART_IIR:
        /* IIR's lowest bit is 0 when interrupt pending, 1 otherwise */
        return u->iir;
    case UART_LCR:
        return u->lcr;
    case UART_MCR:
        return u->mcr;
    case UART_LSR:
        return u->lsr;
    case UART_MSR:
        return u->msr;
    case UART_SCR:
        return u->scr;
    default:
        return 0xff;
    }
}

static void mxser_write_uart_reg(PCIBaseState *s, unsigned int portno,
                                 unsigned int reg, uint8_t val)
{
    MXUARTState *u;

    if (portno >= s->board.nports) {
        return;
    }

    u = &s->board.ports[portno];

    /* DLAB handling */
    if (u->lcr & UART_LCR_DLAB) {
        if (reg == UART_DLL) {
            u->dll = val;
            return;
        } else if (reg == UART_DLM) {
            u->dlm = val;
            return;
        }
    }

    switch (reg) {
    case UART_TX:
        /* THR write, start transmit */
        u->thr = val;
        u->tx_count = 1;
        /* Clear THRE/TEMT, will be set when we mark complete. */
        u->lsr &= ~(UART_LSR_THRE | UART_LSR_TEMT);
        /* Immediate completion for simplicity. */
        mxser_tx_consume_fifo(s, portno);
        break;
    case UART_IER:
        u->ier = val;
        mxser_recalc_port_irq(s, portno);
        break;
    case UART_FCR:
        /* Only model clear bits; enable FIFOs and trigger level are
         * accepted but do not affect behaviour in this simplified model.
         */
        u->fcr = val;
        if (val & UART_FCR_CLEAR_RCVR) {
            u->rx_count = 0;
            u->lsr &= ~UART_LSR_DR;
            u->recv_irq_pending = false;
        }
        if (val & UART_FCR_CLEAR_XMIT) {
            u->tx_count = 0;
            u->lsr |= UART_LSR_THRE | UART_LSR_TEMT;
            u->xmit_irq_pending = false;
        }
        mxser_recalc_port_irq(s, portno);
        break;
    case UART_LCR:
        /* MUST enhanced mode magic: entering enhanced when 0xBF.
         * We also store generic word length/stop/parity bits as the
         * driver uses macros like UART_LCR_WLEN(x), UART_LCR_STOP,
         * UART_LCR_PARITY, UART_LCR_EPAR and UART_LCR_SPAR.
         */
        u->lcr = val;
        break;
    case UART_MCR:
        /* Driver may set DTR/RTS and AFE (auto flow control).  We
         * store them but do not implement external signal effects.
         */
        u->mcr = val;
        break;
    case UART_LSR:
        /* read-only in hardware; ignore */
        break;
    case UART_MSR:
        /* read-only; ignore */
        break;
    case UART_SCR:
        u->scr = val;
        break;
    default:
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    /* Find BAR2 and BAR3 sizes */
    hwaddr bar2_size = 0;
    hwaddr bar3_size = 0;
    int i;
    for (i = 0; i < s->num_bars; i++) {
        if (s->bar_info[i].index == MXSER_IO_BAR_INDEX) {
            bar2_size = s->bar_info[i].size;
        } else if (s->bar_info[i].index == MXSER_VECTOR_BAR_INDEX) {
            bar3_size = s->bar_info[i].size;
        }
    }

    /* If within BAR3 range (vector) */
    if (bar3_size && addr < bar3_size && bar3_size <= 4) {
        /* The driver does: irqbits = inb(brd->vector) & mask; */
        /* For simplicity, just return vector_bits as-is. */
        uint8_t val = s->board.vector_bits;
        return val;
    }

    /* Otherwise BAR2: UARTs */
    if (bar2_size && addr < bar2_size) {
        unsigned int portno = addr / MXSER_IO_STRIDE;
        unsigned int reg = addr % MXSER_IO_STRIDE;
        uint8_t v = mxser_read_uart_reg(s, portno, reg);
        return v;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] pio_read addr=%" PRIx64 " size=%u (out of range)\n",
                  TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
    return 0xff;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    hwaddr bar2_size = 0;
    hwaddr bar3_size = 0;
    int i;
    for (i = 0; i < s->num_bars; i++) {
        if (s->bar_info[i].index == MXSER_IO_BAR_INDEX) {
            bar2_size = s->bar_info[i].size;
        } else if (s->bar_info[i].index == MXSER_VECTOR_BAR_INDEX) {
            bar3_size = s->bar_info[i].size;
        }
    }

    if (bar3_size && addr < bar3_size && bar3_size <= 4) {
        /* The driver writes vector+4 and vector+0x0c only for MU860
         * RS232 mode setup. For simplicity we just store low byte.
         */
        s->board.vector_bits = (uint8_t)val;
        mxser_update_irq(s);
        return;
    }

    if (bar2_size && addr < bar2_size) {
        unsigned int portno = addr / MXSER_IO_STRIDE;
        unsigned int reg = addr % MXSER_IO_STRIDE;
        mxser_write_uart_reg(s, portno, reg, (uint8_t)val);
        return;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] pio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u (out of range)\n",
                  TYPE_PCIBASE_DEVICE, (uint64_t)addr, (uint64_t)val, size);
}

/* ------------------------------------------------------------------ */
/* Reset                                                              */
/* ------------------------------------------------------------------ */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);
    unsigned int i;

    pci_device_reset(pdev);

    if (s->mmio_backing && s->mmio_backing_size) {
        memset(s->mmio_backing, 0, s->mmio_backing_size);
    }

    /* Reset board and UART state to power-on defaults */
    s->board.nports = MXSER_PORTS_PER_BOARD;
    s->board.irq = 0; /* QEMU PCIDevice does not expose an irq field */
    s->board.vector_base = 0;
    s->board.vector_bits = (1u << s->board.nports) - 1u; /* all idle */
    s->board.must_hwid = 0; /* MOXA_OTHER_UART */

    for (i = 0; i < s->board.nports; i++) {
        MXUARTState *u = &s->board.ports[i];
        memset(u, 0, sizeof(*u));
        u->lsr = UART_LSR_THRE | UART_LSR_TEMT; /* transmitter empty */
        u->iir = UART_IIR_NO_INT;
        u->must_hwid = 0; /* generic */
    }

    mxser_update_irq(s);
}

/* ------------------------------------------------------------------ */
/* DMA initialize (not used by driver)                                */
/* ------------------------------------------------------------------ */
static void pcibase_dma_device_realize(PCIDevice *pdev, Error **errp)
{
    /* mxser driver does not use DMA; nothing to do. */
    (void)pdev;
    (void)errp;
}

/* ------------------------------------------------------------------ */
/* PCI config space access (class-level hooks)                        */
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
/* Realize (device init)                                              */
/* ------------------------------------------------------------------ */
static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* PCI IDs: first entry from mxser_pcibrds[] is RC7000 */
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_MOXA);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_MOXA_RC7000);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_COMMUNICATION_SERIAL);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Define BARs exactly as driver uses them: BAR2 and BAR3 are I/O. */
    s->num_bars = 0;

    /* BAR2: UART I/O ports. Driver uses ioaddress + 8*i, up to nports.
     * We'll expose 64 bytes: 8 ports * 8-byte stride.
     */
    s->bar_info[s->num_bars].index = MXSER_IO_BAR_INDEX;
    s->bar_info[s->num_bars].type = BAR_TYPE_PIO;
    s->bar_info[s->num_bars].size = MXSER_PORTS_PER_BOARD * MXSER_IO_STRIDE;
    s->bar_info[s->num_bars].name = "mxser-io";
    s->bar_info[s->num_bars].sparse = false;
    s->num_bars++;

    /* BAR3: vector register. Driver only reads a single byte, but also
     * writes at offsets +4 and +0x0c for MU860 opmode. We map 0x10 bytes.
     */
    s->bar_info[s->num_bars].index = MXSER_VECTOR_BAR_INDEX;
    s->bar_info[s->num_bars].type = BAR_TYPE_PIO;
    s->bar_info[s->num_bars].size = 0x10;
    s->bar_info[s->num_bars].name = "mxser-vector";
    s->bar_info[s->num_bars].sparse = false;
    s->num_bars++;

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
        if (errp && *errp) {
            return;
        }
    }

    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) {
        return;
    }

    /* Initialize board and UART state */
    pcibase_reset(DEVICE(pdev));

    qemu_log_mask(LOG_UNIMP, "[%s] device realized (mxser emulation)\n",
                  TYPE_PCIBASE_DEVICE);
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

    qemu_log_mask(LOG_UNIMP, "[%s] device uninit (mxser emulation)\n",
                  TYPE_PCIBASE_DEVICE);
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

