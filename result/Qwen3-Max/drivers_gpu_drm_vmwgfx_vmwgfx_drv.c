#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/pci/pci.h"
#include "hw/hw.h"
#include "hw/pci/msi.h"
#include "qemu/timer.h"
#include "qemu/main-loop.h" /* iothread mutex */
#include "qemu/module.h"
#include "qapi/visitor.h"

#define DRIVERS_GPU_DRM_VMWGFX_VMWGFX_DRV_BUF_LEN 1024

extern uint64_t target_value[DRIVERS_GPU_DRM_VMWGFX_VMWGFX_DRV_BUF_LEN];
extern bool target_value_reset;

typedef struct {
    PCIDevice pdev;
    MemoryRegion mmio[7];

    uint32_t len;
    uint64_t buf[DRIVERS_GPU_DRM_VMWGFX_VMWGFX_DRV_BUF_LEN];
    int is_probe_time;
} Drivers_gpu_drm_vmwgfx_vmwgfx_drvState;

#define TYPE_PCI_DRIVERS_GPU_DRM_VMWGFX_VMWGFX_DRV_DEVICE "drivers_gpu_drm_vmwgfx_vmwgfx_drv"
#define DRIVERS_GPU_DRM_VMWGFX_VMWGFX_DRV(obj)        OBJECT_CHECK(Drivers_gpu_drm_vmwgfx_vmwgfx_drvState, obj, TYPE_PCI_DRIVERS_GPU_DRM_VMWGFX_VMWGFX_DRV_DEVICE)

static uint64_t drivers_gpu_drm_vmwgfx_vmwgfx_drv_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    Drivers_gpu_drm_vmwgfx_vmwgfx_drvState *drivers_gpu_drm_vmwgfx_vmwgfx_drv = opaque;
    int i;

    if (target_value_reset) {
        drivers_gpu_drm_vmwgfx_vmwgfx_drv->len = 0;
        target_value_reset = false;
    }

    if (!drivers_gpu_drm_vmwgfx_vmwgfx_drv->is_probe_time) {
        for (i = 0; i < DRIVERS_GPU_DRM_VMWGFX_VMWGFX_DRV_BUF_LEN; ++i) {
            drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[i] = target_value[i];
        }
    } else {
        drivers_gpu_drm_vmwgfx_vmwgfx_drv->is_probe_time--;
    }

    return drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[(drivers_gpu_drm_vmwgfx_vmwgfx_drv->len++) % DRIVERS_GPU_DRM_VMWGFX_VMWGFX_DRV_BUF_LEN];
}

static void drivers_gpu_drm_vmwgfx_vmwgfx_drv_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                unsigned size)
{
	return;
}

static const MemoryRegionOps drivers_gpu_drm_vmwgfx_vmwgfx_drv_mmio_ops = {
    .read = drivers_gpu_drm_vmwgfx_vmwgfx_drv_mmio_read,
    .write = drivers_gpu_drm_vmwgfx_vmwgfx_drv_mmio_write,
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

static void pci_drivers_gpu_drm_vmwgfx_vmwgfx_drv_realize(PCIDevice *pdev, Error **errp)
{
    Drivers_gpu_drm_vmwgfx_vmwgfx_drvState *drivers_gpu_drm_vmwgfx_vmwgfx_drv = DRIVERS_GPU_DRM_VMWGFX_VMWGFX_DRV(pdev);
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

    drivers_gpu_drm_vmwgfx_vmwgfx_drv->len = 0;
    drivers_gpu_drm_vmwgfx_vmwgfx_drv->is_probe_time = 104;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[0] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[1] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[2] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[3] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[4] = 0x90;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[5] = 0x0;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[6] = 0x0;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[7] = 0x3;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[8] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[9] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[10] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[11] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[12] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[13] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[14] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[15] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[16] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[17] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[18] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[19] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[20] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[21] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[22] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[23] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[24] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[25] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[26] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[27] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[28] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[29] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[30] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[31] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[32] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[33] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[34] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[35] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[36] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[37] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[38] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[39] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[40] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[41] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[42] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[43] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[44] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[45] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[46] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[47] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[48] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[49] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[50] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[51] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[52] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[53] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[54] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[55] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[56] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[57] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[58] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[59] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[60] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[61] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[62] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[63] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[64] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[65] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[66] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[67] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[68] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[69] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[70] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[71] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[72] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[73] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[74] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[75] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[76] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[77] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[78] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[79] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[80] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[81] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[82] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[83] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[84] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[85] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[86] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[87] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[88] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[89] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[90] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[91] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[92] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[93] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[94] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[95] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[96] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[97] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[98] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[99] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[100] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[101] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[102] = 0xff;
	drivers_gpu_drm_vmwgfx_vmwgfx_drv->buf[103] = 0xff;;

    memory_region_init_io(&drivers_gpu_drm_vmwgfx_vmwgfx_drv->mmio[0], OBJECT(drivers_gpu_drm_vmwgfx_vmwgfx_drv), &drivers_gpu_drm_vmwgfx_vmwgfx_drv_mmio_ops, drivers_gpu_drm_vmwgfx_vmwgfx_drv,
                    "drivers_gpu_drm_vmwgfx_vmwgfx_drv-mmio0", 16 * MiB);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_gpu_drm_vmwgfx_vmwgfx_drv->mmio[0]);
    memory_region_init_io(&drivers_gpu_drm_vmwgfx_vmwgfx_drv->mmio[1], OBJECT(drivers_gpu_drm_vmwgfx_vmwgfx_drv), &drivers_gpu_drm_vmwgfx_vmwgfx_drv_mmio_ops, drivers_gpu_drm_vmwgfx_vmwgfx_drv,
                    "drivers_gpu_drm_vmwgfx_vmwgfx_drv-mmio1", 16 * MiB);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_gpu_drm_vmwgfx_vmwgfx_drv->mmio[1]);
    memory_region_init_io(&drivers_gpu_drm_vmwgfx_vmwgfx_drv->mmio[2], OBJECT(drivers_gpu_drm_vmwgfx_vmwgfx_drv), &drivers_gpu_drm_vmwgfx_vmwgfx_drv_mmio_ops, drivers_gpu_drm_vmwgfx_vmwgfx_drv,
                    "drivers_gpu_drm_vmwgfx_vmwgfx_drv-mmio2", 16 * MiB);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_gpu_drm_vmwgfx_vmwgfx_drv->mmio[2]);
    memory_region_init_io(&drivers_gpu_drm_vmwgfx_vmwgfx_drv->mmio[3], OBJECT(drivers_gpu_drm_vmwgfx_vmwgfx_drv), &drivers_gpu_drm_vmwgfx_vmwgfx_drv_mmio_ops, drivers_gpu_drm_vmwgfx_vmwgfx_drv,
                    "drivers_gpu_drm_vmwgfx_vmwgfx_drv-mmio3", 16 * MiB);
    pci_register_bar(pdev, 3, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_gpu_drm_vmwgfx_vmwgfx_drv->mmio[3]);
    memory_region_init_io(&drivers_gpu_drm_vmwgfx_vmwgfx_drv->mmio[4], OBJECT(drivers_gpu_drm_vmwgfx_vmwgfx_drv), &drivers_gpu_drm_vmwgfx_vmwgfx_drv_mmio_ops, drivers_gpu_drm_vmwgfx_vmwgfx_drv,
                    "drivers_gpu_drm_vmwgfx_vmwgfx_drv-mmio4", 16 * MiB);
    pci_register_bar(pdev, 4, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_gpu_drm_vmwgfx_vmwgfx_drv->mmio[4]);
    memory_region_init_io(&drivers_gpu_drm_vmwgfx_vmwgfx_drv->mmio[5], OBJECT(drivers_gpu_drm_vmwgfx_vmwgfx_drv), &drivers_gpu_drm_vmwgfx_vmwgfx_drv_mmio_ops, drivers_gpu_drm_vmwgfx_vmwgfx_drv,
                    "drivers_gpu_drm_vmwgfx_vmwgfx_drv-mmio5", 16 * MiB);
    pci_register_bar(pdev, 5, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_gpu_drm_vmwgfx_vmwgfx_drv->mmio[5]);
    memory_region_init_io(&drivers_gpu_drm_vmwgfx_vmwgfx_drv->mmio[6], OBJECT(drivers_gpu_drm_vmwgfx_vmwgfx_drv), &drivers_gpu_drm_vmwgfx_vmwgfx_drv_mmio_ops, drivers_gpu_drm_vmwgfx_vmwgfx_drv,
                    "drivers_gpu_drm_vmwgfx_vmwgfx_drv-mmio5", 16 * MiB);
    pci_register_bar(pdev, 6, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_gpu_drm_vmwgfx_vmwgfx_drv->mmio[6]);
}

static void pci_drivers_gpu_drm_vmwgfx_vmwgfx_drv_uninit(PCIDevice *pdev)
{
    msi_uninit(pdev);
}

static void drivers_gpu_drm_vmwgfx_vmwgfx_drv_instance_init(Object *obj)
{
	return;
}

static void drivers_gpu_drm_vmwgfx_vmwgfx_drv_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(class);

    k->realize = pci_drivers_gpu_drm_vmwgfx_vmwgfx_drv_realize;
    k->exit = pci_drivers_gpu_drm_vmwgfx_vmwgfx_drv_uninit;
    k->vendor_id = 0x15ad;
    k->device_id = 0x405;
    k->revision = 0;
    k->subsystem_vendor_id = 0xffffffff;
    k->subsystem_id = 0xffffffff;
    k->class_id = 16777215;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void pci_drivers_gpu_drm_vmwgfx_vmwgfx_drv_register_types(void)
{
    static InterfaceInfo interfaces[] = {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    };
    static const TypeInfo drivers_gpu_drm_vmwgfx_vmwgfx_drv_info = {
        .name          = TYPE_PCI_DRIVERS_GPU_DRM_VMWGFX_VMWGFX_DRV_DEVICE,
        .parent        = TYPE_PCI_DEVICE,
        .instance_size = sizeof(Drivers_gpu_drm_vmwgfx_vmwgfx_drvState),
        .instance_init = drivers_gpu_drm_vmwgfx_vmwgfx_drv_instance_init,
        .class_init    = drivers_gpu_drm_vmwgfx_vmwgfx_drv_class_init,
        .interfaces = interfaces,
    };

    type_register_static(&drivers_gpu_drm_vmwgfx_vmwgfx_drv_info);
}
type_init(pci_drivers_gpu_drm_vmwgfx_vmwgfx_drv_register_types)
