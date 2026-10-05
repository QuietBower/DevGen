#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/pci/pci.h"
#include "hw/hw.h"
#include "hw/pci/msi.h"
#include "qemu/timer.h"
#include "qemu/main-loop.h" /* iothread mutex */
#include "qemu/module.h"
#include "qapi/visitor.h"

#define DRIVERS_ISDN_HARDWARE_MISDN_SPEEDFAX_BUF_LEN 1024

extern uint64_t target_value[DRIVERS_ISDN_HARDWARE_MISDN_SPEEDFAX_BUF_LEN];
extern bool target_value_reset;

typedef struct {
    PCIDevice pdev;
    MemoryRegion mmio[7];

    uint32_t len;
    uint64_t buf[DRIVERS_ISDN_HARDWARE_MISDN_SPEEDFAX_BUF_LEN];
    int is_probe_time;
} Drivers_isdn_hardware_misdn_speedfaxState;

#define TYPE_PCI_DRIVERS_ISDN_HARDWARE_MISDN_SPEEDFAX_DEVICE "drivers_isdn_hardware_misdn_speedfax"
#define DRIVERS_ISDN_HARDWARE_MISDN_SPEEDFAX(obj)        OBJECT_CHECK(Drivers_isdn_hardware_misdn_speedfaxState, obj, TYPE_PCI_DRIVERS_ISDN_HARDWARE_MISDN_SPEEDFAX_DEVICE)

static uint64_t drivers_isdn_hardware_misdn_speedfax_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    Drivers_isdn_hardware_misdn_speedfaxState *drivers_isdn_hardware_misdn_speedfax = opaque;
    int i;

    if (target_value_reset) {
        drivers_isdn_hardware_misdn_speedfax->len = 0;
        target_value_reset = false;
    }

    if (!drivers_isdn_hardware_misdn_speedfax->is_probe_time) {
        for (i = 0; i < DRIVERS_ISDN_HARDWARE_MISDN_SPEEDFAX_BUF_LEN; ++i) {
            drivers_isdn_hardware_misdn_speedfax->buf[i] = target_value[i];
        }
    } else {
        drivers_isdn_hardware_misdn_speedfax->is_probe_time--;
    }

    return drivers_isdn_hardware_misdn_speedfax->buf[(drivers_isdn_hardware_misdn_speedfax->len++) % DRIVERS_ISDN_HARDWARE_MISDN_SPEEDFAX_BUF_LEN];
}

static void drivers_isdn_hardware_misdn_speedfax_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                unsigned size)
{
	return;
}

static const MemoryRegionOps drivers_isdn_hardware_misdn_speedfax_mmio_ops = {
    .read = drivers_isdn_hardware_misdn_speedfax_mmio_read,
    .write = drivers_isdn_hardware_misdn_speedfax_mmio_write,
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

static void pci_drivers_isdn_hardware_misdn_speedfax_realize(PCIDevice *pdev, Error **errp)
{
    Drivers_isdn_hardware_misdn_speedfaxState *drivers_isdn_hardware_misdn_speedfax = DRIVERS_ISDN_HARDWARE_MISDN_SPEEDFAX(pdev);
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

    drivers_isdn_hardware_misdn_speedfax->len = 0;
    drivers_isdn_hardware_misdn_speedfax->is_probe_time = 0;
	;

    memory_region_init_io(&drivers_isdn_hardware_misdn_speedfax->mmio[0], OBJECT(drivers_isdn_hardware_misdn_speedfax), &drivers_isdn_hardware_misdn_speedfax_mmio_ops, drivers_isdn_hardware_misdn_speedfax,
                    "drivers_isdn_hardware_misdn_speedfax-mmio0", 16 * MiB);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_isdn_hardware_misdn_speedfax->mmio[0]);
    memory_region_init_io(&drivers_isdn_hardware_misdn_speedfax->mmio[1], OBJECT(drivers_isdn_hardware_misdn_speedfax), &drivers_isdn_hardware_misdn_speedfax_mmio_ops, drivers_isdn_hardware_misdn_speedfax,
                    "drivers_isdn_hardware_misdn_speedfax-mmio1", 16 * MiB);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_isdn_hardware_misdn_speedfax->mmio[1]);
    memory_region_init_io(&drivers_isdn_hardware_misdn_speedfax->mmio[2], OBJECT(drivers_isdn_hardware_misdn_speedfax), &drivers_isdn_hardware_misdn_speedfax_mmio_ops, drivers_isdn_hardware_misdn_speedfax,
                    "drivers_isdn_hardware_misdn_speedfax-mmio2", 16 * MiB);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_isdn_hardware_misdn_speedfax->mmio[2]);
    memory_region_init_io(&drivers_isdn_hardware_misdn_speedfax->mmio[3], OBJECT(drivers_isdn_hardware_misdn_speedfax), &drivers_isdn_hardware_misdn_speedfax_mmio_ops, drivers_isdn_hardware_misdn_speedfax,
                    "drivers_isdn_hardware_misdn_speedfax-mmio3", 16 * MiB);
    pci_register_bar(pdev, 3, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_isdn_hardware_misdn_speedfax->mmio[3]);
    memory_region_init_io(&drivers_isdn_hardware_misdn_speedfax->mmio[4], OBJECT(drivers_isdn_hardware_misdn_speedfax), &drivers_isdn_hardware_misdn_speedfax_mmio_ops, drivers_isdn_hardware_misdn_speedfax,
                    "drivers_isdn_hardware_misdn_speedfax-mmio4", 16 * MiB);
    pci_register_bar(pdev, 4, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_isdn_hardware_misdn_speedfax->mmio[4]);
    memory_region_init_io(&drivers_isdn_hardware_misdn_speedfax->mmio[5], OBJECT(drivers_isdn_hardware_misdn_speedfax), &drivers_isdn_hardware_misdn_speedfax_mmio_ops, drivers_isdn_hardware_misdn_speedfax,
                    "drivers_isdn_hardware_misdn_speedfax-mmio5", 16 * MiB);
    pci_register_bar(pdev, 5, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_isdn_hardware_misdn_speedfax->mmio[5]);
    memory_region_init_io(&drivers_isdn_hardware_misdn_speedfax->mmio[6], OBJECT(drivers_isdn_hardware_misdn_speedfax), &drivers_isdn_hardware_misdn_speedfax_mmio_ops, drivers_isdn_hardware_misdn_speedfax,
                    "drivers_isdn_hardware_misdn_speedfax-mmio5", 16 * MiB);
    pci_register_bar(pdev, 6, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_isdn_hardware_misdn_speedfax->mmio[6]);
}

static void pci_drivers_isdn_hardware_misdn_speedfax_uninit(PCIDevice *pdev)
{
    msi_uninit(pdev);
}

static void drivers_isdn_hardware_misdn_speedfax_instance_init(Object *obj)
{
	return;
}

static void drivers_isdn_hardware_misdn_speedfax_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(class);

    k->realize = pci_drivers_isdn_hardware_misdn_speedfax_realize;
    k->exit = pci_drivers_isdn_hardware_misdn_speedfax_uninit;
    k->vendor_id = 0xe159;
    k->device_id = 0x2;
    k->revision = 0;
    k->subsystem_vendor_id = 0x51;
    k->subsystem_id = 0x1;
    k->class_id = 16777215;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void pci_drivers_isdn_hardware_misdn_speedfax_register_types(void)
{
    static InterfaceInfo interfaces[] = {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    };
    static const TypeInfo drivers_isdn_hardware_misdn_speedfax_info = {
        .name          = TYPE_PCI_DRIVERS_ISDN_HARDWARE_MISDN_SPEEDFAX_DEVICE,
        .parent        = TYPE_PCI_DEVICE,
        .instance_size = sizeof(Drivers_isdn_hardware_misdn_speedfaxState),
        .instance_init = drivers_isdn_hardware_misdn_speedfax_instance_init,
        .class_init    = drivers_isdn_hardware_misdn_speedfax_class_init,
        .interfaces = interfaces,
    };

    type_register_static(&drivers_isdn_hardware_misdn_speedfax_info);
}
type_init(pci_drivers_isdn_hardware_misdn_speedfax_register_types)
