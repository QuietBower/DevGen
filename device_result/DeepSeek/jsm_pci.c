/*
 * QEMU model for Digi NEO 2DB9 serial adapter (jsm driver)
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
#include "hw/qdev-properties.h"
#include "hw/qdev-core.h"
#include "migration/vmstate.h"

#define TYPE_PCIBASE_DEVICE "jsm_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* ------------------------------------------------------------
 * Hardware definitions extracted from jsm_driver.c and headers
 * ------------------------------------------------------------ */

/* PCI Identification (from jsm_probe_one) */
#define PCI_VENDOR_ID_DIGI         0x114f
#define PCI_DEVICE_ID_NEO_2DB9     0x00C8
#define PCI_CLASS_SERIAL_OTHER     0x0700

/* Register offsets for NEO UART (from struct neo_uart_struct) */
#define NEO_TXRX_OFFSET     0x00
#define NEO_IER_OFFSET      0x01
#define NEO_ISR_FCR_OFFSET  0x02
#define NEO_LCR_OFFSET      0x03
#define NEO_MCR_OFFSET      0x04
#define NEO_LSR_OFFSET      0x05
#define NEO_MSR_OFFSET      0x06
#define NEO_SPR_OFFSET      0x07
#define NEO_FCTR_OFFSET     0x08
#define NEO_EFR_OFFSET      0x09
#define NEO_TFIFO_OFFSET    0x0A
#define NEO_RFIFO_OFFSET    0x0B
#define NEO_XOFFCHAR1_OFFSET 0x0C
#define NEO_XOFFCHAR2_OFFSET 0x0D
#define NEO_XONCHAR1_OFFSET 0x0E
#define NEO_XONCHAR2_OFFSET 0x0F

/* Channel spacing (bd_uart_offset for NEO devices) */
#define UART_CHANNEL_OFFSET  0x200

/* Bit definitions (from various UART_* macros) */
#define UART_CLASSIC_POLL_ADDR_OFFSET     0x40
#define UART_EXAR654_EFR_ECB      0x10
#define UART_17158_RX_LINE_STATUS  0x1
#define UART_17158_MSR             0x4
#define UART_17158_RXRDY_TIMEOUT   0x2
#define UART_17158_TXRDY           0x3
#define UART_17158_POLL_ADDR_OFFSET 0x80
#define UART_17158_TX_FIFOSIZE     64
#define UART_EXAR654_EFR_RTSDTR   0x40
#define UART_EXAR654_EFR_IXOFF    0x8
#define UART_16654_FCR_TXTRIGGER_16 0x10
#define UART_EXAR654_IER_RTSDTR   0x40
#define UART_16654_FCR_RXTRIGGER_16 0x40
#define UART_EXAR654_EFR_CTSDSR   0x80
#define UART_EXAR654_IER_XOFF     0x20
#define UART_EXAR654_EFR_IXON     0x2
#define UART_EXAR654_IER_CTSDSR   0x80
#define UART_16654_FCR_RXTRIGGER_56 0x80
#define UART_IIR_RDI_TIMEOUT          0x0C
#define UART_17158_RX_FIFO_DATA_ERROR 0x80
#define UART_17158_TX_AND_FIFO_CLR    0x40
#define UART_17158_IIR_RDI_TIMEOUT    0x0C
#define UART_17158_IIR_FIFO_ENABLED   0xC0
#define UART_17158_IIR_HWFLOW_STATE_CHANGE 0x20
#define UART_17158_XON_DETECT     0x2
#define UART_17158_IIR_XONXOFF    0x10
#define UART_17158_XOFF_DETECT    0x1
#define UART_17158_EFR_CTSDSR     0x80
#define UART_17158_EFR_ECB        0x10
#define UART_17158_IER_CTSDSR     0x80
#define UART_17158_EFR_IXON       0x2
#define UART_17158_FCTR_TRGD      0xC0
#define UART_17158_FCTR_RTS_4DELAY 0x01
#define UART_17158_FCTR_RTS_8DELAY 0x03
#define UART_17158_EFR_IXOFF      0x8
#define UART_17158_IER_RTSDTR     0x40
#define UART_17158_IER_XOFF       0x20
#define UART_17158_EFR_RTSDTR     0x40

/* Channel flags (combination bits) */
#define CH_OPENING         0x0080
#define CH_CD              0x0008
#define CH_FCAR            0x0010
#define CH_STOP            0x0002
#define CH_RECEIVER_OFF    0x0040
#define CH_STOPI           0x0004
#define CH_FIFO_ENABLED    0x0200
#define CH_BREAK_SENDING   0x1000
#define CH_TX_FIFO_LWM     0x0800
#define CH_TX_FIFO_EMPTY   0x0400
#define CH_BAUD0           0x08000

/* Board constants (NEO_2DB9 specific: maxports = 2) */
#define MAXLINES    256
#define MAXPORTS    2
#define RQUEUESIZE  (RQUEUEMASK + 1)
#define RQUEUEMASK  0x1FFF
#define EQUEUEMASK  0x1FFF
#define EQUEUESIZE  RQUEUESIZE
#define MAX_STOPS_SENT 5

/* ------------------------------------------------------------
 * QEMU device state structure
 * ------------------------------------------------------------ */

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

/* Per-channel register state (mirror of neo_uart_struct) */
typedef struct {
    uint8_t txrx;       /* 0x00 */
    uint8_t ier;        /* 0x01 */
    uint8_t isr_fcr;    /* 0x02 */
    uint8_t lcr;        /* 0x03 */
    uint8_t mcr;        /* 0x04 */
    uint8_t lsr;        /* 0x05 */
    uint8_t msr;        /* 0x06 */
    uint8_t spr;        /* 0x07 */
    uint8_t fctr;       /* 0x08 */
    uint8_t efr;        /* 0x09 */
    uint8_t tfifo;      /* 0x0A */
    uint8_t rfifo;      /* 0x0B */
    uint8_t xoffchar1;  /* 0x0C */
    uint8_t xoffchar2;  /* 0x0D */
    uint8_t xonchar1;   /* 0x0E */
    uint8_t xonchar2;   /* 0x0F */
} NeoChannelState;

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* UART channels */
    NeoChannelState channels[MAXPORTS];

    /* Board-level interrupt status placeholder (offset unknown) */
    uint32_t board_intr_status;
};

/* Update the PCI IRQ line based on channel interrupt status */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    int i;
    bool any_intr = false;

    for (i = 0; i < MAXPORTS; i++) {
        NeoChannelState *ch = &s->channels[i];
        /* Simple interrupt logic: if any IER-enabled status bit is set, intr asserted.
         * Status bits considered: RX data available (LSR bit0), TX empty (LSR bit5).
         * We'll only generate TX empty interrupt for now.
         */
        if (ch->ier & 0x02) { /* THRE interrupt enable */
            if (ch->lsr & 0x20) { /* TX holding empty */
                any_intr = true;
                break;
            }
        }
        /* Additional status checks could be added when driver source provided */
    }

    if (any_intr) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* MMIO read handler */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    int channel;
    hwaddr channel_offset;

    channel = addr / UART_CHANNEL_OFFSET;
    if (channel >= MAXPORTS) {
        return 0;
    }
    channel_offset = addr % UART_CHANNEL_OFFSET;
    if (channel_offset >= 0x10) {
        /* offset not used by known registers, return 0 */
        return 0;
    }

    NeoChannelState *ch = &s->channels[channel];

    switch (channel_offset) {
    case NEO_TXRX_OFFSET:
        val = ch->txrx;
        break;
    case NEO_IER_OFFSET:
        val = ch->ier;
        break;
    case NEO_ISR_FCR_OFFSET:
        /* Read ISR: compute interrupt identification */
        {
            uint8_t iir = 0x01; /* no interrupt pending */
            if (ch->ier & 0x02 && ch->lsr & 0x20) {
                iir = 0x02; /* THRE interrupt */
            }
            val = iir;
        }
        break;
    case NEO_LCR_OFFSET:
        val = ch->lcr;
        break;
    case NEO_MCR_OFFSET:
        val = ch->mcr;
        break;
    case NEO_LSR_OFFSET:
        /* Always report TX holding empty and TX empty, no errors */
        val = 0x60;
        break;
    case NEO_MSR_OFFSET:
        val = ch->msr;
        break;
    case NEO_SPR_OFFSET:
        val = ch->spr;
        break;
    case NEO_FCTR_OFFSET:
        val = ch->fctr;
        break;
    case NEO_EFR_OFFSET:
        val = ch->efr;
        break;
    case NEO_TFIFO_OFFSET:
        val = ch->tfifo;
        break;
    case NEO_RFIFO_OFFSET:
        val = ch->rfifo;
        break;
    case NEO_XOFFCHAR1_OFFSET:
        val = ch->xoffchar1;
        break;
    case NEO_XOFFCHAR2_OFFSET:
        val = ch->xoffchar2;
        break;
    case NEO_XONCHAR1_OFFSET:
        val = ch->xonchar1;
        break;
    case NEO_XONCHAR2_OFFSET:
        val = ch->xonchar2;
        break;
    default:
        break;
    }

    return val;
}

/* MMIO write handler */
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    int channel;
    hwaddr channel_offset;

    channel = addr / UART_CHANNEL_OFFSET;
    if (channel >= MAXPORTS) {
        return;
    }
    channel_offset = addr % UART_CHANNEL_OFFSET;
    if (channel_offset >= 0x10) {
        return; /* not a known register */
    }

    NeoChannelState *ch = &s->channels[channel];

    switch (channel_offset) {
    case NEO_TXRX_OFFSET:
        ch->txrx = (uint8_t)val;
        /* Writing to THR should clear TX empty status momentarily; we ignore for now */
        break;
    case NEO_IER_OFFSET:
        ch->ier = (uint8_t)val;
        pcibase_update_irq(s);
        break;
    case NEO_ISR_FCR_OFFSET:
        /* FCR write (shared address) */
        ch->isr_fcr = (uint8_t)val;
        break;
    case NEO_LCR_OFFSET:
        ch->lcr = (uint8_t)val;
        break;
    case NEO_MCR_OFFSET:
        ch->mcr = (uint8_t)val;
        break;
    case NEO_LSR_OFFSET:
        /* LSR is typically read-only; ignore write */
        break;
    case NEO_MSR_OFFSET:
        ch->msr = (uint8_t)val;
        break;
    case NEO_SPR_OFFSET:
        ch->spr = (uint8_t)val;
        break;
    case NEO_FCTR_OFFSET:
        ch->fctr = (uint8_t)val;
        break;
    case NEO_EFR_OFFSET:
        ch->efr = (uint8_t)val;
        break;
    case NEO_TFIFO_OFFSET:
        ch->tfifo = (uint8_t)val;
        break;
    case NEO_RFIFO_OFFSET:
        ch->rfifo = (uint8_t)val;
        break;
    case NEO_XOFFCHAR1_OFFSET:
        ch->xoffchar1 = (uint8_t)val;
        break;
    case NEO_XOFFCHAR2_OFFSET:
        ch->xoffchar2 = (uint8_t)val;
        break;
    case NEO_XONCHAR1_OFFSET:
        ch->xonchar1 = (uint8_t)val;
        break;
    case NEO_XONCHAR2_OFFSET:
        ch->xonchar2 = (uint8_t)val;
        break;
    default:
        break;
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

/* PIO handlers deleted as NEO device uses only MMIO */

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    int i;

    pci_device_reset(PCI_DEVICE(dev));

    for (i = 0; i < MAXPORTS; i++) {
        memset(&s->channels[i], 0, sizeof(NeoChannelState));
        /* Set LSR to indicate TX ready */
        s->channels[i].lsr = 0x60;
    }
    s->board_intr_status = 0;

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
        /* Not used for NEO */
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_DIGI );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_NEO_2DB9 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_SERIAL_OTHER );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* BAR Initialization: single memory BAR0 (NEO devices use BAR0 only) */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x2000; /* 8K, enough for 2 channels of 0x200 each plus extra */
    s->bar_info[0].name = "jsm-mmio";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Field Init */
    memset(s->channels, 0, sizeof(s->channels));
    s->board_intr_status = 0;

    /* Set default LSR ready for all channels */
    for (int i = 0; i < MAXPORTS; i++) {
        s->channels[i].lsr = 0x60;
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
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "jsm_pci",
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
