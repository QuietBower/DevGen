/*
 * QEMU model for Intel 5400 Memory Controller EDAC functions
 * Based on Linux driver i5400_edac.c
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_device.h"
#include "hw/pci/pcie.h"

/* Device type names */
#define TYPE_I5400_EDAC_FUNC0 "i5400-edac-func0"
#define TYPE_I5400_EDAC_FUNC1 "i5400-edac-func1"
#define TYPE_I5400_EDAC_FUNC2 "i5400-edac-func2"
#define TYPE_I5400_FBD0 "i5400-fbd0"
#define TYPE_I5400_FBD1 "i5400-fbd1"

OBJECT_DECLARE_SIMPLE_TYPE(I5400EDACFunc0State, I5400_EDAC_FUNC0)
OBJECT_DECLARE_SIMPLE_TYPE(I5400EDACFunc1State, I5400_EDAC_FUNC1)
OBJECT_DECLARE_SIMPLE_TYPE(I5400EDACFunc2State, I5400_EDAC_FUNC2)
OBJECT_DECLARE_SIMPLE_TYPE(I5400FBD0State, I5400_FBD0)
OBJECT_DECLARE_SIMPLE_TYPE(I5400FBD1State, I5400_FBD1)

/* Vendor and Device IDs */
#define VENDOR_ID 0x8086
#define DEVICE_ID_EDAC 0x4030   /* PCI_DEVICE_ID_INTEL_5400_ERR */
#define DEVICE_ID_FBD0 0x4035   /* PCI_DEVICE_ID_INTEL_5400_FBD0 (assumed) */
#define DEVICE_ID_FBD1 0x4036   /* PCI_DEVICE_ID_INTEL_5400_FBD1 (assumed) */

/* Register offsets (from driver) */
#define AMBASE          0x48
#define MAXCH           0x56
#define MAXDIMMPERCH    0x57
#define TOLM            0x6C
#define REDMEMB         0x7C
#define MIR0            0x80
#define MIR1            0x84
#define FERR_FAT_FBD    0x98
#define NERR_FAT_FBD    0x9c
#define FERR_NF_FBD     0xa0
#define NERR_NF_FBD     0xa4
#define EMASK_FBD       0xa8
#define NRECMEMA        0xbe
#define NRECMEMB        0xc0
#define RECMEMA         0xf0
#define RECMEMB         0xf4
#define MTR0            0x80
#define MTR1            0x82
#define MTR2            0x84
#define MTR3            0x86
#define AMBPRESENT_0    0x64
#define AMBPRESENT_1    0x66

/* State structures (no extra fields needed beyond PCIDevice) */
typedef struct I5400EDACFunc0State {
    PCIDevice parent_obj;
} I5400EDACFunc0State;

typedef struct I5400EDACFunc1State {
    PCIDevice parent_obj;
} I5400EDACFunc1State;

typedef struct I5400EDACFunc2State {
    PCIDevice parent_obj;
} I5400EDACFunc2State;

typedef struct I5400FBD0State {
    PCIDevice parent_obj;
} I5400FBD0State;

typedef struct I5400FBD1State {
    PCIDevice parent_obj;
} I5400FBD1State;

/* Custom config write for function 1 to implement write-1-to-clear on FERR registers */
static void i5400_func1_config_write(PCIDevice *pdev, uint32_t addr,
                                     uint32_t val, int len)
{
    if (addr == FERR_FAT_FBD && len == 4) {
        /* Write-1-to-clear: clear bits that are written as 1 */
        uint32_t current = pci_get_long(pdev->config + FERR_FAT_FBD);
        current &= ~val;
        pci_set_long(pdev->config + FERR_FAT_FBD, current);
        return;
    }
    if (addr == FERR_NF_FBD && len == 4) {
        uint32_t current = pci_get_long(pdev->config + FERR_NF_FBD);
        current &= ~val;
        pci_set_long(pdev->config + FERR_NF_FBD, current);
        return;
    }
    /* Default write for other registers */
    pci_default_write_config(pdev, addr, val, len);
}

/* Default config reads - no special handling needed */

static void i5400_edac_func0_realize(PCIDevice *pdev, Error **errp)
{
    pci_set_word(pdev->config + PCI_VENDOR_ID, VENDOR_ID);
    pci_set_word(pdev->config + PCI_DEVICE_ID, DEVICE_ID_EDAC);
    pci_set_word(pdev->config + PCI_CLASS_DEVICE, PCI_CLASS_OTHERS);
    pci_set_byte(pdev->config + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pdev, 1);

    /* Set function-specific registers */
    pci_set_byte(pdev->config + MAXCH, 4);       /* 4 channels */
    pci_set_byte(pdev->config + MAXDIMMPERCH, 4); /* 4 DIMMs per channel */
    /* AMBASE set to 0 by default */
}

static void i5400_edac_func1_realize(PCIDevice *pdev, Error **errp)
{
    pci_set_word(pdev->config + PCI_VENDOR_ID, VENDOR_ID);
    pci_set_word(pdev->config + PCI_DEVICE_ID, DEVICE_ID_EDAC);
    pci_set_word(pdev->config + PCI_CLASS_DEVICE, PCI_CLASS_OTHERS);
    pci_set_byte(pdev->config + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pdev, 1);

    /* Initialize error mask: all 1s (errors masked) */
    pci_set_long(pdev->config + EMASK_FBD, 0xFFFFFFFF);
    /* Other registers are 0 by default */

    /* Override config write to handle write-1-to-clear */
    pdev->config_write = i5400_func1_config_write;
}

static void i5400_edac_func2_realize(PCIDevice *pdev, Error **errp)
{
    pci_set_word(pdev->config + PCI_VENDOR_ID, VENDOR_ID);
    pci_set_word(pdev->config + PCI_DEVICE_ID, DEVICE_ID_EDAC);
    pci_set_word(pdev->config + PCI_CLASS_DEVICE, PCI_CLASS_OTHERS);
    pci_set_byte(pdev->config + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pdev, 1);
}

static void i5400_fbd0_realize(PCIDevice *pdev, Error **errp)
{
    pci_set_word(pdev->config + PCI_VENDOR_ID, VENDOR_ID);
    pci_set_word(pdev->config + PCI_DEVICE_ID, DEVICE_ID_FBD0);
    pci_set_word(pdev->config + PCI_CLASS_DEVICE, PCI_CLASS_OTHERS);
    pci_set_byte(pdev->config + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pdev, 1);

    /* Initialize MTR registers to 0 (no DIMMs present) */
    pci_set_word(pdev->config + MTR0, 0);
    pci_set_word(pdev->config + MTR1, 0);
    pci_set_word(pdev->config + MTR2, 0);
    pci_set_word(pdev->config + MTR3, 0);
    /* AMBPRESENT registers to 0 */
    pci_set_word(pdev->config + AMBPRESENT_0, 0);
    pci_set_word(pdev->config + AMBPRESENT_1, 0);
}

static void i5400_fbd1_realize(PCIDevice *pdev, Error **errp)
{
    pci_set_word(pdev->config + PCI_VENDOR_ID, VENDOR_ID);
    pci_set_word(pdev->config + PCI_DEVICE_ID, DEVICE_ID_FBD1);
    pci_set_word(pdev->config + PCI_CLASS_DEVICE, PCI_CLASS_OTHERS);
    pci_set_byte(pdev->config + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pdev, 1);

    pci_set_word(pdev->config + MTR0, 0);
    pci_set_word(pdev->config + MTR1, 0);
    pci_set_word(pdev->config + MTR2, 0);
    pci_set_word(pdev->config + MTR3, 0);
    pci_set_word(pdev->config + AMBPRESENT_0, 0);
    pci_set_word(pdev->config + AMBPRESENT_1, 0);
}

/* Reset functions */
static void i5400_edac_func0_reset(DeviceState *dev)
{
    I5400EDACFunc0State *s = I5400_EDAC_FUNC0(dev);
    pci_device_reset(PCI_DEVICE(s));
    /* Restore custom defaults after reset */
    pci_set_byte(s->parent_obj.config + MAXCH, 4);
    pci_set_byte(s->parent_obj.config + MAXDIMMPERCH, 4);
}

static void i5400_edac_func1_reset(DeviceState *dev)
{
    I5400EDACFunc1State *s = I5400_EDAC_FUNC1(dev);
    pci_device_reset(PCI_DEVICE(s));
    pci_set_long(s->parent_obj.config + EMASK_FBD, 0xFFFFFFFF);
}

static void i5400_edac_func2_reset(DeviceState *dev)
{
    I5400EDACFunc2State *s = I5400_EDAC_FUNC2(dev);
    pci_device_reset(PCI_DEVICE(s));
}

static void i5400_fbd0_reset(DeviceState *dev)
{
    I5400FBD0State *s = I5400_FBD0(dev);
    pci_device_reset(PCI_DEVICE(s));
    pci_set_word(s->parent_obj.config + MTR0, 0);
    pci_set_word(s->parent_obj.config + MTR1, 0);
    pci_set_word(s->parent_obj.config + MTR2, 0);
    pci_set_word(s->parent_obj.config + MTR3, 0);
    pci_set_word(s->parent_obj.config + AMBPRESENT_0, 0);
    pci_set_word(s->parent_obj.config + AMBPRESENT_1, 0);
}

static void i5400_fbd1_reset(DeviceState *dev)
{
    I5400FBD1State *s = I5400_FBD1(dev);
    pci_device_reset(PCI_DEVICE(s));
    pci_set_word(s->parent_obj.config + MTR0, 0);
    pci_set_word(s->parent_obj.config + MTR1, 0);
    pci_set_word(s->parent_obj.config + MTR2, 0);
    pci_set_word(s->parent_obj.config + MTR3, 0);
    pci_set_word(s->parent_obj.config + AMBPRESENT_0, 0);
    pci_set_word(s->parent_obj.config + AMBPRESENT_1, 0);
}

/* Class initializers */
static void i5400_edac_func0_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    k->realize = i5400_edac_func0_realize;
    dc->reset = i5400_edac_func0_reset;
    k->pci_conventional = true;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void i5400_edac_func1_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    k->realize = i5400_edac_func1_realize;
    dc->reset = i5400_edac_func1_reset;
    k->pci_conventional = true;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void i5400_edac_func2_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    k->realize = i5400_edac_func2_realize;
    dc->reset = i5400_edac_func2_reset;
    k->pci_conventional = true;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void i5400_fbd0_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    k->realize = i5400_fbd0_realize;
    dc->reset = i5400_fbd0_reset;
    k->pci_conventional = true;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void i5400_fbd1_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    k->realize = i5400_fbd1_realize;
    dc->reset = i5400_fbd1_reset;
    k->pci_conventional = true;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

/* Type registration */
static const TypeInfo i5400_edac_func0_info = {
    .name = TYPE_I5400_EDAC_FUNC0,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(I5400EDACFunc0State),
    .class_init = i5400_edac_func0_class_init,
};

static const TypeInfo i5400_edac_func1_info = {
    .name = TYPE_I5400_EDAC_FUNC1,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(I5400EDACFunc1State),
    .class_init = i5400_edac_func1_class_init,
};

static const TypeInfo i5400_edac_func2_info = {
    .name = TYPE_I5400_EDAC_FUNC2,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(I5400EDACFunc2State),
    .class_init = i5400_edac_func2_class_init,
};

static const TypeInfo i5400_fbd0_info = {
    .name = TYPE_I5400_FBD0,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(I5400FBD0State),
    .class_init = i5400_fbd0_class_init,
};

static const TypeInfo i5400_fbd1_info = {
    .name = TYPE_I5400_FBD1,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(I5400FBD1State),
    .class_init = i5400_fbd1_class_init,
};

static void i5400_register_types(void)
{
    type_register_static(&i5400_edac_func0_info);
    type_register_static(&i5400_edac_func1_info);
    type_register_static(&i5400_edac_func2_info);
    type_register_static(&i5400_fbd0_info);
    type_register_static(&i5400_fbd1_info);
}

type_init(i5400_register_types)
