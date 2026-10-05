#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/pci/pci.h"
#include "hw/hw.h"
#include "hw/pci/msi.h"
#include "qemu/timer.h"
#include "qemu/main-loop.h" /* iothread mutex */
#include "qemu/module.h"
#include "qapi/visitor.h"

#define DRIVERS_THUNDERBOLT_NHI_BUF_LEN 1024

extern uint64_t target_value[DRIVERS_THUNDERBOLT_NHI_BUF_LEN];
extern bool target_value_reset;

typedef struct {
    PCIDevice pdev;
    MemoryRegion mmio[7];

    uint32_t len;
    uint64_t buf[DRIVERS_THUNDERBOLT_NHI_BUF_LEN];
    int is_probe_time;
} Drivers_thunderbolt_nhiState;

#define TYPE_PCI_DRIVERS_THUNDERBOLT_NHI_DEVICE "drivers_thunderbolt_nhi"
#define DRIVERS_THUNDERBOLT_NHI(obj)        OBJECT_CHECK(Drivers_thunderbolt_nhiState, obj, TYPE_PCI_DRIVERS_THUNDERBOLT_NHI_DEVICE)

static uint64_t drivers_thunderbolt_nhi_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    Drivers_thunderbolt_nhiState *drivers_thunderbolt_nhi = opaque;
    int i;

    if (target_value_reset) {
        drivers_thunderbolt_nhi->len = 0;
        target_value_reset = false;
    }

    if (!drivers_thunderbolt_nhi->is_probe_time) {
        for (i = 0; i < DRIVERS_THUNDERBOLT_NHI_BUF_LEN; ++i) {
            drivers_thunderbolt_nhi->buf[i] = target_value[i];
        }
    } else {
        drivers_thunderbolt_nhi->is_probe_time--;
    }

    return drivers_thunderbolt_nhi->buf[(drivers_thunderbolt_nhi->len++) % DRIVERS_THUNDERBOLT_NHI_BUF_LEN];
}

static void drivers_thunderbolt_nhi_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                unsigned size)
{
	return;
}

static const MemoryRegionOps drivers_thunderbolt_nhi_mmio_ops = {
    .read = drivers_thunderbolt_nhi_mmio_read,
    .write = drivers_thunderbolt_nhi_mmio_write,
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

static void pci_drivers_thunderbolt_nhi_realize(PCIDevice *pdev, Error **errp)
{
    Drivers_thunderbolt_nhiState *drivers_thunderbolt_nhi = DRIVERS_THUNDERBOLT_NHI(pdev);
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

    drivers_thunderbolt_nhi->len = 0;
    drivers_thunderbolt_nhi->is_probe_time = 20;
	drivers_thunderbolt_nhi->buf[0] = 0xff;
	drivers_thunderbolt_nhi->buf[1] = 0xff;
	drivers_thunderbolt_nhi->buf[2] = 0xff;
	drivers_thunderbolt_nhi->buf[3] = 0xff;
	drivers_thunderbolt_nhi->buf[4] = 0xff;
	drivers_thunderbolt_nhi->buf[5] = 0xff;
	drivers_thunderbolt_nhi->buf[6] = 0xff;
	drivers_thunderbolt_nhi->buf[7] = 0xff;
	drivers_thunderbolt_nhi->buf[8] = 0xff;
	drivers_thunderbolt_nhi->buf[9] = 0xff;
	drivers_thunderbolt_nhi->buf[10] = 0xff;
	drivers_thunderbolt_nhi->buf[11] = 0xff;
	drivers_thunderbolt_nhi->buf[12] = 0xff;
	drivers_thunderbolt_nhi->buf[13] = 0xff;
	drivers_thunderbolt_nhi->buf[14] = 0xff;
	drivers_thunderbolt_nhi->buf[15] = 0xff;
	drivers_thunderbolt_nhi->buf[16] = 0xff;
	drivers_thunderbolt_nhi->buf[17] = 0xff;
	drivers_thunderbolt_nhi->buf[18] = 0xff;
	drivers_thunderbolt_nhi->buf[19] = 0xff;;

    memory_region_init_io(&drivers_thunderbolt_nhi->mmio[0], OBJECT(drivers_thunderbolt_nhi), &drivers_thunderbolt_nhi_mmio_ops, drivers_thunderbolt_nhi,
                    "drivers_thunderbolt_nhi-mmio0", 16 * MiB);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_thunderbolt_nhi->mmio[0]);
    memory_region_init_io(&drivers_thunderbolt_nhi->mmio[1], OBJECT(drivers_thunderbolt_nhi), &drivers_thunderbolt_nhi_mmio_ops, drivers_thunderbolt_nhi,
                    "drivers_thunderbolt_nhi-mmio1", 16 * MiB);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_thunderbolt_nhi->mmio[1]);
    memory_region_init_io(&drivers_thunderbolt_nhi->mmio[2], OBJECT(drivers_thunderbolt_nhi), &drivers_thunderbolt_nhi_mmio_ops, drivers_thunderbolt_nhi,
                    "drivers_thunderbolt_nhi-mmio2", 16 * MiB);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_thunderbolt_nhi->mmio[2]);
    memory_region_init_io(&drivers_thunderbolt_nhi->mmio[3], OBJECT(drivers_thunderbolt_nhi), &drivers_thunderbolt_nhi_mmio_ops, drivers_thunderbolt_nhi,
                    "drivers_thunderbolt_nhi-mmio3", 16 * MiB);
    pci_register_bar(pdev, 3, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_thunderbolt_nhi->mmio[3]);
    memory_region_init_io(&drivers_thunderbolt_nhi->mmio[4], OBJECT(drivers_thunderbolt_nhi), &drivers_thunderbolt_nhi_mmio_ops, drivers_thunderbolt_nhi,
                    "drivers_thunderbolt_nhi-mmio4", 16 * MiB);
    pci_register_bar(pdev, 4, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_thunderbolt_nhi->mmio[4]);
    memory_region_init_io(&drivers_thunderbolt_nhi->mmio[5], OBJECT(drivers_thunderbolt_nhi), &drivers_thunderbolt_nhi_mmio_ops, drivers_thunderbolt_nhi,
                    "drivers_thunderbolt_nhi-mmio5", 16 * MiB);
    pci_register_bar(pdev, 5, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_thunderbolt_nhi->mmio[5]);
    memory_region_init_io(&drivers_thunderbolt_nhi->mmio[6], OBJECT(drivers_thunderbolt_nhi), &drivers_thunderbolt_nhi_mmio_ops, drivers_thunderbolt_nhi,
                    "drivers_thunderbolt_nhi-mmio5", 16 * MiB);
    pci_register_bar(pdev, 6, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_thunderbolt_nhi->mmio[6]);
}

static void pci_drivers_thunderbolt_nhi_uninit(PCIDevice *pdev)
{
    msi_uninit(pdev);
}

static void drivers_thunderbolt_nhi_instance_init(Object *obj)
{
	return;
}

static void drivers_thunderbolt_nhi_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(class);

    k->realize = pci_drivers_thunderbolt_nhi_realize;
    k->exit = pci_drivers_thunderbolt_nhi_uninit;
    k->vendor_id = 0x8086;
    k->device_id = 0x1513;
    k->revision = 0;
    k->subsystem_vendor_id = 0x2222;
    k->subsystem_id = 0x1111;
    k->class_id = 2176;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void pci_drivers_thunderbolt_nhi_register_types(void)
{
    static InterfaceInfo interfaces[] = {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    };
    static const TypeInfo drivers_thunderbolt_nhi_info = {
        .name          = TYPE_PCI_DRIVERS_THUNDERBOLT_NHI_DEVICE,
        .parent        = TYPE_PCI_DEVICE,
        .instance_size = sizeof(Drivers_thunderbolt_nhiState),
        .instance_init = drivers_thunderbolt_nhi_instance_init,
        .class_init    = drivers_thunderbolt_nhi_class_init,
        .interfaces = interfaces,
    };

    type_register_static(&drivers_thunderbolt_nhi_info);
}
type_init(pci_drivers_thunderbolt_nhi_register_types)
