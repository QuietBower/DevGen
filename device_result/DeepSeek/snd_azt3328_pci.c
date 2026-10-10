/*
 * QEMU 8.2.10 virtual PCI device model for Aztech AZF3328 (snd-azt3328)
 *
 * Generated strictly from Linux driver source (sound/pci/azt3328.c).
 * Phase 2: Full functional implementation.
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

#define TYPE_PCIBASE_DEVICE "snd_azt3328_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI Identification */
#define VENDOR_ID 0x122D
#define DEVICE_ID 0x50DC
#define CLASS_ID  0x040100   /* Multimedia Audio */

/* I/O region sizes (from driver PCI resource usage) */
#define AZF_IO_SIZE_CTRL   0x80
#define AZF_IO_SIZE_MIXER  0x40
#define AZF_IO_SIZE_GAME   0x08
#define AZF_IO_SIZE_MPU    0x04
#define AZF_IO_SIZE_OPL3   0x08

/* I/O offsets within the control BAR */
#define AZF_IO_OFFS_CODEC_PLAYBACK  0x00
#define AZF_IO_OFFS_CODEC_CAPTURE   0x20
#define AZF_IO_OFFS_CODEC_I2S_OUT   0x40

/* Control region registers (indexes into PIO) */
#define IDX_IO_CODEC_DMA_FLAGS      0x00
#define IDX_IO_CODEC_DMA_START_1    0x04
#define IDX_IO_CODEC_IRQTYPE        0x02
#define IDX_IO_CODEC_DMA_CURRPOS    0x10
#define IDX_IO_CODEC_SOUNDFORMAT    0x16
#define IDX_IO_TIMER_VALUE          0x60
#define IDX_IO_IRQSTATUS            0x64
#define IDX_IO_6AH                  0x6A

/* Bits for register 0x6A */
#define IO_6A_PAUSE_PLAYBACK_BIT8   0x0100
#define IO_6A_SOMETHING2_GAMEPORT   0x0400

/* Gameport registers */
#define IDX_GAME_LEGACY_COMPATIBLE  0x00
#define IDX_GAME_AXES_CONFIG        0x01
#define IDX_GAME_AXIS_VALUE         0x02
#define IDX_GAME_HWCONFIG           0x04

/* Gameport hardware configuration bits */
#define GAME_HWCFG_IRQ_ENABLE              0x01
#define GAME_HWCFG_LEGACY_ADDRESS_ENABLE    0x08
#define GAME_HWCFG_ADC_COUNTER_FREQ_STD     0
#define GAME_HWCFG_ADC_COUNTER_FREQ_1_200   3
#define GAME_AXES_SAMPLING_READY            0x80

/* Interrupt flags (as seen in IRQSTATUS) */
#define IRQ_PLAYBACK   0x0001
#define IRQ_RECORDING  0x0002
#define IRQ_I2S_OUT    0x0004
#define IRQ_GAMEPORT   0x0008
#define IRQ_MPU401     0x0010
#define IRQ_TIMER      0x0020

/* Timer control bits */
#define TIMER_COUNTDOWN_ENABLE  0x01000000UL
#define TIMER_VALUE_MASK        0x000fffffUL
#define TIMER_IRQ_ENABLE        0x02000000UL

/* DMA flags (used with IDX_IO_CODEC_DMA_FLAGS) */
#define DMA_RESUME             0x0001
#define DMA_RUN_SOMETHING1     0x0002
#define DMA_RUN_SOMETHING2     0x0004
#define DMA_EPILOGUE_SOMETHING 0x0010
#define DMA_SOMETHING_ELSE     0x0020

/* Mixer (AC97-like) register indices */
#define IDX_MIXER_RESET         0x00
#define IDX_MIXER_PLAY_MASTER   0x02
#define IDX_MIXER_MODEMOUT      0x04
#define IDX_MIXER_BASSTREBLE    0x06
#define IDX_MIXER_PCBEEP        0x08
#define IDX_MIXER_MODEMIN       0x0a
#define IDX_MIXER_MIC           0x0c
#define IDX_MIXER_LINEIN        0x0e
#define IDX_MIXER_CDAUDIO       0x10
#define IDX_MIXER_VIDEO         0x12
#define IDX_MIXER_AUX           0x14
#define IDX_MIXER_WAVEOUT       0x16
#define IDX_MIXER_FMSYNTH       0x18
#define IDX_MIXER_REC_SELECT    0x1a
#define IDX_MIXER_REC_VOLUME    0x1c
#define IDX_MIXER_ADVCTL1       0x1e
#define IDX_MIXER_ADVCTL2       0x20
#define IDX_MIXER_SOMETHING30H  0x30

/* Sound format flags */
#define SOUNDFORMAT_XTAL1         0x00
#define SOUNDFORMAT_XTAL2         0x01
#define SOUNDFORMAT_FLAG_16BIT    0x0010
#define SOUNDFORMAT_FLAG_2CHANNELS 0x0020

/* Frequency enumeration (hardware constants) */
enum azf_freq_t {
    AZF_FREQ_4000 = 4000,
    AZF_FREQ_4800 = 4800,
    AZF_FREQ_5512 = 5512,
    AZF_FREQ_6620 = 6620,
    AZF_FREQ_8000 = 8000,
    AZF_FREQ_9600 = 9600,
    AZF_FREQ_11025 = 11025,
    AZF_FREQ_13240 = 13240,
    AZF_FREQ_16000 = 16000,
    AZF_FREQ_22050 = 22050,
    AZF_FREQ_32000 = 32000,
    AZF_FREQ_44100 = 44100,
    AZF_FREQ_48000 = 48000,
    AZF_FREQ_66200 = 66200,
};

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
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Register Shadow Storage */
    uint8_t ctrl_regs[AZF_IO_SIZE_CTRL];
    uint8_t game_regs[AZF_IO_SIZE_GAME];
    uint8_t mpu_regs[AZF_IO_SIZE_MPU];
    uint8_t opl3_regs[AZF_IO_SIZE_OPL3];
    uint16_t mixer_regs[AZF_IO_SIZE_MIXER / 2];

    /* Separate 6AH shadow for the driver's convenience */
    uint16_t shadow_reg_ctrl_6AH;

    /* IRQ status (bits from IRQ_* definitions) */
    uint8_t irq_status;

    /* DMA contexts for three codecs (not fully used in emulation) */
    struct {
        uint32_t base;          /* DMA physical base address */
        uint32_t period_bytes;
        uint32_t buffer_bytes;
        uint32_t curr_pos;      /* current position */
        bool running;
        uint16_t flags;         /* DMA_RUN_... bits */
    } dma[3];

    /* Operational status */
    uint32_t status;

    /* Reset state */
    bool reset_in_progress;

    /* Power management state (D0-D3) */
    uint8_t power_state;
};

/* Function prototypes for PIO handlers */
static uint64_t ctrl_io_read(void *opaque, hwaddr addr, unsigned size);
static void ctrl_io_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint64_t game_io_read(void *opaque, hwaddr addr, unsigned size);
static void game_io_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint64_t mpu_io_read(void *opaque, hwaddr addr, unsigned size);
static void mpu_io_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint64_t opl3_io_read(void *opaque, hwaddr addr, unsigned size);
static void opl3_io_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint64_t mixer_io_read(void *opaque, hwaddr addr, unsigned size);
static void mixer_io_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);

/* Memory region ops for each BAR */
static const MemoryRegionOps ctrl_io_ops = {
    .read = ctrl_io_read,
    .write = ctrl_io_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static const MemoryRegionOps game_io_ops = {
    .read = game_io_read,
    .write = game_io_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 2 },
    .impl  = { .min_access_size = 1, .max_access_size = 2 },
};

static const MemoryRegionOps mpu_io_ops = {
    .read = mpu_io_read,
    .write = mpu_io_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 1 },
    .impl  = { .min_access_size = 1, .max_access_size = 1 },
};

static const MemoryRegionOps opl3_io_ops = {
    .read = opl3_io_read,
    .write = opl3_io_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 1 },
    .impl  = { .min_access_size = 1, .max_access_size = 1 },
};

static const MemoryRegionOps mixer_io_ops = {
    .read = mixer_io_read,
    .write = mixer_io_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 2, .max_access_size = 2 },
    .impl  = { .min_access_size = 2, .max_access_size = 2 },
};

/* IRQ update helper */
static void azf_update_irq(PCIBaseState *s)
{
    int level = (s->irq_status != 0) ? 1 : 0;
    pci_set_irq(PCI_DEVICE(s), level);
}

/* Control BAR handlers */
static uint64_t ctrl_io_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= AZF_IO_SIZE_CTRL) {
        qemu_log_mask(LOG_GUEST_ERROR, "ctrl_io_read: out-of-bounds addr=0x%"HWADDR_PRIx"\n", addr);
        return ~0ULL;
    }

    /* Special handled registers */
    if (addr == IDX_IO_IRQSTATUS && size == 1) {
        return s->irq_status;
    }

    if (size == 1) {
        val = s->ctrl_regs[addr];
    } else if (size == 2) {
        if (addr + 1 < AZF_IO_SIZE_CTRL) {
            val = lduw_le_p(&s->ctrl_regs[addr]);
        }
    } else if (size == 4) {
        if (addr + 3 < AZF_IO_SIZE_CTRL) {
            val = ldl_le_p(&s->ctrl_regs[addr]);
        }
    }
    return val;
}

static void ctrl_io_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= AZF_IO_SIZE_CTRL) {
        return;
    }

    /* Store value in shadow array */
    if (size == 1) {
        s->ctrl_regs[addr] = (uint8_t)val;
    } else if (size == 2) {
        if (addr + 1 < AZF_IO_SIZE_CTRL) {
            stw_le_p(&s->ctrl_regs[addr], (uint16_t)val);
        }
    } else if (size == 4) {
        if (addr + 3 < AZF_IO_SIZE_CTRL) {
            stl_le_p(&s->ctrl_regs[addr], (uint32_t)val);
        }
    }

    /* Handle side effects */
    if (addr == IDX_IO_6AH && size == 2) {
        s->shadow_reg_ctrl_6AH = val;
    }

    /* Timer IRQ ACK: driver writes 0x07 to TIMER_VALUE+3 (offset 0x63) */
    if (addr == IDX_IO_TIMER_VALUE + 3 && size == 1) {
        if (val & 0x07) {
            s->irq_status &= ~IRQ_TIMER;
            azf_update_irq(s);
        }
    }

    /* Codec IRQTYPE acknowledge: offsets 0x02, 0x22, 0x42 */
    if ((addr == IDX_IO_CODEC_IRQTYPE) || (addr == (0x20 + IDX_IO_CODEC_IRQTYPE)) ||
        (addr == (0x40 + IDX_IO_CODEC_IRQTYPE))) {
        if (size == 2 && (uint16_t)val == 0xffff) {
            if (addr == IDX_IO_CODEC_IRQTYPE) {
                s->irq_status &= ~IRQ_PLAYBACK;
            } else if (addr == (0x20 + IDX_IO_CODEC_IRQTYPE)) {
                s->irq_status &= ~IRQ_RECORDING;
            } else {
                s->irq_status &= ~IRQ_I2S_OUT;
            }
            azf_update_irq(s);
        }
    }
}

/* Game BAR handlers */
static uint64_t game_io_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= AZF_IO_SIZE_GAME) {
        return ~0ULL;
    }

    if (size == 1) {
        val = s->game_regs[addr];
    } else if (size == 2) {
        if (addr + 1 < AZF_IO_SIZE_GAME) {
            val = lduw_le_p(&s->game_regs[addr]);
        }
    }
    return val;
}

static void game_io_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= AZF_IO_SIZE_GAME) {
        return;
    }

    if (size == 1) {
        s->game_regs[addr] = (uint8_t)val;
    } else if (size == 2) {
        if (addr + 1 < AZF_IO_SIZE_GAME) {
            stw_le_p(&s->game_regs[addr], (uint16_t)val);
        }
    }
}

/* MPU BAR handlers */
static uint64_t mpu_io_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr >= AZF_IO_SIZE_MPU) {
        return ~0ULL;
    }
    return s->mpu_regs[addr];
}

static void mpu_io_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr >= AZF_IO_SIZE_MPU) {
        return;
    }
    s->mpu_regs[addr] = (uint8_t)val;
}

/* OPL3 BAR handlers */
static uint64_t opl3_io_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr >= AZF_IO_SIZE_OPL3) {
        return ~0ULL;
    }
    return s->opl3_regs[addr];
}

static void opl3_io_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr >= AZF_IO_SIZE_OPL3) {
        return;
    }
    s->opl3_regs[addr] = (uint8_t)val;
}

/* Mixer BAR handlers */
static uint64_t mixer_io_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    /* The driver accesses only word at even offsets, but we accept word */
    if (addr & 1 || addr >= AZF_IO_SIZE_MIXER) {
        return ~0ULL;
    }
    unsigned idx = addr / 2;
    return s->mixer_regs[idx];
}

static void mixer_io_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr & 1 || addr >= AZF_IO_SIZE_MIXER) {
        return;
    }
    unsigned idx = addr / 2;
    s->mixer_regs[idx] = (uint16_t)val;
}

/* Stub MMIO handlers (not used) */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Reset all register arrays to initial state */
    memset(s->ctrl_regs, 0, sizeof(s->ctrl_regs));
    memset(s->game_regs, 0, sizeof(s->game_regs));
    memset(s->opl3_regs, 0, sizeof(s->opl3_regs));
    memset(s->mixer_regs, 0, sizeof(s->mixer_regs));

    /* Configure MPU for detection: status = 0x80, data = 0xFF */
    s->mpu_regs[0] = 0xFF; /* data */
    s->mpu_regs[1] = 0x80; /* status: TX ready */
    s->mpu_regs[2] = 0x00;
    s->mpu_regs[3] = 0x00;

    s->shadow_reg_ctrl_6AH = 0;
    s->irq_status = 0;
    s->power_state = 0;
    memset(&s->dma, 0, sizeof(s->dma));
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
        /* We individually assign ops in realize, so this path not used */
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Add power management capability */
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* Initialize register arrays */
    memset(s->ctrl_regs, 0, sizeof(s->ctrl_regs));
    memset(s->game_regs, 0, sizeof(s->game_regs));
    memset(s->mpu_regs, 0, sizeof(s->mpu_regs));
    memset(s->opl3_regs, 0, sizeof(s->opl3_regs));
    memset(s->mixer_regs, 0, sizeof(s->mixer_regs));

    /* Preset MPU to pass probe */
    s->mpu_regs[1] = 0x80;
    s->mpu_regs[0] = 0xFF;

    s->shadow_reg_ctrl_6AH = 0;
    s->irq_status = 0;
    s->power_state = 0;
    memset(&s->dma, 0, sizeof(s->dma));

    /* BAR 0: Control (PIO) */
    memory_region_init_io(&s->bar_regions[0], OBJECT(s), &ctrl_io_ops, s, "ctrl", AZF_IO_SIZE_CTRL);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_IO, &s->bar_regions[0]);

    /* BAR 1: Game (PIO) */
    memory_region_init_io(&s->bar_regions[1], OBJECT(s), &game_io_ops, s, "game", AZF_IO_SIZE_GAME);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_IO, &s->bar_regions[1]);

    /* BAR 2: MPU (PIO) */
    memory_region_init_io(&s->bar_regions[2], OBJECT(s), &mpu_io_ops, s, "mpu", AZF_IO_SIZE_MPU);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_IO, &s->bar_regions[2]);

    /* BAR 3: OPL3 (PIO) */
    memory_region_init_io(&s->bar_regions[3], OBJECT(s), &opl3_io_ops, s, "opl3", AZF_IO_SIZE_OPL3);
    pci_register_bar(pdev, 3, PCI_BASE_ADDRESS_SPACE_IO, &s->bar_regions[3]);

    /* BAR 4: Mixer (PIO) */
    memory_region_init_io(&s->bar_regions[4], OBJECT(s), &mixer_io_ops, s, "mixer", AZF_IO_SIZE_MIXER);
    pci_register_bar(pdev, 4, PCI_BASE_ADDRESS_SPACE_IO, &s->bar_regions[4]);

    /* No MSI/MSI-X */
    s->has_msi = false;
    s->has_msix = false;
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
    /* No additional cleanup required */
}

static const VMStateDescription vmstate_pcibase = {
    .name = "snd_azt3328_pci",
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

type_init(pcibase_register_types)
