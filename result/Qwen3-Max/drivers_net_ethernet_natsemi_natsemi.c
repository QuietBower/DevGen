#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/pci/pci.h"
#include "hw/hw.h"
#include "hw/pci/msi.h"
#include "qemu/timer.h"
#include "qemu/main-loop.h" /* iothread mutex */
#include "qemu/module.h"
#include "qapi/visitor.h"

#define DRIVERS_NET_ETHERNET_NATSEMI_NATSEMI_BUF_LEN 1024

extern uint64_t target_value[DRIVERS_NET_ETHERNET_NATSEMI_NATSEMI_BUF_LEN];
extern bool target_value_reset;

typedef struct {
    PCIDevice pdev;
    MemoryRegion mmio[7];

    uint32_t len;
    uint64_t buf[DRIVERS_NET_ETHERNET_NATSEMI_NATSEMI_BUF_LEN];
    int is_probe_time;
} Drivers_net_ethernet_natsemi_natsemiState;

#define TYPE_PCI_DRIVERS_NET_ETHERNET_NATSEMI_NATSEMI_DEVICE "drivers_net_ethernet_natsemi_natsemi"
#define DRIVERS_NET_ETHERNET_NATSEMI_NATSEMI(obj)        OBJECT_CHECK(Drivers_net_ethernet_natsemi_natsemiState, obj, TYPE_PCI_DRIVERS_NET_ETHERNET_NATSEMI_NATSEMI_DEVICE)

static uint64_t drivers_net_ethernet_natsemi_natsemi_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    Drivers_net_ethernet_natsemi_natsemiState *drivers_net_ethernet_natsemi_natsemi = opaque;
    int i;

    if (target_value_reset) {
        drivers_net_ethernet_natsemi_natsemi->len = 0;
        target_value_reset = false;
    }

    if (!drivers_net_ethernet_natsemi_natsemi->is_probe_time) {
        for (i = 0; i < DRIVERS_NET_ETHERNET_NATSEMI_NATSEMI_BUF_LEN; ++i) {
            drivers_net_ethernet_natsemi_natsemi->buf[i] = target_value[i];
        }
    } else {
        drivers_net_ethernet_natsemi_natsemi->is_probe_time--;
    }

    return drivers_net_ethernet_natsemi_natsemi->buf[(drivers_net_ethernet_natsemi_natsemi->len++) % DRIVERS_NET_ETHERNET_NATSEMI_NATSEMI_BUF_LEN];
}

static void drivers_net_ethernet_natsemi_natsemi_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                unsigned size)
{
	return;
}

static const MemoryRegionOps drivers_net_ethernet_natsemi_natsemi_mmio_ops = {
    .read = drivers_net_ethernet_natsemi_natsemi_mmio_read,
    .write = drivers_net_ethernet_natsemi_natsemi_mmio_write,
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

static void pci_drivers_net_ethernet_natsemi_natsemi_realize(PCIDevice *pdev, Error **errp)
{
    Drivers_net_ethernet_natsemi_natsemiState *drivers_net_ethernet_natsemi_natsemi = DRIVERS_NET_ETHERNET_NATSEMI_NATSEMI(pdev);
    uint8_t *pci_conf = pdev->config;
	int pos;

    pci_config_set_interrupt_pin(pci_conf, 1);
	pci_set_long(&pci_conf[68], 4294967295);
	

    if (msi_init(pdev, 0, 1, true, false, errp)) {
        return;
    }
	
	pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
	pcie_endpoint_cap_init(pdev, 0);

	pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
	pdev->exp.pm_cap = pos;
	pci_set_word(pdev->config + pos + PCI_PM_PMC, 0x3);

    drivers_net_ethernet_natsemi_natsemi->len = 0;
    drivers_net_ethernet_natsemi_natsemi->is_probe_time = 156;
	drivers_net_ethernet_natsemi_natsemi->buf[0] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[1] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[2] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[3] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[4] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[5] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[6] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[7] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[8] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[9] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[10] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[11] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[12] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[13] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[14] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[15] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[16] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[17] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[18] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[19] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[20] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[21] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[22] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[23] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[24] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[25] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[26] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[27] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[28] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[29] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[30] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[31] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[32] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[33] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[34] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[35] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[36] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[37] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[38] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[39] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[40] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[41] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[42] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[43] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[44] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[45] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[46] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[47] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[48] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[49] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[50] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[51] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[52] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[53] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[54] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[55] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[56] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[57] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[58] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[59] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[60] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[61] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[62] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[63] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[64] = 0x0;
	drivers_net_ethernet_natsemi_natsemi->buf[65] = 0x0;
	drivers_net_ethernet_natsemi_natsemi->buf[66] = 0x0;
	drivers_net_ethernet_natsemi_natsemi->buf[67] = 0x0;
	drivers_net_ethernet_natsemi_natsemi->buf[68] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[69] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[70] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[71] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[72] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[73] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[74] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[75] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[76] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[77] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[78] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[79] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[80] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[81] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[82] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[83] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[84] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[85] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[86] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[87] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[88] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[89] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[90] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[91] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[92] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[93] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[94] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[95] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[96] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[97] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[98] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[99] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[100] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[101] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[102] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[103] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[104] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[105] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[106] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[107] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[108] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[109] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[110] = 0x0;
	drivers_net_ethernet_natsemi_natsemi->buf[111] = 0x0;
	drivers_net_ethernet_natsemi_natsemi->buf[112] = 0x0;
	drivers_net_ethernet_natsemi_natsemi->buf[113] = 0x0;
	drivers_net_ethernet_natsemi_natsemi->buf[114] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[115] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[116] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[117] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[118] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[119] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[120] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[121] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[122] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[123] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[124] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[125] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[126] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[127] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[128] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[129] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[130] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[131] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[132] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[133] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[134] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[135] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[136] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[137] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[138] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[139] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[140] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[141] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[142] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[143] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[144] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[145] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[146] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[147] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[148] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[149] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[150] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[151] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[152] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[153] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[154] = 0xff;
	drivers_net_ethernet_natsemi_natsemi->buf[155] = 0xff;;

    memory_region_init_io(&drivers_net_ethernet_natsemi_natsemi->mmio[0], OBJECT(drivers_net_ethernet_natsemi_natsemi), &drivers_net_ethernet_natsemi_natsemi_mmio_ops, drivers_net_ethernet_natsemi_natsemi,
                    "drivers_net_ethernet_natsemi_natsemi-mmio0", 16 * MiB);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_net_ethernet_natsemi_natsemi->mmio[0]);
    memory_region_init_io(&drivers_net_ethernet_natsemi_natsemi->mmio[1], OBJECT(drivers_net_ethernet_natsemi_natsemi), &drivers_net_ethernet_natsemi_natsemi_mmio_ops, drivers_net_ethernet_natsemi_natsemi,
                    "drivers_net_ethernet_natsemi_natsemi-mmio1", 16 * MiB);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_net_ethernet_natsemi_natsemi->mmio[1]);
    memory_region_init_io(&drivers_net_ethernet_natsemi_natsemi->mmio[2], OBJECT(drivers_net_ethernet_natsemi_natsemi), &drivers_net_ethernet_natsemi_natsemi_mmio_ops, drivers_net_ethernet_natsemi_natsemi,
                    "drivers_net_ethernet_natsemi_natsemi-mmio2", 16 * MiB);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_net_ethernet_natsemi_natsemi->mmio[2]);
    memory_region_init_io(&drivers_net_ethernet_natsemi_natsemi->mmio[3], OBJECT(drivers_net_ethernet_natsemi_natsemi), &drivers_net_ethernet_natsemi_natsemi_mmio_ops, drivers_net_ethernet_natsemi_natsemi,
                    "drivers_net_ethernet_natsemi_natsemi-mmio3", 16 * MiB);
    pci_register_bar(pdev, 3, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_net_ethernet_natsemi_natsemi->mmio[3]);
    memory_region_init_io(&drivers_net_ethernet_natsemi_natsemi->mmio[4], OBJECT(drivers_net_ethernet_natsemi_natsemi), &drivers_net_ethernet_natsemi_natsemi_mmio_ops, drivers_net_ethernet_natsemi_natsemi,
                    "drivers_net_ethernet_natsemi_natsemi-mmio4", 16 * MiB);
    pci_register_bar(pdev, 4, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_net_ethernet_natsemi_natsemi->mmio[4]);
    memory_region_init_io(&drivers_net_ethernet_natsemi_natsemi->mmio[5], OBJECT(drivers_net_ethernet_natsemi_natsemi), &drivers_net_ethernet_natsemi_natsemi_mmio_ops, drivers_net_ethernet_natsemi_natsemi,
                    "drivers_net_ethernet_natsemi_natsemi-mmio5", 16 * MiB);
    pci_register_bar(pdev, 5, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_net_ethernet_natsemi_natsemi->mmio[5]);
    memory_region_init_io(&drivers_net_ethernet_natsemi_natsemi->mmio[6], OBJECT(drivers_net_ethernet_natsemi_natsemi), &drivers_net_ethernet_natsemi_natsemi_mmio_ops, drivers_net_ethernet_natsemi_natsemi,
                    "drivers_net_ethernet_natsemi_natsemi-mmio5", 16 * MiB);
    pci_register_bar(pdev, 6, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_net_ethernet_natsemi_natsemi->mmio[6]);
}

static void pci_drivers_net_ethernet_natsemi_natsemi_uninit(PCIDevice *pdev)
{
    msi_uninit(pdev);
}

static void drivers_net_ethernet_natsemi_natsemi_instance_init(Object *obj)
{
	return;
}

static void drivers_net_ethernet_natsemi_natsemi_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(class);

    k->realize = pci_drivers_net_ethernet_natsemi_natsemi_realize;
    k->exit = pci_drivers_net_ethernet_natsemi_natsemi_uninit;
    k->vendor_id = 0x100b;
    k->device_id = 0x20;
    k->revision = 0;
    k->subsystem_vendor_id = 0x12d9;
    k->subsystem_id = 0xc;
    k->class_id = 16777215;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void pci_drivers_net_ethernet_natsemi_natsemi_register_types(void)
{
    static InterfaceInfo interfaces[] = {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    };
    static const TypeInfo drivers_net_ethernet_natsemi_natsemi_info = {
        .name          = TYPE_PCI_DRIVERS_NET_ETHERNET_NATSEMI_NATSEMI_DEVICE,
        .parent        = TYPE_PCI_DEVICE,
        .instance_size = sizeof(Drivers_net_ethernet_natsemi_natsemiState),
        .instance_init = drivers_net_ethernet_natsemi_natsemi_instance_init,
        .class_init    = drivers_net_ethernet_natsemi_natsemi_class_init,
        .interfaces = interfaces,
    };

    type_register_static(&drivers_net_ethernet_natsemi_natsemi_info);
}
type_init(pci_drivers_net_ethernet_natsemi_natsemi_register_types)
