/*
 * QEMU Model for VIA VT82C596 Watchdog Timer (via_wdt)
 * Based on Linux driver via_wdt.c
 * PCI Vendor ID: 0x1106, Device ID: 0x0596
 * Custom config registers at 0xe8 (MMIO base) and 0xec (Control)
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/module.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_device.h"
#include "hw/pci/pcie.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"

/* PCI Identifiers */
#define PCI_VENDOR_ID_VIA           0x1106
#define PCI_DEVICE_ID_VIA_VT82C596_WDT 0x0596

#define TYPE_PCIBASE_DEVICE "via_wdt_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register offsets from driver source */
#define VIA_WDT_MMIO_BASE  0xe8
#define VIA_WDT_CONF        0xec
#define VIA_WDT_CONF_ENABLE 0x01
#define VIA_WDT_CONF_MMIO   0x02
#define VIA_WDT_MMIO_LEN    8

/* Watchdog control/status register offsets (within MMIO region) */
#define VIA_WDT_CTL         0
#define VIA_WDT_COUNT       4

/* Status flags used by driver */
#define VIA_WDT_RUNNING     0x01
#define VIA_WDT_FIRED       0x02
#define VIA_WDT_PWROFF      0x04
#define VIA_WDT_DISABLED    0x08
#define VIA_WDT_TRIGGER     0x80

/* Device state */
struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion mmio;          /* MMIO region (mapped dynamically) */
    uint8_t     conf;           /* VIA_WDT_CONF register value */

    /* Shadow of the MMIO base address (set via VIA_WDT_MMIO_BASE register) */
    uint32_t    mmio_base;

    /* Watchdog registers */
    uint32_t    ctl;            /* VIA_WDT_CTL status/control */
    uint32_t    count;          /* VIA_WDT_COUNT reload value */

    bool        mmio_mapped;    /* Whether the MMIO region is currently mapped */
};

/* Forward declarations of config space handlers */
static uint32_t via_wdt_config_read(PCIDevice *pdev, uint32_t address, int len);
static void via_wdt_config_write(PCIDevice *pdev, uint32_t address, uint32_t val, int len);

/* MMIO read/write handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid read size %u at 0x%"HWADDR_PRIx"\n",
                      __func__, size, addr);
        return ~0ULL;
    }

    switch (addr) {
    case VIA_WDT_CTL:
        val = s->ctl;
        break;
    case VIA_WDT_COUNT:
        val = s->count;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: unknown MMIO read at 0x%"HWADDR_PRIx"\n",
                      __func__, addr);
        val = 0;
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid write size %u at 0x%"HWADDR_PRIx"\n",
                      __func__, size, addr);
        return;
    }

    switch (addr) {
    case VIA_WDT_CTL:
        /* Update control register: the driver may set or clear bits */
        s->ctl = (uint32_t)val;
        break;
    case VIA_WDT_COUNT:
        s->count = (uint32_t)val;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: unknown MMIO write at 0x%"HWADDR_PRIx"\n",
                      __func__, addr);
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

/* Helper to update MMIO mapping based on current conf and base */
static void via_wdt_update_mmio_mapping(PCIBaseState *s)
{
    bool enable = (s->conf & (VIA_WDT_CONF_ENABLE | VIA_WDT_CONF_MMIO)) ==
                  (VIA_WDT_CONF_ENABLE | VIA_WDT_CONF_MMIO);
    if (enable && s->mmio_base && !s->mmio_mapped) {
        /* Map the MMIO region into address space */
        memory_region_add_subregion_overlap(get_system_memory(), s->mmio_base,
                                            &s->mmio, 1);
        s->mmio_mapped = true;
    } else if ((!enable || !s->mmio_base) && s->mmio_mapped) {
        /* Unmap the MMIO region */
        memory_region_del_subregion(get_system_memory(), &s->mmio);
        s->mmio_mapped = false;
    }
}

/* Reset handler */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    s->conf = 0;
    s->mmio_base = 0;
    s->ctl = 0;
    s->count = 0;
    /* Ensure MMIO is unmapped */
    if (s->mmio_mapped) {
        memory_region_del_subregion(get_system_memory(), &s->mmio);
        s->mmio_mapped = false;
    }
}

/* Custom config read handler to support vendor-specific registers */
static uint32_t via_wdt_config_read(PCIDevice *pdev, uint32_t address, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint32_t val;

    if (address == VIA_WDT_MMIO_BASE) {
        /* Return the current MMIO base address */
        if (len == 4) {
            return s->mmio_base;
        } else {
            /* Handle other lengths if needed; driver uses dword */
            return pci_default_read_config(pdev, address, len);
        }
    } else if (address == VIA_WDT_CONF) {
        return s->conf;
    } else {
        return pci_default_read_config(pdev, address, len);
    }
}

/* Custom config write handler to support vendor-specific registers */
static void via_wdt_config_write(PCIDevice *pdev, uint32_t address, uint32_t val, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (address == VIA_WDT_MMIO_BASE) {
        /* Store the 32-bit base address (driver writes dword) */
        if (len == 4) {
            s->mmio_base = val;
            /* If MMIO is currently enabled, remap at the new address */
            if (s->mmio_mapped) {
                memory_region_del_subregion(get_system_memory(), &s->mmio);
                s->mmio_mapped = false;
                if (s->conf & (VIA_WDT_CONF_ENABLE | VIA_WDT_CONF_MMIO) == (VIA_WDT_CONF_ENABLE | VIA_WDT_CONF_MMIO) && s->mmio_base) {
                    memory_region_add_subregion_overlap(get_system_memory(), s->mmio_base, &s->mmio, 1);
                    s->mmio_mapped = true;
                }
            }
        }
        return;
    } else if (address == VIA_WDT_CONF) {
        s->conf = val & 0xff;
        via_wdt_update_mmio_mapping(s);
    } else {
        pci_default_write_config(pdev, address, val, len);
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* PCI IDs */
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_VIA);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_VIA_VT82C596_WDT);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_SYSTEM_OTHER);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    /* No interrupt used, set pin to 0 */
    pci_config_set_interrupt_pin(pci_conf, 0);

    /* Initialize MMIO region but do not map it yet (dynamic mapping on enable) */
    memory_region_init_io(&s->mmio, OBJECT(s), &pcibase_mmio_ops, s,
                          "via_wdt-mmio", VIA_WDT_MMIO_LEN);
    s->mmio_mapped = false;
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (s->mmio_mapped) {
        memory_region_del_subregion(get_system_memory(), &s->mmio);
        s->mmio_mapped = false;
    }
}

/* Minimal VMState */
static const VMStateDescription vmstate_pcibase = {
    .name = "via_wdt_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT8(conf, PCIBaseState),
        VMSTATE_UINT32(mmio_base, PCIBaseState),
        VMSTATE_UINT32(ctl, PCIBaseState),
        VMSTATE_UINT32(count, PCIBaseState),
        VMSTATE_BOOL(mmio_mapped, PCIBaseState),
        VMSTATE_END_OF_LIST()
    }
};

static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pcibase_realize;
    k->exit    = pcibase_uninit;
    k->config_read  = via_wdt_config_read;
    k->config_write = via_wdt_config_write;
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
