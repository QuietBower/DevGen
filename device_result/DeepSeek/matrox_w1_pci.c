/*
 * QEMU Matrox G400 GPIO 1-wire device emulation.
 * Based on driver: /home/eely/linux-7.1/drivers/w1/masters/matrox_w1.c
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/pci/pcie.h"
#include "migration/vmstate.h"

/* Register offsets from BAR base */
#define MATROX_BASE             0x3C00
#define MATROX_PORT_INDEX_OFFSET 0x00
#define MATROX_PORT_DATA_OFFSET  0x0A

#define TYPE_PCIBASE_DEVICE "matrox_w1_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Memory region for BAR1 */
    MemoryRegion mmio;

    /* Device-specific state: indexed I/O registers */
    uint8_t port_index;
    uint8_t regs[256];   /* indexed register space */
};

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Only byte accesses expected, but mask to be safe */
    if (addr >= 0x4000) {
        return 0;
    }

    switch (addr) {
    case MATROX_BASE + MATROX_PORT_DATA_OFFSET:
        /* Read from data register, return currently indexed register */
        val = s->regs[s->port_index];
        break;
    default:
        /* For other addresses, return 0 */
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= 0x4000) {
        return;
    }

    switch (addr) {
    case MATROX_BASE + MATROX_PORT_INDEX_OFFSET:
        /* Write index register */
        s->port_index = (uint8_t)(val & 0xFF);
        break;
    case MATROX_BASE + MATROX_PORT_DATA_OFFSET:
        /* Write data to currently indexed register */
        s->regs[s->port_index] = (uint8_t)(val & 0xFF);
        break;
    default:
        break;
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Reset device state */
    s->port_index = 0;
    memset(s->regs, 0, sizeof(s->regs));
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, 0x102B);
    pci_set_word(pci_conf + PCI_DEVICE_ID, 0x0525);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0xFF);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR 1: MMIO, size 16KB */
    memory_region_init_io(&s->mmio, OBJECT(s), &pcibase_mmio_ops, s,
                          "matrox_w1-mmio", 0x4000);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mmio);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

static const VMStateDescription vmstate_pcibase = {
    .name = "matrox_w1_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT8(port_index, PCIBaseState),
        VMSTATE_UINT8_ARRAY(regs, PCIBaseState, 256),
        VMSTATE_END_OF_LIST()
    }
};

static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pcibase_realize;
    k->exit    = pcibase_uninit;
    dc->reset  = pcibase_reset;
    dc->vmsd   = &vmstate_pcibase;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void pcibase_register_types(void)
{
    static InterfaceInfo interfaces[] = {
        { INTERFACE_PCIE_DEVICE },
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