/*
 * QEMU PCI device model for RDC321x Southbridge
 * 
 * Fixes applied:
 * 1. Changed PCI Class to PCI_CLASS_BRIDGE_ISA (0x0601) to correctly match Southbridge semantics.
 * 2. Explicitly set Interrupt Pin to 0 to confirm no PCI IRQ usage.
 * 3. Updated Device Category to DEVICE_CATEGORY_BRIDGE.
 */

#include "qemu/osdep.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_device.h"
#include "hw/qdev-properties.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qom/object.h"

#define TYPE_PCIBASE_DEVICE "southbridge_pci"
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI_VENDOR_ID_RDC       0x17f3
#define PCI_DEVICE_ID_RDC_R6030 0x6030

#define RDC321X_WDT_CTRL        0x44
#define RDC321X_GPIO_CTRL_REG1  0x48
#define RDC321X_GPIO_CTRL_REG2  0x84

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Shadow registers for config space fields */
    uint32_t wdt_ctrl;
    uint32_t gpio_ctrl_reg1;
    uint32_t gpio_ctrl_reg2;
};

static void pcibase_reset(DeviceState *dev)
{
    PCIDevice *pdev = PCI_DEVICE(dev);
    PCIBaseState *s = PCIBASE_DEVICE(dev);

    pci_device_reset(pdev);

    /* Reset shadow registers to 0 */
    s->wdt_ctrl = 0;
    s->gpio_ctrl_reg1 = 0;
    s->gpio_ctrl_reg2 = 0;

    /* Sync to config space */
    pci_set_long(pdev->config + RDC321X_WDT_CTRL, s->wdt_ctrl);
    pci_set_long(pdev->config + RDC321X_GPIO_CTRL_REG1, s->gpio_ctrl_reg1);
    pci_set_long(pdev->config + RDC321X_GPIO_CTRL_REG2, s->gpio_ctrl_reg2);
}

static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    return pci_default_read_config(pdev, addr, len);
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    pci_default_write_config(pdev, addr, val, len);

    /* Update shadow registers from config space */
    s->wdt_ctrl = pci_get_long(pdev->config + RDC321X_WDT_CTRL);
    s->gpio_ctrl_reg1 = pci_get_long(pdev->config + RDC321X_GPIO_CTRL_REG1);
    s->gpio_ctrl_reg2 = pci_get_long(pdev->config + RDC321X_GPIO_CTRL_REG2);
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_RDC);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_RDC_R6030);
    /* Changed to ISA Bridge to match Southbridge semantics */
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_BRIDGE_ISA);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    
    /* Explicitly set Interrupt Pin to 0 (no interrupt) */
    pci_set_byte(pci_conf + PCI_INTERRUPT_PIN, 0x00);

    /* 
     * Enable write access to custom configuration registers.
     */
    pci_set_long(pdev->wmask + RDC321X_WDT_CTRL, 0xFFFFFFFF);
    pci_set_long(pdev->wmask + RDC321X_GPIO_CTRL_REG1, 0xFFFFFFFF);
    pci_set_long(pdev->wmask + RDC321X_GPIO_CTRL_REG2, 0xFFFFFFFF);
}

static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->config_read = pcibase_config_read;
    k->config_write = pcibase_config_write;
    k->realize = pcibase_realize;
    dc->reset = pcibase_reset;
    /* Changed category to BRIDGE */
    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
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
