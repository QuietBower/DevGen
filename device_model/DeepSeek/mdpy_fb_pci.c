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

#define MDPY_VENDORCAP_OFFSET   0x40
#define MDPY_FORMAT_OFFSET      (MDPY_VENDORCAP_OFFSET + 0x04)
#define MDPY_WIDTH_OFFSET       (MDPY_VENDORCAP_OFFSET + 0x08)
#define MDPY_HEIGHT_OFFSET      (MDPY_VENDORCAP_OFFSET + 0x0c)

/* Framebuffer dimensions */
#define MDPY_DEFAULT_WIDTH  800
#define MDPY_DEFAULT_HEIGHT 600
#define MDPY_BYTES_PER_PIXEL 4
/* Round up to next power of 2 to satisfy pci_register_bar requirement */
#define BAR0_SIZE (1 << 21) /* 2 MiB, originally 800*600*4=1,920,000 */

struct PCIBaseState {
    PCIDevice parent_obj;
    MemoryRegion bar0;
    /* We use BAR0 as framebuffer memory only */
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    /* Config values are set in realize, no reset needed */
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, 0x1b36);
    pci_set_word(pci_conf + PCI_DEVICE_ID, 0x000f);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0000); /* FIXME: unknown class id */
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* Set vendor capability registers */
    pci_set_long(pci_conf + MDPY_FORMAT_OFFSET, 0x34325258); /* DRM_FORMAT_XRGB8888 */
    pci_set_long(pci_conf + MDPY_WIDTH_OFFSET, MDPY_DEFAULT_WIDTH);
    pci_set_long(pci_conf + MDPY_HEIGHT_OFFSET, MDPY_DEFAULT_HEIGHT);

    /* BAR 0: framebuffer memory */
    memory_region_init_ram(&s->bar0, OBJECT(s), "mdpy-fb", BAR0_SIZE, errp);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar0);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    /* No specific cleanup needed beyond PCI core */
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

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