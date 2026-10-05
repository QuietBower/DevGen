/*
 * QEMU Bochs VGA (bochs-drm) PCI Device Emulation
 *
 * Copyright (c) 2025 QEMU Team
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
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
#include "qemu/units.h"

#define TYPE_PCIBASE_DEVICE "bochs_drm_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Related register/macros/constants from driver */
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

#define VENDOR_ID 0x1234
#define DEVICE_ID 0x1111
#define CLASS_ID  PCI_CLASS_DISPLAY_VGA

/* VGA I/O ports */
#define VGA_MIS_W                        0x3CC
#define VGA_IS1_RC                       0x3DA
#define VGA_ATT_W                        0x3C0
#define VGA_MIS_COLOR                    0x01

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

    /* BAR memory regions */
    MemoryRegion bar_regions[6];

    /* Framebuffer RAM */
    MemoryRegion fb_ram;
    uint8_t *fb_ptr;
    size_t fb_size;

    /* BAR table */
    BARInfo bar_info[6];
    int num_bars;

    /* interrupt state feature flags */
    bool has_msi;
    bool has_msix;
    
    /* Register shadows */
    uint16_t dispi_regs[0xb];

    /* Status fields */
    bool ioports;

    /* EDID data (first 128 bytes) */
    uint8_t edid_data[128];
    bool edid_valid;
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
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, bi->size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
        s->fb_ptr = memory_region_get_ram_ptr(mr);
        s->fb_size = bi->size;
    }
}

/* ------------------------------------------------------------------ */
/* MMIO / PIO handlers                                                  */
/* ------------------------------------------------------------------ */

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= 0x400 && addr < 0x500) {
        /* VGA registers (0x3c0-0x3df mapped to 0x400-0x4ff) */
        if (addr == 0x4cc) { /* VGA_MIS_R */
            val = VGA_MIS_COLOR;
        } else if (addr == 0x4da) { /* VGA_IS1_RC */
            val = 0; /* dummy value */
        } else {
            val = 0;
        }
    } else if (addr >= 0x500 && addr < 0x600) {
        /* Bochs dispi registers (0x500 + reg*2) */
        uint16_t reg = (addr - 0x500) >> 1;
        if (reg < 0xb) {
            val = s->dispi_regs[reg];
        }
    } else if (addr >= 0x600 && addr < 0x608) {
        /* QEMU extensions */
        if (addr == 0x600) {
            val = 8; /* qext_size */
        } else if (addr == 0x604) {
            val = 0x1e1e1e1e; /* little endian marker */
        }
    } else if (addr < 0x400) {
        /* EDID data */
        if (addr < sizeof(s->edid_data)) {
            val = s->edid_data[addr];
        }
    }

    switch (size) {
    case 1:
        return val & 0xff;
    case 2:
        return val & 0xffff;
    case 4:
        return val;
    default:
        return val;
    }
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= 0x400 && addr < 0x500) {
        /* VGA registers */
        if (addr == 0x4c0) { /* VGA_ATT_W */
            /* blanking handled via dispi */
        }
    } else if (addr >= 0x500 && addr < 0x600) {
        /* Bochs dispi registers */
        uint16_t reg = (addr - 0x500) >> 1;
        if (reg < 0xb && size == 2) {
            s->dispi_regs[reg] = val;
            if (reg == VBE_DISPI_INDEX_ENABLE) {
                if (val & VBE_DISPI_ENABLED) {
                    /* Enable display */
                }
            }
        }
    } else if (addr == 0x604 && size == 4) {
        /* Endianness setting */
        if (val == 0xbebebebe) {
            /* big endian */
        } else if (val == 0x1e1e1e1e) {
            /* little endian */
        }
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr == VBE_DISPI_IOPORT_INDEX) {
        val = s->dispi_regs[VBE_DISPI_INDEX_ID]; /* dummy index */
    } else if (addr == VBE_DISPI_IOPORT_DATA) {
        /* Return last written index value */
        val = s->dispi_regs[s->dispi_regs[VBE_DISPI_INDEX_ID]];
    } else if (addr >= 0x3c0 && addr <= 0x3df) {
        if (addr == VGA_MIS_W) {
            val = VGA_MIS_COLOR;
        } else if (addr == VGA_IS1_RC) {
            val = 0;
        } else {
            val = 0;
        }
    }

    switch (size) {
    case 1:
        return val & 0xff;
    case 2:
        return val & 0xffff;
    default:
        return val;
    }
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr == VBE_DISPI_IOPORT_INDEX && size == 2) {
        s->dispi_regs[VBE_DISPI_INDEX_ID] = val;
    } else if (addr == VBE_DISPI_IOPORT_DATA && size == 2) {
        uint16_t reg = s->dispi_regs[VBE_DISPI_INDEX_ID];
        if (reg < 0xb) {
            s->dispi_regs[reg] = val;
            if (reg == VBE_DISPI_INDEX_ENABLE) {
                if (val & VBE_DISPI_ENABLED) {
                    /* Enable display */
                }
            }
        }
    } else if (addr == VGA_ATT_W && size == 1) {
        /* blanking handled via dispi */
    }
}

/* ------------------------------------------------------------------ */
/* Reset                                                               */
/* ------------------------------------------------------------------ */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    pci_device_reset(pdev);

    /* Initialize dispi registers */
    memset(s->dispi_regs, 0, sizeof(s->dispi_regs));
    s->dispi_regs[VBE_DISPI_INDEX_ID] = VBE_DISPI_ID0;
    s->dispi_regs[VBE_DISPI_INDEX_VIDEO_MEMORY_64K] = 16 * 1024; /* 16MB */

    /* Clear framebuffer */
    if (s->fb_ptr) {
        memset(s->fb_ptr, 0, s->fb_size);
    }
}

/* ------------------------------------------------------------------ */
/* DMA initialize (not used by this device)                            */
/* ------------------------------------------------------------------ */
static void pcibase_dma_device_realize(PCIDevice *pdev, Error **errp)
{
    /* No DMA needed for this device */
}

/* ------------------------------------------------------------------ */
/* PCI config space access                                             */
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
/* Realize (device init)                                               */
/* ------------------------------------------------------------------ */
static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x02); /* Revision 2 for QEMU extensions */
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* BAR0: Framebuffer (16MB) */
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_RAM;
    s->bar_info[0].size = 16 * MiB;
    s->bar_info[0].name = "bochs-fb";

    /* BAR2: MMIO registers (if used) */
    s->bar_info[2].index = 2;
    s->bar_info[2].type = BAR_TYPE_MMIO;
    s->bar_info[2].size = 0x1000;
    s->bar_info[2].name = "bochs-mmio";

    s->num_bars = 3; /* BAR0, BAR1 unused, BAR2 */

    /* Initialize EDID data with valid header */
    s->edid_data[0] = 0x00;
    s->edid_data[1] = 0xff;
    s->edid_data[2] = 0xff;
    s->edid_data[3] = 0xff;
    s->edid_data[4] = 0xff;
    s->edid_data[5] = 0xff;
    s->edid_data[6] = 0xff;
    s->edid_data[7] = 0x00;
    s->edid_valid = true;

    /* register BARs */
    for (int i = 0; i < s->num_bars; i++) {
        if (s->bar_info[i].type != BAR_TYPE_NONE) {
            pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
            if (errp && *errp) {
                return;
            }
        }
    }

    /* DMA init */
    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) return;

    /* Reset device */
    pcibase_reset(DEVICE(pdev));
}

/* ------------------------------------------------------------------ */
/* Uninit/cleanup                                                      */
/* ------------------------------------------------------------------ */
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

    type_register_static(&pcibase_info);
}

type_init(pcibase_register_types);
