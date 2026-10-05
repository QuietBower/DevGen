/*
 * Generic QEMU PCI device template (QEMU 8.2.x)
 *
 * Phase 1: fill register macros, BAR list, BAR sizes, structs, enums.
 * Phase 2: implement MMIO/PIO read/write, DMA init, IRQ init, reset, config hooks
 * Phase 3 (Repair Phase)：correct syntax, types, includes, missing symbols, remove dead code
 * Phase 4 (Debug & Update Phase)：Based on actual kernel logs from QEMU boot, repair the virtual hardware behavior
 *
 * Replace #PLACEHOLDER# blocks with device-specific code/data.
 * NOTE:
 * - Only essential fixes added (TYPE macro, PIO BAR handling, MMIO impl, DMA init, IRQ).
 * - No optional or advanced features added.
 * - Do not directly fill this file with the Linux kernel source code
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

/* ------------------------------------------------------------------ */
/* Additional include files (Stage1 optional)                          */
/* ------------------------------------------------------------------ */


#define TYPE_PCIBASE_DEVICE "i740fb"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Related register/macros/constants from driver (Stage1) */
#define DACSPEED8	203
#define DACSPEED16	163
#define DACSPEED24_SG	136
#define DACSPEED24_SD	128
#define DACSPEED32	86
#define REG_DDC_DRIVE	0x62
#define REG_DDC_STATE	0x63
#define DDC_SCL		(1 << 3)
#define DDC_SDA		(1 << 2)
#define I740_RFREQ		1000000
#define TARGET_MAX_N		30
#define I740_FFIX		(1 << 8)
#define I740_RFREQ_FIX		(I740_RFREQ / I740_FFIX)
#define I740_REF_FREQ		(6667 * I740_FFIX / 100)
#define I740_MAX_VCO_FREQ	(450 * I740_FFIX)
#define I740_ID_PCI 0x00d1
#define I740_ID_AGP 0x7800
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

#define VGA_SEQ_I   	0x3C4
#define VGA_CRT_C   	0x19
#define VGA_ATT_C   	0x15
#define VGA_GFX_C   	0x09
#define VGA_SEQ_C   	0x05

#define I740_VRAM_SIZE (8 * 1024 * 1024)
#define I740_MMIO_SIZE (512 * 1024)

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
    MemoryRegion bar_regions[2];

    /* BAR table */
    BARInfo bar_info[2];
    int num_bars;

    /* i740 / VGA Registers */
    uint8_t crtc[256];
    uint8_t crtc_index;
    
    uint8_t seq[256];
    uint8_t seq_index;
    
    uint8_t gfx[256];
    uint8_t gfx_index;
    
    uint8_t att[256];
    uint8_t att_index;
    bool att_flipflop; /* 0 = index, 1 = data */
    
    uint8_t xrx[256];
    uint8_t xrx_index;
    
    uint8_t mrx[256];
    uint8_t mrx_index;
    
    uint8_t msr; /* 0x3C2/0x3CC */
    uint8_t is1; /* 0x3DA */
    
    /* DAC */
    uint8_t dac_mask; /* 0x3C6 */
    uint8_t dac_w_index; /* 0x3C8 */
    uint8_t dac_r_index; /* 0x3C7 */
    uint8_t dac_cnt; /* 0..2 for R,G,B */
    uint8_t palette[768];

    /* FWATER_BLC (0x6000) */
    uint32_t fwater_blc;
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
    .impl  = { .min_access_size = 1, .max_access_size = 1 }, /* Force byte access for simplicity */
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
    uint64_t val = 0xFF;

    /* Handle FWATER_BLC (0x6000) */
    if (addr >= FWATER_BLC && addr < FWATER_BLC + 4) {
        int shift = (addr - FWATER_BLC) * 8;
        return (s->fwater_blc >> shift) & 0xFF;
    }

    switch (addr) {
    case 0x3C0: /* Attribute Controller Index/Data */
    case 0x3C1: /* Attribute Read */
        val = s->att[s->att_index];
        break;
        
    case 0x3C2: /* Input Status 0 (Read) */
        val = 0; 
        break;
        
    case 0x3C4: /* Sequencer Index */
        val = s->seq_index;
        break;
        
    case 0x3C5: /* Sequencer Data */
        val = s->seq[s->seq_index];
        break;
        
    case 0x3C6: /* DAC Mask */
        val = s->dac_mask;
        break;
        
    case 0x3C7: /* DAC Read Index */
        val = s->dac_r_index;
        break;
        
    case 0x3C8: /* DAC Write Index */
        val = s->dac_w_index;
        break;
        
    case 0x3C9: /* DAC Data */
        val = 0; 
        break;
        
    case 0x3CC: /* Misc Output Read */
        val = s->msr;
        break;
        
    case 0x3CE: /* Graphics Index */
        val = s->gfx_index;
        break;
        
    case 0x3CF: /* Graphics Data */
        val = s->gfx[s->gfx_index];
        break;
        
    case 0x3D4: /* CRT Index */
        val = s->crtc_index;
        break;
        
    case 0x3D5: /* CRT Data */
        val = s->crtc[s->crtc_index];
        break;
        
    case 0x3DA: /* Input Status 1 */
        /* Reset Attribute FlipFlop */
        s->att_flipflop = false;
        /* Toggle VRetrace to simulate active raster */
        s->is1 ^= 0x08; 
        val = s->is1;
        break;
        
    /* i740 Specific */
    case 0x3D6: /* XRX Index */
        val = s->xrx_index;
        break;
        
    case 0x3D7: /* XRX Data */
        /* Special handling for Probe */
        if (s->xrx_index == DRAM_ROW_BNDRY_0 || s->xrx_index == DRAM_ROW_BNDRY_1) {
            /* DRAM_ROW_BNDRY - return size in MB */
            val = 8; 
        } else {
            val = s->xrx[s->xrx_index];
        }
        break;
        
    case 0x3D2: /* MRX Index */
        val = s->mrx_index;
        break;
        
    case 0x3D3: /* MRX Data */
        val = s->mrx[s->mrx_index];
        break;
        
    default:
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint8_t v = val & 0xFF;

    /* Handle FWATER_BLC (0x6000) */
    if (addr >= FWATER_BLC && addr < FWATER_BLC + 4) {
        int shift = (addr - FWATER_BLC) * 8;
        uint32_t mask = ~(0xFF << shift);
        s->fwater_blc = (s->fwater_blc & mask) | ((uint32_t)v << shift);
        return;
    }

    switch (addr) {
    case 0x3C0: /* Attribute Controller */
        if (!s->att_flipflop) {
            s->att_index = v & 0x1F; /* Mask 0x20 (PAS) */
            s->att_flipflop = true;
        } else {
            s->att[s->att_index] = v;
            s->att_flipflop = false;
        }
        break;
        
    case 0x3C2: /* Misc Output Write */
        s->msr = v;
        break;
        
    case 0x3C4: /* Sequencer Index */
        s->seq_index = v;
        break;
        
    case 0x3C5: /* Sequencer Data */
        s->seq[s->seq_index] = v;
        break;
        
    case 0x3C6: /* DAC Mask */
        s->dac_mask = v;
        break;
        
    case 0x3C7: /* DAC Read Index */
        s->dac_r_index = v;
        s->dac_cnt = 0;
        break;
        
    case 0x3C8: /* DAC Write Index */
        s->dac_w_index = v;
        s->dac_cnt = 0;
        break;
        
    case 0x3C9: /* DAC Data */
        {
            int offset = s->dac_w_index * 3 + s->dac_cnt;
            s->palette[offset] = v;
            s->dac_cnt++;
            if (s->dac_cnt >= 3) {
                s->dac_cnt = 0;
                s->dac_w_index++;
            }
        }
        break;
        
    case 0x3CE: /* Graphics Index */
        s->gfx_index = v;
        break;
        
    case 0x3CF: /* Graphics Data */
        s->gfx[s->gfx_index] = v;
        break;
        
    case 0x3D4: /* CRT Index */
        s->crtc_index = v;
        break;
        
    case 0x3D5: /* CRT Data */
        s->crtc[s->crtc_index] = v;
        break;
        
    case 0x3D6: /* XRX Index */
        s->xrx_index = v;
        break;
        
    case 0x3D7: /* XRX Data */
        s->xrx[s->xrx_index] = v;
        break;
        
    case 0x3D2: /* MRX Index */
        s->mrx_index = v;
        break;
        
    case 0x3D3: /* MRX Data */
        s->mrx[s->mrx_index] = v;
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

/* ------------------------------------------------------------------ */
/* Reset       */
/* ------------------------------------------------------------------ */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    /* core resets */
    pci_device_reset(pdev);

    memset(s->crtc, 0, sizeof(s->crtc));
    memset(s->seq, 0, sizeof(s->seq));
    memset(s->gfx, 0, sizeof(s->gfx));
    memset(s->att, 0, sizeof(s->att));
    memset(s->xrx, 0, sizeof(s->xrx));
    memset(s->mrx, 0, sizeof(s->mrx));
    memset(s->palette, 0, sizeof(s->palette));

    s->crtc_index = 0;
    s->seq_index = 0;
    s->gfx_index = 0;
    s->att_index = 0;
    s->att_flipflop = false;
    s->xrx_index = 0;
    s->mrx_index = 0;
    s->msr = 0;
    s->is1 = 0;
    s->dac_mask = 0xFF;
    s->dac_w_index = 0;
    s->dac_r_index = 0;
    s->dac_cnt = 0;
    s->fwater_blc = 0;

    /* Initialize registers for Probe detection */
    /* DRAM_ROW_TYPE (0x50) & DRAM_ROW_1 (0x38) == DRAM_ROW_1_SDRAM (0x00) */
    s->xrx[DRAM_ROW_TYPE] = 0x00; 

    /* DRAM_ROW_CNTL_LO (0x51) check for SGRAM vs SDRAM */
    /* If (tmp & 0x08) || (tmp & 0x04) -> SDRAM. Else SGRAM. */
    /* We want SDRAM to match 0x50 setting */
    s->xrx[DRAM_ROW_CNTL_LO] = DRAM_RAS_TIMING; 
}

/* ------------------------------------------------------------------ */
/* DMA initialize (called from realize)                                */
/* ------------------------------------------------------------------ */
static void pcibase_dma_device_realize(PCIDevice *pdev, Error **errp)
{
}

/* ------------------------------------------------------------------ */
/* PCI config space access (class-level hooks)                         */
/* ------------------------------------------------------------------ */
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    uint32_t val = pci_default_read_config(pdev, addr, len);
    return val;
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    pci_default_write_config(pdev, addr, val, len);
}

/* ------------------------------------------------------------------ */
/* Realize (device init)                                                */
/* ------------------------------------------------------------------ */
static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_INTEL );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  I740_ID_PCI );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_DISPLAY_VGA );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);
    
    s->num_bars = 2;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_RAM, .size = I740_VRAM_SIZE, .name = "i740.vram" };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_MMIO, .size = I740_MMIO_SIZE, .name = "i740.mmio" };

    /* register BARs */
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) return;
}

/* ------------------------------------------------------------------ */
/* Uninit/cleanup                                                      */
/* ------------------------------------------------------------------ */
static void pcibase_uninit(PCIDevice *pdev)
{
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

    /* Fix for startup error: register alias "i740fb_pci" to match user command line */
    static const TypeInfo pcibase_alias_info = {
        .name = "i740fb_pci",
        .parent = TYPE_PCIBASE_DEVICE,
    };

    type_register_static(&pcibase_info);
    type_register_static(&pcibase_alias_info);
}

type_init(pcibase_register_types);
