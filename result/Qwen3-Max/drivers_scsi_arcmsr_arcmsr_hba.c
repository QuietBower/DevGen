#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/pci/pci.h"
#include "hw/hw.h"
#include "hw/pci/msi.h"
#include "qemu/timer.h"
#include "qemu/main-loop.h" /* iothread mutex */
#include "qemu/module.h"
#include "qapi/visitor.h"

#define DRIVERS_SCSI_ARCMSR_ARCMSR_HBA_BUF_LEN 1024

extern uint64_t target_value[DRIVERS_SCSI_ARCMSR_ARCMSR_HBA_BUF_LEN];
extern bool target_value_reset;

typedef struct {
    PCIDevice pdev;
    MemoryRegion mmio[7];

    uint32_t len;
    uint64_t buf[DRIVERS_SCSI_ARCMSR_ARCMSR_HBA_BUF_LEN];
    int is_probe_time;
} Drivers_scsi_arcmsr_arcmsr_hbaState;

#define TYPE_PCI_DRIVERS_SCSI_ARCMSR_ARCMSR_HBA_DEVICE "drivers_scsi_arcmsr_arcmsr_hba"
#define DRIVERS_SCSI_ARCMSR_ARCMSR_HBA(obj)        OBJECT_CHECK(Drivers_scsi_arcmsr_arcmsr_hbaState, obj, TYPE_PCI_DRIVERS_SCSI_ARCMSR_ARCMSR_HBA_DEVICE)

static uint64_t drivers_scsi_arcmsr_arcmsr_hba_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    Drivers_scsi_arcmsr_arcmsr_hbaState *drivers_scsi_arcmsr_arcmsr_hba = opaque;
    int i;

    if (target_value_reset) {
        drivers_scsi_arcmsr_arcmsr_hba->len = 0;
        target_value_reset = false;
    }

    if (!drivers_scsi_arcmsr_arcmsr_hba->is_probe_time) {
        for (i = 0; i < DRIVERS_SCSI_ARCMSR_ARCMSR_HBA_BUF_LEN; ++i) {
            drivers_scsi_arcmsr_arcmsr_hba->buf[i] = target_value[i];
        }
    } else {
        drivers_scsi_arcmsr_arcmsr_hba->is_probe_time--;
    }

    return drivers_scsi_arcmsr_arcmsr_hba->buf[(drivers_scsi_arcmsr_arcmsr_hba->len++) % DRIVERS_SCSI_ARCMSR_ARCMSR_HBA_BUF_LEN];
}

static void drivers_scsi_arcmsr_arcmsr_hba_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                unsigned size)
{
	return;
}

static const MemoryRegionOps drivers_scsi_arcmsr_arcmsr_hba_mmio_ops = {
    .read = drivers_scsi_arcmsr_arcmsr_hba_mmio_read,
    .write = drivers_scsi_arcmsr_arcmsr_hba_mmio_write,
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

static void pci_drivers_scsi_arcmsr_arcmsr_hba_realize(PCIDevice *pdev, Error **errp)
{
    Drivers_scsi_arcmsr_arcmsr_hbaState *drivers_scsi_arcmsr_arcmsr_hba = DRIVERS_SCSI_ARCMSR_ARCMSR_HBA(pdev);
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

    drivers_scsi_arcmsr_arcmsr_hba->len = 0;
    drivers_scsi_arcmsr_arcmsr_hba->is_probe_time = 152;
	drivers_scsi_arcmsr_arcmsr_hba->buf[0] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[1] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[2] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[3] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[4] = 0x0;
	drivers_scsi_arcmsr_arcmsr_hba->buf[5] = 0x0;
	drivers_scsi_arcmsr_arcmsr_hba->buf[6] = 0x0;
	drivers_scsi_arcmsr_arcmsr_hba->buf[7] = 0x0;
	drivers_scsi_arcmsr_arcmsr_hba->buf[8] = 0x0;
	drivers_scsi_arcmsr_arcmsr_hba->buf[9] = 0x0;
	drivers_scsi_arcmsr_arcmsr_hba->buf[10] = 0x0;
	drivers_scsi_arcmsr_arcmsr_hba->buf[11] = 0x0;
	drivers_scsi_arcmsr_arcmsr_hba->buf[12] = 0x0;
	drivers_scsi_arcmsr_arcmsr_hba->buf[13] = 0x0;
	drivers_scsi_arcmsr_arcmsr_hba->buf[14] = 0x0;
	drivers_scsi_arcmsr_arcmsr_hba->buf[15] = 0x0;
	drivers_scsi_arcmsr_arcmsr_hba->buf[16] = 0x0;
	drivers_scsi_arcmsr_arcmsr_hba->buf[17] = 0x0;
	drivers_scsi_arcmsr_arcmsr_hba->buf[18] = 0x0;
	drivers_scsi_arcmsr_arcmsr_hba->buf[19] = 0x0;
	drivers_scsi_arcmsr_arcmsr_hba->buf[20] = 0x0;
	drivers_scsi_arcmsr_arcmsr_hba->buf[21] = 0x0;
	drivers_scsi_arcmsr_arcmsr_hba->buf[22] = 0x0;
	drivers_scsi_arcmsr_arcmsr_hba->buf[23] = 0x0;
	drivers_scsi_arcmsr_arcmsr_hba->buf[24] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[25] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[26] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[27] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[28] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[29] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[30] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[31] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[32] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[33] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[34] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[35] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[36] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[37] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[38] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[39] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[40] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[41] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[42] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[43] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[44] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[45] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[46] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[47] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[48] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[49] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[50] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[51] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[52] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[53] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[54] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[55] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[56] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[57] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[58] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[59] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[60] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[61] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[62] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[63] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[64] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[65] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[66] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[67] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[68] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[69] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[70] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[71] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[72] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[73] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[74] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[75] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[76] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[77] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[78] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[79] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[80] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[81] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[82] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[83] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[84] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[85] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[86] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[87] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[88] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[89] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[90] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[91] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[92] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[93] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[94] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[95] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[96] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[97] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[98] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[99] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[100] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[101] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[102] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[103] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[104] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[105] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[106] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[107] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[108] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[109] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[110] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[111] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[112] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[113] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[114] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[115] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[116] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[117] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[118] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[119] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[120] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[121] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[122] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[123] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[124] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[125] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[126] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[127] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[128] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[129] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[130] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[131] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[132] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[133] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[134] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[135] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[136] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[137] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[138] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[139] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[140] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[141] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[142] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[143] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[144] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[145] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[146] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[147] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[148] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[149] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[150] = 0xff;
	drivers_scsi_arcmsr_arcmsr_hba->buf[151] = 0xff;;

    memory_region_init_io(&drivers_scsi_arcmsr_arcmsr_hba->mmio[0], OBJECT(drivers_scsi_arcmsr_arcmsr_hba), &drivers_scsi_arcmsr_arcmsr_hba_mmio_ops, drivers_scsi_arcmsr_arcmsr_hba,
                    "drivers_scsi_arcmsr_arcmsr_hba-mmio0", 16 * MiB);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_scsi_arcmsr_arcmsr_hba->mmio[0]);
    memory_region_init_io(&drivers_scsi_arcmsr_arcmsr_hba->mmio[1], OBJECT(drivers_scsi_arcmsr_arcmsr_hba), &drivers_scsi_arcmsr_arcmsr_hba_mmio_ops, drivers_scsi_arcmsr_arcmsr_hba,
                    "drivers_scsi_arcmsr_arcmsr_hba-mmio1", 16 * MiB);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_scsi_arcmsr_arcmsr_hba->mmio[1]);
    memory_region_init_io(&drivers_scsi_arcmsr_arcmsr_hba->mmio[2], OBJECT(drivers_scsi_arcmsr_arcmsr_hba), &drivers_scsi_arcmsr_arcmsr_hba_mmio_ops, drivers_scsi_arcmsr_arcmsr_hba,
                    "drivers_scsi_arcmsr_arcmsr_hba-mmio2", 16 * MiB);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_scsi_arcmsr_arcmsr_hba->mmio[2]);
    memory_region_init_io(&drivers_scsi_arcmsr_arcmsr_hba->mmio[3], OBJECT(drivers_scsi_arcmsr_arcmsr_hba), &drivers_scsi_arcmsr_arcmsr_hba_mmio_ops, drivers_scsi_arcmsr_arcmsr_hba,
                    "drivers_scsi_arcmsr_arcmsr_hba-mmio3", 16 * MiB);
    pci_register_bar(pdev, 3, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_scsi_arcmsr_arcmsr_hba->mmio[3]);
    memory_region_init_io(&drivers_scsi_arcmsr_arcmsr_hba->mmio[4], OBJECT(drivers_scsi_arcmsr_arcmsr_hba), &drivers_scsi_arcmsr_arcmsr_hba_mmio_ops, drivers_scsi_arcmsr_arcmsr_hba,
                    "drivers_scsi_arcmsr_arcmsr_hba-mmio4", 16 * MiB);
    pci_register_bar(pdev, 4, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_scsi_arcmsr_arcmsr_hba->mmio[4]);
    memory_region_init_io(&drivers_scsi_arcmsr_arcmsr_hba->mmio[5], OBJECT(drivers_scsi_arcmsr_arcmsr_hba), &drivers_scsi_arcmsr_arcmsr_hba_mmio_ops, drivers_scsi_arcmsr_arcmsr_hba,
                    "drivers_scsi_arcmsr_arcmsr_hba-mmio5", 16 * MiB);
    pci_register_bar(pdev, 5, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_scsi_arcmsr_arcmsr_hba->mmio[5]);
    memory_region_init_io(&drivers_scsi_arcmsr_arcmsr_hba->mmio[6], OBJECT(drivers_scsi_arcmsr_arcmsr_hba), &drivers_scsi_arcmsr_arcmsr_hba_mmio_ops, drivers_scsi_arcmsr_arcmsr_hba,
                    "drivers_scsi_arcmsr_arcmsr_hba-mmio5", 16 * MiB);
    pci_register_bar(pdev, 6, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_scsi_arcmsr_arcmsr_hba->mmio[6]);
}

static void pci_drivers_scsi_arcmsr_arcmsr_hba_uninit(PCIDevice *pdev)
{
    msi_uninit(pdev);
}

static void drivers_scsi_arcmsr_arcmsr_hba_instance_init(Object *obj)
{
	return;
}

static void drivers_scsi_arcmsr_arcmsr_hba_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(class);

    k->realize = pci_drivers_scsi_arcmsr_arcmsr_hba_realize;
    k->exit = pci_drivers_scsi_arcmsr_arcmsr_hba_uninit;
    k->vendor_id = 0x17d3;
    k->device_id = 0x1110;
    k->revision = 0;
    k->subsystem_vendor_id = 0xffffffff;
    k->subsystem_id = 0xffffffff;
    k->class_id = 16777215;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void pci_drivers_scsi_arcmsr_arcmsr_hba_register_types(void)
{
    static InterfaceInfo interfaces[] = {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    };
    static const TypeInfo drivers_scsi_arcmsr_arcmsr_hba_info = {
        .name          = TYPE_PCI_DRIVERS_SCSI_ARCMSR_ARCMSR_HBA_DEVICE,
        .parent        = TYPE_PCI_DEVICE,
        .instance_size = sizeof(Drivers_scsi_arcmsr_arcmsr_hbaState),
        .instance_init = drivers_scsi_arcmsr_arcmsr_hba_instance_init,
        .class_init    = drivers_scsi_arcmsr_arcmsr_hba_class_init,
        .interfaces = interfaces,
    };

    type_register_static(&drivers_scsi_arcmsr_arcmsr_hba_info);
}
type_init(pci_drivers_scsi_arcmsr_arcmsr_hba_register_types)
