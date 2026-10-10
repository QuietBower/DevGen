/*
 * QEMU Bochs DRM-compatible PCI device
 *
 * Implements the minimal Bochs/QEMU stdvga "dispi" interface and MMIO
 * layout needed by the Linux drivers/gpu/drm/tiny/bochs.c driver.
 *
 * Target QEMU: 8.2.10
 */

#include "qemu/osdep.h"
#include <inttypes.h>
#include <string.h>
#include "qemu/module.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "sysemu/dma.h"
#include "hw/irq.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/pci/pcie.h"
#include "qom/object.h"
#include "qapi/visitor.h"
#include "hw/qdev-properties.h"

#define TYPE_PCIBASE_DEVICE "bochs_drm_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define VBE_DISPI_INDEX_ID               0x0
#define VBE_DISPI_INDEX_XRES             0x1
#define VBE_DISPI_INDEX_YRES             0x2
#define VBE_DISPI_INDEX_BPP              0x3
#define VBE_DISPI_INDEX_ENABLE           0x4
#define VBE_DISPI_INDEX_BANK             0x5
#define VBE_DISPI_INDEX_VIRT_WIDTH       0x6
#define VBE_DISPI_INDEX_VIRT_HEIGHT      0x7
#define VBE_DISPI_INDEX_X_OFFSET         0x8
#define VBE_DISPI_INDEX_Y_OFFSET         0x9
#define VBE_DISPI_INDEX_VIDEO_MEMORY_64K 0xa
#define VBE_DISPI_ID0                    0xB0C0
#define VBE_DISPI_ID1                    0xB0C1
#define VBE_DISPI_ID2                    0xB0C2
#define VBE_DISPI_ID3                    0xB0C3
#define VBE_DISPI_ID4                    0xB0C4
#define VBE_DISPI_ID5                    0xB0C5
#define VBE_DISPI_DISABLED               0x00
#define VBE_DISPI_ENABLED                0x01
#define VBE_DISPI_GETCAPS                0x02
#define VBE_DISPI_8BIT_DAC               0x20
#define VBE_DISPI_LFB_ENABLED            0x40
#define VBE_DISPI_NOCLEARMEM             0x80
#define VBE_DISPI_IOPORT_INDEX           0x01CE
#define VBE_DISPI_IOPORT_DATA            0x01CF
#define BOCHS_VENDOR_ID                  0x1234
#define BOCHS_DEVICE_ID                  0x1111

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

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];

    uint8_t *mmio_backing;
    size_t mmio_backing_size;

    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;

    /* Dispi/VGA emulation state */
    uint16_t dispi_index;
    uint16_t dispi_regs[16];

    /* Simple frame buffer backing for BAR0 (VRAM) */
    MemoryRegion vram_region;
    hwaddr vram_size;

    /* MMIO BAR2 size cached */
    hwaddr mmio_size;
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
    .valid = { .min_access_size = 1, .max_access_size = 4 },
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

/* MMIO BAR2 layout implemented from driver usage:
 *  0x000-0x3ff : EDID buffer (bochs_get_edid_block)
 *  0x400-0x43f : VGA ports 0x3c0-0x3df mapped as bytes
 *  0x500-0x51f : dispi registers, 16-bit each: offset = 0x500 + (reg<<1)
 *  0x600       : qext_size (readl)
 *  0x604       : endian control (writel 0xbebebebe / 0x1e1e1e1e)
 */

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < 0x400) {
        /* EDID and other data; driver only reads bytes */
        if (size == 1 && s->mmio_backing && addr < s->mmio_backing_size) {
            return s->mmio_backing[addr];
        }
        return 0;
    }

    if (addr >= 0x400 && addr < 0x440) {
        /* VGA MMIO window for ports 0x3c0-0x3df. Driver uses only bochs_vga_readb */
        if (size == 1) {
            return 0;
        }
        return 0;
    }

    if (addr >= 0x500 && addr < 0x520) {
        /* dispi registers as 16-bit */
        if (size != 2) {
            return 0;
        }
        uint16_t reg = (addr - 0x500) >> 1;
        if (reg >= 16) {
            return 0;
        }
        return s->dispi_regs[reg];
    }

    if (addr == 0x600 && size == 4) {
        /* qext_size field */
        return s->mmio_size;
    }

    if (addr == 0x604 && size == 4) {
        /* endian control readback: keep as zero; driver only checks write pattern */
        return 0;
    }

    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < 0x400) {
        /* EDID / other read-only for our purposes; driver only reads */
        return;
    }

    if (addr >= 0x400 && addr < 0x440) {
        /* VGA MMIO window for ports 0x3c0-0x3df; driver uses writeb */
        if (size == 1) {
            if (s->mmio_backing && addr < s->mmio_backing_size) {
                s->mmio_backing[addr] = (uint8_t)val;
            }
        }
        return;
    }

    if (addr >= 0x500 && addr < 0x520) {
        if (size != 2) {
            return;
        }
        uint16_t reg = (addr - 0x500) >> 1;
        if (reg >= 16) {
            return;
        }
        uint16_t v = (uint16_t)val;
        s->dispi_regs[reg] = v;
        return;
    }

    if (addr == 0x600 && size == 4) {
        /* qext_size is read-only for driver; ignore writes */
        return;
    }

    if (addr == 0x604 && size == 4) {
        /* endian control: driver writes markers only; no other effect implemented */
        return;
    }
}

/* Bochs driver may fall back to I/O ports if MMIO BAR2 is not present.
 * For simplicity we emulate only the subset of ports used by the driver.
 */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr == (VBE_DISPI_IOPORT_INDEX & 0xfff) && size == 2) {
        return s->dispi_index;
    }
    if (addr == (VBE_DISPI_IOPORT_DATA & 0xfff) && size == 2) {
        uint16_t reg = s->dispi_index;
        if (reg < 16) {
            return s->dispi_regs[reg];
        }
        return 0;
    }

    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr == (VBE_DISPI_IOPORT_INDEX & 0xfff) && size == 2) {
        s->dispi_index = (uint16_t)val;
        return;
    }
    if (addr == (VBE_DISPI_IOPORT_DATA & 0xfff) && size == 2) {
        uint16_t reg = s->dispi_index;
        if (reg < 16) {
            s->dispi_regs[reg] = (uint16_t)val;
        }
        return;
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

    memset(s->dispi_regs, 0, sizeof(s->dispi_regs));
    s->dispi_index = 0;

    /* Initialize ID and video memory size */
    s->dispi_regs[VBE_DISPI_INDEX_ID] = VBE_DISPI_ID5;
    if (s->vram_size) {
        s->dispi_regs[VBE_DISPI_INDEX_VIDEO_MEMORY_64K] = (uint16_t)(s->vram_size / (64 * 1024));
    }
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
    pci_default_write_config(pdev, addr, val, len);
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    s->num_bars = 0;

    /* Set up VRAM (BAR0): 16 MiB */
    s->vram_size = 16 * 1024 * 1024;
    s->mmio_backing_size = s->vram_size;
    s->mmio_backing = g_malloc0(s->mmio_backing_size);

    memory_region_init_ram(&s->vram_region, OBJECT(s), "bochs-vram", s->vram_size, errp);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->vram_region);

    /* MMIO BAR2: map MMIO space used by driver (EDID, VGA, dispi, qext) */
    s->mmio_size = 0x1000;
    s->bar_info[s->num_bars].index = 2;
    s->bar_info[s->num_bars].type = BAR_TYPE_MMIO;
    s->bar_info[s->num_bars].size = s->mmio_size;
    s->bar_info[s->num_bars].name = "bochs-mmio";
    s->bar_info[s->num_bars].sparse = false;
    s->num_bars++;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  BOCHS_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  BOCHS_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_DISPLAY_OTHER);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 2); /* >= 2 so qext is used */
    pci_config_set_interrupt_pin(pci_conf, 1);

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) {
        return;
    }

    pcibase_reset(DEVICE(pdev));
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

