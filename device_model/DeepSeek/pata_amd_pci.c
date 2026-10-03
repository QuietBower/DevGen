/*
 * QEMU pata_amd device model (Virtual AMD IDE Controller)
 * Based on Linux driver pata_amd.c reverse-engineering.
 * For QEMU 8.2.10.
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

#define PCI_VENDOR_ID_AMD           0x1022
#define PCI_DEVICE_ID_AMD_COBRA_7401 0x7401

#define TYPE_PCIBASE_DEVICE "pata_amd_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

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

    /* Shadow of PCI config space vendor-specific registers (0x40-0x63) */
    uint8_t cfg_shadow[0x64];

    /* BMDMA status register for simplex check */
    uint8_t bmdma_status;
};

static const BARInfo ide_bars[] = {
    /* Standard IDE BMDMA BAR layout */
    { .index = 0, .type = BAR_TYPE_PIO, .size = 8,   .name = "ide-cmd0" },
    { .index = 1, .type = BAR_TYPE_PIO, .size = 4,   .name = "ide-ctl0" },
    { .index = 2, .type = BAR_TYPE_PIO, .size = 8,   .name = "ide-cmd1" },
    { .index = 3, .type = BAR_TYPE_PIO, .size = 4,   .name = "ide-ctl1" },
    { .index = 4, .type = BAR_TYPE_PIO, .size = 16,  .name = "bmdma" },
};

/* Dummy PIO handlers for BAR regions: return 0 on read, ignore writes. */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* No-op */
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = NULL,
    .write = NULL,
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

/* BMDMA PIO handlers for BAR4 (simplex check support) */
static uint64_t bmdma_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = PCIBASE_DEVICE(opaque);
    if (addr == 2) {
        return s->bmdma_status;
    }
    return 0;
}

static void bmdma_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = PCIBASE_DEVICE(opaque);
    if (addr == 2) {
        s->bmdma_status = (uint8_t)val;
    }
    /* Ignore other writes */
}

static const MemoryRegionOps bmdma_pio_ops = {
    .read = bmdma_pio_read,
    .write = bmdma_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* PCI config read/write overrides for custom registers at 0x40-0x63 */
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    /* Intercept vendor-specific range */
    if (addr >= 0x40 && addr <= 0x63 && addr + len - 1 <= 0x63) {
        uint32_t val = 0;
        for (int i = 0; i < len; i++) {
            val |= s->cfg_shadow[addr + i] << (8 * i);
        }
        return val;
    }
    return pci_default_read_config(pdev, addr, len);
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    /* Intercept vendor-specific range */
    if (addr >= 0x40 && addr <= 0x63 && addr + len - 1 <= 0x63) {
        for (int i = 0; i < len; i++) {
            s->cfg_shadow[addr + i] = (val >> (8 * i)) & 0xff;
        }
        return;
    }
    pci_default_write_config(pdev, addr, val, len);
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);

    /* Initialize device-specific state only; do not call pci_device_reset
     * to avoid clearing BAR addresses assigned by QEMU's PCI core. */
    memset(s->cfg_shadow, 0, sizeof(s->cfg_shadow));
    s->cfg_shadow[0x40] = 0x03;  /* Both channels enabled */
    s->cfg_shadow[0x42] = 0x0F;  /* 80-wire cable detected for both */
    s->cfg_shadow[0x50] = 0x03;  /* Both ports enabled */
    s->cfg_shadow[0x60] = 0xC7;  /* UDMA6 for P1D0 */
    s->cfg_shadow[0x61] = 0xC7;  /* UDMA6 for P1D1 */
    s->cfg_shadow[0x62] = 0xC7;  /* UDMA6 for P0D0 */
    s->cfg_shadow[0x63] = 0xC7;  /* UDMA6 for P0D1 */
    s->bmdma_status = 0;
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
        const MemoryRegionOps *ops = (bi->index == 4) ? &bmdma_pio_ops : &pcibase_pio_ops;
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

    /* Set full PCI class code (class: Mass Storage, sub: IDE, prog-if: 0x8A native IDE) */
    pci_config_set_class(pci_conf, 0x018A01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Initialize standard IDE BARs */
    s->num_bars = ARRAY_SIZE(ide_bars);
    for (int i = 0; i < s->num_bars; i++) {
        s->bar_info[i] = ide_bars[i];
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize config shadow with default values */
    memset(s->cfg_shadow, 0, sizeof(s->cfg_shadow));
    s->cfg_shadow[0x40] = 0x03;
    s->cfg_shadow[0x42] = 0x0F;
    s->cfg_shadow[0x50] = 0x03;
    s->cfg_shadow[0x60] = 0xC7;
    s->cfg_shadow[0x61] = 0xC7;
    s->cfg_shadow[0x62] = 0xC7;
    s->cfg_shadow[0x63] = 0xC7;
    s->bmdma_status = 0;
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
    .name = "pata_amd_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_BUFFER(cfg_shadow, PCIBaseState),
        VMSTATE_UINT8(bmdma_status, PCIBaseState),
        VMSTATE_END_OF_LIST()
    }
};

static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->vendor_id = PCI_VENDOR_ID_AMD;
    k->device_id = PCI_DEVICE_ID_AMD_COBRA_7401;
    k->revision = 0x01;
    k->class_id = PCI_CLASS_STORAGE_IDE;
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
