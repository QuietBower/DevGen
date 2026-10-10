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

#define TYPE_PCIBASE_DEVICE "pata_marvell_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x11AB
#define DEVICE_ID 0x6101
#define CLASS_ID  0x0101

/* Register offsets from supplementary driver source */
#define MARVELL_DEVCTL_OFFSET  0x0C
#define MARVELL_PATA_ACTIVE_BIT  0x10

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

    /* Hardware Register Shadows (The 'Identity' of the device) */

    /* DMA Context */

    /* Operational status flags */
    /* State used to handle reset sequences */
    /* Power management state (D0-D3) */
};

/* Internal helper for status-triggered signaling. Not used as no IRQs needed. Deleted. */

/* Device-initiated DMA logic not required. Deleted. */

/* I/O handlers for legacy IDE task file (BAR0, primary command block) */
static uint64_t ide_cmd_read(void *opaque, hwaddr addr, unsigned size)
{
    /* Reads always return 0 to indicate no device presence */
    return 0;
}

static void ide_cmd_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Ignore all writes */
}

static const MemoryRegionOps ide_cmd_ops = {
    .read = ide_cmd_read,
    .write = ide_cmd_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* I/O handlers for legacy IDE control block (BAR1, primary control block) */
static uint64_t ide_ctrl_read(void *opaque, hwaddr addr, unsigned size)
{
    /* Return 0, including device control / status */
    return 0;
}

static void ide_ctrl_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Ignore writes, e.g., SRST */
}

static const MemoryRegionOps ide_ctrl_ops = {
    .read = ide_ctrl_read,
    .write = ide_ctrl_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 1 },
    .impl  = { .min_access_size = 1, .max_access_size = 1 },
};

/* I/O handlers for BMDMA registers (BAR4) */
static uint64_t bmdma_read(void *opaque, hwaddr addr, unsigned size)
{
    /* Return 0 for all DMA registers */
    return 0;
}

static void bmdma_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Ignore writes */
}

static const MemoryRegionOps bmdma_ops = {
    .read = bmdma_read,
    .write = bmdma_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Driver does not access BAR5 for device 0x6101; always return 0 */
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* Ignore writes */
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* This handler is not used for any active PIO BAR; defined for completeness */
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    /* Not used */
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

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* All registers reset to 0; no additional state to clear */
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

    /* Set programming interface to native mode, primary only */
    pci_set_byte(pci_conf + PCI_CLASS_PROG, 0x05);

    /* BAR Initialization: Legacy IDE task file (primary command block, BAR0) */
    memory_region_init_io(&s->bar_regions[0], OBJECT(s), &ide_cmd_ops, s, "ide-cmd", 8);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_IO, &s->bar_regions[0]);

    /* Legacy IDE control block (primary control, BAR1) */
    memory_region_init_io(&s->bar_regions[1], OBJECT(s), &ide_ctrl_ops, s, "ide-ctrl", 4);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_IO, &s->bar_regions[1]);

    /* BMDMA registers (BAR4) */
    memory_region_init_io(&s->bar_regions[4], OBJECT(s), &bmdma_ops, s, "bmdma", 16);
    pci_register_bar(pdev, 4, PCI_BASE_ADDRESS_SPACE_IO, &s->bar_regions[4]);

    /* Marvell-specific register window (BAR5) */
    memory_region_init_io(&s->bar_regions[5], OBJECT(s), &pcibase_mmio_ops, s, "marvell-bar5", 16);
    pci_register_bar(pdev, 5, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_regions[5]);
}

static void pcibase_uninit(PCIDevice *pdev)
{
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "pata_marvell_pci",
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
