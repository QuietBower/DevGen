/*
 * QEMU emulation of EMU10K1X audio device for Linux driver snd-emu10k1x.
 * Generated for QEMU 8.2.10.
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

#define TYPE_PCIBASE_DEVICE "snd_emu10k1x_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID  0x1102
#define DEVICE_ID  0x0006
#define CLASS_ID   0x0401
#define BAR0_SIZE  0x80

/* Direct I/O registers */
#define PTR              0x00
#define DATA             0x04
#define IPR              0x08
#define INTE             0x0c
#define HCFG             0x14
#define AC97DATA         0x1c
#define AC97ADDRESS      0x1e
#define GPIO             0x18

/* Indirect registers (accessed via PTR/DATA) */
#define PLAYBACK_LIST_ADDR     0x00
#define PLAYBACK_LIST_SIZE     0x01
#define PLAYBACK_LIST_PTR      0x02
#define PLAYBACK_DMA_ADDR      0x04
#define PLAYBACK_PERIOD_SIZE   0x05
#define PLAYBACK_POINTER       0x06
#define PLAYBACK_UNKNOWN1      0x07
#define PLAYBACK_UNKNOWN2      0x08
#define CAPTURE_DMA_ADDR       0x10
#define CAPTURE_BUFFER_SIZE    0x11
#define CAPTURE_POINTER        0x12
#define CAPTURE_UNKNOWN        0x13
#define TRIGGER_CHANNEL        0x40
#define TRIGGER_CHANNEL_0      0x00000001
#define TRIGGER_CHANNEL_1      0x00000002
#define TRIGGER_CHANNEL_2      0x00000004
#define TRIGGER_CAPTURE        0x00000100
#define ROUTING                0x41
#define ROUTING_FRONT_LEFT     0x00000001
#define ROUTING_FRONT_RIGHT    0x00000002
#define ROUTING_REAR_LEFT      0x00000004
#define ROUTING_REAR_RIGHT     0x00000008
#define ROUTING_CENTER_LFE     0x00010000
#define SPCS0                  0x42
#define SPCS1                  0x43
#define SPCS2                  0x44
#define SPCS_CLKACCYMASK       0x30000000
#define SPCS_CLKACCY_1000PPM   0x00000000
#define SPCS_CLKACCY_50PPM     0x10000000
#define SPCS_CLKACCY_VARIABLE  0x20000000
#define SPCS_SAMPLERATEMASK    0x0f000000
#define SPCS_SAMPLERATE_44     0x00000000
#define SPCS_SAMPLERATE_48     0x02000000
#define SPCS_SAMPLERATE_32     0x03000000
#define SPCS_CHANNELNUMMASK    0x00f00000
#define SPCS_CHANNELNUM_UNSPEC 0x00000000
#define SPCS_CHANNELNUM_LEFT   0x00100000
#define SPCS_CHANNELNUM_RIGHT  0x00200000
#define SPCS_SOURCENUMMASK     0x000f0000
#define SPCS_SOURCENUM_UNSPEC  0x00000000
#define SPCS_GENERATIONSTATUS  0x00008000
#define SPCS_CATEGORYCODEMASK  0x00007f00
#define SPCS_MODEMASK          0x000000c0
#define SPCS_EMPHASISMASK      0x00000038
#define SPCS_EMPHASIS_NONE     0x00000000
#define SPCS_EMPHASIS_50_15    0x00000008
#define SPCS_COPYRIGHT         0x00000004
#define SPCS_NOTAUDIODATA      0x00000002
#define SPCS_PROFESSIONAL      0x00000001
#define SPDIF_SELECT           0x45

/* MIDI/UART registers */
#define MUDATA                0x47
#define MUCMD                 0x48
#define MUSTAT                MUCMD

/* IPR bits */
#define IPR_MIDITRANSBUFEMPTY  0x00000001
#define IPR_MIDIRECVBUFEMPTY   0x00000002
#define IPR_CH_0_LOOP          0x00000800
#define IPR_CH_0_HALF_LOOP     0x00000100
#define IPR_CAP_0_LOOP         0x00080000
#define IPR_CAP_0_HALF_LOOP    0x00010000

/* INTE bits */
#define INTE_MIDITXENABLE      0x00000001
#define INTE_MIDIRXENABLE      0x00000002
#define INTE_CH_0_LOOP         0x00000800
#define INTE_CH_0_HALF_LOOP    0x00000100
#define INTE_CAP_0_LOOP        0x00080000
#define INTE_CAP_0_HALF_LOOP   0x00010000

/* HCFG bits */
#define HCFG_AUDIOENABLE       0x00000001
#define HCFG_LOCKSOUNDCACHE    0x00000008

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
    uint32_t intr_status; /* IPR */
    uint32_t intr_mask;   /* INTE */

    /* Direct registers */
    uint32_t ptr_reg;
    uint32_t hcfg;
    uint32_t gpio;

    /* AC97 registers */
    uint8_t ac97_address;
    uint16_t ac97_regs[128];

    /* Indirect registers – per channel (0..2) */
    uint32_t indirect_regs[0x50][3];

    /* DMA context per voice */
    struct {
        bool running;
        QEMUTimer *timer;
        void *opaque;
    } voice[3];

    /* Capture DMA */
    struct {
        bool running;
        QEMUTimer *timer;
    } capture;

    /* MIDI state */
    struct {
        uint32_t mudata;
        uint32_t mucmd;
        uint32_t mustat;
        bool tx_enable;
        bool rx_enable;
    } midi;
};

/* Forward declarations */
static void pcibase_update_irq(PCIBaseState *s);

/* Timer callbacks */
static void play_voice_timer(void *opaque) {
    PCIBaseState *s = (PCIBaseState *)opaque;
    int v;
    uint32_t period_size, list_ptr, pointer, list_size, periods;
    uint64_t period_us;

    /* infer voice from timer index? We need to pass voice number */
    /* The opaque is the state; but we need to know which voice. */
    /* Instead, we'll create a small struct with state and index. */
    /* For simplicity, this is a stub; real implementation would use a separate struct */
    return;
}

/* Actually we need per-voice timers with voice index; define a wrapper */
struct voice_timer_opaque {
    PCIBaseState *s;
    int voice;
};

static void play_voice_timer_cb(void *opaque) {
    struct voice_timer_opaque *vo = opaque;
    PCIBaseState *s = vo->s;
    int v = vo->voice;

    if (!s->voice[v].running) return;

    uint32_t list_ptr = s->indirect_regs[PLAYBACK_LIST_PTR][v];
    uint32_t pointer = s->indirect_regs[PLAYBACK_POINTER][v];
    uint32_t period_size = s->indirect_regs[PLAYBACK_PERIOD_SIZE][v] >> 16;
    uint32_t list_size_raw = s->indirect_regs[PLAYBACK_LIST_SIZE][v];
    uint32_t periods = ((list_size_raw >> 19) & 0x1F) + 1;
    uint32_t buffer_size = period_size * periods;
    uint64_t period_us;

    pointer += period_size;
    if (pointer >= buffer_size) pointer = 0;

    list_ptr++;
    if (list_ptr >= periods) list_ptr = 0;

    s->indirect_regs[PLAYBACK_POINTER][v] = pointer;
    s->indirect_regs[PLAYBACK_LIST_PTR][v] = list_ptr << 3; /* driver expects *8 */

    /* Set both LOOP and HALF_LOOP IRQ bits for simplicity */
    uint32_t ipr_bits = (IPR_CH_0_LOOP | IPR_CH_0_HALF_LOOP) << v;
    s->intr_status |= ipr_bits;
    pcibase_update_irq(s);

    period_us = (period_size * 1000000ULL) / (48000 * 4);
    timer_mod(s->voice[v].timer, qemu_clock_get_us(QEMU_CLOCK_VIRTUAL) + period_us);
}

static void capture_timer_cb(void *opaque) {
    PCIBaseState *s = opaque;
    if (!s->capture.running) return;

    uint32_t pointer = s->indirect_regs[CAPTURE_POINTER][0];
    uint32_t buffer_size_raw = s->indirect_regs[CAPTURE_BUFFER_SIZE][0];
    uint32_t buffer_size = buffer_size_raw >> 16;
    uint32_t period_size = 0; /* capture period size? driver sets buffer size, no explicit period size for capture? */
    /* The driver prepares capture with a single buffer? In snd_emu10k1x_pcm_prepare_capture, it writes CAPTURE_BUFFER_SIZE << 16. */
    /* We'll assume one period per buffer for simplicity. */
    /* Simulate by wrapping pointer after buffer_size. */
    pointer += 64; /* arbitrary increment */
    if (pointer >= buffer_size) pointer = 0;
    s->indirect_regs[CAPTURE_POINTER][0] = pointer;

    s->intr_status |= (IPR_CAP_0_LOOP | IPR_CAP_0_HALF_LOOP);
    pcibase_update_irq(s);

    /* reschedule at a fixed rate */
    timer_mod(s->capture.timer, qemu_clock_get_us(QEMU_CLOCK_VIRTUAL) + 1000);
}

/* IRQ update logic */
static void pcibase_update_irq(PCIBaseState *s) {
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->intr_status & s->intr_mask) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* PIO read/write handlers */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size) {
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case PTR:
        if (size == 4) val = s->ptr_reg;
        break;
    case DATA:
        if (size == 4) {
            unsigned reg = (s->ptr_reg >> 16) & 0x7f;
            unsigned channel = s->ptr_reg & 0x3;
            if (reg < 0x50 && channel < 3)
                val = s->indirect_regs[reg][channel];
        }
        break;
    case IPR:
        if (size == 4) val = s->intr_status;
        break;
    case INTE:
        if (size == 4) val = s->intr_mask;
        break;
    case HCFG:
        if (size == 4) val = s->hcfg;
        break;
    case GPIO:
        if (size == 4) val = s->gpio;
        break;
    case AC97DATA:
        if (size == 2 || size == 4) {
            val = s->ac97_regs[s->ac97_address];
        }
        break;
    case AC97ADDRESS:
        if (size == 1 || size == 2 || size == 4) {
            val = s->ac97_address;
        }
        break;
    default:
        break;
    }
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size) {
    PCIBaseState *s = opaque;
    unsigned reg, channel;

    switch (addr) {
    case PTR:
        if (size == 4) s->ptr_reg = val;
        break;
    case DATA:
        if (size == 4) {
            reg = (s->ptr_reg >> 16) & 0x7f;
            channel = s->ptr_reg & 0x3;
            if (reg < 0x50 && channel < 3) {
                s->indirect_regs[reg][channel] = val;

                /* Special handling for TRIGGER_CHANNEL */
                if (reg == TRIGGER_CHANNEL && channel == 0) {
                    uint32_t trigger = val;
                    int i;
                    for (i = 0; i < 3; i++) {
                        if (trigger & (TRIGGER_CHANNEL_0 << i)) {
                            if (!s->voice[i].running) {
                                s->voice[i].running = true;
                                timer_mod(s->voice[i].timer, qemu_clock_get_us(QEMU_CLOCK_VIRTUAL) + 1000);
                            }
                        } else {
                            if (s->voice[i].running) {
                                s->voice[i].running = false;
                                timer_del(s->voice[i].timer);
                            }
                        }
                    }
                    if (trigger & TRIGGER_CAPTURE) {
                        if (!s->capture.running) {
                            s->capture.running = true;
                            timer_mod(s->capture.timer, qemu_clock_get_us(QEMU_CLOCK_VIRTUAL) + 1000);
                        }
                    } else {
                        if (s->capture.running) {
                            s->capture.running = false;
                            timer_del(s->capture.timer);
                        }
                    }
                }
            }
        }
        break;
    case IPR:
        if (size == 4) {
            /* W1C: clear bits that are set in val */
            s->intr_status &= ~val;
            pcibase_update_irq(s);
        }
        break;
    case INTE:
        if (size == 4) {
            s->intr_mask = val;
            pcibase_update_irq(s);
        }
        break;
    case HCFG:
        if (size == 4) s->hcfg = val;
        break;
    case GPIO:
        if (size == 4) s->gpio = val;
        break;
    case AC97DATA:
        if (size == 2 || size == 4) {
            s->ac97_regs[s->ac97_address] = (uint16_t)(val & 0xffff);
        }
        break;
    case AC97ADDRESS:
        if (size == 1 || size == 2 || size == 4) {
            s->ac97_address = (uint8_t)(val & 0xff);
        }
        break;
    default:
        break;
    }
}

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* Dummy MMIO handlers (not used) */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size) { return 0; }
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size) {}
static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static void pcibase_reset(DeviceState *dev) {
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    s->ptr_reg = 0;
    s->hcfg = 0;
    s->gpio = 0;
    s->ac97_address = 0;
    memset(s->ac97_regs, 0, sizeof(s->ac97_regs));
    memset(s->indirect_regs, 0, sizeof(s->indirect_regs));
    s->intr_status = 0;
    s->intr_mask = 0;

    int i;
    for (i = 0; i < 3; i++) {
        s->voice[i].running = false;
        timer_del(s->voice[i].timer);
    }
    s->capture.running = false;
    timer_del(s->capture.timer);

    /* Set initial SPCS defaults as driver expects */
    s->indirect_regs[SPCS0][0] = SPCS_CLKACCY_1000PPM | SPCS_SAMPLERATE_48 |
                                  SPCS_CHANNELNUM_LEFT | SPCS_SOURCENUM_UNSPEC |
                                  SPCS_GENERATIONSTATUS | 0x00001200 |
                                  SPCS_EMPHASIS_NONE | SPCS_COPYRIGHT;
    s->indirect_regs[SPCS1][0] = s->indirect_regs[SPCS0][0];
    s->indirect_regs[SPCS2][0] = s->indirect_regs[SPCS0][0];

    /* Set AC'97 reset values to a valid codec ID */
    s->ac97_regs[0] = 0x8000;
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp) {
    if (!bi || bi->type == BAR_TYPE_NONE) return;
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

static void pcibase_realize(PCIDevice *pdev, Error **errp) {
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;
    int i;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID );
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
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = BAR0_SIZE;
    s->bar_info[0].name = "emu10k1x-io";

    for (i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize timers for voices and capture */
    for (i = 0; i < 3; i++) {
        struct voice_timer_opaque *vo = g_new0(struct voice_timer_opaque, 1);
        vo->s = s;
        vo->voice = i;
        s->voice[i].timer = timer_new_us(QEMU_CLOCK_VIRTUAL, play_voice_timer_cb, vo);
        s->voice[i].opaque = vo;
        s->voice[i].running = false;
    }
    s->capture.timer = timer_new_us(QEMU_CLOCK_VIRTUAL, capture_timer_cb, s);
    s->capture.running = false;
}

static void pcibase_uninit(PCIDevice *pdev) {
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    int i;
    for (i = 0; i < 3; i++) {
        timer_del(s->voice[i].timer);
        timer_free(s->voice[i].timer);
        /* Free the opaque struct */
        g_free(s->voice[i].opaque);
    }
    timer_del(s->capture.timer);
    timer_free(s->capture.timer);
    if (msix_enabled(pdev)) msix_uninit(pdev, NULL, NULL);
    if (msi_enabled(pdev)) msi_uninit(pdev);
}

static const VMStateDescription vmstate_pcibase = {
    .name = "snd_emu10k1x_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_END_OF_LIST()
    }
};

static void pcibase_class_init(ObjectClass *klass, void *data) {
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    k->realize = pcibase_realize;
    k->exit    = pcibase_uninit;
    dc->reset  = pcibase_reset;
    dc->vmsd   = &vmstate_pcibase;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void pcibase_register_types(void) {
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
