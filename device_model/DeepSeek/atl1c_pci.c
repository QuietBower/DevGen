#include "qemu/osdep.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_device.h"
#include "qemu/bitops.h"

#define TYPE_ATL1C_PCI "atl1c-pci"
#define ATL1C_PCI(obj) OBJECT_CHECK(ATL1CState, (obj), TYPE_ATL1C_PCI)

#define BAR0_SIZE 0x1000
#define REG_COUNT (BAR0_SIZE / 4)

#define MDIO_CTRL_BUSY  BIT(27)
#define MDIO_CTRL_START BIT(23)

#define REG_MDIO_CTRL (0x14 / 4) /* arbitrary: driver may use different offset */

typedef struct ATL1CState {
    PCIDevice pdev;
    MemoryRegion mmio;
    uint32_t regs[REG_COUNT];
} ATL1CState;

static uint64_t atl1c_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    ATL1CState *s = ATL1C_PCI(opaque);
    uint64_t val = 0;
    uint32_t reg_idx = addr >> 2;

    if (reg_idx < REG_COUNT) {
        val = s->regs[reg_idx];
        if (reg_idx == REG_MDIO_CTRL) {
            val &= ~(MDIO_CTRL_BUSY | MDIO_CTRL_START);
        }
    }
    return val;
}

static void atl1c_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    ATL1CState *s = ATL1C_PCI(opaque);
    uint32_t reg_idx = addr >> 2;

    if (reg_idx < REG_COUNT) {
        s->regs[reg_idx] = val;
        if (reg_idx == REG_MDIO_CTRL) {
            s->regs[reg_idx] &= ~(MDIO_CTRL_BUSY | MDIO_CTRL_START);
        }
    }
}

static const MemoryRegionOps atl1c_mmio_ops = {
    .read = atl1c_mmio_read,
    .write = atl1c_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void atl1c_realize(PCIDevice *pdev, Error **errp)
{
    ATL1CState *s = ATL1C_PCI(pdev);

    pci_config_set_vendor_id(pdev->config, 0x1969); /* Atheros */
    pci_config_set_device_id(pdev->config, 0x1083); /* AR8131 (example) */
    pci_set_word(pdev->config + PCI_COMMAND, PCI_COMMAND_MEMORY);
    pci_set_byte(pdev->config + PCI_LATENCY_TIMER, 0x40);

    memory_region_init_io(&s->mmio, OBJECT(s), &atl1c_mmio_ops, s,
                          "atl1c-mmio", BAR0_SIZE);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mmio);
}

static void atl1c_reset(DeviceState *dev)
{
    ATL1CState *s = ATL1C_PCI(dev);
    memset(s->regs, 0, sizeof(s->regs));
}

static void atl1c_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = atl1c_realize;
    dc->reset = atl1c_reset;
    dc->desc = "Atheros AR813x/AR815x Gigabit Ethernet (QEMU Virtual)";
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
}

static const TypeInfo atl1c_type_info = {
    .name          = TYPE_ATL1C_PCI,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(ATL1CState),
    .class_init    = atl1c_class_init,
    .interfaces    = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void atl1c_register_types(void)
{
    type_register_static(&atl1c_type_info);
}

type_init(atl1c_register_types);