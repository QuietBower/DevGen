/*
 * QEMU PCI device model for ata_piix (PIIX3 IDE controller)
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

#define TYPE_PCIBASE_DEVICE "ata_piix_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI_VENDOR_ID_INTEL 0x8086
#define PCI_DEVICE_ID_PIIX3_IDE 0x7010

/* Per‑channel IDE registers */
struct PCIIDEChannel {
    uint8_t cmd_regs[8];
    uint8_t ctl_regs[4];
};

/* BMDMA registers for a channel */
struct BMDMAChannel {
    uint8_t cmd;
    uint8_t status;
    uint32_t prdt;
};

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];
    int num_bars;

    bool has_msi;
    bool has_msix;

    /* Legacy IDE state */
    struct PCIIDEChannel ide_channel[2];
    struct BMDMAChannel bmdma[2];

    /* Unused AHCI shadow (kept for structural compatibility) */
    uint32_t ahci_global_ctl;
};

/******************************************************************************
 * MMIO / PIO handlers
 ******************************************************************************/

/* Primary command block (BAR0) */
static uint64_t piix_primary_cmd_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    if (addr < 8) {
        val = s->ide_channel[0].cmd_regs[addr];
    }
    return val;
}

static void piix_primary_cmd_write(void *opaque, hwaddr addr, uint64_t val,
                                     unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr < 8) {
        s->ide_channel[0].cmd_regs[addr] = (uint8_t)val;
    }
}

static const MemoryRegionOps piix_primary_cmd_ops = {
    .read = piix_primary_cmd_read,
    .write = piix_primary_cmd_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* Primary control block (BAR1) */
static uint64_t piix_primary_ctl_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    if (addr == 2) {
        /* Alt status */
        val = s->ide_channel[0].cmd_regs[7];
    }
    return val;
}

static void piix_primary_ctl_write(void *opaque, hwaddr addr, uint64_t val,
                                     unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr == 2) {
        s->ide_channel[0].ctl_regs[2] = (uint8_t)val;
    }
}

static const MemoryRegionOps piix_primary_ctl_ops = {
    .read = piix_primary_ctl_read,
    .write = piix_primary_ctl_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* Secondary command block (BAR2) */
static uint64_t piix_secondary_cmd_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    if (addr < 8) {
        val = s->ide_channel[1].cmd_regs[addr];
    }
    return val;
}

static void piix_secondary_cmd_write(void *opaque, hwaddr addr, uint64_t val,
                                       unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr < 8) {
        s->ide_channel[1].cmd_regs[addr] = (uint8_t)val;
    }
}

static const MemoryRegionOps piix_secondary_cmd_ops = {
    .read = piix_secondary_cmd_read,
    .write = piix_secondary_cmd_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* Secondary control block (BAR3) */
static uint64_t piix_secondary_ctl_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    if (addr == 2) {
        val = s->ide_channel[1].cmd_regs[7];
    }
    return val;
}

static void piix_secondary_ctl_write(void *opaque, hwaddr addr, uint64_t val,
                                       unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr == 2) {
        s->ide_channel[1].ctl_regs[2] = (uint8_t)val;
    }
}

static const MemoryRegionOps piix_secondary_ctl_ops = {
    .read = piix_secondary_ctl_read,
    .write = piix_secondary_ctl_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* BMDMA (BAR4) – one BAR contains registers for both channels */
static uint64_t piix_bmdma_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    uint8_t *p;
    if (addr < 8) {
        p = (uint8_t *)&s->bmdma[0];
        val = p[addr];
    } else if (addr >= 8 && addr < 16) {
        p = (uint8_t *)&s->bmdma[1];
        val = p[addr - 8];
    }
    return val;
}

static void piix_bmdma_write(void *opaque, hwaddr addr, uint64_t val,
                              unsigned size)
{
    PCIBaseState *s = opaque;
    uint8_t *p;
    if (addr < 8) {
        p = (uint8_t *)&s->bmdma[0];
        p[addr] = (uint8_t)val;
    } else if (addr >= 8 && addr < 16) {
        p = (uint8_t *)&s->bmdma[1];
        p[addr - 8] = (uint8_t)val;
    }
}

static const MemoryRegionOps piix_bmdma_ops = {
    .read = piix_bmdma_read,
    .write = piix_bmdma_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/******************************************************************************
 * Reset
 ******************************************************************************/
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Clear IDE registers */
    memset(&s->ide_channel, 0, sizeof(s->ide_channel));
    memset(&s->bmdma, 0, sizeof(s->bmdma));
}

/******************************************************************************
 * Realize
 ******************************************************************************/
static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_INTEL);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_PIIX3_IDE);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_STORAGE_IDE);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_set_byte(pci_conf + PCI_CLASS_PROG, 0x8a); /* Native mode, both channels */
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* BAR Initialization */
    /* Primary command block (I/O, 8 bytes) */
    memory_region_init_io(&s->bar_regions[0], OBJECT(s), &piix_primary_cmd_ops,
                          s, "piix-primary-cmd", 8);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_IO, &s->bar_regions[0]);

    /* Primary control block (I/O, 4 bytes) */
    memory_region_init_io(&s->bar_regions[1], OBJECT(s), &piix_primary_ctl_ops,
                          s, "piix-primary-ctl", 4);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_IO, &s->bar_regions[1]);

    /* Secondary command block (I/O, 8 bytes) */
    memory_region_init_io(&s->bar_regions[2], OBJECT(s),
                          &piix_secondary_cmd_ops, s, "piix-secondary-cmd", 8);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_IO, &s->bar_regions[2]);

    /* Secondary control block (I/O, 4 bytes) */
    memory_region_init_io(&s->bar_regions[3], OBJECT(s),
                          &piix_secondary_ctl_ops, s, "piix-secondary-ctl", 4);
    pci_register_bar(pdev, 3, PCI_BASE_ADDRESS_SPACE_IO, &s->bar_regions[3]);

    /* Bus master DMA (I/O, 16 bytes) */
    memory_region_init_io(&s->bar_regions[4], OBJECT(s), &piix_bmdma_ops,
                          s, "piix-bmdma", 16);
    pci_register_bar(pdev, 4, PCI_BASE_ADDRESS_SPACE_IO, &s->bar_regions[4]);
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

/* Minimal VMState */
static const VMStateDescription vmstate_pcibase = {
    .name = "ata_piix_pci",
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
