/*
 * QEMU DE2104x PCI Ethernet Device Emulation
 *
 * Based on Linux driver: /linux-6.18/drivers/net/ethernet/dec/tulip/de2104x.c
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

#define TYPE_PCIBASE_DEVICE "de2104x_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register offsets and bit definitions from driver */
#define PCI_VENDOR_ID_DEC		0x1011
#define PCI_DEVICE_ID_DEC_TULIP		0x0002
#define PCI_DEVICE_ID_DEC_TULIP_PLUS	0x0009

#define DE_NUM_REGS		16
#define DE_REGS_SIZE		(DE_NUM_REGS * sizeof(uint32_t))

/* Interrupt status bits */
#define IntrOK			0x00000001
#define IntrErr			0x00000002
#define RxIntr			0x00000004
#define RxEmpty			0x00000008
#define TxIntr			0x00000010
#define TxEmpty			0x00000020
#define LinkPass		0x00000040
#define LinkFail		0x00000080
#define PciErr			0x00000100

/* Descriptor bits */
#define DescOwn			0x80000000
#define RingEnd			0x02000000
#define FirstFrag		0x01000000
#define LastFrag		0x00800000
#define TxSwInt			0x00020000
#define SetupFrame		0x00080000
#define TxError			0x00008000
#define TxOWC			0x00000400
#define TxMaxCol		0x00000200
#define TxLinkFail		0x00000100
#define TxFIFOUnder		0x00000080
#define RxError			0x00008000
#define RxErrCRC		0x00000020
#define RxErrFIFO		0x00000040

/* MAC mode bits */
#define RxTx			0x00000003
#define FullDuplex		0x00000200
#define AcceptAllMulticast	0x00000020
#define AcceptAllPhys		0x00000010

/* Bus mode bits */
#define CmdReset		0x00000001

/* SIA status bits */
#define NetCxnErr		0x00000001
#define LinkFailStatus		0x00000002
#define NonselPortActive	0x00000004
#define SelPortActive		0x00000008
#define NWayState		0x00000070
#define NWayRestart		0x00000010

/* Media types */
#define DE_MEDIA_TP_AUTO	0
#define DE_MEDIA_BNC		1
#define DE_MEDIA_AUI		2
#define DE_MEDIA_TP		3
#define DE_MEDIA_TP_FD		4
#define DE_MEDIA_INVALID	5

/* EEPROM commands */
#define EE_READ_CMD		0x6
#define EE_ENB			(1<<31)
#define EE_CS			(1<<30)
#define EE_SHIFT_CLK		(1<<29)
#define EE_DATA_WRITE		(1<<28)
#define EE_DATA_READ		(1<<27)

/* Register offsets (in bytes) */
#define ROMCmd			0x00
#define BusMode			0x08
#define TxPoll			0x10
#define RxPoll			0x18
#define RxRingAddr		0x20
#define TxRingAddr		0x28
#define MacStatus		0x38
#define MacMode			0x40
#define IntrMask		0x48
#define RxMissed		0x50
#define CSR11			0x58
#define CSR13			0x68
#define CSR14			0x70
#define CSR15			0x78
#define SIAStatus		0x80

/* Values */
#define NormalRxPoll		0x00000001
#define NormalTxPoll		0x00000001
#define FULL_DUPLEX_MAGIC	0x00000000
#define PM_Mask			0x00000003
#define PM_Sleep		0x00000003
#define PCIPM			0x40

#define DE_RX_RING_SIZE		64
#define DE_TX_RING_SIZE		64
#define PKT_BUF_SZ		1536
#define RX_OFFSET		2
#define rx_copybreak		200

#define TX_BUFFS_AVAIL(de) \
	(((de)->tx_tail <= (de)->tx_head) ? \
		(de)->tx_tail + (DE_TX_RING_SIZE - 1) - (de)->tx_head : \
		(de)->tx_tail - (de)->tx_head - 1)

#define NEXT_RX(n) (((n) + 1) & (DE_RX_RING_SIZE - 1))
#define NEXT_TX(n) (((n) + 1) & (DE_TX_RING_SIZE - 1))

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
/* Descriptor format                                                   */
/* ------------------------------------------------------------------ */
struct de_desc {
    uint32_t opts1;
    uint32_t addr1;
    uint32_t opts2;
    uint32_t addr2;
};

/* ------------------------------------------------------------------ */
/* Device State                                                        */
/* ------------------------------------------------------------------ */
struct PCIBaseState {
    PCIDevice parent_obj;

    /* BAR memory regions */
    MemoryRegion bar_regions[6];

    /* MMIO backing store */
    uint8_t *mmio_backing;
    size_t mmio_backing_size;

    /* BAR table */
    BARInfo bar_info[6];
    int num_bars;

    /* interrupt state feature flags */
    bool has_msi;
    bool has_msix;
    
    /* DMA info */
    dma_addr_t ring_dma;
    struct de_desc *rx_ring;
    struct de_desc *tx_ring;
    size_t ring_size;
    
    /* Register shadows */
    uint32_t mac_status;
    uint32_t mac_mode;
    uint32_t intr_mask;
    uint32_t rx_missed;
    uint32_t sia_status;
    uint32_t bus_mode;
    
    /* Status fields */
    unsigned rx_tail;
    unsigned tx_head;
    unsigned tx_tail;
    bool carrier_ok;
    bool media_lock;
    uint32_t media_type;
    uint32_t media_supported;
    uint32_t media_advertise;
    
    /* reset/probe state */
    bool de21040;
    
    /* power mgmt */
    uint32_t pm_state;
    
    /* other fields */
    QEMUTimer media_timer;
    bool timer_active;
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
static void de_media_timer_cb(void *opaque);
static void de_start_rxtx(PCIBaseState *s);
static void de_stop_rxtx(PCIBaseState *s);

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
/* MMIO / PIO handlers                                                 */
/* ------------------------------------------------------------------ */

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= DE_REGS_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR, "[%s] mmio_read out of range addr=%" PRIx64 "\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr);
        return 0;
    }

    switch (addr) {
    case MacStatus:
        val = s->mac_status;
        break;
    case MacMode:
        val = s->mac_mode;
        break;
    case RxMissed:
        val = s->rx_missed;
        s->rx_missed = 0; /* self-clearing */
        break;
    case SIAStatus:
        val = s->sia_status;
        break;
    case BusMode:
        val = s->bus_mode;
        break;
    default:
        if (s->mmio_backing && addr + size <= s->mmio_backing_size) {
            memcpy(&val, s->mmio_backing + addr, size);
        }
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= DE_REGS_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR, "[%s] mmio_write out of range addr=%" PRIx64 "\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr);
        return;
    }

    switch (addr) {
    case MacStatus:
        /* Writing clears the bits */
        s->mac_status &= ~val;
        break;
    case MacMode:
        s->mac_mode = val;
        if (val & RxTx) {
            de_start_rxtx(s);
        } else {
            de_stop_rxtx(s);
        }
        break;
    case IntrMask:
        s->intr_mask = val;
        break;
    case RxRingAddr:
        s->ring_dma = val;
        break;
    case TxRingAddr:
        /* Ignored, we use the same ring for both */
        break;
    case TxPoll:
        /* Trigger TX */
        if (val & NormalTxPoll) {
            /* Simulate TX completion */
            if (s->tx_tail != s->tx_head) {
                s->mac_status |= TxIntr;
                if (s->intr_mask & TxIntr) {
                    pci_set_irq(PCI_DEVICE(s), 1);
                }
            }
        }
        break;
    case RxPoll:
        /* Trigger RX */
        if (val & NormalRxPoll) {
            /* Simulate RX packet */
            if (s->rx_ring && !(le32_to_cpu(s->rx_ring[s->rx_tail].opts1) & DescOwn)) {
                /* Mark descriptor as owned by device */
                s->rx_ring[s->rx_tail].opts1 = cpu_to_le32(DescOwn | 0x0300 | ((PKT_BUF_SZ - 4) << 16));
                s->mac_status |= RxIntr;
                if (s->intr_mask & RxIntr) {
                    pci_set_irq(PCI_DEVICE(s), 1);
                }
                s->rx_tail = NEXT_RX(s->rx_tail);
            }
        }
        break;
    case CSR13:
    case CSR14:
    case CSR15:
        /* Media setup registers - handled in de_set_media */
        break;
    case SIAStatus:
        s->sia_status = val;
        break;
    case BusMode:
        s->bus_mode = val;
        if (val & CmdReset) {
            /* Reset the device */
            pcibase_reset(DEVICE(s));
        }
        break;
    default:
        if (s->mmio_backing && addr + size <= s->mmio_backing_size) {
            memcpy(s->mmio_backing + addr, &val, size);
        }
        break;
    }
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
/* Media timer callback                                                */
/* ------------------------------------------------------------------ */
static void de_media_timer_cb(void *opaque)
{
    PCIBaseState *s = opaque;
    s->timer_active = false;
    
    /* Simulate link up after timer */
    if (s->media_type == DE_MEDIA_TP_AUTO || 
        s->media_type == DE_MEDIA_TP || 
        s->media_type == DE_MEDIA_TP_FD) {
        /* Inline link up logic to avoid unused function */
        if (!s->carrier_ok) {
            s->carrier_ok = true;
            s->mac_status |= LinkPass;
            if (s->intr_mask & LinkPass) {
                pci_set_irq(PCI_DEVICE(s), 1);
            }
        }
        s->sia_status &= ~LinkFailStatus;
        s->sia_status |= NetCxnErr; /* Clear error */
    }
}

/* ------------------------------------------------------------------ */
/* RX/TX control                                                       */
/* ------------------------------------------------------------------ */
static void de_start_rxtx(PCIBaseState *s)
{
    s->mac_mode |= RxTx;
}

static void de_stop_rxtx(PCIBaseState *s)
{
    s->mac_mode &= ~RxTx;
}

/* ------------------------------------------------------------------ */
/* Reset                                                               */
/* ------------------------------------------------------------------ */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    /* core resets */
    pci_device_reset(pdev);

    /* Clear device state */
    s->mac_status = 0;
    s->mac_mode = 0;
    s->intr_mask = 0;
    s->rx_missed = 0;
    s->sia_status = 0;
    s->bus_mode = 0;
    
    s->rx_tail = 0;
    s->tx_head = 0;
    s->tx_tail = 0;
    s->carrier_ok = false;
    s->media_lock = false;
    s->media_type = DE_MEDIA_TP_AUTO;
    s->media_supported = (1 << DE_MEDIA_TP_AUTO) | (1 << DE_MEDIA_BNC) | 
                         (1 << DE_MEDIA_AUI) | (1 << DE_MEDIA_TP) | 
                         (1 << DE_MEDIA_TP_FD);
    s->media_advertise = s->media_supported;
    
    s->pm_state = 0;
    
    if (s->timer_active) {
        timer_del(&s->media_timer);
        s->timer_active = false;
    }
    
    if (s->mmio_backing && s->mmio_backing_size)
        memset(s->mmio_backing, 0, s->mmio_backing_size);
}

/* ------------------------------------------------------------------ */
/* DMA initialize                                                      */
/* ------------------------------------------------------------------ */
static void pcibase_dma_device_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    
    /* Allocate descriptor rings */
    s->ring_size = (DE_RX_RING_SIZE + DE_TX_RING_SIZE) * sizeof(struct de_desc);
    s->rx_ring = g_malloc0(s->ring_size);
    s->tx_ring = &s->rx_ring[DE_RX_RING_SIZE];
    
    /* Initialize rings */
    s->rx_ring[DE_RX_RING_SIZE - 1].opts2 = cpu_to_le32(RingEnd);
    s->tx_ring[DE_TX_RING_SIZE - 1].opts2 = cpu_to_le32(RingEnd);
}

/* ------------------------------------------------------------------ */
/* PCI config space access                                             */
/* ------------------------------------------------------------------ */
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    uint32_t val = pci_default_read_config(pdev, addr, len);
    
    /* Handle PM config space */
    if (addr == PCIPM) {
        PCIBaseState *s = PCIBASE_DEVICE(pdev);
        val = (val & ~PM_Mask) | (s->pm_state & PM_Mask);
    }
    
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
    
    /* Handle PM config space */
    if (addr == PCIPM) {
        PCIBaseState *s = PCIBASE_DEVICE(pdev);
        s->pm_state = (s->pm_state & ~PM_Mask) | (val & PM_Mask);
        if (s->pm_state & PM_Sleep) {
            /* Device going to sleep */
        } else {
            /* Device waking up */
        }
    }
    
    pci_default_write_config(pdev, addr, val, len);
}

/* ------------------------------------------------------------------ */
/* Realize (device init)                                               */
/* ------------------------------------------------------------------ */
static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_DEC);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_DEC_TULIP);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_ETHERNET);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);
    
    /* Set up BARs */
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_NONE;
    s->bar_info[0].size = 0;
    s->bar_info[0].name = "unused";
    
    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = DE_REGS_SIZE;
    s->bar_info[1].name = "de2104x-mmio";
    
    s->num_bars = 2;
    
    /* Allocate MMIO backing store */
    s->mmio_backing_size = DE_REGS_SIZE;
    s->mmio_backing = g_malloc0(s->mmio_backing_size);
    
    /* register BARs */
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize media type based on device ID */
    if (pci_get_word(pci_conf + PCI_DEVICE_ID) == PCI_DEVICE_ID_DEC_TULIP) {
        s->de21040 = true;
        s->media_type = DE_MEDIA_TP;
        s->media_supported = (1 << DE_MEDIA_TP) | (1 << DE_MEDIA_AUI) | (1 << DE_MEDIA_TP_FD);
    } else {
        s->de21040 = false;
        s->media_type = DE_MEDIA_TP_AUTO;
        s->media_supported = (1 << DE_MEDIA_TP_AUTO) | (1 << DE_MEDIA_BNC) | 
                             (1 << DE_MEDIA_AUI) | (1 << DE_MEDIA_TP) | 
                             (1 << DE_MEDIA_TP_FD);
    }
    s->media_advertise = s->media_supported;
    
    /* Initialize timer */
    timer_init_ms(&s->media_timer, QEMU_CLOCK_VIRTUAL, de_media_timer_cb, s);
    
    /* DMA init */
    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) return;
    
    /* Set initial state */
    s->carrier_ok = false;
    s->mac_status = 0;
    s->mac_mode = 0;
    s->intr_mask = 0;
    s->rx_missed = 0;
    s->sia_status = 0;
    s->bus_mode = 0;
    s->pm_state = 0;
    
    qemu_log_mask(LOG_GUEST_ERROR, "[%s] device realized\n", TYPE_PCIBASE_DEVICE);
}

/* ------------------------------------------------------------------ */
/* Uninit/cleanup                                                      */
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

    /* free mmio_backing or other allocations */
    if (s->mmio_backing) {
        g_free(s->mmio_backing);
        s->mmio_backing = NULL;
    }
    
    if (s->rx_ring) {
        g_free(s->rx_ring);
        s->rx_ring = NULL;
        s->tx_ring = NULL;
    }
    
    if (s->timer_active) {
        timer_del(&s->media_timer);
        s->timer_active = false;
    }

    qemu_log_mask(LOG_GUEST_ERROR, "[%s] device uninit\n", TYPE_PCIBASE_DEVICE);
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

    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
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
