/*
 * QEMU PCI Bt8xx (bttv) minimal device model
 * Target: QEMU 8.2.10
 *
 * This model only implements behavior that is explicitly visible
 * in the provided Linux driver (bttv-driver.c) and related macros
 * that were pre-populated in the Phase-1 template.
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

#define TYPE_PCIBASE_DEVICE "bttv_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* ------------------------------------------------------------------ */
/* Register / macro definitions copied from Phase-1 template          */
/* ------------------------------------------------------------------ */
#define PCI_DEVICE_ID_BT848     0x350
#define PCI_DEVICE_ID_BT849     0x351
#define PCI_DEVICE_ID_BT878     0x36e
#define PCI_DEVICE_ID_BT879     0x36f
#define PCI_DEVICE_ID_FUSION879 0x36c

#define BT848_DSTATUS          0x000
#define BT848_IFORM            0x004
#define BT848_E_CROP           0x00C
#define BT848_E_VDELAY_LO      0x010
#define BT848_E_VACTIVE_LO     0x014
#define BT848_E_HDELAY_LO      0x018
#define BT848_E_HACTIVE_LO     0x01C
#define BT848_E_HSCALE_HI      0x020
#define BT848_E_HSCALE_LO      0x024
#define BT848_BRIGHT           0x028
#define BT848_E_CONTROL        0x02C
#define BT848_CONTRAST_LO      0x030
#define BT848_SAT_U_LO         0x034
#define BT848_SAT_V_LO         0x038
#define BT848_HUE              0x03C
#define BT848_E_SCLOOP         0x040
#define BT848_WC_UP            0x044
#define BT848_OFORM            0x048
#define BT848_E_VSCALE_HI      0x04C
#define BT848_E_VSCALE_LO      0x050
#define BT848_E_VTC            0x06C
#define BT848_ADC              0x068
#define BT848_ADELAY           0x060
#define BT848_BDELAY           0x064
#define BT848_TGLB             0x080
#define BT848_TGCTRL           0x084
#define BT848_O_CROP           0x08C
#define BT848_O_VDELAY_LO      0x090
#define BT848_O_VSCALE_HI      0x0CC
#define BT848_COLOR_FMT        0x0D4
#define BT848_COLOR_CTL        0x0D8
#define BT848_WC_DOWN          0x078
#define BT848_VBI_PACK_SIZE    0x0E0
#define BT848_VBI_PACK_DEL     0x0E4
#define BT848_PLL_F_LO         0x0F0
#define BT848_PLL_F_HI         0x0F4
#define BT848_PLL_XCI          0x0F8
#define BT848_DVSIF            0x0FC
#define BT848_INT_STAT         0x100
#define BT848_INT_MASK         0x104
#define BT848_GPIO_DMA_CTL     0x10C
#define BT848_I2C              0x110
#define BT848_RISC_STRT_ADD    0x114
#define BT848_GPIO_OUT_EN      0x118
#define BT848_GPIO_REG_INP     0x11C
#define BT848_RISC_COUNT       0x120
#define BT848_GPIO_DATA        0x200
#define BT848_VTOTAL_LO        0x0B0
#define BT848_VTOTAL_HI        0x0B4

#define BT848_INT_FMTCHG  (1<<0)
#define BT848_INT_VSYNC   (1<<1)
#define BT848_INT_HLOCK   (1<<4)
#define BT848_INT_VPRES   (1<<5)
#define BT848_INT_I2CDONE (1<<8)
#define BT848_INT_GPINT   (1<<9)
#define BT848_INT_RISCI   (1<<11)
#define BT848_INT_FDSR    (1<<14)
#define BT848_INT_OCERR   (1<<18)
#define BT848_INT_SCERR   (1<<19)
#define BT848_INT_ETBF    (1<<23)
#define BT848_INT_RACK    (1<<25)

#define BT848_DSTATUS_PLOCK (1<<2)
#define BT848_DSTATUS_NUML  (1<<4)
#define BT848_DSTATUS_HLOC  (1<<6)
#define BT848_DSTATUS_PRES  (1<<7)

#define BT848_IFORM_NORM       7
#define BT848_IFORM_AUTO       0
#define BT848_IFORM_NTSC       1
#define BT848_IFORM_NTSC_J     2
#define BT848_IFORM_PAL_BDGHI  3
#define BT848_IFORM_PAL_M      4
#define BT848_IFORM_PAL_N      5
#define BT848_IFORM_SECAM      6
#define BT848_IFORM_PAL_NC     7
#define BT848_IFORM_XT0        (1<<3)
#define BT848_IFORM_XT1        (2<<3)
#define BT848_IFORM_XTBOTH     (3<<3)
#define BT848_IFORM_XTAUTO     (3<<3)

#define BT848_CONTROL_COMP   (1<<6)
#define BT848_CONTROL_LDEC   (1<<5)

#define BT848_CAP_CTL                 0x0DC
#define BT848_CAP_CTL_CAPTURE_EVEN    (1<<0)
#define BT848_CAP_CTL_CAPTURE_ODD     (1<<1)
#define BT848_CAP_CTL_CAPTURE_VBI_EVEN (1<<2)
#define BT848_CAP_CTL_CAPTURE_VBI_ODD  (1<<3)

#define BT848_GPIO_DMA_CTL_RISC_ENABLE (1<<1)
#define BT848_GPIO_DMA_CTL_FIFO_ENABLE (1<<0)
#define BT848_GPIO_DMA_CTL_GPINTI      (1<<14)
#define BT848_GPIO_DMA_CTL_GPINTC      (1<<15)
#define BT848_GPIO_DMA_CTL_PKTP_32     (3<<2)
#define BT848_GPIO_DMA_CTL_PLTP1_16    (2<<4)
#define BT848_GPIO_DMA_CTL_PLTP23_16   (2<<6)
#define BT848_GPIO_DMA_CTL_GPCLKMODE   (1<<10)

#define BT848_SCLOOP_CKILL   (1<<5)
#define BT848_SCLOOP_CAGC    (1<<6)

#define BT848_VSCALE_INT     (1<<5)
#define BT848_VSCALE_COMB    (1<<6)

#define BT848_ADC_CRUSH      (1<<0)
#define BT848_ADC_AGC_EN     (1<<4)
#define BT848_ADC_RESERVED   (2<<6)

#define BT848_OFORM_CORE32   (3<<5)
#define BT848_OFORM_RANGE    (1<<7)

#define BT848_COLOR_CTL_GAMMA (1<<4)

#define BT848_RISC_WRITE       (0x01U<<28)
#define BT848_RISC_SKIP        (0x02U<<28)
#define BT848_RISC_WRITEC      (0x05U<<28)
#define BT848_RISC_JUMP        (0x07U<<28)
#define BT848_RISC_SYNC        (0x08U<<28)
#define BT848_RISC_WRITE123    (0x09U<<28)
#define BT848_RISC_SKIP123     (0x0aU<<28)
#define BT848_RISC_WRITE1S23   (0x0bU<<28)
#define BT848_RISC_TOP         2
#define BT848_RISC_VBI         4
#define BT848_RISC_VIDEO       1
#define BT848_RISC_IRQ         (1U<<24)
#define BT848_RISC_SOL         (1U<<27)
#define BT848_RISC_EOL         (1U<<26)
#define BT848_RISC_RESYNC      (1<<15)

#define BT848_INT_RISCS_TOP    (BT848_RISC_TOP << 28)
#define BT848_INT_RISCS_VBI    (BT848_RISC_VBI << 28)
#define BT848_INT_RISCS_VIDEO  (BT848_RISC_VIDEO << 28)

#define BT848_FIFO_STATUS_VRE  0x04
#define BT848_FIFO_STATUS_VRO  0x0c
#define BT848_FIFO_STATUS_FM1  0x06
#define BT848_FIFO_STATUS_FM3  0x0e

#define BT848_COLOR_FMT_Y8          0x66
#define BT848_COLOR_FMT_RGB8        0x77
#define BT848_COLOR_FMT_RGB15       0x33
#define BT848_COLOR_FMT_RGB16       0x22
#define BT848_COLOR_FMT_RGB24       0x11
#define BT848_COLOR_FMT_RGB32       0x00
#define BT848_COLOR_FMT_YUY2        0x44
#define BT848_COLOR_FMT_YCrCb422    0x88
#define BT848_COLOR_FMT_YCrCb411    0x99
#define BT848_COLOR_FMT_RAW         0xee

#define FORMAT_FLAGS_DITHER  0x01
#define FORMAT_FLAGS_PACKED  0x02
#define FORMAT_FLAGS_PLANAR  0x04
#define FORMAT_FLAGS_RAW     0x08

#define BTTV_MAX        32
#define BTTV_MAX_FBUF   0x208000

#define RAW_BPL         1024
#define RAW_LINES       640
#define VBI_BPL         2048
#define VBI_DEFLINES    16

#define RISC_SLOT_O_VBI   4
#define RISC_SLOT_O_FIELD 6
#define RISC_SLOT_E_VBI   10
#define RISC_SLOT_E_FIELD 12
#define RISC_SLOT_LOOP    14

#define MAX_HDELAY  (0x3FF & -2)
#define MAX_HACTIVE (0x3FF & -4)

#define BTTV_NORMS ( \
        V4L2_STD_PAL    | V4L2_STD_PAL_N | \
        V4L2_STD_PAL_Nc | V4L2_STD_SECAM | \
        V4L2_STD_NTSC   | V4L2_STD_PAL_M | \
        V4L2_STD_PAL_60)

#define BTTV_TIMEOUT msecs_to_jiffies(500)
#define UNSET        (-1U)

#define VBI_RESOURCES  (RESOURCE_VBI)
#define VIDEO_RESOURCES (RESOURCE_VIDEO_READ | RESOURCE_VIDEO_STREAM)

#define RESOURCE_VBI           4
#define RESOURCE_VIDEO_STREAM  2
#define RESOURCE_VIDEO_READ    8

#define BT878_DEVCTRL     0x40
#define BT878_EN_TBFX     0x02
#define BT878_EN_VSFX     0x04

#define BT878_I2C_MODE    (1<<7)
#define BT878_I2C_NOSTOP  (1<<5)
#define BT878_I2C_NOSTART (1<<4)

#define BT848_I2C_W3B   (1<<2)
#define BT848_I2C_SCL   (1<<1)
#define BT848_I2C_SYNC  (1<<3)
#define BT848_I2C_SDA   (1<<0)

#define NO_SVHS  15
#define PLL_NONE 0
#define PLL_28   1
#define PLL_35   2
#define PLL_14   3

/* A few v4l2 std macros used but actual values are irrelevant here
 * since we don't emulate the video pipeline; driver treats them as
 * constants and they don't touch hardware. They are kept only because
 * they were in the template. The QEMU device does not use them. */

typedef uint64_t v4l2_std_id;

typedef uint32_t __u32;

#define v4l2_fourcc(a, b, c, d) \
    ((__u32)(a) | ((__u32)(b) << 8) | ((__u32)(c) << 16) | ((__u32)(d) << 24))

#define V4L2_STD_PAL_BG     (0)
#define V4L2_STD_PAL_DK     (0)
#define V4L2_STD_PAL_H          ((v4l2_std_id)0x00000008)
#define V4L2_STD_PAL_I          ((v4l2_std_id)0x00000010)
#define V4L2_STD_SECAM_B        ((v4l2_std_id)0x00010000)
#define V4L2_STD_SECAM_G        ((v4l2_std_id)0x00040000)
#define V4L2_STD_SECAM_H        ((v4l2_std_id)0x00080000)
#define V4L2_STD_SECAM_DK   (0)
#define V4L2_STD_SECAM_L        ((v4l2_std_id)0x00400000)
#define V4L2_STD_SECAM_LC       ((v4l2_std_id)0x00800000)
#define V4L2_STD_NTSC_M         ((v4l2_std_id)0x00001000)
#define V4L2_STD_NTSC_M_JP      ((v4l2_std_id)0x00002000)
#define V4L2_STD_NTSC_M_KR      ((v4l2_std_id)0x00008000)

#define V4L2_STD_PAL            (0)
#define V4L2_STD_PAL_N          ((v4l2_std_id)0x00000200)
#define V4L2_STD_PAL_Nc         ((v4l2_std_id)0x00000400)
#define V4L2_STD_SECAM          (0)
#define V4L2_STD_NTSC           (0)
#define V4L2_STD_PAL_M          ((v4l2_std_id)0x00000100)
#define V4L2_STD_PAL_60         ((v4l2_std_id)0x00000800)

#define V4L2_PIX_FMT_GREY       v4l2_fourcc('G', 'R', 'E', 'Y')
#define V4L2_PIX_FMT_HI240      v4l2_fourcc('H', 'I', '2', '4')
#define V4L2_PIX_FMT_RGB555     v4l2_fourcc('R', 'G', 'B', 'O')
#define V4L2_PIX_FMT_RGB555X    v4l2_fourcc('R', 'G', 'B', 'Q')
#define V4L2_PIX_FMT_RGB565     v4l2_fourcc('R', 'G', 'B', 'P')
#define V4L2_PIX_FMT_RGB565X    v4l2_fourcc('R', 'G', 'B', 'R')
#define V4L2_PIX_FMT_BGR24      v4l2_fourcc('B', 'G', 'R', '3')
#define V4L2_PIX_FMT_BGR32      v4l2_fourcc('B', 'G', 'R', '4')
#define V4L2_PIX_FMT_RGB32      v4l2_fourcc('R', 'G', 'B', '4')
#define V4L2_PIX_FMT_YUYV       v4l2_fourcc('Y', 'U', 'Y', 'V')
#define V4L2_PIX_FMT_UYVY       v4l2_fourcc('U', 'Y', 'V', 'Y')
#define V4L2_PIX_FMT_YUV422P    v4l2_fourcc('4', '2', '2', 'P')
#define V4L2_PIX_FMT_YUV420     v4l2_fourcc('Y', 'U', '1', '2')
#define V4L2_PIX_FMT_YVU420     v4l2_fourcc('Y', 'V', '1', '2')
#define V4L2_PIX_FMT_YUV411P    v4l2_fourcc('4', '1', '1', 'P')
#define V4L2_PIX_FMT_YUV410     v4l2_fourcc('Y', 'U', 'V', '9')
#define V4L2_PIX_FMT_YVU410     v4l2_fourcc('Y', 'V', 'U', '9')

#define BT848_PLL_X            (1<<7)
#define FORMATS ARRAY_SIZE(formats)
#define BTTV_BOARD_VOODOOTV_FM             0x44
#define BTTV_BOARD_VOODOOTV_200           0x93
#define SAA6588_CMD_READ    _IOR('R', 3, int)
#define SAA6588_CMD_POLL    _IOR('R', 4, int)
#define SAA6588_CMD_CLOSE   _IOW('R', 2, int)
#define V4L2_IN_ST_NO_SIGNAL   0x00000002
#define V4L2_IN_ST_NO_H_LOCK   0x00000100
#define V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC    0x00002000
#define V4L2_FIELD_HAS_BOTH(field) \
    ((field) == V4L2_FIELD_INTERLACED ||\
     (field) == V4L2_FIELD_INTERLACED_TB ||\
     (field) == V4L2_FIELD_INTERLACED_BT ||\
     (field) == V4L2_FIELD_SEQ_TB ||\
     (field) == V4L2_FIELD_SEQ_BT)
#define V4L2_CAP_VIDEO_CAPTURE        0x00000001
#define V4L2_CAP_READWRITE              0x01000000
#define V4L2_CAP_STREAMING              0x04000000
#define V4L2_CAP_DEVICE_CAPS            0x80000000
#define V4L2_CAP_VBI_CAPTURE        0x00000010
#define V4L2_CAP_RADIO            0x00040000
#define V4L2_CAP_RDS_CAPTURE        0x00000100
#define V4L2_CAP_TUNER            0x00010000
#define V4L2_CAP_HW_FREQ_SEEK        0x00000400
#define V4L2_SEL_TGT_CROP        0x0000
#define V4L2_SEL_TGT_CROP_DEFAULT    0x0001
#define V4L2_SEL_TGT_CROP_BOUNDS    0x0002
#define V4L2_INPUT_TYPE_CAMERA        2
#define V4L2_INPUT_TYPE_TUNER        1
#define TVAUDIO_INPUT_TUNER  0
#define TVAUDIO_INPUT_EXTERN 2
#define TVAUDIO_INPUT_RADIO  1
#define TVAUDIO_INPUT_INTERN 3
#define MSP_INPUT_DEFAULT MSP_INPUT(MSP_IN_SCART1, MSP_IN_TUNER1, \
                    MSP_DSP_IN_TUNER, MSP_DSP_IN_TUNER)
#define MSP_OUTPUT_DEFAULT (MSP_SC_TO_SCART1(MSP_SC_IN_SCART3) | \
                MSP_SC_TO_SCART2(MSP_SC_IN_DSP_SCART1))
/* O_NONBLOCK is already provided by system headers; avoid redefinition. */

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
/* Device State                                                        */
/* ------------------------------------------------------------------ */
struct PCIBaseState {
    PCIDevice parent_obj;

    /* BAR memory regions */
    MemoryRegion bar_regions[6];

    /* optional linear backing used as register file */
    uint8_t *mmio_backing;
    size_t mmio_backing_size;

    /* BAR table */
    BARInfo bar_info[6];
    int num_bars;

    /* interrupt state feature flags */
    bool has_msi;
    bool has_msix;

    /* simple interrupt state */
    qemu_irq irq;

    /* cached registers used in driver logic */
    uint32_t reg_dstatus;
    uint32_t reg_iform;
    uint32_t reg_e_crop;
    uint32_t reg_e_vdelay_lo;
    uint32_t reg_o_crop;
    uint32_t reg_o_vdelay_lo;
    uint32_t reg_bright;
    uint32_t reg_hue;
    uint32_t reg_contrast_lo;
    uint32_t reg_sat_u_lo;
    uint32_t reg_sat_v_lo;
    uint32_t reg_e_control;
    uint32_t reg_o_control;
    uint32_t reg_e_scloop;
    uint32_t reg_o_scloop;
    uint32_t reg_wc_up;
    uint32_t reg_wc_down;
    uint32_t reg_oform;
    uint32_t reg_color_fmt;
    uint32_t reg_color_ctl;
    uint32_t reg_vbi_pack_size;
    uint32_t reg_vbi_pack_del;
    uint32_t reg_pll_f_lo;
    uint32_t reg_pll_f_hi;
    uint32_t reg_pll_xci;
    uint32_t reg_dvsif;
    uint32_t reg_int_stat;
    uint32_t reg_int_mask;
    uint32_t reg_gpio_dma_ctl;
    uint32_t reg_i2c;
    uint32_t reg_risc_strt_add;
    uint32_t reg_gpio_out_en;
    uint32_t reg_gpio_reg_inp;
    uint32_t reg_risc_count;
    uint32_t reg_gpio_data;
    uint32_t reg_vtotal_lo;
    uint32_t reg_vtotal_hi;
    uint32_t reg_e_vscale_hi;
    uint32_t reg_e_vscale_lo;

    /* simple notion of RISC/DMA running */
    bool risc_running;
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

/* ------------------------------------------------------------------ */
/* MemoryRegionOps                                                     */
/* ------------------------------------------------------------------ */
static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
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
/* Helper: register a BAR (MMIO or PIO)                               */
/* ------------------------------------------------------------------ */
static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, bi->size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

/* ------------------------------------------------------------------ */
/* Helper: raise/lower legacy INTx according to INT_STAT & MASK       */
/* ------------------------------------------------------------------ */
static void bttv_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t pending = s->reg_int_stat & s->reg_int_mask;

    if (pending) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* ------------------------------------------------------------------ */
/* MMIO / PIO handlers                                                */
/* ------------------------------------------------------------------ */

static uint32_t bttv_mmio_read_reg(PCIBaseState *s, uint32_t addr)
{
    switch (addr) {
    case BT848_DSTATUS:
        return s->reg_dstatus;
    case BT848_IFORM:
        return s->reg_iform;
    case BT848_E_CROP:
        return s->reg_e_crop;
    case BT848_E_VDELAY_LO:
        return s->reg_e_vdelay_lo;
    case BT848_O_CROP:
        return s->reg_o_crop;
    case BT848_O_VDELAY_LO:
        return s->reg_o_vdelay_lo;
    case BT848_BRIGHT:
        return s->reg_bright;
    case BT848_HUE:
        return s->reg_hue;
    case BT848_CONTRAST_LO:
        return s->reg_contrast_lo;
    case BT848_SAT_U_LO:
        return s->reg_sat_u_lo;
    case BT848_SAT_V_LO:
        return s->reg_sat_v_lo;
    case BT848_E_CONTROL:
        return s->reg_e_control;
    case BT848_E_SCLOOP:
        return s->reg_e_scloop;
    case BT848_WC_UP:
        return s->reg_wc_up;
    case BT848_OFORM:
        return s->reg_oform;
    case BT848_E_VSCALE_HI:
        return s->reg_e_vscale_hi;
    case BT848_E_VSCALE_LO:
        return s->reg_e_vscale_lo;
    case BT848_COLOR_FMT:
        return s->reg_color_fmt;
    case BT848_COLOR_CTL:
        return s->reg_color_ctl;
    case BT848_WC_DOWN:
        return s->reg_wc_down;
    case BT848_VBI_PACK_SIZE:
        return s->reg_vbi_pack_size;
    case BT848_VBI_PACK_DEL:
        return s->reg_vbi_pack_del;
    case BT848_PLL_F_LO:
        return s->reg_pll_f_lo;
    case BT848_PLL_F_HI:
        return s->reg_pll_f_hi;
    case BT848_PLL_XCI:
        return s->reg_pll_xci;
    case BT848_DVSIF:
        return s->reg_dvsif;
    case BT848_INT_STAT:
        return s->reg_int_stat;
    case BT848_INT_MASK:
        return s->reg_int_mask;
    case BT848_GPIO_DMA_CTL:
        return s->reg_gpio_dma_ctl;
    case BT848_I2C:
        return s->reg_i2c;
    case BT848_RISC_STRT_ADD:
        return s->reg_risc_strt_add;
    case BT848_GPIO_OUT_EN:
        return s->reg_gpio_out_en;
    case BT848_GPIO_REG_INP:
        return s->reg_gpio_reg_inp;
    case BT848_RISC_COUNT:
        /* driver uses this as a debug value; we just return reg */
        return s->reg_risc_count;
    case BT848_GPIO_DATA:
        return s->reg_gpio_data;
    case BT848_VTOTAL_LO:
        return s->reg_vtotal_lo;
    case BT848_VTOTAL_HI:
        return s->reg_vtotal_hi;
    default:
        break;
    }

    /* Fallback to backing array if present */
    if (s->mmio_backing && addr + 4 <= s->mmio_backing_size) {
        uint32_t v;
        memcpy(&v, s->mmio_backing + addr, 4);
        return v;
    }

    return 0;
}

static void bttv_mmio_write_reg(PCIBaseState *s, uint32_t addr, uint32_t val)
{
    switch (addr) {
    case BT848_DSTATUS:
        /* driver uses this to clear PLOCK in set_pll(); emulate clear bits */
        s->reg_dstatus &= ~val;
        break;
    case BT848_IFORM:
        s->reg_iform = val;
        break;
    case BT848_E_CROP:
        s->reg_e_crop = val;
        break;
    case BT848_E_VDELAY_LO:
        s->reg_e_vdelay_lo = val;
        break;
    case BT848_O_CROP:
        s->reg_o_crop = val;
        break;
    case BT848_O_VDELAY_LO:
        s->reg_o_vdelay_lo = val;
        break;
    case BT848_BRIGHT:
        s->reg_bright = val;
        break;
    case BT848_HUE:
        s->reg_hue = val;
        break;
    case BT848_CONTRAST_LO:
        s->reg_contrast_lo = val;
        break;
    case BT848_SAT_U_LO:
        s->reg_sat_u_lo = val;
        break;
    case BT848_SAT_V_LO:
        s->reg_sat_v_lo = val;
        break;
    case BT848_E_CONTROL:
        s->reg_e_control = val;
        break;
    case BT848_E_SCLOOP:
        s->reg_e_scloop = val;
        break;
    case BT848_WC_UP:
        s->reg_wc_up = val;
        break;
    case BT848_OFORM:
        s->reg_oform = val;
        break;
    case BT848_E_VSCALE_HI:
        s->reg_e_vscale_hi = val;
        break;
    case BT848_E_VSCALE_LO:
        s->reg_e_vscale_lo = val;
        break;
    case BT848_COLOR_FMT:
        s->reg_color_fmt = val;
        break;
    case BT848_COLOR_CTL:
        s->reg_color_ctl = val;
        break;
    case BT848_WC_DOWN:
        s->reg_wc_down = val;
        break;
    case BT848_VBI_PACK_SIZE:
        s->reg_vbi_pack_size = val;
        break;
    case BT848_VBI_PACK_DEL:
        s->reg_vbi_pack_del = val;
        break;
    case BT848_PLL_F_LO:
        s->reg_pll_f_lo = val;
        break;
    case BT848_PLL_F_HI:
        s->reg_pll_f_hi = val;
        break;
    case BT848_PLL_XCI:
        s->reg_pll_xci = val;
        break;
    case BT848_DVSIF:
        s->reg_dvsif = val;
        break;
    case BT848_INT_STAT:
        /* write-1-to-clear semantics */
        s->reg_int_stat &= ~val;
        bttv_update_irq(s);
        break;
    case BT848_INT_MASK:
        s->reg_int_mask = val;
        bttv_update_irq(s);
        break;
    case BT848_GPIO_DMA_CTL:
        s->reg_gpio_dma_ctl = val;
        /* if RISC/FIFO enable bits set and RISC_STRT_ADD configured,
         * we pretend DMA/RISC is running. No real DMA is emulated.
         */
        s->risc_running = (val & (BT848_GPIO_DMA_CTL_RISC_ENABLE | BT848_GPIO_DMA_CTL_FIFO_ENABLE)) != 0;
        break;
    case BT848_I2C:
        s->reg_i2c = val;
        /* driver expects an I2C done interrupt: when I2C register is
         * written, we can set INT_STAT I2CDONE and trigger irq.
         */
        s->reg_int_stat |= BT848_INT_I2CDONE;
        bttv_update_irq(s);
        break;
    case BT848_RISC_STRT_ADD:
        s->reg_risc_strt_add = val;
        break;
    case BT848_GPIO_OUT_EN:
        s->reg_gpio_out_en = val;
        break;
    case BT848_GPIO_REG_INP:
        s->reg_gpio_reg_inp = val;
        break;
    case BT848_RISC_COUNT:
        s->reg_risc_count = val;
        break;
    case BT848_GPIO_DATA:
        s->reg_gpio_data = val;
        break;
    case BT848_VTOTAL_LO:
        s->reg_vtotal_lo = val;
        break;
    case BT848_VTOTAL_HI:
        s->reg_vtotal_hi = val;
        break;
    default:
        /* store in backing array if present */
        if (s->mmio_backing && addr + 4 <= s->mmio_backing_size) {
            memcpy(s->mmio_backing + addr, &val, 4);
        }
        break;
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = 0;

    if (size != 1 && size != 2 && size != 4) {
        return 0;
    }

    val = bttv_mmio_read_reg(s, addr & ~0x3u);

    /* align / size handling: we only support 32-bit aligned regs, but
     * allow sub-width reads by returning low bytes.
     */
    if ((addr & 3) != 0) {
        val >>= (addr & 3) * 8;
    }

    if (size == 1) {
        val &= 0xff;
    } else if (size == 2) {
        val &= 0xffff;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t wval;

    if (size != 1 && size != 2 && size != 4) {
        return;
    }

    /* For simplicity we only implement naturally aligned 32-bit writes
     * as the driver uses btwrite/btor/btand with 32-bit values.
     */
    if (size != 4 || (addr & 3)) {
        /* fall back to simple backing store if present */
        if (s->mmio_backing && addr + size <= s->mmio_backing_size) {
            memcpy(s->mmio_backing + addr, &val, size);
        }
        return;
    }

    wval = (uint32_t)val;
    bttv_mmio_write_reg(s, addr, wval);
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* The driver does not use I/O port BARs, only MMIO, so we return 0. */
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* No PIO behavior required by the driver. */
}

/* ------------------------------------------------------------------ */
/* Reset                                                              */
/* ------------------------------------------------------------------ */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    pci_device_reset(pdev);

    if (s->mmio_backing && s->mmio_backing_size) {
        memset(s->mmio_backing, 0, s->mmio_backing_size);
    }

    /* Initialize register defaults which the driver assumes after reset */
    s->reg_dstatus      = 0;
    s->reg_iform        = BT848_IFORM_XTAUTO | BT848_IFORM_AUTO;
    s->reg_e_crop       = 0;
    s->reg_e_vdelay_lo  = 0;
    s->reg_o_crop       = 0;
    s->reg_o_vdelay_lo  = 0;
    s->reg_bright       = 0;
    s->reg_hue          = 0;
    s->reg_contrast_lo  = 0;
    s->reg_sat_u_lo     = 0;
    s->reg_sat_v_lo     = 0;
    s->reg_e_control    = 0;
    s->reg_o_control    = 0;
    s->reg_e_scloop     = 0;
    s->reg_o_scloop     = 0;
    s->reg_wc_up        = 0;
    s->reg_oform        = 0;
    s->reg_color_fmt    = 0;
    s->reg_color_ctl    = BT848_COLOR_CTL_GAMMA;
    s->reg_wc_down      = 0;
    s->reg_vbi_pack_size = 0;
    s->reg_vbi_pack_del = 0;
    s->reg_pll_f_lo     = 0;
    s->reg_pll_f_hi     = 0;
    s->reg_pll_xci      = 0;
    s->reg_dvsif        = 0;
    s->reg_int_stat     = 0;
    s->reg_int_mask     = 0;
    s->reg_gpio_dma_ctl = 0;
    s->reg_i2c          = 0;
    s->reg_risc_strt_add = 0;
    s->reg_gpio_out_en  = 0;
    s->reg_gpio_reg_inp = 0;
    s->reg_risc_count   = 0;
    s->reg_gpio_data    = 0;
    s->reg_vtotal_lo    = 0;
    s->reg_vtotal_hi    = 0;
    s->reg_e_vscale_hi  = 0;
    s->reg_e_vscale_lo  = 0;
    s->risc_running     = false;

    bttv_update_irq(s);
}

/* ------------------------------------------------------------------ */
/* DMA initialize (called from realize)                               */
/* ------------------------------------------------------------------ */
static void pcibase_dma_device_realize(PCIDevice *pdev, Error **errp)
{
    /* The driver configures DMA via RISC descriptors in system memory
     * and by programming BT848_RISC_STRT_ADD, GPIO_DMA_CTL, CAP_CTL.
     * Actual DMA transfer format is complex and not required for probe
     * and basic register I/O, so we do not implement real DMA here.
     */
    (void)pdev;
    (void)errp;
}

/* ------------------------------------------------------------------ */
/* PCI config space access (class-level hooks)                         */
/* ------------------------------------------------------------------ */
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    uint32_t val = pci_default_read_config(pdev, addr, len);

    switch (len) {
    case 1: val &= 0xFF; break;
    case 2: val &= 0xFFFF; break;
    case 4: default: break;
    }
    return val;
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    if (addr >= PCI_BASE_ADDRESS_0 && addr <= PCI_BASE_ADDRESS_5) {
        pci_default_write_config(pdev, addr, val, len);
        return;
    }

    pci_default_write_config(pdev, addr, val, len);
}

/* ------------------------------------------------------------------ */
/* Realize (device init)                                              */
/* ------------------------------------------------------------------ */
static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Basic PCI config */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x109e);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_BT848);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_MULTIMEDIA_VIDEO);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Define single MMIO BAR 0, size 0x1000 as used in driver ioremap */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    s->bar_info[0].size  = 0x1000;
    s->bar_info[0].name  = "bt848-mmio";
    s->bar_info[0].sparse = false;

    /* backing store to allow unknown registers to be saved */
    s->mmio_backing_size = 0x1000;
    s->mmio_backing = g_malloc0(s->mmio_backing_size);

    /* register BARs */
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No MSI/MSI-X used by the driver; legacy INTx only. */
    s->has_msi = false;
    s->has_msix = false;

    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) {
        return;
    }

    /* Initialize registers to power-on state expected by driver */
    pcibase_reset(DEVICE(pdev));
}

/* ------------------------------------------------------------------ */
/* Uninit/cleanup                                                     */
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

    if (s->mmio_backing) {
        g_free(s->mmio_backing);
        s->mmio_backing = NULL;
        s->mmio_backing_size = 0;
    }
}

/* ------------------------------------------------------------------ */
/* Class init / type registration                                     */
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

type_init(pcibase_register_types);

