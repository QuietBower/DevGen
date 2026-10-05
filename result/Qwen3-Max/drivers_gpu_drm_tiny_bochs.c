#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/pci/pci.h"
#include "hw/hw.h"
#include "hw/pci/msi.h"
#include "qemu/timer.h"
#include "qemu/main-loop.h" /* iothread mutex */
#include "qemu/module.h"
#include "qapi/visitor.h"

#define DRIVERS_GPU_DRM_TINY_BOCHS_BUF_LEN 1024

extern uint64_t target_value[DRIVERS_GPU_DRM_TINY_BOCHS_BUF_LEN];
extern bool target_value_reset;

typedef struct {
    PCIDevice pdev;
    MemoryRegion mmio[7];

    uint32_t len;
    uint64_t buf[DRIVERS_GPU_DRM_TINY_BOCHS_BUF_LEN];
    int is_probe_time;
} Drivers_gpu_drm_tiny_bochsState;

#define TYPE_PCI_DRIVERS_GPU_DRM_TINY_BOCHS_DEVICE "drivers_gpu_drm_tiny_bochs"
#define DRIVERS_GPU_DRM_TINY_BOCHS(obj)        OBJECT_CHECK(Drivers_gpu_drm_tiny_bochsState, obj, TYPE_PCI_DRIVERS_GPU_DRM_TINY_BOCHS_DEVICE)

static uint64_t drivers_gpu_drm_tiny_bochs_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    Drivers_gpu_drm_tiny_bochsState *drivers_gpu_drm_tiny_bochs = opaque;
    int i;

    if (target_value_reset) {
        drivers_gpu_drm_tiny_bochs->len = 0;
        target_value_reset = false;
    }

    if (!drivers_gpu_drm_tiny_bochs->is_probe_time) {
        for (i = 0; i < DRIVERS_GPU_DRM_TINY_BOCHS_BUF_LEN; ++i) {
            drivers_gpu_drm_tiny_bochs->buf[i] = target_value[i];
        }
    } else {
        drivers_gpu_drm_tiny_bochs->is_probe_time--;
    }

    return drivers_gpu_drm_tiny_bochs->buf[(drivers_gpu_drm_tiny_bochs->len++) % DRIVERS_GPU_DRM_TINY_BOCHS_BUF_LEN];
}

static void drivers_gpu_drm_tiny_bochs_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                unsigned size)
{
	return;
}

static const MemoryRegionOps drivers_gpu_drm_tiny_bochs_mmio_ops = {
    .read = drivers_gpu_drm_tiny_bochs_mmio_read,
    .write = drivers_gpu_drm_tiny_bochs_mmio_write,
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

static void pci_drivers_gpu_drm_tiny_bochs_realize(PCIDevice *pdev, Error **errp)
{
    Drivers_gpu_drm_tiny_bochsState *drivers_gpu_drm_tiny_bochs = DRIVERS_GPU_DRM_TINY_BOCHS(pdev);
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

    drivers_gpu_drm_tiny_bochs->len = 0;
    drivers_gpu_drm_tiny_bochs->is_probe_time = 11;
	drivers_gpu_drm_tiny_bochs->buf[0] = 0xff;
	drivers_gpu_drm_tiny_bochs->buf[1] = 0xff;
	drivers_gpu_drm_tiny_bochs->buf[2] = 0xb0;
	drivers_gpu_drm_tiny_bochs->buf[3] = 0xc0;
	drivers_gpu_drm_tiny_bochs->buf[4] = 0xff;
	drivers_gpu_drm_tiny_bochs->buf[5] = 0xff;
	drivers_gpu_drm_tiny_bochs->buf[6] = 0xff;
	drivers_gpu_drm_tiny_bochs->buf[7] = 0xff;
	drivers_gpu_drm_tiny_bochs->buf[8] = 0xff;
	drivers_gpu_drm_tiny_bochs->buf[9] = 0xff;
	drivers_gpu_drm_tiny_bochs->buf[10] = 0xff;;

    memory_region_init_io(&drivers_gpu_drm_tiny_bochs->mmio[0], OBJECT(drivers_gpu_drm_tiny_bochs), &drivers_gpu_drm_tiny_bochs_mmio_ops, drivers_gpu_drm_tiny_bochs,
                    "drivers_gpu_drm_tiny_bochs-mmio0", 16 * MiB);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_gpu_drm_tiny_bochs->mmio[0]);
    memory_region_init_io(&drivers_gpu_drm_tiny_bochs->mmio[1], OBJECT(drivers_gpu_drm_tiny_bochs), &drivers_gpu_drm_tiny_bochs_mmio_ops, drivers_gpu_drm_tiny_bochs,
                    "drivers_gpu_drm_tiny_bochs-mmio1", 16 * MiB);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_gpu_drm_tiny_bochs->mmio[1]);
    memory_region_init_io(&drivers_gpu_drm_tiny_bochs->mmio[2], OBJECT(drivers_gpu_drm_tiny_bochs), &drivers_gpu_drm_tiny_bochs_mmio_ops, drivers_gpu_drm_tiny_bochs,
                    "drivers_gpu_drm_tiny_bochs-mmio2", 16 * MiB);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_gpu_drm_tiny_bochs->mmio[2]);
    memory_region_init_io(&drivers_gpu_drm_tiny_bochs->mmio[3], OBJECT(drivers_gpu_drm_tiny_bochs), &drivers_gpu_drm_tiny_bochs_mmio_ops, drivers_gpu_drm_tiny_bochs,
                    "drivers_gpu_drm_tiny_bochs-mmio3", 16 * MiB);
    pci_register_bar(pdev, 3, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_gpu_drm_tiny_bochs->mmio[3]);
    memory_region_init_io(&drivers_gpu_drm_tiny_bochs->mmio[4], OBJECT(drivers_gpu_drm_tiny_bochs), &drivers_gpu_drm_tiny_bochs_mmio_ops, drivers_gpu_drm_tiny_bochs,
                    "drivers_gpu_drm_tiny_bochs-mmio4", 16 * MiB);
    pci_register_bar(pdev, 4, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_gpu_drm_tiny_bochs->mmio[4]);
    memory_region_init_io(&drivers_gpu_drm_tiny_bochs->mmio[5], OBJECT(drivers_gpu_drm_tiny_bochs), &drivers_gpu_drm_tiny_bochs_mmio_ops, drivers_gpu_drm_tiny_bochs,
                    "drivers_gpu_drm_tiny_bochs-mmio5", 16 * MiB);
    pci_register_bar(pdev, 5, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_gpu_drm_tiny_bochs->mmio[5]);
    memory_region_init_io(&drivers_gpu_drm_tiny_bochs->mmio[6], OBJECT(drivers_gpu_drm_tiny_bochs), &drivers_gpu_drm_tiny_bochs_mmio_ops, drivers_gpu_drm_tiny_bochs,
                    "drivers_gpu_drm_tiny_bochs-mmio5", 16 * MiB);
    pci_register_bar(pdev, 6, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_gpu_drm_tiny_bochs->mmio[6]);
}

static void pci_drivers_gpu_drm_tiny_bochs_uninit(PCIDevice *pdev)
{
    msi_uninit(pdev);
}

static void drivers_gpu_drm_tiny_bochs_instance_init(Object *obj)
{
	return;
}

static void drivers_gpu_drm_tiny_bochs_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(class);

    k->realize = pci_drivers_gpu_drm_tiny_bochs_realize;
    k->exit = pci_drivers_gpu_drm_tiny_bochs_uninit;
    k->vendor_id = 0x1234;
    k->device_id = 0x1111;
    k->revision = 0;
    k->subsystem_vendor_id = 0x1af4;
    k->subsystem_id = 0x1100;
    k->class_id = 16777215;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void pci_drivers_gpu_drm_tiny_bochs_register_types(void)
{
    static InterfaceInfo interfaces[] = {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    };
    static const TypeInfo drivers_gpu_drm_tiny_bochs_info = {
        .name          = TYPE_PCI_DRIVERS_GPU_DRM_TINY_BOCHS_DEVICE,
        .parent        = TYPE_PCI_DEVICE,
        .instance_size = sizeof(Drivers_gpu_drm_tiny_bochsState),
        .instance_init = drivers_gpu_drm_tiny_bochs_instance_init,
        .class_init    = drivers_gpu_drm_tiny_bochs_class_init,
        .interfaces = interfaces,
    };

    type_register_static(&drivers_gpu_drm_tiny_bochs_info);
}
type_init(pci_drivers_gpu_drm_tiny_bochs_register_types)
