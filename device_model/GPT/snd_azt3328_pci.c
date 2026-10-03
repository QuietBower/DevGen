/*
 * QEMU PCI device model for Aztech AZF3328 (minimal behavior for driver probe)
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

/* NOTE: The Linux headers are not actually available inside QEMU build.
 * We keep this include from the template but do not rely on it.
 */
#include <linux/pci.h>

#define TYPE_PCIBASE_DEVICE "snd_azt3328_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCIBASE_VENDOR_ID 0x122d
#define PCIBASE_DEVICE_ID 0x50dc
#define PCIBASE_CLASS_ID  PCI_CLASS_MULTIMEDIA_AUDIO

#define AZF_MUTE_BIT 0x80
#define AZF_REG_MASK 0x3f
#define AZF_AC97_REG_UNSUPPORTED 0x8000
#define AZF_AC97_REG_REAL_IO_READ 0x4000
#define AZF_AC97_REG_REAL_IO_WRITE 0x2000
#define AZF_AC97_REG_REAL_IO_RW \
    (AZF_AC97_REG_REAL_IO_READ | AZF_AC97_REG_REAL_IO_WRITE)
#define AZF_AC97_REG_EMU_IO_READ 0x0400
#define AZF_AC97_REG_EMU_IO_WRITE 0x0200
#define AZF_AC97_REG_EMU_IO_RW \
    (AZF_AC97_REG_EMU_IO_READ | AZF_AC97_REG_EMU_IO_WRITE)

#define IDX_MIXER_PLAY_MASTER   0x02
#define IDX_MIXER_WAVEOUT       0x16
#define IDX_MIXER_RESET         0x00
#define IDX_MIXER_FMSYNTH       0x18
#define IDX_MIXER_VIDEO         0x12
#define IDX_MIXER_ADVCTL1       0x1e
#define IDX_MIXER_BASSTREBLE    0x06
#define IDX_MIXER_PCBEEP        0x08
#define IDX_MIXER_MODEMOUT      0x04
#define IDX_MIXER_ADVCTL2       0x20
#define IDX_MIXER_MIC           0x0c
#define IDX_MIXER_REC_SELECT    0x1a
#define IDX_MIXER_LINEIN        0x0e
#define IDX_MIXER_CDAUDIO       0x10
#define IDX_MIXER_MODEMIN       0x0a
#define IDX_MIXER_AUX           0x14
#define IDX_MIXER_REC_VOLUME    0x1c
#define IDX_MIXER_SOMETHING30H  0x30

#define SOUNDFORMAT_XTAL2       0x01
#define SOUNDFORMAT_XTAL1       0x00

#define SOUNDFORMAT_FREQ_22050          (0x02 | SOUNDFORMAT_XTAL2)
#define IDX_IO_CODEC_DMA_FLAGS          0x00
#define SOUNDFORMAT_FREQ_SUSPECTED_66200        (0x06 | SOUNDFORMAT_XTAL2)
#define DMA_RUN_SOMETHING1              0x0002
#define SOUNDFORMAT_FLAG_2CHANNELS      0x0020
#define SOUNDFORMAT_FREQ_SUSPECTED_4800 (0x0a | SOUNDFORMAT_XTAL1)
#define SOUNDFORMAT_FREQ_11025          (0x00 | SOUNDFORMAT_XTAL2)
#define SOUNDFORMAT_FREQ_SUSPECTED_13240        (0x08 | SOUNDFORMAT_XTAL2)
#define DMA_RUN_SOMETHING2              0x0004
#define SOMETHING_ALMOST_ALWAYS_SET     0x0008
#define SOUNDFORMAT_FLAG_16BIT  0x0010
#define DMA_SOMETHING_ELSE      0x0020
#define SOUNDFORMAT_FREQ_8000           (0x00 | SOUNDFORMAT_XTAL1)
#define SOUNDFORMAT_FREQ_5510           (0x0c | SOUNDFORMAT_XTAL2)
#define SOUNDFORMAT_FREQ_48000          (0x06 | SOUNDFORMAT_XTAL1)
#define SOUNDFORMAT_FREQ_16000          (0x02 | SOUNDFORMAT_XTAL1)
#define SOUNDFORMAT_FREQ_6620           (0x0a | SOUNDFORMAT_XTAL2)
#define SOUNDFORMAT_FREQ_9600           (0x08 | SOUNDFORMAT_XTAL1)
#define SOUNDFORMAT_FREQ_SUSPECTED_4000 (0x0c | SOUNDFORMAT_XTAL1)
#define SOUNDFORMAT_FREQ_44100          (0x04 | SOUNDFORMAT_XTAL2)
#define DMA_EPILOGUE_SOMETHING  0x0010
#define SOUNDFORMAT_FREQ_32000          (0x04 | SOUNDFORMAT_XTAL1)
#define IDX_IO_CODEC_SOUNDFORMAT 0x16

#define IDX_IO_6AH              0x6a
#define IO_6A_PAUSE_PLAYBACK_BIT8       0x0100
#define IDX_IO_CODEC_DMA_START_1 0x04
#define DMA_RESUME              0x0001
#define IDX_IO_CODEC_IRQTYPE    0x02
#define IDX_IO_CODEC_DMA_CURRPOS 0x10

#define GAME_HWCFG_IRQ_ENABLE           0x01
#define IDX_GAME_HWCONFIG       0x04
#define GAME_HWCFG_LEGACY_ADDRESS_ENABLE        0x08
#define IO_6A_SOMETHING2_GAMEPORT      0x0400
#define IDX_GAME_AXIS_VALUE     0x02
#define GAME_HWCFG_ADC_COUNTER_FREQ_STD 0
#define GAME_HWCFG_ADC_COUNTER_FREQ_1_200        3
#define IDX_GAME_AXES_CONFIG            0x01
#define IDX_GAME_LEGACY_COMPATIBLE      0x00
#define GAME_AXES_SAMPLING_READY        0x80

#define IRQ_SOMETHING           0x0001
#define IDX_IO_IRQSTATUS        0x64
#define IRQ_MPU401              0x0010
#define IRQ_TIMER               0x0020
#define IDX_IO_TIMER_VALUE      0x60
#define IRQ_RECORDING           0x0002
#define IRQ_GAMEPORT            0x0008
#define IRQ_I2S_OUT             0x0004
#define IRQ_PLAYBACK            0x0001

#define TIMER_IRQ_ENABLE                0x02000000UL
#define TIMER_VALUE_MASK                0x000fffffUL
#define TIMER_COUNTDOWN_ENABLE          0x01000000UL

#define AZF_IO_SIZE_MIXER       0x40
#define AZF_IO_SIZE_CTRL        0x80
#define AZF_IO_OFFS_CODEC_I2S_OUT      0x40
#define AZF_IO_OFFS_CODEC_PLAYBACK     0x00
#define AZF_IO_OFFS_CODEC_CAPTURE      0x20
#define AZF_IO_SIZE_GAME_PM     0x06
#define AZF_IO_SIZE_MPU_PM      0x04
#define AZF_IO_SIZE_CTRL_PM     0x70
#define AZF_IO_SIZE_MIXER_PM    0x22
#define AZF_ALIGN(x) (((x) + 3) & (~3))
#define AZF_IO_SIZE_OPL3_PM     0x06

#define AZF_FREQ(rate) AZF_FREQ_##rate = rate

typedef enum snd_azf3328_codec_type {
    /* warning: fixed indices (also used for bitmask checks!) */
    AZF_CODEC_PLAYBACK = 0,
    AZF_CODEC_CAPTURE = 1,
    AZF_CODEC_I2S_OUT = 2,
} snd_azf3328_codec_type;

#undef AZF_FREQ
#define AZF_FREQ(rate) AZF_FREQ_##rate = rate

typedef enum azf_freq_t {
    AZF_FREQ(4000),
    AZF_FREQ(4800),
    AZF_FREQ(5512),
    AZF_FREQ(6620),
    AZF_FREQ(8000),
    AZF_FREQ(9600),
    AZF_FREQ(11025),
    AZF_FREQ(13240),
    AZF_FREQ(16000),
    AZF_FREQ(22050),
    AZF_FREQ(32000),
    AZF_FREQ(44100),
    AZF_FREQ(48000),
    AZF_FREQ(66200),
} azf_freq_t;

#undef AZF_FREQ

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

typedef struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    /* from struct snd_azf3328 */
    unsigned long ctrl_io; /* usually 0xb000, size 128 */
    unsigned long game_io;  /* usually 0xb400, size 8 */
    unsigned long mpu_io;   /* usually 0xb800, size 4 */
    unsigned long opl3_io; /* usually 0xbc00, size 8 */
    unsigned long mixer_io; /* usually 0xc000, size 64 */

    /* register 0x6a is write-only, thus need to remember setting. */
    uint16_t shadow_reg_ctrl_6AH;

    /* register value containers for power management */
    uint32_t saved_regs_ctrl[AZF_ALIGN(AZF_IO_SIZE_CTRL_PM) / 4];
    uint32_t saved_regs_game[AZF_ALIGN(AZF_IO_SIZE_GAME_PM) / 4];
    uint32_t saved_regs_mpu[AZF_ALIGN(AZF_IO_SIZE_MPU_PM) / 4];
    uint32_t saved_regs_opl3[AZF_ALIGN(AZF_IO_SIZE_OPL3_PM) / 4];
    uint32_t saved_regs_mixer[AZF_ALIGN(AZF_IO_SIZE_MIXER_PM) / 4];

    /* Simple I/O space backing for all decoded IO ranges */
    uint8_t ctrl_regs[AZF_IO_SIZE_CTRL];
    uint8_t game_regs[AZF_IO_SIZE_GAME_PM];
    uint8_t mpu_regs[AZF_IO_SIZE_MPU_PM];
    uint8_t opl3_regs[AZF_IO_SIZE_OPL3_PM];
    uint8_t mixer_regs[AZF_IO_SIZE_MIXER];

    /* Simple timer emulation */
    QEMUTimer *timer;
    bool timer_enabled;
    uint32_t timer_value_reg;   /* last value written to IDX_IO_TIMER_VALUE */

    /* IRQ status shadow for IDX_IO_IRQSTATUS */
    uint8_t irq_status;
} PCIBaseState;

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    if (s->irq_status) {
        /* Legacy INTx only; no MSI/MSI-X wired */
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* Device-initiated DMA logic based on driver access patterns
 *
 * The real hardware performs bus-master transfers from guest buffers based
 * on DMA_START and DMA_CURRPOS registers. The Linux driver, however, never
 * programs explicit descriptors beyond writing base and using CURRPOS only
 * for readback. For probe/initialization we do not need to actually move
 * data, therefore we leave this unimplemented.
 */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    (void)s;
    (void)is_write;
}

/* Simple helper to route port IO to our register arrays based on BARs.
 *
 * BAR layout follows Linux driver usage:
 *   BAR0: ctrl_io (size 0x80)
 *   BAR1: game_io (size 0x08)
 *   BAR2: mpu_io  (size 0x04)
 *   BAR3: opl3_io (size 0x08)
 *   BAR4: mixer_io (size 0x40)
 */

static inline uint8_t *pcibase_decode_io(PCIBaseState *s, hwaddr addr)
{
    /* We expose each BAR as a separate MemoryRegion using same ops,
     * so 'addr' here is relative to that BAR. QEMU routes accesses
     * via the MemoryRegion associated to the BAR index but does not
     * pass the index, only the relative address.
     *
     * To distinguish BARs, we look at the size of the region we
     * initialized for each index and compare 'addr' against that.
     * However, each MemoryRegion instance already has its own callback,
     * so this function is actually not used for BAR selection.
     * Instead we create a thin wrapper per BAR by reusing the same
     * ops and using 'opaque' to hold PCIBaseState for all of them.
     * The driver uses absolute port numbers via inb/outb, which
     * Linux will translate to the correct BAR/offset and invoke QEMU
     * MemoryRegion callbacks with proper 'addr'. Thus 'addr' is BAR
     * relative and we map it by heuristics below.
     */
    (void)s;
    (void)addr;
    return NULL;
}

/* Map BAR-specific regions explicitly using helper functions */

static inline uint8_t *pcibase_ctrl_ptr(PCIBaseState *s, hwaddr addr)
{
    if (addr < AZF_IO_SIZE_CTRL) {
        return &s->ctrl_regs[addr];
    }
    return NULL;
}

static inline uint8_t *pcibase_game_ptr(PCIBaseState *s, hwaddr addr)
{
    if (addr < AZF_IO_SIZE_GAME_PM) {
        return &s->game_regs[addr];
    }
    return NULL;
}

static inline uint8_t *pcibase_mpu_ptr(PCIBaseState *s, hwaddr addr)
{
    if (addr < AZF_IO_SIZE_MPU_PM) {
        return &s->mpu_regs[addr];
    }
    return NULL;
}

static inline uint8_t *pcibase_opl3_ptr(PCIBaseState *s, hwaddr addr)
{
    if (addr < AZF_IO_SIZE_OPL3_PM) {
        return &s->opl3_regs[addr];
    }
    return NULL;
}

static inline uint8_t *pcibase_mixer_ptr(PCIBaseState *s, hwaddr addr)
{
    if (addr < AZF_IO_SIZE_MIXER) {
        return &s->mixer_regs[addr];
    }
    return NULL;
}

/* Timer callback to emulate sequencer timer and set IRQ_TIMER bit */

static void pcibase_timer_cb(void *opaque)
{
    PCIBaseState *s = opaque;

    if (!s->timer_enabled) {
        return;
    }

    /* Set timer IRQ bit and notify ALSA timer handler via IRQ */
    s->irq_status |= IRQ_TIMER;
    pcibase_update_irq(s);

    /* The driver expects periodic interrupts with frequency derived
     * from IDX_IO_TIMER_VALUE; for simplicity we use a fixed period
     * of 1ms. This is sufficient for driver initialization and basic
     * operation without relying on exact timing.
     */
    timer_mod(s->timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 1);
}

/* MMIO/PIO Handlers generated during Behavioral Modeling
 *
 * The Linux driver uses port IO (inb/outb/inw/outw/inl/outl) only.
 * QEMU treats IO regions for BARs mapped as PCI_BASE_ADDRESS_SPACE_IO
 * using these handlers.
 */

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* We do not expose any MMIO BARs for this device. */
    (void)opaque;
    (void)addr;
    (void)size;
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* No MMIO BARs present */
    (void)opaque;
    (void)addr;
    (void)val;
    (void)size;
}

static uint64_t pcibase_pio_read_ctrl(PCIBaseState *s, hwaddr addr, unsigned size)
{
    uint64_t val = 0xff;
    uint8_t *p8;

    if (size == 1) {
        if (addr == IDX_IO_IRQSTATUS) {
            /* irqstatus is read-only shadow */
            val = s->irq_status;
            /* The driver does not clear on read; it explicitly ACKs
             * specific sources in their own blocks. We keep status. */
            return val;
        }
        p8 = pcibase_ctrl_ptr(s, addr);
        if (!p8) {
            return 0xff;
        }
        val = *p8;
        return val;
    } else if (size == 2) {
        uint8_t *p = pcibase_ctrl_ptr(s, addr);
        if (!p || (addr + 1) >= AZF_IO_SIZE_CTRL) {
            return 0xffff;
        }
        val = p[0] | ((uint16_t)p[1] << 8);
        return val;
    } else if (size == 4) {
        uint8_t *p = pcibase_ctrl_ptr(s, addr);
        if (!p || (addr + 3) >= AZF_IO_SIZE_CTRL) {
            return 0xffffffffU;
        }
        val = p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
        return val;
    }

    return 0;
}

static void pcibase_pio_write_ctrl(PCIBaseState *s, hwaddr addr, uint64_t val, unsigned size)
{
    uint8_t *p8;

    if (size == 1) {
        /* Byte writes */
        if (addr == IDX_IO_TIMER_VALUE + 3) {
            /* Timer control byte: the driver writes 0x07 to ACK IRQ
             * and 0x04 to stop timer. For simplicity we clear IRQ_TIMER
             * on any write and stop timer if bit2 is set.
             */
            s->irq_status &= ~IRQ_TIMER;
            pcibase_update_irq(s);
            if (val & 0x04) {
                /* stop timer countdown and interrupt */
                s->timer_enabled = false;
            }
        }
        p8 = pcibase_ctrl_ptr(s, addr);
        if (!p8) {
            return;
        }
        *p8 = (uint8_t)val;
    } else if (size == 2) {
        uint8_t *p = pcibase_ctrl_ptr(s, addr);
        if (!p || (addr + 1) >= AZF_IO_SIZE_CTRL) {
            return;
        }
        /* Special handling for 0x6a write-only register: we maintain
         * shadow_reg_ctrl_6AH as the driver does and also store it
         * into the backing array for suspend/resume dumps.
         */
        if (addr == IDX_IO_6AH) {
            s->shadow_reg_ctrl_6AH = (uint16_t)val;
        }
        p[0] = (uint8_t)(val & 0xff);
        p[1] = (uint8_t)((val >> 8) & 0xff);
    } else if (size == 4) {
        uint8_t *p = pcibase_ctrl_ptr(s, addr);
        if (!p || (addr + 3) >= AZF_IO_SIZE_CTRL) {
            return;
        }
        if (addr == IDX_IO_TIMER_VALUE) {
            /* Timer configuration: driver sets delay | TIMER_xxx.
             * We only care whether countdown and IRQ are enabled.
             */
            s->timer_value_reg = (uint32_t)val;
            if ((val & TIMER_COUNTDOWN_ENABLE) && (val & TIMER_IRQ_ENABLE)) {
                if (!s->timer_enabled && s->timer) {
                    s->timer_enabled = true;
                    timer_mod(s->timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 1);
                }
            } else {
                s->timer_enabled = false;
            }
        }
        p[0] = (uint8_t)(val & 0xff);
        p[1] = (uint8_t)((val >> 8) & 0xff);
        p[2] = (uint8_t)((val >> 16) & 0xff);
        p[3] = (uint8_t)((val >> 24) & 0xff);
    }
}

static uint64_t pcibase_pio_read_game(PCIBaseState *s, hwaddr addr, unsigned size)
{
    uint8_t *p8;
    uint64_t val = 0xff;

    if (size == 1) {
        p8 = pcibase_game_ptr(s, addr);
        if (!p8) {
            return 0xff;
        }
        val = *p8;
        return val;
    } else if (size == 2) {
        uint8_t *p = pcibase_game_ptr(s, addr);
        if (!p || (addr + 1) >= AZF_IO_SIZE_GAME_PM) {
            return 0xffff;
        }
        val = p[0] | ((uint16_t)p[1] << 8);
        return val;
    }

    return 0;
}

static void pcibase_pio_write_game(PCIBaseState *s, hwaddr addr, uint64_t val, unsigned size)
{
    uint8_t *p8;

    if (size == 1) {
        p8 = pcibase_game_ptr(s, addr);
        if (!p8) {
            return;
        }
        *p8 = (uint8_t)val;
    } else if (size == 2) {
        uint8_t *p = pcibase_game_ptr(s, addr);
        if (!p || (addr + 1) >= AZF_IO_SIZE_GAME_PM) {
            return;
        }
        p[0] = (uint8_t)(val & 0xff);
        p[1] = (uint8_t)((val >> 8) & 0xff);
    }
}

static uint64_t pcibase_pio_read_mpu(PCIBaseState *s, hwaddr addr, unsigned size)
{
    uint8_t *p8;
    uint64_t val = 0xff;

    if (size == 1) {
        p8 = pcibase_mpu_ptr(s, addr);
        if (!p8) {
            return 0xff;
        }
        val = *p8;
        return val;
    } else if (size == 2) {
        uint8_t *p = pcibase_mpu_ptr(s, addr);
        if (!p || (addr + 1) >= AZF_IO_SIZE_MPU_PM) {
            return 0xffff;
        }
        val = p[0] | ((uint16_t)p[1] << 8);
        return val;
    }

    return 0;
}

static void pcibase_pio_write_mpu(PCIBaseState *s, hwaddr addr, uint64_t val, unsigned size)
{
    uint8_t *p8;
    if (size == 1) {
        p8 = pcibase_mpu_ptr(s, addr);
        if (!p8) {
            return;
        }
        *p8 = (uint8_t)val;
    } else if (size == 2) {
        uint8_t *p = pcibase_mpu_ptr(s, addr);
        if (!p || (addr + 1) >= AZF_IO_SIZE_MPU_PM) {
            return;
        }
        p[0] = (uint8_t)(val & 0xff);
        p[1] = (uint8_t)((val >> 8) & 0xff);
    }
}

static uint64_t pcibase_pio_read_opl3(PCIBaseState *s, hwaddr addr, unsigned size)
{
    uint8_t *p8;
    uint64_t val = 0xff;

    if (size == 1) {
        p8 = pcibase_opl3_ptr(s, addr);
        if (!p8) {
            return 0xff;
        }
        val = *p8;
        return val;
    } else if (size == 2) {
        uint8_t *p = pcibase_opl3_ptr(s, addr);
        if (!p || (addr + 1) >= AZF_IO_SIZE_OPL3_PM) {
            return 0xffff;
        }
        val = p[0] | ((uint16_t)p[1] << 8);
        return val;
    }

    return 0;
}

static void pcibase_pio_write_opl3(PCIBaseState *s, hwaddr addr, uint64_t val, unsigned size)
{
    uint8_t *p8;
    if (size == 1) {
        p8 = pcibase_opl3_ptr(s, addr);
        if (!p8) {
            return;
        }
        *p8 = (uint8_t)val;
    } else if (size == 2) {
        uint8_t *p = pcibase_opl3_ptr(s, addr);
        if (!p || (addr + 1) >= AZF_IO_SIZE_OPL3_PM) {
            return;
        }
        p[0] = (uint8_t)(val & 0xff);
        p[1] = (uint8_t)((val >> 8) & 0xff);
    }
}

static uint64_t pcibase_pio_read_mixer(PCIBaseState *s, hwaddr addr, unsigned size)
{
    uint8_t *p8;
    uint64_t val = 0xff;

    if (size == 1) {
        p8 = pcibase_mixer_ptr(s, addr);
        if (!p8) {
            return 0xff;
        }
        val = *p8;
        return val;
    } else if (size == 2) {
        uint8_t *p = pcibase_mixer_ptr(s, addr);
        if (!p || (addr + 1) >= AZF_IO_SIZE_MIXER) {
            return 0xffff;
        }
        val = p[0] | ((uint16_t)p[1] << 8);
        return val;
    }

    return 0;
}

static void pcibase_pio_write_mixer(PCIBaseState *s, hwaddr addr, uint64_t val, unsigned size)
{
    uint8_t *p8;

    if (size == 1) {
        p8 = pcibase_mixer_ptr(s, addr);
        if (!p8) {
            return;
        }
        *p8 = (uint8_t)val;
    } else if (size == 2) {
        uint8_t *p = pcibase_mixer_ptr(s, addr);
        if (!p || (addr + 1) >= AZF_IO_SIZE_MIXER) {
            return;
        }
        p[0] = (uint8_t)(val & 0xff);
        p[1] = (uint8_t)((val >> 8) & 0xff);
    }
}

/* Common PIO ops dispatch: we create separate MemoryRegions per BAR but
 * all of them use these same ops with PCIBaseState as opaque. QEMU does
 * not provide BAR index directly here, but each MemoryRegion has its own
 * callbacks. To distinguish we simply register different ops structs per
 * BAR index that call into these helpers.
 */

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* This generic handler is unused; actual accesses go through
     * bar-specific handlers below.
     */
    (void)opaque;
    (void)addr;
    (void)size;
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    (void)opaque;
    (void)addr;
    (void)val;
    (void)size;
}

/* BAR0: ctrl_io */
static uint64_t pcibase_pio_read_bar0(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    return pcibase_pio_read_ctrl(s, addr, size);
}

static void pcibase_pio_write_bar0(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    pcibase_pio_write_ctrl(s, addr, val, size);
}

/* BAR1: game_io */
static uint64_t pcibase_pio_read_bar1(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    return pcibase_pio_read_game(s, addr, size);
}

static void pcibase_pio_write_bar1(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    pcibase_pio_write_game(s, addr, val, size);
}

/* BAR2: mpu_io */
static uint64_t pcibase_pio_read_bar2(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    return pcibase_pio_read_mpu(s, addr, size);
}

static void pcibase_pio_write_bar2(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    pcibase_pio_write_mpu(s, addr, val, size);
}

/* BAR3: opl3_io */
static uint64_t pcibase_pio_read_bar3(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    return pcibase_pio_read_opl3(s, addr, size);
}

static void pcibase_pio_write_bar3(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    pcibase_pio_write_opl3(s, addr, val, size);
}

/* BAR4: mixer_io */
static uint64_t pcibase_pio_read_bar4(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    return pcibase_pio_read_mixer(s, addr, size);
}

static void pcibase_pio_write_bar4(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    pcibase_pio_write_mixer(s, addr, val, size);
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static const MemoryRegionOps pcibase_pio_ops_bar0 = {
    .read = pcibase_pio_read_bar0,
    .write = pcibase_pio_write_bar0,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static const MemoryRegionOps pcibase_pio_ops_bar1 = {
    .read = pcibase_pio_read_bar1,
    .write = pcibase_pio_write_bar1,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static const MemoryRegionOps pcibase_pio_ops_bar2 = {
    .read = pcibase_pio_read_bar2,
    .write = pcibase_pio_write_bar2,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static const MemoryRegionOps pcibase_pio_ops_bar3 = {
    .read = pcibase_pio_read_bar3,
    .write = pcibase_pio_write_bar3,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static const MemoryRegionOps pcibase_pio_ops_bar4 = {
    .read = pcibase_pio_read_bar4,
    .write = pcibase_pio_write_bar4,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);

    pci_device_reset(PCI_DEVICE(dev));

    /* Clear all register shadows */
    memset(s->ctrl_regs, 0, sizeof(s->ctrl_regs));
    memset(s->game_regs, 0, sizeof(s->game_regs));
    memset(s->mpu_regs, 0, sizeof(s->mpu_regs));
    memset(s->opl3_regs, 0, sizeof(s->opl3_regs));
    memset(s->mixer_regs, 0, sizeof(s->mixer_regs));

    s->shadow_reg_ctrl_6AH = 0;
    s->irq_status = 0;
    s->timer_enabled = false;
    s->timer_value_reg = 0;

    /* Some reasonable defaults based on driver expectations */
    /* Disable legacy gameport address by default */
    if (AZF_IO_SIZE_GAME_PM > IDX_GAME_HWCONFIG) {
        s->game_regs[IDX_GAME_HWCONFIG] &= ~GAME_HWCFG_LEGACY_ADDRESS_ENABLE;
    }
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
        const MemoryRegionOps *ops = NULL;
        switch (bi->index) {
        case 0:
            ops = &pcibase_pio_ops_bar0;
            break;
        case 1:
            ops = &pcibase_pio_ops_bar1;
            break;
        case 2:
            ops = &pcibase_pio_ops_bar2;
            break;
        case 3:
            ops = &pcibase_pio_ops_bar3;
            break;
        case 4:
            ops = &pcibase_pio_ops_bar4;
            break;
        default:
            ops = &pcibase_pio_ops_bar0;
            break;
        }
        memory_region_init_io(mr, OBJECT(s), ops, s, bi->name, aligned_size);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCIBASE_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCIBASE_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCIBASE_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR Initialization -- based strictly on driver usage */
    s->num_bars = 5;

    /* ctrl_io: usually 0xb000, size 0x80 */
    s->bar_info[0].index = 0;
    s->bar_info[0].type  = BAR_TYPE_PIO;
    s->bar_info[0].size  = AZF_IO_SIZE_CTRL;
    s->bar_info[0].name  = "azf3328-ctrl";

    /* game_io: usually 0xb400, size 0x08 */
    s->bar_info[1].index = 1;
    s->bar_info[1].type  = BAR_TYPE_PIO;
    s->bar_info[1].size  = AZF_IO_SIZE_GAME_PM;
    s->bar_info[1].name  = "azf3328-game";

    /* mpu_io: usually 0xb800, size 0x04 */
    s->bar_info[2].index = 2;
    s->bar_info[2].type  = BAR_TYPE_PIO;
    s->bar_info[2].size  = AZF_IO_SIZE_MPU_PM;
    s->bar_info[2].name  = "azf3328-mpu";

    /* opl3_io: usually 0xbc00, size 0x08 */
    s->bar_info[3].index = 3;
    s->bar_info[3].type  = BAR_TYPE_PIO;
    s->bar_info[3].size  = AZF_IO_SIZE_OPL3_PM;
    s->bar_info[3].name  = "azf3328-opl3";

    /* mixer_io: usually 0xc000, size 0x40 */
    s->bar_info[4].index = 4;
    s->bar_info[4].type  = BAR_TYPE_PIO;
    s->bar_info[4].size  = AZF_IO_SIZE_MIXER;
    s->bar_info[4].name  = "azf3328-mixer";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize software state */
    memset(s->ctrl_regs, 0, sizeof(s->ctrl_regs));
    memset(s->game_regs, 0, sizeof(s->game_regs));
    memset(s->mpu_regs, 0, sizeof(s->mpu_regs));
    memset(s->opl3_regs, 0, sizeof(s->opl3_regs));
    memset(s->mixer_regs, 0, sizeof(s->mixer_regs));

    s->shadow_reg_ctrl_6AH = 0;
    s->irq_status = 0;
    s->timer_enabled = false;
    s->timer_value_reg = 0;

    /* Allocate QEMU timer for ALSA sequencer emulation */
    s->timer = timer_new_ms(QEMU_CLOCK_VIRTUAL, pcibase_timer_cb, s);
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
    .name = "snd_azt3328_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT16(shadow_reg_ctrl_6AH, PCIBaseState),
        VMSTATE_UINT8(irq_status, PCIBaseState),
        VMSTATE_BOOL(timer_enabled, PCIBaseState),
        VMSTATE_UINT32(timer_value_reg, PCIBaseState),
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
