/*
 * QEMU Intel i740 framebuffer PCI device model for i740fb.c
 * Target: QEMU 8.2.10
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

#define TYPE_PCIBASE_DEVICE "i740fb_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Minimal VGA constants used by the driver (sizes only) */
#ifndef VGA_CRT_C
#define VGA_CRT_C 25
#endif
#ifndef VGA_ATT_C
#define VGA_ATT_C 21
#endif
#ifndef VGA_GFX_C
#define VGA_GFX_C 9
#endif
#ifndef VGA_SEQ_C
#define VGA_SEQ_C 5
#endif

/* i740-specific and helper macros from the driver */
#define DACSPEED8    203
#define DACSPEED16    163
#define DACSPEED24_SG    136
#define DACSPEED24_SD    128
#define DACSPEED32    86
#define REG_DDC_DRIVE    0x62
#define REG_DDC_STATE    0x63
#define DDC_SCL        (1 << 3)
#define DDC_SDA        (1 << 2)
#define I740_RFREQ        1000000
#define TARGET_MAX_N        30
#define I740_FFIX        (1 << 8)
#define I740_RFREQ_FIX        (I740_RFREQ / I740_FFIX)
#define I740_REF_FREQ        (6667 * I740_FFIX / 100)
#define I740_MAX_VCO_FREQ        (450 * I740_FFIX)
#define I740_ID_PCI 0x00d1
#define XRX 0x3D6
#define VCO_M_MSBS        0x03
#define VCO_N_MSBS        0x30
#define REF_DIV_1        0x01
#define COLEXP_16BPP        0x10
#define DISPLAY_32BPP_MODE        0x07
#define DISPLAY_8BPP_MODE        0x02
#define EXT_START_ADDR_ENABLE    0x80
#define INTERLACE_DISABLE    0x00
#define DISPLAY_GAMMA_ENABLE    0x08
#define OVERLAY_GAMMA_ENABLE    0x04
#define PLL_MEMCLK_100000KHZ    0x03
#define LINEAR_MODE_ENABLE    0x02
#define DISPLAY_24BPP_MODE        0x06
#define DAC_8_BIT        0x80
#define COLEXP_RESERVED        0x30
#define COLEXP_24BPP        0x20
#define HIRES_MODE        0x01
#define PAGE_MAPPING_ENABLE    0x01
#define DISPLAY_16BPP_MODE        0x05
#define DISPLAY_15BPP_MODE        0x04
#define COLEXP_8BPP        0x00
#define EXTENDED_CRTC_CNTL        0x01
#define DISPLAY_COLOR_MODE    0x0F
#define IO_CTNL        0x09
#define PIXPIPE_CONFIG_1 0x81
#define BITBLT_CNTL    0x20
#define COL_KEY_CNTL_1        0x3C
#define EXT_HORIZ_TOTAL        0x35
#define EXT_VERT_DISPLAY        0x31
#define EXT_HORIZ_BLANK        0x39
#define FWATER_BLC    0x00006000
#define DISPLAY_CNTL    0x40
#define INTERLACE_CNTL        0x70
#define EXT_VERT_SYNC_START    0x32
#define PLL_CNTL    0xCE
#define EXT_START_ADDR_HI    0x42
#define COLEXP_MODE        0x30
#define DRAM_REFRESH_DISABLE    0x00
#define DRAM_REFRESH_60HZ    0x01
#define EXT_VERT_TOTAL        0x30
#define LMI_BURST_LENGTH    0x7F000000
#define EXTENDED_ATTR_CNTL    0x02
#define EXT_OFFSET        0x41
#define PIXPIPE_CONFIG_2 0x82
#define EXT_START_ADDR        0x40
#define VCLK2_VCO_N    0xC9
#define GUI_MODE        0x01
#define LMI_FIFO_WATERMARK    0x003F0000
#define INTERLACE_ENABLE    0x80
#define MRX 0x3D2
#define VCLK2_VCO_MN_MSBS 0xCA
#define PIXPIPE_CONFIG_0 0x80
#define VCLK2_VCO_M    0xC8
#define EXT_VERT_BLANK_START    0x33
#define DRAM_EXT_CNTL    0x53
#define VGA_WRAP_MODE        0x02
#define VCLK2_VCO_DIV_SEL 0xCB
#define ADDRESS_MAPPING    0x0A
#define BLANK_DISP_OVERLAY    0x20
#define HSYNC_OFF        0x02
#define SRX 0x3C4
#define VSYNC_OFF        0x08
#define HSYNC_ON        0x00
#define DPMS_SYNC_SELECT 0x61
#define VSYNC_ON        0x00
#define DRAM_ROW_BNDRY_1 0x56
#define DRAM_RAS_TIMING        0x08
#define DRAM_ROW_1        0x38
#define DRAM_ROW_TYPE    0x50
#define DRAM_ROW_CNTL_LO 0x51
#define DRAM_ROW_1_SDRAM    0x00
#define DRAM_ROW_BNDRY_0 0x55
#define DRAM_RAS_PRECHARGE    0x04

#define PCI_VENDOR_ID_INTEL        0x8086
#define PCI_CLASS_DISPLAY_VGA        0x0300

/* VGA I/O port constants used */
#define VGA_SEQ_I 0x3C4
#define VGA_SEQ_RESET 0x00
#define VGA_SEQ_CLOCK_MODE 0x01
#define VGA_SEQ_PLANE_WRITE 0x02
#define VGA_SEQ_CHARACTER_MAP 0x03
#define VGA_SEQ_MEMORY_MODE 0x04

#define VGA_CRT_IC 0x3D4
#define VGA_CRTC_H_TOTAL 0x00
#define VGA_CRTC_H_DISP 0x01
#define VGA_CRTC_H_BLANK_START 0x02
#define VGA_CRTC_H_SYNC_START 0x04
#define VGA_CRTC_H_SYNC_END 0x05
#define VGA_CRTC_H_BLANK_END 0x03
#define VGA_CRTC_V_TOTAL 0x06
#define VGA_CRTC_PRESET_ROW 0x08
#define VGA_CRTC_MAX_SCAN 0x09
#define VGA_CRTC_CURSOR_START 0x0A
#define VGA_CRTC_CURSOR_END 0x0B
#define VGA_CRTC_CURSOR_HI 0x0E
#define VGA_CRTC_CURSOR_LO 0x0F
#define VGA_CRTC_V_DISP_END 0x12
#define VGA_CRTC_V_BLANK_START 0x15
#define VGA_CRTC_V_SYNC_END 0x10
#define VGA_CRTC_V_BLANK_END 0x16
#define VGA_CRTC_UNDERLINE 0x14
#define VGA_CRTC_MODE 0x17
#define VGA_CRTC_LINE_COMPARE 0x18
#define VGA_CRTC_OVERFLOW 0x07
#define VGA_CRTC_OFFSET 0x13
#define VGA_CRTC_START_LO 0x0D
#define VGA_CRTC_START_HI 0x0C

#define VGA_GFX_I 0x3CE
#define VGA_GFX_SR_VALUE 0x00
#define VGA_GFX_SR_ENABLE 0x01
#define VGA_GFX_COMPARE_VALUE 0x02
#define VGA_GFX_DATA_ROTATE 0x03
#define VGA_GFX_PLANE_READ 0x04
#define VGA_GFX_MODE 0x05
#define VGA_GFX_MISC 0x06
#define VGA_GFX_COMPARE_MASK 0x07
#define VGA_GFX_BIT_MASK 0x08

#define VGA_ATT_IW 0x3C0
#define VGA_ATT_W 0x3C0
#define VGA_IS1_RC 0x3DA
#define VGA_ATC_MODE 0x10
#define VGA_ATC_OVERSCAN 0x11
#define VGA_ATC_PLANE_ENABLE 0x12
#define VGA_ATC_COLOR_PAGE 0x13

#define VGA_MIS_W 0x3C2

#define VGA_PEL_MSK 0x3C6
#define VGA_PEL_IW 0x3C8
#define VGA_PEL_D 0x3C9

#define FWATER_BLC_OFFSET FWATER_BLC

#define u32 unsigned int
#define u8 unsigned char

/* Forward declarations for Linux driver-specific types used only as
 * opaque storage here. The exact layout is not needed by this model.
 */
struct i2c_adapter;
struct i2c_algo_bit_data {
    void *data;
    void (*setsda)(void *data, int state);
    void (*setscl)(void *data, int state);
    int (*getsda)(void *data);
    int (*getscl)(void *data);
};
struct mutex;

/* Forward declarations for driver helper functions used as callbacks. */
struct i740fb_par;
static void i740fb_ddc_setsda(void *data, int val);
static void i740fb_ddc_setscl(void *data, int val);
static int i740fb_ddc_getsda(void *data);
static int i740fb_ddc_getscl(void *data);

/* BAR metadata */
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

/* Device State */
struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];

    uint8_t *mmio_backing;
    size_t mmio_backing_size;

    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;

    struct i740fb_par {
        uint8_t *regs; /* BAR1 MMIO base (emulated) */
        bool has_sgram;
        int wc_cookie;
        bool ddc_registered;
        struct i2c_adapter *ddc_adapter;
        struct i2c_algo_bit_data ddc_algo;
        u32 pseudo_palette[16];
        struct mutex *open_lock;
        unsigned int ref_count;

        u8 crtc[VGA_CRT_C];
        u8 atc[VGA_ATT_C];
        u8 gdc[VGA_GFX_C];
        u8 seq[VGA_SEQ_C];
        u8 misc;
        u8 vss;

        /* i740 specific registers */
        u8 display_cntl;
        u8 pixelpipe_cfg0;
        u8 pixelpipe_cfg1;
        u8 pixelpipe_cfg2;
        u8 video_clk2_m;
        u8 video_clk2_n;
        u8 video_clk2_mn_msbs;
        u8 video_clk2_div_sel;
        u8 pll_cntl;
        u8 address_mapping;
        u8 io_cntl;
        u8 bitblt_cntl;
        u8 ext_vert_total;
        u8 ext_vert_disp_end;
        u8 ext_vert_sync_start;
        u8 ext_vert_blank_start;
        u8 ext_horiz_total;
        u8 ext_horiz_blank;
        u8 ext_offset;
        u8 interlace_cntl;
        u32 lmi_fifo_watermark;
        u8 ext_start_addr;
        u8 ext_start_addr_hi;
    } regs_shadow;
};

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len);
static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len);
static void pcibase_reset(DeviceState *dev);
static void pcibase_realize(PCIDevice *pdev, Error **errp);
static void pcibase_uninit(PCIDevice *pdev);

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    MemoryRegion *mr;

    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    mr = &s->bar_regions[bi->index];

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

/* Helper: access to emulated BAR1 MMIO backing */
static inline uint8_t *i740_mmio_base(PCIBaseState *s)
{
    return s->mmio_backing;
}

/* The following helpers are currently unused but kept for potential
 * future extensions. Mark them as unused to avoid compiler warnings
 * without changing their behavior.
 */
static uint32_t G_GNUC_UNUSED i740_mmio_read32(PCIBaseState *s, hwaddr offset)
{
    uint8_t *base;
    if (!s->mmio_backing || offset + 4 > s->mmio_backing_size) {
        return 0;
    }
    base = i740_mmio_base(s) + offset;
    return ldl_le_p(base);
}

static void G_GNUC_UNUSED i740_mmio_write32(PCIBaseState *s, hwaddr offset, uint32_t val)
{
    uint8_t *base;
    if (!s->mmio_backing || offset + 4 > s->mmio_backing_size) {
        return;
    }
    base = i740_mmio_base(s) + offset;
    stl_le_p(base, val);
}

/* vga_mm_* helpers map VGA-style ports to BAR1 MMIO */
static void vga_mm_w(uint8_t *regs, uint16_t port, uint8_t val)
{
    /* i740fb uses mmio VGA: par->regs is base of MMIO region (BAR1) */
    /* Map ports in 0x3C0-0x3DF to offsets; other ports ignored. */
    if (port >= 0x3C0 && port <= 0x3DF) {
        hwaddr off = port - 0x3C0;
        regs[off] = val;
    }
}

static uint8_t G_GNUC_UNUSED vga_mm_r(uint8_t *regs, uint16_t port)
{
    if (port >= 0x3C0 && port <= 0x3DF) {
        hwaddr off = port - 0x3C0;
        return regs[off];
    }
    return 0xFF;
}

static void G_GNUC_UNUSED vga_mm_w_fast(uint8_t *regs, uint16_t port, uint8_t index, uint8_t val)
{
    /* For indexed regs: write index then data to port/port+1 */
    vga_mm_w(regs, port, index);
    vga_mm_w(regs, port + 1, val);
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    if (!s->mmio_backing || addr + size > s->mmio_backing_size) {
        return 0;
    }

    switch (size) {
    case 1:
        return s->mmio_backing[addr];
    case 2:
        return lduw_le_p(&s->mmio_backing[addr]);
    case 4:
        return ldl_le_p(&s->mmio_backing[addr]);
    default:
        return 0;
    }
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (!s->mmio_backing || addr + size > s->mmio_backing_size) {
        return;
    }

    switch (size) {
    case 1:
        s->mmio_backing[addr] = (uint8_t)val;
        break;
    case 2:
        stw_le_p(&s->mmio_backing[addr], (uint16_t)val);
        break;
    case 4:
        stl_le_p(&s->mmio_backing[addr], (uint32_t)val);
        break;
    default:
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* No PIO BARs are used by i740fb.c; return 0 */
    (void)opaque;
    (void)addr;
    (void)size;
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* No PIO BARs are used by i740fb.c */
    (void)opaque;
    (void)addr;
    (void)val;
    (void)size;
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    pci_device_reset(pdev);

    if (s->mmio_backing && s->mmio_backing_size) {
        memset(s->mmio_backing, 0, s->mmio_backing_size);
    }

    memset(&s->regs_shadow, 0, sizeof(s->regs_shadow));
}

static void pcibase_dma_device_realize(PCIDevice *pdev, Error **errp)
{
    (void)pdev;
    (void)errp;
}

static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    uint32_t val = pci_default_read_config(pdev, addr, len);

    switch (len) {
    case 1:
        val &= 0xFF;
        break;
    case 2:
        val &= 0xFFFF;
        break;
    case 4:
    default:
        break;
    }
    return val;
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    if (addr >= PCI_BASE_ADDRESS_0 && addr <= PCI_BASE_ADDRESS_5) {
        pci_default_write_config(pdev, addr, val, len);
        return;
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;
    Error *local_err = NULL;

    /* Basic PCI identification */
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_INTEL);
    pci_set_word(pci_conf + PCI_DEVICE_ID, I740_ID_PCI);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_DISPLAY_VGA);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* BAR layout according to driver usage:
     * BAR0: framebuffer (mmio WC mapping) - choose 8 MiB.
     * BAR1: VGA MMIO regs - choose 64 KiB.
     */
    s->num_bars = 2;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 8 * 1024 * 1024;
    s->bar_info[0].name = "i740fb-vram";
    s->bar_info[0].sparse = false;

    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = 64 * 1024;
    s->bar_info[1].name = "i740fb-mmio";
    s->bar_info[1].sparse = false;

    /* Allocate a single backing buffer for BAR1 MMIO. BAR0 (VRAM) is handled
     * by the MemoryRegion directly and not accessed by the driver via MMIO.
     */
    s->mmio_backing_size = s->bar_info[1].size;
    s->mmio_backing = g_malloc0(s->mmio_backing_size);

    /* Initialize par->regs to BAR1 backing */
    s->regs_shadow.regs = s->mmio_backing;

    /* Initialize DDC I2C bit-banging callbacks according to driver code */
    s->regs_shadow.ddc_algo.data = &s->regs_shadow;
    s->regs_shadow.ddc_algo.setsda = i740fb_ddc_setsda;
    s->regs_shadow.ddc_algo.setscl = i740fb_ddc_setscl;
    s->regs_shadow.ddc_algo.getsda = i740fb_ddc_getsda;
    s->regs_shadow.ddc_algo.getscl = i740fb_ddc_getscl;

    /* Register BARs */
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            return;
        }
    }

    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) {
        return;
    }
}

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

/* Stub implementations of driver helper functions used for DDC bit banging.
 * These are kept minimal and only ensure successful compilation. */
static void i740fb_ddc_setsda(void *data, int val)
{
    (void)data;
    (void)val;
}

static void i740fb_ddc_setscl(void *data, int val)
{
    (void)data;
    (void)val;
}

static int i740fb_ddc_getsda(void *data)
{
    (void)data;
    return 1;
}

static int i740fb_ddc_getscl(void *data)
{
    (void)data;
    return 1;
}

static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->config_read = pcibase_config_read;
    k->config_write = pcibase_config_write;
    k->realize = pcibase_realize;
    k->exit = pcibase_uninit;
    dc->reset = pcibase_reset;

    set_bit(DEVICE_CATEGORY_DISPLAY, dc->categories);
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

