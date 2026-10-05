#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/pci/pci.h"
#include "hw/hw.h"
#include "hw/pci/msi.h"
#include "qemu/timer.h"
#include "qemu/main-loop.h" /* iothread mutex */
#include "qemu/module.h"
#include "qapi/visitor.h"

#define DRIVERS_PCI_SWITCH_SWITCHTEC_BUF_LEN 1024

extern uint64_t target_value[DRIVERS_PCI_SWITCH_SWITCHTEC_BUF_LEN];
extern bool target_value_reset;

typedef struct {
    PCIDevice pdev;
    MemoryRegion mmio[7];

    uint32_t len;
    uint64_t buf[DRIVERS_PCI_SWITCH_SWITCHTEC_BUF_LEN];
    int is_probe_time;
} Drivers_pci_switch_switchtecState;

#define TYPE_PCI_DRIVERS_PCI_SWITCH_SWITCHTEC_DEVICE "drivers_pci_switch_switchtec"
#define DRIVERS_PCI_SWITCH_SWITCHTEC(obj)        OBJECT_CHECK(Drivers_pci_switch_switchtecState, obj, TYPE_PCI_DRIVERS_PCI_SWITCH_SWITCHTEC_DEVICE)

static uint64_t drivers_pci_switch_switchtec_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    Drivers_pci_switch_switchtecState *drivers_pci_switch_switchtec = opaque;
    int i;

    if (target_value_reset) {
        drivers_pci_switch_switchtec->len = 0;
        target_value_reset = false;
    }

    if (!drivers_pci_switch_switchtec->is_probe_time) {
        for (i = 0; i < DRIVERS_PCI_SWITCH_SWITCHTEC_BUF_LEN; ++i) {
            drivers_pci_switch_switchtec->buf[i] = target_value[i];
        }
    } else {
        drivers_pci_switch_switchtec->is_probe_time--;
    }

    return drivers_pci_switch_switchtec->buf[(drivers_pci_switch_switchtec->len++) % DRIVERS_PCI_SWITCH_SWITCHTEC_BUF_LEN];
}

static void drivers_pci_switch_switchtec_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                unsigned size)
{
	return;
}

static const MemoryRegionOps drivers_pci_switch_switchtec_mmio_ops = {
    .read = drivers_pci_switch_switchtec_mmio_read,
    .write = drivers_pci_switch_switchtec_mmio_write,
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

static void pci_drivers_pci_switch_switchtec_realize(PCIDevice *pdev, Error **errp)
{
    Drivers_pci_switch_switchtecState *drivers_pci_switch_switchtec = DRIVERS_PCI_SWITCH_SWITCHTEC(pdev);
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

    drivers_pci_switch_switchtec->len = 0;
    drivers_pci_switch_switchtec->is_probe_time = 30;
	drivers_pci_switch_switchtec->buf[0] = 0xff;
	drivers_pci_switch_switchtec->buf[1] = 0xff;
	drivers_pci_switch_switchtec->buf[2] = 0xff;
	drivers_pci_switch_switchtec->buf[3] = 0xff;
	drivers_pci_switch_switchtec->buf[4] = 0xff;
	drivers_pci_switch_switchtec->buf[5] = 0xff;
	drivers_pci_switch_switchtec->buf[6] = 0xff;
	drivers_pci_switch_switchtec->buf[7] = 0xff;
	drivers_pci_switch_switchtec->buf[8] = 0xff;
	drivers_pci_switch_switchtec->buf[9] = 0xff;
	drivers_pci_switch_switchtec->buf[10] = 0xff;
	drivers_pci_switch_switchtec->buf[11] = 0xff;
	drivers_pci_switch_switchtec->buf[12] = 0xff;
	drivers_pci_switch_switchtec->buf[13] = 0xff;
	drivers_pci_switch_switchtec->buf[14] = 0xff;
	drivers_pci_switch_switchtec->buf[15] = 0xff;
	drivers_pci_switch_switchtec->buf[16] = 0xff;
	drivers_pci_switch_switchtec->buf[17] = 0xff;
	drivers_pci_switch_switchtec->buf[18] = 0xff;
	drivers_pci_switch_switchtec->buf[19] = 0xff;
	drivers_pci_switch_switchtec->buf[20] = 0xff;
	drivers_pci_switch_switchtec->buf[21] = 0xff;
	drivers_pci_switch_switchtec->buf[22] = 0xff;
	drivers_pci_switch_switchtec->buf[23] = 0xff;
	drivers_pci_switch_switchtec->buf[24] = 0xff;
	drivers_pci_switch_switchtec->buf[25] = 0xff;
	drivers_pci_switch_switchtec->buf[26] = 0xff;
	drivers_pci_switch_switchtec->buf[27] = 0xff;
	drivers_pci_switch_switchtec->buf[28] = 0xff;
	drivers_pci_switch_switchtec->buf[29] = 0xff;;

    memory_region_init_io(&drivers_pci_switch_switchtec->mmio[0], OBJECT(drivers_pci_switch_switchtec), &drivers_pci_switch_switchtec_mmio_ops, drivers_pci_switch_switchtec,
                    "drivers_pci_switch_switchtec-mmio0", 16 * MiB);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_pci_switch_switchtec->mmio[0]);
    memory_region_init_io(&drivers_pci_switch_switchtec->mmio[1], OBJECT(drivers_pci_switch_switchtec), &drivers_pci_switch_switchtec_mmio_ops, drivers_pci_switch_switchtec,
                    "drivers_pci_switch_switchtec-mmio1", 16 * MiB);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_pci_switch_switchtec->mmio[1]);
    memory_region_init_io(&drivers_pci_switch_switchtec->mmio[2], OBJECT(drivers_pci_switch_switchtec), &drivers_pci_switch_switchtec_mmio_ops, drivers_pci_switch_switchtec,
                    "drivers_pci_switch_switchtec-mmio2", 16 * MiB);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_pci_switch_switchtec->mmio[2]);
    memory_region_init_io(&drivers_pci_switch_switchtec->mmio[3], OBJECT(drivers_pci_switch_switchtec), &drivers_pci_switch_switchtec_mmio_ops, drivers_pci_switch_switchtec,
                    "drivers_pci_switch_switchtec-mmio3", 16 * MiB);
    pci_register_bar(pdev, 3, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_pci_switch_switchtec->mmio[3]);
    memory_region_init_io(&drivers_pci_switch_switchtec->mmio[4], OBJECT(drivers_pci_switch_switchtec), &drivers_pci_switch_switchtec_mmio_ops, drivers_pci_switch_switchtec,
                    "drivers_pci_switch_switchtec-mmio4", 16 * MiB);
    pci_register_bar(pdev, 4, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_pci_switch_switchtec->mmio[4]);
    memory_region_init_io(&drivers_pci_switch_switchtec->mmio[5], OBJECT(drivers_pci_switch_switchtec), &drivers_pci_switch_switchtec_mmio_ops, drivers_pci_switch_switchtec,
                    "drivers_pci_switch_switchtec-mmio5", 16 * MiB);
    pci_register_bar(pdev, 5, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_pci_switch_switchtec->mmio[5]);
    memory_region_init_io(&drivers_pci_switch_switchtec->mmio[6], OBJECT(drivers_pci_switch_switchtec), &drivers_pci_switch_switchtec_mmio_ops, drivers_pci_switch_switchtec,
                    "drivers_pci_switch_switchtec-mmio5", 16 * MiB);
    pci_register_bar(pdev, 6, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_pci_switch_switchtec->mmio[6]);
}

static void pci_drivers_pci_switch_switchtec_uninit(PCIDevice *pdev)
{
    msi_uninit(pdev);
}

static void drivers_pci_switch_switchtec_instance_init(Object *obj)
{
	return;
}

static void drivers_pci_switch_switchtec_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(class);

    k->realize = pci_drivers_pci_switch_switchtec_realize;
    k->exit = pci_drivers_pci_switch_switchtec_uninit;
    k->vendor_id = 0x11f8;
    k->device_id = 0x8531;
    k->revision = 0;
    k->subsystem_vendor_id = 0xffffffff;
    k->subsystem_id = 0xffffffff;
    k->class_id = 1408;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void pci_drivers_pci_switch_switchtec_register_types(void)
{
    static InterfaceInfo interfaces[] = {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    };
    static const TypeInfo drivers_pci_switch_switchtec_info = {
        .name          = TYPE_PCI_DRIVERS_PCI_SWITCH_SWITCHTEC_DEVICE,
        .parent        = TYPE_PCI_DEVICE,
        .instance_size = sizeof(Drivers_pci_switch_switchtecState),
        .instance_init = drivers_pci_switch_switchtec_instance_init,
        .class_init    = drivers_pci_switch_switchtec_class_init,
        .interfaces = interfaces,
    };

    type_register_static(&drivers_pci_switch_switchtec_info);
}
type_init(pci_drivers_pci_switch_switchtec_register_types)
