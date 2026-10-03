/*
 * QEMU PCI device model for ForteMedia FM801 audio controller
 * Auto-generated for Linux driver sound/pci/fm801.c behavioral emulation
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

/* Additional include files retrieved from driver context */
#include "qemu/typedefs.h"

#define TYPE_PCIBASE_DEVICE "snd_fm801_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define FM801_VENDOR_ID 0x1319
#define FM801_DEVICE_ID 0x0801
#define FM801_CLASS_ID  PCI_CLASS_MULTIMEDIA_AUDIO

#define FM801_PCM_VOL        0x00
#define FM801_FM_VOL         0x02
#define FM801_I2S_VOL        0x04
#define FM801_REC_SRC        0x06
#define FM801_PLY_CTRL       0x08
#define FM801_PLY_COUNT      0x0a
#define FM801_PLY_BUF1       0x0c
#define FM801_PLY_BUF2       0x10
#define FM801_CAP_CTRL       0x14
#define FM801_CAP_COUNT      0x16
#define FM801_CAP_BUF1       0x18
#define FM801_CAP_BUF2       0x1c
#define FM801_CODEC_CTRL     0x22
#define FM801_I2S_MODE       0x24
#define FM801_VOLUME         0x26
#define FM801_I2C_CTRL       0x29
#define FM801_AC97_CMD       0x2a
#define FM801_AC97_DATA      0x2c
#define FM801_MPU401_DATA    0x30
#define FM801_MPU401_CMD     0x31
#define FM801_GPIO_CTRL      0x52
#define FM801_GEN_CTRL       0x54
#define FM801_IRQ_MASK       0x56
#define FM801_IRQ_STATUS     0x5a
#define FM801_OPL3_BANK0     0x68
#define FM801_OPL3_DATA0     0x69
#define FM801_OPL3_BANK1     0x6a
#define FM801_OPL3_DATA1     0x6b
#define FM801_POWERDOWN      0x70

#define FM801_AC97_READ      (1<<7)
#define FM801_AC97_VALID     (1<<8)
#define FM801_AC97_BUSY      (1<<9)
#define FM801_AC97_ADDR_SHIFT 10

#define FM801_BUF1_LAST      (1<<1)
#define FM801_BUF2_LAST      (1<<2)
#define FM801_START          (1<<5)
#define FM801_PAUSE          (1<<6)
#define FM801_IMMED_STOP     (1<<7)
#define FM801_RATE_SHIFT     8
#define FM801_RATE_MASK      (15 << FM801_RATE_SHIFT)
#define FM801_CHANNELS_4     (1<<12)
#define FM801_CHANNELS_6     (2<<12)
#define FM801_CHANNELS_6MS   (3<<12)
#define FM801_CHANNELS_MASK  (3<<12)
#define FM801_16BIT          (1<<14)
#define FM801_STEREO         (1<<15)

#define FM801_IRQ_PLAYBACK   (1<<8)
#define FM801_IRQ_CAPTURE    (1<<9)
#define FM801_IRQ_VOLUME     (1<<14)
#define FM801_IRQ_MPU        (1<<15)

#define FM801_GPIO_GP0       (1<<0)
#define FM801_GPIO_GP1       (1<<1)
#define FM801_GPIO_GP2       (1<<2)
#define FM801_GPIO_GP3       (1<<3)
#define FM801_GPIO_GP(x)     (1<<(0+(x)))
#define FM801_GPIO_GD0       (1<<8)
#define FM801_GPIO_GD1       (1<<9)
#define FM801_GPIO_GD2       (1<<10)
#define FM801_GPIO_GD3       (1<<11)
#define FM801_GPIO_GD(x)     (1<<(8+(x)))
#define FM801_GPIO_GS0       (1<<12)
#define FM801_GPIO_GS1       (1<<13)
#define FM801_GPIO_GS2       (1<<14)
#define FM801_GPIO_GS3       (1<<15)
#define FM801_GPIO_GS(x)     (1<<(12+(x)))

#define FM801_NUM_REGS_SHADOW 0x20


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

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;
    uint16_t intr_status;
    uint16_t intr_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint16_t regs[FM801_NUM_REGS_SHADOW];

    /* DMA Context */
    QEMUTimer playback_timer;
    QEMUTimer capture_timer;
    dma_addr_t playback_buf1;
    dma_addr_t playback_buf2;
    dma_addr_t capture_buf1;
    dma_addr_t capture_buf2;
};


/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    uint16_t pending = s->intr_status & ~s->intr_mask;
    if (pending) {
        if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!msi_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

/* Simple helper to map byte offset to 16-bit register index */
static inline int fm801_reg_index(hwaddr addr)
{
    return (addr & 0x7e) >> 1;
}

static void fm801_playback_timer_cb(void *opaque)
{
    PCIBaseState *s = opaque;

    /* If playback not started, ignore */
    if (!(s->regs[fm801_reg_index(FM801_PLY_CTRL)] & FM801_START)) {
        return;
    }

    /* Simulate a playback period completion: set IRQ bit */
    s->intr_status |= FM801_IRQ_PLAYBACK;
    s->regs[fm801_reg_index(FM801_IRQ_STATUS)] = s->intr_status;
    pcibase_update_irq(s);

    /* Reschedule to keep generating periods while running */
    timer_mod(&s->playback_timer,
              qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 10);
}

static void fm801_capture_timer_cb(void *opaque)
{
    PCIBaseState *s = opaque;

    if (!(s->regs[fm801_reg_index(FM801_CAP_CTRL)] & FM801_START)) {
        return;
    }

    s->intr_status |= FM801_IRQ_CAPTURE;
    s->regs[fm801_reg_index(FM801_IRQ_STATUS)] = s->intr_status;
    pcibase_update_irq(s);

    timer_mod(&s->capture_timer,
              qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 10);
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /*
     * The Linux driver programs buffer base addresses into PLY_BUF1/2 and
     * CAP_BUF1/2 and expects the device to move audio data and generate
     * interrupts. The driver does not inspect DMA data contents, only
     * positions and interrupts. To avoid unsupported inference about the
     * audio format, we keep DMA logic minimal here and instead rely on
     * timers and IRQ bits. No pci_dma_read/write is required for probe
     * and basic operation.
     */
    (void)s;
    (void)is_write;
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* This device uses I/O space BAR0; MMIO is unused */
    (void)s;
    (void)addr;
    (void)size;
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    (void)s;
    (void)addr;
    (void)val;
    (void)size;
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Only 8- and 16-bit port accesses are used by driver, but allow others safely */
    if (size == 1) {
        /* There are no 8-bit only registers we must emulate; return 0 */
        val = 0;
    } else if (size == 2) {
        int reg = fm801_reg_index(addr);
        if (reg >= 0 && reg < FM801_NUM_REGS_SHADOW) {
            val = s->regs[reg];
        } else {
            val = 0;
        }
    } else if (size == 4) {
        /* 32-bit read composed of two 16-bit registers */
        int reg = fm801_reg_index(addr);
        if (reg >= 0 && reg + 1 < FM801_NUM_REGS_SHADOW) {
            uint32_t lo = s->regs[reg];
            uint32_t hi = s->regs[reg + 1];
            val = lo | (hi << 16);
        } else {
            val = 0;
        }
    } else {
        val = 0;
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size == 1) {
        /* Only a few 8-bit fields exist (e.g. I2C_CTRL), but driver uses 16-bit helpers. */
        return;
    } else if (size == 2) {
        int reg = fm801_reg_index(addr);
        uint16_t wval = val & 0xffff;

        if (reg < 0 || reg >= FM801_NUM_REGS_SHADOW) {
            return;
        }

        /* Handle IRQ status (W1C) */
        if ((addr & 0x7e) == FM801_IRQ_STATUS) {
            /* Writing '1' bits clears corresponding interrupt status bits */
            s->intr_status &= ~wval;
            s->regs[reg] = s->intr_status;
            pcibase_update_irq(s);
            return;
        }

        /* Normal register write */
        s->regs[reg] = wval;

        /* Special side effects for some control registers */
        if ((addr & 0x7e) == FM801_IRQ_MASK) {
            /* Driver treats this as mask bits; 1 = masked */
            s->intr_mask = wval;
            pcibase_update_irq(s);
        } else if ((addr & 0x7e) == FM801_PLY_CTRL) {
            /* Start/stop playback timer according to START bit */
            if (wval & FM801_START) {
                timer_mod(&s->playback_timer,
                          qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 10);
            } else {
                timer_del(&s->playback_timer);
            }
        } else if ((addr & 0x7e) == FM801_CAP_CTRL) {
            if (wval & FM801_START) {
                timer_mod(&s->capture_timer,
                          qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 10);
            } else {
                timer_del(&s->capture_timer);
            }
        }
    } else if (size == 4) {
        int reg = fm801_reg_index(addr);

        if (reg < 0 || reg + 1 >= FM801_NUM_REGS_SHADOW) {
            return;
        }

        uint16_t lo = val & 0xffff;
        uint16_t hi = (val >> 16) & 0xffff;

        /* Some 32-bit writes are for buffer base addresses */
        s->regs[reg] = lo;
        s->regs[reg + 1] = hi;

        if ((addr & 0x7e) == FM801_PLY_BUF1) {
            s->playback_buf1 = (dma_addr_t)val;
        } else if ((addr & 0x7e) == FM801_PLY_BUF2) {
            s->playback_buf2 = (dma_addr_t)val;
        } else if ((addr & 0x7e) == FM801_CAP_BUF1) {
            s->capture_buf1 = (dma_addr_t)val;
        } else if ((addr & 0x7e) == FM801_CAP_BUF2) {
            s->capture_buf2 = (dma_addr_t)val;
        }
    } else {
        /* Other sizes not used */
        return;
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
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Revert registers to power-on defaults visible to the driver */
    memset(s->regs, 0, sizeof(s->regs));
    s->intr_status = 0;
    s->intr_mask = 0xffff; /* mask all IRQs until configured */
    s->regs[fm801_reg_index(FM801_IRQ_MASK)] = s->intr_mask;

    /* Default mixer / I2S settings as done in snd_fm801_chip_init() */
    s->regs[fm801_reg_index(FM801_PCM_VOL)] = 0x0808;
    s->regs[fm801_reg_index(FM801_FM_VOL)] = 0x9f1f;
    s->regs[fm801_reg_index(FM801_I2S_VOL)] = 0x8808;
    s->regs[fm801_reg_index(FM801_I2S_MODE)] = 0x0003;

    /* Cancel timers */
    timer_del(&s->playback_timer);
    timer_del(&s->capture_timer);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  FM801_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  FM801_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, FM801_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0xb1); /* FM801-AU (multichannel capable) */
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* Initialize timers for fake period IRQs */
    timer_init_ms(&s->playback_timer, QEMU_CLOCK_VIRTUAL, fm801_playback_timer_cb, s);
    timer_init_ms(&s->capture_timer, QEMU_CLOCK_VIRTUAL, fm801_capture_timer_cb, s);

    /* BAR Initialization: driver uses I/O port BAR 0 with 0x80 bytes */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 0x80;
    s->bar_info[0].name = "fm801-io";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = "unused";
    }
      
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize default register state */
    pcibase_reset(DEVICE(pdev));
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

    /* Free buffers, stop timers, etc. */
    timer_del(&s->playback_timer);
    timer_del(&s->capture_timer);
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "snd_fm801_pci",
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
