/*
 * QEMU e752x EDAC PCI Device Emulation
 * Based on Linux driver e752x_edac.c
 *
 * This device emulates the Intel E7520 Memory Controller (function 0)
 * and its error reporting device (function 1) to allow the EDAC driver to probe.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/module.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_device.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"

/* Device types */
#define TYPE_PCIBASE_DEVICE "e752x_edac_pci"
#define TYPE_E752X_ERR_DEVICE "e752x_edac_err"

OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)
OBJECT_DECLARE_SIMPLE_TYPE(E752XErrState, E752X_ERR_DEVICE)

/* PCI IDs */
#define PCI_VENDOR_ID_INTEL 0x8086
#define PCI_DEVICE_ID_INTEL_7520_0  0x3590
#define PCI_DEVICE_ID_INTEL_7520_1_ERR 0x3591
#define PCI_CLASS_MEMORY_CONTROLLER 0x0580

/* Register offsets from the driver */
#define E752X_FERR_GLOBAL    0x40
#define E752X_NERR_GLOBAL    0x44
#define E752X_HI_FERR        0x50
#define E752X_HI_NERR        0x52
#define E752X_HI_ERRMASK     0x54
#define E752X_HI_SMICMD      0x5A
#define E752X_MCHSCRB        0x52
#define E752X_DRB            0x60
#define E752X_DRA            0x70
#define E752X_DRC            0x7C
#define E752X_DRM            0x80
#define E752X_DDRCSR         0x9A
#define E752X_TOLM           0xC4
#define E752X_REMAPBASE      0xC6
#define E752X_REMAPLIMIT     0xC8
#define E752X_REMAPOFFSET    0xCA
#define E752X_SYSBUS_FERR    0x60
#define E752X_SYSBUS_NERR    0x62
#define E752X_SYSBUS_ERRMASK 0x64
#define E752X_SYSBUS_SMICMD  0x6A
#define E752X_BUF_FERR       0x70
#define E752X_BUF_NERR       0x72
#define E752X_BUF_ERRMASK    0x74
#define E752X_BUF_SMICMD     0x7A
#define E752X_DRAM_FERR      0x80
#define E752X_DRAM_NERR      0x82
#define E752X_DRAM_ERRMASK   0x84
#define E752X_DRAM_SMICMD    0x8A
#define E752X_DRAM_RETR_ADD  0xAC
#define E752X_DRAM_SEC1_ADD  0xA0
#define E752X_DRAM_SEC2_ADD  0xC8
#define E752X_DRAM_DED_ADD   0xA4
#define E752X_DRAM_SCRB_ADD  0xA8
#define E752X_DRAM_SEC1_SYNDROME 0xC4
#define E752X_DRAM_SEC2_SYNDROME 0xC6
#define E752X_DEVPRES1       0xF4
#define I3100_NSI_FERR       0x48
#define I3100_NSI_NERR       0x4C
#define I3100_NSI_SMICMD     0x54
#define I3100_NSI_EMASK      0x90

/* Main EDAC device (function 0) */
struct PCIBaseState {
    PCIDevice parent_obj;
    uint8_t cfg[PCI_CONFIG_SPACE_SIZE];
};

/* Error reporting device (function 1) */
struct E752XErrState {
    PCIDevice parent_obj;
    uint8_t cfg[PCI_CONFIG_SPACE_SIZE];
};

/* ------------------- Config space handlers for main device ------------------- */
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t address, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint32_t val = 0;

    if (address < 0x40) {
        return pci_default_read_config(pdev, address, len);
    }
    if (address + len > PCI_CONFIG_SPACE_SIZE) {
        return 0xFFFFFFFF;
    }
    memcpy(&val, s->cfg + address, MIN(len, (int)sizeof(val)));
    return val;
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t address, uint32_t val, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (address < 0x40) {
        pci_default_write_config(pdev, address, val, len);
        return;
    }
    if (address + len > PCI_CONFIG_SPACE_SIZE) {
        return;
    }
    memcpy(s->cfg + address, &val, len);
}

/* ------------------- Config space handlers for error device ------------------- */
static uint32_t e752x_err_config_read(PCIDevice *pdev, uint32_t address, int len)
{
    E752XErrState *s = E752X_ERR_DEVICE(pdev);
    uint32_t val = 0;

    if (address < 0x40) {
        return pci_default_read_config(pdev, address, len);
    }
    if (address + len > PCI_CONFIG_SPACE_SIZE) {
        return 0xFFFFFFFF;
    }
    memcpy(&val, s->cfg + address, MIN(len, (int)sizeof(val)));
    return val;
}

static void e752x_err_config_write(PCIDevice *pdev, uint32_t address, uint32_t val, int len)
{
    E752XErrState *s = E752X_ERR_DEVICE(pdev);

    if (address < 0x40) {
        pci_default_write_config(pdev, address, val, len);
        return;
    }
    if (address + len > PCI_CONFIG_SPACE_SIZE) {
        return;
    }
    memcpy(s->cfg + address, &val, len);
}

/* ------------------- Reset ------------------- */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    memset(s->cfg + 0x40, 0, PCI_CONFIG_SPACE_SIZE - 0x40);
    /* Enable error registers (bit 5 set) to pass BIOS hidden check */
    s->cfg[E752X_DEVPRES1] = 0x20;
}

static void e752x_err_reset(DeviceState *dev)
{
    E752XErrState *s = E752X_ERR_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    memset(s->cfg + 0x40, 0, PCI_CONFIG_SPACE_SIZE - 0x40);
    /* Enable error registers (bit 5 set) to pass BIOS hidden check */
    s->cfg[E752X_DEVPRES1] = 0x20;
}

/* ------------------- Realize/Uninit ------------------- */
static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_INTEL);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_INTEL_7520_0);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_MEMORY_CONTROLLER);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 0); /* No interrupt pin */

    /* Enable multi-function to allow function 1 to be discovered */
    pci_conf[PCI_HEADER_TYPE] |= PCI_HEADER_TYPE_MULTI_FUNCTION;

    memset(s->cfg, 0, PCI_CONFIG_SPACE_SIZE);
    /* Enable error registers (bit 5 set) to pass BIOS hidden check */
    s->cfg[E752X_DEVPRES1] = 0x20;
}

static void pcibase_uninit(PCIDevice *pdev)
{
    /* Nothing to do */
}

static void e752x_err_realize(PCIDevice *pdev, Error **errp)
{
    E752XErrState *s = E752X_ERR_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_INTEL);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_INTEL_7520_1_ERR);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_MEMORY_CONTROLLER);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 0);

    memset(s->cfg, 0, PCI_CONFIG_SPACE_SIZE);
    /* Enable error registers (bit 5 set) to pass BIOS hidden check */
    s->cfg[E752X_DEVPRES1] = 0x20;
}

static void e752x_err_uninit(PCIDevice *pdev)
{
    /* Nothing to do */
}

/* ------------------- VMState ------------------- */
static const VMStateDescription vmstate_pcibase = {
    .name = "e752x_edac_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_BUFFER(cfg, PCIBaseState),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_e752x_err = {
    .name = "e752x_edac_err",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, E752XErrState),
        VMSTATE_BUFFER(cfg, E752XErrState),
        VMSTATE_END_OF_LIST()
    }
};

/* ------------------- Class init ------------------- */
static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pcibase_realize;
    k->exit = pcibase_uninit;
    k->config_read = pcibase_config_read;
    k->config_write = pcibase_config_write;
    dc->reset = pcibase_reset;
    dc->vmsd = &vmstate_pcibase;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void e752x_err_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = e752x_err_realize;
    k->exit = e752x_err_uninit;
    k->config_read = e752x_err_config_read;
    k->config_write = e752x_err_config_write;
    dc->reset = e752x_err_reset;
    dc->vmsd = &vmstate_e752x_err;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

/* ------------------- Type registration ------------------- */
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

    static const TypeInfo e752x_err_info = {
        .name = TYPE_E752X_ERR_DEVICE,
        .parent = TYPE_PCI_DEVICE,
        .instance_size = sizeof(E752XErrState),
        .class_init = e752x_err_class_init,
        .interfaces = interfaces,
    };

    type_register_static(&pcibase_info);
    type_register_static(&e752x_err_info);
}

type_init(pcibase_register_types);
