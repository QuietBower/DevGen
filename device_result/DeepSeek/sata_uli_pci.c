/*
 * QEMU PCI device model for ULI SATA controller (sata_uli)
 * Derived from Linux driver sata_uli.c
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
#include "hw/qdev-properties.h"
#include "hw/qdev-core.h"
#include "migration/vmstate.h"

#define TYPE_PCIBASE_DEVICE "sata_uli_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI Identification: first entry from pci_device_id table */
#define ULI_VENDOR_ID   0x10b9
#define ULI_DEVICE_ID   0x5289
#define ULI_CLASS_ID    0x0106

/* SCR config space offsets (from kernel sata_uli.c) */
#define ULI5287_BASE  0x80
#define ULI5287_OFFS  0x20

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
    uint32_t intr_status;
    uint32_t intr_mask;

    /* SCR registers for two ports */
    uint32_t scr[2][3];
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t pending = s->intr_status & ~s->intr_mask;
    if (pending) {
        if (msix_enabled(pdev)) {
            msix_notify(pdev, 0);
        } else if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!msix_enabled(pdev) && !msi_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

static void pcibase_scr_update_irq(PCIBaseState *s)
{
    /* SCR writes may affect interrupts; for now no interrupt generation */
}

static uint32_t pcibase_pio_read_ide(PCIBaseState *s, hwaddr addr, unsigned size, int bar_index)
{
    /* Return default values to allow driver probing */
    switch (addr & 7) {
    case 7: /* status register */
        return 0x50; /* DRDY set, no error */
    default:
        return 0;
    }
}

static void pcibase_pio_write_ide(PCIBaseState *s, hwaddr addr, uint64_t val, unsigned size, int bar_index)
{
    /* Ignore writes */
}

static uint32_t pcibase_pio_read_bmdma(PCIBaseState *s, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_pio_write_bmdma(PCIBaseState *s, hwaddr addr, uint64_t val, unsigned size)
{
    /* Ignore writes */
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Determine which BAR based on address ranges; we have multiple PIO regions.
       The PIO ops are registered per BAR, so addr is relative to BAR base.
       We use separate ops for each BAR to simplify; this function is not used.
       Instead, we create dedicated ops per BAR. However, the template has
       a single pcibase_pio_ops. We'll modify realize to create per-BAR ops.
       To keep it simple, we'll use a single handler and differentiate by
       calling context. But QEMU doesn't provide BAR index in the opaque.
       So we'll use separate MemoryRegionOps for each BAR. We'll define
       them in an array. The template's pcibase_pio_ops is unused now.
       So we'll delete the pcibase_pio_read/write functions and replace with
       per-BAR read/write functions below. */
    return 0;
}

/* Per-BAR PIO read/write handlers */
static uint64_t pcibase_pio_read_bar0(void *opaque, hwaddr addr, unsigned size)
{
    return pcibase_pio_read_ide(opaque, addr, size, 0);
}

static void pcibase_pio_write_bar0(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    pcibase_pio_write_ide(opaque, addr, val, size, 0);
}

static uint64_t pcibase_pio_read_bar1(void *opaque, hwaddr addr, unsigned size)
{
    return pcibase_pio_read_ide(opaque, addr, size, 1);
}

static void pcibase_pio_write_bar1(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    pcibase_pio_write_ide(opaque, addr, val, size, 1);
}

static uint64_t pcibase_pio_read_bar2(void *opaque, hwaddr addr, unsigned size)
{
    return pcibase_pio_read_ide(opaque, addr, size, 2);
}

static void pcibase_pio_write_bar2(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    pcibase_pio_write_ide(opaque, addr, val, size, 2);
}

static uint64_t pcibase_pio_read_bar3(void *opaque, hwaddr addr, unsigned size)
{
    return pcibase_pio_read_ide(opaque, addr, size, 3);
}

static void pcibase_pio_write_bar3(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    pcibase_pio_write_ide(opaque, addr, val, size, 3);
}

static uint64_t pcibase_pio_read_bar4(void *opaque, hwaddr addr, unsigned size)
{
    return pcibase_pio_read_bmdma(opaque, addr, size);
}

static void pcibase_pio_write_bar4(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    pcibase_pio_write_bmdma(opaque, addr, val, size);
}

/* Per-BAR memory region ops */
static const MemoryRegionOps pcibase_pio_ops[5] = {
    { .read = pcibase_pio_read_bar0, .write = pcibase_pio_write_bar0, .endianness = DEVICE_LITTLE_ENDIAN, .valid = { .min_access_size = 1, .max_access_size = 4 }, .impl = { .min_access_size = 1, .max_access_size = 4 } },
    { .read = pcibase_pio_read_bar1, .write = pcibase_pio_write_bar1, .endianness = DEVICE_LITTLE_ENDIAN, .valid = { .min_access_size = 1, .max_access_size = 4 }, .impl = { .min_access_size = 1, .max_access_size = 4 } },
    { .read = pcibase_pio_read_bar2, .write = pcibase_pio_write_bar2, .endianness = DEVICE_LITTLE_ENDIAN, .valid = { .min_access_size = 1, .max_access_size = 4 }, .impl = { .min_access_size = 1, .max_access_size = 4 } },
    { .read = pcibase_pio_read_bar3, .write = pcibase_pio_write_bar3, .endianness = DEVICE_LITTLE_ENDIAN, .valid = { .min_access_size = 1, .max_access_size = 4 }, .impl = { .min_access_size = 1, .max_access_size = 4 } },
    { .read = pcibase_pio_read_bar4, .write = pcibase_pio_write_bar4, .endianness = DEVICE_LITTLE_ENDIAN, .valid = { .min_access_size = 1, .max_access_size = 4 }, .impl = { .min_access_size = 1, .max_access_size = 4 } },
};

static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint32_t val;

    /* Check if the address falls within SCR space */
    if (addr >= ULI5287_BASE && addr < ULI5287_BASE + ULI5287_OFFS * 2) {
        int port = (addr - ULI5287_BASE) / ULI5287_OFFS;
        int reg = ((addr - ULI5287_BASE) % ULI5287_OFFS) / 4;
        if (port < 2 && reg < 3) {
            val = s->scr[port][reg];
            qemu_log_mask(LOG_GUEST_ERROR, "%s: SCR read port %d reg %d = 0x%x\n", __func__, port, reg, val);
            return val;
        }
    }

    /* Fallback to default config read */
    return pci_default_read_config(pdev, addr, len);
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    /* Check SCR space */
    if (addr >= ULI5287_BASE && addr < ULI5287_BASE + ULI5287_OFFS * 2) {
        int port = (addr - ULI5287_BASE) / ULI5287_OFFS;
        int reg = ((addr - ULI5287_BASE) % ULI5287_OFFS) / 4;
        if (port < 2 && reg < 3) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: SCR write port %d reg %d = 0x%x\n", __func__, port, reg, val);
            s->scr[port][reg] = val;
            pcibase_scr_update_irq(s);
            return;
        }
    }

    /* Default config write */
    pci_default_write_config(pdev, addr, val, len);
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    memset(s->scr, 0, sizeof(s->scr));
    s->intr_status = 0;
    s->intr_mask = 0;
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }
    
    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops[bi->index], s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
    /* No MMIO handling needed */
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  ULI_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  ULI_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, ULI_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Define legacy IDE BARs */
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_PIO, .size = 8, .name = "primary-cmd" };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_PIO, .size = 4, .name = "primary-ctl" };
    s->bar_info[2] = (BARInfo){ .index = 2, .type = BAR_TYPE_PIO, .size = 8, .name = "secondary-cmd" };
    s->bar_info[3] = (BARInfo){ .index = 3, .type = BAR_TYPE_PIO, .size = 4, .name = "secondary-ctl" };
    s->bar_info[4] = (BARInfo){ .index = 4, .type = BAR_TYPE_PIO, .size = 16, .name = "bmdma" };
    s->num_bars = 5;
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Override config read/write for SCR access */
    pdev->config_read = pcibase_config_read;
    pdev->config_write = pcibase_config_write;
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
    .name = "sata_uli_pci",
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
