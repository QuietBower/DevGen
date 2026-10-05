#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/pci/pci.h"
#include "hw/hw.h"
#include "hw/pci/msi.h"
#include "qemu/timer.h"
#include "qemu/main-loop.h" /* iothread mutex */
#include "qemu/module.h"
#include "qapi/visitor.h"

#define DRIVERS_NET_ETHERNET_BROADCOM_TG3_BUF_LEN 1024

extern uint64_t target_value[DRIVERS_NET_ETHERNET_BROADCOM_TG3_BUF_LEN];
extern bool target_value_reset;

typedef struct {
    PCIDevice pdev;
    MemoryRegion mmio[7];

    uint32_t len;
    uint64_t buf[DRIVERS_NET_ETHERNET_BROADCOM_TG3_BUF_LEN];
    int is_probe_time;
} Drivers_net_ethernet_broadcom_tg3State;

#define TYPE_PCI_DRIVERS_NET_ETHERNET_BROADCOM_TG3_DEVICE "drivers_net_ethernet_broadcom_tg3"
#define DRIVERS_NET_ETHERNET_BROADCOM_TG3(obj)        OBJECT_CHECK(Drivers_net_ethernet_broadcom_tg3State, obj, TYPE_PCI_DRIVERS_NET_ETHERNET_BROADCOM_TG3_DEVICE)

static uint64_t drivers_net_ethernet_broadcom_tg3_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    Drivers_net_ethernet_broadcom_tg3State *drivers_net_ethernet_broadcom_tg3 = opaque;
    int i;

    if (target_value_reset) {
        drivers_net_ethernet_broadcom_tg3->len = 0;
        target_value_reset = false;
    }

    if (!drivers_net_ethernet_broadcom_tg3->is_probe_time) {
        for (i = 0; i < DRIVERS_NET_ETHERNET_BROADCOM_TG3_BUF_LEN; ++i) {
            drivers_net_ethernet_broadcom_tg3->buf[i] = target_value[i];
        }
    } else {
        drivers_net_ethernet_broadcom_tg3->is_probe_time--;
    }

    return drivers_net_ethernet_broadcom_tg3->buf[(drivers_net_ethernet_broadcom_tg3->len++) % DRIVERS_NET_ETHERNET_BROADCOM_TG3_BUF_LEN];
}

static void drivers_net_ethernet_broadcom_tg3_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                unsigned size)
{
	return;
}

static const MemoryRegionOps drivers_net_ethernet_broadcom_tg3_mmio_ops = {
    .read = drivers_net_ethernet_broadcom_tg3_mmio_read,
    .write = drivers_net_ethernet_broadcom_tg3_mmio_write,
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

static void pci_drivers_net_ethernet_broadcom_tg3_realize(PCIDevice *pdev, Error **errp)
{
    Drivers_net_ethernet_broadcom_tg3State *drivers_net_ethernet_broadcom_tg3 = DRIVERS_NET_ETHERNET_BROADCOM_TG3(pdev);
    uint8_t *pci_conf = pdev->config;
	int pos;

    pci_config_set_interrupt_pin(pci_conf, 1);
	pci_set_word(&pci_conf[4], 65535);
	pci_set_long(&pci_conf[4], 1264940628);
	pci_set_byte(&pci_conf[12], 255);
	pci_set_byte(&pci_conf[13], 255);
	pci_set_long(&pci_conf[104], 4294967295);
	pci_set_long(&pci_conf[112], 4294967295);
	pci_set_long(&pci_conf[132], 4294967295);
	pci_set_long(&pci_conf[196], 4294967295);
	

    if (msi_init(pdev, 0, 1, true, false, errp)) {
        return;
    }
	
	pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
	pcie_endpoint_cap_init(pdev, 0);

	pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
	pdev->exp.pm_cap = pos;
	pci_set_word(pdev->config + pos + PCI_PM_PMC, 0x3);

    drivers_net_ethernet_broadcom_tg3->len = 0;
    drivers_net_ethernet_broadcom_tg3->is_probe_time = 44;
	drivers_net_ethernet_broadcom_tg3->buf[0] = 0xff;
	drivers_net_ethernet_broadcom_tg3->buf[1] = 0xff;
	drivers_net_ethernet_broadcom_tg3->buf[2] = 0xff;
	drivers_net_ethernet_broadcom_tg3->buf[3] = 0xff;
	drivers_net_ethernet_broadcom_tg3->buf[4] = 0xff;
	drivers_net_ethernet_broadcom_tg3->buf[5] = 0xff;
	drivers_net_ethernet_broadcom_tg3->buf[6] = 0xff;
	drivers_net_ethernet_broadcom_tg3->buf[7] = 0xff;
	drivers_net_ethernet_broadcom_tg3->buf[8] = 0xff;
	drivers_net_ethernet_broadcom_tg3->buf[9] = 0xff;
	drivers_net_ethernet_broadcom_tg3->buf[10] = 0xff;
	drivers_net_ethernet_broadcom_tg3->buf[11] = 0xff;
	drivers_net_ethernet_broadcom_tg3->buf[12] = 0xff;
	drivers_net_ethernet_broadcom_tg3->buf[13] = 0xff;
	drivers_net_ethernet_broadcom_tg3->buf[14] = 0xff;
	drivers_net_ethernet_broadcom_tg3->buf[15] = 0xff;
	drivers_net_ethernet_broadcom_tg3->buf[16] = 0xff;
	drivers_net_ethernet_broadcom_tg3->buf[17] = 0xff;
	drivers_net_ethernet_broadcom_tg3->buf[18] = 0xff;
	drivers_net_ethernet_broadcom_tg3->buf[19] = 0xff;
	drivers_net_ethernet_broadcom_tg3->buf[20] = 0x0;
	drivers_net_ethernet_broadcom_tg3->buf[21] = 0x0;
	drivers_net_ethernet_broadcom_tg3->buf[22] = 0x0;
	drivers_net_ethernet_broadcom_tg3->buf[23] = 0x0;
	drivers_net_ethernet_broadcom_tg3->buf[24] = 0xff;
	drivers_net_ethernet_broadcom_tg3->buf[25] = 0xff;
	drivers_net_ethernet_broadcom_tg3->buf[26] = 0xff;
	drivers_net_ethernet_broadcom_tg3->buf[27] = 0xff;
	drivers_net_ethernet_broadcom_tg3->buf[28] = 0x41;
	drivers_net_ethernet_broadcom_tg3->buf[29] = 0x50;
	drivers_net_ethernet_broadcom_tg3->buf[30] = 0x45;
	drivers_net_ethernet_broadcom_tg3->buf[31] = 0x21;
	drivers_net_ethernet_broadcom_tg3->buf[32] = 0x41;
	drivers_net_ethernet_broadcom_tg3->buf[33] = 0x50;
	drivers_net_ethernet_broadcom_tg3->buf[34] = 0x45;
	drivers_net_ethernet_broadcom_tg3->buf[35] = 0x21;
	drivers_net_ethernet_broadcom_tg3->buf[36] = 0xff;
	drivers_net_ethernet_broadcom_tg3->buf[37] = 0xff;
	drivers_net_ethernet_broadcom_tg3->buf[38] = 0xff;
	drivers_net_ethernet_broadcom_tg3->buf[39] = 0xff;
	drivers_net_ethernet_broadcom_tg3->buf[40] = 0xff;
	drivers_net_ethernet_broadcom_tg3->buf[41] = 0xff;
	drivers_net_ethernet_broadcom_tg3->buf[42] = 0xff;
	drivers_net_ethernet_broadcom_tg3->buf[43] = 0xff;;

    memory_region_init_io(&drivers_net_ethernet_broadcom_tg3->mmio[0], OBJECT(drivers_net_ethernet_broadcom_tg3), &drivers_net_ethernet_broadcom_tg3_mmio_ops, drivers_net_ethernet_broadcom_tg3,
                    "drivers_net_ethernet_broadcom_tg3-mmio0", 16 * MiB);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_net_ethernet_broadcom_tg3->mmio[0]);
    memory_region_init_io(&drivers_net_ethernet_broadcom_tg3->mmio[1], OBJECT(drivers_net_ethernet_broadcom_tg3), &drivers_net_ethernet_broadcom_tg3_mmio_ops, drivers_net_ethernet_broadcom_tg3,
                    "drivers_net_ethernet_broadcom_tg3-mmio1", 16 * MiB);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_net_ethernet_broadcom_tg3->mmio[1]);
    memory_region_init_io(&drivers_net_ethernet_broadcom_tg3->mmio[2], OBJECT(drivers_net_ethernet_broadcom_tg3), &drivers_net_ethernet_broadcom_tg3_mmio_ops, drivers_net_ethernet_broadcom_tg3,
                    "drivers_net_ethernet_broadcom_tg3-mmio2", 16 * MiB);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_net_ethernet_broadcom_tg3->mmio[2]);
    memory_region_init_io(&drivers_net_ethernet_broadcom_tg3->mmio[3], OBJECT(drivers_net_ethernet_broadcom_tg3), &drivers_net_ethernet_broadcom_tg3_mmio_ops, drivers_net_ethernet_broadcom_tg3,
                    "drivers_net_ethernet_broadcom_tg3-mmio3", 16 * MiB);
    pci_register_bar(pdev, 3, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_net_ethernet_broadcom_tg3->mmio[3]);
    memory_region_init_io(&drivers_net_ethernet_broadcom_tg3->mmio[4], OBJECT(drivers_net_ethernet_broadcom_tg3), &drivers_net_ethernet_broadcom_tg3_mmio_ops, drivers_net_ethernet_broadcom_tg3,
                    "drivers_net_ethernet_broadcom_tg3-mmio4", 16 * MiB);
    pci_register_bar(pdev, 4, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_net_ethernet_broadcom_tg3->mmio[4]);
    memory_region_init_io(&drivers_net_ethernet_broadcom_tg3->mmio[5], OBJECT(drivers_net_ethernet_broadcom_tg3), &drivers_net_ethernet_broadcom_tg3_mmio_ops, drivers_net_ethernet_broadcom_tg3,
                    "drivers_net_ethernet_broadcom_tg3-mmio5", 16 * MiB);
    pci_register_bar(pdev, 5, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_net_ethernet_broadcom_tg3->mmio[5]);
    memory_region_init_io(&drivers_net_ethernet_broadcom_tg3->mmio[6], OBJECT(drivers_net_ethernet_broadcom_tg3), &drivers_net_ethernet_broadcom_tg3_mmio_ops, drivers_net_ethernet_broadcom_tg3,
                    "drivers_net_ethernet_broadcom_tg3-mmio5", 16 * MiB);
    pci_register_bar(pdev, 6, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_net_ethernet_broadcom_tg3->mmio[6]);
}

static void pci_drivers_net_ethernet_broadcom_tg3_uninit(PCIDevice *pdev)
{
    msi_uninit(pdev);
}

static void drivers_net_ethernet_broadcom_tg3_instance_init(Object *obj)
{
	return;
}

static void drivers_net_ethernet_broadcom_tg3_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(class);

    k->realize = pci_drivers_net_ethernet_broadcom_tg3_realize;
    k->exit = pci_drivers_net_ethernet_broadcom_tg3_uninit;
    k->vendor_id = 0x14e4;
    k->device_id = 0x1644;
    k->revision = 0;
    k->subsystem_vendor_id = 0xffffffff;
    k->subsystem_id = 0xffffffff;
    k->class_id = 16777215;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void pci_drivers_net_ethernet_broadcom_tg3_register_types(void)
{
    static InterfaceInfo interfaces[] = {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    };
    static const TypeInfo drivers_net_ethernet_broadcom_tg3_info = {
        .name          = TYPE_PCI_DRIVERS_NET_ETHERNET_BROADCOM_TG3_DEVICE,
        .parent        = TYPE_PCI_DEVICE,
        .instance_size = sizeof(Drivers_net_ethernet_broadcom_tg3State),
        .instance_init = drivers_net_ethernet_broadcom_tg3_instance_init,
        .class_init    = drivers_net_ethernet_broadcom_tg3_class_init,
        .interfaces = interfaces,
    };

    type_register_static(&drivers_net_ethernet_broadcom_tg3_info);
}
type_init(pci_drivers_net_ethernet_broadcom_tg3_register_types)
