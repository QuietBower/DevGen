/*
 * This template provides a robust skeleton for hardware emulation.
 * Integrated QEMU PCI device template (QEMU 8.2.10).
 * Replace #PLACEHOLDER# blocks with driver-specific definitions.
 * Designed for register-level modeling and PCIe driver probing.
 * NOTE: Do not directly fill this file with the Linux kernel source code.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "qemu/host-utils.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "sysemu/dma.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/pci/pcie.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-core.h"
#include "migration/vmstate.h"

#define TYPE_PCIBASE_DEVICE "i5500_temp_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define REG_TSTHRCATA	0xE2
#define REG_TSCTRL	0xE8
#define REG_TSTHRRPEX	0xEB
#define REG_TSTHRLO	0xEC
#define REG_TSTHRHI	0xEE
#define REG_CTHINT	0xF0
#define REG_TSFSC	0xF3
#define REG_CTSTS	0xF4
#define REG_TSTHRRQPI	0xF5
#define REG_CTCTRL	0xF7
#define REG_TSTIMER	0xF8

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint16_t reg_tsthrcata;
    uint8_t  reg_tsctrl;
    uint8_t  reg_tsthrrpex;
    uint16_t reg_tsthrlo;
    uint16_t reg_tsthrhi;
    uint8_t  reg_cthint;
    uint8_t  reg_tsfsc;
    uint8_t  reg_ctsts;
    uint8_t  reg_tsthrrqpi;
    uint8_t  reg_ctctrl;
    uint32_t reg_tstimer;
};

static void pcibase_reset(DeviceState *dev)
{
    PCIDevice *pdev = PCI_DEVICE(dev);

    pci_device_reset(pdev);

    /* Initialize PCI config space registers to avoid probe failure */
    /* tsfsc == 0x7F && tstimer == 0x07D30D40 causes -ENODEV */
    pci_set_byte(pdev->config + REG_TSFSC, 0x00);
    pci_set_long(pdev->config + REG_TSTIMER, 0x00000000);

    /* Other registers read by the driver */
    pci_set_word(pdev->config + REG_TSTHRCATA, 0x0000);
    pci_set_word(pdev->config + REG_TSTHRLO, 0x0000);
    pci_set_word(pdev->config + REG_TSTHRHI, 0x0000);
    pci_set_byte(pdev->config + REG_CTSTS, 0x00);
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_INTEL );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x3438 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_OTHERS );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "i5500_temp_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
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
