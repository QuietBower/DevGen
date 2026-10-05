#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/pci/pci.h"
#include "hw/hw.h"
#include "hw/pci/msi.h"
#include "qemu/timer.h"
#include "qemu/main-loop.h" /* iothread mutex */
#include "qemu/module.h"
#include "qapi/visitor.h"

#define DRIVERS_GPU_DRM_AST_AST_DRV_BUF_LEN 1024

extern uint64_t target_value[DRIVERS_GPU_DRM_AST_AST_DRV_BUF_LEN];
extern bool target_value_reset;

typedef struct {
    PCIDevice pdev;
    MemoryRegion mmio[7];

    uint32_t len;
    uint64_t buf[DRIVERS_GPU_DRM_AST_AST_DRV_BUF_LEN];
    int is_probe_time;
} Drivers_gpu_drm_ast_ast_drvState;

#define TYPE_PCI_DRIVERS_GPU_DRM_AST_AST_DRV_DEVICE "drivers_gpu_drm_ast_ast_drv"
#define DRIVERS_GPU_DRM_AST_AST_DRV(obj)        OBJECT_CHECK(Drivers_gpu_drm_ast_ast_drvState, obj, TYPE_PCI_DRIVERS_GPU_DRM_AST_AST_DRV_DEVICE)

static uint64_t drivers_gpu_drm_ast_ast_drv_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    Drivers_gpu_drm_ast_ast_drvState *drivers_gpu_drm_ast_ast_drv = opaque;
    int i;

    if (target_value_reset) {
        drivers_gpu_drm_ast_ast_drv->len = 0;
        target_value_reset = false;
    }

    if (!drivers_gpu_drm_ast_ast_drv->is_probe_time) {
        for (i = 0; i < DRIVERS_GPU_DRM_AST_AST_DRV_BUF_LEN; ++i) {
            drivers_gpu_drm_ast_ast_drv->buf[i] = target_value[i];
        }
    } else {
        drivers_gpu_drm_ast_ast_drv->is_probe_time--;
    }

    return drivers_gpu_drm_ast_ast_drv->buf[(drivers_gpu_drm_ast_ast_drv->len++) % DRIVERS_GPU_DRM_AST_AST_DRV_BUF_LEN];
}

static void drivers_gpu_drm_ast_ast_drv_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                unsigned size)
{
	return;
}

static const MemoryRegionOps drivers_gpu_drm_ast_ast_drv_mmio_ops = {
    .read = drivers_gpu_drm_ast_ast_drv_mmio_read,
    .write = drivers_gpu_drm_ast_ast_drv_mmio_write,
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

static void pci_drivers_gpu_drm_ast_ast_drv_realize(PCIDevice *pdev, Error **errp)
{
    Drivers_gpu_drm_ast_ast_drvState *drivers_gpu_drm_ast_ast_drv = DRIVERS_GPU_DRM_AST_AST_DRV(pdev);
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

    drivers_gpu_drm_ast_ast_drv->len = 0;
    drivers_gpu_drm_ast_ast_drv->is_probe_time = 0;
	;

    memory_region_init_io(&drivers_gpu_drm_ast_ast_drv->mmio[0], OBJECT(drivers_gpu_drm_ast_ast_drv), &drivers_gpu_drm_ast_ast_drv_mmio_ops, drivers_gpu_drm_ast_ast_drv,
                    "drivers_gpu_drm_ast_ast_drv-mmio0", 16 * MiB);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_gpu_drm_ast_ast_drv->mmio[0]);
    memory_region_init_io(&drivers_gpu_drm_ast_ast_drv->mmio[1], OBJECT(drivers_gpu_drm_ast_ast_drv), &drivers_gpu_drm_ast_ast_drv_mmio_ops, drivers_gpu_drm_ast_ast_drv,
                    "drivers_gpu_drm_ast_ast_drv-mmio1", 16 * MiB);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_gpu_drm_ast_ast_drv->mmio[1]);
    memory_region_init_io(&drivers_gpu_drm_ast_ast_drv->mmio[2], OBJECT(drivers_gpu_drm_ast_ast_drv), &drivers_gpu_drm_ast_ast_drv_mmio_ops, drivers_gpu_drm_ast_ast_drv,
                    "drivers_gpu_drm_ast_ast_drv-mmio2", 16 * MiB);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_gpu_drm_ast_ast_drv->mmio[2]);
    memory_region_init_io(&drivers_gpu_drm_ast_ast_drv->mmio[3], OBJECT(drivers_gpu_drm_ast_ast_drv), &drivers_gpu_drm_ast_ast_drv_mmio_ops, drivers_gpu_drm_ast_ast_drv,
                    "drivers_gpu_drm_ast_ast_drv-mmio3", 16 * MiB);
    pci_register_bar(pdev, 3, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_gpu_drm_ast_ast_drv->mmio[3]);
    memory_region_init_io(&drivers_gpu_drm_ast_ast_drv->mmio[4], OBJECT(drivers_gpu_drm_ast_ast_drv), &drivers_gpu_drm_ast_ast_drv_mmio_ops, drivers_gpu_drm_ast_ast_drv,
                    "drivers_gpu_drm_ast_ast_drv-mmio4", 16 * MiB);
    pci_register_bar(pdev, 4, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_gpu_drm_ast_ast_drv->mmio[4]);
    memory_region_init_io(&drivers_gpu_drm_ast_ast_drv->mmio[5], OBJECT(drivers_gpu_drm_ast_ast_drv), &drivers_gpu_drm_ast_ast_drv_mmio_ops, drivers_gpu_drm_ast_ast_drv,
                    "drivers_gpu_drm_ast_ast_drv-mmio5", 16 * MiB);
    pci_register_bar(pdev, 5, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_gpu_drm_ast_ast_drv->mmio[5]);
    memory_region_init_io(&drivers_gpu_drm_ast_ast_drv->mmio[6], OBJECT(drivers_gpu_drm_ast_ast_drv), &drivers_gpu_drm_ast_ast_drv_mmio_ops, drivers_gpu_drm_ast_ast_drv,
                    "drivers_gpu_drm_ast_ast_drv-mmio5", 16 * MiB);
    pci_register_bar(pdev, 6, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_gpu_drm_ast_ast_drv->mmio[6]);
}

static void pci_drivers_gpu_drm_ast_ast_drv_uninit(PCIDevice *pdev)
{
    msi_uninit(pdev);
}

static void drivers_gpu_drm_ast_ast_drv_instance_init(Object *obj)
{
	return;
}

static void drivers_gpu_drm_ast_ast_drv_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(class);

    k->realize = pci_drivers_gpu_drm_ast_ast_drv_realize;
    k->exit = pci_drivers_gpu_drm_ast_ast_drv_uninit;
    k->vendor_id = 0x1a03;
    k->device_id = 0x2000;
    k->revision = 0;
    k->subsystem_vendor_id = 0xffffffff;
    k->subsystem_id = 0xffffffff;
    k->class_id = 16712703;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void pci_drivers_gpu_drm_ast_ast_drv_register_types(void)
{
    static InterfaceInfo interfaces[] = {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    };
    static const TypeInfo drivers_gpu_drm_ast_ast_drv_info = {
        .name          = TYPE_PCI_DRIVERS_GPU_DRM_AST_AST_DRV_DEVICE,
        .parent        = TYPE_PCI_DEVICE,
        .instance_size = sizeof(Drivers_gpu_drm_ast_ast_drvState),
        .instance_init = drivers_gpu_drm_ast_ast_drv_instance_init,
        .class_init    = drivers_gpu_drm_ast_ast_drv_class_init,
        .interfaces = interfaces,
    };

    type_register_static(&drivers_gpu_drm_ast_ast_drv_info);
}
type_init(pci_drivers_gpu_drm_ast_ast_drv_register_types)
