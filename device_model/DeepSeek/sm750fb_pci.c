/*
 * QEMU SM750 Frame Buffer device model
 * Generated from Linux driver staging/sm750fb/sm750.c
 * Implements basic register interface for probe success.
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

/* Vendor and Device IDs from driver PCI table */
#define VENDOR_ID 0x126f
#define DEVICE_ID 0x0750

/* DE (Draw Engine) register offsets */
#define DE_SOURCE                              0x0
#define DE_DESTINATION                         0x4
#define DE_DIMENSION                           0x8
#define DE_CONTROL                             0xC
#define DE_PITCH                               0x000010
#define DE_FOREGROUND                          0x000014
#define DE_BACKGROUND                          0x000018
#define DE_STRETCH_FORMAT                      0x00001C
#define DE_COLOR_COMPARE                       0x000020
#define DE_COLOR_COMPARE_MASK                  0x000024
#define DE_MASKS                               0x000028
#define DE_CLIP_TL                             0x00002C
#define DE_CLIP_BR                             0x000030
#define DE_WINDOW_WIDTH                        0x00003C
#define DE_WINDOW_SOURCE_BASE                  0x000040
#define DE_WINDOW_DESTINATION_BASE             0x000044

/* DE Control masks and constants */
#define DE_CONTROL_ROP_MASK                    0xff
#define DE_CONTROL_ROP_SELECT                  BIT(15)
#define DE_CONTROL_TRANSPARENCY                BIT(8)
#define DE_CONTROL_TRANSPARENCY_SELECT         BIT(9)
#define DE_CONTROL_TRANSPARENCY_MATCH          BIT(10)
#define DE_CONTROL_DIRECTION                   BIT(27)
#define DE_CONTROL_STATUS                      BIT(31)
#define DE_CONTROL_HOST                        BIT(22)
#define DE_CONTROL_LAST_PIXEL                  BIT(21)
#define DE_CONTROL_COMMAND_BITBLT              (0x0 << 16)
#define DE_CONTROL_COMMAND_HOST_WRITE          (0x8 << 16)
#define DE_CONTROL_COMMAND_RECTANGLE_FILL      (0x1 << 16)

#define HW_ROP2_COPY                           0xc
#define HW_ROP2_XOR                            0x6

#define SM750LE_REVISION_ID                    ((unsigned char)0xfe)

#define DEFAULT_SM750_CHIP_CLOCK               290
#define DEFAULT_SM750LE_CHIP_CLOCK             333

#define DE_BASE_ADDR_TYPE1                     0x100000
#define DE_PORT_ADDR_TYPE1                     0x110000

#define DE_SOURCE_X_K1_MASK                    (0x3fff << 16)
#define DE_SOURCE_X_K1_MONO_MASK               (0x1f << 16)
#define DE_SOURCE_X_K1_SHIFT                   16
#define DE_SOURCE_Y_K2_MASK                    0xffff
#define DE_DESTINATION_X_MASK                  (0x1fff << 16)
#define DE_DESTINATION_X_SHIFT                 16
#define DE_DESTINATION_Y_MASK                  0xffff
#define DE_DIMENSION_X_MASK                    (0x1fff << 16)
#define DE_DIMENSION_X_SHIFT                   16
#define DE_DIMENSION_Y_ET_MASK                 0x1fff
#define DE_PITCH_SOURCE_MASK                   0x1fff
#define DE_PITCH_DESTINATION_MASK              (0x1fff << 16)
#define DE_PITCH_DESTINATION_SHIFT             16
#define DE_WINDOW_WIDTH_SRC_MASK               0x1fff
#define DE_WINDOW_WIDTH_DST_MASK               (0x1fff << 16)
#define DE_WINDOW_WIDTH_DST_SHIFT              16

#define TOP_TO_BOTTOM                          0
#define BOTTOM_TO_TOP                          1
#define LEFT_TO_RIGHT                          0
#define RIGHT_TO_LEFT                          1

/* Enums from driver */
enum sm750_pnltype {
    sm750_24TFT = 0,
    sm750_dualTFT = 2,
    sm750_doubleTFT = 1,
};

enum sm750_dataflow {
    sm750_simul_pri,
    sm750_simul_sec,
    sm750_dual_normal,
    sm750_dual_swap,
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

    /* Hardware Register Shadows (The 'Identity' of the device) */
    /* Init Status Register Shadows */
    uint16_t powerMode;
    uint16_t chip_clk;
    uint16_t mem_clk;
    uint16_t master_clk;
    uint16_t setAllEngOff;
    uint16_t resetMemory;

    /* Operational status flags */
    /* DE Engine registers shadows */
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
};

/* Internal helper for status-triggered signaling. Unused; empty. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
}

/* MMIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size != 4) {
        qemu_log_mask(LOG_UNIMP, "sm750fb: unsupported MMIO read size %u at 0x" HWADDR_FMT_plx "\n", size, addr);
        return 0;
    }

    switch (addr) {
    case DE_SOURCE:
        val = s->de_source;
        break;
    case DE_DESTINATION:
        val = s->de_destination;
        break;
    case DE_DIMENSION:
        val = s->de_dimension;
        break;
    case DE_CONTROL:
        val = s->de_control;
        break;
    case DE_PITCH:
        val = s->de_pitch;
        break;
    case DE_FOREGROUND:
        val = s->de_foreground;
        break;
    case DE_BACKGROUND:
        val = s->de_background;
        break;
    case DE_STRETCH_FORMAT:
        val = s->de_stretch_format;
        break;
    case DE_COLOR_COMPARE:
        val = s->de_color_compare;
        break;
    case DE_COLOR_COMPARE_MASK:
        val = s->de_color_compare_mask;
        break;
    case DE_MASKS:
        val = s->de_masks;
        break;
    case DE_CLIP_TL:
        val = s->de_clip_tl;
        break;
    case DE_CLIP_BR:
        val = s->de_clip_br;
        break;
    case DE_WINDOW_WIDTH:
        val = s->de_window_width;
        break;
    case DE_WINDOW_SOURCE_BASE:
        val = s->de_window_source_base;
        break;
    case DE_WINDOW_DESTINATION_BASE:
        val = s->de_window_destination_base;
        break;
    default:
        /* Unknown register, return 0 */
        val = 0;
        qemu_log_mask(LOG_GUEST_ERROR, "sm750fb: unsupported MMIO read at 0x" HWADDR_FMT_plx "\n", addr);
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        qemu_log_mask(LOG_UNIMP, "sm750fb: unsupported MMIO write size %u at 0x" HWADDR_FMT_plx "\n", size, addr);
        return;
    }

    switch (addr) {
    case DE_SOURCE:
        s->de_source = val;
        break;
    case DE_DESTINATION:
        s->de_destination = val;
        break;
    case DE_DIMENSION:
        s->de_dimension = val;
        break;
    case DE_CONTROL:
        s->de_control = val;
        /* Immediately set idle status to avoid driver hang */
        s->de_control |= DE_CONTROL_STATUS;
        break;
    case DE_PITCH:
        s->de_pitch = val;
        break;
    case DE_FOREGROUND:
        s->de_foreground = val;
        break;
    case DE_BACKGROUND:
        s->de_background = val;
        break;
    case DE_STRETCH_FORMAT:
        s->de_stretch_format = val;
        break;
    case DE_COLOR_COMPARE:
        s->de_color_compare = val;
        break;
    case DE_COLOR_COMPARE_MASK:
        s->de_color_compare_mask = val;
        break;
    case DE_MASKS:
        s->de_masks = val;
        break;
    case DE_CLIP_TL:
        s->de_clip_tl = val;
        break;
    case DE_CLIP_BR:
        s->de_clip_br = val;
        break;
    case DE_WINDOW_WIDTH:
        s->de_window_width = val;
        break;
    case DE_WINDOW_SOURCE_BASE:
        s->de_window_source_base = val;
        break;
    case DE_WINDOW_DESTINATION_BASE:
        s->de_window_destination_base = val;
        break;
    default:
        /* Ignore writes to unknown registers */
        qemu_log_mask(LOG_GUEST_ERROR, "sm750fb: unsupported MMIO write at 0x" HWADDR_FMT_plx ", value 0x%" PRIx64 "\n", addr, val);
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* No PIO registers */
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    /* No PIO registers */
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

    /* Initialize init status parameters */
    s->powerMode = 0;
    s->chip_clk = 0;
    s->mem_clk = 0;
    s->master_clk = 0;
    s->setAllEngOff = 0;
    s->resetMemory = 1;

    /* DE engine: idle, status set, all other registers zero except control */
    s->de_control = DE_CONTROL_STATUS; /* idle */
    s->de_source = 0;
    s->de_destination = 0;
    s->de_dimension = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x038000); /* PCI_CLASS_DISPLAY_OTHER */
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
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = 0x200000, .name = "sm750fb-mmio" };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_RAM, .size = 0x1000000, .name = "sm750fb-vram" };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI or MSI-X initialization - not used */
    s->has_msi = false;
    s->has_msix = false;

    /* Field Initialization */
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
