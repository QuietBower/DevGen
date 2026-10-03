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

/* Additional include files retrieved from driver context */

#define TYPE_PCIBASE_DEVICE "ivbep_uncore_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID  0x8086
#define DEVICE_ID  0x3c46 /* PCI_DEVICE_ID_INTEL_UNC_HA */
#define CLASS_ID   0x000000  /* TODO: actual class */

#define SNBEP_PCI_PMON_BOX_CTL            0xf4
#define SNBEP_PCI_PMON_CTL0               0xd8
#define SNBEP_PCI_PMON_CTR0               0xa0
#define SNBEP_HA_PCI_PMON_BOX_ADDRMATCH0  0x40
#define SNBEP_HA_PCI_PMON_BOX_ADDRMATCH1  0x44
#define SNBEP_HA_PCI_PMON_BOX_OPCODEMATCH 0x48

/* Emulation constants: plausible values for register bits */
#define SNBEP_PMON_BOX_CTL_INT   0x00000001
#define SNBEP_PMON_BOX_CTL_FRZ   0x00000010
#define SNBEP_PMON_CTL_EN        0x40000000

/* BAR0 size (unknown, placeholder) */
#define BAR0_SIZE  0x1000

typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM
} BARType;

typedef struct {
    int     index;    /* BAR index 0-5 */
    BARType type;
    hwaddr  size;
    const char *name;
} BARInfo;

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Hardware Register Shadows (Config Space Registers) */
    uint32_t box_ctl;
    uint32_t ctl0;
    uint32_t ctr0_lo;
    uint32_t ctr0_hi;
    uint32_t addr_match0;
    uint32_t addr_match1;
    uint32_t opcode_match;
};

/* Internal helper for status-triggered signaling. Unused, removed. */

/* Device-initiated DMA logic based on driver access patterns. Unused, removed. */

/* MMIO/PIO Handlers: the driver uses PCI config space, so these are unused. */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* Configuration space read handler */
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t address, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint32_t val = pci_default_read_config(pdev, address, len);

    switch (address) {
    case SNBEP_PCI_PMON_BOX_CTL:
        if (len == 4) val = s->box_ctl;
        break;
    case SNBEP_PCI_PMON_CTL0:
        if (len == 4) val = s->ctl0;
        break;
    case SNBEP_PCI_PMON_CTR0:
        if (len == 4) val = s->ctr0_lo;
        break;
    case SNBEP_PCI_PMON_CTR0 + 4:
        if (len == 4) val = s->ctr0_hi;
        break;
    case SNBEP_HA_PCI_PMON_BOX_ADDRMATCH0:
        if (len == 4) val = s->addr_match0;
        break;
    case SNBEP_HA_PCI_PMON_BOX_ADDRMATCH1:
        if (len == 4) val = s->addr_match1;
        break;
    case SNBEP_HA_PCI_PMON_BOX_OPCODEMATCH:
        if (len == 4) val = s->opcode_match;
        break;
    default:
        break;
    }
    return val;
}

/* Configuration space write handler */
static void pcibase_config_write(PCIDevice *pdev, uint32_t address,
                                 uint32_t val, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    switch (address) {
    case SNBEP_PCI_PMON_BOX_CTL:
        if (len == 4) s->box_ctl = val;
        break;
    case SNBEP_PCI_PMON_CTL0:
        if (len == 4) s->ctl0 = val;
        break;
    case SNBEP_PCI_PMON_CTR0:
        if (len == 4) s->ctr0_lo = val;
        break;
    case SNBEP_PCI_PMON_CTR0 + 4:
        if (len == 4) s->ctr0_hi = val;
        break;
    case SNBEP_HA_PCI_PMON_BOX_ADDRMATCH0:
        if (len == 4) s->addr_match0 = val;
        break;
    case SNBEP_HA_PCI_PMON_BOX_ADDRMATCH1:
        if (len == 4) s->addr_match1 = val;
        break;
    case SNBEP_HA_PCI_PMON_BOX_OPCODEMATCH:
        if (len == 4) s->opcode_match = val;
        break;
    default:
        pci_default_write_config(pdev, address, val, len);
        break;
    }
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    memset(&s->box_ctl, 0, sizeof(*s) - offsetof(PCIBaseState, box_ctl));
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }
    
    /* CRITICAL: PCI requires BAR sizes to be a power of 2. Prevent QEMU assert crash. */
    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
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

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR Initialization (unused by driver, but kept for compatibility) */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = BAR0_SIZE;
    s->bar_info[0].name = "uncore-mmio";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize register shadows */
    s->box_ctl = 0;
    s->ctl0 = 0;
    s->ctr0_lo = 0;
    s->ctr0_hi = 0;
    s->addr_match0 = 0;
    s->addr_match1 = 0;
    s->opcode_match = 0;
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "ivbep_uncore_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(box_ctl, PCIBaseState),
        VMSTATE_UINT32(ctl0, PCIBaseState),
        VMSTATE_UINT32(ctr0_lo, PCIBaseState),
        VMSTATE_UINT32(ctr0_hi, PCIBaseState),
        VMSTATE_UINT32(addr_match0, PCIBaseState),
        VMSTATE_UINT32(addr_match1, PCIBaseState),
        VMSTATE_UINT32(opcode_match, PCIBaseState),
        VMSTATE_END_OF_LIST()
    }
};

static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pcibase_realize;
    k->exit    = pcibase_uninit;
    k->config_read  = pcibase_config_read;
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
