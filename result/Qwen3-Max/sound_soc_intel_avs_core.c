#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/pci/pci.h"
#include "hw/hw.h"
#include "hw/pci/msi.h"
#include "qemu/timer.h"
#include "qemu/main-loop.h" /* iothread mutex */
#include "qemu/module.h"
#include "qapi/visitor.h"

#define SOUND_SOC_INTEL_AVS_CORE_BUF_LEN 1024

extern uint64_t target_value[SOUND_SOC_INTEL_AVS_CORE_BUF_LEN];
extern bool target_value_reset;

typedef struct {
    PCIDevice pdev;
    MemoryRegion mmio[7];

    uint32_t len;
    uint64_t buf[SOUND_SOC_INTEL_AVS_CORE_BUF_LEN];
    int is_probe_time;
} Sound_soc_intel_avs_coreState;

#define TYPE_PCI_SOUND_SOC_INTEL_AVS_CORE_DEVICE "sound_soc_intel_avs_core"
#define SOUND_SOC_INTEL_AVS_CORE(obj)        OBJECT_CHECK(Sound_soc_intel_avs_coreState, obj, TYPE_PCI_SOUND_SOC_INTEL_AVS_CORE_DEVICE)

static uint64_t sound_soc_intel_avs_core_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    Sound_soc_intel_avs_coreState *sound_soc_intel_avs_core = opaque;
    int i;

    if (target_value_reset) {
        sound_soc_intel_avs_core->len = 0;
        target_value_reset = false;
    }

    if (!sound_soc_intel_avs_core->is_probe_time) {
        for (i = 0; i < SOUND_SOC_INTEL_AVS_CORE_BUF_LEN; ++i) {
            sound_soc_intel_avs_core->buf[i] = target_value[i];
        }
    } else {
        sound_soc_intel_avs_core->is_probe_time--;
    }

    return sound_soc_intel_avs_core->buf[(sound_soc_intel_avs_core->len++) % SOUND_SOC_INTEL_AVS_CORE_BUF_LEN];
}

static void sound_soc_intel_avs_core_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                unsigned size)
{
	return;
}

static const MemoryRegionOps sound_soc_intel_avs_core_mmio_ops = {
    .read = sound_soc_intel_avs_core_mmio_read,
    .write = sound_soc_intel_avs_core_mmio_write,
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

static void pci_sound_soc_intel_avs_core_realize(PCIDevice *pdev, Error **errp)
{
    Sound_soc_intel_avs_coreState *sound_soc_intel_avs_core = SOUND_SOC_INTEL_AVS_CORE(pdev);
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

    sound_soc_intel_avs_core->len = 0;
    sound_soc_intel_avs_core->is_probe_time = 6;
	sound_soc_intel_avs_core->buf[0] = 0xff;
	sound_soc_intel_avs_core->buf[1] = 0xff;
	sound_soc_intel_avs_core->buf[2] = 0xff;
	sound_soc_intel_avs_core->buf[3] = 0xff;
	sound_soc_intel_avs_core->buf[4] = 0xff;
	sound_soc_intel_avs_core->buf[5] = 0xff;;

    memory_region_init_io(&sound_soc_intel_avs_core->mmio[0], OBJECT(sound_soc_intel_avs_core), &sound_soc_intel_avs_core_mmio_ops, sound_soc_intel_avs_core,
                    "sound_soc_intel_avs_core-mmio0", 16 * MiB);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &sound_soc_intel_avs_core->mmio[0]);
    memory_region_init_io(&sound_soc_intel_avs_core->mmio[1], OBJECT(sound_soc_intel_avs_core), &sound_soc_intel_avs_core_mmio_ops, sound_soc_intel_avs_core,
                    "sound_soc_intel_avs_core-mmio1", 16 * MiB);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &sound_soc_intel_avs_core->mmio[1]);
    memory_region_init_io(&sound_soc_intel_avs_core->mmio[2], OBJECT(sound_soc_intel_avs_core), &sound_soc_intel_avs_core_mmio_ops, sound_soc_intel_avs_core,
                    "sound_soc_intel_avs_core-mmio2", 16 * MiB);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &sound_soc_intel_avs_core->mmio[2]);
    memory_region_init_io(&sound_soc_intel_avs_core->mmio[3], OBJECT(sound_soc_intel_avs_core), &sound_soc_intel_avs_core_mmio_ops, sound_soc_intel_avs_core,
                    "sound_soc_intel_avs_core-mmio3", 16 * MiB);
    pci_register_bar(pdev, 3, PCI_BASE_ADDRESS_SPACE_MEMORY, &sound_soc_intel_avs_core->mmio[3]);
    memory_region_init_io(&sound_soc_intel_avs_core->mmio[4], OBJECT(sound_soc_intel_avs_core), &sound_soc_intel_avs_core_mmio_ops, sound_soc_intel_avs_core,
                    "sound_soc_intel_avs_core-mmio4", 16 * MiB);
    pci_register_bar(pdev, 4, PCI_BASE_ADDRESS_SPACE_MEMORY, &sound_soc_intel_avs_core->mmio[4]);
    memory_region_init_io(&sound_soc_intel_avs_core->mmio[5], OBJECT(sound_soc_intel_avs_core), &sound_soc_intel_avs_core_mmio_ops, sound_soc_intel_avs_core,
                    "sound_soc_intel_avs_core-mmio5", 16 * MiB);
    pci_register_bar(pdev, 5, PCI_BASE_ADDRESS_SPACE_MEMORY, &sound_soc_intel_avs_core->mmio[5]);
    memory_region_init_io(&sound_soc_intel_avs_core->mmio[6], OBJECT(sound_soc_intel_avs_core), &sound_soc_intel_avs_core_mmio_ops, sound_soc_intel_avs_core,
                    "sound_soc_intel_avs_core-mmio5", 16 * MiB);
    pci_register_bar(pdev, 6, PCI_BASE_ADDRESS_SPACE_MEMORY, &sound_soc_intel_avs_core->mmio[6]);
}

static void pci_sound_soc_intel_avs_core_uninit(PCIDevice *pdev)
{
    msi_uninit(pdev);
}

static void sound_soc_intel_avs_core_instance_init(Object *obj)
{
	return;
}

static void sound_soc_intel_avs_core_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(class);

    k->realize = pci_sound_soc_intel_avs_core_realize;
    k->exit = pci_sound_soc_intel_avs_core_uninit;
    k->vendor_id = 0x8086;
    k->device_id = 0x9d70;
    k->revision = 0;
    k->subsystem_vendor_id = 0xffffffff;
    k->subsystem_id = 0xffffffff;
    k->class_id = 16777215;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void pci_sound_soc_intel_avs_core_register_types(void)
{
    static InterfaceInfo interfaces[] = {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    };
    static const TypeInfo sound_soc_intel_avs_core_info = {
        .name          = TYPE_PCI_SOUND_SOC_INTEL_AVS_CORE_DEVICE,
        .parent        = TYPE_PCI_DEVICE,
        .instance_size = sizeof(Sound_soc_intel_avs_coreState),
        .instance_init = sound_soc_intel_avs_core_instance_init,
        .class_init    = sound_soc_intel_avs_core_class_init,
        .interfaces = interfaces,
    };

    type_register_static(&sound_soc_intel_avs_core_info);
}
type_init(pci_sound_soc_intel_avs_core_register_types)
