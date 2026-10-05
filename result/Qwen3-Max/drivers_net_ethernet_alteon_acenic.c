#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/pci/pci.h"
#include "hw/hw.h"
#include "hw/pci/msi.h"
#include "qemu/timer.h"
#include "qemu/main-loop.h" /* iothread mutex */
#include "qemu/module.h"
#include "qapi/visitor.h"

#define DRIVERS_NET_ETHERNET_ALTEON_ACENIC_BUF_LEN 1024

extern uint64_t target_value[DRIVERS_NET_ETHERNET_ALTEON_ACENIC_BUF_LEN];
extern bool target_value_reset;

typedef struct {
    PCIDevice pdev;
    MemoryRegion mmio[7];

    uint32_t len;
    uint64_t buf[DRIVERS_NET_ETHERNET_ALTEON_ACENIC_BUF_LEN];
    int is_probe_time;
} Drivers_net_ethernet_alteon_acenicState;

#define TYPE_PCI_DRIVERS_NET_ETHERNET_ALTEON_ACENIC_DEVICE "drivers_net_ethernet_alteon_acenic"
#define DRIVERS_NET_ETHERNET_ALTEON_ACENIC(obj)        OBJECT_CHECK(Drivers_net_ethernet_alteon_acenicState, obj, TYPE_PCI_DRIVERS_NET_ETHERNET_ALTEON_ACENIC_DEVICE)

static uint64_t drivers_net_ethernet_alteon_acenic_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    Drivers_net_ethernet_alteon_acenicState *drivers_net_ethernet_alteon_acenic = opaque;
    int i;

    if (target_value_reset) {
        drivers_net_ethernet_alteon_acenic->len = 0;
        target_value_reset = false;
    }

    if (!drivers_net_ethernet_alteon_acenic->is_probe_time) {
        for (i = 0; i < DRIVERS_NET_ETHERNET_ALTEON_ACENIC_BUF_LEN; ++i) {
            drivers_net_ethernet_alteon_acenic->buf[i] = target_value[i];
        }
    } else {
        drivers_net_ethernet_alteon_acenic->is_probe_time--;
    }

    return drivers_net_ethernet_alteon_acenic->buf[(drivers_net_ethernet_alteon_acenic->len++) % DRIVERS_NET_ETHERNET_ALTEON_ACENIC_BUF_LEN];
}

static void drivers_net_ethernet_alteon_acenic_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                unsigned size)
{
	return;
}

static const MemoryRegionOps drivers_net_ethernet_alteon_acenic_mmio_ops = {
    .read = drivers_net_ethernet_alteon_acenic_mmio_read,
    .write = drivers_net_ethernet_alteon_acenic_mmio_write,
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

static void pci_drivers_net_ethernet_alteon_acenic_realize(PCIDevice *pdev, Error **errp)
{
    Drivers_net_ethernet_alteon_acenicState *drivers_net_ethernet_alteon_acenic = DRIVERS_NET_ETHERNET_ALTEON_ACENIC(pdev);
    uint8_t *pci_conf = pdev->config;
	int pos;

    pci_config_set_interrupt_pin(pci_conf, 1);
	pci_set_word(&pci_conf[4], 65535);
	pci_set_byte(&pci_conf[12], 255);
	pci_set_byte(&pci_conf[13], 255);
	

    if (msi_init(pdev, 0, 1, true, false, errp)) {
        return;
    }
	
	pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
	pcie_endpoint_cap_init(pdev, 0);

	pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
	pdev->exp.pm_cap = pos;
	pci_set_word(pdev->config + pos + PCI_PM_PMC, 0x3);

    drivers_net_ethernet_alteon_acenic->len = 0;
    drivers_net_ethernet_alteon_acenic->is_probe_time = 204;
	drivers_net_ethernet_alteon_acenic->buf[0] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[1] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[2] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[3] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[4] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[5] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[6] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[7] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[8] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[9] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[10] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[11] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[12] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[13] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[14] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[15] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[16] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[17] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[18] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[19] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[20] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[21] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[22] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[23] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[24] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[25] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[26] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[27] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[28] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[29] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[30] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[31] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[32] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[33] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[34] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[35] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[36] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[37] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[38] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[39] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[40] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[41] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[42] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[43] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[44] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[45] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[46] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[47] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[48] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[49] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[50] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[51] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[52] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[53] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[54] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[55] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[56] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[57] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[58] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[59] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[60] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[61] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[62] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[63] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[64] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[65] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[66] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[67] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[68] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[69] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[70] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[71] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[72] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[73] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[74] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[75] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[76] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[77] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[78] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[79] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[80] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[81] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[82] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[83] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[84] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[85] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[86] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[87] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[88] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[89] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[90] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[91] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[92] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[93] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[94] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[95] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[96] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[97] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[98] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[99] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[100] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[101] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[102] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[103] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[104] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[105] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[106] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[107] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[108] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[109] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[110] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[111] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[112] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[113] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[114] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[115] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[116] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[117] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[118] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[119] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[120] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[121] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[122] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[123] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[124] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[125] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[126] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[127] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[128] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[129] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[130] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[131] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[132] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[133] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[134] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[135] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[136] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[137] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[138] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[139] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[140] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[141] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[142] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[143] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[144] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[145] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[146] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[147] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[148] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[149] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[150] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[151] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[152] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[153] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[154] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[155] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[156] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[157] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[158] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[159] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[160] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[161] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[162] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[163] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[164] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[165] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[166] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[167] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[168] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[169] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[170] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[171] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[172] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[173] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[174] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[175] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[176] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[177] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[178] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[179] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[180] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[181] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[182] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[183] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[184] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[185] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[186] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[187] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[188] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[189] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[190] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[191] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[192] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[193] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[194] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[195] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[196] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[197] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[198] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[199] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[200] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[201] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[202] = 0xff;
	drivers_net_ethernet_alteon_acenic->buf[203] = 0xff;;

    memory_region_init_io(&drivers_net_ethernet_alteon_acenic->mmio[0], OBJECT(drivers_net_ethernet_alteon_acenic), &drivers_net_ethernet_alteon_acenic_mmio_ops, drivers_net_ethernet_alteon_acenic,
                    "drivers_net_ethernet_alteon_acenic-mmio0", 16 * MiB);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_net_ethernet_alteon_acenic->mmio[0]);
    memory_region_init_io(&drivers_net_ethernet_alteon_acenic->mmio[1], OBJECT(drivers_net_ethernet_alteon_acenic), &drivers_net_ethernet_alteon_acenic_mmio_ops, drivers_net_ethernet_alteon_acenic,
                    "drivers_net_ethernet_alteon_acenic-mmio1", 16 * MiB);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_net_ethernet_alteon_acenic->mmio[1]);
    memory_region_init_io(&drivers_net_ethernet_alteon_acenic->mmio[2], OBJECT(drivers_net_ethernet_alteon_acenic), &drivers_net_ethernet_alteon_acenic_mmio_ops, drivers_net_ethernet_alteon_acenic,
                    "drivers_net_ethernet_alteon_acenic-mmio2", 16 * MiB);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_net_ethernet_alteon_acenic->mmio[2]);
    memory_region_init_io(&drivers_net_ethernet_alteon_acenic->mmio[3], OBJECT(drivers_net_ethernet_alteon_acenic), &drivers_net_ethernet_alteon_acenic_mmio_ops, drivers_net_ethernet_alteon_acenic,
                    "drivers_net_ethernet_alteon_acenic-mmio3", 16 * MiB);
    pci_register_bar(pdev, 3, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_net_ethernet_alteon_acenic->mmio[3]);
    memory_region_init_io(&drivers_net_ethernet_alteon_acenic->mmio[4], OBJECT(drivers_net_ethernet_alteon_acenic), &drivers_net_ethernet_alteon_acenic_mmio_ops, drivers_net_ethernet_alteon_acenic,
                    "drivers_net_ethernet_alteon_acenic-mmio4", 16 * MiB);
    pci_register_bar(pdev, 4, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_net_ethernet_alteon_acenic->mmio[4]);
    memory_region_init_io(&drivers_net_ethernet_alteon_acenic->mmio[5], OBJECT(drivers_net_ethernet_alteon_acenic), &drivers_net_ethernet_alteon_acenic_mmio_ops, drivers_net_ethernet_alteon_acenic,
                    "drivers_net_ethernet_alteon_acenic-mmio5", 16 * MiB);
    pci_register_bar(pdev, 5, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_net_ethernet_alteon_acenic->mmio[5]);
    memory_region_init_io(&drivers_net_ethernet_alteon_acenic->mmio[6], OBJECT(drivers_net_ethernet_alteon_acenic), &drivers_net_ethernet_alteon_acenic_mmio_ops, drivers_net_ethernet_alteon_acenic,
                    "drivers_net_ethernet_alteon_acenic-mmio5", 16 * MiB);
    pci_register_bar(pdev, 6, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_net_ethernet_alteon_acenic->mmio[6]);
}

static void pci_drivers_net_ethernet_alteon_acenic_uninit(PCIDevice *pdev)
{
    msi_uninit(pdev);
}

static void drivers_net_ethernet_alteon_acenic_instance_init(Object *obj)
{
	return;
}

static void drivers_net_ethernet_alteon_acenic_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(class);

    k->realize = pci_drivers_net_ethernet_alteon_acenic_realize;
    k->exit = pci_drivers_net_ethernet_alteon_acenic_uninit;
    k->vendor_id = 0x12ae;
    k->device_id = 0x1;
    k->revision = 0;
    k->subsystem_vendor_id = 0xffffffff;
    k->subsystem_id = 0xffffffff;
    k->class_id = 16712192;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void pci_drivers_net_ethernet_alteon_acenic_register_types(void)
{
    static InterfaceInfo interfaces[] = {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    };
    static const TypeInfo drivers_net_ethernet_alteon_acenic_info = {
        .name          = TYPE_PCI_DRIVERS_NET_ETHERNET_ALTEON_ACENIC_DEVICE,
        .parent        = TYPE_PCI_DEVICE,
        .instance_size = sizeof(Drivers_net_ethernet_alteon_acenicState),
        .instance_init = drivers_net_ethernet_alteon_acenic_instance_init,
        .class_init    = drivers_net_ethernet_alteon_acenic_class_init,
        .interfaces = interfaces,
    };

    type_register_static(&drivers_net_ethernet_alteon_acenic_info);
}
type_init(pci_drivers_net_ethernet_alteon_acenic_register_types)
