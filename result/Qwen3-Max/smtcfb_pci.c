/*
 * QEMU SM712/SM720 PCI VGA Device Emulation
 *
 * Based on Linux driver: /linux-6.18/drivers/video/fbdev/sm712fb.c
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

/* Related register/macros/constants from driver (Stage1) */
#define VENDOR_ID 0x126f
#define DEVICE_ID 0x710
#define CLASS_ID   PCI_CLASS_DISPLAY_VGA

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
    uint8_t seq_index;
    uint8_t crtc_index;
    uint8_t grph_index;
    uint8_t attr_index;
    uint8_t attr_flip_flop;

    /* VRAM size */
    uint32_t vram_size;
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

    /* For SM712, framebuffer is at offset 0, MMIO at 0x400000 */
    if (addr >= 0x400000 && addr < 0x800000) {
        /* MMIO region */
        hwaddr mmio_offset = addr - 0x400000;
        if (mmio_offset >= s->mmio_backing_size) {
            return 0;
        }
        switch (size) {
        case 1:
            return s->mmio_backing[mmio_offset];
        case 2:
            return lduw_le_p(&s->mmio_backing[mmio_offset]);
        case 4:
            return ldl_le_p(&s->mmio_backing[mmio_offset]);
        default:
            return 0;
        }
    }

    qemu_log_mask(LOG_UNIMP, "[smtcfb_pci] mmio_read addr=%" PRIx64 " size=%u\n", (uint64_t)addr, size);
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* For SM712, framebuffer is at offset 0, MMIO at 0x400000 */
    if (addr >= 0x400000 && addr < 0x800000) {
        /* MMIO region */
        hwaddr mmio_offset = addr - 0x400000;
        if (mmio_offset >= s->mmio_backing_size) {
            return;
        }
        switch (size) {
        case 1:
            s->mmio_backing[mmio_offset] = val;
            break;
        case 2:
            stw_le_p(&s->mmio_backing[mmio_offset], val);
            break;
        case 4:
            stl_le_p(&s->mmio_backing[mmio_offset], val);
            break;
        }
        return;
    }

    qemu_log_mask(LOG_UNIMP, "[smtcfb_pci] mmio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n", (uint64_t)addr, (uint64_t)val, size);
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case 0x3c0: /* Attribute Controller */
        if (s->attr_flip_flop) {
            /* Data read */
            val = 0; /* Simplified */
        } else {
            /* Index read */
            val = s->attr_index;
        }
        break;
    case 0x3c1: /* Attribute Controller Data */
        val = 0; /* Simplified */
        break;
    case 0x3c2: /* Miscellaneous Output */
        val = 0; /* Simplified */
        break;
    case 0x3c4: /* Sequencer Index */
        val = s->seq_index;
        break;
    case 0x3c5: /* Sequencer Data */
        val = 0; /* Simplified */
        break;
    case 0x3c6: /* Pixel Mask */
        val = 0xff; /* Default */
        break;
    case 0x3c7: /* DAC Read Address */
        val = 0; /* Simplified */
        break;
    case 0x3c8: /* DAC Write Address */
        val = 0; /* Simplified */
        break;
    case 0x3c9: /* DAC Data */
        val = 0; /* Simplified */
        break;
    case 0x3ca: /* Feature Control */
        val = 0; /* Simplified */
        break;
    case 0x3cc: /* Miscellaneous Output Read */
        val = 0; /* Simplified */
        break;
    case 0x3ce: /* Graphics Controller Index */
        val = s->grph_index;
        break;
    case 0x3cf: /* Graphics Controller Data */
        val = 0; /* Simplified */
        break;
    case 0x3d0: /* CRTC Index (mono) */
    case 0x3d2:
    case 0x3d4: /* CRTC Index (color) */
    case 0x3d6:
        val = s->crtc_index;
        break;
    case 0x3d1: /* CRTC Data (mono) */
    case 0x3d3:
    case 0x3d5: /* CRTC Data (color) */
    case 0x3d7:
        val = 0; /* Simplified */
        break;
    case 0x3da: /* Input Status 1 */
        s->attr_flip_flop = 0;
        val = 0; /* Simplified */
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "[smtcfb_pci] pio_read addr=%" PRIx64 " size=%u\n", (uint64_t)addr, size);
        return 0;
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case 0x3c0: /* Attribute Controller */
        if (s->attr_flip_flop) {
            /* Data write */
            /* Ignore for now */
        } else {
            /* Index write */
            s->attr_index = val & 0x1f;
        }
        s->attr_flip_flop = !s->attr_flip_flop;
        break;
    case 0x3c2: /* Miscellaneous Output */
        /* Ignore for now */
        break;
    case 0x3c4: /* Sequencer Index */
        s->seq_index = val;
        break;
    case 0x3c5: /* Sequencer Data */
        /* Handle special registers */
        if (s->seq_index == 0x76) {
            /* VRAM size detection for SM720 */
            /* We are modeling SM710, so ignore */
        }
        break;
    case 0x3c6: /* Pixel Mask */
        /* Ignore for now */
        break;
    case 0x3c7: /* DAC Read Address */
        /* Ignore for now */
        break;
    case 0x3c8: /* DAC Write Address */
        /* Ignore for now */
        break;
    case 0x3c9: /* DAC Data */
        /* Ignore for now */
        break;
    case 0x3ce: /* Graphics Controller Index */
        s->grph_index = val;
        break;
    case 0x3cf: /* Graphics Controller Data */
        /* Ignore for now */
        break;
    case 0x3d4: /* CRTC Index */
        s->crtc_index = val;
        break;
    case 0x3d5: /* CRTC Data */
        /* Ignore for now */
        break;
    case 0x3da: /* Input Status 1 */
        s->attr_flip_flop = 0;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "[smtcfb_pci] pio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n", (uint64_t)addr, (uint64_t)val, size);
        return;
    }
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

    s->seq_index = 0;
    s->crtc_index = 0;
    s->grph_index = 0;
    s->attr_index = 0;
    s->attr_flip_flop = 0;

    if (s->mmio_backing && s->mmio_backing_size)
        memset(s->mmio_backing, 0, s->mmio_backing_size);
}

/* ------------------------------------------------------------------ */
/* DMA initialize (called from realize)                                */
/* ------------------------------------------------------------------ */
static void pcibase_dma_device_realize(PCIDevice *pdev, Error **errp)
{
    /* No DMA in this device */
    (void)pdev;
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);
    

    /* Set up BARs */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_RAM;
    s->bar_info[0].size = 0x800000; /* 8MB for SM710 */
    s->bar_info[0].name = "smtcfb-ram";
    s->bar_info[0].sparse = false;

    /* VRAM size for SM710 */
    s->vram_size = 0x400000; /* 4MB */

    /* register BARs */
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Allocate MMIO backing for registers */
    s->mmio_backing_size = 0x400000; /* 4MB MMIO space */
    s->mmio_backing = g_malloc0(s->mmio_backing_size);

    /* Interrupt config (placeholders) */

    /* DMA init (called here so device can set DMA masks/etc) */
    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) return;


    /* optional: map legacy INTx behavior if driver expects it */


    /* Power management / other init */
    /* Example:
    *   pdev->pm_cap = true;
    */
    qemu_log_mask(LOG_UNIMP, "[smtcfb_pci] device realized\n");
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

    qemu_log_mask(LOG_UNIMP, "[smtcfb_pci] device uninit\n");
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
