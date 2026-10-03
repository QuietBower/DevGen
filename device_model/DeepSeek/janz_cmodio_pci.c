/*
 * QEMU PCI device model for Janz CMODIO carrier board
 * Based on Linux driver drivers/mfd/janz-cmodio.c
 * Targets QEMU 8.2.10
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

#define TYPE_PCIBASE_DEVICE "janz_cmodio_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI IDs from driver (PLX 9030) */
#define PCI_VENDOR_ID_PLX  0x10b5
#define PCI_DEVICE_ID_PLX_9030 0x9030

/* Onboard register offsets (byte) */
#define REG_INT_DISABLE      0x01
#define REG_INT_ENABLE       0x03
#define REG_RESET_ASSERT     0x05
#define REG_RESET_DEASSERT   0x07
#define REG_EEP              0x09
#define REG_ENID             0x0b

/* BAR indices */
#define BAR_MODULBUS  3
#define BAR_ONBOARD   4

#define MODULBUS_SIZE 0x800   /* 4 modules * 0x200 each */
#define ONBOARD_REGS_SIZE 0x100

struct janz_cmodio_onboard_regs {
    uint8_t unused1;
    uint8_t int_disable;  /* read: interrupt status, write: interrupt disable */
    uint8_t unused2;
    uint8_t int_enable;   /* read: MODULbus number, write: interrupt enable */
    uint8_t unused3;
    uint8_t reset_assert; /* write-only */
    uint8_t unused4;
    uint8_t reset_deassert; /* write-only */
    uint8_t unused5;
    uint8_t eep;          /* read-write serial EEPROM */
    uint8_t unused6;
    uint8_t enid;         /* write-only EEPROM chip select */
};

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar3_region;
    MemoryRegion bar4_region;

    /* Hardware Register Shadows */
    struct janz_cmodio_onboard_regs onboard_regs;
};

/* MODULbus BAR3 MMIO ops (big-endian, minimal dummy) */
static uint64_t modulbus_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void modulbus_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* no-op */
}

static const MemoryRegionOps modulbus_ops = {
    .read = modulbus_read,
    .write = modulbus_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

/* Onboard registers BAR4 MMIO ops (little-endian) */
static uint64_t onboard_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size != 1) {
        return 0;
    }

    switch (addr) {
    case REG_INT_DISABLE:
        val = s->onboard_regs.int_disable;
        break;
    case REG_INT_ENABLE:
        /* Return the module ID (hex switch) - default 0 */
        val = s->onboard_regs.int_enable;
        break;
    case REG_EEP:
        val = s->onboard_regs.eep;
        break;
    default:
        /* Unused registers: return 0 */
        break;
    }
    return val;
}

static void onboard_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 1) {
        return;
    }

    switch (addr) {
    case REG_INT_DISABLE:
        s->onboard_regs.int_disable = (uint8_t)val;
        break;
    case REG_INT_ENABLE:
        s->onboard_regs.int_enable = (uint8_t)val;
        break;
    case REG_RESET_ASSERT:
        /* Write-only, ignore */
        break;
    case REG_RESET_DEASSERT:
        /* Write-only, ignore */
        break;
    case REG_EEP:
        s->onboard_regs.eep = (uint8_t)val;
        break;
    case REG_ENID:
        /* Write-only, ignore */
        break;
    default:
        break;
    }
}

static const MemoryRegionOps onboard_ops = {
    .read = onboard_read,
    .write = onboard_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_PLX);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_PLX_9030);
    /* Class: bridge / other? Driver uses PCI_CLASS_OTHERS, leave as 0 */
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0680); /* Bridge */
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* PCI Express capability (placeholder) */
    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR3: MODULbus access (big-endian, dummy memory region) */
    memory_region_init_io(&s->bar3_region, OBJECT(s), &modulbus_ops, s,
                          "modulbus", MODULBUS_SIZE);
    pci_register_bar(pdev, BAR_MODULBUS, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar3_region);

    /* BAR4: Onboard registers (little-endian) */
    memory_region_init_io(&s->bar4_region, OBJECT(s), &onboard_ops, s,
                          "onboard", ONBOARD_REGS_SIZE);
    pci_register_bar(pdev, BAR_ONBOARD, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar4_region);
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

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    memset(&s->onboard_regs, 0, sizeof(s->onboard_regs));
}

static const VMStateDescription vmstate_pcibase = {
    .name = "janz_cmodio_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT8(onboard_regs.int_disable, PCIBaseState),
        VMSTATE_UINT8(onboard_regs.int_enable, PCIBaseState),
        VMSTATE_UINT8(onboard_regs.eep, PCIBaseState),
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
