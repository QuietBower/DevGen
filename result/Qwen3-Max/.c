#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/pci/pci.h"
#include "hw/hw.h"
#include "hw/pci/msi.h"
#include "qemu/timer.h"
#include "qemu/main-loop.h" /* iothread mutex */
#include "qemu/module.h"
#include "qapi/visitor.h"

#define _BUF_LEN 1024

extern uint64_t target_value[_BUF_LEN];
extern bool target_value_reset;

typedef struct {
    PCIDevice pdev;
    MemoryRegion mmio[7];

    uint32_t len;
    uint64_t buf[_BUF_LEN];
    int is_probe_time;
} State;

#define TYPE_PCI__DEVICE ""
#define (obj)        OBJECT_CHECK(State, obj, TYPE_PCI__DEVICE)

static uint64_t _mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    State * = opaque;
    int i;

    if (target_value_reset) {
        ->len = 0;
        target_value_reset = false;
    }

    if (!->is_probe_time) {
        for (i = 0; i < _BUF_LEN; ++i) {
            ->buf[i] = target_value[i];
        }
    } else {
        ->is_probe_time--;
    }

    return ->buf[(->len++) % _BUF_LEN];
}

static void _mmio_write(void *opaque, hwaddr addr, uint64_t val,
                unsigned size)
{
	return;
}

static const MemoryRegionOps _mmio_ops = {
    .read = _mmio_read,
    .write = _mmio_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 8,
    },

};

static void pci__realize(PCIDevice *pdev, Error **errp)
{
    State * = (pdev);
    uint8_t *pci_conf = pdev->config;
	int pos;

    pci_config_set_interrupt_pin(pci_conf, 1);
	

    if (msi_init(pdev, 0, 1, true, false, errp)) {
        return;
    }
	
	pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
	pcie_endpoint_cap_init(pdev, 0);

	pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
	pdev->exp.pm_cap = pos;
	pci_set_word(pdev->config + pos + PCI_PM_PMC, 0x3);

    ->len = 0;
    ->is_probe_time = 0;
	;

    memory_region_init_io(&->mmio[0], OBJECT(), &_mmio_ops, ,
                    "-mmio0", 16 * MiB);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &->mmio[0]);
    memory_region_init_io(&->mmio[1], OBJECT(), &_mmio_ops, ,
                    "-mmio1", 16 * MiB);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &->mmio[1]);
    memory_region_init_io(&->mmio[2], OBJECT(), &_mmio_ops, ,
                    "-mmio2", 16 * MiB);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &->mmio[2]);
    memory_region_init_io(&->mmio[3], OBJECT(), &_mmio_ops, ,
                    "-mmio3", 16 * MiB);
    pci_register_bar(pdev, 3, PCI_BASE_ADDRESS_SPACE_MEMORY, &->mmio[3]);
    memory_region_init_io(&->mmio[4], OBJECT(), &_mmio_ops, ,
                    "-mmio4", 16 * MiB);
    pci_register_bar(pdev, 4, PCI_BASE_ADDRESS_SPACE_MEMORY, &->mmio[4]);
    memory_region_init_io(&->mmio[5], OBJECT(), &_mmio_ops, ,
                    "-mmio5", 16 * MiB);
    pci_register_bar(pdev, 5, PCI_BASE_ADDRESS_SPACE_MEMORY, &->mmio[5]);
    memory_region_init_io(&->mmio[6], OBJECT(), &_mmio_ops, ,
                    "-mmio5", 16 * MiB);
    pci_register_bar(pdev, 6, PCI_BASE_ADDRESS_SPACE_MEMORY, &->mmio[6]);
}

static void pci__uninit(PCIDevice *pdev)
{
    msi_uninit(pdev);
}

static void _instance_init(Object *obj)
{
	return;
}

static void _class_init(ObjectClass *class, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(class);

    k->realize = pci__realize;
    k->exit = pci__uninit;
    k->vendor_id = 0xffffffff;
    k->device_id = 0xffffffff;
    k->revision = 0;
    k->subsystem_vendor_id = 0xffffffff;
    k->subsystem_id = 0xffffffff;
    k->class_id = 1540;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void pci__register_types(void)
{
    static InterfaceInfo interfaces[] = {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    };
    static const TypeInfo _info = {
        .name          = TYPE_PCI__DEVICE,
        .parent        = TYPE_PCI_DEVICE,
        .instance_size = sizeof(State),
        .instance_init = _instance_init,
        .class_init    = _class_init,
        .interfaces = interfaces,
    };

    type_register_static(&_info);
}
type_init(pci__register_types)
