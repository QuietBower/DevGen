#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/pci/pci.h"
#include "hw/hw.h"
#include "hw/pci/msi.h"
#include "qemu/timer.h"
#include "qemu/main-loop.h" /* iothread mutex */
#include "qemu/module.h"
#include "qapi/visitor.h"

#define DRIVERS_NET_ETHERNET_BROADCOM_BNX2_BUF_LEN 1024

extern uint64_t target_value[DRIVERS_NET_ETHERNET_BROADCOM_BNX2_BUF_LEN];
extern bool target_value_reset;

typedef struct {
    PCIDevice pdev;
    MemoryRegion mmio[7];

    uint32_t len;
    uint64_t buf[DRIVERS_NET_ETHERNET_BROADCOM_BNX2_BUF_LEN];
    int is_probe_time;
} Drivers_net_ethernet_broadcom_bnx2State;

#define TYPE_PCI_DRIVERS_NET_ETHERNET_BROADCOM_BNX2_DEVICE "drivers_net_ethernet_broadcom_bnx2"
#define DRIVERS_NET_ETHERNET_BROADCOM_BNX2(obj)        OBJECT_CHECK(Drivers_net_ethernet_broadcom_bnx2State, obj, TYPE_PCI_DRIVERS_NET_ETHERNET_BROADCOM_BNX2_DEVICE)

static uint64_t drivers_net_ethernet_broadcom_bnx2_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    Drivers_net_ethernet_broadcom_bnx2State *drivers_net_ethernet_broadcom_bnx2 = opaque;
    int i;

    if (target_value_reset) {
        drivers_net_ethernet_broadcom_bnx2->len = 0;
        target_value_reset = false;
    }

    if (!drivers_net_ethernet_broadcom_bnx2->is_probe_time) {
        for (i = 0; i < DRIVERS_NET_ETHERNET_BROADCOM_BNX2_BUF_LEN; ++i) {
            drivers_net_ethernet_broadcom_bnx2->buf[i] = target_value[i];
        }
    } else {
        drivers_net_ethernet_broadcom_bnx2->is_probe_time--;
    }

    return drivers_net_ethernet_broadcom_bnx2->buf[(drivers_net_ethernet_broadcom_bnx2->len++) % DRIVERS_NET_ETHERNET_BROADCOM_BNX2_BUF_LEN];
}

static void drivers_net_ethernet_broadcom_bnx2_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                unsigned size)
{
	return;
}

static const MemoryRegionOps drivers_net_ethernet_broadcom_bnx2_mmio_ops = {
    .read = drivers_net_ethernet_broadcom_bnx2_mmio_read,
    .write = drivers_net_ethernet_broadcom_bnx2_mmio_write,
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

static void pci_drivers_net_ethernet_broadcom_bnx2_realize(PCIDevice *pdev, Error **errp)
{
    Drivers_net_ethernet_broadcom_bnx2State *drivers_net_ethernet_broadcom_bnx2 = DRIVERS_NET_ETHERNET_BROADCOM_BNX2(pdev);
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

    drivers_net_ethernet_broadcom_bnx2->len = 0;
    drivers_net_ethernet_broadcom_bnx2->is_probe_time = 72;
	drivers_net_ethernet_broadcom_bnx2->buf[0] = 0xff;
	drivers_net_ethernet_broadcom_bnx2->buf[1] = 0xff;
	drivers_net_ethernet_broadcom_bnx2->buf[2] = 0xff;
	drivers_net_ethernet_broadcom_bnx2->buf[3] = 0xff;
	drivers_net_ethernet_broadcom_bnx2->buf[4] = 0x0;
	drivers_net_ethernet_broadcom_bnx2->buf[5] = 0x0;
	drivers_net_ethernet_broadcom_bnx2->buf[6] = 0x0;
	drivers_net_ethernet_broadcom_bnx2->buf[7] = 0x0;
	drivers_net_ethernet_broadcom_bnx2->buf[8] = 0x0;
	drivers_net_ethernet_broadcom_bnx2->buf[9] = 0x0;
	drivers_net_ethernet_broadcom_bnx2->buf[10] = 0x0;
	drivers_net_ethernet_broadcom_bnx2->buf[11] = 0x7;
	drivers_net_ethernet_broadcom_bnx2->buf[12] = 0x44;
	drivers_net_ethernet_broadcom_bnx2->buf[13] = 0x56;
	drivers_net_ethernet_broadcom_bnx2->buf[14] = 0x49;
	drivers_net_ethernet_broadcom_bnx2->buf[15] = 0x0;
	drivers_net_ethernet_broadcom_bnx2->buf[16] = 0x0;
	drivers_net_ethernet_broadcom_bnx2->buf[17] = 0x0;
	drivers_net_ethernet_broadcom_bnx2->buf[18] = 0x0;
	drivers_net_ethernet_broadcom_bnx2->buf[19] = 0x0;
	drivers_net_ethernet_broadcom_bnx2->buf[20] = 0xff;
	drivers_net_ethernet_broadcom_bnx2->buf[21] = 0xff;
	drivers_net_ethernet_broadcom_bnx2->buf[22] = 0xff;
	drivers_net_ethernet_broadcom_bnx2->buf[23] = 0xff;
	drivers_net_ethernet_broadcom_bnx2->buf[24] = 0xff;
	drivers_net_ethernet_broadcom_bnx2->buf[25] = 0xff;
	drivers_net_ethernet_broadcom_bnx2->buf[26] = 0xff;
	drivers_net_ethernet_broadcom_bnx2->buf[27] = 0xff;
	drivers_net_ethernet_broadcom_bnx2->buf[28] = 0xff;
	drivers_net_ethernet_broadcom_bnx2->buf[29] = 0xff;
	drivers_net_ethernet_broadcom_bnx2->buf[30] = 0xff;
	drivers_net_ethernet_broadcom_bnx2->buf[31] = 0xff;
	drivers_net_ethernet_broadcom_bnx2->buf[32] = 0xff;
	drivers_net_ethernet_broadcom_bnx2->buf[33] = 0xff;
	drivers_net_ethernet_broadcom_bnx2->buf[34] = 0xff;
	drivers_net_ethernet_broadcom_bnx2->buf[35] = 0xff;
	drivers_net_ethernet_broadcom_bnx2->buf[36] = 0xff;
	drivers_net_ethernet_broadcom_bnx2->buf[37] = 0xff;
	drivers_net_ethernet_broadcom_bnx2->buf[38] = 0xff;
	drivers_net_ethernet_broadcom_bnx2->buf[39] = 0xff;
	drivers_net_ethernet_broadcom_bnx2->buf[40] = 0xff;
	drivers_net_ethernet_broadcom_bnx2->buf[41] = 0xff;
	drivers_net_ethernet_broadcom_bnx2->buf[42] = 0xff;
	drivers_net_ethernet_broadcom_bnx2->buf[43] = 0xff;
	drivers_net_ethernet_broadcom_bnx2->buf[44] = 0xff;
	drivers_net_ethernet_broadcom_bnx2->buf[45] = 0xff;
	drivers_net_ethernet_broadcom_bnx2->buf[46] = 0xff;
	drivers_net_ethernet_broadcom_bnx2->buf[47] = 0xff;
	drivers_net_ethernet_broadcom_bnx2->buf[48] = 0xff;
	drivers_net_ethernet_broadcom_bnx2->buf[49] = 0xff;
	drivers_net_ethernet_broadcom_bnx2->buf[50] = 0xff;
	drivers_net_ethernet_broadcom_bnx2->buf[51] = 0xff;
	drivers_net_ethernet_broadcom_bnx2->buf[52] = 0xff;
	drivers_net_ethernet_broadcom_bnx2->buf[53] = 0xff;
	drivers_net_ethernet_broadcom_bnx2->buf[54] = 0xff;
	drivers_net_ethernet_broadcom_bnx2->buf[55] = 0xff;
	drivers_net_ethernet_broadcom_bnx2->buf[56] = 0xff;
	drivers_net_ethernet_broadcom_bnx2->buf[57] = 0xff;
	drivers_net_ethernet_broadcom_bnx2->buf[58] = 0xff;
	drivers_net_ethernet_broadcom_bnx2->buf[59] = 0xff;
	drivers_net_ethernet_broadcom_bnx2->buf[60] = 0xff;
	drivers_net_ethernet_broadcom_bnx2->buf[61] = 0xff;
	drivers_net_ethernet_broadcom_bnx2->buf[62] = 0xff;
	drivers_net_ethernet_broadcom_bnx2->buf[63] = 0xff;
	drivers_net_ethernet_broadcom_bnx2->buf[64] = 0xff;
	drivers_net_ethernet_broadcom_bnx2->buf[65] = 0xff;
	drivers_net_ethernet_broadcom_bnx2->buf[66] = 0xff;
	drivers_net_ethernet_broadcom_bnx2->buf[67] = 0xff;
	drivers_net_ethernet_broadcom_bnx2->buf[68] = 0xff;
	drivers_net_ethernet_broadcom_bnx2->buf[69] = 0xff;
	drivers_net_ethernet_broadcom_bnx2->buf[70] = 0xff;
	drivers_net_ethernet_broadcom_bnx2->buf[71] = 0xff;;

    memory_region_init_io(&drivers_net_ethernet_broadcom_bnx2->mmio[0], OBJECT(drivers_net_ethernet_broadcom_bnx2), &drivers_net_ethernet_broadcom_bnx2_mmio_ops, drivers_net_ethernet_broadcom_bnx2,
                    "drivers_net_ethernet_broadcom_bnx2-mmio0", 16 * MiB);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_net_ethernet_broadcom_bnx2->mmio[0]);
    memory_region_init_io(&drivers_net_ethernet_broadcom_bnx2->mmio[1], OBJECT(drivers_net_ethernet_broadcom_bnx2), &drivers_net_ethernet_broadcom_bnx2_mmio_ops, drivers_net_ethernet_broadcom_bnx2,
                    "drivers_net_ethernet_broadcom_bnx2-mmio1", 16 * MiB);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_net_ethernet_broadcom_bnx2->mmio[1]);
    memory_region_init_io(&drivers_net_ethernet_broadcom_bnx2->mmio[2], OBJECT(drivers_net_ethernet_broadcom_bnx2), &drivers_net_ethernet_broadcom_bnx2_mmio_ops, drivers_net_ethernet_broadcom_bnx2,
                    "drivers_net_ethernet_broadcom_bnx2-mmio2", 16 * MiB);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_net_ethernet_broadcom_bnx2->mmio[2]);
    memory_region_init_io(&drivers_net_ethernet_broadcom_bnx2->mmio[3], OBJECT(drivers_net_ethernet_broadcom_bnx2), &drivers_net_ethernet_broadcom_bnx2_mmio_ops, drivers_net_ethernet_broadcom_bnx2,
                    "drivers_net_ethernet_broadcom_bnx2-mmio3", 16 * MiB);
    pci_register_bar(pdev, 3, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_net_ethernet_broadcom_bnx2->mmio[3]);
    memory_region_init_io(&drivers_net_ethernet_broadcom_bnx2->mmio[4], OBJECT(drivers_net_ethernet_broadcom_bnx2), &drivers_net_ethernet_broadcom_bnx2_mmio_ops, drivers_net_ethernet_broadcom_bnx2,
                    "drivers_net_ethernet_broadcom_bnx2-mmio4", 16 * MiB);
    pci_register_bar(pdev, 4, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_net_ethernet_broadcom_bnx2->mmio[4]);
    memory_region_init_io(&drivers_net_ethernet_broadcom_bnx2->mmio[5], OBJECT(drivers_net_ethernet_broadcom_bnx2), &drivers_net_ethernet_broadcom_bnx2_mmio_ops, drivers_net_ethernet_broadcom_bnx2,
                    "drivers_net_ethernet_broadcom_bnx2-mmio5", 16 * MiB);
    pci_register_bar(pdev, 5, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_net_ethernet_broadcom_bnx2->mmio[5]);
    memory_region_init_io(&drivers_net_ethernet_broadcom_bnx2->mmio[6], OBJECT(drivers_net_ethernet_broadcom_bnx2), &drivers_net_ethernet_broadcom_bnx2_mmio_ops, drivers_net_ethernet_broadcom_bnx2,
                    "drivers_net_ethernet_broadcom_bnx2-mmio5", 16 * MiB);
    pci_register_bar(pdev, 6, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_net_ethernet_broadcom_bnx2->mmio[6]);
}

static void pci_drivers_net_ethernet_broadcom_bnx2_uninit(PCIDevice *pdev)
{
    msi_uninit(pdev);
}

static void drivers_net_ethernet_broadcom_bnx2_instance_init(Object *obj)
{
	return;
}

static void drivers_net_ethernet_broadcom_bnx2_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(class);

    k->realize = pci_drivers_net_ethernet_broadcom_bnx2_realize;
    k->exit = pci_drivers_net_ethernet_broadcom_bnx2_uninit;
    k->vendor_id = 0x14e4;
    k->device_id = 0x164a;
    k->revision = 0;
    k->subsystem_vendor_id = 0x103c;
    k->subsystem_id = 0x3101;
    k->class_id = 16777215;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void pci_drivers_net_ethernet_broadcom_bnx2_register_types(void)
{
    static InterfaceInfo interfaces[] = {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    };
    static const TypeInfo drivers_net_ethernet_broadcom_bnx2_info = {
        .name          = TYPE_PCI_DRIVERS_NET_ETHERNET_BROADCOM_BNX2_DEVICE,
        .parent        = TYPE_PCI_DEVICE,
        .instance_size = sizeof(Drivers_net_ethernet_broadcom_bnx2State),
        .instance_init = drivers_net_ethernet_broadcom_bnx2_instance_init,
        .class_init    = drivers_net_ethernet_broadcom_bnx2_class_init,
        .interfaces = interfaces,
    };

    type_register_static(&drivers_net_ethernet_broadcom_bnx2_info);
}
type_init(pci_drivers_net_ethernet_broadcom_bnx2_register_types)
