#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/pci/pci.h"
#include "hw/hw.h"
#include "hw/pci/msi.h"
#include "qemu/timer.h"
#include "qemu/main-loop.h" /* iothread mutex */
#include "qemu/module.h"
#include "qapi/visitor.h"

#define SOUND_PCI_RME9652_HDSP_BUF_LEN 1024

extern uint64_t target_value[SOUND_PCI_RME9652_HDSP_BUF_LEN];
extern bool target_value_reset;

typedef struct {
    PCIDevice pdev;
    MemoryRegion mmio[7];

    uint32_t len;
    uint64_t buf[SOUND_PCI_RME9652_HDSP_BUF_LEN];
    int is_probe_time;
} Sound_pci_rme9652_hdspState;

#define TYPE_PCI_SOUND_PCI_RME9652_HDSP_DEVICE "sound_pci_rme9652_hdsp"
#define SOUND_PCI_RME9652_HDSP(obj)        OBJECT_CHECK(Sound_pci_rme9652_hdspState, obj, TYPE_PCI_SOUND_PCI_RME9652_HDSP_DEVICE)

static uint64_t sound_pci_rme9652_hdsp_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    Sound_pci_rme9652_hdspState *sound_pci_rme9652_hdsp = opaque;
    int i;

    if (target_value_reset) {
        sound_pci_rme9652_hdsp->len = 0;
        target_value_reset = false;
    }

    if (!sound_pci_rme9652_hdsp->is_probe_time) {
        for (i = 0; i < SOUND_PCI_RME9652_HDSP_BUF_LEN; ++i) {
            sound_pci_rme9652_hdsp->buf[i] = target_value[i];
        }
    } else {
        sound_pci_rme9652_hdsp->is_probe_time--;
    }

    return sound_pci_rme9652_hdsp->buf[(sound_pci_rme9652_hdsp->len++) % SOUND_PCI_RME9652_HDSP_BUF_LEN];
}

static void sound_pci_rme9652_hdsp_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                unsigned size)
{
	return;
}

static const MemoryRegionOps sound_pci_rme9652_hdsp_mmio_ops = {
    .read = sound_pci_rme9652_hdsp_mmio_read,
    .write = sound_pci_rme9652_hdsp_mmio_write,
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

static void pci_sound_pci_rme9652_hdsp_realize(PCIDevice *pdev, Error **errp)
{
    Sound_pci_rme9652_hdspState *sound_pci_rme9652_hdsp = SOUND_PCI_RME9652_HDSP(pdev);
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

    sound_pci_rme9652_hdsp->len = 0;
    sound_pci_rme9652_hdsp->is_probe_time = 72;
	sound_pci_rme9652_hdsp->buf[0] = 0xff;
	sound_pci_rme9652_hdsp->buf[1] = 0xff;
	sound_pci_rme9652_hdsp->buf[2] = 0xff;
	sound_pci_rme9652_hdsp->buf[3] = 0xff;
	sound_pci_rme9652_hdsp->buf[4] = 0xff;
	sound_pci_rme9652_hdsp->buf[5] = 0xff;
	sound_pci_rme9652_hdsp->buf[6] = 0xff;
	sound_pci_rme9652_hdsp->buf[7] = 0xff;
	sound_pci_rme9652_hdsp->buf[8] = 0xff;
	sound_pci_rme9652_hdsp->buf[9] = 0xff;
	sound_pci_rme9652_hdsp->buf[10] = 0xff;
	sound_pci_rme9652_hdsp->buf[11] = 0xff;
	sound_pci_rme9652_hdsp->buf[12] = 0xff;
	sound_pci_rme9652_hdsp->buf[13] = 0xff;
	sound_pci_rme9652_hdsp->buf[14] = 0xff;
	sound_pci_rme9652_hdsp->buf[15] = 0xff;
	sound_pci_rme9652_hdsp->buf[16] = 0xff;
	sound_pci_rme9652_hdsp->buf[17] = 0xff;
	sound_pci_rme9652_hdsp->buf[18] = 0xff;
	sound_pci_rme9652_hdsp->buf[19] = 0xff;
	sound_pci_rme9652_hdsp->buf[20] = 0xff;
	sound_pci_rme9652_hdsp->buf[21] = 0xff;
	sound_pci_rme9652_hdsp->buf[22] = 0xff;
	sound_pci_rme9652_hdsp->buf[23] = 0xff;
	sound_pci_rme9652_hdsp->buf[24] = 0xff;
	sound_pci_rme9652_hdsp->buf[25] = 0xff;
	sound_pci_rme9652_hdsp->buf[26] = 0xff;
	sound_pci_rme9652_hdsp->buf[27] = 0xff;
	sound_pci_rme9652_hdsp->buf[28] = 0xff;
	sound_pci_rme9652_hdsp->buf[29] = 0xff;
	sound_pci_rme9652_hdsp->buf[30] = 0xff;
	sound_pci_rme9652_hdsp->buf[31] = 0xff;
	sound_pci_rme9652_hdsp->buf[32] = 0xff;
	sound_pci_rme9652_hdsp->buf[33] = 0xff;
	sound_pci_rme9652_hdsp->buf[34] = 0xff;
	sound_pci_rme9652_hdsp->buf[35] = 0xff;
	sound_pci_rme9652_hdsp->buf[36] = 0x0;
	sound_pci_rme9652_hdsp->buf[37] = 0x0;
	sound_pci_rme9652_hdsp->buf[38] = 0x4;
	sound_pci_rme9652_hdsp->buf[39] = 0x0;
	sound_pci_rme9652_hdsp->buf[40] = 0x0;
	sound_pci_rme9652_hdsp->buf[41] = 0x0;
	sound_pci_rme9652_hdsp->buf[42] = 0x0;
	sound_pci_rme9652_hdsp->buf[43] = 0x0;
	sound_pci_rme9652_hdsp->buf[44] = 0xff;
	sound_pci_rme9652_hdsp->buf[45] = 0xff;
	sound_pci_rme9652_hdsp->buf[46] = 0xff;
	sound_pci_rme9652_hdsp->buf[47] = 0xff;
	sound_pci_rme9652_hdsp->buf[48] = 0xff;
	sound_pci_rme9652_hdsp->buf[49] = 0xff;
	sound_pci_rme9652_hdsp->buf[50] = 0xff;
	sound_pci_rme9652_hdsp->buf[51] = 0xff;
	sound_pci_rme9652_hdsp->buf[52] = 0xff;
	sound_pci_rme9652_hdsp->buf[53] = 0xff;
	sound_pci_rme9652_hdsp->buf[54] = 0xff;
	sound_pci_rme9652_hdsp->buf[55] = 0xff;
	sound_pci_rme9652_hdsp->buf[56] = 0xff;
	sound_pci_rme9652_hdsp->buf[57] = 0xff;
	sound_pci_rme9652_hdsp->buf[58] = 0xff;
	sound_pci_rme9652_hdsp->buf[59] = 0xff;
	sound_pci_rme9652_hdsp->buf[60] = 0xff;
	sound_pci_rme9652_hdsp->buf[61] = 0xff;
	sound_pci_rme9652_hdsp->buf[62] = 0xff;
	sound_pci_rme9652_hdsp->buf[63] = 0xff;
	sound_pci_rme9652_hdsp->buf[64] = 0xff;
	sound_pci_rme9652_hdsp->buf[65] = 0xff;
	sound_pci_rme9652_hdsp->buf[66] = 0xff;
	sound_pci_rme9652_hdsp->buf[67] = 0xff;
	sound_pci_rme9652_hdsp->buf[68] = 0xff;
	sound_pci_rme9652_hdsp->buf[69] = 0xff;
	sound_pci_rme9652_hdsp->buf[70] = 0xff;
	sound_pci_rme9652_hdsp->buf[71] = 0xff;;

    memory_region_init_io(&sound_pci_rme9652_hdsp->mmio[0], OBJECT(sound_pci_rme9652_hdsp), &sound_pci_rme9652_hdsp_mmio_ops, sound_pci_rme9652_hdsp,
                    "sound_pci_rme9652_hdsp-mmio0", 16 * MiB);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &sound_pci_rme9652_hdsp->mmio[0]);
    memory_region_init_io(&sound_pci_rme9652_hdsp->mmio[1], OBJECT(sound_pci_rme9652_hdsp), &sound_pci_rme9652_hdsp_mmio_ops, sound_pci_rme9652_hdsp,
                    "sound_pci_rme9652_hdsp-mmio1", 16 * MiB);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &sound_pci_rme9652_hdsp->mmio[1]);
    memory_region_init_io(&sound_pci_rme9652_hdsp->mmio[2], OBJECT(sound_pci_rme9652_hdsp), &sound_pci_rme9652_hdsp_mmio_ops, sound_pci_rme9652_hdsp,
                    "sound_pci_rme9652_hdsp-mmio2", 16 * MiB);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &sound_pci_rme9652_hdsp->mmio[2]);
    memory_region_init_io(&sound_pci_rme9652_hdsp->mmio[3], OBJECT(sound_pci_rme9652_hdsp), &sound_pci_rme9652_hdsp_mmio_ops, sound_pci_rme9652_hdsp,
                    "sound_pci_rme9652_hdsp-mmio3", 16 * MiB);
    pci_register_bar(pdev, 3, PCI_BASE_ADDRESS_SPACE_MEMORY, &sound_pci_rme9652_hdsp->mmio[3]);
    memory_region_init_io(&sound_pci_rme9652_hdsp->mmio[4], OBJECT(sound_pci_rme9652_hdsp), &sound_pci_rme9652_hdsp_mmio_ops, sound_pci_rme9652_hdsp,
                    "sound_pci_rme9652_hdsp-mmio4", 16 * MiB);
    pci_register_bar(pdev, 4, PCI_BASE_ADDRESS_SPACE_MEMORY, &sound_pci_rme9652_hdsp->mmio[4]);
    memory_region_init_io(&sound_pci_rme9652_hdsp->mmio[5], OBJECT(sound_pci_rme9652_hdsp), &sound_pci_rme9652_hdsp_mmio_ops, sound_pci_rme9652_hdsp,
                    "sound_pci_rme9652_hdsp-mmio5", 16 * MiB);
    pci_register_bar(pdev, 5, PCI_BASE_ADDRESS_SPACE_MEMORY, &sound_pci_rme9652_hdsp->mmio[5]);
    memory_region_init_io(&sound_pci_rme9652_hdsp->mmio[6], OBJECT(sound_pci_rme9652_hdsp), &sound_pci_rme9652_hdsp_mmio_ops, sound_pci_rme9652_hdsp,
                    "sound_pci_rme9652_hdsp-mmio5", 16 * MiB);
    pci_register_bar(pdev, 6, PCI_BASE_ADDRESS_SPACE_MEMORY, &sound_pci_rme9652_hdsp->mmio[6]);
}

static void pci_sound_pci_rme9652_hdsp_uninit(PCIDevice *pdev)
{
    msi_uninit(pdev);
}

static void sound_pci_rme9652_hdsp_instance_init(Object *obj)
{
	return;
}

static void sound_pci_rme9652_hdsp_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(class);

    k->realize = pci_sound_pci_rme9652_hdsp_realize;
    k->exit = pci_sound_pci_rme9652_hdsp_uninit;
    k->vendor_id = 0x10ee;
    k->device_id = 0x3fc5;
    k->revision = 0;
    k->subsystem_vendor_id = 0xffffffff;
    k->subsystem_id = 0xffffffff;
    k->class_id = 16777215;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void pci_sound_pci_rme9652_hdsp_register_types(void)
{
    static InterfaceInfo interfaces[] = {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    };
    static const TypeInfo sound_pci_rme9652_hdsp_info = {
        .name          = TYPE_PCI_SOUND_PCI_RME9652_HDSP_DEVICE,
        .parent        = TYPE_PCI_DEVICE,
        .instance_size = sizeof(Sound_pci_rme9652_hdspState),
        .instance_init = sound_pci_rme9652_hdsp_instance_init,
        .class_init    = sound_pci_rme9652_hdsp_class_init,
        .interfaces = interfaces,
    };

    type_register_static(&sound_pci_rme9652_hdsp_info);
}
type_init(pci_sound_pci_rme9652_hdsp_register_types)
