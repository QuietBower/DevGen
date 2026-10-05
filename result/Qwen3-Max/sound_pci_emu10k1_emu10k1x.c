#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/pci/pci.h"
#include "hw/hw.h"
#include "hw/pci/msi.h"
#include "qemu/timer.h"
#include "qemu/main-loop.h" /* iothread mutex */
#include "qemu/module.h"
#include "qapi/visitor.h"

#define SOUND_PCI_EMU10K1_EMU10K1X_BUF_LEN 1024

extern uint64_t target_value[SOUND_PCI_EMU10K1_EMU10K1X_BUF_LEN];
extern bool target_value_reset;

typedef struct {
    PCIDevice pdev;
    MemoryRegion mmio[7];

    uint32_t len;
    uint64_t buf[SOUND_PCI_EMU10K1_EMU10K1X_BUF_LEN];
    int is_probe_time;
} Sound_pci_emu10k1_emu10k1xState;

#define TYPE_PCI_SOUND_PCI_EMU10K1_EMU10K1X_DEVICE "sound_pci_emu10k1_emu10k1x"
#define SOUND_PCI_EMU10K1_EMU10K1X(obj)        OBJECT_CHECK(Sound_pci_emu10k1_emu10k1xState, obj, TYPE_PCI_SOUND_PCI_EMU10K1_EMU10K1X_DEVICE)

static uint64_t sound_pci_emu10k1_emu10k1x_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    Sound_pci_emu10k1_emu10k1xState *sound_pci_emu10k1_emu10k1x = opaque;
    int i;

    if (target_value_reset) {
        sound_pci_emu10k1_emu10k1x->len = 0;
        target_value_reset = false;
    }

    if (!sound_pci_emu10k1_emu10k1x->is_probe_time) {
        for (i = 0; i < SOUND_PCI_EMU10K1_EMU10K1X_BUF_LEN; ++i) {
            sound_pci_emu10k1_emu10k1x->buf[i] = target_value[i];
        }
    } else {
        sound_pci_emu10k1_emu10k1x->is_probe_time--;
    }

    return sound_pci_emu10k1_emu10k1x->buf[(sound_pci_emu10k1_emu10k1x->len++) % SOUND_PCI_EMU10K1_EMU10K1X_BUF_LEN];
}

static void sound_pci_emu10k1_emu10k1x_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                unsigned size)
{
	return;
}

static const MemoryRegionOps sound_pci_emu10k1_emu10k1x_mmio_ops = {
    .read = sound_pci_emu10k1_emu10k1x_mmio_read,
    .write = sound_pci_emu10k1_emu10k1x_mmio_write,
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

static void pci_sound_pci_emu10k1_emu10k1x_realize(PCIDevice *pdev, Error **errp)
{
    Sound_pci_emu10k1_emu10k1xState *sound_pci_emu10k1_emu10k1x = SOUND_PCI_EMU10K1_EMU10K1X(pdev);
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

    sound_pci_emu10k1_emu10k1x->len = 0;
    sound_pci_emu10k1_emu10k1x->is_probe_time = 0;
	;

    memory_region_init_io(&sound_pci_emu10k1_emu10k1x->mmio[0], OBJECT(sound_pci_emu10k1_emu10k1x), &sound_pci_emu10k1_emu10k1x_mmio_ops, sound_pci_emu10k1_emu10k1x,
                    "sound_pci_emu10k1_emu10k1x-mmio0", 16 * MiB);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &sound_pci_emu10k1_emu10k1x->mmio[0]);
    memory_region_init_io(&sound_pci_emu10k1_emu10k1x->mmio[1], OBJECT(sound_pci_emu10k1_emu10k1x), &sound_pci_emu10k1_emu10k1x_mmio_ops, sound_pci_emu10k1_emu10k1x,
                    "sound_pci_emu10k1_emu10k1x-mmio1", 16 * MiB);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &sound_pci_emu10k1_emu10k1x->mmio[1]);
    memory_region_init_io(&sound_pci_emu10k1_emu10k1x->mmio[2], OBJECT(sound_pci_emu10k1_emu10k1x), &sound_pci_emu10k1_emu10k1x_mmio_ops, sound_pci_emu10k1_emu10k1x,
                    "sound_pci_emu10k1_emu10k1x-mmio2", 16 * MiB);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &sound_pci_emu10k1_emu10k1x->mmio[2]);
    memory_region_init_io(&sound_pci_emu10k1_emu10k1x->mmio[3], OBJECT(sound_pci_emu10k1_emu10k1x), &sound_pci_emu10k1_emu10k1x_mmio_ops, sound_pci_emu10k1_emu10k1x,
                    "sound_pci_emu10k1_emu10k1x-mmio3", 16 * MiB);
    pci_register_bar(pdev, 3, PCI_BASE_ADDRESS_SPACE_MEMORY, &sound_pci_emu10k1_emu10k1x->mmio[3]);
    memory_region_init_io(&sound_pci_emu10k1_emu10k1x->mmio[4], OBJECT(sound_pci_emu10k1_emu10k1x), &sound_pci_emu10k1_emu10k1x_mmio_ops, sound_pci_emu10k1_emu10k1x,
                    "sound_pci_emu10k1_emu10k1x-mmio4", 16 * MiB);
    pci_register_bar(pdev, 4, PCI_BASE_ADDRESS_SPACE_MEMORY, &sound_pci_emu10k1_emu10k1x->mmio[4]);
    memory_region_init_io(&sound_pci_emu10k1_emu10k1x->mmio[5], OBJECT(sound_pci_emu10k1_emu10k1x), &sound_pci_emu10k1_emu10k1x_mmio_ops, sound_pci_emu10k1_emu10k1x,
                    "sound_pci_emu10k1_emu10k1x-mmio5", 16 * MiB);
    pci_register_bar(pdev, 5, PCI_BASE_ADDRESS_SPACE_MEMORY, &sound_pci_emu10k1_emu10k1x->mmio[5]);
    memory_region_init_io(&sound_pci_emu10k1_emu10k1x->mmio[6], OBJECT(sound_pci_emu10k1_emu10k1x), &sound_pci_emu10k1_emu10k1x_mmio_ops, sound_pci_emu10k1_emu10k1x,
                    "sound_pci_emu10k1_emu10k1x-mmio5", 16 * MiB);
    pci_register_bar(pdev, 6, PCI_BASE_ADDRESS_SPACE_MEMORY, &sound_pci_emu10k1_emu10k1x->mmio[6]);
}

static void pci_sound_pci_emu10k1_emu10k1x_uninit(PCIDevice *pdev)
{
    msi_uninit(pdev);
}

static void sound_pci_emu10k1_emu10k1x_instance_init(Object *obj)
{
	return;
}

static void sound_pci_emu10k1_emu10k1x_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(class);

    k->realize = pci_sound_pci_emu10k1_emu10k1x_realize;
    k->exit = pci_sound_pci_emu10k1_emu10k1x_uninit;
    k->vendor_id = 0x1102;
    k->device_id = 0x6;
    k->revision = 0;
    k->subsystem_vendor_id = 0xffffffff;
    k->subsystem_id = 0xffffffff;
    k->class_id = 16777215;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void pci_sound_pci_emu10k1_emu10k1x_register_types(void)
{
    static InterfaceInfo interfaces[] = {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    };
    static const TypeInfo sound_pci_emu10k1_emu10k1x_info = {
        .name          = TYPE_PCI_SOUND_PCI_EMU10K1_EMU10K1X_DEVICE,
        .parent        = TYPE_PCI_DEVICE,
        .instance_size = sizeof(Sound_pci_emu10k1_emu10k1xState),
        .instance_init = sound_pci_emu10k1_emu10k1x_instance_init,
        .class_init    = sound_pci_emu10k1_emu10k1x_class_init,
        .interfaces = interfaces,
    };

    type_register_static(&sound_pci_emu10k1_emu10k1x_info);
}
type_init(pci_sound_pci_emu10k1_emu10k1x_register_types)
