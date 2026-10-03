/*
 * QEMU PCI model for cb7210-based GPIB boards (Community CB7210)
 *
 * This file is generated to match Linux driver drivers/staging/gpib/cb7210/cb7210.c
 * as closely as possible using only information visible in that driver.
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

#define TYPE_PCIBASE_DEVICE "cb7210_pci_driver_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI_VENDOR_ID_CBOARDS 0x1307
#define PCI_VENDOR_ID_QUANCOM 0x8008
#define PCI_DEVICE_ID_QUANCOM_GPIB 0x3302

/* HS_* registers visible in driver */
#define HS_MODE        0x8
#define HS_INT_LEVEL   0x9
#define HS_STATUS      0x8

/* HS_STATUS bits used in driver (values unknown, model with generic bits) */
#define HS_RX_MSB_NOT_EMPTY   0x01
#define HS_RX_LSB_NOT_EMPTY   0x02
#define HS_TX_MSB_NOT_EMPTY   0x04
#define HS_TX_LSB_NOT_EMPTY   0x08
#define HS_HALF_FULL          0x10
#define HS_SRQ_INT            0x20
#define HS_EOI_INT            0x40
#define HS_FIFO_FULL          0x80

/* HS_MODE bits used in driver */
#define HS_RX_ENABLE          0x01
#define HS_TX_ENABLE          0x02
#define HS_CLR_SRQ_INT        0x04
#define HS_CLR_EOI_EMPTY_INT  0x08
#define HS_CLR_HF_INT         0x10
#define HS_HF_INT_EN          0x20
#define HS_SYS_CONTROL        0x40
#define HS_ENABLE_MASK        (HS_RX_ENABLE | HS_TX_ENABLE)

/* Size constants from driver */
static const int cb7210_fifo_size = 2048;
static const int cb7210_reg_offset = 1;

/* FIFO IO registers (DIR/CDOR) are accessed via fifo_iobase + offset.
 * The exact offsets are not given in the driver, but DIR/CDOR are single
 * word ports; for modelling, we place them at offset 0.
 */
#define DIR  0x0
#define CDOR 0x0

/* ------------------------------------------------------------------ */
/* BAR metadata definition                                             */
/* ------------------------------------------------------------------ */
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

/* ------------------------------------------------------------------ */
/* Device State                                                        */
/* ------------------------------------------------------------------ */
struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];

    uint8_t *mmio_backing;
    size_t mmio_backing_size;

    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;

    /* Modelled HS registers and state */
    uint8_t hs_mode_bits;
    uint8_t hs_status;

    /* Simple FIFO model for RX and TX */
    uint8_t rx_fifo[2048];
    uint32_t rx_head;
    uint32_t rx_tail;

    uint8_t tx_fifo[2048];
    uint32_t tx_head;
    uint32_t tx_tail;

    /* Mirrored IRQ level code written to HS_INT_LEVEL */
    uint8_t irq_level_code;
};

/* ------------------------------------------------------------------ */
/* Forward declarations                                                */
/* ------------------------------------------------------------------ */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len);
static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len);
static void pcibase_reset(DeviceState *dev);
static void pcibase_realize(PCIDevice *pdev, Error **errp);
static void pcibase_uninit(PCIDevice *pdev);

/* ------------------------------------------------------------------ */
/* MemoryRegionOps                                                     */
/* ------------------------------------------------------------------ */
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

/* ------------------------------------------------------------------ */
/* Helper: register a BAR (MMIO or PIO)                               */
/* ------------------------------------------------------------------ */
static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    MemoryRegion *mr;

    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    mr = &s->bar_regions[bi->index];

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

/* ------------------------------------------------------------------ */
/* FIFO helpers                                                        */
/* ------------------------------------------------------------------ */
static inline uint32_t cb7210_fifo_count(uint32_t head, uint32_t tail)
{
    return (head - tail) & (cb7210_fifo_size - 1);
}

static inline uint32_t cb7210_fifo_space(uint32_t head, uint32_t tail)
{
    return cb7210_fifo_size - cb7210_fifo_count(head, tail);
}

static void cb7210_update_hs_status_rx(PCIBaseState *s)
{
    uint32_t cnt = cb7210_fifo_count(s->rx_head, s->rx_tail);

    s->hs_status &= ~(HS_RX_MSB_NOT_EMPTY | HS_RX_LSB_NOT_EMPTY | HS_FIFO_FULL | HS_HALF_FULL);

    if (cnt >= 2) {
        s->hs_status |= HS_RX_MSB_NOT_EMPTY | HS_RX_LSB_NOT_EMPTY;
    } else if (cnt == 1) {
        s->hs_status |= HS_RX_LSB_NOT_EMPTY;
    }

    if (cnt >= cb7210_fifo_size / 2) {
        s->hs_status |= HS_HALF_FULL;
    }

    if (cnt == cb7210_fifo_size) {
        s->hs_status |= HS_FIFO_FULL;
    }
}

static void cb7210_update_hs_status_tx(PCIBaseState *s)
{
    uint32_t cnt = cb7210_fifo_count(s->tx_head, s->tx_tail);

    s->hs_status &= ~(HS_TX_MSB_NOT_EMPTY | HS_TX_LSB_NOT_EMPTY | HS_HALF_FULL | HS_FIFO_FULL);

    if (cnt >= 2) {
        s->hs_status |= HS_TX_MSB_NOT_EMPTY | HS_TX_LSB_NOT_EMPTY;
    } else if (cnt == 1) {
        s->hs_status |= HS_TX_LSB_NOT_EMPTY;
    }

    if (cnt >= cb7210_fifo_size / 2) {
        s->hs_status |= HS_HALF_FULL;
    }

    if (cnt == cb7210_fifo_size) {
        s->hs_status |= HS_FIFO_FULL;
    }
}

/* ------------------------------------------------------------------ */
/* MMIO / PIO handlers                                                 */
/* ------------------------------------------------------------------ */

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint8_t val8 = 0;
    uint16_t val16 = 0;

    /* NEC7210 core registers are accessed as iobase + register * offset.
     * The cb7210-specific HS_MODE/HS_STATUS/HS_INT_LEVEL use register
     * numbers HS_MODE and HS_INT_LEVEL. We model register * offset as
     * addr / cb7210_reg_offset.
     */

    if (size == 1) {
        uint32_t reg = addr / cb7210_reg_offset;

        if (reg == HS_STATUS) {
            val8 = s->hs_status;
            return val8;
        }
        if (reg == HS_MODE) {
            val8 = s->hs_mode_bits;
            return val8;
        }
        if (reg == HS_INT_LEVEL) {
            val8 = s->irq_level_code;
            return val8;
        }

        /* Default: other NEC7210 registers not modelled */
        return 0;
    } else if (size == 2) {
        /* Possible FIFO word access when nec7210_iobase == fifo_iobase */
        if (addr == DIR) {
            /* Read from RX FIFO */
            uint32_t cnt = cb7210_fifo_count(s->rx_head, s->rx_tail);
            if (cnt >= 2) {
                uint8_t b0 = s->rx_fifo[s->rx_tail & (cb7210_fifo_size - 1)];
                uint8_t b1 = s->rx_fifo[(s->rx_tail + 1) & (cb7210_fifo_size - 1)];
                s->rx_tail = (s->rx_tail + 2) & (cb7210_fifo_size - 1);
                val16 = (uint16_t)b0 | ((uint16_t)b1 << 8);
            } else if (cnt == 1) {
                uint8_t b0 = s->rx_fifo[s->rx_tail & (cb7210_fifo_size - 1)];
                s->rx_tail = (s->rx_tail + 1) & (cb7210_fifo_size - 1);
                val16 = b0;
            } else {
                val16 = 0;
            }
            cb7210_update_hs_status_rx(s);
            return val16;
        }
    }

    qemu_log_mask(LOG_UNIMP,
                  "[%s] mmio_read addr=%" PRIx64 " size=%u\n",
                  TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size == 1) {
        uint32_t reg = addr / cb7210_reg_offset;
        uint8_t v = (uint8_t)val;

        if (reg == HS_MODE) {
            /* Clear interrupt-related bits when CLR_* are set; driver writes
             * HS_MODE with clear bits then again without.
             */
            if (v & HS_CLR_SRQ_INT) {
                s->hs_status &= ~HS_SRQ_INT;
            }
            if (v & HS_CLR_EOI_EMPTY_INT) {
                s->hs_status &= ~HS_EOI_INT;
            }
            if (v & HS_CLR_HF_INT) {
                s->hs_status &= ~HS_HALF_FULL;
            }

            s->hs_mode_bits = v & ~(HS_CLR_SRQ_INT | HS_CLR_EOI_EMPTY_INT | HS_CLR_HF_INT);
            return;
        }

        if (reg == HS_INT_LEVEL) {
            /* Store IRQ routing code; we do not decode to vector because
             * QEMU assigns a fixed PCI INTx line.
             */
            s->irq_level_code = v;
            return;
        }

        if (reg == HS_STATUS) {
            /* HS_RESET7210 is written to HS_INT_LEVEL in driver, not here.
             * Writes to HS_STATUS are not used in the driver.
             */
            return;
        }

        return;
    } else if (size == 2) {
        if (addr == CDOR) {
            /* Write to TX FIFO */
            uint16_t w = (uint16_t)val;
            uint32_t space = cb7210_fifo_space(s->tx_head, s->tx_tail);

            if (space >= 2) {
                s->tx_fifo[s->tx_head & (cb7210_fifo_size - 1)] = w & 0xff;
                s->tx_fifo[(s->tx_head + 1) & (cb7210_fifo_size - 1)] = (w >> 8) & 0xff;
                s->tx_head = (s->tx_head + 2) & (cb7210_fifo_size - 1);
            }
            cb7210_update_hs_status_tx(s);
            return;
        }
    }

    qemu_log_mask(LOG_UNIMP,
                  "[%s] mmio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n",
                  TYPE_PCIBASE_DEVICE, (uint64_t)addr, (uint64_t)val, size);
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* Some boards map nec7210_iobase and fifo_iobase to PCI I/O BARs; the
     * access pattern is identical to MMIO, so reuse MMIO handler semantics.
     */
    return pcibase_mmio_read(opaque, addr, size);
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    pcibase_mmio_write(opaque, addr, val, size);
}

/* ------------------------------------------------------------------ */
/* Reset                                                              */
/* ------------------------------------------------------------------ */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    pci_device_reset(pdev);

    if (s->mmio_backing && s->mmio_backing_size) {
        memset(s->mmio_backing, 0, s->mmio_backing_size);
    }

    s->hs_mode_bits = 0;
    s->hs_status = 0;
    s->irq_level_code = 0;

    s->rx_head = s->rx_tail = 0;
    s->tx_head = s->tx_tail = 0;
}

/* ------------------------------------------------------------------ */
/* DMA initialize (driver does not use PCI DMA directly)              */
/* ------------------------------------------------------------------ */
static void pcibase_dma_device_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    (void)s;
    (void)errp;
}

/* ------------------------------------------------------------------ */
/* PCI config space access                                            */
/* ------------------------------------------------------------------ */
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

/* ------------------------------------------------------------------ */
/* Realize (device init)                                              */
/* ------------------------------------------------------------------ */
static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;
    int i;

    /* Update PCI IDs to match first Quancom GPIB device in the driver */
    pci_set_word(pci_conf + PCI_VENDOR_ID, 0x1307);
    pci_set_word(pci_conf + PCI_DEVICE_ID, 0x6);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_COMMUNICATION_OTHER);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* BAR layout: driver uses three regions for the AMCC-based PCI card:
     *  BAR0: AMCC S5933 registers (amcc_iobase)
     *  BAR1: NEC7210 core registers (iobase)
     *  BAR2: FIFO (fifo_iobase)
     * The exact sizes are not visible, but the driver requests them and
     * uses only small I/O ranges; we map them as 0x20 bytes each.
     */
    s->num_bars = 3;

    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 0x20;
    s->bar_info[0].name = "cb7210-amcc";
    s->bar_info[0].sparse = false;

    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_PIO;
    s->bar_info[1].size = 0x20;
    s->bar_info[1].name = "cb7210-nec7210";
    s->bar_info[1].sparse = false;

    s->bar_info[2].index = 2;
    s->bar_info[2].type = BAR_TYPE_PIO;
    s->bar_info[2].size = 0x20;
    s->bar_info[2].name = "cb7210-fifo";
    s->bar_info[2].sparse = false;

    for (i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
        if (errp && *errp) {
            return;
        }
    }

    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) {
        return;
    }

    s->hs_mode_bits = 0;
    s->hs_status = 0;
    s->irq_level_code = 0;
    s->rx_head = s->rx_tail = 0;
    s->tx_head = s->tx_tail = 0;
}

/* ------------------------------------------------------------------ */
/* Uninit/cleanup                                                     */
/* ------------------------------------------------------------------ */
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
}

/* ------------------------------------------------------------------ */
/* Class init / type registration                                    */
/* ------------------------------------------------------------------ */
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

