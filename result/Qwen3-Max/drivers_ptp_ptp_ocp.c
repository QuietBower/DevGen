#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/pci/pci.h"
#include "hw/hw.h"
#include "hw/pci/msi.h"
#include "qemu/timer.h"
#include "qemu/main-loop.h" /* iothread mutex */
#include "qemu/module.h"
#include "qapi/visitor.h"

#define DRIVERS_PTP_PTP_OCP_BUF_LEN 1024

extern uint64_t target_value[DRIVERS_PTP_PTP_OCP_BUF_LEN];
extern bool target_value_reset;

typedef struct {
    PCIDevice pdev;
    MemoryRegion mmio[7];

    uint32_t len;
    uint64_t buf[DRIVERS_PTP_PTP_OCP_BUF_LEN];
    int is_probe_time;
} Drivers_ptp_ptp_ocpState;

#define TYPE_PCI_DRIVERS_PTP_PTP_OCP_DEVICE "drivers_ptp_ptp_ocp"
#define DRIVERS_PTP_PTP_OCP(obj)        OBJECT_CHECK(Drivers_ptp_ptp_ocpState, obj, TYPE_PCI_DRIVERS_PTP_PTP_OCP_DEVICE)

static uint64_t drivers_ptp_ptp_ocp_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    Drivers_ptp_ptp_ocpState *drivers_ptp_ptp_ocp = opaque;
    int i;

    if (target_value_reset) {
        drivers_ptp_ptp_ocp->len = 0;
        target_value_reset = false;
    }

    if (!drivers_ptp_ptp_ocp->is_probe_time) {
        for (i = 0; i < DRIVERS_PTP_PTP_OCP_BUF_LEN; ++i) {
            drivers_ptp_ptp_ocp->buf[i] = target_value[i];
        }
    } else {
        drivers_ptp_ptp_ocp->is_probe_time--;
    }

    return drivers_ptp_ptp_ocp->buf[(drivers_ptp_ptp_ocp->len++) % DRIVERS_PTP_PTP_OCP_BUF_LEN];
}

static void drivers_ptp_ptp_ocp_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                unsigned size)
{
	return;
}

static const MemoryRegionOps drivers_ptp_ptp_ocp_mmio_ops = {
    .read = drivers_ptp_ptp_ocp_mmio_read,
    .write = drivers_ptp_ptp_ocp_mmio_write,
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

static void pci_drivers_ptp_ptp_ocp_realize(PCIDevice *pdev, Error **errp)
{
    Drivers_ptp_ptp_ocpState *drivers_ptp_ptp_ocp = DRIVERS_PTP_PTP_OCP(pdev);
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

    drivers_ptp_ptp_ocp->len = 0;
    drivers_ptp_ptp_ocp->is_probe_time = 24;
	drivers_ptp_ptp_ocp->buf[0] = 0xff;
	drivers_ptp_ptp_ocp->buf[1] = 0xff;
	drivers_ptp_ptp_ocp->buf[2] = 0xff;
	drivers_ptp_ptp_ocp->buf[3] = 0xff;
	drivers_ptp_ptp_ocp->buf[4] = 0xff;
	drivers_ptp_ptp_ocp->buf[5] = 0xff;
	drivers_ptp_ptp_ocp->buf[6] = 0xff;
	drivers_ptp_ptp_ocp->buf[7] = 0xff;
	drivers_ptp_ptp_ocp->buf[8] = 0xff;
	drivers_ptp_ptp_ocp->buf[9] = 0xff;
	drivers_ptp_ptp_ocp->buf[10] = 0xff;
	drivers_ptp_ptp_ocp->buf[11] = 0xff;
	drivers_ptp_ptp_ocp->buf[12] = 0xff;
	drivers_ptp_ptp_ocp->buf[13] = 0xff;
	drivers_ptp_ptp_ocp->buf[14] = 0xff;
	drivers_ptp_ptp_ocp->buf[15] = 0xff;
	drivers_ptp_ptp_ocp->buf[16] = 0xff;
	drivers_ptp_ptp_ocp->buf[17] = 0xff;
	drivers_ptp_ptp_ocp->buf[18] = 0xff;
	drivers_ptp_ptp_ocp->buf[19] = 0xff;
	drivers_ptp_ptp_ocp->buf[20] = 0xff;
	drivers_ptp_ptp_ocp->buf[21] = 0xff;
	drivers_ptp_ptp_ocp->buf[22] = 0xff;
	drivers_ptp_ptp_ocp->buf[23] = 0xff;;

    memory_region_init_io(&drivers_ptp_ptp_ocp->mmio[0], OBJECT(drivers_ptp_ptp_ocp), &drivers_ptp_ptp_ocp_mmio_ops, drivers_ptp_ptp_ocp,
                    "drivers_ptp_ptp_ocp-mmio0", 16 * MiB);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_ptp_ptp_ocp->mmio[0]);
    memory_region_init_io(&drivers_ptp_ptp_ocp->mmio[1], OBJECT(drivers_ptp_ptp_ocp), &drivers_ptp_ptp_ocp_mmio_ops, drivers_ptp_ptp_ocp,
                    "drivers_ptp_ptp_ocp-mmio1", 16 * MiB);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_ptp_ptp_ocp->mmio[1]);
    memory_region_init_io(&drivers_ptp_ptp_ocp->mmio[2], OBJECT(drivers_ptp_ptp_ocp), &drivers_ptp_ptp_ocp_mmio_ops, drivers_ptp_ptp_ocp,
                    "drivers_ptp_ptp_ocp-mmio2", 16 * MiB);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_ptp_ptp_ocp->mmio[2]);
    memory_region_init_io(&drivers_ptp_ptp_ocp->mmio[3], OBJECT(drivers_ptp_ptp_ocp), &drivers_ptp_ptp_ocp_mmio_ops, drivers_ptp_ptp_ocp,
                    "drivers_ptp_ptp_ocp-mmio3", 16 * MiB);
    pci_register_bar(pdev, 3, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_ptp_ptp_ocp->mmio[3]);
    memory_region_init_io(&drivers_ptp_ptp_ocp->mmio[4], OBJECT(drivers_ptp_ptp_ocp), &drivers_ptp_ptp_ocp_mmio_ops, drivers_ptp_ptp_ocp,
                    "drivers_ptp_ptp_ocp-mmio4", 16 * MiB);
    pci_register_bar(pdev, 4, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_ptp_ptp_ocp->mmio[4]);
    memory_region_init_io(&drivers_ptp_ptp_ocp->mmio[5], OBJECT(drivers_ptp_ptp_ocp), &drivers_ptp_ptp_ocp_mmio_ops, drivers_ptp_ptp_ocp,
                    "drivers_ptp_ptp_ocp-mmio5", 16 * MiB);
    pci_register_bar(pdev, 5, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_ptp_ptp_ocp->mmio[5]);
    memory_region_init_io(&drivers_ptp_ptp_ocp->mmio[6], OBJECT(drivers_ptp_ptp_ocp), &drivers_ptp_ptp_ocp_mmio_ops, drivers_ptp_ptp_ocp,
                    "drivers_ptp_ptp_ocp-mmio5", 16 * MiB);
    pci_register_bar(pdev, 6, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_ptp_ptp_ocp->mmio[6]);
}

static void pci_drivers_ptp_ptp_ocp_uninit(PCIDevice *pdev)
{
    msi_uninit(pdev);
}

static void drivers_ptp_ptp_ocp_instance_init(Object *obj)
{
	return;
}

static void drivers_ptp_ptp_ocp_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(class);

    k->realize = pci_drivers_ptp_ptp_ocp_realize;
    k->exit = pci_drivers_ptp_ptp_ocp_uninit;
    k->vendor_id = 0x1d9b;
    k->device_id = 0x400;
    k->revision = 0;
    k->subsystem_vendor_id = 0xffffffff;
    k->subsystem_id = 0xffffffff;
    k->class_id = 16777215;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void pci_drivers_ptp_ptp_ocp_register_types(void)
{
    static InterfaceInfo interfaces[] = {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    };
    static const TypeInfo drivers_ptp_ptp_ocp_info = {
        .name          = TYPE_PCI_DRIVERS_PTP_PTP_OCP_DEVICE,
        .parent        = TYPE_PCI_DEVICE,
        .instance_size = sizeof(Drivers_ptp_ptp_ocpState),
        .instance_init = drivers_ptp_ptp_ocp_instance_init,
        .class_init    = drivers_ptp_ptp_ocp_class_init,
        .interfaces = interfaces,
    };

    type_register_static(&drivers_ptp_ptp_ocp_info);
}
type_init(pci_drivers_ptp_ptp_ocp_register_types)
