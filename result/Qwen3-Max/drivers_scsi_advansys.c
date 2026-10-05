#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/pci/pci.h"
#include "hw/hw.h"
#include "hw/pci/msi.h"
#include "qemu/timer.h"
#include "qemu/main-loop.h" /* iothread mutex */
#include "qemu/module.h"
#include "qapi/visitor.h"

#define DRIVERS_SCSI_ADVANSYS_BUF_LEN 1024

extern uint64_t target_value[DRIVERS_SCSI_ADVANSYS_BUF_LEN];
extern bool target_value_reset;

typedef struct {
    PCIDevice pdev;
    MemoryRegion mmio[7];

    uint32_t len;
    uint64_t buf[DRIVERS_SCSI_ADVANSYS_BUF_LEN];
    int is_probe_time;
} Drivers_scsi_advansysState;

#define TYPE_PCI_DRIVERS_SCSI_ADVANSYS_DEVICE "drivers_scsi_advansys"
#define DRIVERS_SCSI_ADVANSYS(obj)        OBJECT_CHECK(Drivers_scsi_advansysState, obj, TYPE_PCI_DRIVERS_SCSI_ADVANSYS_DEVICE)

static uint64_t drivers_scsi_advansys_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    Drivers_scsi_advansysState *drivers_scsi_advansys = opaque;
    int i;

    if (target_value_reset) {
        drivers_scsi_advansys->len = 0;
        target_value_reset = false;
    }

    if (!drivers_scsi_advansys->is_probe_time) {
        for (i = 0; i < DRIVERS_SCSI_ADVANSYS_BUF_LEN; ++i) {
            drivers_scsi_advansys->buf[i] = target_value[i];
        }
    } else {
        drivers_scsi_advansys->is_probe_time--;
    }

    return drivers_scsi_advansys->buf[(drivers_scsi_advansys->len++) % DRIVERS_SCSI_ADVANSYS_BUF_LEN];
}

static void drivers_scsi_advansys_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                unsigned size)
{
	return;
}

static const MemoryRegionOps drivers_scsi_advansys_mmio_ops = {
    .read = drivers_scsi_advansys_mmio_read,
    .write = drivers_scsi_advansys_mmio_write,
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

static void pci_drivers_scsi_advansys_realize(PCIDevice *pdev, Error **errp)
{
    Drivers_scsi_advansysState *drivers_scsi_advansys = DRIVERS_SCSI_ADVANSYS(pdev);
    uint8_t *pci_conf = pdev->config;
	int pos;

    pci_config_set_interrupt_pin(pci_conf, 1);
	pci_set_word(&pci_conf[4], 65535);
	pci_set_byte(&pci_conf[13], 255);
	

    if (msi_init(pdev, 0, 1, true, false, errp)) {
        return;
    }
	
	pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
	pcie_endpoint_cap_init(pdev, 0);

	pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
	pdev->exp.pm_cap = pos;
	pci_set_word(pdev->config + pos + PCI_PM_PMC, 0x3);

    drivers_scsi_advansys->len = 0;
    drivers_scsi_advansys->is_probe_time = 120;
	drivers_scsi_advansys->buf[0] = 0xff;
	drivers_scsi_advansys->buf[1] = 0xff;
	drivers_scsi_advansys->buf[2] = 0xff;
	drivers_scsi_advansys->buf[3] = 0xff;
	drivers_scsi_advansys->buf[4] = 0xff;
	drivers_scsi_advansys->buf[5] = 0xff;
	drivers_scsi_advansys->buf[6] = 0xff;
	drivers_scsi_advansys->buf[7] = 0xff;
	drivers_scsi_advansys->buf[8] = 0xff;
	drivers_scsi_advansys->buf[9] = 0xff;
	drivers_scsi_advansys->buf[10] = 0xff;
	drivers_scsi_advansys->buf[11] = 0xff;
	drivers_scsi_advansys->buf[12] = 0xff;
	drivers_scsi_advansys->buf[13] = 0xff;
	drivers_scsi_advansys->buf[14] = 0xff;
	drivers_scsi_advansys->buf[15] = 0xff;
	drivers_scsi_advansys->buf[16] = 0xff;
	drivers_scsi_advansys->buf[17] = 0xff;
	drivers_scsi_advansys->buf[18] = 0xff;
	drivers_scsi_advansys->buf[19] = 0xff;
	drivers_scsi_advansys->buf[20] = 0xff;
	drivers_scsi_advansys->buf[21] = 0xff;
	drivers_scsi_advansys->buf[22] = 0xff;
	drivers_scsi_advansys->buf[23] = 0xff;
	drivers_scsi_advansys->buf[24] = 0xff;
	drivers_scsi_advansys->buf[25] = 0xff;
	drivers_scsi_advansys->buf[26] = 0xff;
	drivers_scsi_advansys->buf[27] = 0xff;
	drivers_scsi_advansys->buf[28] = 0xff;
	drivers_scsi_advansys->buf[29] = 0xff;
	drivers_scsi_advansys->buf[30] = 0xff;
	drivers_scsi_advansys->buf[31] = 0xff;
	drivers_scsi_advansys->buf[32] = 0xff;
	drivers_scsi_advansys->buf[33] = 0xff;
	drivers_scsi_advansys->buf[34] = 0xff;
	drivers_scsi_advansys->buf[35] = 0xff;
	drivers_scsi_advansys->buf[36] = 0xff;
	drivers_scsi_advansys->buf[37] = 0xff;
	drivers_scsi_advansys->buf[38] = 0xff;
	drivers_scsi_advansys->buf[39] = 0xff;
	drivers_scsi_advansys->buf[40] = 0xff;
	drivers_scsi_advansys->buf[41] = 0xff;
	drivers_scsi_advansys->buf[42] = 0xff;
	drivers_scsi_advansys->buf[43] = 0xff;
	drivers_scsi_advansys->buf[44] = 0x0;
	drivers_scsi_advansys->buf[45] = 0x3;
	drivers_scsi_advansys->buf[46] = 0xff;
	drivers_scsi_advansys->buf[47] = 0xff;
	drivers_scsi_advansys->buf[48] = 0xff;
	drivers_scsi_advansys->buf[49] = 0xff;
	drivers_scsi_advansys->buf[50] = 0xff;
	drivers_scsi_advansys->buf[51] = 0xff;
	drivers_scsi_advansys->buf[52] = 0xff;
	drivers_scsi_advansys->buf[53] = 0xff;
	drivers_scsi_advansys->buf[54] = 0xff;
	drivers_scsi_advansys->buf[55] = 0xff;
	drivers_scsi_advansys->buf[56] = 0xff;
	drivers_scsi_advansys->buf[57] = 0xff;
	drivers_scsi_advansys->buf[58] = 0xff;
	drivers_scsi_advansys->buf[59] = 0xff;
	drivers_scsi_advansys->buf[60] = 0xff;
	drivers_scsi_advansys->buf[61] = 0x0;
	drivers_scsi_advansys->buf[62] = 0xff;
	drivers_scsi_advansys->buf[63] = 0x0;
	drivers_scsi_advansys->buf[64] = 0xff;
	drivers_scsi_advansys->buf[65] = 0xff;
	drivers_scsi_advansys->buf[66] = 0xff;
	drivers_scsi_advansys->buf[67] = 0xff;
	drivers_scsi_advansys->buf[68] = 0xff;
	drivers_scsi_advansys->buf[69] = 0xff;
	drivers_scsi_advansys->buf[70] = 0xff;
	drivers_scsi_advansys->buf[71] = 0xff;
	drivers_scsi_advansys->buf[72] = 0xff;
	drivers_scsi_advansys->buf[73] = 0xff;
	drivers_scsi_advansys->buf[74] = 0x0;
	drivers_scsi_advansys->buf[75] = 0x4;
	drivers_scsi_advansys->buf[76] = 0xff;
	drivers_scsi_advansys->buf[77] = 0xff;
	drivers_scsi_advansys->buf[78] = 0x0;
	drivers_scsi_advansys->buf[79] = 0x4;
	drivers_scsi_advansys->buf[80] = 0xff;
	drivers_scsi_advansys->buf[81] = 0xff;
	drivers_scsi_advansys->buf[82] = 0xff;
	drivers_scsi_advansys->buf[83] = 0xff;
	drivers_scsi_advansys->buf[84] = 0xff;
	drivers_scsi_advansys->buf[85] = 0xff;
	drivers_scsi_advansys->buf[86] = 0xff;
	drivers_scsi_advansys->buf[87] = 0xff;
	drivers_scsi_advansys->buf[88] = 0xff;
	drivers_scsi_advansys->buf[89] = 0xff;
	drivers_scsi_advansys->buf[90] = 0xff;
	drivers_scsi_advansys->buf[91] = 0xff;
	drivers_scsi_advansys->buf[92] = 0xff;
	drivers_scsi_advansys->buf[93] = 0xff;
	drivers_scsi_advansys->buf[94] = 0xff;
	drivers_scsi_advansys->buf[95] = 0x0;
	drivers_scsi_advansys->buf[96] = 0xff;
	drivers_scsi_advansys->buf[97] = 0x0;
	drivers_scsi_advansys->buf[98] = 0xff;
	drivers_scsi_advansys->buf[99] = 0xff;
	drivers_scsi_advansys->buf[100] = 0xff;
	drivers_scsi_advansys->buf[101] = 0xff;
	drivers_scsi_advansys->buf[102] = 0xff;
	drivers_scsi_advansys->buf[103] = 0xff;
	drivers_scsi_advansys->buf[104] = 0xff;
	drivers_scsi_advansys->buf[105] = 0xff;
	drivers_scsi_advansys->buf[106] = 0xff;
	drivers_scsi_advansys->buf[107] = 0xff;
	drivers_scsi_advansys->buf[108] = 0x0;
	drivers_scsi_advansys->buf[109] = 0x1;
	drivers_scsi_advansys->buf[110] = 0xff;
	drivers_scsi_advansys->buf[111] = 0xff;
	drivers_scsi_advansys->buf[112] = 0xff;
	drivers_scsi_advansys->buf[113] = 0xff;
	drivers_scsi_advansys->buf[114] = 0x0;
	drivers_scsi_advansys->buf[115] = 0x1;
	drivers_scsi_advansys->buf[116] = 0xff;
	drivers_scsi_advansys->buf[117] = 0xff;
	drivers_scsi_advansys->buf[118] = 0xff;
	drivers_scsi_advansys->buf[119] = 0xff;;

    memory_region_init_io(&drivers_scsi_advansys->mmio[0], OBJECT(drivers_scsi_advansys), &drivers_scsi_advansys_mmio_ops, drivers_scsi_advansys,
                    "drivers_scsi_advansys-mmio0", 16 * MiB);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_scsi_advansys->mmio[0]);
    memory_region_init_io(&drivers_scsi_advansys->mmio[1], OBJECT(drivers_scsi_advansys), &drivers_scsi_advansys_mmio_ops, drivers_scsi_advansys,
                    "drivers_scsi_advansys-mmio1", 16 * MiB);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_scsi_advansys->mmio[1]);
    memory_region_init_io(&drivers_scsi_advansys->mmio[2], OBJECT(drivers_scsi_advansys), &drivers_scsi_advansys_mmio_ops, drivers_scsi_advansys,
                    "drivers_scsi_advansys-mmio2", 16 * MiB);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_scsi_advansys->mmio[2]);
    memory_region_init_io(&drivers_scsi_advansys->mmio[3], OBJECT(drivers_scsi_advansys), &drivers_scsi_advansys_mmio_ops, drivers_scsi_advansys,
                    "drivers_scsi_advansys-mmio3", 16 * MiB);
    pci_register_bar(pdev, 3, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_scsi_advansys->mmio[3]);
    memory_region_init_io(&drivers_scsi_advansys->mmio[4], OBJECT(drivers_scsi_advansys), &drivers_scsi_advansys_mmio_ops, drivers_scsi_advansys,
                    "drivers_scsi_advansys-mmio4", 16 * MiB);
    pci_register_bar(pdev, 4, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_scsi_advansys->mmio[4]);
    memory_region_init_io(&drivers_scsi_advansys->mmio[5], OBJECT(drivers_scsi_advansys), &drivers_scsi_advansys_mmio_ops, drivers_scsi_advansys,
                    "drivers_scsi_advansys-mmio5", 16 * MiB);
    pci_register_bar(pdev, 5, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_scsi_advansys->mmio[5]);
    memory_region_init_io(&drivers_scsi_advansys->mmio[6], OBJECT(drivers_scsi_advansys), &drivers_scsi_advansys_mmio_ops, drivers_scsi_advansys,
                    "drivers_scsi_advansys-mmio5", 16 * MiB);
    pci_register_bar(pdev, 6, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_scsi_advansys->mmio[6]);
}

static void pci_drivers_scsi_advansys_uninit(PCIDevice *pdev)
{
    msi_uninit(pdev);
}

static void drivers_scsi_advansys_instance_init(Object *obj)
{
	return;
}

static void drivers_scsi_advansys_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(class);

    k->realize = pci_drivers_scsi_advansys_realize;
    k->exit = pci_drivers_scsi_advansys_uninit;
    k->vendor_id = 0x10cd;
    k->device_id = 0x1100;
    k->revision = 0;
    k->subsystem_vendor_id = 0xffffffff;
    k->subsystem_id = 0xffffffff;
    k->class_id = 16777215;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void pci_drivers_scsi_advansys_register_types(void)
{
    static InterfaceInfo interfaces[] = {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    };
    static const TypeInfo drivers_scsi_advansys_info = {
        .name          = TYPE_PCI_DRIVERS_SCSI_ADVANSYS_DEVICE,
        .parent        = TYPE_PCI_DEVICE,
        .instance_size = sizeof(Drivers_scsi_advansysState),
        .instance_init = drivers_scsi_advansys_instance_init,
        .class_init    = drivers_scsi_advansys_class_init,
        .interfaces = interfaces,
    };

    type_register_static(&drivers_scsi_advansys_info);
}
type_init(pci_drivers_scsi_advansys_register_types)
