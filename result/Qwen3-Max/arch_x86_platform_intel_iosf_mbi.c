#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/pci/pci.h"
#include "hw/hw.h"
#include "hw/pci/msi.h"
#include "qemu/timer.h"
#include "qemu/main-loop.h" /* iothread mutex */
#include "qemu/module.h"
#include "qapi/visitor.h"

#define ARCH_X86_PLATFORM_INTEL_IOSF_MBI_BUF_LEN 1024

extern uint64_t target_value[ARCH_X86_PLATFORM_INTEL_IOSF_MBI_BUF_LEN];
extern bool target_value_reset;

typedef struct {
    PCIDevice pdev;
    MemoryRegion mmio[7];

    uint32_t len;
    uint64_t buf[ARCH_X86_PLATFORM_INTEL_IOSF_MBI_BUF_LEN];
    int is_probe_time;
} Arch_x86_platform_intel_iosf_mbiState;

#define TYPE_PCI_ARCH_X86_PLATFORM_INTEL_IOSF_MBI_DEVICE "arch_x86_platform_intel_iosf_mbi"
#define ARCH_X86_PLATFORM_INTEL_IOSF_MBI(obj)        OBJECT_CHECK(Arch_x86_platform_intel_iosf_mbiState, obj, TYPE_PCI_ARCH_X86_PLATFORM_INTEL_IOSF_MBI_DEVICE)

static uint64_t arch_x86_platform_intel_iosf_mbi_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    Arch_x86_platform_intel_iosf_mbiState *arch_x86_platform_intel_iosf_mbi = opaque;
    int i;

    if (target_value_reset) {
        arch_x86_platform_intel_iosf_mbi->len = 0;
        target_value_reset = false;
    }

    if (!arch_x86_platform_intel_iosf_mbi->is_probe_time) {
        for (i = 0; i < ARCH_X86_PLATFORM_INTEL_IOSF_MBI_BUF_LEN; ++i) {
            arch_x86_platform_intel_iosf_mbi->buf[i] = target_value[i];
        }
    } else {
        arch_x86_platform_intel_iosf_mbi->is_probe_time--;
    }

    return arch_x86_platform_intel_iosf_mbi->buf[(arch_x86_platform_intel_iosf_mbi->len++) % ARCH_X86_PLATFORM_INTEL_IOSF_MBI_BUF_LEN];
}

static void arch_x86_platform_intel_iosf_mbi_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                unsigned size)
{
	return;
}

static const MemoryRegionOps arch_x86_platform_intel_iosf_mbi_mmio_ops = {
    .read = arch_x86_platform_intel_iosf_mbi_mmio_read,
    .write = arch_x86_platform_intel_iosf_mbi_mmio_write,
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

static void pci_arch_x86_platform_intel_iosf_mbi_realize(PCIDevice *pdev, Error **errp)
{
    Arch_x86_platform_intel_iosf_mbiState *arch_x86_platform_intel_iosf_mbi = ARCH_X86_PLATFORM_INTEL_IOSF_MBI(pdev);
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

    arch_x86_platform_intel_iosf_mbi->len = 0;
    arch_x86_platform_intel_iosf_mbi->is_probe_time = 0;
	;

    memory_region_init_io(&arch_x86_platform_intel_iosf_mbi->mmio[0], OBJECT(arch_x86_platform_intel_iosf_mbi), &arch_x86_platform_intel_iosf_mbi_mmio_ops, arch_x86_platform_intel_iosf_mbi,
                    "arch_x86_platform_intel_iosf_mbi-mmio0", 16 * MiB);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &arch_x86_platform_intel_iosf_mbi->mmio[0]);
    memory_region_init_io(&arch_x86_platform_intel_iosf_mbi->mmio[1], OBJECT(arch_x86_platform_intel_iosf_mbi), &arch_x86_platform_intel_iosf_mbi_mmio_ops, arch_x86_platform_intel_iosf_mbi,
                    "arch_x86_platform_intel_iosf_mbi-mmio1", 16 * MiB);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &arch_x86_platform_intel_iosf_mbi->mmio[1]);
    memory_region_init_io(&arch_x86_platform_intel_iosf_mbi->mmio[2], OBJECT(arch_x86_platform_intel_iosf_mbi), &arch_x86_platform_intel_iosf_mbi_mmio_ops, arch_x86_platform_intel_iosf_mbi,
                    "arch_x86_platform_intel_iosf_mbi-mmio2", 16 * MiB);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &arch_x86_platform_intel_iosf_mbi->mmio[2]);
    memory_region_init_io(&arch_x86_platform_intel_iosf_mbi->mmio[3], OBJECT(arch_x86_platform_intel_iosf_mbi), &arch_x86_platform_intel_iosf_mbi_mmio_ops, arch_x86_platform_intel_iosf_mbi,
                    "arch_x86_platform_intel_iosf_mbi-mmio3", 16 * MiB);
    pci_register_bar(pdev, 3, PCI_BASE_ADDRESS_SPACE_MEMORY, &arch_x86_platform_intel_iosf_mbi->mmio[3]);
    memory_region_init_io(&arch_x86_platform_intel_iosf_mbi->mmio[4], OBJECT(arch_x86_platform_intel_iosf_mbi), &arch_x86_platform_intel_iosf_mbi_mmio_ops, arch_x86_platform_intel_iosf_mbi,
                    "arch_x86_platform_intel_iosf_mbi-mmio4", 16 * MiB);
    pci_register_bar(pdev, 4, PCI_BASE_ADDRESS_SPACE_MEMORY, &arch_x86_platform_intel_iosf_mbi->mmio[4]);
    memory_region_init_io(&arch_x86_platform_intel_iosf_mbi->mmio[5], OBJECT(arch_x86_platform_intel_iosf_mbi), &arch_x86_platform_intel_iosf_mbi_mmio_ops, arch_x86_platform_intel_iosf_mbi,
                    "arch_x86_platform_intel_iosf_mbi-mmio5", 16 * MiB);
    pci_register_bar(pdev, 5, PCI_BASE_ADDRESS_SPACE_MEMORY, &arch_x86_platform_intel_iosf_mbi->mmio[5]);
    memory_region_init_io(&arch_x86_platform_intel_iosf_mbi->mmio[6], OBJECT(arch_x86_platform_intel_iosf_mbi), &arch_x86_platform_intel_iosf_mbi_mmio_ops, arch_x86_platform_intel_iosf_mbi,
                    "arch_x86_platform_intel_iosf_mbi-mmio5", 16 * MiB);
    pci_register_bar(pdev, 6, PCI_BASE_ADDRESS_SPACE_MEMORY, &arch_x86_platform_intel_iosf_mbi->mmio[6]);
}

static void pci_arch_x86_platform_intel_iosf_mbi_uninit(PCIDevice *pdev)
{
    msi_uninit(pdev);
}

static void arch_x86_platform_intel_iosf_mbi_instance_init(Object *obj)
{
	return;
}

static void arch_x86_platform_intel_iosf_mbi_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(class);

    k->realize = pci_arch_x86_platform_intel_iosf_mbi_realize;
    k->exit = pci_arch_x86_platform_intel_iosf_mbi_uninit;
    k->vendor_id = 0x8086;
    k->device_id = 0xf00;
    k->revision = 0;
    k->subsystem_vendor_id = 0xffffffff;
    k->subsystem_id = 0xffffffff;
    k->class_id = 16777215;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void pci_arch_x86_platform_intel_iosf_mbi_register_types(void)
{
    static InterfaceInfo interfaces[] = {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    };
    static const TypeInfo arch_x86_platform_intel_iosf_mbi_info = {
        .name          = TYPE_PCI_ARCH_X86_PLATFORM_INTEL_IOSF_MBI_DEVICE,
        .parent        = TYPE_PCI_DEVICE,
        .instance_size = sizeof(Arch_x86_platform_intel_iosf_mbiState),
        .instance_init = arch_x86_platform_intel_iosf_mbi_instance_init,
        .class_init    = arch_x86_platform_intel_iosf_mbi_class_init,
        .interfaces = interfaces,
    };

    type_register_static(&arch_x86_platform_intel_iosf_mbi_info);
}
type_init(pci_arch_x86_platform_intel_iosf_mbi_register_types)
