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
#ifndef BIT
#define BIT(nr) (1UL << (nr))
#endif

/* Stubs for kernel types to allow compilation of extracted structs */
typedef uint64_t dma_addr_t;
typedef uint64_t phys_addr_t;
typedef uint32_t dma_cookie_t;
typedef void (*dma_filter_fn)(void *chan, void *param);
struct dma_slave_config {};
struct dma_chan {};
struct uart_port {};
struct uart_8250_port {};

/* Added stubs for new structs */
struct device {};
struct clk {};
struct dw_dma {};
struct dw_dma_platform_data {};

/* Updated struct definitions based on driver source */
struct dw_dma_chip {
    struct device   *dev;
    int             id;
    int             irq;
    void            *regs;
    struct clk      *clk;
    struct dw_dma   *dw;
    const struct dw_dma_platform_data   *pdata;
};

struct dw_dma_slave {
    struct device       *dma_dev;
    uint8_t             src_id;
    uint8_t             dst_id;
    uint8_t             m_master;
    uint8_t             p_master;
    uint8_t             channels;
    bool                hs_polarity;
};

#define TYPE_PCIBASE_DEVICE "8250_lpss_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Related register/macros/constants from driver (Stage1) */
#define PCI_DEVICE_ID_INTEL_QRK_UARTx	0x0936
#define PCI_DEVICE_ID_INTEL_BYT_UART1	0x0f0a
#define PCI_DEVICE_ID_INTEL_BYT_UART2	0x0f0c
#define PCI_DEVICE_ID_INTEL_BSW_UART1	0x228a
#define PCI_DEVICE_ID_INTEL_BSW_UART2	0x228c
#define PCI_DEVICE_ID_INTEL_EHL_UART0	0x4b96
#define PCI_DEVICE_ID_INTEL_EHL_UART1	0x4b97
#define PCI_DEVICE_ID_INTEL_EHL_UART2	0x4b98
#define PCI_DEVICE_ID_INTEL_EHL_UART3	0x4b99
#define PCI_DEVICE_ID_INTEL_EHL_UART4	0x4b9a
#define PCI_DEVICE_ID_INTEL_EHL_UART5	0x4b9b
#define PCI_DEVICE_ID_INTEL_BDW_UART1	0x9ce3
#define PCI_DEVICE_ID_INTEL_BDW_UART2	0x9ce4

#define BYT_PRV_CLK			0x800
#define BYT_PRV_CLK_EN			BIT(0)
#define BYT_PRV_CLK_M_VAL_SHIFT		1
#define BYT_PRV_CLK_N_VAL_SHIFT		16
#define BYT_PRV_CLK_UPDATE		BIT(31)
#define BYT_TX_OVF_INT			0x820
#define BYT_TX_OVF_INT_MASK		BIT(1)

#define UART_CAP_FIFO	BIT(8)
#define UART_CAP_AFE	BIT(11)
#define UART_CAP_NOTEMT	BIT(18)
#define UART_CAP_IRDA	BIT(16)
#define UART_CAP_UUE	BIT(12)
#define UART_BUG_NOMSR	BIT(2)
#define UART_CAP_RTOIE	BIT(13)
#define UART_CAP_MINI	BIT(17)
#define UART_CAP_EFR	BIT(9)
#define UART_CAP_HFIFO	BIT(14)
#define UART_CAP_SLEEP	BIT(10)
#define UART_BUG_QUOT	BIT(0)
#define UART_BUG_THRE	BIT(3)
#define UART_CAP_RPM	BIT(15)
#define UART_BUG_TXRACE	BIT(5)
#define UART_BUG_TXEN	BIT(1)
#define PRESL(x) ((x) & 0x30)

/* UART Registers (Shifted by 2 as per driver regshift=2) */
#define REG_RBR 0x00
#define REG_THR 0x00
#define REG_DLL 0x00
#define REG_IER 0x04
#define REG_DLM 0x04
#define REG_IIR 0x08
#define REG_FCR 0x08
#define REG_LCR 0x0C
#define REG_MCR 0x10
#define REG_LSR 0x14
#define REG_MSR 0x18
#define REG_SCR 0x1C

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
    
    /* UART Registers */
    uint32_t rbr; /* Receive Buffer */
    uint32_t ier; /* Interrupt Enable */
    uint32_t iir; /* Interrupt Identity */
    uint32_t lcr; /* Line Control */
    uint32_t mcr; /* Modem Control */
    uint32_t lsr; /* Line Status */
    uint32_t msr; /* Modem Status */
    uint32_t scr; /* Scratch */
    uint32_t dll; /* Divisor Latch Low */
    uint32_t dlm; /* Divisor Latch High */
    uint32_t fcr; /* FIFO Control */

    /* LPSS Registers */
    uint32_t prv_clk;
    uint32_t tx_ovf_int;
};

enum mctrl_gpio_idx {
	UART_GPIO_CTS,
	UART_GPIO_DSR,
	UART_GPIO_DCD,
	UART_GPIO_RNG,
	UART_GPIO_RI = UART_GPIO_RNG,
	UART_GPIO_RTS,
	UART_GPIO_DTR,
	UART_GPIO_MAX,
};

struct uart_8250_dma {
	int (*tx_dma)(struct uart_8250_port *p);
	int (*rx_dma)(struct uart_8250_port *p);
	void (*prepare_tx_dma)(struct uart_8250_port *p);
	void (*prepare_rx_dma)(struct uart_8250_port *p);

	/* Filter function */
	dma_filter_fn		fn;
	/* Parameter to the filter function */
	void			*rx_param;
	void			*tx_param;

	struct dma_slave_config	rxconf;
	struct dma_slave_config	txconf;

	struct dma_chan		*rxchan;
	struct dma_chan		*txchan;

	/* Device address base for DMA operations */
	phys_addr_t		rx_dma_addr;
	phys_addr_t		tx_dma_addr;

	/* DMA address of the buffer in memory */
	dma_addr_t		rx_addr;
	dma_addr_t		tx_addr;

	dma_cookie_t		rx_cookie;
	dma_cookie_t		tx_cookie;

	void			*rx_buf;

	size_t			rx_size;
	size_t			tx_size;

	unsigned char		tx_running;
	unsigned char		tx_err;
	unsigned char		rx_running;
};

struct dw8250_port_data {
	/* Port properties */
	int			line;

	/* DMA operations */
	struct uart_8250_dma	dma;

	/* Hardware configuration */
	uint32_t		cpr_value;
	uint8_t			dlf_size;

	/* RS485 variables */
	bool			hw_rs485_support;
};

struct lpss8250;

struct lpss8250_board {
	unsigned long freq;
	unsigned int base_baud;
	int (*setup)(struct lpss8250 *, struct uart_port *p);
	void (*exit)(struct lpss8250 *);
};

struct lpss8250 {
	struct dw8250_port_data data;
	struct lpss8250_board *board;

	/* DMA parameters */
	struct dw_dma_chip dma_chip;
	struct dw_dma_slave dma_param;
	uint8_t dma_maxburst;
};

/* ------------------------------------------------------------------ */
/* Forward declarations                                                */
/* ------------------------------------------------------------------ */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint64_t pcibase_dma_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_dma_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len);
static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len);
static void pcibase_reset(DeviceState *dev);
static void pcibase_realize(PCIDevice *pdev, Error **errp);
static void pcibase_uninit(PCIDevice *pdev);

/* ------------------------------------------------------------------ */
/* MemoryRegionOps                                                      */
/* ------------------------------------------------------------------ */
static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static const MemoryRegionOps pcibase_dma_ops = {
    .read = pcibase_dma_read,
    .write = pcibase_dma_write,
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
/* Helper: register a BAR (MMIO or PIO)                                 */
/* ------------------------------------------------------------------ */
static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE)
        return;

    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    }else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, bi->size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }

}

/* ------------------------------------------------------------------ */
/* MMIO / PIO handlers (device-specific code goes into placeholders)   */
/* ------------------------------------------------------------------ */

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case REG_RBR: /* 0x00 */
        if (s->lcr & 0x80) { /* DLAB=1 */
            val = s->dll;
        } else {
            val = s->rbr;
        }
        break;
    case REG_IER: /* 0x04 */
        if (s->lcr & 0x80) { /* DLAB=1 */
            val = s->dlm;
        } else {
            val = s->ier;
        }
        break;
    case REG_IIR: /* 0x08 */
        val = s->iir;
        break;
    case REG_LCR: /* 0x0C */
        val = s->lcr;
        break;
    case REG_MCR: /* 0x10 */
        val = s->mcr;
        break;
    case REG_LSR: /* 0x14 */
        val = s->lsr;
        break;
    case REG_MSR: /* 0x18 */
        val = s->msr;
        break;
    case REG_SCR: /* 0x1C */
        val = s->scr;
        break;
    case BYT_PRV_CLK: /* 0x800 */
        val = s->prv_clk;
        break;
    case BYT_TX_OVF_INT: /* 0x820 */
        val = s->tx_ovf_int;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "[%s] mmio_read addr=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case REG_THR: /* 0x00 */
        if (s->lcr & 0x80) { /* DLAB=1 */
            s->dll = val & 0xFF;
        } else {
            /* THR write - transmit data (stub) */
            /* In a real UART, this would push to FIFO and trigger TX logic */
            s->lsr |= 0x60; /* THRE | TEMT */
            /* If THRE interrupt enabled, raise IRQ (not implemented for simplicity) */
        }
        break;
    case REG_IER: /* 0x04 */
        if (s->lcr & 0x80) { /* DLAB=1 */
            s->dlm = val & 0xFF;
        } else {
            s->ier = val & 0x0F; /* Only lower 4 bits usually */
        }
        break;
    case REG_FCR: /* 0x08 */
        s->fcr = val;
        if (val & 0x01) { /* FIFO Enable */
            s->iir |= 0xC0; /* Set FIFO enabled bits in IIR */
        } else {
            s->iir &= ~0xC0;
        }
        break;
    case REG_LCR: /* 0x0C */
        s->lcr = val;
        break;
    case REG_MCR: /* 0x10 */
        s->mcr = val;
        break;
    case REG_LSR: /* 0x14 */
        /* LSR is usually read-only or W1C for some bits, but 8250 is mostly RO */
        break;
    case REG_MSR: /* 0x18 */
        break;
    case REG_SCR: /* 0x1C */
        s->scr = val;
        break;
    case BYT_PRV_CLK: /* 0x800 */
        s->prv_clk = val;
        break;
    case BYT_TX_OVF_INT: /* 0x820 */
        s->tx_ovf_int = val;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "[%s] mmio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, (uint64_t)val, size);
        break;
    }
}

static uint64_t pcibase_dma_read(void *opaque, hwaddr addr, unsigned size)
{
    /* DMA registers stub */
    return 0;
}

static void pcibase_dma_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* DMA registers stub */
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "[%s] pio_read addr=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "[%s] pio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, (uint64_t)val, size);
}

/* ------------------------------------------------------------------ */
/* Reset       */
/* ------------------------------------------------------------------ */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    /* core resets */
    pci_device_reset(pdev);

    /* UART Reset State */
    s->ier = 0x00;
    s->iir = 0x01; /* No interrupt pending */
    s->lcr = 0x00;
    s->mcr = 0x00;
    s->lsr = 0x60; /* THRE | TEMT */
    s->msr = 0x00;
    s->scr = 0x00;
    s->fcr = 0x00;
    s->dll = 0x00;
    s->dlm = 0x00;
    
    s->prv_clk = 0;
    s->tx_ovf_int = 0;
}

/* ------------------------------------------------------------------ */
/* DMA initialize (called from realize)                                */
/* ------------------------------------------------------------------ */
static void pcibase_dma_device_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    (void)s;
    (void)errp;
    /* DMA is handled via BAR 1 stub */
}

/* ------------------------------------------------------------------ */
/* PCI config space access (class-level hooks)                         */
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
/* Realize (device init)                                                */
/* ------------------------------------------------------------------ */
static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x8086); /* Intel */
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_INTEL_QRK_UARTx);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_COMMUNICATION_SERIAL);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);
    
    /* BAR 0: MMIO 4KB (UART + LPSS regs) */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 4096;
    s->bar_info[0].name = "lpss-bar0";
    pcibase_register_bar(pdev, s, &s->bar_info[0], errp);

    /* BAR 1: MMIO 4KB (DMA regs stub) */
    /* We register this manually to use different ops */
    memory_region_init_io(&s->bar_regions[1], OBJECT(s), &pcibase_dma_ops, s, "lpss-bar1-dma", 4096);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_regions[1]);

    /* Interrupt config */
    if (msi_init(pdev, 0, 1, true, false, errp) == 0) {
        s->has_msi = true;
    }

    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) return;

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