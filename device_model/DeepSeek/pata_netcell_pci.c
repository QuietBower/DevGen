/* 
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

#define TYPE_PCIBASE_DEVICE "pata_netcell_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_NETCELL     0x169d
#define PCI_DEVICE_ID_REVOLUTION  0x0044
#define PCI_CLASS_STORAGE_IDE     0x0101

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

typedef struct {
    PCIBaseState *s;
    int channel;
} ChannelState;

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;

    /* Hardware Register Shadows */
    struct {
        uint8_t cmd;   /* BM_COMMAND */
        uint8_t stat;  /* BM_STATUS */
        uint32_t prd_addr; /* BM_PRD_ADDR */
    } bmdma[2]; /* index 0 primary, 1 secondary */

    struct {
        uint16_t data;
        uint8_t error;
        uint8_t features;
        uint8_t sector_count;
        uint8_t sector_number;
        uint8_t cylinder_low;
        uint8_t cylinder_high;
        uint8_t drive_head;
        uint8_t command;
        uint8_t status;
        uint8_t device_control;
        uint8_t alt_status;
    } ata[2]; /* 0 primary, 1 secondary */

    uint16_t identify_data[256];
    bool identify_pending[2];
    int identify_offset[2];

    bool irq_pending;
    QEMUTimer *cmd_timer;

    /* Channel state structs to pass to MemoryRegion */
    ChannelState chan0;
    ChannelState chan1;
};

/* Forward declarations */
static void pcibase_update_irq(PCIBaseState *s);

/* Identifying data for primary master */
static const uint16_t default_id[256] = {
    [0] = 0x0040, /* 4 words? ATA device */
    [1] = 0x3FFF, /* cylinders */
    [2] = 0x0000,
    [3] = 0x0010, /* heads */
    [4] = 0x0000,
    [5] = 0x0000,
    [6] = 0x003F, /* sectors */
    [7] = 0x0000, /* ... */
    [8] = 0x0000,
    [9] = 0x0000,
    [10 ... 19] = 0x0000,
    [20] = 0x0000,
    [21] = 0x0000,
    [22] = 0x0000,
    [23 ... 46] = 0x0000,
    [47] = 0x8000, /* max sectors */
    [48] = 0x0000,
    [49] = 0x0F00, /* capabilities */
    [50] = 0x4000, /* cap */
    [51] = 0x0000,
    [52] = 0x0000,
    [53] = 0x0007, /* words 88-70 valid */
    [54] = 0x0000,
    [55] = 0x0000,
    [56] = 0x0000,
    [57] = 0x0000,
    [58] = 0x0000,
    [59] = 0x0000,
    [60] = 0x0000,
    [61] = 0x0000,
    [62] = 0x0000,
    [63] = 0x0000,
    [64] = 0x0000,
    [65] = 0x0000,
    [66] = 0x0000,
    [67] = 0x0000,
    [68] = 0x0000,
    [69] = 0x0000,
    [70] = 0x0000,
    [71 ... 79] = 0x0000,
    [80] = 0x0000,
    [81] = 0x0000,
    [82] = 0x0000,
    [83] = 0x0000,
    [84] = 0x0000,
    [85] = 0x0000,
    [86] = 0x0000,
    [87] = 0x0000,
    [88] = 0x0000,
    [89] = 0x0000,
    [90] = 0x0000,
    [91] = 0x0000,
    [92] = 0x0000,
    [93] = 0x0000,
    [94] = 0x0000,
    [95] = 0x0000,
    [96] = 0x0000,
    [97] = 0x0000,
    [98] = 0x0000,
    [99] = 0x0000,
    [100 ... 127] = 0x0000,
    [128 ... 159] = 0x0000,
    [160] = 0x0000,
    [161 ... 175] = 0x0000,
    [176 ... 255] = 0x0000,
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->irq_pending) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

static void cmd_timer_cb(void *opaque)
{
    PCIBaseState *s = opaque;
    /* For primary channel, assume command completed; raise interrupt */
    s->irq_pending = true;
    pcibase_update_irq(s);
}

static uint64_t pcibase_bmdma_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    int ch = 0;
    uint64_t val = 0;

    /* Determine channel based on offset */
    if (addr >= 8) {
        ch = 1;
        addr -= 8;
    }

    switch (addr) {
    case 0: /* BM_COMMAND */
        val = s->bmdma[ch].cmd;
        break;
    case 2: /* BM_STATUS */
        val = s->bmdma[ch].stat;
        break;
    case 4: /* BM_PRD_ADDR */
        val = s->bmdma[ch].prd_addr;
        break;
    }
    return val;
}

static void pcibase_bmdma_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    int ch = 0;

    if (addr >= 8) {
        ch = 1;
        addr -= 8;
    }

    switch (addr) {
    case 0: /* BM_COMMAND */
        if (val & 0x01) { /* Start bit */
            s->bmdma[ch].stat = 0x01; /* DMA active */
            /* After a short delay, we complete the transfer */
            timer_mod(s->cmd_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 10);
        }
        s->bmdma[ch].cmd = val;
        break;
    case 2: /* BM_STATUS (W1C) */
        s->bmdma[ch].stat &= ~(val & 0x86); /* clear interrupt, error, and simplex bits */
        pcibase_update_irq(s);
        break;
    case 4: /* BM_PRD_ADDR */
        s->bmdma[ch].prd_addr = val;
        break;
    }
}

static const MemoryRegionOps pcibase_bmdma_ops = {
    .read = pcibase_bmdma_read,
    .write = pcibase_bmdma_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static uint64_t pcibase_cmd_read(void *opaque, hwaddr addr, unsigned size)
{
    ChannelState *cs = opaque;
    PCIBaseState *s = cs->s;
    int ch = cs->channel;
    uint64_t val = 0;

    if (ch != 0) {
        /* Only primary channel emulated; secondary returns zero */
        return 0;
    }

    addr &= 7;
    switch (addr) {
    case 0: /* DATA */
        if (s->identify_pending[ch]) {
            if (s->identify_offset[ch] < 256) {
                val = s->identify_data[s->identify_offset[ch]++];
                if (s->identify_offset[ch] >= 256) {
                    s->identify_pending[ch] = false;
                    s->ata[ch].status = 0x50; /* DRDY, no DRQ */
                }
            }
        } else {
            val = s->ata[ch].data;
        }
        break;
    case 1: /* ERROR */
        val = s->ata[ch].error;
        break;
    case 2: /* FEATURES */
        val = s->ata[ch].features;
        break;
    case 3: /* SECTOR_COUNT */
        val = s->ata[ch].sector_count;
        break;
    case 4: /* SECTOR_NUMBER */
        val = s->ata[ch].sector_number;
        break;
    case 5: /* CYLINDER_LOW */
        val = s->ata[ch].cylinder_low;
        break;
    case 6: /* CYLINDER_HIGH */
        val = s->ata[ch].cylinder_high;
        break;
    case 7: /* DEVICE_HEAD / STATUS */
        if (size == 1) {
            val = s->ata[ch].status;
            /* Clear the device interrupt when status is read */
            s->irq_pending = false;
            pcibase_update_irq(s);
        } else {
            val = s->ata[ch].drive_head;
        }
        break;
    }
    return val;
}

static void pcibase_cmd_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    ChannelState *cs = opaque;
    PCIBaseState *s = cs->s;
    int ch = cs->channel;

    if (ch != 0) {
        return;
    }

    addr &= 7;
    switch (addr) {
    case 0: /* DATA */
        s->ata[ch].data = val;
        break;
    case 1: /* FEATURES */
        s->ata[ch].features = val;
        break;
    case 2: /* SECTOR_COUNT */
        s->ata[ch].sector_count = val;
        break;
    case 3: /* SECTOR_NUMBER */
        s->ata[ch].sector_number = val;
        break;
    case 4: /* CYLINDER_LOW */
        s->ata[ch].cylinder_low = val;
        break;
    case 5: /* CYLINDER_HIGH */
        s->ata[ch].cylinder_high = val;
        break;
    case 6: /* DRIVE_HEAD */
        s->ata[ch].drive_head = val;
        break;
    case 7: /* COMMAND */
        s->ata[ch].command = val;
        if (val == 0xEC) { /* IDENTIFY */
            s->identify_pending[ch] = true;
            s->identify_offset[ch] = 0;
            s->ata[ch].status = 0x58; /* DRDY=1, DRQ=1 */
            s->irq_pending = true;
            pcibase_update_irq(s);
        }
        break;
    }
}

static const MemoryRegionOps pcibase_cmd_ops = {
    .read = pcibase_cmd_read,
    .write = pcibase_cmd_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static uint64_t pcibase_ctl_read(void *opaque, hwaddr addr, unsigned size)
{
    ChannelState *cs = opaque;
    PCIBaseState *s = cs->s;
    int ch = cs->channel;

    if (ch != 0) {
        return 0;
    }

    /* Control block has DEVICE CONTROL (write) / ALT STATUS (read) at offset 2 */
    if (addr == 2) {
        return s->ata[ch].alt_status;
    }
    return 0;
}

static void pcibase_ctl_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    ChannelState *cs = opaque;
    PCIBaseState *s = cs->s;
    int ch = cs->channel;

    if (ch != 0) {
        return;
    }

    if (addr == 2) {
        s->ata[ch].device_control = val;
    }
}

static const MemoryRegionOps pcibase_ctl_ops = {
    .read = pcibase_ctl_read,
    .write = pcibase_ctl_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 1 },
    .impl  = { .min_access_size = 1, .max_access_size = 1 },
};

/* Unused MMIO handlers; we only use PIO */
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

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Reset BMDMA registers */
    s->bmdma[0].cmd = 0;
    s->bmdma[0].stat = 0x84; /* simplex bit set, others clear */
    s->bmdma[0].prd_addr = 0;
    s->bmdma[1].cmd = 0;
    s->bmdma[1].stat = 0x84;
    s->bmdma[1].prd_addr = 0;

    /* Reset ATA registers for primary channel */
    s->ata[0].data = 0;
    s->ata[0].error = 0;
    s->ata[0].features = 0;
    s->ata[0].sector_count = 1;
    s->ata[0].sector_number = 1;
    s->ata[0].cylinder_low = 0;
    s->ata[0].cylinder_high = 0;
    s->ata[0].drive_head = 0xA0; /* master */
    s->ata[0].command = 0;
    s->ata[0].status = 0x50; /* DRDY */
    s->ata[0].device_control = 0;
    s->ata[0].alt_status = 0x50;

    s->ata[1] = s->ata[0]; /* copy reset to secondary */

    memcpy(s->identify_data, default_id, sizeof(default_id));
    s->identify_pending[0] = false;
    s->identify_pending[1] = false;
    s->identify_offset[0] = 0;
    s->identify_offset[1] = 0;

    s->irq_pending = false;
    pcibase_update_irq(s);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_NETCELL);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_REVOLUTION);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_STORAGE_IDE);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Enable I/O and bus mastering from the start to avoid resource assignment issues */
    pci_set_word(pci_conf + PCI_COMMAND, PCI_COMMAND_IO | PCI_COMMAND_MASTER);

    /* Prepare channel state */
    s->chan0.s = s;
    s->chan0.channel = 0;
    s->chan1.s = s;
    s->chan1.channel = 1;

    /* Register BARs manually to use per-bar ops */
    /* BAR0: primary command block (PIO) */
    memory_region_init_io(&s->bar_regions[0], OBJECT(s), &pcibase_cmd_ops, &s->chan0, "pri-cmd-block", 8);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_IO, &s->bar_regions[0]);

    /* BAR1: primary control block (PIO) */
    memory_region_init_io(&s->bar_regions[1], OBJECT(s), &pcibase_ctl_ops, &s->chan0, "pri-ctl-block", 4);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_IO, &s->bar_regions[1]);

    /* BAR2: secondary command block (PIO) */
    memory_region_init_io(&s->bar_regions[2], OBJECT(s), &pcibase_cmd_ops, &s->chan1, "sec-cmd-block", 8);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_IO, &s->bar_regions[2]);

    /* BAR3: secondary control block (PIO) */
    memory_region_init_io(&s->bar_regions[3], OBJECT(s), &pcibase_ctl_ops, &s->chan1, "sec-ctl-block", 4);
    pci_register_bar(pdev, 3, PCI_BASE_ADDRESS_SPACE_IO, &s->bar_regions[3]);

    /* BAR4: Bus Master DMA (PIO) */
    memory_region_init_io(&s->bar_regions[4], OBJECT(s), &pcibase_bmdma_ops, s, "bmdma", 16);
    pci_register_bar(pdev, 4, PCI_BASE_ADDRESS_SPACE_IO, &s->bar_regions[4]);

    /* Set num_bars to 0 to skip the default loop in template */
    s->num_bars = 0;

    /* Create command completion timer */
    s->cmd_timer = timer_new_ms(QEMU_CLOCK_VIRTUAL, cmd_timer_cb, s);
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
    timer_del(s->cmd_timer);
    timer_free(s->cmd_timer);
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "pata_netcell_pci",
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
