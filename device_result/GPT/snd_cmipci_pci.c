/*
 * QEMU PCI Device Model for CMI8738/CMI8338 (cmipci)
 * Phase 2: Behavioral implementation based strictly on Linux driver cmipci.c
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
/* linux/pci_ids.h is not available in QEMU build environment; define needed IDs locally. */

#define PCI_VENDOR_ID_CMEDIA          0x13f6
#define PCI_DEVICE_ID_CMEDIA_CM8338A  0x0100
#define PCI_CLASS_MULTIMEDIA_AUDIO    0x0401

#define TYPE_PCIBASE_DEVICE "snd_cmipci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define CM_REG_FUNCTRL0        0x00
#define CM_RST_CH1             0x00080000
#define CM_RST_CH0             0x00040000
#define CM_CHEN1               0x00020000
#define CM_CHEN0               0x00010000
#define CM_PAUSE1              0x00000008
#define CM_PAUSE0              0x00000004
#define CM_CHADC1              0x00000002
#define CM_CHADC0              0x00000001
#define CM_REG_FUNCTRL1        0x04
#define CM_DSFC_MASK           0x0000E000
#define CM_DSFC_SHIFT          13
#define CM_ASFC_MASK           0x00001C00
#define CM_ASFC_SHIFT          10
#define CM_SPDF_1              0x00000200
#define CM_SPDF_0              0x00000100
#define CM_SPDFLOOP            0x00000080
#define CM_SPDO2DAC            0x00000040
#define CM_INTRM               0x00000020
#define CM_BREQ                0x00000010
#define CM_VOICE_EN            0x00000008
#define CM_UART_EN             0x00000004
#define CM_JYSTK_EN            0x00000002
#define CM_ZVPORT              0x00000001
#define CM_REG_CHFORMAT        0x08
#define CM_CHB3D5C             0x80000000
#define CM_FMOFFSET2           0x40000000
#define CM_CHB3D               0x20000000
#define CM_CHIP_MASK1          0x1f000000
#define CM_CHIP_037            0x01000000
#define CM_SETLAT48            0x00800000
#define CM_EDGEIRQ             0x00400000
#define CM_SPD24SEL39          0x00200000
#define CM_AC3EN1              0x00100000
#define CM_SPDIF_SELECT1       0x00080000
#define CM_SPD24SEL            0x00020000
#define CM_ADCBITLEN_MASK      0x0000C000
#define CM_ADCBITLEN_16        0x00000000
#define CM_ADCBITLEN_15        0x00004000
#define CM_ADCBITLEN_14        0x00008000
#define CM_ADCBITLEN_13        0x0000C000
#define CM_ADCDACLEN_MASK      0x00003000
#define CM_ADCDACLEN_060       0x00000000
#define CM_ADCDACLEN_066       0x00001000
#define CM_ADCDACLEN_130       0x00002000
#define CM_ADCDACLEN_280       0x00003000
#define CM_ADCDLEN_MASK        0x00003000
#define CM_ADCDLEN_ORIGINAL    0x00000000
#define CM_ADCDLEN_EXTRA       0x00001000
#define CM_ADCDLEN_24K         0x00002000
#define CM_ADCDLEN_WEIGHT      0x00003000
#define CM_CH1_SRATE_176K      0x00000800
#define CM_CH1_SRATE_96K       0x00000800
#define CM_CH1_SRATE_88K       0x00000400
#define CM_CH0_SRATE_176K      0x00000200
#define CM_CH0_SRATE_96K       0x00000200
#define CM_CH0_SRATE_88K       0x00000100
#define CM_CH0_SRATE_128K      0x00000300
#define CM_CH0_SRATE_MASK      0x00000300
#define CM_SPDIF_INVERSE2      0x00000080
#define CM_DBLSPDS             0x00000040
#define CM_POLVALID            0x00000020
#define CM_SPDLOCKED           0x00000010
#define CM_CH1FMT_MASK         0x0000000C
#define CM_CH1FMT_SHIFT        2
#define CM_CH0FMT_MASK         0x00000003
#define CM_CH0FMT_SHIFT        0
#define CM_REG_INT_HLDCLR      0x0C
#define CM_CHIP_MASK2          0xff000000
#define CM_CHIP_8768           0x20000000
#define CM_CHIP_055            0x08000000
#define CM_CHIP_039            0x04000000
#define CM_CHIP_039_6CH        0x01000000
#define CM_UNKNOWN_INT_EN      0x00080000
#define CM_TDMA_INT_EN         0x00040000
#define CM_CH1_INT_EN          0x00020000
#define CM_CH0_INT_EN          0x00010000
#define CM_REG_INT_STATUS      0x10
#define CM_INTR                0x80000000
#define CM_VCO                 0x08000000
#define CM_MCBINT              0x04000000
#define CM_UARTINT             0x00010000
#define CM_LTDMAINT            0x00008000
#define CM_HTDMAINT            0x00004000
#define CM_XDO46               0x00000080
#define CM_LHBTOG              0x00000040
#define CM_LEG_HDMA            0x00000020
#define CM_LEG_STEREO          0x00000010
#define CM_CH1BUSY             0x00000008
#define CM_CH0BUSY             0x00000004
#define CM_CHINT1              0x00000002
#define CM_CHINT0              0x00000001
#define CM_REG_LEGACY_CTRL     0x14
#define CM_NXCHG               0x80000000
#define CM_VMPU_MASK           0x60000000
#define CM_VMPU_330            0x00000000
#define CM_VMPU_320            0x20000000
#define CM_VMPU_310            0x40000000
#define CM_VMPU_300            0x60000000
#define CM_ENWR8237            0x10000000
#define CM_VSBSEL_MASK         0x0C000000
#define CM_VSBSEL_220          0x00000000
#define CM_VSBSEL_240          0x04000000
#define CM_VSBSEL_260          0x08000000
#define CM_VSBSEL_280          0x0C000000
#define CM_FMSEL_MASK          0x03000000
#define CM_FMSEL_388           0x00000000
#define CM_FMSEL_3C8           0x01000000
#define CM_FMSEL_3E0           0x02000000
#define CM_FMSEL_3E8           0x03000000
#define CM_ENSPDOUT            0x00800000
#define CM_SPDCOPYRHT          0x00400000
#define CM_DAC2SPDO            0x00200000
#define CM_INVIDWEN            0x00100000
#define CM_SETRETRY            0x00100000
#define CM_C_EEACCESS          0x00080000
#define CM_C_EECS              0x00040000
#define CM_C_EEDI46            0x00020000
#define CM_C_EECK46            0x00010000
#define CM_CHB3D6C             0x00008000
#define CM_CENTR2LIN           0x00004000
#define CM_BASE2LIN            0x00002000
#define CM_EXBASEN             0x00001000
#define CM_REG_MISC_CTRL       0x18
#define CM_PWD                 0x80000000
#define CM_RESET               0x40000000
#define CM_SFIL_MASK           0x30000000
#define CM_VMGAIN              0x10000000
#define CM_TXVX                0x08000000
#define CM_N4SPK3D             0x04000000
#define CM_SPDO5V              0x02000000
#define CM_SPDIF48K            0x01000000
#define CM_SPATUS48K           0x01000000
#define CM_ENDBDAC             0x00800000
#define CM_XCHGDAC             0x00400000
#define CM_SPD32SEL            0x00200000
#define CM_SPDFLOOPI           0x00100000
#define CM_FM_EN               0x00080000
#define CM_AC3EN2              0x00040000
#define CM_ENWRASID            0x00010000
#define CM_VIDWPDSB            0x00010000
#define CM_SPDF_AC97           0x00008000
#define CM_MASK_EN             0x00004000
#define CM_ENWRMSID            0x00002000
#define CM_VIDWPPRT            0x00002000
#define CM_SFILENB             0x00001000
#define CM_MMODE_MASK          0x00000E00
#define CM_SPDIF_SELECT2       0x00000100
#define CM_ENCENTER            0x00000080
#define CM_FLINKON             0x00000040
#define CM_MUTECH1             0x00000040
#define CM_FLINKOFF            0x00000020
#define CM_MIDSMP              0x00000010
#define CM_UPDDMA_MASK         0x0000000C
#define CM_UPDDMA_2048         0x00000000
#define CM_UPDDMA_1024         0x00000004
#define CM_UPDDMA_512          0x00000008
#define CM_UPDDMA_256          0x0000000C
#define CM_TWAIT_MASK          0x00000003
#define CM_TWAIT1              0x00000002
#define CM_TWAIT0              0x00000001
#define CM_REG_TDMA_POSITION   0x1C
#define CM_TDMA_CNT_MASK       0xFFFF0000
#define CM_TDMA_ADR_MASK       0x0000FFFF
#define CM_REG_MIXER0          0x20
#define CM_REG_SBVR            0x20
#define CM_REG_DEV             0x20
#define CM_REG_MIXER21         0x21
#define CM_UNKNOWN_21_MASK     0x78
#define CM_X_ADPCM             0x04
#define CM_PROINV              0x02
#define CM_X_SB16              0x01
#define CM_REG_SB16_DATA       0x22
#define CM_REG_SB16_ADDR       0x23
#define CM_REFFREQ_XIN         ((315 * 1000 * 1000) / 22)
#define CM_ADCMULT_XIN         512
#define CM_TOLERANCE_RATE      0.001
#define CM_MAXIMUM_RATE        80000000
#define CM_REG_MIXER1          0x24
#define CM_FMMUTE              0x80
#define CM_FMMUTE_SHIFT        7
#define CM_WSMUTE              0x40
#define CM_WSMUTE_SHIFT        6
#define CM_REAR2LIN            0x20
#define CM_REAR2LIN_SHIFT      5
#define CM_REAR2FRONT          0x10
#define CM_REAR2FRONT_SHIFT    4
#define CM_WAVEINL             0x08
#define CM_WAVEINL_SHIFT       3
#define CM_WAVEINR             0x04
#define CM_WAVEINR_SHIFT       2
#define CM_X3DEN               0x02
#define CM_X3DEN_SHIFT         1
#define CM_CDPLAY              0x01
#define CM_CDPLAY_SHIFT        0
#define CM_REG_MIXER2          0x25
#define CM_RAUXREN             0x80
#define CM_RAUXREN_SHIFT       7
#define CM_RAUXLEN             0x40
#define CM_RAUXLEN_SHIFT       6
#define CM_VAUXRM              0x20
#define CM_VAUXRM_SHIFT        5
#define CM_VAUXLM              0x10
#define CM_VAUXLM_SHIFT        4
#define CM_VADMIC_MASK         0x0e
#define CM_VADMIC_SHIFT        1
#define CM_MICGAINZ            0x01
#define CM_MICGAINZ_SHIFT      0
#define CM_REG_AUX_VOL         0x26
#define CM_VAUXL_MASK          0xf0
#define CM_VAUXR_MASK          0x0f
#define CM_REG_MISC            0x27
#define CM_UNKNOWN_27_MASK     0xd8
#define CM_XGPO1               0x20
#define CM_MIC_CENTER_LFE      0x04
#define CM_SPDIF_INVERSE       0x04
#define CM_SPDVALID            0x02
#define CM_DMAUTO              0x01
#define CM_REG_AC97            0x28
#define CM_REG_EXTERN_CODEC    CM_REG_AC97
#define CM_REG_MPU_PCI         0x40
#define CM_REG_FM_PCI          0x50
#define CM_REG_EXTENT_IND      0xf0
#define CM_VPHONE_MASK         0xe0
#define CM_VPHONE_SHIFT        5
#define CM_VPHOM               0x10
#define CM_VSPKM               0x08
#define CM_RLOOPREN            0x04
#define CM_RLOOPLEN            0x02
#define CM_VADMIC3             0x01
#define CM_REG_PLL             0xf8
#define CM_REG_CH0_FRAME1      0x80
#define CM_REG_CH0_FRAME2      0x84
#define CM_REG_CH1_FRAME1      0x88
#define CM_REG_CH1_FRAME2      0x8C
#define CM_REG_EXT_MISC        0x90
#define CM_ADC48K44K           0x10000000
#define CM_CHB3D8C             0x00200000
#define CM_SPD32FMT            0x00100000
#define CM_ADC2SPDIF           0x00080000
#define CM_SHAREADC            0x00040000
#define CM_REALTCMP            0x00020000
#define CM_INVLRCK             0x00010000
#define CM_UNKNOWN_90_MASK     0x0000FFFF
#define CM_EXTENT_CODEC        0x100
#define CM_EXTENT_MIDI         0x2
#define CM_EXTENT_SYNTH        0x4
#define CM_CH_PLAY             0
#define CM_CH_CAPT             1
#define CM_OPEN_NONE           0
#define CM_OPEN_CH_MASK        0x01
#define CM_OPEN_DAC            0x10
#define CM_OPEN_ADC            0x20
#define CM_OPEN_SPDIF          0x40
#define CM_OPEN_MCHAN          0x80
#define CM_OPEN_PLAYBACK       (CM_CH_PLAY | CM_OPEN_DAC)
#define CM_OPEN_PLAYBACK2      (CM_CH_CAPT | CM_OPEN_DAC)
#define CM_OPEN_PLAYBACK_MULTI (CM_CH_PLAY | CM_OPEN_DAC | CM_OPEN_MCHAN)
#define CM_OPEN_CAPTURE        (CM_CH_CAPT | CM_OPEN_ADC)
#define CM_OPEN_SPDIF_PLAYBACK (CM_CH_PLAY | CM_OPEN_DAC | CM_OPEN_SPDIF)
#define CM_OPEN_SPDIF_CAPTURE  (CM_CH_CAPT | CM_OPEN_ADC | CM_OPEN_SPDIF)
#define CM_PLAYBACK_SRATE_176K CM_CH0_SRATE_176K
#define CM_PLAYBACK_SPDF       CM_SPDF_0
#define CM_CAPTURE_SPDF        CM_SPDF_1

#define CMIPCI_VENDOR_ID       PCI_VENDOR_ID_CMEDIA
#define CMIPCI_DEVICE_ID       PCI_DEVICE_ID_CMEDIA_CM8338A
#define CMIPCI_CLASS_ID        PCI_CLASS_MULTIMEDIA_AUDIO

#define PCM_CHANNELS 2

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

typedef struct PCMChannelState {
    bool running;
    dma_addr_t base;
    uint32_t dma_size_bytes;
    uint32_t period_bytes;
    uint32_t remaining_bytes;
    uint32_t period_remaining_bytes;
} PCMChannelState;

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t functrl0;
    uint32_t functrl1;
    uint32_t chformat;
    uint32_t int_hldclr;
    uint32_t int_status;
    uint32_t legacy_ctrl;
    uint32_t misc_ctrl;
    uint32_t tdma_position;
    uint8_t mixer0;
    uint8_t mixer21;
    uint8_t sb16_data;
    uint8_t sb16_addr;
    uint8_t mixer1;
    uint8_t mixer2;
    uint8_t aux_vol;
    uint8_t misc;
    uint8_t ac97;
    uint32_t mpu_pci;
    uint32_t fm_pci;
    uint8_t extent_ind;
    uint8_t pll;
    uint32_t ch0_frame1;
    uint32_t ch0_frame2;
    uint32_t ch1_frame1;
    uint32_t ch1_frame2;
    uint32_t ext_misc;

    /* PCM/DMA emulation */
    PCMChannelState pcm[PCM_CHANNELS];
    QEMUTimer pcm_timer;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    if (s->int_status & CM_INTR) {
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

static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /* Simple PCM ring buffer emulation for playback/capture */
    (void)is_write;
    PCIDevice *pdev = PCI_DEVICE(s);
    for (int ch = 0; ch < PCM_CHANNELS; ch++) {
        PCMChannelState *c = &s->pcm[ch];
        if (!c->running || c->dma_size_bytes == 0) {
            continue;
        }

        /* Transfer one period worth of bytes per tick */
        uint32_t todo = c->period_bytes;
        if (todo == 0) {
            todo = 1024; /* fallback */
        }
        if (todo > c->remaining_bytes) {
            todo = c->remaining_bytes;
        }

        if (todo == 0) {
            continue;
        }

        /* We don't care about actual data content; emulate flow. */
        uint8_t buf[256];
        uint32_t offset = c->dma_size_bytes - c->remaining_bytes;
        dma_addr_t addr = c->base + offset;
        uint32_t left = todo;
        while (left) {
            uint32_t chunk = left > sizeof(buf) ? sizeof(buf) : left;
            if (ch == CM_CH_PLAY) {
                /* device reads from guest memory */
                pci_dma_read(pdev, addr, buf, chunk);
            } else {
                /* capture: device writes dummy data into guest buffer */
                memset(buf, 0, chunk);
                pci_dma_write(pdev, addr, buf, chunk);
            }
            addr += chunk;
            left -= chunk;
        }

        c->remaining_bytes -= todo;
        if (c->period_bytes) {
            if (c->period_remaining_bytes <= todo) {
                c->period_remaining_bytes = c->period_bytes;
                /* Set channel interrupt bit */
                if (ch == 0) {
                    s->int_status |= CM_CHINT0;
                } else {
                    s->int_status |= CM_CHINT1;
                }
            } else {
                c->period_remaining_bytes -= todo;
            }
        }

        /* Update remaining count register (frame2 low 16 bits) */
        uint32_t frame2 = (ch == 0) ? s->ch0_frame2 : s->ch1_frame2;
        uint32_t period_cnt = (frame2 >> 16) & 0xFFFF;
        (void)period_cnt;
        uint32_t rem_cnt;
        if (c->remaining_bytes == 0) {
            rem_cnt = 0;
        } else {
            if (c->dma_size_bytes >= c->remaining_bytes) {
                rem_cnt = c->remaining_bytes - 1;
            } else {
                rem_cnt = 0;
            }
        }
        frame2 = (frame2 & 0xFFFF0000u) | (rem_cnt & 0xFFFFu);
        if (ch == 0) {
            s->ch0_frame2 = frame2;
        } else {
            s->ch1_frame2 = frame2;
        }
    }
}

static void pcibase_pcm_timer_cb(void *opaque)
{
    PCIBaseState *s = opaque;
    bool any_running = false;

    pcibase_do_dma(s, true);

    /* If channel interrupts are enabled, raise global interrupt */
    uint32_t mask = 0;
    if (s->int_status & CM_CHINT0) {
        mask |= CM_CH0_INT_EN;
    }
    if (s->int_status & CM_CHINT1) {
        mask |= CM_CH1_INT_EN;
    }
    if (mask & s->int_hldclr) {
        s->int_status |= CM_INTR;
    }

    pcibase_update_irq(s);

    for (int ch = 0; ch < PCM_CHANNELS; ch++) {
        if (s->pcm[ch].running && s->pcm[ch].remaining_bytes > 0) {
            any_running = true;
        }
    }

    if (any_running) {
        /* Schedule next tick after 1ms */
        timer_mod(&s->pcm_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 1);
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)size;
    switch (addr) {
    case CM_REG_FUNCTRL0:
        return s->functrl0;
    case CM_REG_FUNCTRL1:
        return s->functrl1;
    case CM_REG_CHFORMAT:
        return s->chformat;
    case CM_REG_INT_HLDCLR:
        return s->int_hldclr;
    case CM_REG_INT_STATUS:
        return s->int_status;
    case CM_REG_LEGACY_CTRL:
        return s->legacy_ctrl;
    case CM_REG_MISC_CTRL:
        return s->misc_ctrl;
    case CM_REG_TDMA_POSITION:
        return s->tdma_position;
    case CM_REG_MIXER0:
        return s->mixer0;
    case CM_REG_MIXER21:
        return s->mixer21;
    case CM_REG_SB16_DATA:
        return s->sb16_data;
    case CM_REG_SB16_ADDR:
        return s->sb16_addr;
    case CM_REG_MIXER1:
        return s->mixer1;
    case CM_REG_MIXER2:
        return s->mixer2;
    case CM_REG_AUX_VOL:
        return s->aux_vol;
    case CM_REG_MISC:
        return s->misc;
    case CM_REG_AC97:
        return s->ac97;
    case CM_REG_MPU_PCI:
        return s->mpu_pci;
    case CM_REG_FM_PCI:
        return s->fm_pci;
    case CM_REG_EXTENT_IND:
        return s->extent_ind;
    case CM_REG_PLL:
        return s->pll;
    case CM_REG_CH0_FRAME1:
        return s->ch0_frame1;
    case CM_REG_CH0_FRAME2:
        return s->ch0_frame2;
    case CM_REG_CH1_FRAME1:
        return s->ch1_frame1;
    case CM_REG_CH1_FRAME2:
        return s->ch1_frame2;
    case CM_REG_EXT_MISC:
        return s->ext_misc;
    default:
        return 0;
    }
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)size;

    switch (addr) {
    case CM_REG_FUNCTRL0: {
        s->functrl0 = (uint32_t)val;
        /* Update channel running flags based on CHEN bits */
        bool ch0_en = (s->functrl0 & CM_CHEN0) != 0;
        bool ch1_en = (s->functrl0 & CM_CHEN1) != 0;
        s->pcm[0].running = ch0_en;
        s->pcm[1].running = ch1_en;
        /* Start timer if any is running */
        if ((ch0_en && s->pcm[0].dma_size_bytes) || (ch1_en && s->pcm[1].dma_size_bytes)) {
            timer_mod(&s->pcm_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 1);
        }
        break;
    }
    case CM_REG_FUNCTRL1:
        s->functrl1 = (uint32_t)val;
        break;
    case CM_REG_CHFORMAT:
        s->chformat = (uint32_t)val;
        break;
    case CM_REG_INT_HLDCLR: {
        /* HLDCLR register controls interrupt enables; driver also uses W1C sequence here. */
        uint32_t v = (uint32_t)val;
        /* Update enable bits */
        s->int_hldclr = v;
        /* For simplicity, we do not implement complex hold/clear semantics beyond this. */
        break;
    }
    case CM_REG_INT_STATUS:
        /* Driver uses separate clear via INT_HLDCLR; but support direct write to clear bits. */
        s->int_status &= ~((uint32_t)val);
        pcibase_update_irq(s);
        break;
    case CM_REG_LEGACY_CTRL:
        s->legacy_ctrl = (uint32_t)val;
        break;
    case CM_REG_MISC_CTRL:
        s->misc_ctrl = (uint32_t)val;
        /* RESET bit toggling is used for codec reset; we don't model side effects. */
        break;
    case CM_REG_TDMA_POSITION:
        s->tdma_position = (uint32_t)val;
        break;

    case CM_REG_MIXER0:
        s->mixer0 = (uint8_t)val;
        break;
    case CM_REG_MIXER21:
        s->mixer21 = (uint8_t)val;
        break;
    case CM_REG_SB16_ADDR:
        s->sb16_addr = (uint8_t)val;
        break;
    case CM_REG_SB16_DATA:
        s->sb16_data = (uint8_t)val;
        break;
    case CM_REG_MIXER1:
        s->mixer1 = (uint8_t)val;
        break;
    case CM_REG_MIXER2:
        s->mixer2 = (uint8_t)val;
        break;
    case CM_REG_AUX_VOL:
        s->aux_vol = (uint8_t)val;
        break;
    case CM_REG_MISC:
        s->misc = (uint8_t)val;
        break;
    case CM_REG_AC97:
        s->ac97 = (uint8_t)val;
        break;

    case CM_REG_MPU_PCI:
        s->mpu_pci = (uint32_t)val;
        break;
    case CM_REG_FM_PCI:
        s->fm_pci = (uint32_t)val;
        break;
    case CM_REG_EXTENT_IND:
        s->extent_ind = (uint8_t)val;
        break;
    case CM_REG_PLL:
        s->pll = (uint8_t)val;
        break;

    case CM_REG_CH0_FRAME1:
        s->ch0_frame1 = (uint32_t)val;
        s->pcm[0].base = s->ch0_frame1;
        break;
    case CM_REG_CH0_FRAME2: {
        /* low 16 bits: total count-1, high 16 bits: period-1 */
        uint32_t v = (uint32_t)val;
        s->ch0_frame2 = v;
        uint32_t total = (v & 0xFFFFu) + 1;
        uint32_t period = ((v >> 16) & 0xFFFFu) + 1;
        s->pcm[0].dma_size_bytes = total;
        s->pcm[0].period_bytes = period;
        s->pcm[0].remaining_bytes = total;
        s->pcm[0].period_remaining_bytes = period;
        break;
    }
    case CM_REG_CH1_FRAME1:
        s->ch1_frame1 = (uint32_t)val;
        s->pcm[1].base = s->ch1_frame1;
        break;
    case CM_REG_CH1_FRAME2: {
        uint32_t v = (uint32_t)val;
        s->ch1_frame2 = v;
        uint32_t total = (v & 0xFFFFu) + 1;
        uint32_t period = ((v >> 16) & 0xFFFFu) + 1;
        s->pcm[1].dma_size_bytes = total;
        s->pcm[1].period_bytes = period;
        s->pcm[1].remaining_bytes = total;
        s->pcm[1].period_remaining_bytes = period;
        break;
    }
    case CM_REG_EXT_MISC:
        s->ext_misc = (uint32_t)val;
        break;

    default:
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)size;

    /* PIO access mirrors MMIO in this model since BAR0 is I/O space. */
    return pcibase_mmio_read(s, addr, size);
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)size;

    pcibase_mmio_write(s, addr, val, size);
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

    s->functrl0 = 0;
    s->functrl1 = 0;
    s->chformat = 0;
    s->int_hldclr = 0;
    s->int_status = 0;
    s->legacy_ctrl = 0;
    s->misc_ctrl = 0;
    s->tdma_position = 0;
    s->mixer0 = 0;
    s->mixer21 = 0;
    s->sb16_data = 0;
    s->sb16_addr = 0;
    s->mixer1 = 0;
    s->mixer2 = 0;
    s->aux_vol = 0;
    s->misc = 0;
    s->ac97 = 0;
    s->mpu_pci = 0;
    s->fm_pci = 0;
    s->extent_ind = 0;
    s->pll = 0;
    s->ch0_frame1 = 0;
    s->ch0_frame2 = 0;
    s->ch1_frame1 = 0;
    s->ch1_frame2 = 0;
    s->ext_misc = 0;

    for (int i = 0; i < PCM_CHANNELS; i++) {
        s->pcm[i].running = false;
        s->pcm[i].base = 0;
        s->pcm[i].dma_size_bytes = 0;
        s->pcm[i].period_bytes = 0;
        s->pcm[i].remaining_bytes = 0;
        s->pcm[i].period_remaining_bytes = 0;
    }

    timer_del(&s->pcm_timer);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  CMIPCI_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  CMIPCI_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CMIPCI_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization: driver uses BAR0 I/O space */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 0x100;
    s->bar_info[0].name = "cmipci-io";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = "";
    }

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = false;
    s->has_msix = false;

    s->functrl0 = 0;
    s->functrl1 = 0;
    s->chformat = 0;
    s->int_hldclr = 0;
    s->int_status = 0;
    s->legacy_ctrl = 0;
    s->misc_ctrl = 0;
    s->tdma_position = 0;
    s->mixer0 = 0;
    s->mixer21 = 0;
    s->sb16_data = 0;
    s->sb16_addr = 0;
    s->mixer1 = 0;
    s->mixer2 = 0;
    s->aux_vol = 0;
    s->misc = 0;
    s->ac97 = 0;
    s->mpu_pci = 0;
    s->fm_pci = 0;
    s->extent_ind = 0;
    s->pll = 0;
    s->ch0_frame1 = 0;
    s->ch0_frame2 = 0;
    s->ch1_frame1 = 0;
    s->ch1_frame2 = 0;
    s->ext_misc = 0;

    for (int i = 0; i < PCM_CHANNELS; i++) {
        s->pcm[i].running = false;
        s->pcm[i].base = 0;
        s->pcm[i].dma_size_bytes = 0;
        s->pcm[i].period_bytes = 0;
        s->pcm[i].remaining_bytes = 0;
        s->pcm[i].period_remaining_bytes = 0;
    }

    timer_init_ms(&s->pcm_timer, QEMU_CLOCK_VIRTUAL, pcibase_pcm_timer_cb, s);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, 0);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }

    timer_del(&s->pcm_timer);
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "snd_cmipci_pci",
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
