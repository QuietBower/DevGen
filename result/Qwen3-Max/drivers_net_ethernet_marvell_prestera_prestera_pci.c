#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/pci/pci.h"
#include "hw/hw.h"
#include "hw/pci/msi.h"
#include "qemu/timer.h"
#include "qemu/main-loop.h" /* iothread mutex */
#include "qemu/module.h"
#include "qapi/visitor.h"

#define DRIVERS_NET_ETHERNET_MARVELL_PRESTERA_PRESTERA_PCI_BUF_LEN 1024

extern uint64_t target_value[DRIVERS_NET_ETHERNET_MARVELL_PRESTERA_PRESTERA_PCI_BUF_LEN];
extern bool target_value_reset;

typedef struct {
    PCIDevice pdev;
    MemoryRegion mmio[7];

    uint32_t len;
    uint64_t buf[DRIVERS_NET_ETHERNET_MARVELL_PRESTERA_PRESTERA_PCI_BUF_LEN];
    int is_probe_time;
} Drivers_net_ethernet_marvell_prestera_prestera_pciState;

#define TYPE_PCI_DRIVERS_NET_ETHERNET_MARVELL_PRESTERA_PRESTERA_PCI_DEVICE "drivers_net_ethernet_marvell_prestera_prestera_pci"
#define DRIVERS_NET_ETHERNET_MARVELL_PRESTERA_PRESTERA_PCI(obj)        OBJECT_CHECK(Drivers_net_ethernet_marvell_prestera_prestera_pciState, obj, TYPE_PCI_DRIVERS_NET_ETHERNET_MARVELL_PRESTERA_PRESTERA_PCI_DEVICE)

static uint64_t drivers_net_ethernet_marvell_prestera_prestera_pci_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    Drivers_net_ethernet_marvell_prestera_prestera_pciState *drivers_net_ethernet_marvell_prestera_prestera_pci = opaque;
    int i;

    if (target_value_reset) {
        drivers_net_ethernet_marvell_prestera_prestera_pci->len = 0;
        target_value_reset = false;
    }

    if (!drivers_net_ethernet_marvell_prestera_prestera_pci->is_probe_time) {
        for (i = 0; i < DRIVERS_NET_ETHERNET_MARVELL_PRESTERA_PRESTERA_PCI_BUF_LEN; ++i) {
            drivers_net_ethernet_marvell_prestera_prestera_pci->buf[i] = target_value[i];
        }
    } else {
        drivers_net_ethernet_marvell_prestera_prestera_pci->is_probe_time--;
    }

    return drivers_net_ethernet_marvell_prestera_prestera_pci->buf[(drivers_net_ethernet_marvell_prestera_prestera_pci->len++) % DRIVERS_NET_ETHERNET_MARVELL_PRESTERA_PRESTERA_PCI_BUF_LEN];
}

static void drivers_net_ethernet_marvell_prestera_prestera_pci_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                unsigned size)
{
	return;
}

static const MemoryRegionOps drivers_net_ethernet_marvell_prestera_prestera_pci_mmio_ops = {
    .read = drivers_net_ethernet_marvell_prestera_prestera_pci_mmio_read,
    .write = drivers_net_ethernet_marvell_prestera_prestera_pci_mmio_write,
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

static void pci_drivers_net_ethernet_marvell_prestera_prestera_pci_realize(PCIDevice *pdev, Error **errp)
{
    Drivers_net_ethernet_marvell_prestera_prestera_pciState *drivers_net_ethernet_marvell_prestera_prestera_pci = DRIVERS_NET_ETHERNET_MARVELL_PRESTERA_PRESTERA_PCI(pdev);
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

    drivers_net_ethernet_marvell_prestera_prestera_pci->len = 0;
    drivers_net_ethernet_marvell_prestera_prestera_pci->is_probe_time = 92;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[0] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[1] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[2] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[3] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[4] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[5] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[6] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[7] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[8] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[9] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[10] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[11] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[12] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[13] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[14] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[15] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[16] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[17] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[18] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[19] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[20] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[21] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[22] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[23] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[24] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[25] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[26] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[27] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[28] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[29] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[30] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[31] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[32] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[33] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[34] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[35] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[36] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[37] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[38] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[39] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[40] = 0x0;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[41] = 0x0;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[42] = 0x0;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[43] = 0x4;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[44] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[45] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[46] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[47] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[48] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[49] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[50] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[51] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[52] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[53] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[54] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[55] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[56] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[57] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[58] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[59] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[60] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[61] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[62] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[63] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[64] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[65] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[66] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[67] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[68] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[69] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[70] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[71] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[72] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[73] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[74] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[75] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[76] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[77] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[78] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[79] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[80] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[81] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[82] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[83] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[84] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[85] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[86] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[87] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[88] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[89] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[90] = 0xff;
	drivers_net_ethernet_marvell_prestera_prestera_pci->buf[91] = 0xff;;

    memory_region_init_io(&drivers_net_ethernet_marvell_prestera_prestera_pci->mmio[0], OBJECT(drivers_net_ethernet_marvell_prestera_prestera_pci), &drivers_net_ethernet_marvell_prestera_prestera_pci_mmio_ops, drivers_net_ethernet_marvell_prestera_prestera_pci,
                    "drivers_net_ethernet_marvell_prestera_prestera_pci-mmio0", 16 * MiB);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_net_ethernet_marvell_prestera_prestera_pci->mmio[0]);
    memory_region_init_io(&drivers_net_ethernet_marvell_prestera_prestera_pci->mmio[1], OBJECT(drivers_net_ethernet_marvell_prestera_prestera_pci), &drivers_net_ethernet_marvell_prestera_prestera_pci_mmio_ops, drivers_net_ethernet_marvell_prestera_prestera_pci,
                    "drivers_net_ethernet_marvell_prestera_prestera_pci-mmio1", 16 * MiB);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_net_ethernet_marvell_prestera_prestera_pci->mmio[1]);
    memory_region_init_io(&drivers_net_ethernet_marvell_prestera_prestera_pci->mmio[2], OBJECT(drivers_net_ethernet_marvell_prestera_prestera_pci), &drivers_net_ethernet_marvell_prestera_prestera_pci_mmio_ops, drivers_net_ethernet_marvell_prestera_prestera_pci,
                    "drivers_net_ethernet_marvell_prestera_prestera_pci-mmio2", 16 * MiB);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_net_ethernet_marvell_prestera_prestera_pci->mmio[2]);
    memory_region_init_io(&drivers_net_ethernet_marvell_prestera_prestera_pci->mmio[3], OBJECT(drivers_net_ethernet_marvell_prestera_prestera_pci), &drivers_net_ethernet_marvell_prestera_prestera_pci_mmio_ops, drivers_net_ethernet_marvell_prestera_prestera_pci,
                    "drivers_net_ethernet_marvell_prestera_prestera_pci-mmio3", 16 * MiB);
    pci_register_bar(pdev, 3, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_net_ethernet_marvell_prestera_prestera_pci->mmio[3]);
    memory_region_init_io(&drivers_net_ethernet_marvell_prestera_prestera_pci->mmio[4], OBJECT(drivers_net_ethernet_marvell_prestera_prestera_pci), &drivers_net_ethernet_marvell_prestera_prestera_pci_mmio_ops, drivers_net_ethernet_marvell_prestera_prestera_pci,
                    "drivers_net_ethernet_marvell_prestera_prestera_pci-mmio4", 16 * MiB);
    pci_register_bar(pdev, 4, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_net_ethernet_marvell_prestera_prestera_pci->mmio[4]);
    memory_region_init_io(&drivers_net_ethernet_marvell_prestera_prestera_pci->mmio[5], OBJECT(drivers_net_ethernet_marvell_prestera_prestera_pci), &drivers_net_ethernet_marvell_prestera_prestera_pci_mmio_ops, drivers_net_ethernet_marvell_prestera_prestera_pci,
                    "drivers_net_ethernet_marvell_prestera_prestera_pci-mmio5", 16 * MiB);
    pci_register_bar(pdev, 5, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_net_ethernet_marvell_prestera_prestera_pci->mmio[5]);
    memory_region_init_io(&drivers_net_ethernet_marvell_prestera_prestera_pci->mmio[6], OBJECT(drivers_net_ethernet_marvell_prestera_prestera_pci), &drivers_net_ethernet_marvell_prestera_prestera_pci_mmio_ops, drivers_net_ethernet_marvell_prestera_prestera_pci,
                    "drivers_net_ethernet_marvell_prestera_prestera_pci-mmio5", 16 * MiB);
    pci_register_bar(pdev, 6, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_net_ethernet_marvell_prestera_prestera_pci->mmio[6]);
}

static void pci_drivers_net_ethernet_marvell_prestera_prestera_pci_uninit(PCIDevice *pdev)
{
    msi_uninit(pdev);
}

static void drivers_net_ethernet_marvell_prestera_prestera_pci_instance_init(Object *obj)
{
	return;
}

static void drivers_net_ethernet_marvell_prestera_prestera_pci_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(class);

    k->realize = pci_drivers_net_ethernet_marvell_prestera_prestera_pci_realize;
    k->exit = pci_drivers_net_ethernet_marvell_prestera_prestera_pci_uninit;
    k->vendor_id = 0x11ab;
    k->device_id = 0xc804;
    k->revision = 0;
    k->subsystem_vendor_id = 0xffffffff;
    k->subsystem_id = 0xffffffff;
    k->class_id = 16777215;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void pci_drivers_net_ethernet_marvell_prestera_prestera_pci_register_types(void)
{
    static InterfaceInfo interfaces[] = {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    };
    static const TypeInfo drivers_net_ethernet_marvell_prestera_prestera_pci_info = {
        .name          = TYPE_PCI_DRIVERS_NET_ETHERNET_MARVELL_PRESTERA_PRESTERA_PCI_DEVICE,
        .parent        = TYPE_PCI_DEVICE,
        .instance_size = sizeof(Drivers_net_ethernet_marvell_prestera_prestera_pciState),
        .instance_init = drivers_net_ethernet_marvell_prestera_prestera_pci_instance_init,
        .class_init    = drivers_net_ethernet_marvell_prestera_prestera_pci_class_init,
        .interfaces = interfaces,
    };

    type_register_static(&drivers_net_ethernet_marvell_prestera_prestera_pci_info);
}
type_init(pci_drivers_net_ethernet_marvell_prestera_prestera_pci_register_types)
