/*
 * QEMU PCI device model for Moxa Smartio/Industio serial cards
 *
 * Based on Linux driver: /linux-6.18/drivers/tty/mxser.c
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

/* UART register definitions (from standard 8250/16550) */
#define UART_RX		0	/* In:  Receive buffer */
#define UART_TX		0	/* Out: Transmit buffer */
#define UART_IER	1	/* I/O: Interrupt Enable Register */
#define UART_IIR	2	/* In:  Interrupt ID Register */
#define UART_LCR	3	/* I/O: Line Control Register */
#define UART_MCR	4	/* Out: Modem Control Register */
#define UART_LSR	5	/* In:  Line Status Register */
#define UART_MSR	6	/* In:  Modem Status Register */
#define UART_FCR	2	/* Out: FIFO Control Register */

/* UART register bit definitions */
#define UART_LCR_WLEN8	0x03	/* 8 data bits */
#define UART_MCR_DTR	0x01	/* DTR signal */
#define UART_MCR_RTS	0x02	/* RTS signal */
#define UART_LSR_TEMT	0x40	/* Transmitter empty */
#define UART_LSR_THRE	0x20	/* Transmit-hold-register empty */
#define UART_LSR_DR	0x01	/* Data ready */
#define UART_MSR_DCD	0x80	/* Data Carrier Detect */
#define UART_MSR_DSR	0x20	/* Data Set Ready */
#define UART_MSR_CTS	0x10	/* Clear to Send */
#define UART_MSR_RI	0x04	/* Ring Indicator */
#define UART_IIR_NO_INT	0x01	/* No interrupts pending */

#define TYPE_PCIBASE_DEVICE "mxser_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Related register/macros/constants from driver (Stage1) */
#define VENDOR_ID 0x1393
#define DEVICE_ID 0x1680
#define CLASS_ID PCI_CLASS_COMMUNICATION_SERIAL

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
#define MOXA_MUST_RECV_ISR		(UART_IER_RDI | MOXA_MUST_IER_EGDAI)
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

enum mxser_must_hwid {
	MOXA_OTHER_UART		= 0x00,
	MOXA_MUST_MU150_HWID	= 0x01,
	MOXA_MUST_MU860_HWID	= 0x02,
};

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
    
    /* DMA info placeholder */
    

    /* Register shadows */
    uint8_t uart_regs[8][8]; /* 8 ports, each with 8 registers */
    uint8_t vector_reg[16];  /* vector register space */
    uint8_t opmode_reg[2];   /* opmode registers for MU860 */
    
    /* Status fields */
    enum mxser_must_hwid must_hwid;
    uint8_t nports;
    
    /* reset/probe state */
    bool initialized;
    
    /* power mgmt */
    
    /* other fields */
    qemu_irq irq_line;
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
/* MemoryRegionOps                                                      */
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

    if (addr < 0x10) {
        /* Vector register space */
        if (addr < sizeof(s->vector_reg)) {
            val = s->vector_reg[addr];
        }
    } else if (addr >= 0x10 && addr < 0x20) {
        /* Opmode register space for MU860 */
        if (s->must_hwid == MOXA_MUST_MU860_HWID) {
            int idx = (addr - 0x10) / 8;
            if (idx < 2) {
                val = s->opmode_reg[idx];
            }
        }
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < 0x10) {
        /* Vector register space */
        if (addr < sizeof(s->vector_reg)) {
            s->vector_reg[addr] = val & 0xFF;
        }
    } else if (addr >= 0x10 && addr < 0x20) {
        /* Opmode register space for MU860 */
        if (s->must_hwid == MOXA_MUST_MU860_HWID) {
            int idx = (addr - 0x10) / 8;
            if (idx < 2) {
                s->opmode_reg[idx] = val & 0xFF;
            }
        }
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    int port = addr / 8;
    int reg = addr % 8;

    if (port >= s->nports) {
        return 0;
    }

    switch (reg) {
    case UART_RX:
        val = s->uart_regs[port][UART_RX];
        break;
    /* UART_TX and UART_RX share same offset (0), so handle together */
    case UART_IER:
        val = s->uart_regs[port][UART_IER];
        break;
    case UART_IIR:
        /* Simulate no interrupt pending */
        val = UART_IIR_NO_INT;
        break;
    case UART_LCR:
        val = s->uart_regs[port][UART_LCR];
        break;
    case UART_MCR:
        val = s->uart_regs[port][UART_MCR];
        break;
    case UART_LSR:
        /* Always report transmitter empty and data ready */
        val = UART_LSR_TEMT | UART_LSR_THRE | UART_LSR_DR;
        break;
    case UART_MSR:
        /* Report all modem status lines as active */
        val = UART_MSR_DCD | UART_MSR_DSR | UART_MSR_CTS | UART_MSR_RI;
        break;
    case MOXA_MUST_GDL_REGISTER:
        if (s->must_hwid != MOXA_OTHER_UART) {
            val = 0; /* No data available */
        }
        break;
    /* Note: MOXA_MUST_EFR_REGISTER == UART_FCR == 2, so handled in default */
    default:
        if (reg < 8) {
            val = s->uart_regs[port][reg];
        }
        break;
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    int port = addr / 8;
    int reg = addr % 8;

    if (port >= s->nports) {
        return;
    }

    /* UART_TX and UART_RX share same offset (0) */
    if (reg == UART_TX) {
        /* Store transmitted character */
        s->uart_regs[port][UART_RX] = val & 0xFF;
    } else {
        switch (reg) {
        case UART_IER:
            s->uart_regs[port][UART_IER] = val & 0xFF;
            break;
        case UART_FCR:
            s->uart_regs[port][UART_FCR] = val & 0xFF;
            break;
        case UART_LCR:
            s->uart_regs[port][UART_LCR] = val & 0xFF;
            break;
        case UART_MCR:
            s->uart_regs[port][UART_MCR] = val & 0xFF;
            break;
        /* Note: MOXA_MUST_EFR_REGISTER == UART_FCR == 2, so handled above */
        default:
            if (reg < 8) {
                s->uart_regs[port][reg] = val & 0xFF;
            }
            break;
        }
    }
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

    /* Clear all registers */
    memset(s->uart_regs, 0, sizeof(s->uart_regs));
    memset(s->vector_reg, 0, sizeof(s->vector_reg));
    memset(s->opmode_reg, 0, sizeof(s->opmode_reg));
    
    s->must_hwid = MOXA_OTHER_UART;
    s->initialized = false;
}

/* ------------------------------------------------------------------ */
/* DMA initialize (called from realize)                                */
/* ------------------------------------------------------------------ */
static void pcibase_dma_device_realize(PCIDevice *pdev, Error **errp)
{
    /* No DMA used in this device */
}

/* ------------------------------------------------------------------ */
/* PCI config space access (class-level hooks)                         */
/* ------------------------------------------------------------------ */
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    uint32_t val = pci_default_read_config(pdev, addr, len);

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
    
    pci_default_write_config(pdev, addr, val, len);
}

/* ------------------------------------------------------------------ */
/* Realize (device init)                                                */
/* ------------------------------------------------------------------ */
static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);
    
    /* Set up BARs based on the driver's expectations */
    s->bar_info[2].index = 2;
    s->bar_info[2].type = BAR_TYPE_PIO;
    s->bar_info[2].size = 8 * 8; /* 8 ports * 8 bytes each */
    s->bar_info[2].name = "mxser-io";
    
    s->bar_info[3].index = 3;
    s->bar_info[3].type = BAR_TYPE_MMIO;
    s->bar_info[3].size = 0x20; /* Vector + opmode space */
    s->bar_info[3].name = "mxser-vector";
    
    s->num_bars = 4; /* Only BAR2 and BAR3 are used */
    
    /* Initialize device state */
    s->nports = 8; /* Default to 8 ports */
    s->must_hwid = MOXA_MUST_MU150_HWID; /* Default to MU150 */
    
    /* Set initial register values */
    for (int i = 0; i < s->nports; i++) {
        s->uart_regs[i][UART_LCR] = UART_LCR_WLEN8;
        s->uart_regs[i][UART_MCR] = UART_MCR_DTR | UART_MCR_RTS;
    }
    
    /* register BARs */
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Interrupt config */
    s->irq_line = pci_allocate_irq(pdev);
    
    s->initialized = true;
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

    /* Use qemu_free_irq instead of non-existent pci_release_irq */
    qemu_free_irq(s->irq_line);
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
