#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/pci/pci.h"
#include "hw/hw.h"
#include "hw/pci/msi.h"
#include "qemu/timer.h"
#include "qemu/main-loop.h" /* iothread mutex */
#include "qemu/module.h"
#include "qapi/visitor.h"

#define DRIVERS_TTY_SERIAL_RP2_BUF_LEN 1024

extern uint64_t target_value[DRIVERS_TTY_SERIAL_RP2_BUF_LEN];
extern bool target_value_reset;

typedef struct {
    PCIDevice pdev;
    MemoryRegion mmio[7];

    uint32_t len;
    uint64_t buf[DRIVERS_TTY_SERIAL_RP2_BUF_LEN];
    int is_probe_time;
} Drivers_tty_serial_rp2State;

#define TYPE_PCI_DRIVERS_TTY_SERIAL_RP2_DEVICE "drivers_tty_serial_rp2"
#define DRIVERS_TTY_SERIAL_RP2(obj)        OBJECT_CHECK(Drivers_tty_serial_rp2State, obj, TYPE_PCI_DRIVERS_TTY_SERIAL_RP2_DEVICE)

static uint64_t drivers_tty_serial_rp2_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    Drivers_tty_serial_rp2State *drivers_tty_serial_rp2 = opaque;
    int i;

    if (target_value_reset) {
        drivers_tty_serial_rp2->len = 0;
        target_value_reset = false;
    }

    if (!drivers_tty_serial_rp2->is_probe_time) {
        for (i = 0; i < DRIVERS_TTY_SERIAL_RP2_BUF_LEN; ++i) {
            drivers_tty_serial_rp2->buf[i] = target_value[i];
        }
    } else {
        drivers_tty_serial_rp2->is_probe_time--;
    }

    return drivers_tty_serial_rp2->buf[(drivers_tty_serial_rp2->len++) % DRIVERS_TTY_SERIAL_RP2_BUF_LEN];
}

static void drivers_tty_serial_rp2_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                unsigned size)
{
	return;
}

static const MemoryRegionOps drivers_tty_serial_rp2_mmio_ops = {
    .read = drivers_tty_serial_rp2_mmio_read,
    .write = drivers_tty_serial_rp2_mmio_write,
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

static void pci_drivers_tty_serial_rp2_realize(PCIDevice *pdev, Error **errp)
{
    Drivers_tty_serial_rp2State *drivers_tty_serial_rp2 = DRIVERS_TTY_SERIAL_RP2(pdev);
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

    drivers_tty_serial_rp2->len = 0;
    drivers_tty_serial_rp2->is_probe_time = 20;
	drivers_tty_serial_rp2->buf[0] = 0xff;
	drivers_tty_serial_rp2->buf[1] = 0xff;
	drivers_tty_serial_rp2->buf[2] = 0xff;
	drivers_tty_serial_rp2->buf[3] = 0xff;
	drivers_tty_serial_rp2->buf[4] = 0xff;
	drivers_tty_serial_rp2->buf[5] = 0xff;
	drivers_tty_serial_rp2->buf[6] = 0xff;
	drivers_tty_serial_rp2->buf[7] = 0xff;
	drivers_tty_serial_rp2->buf[8] = 0xff;
	drivers_tty_serial_rp2->buf[9] = 0xff;
	drivers_tty_serial_rp2->buf[10] = 0xff;
	drivers_tty_serial_rp2->buf[11] = 0xff;
	drivers_tty_serial_rp2->buf[12] = 0xff;
	drivers_tty_serial_rp2->buf[13] = 0xff;
	drivers_tty_serial_rp2->buf[14] = 0xff;
	drivers_tty_serial_rp2->buf[15] = 0xff;
	drivers_tty_serial_rp2->buf[16] = 0xff;
	drivers_tty_serial_rp2->buf[17] = 0xff;
	drivers_tty_serial_rp2->buf[18] = 0xff;
	drivers_tty_serial_rp2->buf[19] = 0xff;;

    memory_region_init_io(&drivers_tty_serial_rp2->mmio[0], OBJECT(drivers_tty_serial_rp2), &drivers_tty_serial_rp2_mmio_ops, drivers_tty_serial_rp2,
                    "drivers_tty_serial_rp2-mmio0", 16 * MiB);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_tty_serial_rp2->mmio[0]);
    memory_region_init_io(&drivers_tty_serial_rp2->mmio[1], OBJECT(drivers_tty_serial_rp2), &drivers_tty_serial_rp2_mmio_ops, drivers_tty_serial_rp2,
                    "drivers_tty_serial_rp2-mmio1", 16 * MiB);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_tty_serial_rp2->mmio[1]);
    memory_region_init_io(&drivers_tty_serial_rp2->mmio[2], OBJECT(drivers_tty_serial_rp2), &drivers_tty_serial_rp2_mmio_ops, drivers_tty_serial_rp2,
                    "drivers_tty_serial_rp2-mmio2", 16 * MiB);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_tty_serial_rp2->mmio[2]);
    memory_region_init_io(&drivers_tty_serial_rp2->mmio[3], OBJECT(drivers_tty_serial_rp2), &drivers_tty_serial_rp2_mmio_ops, drivers_tty_serial_rp2,
                    "drivers_tty_serial_rp2-mmio3", 16 * MiB);
    pci_register_bar(pdev, 3, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_tty_serial_rp2->mmio[3]);
    memory_region_init_io(&drivers_tty_serial_rp2->mmio[4], OBJECT(drivers_tty_serial_rp2), &drivers_tty_serial_rp2_mmio_ops, drivers_tty_serial_rp2,
                    "drivers_tty_serial_rp2-mmio4", 16 * MiB);
    pci_register_bar(pdev, 4, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_tty_serial_rp2->mmio[4]);
    memory_region_init_io(&drivers_tty_serial_rp2->mmio[5], OBJECT(drivers_tty_serial_rp2), &drivers_tty_serial_rp2_mmio_ops, drivers_tty_serial_rp2,
                    "drivers_tty_serial_rp2-mmio5", 16 * MiB);
    pci_register_bar(pdev, 5, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_tty_serial_rp2->mmio[5]);
    memory_region_init_io(&drivers_tty_serial_rp2->mmio[6], OBJECT(drivers_tty_serial_rp2), &drivers_tty_serial_rp2_mmio_ops, drivers_tty_serial_rp2,
                    "drivers_tty_serial_rp2-mmio5", 16 * MiB);
    pci_register_bar(pdev, 6, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_tty_serial_rp2->mmio[6]);
}

static void pci_drivers_tty_serial_rp2_uninit(PCIDevice *pdev)
{
    msi_uninit(pdev);
}

static void drivers_tty_serial_rp2_instance_init(Object *obj)
{
	return;
}

static void drivers_tty_serial_rp2_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(class);

    k->realize = pci_drivers_tty_serial_rp2_realize;
    k->exit = pci_drivers_tty_serial_rp2_uninit;
    k->vendor_id = 0x11fe;
    k->device_id = 0x40;
    k->revision = 0;
    k->subsystem_vendor_id = 0xffffffff;
    k->subsystem_id = 0xffffffff;
    k->class_id = 16777215;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void pci_drivers_tty_serial_rp2_register_types(void)
{
    static InterfaceInfo interfaces[] = {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    };
    static const TypeInfo drivers_tty_serial_rp2_info = {
        .name          = TYPE_PCI_DRIVERS_TTY_SERIAL_RP2_DEVICE,
        .parent        = TYPE_PCI_DEVICE,
        .instance_size = sizeof(Drivers_tty_serial_rp2State),
        .instance_init = drivers_tty_serial_rp2_instance_init,
        .class_init    = drivers_tty_serial_rp2_class_init,
        .interfaces = interfaces,
    };

    type_register_static(&drivers_tty_serial_rp2_info);
}
type_init(pci_drivers_tty_serial_rp2_register_types)
