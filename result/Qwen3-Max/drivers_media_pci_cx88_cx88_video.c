#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/pci/pci.h"
#include "hw/hw.h"
#include "hw/pci/msi.h"
#include "qemu/timer.h"
#include "qemu/main-loop.h" /* iothread mutex */
#include "qemu/module.h"
#include "qapi/visitor.h"

#define DRIVERS_MEDIA_PCI_CX88_CX88_VIDEO_BUF_LEN 1024

extern uint64_t target_value[DRIVERS_MEDIA_PCI_CX88_CX88_VIDEO_BUF_LEN];
extern bool target_value_reset;

typedef struct {
    PCIDevice pdev;
    MemoryRegion mmio[7];

    uint32_t len;
    uint64_t buf[DRIVERS_MEDIA_PCI_CX88_CX88_VIDEO_BUF_LEN];
    int is_probe_time;
} Drivers_media_pci_cx88_cx88_videoState;

#define TYPE_PCI_DRIVERS_MEDIA_PCI_CX88_CX88_VIDEO_DEVICE "drivers_media_pci_cx88_cx88_video"
#define DRIVERS_MEDIA_PCI_CX88_CX88_VIDEO(obj)        OBJECT_CHECK(Drivers_media_pci_cx88_cx88_videoState, obj, TYPE_PCI_DRIVERS_MEDIA_PCI_CX88_CX88_VIDEO_DEVICE)

static uint64_t drivers_media_pci_cx88_cx88_video_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    Drivers_media_pci_cx88_cx88_videoState *drivers_media_pci_cx88_cx88_video = opaque;
    int i;

    if (target_value_reset) {
        drivers_media_pci_cx88_cx88_video->len = 0;
        target_value_reset = false;
    }

    if (!drivers_media_pci_cx88_cx88_video->is_probe_time) {
        for (i = 0; i < DRIVERS_MEDIA_PCI_CX88_CX88_VIDEO_BUF_LEN; ++i) {
            drivers_media_pci_cx88_cx88_video->buf[i] = target_value[i];
        }
    } else {
        drivers_media_pci_cx88_cx88_video->is_probe_time--;
    }

    return drivers_media_pci_cx88_cx88_video->buf[(drivers_media_pci_cx88_cx88_video->len++) % DRIVERS_MEDIA_PCI_CX88_CX88_VIDEO_BUF_LEN];
}

static void drivers_media_pci_cx88_cx88_video_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                unsigned size)
{
	return;
}

static const MemoryRegionOps drivers_media_pci_cx88_cx88_video_mmio_ops = {
    .read = drivers_media_pci_cx88_cx88_video_mmio_read,
    .write = drivers_media_pci_cx88_cx88_video_mmio_write,
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

static void pci_drivers_media_pci_cx88_cx88_video_realize(PCIDevice *pdev, Error **errp)
{
    Drivers_media_pci_cx88_cx88_videoState *drivers_media_pci_cx88_cx88_video = DRIVERS_MEDIA_PCI_CX88_CX88_VIDEO(pdev);
    uint8_t *pci_conf = pdev->config;
	int pos;

    pci_config_set_interrupt_pin(pci_conf, 1);
	pci_set_byte(&pci_conf[13], 255);
	

    if (msi_init(pdev, 0, 1, true, false, errp)) {
        return;
    }
	
	pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
	pcie_endpoint_cap_init(pdev, 0);

	pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
	pdev->exp.pm_cap = pos;
	pci_set_word(pdev->config + pos + PCI_PM_PMC, 0x3);

    drivers_media_pci_cx88_cx88_video->len = 0;
    drivers_media_pci_cx88_cx88_video->is_probe_time = 44;
	drivers_media_pci_cx88_cx88_video->buf[0] = 0xff;
	drivers_media_pci_cx88_cx88_video->buf[1] = 0xff;
	drivers_media_pci_cx88_cx88_video->buf[2] = 0xff;
	drivers_media_pci_cx88_cx88_video->buf[3] = 0xff;
	drivers_media_pci_cx88_cx88_video->buf[4] = 0xff;
	drivers_media_pci_cx88_cx88_video->buf[5] = 0xff;
	drivers_media_pci_cx88_cx88_video->buf[6] = 0xff;
	drivers_media_pci_cx88_cx88_video->buf[7] = 0xff;
	drivers_media_pci_cx88_cx88_video->buf[8] = 0xff;
	drivers_media_pci_cx88_cx88_video->buf[9] = 0xff;
	drivers_media_pci_cx88_cx88_video->buf[10] = 0xff;
	drivers_media_pci_cx88_cx88_video->buf[11] = 0xff;
	drivers_media_pci_cx88_cx88_video->buf[12] = 0xff;
	drivers_media_pci_cx88_cx88_video->buf[13] = 0xff;
	drivers_media_pci_cx88_cx88_video->buf[14] = 0xff;
	drivers_media_pci_cx88_cx88_video->buf[15] = 0xff;
	drivers_media_pci_cx88_cx88_video->buf[16] = 0xff;
	drivers_media_pci_cx88_cx88_video->buf[17] = 0xff;
	drivers_media_pci_cx88_cx88_video->buf[18] = 0xff;
	drivers_media_pci_cx88_cx88_video->buf[19] = 0xff;
	drivers_media_pci_cx88_cx88_video->buf[20] = 0xff;
	drivers_media_pci_cx88_cx88_video->buf[21] = 0xff;
	drivers_media_pci_cx88_cx88_video->buf[22] = 0xff;
	drivers_media_pci_cx88_cx88_video->buf[23] = 0xff;
	drivers_media_pci_cx88_cx88_video->buf[24] = 0xff;
	drivers_media_pci_cx88_cx88_video->buf[25] = 0xff;
	drivers_media_pci_cx88_cx88_video->buf[26] = 0xff;
	drivers_media_pci_cx88_cx88_video->buf[27] = 0xff;
	drivers_media_pci_cx88_cx88_video->buf[28] = 0xff;
	drivers_media_pci_cx88_cx88_video->buf[29] = 0xff;
	drivers_media_pci_cx88_cx88_video->buf[30] = 0xff;
	drivers_media_pci_cx88_cx88_video->buf[31] = 0xff;
	drivers_media_pci_cx88_cx88_video->buf[32] = 0xff;
	drivers_media_pci_cx88_cx88_video->buf[33] = 0xff;
	drivers_media_pci_cx88_cx88_video->buf[34] = 0xff;
	drivers_media_pci_cx88_cx88_video->buf[35] = 0xff;
	drivers_media_pci_cx88_cx88_video->buf[36] = 0xff;
	drivers_media_pci_cx88_cx88_video->buf[37] = 0xff;
	drivers_media_pci_cx88_cx88_video->buf[38] = 0xff;
	drivers_media_pci_cx88_cx88_video->buf[39] = 0xff;
	drivers_media_pci_cx88_cx88_video->buf[40] = 0xff;
	drivers_media_pci_cx88_cx88_video->buf[41] = 0xff;
	drivers_media_pci_cx88_cx88_video->buf[42] = 0xff;
	drivers_media_pci_cx88_cx88_video->buf[43] = 0xff;;

    memory_region_init_io(&drivers_media_pci_cx88_cx88_video->mmio[0], OBJECT(drivers_media_pci_cx88_cx88_video), &drivers_media_pci_cx88_cx88_video_mmio_ops, drivers_media_pci_cx88_cx88_video,
                    "drivers_media_pci_cx88_cx88_video-mmio0", 16 * MiB);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_media_pci_cx88_cx88_video->mmio[0]);
    memory_region_init_io(&drivers_media_pci_cx88_cx88_video->mmio[1], OBJECT(drivers_media_pci_cx88_cx88_video), &drivers_media_pci_cx88_cx88_video_mmio_ops, drivers_media_pci_cx88_cx88_video,
                    "drivers_media_pci_cx88_cx88_video-mmio1", 16 * MiB);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_media_pci_cx88_cx88_video->mmio[1]);
    memory_region_init_io(&drivers_media_pci_cx88_cx88_video->mmio[2], OBJECT(drivers_media_pci_cx88_cx88_video), &drivers_media_pci_cx88_cx88_video_mmio_ops, drivers_media_pci_cx88_cx88_video,
                    "drivers_media_pci_cx88_cx88_video-mmio2", 16 * MiB);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_media_pci_cx88_cx88_video->mmio[2]);
    memory_region_init_io(&drivers_media_pci_cx88_cx88_video->mmio[3], OBJECT(drivers_media_pci_cx88_cx88_video), &drivers_media_pci_cx88_cx88_video_mmio_ops, drivers_media_pci_cx88_cx88_video,
                    "drivers_media_pci_cx88_cx88_video-mmio3", 16 * MiB);
    pci_register_bar(pdev, 3, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_media_pci_cx88_cx88_video->mmio[3]);
    memory_region_init_io(&drivers_media_pci_cx88_cx88_video->mmio[4], OBJECT(drivers_media_pci_cx88_cx88_video), &drivers_media_pci_cx88_cx88_video_mmio_ops, drivers_media_pci_cx88_cx88_video,
                    "drivers_media_pci_cx88_cx88_video-mmio4", 16 * MiB);
    pci_register_bar(pdev, 4, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_media_pci_cx88_cx88_video->mmio[4]);
    memory_region_init_io(&drivers_media_pci_cx88_cx88_video->mmio[5], OBJECT(drivers_media_pci_cx88_cx88_video), &drivers_media_pci_cx88_cx88_video_mmio_ops, drivers_media_pci_cx88_cx88_video,
                    "drivers_media_pci_cx88_cx88_video-mmio5", 16 * MiB);
    pci_register_bar(pdev, 5, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_media_pci_cx88_cx88_video->mmio[5]);
    memory_region_init_io(&drivers_media_pci_cx88_cx88_video->mmio[6], OBJECT(drivers_media_pci_cx88_cx88_video), &drivers_media_pci_cx88_cx88_video_mmio_ops, drivers_media_pci_cx88_cx88_video,
                    "drivers_media_pci_cx88_cx88_video-mmio5", 16 * MiB);
    pci_register_bar(pdev, 6, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_media_pci_cx88_cx88_video->mmio[6]);
}

static void pci_drivers_media_pci_cx88_cx88_video_uninit(PCIDevice *pdev)
{
    msi_uninit(pdev);
}

static void drivers_media_pci_cx88_cx88_video_instance_init(Object *obj)
{
	return;
}

static void drivers_media_pci_cx88_cx88_video_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(class);

    k->realize = pci_drivers_media_pci_cx88_cx88_video_realize;
    k->exit = pci_drivers_media_pci_cx88_cx88_video_uninit;
    k->vendor_id = 0x14f1;
    k->device_id = 0x8800;
    k->revision = 0;
    k->subsystem_vendor_id = 0xffffffff;
    k->subsystem_id = 0xffffffff;
    k->class_id = 16777215;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void pci_drivers_media_pci_cx88_cx88_video_register_types(void)
{
    static InterfaceInfo interfaces[] = {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    };
    static const TypeInfo drivers_media_pci_cx88_cx88_video_info = {
        .name          = TYPE_PCI_DRIVERS_MEDIA_PCI_CX88_CX88_VIDEO_DEVICE,
        .parent        = TYPE_PCI_DEVICE,
        .instance_size = sizeof(Drivers_media_pci_cx88_cx88_videoState),
        .instance_init = drivers_media_pci_cx88_cx88_video_instance_init,
        .class_init    = drivers_media_pci_cx88_cx88_video_class_init,
        .interfaces = interfaces,
    };

    type_register_static(&drivers_media_pci_cx88_cx88_video_info);
}
type_init(pci_drivers_media_pci_cx88_cx88_video_register_types)
