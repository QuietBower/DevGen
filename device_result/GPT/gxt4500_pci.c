/*
 * QEMU PCI device model for IBM GXT4500P framebuffer
 * Implemented strictly from Linux driver drivers/video/fbdev/gxt4500.c
 * Target QEMU 8.2.x
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

#define TYPE_PCIBASE_DEVICE "gxt4500_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI_VENDOR_ID_IBM        0x1014
#define PCI_DEVICE_ID_IBM_GXT4500P 0x021c
#define PCI_DEVICE_ID_IBM_GXT6500P 0x021b
#define PCI_DEVICE_ID_IBM_GXT4000P 0x016e
#define PCI_DEVICE_ID_IBM_GXT6000P 0x0170
#define CFG_ENDIAN0 0x40
#define STATUS 0x1000
#define CTRL_REG0 0x1004
#define CR0_HALT_DMA 0x4
#define CR0_RASTER_RESET 0x8
#define CR0_GEOM_RESET 0x10
#define CR0_MEM_CTRLER_RESET 0x20
#define FB_AB_CTRL 0x1100
#define FB_CD_CTRL 0x1104
#define FB_WID_CTRL 0x1108
#define FB_Z_CTRL 0x110c
#define FB_VGA_CTRL 0x1110
#define REFRESH_AB_CTRL 0x1114
#define REFRESH_CD_CTRL 0x1118
#define FB_OVL_CTRL 0x111c
#define FB_CTRL_TYPE 0x80000000
#define FB_CTRL_WIDTH_MASK 0x007f0000
#define FB_CTRL_WIDTH_SHIFT 16
#define FB_CTRL_START_SEG_MASK 0x00003fff
#define REFRESH_START 0x1098
#define REFRESH_SIZE 0x109c
#define DFA_FB_A 0x11e0
#define DFA_FB_B 0x11e4
#define DFA_FB_C 0x11e8
#define DFA_FB_D 0x11ec
#define DFA_FB_ENABLE 0x80000000
#define DFA_FB_BASE_MASK 0x03f00000
#define DFA_FB_STRIDE_1k 0x00000000
#define DFA_FB_STRIDE_2k 0x00000010
#define DFA_FB_STRIDE_4k 0x00000020
#define DFA_PIX_8BIT 0x00000000
#define DFA_PIX_16BIT_565 0x00000001
#define DFA_PIX_16BIT_1555 0x00000002
#define DFA_PIX_24BIT 0x00000004
#define DFA_PIX_32BIT 0x00000005
#define DTG_CONTROL 0x1900
#define DTG_CTL_SCREEN_REFRESH 2
#define DTG_CTL_ENABLE 1
#define DTG_HORIZ_EXTENT 0x1904
#define DTG_HORIZ_DISPLAY 0x1908
#define DTG_HSYNC_START 0x190c
#define DTG_HSYNC_END 0x1910
#define DTG_HSYNC_END_COMP 0x1914
#define DTG_VERT_EXTENT 0x1918
#define DTG_VERT_DISPLAY 0x191c
#define DTG_VSYNC_START 0x1920
#define DTG_VSYNC_END 0x1924
#define DTG_VERT_SHORT 0x1928
#define DISP_CTL 0x402c
#define DISP_CTL_OFF 2
#define SYNC_CTL 0x4034
#define SYNC_CTL_SYNC_ON_RGB 1
#define SYNC_CTL_SYNC_OFF 2
#define SYNC_CTL_HSYNC_INV 8
#define SYNC_CTL_VSYNC_INV 0x10
#define SYNC_CTL_HSYNC_OFF 0x20
#define SYNC_CTL_VSYNC_OFF 0x40
#define PLL_M 0x4040
#define PLL_N 0x4044
#define PLL_POSTDIV 0x4048
#define PLL_C 0x404c
#define CURSOR_X 0x4078
#define CURSOR_Y 0x407c
#define CURSOR_HOTSPOT 0x4080
#define CURSOR_MODE_OFF 0
#define CURSOR_MODE_4BPP 1
#define CURSOR_PIXMAP 0x5000
#define CURSOR_CMAP 0x7400
#define WAT_FMT 0x4100
#define WAT_FMT_24BIT 0
#define WAT_FMT_16BIT_565 1
#define WAT_FMT_16BIT_1555 2
#define WAT_FMT_32BIT 3
#define WAT_FMT_8BIT_332 9
#define WAT_FMT_8BIT 0xa
#define WAT_FMT_NO_CMAP 4
#define WAT_CMAP_OFFSET 0x4104
#define WAT_CTRL 0x4108
#define WAT_CTRL_SEL_B 1
#define WAT_CTRL_NO_INC 2
#define WAT_GAMMA_CTRL 0x410c
#define WAT_GAMMA_DISABLE 1
#define WAT_OVL_CTRL 0x430c
#define CMAP 0x6000
#define CURSOR_MODE 0x4084

enum gxt_cards {
    GXT4500P,
    GXT6500P,
    GXT4000P,
    GXT6000P
};

struct gxt4500_par_shadow {
    uint32_t regs_space[0x20000 / 4];
    int pixfmt;
    int refclk_ps;
    int pll_m;
    int pll_n;
    int pll_pd1;
    int pll_pd2;
};

struct cardinfo_shadow {
    int refclk_ps;
    const char *cardname;
};

static const unsigned char pixsize[] QEMU_USED = {
    1, 2, 2, 2, 4, 4
};

static const unsigned char watfmt[] QEMU_USED = {
    WAT_FMT_8BIT, WAT_FMT_16BIT_565, WAT_FMT_16BIT_1555, 0,
    WAT_FMT_24BIT, WAT_FMT_32BIT
};

static const unsigned char mdivtab[] QEMU_USED = {
    0x3f, 0x00, 0x20, 0x10, 0x28, 0x14, 0x2a, 0x15, 0x0a,
    0x25, 0x32, 0x19, 0x0c, 0x26, 0x13, 0x09, 0x04, 0x22, 0x11,
    0x08, 0x24, 0x12, 0x29, 0x34, 0x1a, 0x2d, 0x36, 0x1b, 0x0d,
    0x06, 0x23, 0x31, 0x38, 0x1c, 0x2e, 0x17, 0x0b, 0x05, 0x02,
    0x21, 0x30, 0x18, 0x2c, 0x16, 0x2b, 0x35, 0x3a, 0x1d, 0x0e,
    0x27, 0x33, 0x39, 0x3c, 0x1e, 0x2f, 0x37, 0x3b, 0x3d, 0x3e,
    0x1f, 0x0f, 0x07, 0x03, 0x01,
};

static const unsigned char ndivtab[] QEMU_USED = {
    0x00, 0x80, 0xc0, 0xe0, 0xf0, 0x78, 0xbc, 0x5e,
    0x2f, 0x17, 0x0b, 0x85, 0xc2, 0xe1, 0x70, 0x38, 0x9c, 0x4e,
    0xa7, 0xd3, 0xe9, 0xf4, 0xfa, 0xfd, 0xfe, 0x7f, 0xbf, 0xdf,
    0xef, 0x77, 0x3b, 0x1d, 0x8e, 0xc7, 0xe3, 0x71, 0xb8, 0xdc,
    0x6e, 0xb7, 0x5b, 0x2d, 0x16, 0x8b, 0xc5, 0xe2, 0xf1, 0xf8,
    0xfc, 0x7e, 0x3f, 0x9f, 0xcf, 0x67, 0xb3, 0xd9, 0x6c, 0xb6,
    0xdb, 0x6d, 0x36, 0x9b, 0x4d, 0x26, 0x13, 0x89, 0xc4, 0x62,
    0xb1, 0xd8, 0xec, 0xf6, 0xfb, 0x7d, 0xbe, 0x5f, 0xaf, 0x57,
    0x2b, 0x95, 0x4a, 0x25, 0x92, 0x49, 0xa4, 0x52, 0x29, 0x94,
    0xca, 0x65, 0xb2, 0x59, 0x2c, 0x96, 0xcb, 0xe5, 0xf2, 0x79,
    0x3c, 0x1e, 0x0f, 0x07, 0x83, 0x41, 0x20, 0x90, 0x48, 0x24,
    0x12, 0x09, 0x84, 0x42, 0xa1, 0x50, 0x28, 0x14, 0x8a, 0x45,
    0xa2, 0xd1, 0xe8, 0x74, 0xba, 0xdd, 0xee, 0xf7, 0x7b, 0x3d,
    0x9e, 0x4f, 0x27, 0x93, 0xc9, 0xe4, 0x72, 0x39, 0x1c, 0x0e,
    0x87, 0xc3, 0x61, 0x30, 0x18, 0x8c, 0xc6, 0x63, 0x31, 0x98,
    0xcc, 0xe6, 0x73, 0xb9, 0x5c, 0x2e, 0x97, 0x4b, 0xa5, 0xd2,
    0x69,
};

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

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];

    uint8_t *mmio_backing;
    size_t mmio_backing_size;

    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;

    struct gxt4500_par_shadow par;

    MemoryRegion fb_region;
    uint64_t fb_size;
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

static const MemoryRegionOps gxt4500_fb_ops QEMU_USED = {
    .read = NULL,
    .write = NULL,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

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

static inline uint32_t mmio_reg_read(PCIBaseState *s, hwaddr addr)
{
    if (addr + 4 > s->mmio_backing_size) {
        return 0;
    }
    return le32_to_cpu(*(uint32_t *)(s->mmio_backing + addr));
}

static inline void mmio_reg_write(PCIBaseState *s, hwaddr addr, uint32_t val)
{
    if (addr + 4 > s->mmio_backing_size) {
        return;
    }
    *(uint32_t *)(s->mmio_backing + addr) = cpu_to_le32(val);
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size == 4 && addr + 4 <= s->mmio_backing_size) {
        return mmio_reg_read(s, addr);
    }

    qemu_log_mask(LOG_UNIMP, "[%s] mmio_read addr=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size == 4 && addr + 4 <= s->mmio_backing_size) {
        mmio_reg_write(s, addr, (uint32_t)val);
        return;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] mmio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, (uint64_t)val, size);
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "[%s] pio_read addr=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "[%s] pio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, (uint64_t)val, size);
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    pci_device_reset(pdev);

    if (s->mmio_backing && s->mmio_backing_size) {
        memset(s->mmio_backing, 0, s->mmio_backing_size);
    }

    s->par.pixfmt = DFA_PIX_8BIT;
    s->par.refclk_ps = 0;
    s->par.pll_m = 0;
    s->par.pll_n = 0;
    s->par.pll_pd1 = 0;
    s->par.pll_pd2 = 0;
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

    pci_default_write_config(pdev, addr, val, len);
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_IBM);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_IBM_GXT4500P);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_DISPLAY_OTHER);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    s->mmio_backing_size = 0x20000;
    s->mmio_backing = g_malloc0(s->mmio_backing_size);

    s->num_bars = 2;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x20000;
    s->bar_info[0].name = "gxt4500_mmio";
    s->bar_info[0].sparse = false;

    s->fb_size = 8 * 1024 * 1024;
    memory_region_init_ram(&s->fb_region, OBJECT(s), "gxt4500_fb", s->fb_size, errp);
    if (errp && *errp) {
        return;
    }
    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_RAM;
    s->bar_info[1].size = s->fb_size;
    s->bar_info[1].name = "gxt4500_fb";
    s->bar_info[1].sparse = false;

    for (int i = 0; i < s->num_bars; i++) {
        if (s->bar_info[i].type == BAR_TYPE_RAM && s->bar_info[i].index == 1) {
            pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->fb_region);
        } else {
            pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
        }
    }

    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) {
        return;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] device realized\n", TYPE_PCIBASE_DEVICE);
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
    }

    qemu_log_mask(LOG_UNIMP, "[%s] device uninit\n", TYPE_PCIBASE_DEVICE);
}

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

