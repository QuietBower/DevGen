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

#define TYPE_PCIBASE_DEVICE "pata_sis_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI_VENDOR_ID_SIS 0x1039
#define PCI_DEVICE_ID_SIS_5513 0x5513

/* Driver-specific PCI config space offsets */
#define SIS_IDE_PORT_BASE_OLD 0x40
#define SIS_IDE_CABLE_DETECT_66 0x48
#define SIS_IDE_COMPAT_49 0x49
#define SIS_IDE_ENABLE_4A 0x4a
#define SIS_IDE_FIFO_4B 0x4b
#define SIS_IDE_CABLE_DETECT_133 0x50
#define SIS_IDE_COMPAT_52 0x52
#define SIS_IDE_MISC_54 0x54
#define SIS_IDE_PORT_BASE_NEW 0x70

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

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;
};

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;
    uint64_t val = 0;
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;
    uint64_t val = 0;
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;
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

static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int l)
{
    uint32_t val = pci_default_read_config(pdev, addr, l);
    
    if (addr == PCI_DEVICE_ID && l == 2) {
        uint32_t idemisc = pci_get_long(pdev->config + 0x54);
        if ((idemisc & 0x80000000) == 0) {
            return 0x5518; /* SIS 962/963 */
        }
    } else if (addr == PCI_VENDOR_ID && l == 4) {
        uint32_t idemisc = pci_get_long(pdev->config + 0x54);
        if ((idemisc & 0x80000000) == 0) {
            return (0x5518 << 16) | PCI_VENDOR_ID_SIS;
        }
    }
    return val;
}

static void pcibase_reset(DeviceState *dev)
{
    PCIDevice *pdev = PCI_DEVICE(dev);
    pci_device_reset(pdev);
    
    uint8_t *pci_conf = pdev->config;
    
    /* Restore default config space values after reset */
    pci_set_long(pci_conf + 0x54, 0x80000000);
    pci_set_byte(pci_conf + 0x4a, 0x06);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x1039 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x5513 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_STORAGE_IDE );
    pci_set_byte(pci_conf + PCI_CLASS_PROG, 0x8f); /* Native mode */
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* Initialize device-specific config space */
    pci_set_long(pci_conf + 0x54, 0x80000000); /* idemisc, bit 31 set by default */
    pci_set_byte(pci_conf + 0x4a, 0x06);       /* Enable both IDE ports */

    /* Make device-specific config space writable */
    for (int i = 0x40; i < 0x80; i++) {
        pdev->wmask[i] = 0xff;
    }

    /* Define standard IDE BARs */
    s->num_bars = 5;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 8;
    s->bar_info[0].name = "ide-cmd0";

    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_PIO;
    s->bar_info[1].size = 4;
    s->bar_info[1].name = "ide-ctrl0";

    s->bar_info[2].index = 2;
    s->bar_info[2].type = BAR_TYPE_PIO;
    s->bar_info[2].size = 8;
    s->bar_info[2].name = "ide-cmd1";

    s->bar_info[3].index = 3;
    s->bar_info[3].type = BAR_TYPE_PIO;
    s->bar_info[3].size = 4;
    s->bar_info[3].name = "ide-ctrl1";

    s->bar_info[4].index = 4;
    s->bar_info[4].type = BAR_TYPE_PIO;
    s->bar_info[4].size = 16;
    s->bar_info[4].name = "ide-bmdma";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    (void)s;
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "pata_sis_pci",
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
