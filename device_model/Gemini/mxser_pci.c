/*
 * Generic QEMU PCI device template (QEMU 8.2.x)
 *
 * Phase 1: fill register macros, BAR list, BAR sizes, structs, enums.
 * Phase 2: implement MMIO/PIO read/write, DMA init, IRQ init, reset, config hooks
 * Phase 3 (Repair Phase)：correct syntax, types, includes, missing symbols, remove dead code
 * Phase 4 (Debug & Update Phase)：Based on actual kernel logs from QEMU boot, repair the virtual hardware behavior
 *
 * Replace #PLACEHOLDER# blocks with device-specific code/data.
 * NOTE:
 * - Only essential fixes added (TYPE macro, PIO BAR handling, MMIO impl, DMA init, IRQ).
 * - No optional or advanced features added.
 * - Do not directly fill this file with the Linux kernel source code
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

/* ------------------------------------------------------------------ */
/* Additional include files (Stage1 optional)                          */
/* ------------------------------------------------------------------ */


#define TYPE_PCIBASE_DEVICE "mxser_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Related register/macros/constants from driver (Stage1) */
#define PCI_VENDOR_ID_MOXA		0x1393
#ifndef PCI_CLASS_COMMUNICATION_SERIAL
#define PCI_CLASS_COMMUNICATION_SERIAL	0x0700
#endif

#define WAKEUP_CHARS		256
#define OP_MODE_MASK		3
#define RS232_MODE		0
#define MOXA			0x400
#define MOXA_SET_OP_MODE	(MOXA + 66)
#define MOXA_GET_OP_MODE	(MOXA + 67)
#define RS485_2WIRE_MODE	1
#define RS422_MODE		2
#define RS485_4WIRE_MODE	3
#define MOXA_MUST_ENTER_ENHANCED	0xBF
#define MOXA_MUST_GDL_REGISTER		0x07
#define MOXA_MUST_GDL_MASK		0x7F
#define MOXA_MUST_GDL_HAS_BAD_DATA	0x80
#define MOXA_MUST_LSR_RERR		0x80
#define MOXA_MUST_EFR_REGISTER		0x02
#define MOXA_MUST_EFR_EFRB_ENABLE	0x10
#define MOXA_MUST_EFR_BANK0		0x00
#define MOXA_MUST_EFR_BANK1		0x40
#define MOXA_MUST_EFR_BANK2		0x80
#define MOXA_MUST_EFR_BANK3		0xC0
#define MOXA_MUST_EFR_BANK_MASK		0xC0
#define MOXA_MUST_XON1_REGISTER		0x04
#define MOXA_MUST_XON2_REGISTER		0x05
#define MOXA_MUST_XOFF1_REGISTER	0x06
#define MOXA_MUST_XOFF2_REGISTER	0x07
#define MOXA_MUST_RBRTL_REGISTER	0x04
#define MOXA_MUST_RBRTH_REGISTER	0x05
#define MOXA_MUST_RBRTI_REGISTER	0x06
#define MOXA_MUST_THRTL_REGISTER	0x07
#define MOXA_MUST_ENUM_REGISTER		0x04
#define MOXA_MUST_HWID_REGISTER		0x05
#define MOXA_MUST_ECR_REGISTER		0x06
#define MOXA_MUST_CSR_REGISTER		0x07
#define MOXA_MUST_FCR_GDA_MODE_ENABLE	0x20
#define MOXA_MUST_FCR_GDA_ONLY_ENABLE	0x10
#define MOXA_MUST_IER_ECTSI		0x80
#define MOXA_MUST_IER_ERTSI		0x40
#define MOXA_MUST_IER_XINT		0x20
#define MOXA_MUST_IER_EGDAI		0x10
/* #define MOXA_MUST_RECV_ISR		(UART_IER_RDI | MOXA_MUST_IER_EGDAI) */
#define MOXA_MUST_IIR_GDA		0x1C
#define MOXA_MUST_IIR_RDA		0x04
#define MOXA_MUST_IIR_RTO		0x0C
#define MOXA_MUST_IIR_LSR		0x06
#define MOXA_MUST_IIR_XSC		0x10
#define MOXA_MUST_IIR_RTSCTS		0x20
#define MOXA_MUST_IIR_MASK		0x3E
#define MOXA_MUST_MCR_XON_FLAG		0x40
#define MOXA_MUST_MCR_XON_ANY		0x80
#define MOXA_MUST_MCR_TX_XON		0x08
#define MOXA_MUST_EFR_SF_MASK		0x0F
#define MOXA_MUST_EFR_SF_TX1		0x08
#define MOXA_MUST_EFR_SF_TX2		0x04
#define MOXA_MUST_EFR_SF_TX12		0x0C
#define MOXA_MUST_EFR_SF_TX_NO		0x00
#define MOXA_MUST_EFR_SF_TX_MASK	0x0C
#define MOXA_MUST_EFR_SF_RX_NO		0x00
#define MOXA_MUST_EFR_SF_RX1		0x02
#define MOXA_MUST_EFR_SF_RX2		0x01
#define MOXA_MUST_EFR_SF_RX12		0x03
#define MOXA_MUST_EFR_SF_RX_MASK	0x03
#define MXSERMAJOR	 174
#define MXSER_BOARDS		4
#define MXSER_PORTS_PER_BOARD	8
#define MXSER_PORTS		(MXSER_BOARDS * MXSER_PORTS_PER_BOARD)
#define MXSER_ISR_PASS_LIMIT	100
#define MXSER_BAUD_BASE		921600
#define MXSER_CUSTOM_DIVISOR	(MXSER_BAUD_BASE * 16)
#define PCI_DEVICE_ID_MOXA_RC7000	0x0001
#define PCI_DEVICE_ID_MOXA_CP102	0x1020
#define PCI_DEVICE_ID_MOXA_CP102UL	0x1021
#define PCI_DEVICE_ID_MOXA_CP102U	0x1022
#define PCI_DEVICE_ID_MOXA_CP102UF	0x1023
#define PCI_DEVICE_ID_MOXA_C104		0x1040
#define PCI_DEVICE_ID_MOXA_CP104U	0x1041
#define PCI_DEVICE_ID_MOXA_CP104JU	0x1042
#define PCI_DEVICE_ID_MOXA_CP104EL	0x1043
#define PCI_DEVICE_ID_MOXA_POS104UL	0x1044
#define PCI_DEVICE_ID_MOXA_CB108	0x1080
#define PCI_DEVICE_ID_MOXA_CP112UL	0x1120
#define PCI_DEVICE_ID_MOXA_CT114	0x1140
#define PCI_DEVICE_ID_MOXA_CP114	0x1141
#define PCI_DEVICE_ID_MOXA_CB114	0x1142
#define PCI_DEVICE_ID_MOXA_CP114UL	0x1143
#define PCI_DEVICE_ID_MOXA_CP118U	0x1180
#define PCI_DEVICE_ID_MOXA_CP118EL	0x1181
#define PCI_DEVICE_ID_MOXA_CP132	0x1320
#define PCI_DEVICE_ID_MOXA_CP132U	0x1321
#define PCI_DEVICE_ID_MOXA_CP134U	0x1340
#define PCI_DEVICE_ID_MOXA_CB134I	0x1341
#define PCI_DEVICE_ID_MOXA_CP138U	0x1380
#define PCI_DEVICE_ID_MOXA_C168		0x1680
#define PCI_DEVICE_ID_MOXA_CP168U	0x1681
#define PCI_DEVICE_ID_MOXA_CP168EL	0x1682
#define MXSER_NPORTS(ddata)		((ddata) & 0xffU)
#define MXSER_HIGHBAUD			0x0100

/* UART Registers */
#define UART_RX		0	/* In:  Receive buffer */
#define UART_TX		0	/* Out: Transmit buffer */
#define UART_DLL	0	/* Out: Divisor Latch Low */
#define UART_DLM	1	/* Out: Divisor Latch High */
#define UART_IER	1	/* Out: Interrupt Enable Register */
#define UART_IIR	2	/* In:  Interrupt ID Register */
#define UART_FCR	2	/* Out: FIFO Control Register */
#define UART_EFR	2	/* I/O: Enhanced Feature Register */
#define UART_LCR	3	/* Out: Line Control Register */
#define UART_MCR	4	/* Out: Modem Control Register */
#define UART_LSR	5	/* In:  Line Status Register */
#define UART_MSR	6	/* In:  Modem Status Register */
#define UART_SCR	7	/* I/O: Scratch Register */

#define UART_LCR_DLAB	0x80
#define UART_LCR_SBC	0x40
#define UART_LCR_SPAR	0x20
#define UART_LCR_EPAR	0x10
#define UART_LCR_PARITY	0x08
#define UART_LCR_STOP	0x04
#define UART_LCR_WLEN5  0x00
#define UART_LCR_WLEN6  0x01
#define UART_LCR_WLEN7  0x02
#define UART_LCR_WLEN8  0x03

#define UART_LSR_TEMT	0x40
#define UART_LSR_THRE	0x20
#define UART_LSR_BI	0x10
#define UART_LSR_FE	0x08
#define UART_LSR_PE	0x04
#define UART_LSR_OE	0x02
#define UART_LSR_DR	0x01

#define UART_IIR_NO_INT	0x01
#define UART_IIR_ID	0x06
#define UART_IIR_MSI	0x00
#define UART_IIR_THRI	0x02
#define UART_IIR_RDI	0x04
#define UART_IIR_RLSI	0x06

#define UART_IER_MSI	0x08
#define UART_IER_RLSI	0x04
#define UART_IER_THRI	0x02
#define UART_IER_RDI	0x01

#define UART_MCR_DTR	0x01
#define UART_MCR_RTS	0x02
#define UART_MCR_OUT1	0x04
#define UART_MCR_OUT2	0x08
#define UART_MCR_LOOP	0x10

#define UART_MSR_CTS	0x10
#define UART_MSR_DSR	0x20
#define UART_MSR_RI	0x40
#define UART_MSR_DCD	0x80

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

typedef struct MxserHwPort {
    uint8_t rbr;
    uint8_t thr;
    uint8_t ier;
    uint8_t iir;
    uint8_t fcr;
    uint8_t lcr;
    uint8_t mcr;
    uint8_t lsr;
    uint8_t msr;
    uint8_t scr;
    uint8_t dll;
    uint8_t dlm;
    uint8_t efr;
    uint8_t xon1;
    uint8_t xon2;
    uint8_t xoff1;
    uint8_t xoff2;
    uint8_t hwid;
    uint8_t gdl;
    uint8_t rbrth, rbrti, rbrtl, thrtl;
    uint8_t enum_reg, ecr, csr;
} MxserHwPort;

enum mxser_must_hwid {
	MOXA_OTHER_UART		= 0x00,
	MOXA_MUST_MU150_HWID	= 0x01,
	MOXA_MUST_MU860_HWID	= 0x02,
};

typedef struct MxserPort {
	uint8_t rx_high_water;
	uint8_t rx_low_water;
	int type;		/* UART type */

	uint8_t x_char;		/* xon/xoff character */
	uint8_t IER;			/* Interrupt Enable Register */
	uint8_t MCR;			/* Modem control register */
	uint8_t FCR;			/* FIFO control register */

	unsigned int timeout;

	uint8_t read_status_mask;
	uint8_t ignore_status_mask;
	uint8_t xmit_fifo_size;
} MxserPort;

typedef struct MxserBoard {
	unsigned int idx;
	unsigned short nports;
	int irq;
	unsigned long vector;

	enum mxser_must_hwid must_hwid;
	uint32_t max_baud;

	MxserPort ports[MXSER_PORTS_PER_BOARD];
} MxserBoard;

struct PCIBaseState {
    PCIDevice parent_obj;

    /* BAR memory regions (MMIO/PIO unified handling fix) */
    MemoryRegion bar_regions[6];

    /* optional linear backing */
    uint8_t *mmio_backing;
    size_t mmio_backing_size;

    /* BAR table */
    BARInfo bar_info[6];
    int num_bars;

    /* interrupt state feature flags */
    bool has_msi;
    bool has_msix;
    
    
    /* DMA info placeholder */
    

    /* Register shadows */
    MxserHwPort hw_ports[8];
    uint8_t vector_reg;
    uint8_t opmode_reg;

    /* Status fields */
    

    /* reset/probe state */
    

    /* power mgmt */
    

    /* other fields */
    struct MxserBoard board;
};

/* ------------------------------------------------------------------ */
/* Forward declarations                                                */
/* ------------------------------------------------------------------ */
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len);
static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len);
static void pcibase_reset(DeviceState *dev);
static void pcibase_realize(PCIDevice *pdev, Error **errp);
static void pcibase_uninit(PCIDevice *pdev);

/* ------------------------------------------------------------------ */
/* MMIO / PIO handlers (device-specific code goes into placeholders)   */
/* ------------------------------------------------------------------ */

static void mxser_update_irq(PCIBaseState *s)
{
    int i;
    int irq_level = 0;
    for (i = 0; i < 8; i++) {
        if (!(s->hw_ports[i].iir & UART_IIR_NO_INT)) {
            irq_level = 1;
            break;
        }
    }
    pci_set_irq(PCI_DEVICE(s), irq_level);
}

static uint64_t mxser_uart_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    int port_idx = addr / 8;
    int reg = addr % 8;
    MxserHwPort *p;
    uint64_t ret = 0;

    if (port_idx >= 8) return 0;
    p = &s->hw_ports[port_idx];

    if (p->lcr == MOXA_MUST_ENTER_ENHANCED) {
        if (reg == 2) {
            ret = p->efr;
        } else {
            switch (p->efr & MOXA_MUST_EFR_BANK_MASK) {
            case MOXA_MUST_EFR_BANK0:
                switch (reg) {
                case 4: ret = p->xon1; break;
                case 5: ret = p->xon2; break;
                case 6: ret = p->xoff1; break;
                case 7: ret = p->xoff2; break;
                }
                break;
            case MOXA_MUST_EFR_BANK1:
                switch (reg) {
                case 4: ret = p->rbrth; break;
                case 5: ret = p->rbrti; break;
                case 6: ret = p->rbrtl; break;
                case 7: ret = p->thrtl; break;
                }
                break;
            case MOXA_MUST_EFR_BANK2:
                switch (reg) {
                case 4: ret = p->enum_reg; break;
                case 5: ret = p->hwid; break;
                case 6: ret = p->ecr; break;
                case 7: ret = p->csr; break;
                }
                break;
            }
        }
        return ret;
    }

    switch (reg) {
    case UART_RX:
        if (p->lcr & UART_LCR_DLAB)
            ret = p->dll;
        else
            ret = p->rbr;
        break;
    case UART_IER:
        if (p->lcr & UART_LCR_DLAB)
            ret = p->dlm;
        else
            ret = p->ier;
        break;
    case UART_IIR:
        ret = p->iir;
        break;
    case UART_LCR:
        ret = p->lcr;
        break;
    case UART_MCR:
        ret = p->mcr;
        break;
    case UART_LSR:
        ret = p->lsr;
        break;
    case UART_MSR:
        ret = p->msr;
        break;
    case UART_SCR:
        if ((p->efr & MOXA_MUST_EFR_EFRB_ENABLE) && (reg == MOXA_MUST_GDL_REGISTER))
            ret = p->gdl;
        else
            ret = p->scr;
        break;
    }
    return ret;
}

static void mxser_uart_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    int port_idx = addr / 8;
    int reg = addr % 8;
    MxserHwPort *p;

    if (port_idx >= 8) return;
    p = &s->hw_ports[port_idx];

    if (p->lcr == MOXA_MUST_ENTER_ENHANCED) {
        if (reg == 2) {
            p->efr = val;
        } else {
            switch (p->efr & MOXA_MUST_EFR_BANK_MASK) {
            case MOXA_MUST_EFR_BANK0:
                switch (reg) {
                case 4: p->xon1 = val; break;
                case 5: p->xon2 = val; break;
                case 6: p->xoff1 = val; break;
                case 7: p->xoff2 = val; break;
                }
                break;
            case MOXA_MUST_EFR_BANK1:
                switch (reg) {
                case 4: p->rbrth = val; break;
                case 5: p->rbrti = val; break;
                case 6: p->rbrtl = val; break;
                case 7: p->thrtl = val; break;
                }
                break;
            case MOXA_MUST_EFR_BANK2:
                switch (reg) {
                case 4: p->enum_reg = val; break;
                case 5: p->hwid = val; break;
                case 6: p->ecr = val; break;
                case 7: p->csr = val; break;
                }
                break;
            }
        }
        return;
    }

    switch (reg) {
    case UART_TX:
        if (p->lcr & UART_LCR_DLAB)
            p->dll = val;
        else {
            p->thr = val;
            p->lsr |= (UART_LSR_THRE | UART_LSR_TEMT);
            if (p->ier & UART_IER_THRI) {
                p->iir &= ~UART_IIR_ID;
                p->iir |= UART_IIR_THRI;
                p->iir &= ~UART_IIR_NO_INT;
            } else {
                /* If THRI not enabled, do we clear interrupt? */
            }
            mxser_update_irq(s);
        }
        break;
    case UART_IER:
        if (p->lcr & UART_LCR_DLAB)
            p->dlm = val;
        else {
            p->ier = val;
            /* Re-evaluate interrupts */
            if (p->ier & UART_IER_THRI && (p->lsr & UART_LSR_THRE)) {
                p->iir &= ~UART_IIR_ID;
                p->iir |= UART_IIR_THRI;
                p->iir &= ~UART_IIR_NO_INT;
            } else {
                p->iir |= UART_IIR_NO_INT;
            }
            mxser_update_irq(s);
        }
        break;
    case UART_FCR:
        p->fcr = val;
        break;
    case UART_LCR:
        p->lcr = val;
        break;
    case UART_MCR:
        p->mcr = val;
        break;
    case UART_SCR:
        p->scr = val;
        break;
    }
}

static uint64_t mxser_vector_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t ret = 0;
    int i;

    if (addr == 0) {
        /* Bit 0 for port 0, etc. 0 = pending, 1 = no pending */
        for (i = 0; i < 8; i++) {
            if (s->hw_ports[i].iir & UART_IIR_NO_INT)
                ret |= (1 << i);
        }
        return ret;
    }
    return 0;
}

static void mxser_vector_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr == 4 || addr == 0x0C) {
        s->opmode_reg = val;
    }
}

static const MemoryRegionOps mxser_uart_ops = {
    .read = mxser_uart_read,
    .write = mxser_uart_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 1 },
    .impl  = { .min_access_size = 1, .max_access_size = 1 },
};

static const MemoryRegionOps mxser_vector_ops = {
    .read = mxser_vector_read,
    .write = mxser_vector_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* ------------------------------------------------------------------ */
/* Reset       */
/* ------------------------------------------------------------------ */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);
    int i;

    /* core resets */
    pci_device_reset(pdev);

    for (i = 0; i < 8; i++) {
        MxserHwPort *p = &s->hw_ports[i];
        memset(p, 0, sizeof(*p));
        p->iir = UART_IIR_NO_INT;
        p->lsr = UART_LSR_TEMT | UART_LSR_THRE;
        p->msr = 0; /* No carrier/DSR by default */
        p->hwid = MOXA_MUST_MU860_HWID;
    }
    s->vector_reg = 0xFF;
    s->opmode_reg = 0;

    if (s->mmio_backing && s->mmio_backing_size)
        memset(s->mmio_backing, 0, s->mmio_backing_size);
}

/* ------------------------------------------------------------------ */
/* DMA initialize (called from realize)                                */
/* ------------------------------------------------------------------ */
static void pcibase_dma_device_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    
    (void)s;
    (void)errp;
}

/* ------------------------------------------------------------------ */
/* PCI config space access (class-level hooks)                         */
/* ------------------------------------------------------------------ */
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    uint32_t val = pci_default_read_config(pdev, addr, len);
    /* optional: override certain config offsets if needed */
    

    switch (len) {
        case 1: val &= 0xFF; break;
        case 2: val &= 0xFFFF; break;
        case 4: break;
        default: break;
    }
    return val;
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    if (addr >= PCI_BASE_ADDRESS_0 && addr <= PCI_BASE_ADDRESS_5) {
        pci_default_write_config(pdev, addr, val, len);
        return;
    }
}

/* ------------------------------------------------------------------ */
/* Realize (device init)                                                */
/* ------------------------------------------------------------------ */
static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_MOXA );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_MOXA_C168 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_COMMUNICATION_SERIAL );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);
    
    s->board.nports = 8;

    /* register BARs */
    /* BAR2: UART IOs (8 ports * 8 bytes = 64 bytes) */
    memory_region_init_io(&s->bar_regions[2], OBJECT(s), &mxser_uart_ops, s, "mxser-uart", 64);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_IO, &s->bar_regions[2]);

    /* BAR3: Vector IO (16 bytes) */
    memory_region_init_io(&s->bar_regions[3], OBJECT(s), &mxser_vector_ops, s, "mxser-vector", 16);
    pci_register_bar(pdev, 3, PCI_BASE_ADDRESS_SPACE_IO, &s->bar_regions[3]);

    /* Interrupt config (placeholders) */
    

    /* DMA init (called here so device can set DMA masks/etc) */
    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) return;

    

    /* Example MSI:
     *   if (msi_init(pdev, 0, 1, true, false, errp) == 0) { s->has_msi = true; }
     * Example MSIX:
     *   if (msix_init(pdev, nvec, table_mem, table_bar_nr) == 0) { s->has_msix = true; }
     */

    /* optional: map legacy INTx behavior if driver expects it */
    

    

    /* Power management / other init */
    
    /* Example:
    *   pdev->pm_cap = true;
    */
    qemu_log_mask(LOG_UNIMP, "[%s] device realized\n", TYPE_PCIBASE_DEVICE);
}

/* ------------------------------------------------------------------ */
/* Uninit/cleanup                                                      */
/* ------------------------------------------------------------------ */
static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (s->has_msix) {
        msix_uninit(pdev,NULL,0);
        s->has_msix = false;
    }
    if (s->has_msi) {
        msi_uninit(pdev);
        s->has_msi = false;
    }

    /* free mmio_backing or other allocations */
    

    if (s->mmio_backing) {
        g_free(s->mmio_backing);
        s->mmio_backing = NULL;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] device uninit\n", TYPE_PCIBASE_DEVICE);
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

    k->realize = pcibase_realize;
    k->exit    = pcibase_uninit;
    dc->reset  = pcibase_reset;

    set_bit(DEVICE_CATEGORY_MISC, dc->categories);

    /* optional */
    
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