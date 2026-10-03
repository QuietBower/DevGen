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

#define TYPE_PCIBASE_DEVICE "pata_cypress_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI Identification */
#define PCI_VENDOR_ID_CONTAQ          0x1080
#define PCI_DEVICE_ID_CONTAQ_82C693   0xc693
#define PCI_CLASS_STORAGE_IDE         0x0101

/* Driver-specific register offsets (ide-cmd) */
#define IDE_CMD_DATA       0
#define IDE_CMD_ERROR      1
#define IDE_CMD_NSECTOR    2
#define IDE_CMD_LBA_LOW    3
#define IDE_CMD_LBA_MID    4
#define IDE_CMD_LBA_HIGH   5
#define IDE_CMD_DEVICE     6
#define IDE_CMD_STATUS     7

/* IDE control block offsets (ide-ctrl) */
#define IDE_CTRL_ALTSTATUS 2

/* BMDMA offsets */
#define BMDMA_COMMAND      0
#define BMDMA_STATUS       2
#define BMDMA_PRDT         4

/* Cypress index/data offsets (relative to BAR5 base) */
#define CYPRESS_INDEX      2
#define CYPRESS_DATA       3

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

    /* Hardware Register Shadows */
    uint8_t ide_cmd_regs[8];
    uint8_t ide_ctrl;
    uint8_t bm_command;
    uint8_t bm_status;
    uint32_t bm_prdt;
    uint8_t cypress_index;
    uint8_t cypress_data;

    /* Interrupt state */
    bool ide_irq_pending;
    bool bm_irq_pending;
};

G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->ide_irq_pending || s->bm_irq_pending) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /* No DMA placeholder: driver does not explicitly define DMA beyond BMDMA registers */
}

/* ------------------- IDE Command Block (BAR0) handlers ------------------- */
static uint64_t ide_cmd_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    if (addr < 8) {
        val = s->ide_cmd_regs[addr];
    }
    return val;
}

static void ide_cmd_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr < 8) {
        s->ide_cmd_regs[addr] = val & 0xFF;
    }
    /* On command write (addr 7) we could simulate command execution; keep simple */
}

static const MemoryRegionOps ide_cmd_ops = {
    .read = ide_cmd_read,
    .write = ide_cmd_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl = { .min_access_size = 1, .max_access_size = 4 },
};

/* ------------------- IDE Control Block (BAR1) handlers ------------------- */
static uint64_t ide_ctrl_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    if (addr == IDE_CTRL_ALTSTATUS) {
        val = 0x50; /* drive ready, no error */
    }
    return val;
}

static void ide_ctrl_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr == IDE_CTRL_ALTSTATUS) {
        s->ide_ctrl = val & 0xFF;
        /* If SRST (bit 2) is set, reset IDE task files */
        if (val & 0x04) {
            memset(s->ide_cmd_regs, 0, 8);
            s->ide_cmd_regs[IDE_CMD_STATUS] = 0x50;
        }
    }
}

static const MemoryRegionOps ide_ctrl_ops = {
    .read = ide_ctrl_read,
    .write = ide_ctrl_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 1 },
    .impl = { .min_access_size = 1, .max_access_size = 1 },
};

/* ------------------- BMDMA (BAR4) handlers ------------------- */
static uint64_t bmdma_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    switch (addr) {
    case BMDMA_COMMAND:
        val = s->bm_command;
        break;
    case BMDMA_STATUS:
        val = s->bm_status;
        break;
    case BMDMA_PRDT + 0:
    case BMDMA_PRDT + 1:
    case BMDMA_PRDT + 2:
    case BMDMA_PRDT + 3:
        val = s->bm_prdt;
        break;
    default:
        break;
    }
    return val;
}

static void bmdma_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    switch (addr) {
    case BMDMA_COMMAND:
        s->bm_command = val & 0xFF;
        if (val & 0x01) { /* start transfer */
            s->bm_status |= 0x01; /* active */
            if (s->bm_command & 0x02) { /* interrupt enable */
                s->bm_status |= 0x02; /* set interrupt */
                s->bm_irq_pending = true;
                pcibase_update_irq(s);
            }
        }
        break;
    case BMDMA_STATUS:
        /* Write-1-to-clear on bits 1 and 2 */
        if (val & 0x02) {
            s->bm_status &= ~0x02;
            s->bm_irq_pending = false;
            pcibase_update_irq(s);
        }
        if (val & 0x04) {
            s->bm_status &= ~0x04;
        }
        break;
    case BMDMA_PRDT + 0:
    case BMDMA_PRDT + 1:
    case BMDMA_PRDT + 2:
    case BMDMA_PRDT + 3:
        s->bm_prdt = val & 0xFFFFFFFF;
        break;
    default:
        break;
    }
}

static const MemoryRegionOps bmdma_ops = {
    .read = bmdma_read,
    .write = bmdma_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl = { .min_access_size = 1, .max_access_size = 4 },
};

/* ------------------- Cypress (BAR5) handlers ------------------- */
static uint64_t cypress_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    if (addr == CYPRESS_INDEX) {
        val = s->cypress_index;
    } else if (addr == CYPRESS_DATA) {
        val = s->cypress_data;
    }
    return val;
}

static void cypress_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr == CYPRESS_INDEX) {
        s->cypress_index = val & 0xFF;
    } else if (addr == CYPRESS_DATA) {
        s->cypress_data = val & 0xFF;
    }
}

static const MemoryRegionOps cypress_ops = {
    .read = cypress_read,
    .write = cypress_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 1 },
    .impl = { .min_access_size = 1, .max_access_size = 1 },
};

/* ------------------- Unused MMIO/PIO fallback (kept for template completeness) ------------------- */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

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

/* ------------------- Device reset ------------------- */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    memset(s->ide_cmd_regs, 0, sizeof(s->ide_cmd_regs));
    s->ide_cmd_regs[IDE_CMD_STATUS] = 0x50; /* drive ready */
    s->ide_ctrl = 0;
    s->bm_command = 0;
    s->bm_status = 0;
    s->bm_prdt = 0;
    s->cypress_index = 0;
    s->cypress_data = 0;
    s->ide_irq_pending = false;
    s->bm_irq_pending = false;
}

/* ------------------- PCI realization ------------------- */
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_CONTAQ);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_CONTAQ_82C693);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_STORAGE_IDE);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* No generic BARs using the default helper */
    s->num_bars = 0;
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Custom BAR registration */
    MemoryRegion *mr;

    /* BAR0: Primary IDE command block (8 bytes) */
    mr = &s->bar_regions[0];
    memory_region_init_io(mr, OBJECT(s), &ide_cmd_ops, s, "ide-cmd", 8);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_IO, mr);
    /* Fallback fixed I/O address if unassigned (e.g., on PCIe without I/O space) */
    if (pci_get_long(pci_conf + PCI_BASE_ADDRESS_0) == 0) {
        pci_set_long(pci_conf + PCI_BASE_ADDRESS_0, 0x1F1);  /* 0x1F0 | 0x01 I/O flag */
        memory_region_add_subregion(get_system_io(), 0x1F0, mr);
    }

    /* BAR1: Primary IDE control block (4 bytes) */
    mr = &s->bar_regions[1];
    memory_region_init_io(mr, OBJECT(s), &ide_ctrl_ops, s, "ide-ctrl", 4);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_IO, mr);
    if (pci_get_long(pci_conf + PCI_BASE_ADDRESS_1) == 0) {
        pci_set_long(pci_conf + PCI_BASE_ADDRESS_1, 0x3F5);  /* 0x3F4 | 0x01 */
        memory_region_add_subregion(get_system_io(), 0x3F4, mr);
    }

    /* BAR4: Bus Master DMA (16 bytes) */
    mr = &s->bar_regions[4];
    memory_region_init_io(mr, OBJECT(s), &bmdma_ops, s, "bmdma", 16);
    pci_register_bar(pdev, 4, PCI_BASE_ADDRESS_SPACE_IO, mr);
    if (pci_get_long(pci_conf + PCI_BASE_ADDRESS_4) == 0) {
        pci_set_long(pci_conf + PCI_BASE_ADDRESS_4, 0xFFA1); /* 0xFFA0 | 0x01 */
        memory_region_add_subregion(get_system_io(), 0xFFA0, mr);
    }

    /* BAR5: Cypress index/data (4 bytes) */
    mr = &s->bar_regions[5];
    memory_region_init_io(mr, OBJECT(s), &cypress_ops, s, "cypress", 4);
    pci_register_bar(pdev, 5, PCI_BASE_ADDRESS_SPACE_IO, mr);
    if (pci_get_long(pci_conf + PCI_BASE_ADDRESS_5) == 0) {
        pci_set_long(pci_conf + PCI_BASE_ADDRESS_5, 0xFFA5); /* 0xFFA4 | 0x01 */
        memory_region_add_subregion(get_system_io(), 0xFFA4, mr);
    }

    /* NOTE: The Linux driver (pata_cypress) expects the device to be at PCI function 1.
     * Ensure the guest command line places this device at function 1 (e.g., addr=01.1). */
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
    .name = "pata_cypress_pci",
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
