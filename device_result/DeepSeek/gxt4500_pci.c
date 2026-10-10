/*
 * QEMU GXT4500 framebuffer device model
 * Based on Linux driver drivers/video/fbdev/gxt4500.c
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/module.h"
#include "qemu/log.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "migration/vmstate.h"
#include "qapi/error.h"

#define TYPE_GXT4500_DEVICE "gxt4500_pci"
OBJECT_DECLARE_SIMPLE_TYPE(GXT4500State, GXT4500_DEVICE)

#define VENDOR_ID 0x1014
#define DEVICE_ID 0x021c
#define CLASS_ID  0x030000

/* MMIO register offsets */
#define STATUS          0x1000
#define CTRL_REG0       0x1004
#define REFRESH_START   0x1098
#define REFRESH_SIZE    0x109c
#define FB_AB_CTRL      0x1100
#define FB_CD_CTRL      0x1104
#define REFRESH_AB_CTRL 0x1114
#define REFRESH_CD_CTRL 0x1118
#define DFA_FB_A        0x11e0
#define DTG_CONTROL     0x1900
#define DTG_HORIZ_EXTENT 0x1904
#define DTG_HORIZ_DISPLAY 0x1908
#define DTG_HSYNC_START 0x190c
#define DTG_HSYNC_END   0x1910
#define DTG_HSYNC_END_COMP 0x1914
#define DTG_VERT_EXTENT 0x1918
#define DTG_VERT_DISPLAY 0x191c
#define DTG_VSYNC_START 0x1920
#define DTG_VSYNC_END   0x1924
#define DTG_VERT_SHORT  0x1928
#define DISP_CTL        0x402c
#define SYNC_CTL        0x4034
#define PLL_M           0x4040
#define PLL_N           0x4044
#define PLL_POSTDIV     0x4048
#define PLL_C           0x404c
#define CURSOR_MODE     0x4084

struct GXT4500State {
    PCIDevice parent_obj;

    MemoryRegion bar_mmio;
    MemoryRegion bar_fb;

    /* Shadow registers */
    uint32_t ctrl_reg0;
    uint32_t refresh_start;
    uint32_t refresh_size;
    uint32_t fb_ab_ctrl;
    uint32_t fb_cd_ctrl;
    uint32_t refresh_ab_ctrl;
    uint32_t refresh_cd_ctrl;
    uint32_t dfa_fb_a;
    uint32_t dtg_control;
    uint32_t dtg_horiz_extent;
    uint32_t dtg_horiz_display;
    uint32_t dtg_hsync_start;
    uint32_t dtg_hsync_end;
    uint32_t dtg_hsync_end_comp;
    uint32_t dtg_vert_extent;
    uint32_t dtg_vert_display;
    uint32_t dtg_vsync_start;
    uint32_t dtg_vsync_end;
    uint32_t dtg_vert_short;
    uint32_t disp_ctl;
    uint32_t sync_ctl;
    uint32_t pll_m;
    uint32_t pll_n;
    uint32_t pll_postdiv;
    uint32_t pll_c;
    uint32_t cursor_mode;

    /* WAT: 32 entries x 4 registers per entry */
    uint32_t wat_fmt[32];
    uint32_t wat_cmap_offset[32];
    uint32_t wat_ctrl[32];
    uint32_t wat_gamma_ctrl[32];

    /* Color map: 1024 entries */
    uint32_t cmap[1024];
};

static uint64_t gxt4500_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    GXT4500State *s = GXT4500_DEVICE(opaque);
    uint32_t val = 0;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR, "gxt4500: bad read size %u at 0x%"HWADDR_PRIx"\n", size, addr);
        return ~0ULL;
    }

    switch (addr) {
    case CTRL_REG0:
        val = s->ctrl_reg0;
        break;
    case REFRESH_START:
        val = s->refresh_start;
        break;
    case REFRESH_SIZE:
        val = s->refresh_size;
        break;
    case FB_AB_CTRL:
        val = s->fb_ab_ctrl;
        break;
    case FB_CD_CTRL:
        val = s->fb_cd_ctrl;
        break;
    case REFRESH_AB_CTRL:
        val = s->refresh_ab_ctrl;
        break;
    case REFRESH_CD_CTRL:
        val = s->refresh_cd_ctrl;
        break;
    case DFA_FB_A:
        val = s->dfa_fb_a;
        break;
    case DTG_CONTROL:
        val = s->dtg_control;
        break;
    case DTG_HORIZ_EXTENT:
        val = s->dtg_horiz_extent;
        break;
    case DTG_HORIZ_DISPLAY:
        val = s->dtg_horiz_display;
        break;
    case DTG_HSYNC_START:
        val = s->dtg_hsync_start;
        break;
    case DTG_HSYNC_END:
        val = s->dtg_hsync_end;
        break;
    case DTG_HSYNC_END_COMP:
        val = s->dtg_hsync_end_comp;
        break;
    case DTG_VERT_EXTENT:
        val = s->dtg_vert_extent;
        break;
    case DTG_VERT_DISPLAY:
        val = s->dtg_vert_display;
        break;
    case DTG_VSYNC_START:
        val = s->dtg_vsync_start;
        break;
    case DTG_VSYNC_END:
        val = s->dtg_vsync_end;
        break;
    case DTG_VERT_SHORT:
        val = s->dtg_vert_short;
        break;
    case DISP_CTL:
        val = s->disp_ctl;
        break;
    case SYNC_CTL:
        val = s->sync_ctl;
        break;
    case PLL_C:
        val = s->pll_c;
        break;
    case CURSOR_MODE:
        val = s->cursor_mode;
        break;
    default:
        /* WAT registers at 0x4100..0x42fc */
        if (addr >= 0x4100 && addr <= 0x42fc) {
            int idx = (addr - 0x4100) >> 4;
            int sub = (addr & 0xf) >> 2;
            switch (sub) {
            case 0: val = s->wat_fmt[idx]; break;
            case 1: val = s->wat_cmap_offset[idx]; break;
            case 2: val = s->wat_ctrl[idx]; break;
            case 3: val = s->wat_gamma_ctrl[idx]; break;
            }
        } else if (addr >= 0x6000 && addr <= 0x6fff) {
            /* CMAP at 0x6000..0x6fff (4-byte each) */
            int idx = (addr - 0x6000) >> 2;
            val = s->cmap[idx];
        } else {
            qemu_log_mask(LOG_UNIMP, "gxt4500: unimplemented read at 0x%"HWADDR_PRIx"\n", addr);
        }
        break;
    }
    return val;
}

static void gxt4500_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                               unsigned size)
{
    GXT4500State *s = GXT4500_DEVICE(opaque);

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR, "gxt4500: bad write size %u at 0x%"HWADDR_PRIx"\n", size, addr);
        return;
    }

    switch (addr) {
    case CTRL_REG0:
        s->ctrl_reg0 = val;
        break;
    case REFRESH_START:
        s->refresh_start = val;
        break;
    case REFRESH_SIZE:
        s->refresh_size = val;
        break;
    case FB_AB_CTRL:
        s->fb_ab_ctrl = val;
        break;
    case FB_CD_CTRL:
        s->fb_cd_ctrl = val;
        break;
    case REFRESH_AB_CTRL:
        s->refresh_ab_ctrl = val;
        break;
    case REFRESH_CD_CTRL:
        s->refresh_cd_ctrl = val;
        break;
    case DFA_FB_A:
        s->dfa_fb_a = val;
        break;
    case DTG_CONTROL:
        s->dtg_control = val;
        break;
    case DTG_HORIZ_EXTENT:
        s->dtg_horiz_extent = val;
        break;
    case DTG_HORIZ_DISPLAY:
        s->dtg_horiz_display = val;
        break;
    case DTG_HSYNC_START:
        s->dtg_hsync_start = val;
        break;
    case DTG_HSYNC_END:
        s->dtg_hsync_end = val;
        break;
    case DTG_HSYNC_END_COMP:
        s->dtg_hsync_end_comp = val;
        break;
    case DTG_VERT_EXTENT:
        s->dtg_vert_extent = val;
        break;
    case DTG_VERT_DISPLAY:
        s->dtg_vert_display = val;
        break;
    case DTG_VSYNC_START:
        s->dtg_vsync_start = val;
        break;
    case DTG_VSYNC_END:
        s->dtg_vsync_end = val;
        break;
    case DTG_VERT_SHORT:
        s->dtg_vert_short = val;
        break;
    case DISP_CTL:
        s->disp_ctl = val;
        break;
    case SYNC_CTL:
        s->sync_ctl = val;
        break;
    case PLL_M:
        s->pll_m = val;
        break;
    case PLL_N:
        s->pll_n = val;
        break;
    case PLL_POSTDIV:
        s->pll_postdiv = val;
        break;
    case PLL_C:
        s->pll_c = val;
        break;
    case CURSOR_MODE:
        s->cursor_mode = val;
        break;
    default:
        /* WAT registers */
        if (addr >= 0x4100 && addr <= 0x42fc) {
            int idx = (addr - 0x4100) >> 4;
            int sub = (addr & 0xf) >> 2;
            switch (sub) {
            case 0: s->wat_fmt[idx] = val; break;
            case 1: s->wat_cmap_offset[idx] = val; break;
            case 2: s->wat_ctrl[idx] = val; break;
            case 3: s->wat_gamma_ctrl[idx] = val; break;
            }
        } else if (addr >= 0x6000 && addr <= 0x6fff) {
            int idx = (addr - 0x6000) >> 2;
            s->cmap[idx] = val;
        } else {
            qemu_log_mask(LOG_UNIMP, "gxt4500: unimplemented write at 0x%"HWADDR_PRIx"\n", addr);
        }
        break;
    }
}

static const MemoryRegionOps gxt4500_mmio_ops = {
    .read = gxt4500_mmio_read,
    .write = gxt4500_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void gxt4500_reset(DeviceState *dev)
{
    GXT4500State *s = GXT4500_DEVICE(dev);

    /* Zero all registers */
    memset(&s->ctrl_reg0, 0, (uintptr_t)&s->cmap[1024] - (uintptr_t)&s->ctrl_reg0);
}

static void gxt4500_realize(PCIDevice *pdev, Error **errp)
{
    GXT4500State *s = GXT4500_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 0); /* No interrupt used */

    /* MMIO BAR 0 */
    memory_region_init_io(&s->bar_mmio, OBJECT(s), &gxt4500_mmio_ops, s,
                          "gxt4500-mmio", 0x20000);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_mmio);

    /* Framebuffer BAR 1 */
    memory_region_init_ram(&s->bar_fb, OBJECT(s), "gxt4500-fb", 0x1000000,
                           errp);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_fb);

    /* Initialize all registers to zero */
    memset(&s->ctrl_reg0, 0, (uintptr_t)&s->cmap[1024] - (uintptr_t)&s->ctrl_reg0);
}

static void gxt4500_uninit(PCIDevice *pdev)
{
    /* Nothing to clean up */
}

static const VMStateDescription vmstate_gxt4500 = {
    .name = "gxt4500_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, GXT4500State),
        VMSTATE_UINT32(ctrl_reg0, GXT4500State),
        VMSTATE_UINT32(refresh_start, GXT4500State),
        VMSTATE_UINT32(refresh_size, GXT4500State),
        VMSTATE_UINT32(fb_ab_ctrl, GXT4500State),
        VMSTATE_UINT32(fb_cd_ctrl, GXT4500State),
        VMSTATE_UINT32(refresh_ab_ctrl, GXT4500State),
        VMSTATE_UINT32(refresh_cd_ctrl, GXT4500State),
        VMSTATE_UINT32(dfa_fb_a, GXT4500State),
        VMSTATE_UINT32(dtg_control, GXT4500State),
        VMSTATE_UINT32(dtg_horiz_extent, GXT4500State),
        VMSTATE_UINT32(dtg_horiz_display, GXT4500State),
        VMSTATE_UINT32(dtg_hsync_start, GXT4500State),
        VMSTATE_UINT32(dtg_hsync_end, GXT4500State),
        VMSTATE_UINT32(dtg_hsync_end_comp, GXT4500State),
        VMSTATE_UINT32(dtg_vert_extent, GXT4500State),
        VMSTATE_UINT32(dtg_vert_display, GXT4500State),
        VMSTATE_UINT32(dtg_vsync_start, GXT4500State),
        VMSTATE_UINT32(dtg_vsync_end, GXT4500State),
        VMSTATE_UINT32(dtg_vert_short, GXT4500State),
        VMSTATE_UINT32(disp_ctl, GXT4500State),
        VMSTATE_UINT32(sync_ctl, GXT4500State),
        VMSTATE_UINT32(pll_m, GXT4500State),
        VMSTATE_UINT32(pll_n, GXT4500State),
        VMSTATE_UINT32(pll_postdiv, GXT4500State),
        VMSTATE_UINT32(pll_c, GXT4500State),
        VMSTATE_UINT32(cursor_mode, GXT4500State),
        VMSTATE_UINT32_ARRAY(wat_fmt, GXT4500State, 32),
        VMSTATE_UINT32_ARRAY(wat_cmap_offset, GXT4500State, 32),
        VMSTATE_UINT32_ARRAY(wat_ctrl, GXT4500State, 32),
        VMSTATE_UINT32_ARRAY(wat_gamma_ctrl, GXT4500State, 32),
        VMSTATE_UINT32_ARRAY(cmap, GXT4500State, 1024),
        VMSTATE_END_OF_LIST()
    }
};

static void gxt4500_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = gxt4500_realize;
    k->exit = gxt4500_uninit;
    dc->reset = gxt4500_reset;
    dc->vmsd = &vmstate_gxt4500;
    set_bit(DEVICE_CATEGORY_DISPLAY, dc->categories);
}

static const TypeInfo gxt4500_info = {
    .name          = TYPE_GXT4500_DEVICE,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(GXT4500State),
    .class_init    = gxt4500_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void gxt4500_register_types(void)
{
    type_register_static(&gxt4500_info);
}

type_init(gxt4500_register_types)