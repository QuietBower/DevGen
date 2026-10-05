#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/pci/pci.h"
#include "hw/hw.h"
#include "hw/pci/msi.h"
#include "qemu/timer.h"
#include "qemu/main-loop.h" /* iothread mutex */
#include "qemu/module.h"
#include "qapi/visitor.h"

#define DRIVERS_NTB_HW_EPF_NTB_HW_EPF_BUF_LEN 1024

extern uint64_t target_value[DRIVERS_NTB_HW_EPF_NTB_HW_EPF_BUF_LEN];
extern bool target_value_reset;

typedef struct {
    PCIDevice pdev;
    MemoryRegion mmio[7];

    uint32_t len;
    uint64_t buf[DRIVERS_NTB_HW_EPF_NTB_HW_EPF_BUF_LEN];
    int is_probe_time;
} Drivers_ntb_hw_epf_ntb_hw_epfState;

#define TYPE_PCI_DRIVERS_NTB_HW_EPF_NTB_HW_EPF_DEVICE "drivers_ntb_hw_epf_ntb_hw_epf"
#define DRIVERS_NTB_HW_EPF_NTB_HW_EPF(obj)        OBJECT_CHECK(Drivers_ntb_hw_epf_ntb_hw_epfState, obj, TYPE_PCI_DRIVERS_NTB_HW_EPF_NTB_HW_EPF_DEVICE)

static uint64_t drivers_ntb_hw_epf_ntb_hw_epf_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    Drivers_ntb_hw_epf_ntb_hw_epfState *drivers_ntb_hw_epf_ntb_hw_epf = opaque;
    int i;

    if (target_value_reset) {
        drivers_ntb_hw_epf_ntb_hw_epf->len = 0;
        target_value_reset = false;
    }

    if (!drivers_ntb_hw_epf_ntb_hw_epf->is_probe_time) {
        for (i = 0; i < DRIVERS_NTB_HW_EPF_NTB_HW_EPF_BUF_LEN; ++i) {
            drivers_ntb_hw_epf_ntb_hw_epf->buf[i] = target_value[i];
        }
    } else {
        drivers_ntb_hw_epf_ntb_hw_epf->is_probe_time--;
    }

    return drivers_ntb_hw_epf_ntb_hw_epf->buf[(drivers_ntb_hw_epf_ntb_hw_epf->len++) % DRIVERS_NTB_HW_EPF_NTB_HW_EPF_BUF_LEN];
}

static void drivers_ntb_hw_epf_ntb_hw_epf_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                unsigned size)
{
	return;
}

static const MemoryRegionOps drivers_ntb_hw_epf_ntb_hw_epf_mmio_ops = {
    .read = drivers_ntb_hw_epf_ntb_hw_epf_mmio_read,
    .write = drivers_ntb_hw_epf_ntb_hw_epf_mmio_write,
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

static void pci_drivers_ntb_hw_epf_ntb_hw_epf_realize(PCIDevice *pdev, Error **errp)
{
    Drivers_ntb_hw_epf_ntb_hw_epfState *drivers_ntb_hw_epf_ntb_hw_epf = DRIVERS_NTB_HW_EPF_NTB_HW_EPF(pdev);
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

    drivers_ntb_hw_epf_ntb_hw_epf->len = 0;
    drivers_ntb_hw_epf_ntb_hw_epf->is_probe_time = 18;
	drivers_ntb_hw_epf_ntb_hw_epf->buf[0] = 0xff;
	drivers_ntb_hw_epf_ntb_hw_epf->buf[1] = 0xff;
	drivers_ntb_hw_epf_ntb_hw_epf->buf[2] = 0xff;
	drivers_ntb_hw_epf_ntb_hw_epf->buf[3] = 0xff;
	drivers_ntb_hw_epf_ntb_hw_epf->buf[4] = 0xff;
	drivers_ntb_hw_epf_ntb_hw_epf->buf[5] = 0xff;
	drivers_ntb_hw_epf_ntb_hw_epf->buf[6] = 0xff;
	drivers_ntb_hw_epf_ntb_hw_epf->buf[7] = 0xff;
	drivers_ntb_hw_epf_ntb_hw_epf->buf[8] = 0xff;
	drivers_ntb_hw_epf_ntb_hw_epf->buf[9] = 0xff;
	drivers_ntb_hw_epf_ntb_hw_epf->buf[10] = 0xff;
	drivers_ntb_hw_epf_ntb_hw_epf->buf[11] = 0xff;
	drivers_ntb_hw_epf_ntb_hw_epf->buf[12] = 0xff;
	drivers_ntb_hw_epf_ntb_hw_epf->buf[13] = 0xff;
	drivers_ntb_hw_epf_ntb_hw_epf->buf[14] = 0xff;
	drivers_ntb_hw_epf_ntb_hw_epf->buf[15] = 0xff;
	drivers_ntb_hw_epf_ntb_hw_epf->buf[16] = 0xff;
	drivers_ntb_hw_epf_ntb_hw_epf->buf[17] = 0xff;;

    memory_region_init_io(&drivers_ntb_hw_epf_ntb_hw_epf->mmio[0], OBJECT(drivers_ntb_hw_epf_ntb_hw_epf), &drivers_ntb_hw_epf_ntb_hw_epf_mmio_ops, drivers_ntb_hw_epf_ntb_hw_epf,
                    "drivers_ntb_hw_epf_ntb_hw_epf-mmio0", 16 * MiB);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_ntb_hw_epf_ntb_hw_epf->mmio[0]);
    memory_region_init_io(&drivers_ntb_hw_epf_ntb_hw_epf->mmio[1], OBJECT(drivers_ntb_hw_epf_ntb_hw_epf), &drivers_ntb_hw_epf_ntb_hw_epf_mmio_ops, drivers_ntb_hw_epf_ntb_hw_epf,
                    "drivers_ntb_hw_epf_ntb_hw_epf-mmio1", 16 * MiB);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_ntb_hw_epf_ntb_hw_epf->mmio[1]);
    memory_region_init_io(&drivers_ntb_hw_epf_ntb_hw_epf->mmio[2], OBJECT(drivers_ntb_hw_epf_ntb_hw_epf), &drivers_ntb_hw_epf_ntb_hw_epf_mmio_ops, drivers_ntb_hw_epf_ntb_hw_epf,
                    "drivers_ntb_hw_epf_ntb_hw_epf-mmio2", 16 * MiB);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_ntb_hw_epf_ntb_hw_epf->mmio[2]);
    memory_region_init_io(&drivers_ntb_hw_epf_ntb_hw_epf->mmio[3], OBJECT(drivers_ntb_hw_epf_ntb_hw_epf), &drivers_ntb_hw_epf_ntb_hw_epf_mmio_ops, drivers_ntb_hw_epf_ntb_hw_epf,
                    "drivers_ntb_hw_epf_ntb_hw_epf-mmio3", 16 * MiB);
    pci_register_bar(pdev, 3, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_ntb_hw_epf_ntb_hw_epf->mmio[3]);
    memory_region_init_io(&drivers_ntb_hw_epf_ntb_hw_epf->mmio[4], OBJECT(drivers_ntb_hw_epf_ntb_hw_epf), &drivers_ntb_hw_epf_ntb_hw_epf_mmio_ops, drivers_ntb_hw_epf_ntb_hw_epf,
                    "drivers_ntb_hw_epf_ntb_hw_epf-mmio4", 16 * MiB);
    pci_register_bar(pdev, 4, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_ntb_hw_epf_ntb_hw_epf->mmio[4]);
    memory_region_init_io(&drivers_ntb_hw_epf_ntb_hw_epf->mmio[5], OBJECT(drivers_ntb_hw_epf_ntb_hw_epf), &drivers_ntb_hw_epf_ntb_hw_epf_mmio_ops, drivers_ntb_hw_epf_ntb_hw_epf,
                    "drivers_ntb_hw_epf_ntb_hw_epf-mmio5", 16 * MiB);
    pci_register_bar(pdev, 5, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_ntb_hw_epf_ntb_hw_epf->mmio[5]);
    memory_region_init_io(&drivers_ntb_hw_epf_ntb_hw_epf->mmio[6], OBJECT(drivers_ntb_hw_epf_ntb_hw_epf), &drivers_ntb_hw_epf_ntb_hw_epf_mmio_ops, drivers_ntb_hw_epf_ntb_hw_epf,
                    "drivers_ntb_hw_epf_ntb_hw_epf-mmio5", 16 * MiB);
    pci_register_bar(pdev, 6, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_ntb_hw_epf_ntb_hw_epf->mmio[6]);
}

static void pci_drivers_ntb_hw_epf_ntb_hw_epf_uninit(PCIDevice *pdev)
{
    msi_uninit(pdev);
}

static void drivers_ntb_hw_epf_ntb_hw_epf_instance_init(Object *obj)
{
	return;
}

static void drivers_ntb_hw_epf_ntb_hw_epf_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(class);

    k->realize = pci_drivers_ntb_hw_epf_ntb_hw_epf_realize;
    k->exit = pci_drivers_ntb_hw_epf_ntb_hw_epf_uninit;
    k->vendor_id = 0x104c;
    k->device_id = 0xb00d;
    k->revision = 0;
    k->subsystem_vendor_id = 0xffffffff;
    k->subsystem_id = 0xffffffff;
    k->class_id = 16712960;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void pci_drivers_ntb_hw_epf_ntb_hw_epf_register_types(void)
{
    static InterfaceInfo interfaces[] = {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    };
    static const TypeInfo drivers_ntb_hw_epf_ntb_hw_epf_info = {
        .name          = TYPE_PCI_DRIVERS_NTB_HW_EPF_NTB_HW_EPF_DEVICE,
        .parent        = TYPE_PCI_DEVICE,
        .instance_size = sizeof(Drivers_ntb_hw_epf_ntb_hw_epfState),
        .instance_init = drivers_ntb_hw_epf_ntb_hw_epf_instance_init,
        .class_init    = drivers_ntb_hw_epf_ntb_hw_epf_class_init,
        .interfaces = interfaces,
    };

    type_register_static(&drivers_ntb_hw_epf_ntb_hw_epf_info);
}
type_init(pci_drivers_ntb_hw_epf_ntb_hw_epf_register_types)
