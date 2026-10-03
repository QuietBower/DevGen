/* Virtual VIA PATA IDE controller for QEMU 8.2.10, matching pata_via.c */
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

#define TYPE_PCIBASE_DEVICE "pata_via_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_VIA      0x1106
#define PCI_DEVICE_ID_VIA_0415 0x0415
#define PCI_CLASS_IDE          0x0101

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

/* BMDMA state extracted from standard IDE controller */
typedef struct BMState {
    uint8_t  command;
    uint8_t  status;
    uint32_t prdt_addr;
    uint8_t  prdt_buf[4096];
} BMState;

/* Task file register shadows for each IDE channel */
typedef struct IDEChannel {
    uint8_t cmd[8];          /* command block registers 0-7 */
    uint8_t ctl;             /* control/alt status register */
} IDEChannel;

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    IDEChannel primary;
    IDEChannel secondary;

    /* DMA Context */
    BMState bmstate;
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->bmstate.status & 0x04) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* ---- BAR0: Primary Command Block ---- */
static uint64_t pcibase_bar0_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    if (addr + size > 8) return ~0ULL;
    if (size == 1) {
        val = s->primary.cmd[addr];
    } else if (size == 2 && addr == 0) {
        val = s->primary.cmd[0] | (s->primary.cmd[1] << 8);
    }
    return val;
}

static void pcibase_bar0_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr + size > 8) return;
    if (size == 1) {
        s->primary.cmd[addr] = val;
    } else if (size == 2 && addr == 0) {
        s->primary.cmd[0] = val & 0xFF;
        s->primary.cmd[1] = (val >> 8) & 0xFF;
    }
    if (addr == 7 && size == 1) {
        /* Writing command register triggers a command; ignore for now */
    }
}

static const MemoryRegionOps pcibase_bar0_ops = {
    .read = pcibase_bar0_read,
    .write = pcibase_bar0_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 2 },
    .impl  = { .min_access_size = 1, .max_access_size = 2 },
};

/* ---- BAR1: Primary Control Block ---- */
static uint64_t pcibase_bar1_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    return s->primary.ctl;
}

static void pcibase_bar1_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    s->primary.ctl = val & 0xFF;
    /* nIEN bit (bit1) controls interrupt enable; not fully emulated */
}

static const MemoryRegionOps pcibase_bar1_ops = {
    .read = pcibase_bar1_read,
    .write = pcibase_bar1_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 1 },
    .impl  = { .min_access_size = 1, .max_access_size = 1 },
};

/* ---- BAR2: Secondary Command Block ---- */
static uint64_t pcibase_bar2_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    if (addr + size > 8) return ~0ULL;
    if (size == 1) {
        val = s->secondary.cmd[addr];
    } else if (size == 2 && addr == 0) {
        val = s->secondary.cmd[0] | (s->secondary.cmd[1] << 8);
    }
    return val;
}

static void pcibase_bar2_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr + size > 8) return;
    if (size == 1) {
        s->secondary.cmd[addr] = val;
    } else if (size == 2 && addr == 0) {
        s->secondary.cmd[0] = val & 0xFF;
        s->secondary.cmd[1] = (val >> 8) & 0xFF;
    }
}

static const MemoryRegionOps pcibase_bar2_ops = {
    .read = pcibase_bar2_read,
    .write = pcibase_bar2_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 2 },
    .impl  = { .min_access_size = 1, .max_access_size = 2 },
};

/* ---- BAR3: Secondary Control Block ---- */
static uint64_t pcibase_bar3_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    return s->secondary.ctl;
}

static void pcibase_bar3_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    s->secondary.ctl = val & 0xFF;
}

static const MemoryRegionOps pcibase_bar3_ops = {
    .read = pcibase_bar3_read,
    .write = pcibase_bar3_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 1 },
    .impl  = { .min_access_size = 1, .max_access_size = 1 },
};

/* ---- BAR4: Bus Master DMA ---- */
static uint64_t pcibase_bar4_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    switch (addr) {
    case 0:
        if (size == 1) val = s->bmstate.command;
        break;
    case 2:
        if (size == 1) val = s->bmstate.status;
        break;
    case 4:
        if (size == 4) val = s->bmstate.prdt_addr;
        break;
    default:
        val = 0;
    }
    return val;
}

static void pcibase_bar4_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    switch (addr) {
    case 0:
        if (size == 1) {
            s->bmstate.command = val;
            if (val & 0x1) { /* start bit */
                /* Simulate a DMA transfer: just set interrupt */
                s->bmstate.status |= 0x04; /* set interrupt bit */
                s->bmstate.command &= ~0x1;
                pcibase_update_irq(s);
            }
            if (val & 0x2) { /* read/write not emulated */ }
        }
        break;
    case 2:
        if (size == 1) {
            /* Write-1-to-clear bits: write '1' to clear interrupt(bit2) and error(bit1) */
            if (val & 0x04) s->bmstate.status &= ~0x04;
            if (val & 0x02) s->bmstate.status &= ~0x02;
            pcibase_update_irq(s);
        }
        break;
    case 4:
        if (size == 4) s->bmstate.prdt_addr = val;
        break;
    }
}

static const MemoryRegionOps pcibase_bar4_ops = {
    .read = pcibase_bar4_read,
    .write = pcibase_bar4_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Initialize IDE task file registers */
    memset(&s->primary, 0, sizeof(s->primary));
    memset(&s->secondary, 0, sizeof(s->secondary));
    /* Status register (offset 7): indicate ready and seek complete */
    s->primary.cmd[7] = 0x50;
    s->secondary.cmd[7] = 0x50;
    /* Alt status same */
    s->primary.ctl = 0x50;
    s->secondary.ctl = 0x50;

    /* BMDMA state */
    s->bmstate.command = 0;
    s->bmstate.status = 0x60; /* drives 0 and 1 DMA capable */
    s->bmstate.prdt_addr = 0;

    pcibase_update_irq(s);
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_bar0_ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        const MemoryRegionOps *ops = NULL;
        switch (bi->index) {
        case 0: ops = &pcibase_bar0_ops; break;
        case 1: ops = &pcibase_bar1_ops; break;
        case 2: ops = &pcibase_bar2_ops; break;
        case 3: ops = &pcibase_bar3_ops; break;
        case 4: ops = &pcibase_bar4_ops; break;
        default: g_assert_not_reached();
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_VIA );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_VIA_0415 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_IDE );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    /* Set programming interface to native mode (both channels) */
    pci_set_byte(pci_conf + 0x09, 0x05); /* Native mode primary and secondary */
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Power management capability (standard PCI) */
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* Custom VIA IDE configuration bytes */
    pci_set_byte(pci_conf + 0x40, 0x03);  /* both channels enabled */
    pci_set_byte(pci_conf + 0x43, 0x00);  /* FIFO control */
    pci_set_byte(pci_conf + 0x48, 0x00);  /* PIO timing override */
    pci_set_byte(pci_conf + 0x49, 0x00);
    pci_set_byte(pci_conf + 0x4A, 0x00);
    pci_set_byte(pci_conf + 0x4B, 0x00);
    pci_set_byte(pci_conf + 0x4C, 0x00);  /* address setup */
    pci_set_byte(pci_conf + 0x4D, 0x00);
    pci_set_byte(pci_conf + 0x4E, 0x00);
    pci_set_byte(pci_conf + 0x4F, 0x00);  /* 8-bit timing */
    pci_set_long(pci_conf + 0x50, 0x00000000);  /* UDMA/66 control */

    /* BAR Initialization */
    s->num_bars = 5;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_PIO, .size = 8, .name = "bmdma-bar0" };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_PIO, .size = 4, .name = "bmdma-bar1" };
    s->bar_info[2] = (BARInfo){ .index = 2, .type = BAR_TYPE_PIO, .size = 8, .name = "bmdma-bar2" };
    s->bar_info[3] = (BARInfo){ .index = 3, .type = BAR_TYPE_PIO, .size = 4, .name = "bmdma-bar3" };
    s->bar_info[4] = (BARInfo){ .index = 4, .type = BAR_TYPE_PIO, .size = 16, .name = "bmdma-bar4" };

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Force BAR addresses to ensure they are assigned in the guest */
    pci_set_long(pdev->config + PCI_BASE_ADDRESS_0, 0x3000 | PCI_BASE_ADDRESS_SPACE_IO);
    pci_set_long(pdev->config + PCI_BASE_ADDRESS_1, 0x3008 | PCI_BASE_ADDRESS_SPACE_IO);
    pci_set_long(pdev->config + PCI_BASE_ADDRESS_2, 0x3010 | PCI_BASE_ADDRESS_SPACE_IO);
    pci_set_long(pdev->config + PCI_BASE_ADDRESS_3, 0x3018 | PCI_BASE_ADDRESS_SPACE_IO);
    pci_set_long(pdev->config + PCI_BASE_ADDRESS_4, 0x3020 | PCI_BASE_ADDRESS_SPACE_IO);

    /* Enable I/O space and bus master in the command register */
    pci_set_word(pdev->config + PCI_COMMAND,
                 pci_get_word(pdev->config + PCI_COMMAND) | PCI_COMMAND_IO | PCI_COMMAND_MASTER);
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
    .name = "pata_via_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT8(primary.cmd[0], PCIBaseState),
        VMSTATE_UINT8(primary.ctl, PCIBaseState),
        VMSTATE_UINT8(bmstate.command, PCIBaseState),
        VMSTATE_UINT8(bmstate.status, PCIBaseState),
        VMSTATE_UINT32(bmstate.prdt_addr, PCIBaseState),
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
