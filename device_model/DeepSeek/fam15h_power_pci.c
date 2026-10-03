
/*
 * QEMU model for AMD Fam15h Northbridge Power Reporting
 * Multi-function PCI device: function 3, 4, 5.
 * Function 4 is the driver binding target (vendor=0x1022, device=0x1604).
 * Function 3 and 5 provide required PCI config registers for the driver to operate.
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_device.h"

#define VENDOR_ID              0x1022
#define DEVICE_ID_F3           0x1403
#define DEVICE_ID_F4           0x1604
#define DEVICE_ID_F5           0x1405
#define CLASS_ID               PCI_CLASS_BRIDGE_HOST

/* Register offsets extracted from Linux driver */
#define REG_NORTHBRIDGE_CAP    0xe8  /* function 3 */
#define REG_PROCESSOR_TDP      0x1b8 /* function 4 */
#define REG_TDP_RUNNING_AVERAGE 0xe0 /* function 5 */
#define REG_TDP_LIMIT3         0xe8  /* function 5 */

/* Function 3 */
#define TYPE_FAM15H_FUNC3 "fam15h-func3"
typedef struct Fam15hFunc3State Fam15hFunc3State;
DECLARE_INSTANCE_CHECKER(Fam15hFunc3State, FAM15H_FUNC3,
                         TYPE_FAM15H_FUNC3)
struct Fam15hFunc3State {
    PCIDevice parent_obj;
    /* shadow register at 0xe8 */
    uint32_t northbridge_cap;
};

/* Function 4 (main device, bound by driver) */
#define TYPE_PCIBASE_DEVICE "fam15h_power_pci"
typedef struct Fam15hFunc4State Fam15hFunc4State;
DECLARE_INSTANCE_CHECKER(Fam15hFunc4State, PCIBASE_DEVICE,
                         TYPE_PCIBASE_DEVICE)
struct Fam15hFunc4State {
    PCIDevice parent_obj;
    /* shadow register at 0x1b8 */
    uint32_t processor_tdp;
};

/* Function 5 */
#define TYPE_FAM15H_FUNC5 "fam15h-func5"
typedef struct Fam15hFunc5State Fam15hFunc5State;
DECLARE_INSTANCE_CHECKER(Fam15hFunc5State, FAM15H_FUNC5,
                         TYPE_FAM15H_FUNC5)
struct Fam15hFunc5State {
    PCIDevice parent_obj;
    /* shadow registers */
    uint32_t running_average; /* at 0xe0 */
    uint32_t tdp_limit3;      /* at 0xe8 */
};

/* --- Function 3 config space handlers --- */
static uint32_t fam15h_func3_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    Fam15hFunc3State *s = FAM15H_FUNC3(pdev);
    uint32_t val = pci_default_read_config(pdev, addr, len);

    /* overlay northbridge_cap at offset 0xe8 */
    if (addr <= REG_NORTHBRIDGE_CAP && addr + len > REG_NORTHBRIDGE_CAP) {
        int shift = (REG_NORTHBRIDGE_CAP - addr) * 8;
        uint32_t mask = (1ULL << (len * 8)) - 1;
        if (shift >= 0) {
            val = (val & ~(mask << shift)) | ((s->northbridge_cap >> shift) & mask);
        }
    }
    return val;
}

static void fam15h_func3_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    Fam15hFunc3State *s = FAM15H_FUNC3(pdev);

    /* allow default write for standard fields */
    pci_default_write_config(pdev, addr, val, len);

    /* update our shadow register if written */
    if (addr <= REG_NORTHBRIDGE_CAP && addr + len > REG_NORTHBRIDGE_CAP) {
        int shift = (REG_NORTHBRIDGE_CAP - addr) * 8;
        uint32_t mask = (1ULL << (len * 8)) - 1;
        if (shift >= 0) {
            s->northbridge_cap = (s->northbridge_cap & ~(mask << shift)) | ((val >> shift) & mask);
        }
    }
}

static void fam15h_func3_reset(DeviceState *dev)
{
    Fam15hFunc3State *s = FAM15H_FUNC3(dev);
    pci_device_reset(PCI_DEVICE(dev));
    /* driver checks bit 29; ensure it's clear */
    s->northbridge_cap = 0x00000000;
}

/* --- Function 4 config space handlers --- */
static uint32_t fam15h_func4_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    Fam15hFunc4State *s = PCIBASE_DEVICE(pdev);
    uint32_t val = pci_default_read_config(pdev, addr, len);

    /* overlay processor_tdp at offset 0x1b8 */
    if (addr <= REG_PROCESSOR_TDP && addr + len > REG_PROCESSOR_TDP) {
        int shift = (REG_PROCESSOR_TDP - addr) * 8;
        uint32_t mask = (1ULL << (len * 8)) - 1;
        if (shift >= 0) {
            val = (val & ~(mask << shift)) | ((s->processor_tdp >> shift) & mask);
        }
    }
    return val;
}

static void fam15h_func4_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    Fam15hFunc4State *s = PCIBASE_DEVICE(pdev);
    pci_default_write_config(pdev, addr, val, len);

    if (addr <= REG_PROCESSOR_TDP && addr + len > REG_PROCESSOR_TDP) {
        int shift = (REG_PROCESSOR_TDP - addr) * 8;
        uint32_t mask = (1ULL << (len * 8)) - 1;
        if (shift >= 0) {
            s->processor_tdp = (s->processor_tdp & ~(mask << shift)) | ((val >> shift) & mask);
        }
    }
}

static void fam15h_func4_reset(DeviceState *dev)
{
    Fam15hFunc4State *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    /* provide a valid processor_tdp value to avoid warnings */
    s->processor_tdp = 0x00100010; /* base_tdp=16, tmp=16 => power ~244uW */
}

/* --- Function 5 config space handlers --- */
static uint32_t fam15h_func5_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    Fam15hFunc5State *s = FAM15H_FUNC5(pdev);
    uint32_t val = pci_default_read_config(pdev, addr, len);

    /* overlay running_average at 0xe0 */
    if (addr <= REG_TDP_RUNNING_AVERAGE && addr + len > REG_TDP_RUNNING_AVERAGE) {
        int shift = (REG_TDP_RUNNING_AVERAGE - addr) * 8;
        uint32_t mask = (1ULL << (len * 8)) - 1;
        if (shift >= 0) {
            val = (val & ~(mask << shift)) | ((s->running_average >> shift) & mask);
        }
    }

    /* overlay tdp_limit3 at 0xe8 */
    if (addr <= REG_TDP_LIMIT3 && addr + len > REG_TDP_LIMIT3) {
        int shift = (REG_TDP_LIMIT3 - addr) * 8;
        uint32_t mask = (1ULL << (len * 8)) - 1;
        if (shift >= 0) {
            val = (val & ~(mask << shift)) | ((s->tdp_limit3 >> shift) & mask);
        }
    }
    return val;
}

static void fam15h_func5_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    Fam15hFunc5State *s = FAM15H_FUNC5(pdev);
    pci_default_write_config(pdev, addr, val, len);

    if (addr <= REG_TDP_RUNNING_AVERAGE && addr + len > REG_TDP_RUNNING_AVERAGE) {
        int shift = (REG_TDP_RUNNING_AVERAGE - addr) * 8;
        uint32_t mask = (1ULL << (len * 8)) - 1;
        if (shift >= 0) {
            s->running_average = (s->running_average & ~(mask << shift)) | ((val >> shift) & mask);
        }
    }

    if (addr <= REG_TDP_LIMIT3 && addr + len > REG_TDP_LIMIT3) {
        int shift = (REG_TDP_LIMIT3 - addr) * 8;
        uint32_t mask = (1ULL << (len * 8)) - 1;
        if (shift >= 0) {
            s->tdp_limit3 = (s->tdp_limit3 & ~(mask << shift)) | ((val >> shift) & mask);
        }
    }
}

static void fam15h_func5_reset(DeviceState *dev)
{
    Fam15hFunc5State *s = FAM15H_FUNC5(dev);
    pci_device_reset(PCI_DEVICE(dev));
    /* driver reading these expects safe values */
    s->running_average = 0x00000000;
    s->tdp_limit3 = 0x00000000;
}

/* --- Main realize for function 4: creates sibling functions --- */
static void pci_fam15h_func4_realize(PCIDevice *pdev, Error **errp)
{
    Fam15hFunc4State *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID_F4);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 0);

    /* mark as multifunction device */
    pci_set_byte(pci_conf + PCI_HEADER_TYPE, PCI_HEADER_TYPE_MULTI_FUNCTION | PCI_HEADER_TYPE_NORMAL);

    /* Create function 3 on the same PCI slot */
    {
        DeviceState *f3 = qdev_new(TYPE_FAM15H_FUNC3);
        if (!f3) {
            error_setg(errp, "failed to create function 3");
            return;
        }
        object_property_add_child(OBJECT(s), "fn3", OBJECT(f3));
        pci_set_devfn(PCI_DEVICE(f3), PCI_DEVFN(PCI_SLOT(pdev->devfn), 3));
        if (!qdev_realize(f3, BUS(pci_get_bus(pdev)), errp)) {
            return;
        }
    }

    /* Create function 5 */
    {
        DeviceState *f5 = qdev_new(TYPE_FAM15H_FUNC5);
        if (!f5) {
            error_setg(errp, "failed to create function 5");
            return;
        }
        object_property_add_child(OBJECT(s), "fn5", OBJECT(f5));
        pci_set_devfn(PCI_DEVICE(f5), PCI_DEVFN(PCI_SLOT(pdev->devfn), 5));
        if (!qdev_realize(f5, BUS(pci_get_bus(pdev)), errp)) {
            return;
        }
    }
}

/* --- Type registrations --- */
static void fam15h_func3_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->config_read = fam15h_func3_config_read;
    k->config_write = fam15h_func3_config_write;
    k->vendor_id = VENDOR_ID;
    k->device_id = DEVICE_ID_F3;
    k->class_id = CLASS_ID;
    k->revision = 0x01;
    dc->reset = fam15h_func3_reset;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void fam15h_func4_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pci_fam15h_func4_realize;
    k->config_read = fam15h_func4_config_read;
    k->config_write = fam15h_func4_config_write;
    k->vendor_id = VENDOR_ID;
    k->device_id = DEVICE_ID_F4;
    k->class_id = CLASS_ID;
    k->revision = 0x01;
    dc->reset = fam15h_func4_reset;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void fam15h_func5_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->config_read = fam15h_func5_config_read;
    k->config_write = fam15h_func5_config_write;
    k->vendor_id = VENDOR_ID;
    k->device_id = DEVICE_ID_F5;
    k->class_id = CLASS_ID;
    k->revision = 0x01;
    dc->reset = fam15h_func5_reset;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void pci_register_fam15h_types(void)
{
    static InterfaceInfo interfaces[] = {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    };

    static const TypeInfo func3_info = {
        .name          = TYPE_FAM15H_FUNC3,
        .parent        = TYPE_PCI_DEVICE,
        .instance_size = sizeof(Fam15hFunc3State),
        .class_init    = fam15h_func3_class_init,
        .interfaces    = interfaces,
    };

    static const TypeInfo func4_info = {
        .name          = TYPE_PCIBASE_DEVICE,
        .parent        = TYPE_PCI_DEVICE,
        .instance_size = sizeof(Fam15hFunc4State),
        .class_init    = fam15h_func4_class_init,
        .interfaces    = interfaces,
    };

    static const TypeInfo func5_info = {
        .name          = TYPE_FAM15H_FUNC5,
        .parent        = TYPE_PCI_DEVICE,
        .instance_size = sizeof(Fam15hFunc5State),
        .class_init    = fam15h_func5_class_init,
        .interfaces    = interfaces,
    };

    type_register_static(&func3_info);
    type_register_static(&func4_info);
    type_register_static(&func5_info);
}
type_init(pci_register_fam15h_types);
