/*
 * QEMU PCI device model for Advantech PCI-1760
 * Based on Linux driver adv_pci1760.c (comedi)
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

#define TYPE_PCIBASE_DEVICE "adv_pci1760_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Vendor and device IDs from driver's pci_device_id table */
#define PCI_VENDOR_ID_ADVANTECH    0x13fe
#define DEVICE_ID                  0x1760
#define CLASS_ID                   0xff00

/* Register offsets relative to BAR0 (PIO) */
#define PCI1760_OMB_REG(x)         (0x0c + (x))
#define PCI1760_IMB_REG(x)         (0x1c + (x))
#define PCI1760_INTCSR_REG(x)      (0x38 + (x))

/* Command codes used by driver */
#define PCI1760_CMD_CLR_IMB2       0x00
#define PCI1760_CMD_SET_DO         0x01
#define PCI1760_CMD_GET_DO         0x02
#define PCI1760_CMD_GET_STATUS     0x07

/* BAR info types */
typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM
} BARType;

typedef struct {
    int     index;
    BARType type;
    hwaddr  size;
    const char *name;
} BARInfo;

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Memory regions for BARs */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability flags */
    bool has_msi;
    bool has_msix;

    /* Hardware register shadows */
    uint8_t omb[4];      /* Output Message Buffer (0x0C-0x0F) */
    uint8_t imb[4];      /* Input Message Buffer (0x1C-0x1F) */
    uint8_t intcsr[4];   /* Interrupt Control/Status (0x38-0x3B) */

    /* Device state */
    uint8_t do_state;                /* Digital output state */
    uint16_t command_data[256];      /* Storage for command values (for GET_STATUS replies) */

    /* Probe/Reset state */
    bool reset_active;
};

/* Command execution: triggered by write to OMB3 */
static void pci1760_handle_command(PCIBaseState *s)
{
    uint8_t cmd = s->omb[2];
    uint16_t val = (s->omb[1] << 8) | s->omb[0];

    if (cmd == PCI1760_CMD_CLR_IMB2) {
        /* Clear IMB2 echo */
        s->imb[2] = 0;
        s->imb[0] = 0;
        s->imb[1] = 0;
        return;
    }

    if (cmd == PCI1760_CMD_SET_DO) {
        s->do_state = val & 0xFF;
        s->imb[2] = cmd;
        s->imb[0] = 0;
        s->imb[1] = 0;
        return;
    }

    if (cmd == PCI1760_CMD_GET_DO) {
        s->imb[0] = s->do_state & 0xFF;
        s->imb[1] = 0;
        s->imb[2] = cmd;
        return;
    }

    if (cmd == PCI1760_CMD_GET_STATUS) {
        uint16_t sub = val;
        uint16_t result = s->command_data[sub & 0xFF];
        s->imb[0] = result & 0xFF;
        s->imb[1] = (result >> 8) & 0xFF;
        s->imb[2] = cmd;
        return;
    }

    /* For all other commands, store the value and acknowledge */
    s->command_data[cmd] = val;
    s->imb[2] = cmd;
    s->imb[0] = 0;
    s->imb[1] = 0;
}

/* PIO read handler (1-byte accesses) */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= 0x00 && addr <= 0x0F) {
        if (addr >= 0x0C) {
            return s->omb[addr - 0x0C];
        }
        /* unused region, return 0 */
        return 0;
    }

    if (addr >= 0x10 && addr <= 0x1F) {
        if (addr >= 0x1C) {
            return s->imb[addr - 0x1C];
        }
        return 0;
    }

    if (addr >= 0x20 && addr <= 0x3F) {
        if (addr >= 0x38 && addr <= 0x3B) {
            return s->intcsr[addr - 0x38];
        }
        return 0;
    }

    return 0;
}

/* PIO write handler (1-byte accesses) */
static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= 0x00 && addr <= 0x0F) {
        if (addr >= 0x0C) {
            s->omb[addr - 0x0C] = val & 0xFF;
            if (addr == 0x0F) {
                /* OMB3 written: execute command */
                pci1760_handle_command(s);
            }
        }
        return;
    }

    if (addr >= 0x10 && addr <= 0x1F) {
        if (addr >= 0x1C) {
            s->imb[addr - 0x1C] = val & 0xFF;
        }
        return;
    }

    if (addr >= 0x20 && addr <= 0x3F) {
        if (addr >= 0x38 && addr <= 0x3B) {
            s->intcsr[addr - 0x38] = val & 0xFF;
        }
        return;
    }
}

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 1 },
    .impl  = { .min_access_size = 1, .max_access_size = 1 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Clear all registers and state */
    memset(s->omb, 0, sizeof(s->omb));
    memset(s->imb, 0, sizeof(s->imb));
    memset(s->intcsr, 0, sizeof(s->intcsr));
    s->do_state = 0;
    memset(s->command_data, 0, sizeof(s->command_data));
    s->reset_active = false;
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else {
        error_setg(errp, "Unsupported BAR type");
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x13fe);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x1760);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0xff00);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* Single BAR: PIO at BAR0, size 0x40 */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 0x40;
    s->bar_info[0].name = "pci1760-pio";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize hardware state */
    s->do_state = 0;
    memset(s->command_data, 0, sizeof(s->command_data));
    s->reset_active = false;
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
    .name = "adv_pci1760_pci",
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
