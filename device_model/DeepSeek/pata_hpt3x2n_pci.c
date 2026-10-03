/*
 * QEMU device model for pata_hpt3x2n (HPT366/368/370/371/372N ATA controller)
 * Based on Linux driver pata_hpt3x2n.c
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

#define TYPE_PCIBASE_DEVICE "pata_hpt3x2n_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define VENDOR_ID 0x1103
#define DEVICE_ID 0x0004
#define CLASS_ID  0x0101

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

    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;

    uint8_t config_shadow[256];
    uint8_t bar4_io[0x100];
    bool dpll_active;
};

static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t address, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (address < 0x40) {
        /* BAR0 is not registered; BAR1-3 are unimplemented; always return 0 */
        if (address >= 0x14 && address < 0x20) {
            return 0;
        }
        return pci_default_read_config(pdev, address, len);
    } else if (address + len <= 256) {
        /* Special handling for 0x5B during DPLL calibration */
        if (address == 0x5B && len == 1 && s->dpll_active) {
            return 0x80;
        }
        switch (len) {
            case 1: return s->config_shadow[address];
            case 2: return lduw_le_p(s->config_shadow + address);
            case 4: return ldl_le_p(s->config_shadow + address);
            default: return ~0;
        }
    }
    return ~0;
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t address, uint32_t val, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (address < 0x40) {
        /* Ignore writes to unimplemented BAR1-3 */
        if (address >= 0x14 && address < 0x20) {
            return;
        }
        pci_default_write_config(pdev, address, val, len);
    } else if (address + len <= 256) {
        switch (len) {
            case 1:
                s->config_shadow[address] = val;
                break;
            case 2:
                stw_le_p(s->config_shadow + address, val);
                break;
            case 4:
                stl_le_p(s->config_shadow + address, val);
                break;
        }
        /* DPLL activation detection */
        if (address == 0x5B && len == 1 && val == 0x21) {
            s->dpll_active = true;
        }
        /* DPLL calibration completion detection */
        if (address == 0x5C && len == 4) {
            s->dpll_active = false;
        }
    }
}

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    /* BMDMA interrupt status from primary (offset 2) and secondary (offset 0xA) */
    uint8_t prim_status = s->bar4_io[2] & 0x04; /* bit2: DMA error? Interrupt is via device line. Keep IRQ low for probe */
    uint8_t sec_status = s->bar4_io[0xA] & 0x04;
    if (prim_status || sec_status) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    memset(s->config_shadow, 0, sizeof(s->config_shadow));
    memset(s->bar4_io, 0, sizeof(s->bar4_io));
    s->dpll_active = false;
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    if (addr + size <= sizeof(s->bar4_io)) {
        switch (size) {
            case 1: val = s->bar4_io[addr]; break;
            case 2: val = lduw_le_p(s->bar4_io + addr); break;
            case 4: val = ldl_le_p(s->bar4_io + addr); break;
            case 8: val = ldq_le_p(s->bar4_io + addr); break;
            default: val = ~0;
        }
    }
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr + size <= sizeof(s->bar4_io)) {
        switch (size) {
            case 1: s->bar4_io[addr] = val; break;
            case 2: stw_le_p(s->bar4_io + addr, val); break;
            case 4: stl_le_p(s->bar4_io + addr, val); break;
            case 8: stq_le_p(s->bar4_io + addr, val); break;
        }
        /* Update IRQ after any PIO write that might affect BMDMA status */
        pcibase_update_irq(s);
    }
}

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

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
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x06);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR0 was removed to fix BAR assignment failure */
    /* Only BAR4: I/O port range of size 256 bytes */
    s->bar_info[0].index = 4;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 0x100;
    s->bar_info[0].name = "bar4-pio";
    s->num_bars = 1;
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize config shadow */
    memset(s->config_shadow, 0, sizeof(s->config_shadow));
    /* Pre-set 0x5A to have bit1=0 (ATA_CBL_PATA80) */
    s->config_shadow[0x5A] = 0x00;
    /* Set initial DPLL inactive */
    s->dpll_active = false;

    /* Initialize BAR4 I/O space with valid values */
    memset(s->bar4_io, 0, sizeof(s->bar4_io));
    /* Clock register 0x90: return 0xABCDE0A0 so that (fcnt>>12)==0xABCDE and freq=66MHz */
    stl_le_p(s->bar4_io + 0x90, 0xABCDE0A0);
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

static const VMStateDescription vmstate_pcibase = {
    .name = "pata_hpt3x2n_pci",
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
    k->config_read = pcibase_config_read;
    k->config_write = pcibase_config_write;
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
