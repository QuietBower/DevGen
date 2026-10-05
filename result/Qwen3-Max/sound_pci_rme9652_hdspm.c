#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/pci/pci.h"
#include "hw/hw.h"
#include "hw/pci/msi.h"
#include "qemu/timer.h"
#include "qemu/main-loop.h" /* iothread mutex */
#include "qemu/module.h"
#include "qapi/visitor.h"

#define SOUND_PCI_RME9652_HDSPM_BUF_LEN 1024

extern uint64_t target_value[SOUND_PCI_RME9652_HDSPM_BUF_LEN];
extern bool target_value_reset;

typedef struct {
    PCIDevice pdev;
    MemoryRegion mmio[7];

    uint32_t len;
    uint64_t buf[SOUND_PCI_RME9652_HDSPM_BUF_LEN];
    int is_probe_time;
} Sound_pci_rme9652_hdspmState;

#define TYPE_PCI_SOUND_PCI_RME9652_HDSPM_DEVICE "sound_pci_rme9652_hdspm"
#define SOUND_PCI_RME9652_HDSPM(obj)        OBJECT_CHECK(Sound_pci_rme9652_hdspmState, obj, TYPE_PCI_SOUND_PCI_RME9652_HDSPM_DEVICE)

static uint64_t sound_pci_rme9652_hdspm_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    Sound_pci_rme9652_hdspmState *sound_pci_rme9652_hdspm = opaque;
    int i;

    if (target_value_reset) {
        sound_pci_rme9652_hdspm->len = 0;
        target_value_reset = false;
    }

    if (!sound_pci_rme9652_hdspm->is_probe_time) {
        for (i = 0; i < SOUND_PCI_RME9652_HDSPM_BUF_LEN; ++i) {
            sound_pci_rme9652_hdspm->buf[i] = target_value[i];
        }
    } else {
        sound_pci_rme9652_hdspm->is_probe_time--;
    }

    return sound_pci_rme9652_hdspm->buf[(sound_pci_rme9652_hdspm->len++) % SOUND_PCI_RME9652_HDSPM_BUF_LEN];
}

static void sound_pci_rme9652_hdspm_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                unsigned size)
{
	return;
}

static const MemoryRegionOps sound_pci_rme9652_hdspm_mmio_ops = {
    .read = sound_pci_rme9652_hdspm_mmio_read,
    .write = sound_pci_rme9652_hdspm_mmio_write,
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

static void pci_sound_pci_rme9652_hdspm_realize(PCIDevice *pdev, Error **errp)
{
    Sound_pci_rme9652_hdspmState *sound_pci_rme9652_hdspm = SOUND_PCI_RME9652_HDSPM(pdev);
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

    sound_pci_rme9652_hdspm->len = 0;
    sound_pci_rme9652_hdspm->is_probe_time = 124;
	sound_pci_rme9652_hdspm->buf[0] = 0xff;
	sound_pci_rme9652_hdspm->buf[1] = 0xff;
	sound_pci_rme9652_hdspm->buf[2] = 0xff;
	sound_pci_rme9652_hdspm->buf[3] = 0xff;
	sound_pci_rme9652_hdspm->buf[4] = 0xff;
	sound_pci_rme9652_hdspm->buf[5] = 0xff;
	sound_pci_rme9652_hdspm->buf[6] = 0xff;
	sound_pci_rme9652_hdspm->buf[7] = 0xff;
	sound_pci_rme9652_hdspm->buf[8] = 0xff;
	sound_pci_rme9652_hdspm->buf[9] = 0xff;
	sound_pci_rme9652_hdspm->buf[10] = 0xff;
	sound_pci_rme9652_hdspm->buf[11] = 0xff;
	sound_pci_rme9652_hdspm->buf[12] = 0xff;
	sound_pci_rme9652_hdspm->buf[13] = 0xff;
	sound_pci_rme9652_hdspm->buf[14] = 0xff;
	sound_pci_rme9652_hdspm->buf[15] = 0xff;
	sound_pci_rme9652_hdspm->buf[16] = 0xff;
	sound_pci_rme9652_hdspm->buf[17] = 0xff;
	sound_pci_rme9652_hdspm->buf[18] = 0xff;
	sound_pci_rme9652_hdspm->buf[19] = 0xff;
	sound_pci_rme9652_hdspm->buf[20] = 0xff;
	sound_pci_rme9652_hdspm->buf[21] = 0xff;
	sound_pci_rme9652_hdspm->buf[22] = 0xff;
	sound_pci_rme9652_hdspm->buf[23] = 0xff;
	sound_pci_rme9652_hdspm->buf[24] = 0xff;
	sound_pci_rme9652_hdspm->buf[25] = 0xff;
	sound_pci_rme9652_hdspm->buf[26] = 0xff;
	sound_pci_rme9652_hdspm->buf[27] = 0xff;
	sound_pci_rme9652_hdspm->buf[28] = 0x0;
	sound_pci_rme9652_hdspm->buf[29] = 0x40;
	sound_pci_rme9652_hdspm->buf[30] = 0x0;
	sound_pci_rme9652_hdspm->buf[31] = 0x0;
	sound_pci_rme9652_hdspm->buf[32] = 0xff;
	sound_pci_rme9652_hdspm->buf[33] = 0xff;
	sound_pci_rme9652_hdspm->buf[34] = 0xff;
	sound_pci_rme9652_hdspm->buf[35] = 0xff;
	sound_pci_rme9652_hdspm->buf[36] = 0x0;
	sound_pci_rme9652_hdspm->buf[37] = 0x0;
	sound_pci_rme9652_hdspm->buf[38] = 0x0;
	sound_pci_rme9652_hdspm->buf[39] = 0x0;
	sound_pci_rme9652_hdspm->buf[40] = 0x0;
	sound_pci_rme9652_hdspm->buf[41] = 0x0;
	sound_pci_rme9652_hdspm->buf[42] = 0x0;
	sound_pci_rme9652_hdspm->buf[43] = 0x0;
	sound_pci_rme9652_hdspm->buf[44] = 0x0;
	sound_pci_rme9652_hdspm->buf[45] = 0x0;
	sound_pci_rme9652_hdspm->buf[46] = 0x0;
	sound_pci_rme9652_hdspm->buf[47] = 0x0;
	sound_pci_rme9652_hdspm->buf[48] = 0x0;
	sound_pci_rme9652_hdspm->buf[49] = 0x0;
	sound_pci_rme9652_hdspm->buf[50] = 0x0;
	sound_pci_rme9652_hdspm->buf[51] = 0x0;
	sound_pci_rme9652_hdspm->buf[52] = 0xff;
	sound_pci_rme9652_hdspm->buf[53] = 0xff;
	sound_pci_rme9652_hdspm->buf[54] = 0xff;
	sound_pci_rme9652_hdspm->buf[55] = 0xff;
	sound_pci_rme9652_hdspm->buf[56] = 0xff;
	sound_pci_rme9652_hdspm->buf[57] = 0xff;
	sound_pci_rme9652_hdspm->buf[58] = 0xff;
	sound_pci_rme9652_hdspm->buf[59] = 0xff;
	sound_pci_rme9652_hdspm->buf[60] = 0xff;
	sound_pci_rme9652_hdspm->buf[61] = 0xff;
	sound_pci_rme9652_hdspm->buf[62] = 0xff;
	sound_pci_rme9652_hdspm->buf[63] = 0xff;
	sound_pci_rme9652_hdspm->buf[64] = 0xff;
	sound_pci_rme9652_hdspm->buf[65] = 0xff;
	sound_pci_rme9652_hdspm->buf[66] = 0xff;
	sound_pci_rme9652_hdspm->buf[67] = 0xff;
	sound_pci_rme9652_hdspm->buf[68] = 0x0;
	sound_pci_rme9652_hdspm->buf[69] = 0x0;
	sound_pci_rme9652_hdspm->buf[70] = 0x0;
	sound_pci_rme9652_hdspm->buf[71] = 0x0;
	sound_pci_rme9652_hdspm->buf[72] = 0x0;
	sound_pci_rme9652_hdspm->buf[73] = 0x0;
	sound_pci_rme9652_hdspm->buf[74] = 0x0;
	sound_pci_rme9652_hdspm->buf[75] = 0x0;
	sound_pci_rme9652_hdspm->buf[76] = 0x0;
	sound_pci_rme9652_hdspm->buf[77] = 0x0;
	sound_pci_rme9652_hdspm->buf[78] = 0x0;
	sound_pci_rme9652_hdspm->buf[79] = 0x0;
	sound_pci_rme9652_hdspm->buf[80] = 0xff;
	sound_pci_rme9652_hdspm->buf[81] = 0xff;
	sound_pci_rme9652_hdspm->buf[82] = 0xff;
	sound_pci_rme9652_hdspm->buf[83] = 0xff;
	sound_pci_rme9652_hdspm->buf[84] = 0xff;
	sound_pci_rme9652_hdspm->buf[85] = 0xff;
	sound_pci_rme9652_hdspm->buf[86] = 0xff;
	sound_pci_rme9652_hdspm->buf[87] = 0xff;
	sound_pci_rme9652_hdspm->buf[88] = 0x0;
	sound_pci_rme9652_hdspm->buf[89] = 0x40;
	sound_pci_rme9652_hdspm->buf[90] = 0x0;
	sound_pci_rme9652_hdspm->buf[91] = 0x0;
	sound_pci_rme9652_hdspm->buf[92] = 0xff;
	sound_pci_rme9652_hdspm->buf[93] = 0xff;
	sound_pci_rme9652_hdspm->buf[94] = 0xff;
	sound_pci_rme9652_hdspm->buf[95] = 0xff;
	sound_pci_rme9652_hdspm->buf[96] = 0x0;
	sound_pci_rme9652_hdspm->buf[97] = 0x40;
	sound_pci_rme9652_hdspm->buf[98] = 0x0;
	sound_pci_rme9652_hdspm->buf[99] = 0x0;
	sound_pci_rme9652_hdspm->buf[100] = 0xff;
	sound_pci_rme9652_hdspm->buf[101] = 0xff;
	sound_pci_rme9652_hdspm->buf[102] = 0xff;
	sound_pci_rme9652_hdspm->buf[103] = 0xff;
	sound_pci_rme9652_hdspm->buf[104] = 0xff;
	sound_pci_rme9652_hdspm->buf[105] = 0xff;
	sound_pci_rme9652_hdspm->buf[106] = 0xff;
	sound_pci_rme9652_hdspm->buf[107] = 0xff;
	sound_pci_rme9652_hdspm->buf[108] = 0xff;
	sound_pci_rme9652_hdspm->buf[109] = 0xff;
	sound_pci_rme9652_hdspm->buf[110] = 0xff;
	sound_pci_rme9652_hdspm->buf[111] = 0xff;
	sound_pci_rme9652_hdspm->buf[112] = 0xff;
	sound_pci_rme9652_hdspm->buf[113] = 0xff;
	sound_pci_rme9652_hdspm->buf[114] = 0xff;
	sound_pci_rme9652_hdspm->buf[115] = 0xff;
	sound_pci_rme9652_hdspm->buf[116] = 0xff;
	sound_pci_rme9652_hdspm->buf[117] = 0xff;
	sound_pci_rme9652_hdspm->buf[118] = 0xff;
	sound_pci_rme9652_hdspm->buf[119] = 0xff;
	sound_pci_rme9652_hdspm->buf[120] = 0xff;
	sound_pci_rme9652_hdspm->buf[121] = 0xff;
	sound_pci_rme9652_hdspm->buf[122] = 0xff;
	sound_pci_rme9652_hdspm->buf[123] = 0xff;;

    memory_region_init_io(&sound_pci_rme9652_hdspm->mmio[0], OBJECT(sound_pci_rme9652_hdspm), &sound_pci_rme9652_hdspm_mmio_ops, sound_pci_rme9652_hdspm,
                    "sound_pci_rme9652_hdspm-mmio0", 16 * MiB);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &sound_pci_rme9652_hdspm->mmio[0]);
    memory_region_init_io(&sound_pci_rme9652_hdspm->mmio[1], OBJECT(sound_pci_rme9652_hdspm), &sound_pci_rme9652_hdspm_mmio_ops, sound_pci_rme9652_hdspm,
                    "sound_pci_rme9652_hdspm-mmio1", 16 * MiB);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &sound_pci_rme9652_hdspm->mmio[1]);
    memory_region_init_io(&sound_pci_rme9652_hdspm->mmio[2], OBJECT(sound_pci_rme9652_hdspm), &sound_pci_rme9652_hdspm_mmio_ops, sound_pci_rme9652_hdspm,
                    "sound_pci_rme9652_hdspm-mmio2", 16 * MiB);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &sound_pci_rme9652_hdspm->mmio[2]);
    memory_region_init_io(&sound_pci_rme9652_hdspm->mmio[3], OBJECT(sound_pci_rme9652_hdspm), &sound_pci_rme9652_hdspm_mmio_ops, sound_pci_rme9652_hdspm,
                    "sound_pci_rme9652_hdspm-mmio3", 16 * MiB);
    pci_register_bar(pdev, 3, PCI_BASE_ADDRESS_SPACE_MEMORY, &sound_pci_rme9652_hdspm->mmio[3]);
    memory_region_init_io(&sound_pci_rme9652_hdspm->mmio[4], OBJECT(sound_pci_rme9652_hdspm), &sound_pci_rme9652_hdspm_mmio_ops, sound_pci_rme9652_hdspm,
                    "sound_pci_rme9652_hdspm-mmio4", 16 * MiB);
    pci_register_bar(pdev, 4, PCI_BASE_ADDRESS_SPACE_MEMORY, &sound_pci_rme9652_hdspm->mmio[4]);
    memory_region_init_io(&sound_pci_rme9652_hdspm->mmio[5], OBJECT(sound_pci_rme9652_hdspm), &sound_pci_rme9652_hdspm_mmio_ops, sound_pci_rme9652_hdspm,
                    "sound_pci_rme9652_hdspm-mmio5", 16 * MiB);
    pci_register_bar(pdev, 5, PCI_BASE_ADDRESS_SPACE_MEMORY, &sound_pci_rme9652_hdspm->mmio[5]);
    memory_region_init_io(&sound_pci_rme9652_hdspm->mmio[6], OBJECT(sound_pci_rme9652_hdspm), &sound_pci_rme9652_hdspm_mmio_ops, sound_pci_rme9652_hdspm,
                    "sound_pci_rme9652_hdspm-mmio5", 16 * MiB);
    pci_register_bar(pdev, 6, PCI_BASE_ADDRESS_SPACE_MEMORY, &sound_pci_rme9652_hdspm->mmio[6]);
}

static void pci_sound_pci_rme9652_hdspm_uninit(PCIDevice *pdev)
{
    msi_uninit(pdev);
}

static void sound_pci_rme9652_hdspm_instance_init(Object *obj)
{
	return;
}

static void sound_pci_rme9652_hdspm_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(class);

    k->realize = pci_sound_pci_rme9652_hdspm_realize;
    k->exit = pci_sound_pci_rme9652_hdspm_uninit;
    k->vendor_id = 0x10ee;
    k->device_id = 0x3fc6;
    k->revision = 0;
    k->subsystem_vendor_id = 0xffffffff;
    k->subsystem_id = 0xffffffff;
    k->class_id = 16777215;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void pci_sound_pci_rme9652_hdspm_register_types(void)
{
    static InterfaceInfo interfaces[] = {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    };
    static const TypeInfo sound_pci_rme9652_hdspm_info = {
        .name          = TYPE_PCI_SOUND_PCI_RME9652_HDSPM_DEVICE,
        .parent        = TYPE_PCI_DEVICE,
        .instance_size = sizeof(Sound_pci_rme9652_hdspmState),
        .instance_init = sound_pci_rme9652_hdspm_instance_init,
        .class_init    = sound_pci_rme9652_hdspm_class_init,
        .interfaces = interfaces,
    };

    type_register_static(&sound_pci_rme9652_hdspm_info);
}
type_init(pci_sound_pci_rme9652_hdspm_register_types)
