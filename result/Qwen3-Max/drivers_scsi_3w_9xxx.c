#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/pci/pci.h"
#include "hw/hw.h"
#include "hw/pci/msi.h"
#include "qemu/timer.h"
#include "qemu/main-loop.h" /* iothread mutex */
#include "qemu/module.h"
#include "qapi/visitor.h"

#define DRIVERS_SCSI_3W_9XXX_BUF_LEN 1024

extern uint64_t target_value[DRIVERS_SCSI_3W_9XXX_BUF_LEN];
extern bool target_value_reset;

typedef struct {
    PCIDevice pdev;
    MemoryRegion mmio[7];

    uint32_t len;
    uint64_t buf[DRIVERS_SCSI_3W_9XXX_BUF_LEN];
    int is_probe_time;
} Drivers_scsi_3w_9xxxState;

#define TYPE_PCI_DRIVERS_SCSI_3W_9XXX_DEVICE "drivers_scsi_3w_9xxx"
#define DRIVERS_SCSI_3W_9XXX(obj)        OBJECT_CHECK(Drivers_scsi_3w_9xxxState, obj, TYPE_PCI_DRIVERS_SCSI_3W_9XXX_DEVICE)

static uint64_t drivers_scsi_3w_9xxx_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    Drivers_scsi_3w_9xxxState *drivers_scsi_3w_9xxx = opaque;
    int i;

    if (target_value_reset) {
        drivers_scsi_3w_9xxx->len = 0;
        target_value_reset = false;
    }

    if (!drivers_scsi_3w_9xxx->is_probe_time) {
        for (i = 0; i < DRIVERS_SCSI_3W_9XXX_BUF_LEN; ++i) {
            drivers_scsi_3w_9xxx->buf[i] = target_value[i];
        }
    } else {
        drivers_scsi_3w_9xxx->is_probe_time--;
    }

    return drivers_scsi_3w_9xxx->buf[(drivers_scsi_3w_9xxx->len++) % DRIVERS_SCSI_3W_9XXX_BUF_LEN];
}

static void drivers_scsi_3w_9xxx_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                unsigned size)
{
	return;
}

static const MemoryRegionOps drivers_scsi_3w_9xxx_mmio_ops = {
    .read = drivers_scsi_3w_9xxx_mmio_read,
    .write = drivers_scsi_3w_9xxx_mmio_write,
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

static void pci_drivers_scsi_3w_9xxx_realize(PCIDevice *pdev, Error **errp)
{
    Drivers_scsi_3w_9xxxState *drivers_scsi_3w_9xxx = DRIVERS_SCSI_3W_9XXX(pdev);
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

    drivers_scsi_3w_9xxx->len = 0;
    drivers_scsi_3w_9xxx->is_probe_time = 40;
	drivers_scsi_3w_9xxx->buf[0] = 0xff;
	drivers_scsi_3w_9xxx->buf[1] = 0xff;
	drivers_scsi_3w_9xxx->buf[2] = 0xff;
	drivers_scsi_3w_9xxx->buf[3] = 0xff;
	drivers_scsi_3w_9xxx->buf[4] = 0xff;
	drivers_scsi_3w_9xxx->buf[5] = 0xff;
	drivers_scsi_3w_9xxx->buf[6] = 0xff;
	drivers_scsi_3w_9xxx->buf[7] = 0xff;
	drivers_scsi_3w_9xxx->buf[8] = 0xff;
	drivers_scsi_3w_9xxx->buf[9] = 0xff;
	drivers_scsi_3w_9xxx->buf[10] = 0xff;
	drivers_scsi_3w_9xxx->buf[11] = 0xff;
	drivers_scsi_3w_9xxx->buf[12] = 0xff;
	drivers_scsi_3w_9xxx->buf[13] = 0xff;
	drivers_scsi_3w_9xxx->buf[14] = 0xff;
	drivers_scsi_3w_9xxx->buf[15] = 0xff;
	drivers_scsi_3w_9xxx->buf[16] = 0xff;
	drivers_scsi_3w_9xxx->buf[17] = 0xff;
	drivers_scsi_3w_9xxx->buf[18] = 0xff;
	drivers_scsi_3w_9xxx->buf[19] = 0xff;
	drivers_scsi_3w_9xxx->buf[20] = 0xff;
	drivers_scsi_3w_9xxx->buf[21] = 0xff;
	drivers_scsi_3w_9xxx->buf[22] = 0xff;
	drivers_scsi_3w_9xxx->buf[23] = 0xff;
	drivers_scsi_3w_9xxx->buf[24] = 0xff;
	drivers_scsi_3w_9xxx->buf[25] = 0xff;
	drivers_scsi_3w_9xxx->buf[26] = 0xff;
	drivers_scsi_3w_9xxx->buf[27] = 0xff;
	drivers_scsi_3w_9xxx->buf[28] = 0xff;
	drivers_scsi_3w_9xxx->buf[29] = 0xff;
	drivers_scsi_3w_9xxx->buf[30] = 0xff;
	drivers_scsi_3w_9xxx->buf[31] = 0xff;
	drivers_scsi_3w_9xxx->buf[32] = 0xff;
	drivers_scsi_3w_9xxx->buf[33] = 0xff;
	drivers_scsi_3w_9xxx->buf[34] = 0xff;
	drivers_scsi_3w_9xxx->buf[35] = 0xff;
	drivers_scsi_3w_9xxx->buf[36] = 0xff;
	drivers_scsi_3w_9xxx->buf[37] = 0xff;
	drivers_scsi_3w_9xxx->buf[38] = 0xff;
	drivers_scsi_3w_9xxx->buf[39] = 0xff;;

    memory_region_init_io(&drivers_scsi_3w_9xxx->mmio[0], OBJECT(drivers_scsi_3w_9xxx), &drivers_scsi_3w_9xxx_mmio_ops, drivers_scsi_3w_9xxx,
                    "drivers_scsi_3w_9xxx-mmio0", 16 * MiB);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_scsi_3w_9xxx->mmio[0]);
    memory_region_init_io(&drivers_scsi_3w_9xxx->mmio[1], OBJECT(drivers_scsi_3w_9xxx), &drivers_scsi_3w_9xxx_mmio_ops, drivers_scsi_3w_9xxx,
                    "drivers_scsi_3w_9xxx-mmio1", 16 * MiB);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_scsi_3w_9xxx->mmio[1]);
    memory_region_init_io(&drivers_scsi_3w_9xxx->mmio[2], OBJECT(drivers_scsi_3w_9xxx), &drivers_scsi_3w_9xxx_mmio_ops, drivers_scsi_3w_9xxx,
                    "drivers_scsi_3w_9xxx-mmio2", 16 * MiB);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_scsi_3w_9xxx->mmio[2]);
    memory_region_init_io(&drivers_scsi_3w_9xxx->mmio[3], OBJECT(drivers_scsi_3w_9xxx), &drivers_scsi_3w_9xxx_mmio_ops, drivers_scsi_3w_9xxx,
                    "drivers_scsi_3w_9xxx-mmio3", 16 * MiB);
    pci_register_bar(pdev, 3, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_scsi_3w_9xxx->mmio[3]);
    memory_region_init_io(&drivers_scsi_3w_9xxx->mmio[4], OBJECT(drivers_scsi_3w_9xxx), &drivers_scsi_3w_9xxx_mmio_ops, drivers_scsi_3w_9xxx,
                    "drivers_scsi_3w_9xxx-mmio4", 16 * MiB);
    pci_register_bar(pdev, 4, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_scsi_3w_9xxx->mmio[4]);
    memory_region_init_io(&drivers_scsi_3w_9xxx->mmio[5], OBJECT(drivers_scsi_3w_9xxx), &drivers_scsi_3w_9xxx_mmio_ops, drivers_scsi_3w_9xxx,
                    "drivers_scsi_3w_9xxx-mmio5", 16 * MiB);
    pci_register_bar(pdev, 5, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_scsi_3w_9xxx->mmio[5]);
    memory_region_init_io(&drivers_scsi_3w_9xxx->mmio[6], OBJECT(drivers_scsi_3w_9xxx), &drivers_scsi_3w_9xxx_mmio_ops, drivers_scsi_3w_9xxx,
                    "drivers_scsi_3w_9xxx-mmio5", 16 * MiB);
    pci_register_bar(pdev, 6, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_scsi_3w_9xxx->mmio[6]);
}

static void pci_drivers_scsi_3w_9xxx_uninit(PCIDevice *pdev)
{
    msi_uninit(pdev);
}

static void drivers_scsi_3w_9xxx_instance_init(Object *obj)
{
	return;
}

static void drivers_scsi_3w_9xxx_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(class);

    k->realize = pci_drivers_scsi_3w_9xxx_realize;
    k->exit = pci_drivers_scsi_3w_9xxx_uninit;
    k->vendor_id = 0x13c1;
    k->device_id = 0x1002;
    k->revision = 0;
    k->subsystem_vendor_id = 0xffffffff;
    k->subsystem_id = 0xffffffff;
    k->class_id = 16777215;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void pci_drivers_scsi_3w_9xxx_register_types(void)
{
    static InterfaceInfo interfaces[] = {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    };
    static const TypeInfo drivers_scsi_3w_9xxx_info = {
        .name          = TYPE_PCI_DRIVERS_SCSI_3W_9XXX_DEVICE,
        .parent        = TYPE_PCI_DEVICE,
        .instance_size = sizeof(Drivers_scsi_3w_9xxxState),
        .instance_init = drivers_scsi_3w_9xxx_instance_init,
        .class_init    = drivers_scsi_3w_9xxx_class_init,
        .interfaces = interfaces,
    };

    type_register_static(&drivers_scsi_3w_9xxx_info);
}
type_init(pci_drivers_scsi_3w_9xxx_register_types)
