/*
 * This template provides a robust skeleton for hardware emulation.
 * Integrated QEMU PCI device template (QEMU 8.2.10).
 * Replace #PLACEHOLDER# blocks with driver-specific definitions.
 * Designed for register-level modeling and PCIe driver probing.
 * NOTE: Do not directly fill this file with the Linux kernel source code.
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

#define TYPE_PCIBASE_DEVICE "sm750fb_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define DE_BASE_ADDR_TYPE1 0x100000
#define DE_PORT_ADDR_TYPE1 0x110000
#define DE_SOURCE 0x0
#define DE_DESTINATION 0x4
#define DE_DIMENSION 0x8
#define DE_CONTROL 0xC
#define DE_PITCH 0x000010
#define DE_FOREGROUND 0x000014
#define DE_BACKGROUND 0x000018
#define DE_STRETCH_FORMAT 0x00001C
#define DE_COLOR_COMPARE 0x000020
#define DE_COLOR_COMPARE_MASK 0x000024
#define DE_MASKS 0x000028
#define DE_CLIP_TL 0x00002C
#define DE_CLIP_BR 0x000030
#define DE_WINDOW_WIDTH 0x00003C
#define DE_WINDOW_SOURCE_BASE 0x000040
#define DE_WINDOW_DESTINATION_BASE 0x000044

#define SM750LE_REVISION_ID 0xfe

#define SYSTEM_CTRL                                   0x000000
#define MISC_CTRL                                     0x000004
#define PANEL_DISPLAY_CTRL                            0x080000
#define PANEL_FB_ADDRESS                              0x08000C
#define PANEL_FB_WIDTH                                0x080010
#define PANEL_WINDOW_WIDTH                            0x080014
#define PANEL_WINDOW_HEIGHT                           0x080018
#define PANEL_PLANE_TL                                0x08001C
#define PANEL_PLANE_BR                                0x080020
#define CRT_FB_ADDRESS                                0x080204
#define CRT_FB_WIDTH                                  0x080208
#define CRT_DISPLAY_CTRL                              0x080200
#define DISPLAY_CONTROL_750LE                         0x080288
#define PANEL_PALETTE_RAM                             0x080400
#define CRT_PALETTE_RAM                               0x080C00
#define HWC_ADDRESS                                   0x0
#define HWC_LOCATION                                  0x4
#define HWC_COLOR_12                                  0x8
#define HWC_COLOR_3                                   0xC
#define DE_STATE2                                     0x100058

#define SYSTEM_CTRL_DE_STATUS_BUSY                    (1 << 22)
#define SYSTEM_CTRL_DE_FIFO_EMPTY                     (1 << 23)
#define SYSTEM_CTRL_DE_MEM_FIFO_EMPTY                 (1 << 21)
#define DE_STATE2_DE_STATUS_BUSY                      (1 << 2)
#define DE_STATE2_DE_FIFO_EMPTY                       (1 << 3)
#define DE_STATE2_DE_MEM_FIFO_EMPTY                   (1 << 1)

typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM 
} BARType;

typedef struct {
    int     index;    /* BAR index 0-5 */
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

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t de_source;
    uint32_t de_destination;
    uint32_t de_dimension;
    uint32_t de_control;
    uint32_t de_pitch;
    uint32_t de_foreground;
    uint32_t de_background;
    uint32_t de_stretch_format;
    uint32_t de_color_compare;
    uint32_t de_color_compare_mask;
    uint32_t de_masks;
    uint32_t de_clip_tl;
    uint32_t de_clip_br;
    uint32_t de_window_width;
    uint32_t de_window_source_base;
    uint32_t de_window_destination_base;
    uint32_t de_state2;

    uint32_t system_ctrl;
    uint32_t misc_ctrl;
    uint32_t panel_display_ctrl;
    uint32_t panel_fb_address;
    uint32_t panel_fb_width;
    uint32_t panel_window_width;
    uint32_t panel_window_height;
    uint32_t panel_plane_tl;
    uint32_t panel_plane_br;
    uint32_t crt_fb_address;
    uint32_t crt_fb_width;
    uint32_t crt_display_ctrl;
    uint32_t display_control_750le;
};

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Adjust address if it falls within the DE_BASE_ADDR_TYPE1 region */
    if (addr >= DE_BASE_ADDR_TYPE1 && addr < DE_BASE_ADDR_TYPE1 + 0x100) {
        hwaddr de_addr = addr - DE_BASE_ADDR_TYPE1;
        switch (de_addr) {
            case DE_SOURCE: val = s->de_source; break;
            case DE_DESTINATION: val = s->de_destination; break;
            case DE_DIMENSION: val = s->de_dimension; break;
            case DE_CONTROL: val = s->de_control; break;
            case DE_PITCH: val = s->de_pitch; break;
            case DE_FOREGROUND: val = s->de_foreground; break;
            case DE_BACKGROUND: val = s->de_background; break;
            case DE_STRETCH_FORMAT: val = s->de_stretch_format; break;
            case DE_COLOR_COMPARE: val = s->de_color_compare; break;
            case DE_COLOR_COMPARE_MASK: val = s->de_color_compare_mask; break;
            case DE_MASKS: val = s->de_masks; break;
            case DE_CLIP_TL: val = s->de_clip_tl; break;
            case DE_CLIP_BR: val = s->de_clip_br; break;
            case DE_WINDOW_WIDTH: val = s->de_window_width; break;
            case DE_WINDOW_SOURCE_BASE: val = s->de_window_source_base; break;
            case DE_WINDOW_DESTINATION_BASE: val = s->de_window_destination_base; break;
            case (DE_STATE2 - DE_BASE_ADDR_TYPE1):
                val = s->de_state2 | DE_STATE2_DE_FIFO_EMPTY | DE_STATE2_DE_MEM_FIFO_EMPTY;
                val &= ~DE_STATE2_DE_STATUS_BUSY;
                break;
            default:
                qemu_log_mask(LOG_GUEST_ERROR, "sm750fb: read from undefined DE register 0x%" HWADDR_PRIx "\n", de_addr);
                break;
        }
    } else {
        switch (addr) {
            case SYSTEM_CTRL:
                val = s->system_ctrl | SYSTEM_CTRL_DE_FIFO_EMPTY | SYSTEM_CTRL_DE_MEM_FIFO_EMPTY;
                val &= ~SYSTEM_CTRL_DE_STATUS_BUSY;
                break;
            case MISC_CTRL: val = s->misc_ctrl; break;
            case PANEL_DISPLAY_CTRL: val = s->panel_display_ctrl; break;
            case PANEL_FB_ADDRESS: val = s->panel_fb_address; break;
            case PANEL_FB_WIDTH: val = s->panel_fb_width; break;
            case PANEL_WINDOW_WIDTH: val = s->panel_window_width; break;
            case PANEL_WINDOW_HEIGHT: val = s->panel_window_height; break;
            case PANEL_PLANE_TL: val = s->panel_plane_tl; break;
            case PANEL_PLANE_BR: val = s->panel_plane_br; break;
            case CRT_FB_ADDRESS: val = s->crt_fb_address; break;
            case CRT_FB_WIDTH: val = s->crt_fb_width; break;
            case CRT_DISPLAY_CTRL: val = s->crt_display_ctrl; break;
            case DISPLAY_CONTROL_750LE: val = s->display_control_750le; break;
            default:
                break;
        }
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= DE_BASE_ADDR_TYPE1 && addr < DE_BASE_ADDR_TYPE1 + 0x100) {
        hwaddr de_addr = addr - DE_BASE_ADDR_TYPE1;
        switch (de_addr) {
            case DE_SOURCE: s->de_source = val; break;
            case DE_DESTINATION: s->de_destination = val; break;
            case DE_DIMENSION: s->de_dimension = val; break;
            case DE_CONTROL: s->de_control = val; break;
            case DE_PITCH: s->de_pitch = val; break;
            case DE_FOREGROUND: s->de_foreground = val; break;
            case DE_BACKGROUND: s->de_background = val; break;
            case DE_STRETCH_FORMAT: s->de_stretch_format = val; break;
            case DE_COLOR_COMPARE: s->de_color_compare = val; break;
            case DE_COLOR_COMPARE_MASK: s->de_color_compare_mask = val; break;
            case DE_MASKS: s->de_masks = val; break;
            case DE_CLIP_TL: s->de_clip_tl = val; break;
            case DE_CLIP_BR: s->de_clip_br = val; break;
            case DE_WINDOW_WIDTH: s->de_window_width = val; break;
            case DE_WINDOW_SOURCE_BASE: s->de_window_source_base = val; break;
            case DE_WINDOW_DESTINATION_BASE: s->de_window_destination_base = val; break;
            case (DE_STATE2 - DE_BASE_ADDR_TYPE1): s->de_state2 = val; break;
            default:
                qemu_log_mask(LOG_GUEST_ERROR, "sm750fb: write to undefined DE register 0x%" HWADDR_PRIx "\n", de_addr);
                break;
        }
    } else if (addr >= DE_PORT_ADDR_TYPE1 && addr < DE_PORT_ADDR_TYPE1 + 0x10000) {
        /* Data port write, ignore for now */
    } else {
        switch (addr) {
            case SYSTEM_CTRL: s->system_ctrl = val; break;
            case MISC_CTRL: s->misc_ctrl = val; break;
            case PANEL_DISPLAY_CTRL: s->panel_display_ctrl = val; break;
            case PANEL_FB_ADDRESS: s->panel_fb_address = val; break;
            case PANEL_FB_WIDTH: s->panel_fb_width = val; break;
            case PANEL_WINDOW_WIDTH: s->panel_window_width = val; break;
            case PANEL_WINDOW_HEIGHT: s->panel_window_height = val; break;
            case PANEL_PLANE_TL: s->panel_plane_tl = val; break;
            case PANEL_PLANE_BR: s->panel_plane_br = val; break;
            case CRT_FB_ADDRESS: s->crt_fb_address = val; break;
            case CRT_FB_WIDTH: s->crt_fb_width = val; break;
            case CRT_DISPLAY_CTRL: s->crt_display_ctrl = val; break;
            case DISPLAY_CONTROL_750LE: s->display_control_750le = val; break;
            default:
                break;
        }
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    s->de_source = 0;
    s->de_destination = 0;
    s->de_dimension = 0;
    s->de_control = 0;
    s->de_pitch = 0;
    s->de_foreground = 0;
    s->de_background = 0;
    s->de_stretch_format = 0;
    s->de_color_compare = 0;
    s->de_color_compare_mask = 0;
    s->de_masks = 0;
    s->de_clip_tl = 0;
    s->de_clip_br = 0;
    s->de_window_width = 0;
    s->de_window_source_base = 0;
    s->de_window_destination_base = 0;
    s->de_state2 = 0;

    s->system_ctrl = 0;
    s->misc_ctrl = 0;
    s->panel_display_ctrl = 0;
    s->panel_fb_address = 0;
    s->panel_fb_width = 0;
    s->panel_window_width = 0;
    s->panel_window_height = 0;
    s->panel_plane_tl = 0;
    s->panel_plane_br = 0;
    s->crt_fb_address = 0;
    s->crt_fb_width = 0;
    s->crt_display_ctrl = 0;
    s->display_control_750le = 0;
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE || bi->size == 0) {
        return;
    }
    
    /* CRITICAL: PCI requires BAR sizes to be a power of 2. Prevent QEMU assert crash. */
    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x126f );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x0750 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_DISPLAY_VGA );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 2;

    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_RAM;
    s->bar_info[0].size = 0x1000000; /* 16MB default for framebuffer */
    s->bar_info[0].name = "sm750fb-vidmem";

    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = 0x200000; /* SZ_2M */
    s->bar_info[1].name = "sm750fb-vidreg";

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
    .name = "sm750fb_pci",
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
