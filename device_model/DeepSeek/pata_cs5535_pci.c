/*
 * QEMU emulation of CS5535 IDE controller for pata_cs5535 driver.
 * Based on provided driver source, implements necessary PCI, BARs, and BMDMA.
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

#define TYPE_PCIBASE_DEVICE "pata_cs5535_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register offset from driver */
#define CS5535_CABLE_DETECT 0x48

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

    /* Shadow registers */
    uint8_t bmdma_regs[16];
    uint8_t ide_cmd_regs0[8];   /* BAR0 primary command block */
    uint8_t ide_ctl_regs0[4];   /* BAR1 primary control block */
    uint8_t ide_cmd_regs1[8];   /* BAR2 secondary command block */
    uint8_t ide_ctl_regs1[4];   /* BAR3 secondary control block */
};

/*****************************************************************************/
/* MMIO read/write handler for IDE task file registers (dummy)               */
/*****************************************************************************/
static uint64_t ide_taskfile_read(void *opaque, hwaddr addr, unsigned size)
{
    /* Return 0 for dummy registers */
    return 0;
}

static void ide_taskfile_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* ignore writes */
}

static const MemoryRegionOps ide_taskfile_ops = {
    .read = ide_taskfile_read,
    .write = ide_taskfile_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/*****************************************************************************/
/* BMDMA MMIO handler                                                        */
/*****************************************************************************/
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr < 16) {
        switch (size) {
        case 1:
            val = s->bmdma_regs[addr];
            break;
        case 2:
            val = lduw_le_p(&s->bmdma_regs[addr]);
            break;
        case 4:
            val = ldl_le_p(&s->bmdma_regs[addr]);
            break;
        default:
            break;
        }
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < 16) {
        switch (size) {
        case 1:
            s->bmdma_regs[addr] = (uint8_t)val;
            break;
        case 2:
            stw_le_p(&s->bmdma_regs[addr], (uint16_t)val);
            break;
        case 4:
            stl_le_p(&s->bmdma_regs[addr], (uint32_t)val);
            break;
        default:
            break;
        }
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    memset(s->bmdma_regs, 0, sizeof(s->bmdma_regs));
    memset(s->ide_cmd_regs0, 0, sizeof(s->ide_cmd_regs0));
    memset(s->ide_ctl_regs0, 0, sizeof(s->ide_ctl_regs0));
    memset(s->ide_cmd_regs1, 0, sizeof(s->ide_cmd_regs1));
    memset(s->ide_ctl_regs1, 0, sizeof(s->ide_ctl_regs1));
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];
    const MemoryRegionOps *ops;

    if (bi->type == BAR_TYPE_MMIO) {
        ops = (bi->name[0] == 'i') ? &ide_taskfile_ops : &pcibase_mmio_ops;
        memory_region_init_io(mr, OBJECT(s), ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        /* Choose ops based on BAR index: 0-3 for IDE task file, 4 for BMDMA */
        if (bi->index >= 0 && bi->index <= 3) {
            ops = &ide_taskfile_ops;
        } else {
            ops = &pcibase_mmio_ops;
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

    pci_set_word(pci_conf + PCI_VENDOR_ID, 0x100b);
    pci_set_word(pci_conf + PCI_DEVICE_ID, 0x002d);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_STORAGE_IDE);
    pci_set_byte(pci_conf + PCI_CLASS_PROG, 0x8a); /* Native mode, both channels */
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Cable detect config register: set to indicate 80-pin cable */
    pci_conf[CS5535_CABLE_DETECT] = 0x01;

    /* Initialize BARs */
    s->num_bars = 5;
    for (int i = 0; i < s->num_bars; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
    }

    /* Set up BAR0-3 for IDE command/control blocks (I/O) */
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 8;
    s->bar_info[0].name = "ide-cmd0";
    s->bar_info[1].type = BAR_TYPE_PIO;
    s->bar_info[1].size = 4;
    s->bar_info[1].name = "ide-ctl0";
    s->bar_info[2].type = BAR_TYPE_PIO;
    s->bar_info[2].size = 8;
    s->bar_info[2].name = "ide-cmd1";
    s->bar_info[3].type = BAR_TYPE_PIO;
    s->bar_info[3].size = 4;
    s->bar_info[3].name = "ide-ctl1";

    /* Set up BAR4: BMDMA registers (16 bytes, I/O) */
    s->bar_info[4].type = BAR_TYPE_PIO;
    s->bar_info[4].size = 16;
    s->bar_info[4].name = "bmdma";

    /* Register all BARs */
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    /* nothing to free */
}

static const VMStateDescription vmstate_pcibase = {
    .name = "pata_cs5535_pci",
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
