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

#define TYPE_PCIBASE_DEVICE "pata_sis_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x1039
#define DEVICE_ID 0x5513
#define CLASS_ID  PCI_CLASS_STORAGE_IDE

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

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Extended config registers (offsets 0x40-0x7F) */
    uint8_t ext_cfg[0x40]; /* index = offset - 0x40 */
};

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "pata_sis: unimplemented MMIO read offset 0x%"PRIx64" size %u\n", addr, size);
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "pata_sis: unimplemented MMIO write offset 0x%"PRIx64" size %u val 0x%"PRIx64"\n", addr, size, val);
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "pata_sis: unimplemented PIO read offset 0x%"PRIx64" size %u\n", addr, size);
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "pata_sis: unimplemented PIO write offset 0x%"PRIx64" size %u val 0x%"PRIx64"\n", addr, size, val);
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

    /* Initialize extended config defaults */
    memset(s->ext_cfg, 0, sizeof(s->ext_cfg));
    s->ext_cfg[0x0A] = 0x06; /* 0x4A: bits 1,2 enable ports 0,1 */
    s->ext_cfg[0x14] = 0x80; /* 0x54 bit31: conceal device ID */

    /* Pre-assign non-overlapping I/O BARs to survive reset and ensure probe succeeds */
    uint32_t bar_values[5] = {
        0x1001, /* BAR0: I/O, size 8, base 0x1000 */
        0x1009, /* BAR1: I/O, size 4, base 0x1008 */
        0x1011, /* BAR2: I/O, size 8, base 0x1010 */
        0x1019, /* BAR3: I/O, size 4, base 0x1018 */
        0x1021  /* BAR4: I/O, size 16, base 0x1020 */
    };
    for (int i = 0; i < 5; i++) {
        pci_default_write_config(PCI_DEVICE(dev), PCI_BASE_ADDRESS_0 + i * 4, bar_values[i], 4);
    }
}

static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t address, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (address >= 0x40 && address + len <= 0x80) {
        uint32_t val = 0;
        memcpy(&val, s->ext_cfg + (address - 0x40), len);
        return val;
    }
    if (address == 0x02 && len == 2) {
        bool conceal = (s->ext_cfg[0x14] & 0x80) != 0;
        return conceal ? 0x5513 : 0x5518;
    }
    return pci_default_read_config(pdev, address, len);
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t address, uint32_t val, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (address >= 0x40 && address + len <= 0x80) {
        memcpy(s->ext_cfg + (address - 0x40), &val, len);
        return;
    }
    pci_default_write_config(pdev, address, val, len);
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }
    
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* Override config access to handle extended registers */
    pdev->config_read = pcibase_config_read;
    pdev->config_write = pcibase_config_write;

    /* BAR Initialization */
    s->num_bars = 5;
    BARInfo bars[] = {
        {0, BAR_TYPE_PIO, 8, "bar0-primary-cmd"},
        {1, BAR_TYPE_PIO, 4, "bar1-primary-ctrl"},
        {2, BAR_TYPE_PIO, 8, "bar2-secondary-cmd"},
        {3, BAR_TYPE_PIO, 4, "bar3-secondary-ctrl"},
        {4, BAR_TYPE_PIO, 16, "bar4-bmdma"},
    };
    memcpy(s->bar_info, bars, sizeof(bars));
    
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Pre-assignment moved to reset function to survive machine resets */
}

static void pcibase_uninit(PCIDevice *pdev)
{
    /* No MSI/MSIX to clean up; template left as-is but safe */
}

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
