#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/pci/pci.h"
#include "hw/hw.h"
#include "hw/pci/msi.h"
#include "qemu/timer.h"
#include "qemu/main-loop.h" /* iothread mutex */
#include "qemu/module.h"
#include "qapi/visitor.h"

#define DRIVERS_ATM_IPHASE_BUF_LEN 1024

extern uint64_t target_value[DRIVERS_ATM_IPHASE_BUF_LEN];
extern bool target_value_reset;

typedef struct {
    PCIDevice pdev;
    MemoryRegion mmio[7];

    uint32_t len;
    uint64_t buf[DRIVERS_ATM_IPHASE_BUF_LEN];
    int is_probe_time;
} Drivers_atm_iphaseState;

#define TYPE_PCI_DRIVERS_ATM_IPHASE_DEVICE "drivers_atm_iphase"
#define DRIVERS_ATM_IPHASE(obj)        OBJECT_CHECK(Drivers_atm_iphaseState, obj, TYPE_PCI_DRIVERS_ATM_IPHASE_DEVICE)

static uint64_t drivers_atm_iphase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    Drivers_atm_iphaseState *drivers_atm_iphase = opaque;
    int i;

    if (target_value_reset) {
        drivers_atm_iphase->len = 0;
        target_value_reset = false;
    }

    if (!drivers_atm_iphase->is_probe_time) {
        for (i = 0; i < DRIVERS_ATM_IPHASE_BUF_LEN; ++i) {
            drivers_atm_iphase->buf[i] = target_value[i];
        }
    } else {
        drivers_atm_iphase->is_probe_time--;
    }

    return drivers_atm_iphase->buf[(drivers_atm_iphase->len++) % DRIVERS_ATM_IPHASE_BUF_LEN];
}

static void drivers_atm_iphase_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                unsigned size)
{
	return;
}

static const MemoryRegionOps drivers_atm_iphase_mmio_ops = {
    .read = drivers_atm_iphase_mmio_read,
    .write = drivers_atm_iphase_mmio_write,
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

static void pci_drivers_atm_iphase_realize(PCIDevice *pdev, Error **errp)
{
    Drivers_atm_iphaseState *drivers_atm_iphase = DRIVERS_ATM_IPHASE(pdev);
    uint8_t *pci_conf = pdev->config;
	int pos;

    pci_config_set_interrupt_pin(pci_conf, 1);
	pci_set_word(&pci_conf[4], 65535);
	

    if (msi_init(pdev, 0, 1, true, false, errp)) {
        return;
    }
	
	pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
	pcie_endpoint_cap_init(pdev, 0);

	pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
	pdev->exp.pm_cap = pos;
	pci_set_word(pdev->config + pos + PCI_PM_PMC, 0x3);

    drivers_atm_iphase->len = 0;
    drivers_atm_iphase->is_probe_time = 184;
	drivers_atm_iphase->buf[0] = 0xff;
	drivers_atm_iphase->buf[1] = 0xff;
	drivers_atm_iphase->buf[2] = 0xff;
	drivers_atm_iphase->buf[3] = 0xff;
	drivers_atm_iphase->buf[4] = 0xff;
	drivers_atm_iphase->buf[5] = 0xff;
	drivers_atm_iphase->buf[6] = 0xff;
	drivers_atm_iphase->buf[7] = 0xff;
	drivers_atm_iphase->buf[8] = 0xff;
	drivers_atm_iphase->buf[9] = 0xff;
	drivers_atm_iphase->buf[10] = 0xff;
	drivers_atm_iphase->buf[11] = 0xff;
	drivers_atm_iphase->buf[12] = 0xff;
	drivers_atm_iphase->buf[13] = 0xff;
	drivers_atm_iphase->buf[14] = 0xff;
	drivers_atm_iphase->buf[15] = 0xff;
	drivers_atm_iphase->buf[16] = 0xff;
	drivers_atm_iphase->buf[17] = 0xff;
	drivers_atm_iphase->buf[18] = 0xff;
	drivers_atm_iphase->buf[19] = 0xff;
	drivers_atm_iphase->buf[20] = 0xff;
	drivers_atm_iphase->buf[21] = 0xff;
	drivers_atm_iphase->buf[22] = 0xff;
	drivers_atm_iphase->buf[23] = 0xff;
	drivers_atm_iphase->buf[24] = 0xff;
	drivers_atm_iphase->buf[25] = 0xff;
	drivers_atm_iphase->buf[26] = 0xff;
	drivers_atm_iphase->buf[27] = 0xff;
	drivers_atm_iphase->buf[28] = 0xff;
	drivers_atm_iphase->buf[29] = 0xff;
	drivers_atm_iphase->buf[30] = 0xff;
	drivers_atm_iphase->buf[31] = 0xff;
	drivers_atm_iphase->buf[32] = 0xff;
	drivers_atm_iphase->buf[33] = 0xff;
	drivers_atm_iphase->buf[34] = 0xff;
	drivers_atm_iphase->buf[35] = 0xff;
	drivers_atm_iphase->buf[36] = 0xff;
	drivers_atm_iphase->buf[37] = 0xff;
	drivers_atm_iphase->buf[38] = 0xff;
	drivers_atm_iphase->buf[39] = 0xff;
	drivers_atm_iphase->buf[40] = 0xff;
	drivers_atm_iphase->buf[41] = 0xff;
	drivers_atm_iphase->buf[42] = 0xff;
	drivers_atm_iphase->buf[43] = 0xff;
	drivers_atm_iphase->buf[44] = 0xff;
	drivers_atm_iphase->buf[45] = 0xff;
	drivers_atm_iphase->buf[46] = 0xff;
	drivers_atm_iphase->buf[47] = 0xff;
	drivers_atm_iphase->buf[48] = 0xff;
	drivers_atm_iphase->buf[49] = 0xff;
	drivers_atm_iphase->buf[50] = 0xff;
	drivers_atm_iphase->buf[51] = 0xff;
	drivers_atm_iphase->buf[52] = 0xff;
	drivers_atm_iphase->buf[53] = 0xff;
	drivers_atm_iphase->buf[54] = 0xff;
	drivers_atm_iphase->buf[55] = 0xff;
	drivers_atm_iphase->buf[56] = 0xff;
	drivers_atm_iphase->buf[57] = 0xff;
	drivers_atm_iphase->buf[58] = 0xff;
	drivers_atm_iphase->buf[59] = 0xff;
	drivers_atm_iphase->buf[60] = 0xff;
	drivers_atm_iphase->buf[61] = 0xff;
	drivers_atm_iphase->buf[62] = 0xff;
	drivers_atm_iphase->buf[63] = 0xff;
	drivers_atm_iphase->buf[64] = 0xff;
	drivers_atm_iphase->buf[65] = 0xff;
	drivers_atm_iphase->buf[66] = 0xff;
	drivers_atm_iphase->buf[67] = 0xff;
	drivers_atm_iphase->buf[68] = 0xff;
	drivers_atm_iphase->buf[69] = 0xff;
	drivers_atm_iphase->buf[70] = 0xff;
	drivers_atm_iphase->buf[71] = 0xff;
	drivers_atm_iphase->buf[72] = 0xff;
	drivers_atm_iphase->buf[73] = 0xff;
	drivers_atm_iphase->buf[74] = 0xff;
	drivers_atm_iphase->buf[75] = 0xff;
	drivers_atm_iphase->buf[76] = 0xff;
	drivers_atm_iphase->buf[77] = 0xff;
	drivers_atm_iphase->buf[78] = 0xff;
	drivers_atm_iphase->buf[79] = 0xff;
	drivers_atm_iphase->buf[80] = 0xff;
	drivers_atm_iphase->buf[81] = 0xff;
	drivers_atm_iphase->buf[82] = 0xff;
	drivers_atm_iphase->buf[83] = 0xff;
	drivers_atm_iphase->buf[84] = 0xff;
	drivers_atm_iphase->buf[85] = 0xff;
	drivers_atm_iphase->buf[86] = 0xff;
	drivers_atm_iphase->buf[87] = 0xff;
	drivers_atm_iphase->buf[88] = 0xff;
	drivers_atm_iphase->buf[89] = 0xff;
	drivers_atm_iphase->buf[90] = 0xff;
	drivers_atm_iphase->buf[91] = 0xff;
	drivers_atm_iphase->buf[92] = 0xff;
	drivers_atm_iphase->buf[93] = 0xff;
	drivers_atm_iphase->buf[94] = 0xff;
	drivers_atm_iphase->buf[95] = 0xff;
	drivers_atm_iphase->buf[96] = 0xff;
	drivers_atm_iphase->buf[97] = 0xff;
	drivers_atm_iphase->buf[98] = 0xff;
	drivers_atm_iphase->buf[99] = 0xff;
	drivers_atm_iphase->buf[100] = 0xff;
	drivers_atm_iphase->buf[101] = 0xff;
	drivers_atm_iphase->buf[102] = 0xff;
	drivers_atm_iphase->buf[103] = 0xff;
	drivers_atm_iphase->buf[104] = 0xff;
	drivers_atm_iphase->buf[105] = 0xff;
	drivers_atm_iphase->buf[106] = 0xff;
	drivers_atm_iphase->buf[107] = 0xff;
	drivers_atm_iphase->buf[108] = 0xff;
	drivers_atm_iphase->buf[109] = 0xff;
	drivers_atm_iphase->buf[110] = 0xff;
	drivers_atm_iphase->buf[111] = 0xff;
	drivers_atm_iphase->buf[112] = 0xff;
	drivers_atm_iphase->buf[113] = 0xff;
	drivers_atm_iphase->buf[114] = 0xff;
	drivers_atm_iphase->buf[115] = 0xff;
	drivers_atm_iphase->buf[116] = 0xff;
	drivers_atm_iphase->buf[117] = 0xff;
	drivers_atm_iphase->buf[118] = 0xff;
	drivers_atm_iphase->buf[119] = 0xff;
	drivers_atm_iphase->buf[120] = 0xff;
	drivers_atm_iphase->buf[121] = 0xff;
	drivers_atm_iphase->buf[122] = 0xff;
	drivers_atm_iphase->buf[123] = 0xff;
	drivers_atm_iphase->buf[124] = 0xff;
	drivers_atm_iphase->buf[125] = 0xff;
	drivers_atm_iphase->buf[126] = 0xff;
	drivers_atm_iphase->buf[127] = 0xff;
	drivers_atm_iphase->buf[128] = 0xff;
	drivers_atm_iphase->buf[129] = 0xff;
	drivers_atm_iphase->buf[130] = 0xff;
	drivers_atm_iphase->buf[131] = 0xff;
	drivers_atm_iphase->buf[132] = 0xff;
	drivers_atm_iphase->buf[133] = 0xff;
	drivers_atm_iphase->buf[134] = 0xff;
	drivers_atm_iphase->buf[135] = 0xff;
	drivers_atm_iphase->buf[136] = 0xff;
	drivers_atm_iphase->buf[137] = 0xff;
	drivers_atm_iphase->buf[138] = 0xff;
	drivers_atm_iphase->buf[139] = 0xff;
	drivers_atm_iphase->buf[140] = 0xff;
	drivers_atm_iphase->buf[141] = 0xff;
	drivers_atm_iphase->buf[142] = 0xff;
	drivers_atm_iphase->buf[143] = 0xff;
	drivers_atm_iphase->buf[144] = 0xff;
	drivers_atm_iphase->buf[145] = 0xff;
	drivers_atm_iphase->buf[146] = 0xff;
	drivers_atm_iphase->buf[147] = 0xff;
	drivers_atm_iphase->buf[148] = 0xff;
	drivers_atm_iphase->buf[149] = 0xff;
	drivers_atm_iphase->buf[150] = 0xff;
	drivers_atm_iphase->buf[151] = 0xff;
	drivers_atm_iphase->buf[152] = 0xff;
	drivers_atm_iphase->buf[153] = 0xff;
	drivers_atm_iphase->buf[154] = 0xff;
	drivers_atm_iphase->buf[155] = 0xff;
	drivers_atm_iphase->buf[156] = 0xff;
	drivers_atm_iphase->buf[157] = 0xff;
	drivers_atm_iphase->buf[158] = 0xff;
	drivers_atm_iphase->buf[159] = 0xff;
	drivers_atm_iphase->buf[160] = 0xff;
	drivers_atm_iphase->buf[161] = 0xff;
	drivers_atm_iphase->buf[162] = 0xff;
	drivers_atm_iphase->buf[163] = 0xff;
	drivers_atm_iphase->buf[164] = 0xff;
	drivers_atm_iphase->buf[165] = 0xff;
	drivers_atm_iphase->buf[166] = 0xff;
	drivers_atm_iphase->buf[167] = 0xff;
	drivers_atm_iphase->buf[168] = 0xff;
	drivers_atm_iphase->buf[169] = 0xff;
	drivers_atm_iphase->buf[170] = 0xff;
	drivers_atm_iphase->buf[171] = 0xff;
	drivers_atm_iphase->buf[172] = 0xff;
	drivers_atm_iphase->buf[173] = 0xff;
	drivers_atm_iphase->buf[174] = 0xff;
	drivers_atm_iphase->buf[175] = 0xff;
	drivers_atm_iphase->buf[176] = 0xff;
	drivers_atm_iphase->buf[177] = 0xff;
	drivers_atm_iphase->buf[178] = 0xff;
	drivers_atm_iphase->buf[179] = 0xff;
	drivers_atm_iphase->buf[180] = 0xff;
	drivers_atm_iphase->buf[181] = 0xff;
	drivers_atm_iphase->buf[182] = 0xff;
	drivers_atm_iphase->buf[183] = 0xff;;

    memory_region_init_io(&drivers_atm_iphase->mmio[0], OBJECT(drivers_atm_iphase), &drivers_atm_iphase_mmio_ops, drivers_atm_iphase,
                    "drivers_atm_iphase-mmio0", 16 * MiB);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_atm_iphase->mmio[0]);
    memory_region_init_io(&drivers_atm_iphase->mmio[1], OBJECT(drivers_atm_iphase), &drivers_atm_iphase_mmio_ops, drivers_atm_iphase,
                    "drivers_atm_iphase-mmio1", 16 * MiB);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_atm_iphase->mmio[1]);
    memory_region_init_io(&drivers_atm_iphase->mmio[2], OBJECT(drivers_atm_iphase), &drivers_atm_iphase_mmio_ops, drivers_atm_iphase,
                    "drivers_atm_iphase-mmio2", 16 * MiB);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_atm_iphase->mmio[2]);
    memory_region_init_io(&drivers_atm_iphase->mmio[3], OBJECT(drivers_atm_iphase), &drivers_atm_iphase_mmio_ops, drivers_atm_iphase,
                    "drivers_atm_iphase-mmio3", 16 * MiB);
    pci_register_bar(pdev, 3, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_atm_iphase->mmio[3]);
    memory_region_init_io(&drivers_atm_iphase->mmio[4], OBJECT(drivers_atm_iphase), &drivers_atm_iphase_mmio_ops, drivers_atm_iphase,
                    "drivers_atm_iphase-mmio4", 16 * MiB);
    pci_register_bar(pdev, 4, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_atm_iphase->mmio[4]);
    memory_region_init_io(&drivers_atm_iphase->mmio[5], OBJECT(drivers_atm_iphase), &drivers_atm_iphase_mmio_ops, drivers_atm_iphase,
                    "drivers_atm_iphase-mmio5", 16 * MiB);
    pci_register_bar(pdev, 5, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_atm_iphase->mmio[5]);
    memory_region_init_io(&drivers_atm_iphase->mmio[6], OBJECT(drivers_atm_iphase), &drivers_atm_iphase_mmio_ops, drivers_atm_iphase,
                    "drivers_atm_iphase-mmio5", 16 * MiB);
    pci_register_bar(pdev, 6, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_atm_iphase->mmio[6]);
}

static void pci_drivers_atm_iphase_uninit(PCIDevice *pdev)
{
    msi_uninit(pdev);
}

static void drivers_atm_iphase_instance_init(Object *obj)
{
	return;
}

static void drivers_atm_iphase_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(class);

    k->realize = pci_drivers_atm_iphase_realize;
    k->exit = pci_drivers_atm_iphase_uninit;
    k->vendor_id = 0x107e;
    k->device_id = 0x8;
    k->revision = 0;
    k->subsystem_vendor_id = 0xffffffff;
    k->subsystem_id = 0xffffffff;
    k->class_id = 16777215;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void pci_drivers_atm_iphase_register_types(void)
{
    static InterfaceInfo interfaces[] = {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    };
    static const TypeInfo drivers_atm_iphase_info = {
        .name          = TYPE_PCI_DRIVERS_ATM_IPHASE_DEVICE,
        .parent        = TYPE_PCI_DEVICE,
        .instance_size = sizeof(Drivers_atm_iphaseState),
        .instance_init = drivers_atm_iphase_instance_init,
        .class_init    = drivers_atm_iphase_class_init,
        .interfaces = interfaces,
    };

    type_register_static(&drivers_atm_iphase_info);
}
type_init(pci_drivers_atm_iphase_register_types)
