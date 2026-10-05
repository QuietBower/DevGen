/*
 * QEMU Intel i740 PCI VGA Device Emulation
 *
 * Copyright (c) 2025 QEMU contributors
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
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
#include "qemu/units.h"

#define TYPE_PCIBASE_DEVICE "i740fb_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Related register/macros/constants from driver (Stage1) */
#define PCI_VENDOR_ID_INTEL 0x8086
#define I740_ID_PCI 0x00d1
#define I740_ID_AGP 0x7800

#define REG_DDC_DRIVE	0x62
#define REG_DDC_STATE	0x63
#define DDC_SCL		(1 << 3)
#define DDC_SDA		(1 << 2)
#define XRX 0x3D6
#define VCO_M_MSBS		0x03
#define VCO_N_MSBS		0x30
#define REF_DIV_1		0x01
#define COLEXP_16BPP		0x10
#define DISPLAY_32BPP_MODE	0x07
#define DISPLAY_8BPP_MODE	0x02
#define EXT_START_ADDR_ENABLE	0x80
#define INTERLACE_DISABLE	0x00
#define DISPLAY_GAMMA_ENABLE	0x08
#define OVERLAY_GAMMA_ENABLE	0x04
#define PLL_MEMCLK_100000KHZ	0x03
#define LINEAR_MODE_ENABLE	0x02
#define DISPLAY_24BPP_MODE	0x06
#define DAC_8_BIT		0x80
#define COLEXP_RESERVED		0x30
#define COLEXP_24BPP		0x20
#define HIRES_MODE		0x01
#define PAGE_MAPPING_ENABLE	0x01
#define DISPLAY_16BPP_MODE	0x05
#define DISPLAY_15BPP_MODE	0x04
#define COLEXP_8BPP		0x00
#define EXTENDED_CRTC_CNTL	0x01
#define DISPLAY_COLOR_MODE	0x0F
#define IO_CTNL		0x09
#define PIXPIPE_CONFIG_1 0x81
#define BITBLT_CNTL	0x20
#define COL_KEY_CNTL_1		0x3C
#define EXT_HORIZ_TOTAL		0x35
#define EXT_VERT_DISPLAY	0x31
#define EXT_HORIZ_BLANK		0x39
#define FWATER_BLC	0x00006000
#define DISPLAY_CNTL	0x40
#define INTERLACE_CNTL		0x70
#define EXT_VERT_SYNC_START	0x32
#define PLL_CNTL	0xCE
#define EXT_START_ADDR_HI	0x42
#define COLEXP_MODE		0x30
#define DRAM_REFRESH_DISABLE	0x00
#define DRAM_REFRESH_60HZ	0x01
#define EXT_VERT_TOTAL		0x30
#define LMI_BURST_LENGTH	0x7F000000
#define EXTENDED_ATTR_CNTL	0x02
#define EXT_OFFSET		0x41
#define PIXPIPE_CONFIG_2 0x82
#define EXT_START_ADDR		0x40
#define VCLK2_VCO_N	0xC9
#define GUI_MODE		0x01
#define LMI_FIFO_WATERMARK	0x003F0000
#define INTERLACE_ENABLE	0x80
#define MRX 0x3D2
#define VCLK2_VCO_MN_MSBS 0xCA
#define PIXPIPE_CONFIG_0 0x80
#define VCLK2_VCO_M	0xC8
#define EXT_VERT_BLANK_START	0x33
#define DRAM_EXT_CNTL	0x53
#define VGA_WRAP_MODE		0x02
#define VCLK2_VCO_DIV_SEL 0xCB
#define ADDRESS_MAPPING	0x0A
#define BLANK_DISP_OVERLAY	0x20
#define HSYNC_OFF		0x02
#define SRX VGA_SEQ_I
#define VSYNC_OFF		0x08
#define HSYNC_ON		0x00
#define DPMS_SYNC_SELECT 0x61
#define VSYNC_ON		0x00
#define DRAM_ROW_BNDRY_1 0x56
#define DRAM_RAS_TIMING		0x08
#define DRAM_ROW_1		0x38
#define DRAM_ROW_TYPE	0x50
#define DRAM_ROW_CNTL_LO 0x51
#define DRAM_ROW_1_SDRAM	0x00
#define DRAM_ROW_BNDRY_0 0x55
#define DRAM_RAS_PRECHARGE	0x04

/* VGA register indices */
#define VGA_SEQ_I		0x3C4
#define VGA_SEQ_RESET		0x00
#define VGA_SEQ_CLOCK_MODE	0x01
#define VGA_SEQ_PLANE_WRITE	0x02
#define VGA_SEQ_CHARACTER_MAP	0x03
#define VGA_SEQ_MEMORY_MODE	0x04
#define VGA_SEQ_C		5

#define VGA_CRT_IC		0x3D4
#define VGA_CRTC_H_TOTAL	0x00
#define VGA_CRTC_H_DISP		0x01
#define VGA_CRTC_H_BLANK_START	0x02
#define VGA_CRTC_H_BLANK_END	0x03
#define VGA_CRTC_H_SYNC_START	0x04
#define VGA_CRTC_H_SYNC_END	0x05
#define VGA_CRTC_V_TOTAL	0x06
#define VGA_CRTC_OVERFLOW	0x07
#define VGA_CRTC_PRESET_ROW	0x08
#define VGA_CRTC_MAX_SCAN	0x09
#define VGA_CRTC_CURSOR_START	0x0A
#define VGA_CRTC_CURSOR_END	0x0B
#define VGA_CRTC_START_HI	0x0C
#define VGA_CRTC_START_LO	0x0D
#define VGA_CRTC_CURSOR_HI	0x0E
#define VGA_CRTC_CURSOR_LO	0x0F
#define VGA_CRTC_V_SYNC_START	0x10
#define VGA_CRTC_V_SYNC_END	0x11
#define VGA_CRTC_V_DISP_END	0x12
#define VGA_CRTC_OFFSET		0x13
#define VGA_CRTC_UNDERLINE	0x14
#define VGA_CRTC_V_BLANK_START	0x15
#define VGA_CRTC_V_BLANK_END	0x16
#define VGA_CRTC_MODE		0x17
#define VGA_CRTC_LINE_COMPARE	0x18
#define VGA_CRTC_C		0x19

#define VGA_GFX_I		0x3CE
#define VGA_GFX_SR_VALUE	0x00
#define VGA_GFX_SR_ENABLE	0x01
#define VGA_GFX_COMPARE_VALUE	0x02
#define VGA_GFX_DATA_ROTATE	0x03
#define VGA_GFX_PLANE_READ	0x04
#define VGA_GFX_MODE		0x05
#define VGA_GFX_MISC		0x06
#define VGA_GFX_COMPARE_MASK	0x07
#define VGA_GFX_BIT_MASK	0x08
#define VGA_GFX_C		9

#define VGA_ATT_IW		0x3C0
#define VGA_ATT_IR		0x3C1
#define VGA_ATC_MODE		0x10
#define VGA_ATC_OVERSCAN	0x11
#define VGA_ATC_PLANE_ENABLE	0x12
#define VGA_ATC_COLOR_PAGE	0x13
#define VGA_ATT_C		0x14

#define VGA_PEL_MSK		0x3C6
#define VGA_PEL_IW		0x3C8
#define VGA_PEL_IR		0x3C7
#define VGA_PEL_D		0x3C9

#define VGA_MIS_W		0x3C2
#define VGA_MIS_R		0x3CC

#define VGA_IS1_RC		0x3DA
#define VGA_IS1_RW		0x3C2

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

    /* BAR memory regions (MMIO/PIO unified handling fix) */
    MemoryRegion bar_regions[6];

    /* optional linear backing */
    uint8_t *mmio_backing;
    size_t mmio_backing_size;

    /* BAR table */
    BARInfo bar_info[6];
    int num_bars;

    /* interrupt state feature flags */
    bool has_msi;
    bool has_msix;
    
    /* Register shadows */
    uint8_t crtc[0x20];
    uint8_t atc[0x14];
    uint8_t gdc[0x09];
    uint8_t seq[0x05];
    uint8_t misc;
    uint8_t vss;

    /* i740 specific registers */
    uint8_t display_cntl;
    uint8_t pixelpipe_cfg0;
    uint8_t pixelpipe_cfg1;
    uint8_t pixelpipe_cfg2;
    uint8_t video_clk2_m;
    uint8_t video_clk2_n;
    uint8_t video_clk2_mn_msbs;
    uint8_t video_clk2_div_sel;
    uint8_t pll_cntl;
    uint8_t address_mapping;
    uint8_t io_cntl;
    uint8_t bitblt_cntl;
    uint8_t ext_vert_total;
    uint8_t ext_vert_disp_end;
    uint8_t ext_vert_sync_start;
    uint8_t ext_vert_blank_start;
    uint8_t ext_horiz_total;
    uint8_t ext_horiz_blank;
    uint8_t ext_offset;
    uint8_t interlace_cntl;
    uint32_t lmi_fifo_watermark;
    uint8_t ext_start_addr;
    uint8_t ext_start_addr_hi;
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
/* MemoryRegionOps                                                      */
/* ------------------------------------------------------------------ */
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

/* ------------------------------------------------------------------ */
/* Helper: register a BAR (MMIO or PIO)                                 */
/* ------------------------------------------------------------------ */
static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE)
        return;

    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    }else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, bi->size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }

}

/* ------------------------------------------------------------------ */
/* MMIO / PIO handlers (device-specific code goes into placeholders)   */
/* ------------------------------------------------------------------ */

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= FWATER_BLC && addr < FWATER_BLC + 4) {
        if (size == 4) {
            return s->lmi_fifo_watermark;
        }
    }

    qemu_log_mask(LOG_UNIMP, "[%s] mmio_read addr=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= FWATER_BLC && addr < FWATER_BLC + 4) {
        if (size == 4) {
            s->lmi_fifo_watermark = val;
            return;
        }
    }

    qemu_log_mask(LOG_UNIMP, "[%s] mmio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, (uint64_t)val, size);
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size != 1) {
        goto unimp;
    }

    switch (addr) {
    case 0x3C0:
        /* Attribute Controller Index */
        val = 0; /* dummy read */
        break;
    case 0x3C1:
        /* Attribute Controller Data */
        val = s->atc[s->vss & 0x1F];
        break;

    case 0x3B0 ... 0x3BB:
        /* CRT Controller (Mono) */
        if (addr == 0x3B4) {
            val = s->vss; /* status */
        } else if (addr == 0x3B5) {
            val = s->crtc[s->vss];
        }
        break;

    case 0x3D0 ... 0x3DF:
        /* CRT Controller (Color) - skip overlapping range with mono */
        if (addr == 0x3D4) {
            val = s->vss; /* status */
        } else if (addr == 0x3D5) {
            val = s->crtc[s->vss];
        }
        break;

    case 0x3C2:
        /* Miscellaneous Output */
        val = s->misc;
        break;

    case 0x3C4:
        /* Sequencer Index */
        val = s->vss;
        break;
    case 0x3C5:
        /* Sequencer Data */
        val = s->seq[s->vss];
        break;

    case 0x3CE:
        /* Graphics Controller Index */
        val = s->vss;
        break;
    case 0x3CF:
        /* Graphics Controller Data */
        val = s->gdc[s->vss];
        break;

    case 0x3C6:
        /* Pixel Mask */
        val = 0xFF;
        break;

    case 0x3C7:
    case 0x3C8:
    case 0x3C9:
        /* DAC */
        val = 0;
        break;

    default:
        goto unimp;
    }

    return val;

unimp:
    qemu_log_mask(LOG_UNIMP, "[%s] pio_read addr=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 1) {
        goto unimp;
    }

    switch (addr) {
    case 0x3C0:
        /* Attribute Controller Index */
        s->vss = val & 0x3F;
        if (val & 0x20) {
            s->atc[val & 0x1F] = s->vss;
        }
        break;

    case 0x3C2:
        /* Miscellaneous Output */
        s->misc = val;
        break;

    case 0x3C4:
        /* Sequencer Index */
        s->vss = val;
        break;

    case 0x3C5:
        /* Sequencer Data */
        if (s->vss < VGA_SEQ_C) {
            s->seq[s->vss] = val;
        }
        break;

    case 0x3CE:
        /* Graphics Controller Index */
        s->vss = val;
        break;

    case 0x3CF:
        /* Graphics Controller Data */
        if (s->vss < VGA_GFX_C) {
            s->gdc[s->vss] = val;
        }
        break;

    case 0x3D4:
        /* CRT Controller Index */
        s->vss = val;
        break;

    case 0x3D5:
        /* CRT Controller Data */
        if (s->vss < VGA_CRTC_C) {
            s->crtc[s->vss] = val;
        } else {
            switch (s->vss) {
            case EXT_VERT_TOTAL:
                s->ext_vert_total = val;
                break;
            case EXT_VERT_DISPLAY:
                s->ext_vert_disp_end = val;
                break;
            case EXT_VERT_SYNC_START:
                s->ext_vert_sync_start = val;
                break;
            case EXT_VERT_BLANK_START:
                s->ext_vert_blank_start = val;
                break;
            case EXT_HORIZ_TOTAL:
                s->ext_horiz_total = val;
                break;
            case EXT_HORIZ_BLANK:
                s->ext_horiz_blank = val;
                break;
            case EXT_OFFSET:
                s->ext_offset = val;
                break;
            case EXT_START_ADDR:
                s->ext_start_addr = val;
                break;
            case EXT_START_ADDR_HI:
                s->ext_start_addr_hi = val;
                break;
            case INTERLACE_CNTL:
                s->interlace_cntl = val;
                break;
            }
        }
        break;

    case 0x3C6:
    case 0x3C7:
    case 0x3C8:
    case 0x3C9:
        /* DAC - ignore */
        break;

    default:
        goto unimp;
    }

    return;

unimp:
    qemu_log_mask(LOG_UNIMP, "[%s] pio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, (uint64_t)val, size);
}

/* ------------------------------------------------------------------ */
/* Reset       */
/* ------------------------------------------------------------------ */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);
    int i;

    /* core resets */
    pci_device_reset(pdev);

    if (s->mmio_backing && s->mmio_backing_size)
        memset(s->mmio_backing, 0, s->mmio_backing_size);

    /* Reset VGA registers to defaults */
    for (i = 0; i < 0x20; i++)
        s->crtc[i] = 0;
    for (i = 0; i < 0x14; i++)
        s->atc[i] = 0;
    for (i = 0; i < 0x09; i++)
        s->gdc[i] = 0;
    for (i = 0; i < 0x05; i++)
        s->seq[i] = 0;

    s->misc = 0;
    s->vss = 0;

    /* i740 specific defaults */
    s->display_cntl = HIRES_MODE;
    s->pixelpipe_cfg0 = DAC_8_BIT;
    s->pixelpipe_cfg1 = DISPLAY_8BPP_MODE;
    s->pixelpipe_cfg2 = DISPLAY_GAMMA_ENABLE | OVERLAY_GAMMA_ENABLE;
    s->address_mapping = LINEAR_MODE_ENABLE | PAGE_MAPPING_ENABLE;
    s->io_cntl = EXTENDED_CRTC_CNTL;
    s->bitblt_cntl = COLEXP_8BPP;
    s->interlace_cntl = INTERLACE_DISABLE;
    s->pll_cntl = PLL_MEMCLK_100000KHZ;
    s->lmi_fifo_watermark = 0x100D0000; /* default for 8bpp <=135MHz */
    s->ext_start_addr = EXT_START_ADDR_ENABLE;
    s->ext_start_addr_hi = 0;
}

/* ------------------------------------------------------------------ */
/* PCI config space access (class-level hooks)                         */
/* ------------------------------------------------------------------ */
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    uint32_t val = pci_default_read_config(pdev, addr, len);
    /* optional: override certain config offsets if needed */

    switch (len) {
        case 1: val &= 0xFF; break;
        case 2: val &= 0xFFFF; break;
        case 4: break;
        default: break;
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

/* ------------------------------------------------------------------ */
/* Realize (device init)                                                */
/* ------------------------------------------------------------------ */
static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_INTEL);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  I740_ID_PCI);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_DISPLAY_VGA);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);
    
    /* Setup BARs based on driver usage */
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_RAM;
    s->bar_info[0].size = 4 * MiB; /* typical i740 memory size */
    s->bar_info[0].name = "i740fb-ram";

    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = 0x1000;
    s->bar_info[1].name = "i740fb-mmio";

    s->num_bars = 2;

    /* register BARs */
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    qemu_log_mask(LOG_UNIMP, "[%s] device realized\n", TYPE_PCIBASE_DEVICE);
}

/* ------------------------------------------------------------------ */
/* Uninit/cleanup                                                      */
/* ------------------------------------------------------------------ */
static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (s->has_msix) {
        msix_uninit(pdev,NULL,0);
        s->has_msix = false;
    }
    if (s->has_msi) {
        msi_uninit(pdev);
        s->has_msi = false;
    }

    if (s->mmio_backing) {
        g_free(s->mmio_backing);
        s->mmio_backing = NULL;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] device uninit\n", TYPE_PCIBASE_DEVICE);
}

/* ------------------------------------------------------------------ */
/* Class init / type registration                                      */
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

type_init(pcibase_register_types);
