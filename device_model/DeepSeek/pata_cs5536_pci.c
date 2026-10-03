/*
 * QEMU CS5536 ATA/IDE Controller Device Model
 * Implements behavior based on Linux driver pata_cs5536.c
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

#define TYPE_PCIBASE_DEVICE "pata_cs5536_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI Identifiers */
#define PCI_VENDOR_ID_AMD 0x1022
#define PCI_DEVICE_ID_AMD_CS5536_IDE 0x209A
#define PCI_CLASS_STORAGE_IDE 0x0101

/* ------------------------------------------------------------------ */
/* Register and Bit Definitions required by pata_cs5536.c               */
/* These are extracted from the driver.                                 */
/* ------------------------------------------------------------------ */

/* PCI IDE Configuration register base offset in PCI config space */
#define PCI_IDE_CFG  0x40

/* Register indices (dword offsets from PCI_IDE_CFG) */
#define CFG  0
#define DTC  1
#define CAST 2
#define ETC  3

/* CFG register bits */
#define IDE_CFG_CHANEN  (1 << 1)
#define IDE_CFG_CABLE   (1 << 3)

/* ------------------------------------------------------------------ */

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

typedef struct {
    PCIBaseState *state;
    int          bar_index;
} BARContext;

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARContext bar_ctx[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;

    /* CS5536 Custom PCI Configuration Registers */
    uint32_t cfgraf;  /* CFG */
    uint32_t dtctim;  /* DTC */
    uint32_t casttim; /* CAST */
    uint32_t etctim;  /* ETC */

    /* ATA / IDE Register Shadows */
    uint8_t ide_cmd0[8];   /* Primary Command Block */
    uint8_t ide_ctl0[4];   /* Primary Control Block */
    uint8_t ide_cmd1[8];   /* Secondary Command Block */
    uint8_t ide_ctl1[4];   /* Secondary Control Block */
    uint8_t ide_bmdma[16]; /* Bus Master DMA registers */

    /* Reset Timer for soft-reset emulation */
    QEMUTimer *reset_timer;
    bool reset_active;
    uint8_t reset_channel; /* 0: primary, 1: secondary */
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* Simple IRQ logic: assert if any IDE channel has an interrupt pending.
     * Real CS5536 may have separate interrupts per channel, but driver uses
     * shared interrupt. We just raise a single interrupt. */
    bool intr_pending = false;
    if (s->ide_bmdma[2] & 0x04) {     /* BM_STATUS_INT (primary) */
        intr_pending = true;
    }
    if (s->ide_bmdma[10] & 0x04) {    /* secondary */
        intr_pending = true;
    }
    pci_set_irq(pdev, intr_pending);
}

/* Device-initiated DMA logic (not needed by this driver) */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /* Driver uses BMDMA, not explicit device-initiated DMA. */
}

/* ------------------------------------------------------------------ */
/* Reset timer callback: finish a soft reset                          */
/* ------------------------------------------------------------------ */
static void cs5536_reset_timer(void *opaque)
{
    PCIBaseState *s = opaque;
    int ch = s->reset_channel;
    uint8_t *cmd = ch ? s->ide_cmd1 : s->ide_cmd0;
    uint8_t *bmdma = s->ide_bmdma + (ch ? 8 : 0);

    cmd[7] &= ~0x80; /* clear BSY */
    cmd[7] |= 0x40;  /* set DRDY */
    bmdma[2] &= ~0x01; /* clear DMA active */
    s->reset_active = false;
}

/* ------------------------------------------------------------------ */
/* PIO Handlers for IDE registers (BAR 0-4)                           */
/* ------------------------------------------------------------------ */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    BARContext *ctx = opaque;
    PCIBaseState *s = ctx->state;
    int bar = ctx->bar_index;
    uint64_t val = 0;

    switch (bar) {
    case 0: /* Primary Command Block */
        if (addr < 8) {
            val = s->ide_cmd0[addr];
        }
        break;
    case 1: /* Primary Control Block */
        if (addr < 4) {
            if (addr == 0) {
                /* Alternate Status (read-only) */
                val = s->ide_cmd0[7];
            } else {
                val = s->ide_ctl0[addr];
            }
        }
        break;
    case 2: /* Secondary Command Block */
        if (addr < 8) {
            val = s->ide_cmd1[addr];
        }
        break;
    case 3: /* Secondary Control Block */
        if (addr < 4) {
            if (addr == 0) {
                val = s->ide_cmd1[7];
            } else {
                val = s->ide_ctl1[addr];
            }
        }
        break;
    case 4: /* Bus Master IDE */
        if (addr < 16) {
            val = s->ide_bmdma[addr];
        }
        break;
    default:
        break;
    }
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    BARContext *ctx = opaque;
    PCIBaseState *s = ctx->state;
    int bar = ctx->bar_index;
    uint8_t *regs = NULL;
    int max_addr = 0;

    switch (bar) {
    case 0:
        regs = s->ide_cmd0;
        max_addr = 7;
        break;
    case 1:
        regs = s->ide_ctl0;
        max_addr = 3;
        break;
    case 2:
        regs = s->ide_cmd1;
        max_addr = 7;
        break;
    case 3:
        regs = s->ide_ctl1;
        max_addr = 3;
        break;
    case 4:
        regs = s->ide_bmdma;
        max_addr = 15;
        break;
    default:
        return;
    }

    if (addr > max_addr) {
        return;
    }

    /* Handle special registers with side effects */
    if (bar == 0 || bar == 2) {
        if (addr == 7) { /* Command register write */
            /* FIXME: emulate command execution minimally:
             * For IDENTIFY (0xEC), we would need to prepare data buffer.
             * For now, simply set DRDY and clear BSY; real chip would do more. */
            s->ide_cmd0[addr] = val; /* or ide_cmd1 */
            if (bar == 0) {
                s->ide_cmd0[7] &= ~0x80; /* clear BSY */
                s->ide_cmd0[7] |= 0x40;  /* set DRDY */
            } else {
                s->ide_cmd1[7] &= ~0x80;
                s->ide_cmd1[7] |= 0x40;
            }
            return;
        }
    } else if (bar == 1 || bar == 3) {
        if (addr == 0) { /* Device Control register */
            uint8_t old = regs[0];
            regs[0] = val & 0xFF;
            if ((val & 0x04) && !(old & 0x04)) {
                /* SRST asserted – enter reset state */
                int ch = (bar == 1) ? 0 : 1;
                uint8_t *cmd = ch ? s->ide_cmd1 : s->ide_cmd0;
                cmd[7] |= 0x80; /* set BSY */
                s->reset_active = true;
                s->reset_channel = ch;
                timer_mod(s->reset_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 2);
            } else if (!(val & 0x04) && (old & 0x04)) {
                /* SRST de-asserted; BSY will clear when timer fires */
            }
            return;
        }
    } else if (bar == 4) {
        if (addr == 0 || addr == 8) { /* BM_COMMAND */
            regs[addr] = val & 0xFF;
            if (val & 0x01) { /* Start DMA */
                /* FIXME: emulate DMA data transfer if needed */
            }
            return;
        } else if (addr == 2 || addr == 10) { /* BM_STATUS */
            /* BM_STATUS: bits 0–1: currently only bit 0 maybe set,
             * bits 2: interrupt, bits 6: DMA capable. Writing 1 clears bits 0–2. */
            uint8_t clear = val & 0x07; /* bits 0,1,2 are WC */
            regs[addr] &= ~clear;
            regs[addr] &= ~(val & 0x07); /* already done */
            /* After clearing interrupt, update IRQ state */
            pcibase_update_irq(s);
            return;
        }
    }

    /* Default: store value */
    regs[addr] = (uint8_t)val;
}

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* ------------------------------------------------------------------ */
/* Custom PCI Configuration Space Overrides                           */
/* ------------------------------------------------------------------ */
static uint32_t cs5536_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint32_t val = 0;

    /* Check if the access falls within the CS5536 IDE config register block. */
    if (addr >= PCI_IDE_CFG && addr < PCI_IDE_CFG + 16) {
        int reg = (addr - PCI_IDE_CFG) >> 2;
        switch (reg) {
        case CFG:
            val = s->cfgraf;
            break;
        case DTC:
            val = s->dtctim;
            break;
        case CAST:
            val = s->casttim;
            break;
        case ETC:
            val = s->etctim;
            break;
        default:
            val = 0;
            break;
        }
        return val;
    }

    /* Fallback to default handler for standard registers */
    return pci_default_read_config(pdev, addr, len);
}

static void cs5536_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (addr >= PCI_IDE_CFG && addr < PCI_IDE_CFG + 16) {
        int reg = (addr - PCI_IDE_CFG) >> 2;
        switch (reg) {
        case CFG:
            s->cfgraf = val;
            break;
        case DTC:
            s->dtctim = val;
            break;
        case CAST:
            s->casttim = val;
            break;
        case ETC:
            s->etctim = val;
            break;
        default:
            break;
        }
        return;
    }

    pci_default_write_config(pdev, addr, val, len);
}

/* ------------------------------------------------------------------ */
/* Reset Handler                                                      */
/* ------------------------------------------------------------------ */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Reset CS5536 custom config registers */
    s->cfgraf = IDE_CFG_CHANEN; /* Channels enabled */
    s->dtctim = 0;
    s->casttim = 0;
    s->etctim = 0;

    /* Reset IDE register shadows */
    memset(s->ide_cmd0, 0, sizeof(s->ide_cmd0));
    memset(s->ide_ctl0, 0, sizeof(s->ide_ctl0));
    memset(s->ide_cmd1, 0, sizeof(s->ide_cmd1));
    memset(s->ide_ctl1, 0, sizeof(s->ide_ctl1));
    memset(s->ide_bmdma, 0, sizeof(s->ide_bmdma));

    /* Set default status: DRDY and DSC */
    s->ide_cmd0[7] = 0x50;
    s->ide_cmd1[7] = 0x50;

    /* BMDMA: mark DMA capable (bit 6 in BM_STATUS) */
    s->ide_bmdma[2]  = 0x20;   /* primary BM_STATUS */
    s->ide_bmdma[10] = 0x20;   /* secondary BM_STATUS */

    /* Cancel any pending soft reset */
    if (s->reset_active) {
        timer_del(s->reset_timer);
        s->reset_active = false;
    }
}

/* ------------------------------------------------------------------ */
/* BAR Registration Helper                                            */
/* ------------------------------------------------------------------ */
static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    /* Initialize BAR context as opaque for the PIO handlers */
    s->bar_ctx[bi->index].state = s;
    s->bar_ctx[bi->index].bar_index = bi->index;

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, &s->bar_ctx[bi->index], bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, &s->bar_ctx[bi->index], bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

/* ------------------------------------------------------------------ */
/* Device Realize                                                     */
/* ------------------------------------------------------------------ */
static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_AMD );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_AMD_CS5536_IDE );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_STORAGE_IDE );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);
    /* Set prog_if for dual-channel BMDMA capable */
    pci_set_byte(pci_conf + PCI_CLASS_PROG, 0x8A);

    /* Remove PCIe and PM capabilities – CS5536 is a conventional PCI device */
    /* No DMA, MSI/MSI-X, or other advanced features required */

    /* Setup BARs as PIO, matching the real hardware interface */
    s->num_bars = 5;
    s->bar_info[0] = (BARInfo){ .index=0, .type=BAR_TYPE_PIO, .size=8,  .name="ide-cmd0" };
    s->bar_info[1] = (BARInfo){ .index=1, .type=BAR_TYPE_PIO, .size=4,  .name="ide-ctl0" };
    s->bar_info[2] = (BARInfo){ .index=2, .type=BAR_TYPE_PIO, .size=8,  .name="ide-cmd1" };
    s->bar_info[3] = (BARInfo){ .index=3, .type=BAR_TYPE_PIO, .size=4,  .name="ide-ctl1" };
    s->bar_info[4] = (BARInfo){ .index=4, .type=BAR_TYPE_PIO, .size=16, .name="ide-bmdma" };

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize timer for soft reset emulation */
    s->reset_timer = timer_new_ms(QEMU_CLOCK_VIRTUAL, cs5536_reset_timer, s);
    s->reset_active = false;

    /* Set initial register values (same as reset) */
    pcibase_reset(DEVICE(s));
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    timer_del(s->reset_timer);
    timer_free(s->reset_timer);

    /* Standard cleanup */
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "pata_cs5536_pci",
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
    k->config_read = cs5536_config_read;
    k->config_write = cs5536_config_write;
    dc->reset  = pcibase_reset;
    dc->vmsd   = &vmstate_pcibase;
    set_bit(DEVICE_CATEGORY_MISC, &dc->categories);
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
