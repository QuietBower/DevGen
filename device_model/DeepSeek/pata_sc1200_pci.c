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
#include "hw/pci/pci_device.h"
#include "hw/pci/pci_bus.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-core.h"
#include "migration/vmstate.h"

#define TYPE_PCIBASE_DEVICE "pata_sc1200_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI_VENDOR_ID_NS 0x100B
#define PCI_DEVICE_ID_NS_SCx200_IDE 0x0502
#define PCI_CLASS_IDE 0x0101

typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM
} BARType;

typedef struct {
    int     index;
    BARType type;
    hwaddr  size;
    const char *name;
} BARInfo;

struct PCIBaseState {
    PCIDevice parent_obj;
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;
};

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    pci_device_reset(PCI_DEVICE(dev));
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    } else {
        g_assert_not_reached();
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_NS);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_NS_SCx200_IDE);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_IDE);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    static const BARInfo sc1200_bar_info[] = {
        { 0, BAR_TYPE_PIO, 8, "primary-cmd" },
        { 1, BAR_TYPE_PIO, 4, "primary-ctl" },
        { 2, BAR_TYPE_PIO, 8, "secondary-cmd" },
        { 3, BAR_TYPE_PIO, 4, "secondary-ctl" },
        { 4, BAR_TYPE_PIO, 16, "bmdma" },
    };
    s->num_bars = ARRAY_SIZE(sc1200_bar_info);
    memcpy(s->bar_info, sc1200_bar_info, sizeof(sc1200_bar_info));

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Simulate BIOS resource assignment to work around platforms that
     * do not assign I/O BARs to this device (e.g., behind bridges without
     * IO window or PCIe root ports). Assign non-conflicting, high I/O
     * addresses so that the kernel sees BARs as assigned and enables the
     * device. */
    pci_set_long(pci_conf + PCI_BASE_ADDRESS_0, 0x1000 | PCI_BASE_ADDRESS_SPACE_IO);
    pci_set_long(pci_conf + PCI_BASE_ADDRESS_1, 0x1008 | PCI_BASE_ADDRESS_SPACE_IO);
    pci_set_long(pci_conf + PCI_BASE_ADDRESS_2, 0x1010 | PCI_BASE_ADDRESS_SPACE_IO);
    pci_set_long(pci_conf + PCI_BASE_ADDRESS_3, 0x1018 | PCI_BASE_ADDRESS_SPACE_IO);
    pci_set_long(pci_conf + PCI_BASE_ADDRESS_4, 0x1020 | PCI_BASE_ADDRESS_SPACE_IO);

    /* Set PCI_COMMAND_IO to allow the kernel PCI layer to treat the
     * device as IO-capable and proceed with resource checks. */
    pci_set_word(pci_conf + PCI_COMMAND, PCI_COMMAND_IO);
}

static void pcibase_uninit(PCIDevice *pdev)
{
}

static const VMStateDescription vmstate_pcibase = {
    .name = "pata_sc1200_pci",
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
