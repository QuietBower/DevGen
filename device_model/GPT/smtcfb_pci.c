/*
 * QEMU PCI device model for Silicon Motion SM712/SM720 framebuffer (minimal)
 *
 * Implemented strictly from Linux sm712fb.c driver behavior.
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

#define TYPE_PCIBASE_DEVICE "smtcfb_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define SIZE_SR30_SR75      (0x75 - 0x30 + 1)
#define SIZE_SR10_SR24      (0x24 - 0x10 + 1)
#define SIZE_SR80_SR93      (0x93 - 0x80 + 1)
#define SIZE_SR00_SR04      (0x04 - 0x00 + 1)
#define SIZE_AR00_AR14      (0x14 - 0x00 + 1)
#define SIZE_CR00_CR18      (0x18 - 0x00 + 1)
#define SIZE_SRA0_SRAF      (0xAF - 0xA0 + 1)
#define SIZE_CR30_CR4D      (0x4D - 0x30 + 1)
#define SIZE_CR90_CRA7      (0xA7 - 0x90 + 1)
#define SIZE_GR00_GR08      (0x08 - 0x00 + 1)
#define SCREEN_X_RES          1024
#define SCREEN_Y_RES_NETBOOK  600
#define SCREEN_Y_RES_PC       768
#define SCREEN_BPP            16
#define FB_ACCEL_SMI_LYNX 88

struct smtcfb_screen_info {
    uint16_t lfb_width;
    uint16_t lfb_height;
    uint16_t lfb_depth;
};

/*
 * The Linux driver struct is copied here only for reference of field
 * layout/macros; it is not used by QEMU runtime logic. To make this
 * compile in QEMU (which lacks __iomem and kernel struct types), we
 * replace kernel-specific pointer annotations with plain void *.
 */
struct smtcfb_info {
    struct pci_dev *pdev;
    struct fb_info *fb;
    uint16_t chip_id;
    uint8_t  chip_rev_id;

    void *lfb;     /* linear frame buffer */
    void *dp_regs; /* drawing processor control regs */
    void *vp_regs; /* video processor control regs */
    void *cp_regs; /* capture processor control regs */
    void *mmio;    /* memory map IO port */

    unsigned int width;
    unsigned int height;
    unsigned int hz;

    uint32_t colreg[17];
};

struct vesa_mode {
    char index[6];
    uint16_t lfb_width;
    uint16_t lfb_height;
    uint16_t lfb_depth;
};

struct modeinit {
    int mmsizex;
    int mmsizey;
    int bpp;
    int hz;
    unsigned char init_misc;
    unsigned char init_sr00_sr04[SIZE_SR00_SR04];
    unsigned char init_sr10_sr24[SIZE_SR10_SR24];
    unsigned char init_sr30_sr75[SIZE_SR30_SR75];
    unsigned char init_sr80_sr93[SIZE_SR80_SR93];
    unsigned char init_sra0_sraf[SIZE_SRA0_SRAF];
    unsigned char init_gr00_gr08[SIZE_GR00_GR08];
    unsigned char init_ar00_ar14[SIZE_AR00_AR14];
    unsigned char init_cr00_cr18[SIZE_CR00_CR18];
    unsigned char init_cr30_cr4d[SIZE_CR30_CR4D];
    unsigned char init_cr90_cra7[SIZE_CR90_CRA7];
};

#define FB_ACTIVATE_NOW           0
#define FB_VMODE_NONINTERLACED    0
#define FB_ACCELF_TEXT            1
#define FB_TYPE_PACKED_PIXELS     0
#define FB_VISUAL_TRUECOLOR       2

/* New driver macros / constants from this iteration */
#define DAC_REG 0x3c8
#define DAC_VAL 0x3c9
#define MMIO_ADDR 0x00c00000


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

    /* Simple internal register/model state for VGA-ish ports */
    uint8_t seq_index;
    uint8_t crtc_index;
    uint8_t grph_index;
    uint8_t attr_index;
    bool attr_flip_flop;

    uint8_t seq_regs[0x100];
    uint8_t crtc_regs[0x100];
    uint8_t grph_regs[0x100];
    uint8_t attr_regs[0x100];

    /* Framebuffer RAM is inside BAR0; we maintain an offset/size */
    hwaddr fb_offset;
    hwaddr fb_size;
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

/* Helper for access into mmio_backing (BAR0) */
static uint32_t pcibase_mmio_backing_read(PCIBaseState *s, hwaddr addr, unsigned size)
{
    uint32_t val = 0;
    if (!s->mmio_backing || !s->mmio_backing_size) {
        return 0;
    }
    if (addr + size > s->mmio_backing_size) {
        return 0;
    }
    memcpy(&val, s->mmio_backing + addr, size);
    return val;
}

static void pcibase_mmio_backing_write(PCIBaseState *s, hwaddr addr, uint64_t val, unsigned size)
{
    if (!s->mmio_backing || !s->mmio_backing_size) {
        return;
    }
    if (addr + size > s->mmio_backing_size) {
        return;
    }
    memcpy(s->mmio_backing + addr, &val, size);
}

/* PIO helpers: we map the legacy VGA ports in BAR0 PIO window 0x3c0-0x3df
 * to internal register arrays. These are used by sm712fb via outb_p/inb_p. */

static uint8_t pcibase_pio_readb_port(PCIBaseState *s, hwaddr port)
{
    switch (port) {
    case 0x3c4: /* Sequencer index */
        return s->seq_index;
    case 0x3c5: /* Sequencer data */
        return s->seq_regs[s->seq_index];
    case 0x3d4: /* CRTC index */
        return s->crtc_index;
    case 0x3d5: /* CRTC data */
        return s->crtc_regs[s->crtc_index];
    case 0x3ce: /* Graphics index */
        return s->grph_index;
    case 0x3cf: /* Graphics data */
        return s->grph_regs[s->grph_index];
    case 0x3da:
        /* Input status 1 - toggles attribute flip-flop */
        s->attr_flip_flop = false;
        return 0;
    case 0x3c0:
        /* Attribute controller index/data port, reading usually not used */
        if (!s->attr_flip_flop) {
            return 0; /* index */
        } else {
            return s->attr_regs[s->attr_index];
        }
    case 0x3c1:
        /* Attribute controller data read */
        return s->attr_regs[s->attr_index];
    case DAC_REG: /* DAC index register (0x3c8) */
        /* driver uses readb on smtc_regbaseaddress + dac_reg; map to 0 */
        return 0x00;
    case DAC_VAL: /* DAC data register (0x3c9) */
        /* return last written DAC value if we ever store it; otherwise 0 */
        return 0x00;
    default:
        return 0xff;
    }
}

static void pcibase_pio_writeb_port(PCIBaseState *s, hwaddr port, uint8_t val)
{
    switch (port) {
    case 0x3c4: /* Sequencer index */
        s->seq_index = val;
        break;
    case 0x3c5: /* Sequencer data */
        s->seq_regs[s->seq_index] = val;
        break;
    case 0x3d4: /* CRTC index */
        s->crtc_index = val;
        break;
    case 0x3d5: /* CRTC data */
        s->crtc_regs[s->crtc_index] = val;
        break;
    case 0x3ce: /* Graphics index */
        s->grph_index = val;
        break;
    case 0x3cf: /* Graphics data */
        s->grph_regs[s->grph_index] = val;
        break;
    case 0x3c0:
        /* Attribute controller combined index/data port */
        if (!s->attr_flip_flop) {
            s->attr_index = val & 0x1f;
        } else {
            s->attr_regs[s->attr_index] = val;
        }
        s->attr_flip_flop = !s->attr_flip_flop;
        break;
    case DAC_REG: /* DAC index */
        /* The driver may program palette index here; we accept and ignore */
        break;
    case DAC_VAL: /* DAC value */
        /* The driver may write RGB components here; accept and ignore */
        break;
    default:
        break;
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    /* For simplicity, BAR0 covers framebuffer and other MMIO; we just
     * provide backing store. The driver uses ioremap() and writel()/readl(),
     * memset_io() on that region.
     *
     * The driver macro smtc_mmiorb(reg) maps to readb(smtc_regbaseaddress + reg)
     * where smtc_regbaseaddress is MMIO base at offset MMIO_ADDR (0x00c00000)
     * into BAR0. We therefore treat accesses to that region as normal
     * mmio_backing reads.
     */
    return pcibase_mmio_backing_read(s, addr, size);
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    pcibase_mmio_backing_write(s, addr, val, size);
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size == 1) {
        hwaddr port = addr;
        return pcibase_pio_readb_port(s, port);
    }

    return 0xff;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size == 1) {
        hwaddr port = addr;
        pcibase_pio_writeb_port(s, port, (uint8_t)val);
    }
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    pci_device_reset(pdev);

    if (s->mmio_backing && s->mmio_backing_size) {
        memset(s->mmio_backing, 0, s->mmio_backing_size);
    }

    memset(s->seq_regs, 0, sizeof(s->seq_regs));
    memset(s->crtc_regs, 0, sizeof(s->crtc_regs));
    memset(s->grph_regs, 0, sizeof(s->grph_regs));
    memset(s->attr_regs, 0, sizeof(s->attr_regs));

    s->seq_index = 0;
    s->crtc_index = 0;
    s->grph_index = 0;
    s->attr_index = 0;
    s->attr_flip_flop = false;
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

    /* IDs from driver table: vendor 0x126f, device 0x0710 (SM712) */
    pci_set_word(pci_conf + PCI_VENDOR_ID,   0x126f);
    pci_set_word(pci_conf + PCI_DEVICE_ID,   0x0710);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_DISPLAY_VGA);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Define BAR0 as 16MB MMIO RAM to cover VRAM + registers used by driver */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x01000000; /* 16 MiB */
    s->bar_info[0].name = "sm712-mmio";
    s->bar_info[0].sparse = false;

    s->mmio_backing_size = s->bar_info[0].size;
    s->mmio_backing = g_malloc0(s->mmio_backing_size);

    /* FB offset/size used by driver cases (4 MB VRAM typical for SM712) */
    s->fb_offset = 0;          /* framebuffer base inside BAR0 */
    s->fb_size   = 0x00400000; /* 4 MiB */

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
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

