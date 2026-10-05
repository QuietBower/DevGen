#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/pci/pci.h"
#include "hw/hw.h"
#include "hw/pci/msi.h"
#include "qemu/timer.h"
#include "qemu/main-loop.h" /* iothread mutex */
#include "qemu/module.h"
#include "qapi/visitor.h"

#define DRIVERS_NET_ETHERNET_QLOGIC_QLA3XXX_BUF_LEN 1024

extern uint64_t target_value[DRIVERS_NET_ETHERNET_QLOGIC_QLA3XXX_BUF_LEN];
extern bool target_value_reset;

typedef struct {
    PCIDevice pdev;
    MemoryRegion mmio[7];

    uint32_t len;
    uint64_t buf[DRIVERS_NET_ETHERNET_QLOGIC_QLA3XXX_BUF_LEN];
    int is_probe_time;
} Drivers_net_ethernet_qlogic_qla3xxxState;

#define TYPE_PCI_DRIVERS_NET_ETHERNET_QLOGIC_QLA3XXX_DEVICE "drivers_net_ethernet_qlogic_qla3xxx"
#define DRIVERS_NET_ETHERNET_QLOGIC_QLA3XXX(obj)        OBJECT_CHECK(Drivers_net_ethernet_qlogic_qla3xxxState, obj, TYPE_PCI_DRIVERS_NET_ETHERNET_QLOGIC_QLA3XXX_DEVICE)

static uint64_t drivers_net_ethernet_qlogic_qla3xxx_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    Drivers_net_ethernet_qlogic_qla3xxxState *drivers_net_ethernet_qlogic_qla3xxx = opaque;
    int i;

    if (target_value_reset) {
        drivers_net_ethernet_qlogic_qla3xxx->len = 0;
        target_value_reset = false;
    }

    if (!drivers_net_ethernet_qlogic_qla3xxx->is_probe_time) {
        for (i = 0; i < DRIVERS_NET_ETHERNET_QLOGIC_QLA3XXX_BUF_LEN; ++i) {
            drivers_net_ethernet_qlogic_qla3xxx->buf[i] = target_value[i];
        }
    } else {
        drivers_net_ethernet_qlogic_qla3xxx->is_probe_time--;
    }

    return drivers_net_ethernet_qlogic_qla3xxx->buf[(drivers_net_ethernet_qlogic_qla3xxx->len++) % DRIVERS_NET_ETHERNET_QLOGIC_QLA3XXX_BUF_LEN];
}

static void drivers_net_ethernet_qlogic_qla3xxx_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                unsigned size)
{
	return;
}

static const MemoryRegionOps drivers_net_ethernet_qlogic_qla3xxx_mmio_ops = {
    .read = drivers_net_ethernet_qlogic_qla3xxx_mmio_read,
    .write = drivers_net_ethernet_qlogic_qla3xxx_mmio_write,
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

static void pci_drivers_net_ethernet_qlogic_qla3xxx_realize(PCIDevice *pdev, Error **errp)
{
    Drivers_net_ethernet_qlogic_qla3xxxState *drivers_net_ethernet_qlogic_qla3xxx = DRIVERS_NET_ETHERNET_QLOGIC_QLA3XXX(pdev);
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

    drivers_net_ethernet_qlogic_qla3xxx->len = 0;
    drivers_net_ethernet_qlogic_qla3xxx->is_probe_time = 52;
	drivers_net_ethernet_qlogic_qla3xxx->buf[0] = 0xff;
	drivers_net_ethernet_qlogic_qla3xxx->buf[1] = 0xff;
	drivers_net_ethernet_qlogic_qla3xxx->buf[2] = 0xff;
	drivers_net_ethernet_qlogic_qla3xxx->buf[3] = 0xff;
	drivers_net_ethernet_qlogic_qla3xxx->buf[4] = 0xff;
	drivers_net_ethernet_qlogic_qla3xxx->buf[5] = 0xff;
	drivers_net_ethernet_qlogic_qla3xxx->buf[6] = 0xff;
	drivers_net_ethernet_qlogic_qla3xxx->buf[7] = 0xff;
	drivers_net_ethernet_qlogic_qla3xxx->buf[8] = 0xff;
	drivers_net_ethernet_qlogic_qla3xxx->buf[9] = 0xff;
	drivers_net_ethernet_qlogic_qla3xxx->buf[10] = 0xff;
	drivers_net_ethernet_qlogic_qla3xxx->buf[11] = 0xff;
	drivers_net_ethernet_qlogic_qla3xxx->buf[12] = 0xff;
	drivers_net_ethernet_qlogic_qla3xxx->buf[13] = 0xff;
	drivers_net_ethernet_qlogic_qla3xxx->buf[14] = 0xff;
	drivers_net_ethernet_qlogic_qla3xxx->buf[15] = 0xff;
	drivers_net_ethernet_qlogic_qla3xxx->buf[16] = 0xff;
	drivers_net_ethernet_qlogic_qla3xxx->buf[17] = 0xff;
	drivers_net_ethernet_qlogic_qla3xxx->buf[18] = 0xff;
	drivers_net_ethernet_qlogic_qla3xxx->buf[19] = 0xff;
	drivers_net_ethernet_qlogic_qla3xxx->buf[20] = 0xff;
	drivers_net_ethernet_qlogic_qla3xxx->buf[21] = 0xff;
	drivers_net_ethernet_qlogic_qla3xxx->buf[22] = 0xff;
	drivers_net_ethernet_qlogic_qla3xxx->buf[23] = 0xff;
	drivers_net_ethernet_qlogic_qla3xxx->buf[24] = 0x0;
	drivers_net_ethernet_qlogic_qla3xxx->buf[25] = 0x0;
	drivers_net_ethernet_qlogic_qla3xxx->buf[26] = 0x4;
	drivers_net_ethernet_qlogic_qla3xxx->buf[27] = 0x0;
	drivers_net_ethernet_qlogic_qla3xxx->buf[28] = 0x0;
	drivers_net_ethernet_qlogic_qla3xxx->buf[29] = 0x0;
	drivers_net_ethernet_qlogic_qla3xxx->buf[30] = 0x4;
	drivers_net_ethernet_qlogic_qla3xxx->buf[31] = 0x0;
	drivers_net_ethernet_qlogic_qla3xxx->buf[32] = 0xff;
	drivers_net_ethernet_qlogic_qla3xxx->buf[33] = 0xff;
	drivers_net_ethernet_qlogic_qla3xxx->buf[34] = 0xff;
	drivers_net_ethernet_qlogic_qla3xxx->buf[35] = 0xff;
	drivers_net_ethernet_qlogic_qla3xxx->buf[36] = 0xff;
	drivers_net_ethernet_qlogic_qla3xxx->buf[37] = 0xff;
	drivers_net_ethernet_qlogic_qla3xxx->buf[38] = 0xff;
	drivers_net_ethernet_qlogic_qla3xxx->buf[39] = 0xff;
	drivers_net_ethernet_qlogic_qla3xxx->buf[40] = 0xff;
	drivers_net_ethernet_qlogic_qla3xxx->buf[41] = 0xff;
	drivers_net_ethernet_qlogic_qla3xxx->buf[42] = 0xff;
	drivers_net_ethernet_qlogic_qla3xxx->buf[43] = 0xff;
	drivers_net_ethernet_qlogic_qla3xxx->buf[44] = 0xff;
	drivers_net_ethernet_qlogic_qla3xxx->buf[45] = 0xff;
	drivers_net_ethernet_qlogic_qla3xxx->buf[46] = 0xff;
	drivers_net_ethernet_qlogic_qla3xxx->buf[47] = 0xff;
	drivers_net_ethernet_qlogic_qla3xxx->buf[48] = 0x0;
	drivers_net_ethernet_qlogic_qla3xxx->buf[49] = 0x0;
	drivers_net_ethernet_qlogic_qla3xxx->buf[50] = 0x0;
	drivers_net_ethernet_qlogic_qla3xxx->buf[51] = 0x0;;

    memory_region_init_io(&drivers_net_ethernet_qlogic_qla3xxx->mmio[0], OBJECT(drivers_net_ethernet_qlogic_qla3xxx), &drivers_net_ethernet_qlogic_qla3xxx_mmio_ops, drivers_net_ethernet_qlogic_qla3xxx,
                    "drivers_net_ethernet_qlogic_qla3xxx-mmio0", 16 * MiB);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_net_ethernet_qlogic_qla3xxx->mmio[0]);
    memory_region_init_io(&drivers_net_ethernet_qlogic_qla3xxx->mmio[1], OBJECT(drivers_net_ethernet_qlogic_qla3xxx), &drivers_net_ethernet_qlogic_qla3xxx_mmio_ops, drivers_net_ethernet_qlogic_qla3xxx,
                    "drivers_net_ethernet_qlogic_qla3xxx-mmio1", 16 * MiB);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_net_ethernet_qlogic_qla3xxx->mmio[1]);
    memory_region_init_io(&drivers_net_ethernet_qlogic_qla3xxx->mmio[2], OBJECT(drivers_net_ethernet_qlogic_qla3xxx), &drivers_net_ethernet_qlogic_qla3xxx_mmio_ops, drivers_net_ethernet_qlogic_qla3xxx,
                    "drivers_net_ethernet_qlogic_qla3xxx-mmio2", 16 * MiB);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_net_ethernet_qlogic_qla3xxx->mmio[2]);
    memory_region_init_io(&drivers_net_ethernet_qlogic_qla3xxx->mmio[3], OBJECT(drivers_net_ethernet_qlogic_qla3xxx), &drivers_net_ethernet_qlogic_qla3xxx_mmio_ops, drivers_net_ethernet_qlogic_qla3xxx,
                    "drivers_net_ethernet_qlogic_qla3xxx-mmio3", 16 * MiB);
    pci_register_bar(pdev, 3, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_net_ethernet_qlogic_qla3xxx->mmio[3]);
    memory_region_init_io(&drivers_net_ethernet_qlogic_qla3xxx->mmio[4], OBJECT(drivers_net_ethernet_qlogic_qla3xxx), &drivers_net_ethernet_qlogic_qla3xxx_mmio_ops, drivers_net_ethernet_qlogic_qla3xxx,
                    "drivers_net_ethernet_qlogic_qla3xxx-mmio4", 16 * MiB);
    pci_register_bar(pdev, 4, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_net_ethernet_qlogic_qla3xxx->mmio[4]);
    memory_region_init_io(&drivers_net_ethernet_qlogic_qla3xxx->mmio[5], OBJECT(drivers_net_ethernet_qlogic_qla3xxx), &drivers_net_ethernet_qlogic_qla3xxx_mmio_ops, drivers_net_ethernet_qlogic_qla3xxx,
                    "drivers_net_ethernet_qlogic_qla3xxx-mmio5", 16 * MiB);
    pci_register_bar(pdev, 5, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_net_ethernet_qlogic_qla3xxx->mmio[5]);
    memory_region_init_io(&drivers_net_ethernet_qlogic_qla3xxx->mmio[6], OBJECT(drivers_net_ethernet_qlogic_qla3xxx), &drivers_net_ethernet_qlogic_qla3xxx_mmio_ops, drivers_net_ethernet_qlogic_qla3xxx,
                    "drivers_net_ethernet_qlogic_qla3xxx-mmio5", 16 * MiB);
    pci_register_bar(pdev, 6, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_net_ethernet_qlogic_qla3xxx->mmio[6]);
}

static void pci_drivers_net_ethernet_qlogic_qla3xxx_uninit(PCIDevice *pdev)
{
    msi_uninit(pdev);
}

static void drivers_net_ethernet_qlogic_qla3xxx_instance_init(Object *obj)
{
	return;
}

static void drivers_net_ethernet_qlogic_qla3xxx_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(class);

    k->realize = pci_drivers_net_ethernet_qlogic_qla3xxx_realize;
    k->exit = pci_drivers_net_ethernet_qlogic_qla3xxx_uninit;
    k->vendor_id = 0x1077;
    k->device_id = 0x3022;
    k->revision = 0;
    k->subsystem_vendor_id = 0xffffffff;
    k->subsystem_id = 0xffffffff;
    k->class_id = 16777215;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void pci_drivers_net_ethernet_qlogic_qla3xxx_register_types(void)
{
    static InterfaceInfo interfaces[] = {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    };
    static const TypeInfo drivers_net_ethernet_qlogic_qla3xxx_info = {
        .name          = TYPE_PCI_DRIVERS_NET_ETHERNET_QLOGIC_QLA3XXX_DEVICE,
        .parent        = TYPE_PCI_DEVICE,
        .instance_size = sizeof(Drivers_net_ethernet_qlogic_qla3xxxState),
        .instance_init = drivers_net_ethernet_qlogic_qla3xxx_instance_init,
        .class_init    = drivers_net_ethernet_qlogic_qla3xxx_class_init,
        .interfaces = interfaces,
    };

    type_register_static(&drivers_net_ethernet_qlogic_qla3xxx_info);
}
type_init(pci_drivers_net_ethernet_qlogic_qla3xxx_register_types)
