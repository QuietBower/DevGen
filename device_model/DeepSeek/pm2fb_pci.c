/*
 * QEMU 3Dlabs Permedia2/2V virtual device
 * Generated based on Linux driver pm2fb.c
 * This device model provides basic register read/write functionality
 * allowing the driver to probe and bind.
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

#define TYPE_PCIBASE_DEVICE "pm2fb_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PM2FB_VENDOR_ID 0x104c
#define PM2FB_DEVICE_ID 0x3d07
#define PM2FB_CLASS_ID 0x038000

/* Driver-provided register offsets */
#define PM2R_RESET_STATUS              0x0000
#define PM2F_BEING_RESET               (1L<<31)
#define PM2R_MEM_CONTROL               0x1040
#define PM2R_BOOT_ADDRESS              0x1080
#define PM2R_MEM_CONFIG                0x10c0
#define PM2F_MEM_CONFIG_RAM_MASK       (3L<<29)
#define PM2F_MEM_BANKS_1               0L
#define PM2F_MEM_BANKS_2               (1L<<29)
#define PM2F_MEM_BANKS_3               (2L<<29)
#define PM2F_MEM_BANKS_4               (3L<<29)
#define PM2R_CHIP_CONFIG               0x0070
#define PM2F_VGA_ENABLE                0x02
#define PM2F_VGA_FIXED                 0x04
#define PM2R_BYPASS_WRITE_MASK         0x1100
#define PM2R_FRAMEBUFFER_WRITE_MASK    0x1140
#define PM2R_FIFO_CONTROL              0x3078
#define PM2R_APERTURE_ONE              0x0050
#define PM2R_APERTURE_TWO              0x0058
#define PM2R_RASTERIZER_MODE           0x80a0
#define PM2R_DELTA_MODE                0x9300
#define PM2F_DELTA_ORDER_RGB           (1L<<18)
#define PM2R_LB_READ_FORMAT            0x8888
#define PM2R_LB_WRITE_FORMAT           0x88c8
#define PM2R_LB_READ_MODE              0x8880
#define PM2R_LB_SOURCE_OFFSET          0x8890
#define PM2R_FB_SOURCE_OFFSET          0x8a88
#define PM2R_FB_PIXEL_OFFSET           0x8a90
#define PM2R_FB_WINDOW_BASE            0x8ab0
#define PM2R_LB_WINDOW_BASE            0x88b8
#define PM2R_FB_SOFT_WRITE_MASK        0x8820
#define PM2R_FB_HARD_WRITE_MASK        0x8ac0
#define PM2R_FB_READ_PIXEL             0x8ad0
#define PM2R_DITHER_MODE               0x8818
#define PM2R_AREA_STIPPLE_MODE         0x81a0
#define PM2R_DEPTH_MODE                0x89a0
#define PM2R_STENCIL_MODE              0x8988
#define PM2R_TEXTURE_ADDRESS_MODE      0x8380
#define PM2R_TEXTURE_READ_MODE         0x8670
#define PM2R_TEXEL_LUT_MODE            0x8678
#define PM2R_YUV_MODE                  0x8f00
#define PM2R_COLOR_DDA_MODE            0x87e0
#define PM2R_TEXTURE_COLOR_MODE        0x8680
#define PM2R_FOG_MODE                  0x8690
#define PM2R_ALPHA_BLEND_MODE          0x8810
#define PM2R_LOGICAL_OP_MODE           0x8828
#define PM2R_STATISTICS_MODE           0x8c08
#define PM2R_SCISSOR_MODE              0x8180
#define PM2R_FILTER_MODE               0x8c00
#define PM2F_SYNCHRONIZATION           (1L<<10)
#define PM2R_RD_PIXEL_MASK             0x4010
#define PM2I_RD_MODE_CONTROL           0x19
#define PM2I_RD_CURSOR_CONTROL         0x06
#define PM2I_RD_MISC_CONTROL           0x1e
#define PM2F_RD_PALETTE_WIDTH_8        0x02
#define PM2I_RD_COLOR_KEY_CONTROL      0x40
#define PM2I_RD_OVERLAY_KEY            0x41
#define PM2I_RD_RED_KEY                0x42
#define PM2I_RD_GREEN_KEY              0x43
#define PM2I_RD_BLUE_KEY               0x44
#define PM2VI_RD_MISC_CONTROL          0x000
#define PM2F_APERTURE_STANDARD         0
#define PM2F_APERTURE_HALFWORDSWAP     2
#define PM2F_APERTURE_BYTESWAP         1
#define PM2R_RD_PALETTE_WRITE_ADDRESS  0x4000
#define PM2R_RD_PALETTE_DATA           0x4008
#define PM2R_RD_INDEXED_DATA           0x4050
#define PM2VR_RD_INDEX_LOW             0x4020
#define PM2VR_RD_INDEX_HIGH            0x4028
#define PM2VR_RD_INDEXED_DATA          0x4030
#define PM2F_PLL_LOCKED                0x10
#define PM2I_RD_MEMORY_CLOCK_3         0x32
#define PM2I_RD_MEMORY_CLOCK_1         0x30
#define PM2I_RD_MEMORY_CLOCK_2         0x31
#define PM2I_RD_MEMORY_CLOCK_STATUS    0x33
#define PM2I_RD_PIXEL_CLOCK_A3         0x22
#define PM2I_RD_PIXEL_CLOCK_A1         0x20
#define PM2I_RD_PIXEL_CLOCK_A2         0x21
#define PM2I_RD_PIXEL_CLOCK_STATUS     0x29
#define PM2R_VIDEO_CONTROL             0x3058
#define PM2F_HSYNC_MASK                0x18
#define PM2F_VSYNC_MASK                0x60
#define PM2F_HSYNC_ACT_HIGH            0x08
#define PM2F_HSYNC_ACT_LOW             0x18
#define PM2F_VSYNC_ACT_HIGH            0x20
#define PM2F_VSYNC_ACT_LOW             0x60
#define PM2F_DATA_64_ENABLE            0x00010000
#define PM2F_LINE_DOUBLE               0x04
#define PM2F_VIDEO_ENABLE              0x01
#define PM2R_FB_WRITE_MODE             0x8ab8
#define PM2F_FB_WRITE_ENABLE           0x01
#define PM2R_FB_READ_MODE              0x8a80
#define PM2R_TEXTURE_MAP_FORMAT        0x8588
#define PM2R_H_TOTAL                   0x3010
#define PM2R_HS_START                  0x3028
#define PM2R_HS_END                    0x3030
#define PM2R_HG_END                    0x3018
#define PM2R_HB_END                    0x3020
#define PM2R_V_TOTAL                   0x3038
#define PM2R_VS_START                  0x3048
#define PM2R_VS_END                    0x3050
#define PM2R_VB_END                    0x3040
#define PM2R_SCREEN_STRIDE             0x3008
#define PM2R_WINDOW_ORIGIN             0x81c8
#define PM2R_SCREEN_SIZE               0x8198
#define PM2R_SCREEN_BASE               0x3000
#define PM2R_SYNC                      0x8c40
#define PM2R_OUT_FIFO_WORDS            0x0020
#define PM2R_OUT_FIFO                  0x2000
#define PM2TAG(r)           (u32 )(((r)-0x8000)>>3)
#define PM2R_CONFIG                    0x8d90
#define PM2F_CONFIG_FB_WRITE_ENABLE            (1L<<3)
#define PM2F_CONFIG_FB_READ_SOURCE_ENABLE      (1L<<0)
#define PM2R_RECTANGLE_ORIGIN          0x80d0
#define PM2R_RECTANGLE_SIZE            0x80d8
#define PM2R_FB_BLOCK_COLOR            0x8ac8
#define PM2R_RENDER                    0x8038
#define PM2F_RENDER_RECTANGLE          (3L<<6)
#define PM2F_RENDER_FASTFILL           (1L<<3)
#define PM2F_RENDER_SYNC_ON_BIT_MASK   (1L<<11)
#define PM2F_INCREASE_X                (1L<<21)
#define PM2F_INCREASE_Y                (1L<<22)
#define PM2R_CONSTANT_COLOR            0x87e8
#define PM2R_BIT_MASK_PATTERN          0x8068
#define PM2F_SCREEN_SCISSOR_ENABLE     0x02
#define PM2R_SCISSOR_MIN_XY            0x8188
#define PM2R_SCISSOR_MAX_XY            0x8190
#define PM2F_TEXTEL_SIZE_16            0x00080000
#define PM2F_TEXTEL_SIZE_32            0x00100000
#define PM2F_TEXTEL_SIZE_24            0x00200000
#define PM2F_RD_COLOR_MODE_RGB         0x20
#define PM2F_RD_GUI_ACTIVE             0x10
#define PM2F_RD_TRUECOLOR              0x80
#define PM2F_RD_PIXELFORMAT_RGB565     0x06
#define PM2F_RD_PIXELFORMAT_RGBA8888   0x08
#define PM2F_RD_PIXELFORMAT_RGB888     0x09
#define PM2F_COLOR_KEY_TEST_OFF        (1L<<4)
#define PM2F_CURSORMODE_TYPE_X         (1 << 4)
#define PM2F_CURSORMODE_CURSOR_ENABLE  (1 << 0)
#define PM2I_RD_COLOR_MODE             0x18
#define PM2R_FIFO_DISCON               0x0068
#define PM2_REFERENCE_CLOCK    14318
#define PM2_MAX_PIXCLOCK       230000
#define PM2_REGS_SIZE          0x10000
#define PM2_PIXMAP_SIZE        (1600 * 4)

#define PM2VI_RD_MCLK_CONTROL          0x20D
#define PM2VI_RD_MCLK_PRESCALE         0x20E
#define PM2VI_RD_MCLK_FEEDBACK         0x20F
#define PM2VI_RD_MCLK_POSTSCALE        0x210
#define PM2VI_RD_CLK0_PRESCALE         0x201
#define PM2VI_RD_CLK0_FEEDBACK         0x202
#define PM2VI_RD_CLK0_POSTSCALE        0x203
#define PM2VI_RD_SYNC_CONTROL          0x001
#define PM2VI_RD_DAC_CONTROL           0x002
#define PM2VI_RD_PIXEL_SIZE            0x003
#define PM2VI_RD_COLOR_FORMAT          0x004
#define PM2VI_RD_OVERLAY_KEY           0x00D
#define PM2VI_RD_CURSOR_MODE           0x005
#define PM2VI_RD_CURSOR_X_LOW          0x007
#define PM2VI_RD_CURSOR_X_HIGH         0x008
#define PM2VI_RD_CURSOR_Y_LOW          0x009
#define PM2VI_RD_CURSOR_Y_HIGH         0x00A
#define PM2VI_RD_CURSOR_X_HOT          0x00B
#define PM2VI_RD_CURSOR_Y_HOT          0x00C
#define PM2VI_RD_CURSOR_PALETTE        0x303
#define PM2VI_RD_CURSOR_PATTERN        0x400
#define PM2R_RD_CURSOR_X_LSB           0x4060
#define PM2R_RD_CURSOR_X_MSB           0x4068
#define PM2R_RD_CURSOR_Y_LSB           0x4070
#define PM2R_RD_CURSOR_Y_MSB           0x4078
#define PM2R_RD_CURSOR_COLOR_ADDRESS   0x4020
#define PM2R_RD_CURSOR_COLOR_DATA      0x4028
#define PM2R_RD_CURSOR_DATA            0x4058
#define PM2R_FB_SOURCE_DELTA           0x8d88
#define CVPPC_MEMCLOCK         83000

/* PCI BAR layout */
#define BAR0_MMIO_SIZE 0x20000   /* 128KB */
#define BAR1_RAM_SIZE  0x800000  /* 8MB */

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

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;

    /* Register state */
    uint32_t regs[BAR0_MMIO_SIZE / sizeof(uint32_t)];
};

/* MMIO Handlers: implement basic register read/write */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= BAR0_MMIO_SIZE) {
        return ~0ULL;
    }

    /* Align down to 4-byte boundary, then extract based on size */
    hwaddr offset = addr & ~3;
    if (offset < BAR0_MMIO_SIZE) {
        uint32_t reg_val = s->regs[offset >> 2];
        switch (size) {
        case 1:
            val = (reg_val >> (8 * (addr & 3))) & 0xff;
            break;
        case 2:
            if (addr & 1) {
                qemu_log_mask(LOG_GUEST_ERROR, "pm2fb: unaligned 2-byte read at 0x%" HWADDR_PRIx "\n", addr);
                val = 0;
            } else {
                val = (reg_val >> (8 * (addr & 3))) & 0xffff;
            }
            break;
        case 4:
            if (addr & 3) {
                qemu_log_mask(LOG_GUEST_ERROR, "pm2fb: unaligned 4-byte read at 0x%" HWADDR_PRIx "\n", addr);
                val = 0;
            } else {
                val = reg_val;
            }
            break;
        case 8:
            if (addr & 3) {
                qemu_log_mask(LOG_GUEST_ERROR, "pm2fb: unaligned 8-byte read at 0x%" HWADDR_PRIx "\n", addr);
                val = 0;
            } else {
                uint32_t lo = reg_val;
                hwaddr next_offset = offset + 4;
                uint32_t hi = (next_offset < BAR0_MMIO_SIZE) ? s->regs[next_offset >> 2] : 0;
                val = ((uint64_t)hi << 32) | lo;
            }
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR, "pm2fb: unsupported read size %u at 0x%" HWADDR_PRIx "\n", size, addr);
            val = ~0ULL;
            break;
        }
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= BAR0_MMIO_SIZE) {
        return;
    }

    /* Align down to 4-byte boundary */
    hwaddr offset = addr & ~3;
    if (offset < BAR0_MMIO_SIZE) {
        uint32_t *reg = &s->regs[offset >> 2];
        uint32_t mask = 0;
        uint32_t shifted_val = 0;

        switch (size) {
        case 1:
            mask = 0xff << (8 * (addr & 3));
            shifted_val = (val & 0xff) << (8 * (addr & 3));
            break;
        case 2:
            if (addr & 1) {
                qemu_log_mask(LOG_GUEST_ERROR, "pm2fb: unaligned 2-byte write at 0x%" HWADDR_PRIx "\n", addr);
                return;
            }
            mask = 0xffff << (8 * (addr & 3));
            shifted_val = (val & 0xffff) << (8 * (addr & 3));
            break;
        case 4:
            if (addr & 3) {
                qemu_log_mask(LOG_GUEST_ERROR, "pm2fb: unaligned 4-byte write at 0x%" HWADDR_PRIx "\n", addr);
                return;
            }
            mask = 0xffffffff;
            shifted_val = (uint32_t)val;
            break;
        case 8:
            if (addr & 3) {
                qemu_log_mask(LOG_GUEST_ERROR, "pm2fb: unaligned 8-byte write at 0x%" HWADDR_PRIx "\n", addr);
                return;
            }
            {
                uint32_t lo = (uint32_t)(val & 0xffffffff);
                uint32_t hi = (uint32_t)(val >> 32);
                mask = 0xffffffff;
                shifted_val = lo;
                /* Write high word to next 4-byte offset */
                hwaddr next_offset = offset + 4;
                if (next_offset < BAR0_MMIO_SIZE) {
                    s->regs[next_offset >> 2] = hi;
                }
            }
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR, "pm2fb: unsupported write size %u at 0x%" HWADDR_PRIx "\n", size, addr);
            return;
        }

        /* Special handling for registers that require side effects */
        switch (offset) {
        case PM2R_RENDER:
            /* Render operation completes immediately: clear sync bit */
            shifted_val &= ~PM2F_RENDER_SYNC_ON_BIT_MASK;
            break;
        case PM2R_SYNC:
            /* Sync register: always clear to indicate completion */
            shifted_val = 0;
            break;
        default:
            break;
        }

        /* Update register: mask and merge */
        *reg = (*reg & ~mask) | (shifted_val & mask);
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    
    /* Clear all registers and set default values expected by the driver */
    memset(s->regs, 0, sizeof(s->regs));
    
    /* Register state that probe code relies on */
    s->regs[PM2R_RESET_STATUS >> 2] = 0;  /* not resetting */
    s->regs[PM2R_MEM_CONFIG >> 2] = PM2F_MEM_BANKS_2;  /* 2 banks -> 8MB */
    s->regs[PM2R_CHIP_CONFIG >> 2] = 0;  /* VGA disabled */
    s->regs[PM2R_BOOT_ADDRESS >> 2] = 0;
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
        /* Not used */
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PM2FB_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PM2FB_DEVICE_ID );
    pci_config_set_class(pci_conf, PM2FB_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization: BAR0 MMIO registers (128KB), BAR1 framebuffer RAM (8MB) */
    s->num_bars = 2;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = BAR0_MMIO_SIZE;
    s->bar_info[0].name = "pm2fb-bar0";
    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_RAM;
    s->bar_info[1].size = BAR1_RAM_SIZE;
    s->bar_info[1].name = "pm2fb-bar1";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
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

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "pm2fb_pci",
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
