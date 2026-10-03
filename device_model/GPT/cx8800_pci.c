/*
 * QEMU PCI device model for cx8800 (minimal behavioral emulation
 * sufficient for Linux cx88-video driver probing and basic operation).
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
#include "linux/pci_regs.h"

#define TYPE_PCIBASE_DEVICE "cx8800_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define CX88_VENDOR_ID 0x14f1
#define CX88_DEVICE_ID 0x8800
#define CX88_PCI_CLASS PCI_CLASS_MULTIMEDIA_VIDEO

#define MO_DEVICE_STATUS    0x310100
#define MO_AFECFG_IO        0x35C04C
#define AUD_CTL             0x32058c
#define EN_I2SIN_ENABLE     0x00008000
#define MO_GP0_IO           0x350010
#define MO_FILTER_EVEN      0x31015c
#define MO_GP2_IO           0x350018
#define AUD_I2SCNTL         0x3205ec
#define MO_GP1_IO           0x350014
#define MO_INPUT_FORMAT     0x310104
#define MO_GP3_IO           0x35001C
#define MO_FILTER_ODD       0x310160
#define MO_PCI_INTMSK       0x200040
#define MO_COLOR_CTRL       0x310184
#define VID_CAPTURE_CONTROL 0x310180
#define MO_VIDY_GPCNTRL     0x31C030
#define MO_DEV_CNTRL2       0x200034
#define MO_VID_INTMSK       0x200050
#define GP_COUNT_CONTROL_RESET 0x3
#define PCI_INT_VIDINT      (1 << 0)
#define MO_VID_DMACNTRL     0x31C040
#define RISC_JUMP           0x70000000
#define RISC_IRQ1           0x01000000
#define RISC_CNT_INC        0x00010000
#define MO_VID_INTSTAT      0x200054
#define MO_VIDY_GPCNT       0x31C020
#define MO_VBI_GPCNT        0x31C02C
#define MO_PCI_INTSTAT      0x200044
#define ColorFormatRGB16    0x0022
#define ColorFormatRGB24    0x0011
#define ColorFormatRGB15    0x0033
#define FORMAT_FLAGS_PACKED 0x01
#define ColorFormatYUY2     0x0044
#define ColorFormatRGB32    0x0000
#define ColorFormatY8       0x0066
#define MO_UV_SATURATION    0x310114
#define MO_HTOTAL           0x310120
#define MO_HUE              0x310118
#define MO_CONTR_BRIGHT     0x310110
#define SHADOW_AUD_VOL_CTL  1
#define AUD_BAL_CTL         0x320598
#define SHADOW_AUD_BAL_CTL  2
#define AUD_VOL_CTL         0x320594
#define SHADOW_MAX          3
#define AUD_STATUS          0x320590
#define AUD_I2SINPUTCNTL    0x320120
#define MO_HSCALE_ODD       0x310150
#define MO_HSCALE_EVEN      0x31014c
#define MO_VACTIVE_EVEN     0x310144
#define MO_HACTIVE_EVEN     0x31013c
#define MO_VSCALE_EVEN      0x310154
#define MO_HDELAY_ODD       0x310128
#define MO_HDELAY_EVEN      0x310124
#define MO_VDELAY_EVEN      0x310130
#define MO_VSCALE_ODD       0x310158
#define MO_VACTIVE_ODD      0x310148
#define MO_VDELAY_ODD       0x31012c
#define MO_HACTIVE_ODD      0x310140
#define MO_AGC_BURST        0x31010c
#define MO_SUB_STEP_DR      0x31017c
#define VideoFormatPAL60    0x8
#define VideoFormatNTSC     0x1
#define VideoFormatNTSC443  0x3
#define VideoFormatPALNC    0x7
#define MO_OUTPUT_FORMAT    0x310164
#define MO_VBI_PACKET       0x310188
#define VideoFormatSECAM    0x9
#define MO_SUB_STEP         0x310178
#define VideoFormatPAL      0x4
#define MO_SCONV_REG        0x310170
#define VideoFormatNTSCJapan 0x2
#define VideoFormatPALM     0x5
#define VideoFormatPALN     0x6
#define PCI_INT_IR_SMPINT   (1 << 18)
#define MO_AUD_DMACNTRL     0x32C040
#define MO_TS_INTMSK        0x200070
#define MO_GPHST_DMACNTRL   0x38C040
#define MO_TS_DMACNTRL      0x33C040
#define MO_GPHST_INTMSK     0x200090
#define MO_AUD_INTMSK       0x200060
#define MO_VIP_INTMSK       0x200080
#define MO_VIP_DMACNTRL     0x34C040
#define MO_AGC_BACK_VBI     0x310200
#define MO_PDMA_DTHRSH      0x200010
#define MO_INT1_STAT        0x35C064
#define MO_SRST_IO          0x35C05C
#define MO_PDMA_STHRSH      0x200000
#define MO_AGC_SYNC_TIP1    0x310208
#define EN_FMRADIO_EN_RDS   0x00000200
#define AUD_IIR1_1_SEL      0x320158
#define AUD_IIR3_1_SHIFT    0x3201cc
#define AUD_DN1_FREQ        0x320278
#define AUD_CRDC1_SHIFT     0x320310
#define AUD_DCOC_0_SHIFT_IN1 0x32032c
#define AUD_DBX_WBE_GAIN    0x320504
#define AUD_DCOC_0_SHIFT_IN0 0x320328
#define AUD_DCOC_1_SHIFT_IN0 0x320338
#define AUD_IIR2_3_SEL      0x3201a8
#define AUD_IIR3_0_SEL      0x3201c0
#define AUD_DEEMPH1_A1      0x320470
#define AUD_CRDC1_SRC_SEL   0x32030c
#define AUD_DN1_AFC         0x320280
#define AUD_IIR1_3_SEL      0x320168
#define AUD_OUT0_SEL        0x320490
#define AUD_IIR1_0_SEL      0x320150
#define AUD_DEEMPH1_A0      0x320468
#define AUD_DCOC_1_SRC      0x320330
#define AUD_IIR1_4_SEL      0x320170
#define AUD_IIR3_2_SHIFT    0x3201d4
#define AUD_IIR2_2_SEL      0x3201a0
#define AUD_IIR3_0_SHIFT    0x3201c4
#define AUD_IIR2_2_SHIFT    0x3201a4
#define AUD_DEEMPH1_SHIFT   0x320460
#define AUD_DBX_SE_GAIN     0x320508
#define AUD_CORDIC_SHIFT_1  0x320314
#define AUD_DCOC1_SHIFT     0x320334
#define AUD_IIR2_1_SHIFT    0x32019c
#define AUD_DMD_RA_DDS      0x3205bc
#define AUD_DN0_FREQ        0x320274
#define AUD_OUT1_SEL        0x320498
#define AUD_DEEMPH1_G0      0x320464
#define AUD_IIR2_1_SEL      0x320198
#define AUD_OUT1_SHIFT      0x32049c
#define AUD_DCOC2_SHIFT     0x320344
#define AUD_DN2_SRC_SEL     0x320298
#define AUD_RDSQ_SHIFT      0x3204ac
#define AUD_IIR2_3_SHIFT    0x3201ac
#define AUD_IIR3_1_SEL      0x3201c8
#define AUD_RDSQ_SEL        0x3204a8
#define AUD_DEEMPH1_B1      0x320474
#define AUD_DN2_FREQ        0x32028c
#define AUD_IIR1_2_SEL      0x320160
#define AUD_POLY0_DDS_CONSTANT 0x320270
#define AUD_IIR1_4_SHIFT    0x32017c
#define AUD_IIR3_2_SEL      0x3201d0
#define AUD_DCOC_PASS_IN    0x320350
#define AUD_DCOC_1_SHIFT_IN1 0x32033c
#define AUD_DCOC_0_SRC      0x320320
#define AUD_DN1_SRC_SEL     0x320284
#define AUD_RDSI_SEL        0x3204a0
#define AUD_DEEMPH1_B0      0x32046c
#define AUD_DEEMPH0_SRC_SEL 0x320440
#define AUD_DN2_AFC         0x320294
#define AUD_RDSI_SHIFT      0x3204a4
#define AUD_POLYPH80SCALEFAC 0x3205b8
#define AUD_AFE_12DB_EN     0x320628
#define AUD_DN2_SHFT        0x32029c
#define AUD_DBX_IN_GAIN     0x320500
#define AUD_DEEMPH1_SRC_SEL 0x32045c
#define AUD_ERRLOGPERIOD_R  0x32054c
#define AUD_PLL_INT         0x320608
#define AUD_RATE_THRES_DMD  0x3205d0
#define AUD_RATE_ADJ2       0x3205dc
#define AUD_RATE_ADJ1       0x3205d8
#define AUD_ERRINTRPTTHSHLD3_R 0x320558
#define AUD_PDF_DDS_CNST_BYTE0 0x320d03
#define AUD_PDF_DDS_CNST_BYTE2 0x320d01
#define AUD_PLL_DDS         0x320604
#define AUD_PHACC_FREQ_8LSB 0x320d2b
#define AUD_RATE_ADJ4       0x3205e4
#define AUD_PHACC_FREQ_8MSB 0x320d2a
#define AUD_RATE_ADJ5       0x3205e8
#define AUD_DEEMPHDENOM1_R  0x320544
#define AUD_DEEMPHDENOM2_R  0x320548
#define AUD_DEEMPHNUMER1_R  0x32053c
#define AUD_ERRINTRPTTHSHLD1_R 0x320550
#define AUD_QAM_MODE        0x320d04
#define AUD_PDF_DDS_CNST_BYTE1 0x320d02
#define AUD_PLL_FRAC        0x32060c
#define AUD_START_TIMER     0x3205b0
#define AUD_RATE_ADJ3       0x3205e0
#define AUD_ERRINTRPTTHSHLD2_R 0x320554
#define AUD_DEEMPHNUMER2_R  0x320540
#define AUD_DEEMPHGAIN_R    0x320538
#define AUD_DEEMPH0_A0      0x32044c
#define AUD_FM_MODE_ENABLE  0x320258
#define AAGC_DEF            0x32013c
#define AAGC_HYST           0x320134
#define AUD_C2_LO_THR       0x3203dc
#define AUD_C1_LO_THR       0x3203d4
#define AUD_IIR1_0_SHIFT    0x320154
#define AUD_DEEMPH0_B0      0x320450
#define AUD_PILOT_BQD_1_K0  0x320380
#define AUD_PILOT_BQD_2_K3  0x3203a0
#define AUD_PILOT_BQD_1_K1  0x320384
#define AUD_DEEMPH0_A1      0x320454
#define AUD_DEEMPH0_G0      0x320448
#define AUD_PILOT_BQD_2_K0  0x320394
#define AUD_C1_UP_THR       0x3203d0
#define AUD_C2_UP_THR       0x3203d8
#define AUD_PILOT_BQD_2_K2  0x32039c
#define AUD_IIR2_0_SHIFT    0x320194
#define AUD_THR_FR          0x3203c0
#define AUD_IIR1_2_SHIFT    0x320164
#define AUD_PILOT_BQD_2_K4  0x3203a4
#define AUD_DEEMPH0_B1      0x320458
#define AUD_DEEMPH0_SHIFT   0x320444
#define AUD_IIR1_3_SHIFT    0x32016c
#define AUD_PILOT_BQD_2_K1  0x320398
#define AUD_PILOT_BQD_1_K3  0x32038c
#define AUD_CORDIC_SHIFT_0  0x320308
#define AUD_DCOC0_SHIFT     0x320324
#define AUD_IIR1_1_SHIFT    0x32015c
#define AUD_MODE_CHG_TIMER  0x3205b4
#define AAGC_GAIN           0x320138
#define AUD_PILOT_BQD_1_K4  0x320390
#define AUD_CRDC0_SRC_SEL   0x320300
#define AUD_IIR2_0_SEL      0x320190
#define AUD_PILOT_BQD_1_K2  0x320388
#define AUD_SOFT_RESET      0x320108
#define AUD_INIT            0x320100
#define AUD_INIT_LD         0x320104
#define AUD_NICAM_STATUS2   0x320560
#define EN_DAC_ENABLE       0x00001000
#define EN_I2SOUT_ENABLE    0x00002000
#define AUD_BAUDRATE        0x320124
#define AUD_I2SOUTPUTCNTL   0x320128
#define RISC_SOL            0x08000000
#define RISC_RESYNC         0x80008000
#define RISC_WRITE          0x10000000
#define RISC_EOL            0x04000000
#define MO_PLL_REG          0x310168
#define RISC_READ           0x90000000
#define RISC_READC          0xA0000000
#define RISC_WRITECM        0xC0000000
#define RISC_WRITEC         0x50000000
#define RISC_WRITERM        0xB0000000
#define RISC_SKIP           0x20000000
#define RISC_SYNC           0x80000000
#define RISC_WRITECR        0xD0000000
#define MO_SAMPLE_IO        0x35C058
#define PCI_INT_RISC_WR_BERRINT (1 << 11)
#define PCI_INT_IPB_DMA_BERRINT (1 << 15)
#define PCI_INT_DST_DMA_BERRINT (1 << 14)
#define PCI_INT_BRDG_BERRINT (1 << 12)
#define PCI_INT_RISC_RD_BERRINT (1 << 10)
#define PCI_INT_SRC_DMA_BERRINT (1 << 13)
#define MO_VBOS_CONTROL     0x3101a8
#define MO_VBI_GPCNTRL      0x31C03C
#define MO_AUD_INTSTAT      0x200064
#define MO_AUDD_LNGTH       0x32C048
#define MO_AUDR_LNGTH       0x32C04C
#define CX88X_EN_VSFX       0x04
#define CX88X_DEVCTRL       0x40
#define CX88X_EN_TBFX       0x02
#define MO_I2C              0x368000


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

    /* Hardware Register Shadows (The 'Identity' of the device) */
    /* Simplified 24-bit register space backing store. */
    uint32_t *regs;
    uint32_t regs_size; /* in bytes */

    /* Interrupt-related state */
    uint32_t pci_int_status;
    uint32_t pci_int_mask;
    uint32_t vid_int_status;
    uint32_t vid_int_mask;

    /* Video counters used by ISR */
    uint32_t vidy_gpcnt;
    uint32_t vbi_gpcnt;

    /* Simple periodic timer to generate video interrupts when enabled */
    QEMUTimer vid_timer;
};


/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool assert_irq = false;

    /* PCI_INT_VIDINT is bit 0, matched to driver. */
    if (s->pci_int_status & s->pci_int_mask & PCI_INT_VIDINT) {
        assert_irq = true;
    }

    pci_set_irq(pdev, assert_irq ? 1 : 0);
}

/* Device-initiated DMA logic based on driver access patterns
 * The cx88-video driver programs RISC DMA engines via helper functions
 * (cx88_sram_channel_setup, cx88_risc_buffer, etc.) that only touch
 * system memory descriptors and card registers. The actual DMA engine
 * is not explicitly manipulated in this file apart from enabling bits
 * in MO_VID_DMACNTRL and friends. Since no detailed descriptor format
 * is provided here, we do not implement bus mastering and merely
 * provide a stub to keep structure. */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    (void)s;
    (void)is_write;
    /* No DMA engine behavior is implemented due to missing descriptor
     * layout in the provided source. */
}

/* Helper for 24-bit register space: map full hwaddr to 24-bit, word-aligned. */
static inline hwaddr cx88_reg_index(hwaddr addr)
{
    /* Registers are 32-bit and aligned; low 2 bits ignored. Space is 24-bit. */
    return (addr & 0x00FFFFFCu) >> 2;
}

static inline bool cx88_reg_valid(PCIBaseState *s, hwaddr addr)
{
    hwaddr offset = addr & 0x00FFFFFCu;
    return offset + 4 <= s->regs_size;
}

static uint32_t cx88_reg_read(PCIBaseState *s, hwaddr addr)
{
    if (!cx88_reg_valid(s, addr)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "cx8800: read invalid reg 0x%08" HWADDR_PRIx "\n", addr);
        return 0;
    }
    return s->regs[cx88_reg_index(addr)];
}

static void cx88_reg_write(PCIBaseState *s, hwaddr addr, uint32_t val)
{
    if (!cx88_reg_valid(s, addr)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "cx8800: write invalid reg 0x%08" HWADDR_PRIx " = 0x%08x\n",
                      addr, val);
        return;
    }
    s->regs[cx88_reg_index(addr)] = val;
}

static void cx88_vid_timer_cb(void *opaque)
{
    PCIBaseState *s = opaque;

    /* Only generate interrupts if video DMA and capture are enabled. */
    uint32_t vid_dmacntrl = cx88_reg_read(s, MO_VID_DMACNTRL);
    uint32_t cap_ctrl = cx88_reg_read(s, VID_CAPTURE_CONTROL);

    if ((vid_dmacntrl & 0x11) && (cap_ctrl & 0x06)) {
        /* Increment counters similar to hardware GP counters. */
        s->vidy_gpcnt++;
        s->vbi_gpcnt++;
        cx88_reg_write(s, MO_VIDY_GPCNT, s->vidy_gpcnt);
        cx88_reg_write(s, MO_VBI_GPCNT, s->vbi_gpcnt);

        /* Set video interrupt status bits: bit0 for Y, bit3 for VBI
         * as used by cx8800_vid_irq(). */
        s->vid_int_status |= 0x01; /* Y */
        s->vid_int_status |= 0x08; /* VBI */
        cx88_reg_write(s, MO_VID_INTSTAT, s->vid_int_status);

        /* If any enabled video IRQ bits are set, raise PCI_INT_VIDINT. */
        if (s->vid_int_status & s->vid_int_mask) {
            s->pci_int_status |= PCI_INT_VIDINT;
            cx88_reg_write(s, MO_PCI_INTSTAT, s->pci_int_status);
            pcibase_update_irq(s);
        }
    }

    /* Reschedule timer to periodically emulate field interrupts. */
    timer_mod(&s->vid_timer,
              qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 40); /* ~25 fps */
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size != 4) {
        /* Driver uses 32-bit accesses (cx_read/cx_write), but
         * debug ioctl may use others; return zero for mismatched sizes. */
        return 0;
    }

    switch (addr & 0x00FFFFFFu) {
    case MO_PCI_INTSTAT:
        /* Return current PCI interrupt status. */
        val = s->pci_int_status;
        break;
    case MO_PCI_INTMSK:
        val = s->pci_int_mask;
        break;
    case MO_VID_INTSTAT:
        val = s->vid_int_status;
        break;
    case MO_VID_INTMSK:
        val = s->vid_int_mask;
        break;
    case MO_VIDY_GPCNT:
        val = s->vidy_gpcnt;
        break;
    case MO_VBI_GPCNT:
        val = s->vbi_gpcnt;
        break;
    default:
        /* General register read from 24-bit space. */
        val = cx88_reg_read(s, addr);
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        return;
    }

    uint32_t v = (uint32_t)val;
    hwaddr off = addr & 0x00FFFFFFu;

    switch (off) {
    case MO_PCI_INTMSK:
        s->pci_int_mask = v;
        cx88_reg_write(s, MO_PCI_INTMSK, v);
        pcibase_update_irq(s);
        break;
    case MO_PCI_INTSTAT:
        /* Write-1-to-clear semantics, as driver uses cx_write(MO_PCI_INTSTAT, status). */
        s->pci_int_status &= ~v;
        cx88_reg_write(s, MO_PCI_INTSTAT, s->pci_int_status);
        pcibase_update_irq(s);
        break;
    case MO_VID_INTMSK:
        s->vid_int_mask = v;
        cx88_reg_write(s, MO_VID_INTMSK, v);
        break;
    case MO_VID_INTSTAT:
        /* W1C for video interrupt status. */
        s->vid_int_status &= ~v;
        cx88_reg_write(s, MO_VID_INTSTAT, s->vid_int_status);
        /* If no more video status bits, clear PCI_INT_VIDINT. */
        if ((s->vid_int_status & s->vid_int_mask) == 0) {
            s->pci_int_status &= ~PCI_INT_VIDINT;
            cx88_reg_write(s, MO_PCI_INTSTAT, s->pci_int_status);
            pcibase_update_irq(s);
        }
        break;
    case MO_VIDY_GPCNTRL:
        /* Reset counter if requested. Driver writes GP_COUNT_CONTROL_RESET. */
        cx88_reg_write(s, MO_VIDY_GPCNTRL, v);
        if (v & GP_COUNT_CONTROL_RESET) {
            s->vidy_gpcnt = 0;
            cx88_reg_write(s, MO_VIDY_GPCNT, s->vidy_gpcnt);
        }
        break;
    case MO_VBI_GPCNTRL:
        /* For completeness, reset VBI counter similarly. */
        cx88_reg_write(s, MO_VBI_GPCNTRL, v);
        if (v & GP_COUNT_CONTROL_RESET) {
            s->vbi_gpcnt = 0;
            cx88_reg_write(s, MO_VBI_GPCNT, s->vbi_gpcnt);
        }
        break;
    case MO_VID_DMACNTRL:
        /* Store DMA control; used by timer to know if DMA is enabled. */
        cx88_reg_write(s, MO_VID_DMACNTRL, v);
        break;
    case VID_CAPTURE_CONTROL:
        cx88_reg_write(s, VID_CAPTURE_CONTROL, v);
        break;
    default:
        /* Generic write to register space. */
        cx88_reg_write(s, addr, v);
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    (void)opaque;
    (void)addr;
    (void)size;
    uint64_t val = 0;

    /* Driver does not use any PIO space in the provided source. */
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    (void)opaque;
    (void)addr;
    (void)val;
    (void)size;
    /* No PIO usage in driver; ignore. */
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

    /* Clear register array */
    if (s->regs && s->regs_size) {
        memset(s->regs, 0, s->regs_size);
    }

    s->pci_int_status = 0;
    s->pci_int_mask = 0;
    s->vid_int_status = 0;
    s->vid_int_mask = 0;
    s->vidy_gpcnt = 0;
    s->vbi_gpcnt = 0;

    /* Program initial register values that driver may rely on. */
    cx88_reg_write(s, MO_PCI_INTSTAT, 0);
    cx88_reg_write(s, MO_PCI_INTMSK, 0);
    cx88_reg_write(s, MO_VID_INTSTAT, 0);
    cx88_reg_write(s, MO_VID_INTMSK, 0);
    cx88_reg_write(s, MO_VIDY_GPCNT, 0);
    cx88_reg_write(s, MO_VBI_GPCNT, 0);
    cx88_reg_write(s, MO_VIDY_GPCNTRL, 0);
    cx88_reg_write(s, MO_VBI_GPCNTRL, 0);
    cx88_reg_write(s, MO_VID_DMACNTRL, 0);
    cx88_reg_write(s, VID_CAPTURE_CONTROL, 0);
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

    /* Allocate simple 24-bit register space backing store (0x01000000 bytes). */
    s->regs_size = 0x01000000; /* 16 MiB covers 0x000000-0xFFFFFF */
    s->regs = g_new0(uint32_t, s->regs_size / 4);

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  CX88_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  CX88_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CX88_PCI_CLASS );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization: expose a single MMIO BAR large enough for the
     * 24-bit register space. The driver uses BAR 0 (pci_resource_start(pci_dev, 0)).
     */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = s->regs_size;
    s->bar_info[0].name = "cx8800-mmio";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No MSI/MSI-X explicitly used by driver in provided context */

    /* Initialize interrupt-related registers. */
    s->pci_int_status = 0;
    s->pci_int_mask = 0;
    s->vid_int_status = 0;
    s->vid_int_mask = 0;

    cx88_reg_write(s, MO_PCI_INTSTAT, 0);
    cx88_reg_write(s, MO_PCI_INTMSK, 0);
    cx88_reg_write(s, MO_VID_INTSTAT, 0);
    cx88_reg_write(s, MO_VID_INTMSK, 0);

    /* Setup video interrupt emulation timer. */
    timer_init_ms(&s->vid_timer, QEMU_CLOCK_VIRTUAL, cx88_vid_timer_cb, s);
    timer_mod(&s->vid_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 40);

    /* No explicit DMA mask or additional timers in provided context */
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    timer_del(&s->vid_timer);

    if (s->regs) {
        g_free(s->regs);
        s->regs = NULL;
        s->regs_size = 0;
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
    .name = "cx8800_pci",
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
