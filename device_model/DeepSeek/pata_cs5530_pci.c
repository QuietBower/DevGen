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
#include "hw/pci/pci_ids.h"

/* Cyrix vendor/device IDs not in QEMU pci_ids.h */
#define PCI_VENDOR_ID_CYRIX 0x1078
#define PCI_DEVICE_ID_CYRIX_5530_IDE 0x0102

#define TYPE_PCIBASE_DEVICE "pata_cs5530_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID PCI_VENDOR_ID_CYRIX
#define DEVICE_ID PCI_DEVICE_ID_CYRIX_5530_IDE
#define CLASS_ID PCI_CLASS_STORAGE_IDE

#define NUM_PORTS 2

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

    /* BMDMA registers (standard) */
    uint8_t bmdma_primary_cmd;
    uint8_t bmdma_primary_status;
    uint32_t bmdma_primary_dtpr;

    /* CS5530-specific extra timing registers (per port: 0 and 1) */
    uint32_t pio_timing_master[NUM_PORTS];   /* offset 0x00 */
    uint32_t dma_timing_master[NUM_PORTS];  /* offset 0x04 */
    uint32_t pio_timing_slave[NUM_PORTS];   /* offset 0x08 */
    uint32_t dma_timing_slave[NUM_PORTS];   /* offset 0x0C */
};

/* IDE legacy I/O handlers */
static uint64_t pcibase_ide_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    /* Minimal ATA status for probing: return DRDY | DSC */
    if (addr == 7 && size == 1) {
        val = 0x50;
    }
    return val;
}

static void pcibase_ide_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Ignore writes for minimal functionality */
}

static const MemoryRegionOps pcibase_ide_ops = {
    .read = pcibase_ide_read,
    .write = pcibase_ide_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* PIO Handlers for the BMDMA BAR */
static uint64_t pcibase_bmdma_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= 0x20 && addr < 0x40) {
        /* Extra timing registers */
        unsigned port = (addr - 0x20) / 0x10;
        unsigned reg = (addr - 0x20) % 0x10;
        if (port >= NUM_PORTS) return 0;
        switch (reg) {
        case 0x00: val = s->pio_timing_master[port]; break;
        case 0x04: val = s->dma_timing_master[port]; break;
        case 0x08: val = s->pio_timing_slave[port]; break;
        case 0x0C: val = s->dma_timing_slave[port]; break;
        default: val = 0; break;
        }
        /* Mask for smaller reads */
        if (size == 1) val &= 0xFF;
        else if (size == 2) val &= 0xFFFF;
        else if (size == 3) val &= 0xFFFFFF;
        return val;
    }

    switch (addr) {
    case 0x00:
        val = s->bmdma_primary_cmd;
        break;
    case 0x01: /* Reserved */
        val = 0;
        break;
    case 0x02:
        val = s->bmdma_primary_status;
        break;
    case 0x03:
        val = 0;
        break;
    case 0x04: case 0x05: case 0x06: case 0x07: {
        /* DTPR reads as 32-bit little-endian */
        uint32_t dtpr = s->bmdma_primary_dtpr;
        if (size == 4) {
            val = dtpr;
        } else {
            val = (dtpr >> (8 * (addr - 0x04))) & ((1 << (8 * size)) - 1);
        }
        break;
    }
    default:
        val = 0;
        break;
    }

    return val;
}

static void pcibase_bmdma_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= 0x20 && addr < 0x40) {
        /* Extra timing registers */
        unsigned port = (addr - 0x20) / 0x10;
        unsigned reg = (addr - 0x20) % 0x10;
        if (port >= NUM_PORTS) return;
        uint32_t mask = (size == 4) ? 0xFFFFFFFFul : ((1ul << (8 * size)) - 1);
        switch (reg) {
        case 0x00:
            s->pio_timing_master[port] = (s->pio_timing_master[port] & ~mask) | (val & mask);
            break;
        case 0x04:
            s->dma_timing_master[port] = (s->dma_timing_master[port] & ~mask) | (val & mask);
            break;
        case 0x08:
            s->pio_timing_slave[port] = (s->pio_timing_slave[port] & ~mask) | (val & mask);
            break;
        case 0x0C:
            s->dma_timing_slave[port] = (s->dma_timing_slave[port] & ~mask) | (val & mask);
            break;
        default:
            break;
        }
        return;
    }

    switch (addr) {
    case 0x00: /* Primary Command (1 byte) */
        if (size == 1) s->bmdma_primary_cmd = val & 0xFF;
        break;
    case 0x02: /* Primary Status (1 byte) */
        if (size == 1) s->bmdma_primary_status = val & 0xFF;
        break;
    case 0x04: case 0x05: case 0x06: case 0x07: {
        /* DTPR write */
        uint32_t mask = (size == 4) ? 0xFFFFFFFFul : ((1ul << (8 * size)) - 1);
        uint32_t shift = 8 * (addr - 0x04);
        s->bmdma_primary_dtpr = (s->bmdma_primary_dtpr & ~(mask << shift)) | ((val & mask) << shift);
        break;
    }
    default:
        break;
    }
}

static const MemoryRegionOps pcibase_bmdma_ops = {
    .read = pcibase_bmdma_read,
    .write = pcibase_bmdma_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

/* Unused MMIO handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    s->bmdma_primary_cmd = 0;
    s->bmdma_primary_status = 0;
    s->bmdma_primary_dtpr = 0;
    for (int i = 0; i < NUM_PORTS; i++) {
        s->pio_timing_master[i] = 0;
        s->dma_timing_master[i] = 0;
        s->pio_timing_slave[i] = 0;
        s->dma_timing_slave[i] = 0;
    }
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
        const MemoryRegionOps *ops;
        if (bi->index == 4) {
            ops = &pcibase_bmdma_ops;
        } else {
            ops = &pcibase_ide_ops;
        }
        memory_region_init_io(mr, OBJECT(s), ops, s, bi->name, aligned_size);
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
    pci_config_set_vendor_id(pci_conf, VENDOR_ID);
    pci_config_set_device_id(pci_conf, DEVICE_ID);
    pci_config_set_class(pci_conf, CLASS_ID);
    pci_set_byte(pci_conf + PCI_CLASS_PROG, 0x8a); /* native IDE mode */
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* BAR Initialization */
    s->bar_info[0] = (BARInfo){
        .index = 0,
        .type = BAR_TYPE_PIO,
        .size = 8,
        .name = "ide-pri-cmd",
    };
    s->bar_info[1] = (BARInfo){
        .index = 1,
        .type = BAR_TYPE_PIO,
        .size = 4,
        .name = "ide-pri-ctl",
    };
    s->bar_info[2] = (BARInfo){
        .index = 2,
        .type = BAR_TYPE_PIO,
        .size = 8,
        .name = "ide-sec-cmd",
    };
    s->bar_info[3] = (BARInfo){
        .index = 3,
        .type = BAR_TYPE_PIO,
        .size = 4,
        .name = "ide-sec-ctl",
    };
    s->bar_info[4] = (BARInfo){
        .index = 4,
        .type = BAR_TYPE_PIO,
        .size = 0x100,
        .name = "bmdma",
    };
    s->num_bars = 5;
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
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
    .name = "pata_cs5530_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT8(bmdma_primary_cmd, PCIBaseState),
        VMSTATE_UINT8(bmdma_primary_status, PCIBaseState),
        VMSTATE_UINT32(bmdma_primary_dtpr, PCIBaseState),
        VMSTATE_UINT32_ARRAY(pio_timing_master, PCIBaseState, NUM_PORTS),
        VMSTATE_UINT32_ARRAY(dma_timing_master, PCIBaseState, NUM_PORTS),
        VMSTATE_UINT32_ARRAY(pio_timing_slave, PCIBaseState, NUM_PORTS),
        VMSTATE_UINT32_ARRAY(dma_timing_slave, PCIBaseState, NUM_PORTS),
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
