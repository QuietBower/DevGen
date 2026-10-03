/*
 * QEMU RME9652 PCI audio device model
 * Based on driver snd-rme9652 (/home/eely/linux-7.1/sound/pci/rme9652/rme9652.c)
 * Emulates PIO register interface, legacy IRQ, and DMA pointer updates.
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

#define TYPE_PCIBASE_DEVICE "snd_rme9652_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Vendor and Device IDs */
#define RME9652_VENDOR_ID          0x10ee
#define RME9652_DEVICE_ID          0x3fc4
#define RME9652_CLASS_ID           PCI_CLASS_MULTIMEDIA_AUDIO

/* I/O region size (power of 2) */
#define RME9652_IO_EXTENT          1024

/* Register offsets (in bytes from BAR0 base) */
#define RME9652_STATUS_REGISTER    0x00
#define RME9652_INIT_BUFFER        0x00   /* write-only, same offset as status */
#define RME9652_play_buffer        0x20   /* 32 */
#define RME9652_rec_buffer         0x24   /* 36 */
#define RME9652_control_register   0x40   /* 64 */
#define RME9652_IRQ_CLEAR          0x60   /* 96 */
#define RME9652_TIME_CODE          0x64   /* 100 */
#define RME9652_thru_base          0x80   /* 128 */

/* Control register bits */
#define RME9652_START_BIT          (1<<0)
#define RME9652_MASTER             (1<<4)
#define RME9652_IE                 (1<<5)
#define RME9652_FREQ               (1<<6)
#define RME9652_FREQ1              (1<<7)
#define RME9652_DS                 (1<<8)
#define RME9652_PRO                (1<<9)
#define RME9652_EMP                (1<<10)
#define RME9652_DOLBY              (1<<11)
#define RME9652_OPT_OUT            (1<<12)
#define RME9652_WSEL               (1<<13)
#define RME9652_inp_0              (1<<14)
#define RME9652_INP_1              (1<<15)
#define RME9652_SYNCPREF_ADAT2     (1<<16)
#define RME9652_SYNCPREF_ADAT3     (1<<17)
#define RME9652_SPDIF_RESET        (1<<18)
#define RME9652_SPDIF_SELECT       (1<<19)
#define RME9652_SPDIF_CLOCK        (1<<20)
#define RME9652_SPDIF_WRITE        (1<<21)
#define RME9652_ADAT1_INTERNAL     (1<<22)
#define RME9652_LATENCY_MASK       0x0e

/* Status register bits */
#define RME9652_IRQ                (1<<0)
#define RME9652_LOCK_2             (1<<1)
#define RME9652_LOCK_1             (1<<2)
#define RME9652_LOCK_0             (1<<3)
#define RME9652_FS48               (1<<4)
#define RME9652_WSEL_RD            (1<<5)
#define RME9652_SYNC_2             (1<<16)
#define RME9652_SYNC_1             (1<<17)
#define RME9652_SYNC_0             (1<<18)
#define RME9652_DS_RD              (1<<19)
#define RME9652_TC_BUSY            (1<<20)
#define RME9652_TC_OUT             (1<<21)
#define RME9652_F_0                (1<<22)
#define RME9652_F_1                (1<<23)
#define RME9652_F_2                (1<<24)
#define RME9652_ERF                (1<<25)
#define RME9652_BUFFER_ID          (1<<26)
#define RME9652_TC_VALID           (1<<27)
#define RME9652_SPDIF_READ         (1<<28)

#define RME9652_SYNC_MASK          (RME9652_SYNC_0|RME9652_SYNC_1|RME9652_SYNC_2)
#define RME9652_LOCK_MASK          (RME9652_LOCK_0|RME9652_LOCK_1|RME9652_LOCK_2)
#define RME9652_F_MASK             (RME9652_F_0|RME9652_F_1|RME9652_F_2)

/* Additional inferred/needed defines */
#define RME9652_buf_pos            0xFFFF  /* 16-bit buffer position */
#define RME9652_latency            RME9652_LATENCY_MASK
#define RME9652_Master             RME9652_MASTER
#define RME9652_wsel               RME9652_WSEL
#define RME9652_freq               RME9652_FREQ
#define RME9652_inp                (RME9652_inp_0 | RME9652_INP_1)
#define RME9652_opt_out            RME9652_OPT_OUT
#define RME9652_PRO                RME9652_PRO
#define RME9652_Dolby              RME9652_DOLBY
#define RME9652_EMP                RME9652_EMP
#define RME9652_ADAT1_INTERNAL     RME9652_ADAT1_INTERNAL
#define RME9652_SPDIF_WRITE        RME9652_SPDIF_WRITE
#define RME9652_SPDIF_CLOCK        RME9652_SPDIF_CLOCK
#define RME9652_SPDIF_SELECT       RME9652_SPDIF_SELECT
#define RME9652_SPDIF_RESET        RME9652_SPDIF_RESET
#define RME9652_SPDIF_READ         RME9652_SPDIF_READ
#define RME9652_SyncPref_ADAT1     0
#define RME9652_SyncPref_ADAT2     RME9652_SYNCPREF_ADAT2
#define RME9652_SyncPref_ADAT3     RME9652_SYNCPREF_ADAT3
#define RME9652_SyncPref_SPDIF     (RME9652_SYNCPREF_ADAT2 | RME9652_SYNCPREF_ADAT3)
#define RME9652_SyncPref_Mask      (RME9652_SYNCPREF_ADAT2 | RME9652_SYNCPREF_ADAT3)

/* Channel and buffer counts */
#define RME9652_NCHANNELS          26
/* Buffer sizes are not needed for basic register interface */

/* BAR types */
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

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;
    uint32_t intr_status;   /* pending interrupts */
    uint32_t intr_mask;     /* optional interrupt mask */

    /* Hardware Register Shadows */
    uint32_t init_buffer;       /* offset 0 (write) */
    uint32_t play_buffer;       /* offset 0x20 */
    uint32_t rec_buffer;        /* offset 0x24 */
    uint32_t control;           /* offset 0x40 */
    uint32_t status;            /* offset 0x00 (read) */
    uint32_t irq_clear;         /* offset 0x60 (write) */
    uint32_t time_code;         /* offset 0x64 */
    uint32_t thru_base;         /* offset 0x80 */

    /* DMA Context */
    struct {
        dma_addr_t play_addr;
        dma_addr_t rec_addr;
    } dma;

    /* Operational status flags */
    bool running;

    /* Hardware revision */
    int hw_rev;

    /* Timer for DMA pointer updates and interrupt generation */
    QEMUTimer *timer;
    uint32_t dma_pos;           /* current DMA buffer position (in bytes) */
    uint32_t period_bytes;      /* cached period size in bytes */
    bool irq_active;            /* internal IRQ line state */
};

/* Forward declarations */
static void pcibase_timer_cb(void *opaque);
static void pcibase_update_irq(PCIBaseState *s);

/* Compute the timer period in microseconds based on current rate and period_bytes */
static uint64_t pcibase_timer_interval(PCIBaseState *s)
{
    uint32_t sample_rate;
    uint32_t period_samples;

    /* Determine current sample rate */
    bool ds = s->control & RME9652_DS;
    bool fs48 = s->status & RME9652_FS48;
    if (ds) {
        sample_rate = fs48 ? 96000 : 88200;
    } else {
        sample_rate = fs48 ? 48000 : 44100;
    }

    /* period_bytes is set when latency changes or on start */
    period_samples = s->period_bytes / 4;  /* 4 bytes per sample per channel */
    if (period_samples == 0) {
        period_samples = 1; /* avoid divide by zero */
    }

    return (uint64_t)period_samples * 1000000 / sample_rate;
}

static void pcibase_timer_cb(void *opaque)
{
    PCIBaseState *s = opaque;

    if (!(s->control & RME9652_START_BIT)) {
        return;
    }

    /* Advance DMA pointer by one period, wrapping at double-buffer (2 * period_bytes) */
    s->dma_pos += s->period_bytes;
    if (s->dma_pos >= 2 * s->period_bytes) {
        s->dma_pos -= 2 * s->period_bytes;
    }

    /* Update status register: set buffer_id bit if in second half */
    uint32_t bufid = (s->dma_pos >= s->period_bytes) ? RME9652_BUFFER_ID : 0;
    s->status = (s->status & ~(RME9652_buf_pos | RME9652_BUFFER_ID)) |
                (s->dma_pos & RME9652_buf_pos) | bufid;

    /* Set IRQ pending */
    s->status |= RME9652_IRQ;
    pcibase_update_irq(s);

    /* Reschedule timer */
    timer_mod(s->timer, qemu_clock_get_us(QEMU_CLOCK_VIRTUAL) + pcibase_timer_interval(s));
}

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool irq_should_be_active = (s->status & RME9652_IRQ) && (s->control & RME9652_IE);
    if (irq_should_be_active != s->irq_active) {
        s->irq_active = irq_should_be_active;
        pci_set_irq(pdev, irq_should_be_active ? 1 : 0);
    }
}

static void pcibase_start_timer(PCIBaseState *s)
{
    if (!s->timer) {
        return;
    }
    if (s->period_bytes == 0) {
        /* Compute period_bytes from latency bits (bits 1-3) */
        uint32_t lat = s->control & RME9652_LATENCY_MASK;
        uint32_t decode = lat >> 1;  /* decode_latency: shift right by 1 */
        s->period_bytes = 1 << (decode + 8);  /* 1 << (decode + 8) */
    }
    s->dma_pos = 0;
    s->status &= ~(RME9652_buf_pos | RME9652_BUFFER_ID);
    /* Schedule first interrupt after one period */
    timer_mod(s->timer, qemu_clock_get_us(QEMU_CLOCK_VIRTUAL) + pcibase_timer_interval(s));
}

static void pcibase_stop_timer(PCIBaseState *s)
{
    if (s->timer) {
        timer_del(s->timer);
    }
    s->dma_pos = 0;
    s->status &= ~(RME9652_buf_pos | RME9652_BUFFER_ID | RME9652_IRQ);
    pcibase_update_irq(s);
}

/* MMIO/PIO handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size != 4) {
        return ~0ULL;
    }

    switch (addr) {
    case 0x00: /* status register (read) */
        val = s->status;
        break;
    case 0x20:
        val = s->play_buffer;
        break;
    case 0x24:
        val = s->rec_buffer;
        break;
    case 0x40:
        val = s->control;
        break;
    case 0x64:
        val = s->time_code;
        break;
    default:
        if (addr >= 0x80 && addr < 0x80 + RME9652_NCHANNELS * 4) {
            /* thru_base: not read by driver, return 0 */
            val = 0;
        } else {
            val = 0;
        }
        break;
    }
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        return;
    }

    switch (addr) {
    case 0x00: /* init_buffer (write-only) */
        s->init_buffer = val;
        break;
    case 0x20:
        s->play_buffer = val;
        break;
    case 0x24:
        s->rec_buffer = val;
        break;
    case 0x40:
    {
        uint32_t old_control = s->control;
        s->control = val;

        /* Update status register FS48 bit based on FREQ and DS settings */
        bool ds = s->control & RME9652_DS;
        bool freq = s->control & RME9652_FREQ;
        if (ds) {
            s->status = (s->status & ~RME9652_FS48) | (freq ? RME9652_FS48 : 0);
        } else {
            s->status = (s->status & ~RME9652_FS48) | (freq ? RME9652_FS48 : 0);
        }

        /* Handle start/stop and IE changes */
        bool old_running = old_control & RME9652_START_BIT;
        bool new_running = s->control & RME9652_START_BIT;
        bool ie = s->control & RME9652_IE;

        if (new_running && ie) {
            if (!old_running) {
                /* Start timer */
                pcibase_start_timer(s);
            } else {
                /* Already running, possibly rate/latency changed: restart timer with new period */
                pcibase_start_timer(s);
            }
        } else {
            pcibase_stop_timer(s);
        }
        break;
    }
    case 0x60: /* IRQ clear: write 0 clears IRQ */
        s->status &= ~RME9652_IRQ;
        pcibase_update_irq(s);
        break;
    case 0x64:
        s->time_code = val;
        break;
    default:
        if (addr >= 0x80 && addr < 0x80 + RME9652_NCHANNELS * 4) {
            /* thru_base writes: ignored */
        } else {
            /* Other offsets (e.g., 0x00-0x1C reset pointer writes): ignored */
        }
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

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Reset registers to power-on defaults */
    s->init_buffer = 0;
    s->play_buffer = 0;
    s->rec_buffer = 0;
    s->control = 0;
    s->status = 0;
    s->irq_clear = 0;
    s->time_code = 0;
    s->thru_base = 0;
    s->intr_status = 0;
    s->running = false;
    s->period_bytes = 0;
    s->dma_pos = 0;
    s->irq_active = false;
    pcibase_stop_timer(s);
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    /* CRITICAL: PCI requires BAR sizes to be a power of 2. Prevent QEMU assert crash. */
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  RME9652_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  RME9652_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, RME9652_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x08);  /* rev 8 (RME9636 eprom) to match driver acceptance */
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = RME9652_IO_EXTENT;
    s->bar_info[0].name = "rme9652-io";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No MSI or MSI-X; legacy IRQ only */

    /* DMA address initialization */
    s->dma.play_addr = 0;
    s->dma.rec_addr = 0;

    /* Timer for DMA pointer update */
    s->timer = timer_new_us(QEMU_CLOCK_VIRTUAL, pcibase_timer_cb, s);

    /* Final state initialization */
    s->hw_rev = 0;
    pcibase_reset(DEVICE(pdev));
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (s->timer) {
        timer_del(s->timer);
        timer_free(s->timer);
        s->timer = NULL;
    }
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "snd_rme9652_pci",
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
