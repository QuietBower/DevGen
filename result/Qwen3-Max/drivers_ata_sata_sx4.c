#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/pci/pci.h"
#include "hw/hw.h"
#include "hw/pci/msi.h"
#include "qemu/timer.h"
#include "qemu/main-loop.h" /* iothread mutex */
#include "qemu/module.h"
#include "qapi/visitor.h"

#define DRIVERS_ATA_SATA_SX4_BUF_LEN 1024

extern uint64_t target_value[DRIVERS_ATA_SATA_SX4_BUF_LEN];
extern bool target_value_reset;

typedef struct {
    PCIDevice pdev;
    MemoryRegion mmio[7];

    uint32_t len;
    uint64_t buf[DRIVERS_ATA_SATA_SX4_BUF_LEN];
    int is_probe_time;
} Drivers_ata_sata_sx4State;

#define TYPE_PCI_DRIVERS_ATA_SATA_SX4_DEVICE "drivers_ata_sata_sx4"
#define DRIVERS_ATA_SATA_SX4(obj)        OBJECT_CHECK(Drivers_ata_sata_sx4State, obj, TYPE_PCI_DRIVERS_ATA_SATA_SX4_DEVICE)

static uint64_t drivers_ata_sata_sx4_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    Drivers_ata_sata_sx4State *drivers_ata_sata_sx4 = opaque;
    int i;

    if (target_value_reset) {
        drivers_ata_sata_sx4->len = 0;
        target_value_reset = false;
    }

    if (!drivers_ata_sata_sx4->is_probe_time) {
        for (i = 0; i < DRIVERS_ATA_SATA_SX4_BUF_LEN; ++i) {
            drivers_ata_sata_sx4->buf[i] = target_value[i];
        }
    } else {
        drivers_ata_sata_sx4->is_probe_time--;
    }

    return drivers_ata_sata_sx4->buf[(drivers_ata_sata_sx4->len++) % DRIVERS_ATA_SATA_SX4_BUF_LEN];
}

static void drivers_ata_sata_sx4_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                unsigned size)
{
	return;
}

static const MemoryRegionOps drivers_ata_sata_sx4_mmio_ops = {
    .read = drivers_ata_sata_sx4_mmio_read,
    .write = drivers_ata_sata_sx4_mmio_write,
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

static void pci_drivers_ata_sata_sx4_realize(PCIDevice *pdev, Error **errp)
{
    Drivers_ata_sata_sx4State *drivers_ata_sata_sx4 = DRIVERS_ATA_SATA_SX4(pdev);
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

    drivers_ata_sata_sx4->len = 0;
    drivers_ata_sata_sx4->is_probe_time = 112;
	drivers_ata_sata_sx4->buf[0] = 0xff;
	drivers_ata_sata_sx4->buf[1] = 0xff;
	drivers_ata_sata_sx4->buf[2] = 0xff;
	drivers_ata_sata_sx4->buf[3] = 0xff;
	drivers_ata_sata_sx4->buf[4] = 0xff;
	drivers_ata_sata_sx4->buf[5] = 0xff;
	drivers_ata_sata_sx4->buf[6] = 0xff;
	drivers_ata_sata_sx4->buf[7] = 0xff;
	drivers_ata_sata_sx4->buf[8] = 0xff;
	drivers_ata_sata_sx4->buf[9] = 0xff;
	drivers_ata_sata_sx4->buf[10] = 0xff;
	drivers_ata_sata_sx4->buf[11] = 0xff;
	drivers_ata_sata_sx4->buf[12] = 0xff;
	drivers_ata_sata_sx4->buf[13] = 0xff;
	drivers_ata_sata_sx4->buf[14] = 0xff;
	drivers_ata_sata_sx4->buf[15] = 0xff;
	drivers_ata_sata_sx4->buf[16] = 0xff;
	drivers_ata_sata_sx4->buf[17] = 0xff;
	drivers_ata_sata_sx4->buf[18] = 0xff;
	drivers_ata_sata_sx4->buf[19] = 0xff;
	drivers_ata_sata_sx4->buf[20] = 0x0;
	drivers_ata_sata_sx4->buf[21] = 0x0;
	drivers_ata_sata_sx4->buf[22] = 0x0;
	drivers_ata_sata_sx4->buf[23] = 0x0;
	drivers_ata_sata_sx4->buf[24] = 0x0;
	drivers_ata_sata_sx4->buf[25] = 0x0;
	drivers_ata_sata_sx4->buf[26] = 0x0;
	drivers_ata_sata_sx4->buf[27] = 0x0;
	drivers_ata_sata_sx4->buf[28] = 0xff;
	drivers_ata_sata_sx4->buf[29] = 0xff;
	drivers_ata_sata_sx4->buf[30] = 0xff;
	drivers_ata_sata_sx4->buf[31] = 0xff;
	drivers_ata_sata_sx4->buf[32] = 0xff;
	drivers_ata_sata_sx4->buf[33] = 0xff;
	drivers_ata_sata_sx4->buf[34] = 0xff;
	drivers_ata_sata_sx4->buf[35] = 0xff;
	drivers_ata_sata_sx4->buf[36] = 0xff;
	drivers_ata_sata_sx4->buf[37] = 0xff;
	drivers_ata_sata_sx4->buf[38] = 0xff;
	drivers_ata_sata_sx4->buf[39] = 0xff;
	drivers_ata_sata_sx4->buf[40] = 0xff;
	drivers_ata_sata_sx4->buf[41] = 0xff;
	drivers_ata_sata_sx4->buf[42] = 0xff;
	drivers_ata_sata_sx4->buf[43] = 0xff;
	drivers_ata_sata_sx4->buf[44] = 0xff;
	drivers_ata_sata_sx4->buf[45] = 0xff;
	drivers_ata_sata_sx4->buf[46] = 0xff;
	drivers_ata_sata_sx4->buf[47] = 0xff;
	drivers_ata_sata_sx4->buf[48] = 0xff;
	drivers_ata_sata_sx4->buf[49] = 0xff;
	drivers_ata_sata_sx4->buf[50] = 0xff;
	drivers_ata_sata_sx4->buf[51] = 0xff;
	drivers_ata_sata_sx4->buf[52] = 0xff;
	drivers_ata_sata_sx4->buf[53] = 0xff;
	drivers_ata_sata_sx4->buf[54] = 0xff;
	drivers_ata_sata_sx4->buf[55] = 0xff;
	drivers_ata_sata_sx4->buf[56] = 0xff;
	drivers_ata_sata_sx4->buf[57] = 0xff;
	drivers_ata_sata_sx4->buf[58] = 0xff;
	drivers_ata_sata_sx4->buf[59] = 0xff;
	drivers_ata_sata_sx4->buf[60] = 0xff;
	drivers_ata_sata_sx4->buf[61] = 0xff;
	drivers_ata_sata_sx4->buf[62] = 0xff;
	drivers_ata_sata_sx4->buf[63] = 0xff;
	drivers_ata_sata_sx4->buf[64] = 0xff;
	drivers_ata_sata_sx4->buf[65] = 0xff;
	drivers_ata_sata_sx4->buf[66] = 0xff;
	drivers_ata_sata_sx4->buf[67] = 0xff;
	drivers_ata_sata_sx4->buf[68] = 0xff;
	drivers_ata_sata_sx4->buf[69] = 0xff;
	drivers_ata_sata_sx4->buf[70] = 0xff;
	drivers_ata_sata_sx4->buf[71] = 0xff;
	drivers_ata_sata_sx4->buf[72] = 0xff;
	drivers_ata_sata_sx4->buf[73] = 0xff;
	drivers_ata_sata_sx4->buf[74] = 0xff;
	drivers_ata_sata_sx4->buf[75] = 0xff;
	drivers_ata_sata_sx4->buf[76] = 0xff;
	drivers_ata_sata_sx4->buf[77] = 0xff;
	drivers_ata_sata_sx4->buf[78] = 0xff;
	drivers_ata_sata_sx4->buf[79] = 0xff;
	drivers_ata_sata_sx4->buf[80] = 0xff;
	drivers_ata_sata_sx4->buf[81] = 0xff;
	drivers_ata_sata_sx4->buf[82] = 0xff;
	drivers_ata_sata_sx4->buf[83] = 0xff;
	drivers_ata_sata_sx4->buf[84] = 0xff;
	drivers_ata_sata_sx4->buf[85] = 0xff;
	drivers_ata_sata_sx4->buf[86] = 0xff;
	drivers_ata_sata_sx4->buf[87] = 0xff;
	drivers_ata_sata_sx4->buf[88] = 0xff;
	drivers_ata_sata_sx4->buf[89] = 0xff;
	drivers_ata_sata_sx4->buf[90] = 0xff;
	drivers_ata_sata_sx4->buf[91] = 0xff;
	drivers_ata_sata_sx4->buf[92] = 0xff;
	drivers_ata_sata_sx4->buf[93] = 0xff;
	drivers_ata_sata_sx4->buf[94] = 0xff;
	drivers_ata_sata_sx4->buf[95] = 0xff;
	drivers_ata_sata_sx4->buf[96] = 0xff;
	drivers_ata_sata_sx4->buf[97] = 0xff;
	drivers_ata_sata_sx4->buf[98] = 0xff;
	drivers_ata_sata_sx4->buf[99] = 0xff;
	drivers_ata_sata_sx4->buf[100] = 0xff;
	drivers_ata_sata_sx4->buf[101] = 0xff;
	drivers_ata_sata_sx4->buf[102] = 0xff;
	drivers_ata_sata_sx4->buf[103] = 0xff;
	drivers_ata_sata_sx4->buf[104] = 0xff;
	drivers_ata_sata_sx4->buf[105] = 0xff;
	drivers_ata_sata_sx4->buf[106] = 0xff;
	drivers_ata_sata_sx4->buf[107] = 0xff;
	drivers_ata_sata_sx4->buf[108] = 0xff;
	drivers_ata_sata_sx4->buf[109] = 0xff;
	drivers_ata_sata_sx4->buf[110] = 0xff;
	drivers_ata_sata_sx4->buf[111] = 0xff;;

    memory_region_init_io(&drivers_ata_sata_sx4->mmio[0], OBJECT(drivers_ata_sata_sx4), &drivers_ata_sata_sx4_mmio_ops, drivers_ata_sata_sx4,
                    "drivers_ata_sata_sx4-mmio0", 16 * MiB);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_ata_sata_sx4->mmio[0]);
    memory_region_init_io(&drivers_ata_sata_sx4->mmio[1], OBJECT(drivers_ata_sata_sx4), &drivers_ata_sata_sx4_mmio_ops, drivers_ata_sata_sx4,
                    "drivers_ata_sata_sx4-mmio1", 16 * MiB);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_ata_sata_sx4->mmio[1]);
    memory_region_init_io(&drivers_ata_sata_sx4->mmio[2], OBJECT(drivers_ata_sata_sx4), &drivers_ata_sata_sx4_mmio_ops, drivers_ata_sata_sx4,
                    "drivers_ata_sata_sx4-mmio2", 16 * MiB);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_ata_sata_sx4->mmio[2]);
    memory_region_init_io(&drivers_ata_sata_sx4->mmio[3], OBJECT(drivers_ata_sata_sx4), &drivers_ata_sata_sx4_mmio_ops, drivers_ata_sata_sx4,
                    "drivers_ata_sata_sx4-mmio3", 16 * MiB);
    pci_register_bar(pdev, 3, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_ata_sata_sx4->mmio[3]);
    memory_region_init_io(&drivers_ata_sata_sx4->mmio[4], OBJECT(drivers_ata_sata_sx4), &drivers_ata_sata_sx4_mmio_ops, drivers_ata_sata_sx4,
                    "drivers_ata_sata_sx4-mmio4", 16 * MiB);
    pci_register_bar(pdev, 4, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_ata_sata_sx4->mmio[4]);
    memory_region_init_io(&drivers_ata_sata_sx4->mmio[5], OBJECT(drivers_ata_sata_sx4), &drivers_ata_sata_sx4_mmio_ops, drivers_ata_sata_sx4,
                    "drivers_ata_sata_sx4-mmio5", 16 * MiB);
    pci_register_bar(pdev, 5, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_ata_sata_sx4->mmio[5]);
    memory_region_init_io(&drivers_ata_sata_sx4->mmio[6], OBJECT(drivers_ata_sata_sx4), &drivers_ata_sata_sx4_mmio_ops, drivers_ata_sata_sx4,
                    "drivers_ata_sata_sx4-mmio5", 16 * MiB);
    pci_register_bar(pdev, 6, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_ata_sata_sx4->mmio[6]);
}

static void pci_drivers_ata_sata_sx4_uninit(PCIDevice *pdev)
{
    msi_uninit(pdev);
}

static void drivers_ata_sata_sx4_instance_init(Object *obj)
{
	return;
}

static void drivers_ata_sata_sx4_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(class);

    k->realize = pci_drivers_ata_sata_sx4_realize;
    k->exit = pci_drivers_ata_sata_sx4_uninit;
    k->vendor_id = 0x105a;
    k->device_id = 0x6622;
    k->revision = 0;
    k->subsystem_vendor_id = 0xffffffff;
    k->subsystem_id = 0xffffffff;
    k->class_id = 16777215;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void pci_drivers_ata_sata_sx4_register_types(void)
{
    static InterfaceInfo interfaces[] = {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    };
    static const TypeInfo drivers_ata_sata_sx4_info = {
        .name          = TYPE_PCI_DRIVERS_ATA_SATA_SX4_DEVICE,
        .parent        = TYPE_PCI_DEVICE,
        .instance_size = sizeof(Drivers_ata_sata_sx4State),
        .instance_init = drivers_ata_sata_sx4_instance_init,
        .class_init    = drivers_ata_sata_sx4_class_init,
        .interfaces = interfaces,
    };

    type_register_static(&drivers_ata_sata_sx4_info);
}
type_init(pci_drivers_ata_sata_sx4_register_types)
