/*
 * QEMU PCI device model for mdpy-fb framebuffer device
 * Generated according to Linux driver samples/vfio-mdev/mdpy-fb.c
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
/* #include <linux/pci_ids.h> */

#define TYPE_PCIBASE_DEVICE "mdpy_fb_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define MDPY_VENDORCAP_OFFSET   0x40
#define MDPY_FORMAT_OFFSET      (MDPY_VENDORCAP_OFFSET + 0x04)
#define MDPY_WIDTH_OFFSET       (MDPY_VENDORCAP_OFFSET + 0x08)
#define MDPY_HEIGHT_OFFSET      (MDPY_VENDORCAP_OFFSET + 0x0c)

/* Local definitions for IDs normally provided by linux/pci_ids.h */
#ifndef PCI_VENDOR_ID_REDHAT
#define PCI_VENDOR_ID_REDHAT 0x1b36
#endif
#ifndef PCI_SUBVENDOR_ID_REDHAT_QUMRANET
#define PCI_SUBVENDOR_ID_REDHAT_QUMRANET 0x1af4
#endif
#ifndef PCI_SUBDEVICE_ID_QEMU
#define PCI_SUBDEVICE_ID_QEMU 0x1100
#endif

#define MDPY_PCI_VENDOR_ID      PCI_VENDOR_ID_REDHAT
#define MDPY_PCI_DEVICE_ID      0x000f
#define MDPY_PCI_SUBVENDOR_ID   PCI_SUBVENDOR_ID_REDHAT_QUMRANET
#define MDPY_PCI_SUBDEVICE_ID   PCI_SUBDEVICE_ID_QEMU

#define MDPY_FB_BAR_INDEX       0
#define MDPY_FB_BAR_SIZE        (1 * MiB)

#define PCIBASE_PCI_VENDOR_ID   MDPY_PCI_VENDOR_ID
#define PCIBASE_PCI_DEVICE_ID   MDPY_PCI_DEVICE_ID
#define PCIBASE_PCI_CLASS_ID    PCI_CLASS_DISPLAY_OTHER

/* The driver checks for this DRM format value in PCI config space */
#ifndef DRM_FORMAT_XRGB8888
#define DRM_FORMAT_XRGB8888 0x34325258 /* 'XRGB8888' */
#endif

/* Default framebuffer dimensions used if firmware/host doesn't override */
#define MDPY_DEFAULT_WIDTH   1024
#define MDPY_DEFAULT_HEIGHT  768

/* Vendor specific capability header layout (simple, as used by driver) */
#define MDPY_VENDOR_CAP_ID   0x09  /* Vendor specific capability ID */

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
    uint32_t cfg_vendor_cap;   /* dword at 0x40 */
    uint32_t cfg_format;       /* MDPY_FORMAT_OFFSET */
    uint32_t cfg_width;        /* MDPY_WIDTH_OFFSET */
    uint32_t cfg_height;       /* MDPY_HEIGHT_OFFSET */

    /* Simple framebuffer backing store (BAR0 MMIO RAM) */
    uint8_t *fb_data;
    hwaddr fb_size;
};


/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    /* mdpy-fb driver does not use interrupts; nothing to do */
    (void)s;
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /* mdpy-fb driver does not program any DMA engines; stub */
    (void)s;
    (void)is_write;
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* The driver maps BAR0 as framebuffer I/O memory using ioremap and
     * then the fb layer will access it directly via the mapping.
     * These accesses go to the MemoryRegion backing store directly
     * (fb_data), not through this callback, because we register BAR0
     * as an IO region. However, we implement a minimal handler in case
     * of incidental probes or tools touching the region.
     */

    /* For now we simply return zeroed data for any MMIO reads. */
    switch (size) {
    case 1:
        if (addr < s->fb_size && s->fb_data) {
            val = s->fb_data[addr];
        } else {
            val = 0;
        }
        break;
    case 2:
        if (addr + 1 < s->fb_size && s->fb_data) {
            val = lduw_le_p(&s->fb_data[addr]);
        } else {
            val = 0;
        }
        break;
    case 4:
        if (addr + 3 < s->fb_size && s->fb_data) {
            val = ldl_le_p(&s->fb_data[addr]);
        } else {
            val = 0;
        }
        break;
    case 8:
        if (addr + 7 < s->fb_size && s->fb_data) {
            val = ldq_le_p(&s->fb_data[addr]);
        } else {
            val = 0;
        }
        break;
    default:
        val = 0;
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* Similar to read side: this region represents framebuffer memory.
     * Store writes into our fb_data backing buffer.
     */

    if (!s->fb_data || addr >= s->fb_size) {
        return;
    }

    switch (size) {
    case 1:
        s->fb_data[addr] = (uint8_t)val;
        break;
    case 2:
        if (addr + 1 < s->fb_size) {
            stw_le_p(&s->fb_data[addr], (uint16_t)val);
        }
        break;
    case 4:
        if (addr + 3 < s->fb_size) {
            stl_le_p(&s->fb_data[addr], (uint32_t)val);
        }
        break;
    case 8:
        if (addr + 7 < s->fb_size) {
            stq_le_p(&s->fb_data[addr], (uint64_t)val);
        }
        break;
    default:
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    (void)s;
    (void)addr;
    (void)size;

    /* mdpy-fb driver does not use PIO */
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    (void)s;
    (void)addr;
    (void)val;
    (void)size;

    /* mdpy-fb driver does not use PIO */
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
    PCIDevice *pdev = PCI_DEVICE(dev);
    uint8_t *pci_conf = pdev->config;

    pci_device_reset(pdev);

    /* Revert registers to power-on defaults */
    s->cfg_vendor_cap = 0;
    s->cfg_format = DRM_FORMAT_XRGB8888;
    s->cfg_width = MDPY_DEFAULT_WIDTH;
    s->cfg_height = MDPY_DEFAULT_HEIGHT;

    /* Program config space dwords the driver reads via pci_read_config_dword */
    pci_set_long(pci_conf + MDPY_VENDORCAP_OFFSET, s->cfg_vendor_cap);
    pci_set_long(pci_conf + MDPY_FORMAT_OFFSET, s->cfg_format);
    pci_set_long(pci_conf + MDPY_WIDTH_OFFSET, s->cfg_width);
    pci_set_long(pci_conf + MDPY_HEIGHT_OFFSET, s->cfg_height);
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
        /* Use IO region backed by our fb_data through callbacks */
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCIBASE_PCI_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCIBASE_PCI_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCIBASE_PCI_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Subsystem IDs as in the mdpy example */
    pci_set_word(pci_conf + PCI_SUBSYSTEM_VENDOR_ID, MDPY_PCI_SUBVENDOR_ID);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_ID, MDPY_PCI_SUBDEVICE_ID);

    /* Initialize PCI Express capability */
    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* Initialize vendor-specific configuration dwords that the driver reads. */
    s->cfg_vendor_cap = 0;
    s->cfg_format = DRM_FORMAT_XRGB8888;
    s->cfg_width = MDPY_DEFAULT_WIDTH;
    s->cfg_height = MDPY_DEFAULT_HEIGHT;

    pci_set_long(pci_conf + MDPY_VENDORCAP_OFFSET, s->cfg_vendor_cap);
    pci_set_long(pci_conf + MDPY_FORMAT_OFFSET, s->cfg_format);
    pci_set_long(pci_conf + MDPY_WIDTH_OFFSET, s->cfg_width);
    pci_set_long(pci_conf + MDPY_HEIGHT_OFFSET, s->cfg_height);

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0].index = MDPY_FB_BAR_INDEX;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    s->bar_info[0].size  = MDPY_FB_BAR_SIZE;
    s->bar_info[0].name  = "mdpy-fb-bar0";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type  = BAR_TYPE_NONE;
        s->bar_info[i].size  = 0;
        s->bar_info[i].name  = NULL;
    }

    /* Allocate framebuffer backing storage */
    s->fb_size = MDPY_FB_BAR_SIZE;
    s->fb_data = g_malloc0(s->fb_size);

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* The mdpy-fb driver does not use MSI/MSI-X or DMA, so we don't
     * enable them here.
     */
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

    /* Free framebuffer backing store */
    if (s->fb_data) {
        g_free(s->fb_data);
        s->fb_data = NULL;
        s->fb_size = 0;
    }

    /* Free buffers, stop timers, etc. (none used) */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "mdpy_fb_pci",
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
