#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/pci/pci.h"
#include "hw/hw.h"
#include "hw/pci/msi.h"
#include "qemu/timer.h"
#include "qemu/main-loop.h" /* iothread mutex */
#include "qemu/module.h"
#include "qapi/visitor.h"

#define SOUND_PCI_AU88X0_AU8820_BUF_LEN 1024

extern uint64_t target_value[SOUND_PCI_AU88X0_AU8820_BUF_LEN];
extern bool target_value_reset;

typedef struct {
    PCIDevice pdev;
    MemoryRegion mmio[7];

    uint32_t len;
    uint64_t buf[SOUND_PCI_AU88X0_AU8820_BUF_LEN];
    int is_probe_time;
} Sound_pci_au88x0_au8820State;

#define TYPE_PCI_SOUND_PCI_AU88X0_AU8820_DEVICE "sound_pci_au88x0_au8820"
#define SOUND_PCI_AU88X0_AU8820(obj)        OBJECT_CHECK(Sound_pci_au88x0_au8820State, obj, TYPE_PCI_SOUND_PCI_AU88X0_AU8820_DEVICE)

static uint64_t sound_pci_au88x0_au8820_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    Sound_pci_au88x0_au8820State *sound_pci_au88x0_au8820 = opaque;
    int i;

    if (target_value_reset) {
        sound_pci_au88x0_au8820->len = 0;
        target_value_reset = false;
    }

    if (!sound_pci_au88x0_au8820->is_probe_time) {
        for (i = 0; i < SOUND_PCI_AU88X0_AU8820_BUF_LEN; ++i) {
            sound_pci_au88x0_au8820->buf[i] = target_value[i];
        }
    } else {
        sound_pci_au88x0_au8820->is_probe_time--;
    }

    return sound_pci_au88x0_au8820->buf[(sound_pci_au88x0_au8820->len++) % SOUND_PCI_AU88X0_AU8820_BUF_LEN];
}

static void sound_pci_au88x0_au8820_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                unsigned size)
{
	return;
}

static const MemoryRegionOps sound_pci_au88x0_au8820_mmio_ops = {
    .read = sound_pci_au88x0_au8820_mmio_read,
    .write = sound_pci_au88x0_au8820_mmio_write,
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

static void pci_sound_pci_au88x0_au8820_realize(PCIDevice *pdev, Error **errp)
{
    Sound_pci_au88x0_au8820State *sound_pci_au88x0_au8820 = SOUND_PCI_AU88X0_AU8820(pdev);
    uint8_t *pci_conf = pdev->config;
	int pos;

    pci_config_set_interrupt_pin(pci_conf, 1);
	pci_set_byte(&pci_conf[66], 255);
	

    if (msi_init(pdev, 0, 1, true, false, errp)) {
        return;
    }
	
	pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
	pcie_endpoint_cap_init(pdev, 0);

	pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
	pdev->exp.pm_cap = pos;
	pci_set_word(pdev->config + pos + PCI_PM_PMC, 0x3);

    sound_pci_au88x0_au8820->len = 0;
    sound_pci_au88x0_au8820->is_probe_time = 196;
	sound_pci_au88x0_au8820->buf[0] = 0xff;
	sound_pci_au88x0_au8820->buf[1] = 0xff;
	sound_pci_au88x0_au8820->buf[2] = 0xff;
	sound_pci_au88x0_au8820->buf[3] = 0xff;
	sound_pci_au88x0_au8820->buf[4] = 0xff;
	sound_pci_au88x0_au8820->buf[5] = 0xff;
	sound_pci_au88x0_au8820->buf[6] = 0xff;
	sound_pci_au88x0_au8820->buf[7] = 0xff;
	sound_pci_au88x0_au8820->buf[8] = 0xff;
	sound_pci_au88x0_au8820->buf[9] = 0xff;
	sound_pci_au88x0_au8820->buf[10] = 0xff;
	sound_pci_au88x0_au8820->buf[11] = 0xff;
	sound_pci_au88x0_au8820->buf[12] = 0xff;
	sound_pci_au88x0_au8820->buf[13] = 0xff;
	sound_pci_au88x0_au8820->buf[14] = 0xff;
	sound_pci_au88x0_au8820->buf[15] = 0xff;
	sound_pci_au88x0_au8820->buf[16] = 0xff;
	sound_pci_au88x0_au8820->buf[17] = 0xff;
	sound_pci_au88x0_au8820->buf[18] = 0xff;
	sound_pci_au88x0_au8820->buf[19] = 0xff;
	sound_pci_au88x0_au8820->buf[20] = 0xff;
	sound_pci_au88x0_au8820->buf[21] = 0xff;
	sound_pci_au88x0_au8820->buf[22] = 0xff;
	sound_pci_au88x0_au8820->buf[23] = 0xff;
	sound_pci_au88x0_au8820->buf[24] = 0xff;
	sound_pci_au88x0_au8820->buf[25] = 0xff;
	sound_pci_au88x0_au8820->buf[26] = 0xff;
	sound_pci_au88x0_au8820->buf[27] = 0xff;
	sound_pci_au88x0_au8820->buf[28] = 0xff;
	sound_pci_au88x0_au8820->buf[29] = 0xff;
	sound_pci_au88x0_au8820->buf[30] = 0xff;
	sound_pci_au88x0_au8820->buf[31] = 0xff;
	sound_pci_au88x0_au8820->buf[32] = 0xff;
	sound_pci_au88x0_au8820->buf[33] = 0xff;
	sound_pci_au88x0_au8820->buf[34] = 0xff;
	sound_pci_au88x0_au8820->buf[35] = 0xff;
	sound_pci_au88x0_au8820->buf[36] = 0xff;
	sound_pci_au88x0_au8820->buf[37] = 0xff;
	sound_pci_au88x0_au8820->buf[38] = 0xff;
	sound_pci_au88x0_au8820->buf[39] = 0xff;
	sound_pci_au88x0_au8820->buf[40] = 0x0;
	sound_pci_au88x0_au8820->buf[41] = 0x0;
	sound_pci_au88x0_au8820->buf[42] = 0x0;
	sound_pci_au88x0_au8820->buf[43] = 0x0;
	sound_pci_au88x0_au8820->buf[44] = 0xff;
	sound_pci_au88x0_au8820->buf[45] = 0xff;
	sound_pci_au88x0_au8820->buf[46] = 0xff;
	sound_pci_au88x0_au8820->buf[47] = 0xff;
	sound_pci_au88x0_au8820->buf[48] = 0xff;
	sound_pci_au88x0_au8820->buf[49] = 0xff;
	sound_pci_au88x0_au8820->buf[50] = 0xff;
	sound_pci_au88x0_au8820->buf[51] = 0xff;
	sound_pci_au88x0_au8820->buf[52] = 0xff;
	sound_pci_au88x0_au8820->buf[53] = 0xff;
	sound_pci_au88x0_au8820->buf[54] = 0xff;
	sound_pci_au88x0_au8820->buf[55] = 0xff;
	sound_pci_au88x0_au8820->buf[56] = 0xff;
	sound_pci_au88x0_au8820->buf[57] = 0xff;
	sound_pci_au88x0_au8820->buf[58] = 0xff;
	sound_pci_au88x0_au8820->buf[59] = 0xff;
	sound_pci_au88x0_au8820->buf[60] = 0xff;
	sound_pci_au88x0_au8820->buf[61] = 0xff;
	sound_pci_au88x0_au8820->buf[62] = 0xff;
	sound_pci_au88x0_au8820->buf[63] = 0xff;
	sound_pci_au88x0_au8820->buf[64] = 0xff;
	sound_pci_au88x0_au8820->buf[65] = 0xff;
	sound_pci_au88x0_au8820->buf[66] = 0xff;
	sound_pci_au88x0_au8820->buf[67] = 0xff;
	sound_pci_au88x0_au8820->buf[68] = 0xff;
	sound_pci_au88x0_au8820->buf[69] = 0xff;
	sound_pci_au88x0_au8820->buf[70] = 0xff;
	sound_pci_au88x0_au8820->buf[71] = 0xff;
	sound_pci_au88x0_au8820->buf[72] = 0xff;
	sound_pci_au88x0_au8820->buf[73] = 0xff;
	sound_pci_au88x0_au8820->buf[74] = 0xff;
	sound_pci_au88x0_au8820->buf[75] = 0xff;
	sound_pci_au88x0_au8820->buf[76] = 0xff;
	sound_pci_au88x0_au8820->buf[77] = 0xff;
	sound_pci_au88x0_au8820->buf[78] = 0xff;
	sound_pci_au88x0_au8820->buf[79] = 0xff;
	sound_pci_au88x0_au8820->buf[80] = 0xff;
	sound_pci_au88x0_au8820->buf[81] = 0xff;
	sound_pci_au88x0_au8820->buf[82] = 0xff;
	sound_pci_au88x0_au8820->buf[83] = 0xff;
	sound_pci_au88x0_au8820->buf[84] = 0xff;
	sound_pci_au88x0_au8820->buf[85] = 0xff;
	sound_pci_au88x0_au8820->buf[86] = 0xff;
	sound_pci_au88x0_au8820->buf[87] = 0xff;
	sound_pci_au88x0_au8820->buf[88] = 0xff;
	sound_pci_au88x0_au8820->buf[89] = 0xff;
	sound_pci_au88x0_au8820->buf[90] = 0xff;
	sound_pci_au88x0_au8820->buf[91] = 0xff;
	sound_pci_au88x0_au8820->buf[92] = 0xff;
	sound_pci_au88x0_au8820->buf[93] = 0xff;
	sound_pci_au88x0_au8820->buf[94] = 0xff;
	sound_pci_au88x0_au8820->buf[95] = 0xff;
	sound_pci_au88x0_au8820->buf[96] = 0xff;
	sound_pci_au88x0_au8820->buf[97] = 0xff;
	sound_pci_au88x0_au8820->buf[98] = 0xff;
	sound_pci_au88x0_au8820->buf[99] = 0xff;
	sound_pci_au88x0_au8820->buf[100] = 0x0;
	sound_pci_au88x0_au8820->buf[101] = 0x0;
	sound_pci_au88x0_au8820->buf[102] = 0x0;
	sound_pci_au88x0_au8820->buf[103] = 0x7f;
	sound_pci_au88x0_au8820->buf[104] = 0xff;
	sound_pci_au88x0_au8820->buf[105] = 0xff;
	sound_pci_au88x0_au8820->buf[106] = 0xff;
	sound_pci_au88x0_au8820->buf[107] = 0xff;
	sound_pci_au88x0_au8820->buf[108] = 0xff;
	sound_pci_au88x0_au8820->buf[109] = 0xff;
	sound_pci_au88x0_au8820->buf[110] = 0xff;
	sound_pci_au88x0_au8820->buf[111] = 0xff;
	sound_pci_au88x0_au8820->buf[112] = 0x0;
	sound_pci_au88x0_au8820->buf[113] = 0x0;
	sound_pci_au88x0_au8820->buf[114] = 0x0;
	sound_pci_au88x0_au8820->buf[115] = 0x7f;
	sound_pci_au88x0_au8820->buf[116] = 0x0;
	sound_pci_au88x0_au8820->buf[117] = 0x0;
	sound_pci_au88x0_au8820->buf[118] = 0x0;
	sound_pci_au88x0_au8820->buf[119] = 0x7f;
	sound_pci_au88x0_au8820->buf[120] = 0xff;
	sound_pci_au88x0_au8820->buf[121] = 0xff;
	sound_pci_au88x0_au8820->buf[122] = 0xff;
	sound_pci_au88x0_au8820->buf[123] = 0xff;
	sound_pci_au88x0_au8820->buf[124] = 0xff;
	sound_pci_au88x0_au8820->buf[125] = 0xff;
	sound_pci_au88x0_au8820->buf[126] = 0xff;
	sound_pci_au88x0_au8820->buf[127] = 0xff;
	sound_pci_au88x0_au8820->buf[128] = 0x0;
	sound_pci_au88x0_au8820->buf[129] = 0x0;
	sound_pci_au88x0_au8820->buf[130] = 0x0;
	sound_pci_au88x0_au8820->buf[131] = 0x0;
	sound_pci_au88x0_au8820->buf[132] = 0x0;
	sound_pci_au88x0_au8820->buf[133] = 0x0;
	sound_pci_au88x0_au8820->buf[134] = 0x0;
	sound_pci_au88x0_au8820->buf[135] = 0x0;
	sound_pci_au88x0_au8820->buf[136] = 0x0;
	sound_pci_au88x0_au8820->buf[137] = 0x0;
	sound_pci_au88x0_au8820->buf[138] = 0x0;
	sound_pci_au88x0_au8820->buf[139] = 0x0;
	sound_pci_au88x0_au8820->buf[140] = 0x0;
	sound_pci_au88x0_au8820->buf[141] = 0x0;
	sound_pci_au88x0_au8820->buf[142] = 0x0;
	sound_pci_au88x0_au8820->buf[143] = 0x0;
	sound_pci_au88x0_au8820->buf[144] = 0xff;
	sound_pci_au88x0_au8820->buf[145] = 0xff;
	sound_pci_au88x0_au8820->buf[146] = 0xff;
	sound_pci_au88x0_au8820->buf[147] = 0xff;
	sound_pci_au88x0_au8820->buf[148] = 0xff;
	sound_pci_au88x0_au8820->buf[149] = 0xff;
	sound_pci_au88x0_au8820->buf[150] = 0xff;
	sound_pci_au88x0_au8820->buf[151] = 0xff;
	sound_pci_au88x0_au8820->buf[152] = 0xff;
	sound_pci_au88x0_au8820->buf[153] = 0xff;
	sound_pci_au88x0_au8820->buf[154] = 0xff;
	sound_pci_au88x0_au8820->buf[155] = 0xff;
	sound_pci_au88x0_au8820->buf[156] = 0x0;
	sound_pci_au88x0_au8820->buf[157] = 0x0;
	sound_pci_au88x0_au8820->buf[158] = 0x0;
	sound_pci_au88x0_au8820->buf[159] = 0x0;
	sound_pci_au88x0_au8820->buf[160] = 0x0;
	sound_pci_au88x0_au8820->buf[161] = 0x0;
	sound_pci_au88x0_au8820->buf[162] = 0x0;
	sound_pci_au88x0_au8820->buf[163] = 0x0;
	sound_pci_au88x0_au8820->buf[164] = 0x0;
	sound_pci_au88x0_au8820->buf[165] = 0x0;
	sound_pci_au88x0_au8820->buf[166] = 0x0;
	sound_pci_au88x0_au8820->buf[167] = 0x0;
	sound_pci_au88x0_au8820->buf[168] = 0x0;
	sound_pci_au88x0_au8820->buf[169] = 0x0;
	sound_pci_au88x0_au8820->buf[170] = 0x0;
	sound_pci_au88x0_au8820->buf[171] = 0x0;
	sound_pci_au88x0_au8820->buf[172] = 0xff;
	sound_pci_au88x0_au8820->buf[173] = 0xff;
	sound_pci_au88x0_au8820->buf[174] = 0xff;
	sound_pci_au88x0_au8820->buf[175] = 0xff;
	sound_pci_au88x0_au8820->buf[176] = 0x0;
	sound_pci_au88x0_au8820->buf[177] = 0x0;
	sound_pci_au88x0_au8820->buf[178] = 0x0;
	sound_pci_au88x0_au8820->buf[179] = 0x80;
	sound_pci_au88x0_au8820->buf[180] = 0x0;
	sound_pci_au88x0_au8820->buf[181] = 0x0;
	sound_pci_au88x0_au8820->buf[182] = 0x0;
	sound_pci_au88x0_au8820->buf[183] = 0x80;
	sound_pci_au88x0_au8820->buf[184] = 0xff;
	sound_pci_au88x0_au8820->buf[185] = 0xff;
	sound_pci_au88x0_au8820->buf[186] = 0xff;
	sound_pci_au88x0_au8820->buf[187] = 0xff;
	sound_pci_au88x0_au8820->buf[188] = 0xff;
	sound_pci_au88x0_au8820->buf[189] = 0xff;
	sound_pci_au88x0_au8820->buf[190] = 0xff;
	sound_pci_au88x0_au8820->buf[191] = 0xff;
	sound_pci_au88x0_au8820->buf[192] = 0xff;
	sound_pci_au88x0_au8820->buf[193] = 0xff;
	sound_pci_au88x0_au8820->buf[194] = 0xff;
	sound_pci_au88x0_au8820->buf[195] = 0xff;;

    memory_region_init_io(&sound_pci_au88x0_au8820->mmio[0], OBJECT(sound_pci_au88x0_au8820), &sound_pci_au88x0_au8820_mmio_ops, sound_pci_au88x0_au8820,
                    "sound_pci_au88x0_au8820-mmio0", 16 * MiB);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &sound_pci_au88x0_au8820->mmio[0]);
    memory_region_init_io(&sound_pci_au88x0_au8820->mmio[1], OBJECT(sound_pci_au88x0_au8820), &sound_pci_au88x0_au8820_mmio_ops, sound_pci_au88x0_au8820,
                    "sound_pci_au88x0_au8820-mmio1", 16 * MiB);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &sound_pci_au88x0_au8820->mmio[1]);
    memory_region_init_io(&sound_pci_au88x0_au8820->mmio[2], OBJECT(sound_pci_au88x0_au8820), &sound_pci_au88x0_au8820_mmio_ops, sound_pci_au88x0_au8820,
                    "sound_pci_au88x0_au8820-mmio2", 16 * MiB);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &sound_pci_au88x0_au8820->mmio[2]);
    memory_region_init_io(&sound_pci_au88x0_au8820->mmio[3], OBJECT(sound_pci_au88x0_au8820), &sound_pci_au88x0_au8820_mmio_ops, sound_pci_au88x0_au8820,
                    "sound_pci_au88x0_au8820-mmio3", 16 * MiB);
    pci_register_bar(pdev, 3, PCI_BASE_ADDRESS_SPACE_MEMORY, &sound_pci_au88x0_au8820->mmio[3]);
    memory_region_init_io(&sound_pci_au88x0_au8820->mmio[4], OBJECT(sound_pci_au88x0_au8820), &sound_pci_au88x0_au8820_mmio_ops, sound_pci_au88x0_au8820,
                    "sound_pci_au88x0_au8820-mmio4", 16 * MiB);
    pci_register_bar(pdev, 4, PCI_BASE_ADDRESS_SPACE_MEMORY, &sound_pci_au88x0_au8820->mmio[4]);
    memory_region_init_io(&sound_pci_au88x0_au8820->mmio[5], OBJECT(sound_pci_au88x0_au8820), &sound_pci_au88x0_au8820_mmio_ops, sound_pci_au88x0_au8820,
                    "sound_pci_au88x0_au8820-mmio5", 16 * MiB);
    pci_register_bar(pdev, 5, PCI_BASE_ADDRESS_SPACE_MEMORY, &sound_pci_au88x0_au8820->mmio[5]);
    memory_region_init_io(&sound_pci_au88x0_au8820->mmio[6], OBJECT(sound_pci_au88x0_au8820), &sound_pci_au88x0_au8820_mmio_ops, sound_pci_au88x0_au8820,
                    "sound_pci_au88x0_au8820-mmio5", 16 * MiB);
    pci_register_bar(pdev, 6, PCI_BASE_ADDRESS_SPACE_MEMORY, &sound_pci_au88x0_au8820->mmio[6]);
}

static void pci_sound_pci_au88x0_au8820_uninit(PCIDevice *pdev)
{
    msi_uninit(pdev);
}

static void sound_pci_au88x0_au8820_instance_init(Object *obj)
{
	return;
}

static void sound_pci_au88x0_au8820_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(class);

    k->realize = pci_sound_pci_au88x0_au8820_realize;
    k->exit = pci_sound_pci_au88x0_au8820_uninit;
    k->vendor_id = 0x12eb;
    k->device_id = 0x1;
    k->revision = 0;
    k->subsystem_vendor_id = 0xffffffff;
    k->subsystem_id = 0xffffffff;
    k->class_id = 16777215;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void pci_sound_pci_au88x0_au8820_register_types(void)
{
    static InterfaceInfo interfaces[] = {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    };
    static const TypeInfo sound_pci_au88x0_au8820_info = {
        .name          = TYPE_PCI_SOUND_PCI_AU88X0_AU8820_DEVICE,
        .parent        = TYPE_PCI_DEVICE,
        .instance_size = sizeof(Sound_pci_au88x0_au8820State),
        .instance_init = sound_pci_au88x0_au8820_instance_init,
        .class_init    = sound_pci_au88x0_au8820_class_init,
        .interfaces = interfaces,
    };

    type_register_static(&sound_pci_au88x0_au8820_info);
}
type_init(pci_sound_pci_au88x0_au8820_register_types)
