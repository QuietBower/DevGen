/*
 * QEMU PCI device model for bt878 audio function (minimal behavior)
 *
 * This is derived strictly from the Linux driver drivers/media/pci/bt8xx/bt878.c
 * and the Stage-1 template. Only behavior visible in the driver is implemented.
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

/* RISC engine and bt878-specific macros (from driver) */
#define RISC_WRITE              (0x01u << 28)
#define RISC_JUMP               (0x07u << 28)
#define RISC_SYNC               (0x08u << 28)
#define RISC_IRQ                (1u << 24)
#define RISC_SYNC_FM1           0x06u
#define RISC_SYNC_VRO           0x0Cu
#define RISC_WR_SOL             (1u << 27)
#define RISC_WR_EOL             (1u << 26)
#define RISC_STATUS(status)     ((((~(status)) & 0x0Fu) << 20) | (((status) & 0x0Fu) << 16))
#define RISC_SYNC_RESYNC        (1u << 15)

#define BT878_APACK_LEN         0x110u
#define BT878_APPERR            (1u << 15)
#define BT878_ARIPERR           (1u << 16)
#define BT878_APABORT           (1u << 17)
#define BT878_AFTRGT            (1u << 13)
#define BT878_AFDSR             (1u << 14)
#define BT878_AOCERR            (1u << 18)
#define BT878_AFBUS             (1u << 12)
#define BT878_ARISCI            (1u << 11)
#define BT878_AINT_MASK         0x104u
#define BT878_ARISC_START       0x114u
#define BT878_ASCERR            (1u << 19)
#define BT878_AGPIO_DMA_CTL     0x10cu
#define BT878_ARISC_EN          (1u << 27)
#define BT878_AINT_STAT         0x100u
#define BT878_ARISCS            (0xFu << 28)
#define BT878_ARISC_PC          0x120u

#define DST_IG_ENABLE           0u
#define DST_IG_READ             2u
#define DST_IG_TS               3u
#define DST_IG_WRITE            1u

#define BT848_INT_MASK          0x104u
#define BT878_MAX               4
#define BT878_VERSION_CODE      0x000000u

#define BT848_GPIO_DATA         0x200u
#define BT848_GPIO_OUT_EN       0x118u

/* PCI IDs: first entry of bt878_pci_tbl uses BROOKTREE_878_DEVICE macro */
#define BT878_VENDOR_ID         0x109eu
#define BT878_DEVICE_ID         0x036e
#define BT878_CLASS_ID          PCI_CLASS_OTHERS

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

    /* optional linear backing (we don't actually use it for registers) */
    uint8_t *mmio_backing;
    size_t mmio_backing_size;

    /* BAR table */
    BARInfo bar_info[6];
    int num_bars;

    /* interrupt/state */
    bool has_msi;
    bool has_msix;

    /* bt878 visible registers we emulate */
    uint32_t aint_stat;       /* BT878_AINT_STAT 0x100 */
    uint32_t aint_mask;       /* BT878_AINT_MASK 0x104 / BT848_INT_MASK */
    uint32_t agpio_dma_ctl;   /* BT878_AGPIO_DMA_CTL 0x10c */
    uint32_t apack_len;       /* BT878_APACK_LEN 0x110 */
    uint32_t arisc_start;     /* BT878_ARISC_START 0x114 */
    uint32_t arisc_pc;        /* BT878_ARISC_PC 0x120 */

    uint32_t gpio_data;       /* BT848_GPIO_DATA 0x200 */
    uint32_t gpio_out_en;     /* BT848_GPIO_OUT_EN 0x118 */

    /* simple RISC execution model */
    bool     risc_running;    /* reflected as BT878_ARISC_EN bit in AINT_STAT */
    uint32_t current_block;   /* simulated finished block index (0..15) */

    /* IRQ line state */
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

/* MMIO/PIO ops */
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

/* IRQ helper: recompute line from aint_stat & aint_mask */
static void bt878_update_irq(PCIBaseState *s)
{
    uint32_t pending = s->aint_stat & s->aint_mask;
    if (pending) {
        qemu_set_irq(s->irq, 1);
    } else {
        qemu_set_irq(s->irq, 0);
    }
}

/* MMIO handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = 0;

    /* Only registers used by the driver are implemented. Others return 0. */
    switch (addr) {
    case BT878_AINT_STAT: /* 0x100 */
        /* ARISC_EN reflected in this register's bitfield */
        if (s->risc_running) {
            s->aint_stat |= BT878_ARISC_EN;
        } else {
            s->aint_stat &= ~BT878_ARISC_EN;
        }
        val = s->aint_stat;
        break;
    case BT878_AINT_MASK: /* 0x104 (also BT848_INT_MASK) */
        val = s->aint_mask;
        break;
    case BT848_GPIO_OUT_EN: /* 0x118 */
        val = s->gpio_out_en;
        break;
    case BT878_AGPIO_DMA_CTL: /* 0x10c */
        val = s->agpio_dma_ctl;
        break;
    case BT878_APACK_LEN: /* 0x110 */
        val = s->apack_len;
        break;
    case BT878_ARISC_START: /* 0x114 */
        val = s->arisc_start;
        break;
    case BT878_ARISC_PC: /* 0x120 */
        /* driver reads this for debug when errors happen */
        val = s->arisc_pc;
        break;
    case BT848_GPIO_DATA: /* 0x200 */
        /* model GPIO as simple data register; read returns latched value */
        val = s->gpio_data;
        break;
    default:
        /* Unimplemented register space -> 0 */
        val = 0;
        break;
    }

    /* return little-endian truncated to access size */
    uint64_t ret = val;
    if (size == 1) {
        ret &= 0xff;
    } else if (size == 2) {
        ret &= 0xffff;
    } else if (size == 4) {
        ret &= 0xffffffffu;
    }
    return ret;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t v32 = (uint32_t)val;

    switch (addr) {
    case BT878_AINT_MASK: /* 0x104 and BT848_INT_MASK usage */
        s->aint_mask = v32;
        bt878_update_irq(s);
        break;
    case BT878_AINT_STAT: /* 0x100 - driver writes astat to clear */
        /* writing '1' bits clears corresponding bits */
        s->aint_stat &= ~v32;
        bt878_update_irq(s);
        break;
    case BT878_AGPIO_DMA_CTL: /* 0x10c */
        s->agpio_dma_ctl = v32;
        /* driver clears some bits with btand(~0x13, ...) when stopping */
        break;
    case BT878_APACK_LEN: /* 0x110 - written in bt878_risc_program */
        s->apack_len = v32;
        break;
    case BT878_ARISC_START: /* 0x114 - start address of RISC program */
        s->arisc_start = v32;
        /* simplify: start engine and set PC to start */
        s->risc_running = true;
        s->arisc_pc = s->arisc_start;
        s->current_block = 0;
        break;
    case BT848_GPIO_OUT_EN: /* 0x118 - GPIO direction register */
        s->gpio_out_en = v32;
        break;
    case BT848_GPIO_DATA: /* 0x200 - GPIO data register */
        /* emulate gpio_bits(mask, data) style writes: full register set */
        s->gpio_data = v32;
        break;
    default:
        /* ignore writes to unimplemented registers */
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* bt878 driver uses only MMIO BAR0; PIO is unused, return 0 */
    (void)opaque;
    (void)addr;
    (void)size;
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* bt878 driver uses only MMIO BAR0; ignore PIO */
    (void)opaque;
    (void)addr;
    (void)val;
    (void)size;
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

    /* Reset bt878-specific registers */
    s->aint_stat = 0;
    s->aint_mask = 0;
    s->agpio_dma_ctl = 0;
    s->apack_len = 0;
    s->arisc_start = 0;
    s->arisc_pc = 0;
    s->gpio_data = 0;
    s->gpio_out_en = 0;
    s->risc_running = false;
    s->current_block = 0;

    bt878_update_irq(s);
}

/* DMA initialize (no actual engine implemented; driver-visible DMA only) */
static void pcibase_dma_device_realize(PCIDevice *pdev, Error **errp)
{
    (void)pdev;
    (void)errp;
    /* The driver uses dma_alloc_coherent to allocate buffers; actual
     * bus-master DMA data transfers are not visible here and are not
     * required for probe/start to succeed, so we do not model them.
     */
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
    /* Allow normal BAR programming and other standard config writes */
    pci_default_write_config(pdev, addr, val, len);
}

/* Realize (device init) */
static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Basic PCI IDs */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  BT878_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  BT878_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, BT878_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* BAR layout: driver uses pci_resource_start(dev, 0) and ioremap(...,0x1000) */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x1000; /* size used by ioremap */
    s->bar_info[0].name = "bt878-mmio";
    s->bar_info[0].sparse = false;

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize IRQ line */
    s->irq = pci_allocate_irq(pdev);

    /* No MSI/MSI-X used by the driver */
    s->has_msi = false;
    s->has_msix = false;

    /* Initialize DMA-related (no real engine) */
    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) {
        return;
    }

    /* Reset internal state */
    s->aint_stat = 0;
    s->aint_mask = 0;
    s->agpio_dma_ctl = 0;
    s->apack_len = 0;
    s->arisc_start = 0;
    s->arisc_pc = 0;
    s->gpio_data = 0;
    s->gpio_out_en = 0;
    s->risc_running = false;
    s->current_block = 0;

    bt878_update_irq(s);
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

