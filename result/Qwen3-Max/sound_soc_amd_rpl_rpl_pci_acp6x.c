#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/pci/pci.h"
#include "hw/hw.h"
#include "hw/pci/msi.h"
#include "qemu/timer.h"
#include "qemu/main-loop.h" /* iothread mutex */
#include "qemu/module.h"
#include "qapi/visitor.h"

#define SOUND_SOC_AMD_RPL_RPL_PCI_ACP6X_BUF_LEN 1024

extern uint64_t target_value[SOUND_SOC_AMD_RPL_RPL_PCI_ACP6X_BUF_LEN];
extern bool target_value_reset;

typedef struct {
    PCIDevice pdev;
    MemoryRegion mmio[7];

    uint32_t len;
    uint64_t buf[SOUND_SOC_AMD_RPL_RPL_PCI_ACP6X_BUF_LEN];
    int is_probe_time;
} Sound_soc_amd_rpl_rpl_pci_acp6xState;

#define TYPE_PCI_SOUND_SOC_AMD_RPL_RPL_PCI_ACP6X_DEVICE "sound_soc_amd_rpl_rpl_pci_acp6x"
#define SOUND_SOC_AMD_RPL_RPL_PCI_ACP6X(obj)        OBJECT_CHECK(Sound_soc_amd_rpl_rpl_pci_acp6xState, obj, TYPE_PCI_SOUND_SOC_AMD_RPL_RPL_PCI_ACP6X_DEVICE)

static uint64_t sound_soc_amd_rpl_rpl_pci_acp6x_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    Sound_soc_amd_rpl_rpl_pci_acp6xState *sound_soc_amd_rpl_rpl_pci_acp6x = opaque;
    int i;

    if (target_value_reset) {
        sound_soc_amd_rpl_rpl_pci_acp6x->len = 0;
        target_value_reset = false;
    }

    if (!sound_soc_amd_rpl_rpl_pci_acp6x->is_probe_time) {
        for (i = 0; i < SOUND_SOC_AMD_RPL_RPL_PCI_ACP6X_BUF_LEN; ++i) {
            sound_soc_amd_rpl_rpl_pci_acp6x->buf[i] = target_value[i];
        }
    } else {
        sound_soc_amd_rpl_rpl_pci_acp6x->is_probe_time--;
    }

    return sound_soc_amd_rpl_rpl_pci_acp6x->buf[(sound_soc_amd_rpl_rpl_pci_acp6x->len++) % SOUND_SOC_AMD_RPL_RPL_PCI_ACP6X_BUF_LEN];
}

static void sound_soc_amd_rpl_rpl_pci_acp6x_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                unsigned size)
{
	return;
}

static const MemoryRegionOps sound_soc_amd_rpl_rpl_pci_acp6x_mmio_ops = {
    .read = sound_soc_amd_rpl_rpl_pci_acp6x_mmio_read,
    .write = sound_soc_amd_rpl_rpl_pci_acp6x_mmio_write,
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

static void pci_sound_soc_amd_rpl_rpl_pci_acp6x_realize(PCIDevice *pdev, Error **errp)
{
    Sound_soc_amd_rpl_rpl_pci_acp6xState *sound_soc_amd_rpl_rpl_pci_acp6x = SOUND_SOC_AMD_RPL_RPL_PCI_ACP6X(pdev);
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

    sound_soc_amd_rpl_rpl_pci_acp6x->len = 0;
    sound_soc_amd_rpl_rpl_pci_acp6x->is_probe_time = 36;
	sound_soc_amd_rpl_rpl_pci_acp6x->buf[0] = 0xff;
	sound_soc_amd_rpl_rpl_pci_acp6x->buf[1] = 0xff;
	sound_soc_amd_rpl_rpl_pci_acp6x->buf[2] = 0xff;
	sound_soc_amd_rpl_rpl_pci_acp6x->buf[3] = 0xff;
	sound_soc_amd_rpl_rpl_pci_acp6x->buf[4] = 0xff;
	sound_soc_amd_rpl_rpl_pci_acp6x->buf[5] = 0xff;
	sound_soc_amd_rpl_rpl_pci_acp6x->buf[6] = 0xff;
	sound_soc_amd_rpl_rpl_pci_acp6x->buf[7] = 0xff;
	sound_soc_amd_rpl_rpl_pci_acp6x->buf[8] = 0xff;
	sound_soc_amd_rpl_rpl_pci_acp6x->buf[9] = 0xff;
	sound_soc_amd_rpl_rpl_pci_acp6x->buf[10] = 0xff;
	sound_soc_amd_rpl_rpl_pci_acp6x->buf[11] = 0xff;
	sound_soc_amd_rpl_rpl_pci_acp6x->buf[12] = 0xff;
	sound_soc_amd_rpl_rpl_pci_acp6x->buf[13] = 0xff;
	sound_soc_amd_rpl_rpl_pci_acp6x->buf[14] = 0xff;
	sound_soc_amd_rpl_rpl_pci_acp6x->buf[15] = 0xff;
	sound_soc_amd_rpl_rpl_pci_acp6x->buf[16] = 0xff;
	sound_soc_amd_rpl_rpl_pci_acp6x->buf[17] = 0xff;
	sound_soc_amd_rpl_rpl_pci_acp6x->buf[18] = 0xff;
	sound_soc_amd_rpl_rpl_pci_acp6x->buf[19] = 0xff;
	sound_soc_amd_rpl_rpl_pci_acp6x->buf[20] = 0xff;
	sound_soc_amd_rpl_rpl_pci_acp6x->buf[21] = 0xff;
	sound_soc_amd_rpl_rpl_pci_acp6x->buf[22] = 0xff;
	sound_soc_amd_rpl_rpl_pci_acp6x->buf[23] = 0xff;
	sound_soc_amd_rpl_rpl_pci_acp6x->buf[24] = 0xff;
	sound_soc_amd_rpl_rpl_pci_acp6x->buf[25] = 0xff;
	sound_soc_amd_rpl_rpl_pci_acp6x->buf[26] = 0xff;
	sound_soc_amd_rpl_rpl_pci_acp6x->buf[27] = 0xff;
	sound_soc_amd_rpl_rpl_pci_acp6x->buf[28] = 0xff;
	sound_soc_amd_rpl_rpl_pci_acp6x->buf[29] = 0xff;
	sound_soc_amd_rpl_rpl_pci_acp6x->buf[30] = 0xff;
	sound_soc_amd_rpl_rpl_pci_acp6x->buf[31] = 0xff;
	sound_soc_amd_rpl_rpl_pci_acp6x->buf[32] = 0xff;
	sound_soc_amd_rpl_rpl_pci_acp6x->buf[33] = 0xff;
	sound_soc_amd_rpl_rpl_pci_acp6x->buf[34] = 0xff;
	sound_soc_amd_rpl_rpl_pci_acp6x->buf[35] = 0xff;;

    memory_region_init_io(&sound_soc_amd_rpl_rpl_pci_acp6x->mmio[0], OBJECT(sound_soc_amd_rpl_rpl_pci_acp6x), &sound_soc_amd_rpl_rpl_pci_acp6x_mmio_ops, sound_soc_amd_rpl_rpl_pci_acp6x,
                    "sound_soc_amd_rpl_rpl_pci_acp6x-mmio0", 16 * MiB);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &sound_soc_amd_rpl_rpl_pci_acp6x->mmio[0]);
    memory_region_init_io(&sound_soc_amd_rpl_rpl_pci_acp6x->mmio[1], OBJECT(sound_soc_amd_rpl_rpl_pci_acp6x), &sound_soc_amd_rpl_rpl_pci_acp6x_mmio_ops, sound_soc_amd_rpl_rpl_pci_acp6x,
                    "sound_soc_amd_rpl_rpl_pci_acp6x-mmio1", 16 * MiB);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &sound_soc_amd_rpl_rpl_pci_acp6x->mmio[1]);
    memory_region_init_io(&sound_soc_amd_rpl_rpl_pci_acp6x->mmio[2], OBJECT(sound_soc_amd_rpl_rpl_pci_acp6x), &sound_soc_amd_rpl_rpl_pci_acp6x_mmio_ops, sound_soc_amd_rpl_rpl_pci_acp6x,
                    "sound_soc_amd_rpl_rpl_pci_acp6x-mmio2", 16 * MiB);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &sound_soc_amd_rpl_rpl_pci_acp6x->mmio[2]);
    memory_region_init_io(&sound_soc_amd_rpl_rpl_pci_acp6x->mmio[3], OBJECT(sound_soc_amd_rpl_rpl_pci_acp6x), &sound_soc_amd_rpl_rpl_pci_acp6x_mmio_ops, sound_soc_amd_rpl_rpl_pci_acp6x,
                    "sound_soc_amd_rpl_rpl_pci_acp6x-mmio3", 16 * MiB);
    pci_register_bar(pdev, 3, PCI_BASE_ADDRESS_SPACE_MEMORY, &sound_soc_amd_rpl_rpl_pci_acp6x->mmio[3]);
    memory_region_init_io(&sound_soc_amd_rpl_rpl_pci_acp6x->mmio[4], OBJECT(sound_soc_amd_rpl_rpl_pci_acp6x), &sound_soc_amd_rpl_rpl_pci_acp6x_mmio_ops, sound_soc_amd_rpl_rpl_pci_acp6x,
                    "sound_soc_amd_rpl_rpl_pci_acp6x-mmio4", 16 * MiB);
    pci_register_bar(pdev, 4, PCI_BASE_ADDRESS_SPACE_MEMORY, &sound_soc_amd_rpl_rpl_pci_acp6x->mmio[4]);
    memory_region_init_io(&sound_soc_amd_rpl_rpl_pci_acp6x->mmio[5], OBJECT(sound_soc_amd_rpl_rpl_pci_acp6x), &sound_soc_amd_rpl_rpl_pci_acp6x_mmio_ops, sound_soc_amd_rpl_rpl_pci_acp6x,
                    "sound_soc_amd_rpl_rpl_pci_acp6x-mmio5", 16 * MiB);
    pci_register_bar(pdev, 5, PCI_BASE_ADDRESS_SPACE_MEMORY, &sound_soc_amd_rpl_rpl_pci_acp6x->mmio[5]);
    memory_region_init_io(&sound_soc_amd_rpl_rpl_pci_acp6x->mmio[6], OBJECT(sound_soc_amd_rpl_rpl_pci_acp6x), &sound_soc_amd_rpl_rpl_pci_acp6x_mmio_ops, sound_soc_amd_rpl_rpl_pci_acp6x,
                    "sound_soc_amd_rpl_rpl_pci_acp6x-mmio5", 16 * MiB);
    pci_register_bar(pdev, 6, PCI_BASE_ADDRESS_SPACE_MEMORY, &sound_soc_amd_rpl_rpl_pci_acp6x->mmio[6]);
}

static void pci_sound_soc_amd_rpl_rpl_pci_acp6x_uninit(PCIDevice *pdev)
{
    msi_uninit(pdev);
}

static void sound_soc_amd_rpl_rpl_pci_acp6x_instance_init(Object *obj)
{
	return;
}

static void sound_soc_amd_rpl_rpl_pci_acp6x_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(class);

    k->realize = pci_sound_soc_amd_rpl_rpl_pci_acp6x_realize;
    k->exit = pci_sound_soc_amd_rpl_rpl_pci_acp6x_uninit;
    k->vendor_id = 0x1022;
    k->device_id = 0x15e2;
    k->revision = 0;
    k->subsystem_vendor_id = 0xffffffff;
    k->subsystem_id = 0xffffffff;
    k->class_id = 16712832;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void pci_sound_soc_amd_rpl_rpl_pci_acp6x_register_types(void)
{
    static InterfaceInfo interfaces[] = {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    };
    static const TypeInfo sound_soc_amd_rpl_rpl_pci_acp6x_info = {
        .name          = TYPE_PCI_SOUND_SOC_AMD_RPL_RPL_PCI_ACP6X_DEVICE,
        .parent        = TYPE_PCI_DEVICE,
        .instance_size = sizeof(Sound_soc_amd_rpl_rpl_pci_acp6xState),
        .instance_init = sound_soc_amd_rpl_rpl_pci_acp6x_instance_init,
        .class_init    = sound_soc_amd_rpl_rpl_pci_acp6x_class_init,
        .interfaces = interfaces,
    };

    type_register_static(&sound_soc_amd_rpl_rpl_pci_acp6x_info);
}
type_init(pci_sound_soc_amd_rpl_rpl_pci_acp6x_register_types)
