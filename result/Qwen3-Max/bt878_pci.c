/*
 * QEMU BT878 PCI Device Emulation
 *
 * Copyright (c) 2025 QEMU Team
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
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

#define TYPE_PCIBASE_DEVICE "bt878_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register offsets and bit definitions from driver */
#define PCI_VENDOR_ID_BROOKTREE        0x109e
#define PCI_DEVICE_ID_BROOKTREE_878    0x036e
#define BT878_AINT_STAT		0x100
#define BT878_AINT_MASK		0x104
#define BT878_AGPIO_DMA_CTL	0x10c
#define BT878_ARISC_START	0x114
#define BT878_ARISC_PC		0x120
#define BT878_APACK_LEN		0x110
#define BT848_GPIO_DATA        0x200
#define BT848_GPIO_OUT_EN      0x118

#define BT878_ARISCI		(1<<11)
#define BT878_AFBUS		(1<<12)
#define BT878_AFTRGT		(1<<13)
#define BT878_AFDSR		(1<<14)
#define BT878_APPERR		(1<<15)
#define BT878_ARIPERR		(1<<16)
#define BT878_APABORT		(1<<17)
#define BT878_AOCERR		(1<<18)
#define BT878_ASCERR		(1<<19)
#define BT878_ARISC_EN		(1<<27)
#define BT878_ARISCS		(0xf<<28)

#define RISC_WRITE		(0x01 << 28)
#define RISC_JUMP		(0x07 << 28)
#define RISC_SYNC		(0x08 << 28)
#define RISC_IRQ		(1 << 24)
#define RISC_SYNC_FM1		0x06
#define RISC_SYNC_VRO		0x0C
#define RISC_WR_SOL		(1 << 27)
#define RISC_WR_EOL		(1 << 26)
#define RISC_SYNC_RESYNC	(1 << 15)

/* BAR metadata definition */
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

/* Device State */
struct PCIBaseState {
    PCIDevice parent_obj;

    /* BAR memory regions */
    MemoryRegion bar_regions[6];

    /* optional linear backing */
    uint8_t *mmio_backing;
    size_t mmio_backing_size;

    /* BAR table */
    BARInfo bar_info[6];
    int num_bars;

    /* interrupt state feature flags */
    bool has_msi;
    bool has_msix;
    
    /* DMA info */
    dma_addr_t risc_dma;
    dma_addr_t buf_dma;
    size_t risc_size;
    size_t buf_size;

    /* Register shadows */
    uint32_t aint_stat;
    uint32_t aint_mask;
    uint32_t agpio_dma_ctl;
    uint32_t arisc_start;
    uint32_t arisc_pc;
    uint32_t apack_len;
    uint32_t gpio_data;
    uint32_t gpio_out_en;

    /* Status fields */
    uint32_t finished_block;

    /* reset/probe state */
    bool shutdown;

    /* power mgmt */
    
    /* other fields */
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

/* MemoryRegionOps */
static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* Helper: register a BAR */
static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE)
        return;

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

/* MMIO / PIO handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case BT878_AINT_STAT:
        return s->aint_stat;
    case BT878_AINT_MASK:
        return s->aint_mask;
    case BT878_AGPIO_DMA_CTL:
        return s->agpio_dma_ctl;
    case BT878_ARISC_START:
        return s->arisc_start;
    case BT878_ARISC_PC:
        return s->arisc_pc;
    case BT878_APACK_LEN:
        return s->apack_len;
    case BT848_GPIO_DATA:
        return s->gpio_data;
    case BT848_GPIO_OUT_EN:
        return s->gpio_out_en;
    }

    qemu_log_mask(LOG_UNIMP, "[bt878_pci] mmio_read addr=%" PRIx64 " size=%u\n", (uint64_t)addr, size);
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case BT878_AINT_STAT:
        s->aint_stat = val;
        /* Clear interrupt condition by writing back the status bits */
        if (val & BT878_ARISCI) {
            /* Simulate completion of RISC program */
            s->aint_stat &= ~BT878_ARISC_EN;
            s->finished_block = (val & BT878_ARISCS) >> 28;
        }
        break;
    case BT878_AINT_MASK:
        s->aint_mask = val;
        break;
    case BT878_AGPIO_DMA_CTL:
        s->agpio_dma_ctl = val;
        if (val & 0x1b) {
            /* Enable RISC engine */
            s->aint_stat |= BT878_ARISC_EN;
            /* Trigger interrupt if RISC program completes immediately */
            if (s->arisc_start) {
                s->aint_stat |= BT878_ARISCI | ((0 << 28) & BT878_ARISCS);
                pci_set_irq(&s->parent_obj, 1);
            }
        } else {
            /* Disable RISC engine */
            s->aint_stat &= ~BT878_ARISC_EN;
        }
        break;
    case BT878_ARISC_START:
        s->arisc_start = val;
        s->risc_dma = val;
        break;
    case BT878_ARISC_PC:
        s->arisc_pc = val;
        break;
    case BT878_APACK_LEN:
        s->apack_len = val;
        break;
    case BT848_GPIO_DATA:
        s->gpio_data = val;
        break;
    case BT848_GPIO_OUT_EN:
        s->gpio_out_en = val;
        break;
    }

    qemu_log_mask(LOG_UNIMP, "[bt878_pci] mmio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n", (uint64_t)addr, (uint64_t)val, size);
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "[bt878_pci] pio_read addr=%" PRIx64 " size=%u\n", (uint64_t)addr, size);
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "[bt878_pci] pio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n", (uint64_t)addr, (uint64_t)val, size);
}

/* Reset */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    pci_device_reset(pdev);

    s->aint_stat = 0;
    s->aint_mask = 0;
    s->agpio_dma_ctl = 0;
    s->arisc_start = 0;
    s->arisc_pc = 0;
    s->apack_len = 0;
    s->gpio_data = 0;
    s->gpio_out_en = 0;
    s->finished_block = 0;
    s->shutdown = false;

    if (s->mmio_backing && s->mmio_backing_size)
        memset(s->mmio_backing, 0, s->mmio_backing_size);
}

/* DMA initialize */
static void pcibase_dma_device_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    /* Initialize DMA buffers as per driver allocation */
    s->buf_size = 128 * 1024;
    s->risc_size = 4096; /* PAGE_SIZE */

    (void)errp;
}

/* PCI config space access */
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    uint32_t val = pci_default_read_config(pdev, addr, len);

    switch (len) {
        case 1: val &= 0xFF; break;
        case 2: val &= 0xFFFF; break;
        case 4: break;
        default: break;
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

/* Realize */
static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_BROOKTREE);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_BROOKTREE_878);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_MULTIMEDIA_VIDEO);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* register BARs */
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x1000;
    s->bar_info[0].name = "bt878-mmio";
    s->num_bars = 1;

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* DMA init */
    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) return;

    qemu_log_mask(LOG_UNIMP, "[bt878_pci] device realized\n");
}

/* Uninit/cleanup */
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

    qemu_log_mask(LOG_UNIMP, "[bt878_pci] device uninit\n");
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
