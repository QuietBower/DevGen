/*
 * QEMU model for Intersil p54pci wireless adapter.
 * Generated based on p54pci.c Linux driver analysis.
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

#define TYPE_PCIBASE_DEVICE "p54pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Driver-derived register offsets and bit definitions */
#define PCI_VENDOR_ID_INTERSIL      0x1260
#define PCI_DEVICE_ID_P54P          0x3890
#define PCI_CLASS_P54P              0x028000

#define P54P_DEV_INT                0x00
#define P54P_INT_IDENT              0x10
#define P54P_INT_ACK                0x14
#define P54P_INT_ENABLE             0x18
#define P54P_RING_CONTROL_BASE      0x20
#define P54P_DIRECT_MEM_BASE        0x30
#define P54P_DMA_ADDR               0x60
#define P54P_DMA_LEN                0x64
#define P54P_DMA_CTRL               0x68
#define P54P_CTRL_STAT              0x78

#define ISL38XX_CTRL_STAT_RESET     0x10000000
#define ISL38XX_CTRL_STAT_RAMBOOT   0x20000000
#define ISL38XX_CTRL_STAT_CLKRUN    0x00800000
#define ISL38XX_DEV_INT_UPDATE      0x0002
#define ISL38XX_DEV_INT_RESET       0x0001
#define ISL38XX_INT_IDENT_UPDATE    0x0002
#define ISL38XX_INT_IDENT_INIT      0x0004

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

struct p54p_csr {
    uint32_t dev_int;           /* 0x00 */
    uint8_t unused_1[12];       /* 0x04 - 0x0F */
    uint32_t int_ident;         /* 0x10 */
    uint32_t int_ack;           /* 0x14 */
    uint32_t int_enable;        /* 0x18 */
    uint8_t unused_2[4];        /* 0x1C - 0x1F */
    union {
        uint32_t ring_control_base;
        uint32_t gen_purp_com[2];
    };                          /* 0x20 - 0x27 */
    uint8_t unused_3[8];        /* 0x28 - 0x2F */
    uint32_t direct_mem_base;   /* 0x30 */
    uint8_t unused_4[44];       /* 0x34 - 0x5F */
    uint32_t dma_addr;          /* 0x60 */
    uint32_t dma_len;           /* 0x64 */
    uint32_t dma_ctrl;          /* 0x68 */
    uint8_t unused_5[12];       /* 0x6C - 0x77 */
    uint32_t ctrl_stat;         /* 0x78 */
    uint8_t unused_6[1924];     /* 0x7C - 0x7FF */
    uint8_t cardbus_cis[0x800]; /* 0x800 - 0xFFF */
    /* direct_mem_win handled as separate RAM region */
};

struct p54p_desc {
    uint32_t host_addr;
    uint32_t device_addr;
    uint16_t len;
    uint16_t flags;
};

struct p54p_ring_control {
    uint32_t host_idx[4];
    uint32_t device_idx[4];
    struct p54p_desc rx_data[8];
    struct p54p_desc tx_data[32];
    struct p54p_desc rx_mgmt[4];
    struct p54p_desc tx_mgmt[4];
};

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;

    struct p54p_csr regs;

    struct dma_state {
        dma_addr_t ring_control_dma;
    } dma;

    bool fw_loaded;
    void *firmware;

    /* Subregion for direct memory window, implemented as RAM */
    MemoryRegion direct_mem_mr;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->regs.int_ident & s->regs.int_enable) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case 0x00:
        val = s->regs.dev_int;
        break;
    case 0x04 ... 0x0F:
    case 0x1C ... 0x1F:
    case 0x28 ... 0x2F:
    case 0x34 ... 0x5F:
    case 0x6C ... 0x77:
    case 0x7C ... 0xFFF:
        /* unused areas */
        val = 0;
        break;
    case 0x10:
        val = s->regs.int_ident;
        break;
    case 0x14:
        val = s->regs.int_ack;
        break;
    case 0x18:
        val = s->regs.int_enable;
        break;
    case 0x20:
        val = s->regs.ring_control_base;
        break;
    case 0x30:
        val = s->regs.direct_mem_base;
        break;
    case 0x60:
        val = s->regs.dma_addr;
        break;
    case 0x64:
        val = s->regs.dma_len;
        break;
    case 0x68:
        val = s->regs.dma_ctrl;
        break;
    case 0x78:
        val = s->regs.ctrl_stat;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "p54pci: unsupported MMIO read at 0x%" HWADDR_PRIx "\n", addr);
        val = 0;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case 0x00: /* dev_int */
        s->regs.dev_int = (uint32_t)val;
        if (val & ISL38XX_DEV_INT_UPDATE) {
            s->regs.int_ident |= ISL38XX_INT_IDENT_UPDATE;
        }
        if (val & ISL38XX_DEV_INT_RESET) {
            s->regs.int_ident |= ISL38XX_INT_IDENT_INIT;
        }
        pcibase_update_irq(s);
        break;
    case 0x14: /* int_ack */
        s->regs.int_ident &= ~(uint32_t)val;
        pcibase_update_irq(s);
        break;
    case 0x18: /* int_enable */
        s->regs.int_enable = (uint32_t)val;
        pcibase_update_irq(s);
        break;
    case 0x20: /* ring_control_base */
        s->regs.ring_control_base = (uint32_t)val;
        break;
    case 0x30: /* direct_mem_base */
        s->regs.direct_mem_base = (uint32_t)val;
        break;
    case 0x60: /* dma_addr */
        s->regs.dma_addr = (uint32_t)val;
        break;
    case 0x64: /* dma_len */
        s->regs.dma_len = (uint32_t)val;
        break;
    case 0x68: /* dma_ctrl */
        s->regs.dma_ctrl = (uint32_t)val;
        break;
    case 0x78: /* ctrl_stat */
        s->regs.ctrl_stat = (uint32_t)val;
        break;
    case 0x04 ... 0x0F:
    case 0x1C ... 0x1F:
    case 0x28 ... 0x2F:
    case 0x34 ... 0x5F:
    case 0x6C ... 0x77:
    case 0x7C ... 0xFFF:
        /* unused areas: ignore writes */
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "p54pci: unsupported MMIO write at 0x%" HWADDR_PRIx " val 0x%" PRIx64 "\n", addr, val);
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* Not used */
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Not used */
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

    /* Reset all registers to power-on defaults */
    memset(&s->regs, 0, sizeof(s->regs));
    s->fw_loaded = false;
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

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_INTERSIL);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_P54P);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_P54P);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = 0x2000, .name = "p54p-mmio" };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Direct memory window (firmware upload) as RAM subregion */
    memory_region_init_ram(&s->direct_mem_mr, OBJECT(s), "p54p-direct-mem", 0x1000, errp);
    memory_region_add_subregion(&s->bar_regions[0], 0x1000, &s->direct_mem_mr);

    /* Field Initialization */
    memset(&s->regs, 0, sizeof(s->regs));
    s->fw_loaded = false;
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

    /* No extra resources allocated */
}

static const VMStateDescription vmstate_pcibase = {
    .name = "p54pci_pci",
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
