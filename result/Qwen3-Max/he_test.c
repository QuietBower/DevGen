#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/pci/pci.h"
#include "hw/hw.h"
#include "hw/pci/msi.h"
#include "qemu/timer.h"
#include "qemu/main-loop.h" /* iothread mutex */
#include "qemu/module.h"
#include "qapi/visitor.h"

#define HE_TEST_BUF_LEN 1024

extern uint64_t target_value[HE_TEST_BUF_LEN];
extern bool target_value_reset;

typedef struct {
    PCIDevice pdev;
    MemoryRegion mmio[7];

    uint32_t len;
    uint64_t buf[HE_TEST_BUF_LEN];
    int is_probe_time;
} He_testState;

#define TYPE_PCI_HE_TEST_DEVICE "he_test"
#define HE_TEST(obj)        OBJECT_CHECK(He_testState, obj, TYPE_PCI_HE_TEST_DEVICE)

static uint64_t he_test_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    He_testState *he_test = opaque;
    int i;

    if (target_value_reset) {
        he_test->len = 0;
        target_value_reset = false;
    }

    if (!he_test->is_probe_time) {
        for (i = 0; i < HE_TEST_BUF_LEN; ++i) {
            he_test->buf[i] = target_value[i];
        }
    } else {
        he_test->is_probe_time--;
    }

    return he_test->buf[(he_test->len++) % HE_TEST_BUF_LEN];
}

static void he_test_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                unsigned size)
{
	return;
}

static const MemoryRegionOps he_test_mmio_ops = {
    .read = he_test_mmio_read,
    .write = he_test_mmio_write,
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

static void pci_he_test_realize(PCIDevice *pdev, Error **errp)
{
    He_testState *he_test = HE_TEST(pdev);
    uint8_t *pci_conf = pdev->config;
	int pos;

    pci_config_set_interrupt_pin(pci_conf, 1);
	pci_set_word(&pci_conf[0x04], 0xFFFF);
    pci_set_byte(&pci_conf[0x0C], 16);
    pci_set_byte(&pci_conf[0x0D], 209);
    pci_set_long(&pci_conf[0x40], 0xFFFFFFFF);
    pci_set_long(&pci_conf[0x10], 0xFFFFFFFF);
    pci_set_long(&pci_conf[0x14], 0xFFFFFFFF);
    pci_set_long(&pci_conf[0x18], 0xFFFFFFFF);
    pci_set_long(&pci_conf[0x1C], 0xFFFFFFFF);
    pci_set_long(&pci_conf[0x20], 0xFFFFFFFF);
    pci_set_long(&pci_conf[0x24], 0xFFFFFFFF);
    pci_set_long(&pci_conf[0x28], 0xFFFFFFFF);


    if (msi_init(pdev, 0, 1, true, false, errp)) {
        return;
    }
	
	pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
	pcie_endpoint_cap_init(pdev, 0);

	pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
	pdev->exp.pm_cap = pos;
	pci_set_word(pdev->config + pos + PCI_PM_PMC, 0x3);

    drivers_atm_he->len = 0;
    drivers_atm_he->is_probe_time = 72;
	drivers_atm_he->buf[0] = 0xff;
	drivers_atm_he->buf[1] = 0xff;
	drivers_atm_he->buf[2] = 0xff;
	drivers_atm_he->buf[3] = 0xff;
	drivers_atm_he->buf[4] = 0xff;
	drivers_atm_he->buf[5] = 0xff;
	drivers_atm_he->buf[6] = 0xff;
	drivers_atm_he->buf[7] = 0xff;
	drivers_atm_he->buf[8] = 0xff;
	drivers_atm_he->buf[9] = 0xff;
	drivers_atm_he->buf[10] = 0xff;
	drivers_atm_he->buf[11] = 0xff;
	drivers_atm_he->buf[12] = 0xff;
	drivers_atm_he->buf[13] = 0xff;
	drivers_atm_he->buf[14] = 0xff;
	drivers_atm_he->buf[15] = 0xff;
	drivers_atm_he->buf[16] = 0xff;
	drivers_atm_he->buf[17] = 0xff;
	drivers_atm_he->buf[18] = 0xff;
	drivers_atm_he->buf[19] = 0xff;
	drivers_atm_he->buf[20] = 0xff;
	drivers_atm_he->buf[21] = 0xff;
	drivers_atm_he->buf[22] = 0xff;
	drivers_atm_he->buf[23] = 0xff;
	drivers_atm_he->buf[24] = 0xff;
	drivers_atm_he->buf[25] = 0xff;
	drivers_atm_he->buf[26] = 0xff;
	drivers_atm_he->buf[27] = 0xff;
	drivers_atm_he->buf[28] = 0xff;
	drivers_atm_he->buf[29] = 0xff;
	drivers_atm_he->buf[30] = 0xff;
	drivers_atm_he->buf[31] = 0xff;
	drivers_atm_he->buf[32] = 0xff;
	drivers_atm_he->buf[33] = 0xff;
	drivers_atm_he->buf[34] = 0xff;
	drivers_atm_he->buf[35] = 0xff;
	drivers_atm_he->buf[36] = 0xff;
	drivers_atm_he->buf[37] = 0xff;
	drivers_atm_he->buf[38] = 0xff;
	drivers_atm_he->buf[39] = 0xff;
	drivers_atm_he->buf[40] = 0xff;
	drivers_atm_he->buf[41] = 0xff;
	drivers_atm_he->buf[42] = 0xff;
	drivers_atm_he->buf[43] = 0xff;
	drivers_atm_he->buf[44] = 0xff;
	drivers_atm_he->buf[45] = 0xff;
	drivers_atm_he->buf[46] = 0xff;
	drivers_atm_he->buf[47] = 0xff;
	drivers_atm_he->buf[48] = 0xff;
	drivers_atm_he->buf[49] = 0xff;
	drivers_atm_he->buf[50] = 0xff;
	drivers_atm_he->buf[51] = 0xff;
	drivers_atm_he->buf[52] = 0xff;
	drivers_atm_he->buf[53] = 0xff;
	drivers_atm_he->buf[54] = 0xff;
	drivers_atm_he->buf[55] = 0xff;
	drivers_atm_he->buf[56] = 0xff;
	drivers_atm_he->buf[57] = 0xff;
	drivers_atm_he->buf[58] = 0xff;
	drivers_atm_he->buf[59] = 0xff;
	drivers_atm_he->buf[60] = 0xff;
	drivers_atm_he->buf[61] = 0xff;
	drivers_atm_he->buf[62] = 0xff;
	drivers_atm_he->buf[63] = 0xff;
	drivers_atm_he->buf[64] = 0xff;
	drivers_atm_he->buf[65] = 0xff;
	drivers_atm_he->buf[66] = 0xff;
	drivers_atm_he->buf[67] = 0xff;
	drivers_atm_he->buf[68] = 0xff;
	drivers_atm_he->buf[69] = 0xff;
	drivers_atm_he->buf[70] = 0xff;
	drivers_atm_he->buf[71] = 0xff;

#if defined(HE_REGMAP_SIZE)
    size_t he_test_bar0_size = HE_REGMAP_SIZE;
#else
    size_t he_test_bar0_size = 16 * MiB;
#endif
    memory_region_init_io(&he_test->mmio[0], OBJECT(he_test),&he_test_mmio_ops, he_test,"he_test-mmio0", he_test_bar0_size);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &he_test->mmio[0]);
    const size_t he_test_stub_size = 4 * KiB;
    memory_region_init_io(&he_test->mmio[1], OBJECT(he_test),&he_test_mmio_ops, he_test,"he_test-mmio1", he_test_stub_size);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &he_test->mmio[1]);
    memory_region_init_io(&he_test->mmio[2], OBJECT(he_test),&he_test_mmio_ops, he_test,"he_test-mmio2", he_test_stub_size);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &he_test->mmio[2]);
    memory_region_init_io(&he_test->mmio[3], OBJECT(he_test),&he_test_mmio_ops, he_test,"he_test-mmio3", he_test_stub_size);
    pci_register_bar(pdev, 3, PCI_BASE_ADDRESS_SPACE_MEMORY, &he_test->mmio[3]);
    memory_region_init_io(&he_test->mmio[4], OBJECT(he_test),&he_test_mmio_ops, he_test,"he_test-mmio4", he_test_stub_size);
    pci_register_bar(pdev, 4, PCI_BASE_ADDRESS_SPACE_MEMORY, &he_test->mmio[4]);
    memory_region_init_io(&he_test->mmio[5], OBJECT(he_test),&he_test_mmio_ops, he_test,"he_test-mmio5", he_test_stub_size);
    pci_register_bar(pdev, 5, PCI_BASE_ADDRESS_SPACE_MEMORY, &he_test->mmio[5]);
    memory_region_init_io(&he_test->mmio[6], OBJECT(he_test),&he_test_mmio_ops, he_test,"he_test-mmio6", he_test_stub_size);
    pci_register_bar(pdev, 6, PCI_BASE_ADDRESS_SPACE_MEMORY, &he_test->mmio[6]);

}

static void pci_he_test_uninit(PCIDevice *pdev)
{
    msi_uninit(pdev);
}

static void he_test_instance_init(Object *obj)
{
	return;
}

static void he_test_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(class);

    k->realize = pci_he_test_realize;
    k->exit = pci_he_test_uninit;
    k->vendor_id = 0x1127;
    k->device_id = 0x0400;
    k->revision = 0x00;
    k->subsystem_vendor_id = 0x0000;
    k->subsystem_id = 0x0000;
    k->class_id = 0x020000;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void pci_he_test_register_types(void)
{
    static InterfaceInfo interfaces[] = {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    };
    static const TypeInfo he_test_info = {
        .name          = TYPE_PCI_HE_TEST_DEVICE,
        .parent        = TYPE_PCI_DEVICE,
        .instance_size = sizeof(He_testState),
        .instance_init = he_test_instance_init,
        .class_init    = he_test_class_init,
        .interfaces = interfaces,
    };

    type_register_static(&he_test_info);
}
type_init(pci_he_test_register_types)