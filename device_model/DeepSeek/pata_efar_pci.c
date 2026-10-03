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
#include "hw/qdev-properties.h"
#include "hw/qdev-core.h"
#include "migration/vmstate.h"

#define TYPE_PCIBASE_DEVICE "pata_efar_pci"
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI_VENDOR_ID_EFAR 0x1055
#define PCI_DEVICE_ID_EFAR_9130 0x9130
#define PCI_CLASS_STORAGE_IDE 0x0101

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

    /* Custom PCI config registers for EFAR IDE controller */
    uint16_t master_data[2];  /* 0x40, 0x42 */
    uint8_t slave_data;       /* 0x44 */
    uint8_t cable_detect;     /* 0x47 */
    uint8_t udma_enable;      /* 0x48 */
    uint16_t udma_timing;     /* 0x4A */
};

/* PCI config read/write overrides */
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint32_t val = pci_default_read_config(pdev, addr, len);

    switch (addr) {
    case 0x40:
        if (len >= 2) val = s->master_data[0];
        break;
    case 0x42:
        if (len >= 2) val = s->master_data[1];
        break;
    case 0x44:
        val = s->slave_data;
        break;
    case 0x47:
        val = s->cable_detect;
        break;
    case 0x48:
        val = s->udma_enable;
        break;
    case 0x4A:
        if (len >= 2) val = s->udma_timing;
        else val = (s->udma_timing >> ((addr & 1) * 8)) & 0xff;
        break;
    default:
        break;
    }
    return val;
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    pci_default_write_config(pdev, addr, val, len);

    switch (addr) {
    case 0x40:
        if (len >= 2) s->master_data[0] = val;
        break;
    case 0x42:
        if (len >= 2) s->master_data[1] = val;
        break;
    case 0x44:
        s->slave_data = val;
        break;
    case 0x47:
        s->cable_detect = val;
        break;
    case 0x48:
        s->udma_enable = val;
        break;
    case 0x4A:
        if (len >= 2) s->udma_timing = val;
        else {
            uint8_t *p = (uint8_t *)&s->udma_timing + (addr & 1);
            *p = val;
        }
        break;
    default:
        break;
    }
}

/* MMIO Handlers for IDE registers (used by all BARs now) */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

/* Unused PIO Handlers (kept for completeness) */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
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
    PCIDevice *pdev = PCI_DEVICE(dev);
    uint8_t *pci_conf = pdev->config;

    pci_device_reset(pdev);

    /* Initialize custom registers with reset defaults */
    s->master_data[0] = 0x4000; /* SITRE set */
    s->master_data[1] = 0x4000;
    s->slave_data = 0;
    s->cable_detect = 0; /* indicate 80-conductor cable */
    s->udma_enable = 0;
    s->udma_timing = 0;

    /* Enable port0 and port1 by setting bit7 of 0x41 and 0x43 */
    pci_conf[0x41] |= 0x80;
    pci_conf[0x43] |= 0x80;
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

    /* Set PCI identification */
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_EFAR);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_EFAR_9130);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_STORAGE_IDE);
    pci_config_set_class(pci_conf, PCI_CLASS_STORAGE_IDE);
    pci_config_set_prog_interface(pci_conf, 0x8a);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Override config read/write for custom registers */
    pdev->config_read = pcibase_config_read;
    pdev->config_write = pcibase_config_write;

    /* BAR setup: legacy IDE ports as I/O BARs */
    static const BARInfo bmdma_ide_bars[] = {
        {0, BAR_TYPE_PIO, 8, "primary-io"},
        {1, BAR_TYPE_PIO, 4, "primary-ctl"},
        {2, BAR_TYPE_PIO, 8, "secondary-io"},
        {3, BAR_TYPE_PIO, 4, "secondary-ctl"},
        {4, BAR_TYPE_PIO, 16, "bmdma"},
    };
    s->num_bars = 5;
    memcpy(s->bar_info, bmdma_ide_bars, sizeof(bmdma_ide_bars));

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
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
    .name = "pata_efar_pci",
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
