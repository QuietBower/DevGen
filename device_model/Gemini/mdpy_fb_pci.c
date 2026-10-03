/*
 * Integrated QEMU PCI device template (QEMU 8.2.10).
 * Designed for register-level modeling and PCIe driver probing.
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

#define TYPE_PCIBASE_DEVICE "mdpy_fb_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#ifndef fourcc_code
#define fourcc_code(a, b, c, d) ((uint32_t)(a) | ((uint32_t)(b) << 8) | \
                                 ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))
#endif
#define DRM_FORMAT_XRGB8888	fourcc_code('X', 'R', '2', '4')

/* Register Layout and Hardware Identifiers extracted from driver source */
#define MDPY_PCI_VENDOR_ID	PCI_VENDOR_ID_REDHAT
#define MDPY_PCI_DEVICE_ID	0x000f
#define MDPY_VENDORCAP_OFFSET   0x40
#define MDPY_FORMAT_OFFSET	(MDPY_VENDORCAP_OFFSET + 0x04)
#define MDPY_WIDTH_OFFSET	(MDPY_VENDORCAP_OFFSET + 0x08)
#define MDPY_HEIGHT_OFFSET	(MDPY_VENDORCAP_OFFSET + 0x0c)

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

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t format;
    uint32_t width;
    uint32_t height;
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    
    /* Reset values based on driver expectations */
    s->format = DRM_FORMAT_XRGB8888;
    s->width = 800;  /* Default valid width (100 <= width <= 10000) */
    s->height = 600; /* Default valid height (100 <= height <= 10000) */
    
    pci_set_long(PCI_DEVICE(dev)->config + MDPY_FORMAT_OFFSET, s->format);
    pci_set_long(PCI_DEVICE(dev)->config + MDPY_WIDTH_OFFSET, s->width);
    pci_set_long(PCI_DEVICE(dev)->config + MDPY_HEIGHT_OFFSET, s->height);
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE || bi->size == 0) {
        return;
    }
    
    /* CRITICAL: PCI requires BAR sizes to be a power of 2. Prevent QEMU assert crash. */
    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  MDPY_PCI_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  MDPY_PCI_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_DISPLAY_OTHER );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    
    /* Vendor Specific Capability for mdpy registers */
    pci_add_capability(pdev, PCI_CAP_ID_VNDR, MDPY_VENDORCAP_OFFSET, 0x10, errp);

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_RAM;
    s->bar_info[0].name = "mdpy-fb";
    /* Size is implicitly width * height * 4, setting a default to allow mapping */
    s->bar_info[0].size = 800 * 600 * 4; 

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    /* No MSI/MSIX or other resources to uninit */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "mdpy_fb_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(format, PCIBaseState),
        VMSTATE_UINT32(width, PCIBaseState),
        VMSTATE_UINT32(height, PCIBaseState),
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
