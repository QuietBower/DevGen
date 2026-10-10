/*
 * QEMU ESS Maestro1/2/2E (ES1968) PCI Sound Device Model
 * Based on Linux driver sound/pci/es1968.c
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

#define TYPE_PCIBASE_DEVICE "snd_es1968_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x1285
#define DEVICE_ID 0x0100
#define CLASS_ID PCI_CLASS_MULTIMEDIA_AUDIO

#define DRIVER_NAME "ES1968"
#define CARD_NAME "ESS Maestro1/2"
#define GPIO_DATA	0x60
#define SUPPORT_JOYSTICK 1
#define DEBOUNCE		0x0040
#define NR_APUS			64
#define NR_APU_REGS		16
#define NEC_VERSA_SUBID1	0x80581033
#define NEC_VERSA_SUBID2	0x803c1033
#define ESS_FMT_STEREO     	0x01
#define ESS_FMT_16BIT      	0x02
#define DAC_RUNNING		1
#define ADC_RUNNING		2
#define ESS_DISABLE_AUDIO	0x8000
#define ESS_ENABLE_SERIAL_IRQ	0x4000
#define IO_ADRESS_ALIAS		0x0020
#define MPU401_IRQ_ENABLE	0x0010
#define MPU401_IO_ENABLE	0x0008
#define GAME_IO_ENABLE		0x0004
#define FM_IO_ENABLE		0x0002
#define SB_IO_ENABLE		0x0001
#define PIC_SNOOP1		0x4000
#define PIC_SNOOP2		0x2000
#define SAFEGUARD		0x0800
#define DMA_CLEAR		0x0700
#define DMA_DDMA		0x0000
#define DMA_TDMA		0x0100
#define DMA_PCPCI		0x0200
#define POST_WRITE		0x0080
#define PCI_TIMING		0x0040
#define SWAP_LR			0x0020
#define SUBTR_DECODE		0x0002
#define SPDIF_CONFB		0x0100
#define HWV_CONFB		0x0080
#define GPIO_CONFB		0x0020
#define CHI_CONFB		0x0010
#define IDMA_CONFB		0x0008
#define MIDI_FIX		0x0004
#define IRQ_TO_ISA		0x0001
#define RINGB_2CODEC_ID_MASK	0x0003
#define RINGB_DIS_VALIDATION	0x0008
#define RINGB_EN_SPDIF		0x0010
#define RINGB_EN_2CODEC		0x0020
#define RINGB_SING_BIT_DUAL	0x0040
#define ESM_INDEX		0x02
#define ESM_DATA		0x00
#define ESM_AC97_INDEX		0x30
#define ESM_AC97_DATA		0x32
#define ESM_RING_BUS_DEST	0x34
#define ESM_RING_BUS_CONTR_A	0x36
#define ESM_RING_BUS_CONTR_B	0x38
#define ESM_RING_BUS_SDO	0x3A
#define WC_INDEX		0x10
#define WC_DATA			0x12
#define WC_CONTROL		0x14
#define ASSP_INDEX		0x80
#define ASSP_MEMORY		0x82
#define ASSP_DATA		0x84
#define ASSP_CONTROL_A		0xA2
#define ASSP_CONTROL_B		0xA4
#define ASSP_CONTROL_C		0xA6
#define ASSP_HOSTW_INDEX	0xA8
#define ASSP_HOSTW_DATA		0xAA
#define ASSP_HOSTW_IRQ		0xAC
#define ESM_MPU401_PORT		0x98
#define ESM_PORT_HOST_IRQ	0x18
#define IDR0_DATA_PORT		0x00
#define IDR1_CRAM_POINTER	0x01
#define IDR2_CRAM_DATA		0x02
#define IDR3_WAVE_DATA		0x03
#define IDR4_WAVE_PTR_LOW	0x04
#define IDR5_WAVE_PTR_HI	0x05
#define IDR6_TIMER_CTRL		0x06
#define IDR7_WAVE_ROMRAM	0x07
#define WRITEABLE_MAP		0xEFFFFF
#define READABLE_MAP		0x64003F
#define ESM_LEGACY_AUDIO_CONTROL 0x40
#define ESM_ACPI_COMMAND	0x54
#define ESM_CONFIG_A		0x50
#define ESM_CONFIG_B		0x52
#define ESM_DDMA		0x60
#define ESM_BOB_ENABLE		0x0001
#define ESM_BOB_START		0x0001
#define ESM_RESET_MAESTRO	0x8000
#define ESM_RESET_DIRECTSOUND   0x4000
#define ESM_HIRQ_ClkRun		0x0100
#define ESM_HIRQ_HW_VOLUME	0x0040
#define ESM_HIRQ_HARPO		0x0030
#define ESM_HIRQ_ASSP		0x0010
#define ESM_HIRQ_DSIE		0x0004
#define ESM_HIRQ_MPU401		0x0002
#define ESM_HIRQ_SB		0x0001
#define ESM_MPU401_IRQ		0x02
#define ESM_SB_IRQ		0x01
#define ESM_SOUND_IRQ		0x04
#define ESM_ASSP_IRQ		0x10
#define ESM_HWVOL_IRQ		0x40
#define ESS_SYSCLK		50000000
#define ESM_BOB_FREQ 		200
#define ESM_BOB_FREQ_MAX	800
#define ESM_FREQ_ESM1  		(49152000L / 1024L)
#define ESM_FREQ_ESM2  		(50000000L / 1024L)
#define ESM_APU_MODE_SHIFT	4
#define ESM_APU_MODE_MASK	(0xf << 4)
#define ESM_APU_OFF		0x00
#define ESM_APU_16BITLINEAR	0x01
#define ESM_APU_16BITSTEREO	0x02
#define ESM_APU_8BITLINEAR	0x03
#define ESM_APU_8BITSTEREO	0x04
#define ESM_APU_8BITDIFF	0x05
#define ESM_APU_DIGITALDELAY	0x06
#define ESM_APU_DUALTAP		0x07
#define ESM_APU_CORRELATOR	0x08
#define ESM_APU_INPUTMIXER	0x09
#define ESM_APU_WAVETABLE	0x0A
#define ESM_APU_SRCONVERTOR	0x0B
#define ESM_APU_16BITPINGPONG	0x0C
#define ESM_APU_RESERVED1	0x0D
#define ESM_APU_RESERVED2	0x0E
#define ESM_APU_RESERVED3	0x0F
#define ESM_APU_FILTER_Q_SHIFT		0
#define ESM_APU_FILTER_Q_MASK		(3 << 0)
#define ESM_APU_FILTER_LESSQ	0x00
#define ESM_APU_FILTER_MOREQ	0x03
#define ESM_APU_FILTER_TYPE_SHIFT	2
#define ESM_APU_FILTER_TYPE_MASK	(3 << 2)
#define ESM_APU_ENV_TYPE_SHIFT		8
#define ESM_APU_ENV_TYPE_MASK		(3 << 8)
#define ESM_APU_ENV_STATE_SHIFT		10
#define ESM_APU_ENV_STATE_MASK		(3 << 10)
#define ESM_APU_END_CURVE		(1 << 12)
#define ESM_APU_INT_ON_LOOP		(1 << 13)
#define ESM_APU_DMA_ENABLE		(1 << 14)
#define ESM_APU_SUBMIX_GROUP_SHIRT	0
#define ESM_APU_SUBMIX_GROUP_MASK	(7 << 0)
#define ESM_APU_SUBMIX_MODE		(1 << 3)
#define ESM_APU_6dB			(1 << 4)
#define ESM_APU_DUAL_EFFECT		(1 << 5)
#define ESM_APU_EFFECT_CHANNELS_SHIFT	6
#define ESM_APU_EFFECT_CHANNELS_MASK	(3 << 6)
#define ESM_APU_STEP_SIZE_MASK		0x0fff
#define ESM_APU_PHASE_SHIFT		0
#define ESM_APU_PHASE_MASK		(0xff << 0)
#define ESM_APU_WAVE64K_PAGE_SHIFT	8
#define ESM_APU_WAVE64K_PAGE_MASK	(0xff << 8)
#define ESM_APU_EFFECT_GAIN_SHIFT	0
#define ESM_APU_EFFECT_GAIN_MASK	(0xff << 0)
#define ESM_APU_TREMOLO_DEPTH_SHIFT	8
#define ESM_APU_TREMOLO_DEPTH_MASK	(0xf << 8)
#define ESM_APU_TREMOLO_RATE_SHIFT	12
#define ESM_APU_TREMOLO_RATE_MASK	(0xf << 12)
#define ESM_APU_AMPLITUDE_NOW_SHIFT	8
#define ESM_APU_AMPLITUDE_NOW_MASK	(0xff << 8)
#define ESM_APU_POLAR_PAN_SHIFT		0
#define ESM_APU_POLAR_PAN_MASK		(0x3f << 0)
#define ESM_APU_PAN_CENTER_CIRCLE		0x00
#define ESM_APU_PAN_MIDDLE_RADIUS		0x01
#define ESM_APU_PAN_OUTSIDE_RADIUS		0x02
#define ESM_APU_FILTER_TUNING_SHIFT	8
#define ESM_APU_FILTER_TUNING_MASK	(0xff << 8)
#define ESM_APU_DATA_SRC_A_SHIFT	0
#define ESM_APU_DATA_SRC_A_MASK		(0x7f << 0)
#define ESM_APU_INV_POL_A		(1 << 7)
#define ESM_APU_DATA_SRC_B_SHIFT	8
#define ESM_APU_DATA_SRC_B_MASK		(0x7f << 8)
#define ESM_APU_INV_POL_B		(1 << 15)
#define ESM_APU_VIBRATO_RATE_SHIFT	0
#define ESM_APU_VIBRATO_RATE_MASK	(0xf << 0)
#define ESM_APU_VIBRATO_DEPTH_SHIFT	4
#define ESM_APU_VIBRATO_DEPTH_MASK	(0xf << 4)
#define ESM_APU_VIBRATO_PHASE_SHIFT	8
#define ESM_APU_VIBRATO_PHASE_MASK	(0xff << 8)
#define ESM_APU_RADIUS_SELECT		(1 << 6)
#define ESM_APU_FILTER_2POLE_LOPASS	0x00
#define ESM_APU_FILTER_2POLE_BANDPASS	0x01
#define ESM_APU_FILTER_2POLE_HIPASS	0x02
#define ESM_APU_FILTER_1POLE_LOPASS	0x03
#define ESM_APU_FILTER_1POLE_HIPASS	0x04
#define ESM_APU_FILTER_OFF		0x05
#define ESM_APU_ATFP_AMPLITUDE			0x00
#define ESM_APU_ATFP_TREMELO			0x01
#define ESM_APU_ATFP_FILTER			0x02
#define ESM_APU_ATFP_PAN			0x03
#define ESM_APU_ATFP_FLG_OFF			0x00
#define ESM_APU_ATFP_FLG_WAIT			0x01
#define ESM_APU_ATFP_FLG_DONE			0x02
#define ESM_APU_ATFP_FLG_INPROCESS		0x03
#define ESM_MEM_ALIGN		0x1000
#define ESM_MIXBUF_SIZE		0x400
#define ESM_MODE_PLAY		0
#define ESM_MODE_CAPTURE	1
#define CLOCK_MEASURE_BUFSIZE	16768
#define JOYSTICK_ADDR	0x200
#define IO_MASK		4
#define IO_DIR		8

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

#define WAVE_CACHE_SIZE 512

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;
    uint16_t intr_status;  /* unused but kept */
    uint16_t intr_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint16_t regs[256];          /* maestro registers 0..255 */
    uint8_t maestro_index;       /* last index written to ESM_INDEX */

    /* Wave Cache */
    uint16_t wave_index;          /* last index written to WC_INDEX */
    uint16_t wave_cache[WAVE_CACHE_SIZE];

    /* AC97 codec */
    uint8_t ac97_index;
    uint16_t ac97_regs[0x80];

    /* Direct registers */
    uint16_t wp_status;          /* port 0x04 */
    uint8_t irq_status;          /* port 0x1A */
    uint16_t irq_enable;         /* port 0x18 */
    uint8_t hw_vol[4];           /* ports 0x1c-0x1f */
    uint16_t ring_bus_dest;      /* port 0x34 */
    uint16_t ring_bus_ctrl_a;    /* port 0x36 */
    uint16_t ring_bus_ctrl_b;    /* port 0x38 */
    uint16_t ring_bus_sdo;       /* port 0x3A */
    uint16_t legacy_audio_ctrl;  /* port 0x40 */
    uint16_t config_a;           /* port 0x50 */
    uint16_t config_b;           /* port 0x52 */
    uint16_t ddma;               /* port 0x60 */
    uint16_t assp_ctrl_a;        /* port 0xA2 */
    uint16_t assp_ctrl_b;        /* port 0xA4 */
    uint16_t assp_ctrl_c;        /* port 0xA6 */
    uint16_t assp_hostw_index;   /* port 0xA8 */
    uint16_t assp_hostw_data;    /* port 0xAA */
    uint16_t assp_hostw_irq;     /* port 0xAC */
    uint16_t wc_control;         /* port 0x14 */

    /* Other state */
    uint16_t audio_status;      /* Operational status flags */
    bool reset_done;            /* State used to handle reset sequences */
    bool pm_enabled;            /* Power management state (D0-D3) */
    int power_state;

    /* Not emulated: BOB timer, APU internal state, etc. */
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint8_t active = s->irq_status & (s->irq_enable & 0xFF);
    pci_set_irq(pdev, active ? 1 : 0);
}

/* PIO Handlers */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = 0;

    switch (addr) {
    case ESM_DATA: /* 0x00 */
        if (size == 2) {
            val = s->regs[s->maestro_index];
        } else if (size == 1) {
            /* low byte of the word register */
            val = s->regs[s->maestro_index] & 0xFF;
        }
        break;
    case ESM_INDEX: /* 0x02 */
        if (size == 2) {
            val = s->maestro_index;
        } else if (size == 1) {
            val = s->maestro_index;
        }
        break;
    case WC_INDEX: /* 0x10 */
        if (size == 2) {
            val = s->wave_index;
        } else if (size == 1) {
            val = s->wave_index & 0xFF;
        }
        break;
    case WC_DATA: /* 0x12 */
        if (s->wave_index < WAVE_CACHE_SIZE) {
            if (size == 2) {
                val = s->wave_cache[s->wave_index];
            } else if (size == 1) {
                val = s->wave_cache[s->wave_index] & 0xFF;
            }
        }
        break;
    case WC_CONTROL: /* 0x14 */
        if (size == 2) {
            val = s->wc_control;
        } else if (size == 1) {
            val = s->wc_control & 0xFF;
        }
        break;
    case ESM_PORT_HOST_IRQ: /* 0x18 */
        if (size == 2) {
            val = s->irq_enable;
        } else if (size == 1) {
            val = s->irq_enable & 0xFF;
        }
        break;
    case 0x04: /* WP interrupt status/clear */
        if (size == 2) {
            val = s->wp_status;
        } else if (size == 1) {
            val = s->wp_status & 0xFF;
        }
        break;
    case 0x1A: /* Interrupt status */
        if (size == 1) {
            val = s->irq_status;
        } else if (size == 2) {
            val = s->irq_status;
        }
        break;
    case 0x1c: /* HW volume counter 1 */
    case 0x1d: /* HW volume counter 2 */
    case 0x1e: /* HW volume counter 3 */
    case 0x1f: /* HW volume counter 4 */
        if (size == 1) {
            val = s->hw_vol[addr - 0x1c];
        } else if (size == 2) {
            val = s->hw_vol[addr - 0x1c];
        }
        break;
    case ESM_AC97_INDEX: /* 0x30 */
        /* AC97 index read: bit 0 is busy, we return 0 (not busy) */
        if (size == 1) {
            val = 0;
        } else {
            val = 0;
        }
        break;
    case ESM_AC97_DATA: /* 0x32 */
        if (size == 2) {
            val = s->ac97_regs[s->ac97_index & 0x7F];
        } else if (size == 1) {
            val = s->ac97_regs[s->ac97_index & 0x7F] & 0xFF;
        }
        break;
    case ESM_RING_BUS_DEST: /* 0x34 */
        if (size == 2) val = s->ring_bus_dest;
        else if (size == 1) val = s->ring_bus_dest & 0xFF;
        break;
    case ESM_RING_BUS_CONTR_A: /* 0x36 */
        if (size == 2) val = s->ring_bus_ctrl_a;
        else if (size == 1) val = s->ring_bus_ctrl_a & 0xFF;
        break;
    case ESM_RING_BUS_CONTR_B: /* 0x38 */
        if (size == 2) val = s->ring_bus_ctrl_b;
        else if (size == 1) val = s->ring_bus_ctrl_b & 0xFF;
        break;
    case ESM_RING_BUS_SDO: /* 0x3A */
        if (size == 2) val = s->ring_bus_sdo;
        else if (size == 1) val = s->ring_bus_sdo & 0xFF;
        break;
    case ESM_LEGACY_AUDIO_CONTROL: /* 0x40 */
        if (size == 2) val = s->legacy_audio_ctrl;
        else if (size == 1) val = s->legacy_audio_ctrl & 0xFF;
        break;
    case ESM_CONFIG_A: /* 0x50 */
        if (size == 2) val = s->config_a;
        else if (size == 1) val = s->config_a & 0xFF;
        break;
    case ESM_CONFIG_B: /* 0x52 */
        if (size == 2) val = s->config_b;
        else if (size == 1) val = s->config_b & 0xFF;
        break;
    case ESM_DDMA: /* 0x60 */
        if (size == 2) val = s->ddma;
        else if (size == 1) val = s->ddma & 0xFF;
        break;
    case ASSP_CONTROL_A: /* 0xA2 */
        if (size == 2) val = s->assp_ctrl_a;
        else if (size == 1) val = s->assp_ctrl_a & 0xFF;
        break;
    case ASSP_CONTROL_B: /* 0xA4 */
        if (size == 2) val = s->assp_ctrl_b;
        else if (size == 1) val = s->assp_ctrl_b & 0xFF;
        break;
    case ASSP_CONTROL_C: /* 0xA6 */
        if (size == 2) val = s->assp_ctrl_c;
        else if (size == 1) val = s->assp_ctrl_c & 0xFF;
        break;
    case ASSP_HOSTW_INDEX: /* 0xA8 */
        if (size == 2) val = s->assp_hostw_index;
        else if (size == 1) val = s->assp_hostw_index & 0xFF;
        break;
    case ASSP_HOSTW_DATA: /* 0xAA */
        if (size == 2) val = s->assp_hostw_data;
        else if (size == 1) val = s->assp_hostw_data & 0xFF;
        break;
    case ASSP_HOSTW_IRQ: /* 0xAC */
        if (size == 2) val = s->assp_hostw_irq;
        else if (size == 1) val = s->assp_hostw_irq & 0xFF;
        break;
    default:
        /* For addresses not specifically handled, return 0 */
        if (addr < 0x100) {
            val = 0;
        }
        break;
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint16_t word_val = (size == 2) ? (uint16_t)val : (uint8_t)val;
    uint8_t byte_val = (uint8_t)val;

    switch (addr) {
    case ESM_DATA: /* 0x00 */
        if (size == 2) {
            s->regs[s->maestro_index] = word_val;
        } else if (size == 1) {
            /* Write low byte, preserve high byte? driver writes whole word */
            s->regs[s->maestro_index] = (s->regs[s->maestro_index] & 0xFF00) | byte_val;
        }
        break;
    case ESM_INDEX: /* 0x02 */
        if (size == 2) {
            s->maestro_index = word_val & 0xFF;
        } else if (size == 1) {
            s->maestro_index = byte_val;
        }
        break;
    case WC_INDEX: /* 0x10 */
        if (size == 2) {
            s->wave_index = word_val;
        } else if (size == 1) {
            s->wave_index = byte_val;
        }
        break;
    case WC_DATA: /* 0x12 */
        if (s->wave_index < WAVE_CACHE_SIZE) {
            if (size == 2) {
                s->wave_cache[s->wave_index] = word_val;
            } else if (size == 1) {
                s->wave_cache[s->wave_index] = (s->wave_cache[s->wave_index] & 0xFF00) | byte_val;
            }
        }
        break;
    case WC_CONTROL: /* 0x14 */
        if (size == 2) {
            s->wc_control = word_val;
        } else if (size == 1) {
            s->wc_control = (s->wc_control & 0xFF00) | byte_val;
        }
        break;
    case ESM_PORT_HOST_IRQ: /* 0x18 */
        if (size == 2) {
            s->irq_enable = word_val;
        } else if (size == 1) {
            s->irq_enable = (s->irq_enable & 0xFF00) | byte_val;
        }
        pcibase_update_irq(s);
        break;
    case 0x04: /* WP interrupt status/clear, W1C */
        if (size == 2) {
            s->wp_status &= ~word_val;
        } else if (size == 1) {
            s->wp_status &= ~byte_val;
        }
        break;
    case 0x1A: /* Interrupt status, write to clear */
        if (size == 1) {
            s->irq_status &= ~byte_val;
        } else if (size == 2) {
            s->irq_status &= ~((uint8_t)val);
        }
        pcibase_update_irq(s);
        break;
    case 0x1c:
    case 0x1d:
    case 0x1e:
    case 0x1f:
        if (size == 1) {
            s->hw_vol[addr - 0x1c] = byte_val;
        } else if (size == 2) {
            s->hw_vol[addr - 0x1c] = (uint8_t)word_val;
        }
        break;
    case ESM_AC97_INDEX: /* 0x30 */
        if (size == 1) {
            s->ac97_index = byte_val;
        } else if (size == 2) {
            s->ac97_index = (uint8_t)word_val;
        }
        break;
    case ESM_AC97_DATA: /* 0x32 */
        if (size == 2) {
            s->ac97_regs[s->ac97_index & 0x7F] = word_val;
        } else if (size == 1) {
            s->ac97_regs[s->ac97_index & 0x7F] = (s->ac97_regs[s->ac97_index & 0x7F] & 0xFF00) | byte_val;
        }
        break;
    case ESM_RING_BUS_DEST: /* 0x34 */
        if (size == 2) s->ring_bus_dest = word_val;
        else if (size == 1) s->ring_bus_dest = (s->ring_bus_dest & 0xFF00) | byte_val;
        break;
    case ESM_RING_BUS_CONTR_A: /* 0x36 */
        if (size == 2) s->ring_bus_ctrl_a = word_val;
        else if (size == 1) s->ring_bus_ctrl_a = (s->ring_bus_ctrl_a & 0xFF00) | byte_val;
        break;
    case ESM_RING_BUS_CONTR_B: /* 0x38 */
        if (size == 2) s->ring_bus_ctrl_b = word_val;
        else if (size == 1) s->ring_bus_ctrl_b = (s->ring_bus_ctrl_b & 0xFF00) | byte_val;
        break;
    case ESM_RING_BUS_SDO: /* 0x3A */
        if (size == 2) s->ring_bus_sdo = word_val;
        else if (size == 1) s->ring_bus_sdo = (s->ring_bus_sdo & 0xFF00) | byte_val;
        break;
    case ESM_LEGACY_AUDIO_CONTROL: /* 0x40 */
        if (size == 2) s->legacy_audio_ctrl = word_val;
        else if (size == 1) s->legacy_audio_ctrl = (s->legacy_audio_ctrl & 0xFF00) | byte_val;
        break;
    case ESM_CONFIG_A: /* 0x50 */
        if (size == 2) s->config_a = word_val;
        else if (size == 1) s->config_a = (s->config_a & 0xFF00) | byte_val;
        break;
    case ESM_CONFIG_B: /* 0x52 */
        if (size == 2) s->config_b = word_val;
        else if (size == 1) s->config_b = (s->config_b & 0xFF00) | byte_val;
        break;
    case ESM_DDMA: /* 0x60 */
        if (size == 2) s->ddma = word_val;
        else if (size == 1) s->ddma = (s->ddma & 0xFF00) | byte_val;
        break;
    case ASSP_CONTROL_A: /* 0xA2 */
        if (size == 2) s->assp_ctrl_a = word_val;
        else if (size == 1) s->assp_ctrl_a = (s->assp_ctrl_a & 0xFF00) | byte_val;
        break;
    case ASSP_CONTROL_B: /* 0xA4 */
        if (size == 2) s->assp_ctrl_b = word_val;
        else if (size == 1) s->assp_ctrl_b = (s->assp_ctrl_b & 0xFF00) | byte_val;
        break;
    case ASSP_CONTROL_C: /* 0xA6 */
        if (size == 2) s->assp_ctrl_c = word_val;
        else if (size == 1) s->assp_ctrl_c = (s->assp_ctrl_c & 0xFF00) | byte_val;
        break;
    case ASSP_HOSTW_INDEX: /* 0xA8 */
        if (size == 2) s->assp_hostw_index = word_val;
        else if (size == 1) s->assp_hostw_index = (s->assp_hostw_index & 0xFF00) | byte_val;
        break;
    case ASSP_HOSTW_DATA: /* 0xAA */
        if (size == 2) s->assp_hostw_data = word_val;
        else if (size == 1) s->assp_hostw_data = (s->assp_hostw_data & 0xFF00) | byte_val;
        break;
    case ASSP_HOSTW_IRQ: /* 0xAC */
        if (size == 2) s->assp_hostw_irq = word_val;
        else if (size == 1) s->assp_hostw_irq = (s->assp_hostw_irq & 0xFF00) | byte_val;
        break;
    default:
        /* Ignore writes to unhandled ports */
        break;
    }
}

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 2 },
    .impl  = { .min_access_size = 1, .max_access_size = 2 },
};

/* MMIO handlers are not used, but required for completeness */
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
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Reset all registers to zero */
    memset(s->regs, 0, sizeof(s->regs));
    memset(s->wave_cache, 0, sizeof(s->wave_cache));
    memset(s->ac97_regs, 0, sizeof(s->ac97_regs));
    s->maestro_index = 0;
    s->wave_index = 0;
    s->ac97_index = 0;
    s->wp_status = 0;
    s->irq_status = 0;
    s->irq_enable = 0;
    memset(s->hw_vol, 0x88, sizeof(s->hw_vol)); /* default hw vol counters to 0x88 */
    s->ring_bus_dest = 0;
    s->ring_bus_ctrl_a = 0;
    s->ring_bus_ctrl_b = 0;
    s->ring_bus_sdo = 0;
    s->legacy_audio_ctrl = 0;
    s->config_a = 0;
    s->config_b = 0;
    s->ddma = 0;
    s->assp_ctrl_a = 0;
    s->assp_ctrl_b = 0;
    s->assp_ctrl_c = 0;
    s->assp_hostw_index = 0;
    s->assp_hostw_data = 0;
    s->assp_hostw_irq = 0;
    s->wc_control = 0;

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
    pci_set_word(pci_conf + PCI_VENDOR_ID, VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
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
    s->bar_info[0].size = 256; /* IO space of 256 bytes */
    s->bar_info[0].name = "es1968-io";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize interrupts */
    s->irq_enable = 0;
    s->irq_status = 0;
    pcibase_update_irq(s);
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

static const VMStateDescription vmstate_pcibase = {
    .name = "snd_es1968_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT16_ARRAY(regs, PCIBaseState, 256),
        VMSTATE_UINT8(maestro_index, PCIBaseState),
        VMSTATE_UINT16(wave_index, PCIBaseState),
        VMSTATE_UINT16_ARRAY(wave_cache, PCIBaseState, WAVE_CACHE_SIZE),
        VMSTATE_UINT8(ac97_index, PCIBaseState),
        VMSTATE_UINT16_ARRAY(ac97_regs, PCIBaseState, 0x80),
        VMSTATE_UINT16(wp_status, PCIBaseState),
        VMSTATE_UINT8(irq_status, PCIBaseState),
        VMSTATE_UINT16(irq_enable, PCIBaseState),
        VMSTATE_UINT8_ARRAY(hw_vol, PCIBaseState, 4),
        VMSTATE_UINT16(ring_bus_dest, PCIBaseState),
        VMSTATE_UINT16(ring_bus_ctrl_a, PCIBaseState),
        VMSTATE_UINT16(ring_bus_ctrl_b, PCIBaseState),
        VMSTATE_UINT16(ring_bus_sdo, PCIBaseState),
        VMSTATE_UINT16(legacy_audio_ctrl, PCIBaseState),
        VMSTATE_UINT16(config_a, PCIBaseState),
        VMSTATE_UINT16(config_b, PCIBaseState),
        VMSTATE_UINT16(ddma, PCIBaseState),
        VMSTATE_UINT16(assp_ctrl_a, PCIBaseState),
        VMSTATE_UINT16(assp_ctrl_b, PCIBaseState),
        VMSTATE_UINT16(assp_ctrl_c, PCIBaseState),
        VMSTATE_UINT16(assp_hostw_index, PCIBaseState),
        VMSTATE_UINT16(assp_hostw_data, PCIBaseState),
        VMSTATE_UINT16(assp_hostw_irq, PCIBaseState),
        VMSTATE_UINT16(wc_control, PCIBaseState),
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
