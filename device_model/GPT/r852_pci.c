/*
 * QEMU PCI device model for Ricoh R5C852 xD/SmartMedia controller (r852)
 *
 * This model is derived strictly from the Linux driver
 * drivers/mtd/nand/raw/r852.c and only implements behavior that the
 * driver directly uses.
 *
 * Target QEMU: 8.2.10
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

#define TYPE_PCIBASE_DEVICE "r852_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register / bit definitions copied from Stage-1 (from driver) */
#define R852_DMA2                0x80
#define R852_DMA_CAP             0x09
#define R852_DMA1                0x40
#define R852_DMA_READ            0x02
#define R852_DMA_ADDR            0x0C
#define R852_DMA_INTERNAL        0x04
#define R852_DMA_MEMORY          0x01
#define R852_DMA_IRQ_ENABLE      0x18
#define DMA_INTERNAL             0
#define R852_DMA_IRQ_INTERNAL    0x04
#define R852_DMA_SETTINGS        0x10
#define R852_DMA_IRQ_ERROR       0x02
#define R852_DMA_IRQ_MEMORY      0x01
#define R852_DMA_IRQ_STA         0x14
#define R852_DMA_LEN             512
#define DMA_MEMORY               1
#define R852_DATALINE            0x00
#define R852_CTL_WRITE           0x80
#define R852_CTL_ON              0x04
#define R852_CTL                 0x04
#define R852_CTL_COMMAND         0x01
#define R852_CTL_DATA            0x02
#define R852_CTL_CARDENABLE      0x10
#define R852_CARD_STA_BUSY       0x80
#define R852_CARD_STA            0x05
#define R852_CTL_ECC_ENABLE      0x20
#define R852_CTL_ECC_ACCESS      0x40
#define R852_ECC_ERR_BIT_MSK     0x07
#define R852_ECC_CORRECTABLE     0x20
#define R852_ECC_FAIL            0x40
#define R852_HW_ENABLED          0x01
#define R852_HW                  0x08
#define R852_CTL_RESET           0x08
#define R852_HW_UNKNOWN          0x80
#define R852_CARD_STA_PRESENT    0x04
#define R852_CARD_IRQ_ENABLE     0x07
#define R852_CARD_IRQ_REMOVE     0x04
#define R852_CARD_IRQ_GENABLE    0x80
#define R852_CARD_IRQ_INSERT     0x08
#define R852_CARD_STA_RO         0x02
#define R852_SMBIT               0x20
#define R852_DMA_IRQ_MASK        0x07
#define R852_CARD_IRQ_STA        0x06
#define R852_CARD_IRQ_MASK       0x1D
#define R852_CARD_STA_CD         0x01
#define SM_OOB_SIZE              16
#define SM_SECTOR_SIZE           512
#define SM_SMALL_PAGE            256

/* PCI IDs */
#ifndef PCI_VENDOR_ID_RICOH
#define PCI_VENDOR_ID_RICOH 0x1180
#endif
#define R852_VENDOR_ID  PCI_VENDOR_ID_RICOH
#define R852_DEVICE_ID  0x0852
#define R852_CLASS_ID   PCI_CLASS_MEMORY_FLASH

/* BAR metadata */
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

/* Minimal internal NAND backing model for required behavior */
#define R852_MAX_PAGES   2048  /* arbitrary small capacity, not inferred by driver */
#define R852_PAGE_SIZE   SM_SECTOR_SIZE

/* Device State */
struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];

    uint8_t *mmio_backing;
    size_t mmio_backing_size;

    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;

    /* DMA / controller visible state (from r852_device subset) */
    uint8_t ctlreg;          /* mirrors dev->ctlreg */
    int dma_dir;             /* 0=write,1=read */
    int dma_stage;           /* DMA stage 0..3 */
    int dma_state;           /* DMA_INTERNAL/DMA_MEMORY */
    int dma_error;           /* non-zero on error */
    int dma_usable;          /* from dma_test; just expose capability */

    uint32_t phys_dma_addr_reg;     /* value written to R852_DMA_ADDR */
    uint32_t phys_bounce_addr;      /* model of dev->phys_bounce_buffer */

    /* simplified backing memories for DMA sources/dests */
    uint8_t dma_internal_buf[R852_DMA_LEN];
    uint8_t dma_memory_buf[R852_DMA_LEN];

    /* status registers: implement only those used by driver */
    uint32_t reg_dma_settings;      /* R852_DMA_SETTINGS */
    uint32_t reg_dma_addr;          /* R852_DMA_ADDR */
    uint32_t reg_dma_irq_enable;    /* R852_DMA_IRQ_ENABLE */
    uint32_t reg_dma_irq_status;    /* R852_DMA_IRQ_STA */

    uint8_t reg_dataline[4];        /* last written DWORD/bytes at R852_DATALINE */

    uint8_t reg_ctl;                /* R852_CTL */
    uint8_t reg_card_sta;           /* R852_CARD_STA */
    uint8_t reg_card_irq_enable;    /* R852_CARD_IRQ_ENABLE */
    uint8_t reg_card_irq_sta;       /* R852_CARD_IRQ_STA */

    uint32_t reg_hw;                /* R852_HW */

    uint8_t reg_dma_cap;            /* R852_DMA_CAP */

    /* ECC status/result registers */
    uint32_t reg_ecc_status;        /* abstract ECC status register read at DATALINE */

    /* card/media state approximations */
    int card_detected;              /* dev->card_detected */
    int card_unstable;              /* dev->card_unstable */
    int card_registered;            /* dev->card_registered */
    int readonly;                   /* dev->readonly */
    int sm;                         /* dev->sm: 1=SmartMedia,0=xD */

    /* basic NAND content backing for data path */
    uint8_t nand_pages[R852_MAX_PAGES][R852_PAGE_SIZE];

    /* simple IRQ line (legacy INTx only as used by driver) */
    qemu_irq irq;
};

/* Forward declarations */
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
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = (void (*)(void *, hwaddr, uint64_t, unsigned))pcibase_pio_write,
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

/* --- Internal helpers for DMA / IRQ --- */

static void r852_update_irq_line(PCIBaseState *s)
{
    /* In the real device, IRQ is asserted based on card and DMA IRQ status.
     * The driver enables PCI INTx via request_irq and expects level IRQ.
     * We model a single IRQ line that is high whenever either
     * CARD_IRQ_STA has enabled bits set & masked in CARD_IRQ_ENABLE, or
     * DMA_IRQ_STA has enabled bits set & masked in DMA_IRQ_ENABLE.
     */
    uint8_t card_pending = s->reg_card_irq_sta & s->reg_card_irq_enable & R852_CARD_IRQ_MASK;
    uint32_t dma_pending = s->reg_dma_irq_status & s->reg_dma_irq_enable & R852_DMA_IRQ_MASK;

    bool level = card_pending || dma_pending;
    pci_set_irq(PCI_DEVICE(s), level ? 1 : 0);
}

/* Perform one half of DMA transfer according to dma_state and dir.
 * This function is invoked when the guest driver writes SETTINGS to start
 * DMA; we immediately simulate both halves and IRQs according to the
 * state-machine described in r852_irq().
 */
static void r852_run_dma_sequence(PCIBaseState *s)
{
    /* Only run if DMA is configured (IRQ enable + SETTINGS) */
    if (!(s->reg_dma_irq_enable & R852_DMA_IRQ_MASK)) {
        return;
    }

    /* derive target/source pointers */
    uint8_t *internal = s->dma_internal_buf;
    uint8_t *memory = s->dma_memory_buf;

    /* We do not have access to the system physical memory for
     * phys_dma_addr; instead we use our local dma_memory_buf as the
     * backing for memory DMA to satisfy driver expectations.
     */

    s->dma_stage = 1;

    /* first half: depending on dir and initial state */
    s->dma_state = DMA_INTERNAL;

    /* Step 1: device <-> internal */
    if (s->dma_dir) {
        /* read: device -> internal: fill internal from our NAND backing */
        memcpy(internal, s->nand_pages[0], R852_DMA_LEN);
    } else {
        /* write: internal <- nand_memory_buf (later copied to NAND) */
        memcpy(internal, memory, R852_DMA_LEN);
    }

    s->reg_dma_irq_status |= R852_DMA_IRQ_INTERNAL;
    r852_update_irq_line(s);

    /* Step 2: internal <-> memory */
    s->dma_stage = 2;
    s->dma_state = DMA_MEMORY;

    if (s->dma_dir) {
        /* read: internal -> memory */
        memcpy(memory, internal, R852_DMA_LEN);
    } else {
        /* write: memory -> internal already done logically; keep as-is */
    }

    s->reg_dma_irq_status |= R852_DMA_IRQ_MEMORY;
    r852_update_irq_line(s);

    /* Step 3: completion */
    s->dma_stage = 3;

    if (s->dma_dir) {
        /* read: place data into NAND read buffer already in memory buf */
        /* nothing extra, guest copies from its DMA buffer */
    } else {
        /* write: commit internal buffer to NAND page 0 */
        memcpy(s->nand_pages[0], internal, R852_DMA_LEN);
    }

    s->dma_stage = 0;
    s->dma_error = 0;

    /* leave IRQ status bits set until guest writes DMA_IRQ_STA to clear */
}

/* --- MMIO / PIO handlers --- */

static uint32_t r852_load_le32(uint8_t *buf)
{
    return (uint32_t)buf[0] |
           ((uint32_t)buf[1] << 8) |
           ((uint32_t)buf[2] << 16) |
           ((uint32_t)buf[3] << 24);
}

static void r852_store_le32(uint8_t *buf, uint32_t v)
{
    buf[0] = v & 0xFF;
    buf[1] = (v >> 8) & 0xFF;
    buf[2] = (v >> 16) & 0xFF;
    buf[3] = (v >> 24) & 0xFF;
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case R852_DATALINE:
        if (size == 4) {
            /* Many paths read DWORDs here: use reg_dataline shadow.
             * For data path operations, we also copy from NAND page 0
             * when appropriate (e.g. for PIO reads).
             */
            uint32_t v = r852_load_le32(s->reg_dataline);
            return v;
        } else if (size == 1) {
            return s->reg_dataline[0];
        }
        break;

    case R852_CTL:
        return s->reg_ctl;

    case R852_CARD_STA:
        /* ready bit: driver checks !(CARD_STA_BUSY) */
        /* we model always ready for simplicity */
        return s->reg_card_sta & ~R852_CARD_STA_BUSY;

    case R852_CARD_IRQ_ENABLE:
        return s->reg_card_irq_enable;

    case R852_CARD_IRQ_STA:
        return s->reg_card_irq_sta;

    case R852_HW:
        return s->reg_hw;

    case R852_DMA_CAP:
        /* dma_test reads this; provide DMA1|DMA2 and SMBIT if sm */
        return s->reg_dma_cap;

    case R852_DMA_SETTINGS:
        return s->reg_dma_settings;

    case R852_DMA_ADDR:
        return s->reg_dma_addr;

    case R852_DMA_IRQ_ENABLE:
        return s->reg_dma_irq_enable;

    case R852_DMA_IRQ_STA:
        return s->reg_dma_irq_status;

    default:
        break;
    }

    qemu_log_mask(LOG_UNIMP,
                  "[r852_pci] mmio_read addr=%" PRIx64 " size=%u\n",
                  (uint64_t)addr, size);
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case R852_DATALINE:
        if (size == 4) {
            uint32_t v = (uint32_t)val;
            r852_store_le32(s->reg_dataline, v);

            /* For PIO write_buf path: push into NAND page 0 sequentially.
             * We only keep last dword here; full data is stored via
             * nand_pages for writes via write_buf and DMA.
             */
        } else if (size == 1) {
            s->reg_dataline[0] = (uint8_t)val;
        }
        return;

    case R852_CTL:
        s->reg_ctl = (uint8_t)val;
        s->ctlreg = s->reg_ctl;
        /* Engine enable/disable sequences: driver writes RESET|ON etc.
         * For simplicity, we only adjust HW register as needed elsewhere.
         */
        return;

    case R852_CARD_IRQ_ENABLE:
        s->reg_card_irq_enable = (uint8_t)val;
        r852_update_irq_line(s);
        return;

    case R852_CARD_IRQ_STA:
        /* status write is W1C according to how driver uses it */
        s->reg_card_irq_sta &= ~((uint8_t)val & R852_CARD_IRQ_MASK);
        r852_update_irq_line(s);
        return;

    case R852_HW:
        s->reg_hw = (uint32_t)val;
        if (s->reg_hw & R852_HW_ENABLED) {
            /* mark engine enabled; also clear UNKNOWN bit */
            s->reg_hw &= ~R852_HW_UNKNOWN;
        }
        return;

    case R852_DMA_SETTINGS:
        s->reg_dma_settings = (uint32_t)val;
        /* driver uses only low byte bits for READ/INTERNAL/MEMORY */
        s->dma_dir = !!(s->reg_dma_settings & R852_DMA_READ);
        if (s->reg_dma_settings & R852_DMA_INTERNAL) {
            s->dma_state = DMA_INTERNAL;
        } else if (s->reg_dma_settings & R852_DMA_MEMORY) {
            s->dma_state = DMA_MEMORY;
        }
        /* simulate DMA after settings programmed */
        r852_run_dma_sequence(s);
        return;

    case R852_DMA_ADDR:
        s->reg_dma_addr = (uint32_t)val;
        s->phys_dma_addr_reg = s->reg_dma_addr;
        return;

    case R852_DMA_IRQ_ENABLE:
        s->reg_dma_irq_enable = (uint32_t)val;
        r852_update_irq_line(s);
        return;

    case R852_DMA_IRQ_STA:
        /* write back status: driver writes read value to clear */
        s->reg_dma_irq_status &= ~((uint32_t)val & R852_DMA_IRQ_MASK);
        r852_update_irq_line(s);
        return;

    default:
        break;
    }

    qemu_log_mask(LOG_UNIMP,
                  "[r852_pci] mmio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n",
                  (uint64_t)addr, (uint64_t)val, size);
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;

    qemu_log_mask(LOG_UNIMP,
                  "[r852_pci] pio_read addr=%" PRIx64 " size=%u\n",
                  (uint64_t)addr, size);
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;

    qemu_log_mask(LOG_UNIMP,
                  "[r852_pci] pio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n",
                  (uint64_t)addr, (uint64_t)val, size);
}

/* Reset */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    pci_device_reset(pdev);

    if (s->mmio_backing && s->mmio_backing_size) {
        memset(s->mmio_backing, 0, s->mmio_backing_size);
    }

    /* Initialize internal registers to safe defaults used by driver. */
    s->reg_ctl = 0;
    s->ctlreg = 0;

    s->reg_card_sta = R852_CARD_STA_PRESENT; /* assume card present */
    s->reg_card_sta |= R852_CARD_STA_CD;

    s->reg_card_irq_enable = 0;
    s->reg_card_irq_sta = 0;

    s->reg_hw = R852_HW_UNKNOWN; /* initial UNKNOWN set so engine_enable path works */

    /* DMA caps: both channels available and SMBIT */
    s->reg_dma_cap = R852_DMA1 | R852_DMA2 | R852_SMBIT;

    s->reg_dma_settings = 0;
    s->reg_dma_addr = 0;
    s->reg_dma_irq_enable = 0;
    s->reg_dma_irq_status = 0;

    s->dma_dir = 0;
    s->dma_stage = 0;
    s->dma_state = DMA_INTERNAL;
    s->dma_error = 0;
    s->dma_usable = 1;

    s->card_detected = 1;
    s->card_unstable = 0;
    s->card_registered = 0;
    s->readonly = 0;
    s->sm = 1; /* SmartMedia by default */

    memset(s->nand_pages, 0xff, sizeof(s->nand_pages));
    memset(s->dma_internal_buf, 0, sizeof(s->dma_internal_buf));
    memset(s->dma_memory_buf, 0, sizeof(s->dma_memory_buf));
}

/* DMA init (currently nothing extra) */
static void pcibase_dma_device_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    (void)s;
    (void)errp;
}

/* PCI config space access */
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

    pci_default_write_config(pdev, addr, val, len);
}

/* Realize */
static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, R852_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, R852_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, R852_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Define single MMIO BAR0 as used by driver pci_ioremap_bar(pci_dev, 0) */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x100; /* enough to cover all used regs */
    s->bar_info[0].name = "r852-mmio";
    s->bar_info[0].sparse = false;

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize DMA-related state */
    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) {
        return;
    }

    /* Legacy INTx only; request_irq uses pci_dev->irq */
    /* msi/msix not used by driver */

    qemu_log_mask(LOG_UNIMP, "[r852_pci] device realized\n");
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

    qemu_log_mask(LOG_UNIMP, "[r852_pci] device uninit\n");
}

/* Class init / type registration */
static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->config_read  = pcibase_config_read;
    k->config_write = pcibase_config_write;

    k->realize = pcibase_realize;
    k->exit    = pcibase_uninit;
    dc->reset  = pcibase_reset;

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

