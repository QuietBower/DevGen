/*
 * Generic QEMU PCI device template (QEMU 8.2.x)
 *
 * Phase 1: fill register macros, BAR list, BAR sizes, structs, enums.
 * Phase 2: implement MMIO/PIO read/write, DMA init, IRQ init, reset, config hooks
 * Phase 3 (Repair Phase)：correct syntax, types, includes, missing symbols, remove dead code
 * Phase 4 (Debug & Update Phase)：Based on actual kernel logs from QEMU boot, repair the virtual hardware behavior
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
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

#define BOCHS_QEXT_OFFSET                0x600

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
} BARInfo;

/* ------------------------------------------------------------------ */
/* Device State                                                        */
/* ------------------------------------------------------------------ */
struct PCIBaseState {
    PCIDevice parent_obj;

    /* BAR memory regions */
    MemoryRegion bar_regions[6];

    /* BAR table */
    BARInfo bar_info[6];
    int num_bars;

    /* Register shadows */
    uint16_t vbe_regs[0x10];
    
    /* QEXT registers: 0x600 (size), 0x604 (endian) */
    uint32_t qext_regs[2];

    /* EDID blob (0x000 - 0x3FF) */
    uint8_t edid_blob[0x400];

    /* VGA IO ports shadow (0x400 - 0x41F maps to 0x3c0 - 0x3df) */
    uint8_t vga_ports[0x20];
};

/* ------------------------------------------------------------------ */
/* Forward declarations                                                */
/* ------------------------------------------------------------------ */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
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
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, bi->size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

/* ------------------------------------------------------------------ */
/* MMIO / PIO handlers                                                */
/* ------------------------------------------------------------------ */

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* EDID Region: 0x000 - 0x3FF */
    if (addr < 0x400) {
        if (size == 1) {
            val = s->edid_blob[addr];
        } else {
            /* Driver reads EDID byte-by-byte, but handle larger just in case */
            memcpy(&val, &s->edid_blob[addr], size);
        }
        return val;
    }

    /* VGA Ports: 0x400 - 0x41F (Maps to 0x3c0 - 0x3df) */
    if (addr >= 0x400 && addr < 0x420) {
        int offset = addr - 0x400;
        val = s->vga_ports[offset];
        /* 
         * Driver reads 0x3da (VGA_IS1_RC) -> offset 0x1a 
         * We just return the stored value (0 by default) which is fine.
         */
        return val;
    }

    /* VBE Registers: 0x500 - 0x51F */
    if (addr >= 0x500 && addr < 0x520) {
        int idx = (addr - 0x500) >> 1;
        if (idx < 0x10) {
            val = s->vbe_regs[idx];
        }
        return val;
    }

    /* QEXT Registers: 0x600, 0x604 */
    if (addr == 0x600) {
        return s->qext_regs[0]; /* Size */
    }
    if (addr == 0x604) {
        return s->qext_regs[1]; /* Endianness */
    }

    qemu_log_mask(LOG_UNIMP, "[%s] mmio_read unhandled addr=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* EDID is read-only */
    if (addr < 0x400) {
        return;
    }

    /* VGA Ports: 0x400 - 0x41F */
    if (addr >= 0x400 && addr < 0x420) {
        int offset = addr - 0x400;
        s->vga_ports[offset] = (uint8_t)val;
        return;
    }

    /* VBE Registers: 0x500 - 0x51F */
    if (addr >= 0x500 && addr < 0x520) {
        int idx = (addr - 0x500) >> 1;
        if (idx < 0x10) {
            /* ID and Video Memory size are read-only */
            if (idx == VBE_DISPI_INDEX_ID || idx == VBE_DISPI_INDEX_VIDEO_MEMORY_64K) {
                return;
            }
            s->vbe_regs[idx] = (uint16_t)val;
            
            /* 
             * If enabling, we might want to update internal state, 
             * but for this model we just store the values.
             */
        }
        return;
    }

    /* QEXT Registers */
    if (addr == 0x600) {
        /* Size is read-only */
        return;
    }
    if (addr == 0x604) {
        s->qext_regs[1] = (uint32_t)val;
        return;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] mmio_write unhandled addr=%" PRIx64 " val=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, (uint64_t)val, size);
}

/* ------------------------------------------------------------------ */
/* Reset       */
/* ------------------------------------------------------------------ */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    pci_device_reset(pdev);
    
    memset(s->vbe_regs, 0, sizeof(s->vbe_regs));
    memset(s->vga_ports, 0, sizeof(s->vga_ports));
    memset(s->qext_regs, 0, sizeof(s->qext_regs));

    /* Initialize VBE ID */
    s->vbe_regs[VBE_DISPI_INDEX_ID] = VBE_DISPI_ID0;
    
    /* Initialize VRAM size (16MB / 64KB = 256) */
    s->vbe_regs[VBE_DISPI_INDEX_VIDEO_MEMORY_64K] = (16 * MiB) / (64 * KiB);

    /* Initialize QEXT size (8 bytes: 0x600 and 0x604) */
    s->qext_regs[0] = 8;

    /* Initialize EDID Header (00 FF FF FF FF FF FF 00) to satisfy drm_edid_header_is_valid */
    memset(s->edid_blob, 0, sizeof(s->edid_blob));
    s->edid_blob[0] = 0x00;
    s->edid_blob[1] = 0xff;
    s->edid_blob[2] = 0xff;
    s->edid_blob[3] = 0xff;
    s->edid_blob[4] = 0xff;
    s->edid_blob[5] = 0xff;
    s->edid_blob[6] = 0xff;
    s->edid_blob[7] = 0x00;
}

/* ------------------------------------------------------------------ */
/* PCI config space access (class-level hooks)                         */
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
/* Realize (device init)                                                */
/* ------------------------------------------------------------------ */
static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Standard Bochs VGA ID */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x1234);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x1111);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_DISPLAY_VGA);
    
    /* Driver checks revision >= 2 for MMIO qext support */
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x02);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* BAR 0: Framebuffer (VRAM) - 16 MiB default */
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_RAM;
    s->bar_info[0].size = 16 * MiB;
    s->bar_info[0].name = "bochs-vram";

    /* BAR 2: MMIO Registers - 4KB */
    s->bar_info[1].index = 2;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = 4096;
    s->bar_info[1].name = "bochs-mmio";

    s->num_bars = 2;

    /* register BARs */
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
        if (errp && *errp) return;
    }
    
    /* Perform initial reset to set default values */
    pcibase_reset(DEVICE(pdev));

    qemu_log_mask(LOG_UNIMP, "[%s] device realized\n", TYPE_PCIBASE_DEVICE);
}

/* ------------------------------------------------------------------ */
/* Uninit/cleanup                                                      */
/* ------------------------------------------------------------------ */
static void pcibase_uninit(PCIDevice *pdev)
{
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