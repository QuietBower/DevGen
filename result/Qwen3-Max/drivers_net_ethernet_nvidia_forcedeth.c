#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/pci/pci.h"
#include "hw/hw.h"
#include "hw/pci/msi.h"
#include "qemu/timer.h"
#include "qemu/main-loop.h" /* iothread mutex */
#include "qemu/module.h"
#include "qapi/visitor.h"

#define DRIVERS_NET_ETHERNET_NVIDIA_FORCEDETH_BUF_LEN 1024

extern uint64_t target_value[DRIVERS_NET_ETHERNET_NVIDIA_FORCEDETH_BUF_LEN];
extern bool target_value_reset;

typedef struct {
    PCIDevice pdev;
    MemoryRegion mmio[7];

    uint32_t len;
    uint64_t buf[DRIVERS_NET_ETHERNET_NVIDIA_FORCEDETH_BUF_LEN];
    int is_probe_time;
} Drivers_net_ethernet_nvidia_forcedethState;

#define TYPE_PCI_DRIVERS_NET_ETHERNET_NVIDIA_FORCEDETH_DEVICE "drivers_net_ethernet_nvidia_forcedeth"
#define DRIVERS_NET_ETHERNET_NVIDIA_FORCEDETH(obj)        OBJECT_CHECK(Drivers_net_ethernet_nvidia_forcedethState, obj, TYPE_PCI_DRIVERS_NET_ETHERNET_NVIDIA_FORCEDETH_DEVICE)

static uint64_t drivers_net_ethernet_nvidia_forcedeth_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    Drivers_net_ethernet_nvidia_forcedethState *drivers_net_ethernet_nvidia_forcedeth = opaque;
    int i;

    if (target_value_reset) {
        drivers_net_ethernet_nvidia_forcedeth->len = 0;
        target_value_reset = false;
    }

    if (!drivers_net_ethernet_nvidia_forcedeth->is_probe_time) {
        for (i = 0; i < DRIVERS_NET_ETHERNET_NVIDIA_FORCEDETH_BUF_LEN; ++i) {
            drivers_net_ethernet_nvidia_forcedeth->buf[i] = target_value[i];
        }
    } else {
        drivers_net_ethernet_nvidia_forcedeth->is_probe_time--;
    }

    return drivers_net_ethernet_nvidia_forcedeth->buf[(drivers_net_ethernet_nvidia_forcedeth->len++) % DRIVERS_NET_ETHERNET_NVIDIA_FORCEDETH_BUF_LEN];
}

static void drivers_net_ethernet_nvidia_forcedeth_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                unsigned size)
{
	return;
}

static const MemoryRegionOps drivers_net_ethernet_nvidia_forcedeth_mmio_ops = {
    .read = drivers_net_ethernet_nvidia_forcedeth_mmio_read,
    .write = drivers_net_ethernet_nvidia_forcedeth_mmio_write,
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

static void pci_drivers_net_ethernet_nvidia_forcedeth_realize(PCIDevice *pdev, Error **errp)
{
    Drivers_net_ethernet_nvidia_forcedethState *drivers_net_ethernet_nvidia_forcedeth = DRIVERS_NET_ETHERNET_NVIDIA_FORCEDETH(pdev);
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

    drivers_net_ethernet_nvidia_forcedeth->len = 0;
    drivers_net_ethernet_nvidia_forcedeth->is_probe_time = 116;
	drivers_net_ethernet_nvidia_forcedeth->buf[0] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[1] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[2] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[3] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[4] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[5] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[6] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[7] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[8] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[9] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[10] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[11] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[12] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[13] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[14] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[15] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[16] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[17] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[18] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[19] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[20] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[21] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[22] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[23] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[24] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[25] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[26] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[27] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[28] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[29] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[30] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[31] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[32] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[33] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[34] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[35] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[36] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[37] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[38] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[39] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[40] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[41] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[42] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[43] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[44] = 0x0;
	drivers_net_ethernet_nvidia_forcedeth->buf[45] = 0x0;
	drivers_net_ethernet_nvidia_forcedeth->buf[46] = 0x0;
	drivers_net_ethernet_nvidia_forcedeth->buf[47] = 0x0;
	drivers_net_ethernet_nvidia_forcedeth->buf[48] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[49] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[50] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[51] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[52] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[53] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[54] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[55] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[56] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[57] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[58] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[59] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[60] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[61] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[62] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[63] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[64] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[65] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[66] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[67] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[68] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[69] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[70] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[71] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[72] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[73] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[74] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[75] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[76] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[77] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[78] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[79] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[80] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[81] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[82] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[83] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[84] = 0x0;
	drivers_net_ethernet_nvidia_forcedeth->buf[85] = 0x0;
	drivers_net_ethernet_nvidia_forcedeth->buf[86] = 0x0;
	drivers_net_ethernet_nvidia_forcedeth->buf[87] = 0x0;
	drivers_net_ethernet_nvidia_forcedeth->buf[88] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[89] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[90] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[91] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[92] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[93] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[94] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[95] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[96] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[97] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[98] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[99] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[100] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[101] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[102] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[103] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[104] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[105] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[106] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[107] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[108] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[109] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[110] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[111] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[112] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[113] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[114] = 0xff;
	drivers_net_ethernet_nvidia_forcedeth->buf[115] = 0xff;;

    memory_region_init_io(&drivers_net_ethernet_nvidia_forcedeth->mmio[0], OBJECT(drivers_net_ethernet_nvidia_forcedeth), &drivers_net_ethernet_nvidia_forcedeth_mmio_ops, drivers_net_ethernet_nvidia_forcedeth,
                    "drivers_net_ethernet_nvidia_forcedeth-mmio0", 16 * MiB);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_net_ethernet_nvidia_forcedeth->mmio[0]);
    memory_region_init_io(&drivers_net_ethernet_nvidia_forcedeth->mmio[1], OBJECT(drivers_net_ethernet_nvidia_forcedeth), &drivers_net_ethernet_nvidia_forcedeth_mmio_ops, drivers_net_ethernet_nvidia_forcedeth,
                    "drivers_net_ethernet_nvidia_forcedeth-mmio1", 16 * MiB);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_net_ethernet_nvidia_forcedeth->mmio[1]);
    memory_region_init_io(&drivers_net_ethernet_nvidia_forcedeth->mmio[2], OBJECT(drivers_net_ethernet_nvidia_forcedeth), &drivers_net_ethernet_nvidia_forcedeth_mmio_ops, drivers_net_ethernet_nvidia_forcedeth,
                    "drivers_net_ethernet_nvidia_forcedeth-mmio2", 16 * MiB);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_net_ethernet_nvidia_forcedeth->mmio[2]);
    memory_region_init_io(&drivers_net_ethernet_nvidia_forcedeth->mmio[3], OBJECT(drivers_net_ethernet_nvidia_forcedeth), &drivers_net_ethernet_nvidia_forcedeth_mmio_ops, drivers_net_ethernet_nvidia_forcedeth,
                    "drivers_net_ethernet_nvidia_forcedeth-mmio3", 16 * MiB);
    pci_register_bar(pdev, 3, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_net_ethernet_nvidia_forcedeth->mmio[3]);
    memory_region_init_io(&drivers_net_ethernet_nvidia_forcedeth->mmio[4], OBJECT(drivers_net_ethernet_nvidia_forcedeth), &drivers_net_ethernet_nvidia_forcedeth_mmio_ops, drivers_net_ethernet_nvidia_forcedeth,
                    "drivers_net_ethernet_nvidia_forcedeth-mmio4", 16 * MiB);
    pci_register_bar(pdev, 4, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_net_ethernet_nvidia_forcedeth->mmio[4]);
    memory_region_init_io(&drivers_net_ethernet_nvidia_forcedeth->mmio[5], OBJECT(drivers_net_ethernet_nvidia_forcedeth), &drivers_net_ethernet_nvidia_forcedeth_mmio_ops, drivers_net_ethernet_nvidia_forcedeth,
                    "drivers_net_ethernet_nvidia_forcedeth-mmio5", 16 * MiB);
    pci_register_bar(pdev, 5, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_net_ethernet_nvidia_forcedeth->mmio[5]);
    memory_region_init_io(&drivers_net_ethernet_nvidia_forcedeth->mmio[6], OBJECT(drivers_net_ethernet_nvidia_forcedeth), &drivers_net_ethernet_nvidia_forcedeth_mmio_ops, drivers_net_ethernet_nvidia_forcedeth,
                    "drivers_net_ethernet_nvidia_forcedeth-mmio5", 16 * MiB);
    pci_register_bar(pdev, 6, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_net_ethernet_nvidia_forcedeth->mmio[6]);
}

static void pci_drivers_net_ethernet_nvidia_forcedeth_uninit(PCIDevice *pdev)
{
    msi_uninit(pdev);
}

static void drivers_net_ethernet_nvidia_forcedeth_instance_init(Object *obj)
{
	return;
}

static void drivers_net_ethernet_nvidia_forcedeth_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(class);

    k->realize = pci_drivers_net_ethernet_nvidia_forcedeth_realize;
    k->exit = pci_drivers_net_ethernet_nvidia_forcedeth_uninit;
    k->vendor_id = 0x10de;
    k->device_id = 0x1c3;
    k->revision = 0;
    k->subsystem_vendor_id = 0xffffffff;
    k->subsystem_id = 0xffffffff;
    k->class_id = 16777215;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void pci_drivers_net_ethernet_nvidia_forcedeth_register_types(void)
{
    static InterfaceInfo interfaces[] = {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    };
    static const TypeInfo drivers_net_ethernet_nvidia_forcedeth_info = {
        .name          = TYPE_PCI_DRIVERS_NET_ETHERNET_NVIDIA_FORCEDETH_DEVICE,
        .parent        = TYPE_PCI_DEVICE,
        .instance_size = sizeof(Drivers_net_ethernet_nvidia_forcedethState),
        .instance_init = drivers_net_ethernet_nvidia_forcedeth_instance_init,
        .class_init    = drivers_net_ethernet_nvidia_forcedeth_class_init,
        .interfaces = interfaces,
    };

    type_register_static(&drivers_net_ethernet_nvidia_forcedeth_info);
}
type_init(pci_drivers_net_ethernet_nvidia_forcedeth_register_types)
