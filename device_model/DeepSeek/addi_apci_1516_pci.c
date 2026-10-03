/*
 * QEMU PCI device model for ADDI-DATA APCI-1516 (Comedi driver)
 * Phase 2: Functional implementation based on driver source.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/module.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/pcie.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"

#define TYPE_PCIBASE_DEVICE "addi_apci_1516_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_ADDIDATA     0x15B8
#define PCI_DEVICE_ID_APCI1516     0x1000
#define PCI_CLASS_ID               0x00  /* PCI_CLASS_OTHERS */
#define APCI1516_DI_REG            0x00
#define APCI1516_DO_REG            0x04
#define APCI1516_WDOG_REG          0x00
#define ADDI_TCW_RELOAD_REG        0x04
#define ADDI_TCW_CTRL_REG          0x0c

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];

    /* Hardware Register Shadows */
    uint16_t reg_di;
    uint16_t reg_do;
};

/* PIO handlers for BAR1 (Digital I/O) */
static uint64_t pcibase_bar1_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    if (addr == APCI1516_DI_REG) {
        val = s->reg_di;
    } else if (addr == APCI1516_DO_REG) {
        val = s->reg_do;
    }
    return val;
}

static void pcibase_bar1_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr == APCI1516_DO_REG) {
        s->reg_do = (uint16_t)val;
    }
}

static const MemoryRegionOps pcibase_bar1_ops = {
    .read = pcibase_bar1_read,
    .write = pcibase_bar1_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 2, .max_access_size = 2 },
    .impl  = { .min_access_size = 2, .max_access_size = 2 },
};

/* PIO handlers for BAR2 (Watchdog) */
static uint64_t pcibase_bar2_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_bar2_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static const MemoryRegionOps pcibase_bar2_ops = {
    .read = pcibase_bar2_read,
    .write = pcibase_bar2_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    s->reg_di = 0;
    s->reg_do = 0;
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Basic PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_ADDIDATA );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_APCI1516 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* BAR 1: Digital I/O (PIO) */
    memory_region_init_io(&s->bar_regions[1], OBJECT(s), &pcibase_bar1_ops, s,
                          "apci1516-di-do", 0x100);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_IO, &s->bar_regions[1]);

    /* BAR 2: Watchdog (PIO) */
    memory_region_init_io(&s->bar_regions[2], OBJECT(s), &pcibase_bar2_ops, s,
                          "apci1516-wdog", 0x100);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_IO, &s->bar_regions[2]);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    /* Nothing to clean up */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "addi_apci_1516_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT16(reg_di, PCIBaseState),
        VMSTATE_UINT16(reg_do, PCIBaseState),
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
