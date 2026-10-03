/*
 * QEMU PCI device model for Intel LPSS 8250 UART (minimal, driver-visible)
 *
 * Target QEMU: 8.2.10
 *
 * NOTE: Implemented strictly from drivers/tty/serial/8250/8250_lpss.c
 * Only behavior visible in that driver is modeled.
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

#define TYPE_PCIBASE_DEVICE "8250_lpss_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI device IDs from driver */
#define PCI_DEVICE_ID_INTEL_QRK_UARTx 0x0936
#define PCI_DEVICE_ID_INTEL_BYT_UART1 0x0f0a
#define PCI_DEVICE_ID_INTEL_BYT_UART2 0x0f0c
#define PCI_DEVICE_ID_INTEL_BSW_UART1 0x228a
#define PCI_DEVICE_ID_INTEL_BSW_UART2 0x228c
#define PCI_DEVICE_ID_INTEL_EHL_UART0 0x4b96
#define PCI_DEVICE_ID_INTEL_EHL_UART1 0x4b97
#define PCI_DEVICE_ID_INTEL_EHL_UART2 0x4b98
#define PCI_DEVICE_ID_INTEL_EHL_UART3 0x4b99
#define PCI_DEVICE_ID_INTEL_EHL_UART4 0x4b9a
#define PCI_DEVICE_ID_INTEL_EHL_UART5 0x4b9b
#define PCI_DEVICE_ID_INTEL_BDW_UART1 0x9ce3
#define PCI_DEVICE_ID_INTEL_BDW_UART2 0x9ce4

/* LPSS-specific registers used by driver */
#define BYT_PRV_CLK                  0x800
#define BYT_PRV_CLK_EN               (1U << 0)
#define BYT_PRV_CLK_M_VAL_SHIFT      1
#define BYT_PRV_CLK_N_VAL_SHIFT      16
#define BYT_PRV_CLK_UPDATE           (1U << 31)

#define BYT_TX_OVF_INT               0x820
#define BYT_TX_OVF_INT_MASK          (1U << 1)

#define PCI_CLASS_COMMUNICATION_SERIAL 0x0700

/* 8250 register offsets in MEM32 with regshift=2 (from generic 8250) */
#define UART_RBR   0x00
#define UART_THR   0x00
#define UART_IER   0x04
#define UART_IIR   0x08
#define UART_FCR   0x08
#define UART_LCR   0x0c
#define UART_MCR   0x10
#define UART_LSR   0x14
#define UART_MSR   0x18
#define UART_SCR   0x1c

#define UART_DLL   0x00
#define UART_DLM   0x04

/* LSR bits (standard 16550A) */
#define UART_LSR_DR   0x01
#define UART_LSR_OE   0x02
#define UART_LSR_PE   0x04
#define UART_LSR_FE   0x08
#define UART_LSR_BI   0x10
#define UART_LSR_THRE 0x20
#define UART_LSR_TEMT 0x40
#define UART_LSR_ERR  0x80

/* MCR bits used by generic helpers (not needed for internal behavior) */
#define UART_MCR_RTS  0x02
#define UART_MCR_DTR  0x01
#define UART_MCR_OUT1 0x04
#define UART_MCR_OUT2 0x08
#define UART_MCR_LOOP 0x10

/* IER bits */
#define UART_IER_RDI   0x01
#define UART_IER_THRI  0x02
#define UART_IER_RLSI  0x04
#define UART_IER_MSI   0x08

/* IIR bits */
#define UART_IIR_NO_INT   0x01
#define UART_IIR_THRI     0x02
#define UART_IIR_RDI      0x04
#define UART_IIR_RLSI     0x06
#define UART_IIR_MSI      0x00

/* BAR metadata */
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

/* UART FIFO sizes (simple approximation) */
#define LPSS_UART_TX_FIFO_SIZE 16
#define LPSS_UART_RX_FIFO_SIZE 16

/* DesignWare extended UART registers used by driver */
#define DW_UART_RE_EN   0xb4
#define DW_UART_TCR     0xac
#define DW_UART_DE_EN   0xb0
#define DW_UART_DLF     0xc0

struct PCIBaseState {
    PCIDevice parent_obj;

    /* BAR memory regions */
    MemoryRegion bar_regions[6];

    /* optional linear backing (unused for now) */
    uint8_t *mmio_backing;
    size_t mmio_backing_size;

    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;

    /* Simple 8250-like UART state visible to generic 8250 driver */
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

    /* Divisor latch */
    uint8_t dll;
    uint8_t dlm;

    /* BYT private clock register */
    uint32_t byt_prv_clk;

    /* BYT TX overflow interrupt mask register */
    uint32_t byt_tx_ovf_int;

    /* Simple RX FIFO buffer */
    uint8_t rx_fifo[LPSS_UART_RX_FIFO_SIZE];
    uint8_t tx_fifo[LPSS_UART_TX_FIFO_SIZE];
    uint32_t rx_head;
    uint32_t rx_tail;
    uint32_t tx_head;
    uint32_t tx_tail;

    /* DesignWare-specific registers touched by driver */
    uint32_t dw_re_en;
    uint32_t dw_de_en;
    uint32_t dw_tcr;
    uint32_t dw_dlf;

    qemu_irq irq;
};

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len);
static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len);
static void pcibase_reset(DeviceState *dev);
static void pcibase_realize(PCIDevice *pdev, Error **errp);
static void pcibase_uninit(PCIDevice *pdev);

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

static void lpss_uart_update_irq(PCIBaseState *s)
{
    bool pending = false;

    if ((s->ier & UART_IER_RDI) && (s->lsr & UART_LSR_DR)) {
        pending = true;
        s->iir = UART_IIR_RDI;
    } else if ((s->ier & UART_IER_THRI) && (s->lsr & UART_LSR_THRE)) {
        pending = true;
        s->iir = UART_IIR_THRI;
    } else if ((s->ier & UART_IER_MSI) && (s->msr & 0x0F)) {
        pending = true;
        s->iir = UART_IIR_MSI;
    } else {
        s->iir = UART_IIR_NO_INT;
    }

    if (pending) {
        qemu_set_irq(s->irq, 1);
    } else {
        qemu_set_irq(s->irq, 0);
    }
}

static void lpss_uart_reset_state(PCIBaseState *s)
{
    s->rbr = 0;
    s->thr = 0;
    s->ier = 0;
    s->iir = UART_IIR_NO_INT;
    s->fcr = 0;
    s->lcr = 0;
    s->mcr = 0;
    s->lsr = UART_LSR_THRE | UART_LSR_TEMT;
    s->msr = 0;
    s->scr = 0;
    s->dll = 0;
    s->dlm = 0;
    s->byt_prv_clk = 0;
    s->byt_tx_ovf_int = 0;
    s->rx_head = s->rx_tail = 0;
    s->tx_head = s->tx_tail = 0;
    s->dw_re_en = 0;
    s->dw_de_en = 0;
    s->dw_tcr = 0;
    s->dw_dlf = 0;
}

static uint8_t lpss_uart_readb(PCIBaseState *s, hwaddr offset)
{
    bool dlab = (s->lcr & 0x80) != 0;

    switch (offset) {
    case UART_RBR:
        if (dlab) {
            return s->dll;
        } else {
            uint8_t val = s->rbr;
            s->lsr &= ~UART_LSR_DR;
            lpss_uart_update_irq(s);
            return val;
        }
    case UART_IER:
        if (dlab) {
            return s->dlm;
        } else {
            return s->ier;
        }
    case UART_IIR:
        return s->iir | 0xC0; /* FIFO enabled flags as typical */
    case UART_LCR:
        return s->lcr;
    case UART_MCR:
        return s->mcr;
    case UART_LSR:
        return s->lsr;
    case UART_MSR:
        return s->msr;
    case UART_SCR:
        return s->scr;
    default:
        return 0x00;
    }
}

static void lpss_uart_writeb(PCIBaseState *s, hwaddr offset, uint8_t value)
{
    bool dlab = (s->lcr & 0x80) != 0;

    switch (offset) {
    case UART_THR:
        if (dlab) {
            s->dll = value;
        } else {
            s->thr = value;
            /* For minimal model, treat TX as immediate and keep THR empty */
            s->lsr |= UART_LSR_THRE | UART_LSR_TEMT;
        }
        break;
    case UART_IER:
        if (dlab) {
            s->dlm = value;
        } else {
            s->ier = value & 0x0F;
        }
        break;
    case UART_FCR:
        s->fcr = value;
        /* Clear FIFOs if requested */
        if (value & 0x02) {
            s->rx_head = s->rx_tail = 0;
            s->lsr &= ~UART_LSR_DR;
        }
        if (value & 0x04) {
            s->tx_head = s->tx_tail = 0;
        }
        break;
    case UART_LCR:
        s->lcr = value;
        break;
    case UART_MCR:
        s->mcr = value;
        break;
    case UART_LSR:
        /* Read-only in hardware; ignore writes */
        break;
    case UART_MSR:
        /* Read-only; ignore writes */
        break;
    case UART_SCR:
        s->scr = value;
        break;
    default:
        break;
    }

    lpss_uart_update_irq(s);
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 1 && size != 2 && size != 4) {
        return 0;
    }

    /* UART registers are at offset 0..0x1c with regshift 2 */
    if (addr < 0x20) {
        uint8_t v = lpss_uart_readb(s, addr);
        return v;
    }

    /* BYT private clock register */
    if (addr == BYT_PRV_CLK && size == 4) {
        return s->byt_prv_clk;
    }

    /* BYT TX overflow interrupt mask register */
    if (addr == BYT_TX_OVF_INT && size == 4) {
        return s->byt_tx_ovf_int;
    }

    /* DesignWare extended UART registers used by driver */
    if (addr == DW_UART_RE_EN && size == 4) {
        return s->dw_re_en;
    }
    if (addr == DW_UART_DE_EN && size == 4) {
        return s->dw_de_en;
    }
    if (addr == DW_UART_TCR && size == 4) {
        return s->dw_tcr;
    }
    if (addr == DW_UART_DLF && size == 4) {
        return s->dw_dlf;
    }

    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 1 && size != 2 && size != 4) {
        return;
    }

    if (addr < 0x20) {
        lpss_uart_writeb(s, addr, (uint8_t)val);
        return;
    }

    if (addr == BYT_PRV_CLK && size == 4) {
        /* Driver writes this to program clock; store value */
        s->byt_prv_clk = (uint32_t)val;
        return;
    }

    if (addr == BYT_TX_OVF_INT && size == 4) {
        /* Driver writes mask to disable TX overflow interrupt */
        s->byt_tx_ovf_int = (uint32_t)val;
        return;
    }

    /* DesignWare extended UART registers used by driver */
    if (addr == DW_UART_RE_EN && size == 4) {
        s->dw_re_en = (uint32_t)val;
        return;
    }
    if (addr == DW_UART_DE_EN && size == 4) {
        s->dw_de_en = (uint32_t)val;
        return;
    }
    if (addr == DW_UART_TCR && size == 4) {
        s->dw_tcr = (uint32_t)val;
        return;
    }
    if (addr == DW_UART_DLF && size == 4) {
        s->dw_dlf = (uint32_t)val;
        return;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;
    (void)addr;
    (void)size;
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;
    (void)addr;
    (void)val;
    (void)size;
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    pci_device_reset(pdev);

    if (s->mmio_backing && s->mmio_backing_size) {
        memset(s->mmio_backing, 0, s->mmio_backing_size);
    }

    lpss_uart_reset_state(s);
}

static void pcibase_dma_device_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    (void)s;
    (void)errp;
}

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

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Device IDs: use first entry from pci_ids[] in driver: QRK_UARTx */
    pci_set_word(pci_conf + PCI_VENDOR_ID, 0x8086);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_INTEL_QRK_UARTx);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_COMMUNICATION_SERIAL);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* One MMIO BAR (BAR0) used by driver */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    /* Size large enough for UART and BYT private regs used by driver */
    s->bar_info[0].size = 0x1000;
    s->bar_info[0].name = "lpss-uart-mmio";
    s->bar_info[0].sparse = false;

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
        if (errp && *errp) {
            return;
        }
    }

    /* Wire legacy INTx */
    s->irq = pci_allocate_irq(pdev);

    lpss_uart_reset_state(s);

    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) {
        return;
    }
}

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
}

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

