/*
 * QEMU PCI device model for Philips SAA7134 (simplified for driver probe).
 *
 * This model is derived strictly from observed usage patterns in
 * linux/drivers/media/pci/saa7134/saa7134-core.c.
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

#define TYPE_PCIBASE_DEVICE "saa7134_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_PHILIPS 0x1131
#define PCI_DEVICE_ID_PHILIPS_SAA7130 0x7130
#define PCI_DEVICE_ID_PHILIPS_SAA7133 0x7133
#define PCI_DEVICE_ID_PHILIPS_SAA7134 0x7134
#define PCI_DEVICE_ID_PHILIPS_SAA7135 0x7135

#define SAA7134_VENDOR_ID   PCI_VENDOR_ID_PHILIPS
#define SAA7134_DEVICE_ID   PCI_DEVICE_ID_PHILIPS_SAA7130
#define SAA7134_CLASS_ID    PCI_CLASS_MULTIMEDIA_VIDEO

#define SAA7134_GPIO_GPRESCAN                   0x80
#define SAA7134_GPIO_GPMODE3                    0x1B3
#define SAA7134_GPIO_GPMODE0                    0x1B0
#define SAA7134_GPIO_GPSTATUS0                  0x1B4
#define SAA7134_VIDEO_PORT_CTRL6                0x196
#define SAA7134_REGION_ENABLE                   0x004
#define SAA7134_IRQ1                            (0x2c4 >> 2)
#define SAA7134_MAIN_CTRL                       (0x2a8 >> 2)
#define SAA7134_TASK_CONDITIONS(t)              (0x000 + (t))
#define SAA7134_FIELD_HANDLING(t)               (0x001 + (t))
#define TASK_B                                  0x080
#define TASK_A                                  0x040
#define SAA7134_IRQ2                            (0x2c8 >> 2)
#define SAA7134_IRQ_STATUS                      (0x2d0 >> 2)
#define SAA7134_IRQ_REPORT                      (0x2cc >> 2)
#define SAA7134_THRESHOULD                      (0x2a4 >> 2)
#define SAA7134_SPECIAL_MODE                    0x1d0
#define SAA7134_SOURCE_TIMING2                  0x001
#define SAA7134_FIFO_SIZE                       (0x2a0 >> 2)
#define SAA7134_INPUT_MAX                       8
#define SAA7134_STATUS_VIDEO1                   0x11e
#define SAA7134_SYNC_CTRL                       0x108
#define SAA7134_STATUS_VIDEO2                   0x11f
#define SAA7134_ANALOG_IO_SELECT                0x16e
#define SAA7134_I2S_OUTPUT_LEVEL                0x16a
#define SAA7134_I2S_OUTPUT_SELECT               0x169
#define SAA7134_I2S_AUDIO_OUTPUT                0x1c0
#define SAA7133_I2S_AUDIO_CONTROL               0x591
#define SAA7134_I2S_OUTPUT_FORMAT               0x168
#define SAA7134_CHANNEL1_LEVEL                  0x163
#define SAA7134_NICAM_LEVEL_ADJUST              0x166
#define SAA7134_CHANNEL2_LEVEL                  0x164
#define SAA7134_VIDEO_PORT_CTRL0                0x190
#define SAA7134_VIDEO_PORT_CTRL5                0x195
#define SAA7134_VIDEO_PORT_CTRL3                0x193
#define SAA7134_VIDEO_PORT_CTRL4                0x194
#define SAA7134_VIDEO_PORT_CTRL2                0x192
#define SAA7134_VIDEO_PORT_CTRL1                0x191
#define SAA7134_VIDEO_PORT_CTRL8                0x198
#define SAA7134_VIDEO_PORT_CTRL7                0x197
#define SAA7134_TS_DMA2                         0x1a6
#define SAA7134_TS_PARALLEL                     0x1a0
#define SAA7134_TS_DMA0                         0x1a4
#define SAA7134_TS_SERIAL1                      0x1a3
#define SAA7134_TS_PARALLEL_SERIAL              0x1a1
#define SAA7134_TS_DMA1                         0x1a5
#define TS_PACKET_SIZE                          188
#define SAA7134_AUDIO_CLOCK1                    0x171
#define SAA7134_NICAM_ERROR_LOW                 0x148
#define SAA7134_AUDIO_CLOCK2                    0x172
#define SAA7134_AUDIO_PLL_CTRL                  0x173
#define SAA7134_AUDIO_CLOCK0                    0x170
#define SAA7134_NICAM_ERROR_HIGH                0x149
#define SAA7134_MONITOR_SELECT                  0x160
#define SAA7134_FM_DEMATRIX                     0x162
#define SAA7134_STEREO_DAC_OUTPUT_SELECT        0x167
#define SAA7134_SOURCE_TIMING1                  0x000
#define SAA7134_MISC_VGATE_MSB                  0x117
#define SAA7134_ANALOG_IN_CTRL2                 0x103
#define SAA7134_DEC_LUMA_BRIGHT                 0x10a
#define SAA7134_CHROMA_CTRL2                    0x110
#define SAA7134_MODE_DELAY_CTRL                 0x111
#define SAA7134_VGATE_START                     0x115
#define SAA7134_INCR_DELAY                      0x101
#define SAA7134_CHROMA_GAIN                     0x10f
#define SAA7134_ANALOG_ADC                      0x114
#define SAA7134_HSYNC_STOP                      0x107
#define SAA7134_RAW_DATA_GAIN                   0x118
#define SAA7134_LUMA_CTRL                       0x109
#define SAA7134_ANALOG_IN_CTRL1                 0x102
#define SAA7134_ANALOG_IN_CTRL3                 0x104
#define SAA7134_CHROMA_CTRL1                    0x10e
#define SAA7134_DEC_CHROMA_HUE                  0x10d
#define SAA7134_RAW_DATA_OFFSET                 0x119
#define SAA7134_DEC_LUMA_CONTRAST               0x10b
#define SAA7134_DEC_CHROMA_SATURATION           0x10c
#define SAA7134_VGATE_STOP                      0x116
#define SAA7134_ANALOG_IN_CTRL4                 0x105
#define SAA7134_HSYNC_START                     0x106
#define SAA7134_SIF_SAMPLE_FREQ                 0x16d
#define SAA7134_AUDIO_FORMAT_CTRL               0x15b
#define SAA7134_AUDIO_MUTE_CTRL                 0x16c
#define SAA7134_IDENT_SIF                       0x145
#define SAA7134_AUDIO_STATUS                    0x143
#define SAA7134_NICAM_STATUS                    0x142
#define SAA7134_LEVEL_READOUT1                  0x146
#define SAA7134_AUDIO_CLOCKS_PER_FIELD1         0x175
#define SAA7134_AUDIO_CLOCKS_PER_FIELD0         0x174
#define SAA7134_DEMODULATOR                     0x14b
#define SAA7134_DCXO_IDENT_CTRL                 0x14a
#define SAA7134_FM_DEEMPHASIS                   0x161
#define SAA7134_AUDIO_CLOCKS_PER_FIELD2         0x176
#define SAA7134_NICAM_CONFIG                    0x165
#define SAA7134_CARRIER2_FREQ0                  0x154
#define SAA7134_CARRIER1_FREQ0                  0x150
#define SAA7135_DSP_RWSTATE                     0x580
#define SAA7134_DATA_PATH(t)                    (0x002 + (t))
#define SAA7134_OFMT_VIDEO_A                    0x300
#define SAA7134_RS_PITCH(n)                     ((0x208 >> 2) + 4 * (n))
#define SAA7134_RS_BA1(n)                       ((0x200 >> 2) + 4 * (n))
#define SAA7134_RS_CONTROL(n)                   ((0x20c >> 2) + 4 * (n))
#define SAA7134_RS_BA2(n)                       ((0x204 >> 2) + 4 * (n))
#define SAA7133_ANALOG_IO_SELECT                (0x594 >> 2)
#define SAA7135_DSP_RWCLEAR                     0x586
#define SAA7134_VIDEO_V_STOP1(t)                (0x01a + (t))
#define SAA7134_VIDEO_PIXELS2(t)                (0x01d + (t))
#define SAA7134_V_PHASE_OFFSET0(t)              (0x034 + (t))
#define SAA7134_V_PHASE_OFFSET3(t)              (0x037 + (t))
#define SAA7134_VIDEO_H_START1(t)               (0x014 + (t))
#define SAA7134_V_PHASE_OFFSET1(t)              (0x035 + (t))
#define SAA7134_H_SCALE_INC1(t)                 (0x02c + (t))
#define SAA7134_VIDEO_H_START2(t)               (0x015 + (t))
#define SAA7134_VIDEO_H_STOP2(t)                (0x017 + (t))
#define SAA7134_VIDEO_V_START2(t)               (0x019 + (t))
#define SAA7134_VIDEO_LINES1(t)                 (0x01e + (t))
#define SAA7134_VIDEO_V_STOP2(t)                (0x01b + (t))
#define SAA7134_VIDEO_H_STOP1(t)                (0x016 + (t))
#define SAA7134_VIDEO_PIXELS1(t)                (0x01c + (t))
#define SAA7134_V_PHASE_OFFSET2(t)              (0x036 + (t))
#define SAA7134_VIDEO_V_START1(t)               (0x018 + (t))
#define SAA7134_VIDEO_LINES2(t)                 (0x01f + (t))
#define SAA7134_H_SCALE_INC2(t)                 (0x02d + (t))
#define SAA7134_I2C_ATTR_STATUS                 0x180
#define SAA7134_I2C_DATA                        0x181
#define SAA7134_FIR_PREFILTER_CTRL(t)           (0x023 + (t))
#define SAA7134_ACC_LENGTH(t)                   (0x021 + (t))
#define SAA7134_H_PRESCALE(t)                   (0x020 + (t))
#define SAA7134_LEVEL_CTRL(t)                   (0x022 + (t))
#define SAA7134_CHROMA_SATURATION(t)            (0x026 + (t))
#define SAA7134_LUMA_CONTRAST(t)                (0x025 + (t))
#define SAA7134_V_SCALE_RATIO2(t)               (0x031 + (t))
#define SAA7134_V_FILTER(t)                     (0x032 + (t))
#define SAA7134_V_SCALE_RATIO1(t)               (0x030 + (t))
#define SAA7134_LUMA_BRIGHT(t)                  (0x024 + (t))

/* New interrupt / control bit definitions from supplementary driver source */
#define SAA7134_IRQ1_INTE_RA0_1               (1 << 1)
#define SAA7134_IRQ1_INTE_RA0_0               (1 << 0)

#define SAA7134_MAIN_CTRL_TE0                 (1 << 0)
#define SAA7134_MAIN_CTRL_TE1                 (1 << 1)
#define SAA7134_MAIN_CTRL_TE2                 (1 << 2)
#define SAA7134_MAIN_CTRL_TE3                 (1 << 3)
#define SAA7134_MAIN_CTRL_TE4                 (1 << 4)
#define SAA7134_MAIN_CTRL_TE5                 (1 << 5)
#define SAA7134_MAIN_CTRL_TE6                 (1 << 6)
#define SAA7134_MAIN_CTRL_VPLLE               (1 << 15)
#define SAA7134_MAIN_CTRL_APLLE               (1 << 14)
#define SAA7134_MAIN_CTRL_EXOSC               (1 << 13)
#define SAA7134_MAIN_CTRL_EVFE1               (1 << 12)
#define SAA7134_MAIN_CTRL_EVFE2               (1 << 11)
#define SAA7134_MAIN_CTRL_ESFE                (1 << 10)
#define SAA7134_MAIN_CTRL_EBDAC               (1 << 8)

#define SAA7134_IRQ_REPORT_DONE_RA0           (1 << 0)
#define SAA7134_IRQ_REPORT_DONE_RA2           (1 << 2)
#define SAA7134_IRQ_REPORT_DONE_RA3           (1 << 3)
#define SAA7134_IRQ_REPORT_RDCAP              (1 << 7)
#define SAA7134_IRQ_REPORT_INTL               (1 << 8)
#define SAA7134_IRQ_REPORT_GPIO16             (1 << 14)
#define SAA7134_IRQ_REPORT_GPIO18             (1 << 15)
#define SAA7134_IRQ_REPORT_PE                 (1 << 5)

#define SAA7134_IRQ2_INTE_PE                  (1 << 1)
#define SAA7134_IRQ2_INTE_GPIO16_P            (1 << 10)
#define SAA7134_IRQ2_INTE_GPIO16_N            (1 << 11)
#define SAA7134_IRQ2_INTE_GPIO18_P            (1 << 12)
#define SAA7134_IRQ2_INTE_GPIO18_N            (1 << 13)
#define SAA7134_IRQ2_INTE_DEC3                (1 << 5)
#define SAA7134_IRQ2_INTE_DEC2                (1 << 4)
#define SAA7134_IRQ2_INTE_DEC1                (1 << 3)
#define SAA7134_IRQ2_INTE_DEC0                (1 << 2)
#define SAA7134_IRQ2_INTE_AR                  (1 << 0)

#define SAA7134_PGTABLE_SIZE                  4096

#define SAA7134_BAR_INDEX_LMMIO   0
#define SAA7134_BAR_INDEX_BMMIO   1

#define SAA7134_LMMIO_SIZE        (256 * 1024)
#define SAA7134_BMMIO_SIZE        (256 * 1024)


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
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t main_ctrl;
    uint32_t irq1;
    uint32_t irq2;
    uint32_t irq_status;
    uint32_t irq_report;

    /* Basic register arrays to cover lmmio accesses used by driver */
    uint32_t lmmio_regs[0x400]; /* 0x400 * 4 = 4KB, enough for used regs */
};


/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* Level-triggered INTx: assert when any report bit is set and enabled */
    if ((s->irq_report & s->irq1) || (s->irq_report & s->irq2)) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* Device-initiated DMA logic based on driver access patterns
 *
 * The core driver builds page tables and sets up DMA, but the
 * actual register formats and sequencing are not shown in this
 * excerpt. We therefore do not implement active DMA transfers
 * here; we only provide register storage. */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    (void)s;
    (void)pdev;
    (void)is_write;
}

/* Helper to access the lmmio_regs array using byte address (BAR0) */
static inline uint32_t *pcibase_lmmio_ptr(PCIBaseState *s, hwaddr addr)
{
    /* BAR0 is memory mapped; driver uses saa_readl/saa_writel with
     * word offsets like (0x2c4 >> 2). In the card code, SAA7134_IRQ1
     * is defined as (0x2c4 >> 2) and passed to saa_writel directly,
     * so addr here will already be the lmmio index * 4.
     */
    hwaddr index = addr >> 2; /* 32-bit registers */
    if (index >= G_N_ELEMENTS(s->lmmio_regs)) {
        return NULL;
    }
    return &s->lmmio_regs[index];
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Only 32-bit accesses are used by this driver (saa_readl/writeb). */
    if (size != 1 && size != 2 && size != 4) {
        return 0;
    }

    /* Handle BAR0 lmmio as an array of 32-bit regs. */
    uint32_t *preg = pcibase_lmmio_ptr(s, addr & ~0x3ULL);
    if (!preg) {
        return 0;
    }

    uint32_t reg_index = (addr & ~0x3ULL) >> 2;

    switch (reg_index) {
    case SAA7134_IRQ1:
        val = s->irq1;
        break;
    case SAA7134_IRQ2:
        val = s->irq2;
        break;
    case SAA7134_IRQ_STATUS:
        val = s->irq_status;
        break;
    case SAA7134_IRQ_REPORT:
        val = s->irq_report;
        break;
    case SAA7134_MAIN_CTRL:
        val = s->main_ctrl;
        break;
    default:
        /* Generic backing store for other registers */
        val = *preg;
        break;
    }

    /* Byte/word reads: return the corresponding part of the 32-bit reg. */
    if (size == 1) {
        unsigned shift = (addr & 0x3) * 8;
        val = (val >> shift) & 0xff;
    } else if (size == 2) {
        unsigned shift = (addr & 0x2) * 8;
        val = (val >> shift) & 0xffff;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 1 && size != 2 && size != 4) {
        return;
    }

    uint32_t *preg = pcibase_lmmio_ptr(s, addr & ~0x3ULL);
    if (!preg) {
        return;
    }

    uint32_t reg_index = (addr & ~0x3ULL) >> 2;
    uint32_t cur = *preg;
    uint32_t newv = cur;

    /* For writeb/w, modify only the targeted byte/halfword of the 32-bit reg */
    if (size == 4) {
        newv = (uint32_t)val;
    } else if (size == 2) {
        unsigned shift = (addr & 0x2) * 8;
        uint32_t mask = 0xffffu << shift;
        newv = (cur & ~mask) | (((uint32_t)val & 0xffffu) << shift);
    } else if (size == 1) {
        unsigned shift = (addr & 0x3) * 8;
        uint32_t mask = 0xffu << shift;
        newv = (cur & ~mask) | (((uint32_t)val & 0xffu) << shift);
    }

    switch (reg_index) {
    case SAA7134_IRQ1:
        /* IRQ1 is the primary interrupt enable/mask register used with
         * saa_writel(SAA7134_IRQ1, irq); Reads return stored value.
         */
        s->irq1 = newv;
        *preg = newv;
        pcibase_update_irq(s);
        break;

    case SAA7134_IRQ2:
        /* Secondary interrupt enables. */
        s->irq2 = newv;
        *preg = newv;
        pcibase_update_irq(s);
        break;

    case SAA7134_IRQ_REPORT:
        /* Driver writes back the value it just read to acknowledge
         * (W1C-like behavior). We clear the bits written as 1.
         */
        if (size == 4) {
            uint32_t w = (uint32_t)val;
            s->irq_report &= ~w;
        } else if (size == 2) {
            unsigned shift = (addr & 0x2) * 8;
            uint32_t w = ((uint32_t)val & 0xffffu) << shift;
            s->irq_report &= ~w;
        } else { /* size == 1 */
            unsigned shift = (addr & 0x3) * 8;
            uint32_t w = ((uint32_t)val & 0xffu) << shift;
            s->irq_report &= ~w;
        }
        *preg = s->irq_report;
        pcibase_update_irq(s);
        break;

    case SAA7134_IRQ_STATUS:
        /* Status is read-only from device viewpoint in this model; the
         * core uses it only for information, not for control. Accept
         * writes but just update shadow to keep readbacks consistent.
         */
        s->irq_status = newv;
        *preg = newv;
        break;

    case SAA7134_MAIN_CTRL:
        /* Main control register (enables various engines).
         * We only store the value; no further behavior is modeled.
         */
        s->main_ctrl = newv;
        *preg = newv;
        break;

    default:
        /* Some specific bytes are used with saa_writeb in the core. */
        if ((addr & ~0x3ULL) == (SAA7134_REGION_ENABLE & ~0x3)) {
            /* Region enable is written as byte. Keep backing store only. */
            *preg = newv;
        } else if ((addr & ~0x3ULL) == (SAA7134_SPECIAL_MODE & ~0x3)) {
            *preg = newv;
        } else if ((addr & ~0x3ULL) == (SAA7134_SOURCE_TIMING2 & ~0x3)) {
            *preg = newv;
        } else if ((addr & ~0x3ULL) == (SAA7134_FIFO_SIZE & ~0x3)) {
            *preg = newv;
        } else if ((addr & ~0x3ULL) == (SAA7134_THRESHOULD & ~0x3)) {
            *preg = newv;
        } else if ((addr & ~0x3ULL) == (SAA7134_GPIO_GPMODE0 & ~0x3)) {
            *preg = newv;
        } else if ((addr & ~0x3ULL) == (SAA7134_GPIO_GPSTATUS0 & ~0x3)) {
            *preg = newv;
        } else if ((addr & ~0x3ULL) == (SAA7134_GPIO_GPMODE3 & ~0x3)) {
            *preg = newv;
        } else if ((addr & ~0x3ULL) == (SAA7134_VIDEO_PORT_CTRL6 & ~0x3)) {
            *preg = newv;
        } else {
            /* Generic store for any other register accesses. */
            *preg = newv;
        }
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    (void)s;
    (void)addr;
    (void)size;

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    (void)s;
    (void)addr;
    (void)val;
    (void)size;
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

    s->main_ctrl = 0;
    s->irq1 = 0;
    s->irq2 = 0;
    s->irq_status = 0;
    s->irq_report = 0;

    memset(s->lmmio_regs, 0, sizeof(s->lmmio_regs));
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  SAA7134_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  SAA7134_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, SAA7134_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 2;
    s->bar_info[0].index = SAA7134_BAR_INDEX_LMMIO;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    s->bar_info[0].size  = SAA7134_LMMIO_SIZE;
    s->bar_info[0].name  = "saa7134-lmmio";

    s->bar_info[1].index = SAA7134_BAR_INDEX_BMMIO;
    s->bar_info[1].type  = BAR_TYPE_MMIO;
    s->bar_info[1].size  = SAA7134_BMMIO_SIZE;
    s->bar_info[1].name  = "saa7134-bmmio";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = false;
    s->has_msix = false;

    s->main_ctrl = 0;
    s->irq1 = 0;
    s->irq2 = 0;
    s->irq_status = 0;
    s->irq_report = 0;

    memset(s->lmmio_regs, 0, sizeof(s->lmmio_regs));
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

    (void)s;
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "saa7134_pci",
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
