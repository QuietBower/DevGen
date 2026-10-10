/*
 * This template provides a robust skeleton for hardware emulation.
 * Integrated QEMU PCI device template (QEMU 8.2.10).
 * Replace #PLACEHOLDER# blocks with driver-specific definitions.
 * Designed for register-level modeling and PCIe driver probing.
 * NOTE: Do not directly fill this file with the Linux kernel source code.
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


#define TYPE_PCIBASE_DEVICE "i740fb_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
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
#define VCO_N_MSBS		0x30
#define VCO_M_MSBS		0x03
#define REF_DIV_1		0x01
#define DISPLAY_8BPP_MODE	0x02
#define LINEAR_MODE_ENABLE	0x02
#define EXTENDED_CRTC_CNTL	0x01
#define COLEXP_24BPP		0x20
#define DISPLAY_15BPP_MODE	0x04
#define DAC_8_BIT		0x80
#define COLEXP_RESERVED		0x30
#define INTERLACE_DISABLE	0x00
#define PAGE_MAPPING_ENABLE	0x01
#define COLEXP_8BPP		0x00
#define DISPLAY_GAMMA_ENABLE	0x08
#define PLL_MEMCLK_100000KHZ	0x03
#define DISPLAY_16BPP_MODE	0x05
#define DISPLAY_24BPP_MODE	0x06
#define HIRES_MODE		0x01
#define OVERLAY_GAMMA_ENABLE	0x04
#define DISPLAY_32BPP_MODE	0x07
#define COLEXP_16BPP		0x10
#define EXT_START_ADDR_ENABLE	0x80
#define VGA_WRAP_MODE		0x02
#define GUI_MODE		0x01
#define LMI_FIFO_WATERMARK	0x003F0000
#define MRX 0x3D2
#define PIXPIPE_CONFIG_0 0x80
#define EXT_START_ADDR		0x40
#define INTERLACE_ENABLE	0x80
#define INTERLACE_CNTL		0x70
#define FWATER_BLC		0x00006000
#define BLANK_DISP_OVERLAY	0x20
#define EXT_OFFSET		0x41
#define EXTENDED_ATTR_CNTL	0x02
#define EXT_VERT_TOTAL		0x30
#define VCLK2_VCO_N	0xC9
#define COL_KEY_CNTL_1		0x3C
#define EXT_HORIZ_BLANK		0x39
#define ADDRESS_MAPPING	0x0A
#define PIXPIPE_CONFIG_2 0x82
#define LMI_BURST_LENGTH	0x7F000000
#define IO_CTNL		0x09
#define EXT_HORIZ_TOTAL		0x35
#define VCLK2_VCO_DIV_SEL 0xCB
#define PLL_CNTL	0xCE
#define EXT_START_ADDR_HI	0x42
#define BITBLT_CNTL	0x20
#define EXT_VERT_BLANK_START	0x33
#define DRAM_EXT_CNTL	0x53
#define DISPLAY_COLOR_MODE	0x0F
#define DRAM_REFRESH_60HZ	0x01
#define COLEXP_MODE		0x30
#define PIXPIPE_CONFIG_1 0x81
#define EXT_VERT_SYNC_START	0x32
#define DRAM_REFRESH_DISABLE	0x00
#define VCLK2_VCO_M	0xC8
#define VCLK2_VCO_MN_MSBS 0xCA
#define DISPLAY_CNTL	0x40
#define EXT_VERT_DISPLAY	0x31
#define VSYNC_OFF		0x08
#define DPMS_SYNC_SELECT 0x61
#define VSYNC_ON		0x00
#define HSYNC_ON		0x00
#define HSYNC_OFF		0x02
#define DRAM_ROW_TYPE	0x50
#define DRAM_RAS_PRECHARGE	0x04
#define DRAM_ROW_BNDRY_0 0x55
#define DRAM_ROW_BNDRY_1 0x56
#define DRAM_ROW_CNTL_LO 0x51
#define DRAM_ROW_1		0x38
#define DRAM_ROW_1_SDRAM	0x00
#define DRAM_RAS_TIMING		0x08
#define VGA_CRT_C   	0x19
#define VGA_ATT_C   	0x15
#define VGA_GFX_C   	0x09
#define VGA_SEQ_C   	0x05
#define VGA_SEQ_I   	0x3C4

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
    uint8_t crtc[VGA_CRT_C];
    uint8_t atc[VGA_ATT_C];
    uint8_t gdc[VGA_GFX_C];
    uint8_t seq[VGA_SEQ_C];
    uint8_t misc;
    uint8_t vss;
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

static uint8_t xrx_index = 0;
static uint8_t mrx_index = 0;
static uint8_t seq_index = 0;
static uint8_t crtc_index = 0;
static uint8_t gfx_index = 0;
static uint8_t att_index = 0;
static uint8_t att_ff = 0;
static uint8_t xrx_regs[256] = {0};
static uint8_t mrx_regs[256] = {0};

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
        case 0x3D6: val = xrx_index; break;
        case 0x3D7:
            switch (xrx_index) {
                case DRAM_ROW_TYPE: val = DRAM_ROW_1_SDRAM; break;
                case DRAM_ROW_BNDRY_0:
                case DRAM_ROW_BNDRY_1: val = 16; break; /* 16MB */
                case DRAM_ROW_CNTL_LO: val = 0; break; /* has_sgram = true */
                case REG_DDC_STATE: val = xrx_regs[REG_DDC_STATE]; break;
                case DISPLAY_CNTL: val = s->display_cntl; break;
                case PIXPIPE_CONFIG_0: val = s->pixelpipe_cfg0; break;
                case PIXPIPE_CONFIG_1: val = s->pixelpipe_cfg1; break;
                case PIXPIPE_CONFIG_2: val = s->pixelpipe_cfg2; break;
                case VCLK2_VCO_M: val = s->video_clk2_m; break;
                case VCLK2_VCO_N: val = s->video_clk2_n; break;
                case VCLK2_VCO_MN_MSBS: val = s->video_clk2_mn_msbs; break;
                case VCLK2_VCO_DIV_SEL: val = s->video_clk2_div_sel; break;
                case PLL_CNTL: val = s->pll_cntl; break;
                case ADDRESS_MAPPING: val = s->address_mapping; break;
                case IO_CTNL: val = s->io_cntl; break;
                case BITBLT_CNTL: val = s->bitblt_cntl; break;
                case INTERLACE_CNTL: val = s->interlace_cntl; break;
                default: val = xrx_regs[xrx_index]; break;
            }
            break;
        case 0x3D2: val = mrx_index; break;
        case 0x3D3: val = mrx_regs[mrx_index]; break;
        case 0x3C4: val = seq_index; break;
        case 0x3C5: 
            if (seq_index < VGA_SEQ_C) val = s->seq[seq_index]; 
            break;
        case 0x3D4: val = crtc_index; break;
        case 0x3D5:
            switch (crtc_index) {
                case EXT_VERT_TOTAL: val = s->ext_vert_total; break;
                case EXT_VERT_DISPLAY: val = s->ext_vert_disp_end; break;
                case EXT_VERT_SYNC_START: val = s->ext_vert_sync_start; break;
                case EXT_VERT_BLANK_START: val = s->ext_vert_blank_start; break;
                case EXT_HORIZ_TOTAL: val = s->ext_horiz_total; break;
                case EXT_HORIZ_BLANK: val = s->ext_horiz_blank; break;
                case EXT_OFFSET: val = s->ext_offset; break;
                case EXT_START_ADDR_HI: val = s->ext_start_addr_hi; break;
                case EXT_START_ADDR: val = s->ext_start_addr; break;
                case INTERLACE_CNTL: val = s->interlace_cntl; break;
                default: 
                    if (crtc_index < VGA_CRT_C) val = s->crtc[crtc_index];
                    break;
            }
            break;
        case 0x3CE: val = gfx_index; break;
        case 0x3CF: 
            if (gfx_index < VGA_GFX_C) val = s->gdc[gfx_index]; 
            break;
        case 0x3C0: val = att_index; break;
        case 0x3C1: 
            if (att_index < VGA_ATT_C) val = s->atc[att_index]; 
            break;
        case 0x3DA: 
            att_ff = 0; 
            val = s->vss; 
            break;
        case 0x6000:
            val = s->lmi_fifo_watermark;
            break;
    }
    
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
        case 0x3D6: xrx_index = val; break;
        case 0x3D7:
            xrx_regs[xrx_index] = val;
            switch (xrx_index) {
                case DISPLAY_CNTL: s->display_cntl = val; break;
                case PIXPIPE_CONFIG_0: s->pixelpipe_cfg0 = val; break;
                case PIXPIPE_CONFIG_1: s->pixelpipe_cfg1 = val; break;
                case PIXPIPE_CONFIG_2: s->pixelpipe_cfg2 = val; break;
                case VCLK2_VCO_M: s->video_clk2_m = val; break;
                case VCLK2_VCO_N: s->video_clk2_n = val; break;
                case VCLK2_VCO_MN_MSBS: s->video_clk2_mn_msbs = val; break;
                case VCLK2_VCO_DIV_SEL: s->video_clk2_div_sel = val; break;
                case PLL_CNTL: s->pll_cntl = val; break;
                case ADDRESS_MAPPING: s->address_mapping = val; break;
                case IO_CTNL: s->io_cntl = val; break;
                case BITBLT_CNTL: s->bitblt_cntl = val; break;
                case INTERLACE_CNTL: s->interlace_cntl = val; break;
            }
            break;
        case 0x3D2: mrx_index = val; break;
        case 0x3D3: mrx_regs[mrx_index] = val; break;
        case 0x3C4: seq_index = val; break;
        case 0x3C5: 
            if (seq_index < VGA_SEQ_C) s->seq[seq_index] = val; 
            break;
        case 0x3D4: crtc_index = val; break;
        case 0x3D5:
            switch (crtc_index) {
                case EXT_VERT_TOTAL: s->ext_vert_total = val; break;
                case EXT_VERT_DISPLAY: s->ext_vert_disp_end = val; break;
                case EXT_VERT_SYNC_START: s->ext_vert_sync_start = val; break;
                case EXT_VERT_BLANK_START: s->ext_vert_blank_start = val; break;
                case EXT_HORIZ_TOTAL: s->ext_horiz_total = val; break;
                case EXT_HORIZ_BLANK: s->ext_horiz_blank = val; break;
                case EXT_OFFSET: s->ext_offset = val; break;
                case EXT_START_ADDR_HI: s->ext_start_addr_hi = val; break;
                case EXT_START_ADDR: s->ext_start_addr = val; break;
                case INTERLACE_CNTL: s->interlace_cntl = val; break;
                default: 
                    if (crtc_index < VGA_CRT_C) s->crtc[crtc_index] = val;
                    break;
            }
            break;
        case 0x3CE: gfx_index = val; break;
        case 0x3CF: 
            if (gfx_index < VGA_GFX_C) s->gdc[gfx_index] = val; 
            break;
        case 0x3C0: 
            if (!att_ff) {
                att_index = val & 0x1F;
                att_ff = 1;
            } else {
                if (att_index < VGA_ATT_C) s->atc[att_index] = val;
                att_ff = 0;
            }
            break;
        case 0x3C2: s->misc = val; break;
        case 0x6000:
            s->lmi_fifo_watermark = val;
            break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    uint64_t val = 0;
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
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

    xrx_index = 0;
    mrx_index = 0;
    seq_index = 0;
    crtc_index = 0;
    gfx_index = 0;
    att_index = 0;
    att_ff = 0;
    memset(xrx_regs, 0, sizeof(xrx_regs));
    memset(mrx_regs, 0, sizeof(mrx_regs));
    
    s->lmi_fifo_watermark = 0;
    s->misc = 0;
    s->vss = 0;
    s->display_cntl = 0;
    s->pixelpipe_cfg0 = 0;
    s->pixelpipe_cfg1 = 0;
    s->pixelpipe_cfg2 = 0;
    s->video_clk2_m = 0;
    s->video_clk2_n = 0;
    s->video_clk2_mn_msbs = 0;
    s->video_clk2_div_sel = 0;
    s->pll_cntl = 0;
    s->address_mapping = 0;
    s->io_cntl = 0;
    s->bitblt_cntl = 0;
    s->ext_vert_total = 0;
    s->ext_vert_disp_end = 0;
    s->ext_vert_sync_start = 0;
    s->ext_vert_blank_start = 0;
    s->ext_horiz_total = 0;
    s->ext_horiz_blank = 0;
    s->ext_offset = 0;
    s->interlace_cntl = 0;
    s->ext_start_addr = 0;
    s->ext_start_addr_hi = 0;
    memset(s->crtc, 0, sizeof(s->crtc));
    memset(s->atc, 0, sizeof(s->atc));
    memset(s->gdc, 0, sizeof(s->gdc));
    memset(s->seq, 0, sizeof(s->seq));
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_INTEL );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  I740_ID_PCI );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_DISPLAY_VGA );
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
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_RAM, .size = 16 * MiB, .name = "i740fb-ram" };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_MMIO, .size = 512 * KiB, .name = "i740fb-mmio" };
    
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "i740fb_pci",
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
