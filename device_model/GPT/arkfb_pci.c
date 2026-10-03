/*
 * QEMU PCI device model for arkfb (minimal behavioral emulation)
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

/* Additional include files retrieved from driver context */
#include "hw/display/vga_regs.h"
#include "hw/display/vga_int.h"

/* Minimal local definitions for legacy VGA I/O port indices used */
#ifndef VGA_CRTC_INDEX
#define VGA_CRTC_INDEX 0x3D4
#endif
#ifndef VGA_CRTC_DATA
#define VGA_CRTC_DATA  0x3D5
#endif
#ifndef VGA_SEQ_INDEX
#define VGA_SEQ_INDEX  0x3C4
#endif
#ifndef VGA_SEQ_DATA
#define VGA_SEQ_DATA   0x3C5
#endif
#ifndef VGA_GFX_INDEX
#define VGA_GFX_INDEX  0x3CE
#endif
#ifndef VGA_GFX_DATA
#define VGA_GFX_DATA   0x3CF
#endif
#ifndef VGA_MIS_R
#define VGA_MIS_R      0x3CC
#endif
#ifndef VGA_MIS_W
#define VGA_MIS_W      0x3C2
#endif
#ifndef VGA_IS1_RC
#define VGA_IS1_RC     0x3DA
#endif
#ifndef VGA_ATT_W
#define VGA_ATT_W      0x3C0
#endif
#ifndef VGA_PEL_MSK
#define VGA_PEL_MSK    0x3C6
#endif
#ifndef VGA_PEL_IW
#define VGA_PEL_IW     0x3C8
#endif
#ifndef VGA_PEL_D
#define VGA_PEL_D      0x3C9
#endif

#define TYPE_PCIBASE_DEVICE "arkfb_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Local recreation of small pieces of arkfb timing helper types */
typedef struct vga_regset {
    uint8_t regnum;
    uint8_t lowbit;
    uint8_t highbit;
} vga_regset;

#define VGA_REGSET_END_VAL 0xff
#define VGA_REGSET_END     { VGA_REGSET_END_VAL, 0, 0 }

typedef struct svga_timing_regs {
    const vga_regset *h_total_regs;
    const vga_regset *h_display_regs;
    const vga_regset *h_blank_start_regs;
    const vga_regset *h_blank_end_regs;
    const vga_regset *h_sync_start_regs;
    const vga_regset *h_sync_end_regs;

    const vga_regset *v_total_regs;
    const vga_regset *v_display_regs;
    const vga_regset *v_blank_start_regs;
    const vga_regset *v_blank_end_regs;
    const vga_regset *v_sync_start_regs;
    const vga_regset *v_sync_end_regs;
} svga_timing_regs;

/* Register Layout and Hardware Identifiers extracted from driver source */
#define ARK_VENDOR_ID 0xEDD8
#define ARK_DEVICE_ID 0xA099
#define ARK_CLASS_ID  PCI_CLASS_DISPLAY_VGA

static const vga_regset ark_h_total_regs[]        = {{0x00, 0, 7}, {0x41, 7, 7}, VGA_REGSET_END};
static const vga_regset ark_h_display_regs[]      = {{0x01, 0, 7}, {0x41, 6, 6}, VGA_REGSET_END};
static const vga_regset ark_h_blank_start_regs[]  = {{0x02, 0, 7}, {0x41, 5, 5}, VGA_REGSET_END};
static const vga_regset ark_h_blank_end_regs[]    = {{0x03, 0, 4}, {0x05, 7, 7}, VGA_REGSET_END};
static const vga_regset ark_h_sync_start_regs[]   = {{0x04, 0, 7}, {0x41, 4, 4}, VGA_REGSET_END};
static const vga_regset ark_h_sync_end_regs[]     = {{0x05, 0, 4}, VGA_REGSET_END};
static const vga_regset ark_v_total_regs[]        = {{0x06, 0, 7}, {0x07, 0, 0}, {0x07, 5, 5}, {0x40, 7, 7}, VGA_REGSET_END};
static const vga_regset ark_v_display_regs[]      = {{0x12, 0, 7}, {0x07, 1, 1}, {0x07, 6, 6}, {0x40, 6, 6}, VGA_REGSET_END};
static const vga_regset ark_v_blank_start_regs[]  = {{0x15, 0, 7}, {0x07, 3, 3}, {0x09, 5, 5}, {0x40, 5, 5}, VGA_REGSET_END};
static const vga_regset ark_v_blank_end_regs[]    = {{0x16, 0, 7}, VGA_REGSET_END};
static const vga_regset ark_v_sync_start_regs[]   = {{0x10, 0, 7}, {0x07, 2, 2}, {0x07, 7, 7}, {0x40, 4, 4}, VGA_REGSET_END};
static const vga_regset ark_v_sync_end_regs[]     = {{0x11, 0, 3}, VGA_REGSET_END};
/* Unused helper tables from driver timing code */
static const vga_regset ark_line_compare_regs[]   G_GNUC_UNUSED = {{0x18, 0, 7}, {0x07, 4, 4}, {0x09, 6, 6}, VGA_REGSET_END};
static const vga_regset ark_start_address_regs[]  G_GNUC_UNUSED = {{0x0d, 0, 7}, {0x0c, 0, 7}, {0x40, 0, 2}, VGA_REGSET_END};
static const vga_regset ark_offset_regs[]         G_GNUC_UNUSED = {{0x13, 0, 7}, {0x41, 3, 3}, VGA_REGSET_END};

static const svga_timing_regs ark_timing_regs     = {
    .h_total_regs        = ark_h_total_regs,
    .h_display_regs      = ark_h_display_regs,
    .h_blank_start_regs  = ark_h_blank_start_regs,
    .h_blank_end_regs    = ark_h_blank_end_regs,
    .h_sync_start_regs   = ark_h_sync_start_regs,
    .h_sync_end_regs     = ark_h_sync_end_regs,
    .v_total_regs        = ark_v_total_regs,
    .v_display_regs      = ark_v_display_regs,
    .v_blank_start_regs  = ark_v_blank_start_regs,
    .v_blank_end_regs    = ark_v_blank_end_regs,
    .v_sync_start_regs   = ark_v_sync_start_regs,
    .v_sync_end_regs     = ark_v_sync_end_regs,
};

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
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    /* For arkfb we keep a minimal shadow of VGA CRTC/SEQ/GRAPH/ATTR index/data and misc regs */
    struct {
        uint8_t crtc[0x60];
        uint8_t seq[0x30];
        uint8_t graph[0x09];
        uint8_t attr[0x15];
        uint8_t misc_out;
        /* index latches */
        uint8_t crtc_index;
        uint8_t seq_index;
        uint8_t graph_index;
        uint8_t attr_index;
        bool attr_flipflop;
    } regs;

    /* No device-initiated DMA is used by arkfb */

    /* Framebuffer RAM for BAR0 */
    MemoryRegion fb_ram;
    uint64_t fb_ram_size;
};


/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* arkfb driver does not use interrupts on this device, keep line deasserted */
    pci_set_irq(pdev, 0);
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /* arkfb driver does not program any DMA engines; nothing to do */
    (void)s;
    (void)is_write;
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    /* Linear framebuffer BAR: direct access to fb_ram */
    if (addr + size > s->fb_ram_size) {
        return 0;
    }

    switch (size) {
    case 1: {
        uint8_t v = address_space_ldub(&address_space_memory, memory_region_get_ram_addr(&s->fb_ram) + addr, MEMTXATTRS_UNSPECIFIED, NULL);
        return v;
    }
    case 2: {
        uint16_t v = address_space_lduw_le(&address_space_memory, memory_region_get_ram_addr(&s->fb_ram) + addr, MEMTXATTRS_UNSPECIFIED, NULL);
        return v;
    }
    case 4: {
        uint32_t v = address_space_ldl_le(&address_space_memory, memory_region_get_ram_addr(&s->fb_ram) + addr, MEMTXATTRS_UNSPECIFIED, NULL);
        return v;
    }
    default: {
        uint64_t val = 0;
        for (unsigned i = 0; i < size && (addr + i) < s->fb_ram_size; i++) {
            uint8_t b = address_space_ldub(&address_space_memory, memory_region_get_ram_addr(&s->fb_ram) + addr + i, MEMTXATTRS_UNSPECIFIED, NULL);
            val |= ((uint64_t)b) << (i * 8);
        }
        return val;
    }
    }
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr + size > s->fb_ram_size) {
        return;
    }

    switch (size) {
    case 1: {
        uint8_t v = val & 0xff;
        address_space_stb(&address_space_memory, memory_region_get_ram_addr(&s->fb_ram) + addr, v, MEMTXATTRS_UNSPECIFIED, NULL);
        break;
    }
    case 2: {
        uint16_t v = val & 0xffff;
        address_space_stw_le(&address_space_memory, memory_region_get_ram_addr(&s->fb_ram) + addr, v, MEMTXATTRS_UNSPECIFIED, NULL);
        break;
    }
    case 4: {
        uint32_t v = val & 0xffffffffU;
        address_space_stl_le(&address_space_memory, memory_region_get_ram_addr(&s->fb_ram) + addr, v, MEMTXATTRS_UNSPECIFIED, NULL);
        break;
    }
    default:
        for (unsigned i = 0; i < size && (addr + i) < s->fb_ram_size; i++) {
            uint8_t b = (val >> (i * 8)) & 0xff;
            address_space_stb(&address_space_memory, memory_region_get_ram_addr(&s->fb_ram) + addr + i, b, MEMTXATTRS_UNSPECIFIED, NULL);
        }
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0xff;

    /* Emulate standard VGA I/O space the arkfb helpers use. We implement
     * only the subset actually touched by the driver using simple shadows.
     */

    if (size != 1) {
        return 0xff;
    }

    switch (addr) {
    case VGA_CRTC_INDEX:
        val = s->regs.crtc_index;
        break;
    case VGA_CRTC_DATA:
        val = s->regs.crtc[s->regs.crtc_index];
        break;
    case VGA_SEQ_INDEX:
        val = s->regs.seq_index;
        break;
    case VGA_SEQ_DATA:
        val = s->regs.seq[s->regs.seq_index];
        break;
    case VGA_GFX_INDEX:
        val = s->regs.graph_index;
        break;
    case VGA_GFX_DATA:
        if (s->regs.graph_index < sizeof(s->regs.graph)) {
            val = s->regs.graph[s->regs.graph_index];
        } else {
            val = 0;
        }
        break;
    case VGA_MIS_R:
        val = s->regs.misc_out;
        break;
    case VGA_IS1_RC: /* input status 1 (colour) */
        /* Return 0; driver only uses this to flip ATC address/data latch */
        val = 0;
        break;
    case VGA_ATT_W:
        /* ATC address/data port read isn't used by arkfb; return 0 */
        val = 0;
        break;
    default:
        /* Other ports not used by arkfb; return 0 */
        val = 0;
        break;
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 1) {
        return;
    }

    uint8_t v = (uint8_t)val;

    switch (addr) {
    case VGA_CRTC_INDEX:
        s->regs.crtc_index = v;
        break;
    case VGA_CRTC_DATA:
        s->regs.crtc[s->regs.crtc_index] = v;
        break;
    case VGA_SEQ_INDEX:
        s->regs.seq_index = v;
        break;
    case VGA_SEQ_DATA:
        s->regs.seq[s->regs.seq_index] = v;
        break;
    case VGA_GFX_INDEX:
        s->regs.graph_index = v;
        break;
    case VGA_GFX_DATA:
        if (s->regs.graph_index < sizeof(s->regs.graph)) {
            s->regs.graph[s->regs.graph_index] = v;
        }
        break;
    case VGA_MIS_W:
        s->regs.misc_out = v;
        break;
    case VGA_ATT_W:
        /* ATC write port: first write selects index, second writes data. */
        if (!s->regs.attr_flipflop) {
            s->regs.attr_index = v & 0x1f;
            s->regs.attr_flipflop = true;
        } else {
            if (s->regs.attr_index < sizeof(s->regs.attr)) {
                s->regs.attr[s->regs.attr_index] = v;
            }
            s->regs.attr_flipflop = false;
        }
        break;
    case VGA_PEL_MSK:
        /* arkfb only uses VGA DAC palette ports to program colours.
         * We do not emulate an RGB DAC here, writes are ignored.
         */
        break;
    case VGA_PEL_IW:
    case VGA_PEL_D:
        /* Ignore DAC palette writes; they don't affect probe success. */
        break;
    default:
        /* Ports not needed by arkfb left unimplemented */
        break;
    }
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

    /* Revert registers to power-on defaults approximating VGA reset. */
    memset(&s->regs, 0, sizeof(s->regs));
    s->regs.misc_out = 0x67; /* standard VGA colour text mode default */
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }
    
    /* CRITICAL: PCI requires BAR sizes to be a power of 2. Prevent QEMU assert crash. */
    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        /* For arkfb BAR0 we back with fb_ram and expose via mmio ops */
        if (bi->index == 0) {
            s->fb_ram_size = aligned_size;
            memory_region_init_ram(&s->fb_ram, OBJECT(s), bi->name, aligned_size, errp);
            memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, aligned_size);
            /* Map RAM as subregion of MMIO so fb accesses go to fb_ram */
            memory_region_add_subregion(mr, 0, &s->fb_ram);
            pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
        } else {
            memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, aligned_size);
            pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
        }
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  ARK_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  ARK_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, ARK_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    /* arkfb uses only BAR0 as linear framebuffer; mmio_len is reported 0 */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    s->bar_info[0].size  = 4 * MiB; /* large enough for reported screen_size */
    s->bar_info[0].name  = "arkfb-mmio";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type  = BAR_TYPE_NONE;
        s->bar_info[i].size  = 0;
        s->bar_info[i].name  = "unused";
    }
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize legacy VGA I/O space for the device's bus as conventional VGA. */
    /* The driver uses pcibios_bus_to_resource on a 64K IO range; QEMU's generic
     * VGA will already have registered those ports globally. Our pio ops are
     * bound only to PCI BARs, so they do not conflict.
     */

    /* msi_init or msix_init calls */
    s->has_msi = false;
    s->has_msix = false;

    /* Initialize register shadows */
    memset(&s->regs, 0, sizeof(s->regs));
    s->regs.misc_out = 0x67;

    (void)ark_timing_regs; /* referenced to avoid unused warning; timing
                              information is used only by the driver for
                              mode programming and does not affect device
                              side-effects here. */
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

    /* Nothing special to free; MemoryRegions are freed by QOM */
    (void)s;
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "arkfb_pci",
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

