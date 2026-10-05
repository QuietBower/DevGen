#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/pci/pci.h"
#include "hw/hw.h"
#include "hw/pci/msi.h"
#include "qemu/timer.h"
#include "qemu/main-loop.h" /* iothread mutex */
#include "qemu/module.h"
#include "qapi/visitor.h"

#define DRIVERS_NET_WIRELESS_INTERSIL_ORINOCO_ORINOCO_TMD_BUF_LEN 1024

extern uint64_t target_value[DRIVERS_NET_WIRELESS_INTERSIL_ORINOCO_ORINOCO_TMD_BUF_LEN];
extern bool target_value_reset;

typedef struct {
    PCIDevice pdev;
    MemoryRegion mmio[7];

    uint32_t len;
    uint64_t buf[DRIVERS_NET_WIRELESS_INTERSIL_ORINOCO_ORINOCO_TMD_BUF_LEN];
    int is_probe_time;
} Drivers_net_wireless_intersil_orinoco_orinoco_tmdState;

#define TYPE_PCI_DRIVERS_NET_WIRELESS_INTERSIL_ORINOCO_ORINOCO_TMD_DEVICE "drivers_net_wireless_intersil_orinoco_orinoco_tmd"
#define DRIVERS_NET_WIRELESS_INTERSIL_ORINOCO_ORINOCO_TMD(obj)        OBJECT_CHECK(Drivers_net_wireless_intersil_orinoco_orinoco_tmdState, obj, TYPE_PCI_DRIVERS_NET_WIRELESS_INTERSIL_ORINOCO_ORINOCO_TMD_DEVICE)

static uint64_t drivers_net_wireless_intersil_orinoco_orinoco_tmd_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    Drivers_net_wireless_intersil_orinoco_orinoco_tmdState *drivers_net_wireless_intersil_orinoco_orinoco_tmd = opaque;
    int i;

    if (target_value_reset) {
        drivers_net_wireless_intersil_orinoco_orinoco_tmd->len = 0;
        target_value_reset = false;
    }

    if (!drivers_net_wireless_intersil_orinoco_orinoco_tmd->is_probe_time) {
        for (i = 0; i < DRIVERS_NET_WIRELESS_INTERSIL_ORINOCO_ORINOCO_TMD_BUF_LEN; ++i) {
            drivers_net_wireless_intersil_orinoco_orinoco_tmd->buf[i] = target_value[i];
        }
    } else {
        drivers_net_wireless_intersil_orinoco_orinoco_tmd->is_probe_time--;
    }

    return drivers_net_wireless_intersil_orinoco_orinoco_tmd->buf[(drivers_net_wireless_intersil_orinoco_orinoco_tmd->len++) % DRIVERS_NET_WIRELESS_INTERSIL_ORINOCO_ORINOCO_TMD_BUF_LEN];
}

static void drivers_net_wireless_intersil_orinoco_orinoco_tmd_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                unsigned size)
{
	return;
}

static const MemoryRegionOps drivers_net_wireless_intersil_orinoco_orinoco_tmd_mmio_ops = {
    .read = drivers_net_wireless_intersil_orinoco_orinoco_tmd_mmio_read,
    .write = drivers_net_wireless_intersil_orinoco_orinoco_tmd_mmio_write,
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

static void pci_drivers_net_wireless_intersil_orinoco_orinoco_tmd_realize(PCIDevice *pdev, Error **errp)
{
    Drivers_net_wireless_intersil_orinoco_orinoco_tmdState *drivers_net_wireless_intersil_orinoco_orinoco_tmd = DRIVERS_NET_WIRELESS_INTERSIL_ORINOCO_ORINOCO_TMD(pdev);
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

    drivers_net_wireless_intersil_orinoco_orinoco_tmd->len = 0;
    drivers_net_wireless_intersil_orinoco_orinoco_tmd->is_probe_time = 4;
	drivers_net_wireless_intersil_orinoco_orinoco_tmd->buf[0] = 0xff;
	drivers_net_wireless_intersil_orinoco_orinoco_tmd->buf[1] = 0xff;
	drivers_net_wireless_intersil_orinoco_orinoco_tmd->buf[2] = 0xff;
	drivers_net_wireless_intersil_orinoco_orinoco_tmd->buf[3] = 0xff;;

    memory_region_init_io(&drivers_net_wireless_intersil_orinoco_orinoco_tmd->mmio[0], OBJECT(drivers_net_wireless_intersil_orinoco_orinoco_tmd), &drivers_net_wireless_intersil_orinoco_orinoco_tmd_mmio_ops, drivers_net_wireless_intersil_orinoco_orinoco_tmd,
                    "drivers_net_wireless_intersil_orinoco_orinoco_tmd-mmio0", 16 * MiB);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_net_wireless_intersil_orinoco_orinoco_tmd->mmio[0]);
    memory_region_init_io(&drivers_net_wireless_intersil_orinoco_orinoco_tmd->mmio[1], OBJECT(drivers_net_wireless_intersil_orinoco_orinoco_tmd), &drivers_net_wireless_intersil_orinoco_orinoco_tmd_mmio_ops, drivers_net_wireless_intersil_orinoco_orinoco_tmd,
                    "drivers_net_wireless_intersil_orinoco_orinoco_tmd-mmio1", 16 * MiB);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_net_wireless_intersil_orinoco_orinoco_tmd->mmio[1]);
    memory_region_init_io(&drivers_net_wireless_intersil_orinoco_orinoco_tmd->mmio[2], OBJECT(drivers_net_wireless_intersil_orinoco_orinoco_tmd), &drivers_net_wireless_intersil_orinoco_orinoco_tmd_mmio_ops, drivers_net_wireless_intersil_orinoco_orinoco_tmd,
                    "drivers_net_wireless_intersil_orinoco_orinoco_tmd-mmio2", 16 * MiB);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_net_wireless_intersil_orinoco_orinoco_tmd->mmio[2]);
    memory_region_init_io(&drivers_net_wireless_intersil_orinoco_orinoco_tmd->mmio[3], OBJECT(drivers_net_wireless_intersil_orinoco_orinoco_tmd), &drivers_net_wireless_intersil_orinoco_orinoco_tmd_mmio_ops, drivers_net_wireless_intersil_orinoco_orinoco_tmd,
                    "drivers_net_wireless_intersil_orinoco_orinoco_tmd-mmio3", 16 * MiB);
    pci_register_bar(pdev, 3, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_net_wireless_intersil_orinoco_orinoco_tmd->mmio[3]);
    memory_region_init_io(&drivers_net_wireless_intersil_orinoco_orinoco_tmd->mmio[4], OBJECT(drivers_net_wireless_intersil_orinoco_orinoco_tmd), &drivers_net_wireless_intersil_orinoco_orinoco_tmd_mmio_ops, drivers_net_wireless_intersil_orinoco_orinoco_tmd,
                    "drivers_net_wireless_intersil_orinoco_orinoco_tmd-mmio4", 16 * MiB);
    pci_register_bar(pdev, 4, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_net_wireless_intersil_orinoco_orinoco_tmd->mmio[4]);
    memory_region_init_io(&drivers_net_wireless_intersil_orinoco_orinoco_tmd->mmio[5], OBJECT(drivers_net_wireless_intersil_orinoco_orinoco_tmd), &drivers_net_wireless_intersil_orinoco_orinoco_tmd_mmio_ops, drivers_net_wireless_intersil_orinoco_orinoco_tmd,
                    "drivers_net_wireless_intersil_orinoco_orinoco_tmd-mmio5", 16 * MiB);
    pci_register_bar(pdev, 5, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_net_wireless_intersil_orinoco_orinoco_tmd->mmio[5]);
    memory_region_init_io(&drivers_net_wireless_intersil_orinoco_orinoco_tmd->mmio[6], OBJECT(drivers_net_wireless_intersil_orinoco_orinoco_tmd), &drivers_net_wireless_intersil_orinoco_orinoco_tmd_mmio_ops, drivers_net_wireless_intersil_orinoco_orinoco_tmd,
                    "drivers_net_wireless_intersil_orinoco_orinoco_tmd-mmio5", 16 * MiB);
    pci_register_bar(pdev, 6, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_net_wireless_intersil_orinoco_orinoco_tmd->mmio[6]);
}

static void pci_drivers_net_wireless_intersil_orinoco_orinoco_tmd_uninit(PCIDevice *pdev)
{
    msi_uninit(pdev);
}

static void drivers_net_wireless_intersil_orinoco_orinoco_tmd_instance_init(Object *obj)
{
	return;
}

static void drivers_net_wireless_intersil_orinoco_orinoco_tmd_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(class);

    k->realize = pci_drivers_net_wireless_intersil_orinoco_orinoco_tmd_realize;
    k->exit = pci_drivers_net_wireless_intersil_orinoco_orinoco_tmd_uninit;
    k->vendor_id = 0x15e8;
    k->device_id = 0x131;
    k->revision = 0;
    k->subsystem_vendor_id = 0xffffffff;
    k->subsystem_id = 0xffffffff;
    k->class_id = 16777215;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void pci_drivers_net_wireless_intersil_orinoco_orinoco_tmd_register_types(void)
{
    static InterfaceInfo interfaces[] = {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    };
    static const TypeInfo drivers_net_wireless_intersil_orinoco_orinoco_tmd_info = {
        .name          = TYPE_PCI_DRIVERS_NET_WIRELESS_INTERSIL_ORINOCO_ORINOCO_TMD_DEVICE,
        .parent        = TYPE_PCI_DEVICE,
        .instance_size = sizeof(Drivers_net_wireless_intersil_orinoco_orinoco_tmdState),
        .instance_init = drivers_net_wireless_intersil_orinoco_orinoco_tmd_instance_init,
        .class_init    = drivers_net_wireless_intersil_orinoco_orinoco_tmd_class_init,
        .interfaces = interfaces,
    };

    type_register_static(&drivers_net_wireless_intersil_orinoco_orinoco_tmd_info);
}
type_init(pci_drivers_net_wireless_intersil_orinoco_orinoco_tmd_register_types)
