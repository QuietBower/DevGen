/*
 * QEMU virtual PCI device model for AEC Time Code (uio_aec)
 *
 * Fixes:
 * - Implemented interrupt trigger logic at offset 0x2F
 * - Verified register offsets and bit flags against driver logic
 * - Ensured correct BAR0 MMIO layout
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_device.h"
#include "hw/qdev-properties.h"
#include "qom/object.h"
#include "exec/memory.h"

#define TYPE_PCIBASE_DEVICE "aectc_pci"
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Offsets & Constants */
#define INT_ENABLE_ADDR     0xFC
#define INT_MASK_ADDR       0x2E
#define INTA_DRVR_ADDR      0xFE
#define MAILBOX             0x0F
#define TRIGGER_ADDR        0x2F

#define INT_ENABLE          0x10
#define INT_DISABLE         0x00
#define INTA_ENABLED_FLAG   0x08
#define INTA_FLAG           0x01

#define PCI_VENDOR_ID_AEC           0xaecb
#define PCI_DEVICE_ID_AEC_VITCLTC   0x6250

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion mmio;

    /* Registers */
    uint32_t int_enable;
    uint8_t int_mask;
    uint8_t inta_drvr;
    uint8_t mailbox;

    /* Board ID (simulated) */
    uint8_t board_vendor[2];
    uint8_t board_number[2];
    uint8_t board_revision[2];
};

static void pcibase_update_irq(PCIBaseState *s)
{
    /*
     * Interrupt logic:
     * IRQ is asserted if interrupts are enabled (INTA_ENABLED_FLAG)
     * AND an interrupt condition is active (INTA_FLAG).
     */
    bool enabled = (s->inta_drvr & INTA_ENABLED_FLAG);
    bool active = (s->inta_drvr & INTA_FLAG);

    pci_set_irq(&s->parent_obj, enabled && active);
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case 0x00: val = s->board_vendor[0]; break;
    case 0x01: val = s->board_vendor[1]; break;
    case 0x02: val = s->board_number[0]; break;
    case 0x03: val = s->board_number[1]; break;
    case 0x06: val = s->board_revision[0]; break;
    case 0x07: val = s->board_revision[1]; break;

    case MAILBOX: /* 0x0F */
        val = s->mailbox;
        /* 
         * Reading mailbox acknowledges/clears the interrupt in the device.
         * Driver: "read mailbox to ensure board drops irq"
         */
        s->inta_drvr &= ~INTA_FLAG;
        pcibase_update_irq(s);
        break;

    case INT_MASK_ADDR: /* 0x2E */
        val = s->int_mask;
        break;

    case INT_ENABLE_ADDR: /* 0xFC */
        val = s->int_enable;
        break;

    case INTA_DRVR_ADDR: /* 0xFE */
        val = s->inta_drvr;
        break;

    default:
        /* Allow reading 0 for undefined registers to prevent crashes/logs spam */
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case INT_MASK_ADDR: /* 0x2E */
        s->int_mask = val & 0xFF;
        break;

    case INT_ENABLE_ADDR: /* 0xFC */
        s->int_enable = (uint32_t)val;
        /*
         * Sync enable register with status flag.
         * Driver checks INTA_DRVR_ADDR for INTA_ENABLED_FLAG.
         */
        if (s->int_enable == INT_ENABLE) {
            s->inta_drvr |= INTA_ENABLED_FLAG;
        } else {
            s->inta_drvr &= ~INTA_ENABLED_FLAG;
        }
        pcibase_update_irq(s);
        break;

    case TRIGGER_ADDR: /* 0x2F */
        /*
         * Application writes here to trigger an interrupt.
         * We simulate the hardware generating an interrupt event.
         */
        s->inta_drvr |= INTA_FLAG;
        pcibase_update_irq(s);
        break;

    default:
        break;
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);

    s->int_enable = 0;
    s->int_mask = 0;
    s->inta_drvr = 0;
    s->mailbox = 0;

    /* Dummy board data */
    s->board_vendor[0] = 0x11; s->board_vendor[1] = 0x22;
    s->board_number[0] = 0x33; s->board_number[1] = 0x44;
    s->board_revision[0] = 'A'; s->board_revision[1] = '0';

    pcibase_update_irq(s);
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_AEC);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_AEC_VITCLTC);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_OTHERS);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    
    /* Interrupt Pin A */
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* BAR0: MMIO 256 bytes */
    memory_region_init_io(&s->mmio, OBJECT(s), &pcibase_mmio_ops, s, "aectc-mmio", 0x100);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mmio);
}

static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pcibase_realize;
    dc->reset = pcibase_reset;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->desc = "AEC Time Code PCI Device";
}

static const TypeInfo pcibase_info = {
    .name = TYPE_PCIBASE_DEVICE,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIBaseState),
    .class_init = pcibase_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void pcibase_register_types(void)
{
    type_register_static(&pcibase_info);
}

type_init(pcibase_register_types);
