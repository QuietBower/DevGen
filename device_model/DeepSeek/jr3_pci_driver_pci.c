#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include <string.h>

#define TYPE_PCIBASE_DEVICE "jr3_pci_driver_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI_VENDOR_ID_JR3 0x1762
#define PCI_DEVICE_ID_JR3_1 0x1111
#define PCI_CLASS_JR3 PCI_CLASS_OTHERS
#define JR3_BAR0_SIZE (0x80000)

struct PCIBaseState {
    PCIDevice parent_obj;
    MemoryRegion mmio;
    uint8_t mmio_data[JR3_BAR0_SIZE];
};

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr < JR3_BAR0_SIZE && size == 4) {
        val = le32_to_cpu(*(uint32_t *)(s->mmio_data + addr));
    } else {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "jr3_pci: bad read offset 0x%" HWADDR_PRIx " size %u\n",
                      addr, size);
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                               unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < JR3_BAR0_SIZE && size == 4) {
        if (addr == 0x60000) {
            memset(s->mmio_data, 0, sizeof(s->mmio_data));
            *(uint32_t *)(s->mmio_data + 0x103E4) = cpu_to_le32(1);
            *(uint32_t *)(s->mmio_data + 0x103E0) = cpu_to_le32(1);
            *(uint32_t *)(s->mmio_data + 0x103C0) = cpu_to_le32(0x4A523320);
            const char *copy = "QEMU-jr3";
            for (int i = 0; copy[i]; i++) {
                *(uint32_t *)(s->mmio_data + 0x10100 + i * 4) =
                    cpu_to_le32((unsigned char)copy[i]);
            }
            *(uint32_t *)(s->mmio_data + 0x103D4) = cpu_to_le32(0x302);
            *(uint32_t *)(s->mmio_data + 0x103D0) = cpu_to_le32(1);
            *(uint32_t *)(s->mmio_data + 0x103F4) = cpu_to_le32(12);
            *(uint32_t *)(s->mmio_data + 0x103F8) = cpu_to_le32(0x3FFF);
            *(uint32_t *)(s->mmio_data + 0x103F0) = cpu_to_le32(1);
            *(uint32_t *)(s->mmio_data + 0x10380) = cpu_to_le32(26214);
            *(uint32_t *)(s->mmio_data + 0x10384) = cpu_to_le32(32752);
            return;
        }
        *(uint32_t *)(s->mmio_data + addr) = cpu_to_le32((uint32_t)val);
        if (addr == 0x1039C) {
            *(uint32_t *)(s->mmio_data + addr) = 0;
        }
    } else {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "jr3_pci: bad write offset 0x%" HWADDR_PRIx " size %u\n",
                      addr, size);
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    memset(s->mmio_data, 0, sizeof(s->mmio_data));
    *(uint32_t *)(s->mmio_data + 0x103E4) = cpu_to_le32(1);
    *(uint32_t *)(s->mmio_data + 0x103E0) = cpu_to_le32(1);
    *(uint32_t *)(s->mmio_data + 0x103C0) = cpu_to_le32(0x4A523320);
    const char *copy = "QEMU-jr3";
    for (int i = 0; copy[i]; i++) {
        *(uint32_t *)(s->mmio_data + 0x10100 + i * 4) =
            cpu_to_le32((unsigned char)copy[i]);
    }
    *(uint32_t *)(s->mmio_data + 0x103D4) = cpu_to_le32(0x302);
    *(uint32_t *)(s->mmio_data + 0x103D0) = cpu_to_le32(1);
    *(uint32_t *)(s->mmio_data + 0x103F4) = cpu_to_le32(12);
    *(uint32_t *)(s->mmio_data + 0x103F8) = cpu_to_le32(0x3FFF);
    *(uint32_t *)(s->mmio_data + 0x103F0) = cpu_to_le32(1);
    *(uint32_t *)(s->mmio_data + 0x10380) = cpu_to_le32(26214);
    *(uint32_t *)(s->mmio_data + 0x10384) = cpu_to_le32(32752);
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_JR3);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_JR3_1);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_JR3);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 0);

    memset(s->mmio_data, 0, sizeof(s->mmio_data));

    memory_region_init_io(&s->mmio, OBJECT(s), &pcibase_mmio_ops, s,
                          "jr3-mmio", JR3_BAR0_SIZE);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mmio);
}

static void pcibase_uninit(PCIDevice *pdev)
{
}

static const VMStateDescription vmstate_pcibase = {
    .name = "jr3_pci_driver_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT8_ARRAY(mmio_data, PCIBaseState, JR3_BAR0_SIZE),
        VMSTATE_END_OF_LIST()
    }
};

static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pcibase_realize;
    k->exit = pcibase_uninit;
    dc->reset = pcibase_reset;
    dc->vmsd = &vmstate_pcibase;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void pcibase_register_types(void)
{
    static InterfaceInfo interfaces[] = {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    };

    static const TypeInfo pcibase_info = {
        .name = TYPE_PCIBASE_DEVICE,
        .parent = TYPE_PCI_DEVICE,
        .instance_size = sizeof(PCIBaseState),
        .class_init = pcibase_class_init,
        .interfaces = interfaces,
    };

    type_register_static(&pcibase_info);
}
type_init(pcibase_register_types);
