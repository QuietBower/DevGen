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
#include "hw/pci/pci_ids.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-core.h"
#include "migration/vmstate.h"

#define TYPE_PCIBASE_DEVICE "i740fb_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define VENDOR_ID PCI_VENDOR_ID_INTEL
#define DEVICE_ID 0x00d1
#define CLASS_ID PCI_CLASS_DISPLAY_VGA

#define DACSPEED8 203
#define DACSPEED16 163
#define DACSPEED24_SG 136
#define DACSPEED24_SD 128
#define DACSPEED32 86
#define REG_DDC_DRIVE 0x62
#define REG_DDC_STATE 0x63
#define DDC_SCL (1 << 3)
#define DDC_SDA (1 << 2)
#define I740_RFREQ 1000000
#define TARGET_MAX_N 30
#define I740_FFIX (1 << 8)
#define I740_RFREQ_FIX (I740_RFREQ / I740_FFIX)
#define I740_REF_FREQ (6667 * I740_FFIX / 100)
#define I740_MAX_VCO_FREQ (450 * I740_FFIX)
#define I740_ID_PCI 0x00d1
#define I740_ID_AGP 0x7800
#define XRX 0x3D6
#define MRX 0x3D2
#define SRX VGA_SEQ_I
#define VCO_N_MSBS 0x30
#define VCO_M_MSBS 0x03
#define REF_DIV_1 0x01
#define DISPLAY_8BPP_MODE 0x02
#define LINEAR_MODE_ENABLE 0x02
#define EXTENDED_CRTC_CNTL 0x01
#define COLEXP_24BPP 0x20
#define DISPLAY_15BPP_MODE 0x04
#define DAC_8_BIT 0x80
#define COLEXP_RESERVED 0x30
#define INTERLACE_DISABLE 0x00
#define PAGE_MAPPING_ENABLE 0x01
#define COLEXP_8BPP 0x00
#define DISPLAY_GAMMA_ENABLE 0x08
#define PLL_MEMCLK_100000KHZ 0x03
#define DISPLAY_16BPP_MODE 0x05
#define DISPLAY_24BPP_MODE 0x06
#define HIRES_MODE 0x01
#define OVERLAY_GAMMA_ENABLE 0x04
#define DISPLAY_32BPP_MODE 0x07
#define COLEXP_16BPP 0x10
#define EXT_START_ADDR_ENABLE 0x80
#define VGA_WRAP_MODE 0x02
#define GUI_MODE 0x01
#define LMI_FIFO_WATERMARK 0x003F0000
#define PIXPIPE_CONFIG_0 0x80
#define EXT_START_ADDR 0x40
#define INTERLACE_ENABLE 0x80
#define INTERLACE_CNTL 0x70
#define FWATER_BLC 0x00006000
#define BLANK_DISP_OVERLAY 0x20
#define EXT_OFFSET 0x41
#define EXTENDED_ATTR_CNTL 0x02
#define EXT_VERT_TOTAL 0x30
#define VCLK2_VCO_N 0xC9
#define COL_KEY_CNTL_1 0x3C
#define EXT_HORIZ_BLANK 0x39
#define ADDRESS_MAPPING 0x0A
#define PIXPIPE_CONFIG_2 0x82
#define LMI_BURST_LENGTH 0x7F000000
#define IO_CTNL 0x09
#define EXT_HORIZ_TOTAL 0x35
#define VCLK2_VCO_DIV_SEL 0xCB
#define PLL_CNTL 0xCE
#define EXT_START_ADDR_HI 0x42
#define BITBLT_CNTL 0x20
#define EXT_VERT_BLANK_START 0x33
#define DRAM_EXT_CNTL 0x53
#define DISPLAY_COLOR_MODE 0x0F
#define DRAM_REFRESH_60HZ 0x01
#define COLEXP_MODE 0x30
#define PIXPIPE_CONFIG_1 0x81
#define EXT_VERT_SYNC_START 0x32
#define DRAM_REFRESH_DISABLE 0x00
#define VCLK2_VCO_M 0xC8
#define VCLK2_VCO_MN_MSBS 0xCA
#define DISPLAY_CNTL 0x40
#define EXT_VERT_DISPLAY 0x31
#define VSYNC_OFF 0x08
#define DPMS_SYNC_SELECT 0x61
#define VSYNC_ON 0x00
#define HSYNC_ON 0x00
#define HSYNC_OFF 0x02
#define DRAM_ROW_TYPE 0x50
#define DRAM_RAS_PRECHARGE 0x04
#define DRAM_ROW_BNDRY_0 0x55
#define DRAM_ROW_BNDRY_1 0x56
#define DRAM_ROW_CNTL_LO 0x51
#define DRAM_ROW_1 0x38
#define DRAM_ROW_1_SDRAM 0x00
#define DRAM_RAS_TIMING 0x08

typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM 
} BARType;

typedef struct {
    int     index;
    BARType type;
    hwaddr  size;
    const char *name;
} BARInfo;

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;

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

    /* indexed register state */
    uint8_t xrx_index;
    uint8_t xrx_regs[256];
    uint8_t seq_index;
    uint8_t seq_data[256];
    uint8_t crtc_index;
    uint8_t crtc_data[256];
    uint8_t gfx_index;
    uint8_t gfx_data[256];
    uint8_t attr_flipflop;
    uint8_t attr_index;
    uint8_t attr_data[0x15];
    uint8_t misc_output;
};

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr == FWATER_BLC) {
        if (size == 4) {
            val = s->lmi_fifo_watermark;
        }
        return val;
    }

    if (addr > 0x3FF) {
        return 0;
    }

    switch (addr) {
    case 0x3C4:
        val = s->seq_index;
        break;
    case 0x3C5:
        val = s->seq_data[s->seq_index];
        break;
    case 0x3CE:
        val = s->gfx_index;
        break;
    case 0x3CF:
        val = s->gfx_data[s->gfx_index];
        break;
    case 0x3D4:
        val = s->crtc_index;
        break;
    case 0x3D5:
        val = s->crtc_data[s->crtc_index];
        break;
    case 0x3C0:
        val = 0;
        break;
    case 0x3C1:
        val = s->attr_data[s->attr_index];
        s->attr_flipflop = 0;
        break;
    case 0x3C2:
        val = s->misc_output;
        break;
    case 0x3DA:
        val = 0x00;
        s->attr_flipflop = 0;
        break;
    case 0x3D6:
        val = s->xrx_index;
        break;
    case 0x3D7:
        if (s->xrx_index == REG_DDC_STATE) {
            uint8_t ddc_drive = s->xrx_regs[REG_DDC_DRIVE];
            uint8_t ddc_output = s->xrx_regs[REG_DDC_STATE];
            val = (ddc_output & ddc_drive) | (~ddc_drive & (DDC_SCL | DDC_SDA));
        } else {
            val = s->xrx_regs[s->xrx_index];
        }
        break;
    default:
        val = 0;
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint8_t new_val = (uint8_t)val;

    if (addr == FWATER_BLC) {
        if (size == 4) {
            s->lmi_fifo_watermark = (uint32_t)val;
        }
        return;
    }

    if (addr > 0x3FF) {
        return;
    }

    switch (addr) {
    case 0x3C4:
        s->seq_index = new_val;
        break;
    case 0x3C5:
        s->seq_data[s->seq_index] = new_val;
        break;
    case 0x3CE:
        s->gfx_index = new_val;
        break;
    case 0x3CF:
        s->gfx_data[s->gfx_index] = new_val;
        break;
    case 0x3D4:
        s->crtc_index = new_val;
        break;
    case 0x3D5:
        s->crtc_data[s->crtc_index] = new_val;
        break;
    case 0x3C0:
        if (s->attr_flipflop == 0) {
            s->attr_index = new_val & 0x1F;
        } else {
            s->attr_data[s->attr_index] = new_val;
        }
        s->attr_flipflop ^= 1;
        break;
    case 0x3C2:
        s->misc_output = new_val;
        break;
    case 0x3D6:
        s->xrx_index = new_val;
        break;
    case 0x3D7:
        s->xrx_regs[s->xrx_index] = new_val;
        break;
    default:
        break;
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

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

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    s->xrx_index = 0;
    memset(s->xrx_regs, 0, sizeof(s->xrx_regs));
    s->xrx_regs[0x50] = 0x00;
    s->xrx_regs[0x51] = 0x00;
    s->xrx_regs[0x55] = 8;
    s->xrx_regs[0x56] = 8;
    s->xrx_regs[0xCE] = 0x03;

    s->seq_index = 0;
    memset(s->seq_data, 0, sizeof(s->seq_data));
    s->crtc_index = 0;
    memset(s->crtc_data, 0, sizeof(s->crtc_data));
    s->gfx_index = 0;
    memset(s->gfx_data, 0, sizeof(s->gfx_data));
    s->attr_flipflop = 0;
    s->attr_index = 0;
    memset(s->attr_data, 0, sizeof(s->attr_data));
    s->misc_output = 0xC3;
    s->lmi_fifo_watermark = 0;
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

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

    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_RAM, .size = 8 * 1024 * 1024, .name = "i740fb-fb" };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_MMIO, .size = 0x10000, .name = "i740fb-mmio" };
    s->num_bars = 2;
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
