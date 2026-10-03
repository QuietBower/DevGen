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

#define TYPE_PCIBASE_DEVICE "pata_pdc202xx_old_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Supplementary definitions from driver source */
#define PCI_VENDOR_ID_PROMISE          0x105a
#define PCI_DEVICE_ID_PROMISE_20265    0x4d30
#define PCI_DEVICE_ID_PROMISE_20246    0x4d33
#define PCI_CLASS_STORAGE_IDE          0x0101

/* BM-DMA extended register offsets */
#define BM_CLOCK_SEL   0x11
#define BM_IRQ_FIFO    0x1d
#define BM_BURST_MODE  0x1f
#define BM_ATAPI_REG0  0x20
#define BM_ATAPI_REG1  0x24

typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM
} BARType;

typedef struct {
    int     index;    /* BAR index 0-5 */
    BARType type;
    hwaddr  size;
    const char *name;
} BARInfo;

typedef struct {
    uint8_t clock_sel;
    uint8_t irq_fifo;
    uint8_t burst_mode;
    uint32_t atapi_reg0;  /* port 0 */
    uint32_t atapi_reg1;  /* port 1 */
    /* standard BM-DMA registers (per channel) */
    uint8_t bm_cmd[2];
    uint8_t bm_status[2];
    uint32_t bm_prd[2];
} BMDMAState;

struct PCIBaseState {
    PCIDevice parent_obj;
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;
    bool has_msi;
    bool has_msix;

    BMDMAState bmdma;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    int level = 0;

    /* IRQ pending if any channel's bm_status[ch] & 0x04 (Interrupt) */
    for (int i = 0; i < 2; i++) {
        if (s->bmdma.bm_status[i] & 0x04) {
            level = 1;
            break;
        }
    }
    pci_set_irq(pdev, level);
}

/* PIO handlers for ATA command blocks (primary/secondary) */
static uint64_t pcibase_command_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    uint8_t val = 0;

    if (addr == 7) {
        /* Status register: return DRDY (0x40) + DSC (0x10) to indicate ready, no BSY */
        val = 0x50;
    }
    return val;
}

static void pcibase_command_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Ignore writes to command block (including command register) */
}

static const MemoryRegionOps pcibase_command_ops = {
    .read = pcibase_command_pio_read,
    .write = pcibase_command_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* PIO handlers for control block (single byte at offset 2: alternate status/device control) */
static uint64_t pcibase_control_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    uint8_t val = 0;
    if (addr == 2) {
        val = 0x50;  /* same as status */
    }
    return val;
}

static void pcibase_control_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Ignore */
}

static const MemoryRegionOps pcibase_control_ops = {
    .read = pcibase_control_pio_read,
    .write = pcibase_control_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* PIO handlers for BM-DMA and extended registers (BAR4) */
static uint64_t pcibase_bmdma_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    BMDMAState *b = &s->bmdma;
    uint64_t val = 0;

    switch (addr) {
    case 0x00: case 0x08: /* primary/secondary command */
        val = b->bm_cmd[addr == 0x08 ? 1 : 0];
        break;
    case 0x02: case 0x0A: /* primary/secondary status */
        val = b->bm_status[addr == 0x0A ? 1 : 0];
        break;
    case 0x04: case 0x0C: /* primary/secondary PRD pointer */
        val = b->bm_prd[addr == 0x0C ? 1 : 0];
        break;
    case BM_CLOCK_SEL:
        val = b->clock_sel;
        break;
    case BM_IRQ_FIFO:
        val = b->irq_fifo;
        break;
    case BM_BURST_MODE:
        val = b->burst_mode;
        break;
    case BM_ATAPI_REG0:
        val = b->atapi_reg0;
        break;
    case BM_ATAPI_REG1:
        val = b->atapi_reg1;
        break;
    default:
        break;
    }
    return val;
}

static void pcibase_bmdma_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    BMDMAState *b = &s->bmdma;
    int ch;

    switch (addr) {
    case 0x00: case 0x08: /* command register */
        ch = (addr == 0x08) ? 1 : 0;
        b->bm_cmd[ch] = val & 0xFF;
        /* If start bit (bit 0) is set, we could start DMA; for simplicity, do nothing */
        if (val & 0x01) {
            /* Start DMA: could fake completion by setting interrupt */
            /* if (val & 0x80) IRQ enable */
            /* Not needed for binding */
        }
        /* If stop (bit 0 cleared), clear active and interrupt? */
        if (!(val & 0x01)) {
            b->bm_status[ch] &= ~0x01; /* clear active */
        }
        break;
    case 0x02: case 0x0A: /* status register - write clears interrupt */
        ch = (addr == 0x0A) ? 1 : 0;
        if (val & 0x04) {
            b->bm_status[ch] &= ~0x04;
        }
        break;
    case 0x04: case 0x0C: /* PRD pointer */
        ch = (addr == 0x0C) ? 1 : 0;
        b->bm_prd[ch] = val;
        break;
    case BM_CLOCK_SEL:
        b->clock_sel = val;
        break;
    case BM_IRQ_FIFO:
        /* Read-only, ignore writes */
        break;
    case BM_BURST_MODE:
        b->burst_mode = val;
        break;
    case BM_ATAPI_REG0:
        b->atapi_reg0 = val;
        break;
    case BM_ATAPI_REG1:
        b->atapi_reg1 = val;
        break;
    default:
        break;
    }
    pcibase_update_irq(s);
}

static const MemoryRegionOps pcibase_bmdma_ops = {
    .read = pcibase_bmdma_pio_read,
    .write = pcibase_bmdma_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* Generic unused DMA placeholder deleted */

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);
    uint8_t *pci_conf = pdev->config;

    memset(&s->bmdma, 0, sizeof(s->bmdma));
    /* Set initial values */
    s->bmdma.irq_fifo = 0x00; /* no interrupt */
    pci_device_reset(pdev);

    /* Force-assign fixed I/O addresses to ensure BARs are not left unassigned,
     * circumventing missing firmware assignment on PCIe root ports. */
    pci_set_long(pci_conf + PCI_BASE_ADDRESS_0, 0xe000 | PCI_BASE_ADDRESS_SPACE_IO); /* BAR0 primary cmd */
    pci_set_long(pci_conf + PCI_BASE_ADDRESS_1, 0xe008 | PCI_BASE_ADDRESS_SPACE_IO); /* BAR1 primary ctl */
    pci_set_long(pci_conf + PCI_BASE_ADDRESS_2, 0xe010 | PCI_BASE_ADDRESS_SPACE_IO); /* BAR2 secondary cmd */
    pci_set_long(pci_conf + PCI_BASE_ADDRESS_3, 0xe018 | PCI_BASE_ADDRESS_SPACE_IO); /* BAR3 secondary ctl */
    pci_set_long(pci_conf + PCI_BASE_ADDRESS_4, 0xe020 | PCI_BASE_ADDRESS_SPACE_IO); /* BAR4 bmdma */

    /* Enable I/O access in PCI command register so the device can be enabled */
    pci_set_word(pci_conf + PCI_COMMAND, PCI_COMMAND_IO);
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];
    const MemoryRegionOps *ops;

    /* Assign ops based on BAR role */
    switch (bi->index) {
    case 0: /* Primary command */
    case 2: /* Secondary command */
        ops = &pcibase_command_ops;
        break;
    case 1: /* Primary control */
    case 3: /* Secondary control */
        ops = &pcibase_control_ops;
        break;
    case 4: /* BM-DMA */
        ops = &pcibase_bmdma_ops;
        break;
    default:
        ops = &pcibase_command_ops; /* fallback */
    }

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), ops, s, bi->name, aligned_size);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_PROMISE );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_PROMISE_20265 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_STORAGE_IDE );
    pci_config_set_prog_interface(pci_conf, 0x8A);  /* native IDE mode, supporting two channels and DMA */
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Remove PCIe capability to avoid IO BAR assignment failure */
    /* pdev->cap_present |= QEMU_PCI_CAP_EXPRESS; */
    /* pcie_endpoint_cap_init(pdev, 0x80); */

    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR definitions: 0-3 ATA ports, 4 BM-DMA */
    s->num_bars = 5;
    BARInfo *bars = s->bar_info;

    bars[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_PIO, .size = 8, .name = "primary-cmd" };
    bars[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_PIO, .size = 4, .name = "primary-ctl" };
    bars[2] = (BARInfo){ .index = 2, .type = BAR_TYPE_PIO, .size = 8, .name = "secondary-cmd" };
    bars[3] = (BARInfo){ .index = 3, .type = BAR_TYPE_PIO, .size = 4, .name = "secondary-ctl" };
    bars[4] = (BARInfo){ .index = 4, .type = BAR_TYPE_PIO, .size = 64, .name = "bmdma" };

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
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
    .name = "pata_pdc202xx_old_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT8(bmdma.clock_sel, PCIBaseState),
        VMSTATE_UINT8(bmdma.irq_fifo, PCIBaseState),
        VMSTATE_UINT8(bmdma.burst_mode, PCIBaseState),
        VMSTATE_UINT32(bmdma.atapi_reg0, PCIBaseState),
        VMSTATE_UINT32(bmdma.atapi_reg1, PCIBaseState),
        VMSTATE_UINT8_ARRAY(bmdma.bm_cmd, PCIBaseState, 2),
        VMSTATE_UINT8_ARRAY(bmdma.bm_status, PCIBaseState, 2),
        VMSTATE_UINT32_ARRAY(bmdma.bm_prd, PCIBaseState, 2),
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
