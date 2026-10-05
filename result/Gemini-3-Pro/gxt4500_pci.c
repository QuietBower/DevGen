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


#define TYPE_PCIBASE_DEVICE "gxt4500_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Related register/macros/constants from driver (Stage1) */
/* Example:
 *   #define VENDOR_ID 0x8086
 *   #define DEVICE_ID 0x19e3
 *   #define CLASS_ID   PCI_CLASS_OTHERS
 *   #define FACT_IRQ        0x00000001
 *   #define DMA_IRQ         0x00000100
 */
#define PCI_DEVICE_ID_IBM_GXT4500P	0x21c
#define PCI_DEVICE_ID_IBM_GXT6500P	0x21b
#define PCI_DEVICE_ID_IBM_GXT4000P	0x16e
#define PCI_DEVICE_ID_IBM_GXT6000P	0x170
#define CFG_ENDIAN0		0x40
#define STATUS			0x1000
#define CTRL_REG0		0x1004
#define CR0_HALT_DMA			0x4
#define CR0_RASTER_RESET		0x8
#define CR0_GEOM_RESET		0x10
#define CR0_MEM_CTRLER_RESET		0x20
#define FB_AB_CTRL		0x1100
#define FB_CD_CTRL		0x1104
#define FB_WID_CTRL		0x1108
#define FB_Z_CTRL		0x110c
#define FB_VGA_CTRL		0x1110
#define REFRESH_AB_CTRL		0x1114
#define REFRESH_CD_CTRL		0x1118
#define FB_OVL_CTRL		0x111c
#define FB_CTRL_TYPE			0x80000000
#define FB_CTRL_WIDTH_MASK		0x007f0000
#define FB_CTRL_WIDTH_SHIFT		16
#define FB_CTRL_START_SEG_MASK	0x00003fff
#define REFRESH_START		0x1098
#define REFRESH_SIZE		0x109c
#define DFA_FB_A		0x11e0
#define DFA_FB_B		0x11e4
#define DFA_FB_C		0x11e8
#define DFA_FB_D		0x11ec
#define DFA_FB_ENABLE			0x80000000
#define DFA_FB_BASE_MASK		0x03f00000
#define DFA_FB_STRIDE_1k		0x00000000
#define DFA_FB_STRIDE_2k		0x00000010
#define DFA_FB_STRIDE_4k		0x00000020
#define DFA_PIX_8BIT			0x00000000
#define DFA_PIX_16BIT_565		0x00000001
#define DFA_PIX_16BIT_1555		0x00000002
#define DFA_PIX_24BIT			0x00000004
#define DFA_PIX_32BIT			0x00000005
#define DTG_CONTROL		0x1900
#define DTG_CTL_SCREEN_REFRESH	2
#define DTG_CTL_ENABLE		1
#define DTG_HORIZ_EXTENT	0x1904
#define DTG_HORIZ_DISPLAY	0x1908
#define DTG_HSYNC_START		0x190c
#define DTG_HSYNC_END		0x1910
#define DTG_HSYNC_END_COMP	0x1914
#define DTG_VERT_EXTENT		0x1918
#define DTG_VERT_DISPLAY	0x191c
#define DTG_VSYNC_START		0x1920
#define DTG_VSYNC_END		0x1924
#define DTG_VERT_SHORT		0x1928
#define DISP_CTL		0x402c
#define DISP_CTL_OFF			2
#define SYNC_CTL		0x4034
#define SYNC_CTL_SYNC_ON_RGB		1
#define SYNC_CTL_SYNC_OFF		2
#define SYNC_CTL_HSYNC_INV		8
#define SYNC_CTL_VSYNC_INV		0x10
#define SYNC_CTL_HSYNC_OFF		0x20
#define SYNC_CTL_VSYNC_OFF		0x40
#define PLL_M			0x4040
#define PLL_N			0x4044
#define PLL_POSTDIV		0x4048
#define PLL_C			0x404c
#define CURSOR_X		0x4078
#define CURSOR_Y		0x407c
#define CURSOR_HOTSPOT		0x4080
#define CURSOR_MODE_OFF		0
#define CURSOR_MODE_4BPP		1
#define CURSOR_PIXMAP		0x5000
#define CURSOR_CMAP		0x7400
#define WAT_FMT			0x4100
#define WAT_FMT_24BIT			0
#define WAT_FMT_16BIT_565		1
#define WAT_FMT_16BIT_1555		2
#define WAT_FMT_32BIT			3
#define WAT_FMT_8BIT_332		9
#define WAT_FMT_8BIT			0xa
#define WAT_FMT_NO_CMAP		4
#define WAT_CMAP_OFFSET		0x4104
#define WAT_CTRL		0x4108
#define WAT_CTRL_SEL_B		1
#define WAT_CTRL_NO_INC		2
#define WAT_GAMMA_CTRL		0x410c
#define WAT_GAMMA_DISABLE		1
#define WAT_OVL_CTRL		0x430c
#define CMAP			0x6000
#define CURSOR_MODE		0x4084

#define GXT4500_MMIO_SIZE 0x20000

#define u32 uint32_t
#define __iomem

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
    
    
    /* DMA info placeholder */
    

    /* Register shadows */
    

    /* Status fields */
    

    /* reset/probe state */
    

    /* power mgmt */
    

    /* other fields */
    struct gxt4500_par {
        void *regs;
        int wc_cookie;
        int pixfmt;		/* pixel format, see DFA_PIX_* values */

        /* PLL parameters */
        int refclk_ps;		/* ref clock period in picoseconds */
        int pll_m;		/* ref clock divisor */
        int pll_n;		/* VCO divisor */
        int pll_pd1;		/* first post-divisor */
        int pll_pd2;		/* second post-divisor */

        u32 pseudo_palette[16];	/* used in color blits */
    } par;
};

enum gxt_cards {
	GXT4500P,
	GXT6500P,
	GXT4000P,
	GXT6000P
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
    uint64_t val = 0;

    if (s->mmio_backing && addr + size <= s->mmio_backing_size) {
        memcpy(&val, s->mmio_backing + addr, size);
    } else {
        qemu_log_mask(LOG_GUEST_ERROR, "[%s] mmio_read OOB addr=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (s->mmio_backing && addr + size <= s->mmio_backing_size) {
        memcpy(s->mmio_backing + addr, &val, size);
    } else {
        qemu_log_mask(LOG_GUEST_ERROR, "[%s] mmio_write OOB addr=%" PRIx64 " val=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, (uint64_t)val, size);
    }
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

/* ------------------------------------------------------------------ */
/* Reset       */
/* ------------------------------------------------------------------ */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    /* core resets */
    pci_device_reset(pdev);

    if (s->mmio_backing && s->mmio_backing_size)
        memset(s->mmio_backing, 0, s->mmio_backing_size);
}

/* ------------------------------------------------------------------ */
/* DMA initialize (called from realize)                                */
/* ------------------------------------------------------------------ */
static void pcibase_dma_device_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    
    (void)s;
    (void)errp;
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
    pci_default_write_config(pdev, addr, val, len);
    
    if (addr == CFG_ENDIAN0 || addr == CFG_ENDIAN0 + 8) {
        /* Endianness control write - handled by guest writing to config space */
    }
}

/* ------------------------------------------------------------------ */
/* Realize (device init)                                                */
/* ------------------------------------------------------------------ */
static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_IBM );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_IBM_GXT4500P );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_DISPLAY_OTHER );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);
    
    s->mmio_backing_size = GXT4500_MMIO_SIZE;
    s->mmio_backing = g_malloc0(s->mmio_backing_size);

    s->num_bars = 2;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = s->mmio_backing_size;
    s->bar_info[0].name = "gxt4500.mmio";

    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_RAM;
    s->bar_info[1].size = 32 * 1024 * 1024; /* 32MB VRAM */
    s->bar_info[1].name = "gxt4500.vram";

    /* register BARs */
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Interrupt config (placeholders) */
    

    /* DMA init (called here so device can set DMA masks/etc) */
    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) return;

    

    /* Example MSI:
     *   if (msi_init(pdev, 0, 1, true, false, errp) == 0) { s->has_msi = true; }
     * Example MSIX:
     *   if (msix_init(pdev, nvec, table_mem, table_bar_nr) == 0) { s->has_msix = true; }
     */

    /* optional: map legacy INTx behavior if driver expects it */
    

    

    /* Power management / other init */
    
    /* Example:
    *   pdev->pm_cap = true;
    */
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

    /* free mmio_backing or other allocations */
    

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

    /* optional */
    
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