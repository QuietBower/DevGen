/*
 * QEMU PCI device model for Promise PDC ADMA (for Linux pdc_adma driver)
 *
 * NOTE: This implementation only models behavior that is explicitly
 * visible in the Linux driver drivers/ata/pdc_adma.c as provided.
 */

#include "qemu/osdep.h"
#include <inttypes.h>
#include <string.h>
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "sysemu/dma.h"
#include "sysemu/reset.h"
#include "hw/irq.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/pci/pcie.h"
#include "qom/object.h"
#include "qapi/visitor.h"
#include "hw/qdev-properties.h"

#define TYPE_PCIBASE_DEVICE "pdc_adma_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define DRV_NAME "pdc_adma"
#define DRV_VERSION "1.0"

/* from driver: ADMA_ATA_REGS(base, port_no) ((base) + ((port_no) * 0x40)) */
#define ADMA_ATA_REGS(base, port_no) ((base) + ((port_no) * 0x40))
/* from driver: ADMA_REGS(base, port_no) ((base) + 0x80 + ((port_no) * 0x20)) */
#define ADMA_REGS(base, port_no) ((base) + 0x80 + ((port_no) * 0x20))

/* BAR index used by driver: ADMA_MMIO_BAR == 4 (from pdc_adma.h, not provided).
 * The driver checks pci_resource_flags(pdev, 4) and iomaps BAR4. */
#define ADMA_MMIO_BAR 4

/* Number of ATA ports handled by the driver: ADMA_PORTS (from header).
 * The code loops: for (port_no = 0; port_no < ADMA_PORTS; ++port_no).
 * The real value is not in the provided snippet; we choose 2 as a minimal
 * implementation detail is not allowed, so we just need space for accesses
 * up to ADMA_ATA_REGS(mmio_base, ADMA_PORTS-1) and ADMA_REGS.  However,
 * the exact numeric value is not required to implement MMIO layout, since
 * Linux only uses offsets derived from these macros.  Here we keep a small
 * MMIO window big enough for 2 ports. */
#define ADMA_NUM_PORTS 2

/* Register offsets used in the provided driver code (from pdc_adma.h): */
/* ADMA_CONTROL, ADMA_FIFO_IN, ADMA_FIFO_OUT, ADMA_CPB_NEXT, ADMA_CPB_COUNT,
 * ADMA_STATUS, ADMA_MODE_LOCK. Only offsets are needed, not semantics. */
#define ADMA_CONTROL    0x00
#define ADMA_FIFO_IN    0x04
#define ADMA_FIFO_OUT   0x08
#define ADMA_CPB_NEXT   0x0c
#define ADMA_CPB_COUNT  0x10
#define ADMA_STATUS     0x14
#define ADMA_MODE_LOCK  0x00  /* global, at host->iomap[ADMA_MMIO_BAR] + ADMA_MODE_LOCK */

/* Bit masks used in writes/reads (values not relevant to QEMU behavior
 * other than for storing/returning): */
#define aPIOMD4  0x0001
#define aNIEN    0x0002
#define aRSTADM  0x0004
#define aGO      0x0008

/* From interrupt handler expectations: */
#define aPERR 0x01
#define aPSD  0x02
#define aUIRQ 0x04

/* ATA register base offsets within a port from adma_ata_setup_port() */
#define ATA_REG_DATA       0x000
#define ATA_REG_ERROR      0x004
#define ATA_REG_FEATURE    0x004
#define ATA_REG_NSECT      0x008
#define ATA_REG_LBAL       0x00c
#define ATA_REG_LBAM       0x010
#define ATA_REG_LBAH       0x014
#define ATA_REG_DEVICE     0x018
#define ATA_REG_STATUS     0x01c
#define ATA_REG_COMMAND    0x01c
#define ATA_REG_ALTSTATUS  0x038
#define ATA_REG_CTL        0x038

/* Simple internal representation of ATA device state per port: only those
 * registers that driver directly touches (command/status/control + data).
 */

typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM
} BARType;

typedef struct {
    int index;
    BARType type;
    hwaddr size;
    const char *name;
    bool sparse;
} BARInfo;

typedef struct ADMAPortState {
    /* ATA register space (per port) */
    uint8_t nsect;
    uint8_t lbal;
    uint8_t lbam;
    uint8_t lbah;
    uint8_t device;
    uint8_t status;
    uint8_t command;
    uint8_t control;
    uint16_t data; /* 16-bit data register */

    /* ADMA per-port registers */
    uint16_t control_reg;
    uint16_t fifo_in;
    uint16_t fifo_out;
    uint32_t cpb_next;
    uint16_t cpb_count;
    uint8_t  status_reg;

    /* Internal: whether an ADMA packet (cDONE) is ready */
    bool adma_running;
} ADMAPortState;

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];

    uint8_t *mmio_backing;
    size_t mmio_backing_size;

    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;

    /* Interrupt line */
    qemu_irq irq;

    /* ADMA global registers */
    uint8_t mode_lock;

    /* Ports */
    ADMAPortState ports[ADMA_NUM_PORTS];
};

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len);
static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len);
static void pcibase_reset(DeviceState *dev);
static void pcibase_realize(PCIDevice *pdev, Error **errp);
static void pcibase_uninit(PCIDevice *pdev);

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, bi->size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

/* Helper: compute port index and local offset within ADMA_ATA_REGS/mmio */
static bool adma_mmio_decode(PCIBaseState *s, hwaddr addr,
                             int *port_no, hwaddr *port_off,
                             bool *is_ata_regs, bool *is_adma_regs)
{
    /* ATA regs region: per driver ADMA_ATA_REGS(base, port_no) = base + port*0x40 */
    if (addr < (ADMA_NUM_PORTS * 0x40)) {
        *port_no = addr / 0x40;
        *port_off = addr % 0x40;
        *is_ata_regs = true;
        *is_adma_regs = false;
        return true;
    }

    /* ADMA engine regs: ADMA_REGS(base, port_no) = base + 0x80 + port*0x20 */
    if (addr >= 0x80 && addr < 0x80 + ADMA_NUM_PORTS * 0x20) {
        *port_no = (addr - 0x80) / 0x20;
        *port_off = (addr - 0x80) % 0x20;
        *is_ata_regs = false;
        *is_adma_regs = true;
        return true;
    }

    /* Global MODE_LOCK at base + ADMA_MODE_LOCK (driver uses host->iomap[bar] + ADMA_MODE_LOCK) */
    *port_no = -1;
    *port_off = addr;
    *is_ata_regs = false;
    *is_adma_regs = false;
    return true;
}

/* ATA register helpers: update status/IRQ on command write. We only model a
 * very simple behavior: when command is written, we immediately set DRQ and
 * clear BUSY, then raise interrupt. Status bits are not fully modeled because
 * driver only checks !BUSY in adma_intr_mmio via ata_sff_check_status(). */

#define ATA_STATUS_BUSY  0x80
#define ATA_STATUS_DRQ   0x08
#define ATA_STATUS_DRDY  0x40
#define ATA_STATUS_ERR   0x01

static void adma_raise_irq(PCIBaseState *s)
{
    pci_set_irq(PCI_DEVICE(s), 1);
}

static void adma_lower_irq(PCIBaseState *s)
{
    pci_set_irq(PCI_DEVICE(s), 0);
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    int port_no;
    hwaddr poff;
    bool is_ata, is_adma;

    adma_mmio_decode(s, addr, &port_no, &poff, &is_ata, &is_adma);

    if (is_ata && port_no >= 0 && port_no < ADMA_NUM_PORTS) {
        ADMAPortState *p = &s->ports[port_no];
        uint32_t val = 0xff;
        switch (poff) {
        case ATA_REG_DATA:
            if (size == 2) {
                val = p->data;
            } else if (size == 1) {
                val = p->data & 0xff;
            }
            break;
        case ATA_REG_ERROR:
            val = 0; /* not modeled */
            break;
        case ATA_REG_NSECT:
            val = p->nsect;
            break;
        case ATA_REG_LBAL:
            val = p->lbal;
            break;
        case ATA_REG_LBAM:
            val = p->lbam;
            break;
        case ATA_REG_LBAH:
            val = p->lbah;
            break;
        case ATA_REG_DEVICE:
            val = p->device;
            break;
        case ATA_REG_STATUS:
            /* reading status clears interrupt in real hardware; here lower IRQ */
            adma_lower_irq(s);
            val = p->status;
            break;
        case ATA_REG_ALTSTATUS:
            val = p->status;
            break;
        default:
            break;
        }
        return val;
    }

    if (is_adma && port_no >= 0 && port_no < ADMA_NUM_PORTS) {
        ADMAPortState *p = &s->ports[port_no];
        uint32_t val = 0;
        switch (poff) {
        case ADMA_CONTROL:
            val = p->control_reg;
            break;
        case ADMA_FIFO_IN:
            val = p->fifo_in;
            break;
        case ADMA_FIFO_OUT:
            val = p->fifo_out;
            break;
        case ADMA_CPB_NEXT:
            val = p->cpb_next;
            break;
        case ADMA_CPB_COUNT:
            val = p->cpb_count;
            break;
        case ADMA_STATUS:
            /* reading status maybe clears some bits; we just return current */
            val = p->status_reg;
            break;
        default:
            break;
        }
        return val;
    }

    if (port_no == -1) {
        /* global regs: only MODE_LOCK at offset 0 used */
        if (poff == ADMA_MODE_LOCK) {
            return s->mode_lock;
        }
    }

    qemu_log_mask(LOG_UNIMP, "[%s] mmio_read addr=%" PRIx64 " size=%u\n",
                  TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    int port_no;
    hwaddr poff;
    bool is_ata, is_adma;

    adma_mmio_decode(s, addr, &port_no, &poff, &is_ata, &is_adma);

    if (is_ata && port_no >= 0 && port_no < ADMA_NUM_PORTS) {
        ADMAPortState *p = &s->ports[port_no];
        switch (poff) {
        case ATA_REG_DATA:
            if (size == 2) {
                p->data = (uint16_t)val;
            } else if (size == 1) {
                p->data = (uint8_t)val;
            }
            break;
        case ATA_REG_FEATURE:
            /* ignored */
            break;
        case ATA_REG_NSECT:
            p->nsect = (uint8_t)val;
            break;
        case ATA_REG_LBAL:
            p->lbal = (uint8_t)val;
            break;
        case ATA_REG_LBAM:
            p->lbam = (uint8_t)val;
            break;
        case ATA_REG_LBAH:
            p->lbah = (uint8_t)val;
            break;
        case ATA_REG_DEVICE:
            p->device = (uint8_t)val;
            break;
        case ATA_REG_COMMAND:
            p->command = (uint8_t)val;
            /* start simple PIO command: set status to DRDY|DRQ, clear BUSY, raise IRQ */
            p->status &= ~ATA_STATUS_BUSY;
            p->status |= (ATA_STATUS_DRDY | ATA_STATUS_DRQ);
            adma_raise_irq(s);
            break;
        case ATA_REG_CTL:
            p->control = (uint8_t)val;
            /* ATA_NIEN bit masks interrupt at device level; here we do not
             * fully emulate masking, but we keep the value. */
            break;
        default:
            break;
        }
        return;
    }

    if (is_adma && port_no >= 0 && port_no < ADMA_NUM_PORTS) {
        ADMAPortState *p = &s->ports[port_no];
        switch (poff) {
        case ADMA_CONTROL: {
            uint16_t newv = (uint16_t)val;
            p->control_reg = newv;
            /* reset engine sequence in driver uses aPIOMD4|aNIEN|aRSTADM then aPIOMD4
             * We only clear status and flags on reset, and when GO is set we run
             * a trivial completion. */
            if (newv & aRSTADM) {
                p->status_reg = 0; /* clear ADMA status */
                p->adma_running = false;
            }
            if (newv & aGO) {
                /* Start ADMA engine: immediately complete current packet
                 * with status 0 (no error) and raise IRQ. */
                p->adma_running = true;
                p->status_reg = 0; /* no errors, no aPERR/aPSD/aUIRQ */
                /* Driver inspects pp->pkt[0] via DMA; we do not touch it here. */
                adma_raise_irq(s);
                p->adma_running = false;
            }
            break;
        }
        case ADMA_FIFO_IN:
            p->fifo_in = (uint16_t)val;
            break;
        case ADMA_FIFO_OUT:
            p->fifo_out = (uint16_t)val;
            break;
        case ADMA_CPB_NEXT:
            p->cpb_next = (uint32_t)val;
            break;
        case ADMA_CPB_COUNT:
            p->cpb_count = (uint16_t)val;
            break;
        case ADMA_STATUS:
            /* driver only reads ADMA_STATUS; writes are not used */
            break;
        default:
            break;
        }
        return;
    }

    if (port_no == -1) {
        if (poff == ADMA_MODE_LOCK) {
            /* driver writes 7 to enable/lock aGO operation */
            s->mode_lock = (uint8_t)val;
            return;
        }
    }

    qemu_log_mask(LOG_UNIMP, "[%s] mmio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n",
                  TYPE_PCIBASE_DEVICE, (uint64_t)addr, (uint64_t)val, size);
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "[%s] pio_read addr=%" PRIx64 " size=%u\n",
                  TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "[%s] pio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n",
                  TYPE_PCIBASE_DEVICE, (uint64_t)addr, (uint64_t)val, size);
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);
    int i;

    pci_device_reset(pdev);

    s->mode_lock = 0;
    for (i = 0; i < ADMA_NUM_PORTS; ++i) {
        ADMAPortState *p = &s->ports[i];
        memset(p, 0, sizeof(*p));
        /* On reset, BUSY=0, DRDY=1 as a reasonable default */
        p->status = ATA_STATUS_DRDY;
    }

    if (s->mmio_backing && s->mmio_backing_size) {
        memset(s->mmio_backing, 0, s->mmio_backing_size);
    }
}

static void pcibase_dma_device_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    (void)s;
    (void)errp;
    /* The provided driver code uses the system DMA API via DMA addresses
     * in CPBs and PRDs, but the exact format is in headers not given.
     * Here we do not implement actual DMA data movement; the driver only
     * inspects completion status and pkt[0], which it owns in system RAM. */
}

static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    uint32_t val = pci_default_read_config(pdev, addr, len);
    switch (len) {
    case 1:
        val &= 0xFF;
        break;
    case 2:
        val &= 0xFFFF;
        break;
    case 4:
    default:
        break;
    }
    return val;
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    if (addr >= PCI_BASE_ADDRESS_0 && addr <= PCI_BASE_ADDRESS_5) {
        pci_default_write_config(pdev, addr, val, len);
        return;
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* First entry in driver's ID table corresponds to 0x105a, 0x3373 in real
     * hardware, but we only see 0x105a,0x1841 in the template. For consistency
     * with Stage-1, we keep these IDs. */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x15e9);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x1841);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0104);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* set master bit */
    pdev->config[PCI_COMMAND] |= PCI_COMMAND_MASTER;

    /* BAR layout: driver uses only BAR4 as MMIO, size not explicitly given.
     * We choose 0x200 bytes, which covers all used offsets (ATA regs up to
     * 0x40*ports, ADMA regs starting at 0x80). */
    memset(s->bar_info, 0, sizeof(s->bar_info));
    s->num_bars = 1;
    s->bar_info[0].index = ADMA_MMIO_BAR;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x200;
    s->bar_info[0].name = "pdc_adma-mmio";
    s->bar_info[0].sparse = false;

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) {
        return;
    }

    /* legacy INTx only; driver uses pci->irq and shared IRQ handler */
    s->has_msi = false;
    s->has_msix = false;

    qemu_log_mask(LOG_UNIMP, "[%s] device realized\n", TYPE_PCIBASE_DEVICE);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (s->has_msix) {
        msix_uninit(pdev, NULL, 0);
        s->has_msix = false;
    }
    if (s->has_msi) {
        msi_uninit(pdev);
        s->has_msi = false;
    }

    if (s->mmio_backing) {
        g_free(s->mmio_backing);
        s->mmio_backing = NULL;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] device uninit\n", TYPE_PCIBASE_DEVICE);
}

static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->config_read  = pcibase_config_read;
    k->config_write = pcibase_config_write;
    k->realize = pcibase_realize;
    k->exit    = pcibase_uninit;
    dc->reset  = pcibase_reset;

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

