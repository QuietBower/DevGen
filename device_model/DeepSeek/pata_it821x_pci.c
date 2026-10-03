/*
 * QEMU 8.2.10 PCI device model for pata_it821x
 * Implements an IDE controller with two channels, PIO-only.
 *
 * Fix: Changed all BARs from IO to Memory to allow resource assignment
 * on PCIe root ports where IO forwarding is disabled.
 * Added programming interface byte 0x85 to indicate native mode IDE
 * with bus master support, preventing kernel legacy mode fallback.
 * Increased BAR sizes to 16 bytes (minimum valid for memory BARs)
 * Added PCIe capability and Express interface to allow assignment
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

#define TYPE_PCIBASE_DEVICE "pata_it821x_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define VENDOR_ID 0x1283
#define DEVICE_ID 0x8211
#define CLASS_ID  0x0101

/* Per-channel IDE state */
typedef struct IDEChannel {
    /* Command block registers */
    uint8_t error;     /* read at offset 1 */
    uint8_t features;  /* write at offset 1 */
    uint8_t sector_count;
    uint8_t lba_low;
    uint8_t lba_mid;
    uint8_t lba_high;
    uint8_t device;
    uint8_t status;
    uint8_t command;   /* write-only at offset 7 */
    /* Control block */
    uint8_t device_control;
    /* BMDMA registers */
    uint8_t bmdma_cmd;
    uint8_t bmdma_status;
    uint32_t bmdma_prd;
    /* PIO data transfer state */
    uint8_t id_data[512];
    int id_data_index;
    bool bsy;
    bool drdy;
    bool drq;
    bool err;
    bool intrq;
    /* Soft reset tracking */
    bool srst; /* true if SRST bit is set in device_control */
} IDEChannel;

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];
    int num_bars;

    bool has_msi;
    bool has_msix;
    uint32_t intr_status;   /* unused, kept for template */
    uint32_t intr_mask;

    IDEChannel ch[2];
};

/* Default IDENTIFY DEVICE data for a minimal PIO-only ATA device */
static const uint8_t def_identify_data[512] = {
    /* Word 0: general configuration */
    0x5A, 0x04,   /* bits 15-0: non-removable, not removable, etc. */
    /* Word 1: cylinders */
    0xFF, 0x3F,   /* 16383 */
    /* Word 2: reserved */
    0x00, 0x00,
    /* Word 3: heads */
    0x10, 0x00,   /* 16 */
    /* Word 4-5: unformatted bytes per track, sectors per track */
    0x00, 0x00, 0x3F, 0x00, /* 63 sectors per track */
    /* Word 6: sectors default translation */
    0x10, 0x9F, 0x00, 0x00,
    /* Word 7-8: reserved for assignment */
    0x00, 0x00, 0x00, 0x00,
    /* Word 9: reserved */
    0x00, 0x00,
    /* Word 10-19: serial number (right-justified, padded with spaces) */
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    0x20, 0x20, 0x20, 0x20,
    /* Word 20: buffer type */
    0x03, 0x00,
    /* Word 21: buffer size in 512-byte increments */
    0x00, 0x02,   /* 512 */
    /* Word 22: reserved */
    0x00, 0x00,
    /* Words 23-26: firmware revision */
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    /* Words 27-46: model number */
    'Q', 'E', 'M', 'U', ' ', 'D', 'r', 'i',
    'v', 'e', ' ', ' ', ' ', ' ', ' ', ' ',
    ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
    ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
    ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
    /* Word 47: maximum sectors per interrupt on READ/WRITE MULTIPLE */
    0x00, 0x80,   /* some drives use 0x8010 */
    /* Word 48: reserved */
    0x00, 0x00,
    /* Word 49: capabilities */
    0x00, 0x0A,   /* LBA supported (0x200), IORDY (0x800) => 0x0A00, no DMA */
    /* Word 50: capabilities */
    0x00, 0x00,
    /* Word 51: PIO timing */
    0x00, 0x00,
    /* Word 52: DMA timing (obsolete) */
    0x00, 0x00,
    /* Word 53-58: valid fields, current settings */
    0x03, 0x00,   /* Word 53: fields 54-58 valid */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    /* Word 59: number of sectors per DRQ block */
    0x00, 0x01,   /* 256 */
    /* Word 60-61: LBA28 total user addressable sectors */
    0x10, 0x9F, 0x00, 0x00,  /* 0x009F10 = 40720? Actually 16383*16*63=16514064 = 0xFC0810, but we use small */
    /* Word 62: obsolete */
    0x00, 0x00,
    /* Word 63: multi-word DMA */
    0x07, 0x00,   /* MWDMA modes 0-2 supported? No, we disable DMA, so 0? Keep 0x0007? Leave as 0? We'll zero it. */
    /* Word 64-70: PIO modes, minimum cycle times, etc. (can be zero) */
    /* Word 71-79: reserved */
    /* Word 80: major version */
    0x1F, 0x00,
    /* Word 81: minor version */
    0x00, 0x00,
    /* Word 82-127: features/commands: we set no DMA, no special features */
    /* Zero them all */
    /* Fill rest with zeros */
};

/* Forward declarations */
static void pcibase_update_irq(PCIBaseState *s);

/* Command block handlers */
static uint64_t pcibase_cmd_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_cmd_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
/* Control block handlers */
static uint64_t pcibase_ctrl_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_ctrl_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
/* BMDMA handlers */
static uint64_t pcibase_bmdma_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_bmdma_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);

static const MemoryRegionOps pcibase_cmd_ops = {
    .read = pcibase_cmd_read,
    .write = pcibase_cmd_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 2 },
};

static const MemoryRegionOps pcibase_ctrl_ops = {
    .read = pcibase_ctrl_read,
    .write = pcibase_ctrl_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 1 },
};

static const MemoryRegionOps pcibase_bmdma_ops = {
    .read = pcibase_bmdma_read,
    .write = pcibase_bmdma_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* Update IRQ level based on pending interrupts from both channels */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool level = (s->ch[0].intrq || (s->ch[0].bmdma_status & 0x04)) ||
                 (s->ch[1].intrq || (s->ch[1].bmdma_status & 0x04));
    pci_set_irq(pdev, level);
}

/* Perform a software reset on a channel */
static void pcibase_channel_reset(IDEChannel *ch)
{
    ch->status = 0x50;  /* DRDY | DSC */
    ch->bsy = false;
    ch->drq = false;
    ch->intrq = false;
    ch->err = false;
    ch->drdy = true;
    ch->id_data_index = 0;
    ch->command = 0;
    ch->device_control = 0x00;
    ch->bmdma_cmd = 0;
    ch->bmdma_status = 0;
    ch->bmdma_prd = 0;
    ch->srst = false;
    /* Keep error/sector count etc as is, but we zero them */
    ch->error = 1;      /* default */
    ch->sector_count = 1;
    ch->lba_low = 1;
    ch->lba_mid = 0;
    ch->lba_high = 0;
    ch->device = 0xA0;  /* Master, LBA */
}

/* Command block read (opaque = IDEChannel*) */
static uint64_t pcibase_cmd_read(void *opaque, hwaddr addr, unsigned size)
{
    IDEChannel *ch = (IDEChannel *)opaque;
    uint64_t ret = 0;

    switch (addr) {
    case 0: /* Data register (16-bit) */
        if (size == 2) {
            if (ch->drq && ch->id_data_index < 256) {
                uint16_t val = le16_to_cpu(*(uint16_t *)&ch->id_data[ch->id_data_index * 2]);
                ch->id_data_index++;
                if (ch->id_data_index >= 256) {
                    ch->drq = false;
                    ch->intrq = true;
                    pcibase_update_irq(container_of(ch, PCIBaseState, ch[0]));
                }
                ret = val;
            }
        } else {
            /* Unexpected size */
        }
        break;
    case 1: /* Error register */
        ret = ch->error;
        break;
    case 2:
        ret = ch->sector_count;
        break;
    case 3:
        ret = ch->lba_low;
        break;
    case 4:
        ret = ch->lba_mid;
        break;
    case 5:
        ret = ch->lba_high;
        break;
    case 6:
        ret = ch->device;
        break;
    case 7: /* Status */
        ret = ch->status;
        if (ch->intrq) {
            ch->intrq = false;
            pcibase_update_irq(container_of(ch, PCIBaseState, ch[0]));
        }
        break;
    default:
        break;
    }
    return ret;
}

/* Command block write */
static void pcibase_cmd_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    IDEChannel *ch = (IDEChannel *)opaque;

    switch (addr) {
    case 0: /* Data register (16-bit) - not used for writes in our emulation */
        break;
    case 1: /* Features */
        ch->features = val & 0xFF;
        break;
    case 2:
        ch->sector_count = val & 0xFF;
        break;
    case 3:
        ch->lba_low = val & 0xFF;
        break;
    case 4:
        ch->lba_mid = val & 0xFF;
        break;
    case 5:
        ch->lba_high = val & 0xFF;
        break;
    case 6:
        ch->device = val & 0xFF;
        break;
    case 7: /* Command */
        ch->command = val & 0xFF;
        switch (ch->command) {
        case 0xEC: /* IDENTIFY DEVICE */
            memcpy(ch->id_data, def_identify_data, 512);
            ch->id_data_index = 0;
            ch->status = 0x48; /* DRDY | DRQ */
            ch->bsy = false;
            ch->drq = true;
            ch->drdy = true;
            ch->intrq = false;
            ch->err = false;
            break;
        default:
            /* Unknown command: set error and abort */
            ch->status = 0x51; /* DRDY | ERR */
            ch->err = true;
            ch->bsy = false;
            ch->drq = false;
            ch->intrq = true;
            pcibase_update_irq(container_of(ch, PCIBaseState, ch[0]));
            break;
        }
        break;
    default:
        break;
    }
}

/* Control block read (opaque = IDEChannel*) */
static uint64_t pcibase_ctrl_read(void *opaque, hwaddr addr, unsigned size)
{
    IDEChannel *ch = (IDEChannel *)opaque;
    uint64_t ret = 0;

    switch (addr) {
    case 2: /* Alternate Status */
        ret = ch->status & ~0x80; /* mask BSY? Actually alt status does not mask BSY, but we return same as status */
        /* Reading alt status does NOT clear interrupt */
        break;
    case 3: /* Drive Address - not used, return 0 */
        break;
    default:
        break;
    }
    return ret;
}

/* Control block write */
static void pcibase_ctrl_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    IDEChannel *ch = (IDEChannel *)opaque;

    switch (addr) {
    case 2: /* Device Control */
        {
            uint8_t new_val = val & 0xFF;
            bool new_srst = new_val & 0x04; /* bit 2: SRST */
            if (ch->srst && !new_srst) {
                /* SRST cleared: perform reset */
                pcibase_channel_reset(ch);
                pcibase_update_irq(container_of(ch, PCIBaseState, ch[0]));
            }
            ch->device_control = new_val;
            ch->srst = new_srst;
        }
        break;
    case 3: /* Drive Address - ignore */
        break;
    default:
        break;
    }
}

/* BMDMA read (opaque = PCIBaseState*) */
static uint64_t pcibase_bmdma_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = (PCIBaseState *)opaque;
    int ch_idx = (addr >= 8) ? 1 : 0;
    hwaddr ch_addr = addr - ch_idx * 8;
    IDEChannel *ch = &s->ch[ch_idx];
    uint64_t ret = 0;

    switch (ch_addr) {
    case 0: /* Command */
        ret = ch->bmdma_cmd;
        break;
    case 2: /* Status */
        ret = ch->bmdma_status;
        /* Reading status does not clear interrupt by BMDMA spec; but typical handler writes to clear */
        break;
    case 4: /* PRD Table Address (32-bit) */
        ret = ch->bmdma_prd;
        break;
    default:
        break;
    }
    return ret;
}

/* BMDMA write */
static void pcibase_bmdma_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = (PCIBaseState *)opaque;
    int ch_idx = (addr >= 8) ? 1 : 0;
    hwaddr ch_addr = addr - ch_idx * 8;
    IDEChannel *ch = &s->ch[ch_idx];

    switch (ch_addr) {
    case 0: /* Command */
        ch->bmdma_cmd = val & 0xFF;
        break;
    case 2: /* Status (write to clear) */
        if (val & 0x04) {
            ch->bmdma_status &= ~0x04;
            pcibase_update_irq(s);
        }
        break;
    case 4: /* PRD Table Address */
        ch->bmdma_prd = (uint32_t)val;
        break;
    default:
        break;
    }
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    s->intr_status = 0;
    s->intr_mask = 0;

    for (int i = 0; i < 2; i++) {
        pcibase_channel_reset(&s->ch[i]);
    }
    pcibase_update_irq(s);
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_CLASS_PROG, 0x85);  /* Native mode IDE with bus master */
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Initialize PCIe capability */
    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);

    /* Set vendor-specific config register 0x50: passthru mode, ATA_66 clock for both channels */
    pci_set_byte(pci_conf + 0x50, 0x00);

    /* Initialize BARs as Memory regions with minimum size 16 for PCIe memory compatibility */
    memory_region_init_io(&s->bar_regions[0], OBJECT(s), &pcibase_cmd_ops, &s->ch[0],
                          "ata-primary-cmd", 16);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_regions[0]);

    memory_region_init_io(&s->bar_regions[1], OBJECT(s), &pcibase_ctrl_ops, &s->ch[0],
                          "ata-primary-ctrl", 16);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_regions[1]);

    memory_region_init_io(&s->bar_regions[2], OBJECT(s), &pcibase_cmd_ops, &s->ch[1],
                          "ata-secondary-cmd", 16);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_regions[2]);

    memory_region_init_io(&s->bar_regions[3], OBJECT(s), &pcibase_ctrl_ops, &s->ch[1],
                          "ata-secondary-ctrl", 16);
    pci_register_bar(pdev, 3, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_regions[3]);

    memory_region_init_io(&s->bar_regions[4], OBJECT(s), &pcibase_bmdma_ops, s,
                          "ata-bmdma", 16);
    pci_register_bar(pdev, 4, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_regions[4]);

    s->num_bars = 5;

    /* Reset channels to default state */
    for (int i = 0; i < 2; i++) {
        pcibase_channel_reset(&s->ch[i]);
    }

    /* No MSI/MSI-X */
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
    .name = "pata_it821x_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        /* Add channel states if migration needed, not implemented for now */
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
