/*
 * QEMU PCI device model for AMD PATA (pata_amd)
 * Phase 2: Minimal behavioral implementation sufficient for Linux driver probe.
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
#include "hw/hw.h"

#define TYPE_PCIBASE_DEVICE "pata_amd_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCIBASE_VENDOR_ID 0x1022  /* AMD */
#define PCIBASE_DEVICE_ID 0x7401  /* PCI_DEVICE_ID_AMD_COBRA_7401 */
#define PCIBASE_CLASS_ID  0x0101  /* PCI_CLASS_STORAGE_IDE */

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

/* Simple internal IDE register layout for two channels (primary/secondary).
 * This is ONLY to provide a stable software-visible interface for libata's
 * generic SFF/BMDMA helpers. No attempt is made to fully emulate ATA.
 */

typedef struct IDEChannelRegs {
    /* Taskfile registers */
    uint8_t data;      /* 0 */
    uint8_t error;     /* 1 - read */
    uint8_t feature;   /* 1 - write */
    uint8_t nsect;     /* 2 */
    uint8_t lbal;      /* 3 */
    uint8_t lbam;      /* 4 */
    uint8_t lbah;      /* 5 */
    uint8_t device;    /* 6 */
    uint8_t status;    /* 7 - read */
    uint8_t command;   /* 7 - write */

    /* Control/altstatus (separate BAR) */
    uint8_t control;   /* write */
    uint8_t altstatus; /* read */

    /* Simple interrupt latch */
    bool irq_pending;
} IDEChannelRegs;

/* Bus-master IDE (BMDMA) per-channel registers */
typedef struct BMDMARegs {
    uint8_t cmd;       /* offset 0 */
    uint8_t reserved1; /* 1 */
    uint8_t status;    /* 2 */
    uint8_t reserved2; /* 3 */
    uint32_t prd;      /* 4-7 PRD table address */
} BMDMARegs;

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;

    /* Two SFF IDE channels */
    IDEChannelRegs chan[2];

    /* Single BMDMA engine at BAR4, two channels (primary/secondary) */
    BMDMARegs bmdma[2];
};

static void pcibase_update_irq(PCIBaseState *s)
{
    /* Very simple: raise legacy INTx whenever any channel has irq_pending. */
    PCIDevice *pdev = PCI_DEVICE(s);
    bool any = s->chan[0].irq_pending || s->chan[1].irq_pending;
    pci_set_irq(pdev, any ? 1 : 0);
}

static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /* Minimal stub: clear BUSY/DRQ and complete immediately, set INTRQ. */
    int i;
    for (i = 0; i < 2; i++) {
        BMDMARegs *b = &s->bmdma[i];
        if (b->cmd & 0x01) {
            /* Stop bit is auto-cleared */
            b->cmd &= ~0x01;
            /* Mark DMA completed: set interrupt and clear active bits. */
            b->status |= 0x04; /* INT */
            b->status &= ~0x01; /* clear ACTIVE */

            /* For the corresponding channel, clear DRQ/BUSY and set DRDY+INTRQ. */
            IDEChannelRegs *c = &s->chan[i];
            c->status &= ~(0x80 /*BUSY*/ | 0x08 /*DRQ*/);
            c->status |= 0x40; /* DRDY */
            c->irq_pending = true;
        }
    }
    pcibase_update_irq(s);
}

/* Helper to map BAR+offset to channel index and register offset. */
static int pcibase_bar_to_channel(hwaddr addr, int *chan_index)
{
    /* Primary commands at BAR0, secondary at BAR2 (separate BAR entries). */
    if (addr < 0x08) {
        *chan_index = 0;
        return 0;
    }
    if (addr >= 0x100 && addr < 0x108) {
        *chan_index = 1;
        return 0;
    }
    return -1;
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0xff;

    /* This implementation is only used if we configured an MMIO BAR,
     * which we currently do not. Keep for completeness. */
    (void)s;
    (void)addr;
    (void)size;
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;
    (void)addr;
    (void)val;
    (void)size;
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0xff;
    int chan_idx;

    /* Command block regions: primary at BAR0 (0-7), secondary at BAR2 (we map at 0x100-0x107). */
    if (pcibase_bar_to_channel(addr, &chan_idx) == 0) {
        IDEChannelRegs *c = &s->chan[chan_idx];
        hwaddr off = (chan_idx == 0) ? addr : (addr - 0x100);
        switch (off & 7) {
        case 0: /* data */
            if (size == 2 || size == 4)
                val = c->data; /* trivial */
            else
                val = c->data;
            break;
        case 1: /* error */
            val = c->error;
            break;
        case 2:
            val = c->nsect;
            break;
        case 3:
            val = c->lbal;
            break;
        case 4:
            val = c->lbam;
            break;
        case 5:
            val = c->lbah;
            break;
        case 6:
            val = c->device;
            break;
        case 7: /* status */
            val = c->status;
            /* Reading status clears pending interrupt on this channel. */
            c->irq_pending = false;
            pcibase_update_irq(s);
            break;
        default:
            break;
        }
        return val;
    }

    /* Control block (altstatus) for primary/secondary: we map to small regions
     * after command ranges: primary at 0x08, secondary at 0x108. Only altstatus
     * read is used by generic SFF helpers.
     */
    if (addr == 0x08) {
        val = s->chan[0].status; /* altstatus mirrors status */
        return val;
    }
    if (addr == 0x108) {
        val = s->chan[1].status;
        return val;
    }

    /* BMDMA BAR4: base we set to IO space via separate BAR, handled in same pio ops. */
    /* We map BAR4 at 0x200-0x20f (8 bytes per channel). */
    if (addr >= 0x200 && addr < 0x210) {
        hwaddr off = addr - 0x200;
        int ch = (off & 0x08) ? 1 : 0;
        hwaddr roff = off & 0x07;
        BMDMARegs *b = &s->bmdma[ch];
        switch (roff) {
        case 0x0:
            val = b->cmd;
            break;
        case 0x2:
            val = b->status;
            break;
        case 0x4:
            val = b->prd & 0xff;
            if (size == 2)
                val = b->prd & 0xffff;
            else if (size == 4)
                val = b->prd;
            break;
        default:
            val = 0xff;
            break;
        }
        return val;
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    int chan_idx;

    if (pcibase_bar_to_channel(addr, &chan_idx) == 0) {
        IDEChannelRegs *c = &s->chan[chan_idx];
        hwaddr off = (chan_idx == 0) ? addr : (addr - 0x100);
        switch (off & 7) {
        case 0: /* data */
            c->data = (uint8_t)val;
            break;
        case 1: /* feature */
            c->feature = (uint8_t)val;
            break;
        case 2:
            c->nsect = (uint8_t)val;
            break;
        case 3:
            c->lbal = (uint8_t)val;
            break;
        case 4:
            c->lbam = (uint8_t)val;
            break;
        case 5:
            c->lbah = (uint8_t)val;
            break;
        case 6:
            c->device = (uint8_t)val;
            break;
        case 7: /* command register: accept and synthesize completion */
            c->command = (uint8_t)val;
            /* Emulate simple PIO command complete: BUSY clear, DRDY set, INTRQ. */
            c->status &= ~(0x80 /*BUSY*/ | 0x08 /*DRQ*/);
            c->status |= 0x40; /* DRDY */
            c->irq_pending = true;
            pcibase_update_irq(s);
            break;
        default:
            break;
        }
        return;
    }

    /* Control block: device control writes */
    if (addr == 0x08) {
        s->chan[0].control = (uint8_t)val;
        /* If SRST bit (0x04) is toggled, pretend reset completes quickly. */
        if (s->chan[0].control & 0x04) {
            s->chan[0].status = 0x00; /* BUSY cleared, no device yet */
        }
        return;
    }
    if (addr == 0x108) {
        s->chan[1].control = (uint8_t)val;
        if (s->chan[1].control & 0x04) {
            s->chan[1].status = 0x00;
        }
        return;
    }

    /* BMDMA BAR4 region: 0x200-0x20f */
    if (addr >= 0x200 && addr < 0x210) {
        hwaddr off = addr - 0x200;
        int ch = (off & 0x08) ? 1 : 0;
        hwaddr roff = off & 0x07;
        BMDMARegs *b = &s->bmdma[ch];
        switch (roff) {
        case 0x0: /* command */
            b->cmd = (uint8_t)val;
            /* If START bit set, set ACTIVE bit and perform dummy DMA. */
            if (b->cmd & 0x01) {
                b->status |= 0x01; /* ACTIVE */
                pcibase_do_dma(s, (b->cmd & 0x08) != 0);
            }
            break;
        case 0x2: /* status: write-back clears bits written as 1 */
            b->status &= ~((uint8_t)val);
            break;
        case 0x4: /* PRD table address */
            if (size == 4) {
                b->prd = (uint32_t)val;
            } else {
                uint32_t cur = b->prd;
                if (size == 1) {
                    cur = (cur & ~0xffu) | ((uint8_t)val);
                } else if (size == 2) {
                    cur = (cur & ~0xffffu) | ((uint16_t)val);
                }
                b->prd = cur;
            }
            break;
        default:
            break;
        }
        return;
    }
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

    for (int i = 0; i < 2; i++) {
        IDEChannelRegs *c = &s->chan[i];
        memset(c, 0, sizeof(*c));
        c->status = 0x40; /* DRDY only, device present and idle */
        c->error = 0x01;  /* diagnostic passed (for dev0) */
    }

    for (int i = 0; i < 2; i++) {
        BMDMARegs *b = &s->bmdma[i];
        memset(b, 0, sizeof(*b));
        /* Status bit 0x80 is SIMPLEX; leave 0 so libata can clear it. */
    }

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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCIBASE_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCIBASE_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCIBASE_CLASS_ID);

    pci_set_byte(pci_conf + PCI_CLASS_PROG, 0x8F);
    
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* The real chips are legacy PCI IDE, not PCIe; we do not need PCIe caps
     * for libata to function. Leave caps minimal.
     */

    /* Initialize BAR layout compatible with generic ata_pci_sff_init_host()
     * and ata_pci_bmdma_init():
     *   BAR0: primary command block (8 IO ports)
     *   BAR1: primary control block (4 IO ports)
     *   BAR2: secondary command block (8 IO ports)
     *   BAR3: secondary control block (4 IO ports)
     *   BAR4: bus-master IDE (BMDMA) (16 IO ports)
     */

    s->num_bars = 5;

    s->bar_info[0].index = 0;
    s->bar_info[0].type  = BAR_TYPE_PIO;
    s->bar_info[0].size  = 8;
    s->bar_info[0].name  = "pata-amd-prim-cmd";

    s->bar_info[1].index = 1;
    s->bar_info[1].type  = BAR_TYPE_PIO;
    s->bar_info[1].size  = 4;
    s->bar_info[1].name  = "pata-amd-prim-ctl";

    s->bar_info[2].index = 2;
    s->bar_info[2].type  = BAR_TYPE_PIO;
    s->bar_info[2].size  = 8;
    s->bar_info[2].name  = "pata-amd-sec-cmd";

    s->bar_info[3].index = 3;
    s->bar_info[3].type  = BAR_TYPE_PIO;
    s->bar_info[3].size  = 4;
    s->bar_info[3].name  = "pata-amd-sec-ctl";

    s->bar_info[4].index = 4;
    s->bar_info[4].type  = BAR_TYPE_PIO;
    s->bar_info[4].size  = 16;
    s->bar_info[4].name  = "pata-amd-bmdma";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = false;
    s->has_msix = false;

    /* Initialize internal state */
    for (int i = 0; i < 2; i++) {
        IDEChannelRegs *c = &s->chan[i];
        memset(c, 0, sizeof(*c));
        c->status = 0x40; /* DRDY */
        c->error = 0x01;
    }
    for (int i = 0; i < 2; i++) {
        memset(&s->bmdma[i], 0, sizeof(s->bmdma[i]));
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
    (void)s;
}

static const VMStateDescription vmstate_pcibase = {
    .name = "pata_amd_pci",
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
    k->vendor_id = PCIBASE_VENDOR_ID;
    k->device_id = PCIBASE_DEVICE_ID;
    k->class_id  = PCIBASE_CLASS_ID;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
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

