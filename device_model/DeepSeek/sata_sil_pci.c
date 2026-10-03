/*
 * QEMU SIL3112 SATA controller device model
 * Based on Linux driver sata_sil.c and SiI3112 datasheet
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

#define TYPE_PCIBASE_DEVICE "sata_sil_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI Identifiers */
#define PCI_VENDOR_ID_SIL 0x1095
#define PCI_DEVICE_ID_SIL_3112 0x3112
#define PCI_CLASS_STORAGE_SATA 0x0106

/* Global register offsets */
#define SIL_SYSCFG          0x48

/* System configuration register bits */
#define SIL_MASK_IDE0_INT   0x02
#define SIL_MASK_IDE1_INT   0x04
#define SIL_MASK_IDE2_INT   0x08
#define SIL_MASK_IDE3_INT   0x10

/* BMDMA2 register bits (per port extended DMA control/status) */
#define SIL_DMA_COMPLETE    0x02
#define SIL_DMA_SATA_IRQ    0x08
#define SIL_DMA_ERROR       0x10

/* BMDMA control bits (standard) */
#define SIL_DMA_ENABLE      0x01

/* BMDMA2 control bits */
#define SIL_DMA_START       0x01
#define SIL_DMA_WR          0x08

/* SFIS configuration bits */
#define SIL_INTR_STEERING   0x10

/* SATA Interrupt Enable value */
#define SIL_SIEN_N          0x00040000

/* Port flags used in driver */
#define SIL_FLAG_NO_SATA_IRQ    (1 << 8)
#define SIL_FLAG_RERR_ON_DMA_ACT (1 << 9)

/* ATA standard commands */
#define ATA_CMD_IDENTIFY    0xEC

/* ATA status bits */
#define ATA_BUSY            0x80
#define ATA_DRQ             0x08
#define ATA_ERR             0x01

/* ATA device control bits */
#define ATA_CTL_NIEN        0x02

/* BMDMA registers offsets within port bmdma region */
#define ATA_DMA_TABLE_OFS   4

/* PRD flags */
#define ATA_PRD_EOT         0x80000000

/* DMA mask */
#define ATA_DMA_MASK        0xffffffff  /* 32-bit */

/* Number of ports for 3112 */
#define SIL_MAX_PORTS       2

/* MMIO region size */
#define SIL_MMIO_SIZE       0x400

/* Define port offsets from driver */
struct sil_port_offset {
    unsigned long tf;
    unsigned long ctl;
    unsigned long bmdma;
    unsigned long bmdma2;
    unsigned long fifo_cfg;
    unsigned long scr;
    unsigned long sien;
    unsigned long xfer_mode;
    unsigned long sfis_cfg;
};

/* Standard ATA taskfile registers (8 bytes) */
typedef struct {
    uint8_t data;
    uint8_t feature;
    uint8_t nsect;
    uint8_t lbal;
    uint8_t lbam;
    uint8_t lbah;
    uint8_t device;
    uint8_t command;
} ATATaskFile;

typedef struct SilPortState {
    /* Task file registers */
    ATATaskFile tf;
    uint8_t ctl;           /* control register (altstatus when read) */

    /* BMDMA state */
    uint32_t bmdma2;       /* extended DMA status/control */
    uint32_t bmdma_prd;    /* PRD table address */
    bool dma_active;

    /* Other registers */
    uint32_t sien;
    uint32_t xfer_mode;
    uint32_t sfis_cfg;
    uint16_t fifo_cfg;

    /* DMA completion timer */
    QEMUTimer *dma_timer;

    /* ATA command being processed */
    uint8_t command_pending;
} SilPortState;

typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM
} BARType;

typedef struct BARInfo {
    int index;
    BARType type;
    hwaddr size;
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
    uint32_t irq_status;
    uint32_t irq_mask;

    /* Global system config register (IRQ mask) */
    uint32_t syscfg;

    /* Per-port state */
    SilPortState ports[SIL_MAX_PORTS];

    /* Port offset array (from driver) */
    struct sil_port_offset port_offsets[SIL_MAX_PORTS];
};

/* Default IDENTIFY data for a basic ATA disk (minimal valid structure) */
static const uint16_t sil_identify_data[256] = {
    [0] = 0x0040,                  /* CF: configuration word, non-removable */
    [1] = 0xffff,                  /* obsolete */
    [3] = 0x0010,                  /* heads (default) */
    [6] = 0x003f,                  /* sectors */
    [47] = 0x8000,                 /* max sectors per DRQ (multi) */
    [49] = 0x0f00,                 /* capabilities: LBA, DMA */
    [50] = 0x4000,                 /* capabilities */
    [53] = 0x0007,                 /* words 54-58 valid */
    [54] = 0x3fff,                 /* cylinders */
    [57] = 0x0010,                 /* heads */
    [58] = 0x003f,                 /* sectors */
    [59] = 0x0100,                 /* multi-sector */
    [60] = 0x00a0,                 /* LBA capacity ... */
    [61] = 0x3fff,                 /* ... total 4194303 sectors */
    [63] = 0x0007,                 /* multiword DMA */
    [64] = 0x0003,                 /* PIO modes */
    [65] = 0x0078,                 /* minimum DMA cycle time */
    [66] = 0x0078,                 /* recommended DMA cycle time */
    [67] = 0x0078,                 /* minimum PIO cycle time without flow control */
    [68] = 0x0078,                 /* minimum PIO cycle time with IORDY */
    [80] = 0x007e,                 /* major version */
    [81] = 0x0022,                 /* minor version */
    [82] = 0x346b,                 /* command set supported */
    [83] = 0x7d09,                 /* command sets supported */
    [84] = 0x4000,                 /* additional features */
    [85] = 0x0029,                 /* command set/feature enabled */
    [86] = 0x0001,                 /* command set/feature enabled */
    [87] = 0x4000,                 /* additional features enabled */
    [88] = 0x203f,                 /* ultra DMA modes */
    [93] = 0x0001,                 /* hardware reset result */
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool intr = false;

    for (int i = 0; i < SIL_MAX_PORTS; i++) {
        uint32_t mask_bit = (SIL_MASK_IDE0_INT << i);
        if ((s->syscfg & mask_bit) == 0) {
            if (s->ports[i].bmdma2 & (SIL_DMA_COMPLETE | SIL_DMA_SATA_IRQ)) {
                intr = true;
                break;
            }
        }
    }

    pci_set_irq(pdev, intr ? 1 : 0);
}

static void sil_dma_timer_cb(void *opaque)
{
    PCIBaseState *s = opaque;
    int port = -1;
    /* Find which port triggered this timer (set in timer_opaque) */
    for (int i = 0; i < SIL_MAX_PORTS; i++) {
        if (s->ports[i].dma_timer && timer_pending(s->ports[i].dma_timer)) {
            port = i;
            break;
        }
    }
    if (port < 0) return;

    PCIDevice *pdev = PCI_DEVICE(s);
    SilPortState *ps = &s->ports[port];

    if (!ps->dma_active) {
        return;
    }

    /* DMA transfer from device to memory (read) */
    bool is_write = (ps->bmdma2 & SIL_DMA_WR) != 0;
    dma_addr_t prd_addr = ps->bmdma_prd;
    uint32_t prd[2];
    uint32_t xfer_remaining = 512;  /* IDENTIFY data size */
    uint32_t data_offset = 0;

    /* Walk PRD table */
    while (xfer_remaining > 0) {
        pci_dma_read(pdev, prd_addr, &prd, sizeof(prd));
        uint32_t addr = le32_to_cpu(prd[0]);
        uint32_t flags_len = le32_to_cpu(prd[1]);
        uint32_t len = flags_len & 0xffff;
        bool eot = (flags_len & ATA_PRD_EOT) != 0;

        if (len == 0) len = 0x10000;  /* 64K */
        if (len > xfer_remaining) len = xfer_remaining;

        if (!is_write) {
            /* Device to memory */
            pci_dma_write(pdev, addr, (uint8_t *)sil_identify_data + data_offset, len);
        }

        data_offset += len;
        xfer_remaining -= len;
        if (eot) break;
        prd_addr += 8;
    }

    /* Mark DMA complete, set status */
    ps->bmdma2 |= SIL_DMA_COMPLETE;
    ps->dma_active = false;

    /* Update taskfile status: clear BSY, set DRQ, assert INTRQ */
    ps->tf.command = 0x50;  /* status: DRQ, no error */

    /* Raise interrupt if PCI not masked */
    pcibase_update_irq(s);
}

static void sil_handle_command(PCIBaseState *s, int port, uint8_t cmd)
{
    SilPortState *ps = &s->ports[port];

    switch (cmd) {
    case ATA_CMD_IDENTIFY:
        ps->command_pending = cmd;
        ps->tf.command = ATA_BUSY;  /* device busy */
        /* If DMA is active, start timer for transfer */
        if (ps->dma_active && (ps->bmdma2 & SIL_DMA_START)) {
            timer_mod(ps->dma_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 1);
        }
        break;
    default:
        ps->command_pending = 0;
        ps->tf.command = ATA_ERR;
        break;
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    /* Determine port and register type from address */
    int port = -1;
    SilPortState *ps = NULL;
    const struct sil_port_offset *offsets = s->port_offsets;

    for (int i = 0; i < SIL_MAX_PORTS; i++) {
        if (addr >= offsets[i].tf && addr < offsets[i].tf + 8) {
            port = i;
            break;
        }
        if (addr == offsets[i].ctl) {
            port = i;
            break;
        }
        if (addr >= offsets[i].bmdma && addr < offsets[i].bmdma + 8) {
            port = i;
            break;
        }
        if (addr >= offsets[i].bmdma2 && addr < offsets[i].bmdma2 + 4) {
            port = i;
            break;
        }
        if (addr == offsets[i].fifo_cfg) {
            port = i;
            break;
        }
        if (addr >= offsets[i].scr && addr < offsets[i].scr + 12) {
            port = i;
            break;
        }
        if (addr == offsets[i].sien) {
            port = i;
            break;
        }
        if (addr == offsets[i].xfer_mode) {
            port = i;
            break;
        }
        if (addr == offsets[i].sfis_cfg) {
            port = i;
            break;
        }
    }

    /* Global registers */
    if (addr == SIL_SYSCFG) {
        return s->syscfg;
    }

    if (port >= 0 && port < SIL_MAX_PORTS) {
        ps = &s->ports[port];

        /* Task file registers */
        if (addr >= offsets[port].tf && addr < offsets[port].tf + 8) {
            int reg = addr - offsets[port].tf;
            switch (reg) {
            case 0: return ps->tf.data;
            case 1: return ps->tf.feature;
            case 2: return ps->tf.nsect;
            case 3: return ps->tf.lbal;
            case 4: return ps->tf.lbam;
            case 5: return ps->tf.lbah;
            case 6: return ps->tf.device;
            case 7: return ps->tf.command;
            }
        }

        /* Control register */
        if (addr == offsets[port].ctl) {
            if (size == 1) {
                return ps->ctl;
            }
        }

        /* BMDMA registers (standard) */
        if (addr >= offsets[port].bmdma && addr < offsets[port].bmdma + 4) {
            int reg = addr - offsets[port].bmdma;
            if (reg == ATA_DMA_TABLE_OFS) {
                return ps->bmdma_prd;
            }
            /* other bmdma regs not used */
            return 0;
        }

        /* BMDMA2 register (32-bit) */
        if (addr >= offsets[port].bmdma2 && addr < offsets[port].bmdma2 + 4) {
            return ps->bmdma2;
        }

        /* FIFO config */
        if (addr == offsets[port].fifo_cfg) {
            return ps->fifo_cfg;
        }

        /* SCR registers (SStatus, SError, SControl) */
        if (addr >= offsets[port].scr && addr < offsets[port].scr + 12) {
            /* Return 0 for all, no SATA link up */
            return 0;
        }

        /* SIEN */
        if (addr == offsets[port].sien) {
            return ps->sien;
        }

        /* Xfer mode */
        if (addr == offsets[port].xfer_mode) {
            return ps->xfer_mode;
        }

        /* SFIS config */
        if (addr == offsets[port].sfis_cfg) {
            return ps->sfis_cfg;
        }
    }

    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    int port = -1;
    SilPortState *ps = NULL;
    const struct sil_port_offset *offsets = s->port_offsets;

    for (int i = 0; i < SIL_MAX_PORTS; i++) {
        if (addr >= offsets[i].tf && addr < offsets[i].tf + 8) {
            port = i;
            break;
        }
        if (addr == offsets[i].ctl) {
            port = i;
            break;
        }
        if (addr >= offsets[i].bmdma && addr < offsets[i].bmdma + 8) {
            port = i;
            break;
        }
        if (addr >= offsets[i].bmdma2 && addr < offsets[i].bmdma2 + 4) {
            port = i;
            break;
        }
        if (addr == offsets[i].fifo_cfg) {
            port = i;
            break;
        }
        if (addr >= offsets[i].scr && addr < offsets[i].scr + 12) {
            port = i;
            break;
        }
        if (addr == offsets[i].sien) {
            port = i;
            break;
        }
        if (addr == offsets[i].xfer_mode) {
            port = i;
            break;
        }
        if (addr == offsets[i].sfis_cfg) {
            port = i;
            break;
        }
    }

    /* Global system config */
    if (addr == SIL_SYSCFG) {
        s->syscfg = val;
        pcibase_update_irq(s);
        return;
    }

    if (port >= 0 && port < SIL_MAX_PORTS) {
        ps = &s->ports[port];

        /* Task file registers */
        if (addr >= offsets[port].tf && addr < offsets[port].tf + 8) {
            int reg = addr - offsets[port].tf;
            switch (reg) {
            case 0: ps->tf.data = val; break;
            case 1: ps->tf.feature = val; break;
            case 2: ps->tf.nsect = val; break;
            case 3: ps->tf.lbal = val; break;
            case 4: ps->tf.lbam = val; break;
            case 5: ps->tf.lbah = val; break;
            case 6: ps->tf.device = val; break;
            case 7: /* command write triggers action */
                if (ps->dma_active && (ps->bmdma2 & SIL_DMA_START)) {
                    /* DMA in progress, need to wait for completion? ignore */
                } else {
                    sil_handle_command(s, port, val);
                }
                break;
            }
            return;
        }

        /* Control register (write - nIEN, etc.) */
        if (addr == offsets[port].ctl) {
            ps->ctl = val;
            return;
        }

        /* BMDMA registers (PRD table address) */
        if (addr == offsets[port].bmdma + ATA_DMA_TABLE_OFS) {
            ps->bmdma_prd = val;
            return;
        }

        /* BMDMA2 register (start/stop, direction) */
        if (addr >= offsets[port].bmdma2 && addr < offsets[port].bmdma2 + 4) {
            uint32_t old = ps->bmdma2;
            ps->bmdma2 = val;
            if (val & SIL_DMA_START) {
                ps->dma_active = true;
                /* If a command was pending, start transfer now */
                if (ps->command_pending == ATA_CMD_IDENTIFY) {
                    timer_mod(ps->dma_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 1);
                }
            } else {
                ps->dma_active = false;
            }
            /* Writing 0 to clear bits? Driver writes 0 to bmdma2 to stop, may clear completion */
            if ((old & SIL_DMA_COMPLETE) && !(val & SIL_DMA_COMPLETE)) {
                ps->bmdma2 &= ~SIL_DMA_COMPLETE;
            }
            if ((old & SIL_DMA_ERROR) && !(val & SIL_DMA_ERROR)) {
                ps->bmdma2 &= ~SIL_DMA_ERROR;
            }
            pcibase_update_irq(s);
            return;
        }

        /* FIFO config */
        if (addr == offsets[port].fifo_cfg) {
            ps->fifo_cfg = val;
            return;
        }

        /* SCR registers (writable SControl and SError clear) */
        if (addr >= offsets[port].scr && addr < offsets[port].scr + 12) {
            int scr_reg = addr - offsets[port].scr;
            if (scr_reg == 0) { /* SControl */
                /* ignore */
            } else if (scr_reg == 8) { /* SError (clear) */
                /* driver writes to SError to clear bits */
            }
            return;
        }

        /* SIEN */
        if (addr == offsets[port].sien) {
            ps->sien = val;
            return;
        }

        /* Xfer mode */
        if (addr == offsets[port].xfer_mode) {
            ps->xfer_mode = val;
            return;
        }

        /* SFIS config */
        if (addr == offsets[port].sfis_cfg) {
            ps->sfis_cfg = val;
            return;
        }
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Reset global registers */
    s->syscfg = 0;  /* all interrupts masked initially? */

    for (int i = 0; i < SIL_MAX_PORTS; i++) {
        memset(&s->ports[i].tf, 0, sizeof(s->ports[i].tf));
        s->ports[i].ctl = 0;
        s->ports[i].bmdma2 = 0;
        s->ports[i].bmdma_prd = 0;
        s->ports[i].dma_active = false;
        s->ports[i].sien = 0;
        s->ports[i].xfer_mode = 0;
        s->ports[i].sfis_cfg = 0;
        s->ports[i].fifo_cfg = 0;
        s->ports[i].command_pending = 0;
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
        /* Not used in this device; PIO ops removed due to being unused */
        g_assert_not_reached();
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_SIL);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_SIL_3112);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_STORAGE_SATA);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* BAR Initialization: BAR5 MMIO */
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo) {
        .index = 5,
        .type = BAR_TYPE_MMIO,
        .size = SIL_MMIO_SIZE,
        .name = "sata_sil_mmio"
    };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize port offsets (as in driver) */
    s->port_offsets[0] = (struct sil_port_offset) { 0x80, 0x8A, 0x00, 0x10, 0x40, 0x100, 0x148, 0xb4, 0x14c };
    s->port_offsets[1] = (struct sil_port_offset) { 0xC0, 0xCA, 0x08, 0x18, 0x44, 0x180, 0x1c8, 0xf4, 0x1cc };

    /* Init per-port timers */
    for (int i = 0; i < SIL_MAX_PORTS; i++) {
        s->ports[i].dma_timer = timer_new_ms(QEMU_CLOCK_VIRTUAL, sil_dma_timer_cb, s);
        s->ports[i].dma_active = false;
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    for (int i = 0; i < SIL_MAX_PORTS; i++) {
        timer_free(s->ports[i].dma_timer);
    }
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

static const VMStateDescription vmstate_pcibase = {
    .name = "sata_sil_pci",
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
