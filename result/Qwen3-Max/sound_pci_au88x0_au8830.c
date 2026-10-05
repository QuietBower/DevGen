#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/pci/pci.h"
#include "hw/hw.h"
#include "hw/pci/msi.h"
#include "qemu/timer.h"
#include "qemu/main-loop.h" /* iothread mutex */
#include "qemu/module.h"
#include "qapi/visitor.h"

#define SOUND_PCI_AU88X0_AU8830_BUF_LEN 1024

extern uint64_t target_value[SOUND_PCI_AU88X0_AU8830_BUF_LEN];
extern bool target_value_reset;

typedef struct {
    PCIDevice pdev;
    MemoryRegion mmio[7];

    uint32_t len;
    uint64_t buf[SOUND_PCI_AU88X0_AU8830_BUF_LEN];
    int is_probe_time;
} Sound_pci_au88x0_au8830State;

#define TYPE_PCI_SOUND_PCI_AU88X0_AU8830_DEVICE "sound_pci_au88x0_au8830"
#define SOUND_PCI_AU88X0_AU8830(obj)        OBJECT_CHECK(Sound_pci_au88x0_au8830State, obj, TYPE_PCI_SOUND_PCI_AU88X0_AU8830_DEVICE)

static uint64_t sound_pci_au88x0_au8830_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    Sound_pci_au88x0_au8830State *sound_pci_au88x0_au8830 = opaque;
    int i;

    if (target_value_reset) {
        sound_pci_au88x0_au8830->len = 0;
        target_value_reset = false;
    }

    if (!sound_pci_au88x0_au8830->is_probe_time) {
        for (i = 0; i < SOUND_PCI_AU88X0_AU8830_BUF_LEN; ++i) {
            sound_pci_au88x0_au8830->buf[i] = target_value[i];
        }
    } else {
        sound_pci_au88x0_au8830->is_probe_time--;
    }

    return sound_pci_au88x0_au8830->buf[(sound_pci_au88x0_au8830->len++) % SOUND_PCI_AU88X0_AU8830_BUF_LEN];
}

static void sound_pci_au88x0_au8830_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                unsigned size)
{
	return;
}

static const MemoryRegionOps sound_pci_au88x0_au8830_mmio_ops = {
    .read = sound_pci_au88x0_au8830_mmio_read,
    .write = sound_pci_au88x0_au8830_mmio_write,
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

static void pci_sound_pci_au88x0_au8830_realize(PCIDevice *pdev, Error **errp)
{
    Sound_pci_au88x0_au8830State *sound_pci_au88x0_au8830 = SOUND_PCI_AU88X0_AU8830(pdev);
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

    sound_pci_au88x0_au8830->len = 0;
    sound_pci_au88x0_au8830->is_probe_time = 224;
	sound_pci_au88x0_au8830->buf[0] = 0xff;
	sound_pci_au88x0_au8830->buf[1] = 0xff;
	sound_pci_au88x0_au8830->buf[2] = 0xff;
	sound_pci_au88x0_au8830->buf[3] = 0xff;
	sound_pci_au88x0_au8830->buf[4] = 0xff;
	sound_pci_au88x0_au8830->buf[5] = 0xff;
	sound_pci_au88x0_au8830->buf[6] = 0xff;
	sound_pci_au88x0_au8830->buf[7] = 0xff;
	sound_pci_au88x0_au8830->buf[8] = 0xff;
	sound_pci_au88x0_au8830->buf[9] = 0xff;
	sound_pci_au88x0_au8830->buf[10] = 0xff;
	sound_pci_au88x0_au8830->buf[11] = 0xff;
	sound_pci_au88x0_au8830->buf[12] = 0xff;
	sound_pci_au88x0_au8830->buf[13] = 0xff;
	sound_pci_au88x0_au8830->buf[14] = 0xff;
	sound_pci_au88x0_au8830->buf[15] = 0xff;
	sound_pci_au88x0_au8830->buf[16] = 0xff;
	sound_pci_au88x0_au8830->buf[17] = 0xff;
	sound_pci_au88x0_au8830->buf[18] = 0xff;
	sound_pci_au88x0_au8830->buf[19] = 0xff;
	sound_pci_au88x0_au8830->buf[20] = 0xff;
	sound_pci_au88x0_au8830->buf[21] = 0xff;
	sound_pci_au88x0_au8830->buf[22] = 0xff;
	sound_pci_au88x0_au8830->buf[23] = 0xff;
	sound_pci_au88x0_au8830->buf[24] = 0xff;
	sound_pci_au88x0_au8830->buf[25] = 0xff;
	sound_pci_au88x0_au8830->buf[26] = 0xff;
	sound_pci_au88x0_au8830->buf[27] = 0xff;
	sound_pci_au88x0_au8830->buf[28] = 0xff;
	sound_pci_au88x0_au8830->buf[29] = 0xff;
	sound_pci_au88x0_au8830->buf[30] = 0xff;
	sound_pci_au88x0_au8830->buf[31] = 0xff;
	sound_pci_au88x0_au8830->buf[32] = 0xff;
	sound_pci_au88x0_au8830->buf[33] = 0xff;
	sound_pci_au88x0_au8830->buf[34] = 0xff;
	sound_pci_au88x0_au8830->buf[35] = 0xff;
	sound_pci_au88x0_au8830->buf[36] = 0xff;
	sound_pci_au88x0_au8830->buf[37] = 0xff;
	sound_pci_au88x0_au8830->buf[38] = 0xff;
	sound_pci_au88x0_au8830->buf[39] = 0xff;
	sound_pci_au88x0_au8830->buf[40] = 0xff;
	sound_pci_au88x0_au8830->buf[41] = 0xff;
	sound_pci_au88x0_au8830->buf[42] = 0xff;
	sound_pci_au88x0_au8830->buf[43] = 0xff;
	sound_pci_au88x0_au8830->buf[44] = 0xff;
	sound_pci_au88x0_au8830->buf[45] = 0xff;
	sound_pci_au88x0_au8830->buf[46] = 0xff;
	sound_pci_au88x0_au8830->buf[47] = 0xff;
	sound_pci_au88x0_au8830->buf[48] = 0xff;
	sound_pci_au88x0_au8830->buf[49] = 0xff;
	sound_pci_au88x0_au8830->buf[50] = 0xff;
	sound_pci_au88x0_au8830->buf[51] = 0xff;
	sound_pci_au88x0_au8830->buf[52] = 0xff;
	sound_pci_au88x0_au8830->buf[53] = 0xff;
	sound_pci_au88x0_au8830->buf[54] = 0xff;
	sound_pci_au88x0_au8830->buf[55] = 0xff;
	sound_pci_au88x0_au8830->buf[56] = 0xff;
	sound_pci_au88x0_au8830->buf[57] = 0xff;
	sound_pci_au88x0_au8830->buf[58] = 0xff;
	sound_pci_au88x0_au8830->buf[59] = 0xff;
	sound_pci_au88x0_au8830->buf[60] = 0xff;
	sound_pci_au88x0_au8830->buf[61] = 0xff;
	sound_pci_au88x0_au8830->buf[62] = 0xff;
	sound_pci_au88x0_au8830->buf[63] = 0xff;
	sound_pci_au88x0_au8830->buf[64] = 0xff;
	sound_pci_au88x0_au8830->buf[65] = 0xff;
	sound_pci_au88x0_au8830->buf[66] = 0xff;
	sound_pci_au88x0_au8830->buf[67] = 0xff;
	sound_pci_au88x0_au8830->buf[68] = 0x0;
	sound_pci_au88x0_au8830->buf[69] = 0x0;
	sound_pci_au88x0_au8830->buf[70] = 0x0;
	sound_pci_au88x0_au8830->buf[71] = 0x0;
	sound_pci_au88x0_au8830->buf[72] = 0xff;
	sound_pci_au88x0_au8830->buf[73] = 0xff;
	sound_pci_au88x0_au8830->buf[74] = 0xff;
	sound_pci_au88x0_au8830->buf[75] = 0xff;
	sound_pci_au88x0_au8830->buf[76] = 0xff;
	sound_pci_au88x0_au8830->buf[77] = 0xff;
	sound_pci_au88x0_au8830->buf[78] = 0xff;
	sound_pci_au88x0_au8830->buf[79] = 0xff;
	sound_pci_au88x0_au8830->buf[80] = 0xff;
	sound_pci_au88x0_au8830->buf[81] = 0xff;
	sound_pci_au88x0_au8830->buf[82] = 0xff;
	sound_pci_au88x0_au8830->buf[83] = 0xff;
	sound_pci_au88x0_au8830->buf[84] = 0xff;
	sound_pci_au88x0_au8830->buf[85] = 0xff;
	sound_pci_au88x0_au8830->buf[86] = 0xff;
	sound_pci_au88x0_au8830->buf[87] = 0xff;
	sound_pci_au88x0_au8830->buf[88] = 0xff;
	sound_pci_au88x0_au8830->buf[89] = 0xff;
	sound_pci_au88x0_au8830->buf[90] = 0xff;
	sound_pci_au88x0_au8830->buf[91] = 0xff;
	sound_pci_au88x0_au8830->buf[92] = 0xff;
	sound_pci_au88x0_au8830->buf[93] = 0xff;
	sound_pci_au88x0_au8830->buf[94] = 0xff;
	sound_pci_au88x0_au8830->buf[95] = 0xff;
	sound_pci_au88x0_au8830->buf[96] = 0xff;
	sound_pci_au88x0_au8830->buf[97] = 0xff;
	sound_pci_au88x0_au8830->buf[98] = 0xff;
	sound_pci_au88x0_au8830->buf[99] = 0xff;
	sound_pci_au88x0_au8830->buf[100] = 0xff;
	sound_pci_au88x0_au8830->buf[101] = 0xff;
	sound_pci_au88x0_au8830->buf[102] = 0xff;
	sound_pci_au88x0_au8830->buf[103] = 0xff;
	sound_pci_au88x0_au8830->buf[104] = 0xff;
	sound_pci_au88x0_au8830->buf[105] = 0xff;
	sound_pci_au88x0_au8830->buf[106] = 0xff;
	sound_pci_au88x0_au8830->buf[107] = 0xff;
	sound_pci_au88x0_au8830->buf[108] = 0xff;
	sound_pci_au88x0_au8830->buf[109] = 0xff;
	sound_pci_au88x0_au8830->buf[110] = 0xff;
	sound_pci_au88x0_au8830->buf[111] = 0xff;
	sound_pci_au88x0_au8830->buf[112] = 0xff;
	sound_pci_au88x0_au8830->buf[113] = 0xff;
	sound_pci_au88x0_au8830->buf[114] = 0xff;
	sound_pci_au88x0_au8830->buf[115] = 0xff;
	sound_pci_au88x0_au8830->buf[116] = 0xff;
	sound_pci_au88x0_au8830->buf[117] = 0xff;
	sound_pci_au88x0_au8830->buf[118] = 0xff;
	sound_pci_au88x0_au8830->buf[119] = 0xff;
	sound_pci_au88x0_au8830->buf[120] = 0xff;
	sound_pci_au88x0_au8830->buf[121] = 0xff;
	sound_pci_au88x0_au8830->buf[122] = 0xff;
	sound_pci_au88x0_au8830->buf[123] = 0xff;
	sound_pci_au88x0_au8830->buf[124] = 0xff;
	sound_pci_au88x0_au8830->buf[125] = 0xff;
	sound_pci_au88x0_au8830->buf[126] = 0xff;
	sound_pci_au88x0_au8830->buf[127] = 0xff;
	sound_pci_au88x0_au8830->buf[128] = 0x0;
	sound_pci_au88x0_au8830->buf[129] = 0x0;
	sound_pci_au88x0_au8830->buf[130] = 0x0;
	sound_pci_au88x0_au8830->buf[131] = 0xff;
	sound_pci_au88x0_au8830->buf[132] = 0xff;
	sound_pci_au88x0_au8830->buf[133] = 0xff;
	sound_pci_au88x0_au8830->buf[134] = 0xff;
	sound_pci_au88x0_au8830->buf[135] = 0xff;
	sound_pci_au88x0_au8830->buf[136] = 0xff;
	sound_pci_au88x0_au8830->buf[137] = 0xff;
	sound_pci_au88x0_au8830->buf[138] = 0xff;
	sound_pci_au88x0_au8830->buf[139] = 0xff;
	sound_pci_au88x0_au8830->buf[140] = 0x0;
	sound_pci_au88x0_au8830->buf[141] = 0x0;
	sound_pci_au88x0_au8830->buf[142] = 0x0;
	sound_pci_au88x0_au8830->buf[143] = 0xff;
	sound_pci_au88x0_au8830->buf[144] = 0x0;
	sound_pci_au88x0_au8830->buf[145] = 0x0;
	sound_pci_au88x0_au8830->buf[146] = 0x0;
	sound_pci_au88x0_au8830->buf[147] = 0xff;
	sound_pci_au88x0_au8830->buf[148] = 0xff;
	sound_pci_au88x0_au8830->buf[149] = 0xff;
	sound_pci_au88x0_au8830->buf[150] = 0xff;
	sound_pci_au88x0_au8830->buf[151] = 0xff;
	sound_pci_au88x0_au8830->buf[152] = 0xff;
	sound_pci_au88x0_au8830->buf[153] = 0xff;
	sound_pci_au88x0_au8830->buf[154] = 0xff;
	sound_pci_au88x0_au8830->buf[155] = 0xff;
	sound_pci_au88x0_au8830->buf[156] = 0x0;
	sound_pci_au88x0_au8830->buf[157] = 0x0;
	sound_pci_au88x0_au8830->buf[158] = 0x0;
	sound_pci_au88x0_au8830->buf[159] = 0x0;
	sound_pci_au88x0_au8830->buf[160] = 0x0;
	sound_pci_au88x0_au8830->buf[161] = 0x0;
	sound_pci_au88x0_au8830->buf[162] = 0x0;
	sound_pci_au88x0_au8830->buf[163] = 0x0;
	sound_pci_au88x0_au8830->buf[164] = 0x0;
	sound_pci_au88x0_au8830->buf[165] = 0x0;
	sound_pci_au88x0_au8830->buf[166] = 0x0;
	sound_pci_au88x0_au8830->buf[167] = 0x0;
	sound_pci_au88x0_au8830->buf[168] = 0x0;
	sound_pci_au88x0_au8830->buf[169] = 0x0;
	sound_pci_au88x0_au8830->buf[170] = 0x0;
	sound_pci_au88x0_au8830->buf[171] = 0x0;
	sound_pci_au88x0_au8830->buf[172] = 0xff;
	sound_pci_au88x0_au8830->buf[173] = 0xff;
	sound_pci_au88x0_au8830->buf[174] = 0xff;
	sound_pci_au88x0_au8830->buf[175] = 0xff;
	sound_pci_au88x0_au8830->buf[176] = 0xff;
	sound_pci_au88x0_au8830->buf[177] = 0xff;
	sound_pci_au88x0_au8830->buf[178] = 0xff;
	sound_pci_au88x0_au8830->buf[179] = 0xff;
	sound_pci_au88x0_au8830->buf[180] = 0xff;
	sound_pci_au88x0_au8830->buf[181] = 0xff;
	sound_pci_au88x0_au8830->buf[182] = 0xff;
	sound_pci_au88x0_au8830->buf[183] = 0xff;
	sound_pci_au88x0_au8830->buf[184] = 0x0;
	sound_pci_au88x0_au8830->buf[185] = 0x0;
	sound_pci_au88x0_au8830->buf[186] = 0x0;
	sound_pci_au88x0_au8830->buf[187] = 0x0;
	sound_pci_au88x0_au8830->buf[188] = 0x0;
	sound_pci_au88x0_au8830->buf[189] = 0x0;
	sound_pci_au88x0_au8830->buf[190] = 0x0;
	sound_pci_au88x0_au8830->buf[191] = 0x0;
	sound_pci_au88x0_au8830->buf[192] = 0x0;
	sound_pci_au88x0_au8830->buf[193] = 0x0;
	sound_pci_au88x0_au8830->buf[194] = 0x0;
	sound_pci_au88x0_au8830->buf[195] = 0x0;
	sound_pci_au88x0_au8830->buf[196] = 0x0;
	sound_pci_au88x0_au8830->buf[197] = 0x0;
	sound_pci_au88x0_au8830->buf[198] = 0x0;
	sound_pci_au88x0_au8830->buf[199] = 0x0;
	sound_pci_au88x0_au8830->buf[200] = 0xff;
	sound_pci_au88x0_au8830->buf[201] = 0xff;
	sound_pci_au88x0_au8830->buf[202] = 0xff;
	sound_pci_au88x0_au8830->buf[203] = 0xff;
	sound_pci_au88x0_au8830->buf[204] = 0x0;
	sound_pci_au88x0_au8830->buf[205] = 0x0;
	sound_pci_au88x0_au8830->buf[206] = 0x0;
	sound_pci_au88x0_au8830->buf[207] = 0x80;
	sound_pci_au88x0_au8830->buf[208] = 0x0;
	sound_pci_au88x0_au8830->buf[209] = 0x0;
	sound_pci_au88x0_au8830->buf[210] = 0x0;
	sound_pci_au88x0_au8830->buf[211] = 0x80;
	sound_pci_au88x0_au8830->buf[212] = 0xff;
	sound_pci_au88x0_au8830->buf[213] = 0xff;
	sound_pci_au88x0_au8830->buf[214] = 0xff;
	sound_pci_au88x0_au8830->buf[215] = 0xff;
	sound_pci_au88x0_au8830->buf[216] = 0xff;
	sound_pci_au88x0_au8830->buf[217] = 0xff;
	sound_pci_au88x0_au8830->buf[218] = 0xff;
	sound_pci_au88x0_au8830->buf[219] = 0xff;
	sound_pci_au88x0_au8830->buf[220] = 0xff;
	sound_pci_au88x0_au8830->buf[221] = 0xff;
	sound_pci_au88x0_au8830->buf[222] = 0xff;
	sound_pci_au88x0_au8830->buf[223] = 0xff;;

    memory_region_init_io(&sound_pci_au88x0_au8830->mmio[0], OBJECT(sound_pci_au88x0_au8830), &sound_pci_au88x0_au8830_mmio_ops, sound_pci_au88x0_au8830,
                    "sound_pci_au88x0_au8830-mmio0", 16 * MiB);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &sound_pci_au88x0_au8830->mmio[0]);
    memory_region_init_io(&sound_pci_au88x0_au8830->mmio[1], OBJECT(sound_pci_au88x0_au8830), &sound_pci_au88x0_au8830_mmio_ops, sound_pci_au88x0_au8830,
                    "sound_pci_au88x0_au8830-mmio1", 16 * MiB);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &sound_pci_au88x0_au8830->mmio[1]);
    memory_region_init_io(&sound_pci_au88x0_au8830->mmio[2], OBJECT(sound_pci_au88x0_au8830), &sound_pci_au88x0_au8830_mmio_ops, sound_pci_au88x0_au8830,
                    "sound_pci_au88x0_au8830-mmio2", 16 * MiB);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &sound_pci_au88x0_au8830->mmio[2]);
    memory_region_init_io(&sound_pci_au88x0_au8830->mmio[3], OBJECT(sound_pci_au88x0_au8830), &sound_pci_au88x0_au8830_mmio_ops, sound_pci_au88x0_au8830,
                    "sound_pci_au88x0_au8830-mmio3", 16 * MiB);
    pci_register_bar(pdev, 3, PCI_BASE_ADDRESS_SPACE_MEMORY, &sound_pci_au88x0_au8830->mmio[3]);
    memory_region_init_io(&sound_pci_au88x0_au8830->mmio[4], OBJECT(sound_pci_au88x0_au8830), &sound_pci_au88x0_au8830_mmio_ops, sound_pci_au88x0_au8830,
                    "sound_pci_au88x0_au8830-mmio4", 16 * MiB);
    pci_register_bar(pdev, 4, PCI_BASE_ADDRESS_SPACE_MEMORY, &sound_pci_au88x0_au8830->mmio[4]);
    memory_region_init_io(&sound_pci_au88x0_au8830->mmio[5], OBJECT(sound_pci_au88x0_au8830), &sound_pci_au88x0_au8830_mmio_ops, sound_pci_au88x0_au8830,
                    "sound_pci_au88x0_au8830-mmio5", 16 * MiB);
    pci_register_bar(pdev, 5, PCI_BASE_ADDRESS_SPACE_MEMORY, &sound_pci_au88x0_au8830->mmio[5]);
    memory_region_init_io(&sound_pci_au88x0_au8830->mmio[6], OBJECT(sound_pci_au88x0_au8830), &sound_pci_au88x0_au8830_mmio_ops, sound_pci_au88x0_au8830,
                    "sound_pci_au88x0_au8830-mmio5", 16 * MiB);
    pci_register_bar(pdev, 6, PCI_BASE_ADDRESS_SPACE_MEMORY, &sound_pci_au88x0_au8830->mmio[6]);
}

static void pci_sound_pci_au88x0_au8830_uninit(PCIDevice *pdev)
{
    msi_uninit(pdev);
}

static void sound_pci_au88x0_au8830_instance_init(Object *obj)
{
	return;
}

static void sound_pci_au88x0_au8830_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(class);

    k->realize = pci_sound_pci_au88x0_au8830_realize;
    k->exit = pci_sound_pci_au88x0_au8830_uninit;
    k->vendor_id = 0x12eb;
    k->device_id = 0x2;
    k->revision = 0;
    k->subsystem_vendor_id = 0xffffffff;
    k->subsystem_id = 0xffffffff;
    k->class_id = 16777215;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void pci_sound_pci_au88x0_au8830_register_types(void)
{
    static InterfaceInfo interfaces[] = {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    };
    static const TypeInfo sound_pci_au88x0_au8830_info = {
        .name          = TYPE_PCI_SOUND_PCI_AU88X0_AU8830_DEVICE,
        .parent        = TYPE_PCI_DEVICE,
        .instance_size = sizeof(Sound_pci_au88x0_au8830State),
        .instance_init = sound_pci_au88x0_au8830_instance_init,
        .class_init    = sound_pci_au88x0_au8830_class_init,
        .interfaces = interfaces,
    };

    type_register_static(&sound_pci_au88x0_au8830_info);
}
type_init(pci_sound_pci_au88x0_au8830_register_types)
