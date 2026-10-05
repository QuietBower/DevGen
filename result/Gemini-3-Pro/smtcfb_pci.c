/*
 * QEMU SM712 Framebuffer Device Model
 *
 * Based on Linux driver: drivers/video/fbdev/sm712fb.c
 * Target: QEMU 8.2.10
 */

#include "qemu/osdep.h"
#include <inttypes.h>
#include "qemu/module.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_device.h"
#include "hw/qdev-properties.h"
#include "qom/object.h"

#define TYPE_PCIBASE_DEVICE "smtcfb_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI IDs */
#define VENDOR_ID 0x126f
#define DEVICE_ID 0x710
#define CLASS_ID  PCI_CLASS_DISPLAY_OTHER

/* BAR 0 Layout */
#define SM712_BAR0_SIZE     0x1000000 /* 16MB */
#define SM712_VRAM_SIZE     0x400000  /* 4MB */

/* Register Offsets within BAR 0 (Derived from driver snippet) */
#define SM712_MMIO_OFFSET   0x700000
#define SM712_DP_OFFSET     0x408000
#define SM712_VP_OFFSET     0x40c000

/* VGA Offsets relative to MMIO_OFFSET */
#define VGA_SEQ_INDEX       0x3c4
#define VGA_SEQ_DATA        0x3c5
#define VGA_CRTC_INDEX      0x3d4
#define VGA_CRTC_DATA       0x3d5
#define VGA_GRPH_INDEX      0x3ce
#define VGA_GRPH_DATA       0x3cf
#define VGA_ATTR_INDEX      0x3c0
#define VGA_ATTR_DATA       0x3c1
#define VGA_MISC_WRITE      0x3c2
#define VGA_MISC_READ       0x3cc
#define VGA_DAC_MASK        0x3c6
#define VGA_DAC_READ_ADDR   0x3c7
#define VGA_DAC_WRITE_ADDR  0x3c8
#define VGA_DAC_DATA        0x3c9

/* ------------------------------------------------------------------ */
/* Device State                                                        */
/* ------------------------------------------------------------------ */
struct PCIBaseState {
    PCIDevice parent_obj;

    /* Memory Regions */
    MemoryRegion bar0_container;
    MemoryRegion vram;
    MemoryRegion mmio;

    /* VGA Registers */
    uint8_t seq_index;
    uint8_t seq[0x100];
    uint8_t crtc_index;
    uint8_t crtc[0x100];
    uint8_t grph_index;
    uint8_t grph[0x100];
    uint8_t attr_index;
    uint8_t attr[0x100];
    bool attr_ff; /* Attribute flip-flop */
    uint8_t misc;
    
    /* DAC */
    uint8_t dac_w_index;
    uint8_t dac_r_index;
    uint8_t dac_mask;
    uint8_t dac_data[3]; /* R, G, B */
    uint8_t dac_cnt;

    /* SM712 Specific Registers */
    uint32_t vp_regs[0x100];
    uint32_t dp_regs[0x100];
};

/* ------------------------------------------------------------------ */
/* MMIO Handlers                                                       */
/* ------------------------------------------------------------------ */

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Handle VGA Registers at 0x700000 */
    if (addr >= SM712_MMIO_OFFSET && addr < SM712_MMIO_OFFSET + 0x1000) {
        hwaddr vga_addr = addr - SM712_MMIO_OFFSET;
        switch (vga_addr) {
        case VGA_SEQ_INDEX:
            val = s->seq_index;
            break;
        case VGA_SEQ_DATA:
            val = s->seq[s->seq_index];
            break;
        case VGA_CRTC_INDEX:
            val = s->crtc_index;
            break;
        case VGA_CRTC_DATA:
            val = s->crtc[s->crtc_index];
            break;
        case VGA_GRPH_INDEX:
            val = s->grph_index;
            break;
        case VGA_GRPH_DATA:
            val = s->grph[s->grph_index];
            break;
        case VGA_ATTR_DATA:
            /* Attribute controller read is complex, simplified here */
            val = s->attr[s->attr_index];
            break;
        case VGA_MISC_READ:
            val = s->misc;
            break;
        case VGA_DAC_READ_ADDR:
            val = s->dac_r_index;
            break;
        case VGA_DAC_WRITE_ADDR:
            val = s->dac_w_index;
            break;
        case VGA_DAC_DATA:
            /* Simplified DAC read */
            val = s->dac_data[s->dac_cnt % 3];
            s->dac_cnt++;
            break;
        case VGA_DAC_MASK:
            val = s->dac_mask;
            break;
        default:
            break;
        }
        return val;
    }

    /* Handle VP Registers */
    if (addr >= SM712_VP_OFFSET && addr < SM712_VP_OFFSET + 0x400) {
        hwaddr reg = (addr - SM712_VP_OFFSET) >> 2;
        if (reg < 0x100) {
            val = s->vp_regs[reg];
        }
        return val;
    }

    /* Handle DP Registers */
    if (addr >= SM712_DP_OFFSET && addr < SM712_DP_OFFSET + 0x400) {
        hwaddr reg = (addr - SM712_DP_OFFSET) >> 2;
        if (reg < 0x100) {
            val = s->dp_regs[reg];
        }
        return val;
    }

    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* Handle VGA Registers at 0x700000 */
    if (addr >= SM712_MMIO_OFFSET && addr < SM712_MMIO_OFFSET + 0x1000) {
        hwaddr vga_addr = addr - SM712_MMIO_OFFSET;
        switch (vga_addr) {
        case VGA_SEQ_INDEX:
            s->seq_index = val & 0xFF;
            break;
        case VGA_SEQ_DATA:
            s->seq[s->seq_index] = val & 0xFF;
            break;
        case VGA_CRTC_INDEX:
            s->crtc_index = val & 0xFF;
            break;
        case VGA_CRTC_DATA:
            s->crtc[s->crtc_index] = val & 0xFF;
            break;
        case VGA_GRPH_INDEX:
            s->grph_index = val & 0xFF;
            break;
        case VGA_GRPH_DATA:
            s->grph[s->grph_index] = val & 0xFF;
            break;
        case VGA_ATTR_INDEX:
            if (!s->attr_ff) {
                s->attr_index = val & 0x1F;
            }
            /* Note: Attribute controller flip-flop logic is simplified */
            s->attr_ff = !s->attr_ff;
            break;
        case VGA_ATTR_DATA:
             s->attr[s->attr_index] = val & 0xFF;
             break;
        case VGA_MISC_WRITE:
            s->misc = val & 0xFF;
            break;
        case VGA_DAC_MASK:
            s->dac_mask = val & 0xFF;
            break;
        case VGA_DAC_READ_ADDR:
            s->dac_r_index = val & 0xFF;
            s->dac_cnt = 0;
            break;
        case VGA_DAC_WRITE_ADDR:
            s->dac_w_index = val & 0xFF;
            s->dac_cnt = 0;
            break;
        case VGA_DAC_DATA:
            s->dac_data[s->dac_cnt % 3] = val & 0xFF;
            s->dac_cnt++;
            if (s->dac_cnt == 3) {
                s->dac_cnt = 0;
                s->dac_w_index++;
            }
            break;
        default:
            break;
        }
        return;
    }

    /* Handle VP Registers */
    if (addr >= SM712_VP_OFFSET && addr < SM712_VP_OFFSET + 0x400) {
        hwaddr reg = (addr - SM712_VP_OFFSET) >> 2;
        if (reg < 0x100) {
            s->vp_regs[reg] = val;
        }
        return;
    }

    /* Handle DP Registers */
    if (addr >= SM712_DP_OFFSET && addr < SM712_DP_OFFSET + 0x400) {
        hwaddr reg = (addr - SM712_DP_OFFSET) >> 2;
        if (reg < 0x100) {
            s->dp_regs[reg] = val;
        }
        return;
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

/* ------------------------------------------------------------------ */
/* Reset                                                               */
/* ------------------------------------------------------------------ */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);

    memset(s->seq, 0, sizeof(s->seq));
    memset(s->crtc, 0, sizeof(s->crtc));
    memset(s->grph, 0, sizeof(s->grph));
    memset(s->attr, 0, sizeof(s->attr));
    memset(s->vp_regs, 0, sizeof(s->vp_regs));
    memset(s->dp_regs, 0, sizeof(s->dp_regs));
    
    s->seq_index = 0;
    s->crtc_index = 0;
    s->grph_index = 0;
    s->attr_index = 0;
    s->attr_ff = false;
    s->misc = 0;
    s->dac_w_index = 0;
    s->dac_r_index = 0;
    s->dac_mask = 0xFF;
    s->dac_cnt = 0;
}

/* ------------------------------------------------------------------ */
/* Realize                                                             */
/* ------------------------------------------------------------------ */
static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* 
     * BAR0 Layout:
     * 0x000000 - 0x3FFFFF: VRAM (4MB)
     * 0x408000: DP Registers
     * 0x40C000: VP Registers
     * 0x700000: MMIO (VGA Registers)
     * Total Size: 16MB
     */

    /* Initialize Container */
    memory_region_init(&s->bar0_container, OBJECT(s), "sm712.bar0", SM712_BAR0_SIZE);

    /* Initialize VRAM (RAM) */
    memory_region_init_ram(&s->vram, OBJECT(s), "sm712.vram", SM712_VRAM_SIZE, &error_fatal);
    memory_region_add_subregion(&s->bar0_container, 0, &s->vram);

    /* Initialize MMIO (IO) - Background region covering registers */
    /* We map this at priority -1 so VRAM at 0 takes precedence */
    memory_region_init_io(&s->mmio, OBJECT(s), &pcibase_mmio_ops, s, "sm712.mmio", SM712_BAR0_SIZE);
    memory_region_add_subregion_overlap(&s->bar0_container, 0, &s->mmio, -1);

    /* Register BAR0 */
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar0_container);
}

static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pcibase_realize;
    /* k->exit is removed in QEMU 8.2 */
    k->vendor_id = VENDOR_ID;
    k->device_id = DEVICE_ID;
    k->class_id = CLASS_ID;
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
