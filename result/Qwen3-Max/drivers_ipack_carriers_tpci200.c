#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/pci/pci.h"
#include "hw/hw.h"
#include "hw/pci/msi.h"
#include "qemu/timer.h"
#include "qemu/main-loop.h" /* iothread mutex */
#include "qemu/module.h"
#include "qapi/visitor.h"

#define DRIVERS_IPACK_CARRIERS_TPCI200_BUF_LEN 1024

extern uint64_t target_value[DRIVERS_IPACK_CARRIERS_TPCI200_BUF_LEN];
extern bool target_value_reset;

typedef struct {
    PCIDevice pdev;
    MemoryRegion mmio[7];

    uint32_t len;
    uint64_t buf[DRIVERS_IPACK_CARRIERS_TPCI200_BUF_LEN];
    int is_probe_time;
} Drivers_ipack_carriers_tpci200State;

#define TYPE_PCI_DRIVERS_IPACK_CARRIERS_TPCI200_DEVICE "drivers_ipack_carriers_tpci200"
#define DRIVERS_IPACK_CARRIERS_TPCI200(obj)        OBJECT_CHECK(Drivers_ipack_carriers_tpci200State, obj, TYPE_PCI_DRIVERS_IPACK_CARRIERS_TPCI200_DEVICE)

static uint64_t drivers_ipack_carriers_tpci200_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    Drivers_ipack_carriers_tpci200State *drivers_ipack_carriers_tpci200 = opaque;
    int i;

    if (target_value_reset) {
        drivers_ipack_carriers_tpci200->len = 0;
        target_value_reset = false;
    }

    if (!drivers_ipack_carriers_tpci200->is_probe_time) {
        for (i = 0; i < DRIVERS_IPACK_CARRIERS_TPCI200_BUF_LEN; ++i) {
            drivers_ipack_carriers_tpci200->buf[i] = target_value[i];
        }
    } else {
        drivers_ipack_carriers_tpci200->is_probe_time--;
    }

    return drivers_ipack_carriers_tpci200->buf[(drivers_ipack_carriers_tpci200->len++) % DRIVERS_IPACK_CARRIERS_TPCI200_BUF_LEN];
}

static void drivers_ipack_carriers_tpci200_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                unsigned size)
{
	return;
}

static const MemoryRegionOps drivers_ipack_carriers_tpci200_mmio_ops = {
    .read = drivers_ipack_carriers_tpci200_mmio_read,
    .write = drivers_ipack_carriers_tpci200_mmio_write,
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

static void pci_drivers_ipack_carriers_tpci200_realize(PCIDevice *pdev, Error **errp)
{
    Drivers_ipack_carriers_tpci200State *drivers_ipack_carriers_tpci200 = DRIVERS_IPACK_CARRIERS_TPCI200(pdev);
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

    drivers_ipack_carriers_tpci200->len = 0;
    drivers_ipack_carriers_tpci200->is_probe_time = 8;
	drivers_ipack_carriers_tpci200->buf[0] = 0xff;
	drivers_ipack_carriers_tpci200->buf[1] = 0xff;
	drivers_ipack_carriers_tpci200->buf[2] = 0xff;
	drivers_ipack_carriers_tpci200->buf[3] = 0xff;
	drivers_ipack_carriers_tpci200->buf[4] = 0xff;
	drivers_ipack_carriers_tpci200->buf[5] = 0xff;
	drivers_ipack_carriers_tpci200->buf[6] = 0xff;
	drivers_ipack_carriers_tpci200->buf[7] = 0xff;;

    memory_region_init_io(&drivers_ipack_carriers_tpci200->mmio[0], OBJECT(drivers_ipack_carriers_tpci200), &drivers_ipack_carriers_tpci200_mmio_ops, drivers_ipack_carriers_tpci200,
                    "drivers_ipack_carriers_tpci200-mmio0", 16 * MiB);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_ipack_carriers_tpci200->mmio[0]);
    memory_region_init_io(&drivers_ipack_carriers_tpci200->mmio[1], OBJECT(drivers_ipack_carriers_tpci200), &drivers_ipack_carriers_tpci200_mmio_ops, drivers_ipack_carriers_tpci200,
                    "drivers_ipack_carriers_tpci200-mmio1", 16 * MiB);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_ipack_carriers_tpci200->mmio[1]);
    memory_region_init_io(&drivers_ipack_carriers_tpci200->mmio[2], OBJECT(drivers_ipack_carriers_tpci200), &drivers_ipack_carriers_tpci200_mmio_ops, drivers_ipack_carriers_tpci200,
                    "drivers_ipack_carriers_tpci200-mmio2", 16 * MiB);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_ipack_carriers_tpci200->mmio[2]);
    memory_region_init_io(&drivers_ipack_carriers_tpci200->mmio[3], OBJECT(drivers_ipack_carriers_tpci200), &drivers_ipack_carriers_tpci200_mmio_ops, drivers_ipack_carriers_tpci200,
                    "drivers_ipack_carriers_tpci200-mmio3", 16 * MiB);
    pci_register_bar(pdev, 3, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_ipack_carriers_tpci200->mmio[3]);
    memory_region_init_io(&drivers_ipack_carriers_tpci200->mmio[4], OBJECT(drivers_ipack_carriers_tpci200), &drivers_ipack_carriers_tpci200_mmio_ops, drivers_ipack_carriers_tpci200,
                    "drivers_ipack_carriers_tpci200-mmio4", 16 * MiB);
    pci_register_bar(pdev, 4, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_ipack_carriers_tpci200->mmio[4]);
    memory_region_init_io(&drivers_ipack_carriers_tpci200->mmio[5], OBJECT(drivers_ipack_carriers_tpci200), &drivers_ipack_carriers_tpci200_mmio_ops, drivers_ipack_carriers_tpci200,
                    "drivers_ipack_carriers_tpci200-mmio5", 16 * MiB);
    pci_register_bar(pdev, 5, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_ipack_carriers_tpci200->mmio[5]);
    memory_region_init_io(&drivers_ipack_carriers_tpci200->mmio[6], OBJECT(drivers_ipack_carriers_tpci200), &drivers_ipack_carriers_tpci200_mmio_ops, drivers_ipack_carriers_tpci200,
                    "drivers_ipack_carriers_tpci200-mmio5", 16 * MiB);
    pci_register_bar(pdev, 6, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_ipack_carriers_tpci200->mmio[6]);
}

static void pci_drivers_ipack_carriers_tpci200_uninit(PCIDevice *pdev)
{
    msi_uninit(pdev);
}

static void drivers_ipack_carriers_tpci200_instance_init(Object *obj)
{
	return;
}

static void drivers_ipack_carriers_tpci200_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(class);

    k->realize = pci_drivers_ipack_carriers_tpci200_realize;
    k->exit = pci_drivers_ipack_carriers_tpci200_uninit;
    k->vendor_id = 0x1498;
    k->device_id = 0x30c8;
    k->revision = 0;
    k->subsystem_vendor_id = 0x1498;
    k->subsystem_id = 0x300a;
    k->class_id = 16777215;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void pci_drivers_ipack_carriers_tpci200_register_types(void)
{
    static InterfaceInfo interfaces[] = {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    };
    static const TypeInfo drivers_ipack_carriers_tpci200_info = {
        .name          = TYPE_PCI_DRIVERS_IPACK_CARRIERS_TPCI200_DEVICE,
        .parent        = TYPE_PCI_DEVICE,
        .instance_size = sizeof(Drivers_ipack_carriers_tpci200State),
        .instance_init = drivers_ipack_carriers_tpci200_instance_init,
        .class_init    = drivers_ipack_carriers_tpci200_class_init,
        .interfaces = interfaces,
    };

    type_register_static(&drivers_ipack_carriers_tpci200_info);
}
type_init(pci_drivers_ipack_carriers_tpci200_register_types)
