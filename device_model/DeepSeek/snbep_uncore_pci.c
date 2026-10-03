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
/* No additional includes needed */

#define TYPE_PCIBASE_DEVICE "snbep_uncore_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
/* PCI Vendor/Device IDs */
#ifndef PCI_VENDOR_ID_INTEL
#define PCI_VENDOR_ID_INTEL 0x8086
#endif
/* Device ID for Intel Uncore Home Agent */
#ifndef PCI_DEVICE_ID_INTEL_UNC_HA
#define PCI_DEVICE_ID_INTEL_UNC_HA 0x3c46
#endif

/* Register offsets (from uncore_snbep.c) */
#define SNBEP_PCI_PMON_BOX_CTL         0xf4
#define SNBEP_PCI_PMON_CTL0            0xd8
#define SNBEP_PCI_PMON_CTR0            0xa0
#define SNBEP_HA_PCI_PMON_BOX_ADDRMATCH0 0x40
#define SNBEP_HA_PCI_PMON_BOX_ADDRMATCH1 0x44
#define SNBEP_HA_PCI_PMON_BOX_OPCODEMATCH 0x48

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

typedef struct {
    uint64_t addr_match0;
    uint64_t addr_match1;
    uint64_t opcode_match;
    uint64_t counter[4];
    uint64_t ctl[4];
    uint64_t box_ctl;
} SNBEP_HA_Regs;

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    SNBEP_HA_Regs regs;

    /* DMA Context */
    
    
    
    
    
};


/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* Not used by driver, return 0 */
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Not used by driver */
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* Not used by driver */
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Not used by driver */
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

/* Config space read/write handlers */
static uint32_t pcibase_config_read(PCIDevice *d, uint32_t addr, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(d);
    switch (addr) {
    case 0x40: return s->regs.addr_match0 & 0xffffffff;
    case 0x44: return s->regs.addr_match1 & 0xffffffff;
    case 0x48: return s->regs.opcode_match & 0xffffffff;
    case 0xa0: return s->regs.counter[0] & 0xffffffff;
    case 0xa4: return (s->regs.counter[0] >> 32) & 0xffffffff;
    case 0xa8: return s->regs.counter[1] & 0xffffffff;
    case 0xac: return (s->regs.counter[1] >> 32) & 0xffffffff;
    case 0xb0: return s->regs.counter[2] & 0xffffffff;
    case 0xb4: return (s->regs.counter[2] >> 32) & 0xffffffff;
    case 0xb8: return s->regs.counter[3] & 0xffffffff;
    case 0xbc: return (s->regs.counter[3] >> 32) & 0xffffffff;
    case 0xd8: return s->regs.ctl[0] & 0xffffffff;
    case 0xdc: return s->regs.ctl[1] & 0xffffffff;
    case 0xe0: return s->regs.ctl[2] & 0xffffffff;
    case 0xe4: return s->regs.ctl[3] & 0xffffffff;
    case 0xf4: return s->regs.box_ctl & 0xffffffff;
    default: return pci_default_read_config(d, addr, len);
    }
}

static void pcibase_config_write(PCIDevice *d, uint32_t addr, uint32_t val, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(d);
    switch (addr) {
    case 0x40: s->regs.addr_match0 = (s->regs.addr_match0 & ~0xffffffffULL) | val; break;
    case 0x44: s->regs.addr_match1 = (s->regs.addr_match1 & ~0xffffffffULL) | val; break;
    case 0x48: s->regs.opcode_match = (s->regs.opcode_match & ~0xffffffffULL) | val; break;
    case 0xa0: s->regs.counter[0] = (s->regs.counter[0] & ~0xffffffffULL) | val; break;
    case 0xa4: s->regs.counter[0] = (s->regs.counter[0] & 0xffffffffULL) | ((uint64_t)val << 32); break;
    case 0xa8: s->regs.counter[1] = (s->regs.counter[1] & ~0xffffffffULL) | val; break;
    case 0xac: s->regs.counter[1] = (s->regs.counter[1] & 0xffffffffULL) | ((uint64_t)val << 32); break;
    case 0xb0: s->regs.counter[2] = (s->regs.counter[2] & ~0xffffffffULL) | val; break;
    case 0xb4: s->regs.counter[2] = (s->regs.counter[2] & 0xffffffffULL) | ((uint64_t)val << 32); break;
    case 0xb8: s->regs.counter[3] = (s->regs.counter[3] & ~0xffffffffULL) | val; break;
    case 0xbc: s->regs.counter[3] = (s->regs.counter[3] & 0xffffffffULL) | ((uint64_t)val << 32); break;
    case 0xd8: s->regs.ctl[0] = (s->regs.ctl[0] & ~0xffffffffULL) | val; break;
    case 0xdc: s->regs.ctl[1] = (s->regs.ctl[1] & ~0xffffffffULL) | val; break;
    case 0xe0: s->regs.ctl[2] = (s->regs.ctl[2] & ~0xffffffffULL) | val; break;
    case 0xe4: s->regs.ctl[3] = (s->regs.ctl[3] & ~0xffffffffULL) | val; break;
    case 0xf4: s->regs.box_ctl = (s->regs.box_ctl & ~0xffffffffULL) | val; break;
    default: pci_default_write_config(d, addr, val, len); break;
    }
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    memset(&s->regs, 0, sizeof(s->regs));
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x8086 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_INTEL_UNC_HA );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0880 );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo){
        .index = 0,
        .type = BAR_TYPE_MMIO,
        .size = 0x200,
        .name = "snbep-ha-mmio"
    };

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
    
    /* Final state initialization before the device is 'live' */
    memset(&s->regs, 0, sizeof(s->regs));
    
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

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "snbep_uncore_pci",
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

    k->config_read = pcibase_config_read;
    k->config_write = pcibase_config_write;
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
