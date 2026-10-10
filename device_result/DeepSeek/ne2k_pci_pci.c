/*
 * QEMU 8.2.10 virtual NE2000 PCI device model
 * Based on Linux ne2k-pci.c driver (8390 series)
 * Fully functional PIO-based emulation for probe/bind success.
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

#define TYPE_PCIBASE_DEVICE "ne2k_pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI IDs from driver pci_device_id table (first entry) */
#define PCI_VENDOR_ID_REALTEK 0x10ec
#define PCI_DEVICE_ID_NE2K    0x8029
#define PCI_CLASS_NETWORK_ETHERNET 0x0200

/* Register offsets (driver uses these directly or via EI_SHIFT(x)) */
#define NE_CMD         0x00
#define NE_RESET       0x1f
#define NE_DATAPORT    0x10
#define EN0_ISR        0x07
#define EN0_IMR        0x0f
#define EN0_DCFG       0x0e
#define EN0_RCNTLO     0x0a
#define EN0_RCNTHI     0x0b
#define EN0_RSARLO     0x08
#define EN0_RSARHI     0x09
#define EN0_RXCR       0x0c
#define EN0_TXCR       0x0d

/* Command register bits */
#define E8390_STOP     0x01
#define E8390_START    0x02
#define E8390_RREAD    0x08
#define E8390_RWRITE   0x10
#define E8390_NODMA    0x20
#define E8390_PAGE0    0x00
#define E8390_PAGE1    0x40

/* ISR bits */
#define ENISR_RESET    0x80
#define ENISR_RDC      0x40

#define NE_IO_EXTENT   0x20

/* BAR type definitions (local) */
typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_PIO,
    BAR_TYPE_MMIO,
    BAR_TYPE_RAM
} BARType;

typedef struct {
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

    /* Capability and Interrupt State (unused, but keep for structure) */
    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Placeholder: kept for compatibility, not used in PIO emulation */
    uint8_t io_regs[0x20];

    /* NE2000-specific state */
    uint8_t current_page;             /* 0 or 1 */
    uint8_t isr;                      /* Interrupt Status Register */
    uint8_t imr;                      /* Interrupt Mask Register */
    uint16_t dma_addr;                /* Remote DMA address (remote start addr) */
    uint16_t dma_count;               /* Remote DMA byte count */
    uint16_t dma_ptr;                 /* Current pointer in DMA transfer */
    bool dma_reading;                 /* True if remote read DMA in progress */
    uint8_t mac[6];                   /* Station address (MAC) */
    uint8_t prom[32];                 /* Full 32-byte PROM buffer */
    uint8_t reg_page0[0x20];          /* Page 0 registers (not all used) */
    uint8_t reg_page1[0x20];          /* Page 1 registers */
};

/* Forward declarations */
static void pcibase_reset(DeviceState *dev);
static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp);

/* Update IRQ line based on ISR and IMR */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->imr & s->isr) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* PIO read handler */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case 0x00: /* CMD register */
        val = s->reg_page0[0x00];
        break;
    case 0x07: /* ISR */
        val = s->isr;
        break;
    case 0x0d: /* Page-dependent: Counter0 (page0) or CURR (page1) */
        if (s->current_page == 0) {
            /* Counter0: reading clears it; we always return 0 */
            val = 0;
        } else {
            val = s->reg_page1[0x0d];
        }
        break;
    case 0x10: /* DATAPORT */
        if (s->dma_reading && s->dma_ptr < s->dma_count) {
            /* Remote DMA read: return bytes from PROM */
            int remain = s->dma_count - s->dma_ptr;
            int bytes = MIN(size, remain);
            val = 0;
            for (int i = 0; i < bytes; i++) {
                val |= (uint64_t)s->prom[s->dma_ptr++] << (i * 8);
            }
        } else {
            val = 0;
        }
        break;
    case 0x1f: /* NE_RESET */
        val = 0;
        break;
    default:
        if (s->current_page == 0) {
            val = s->reg_page0[addr];
        } else {
            val = s->reg_page1[addr];
        }
        break;
    }

    return val;
}

/* PIO write handler */
static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint8_t byte_val = val & 0xFF;

    switch (addr) {
    case 0x00: /* CMD register */
        s->reg_page0[0x00] = byte_val;
        if (byte_val & 0x40) {
            s->current_page = 1;
        } else {
            s->current_page = 0;
        }
        if ((byte_val & 0x0F) == (E8390_RREAD | E8390_START)) {
            s->dma_reading = true;
            s->dma_ptr = s->dma_addr;
        } else if ((byte_val & 0x0F) == (E8390_RWRITE | E8390_START)) {
            s->dma_reading = false;
        } else {
            s->dma_reading = false;
        }
        break;
    case 0x07: /* ISR: Write-1-to-clear */
        s->isr &= ~byte_val;
        pcibase_update_irq(s);
        break;
    case 0x0f: /* IMR */
        s->imr = byte_val;
        pcibase_update_irq(s);
        break;
    case 0x08: /* RSARLO */
        if (s->current_page == 0) {
            s->dma_addr = (s->dma_addr & 0xFF00) | byte_val;
        }
        s->reg_page0[addr] = byte_val;
        break;
    case 0x09: /* RSARHI */
        if (s->current_page == 0) {
            s->dma_addr = (s->dma_addr & 0x00FF) | (byte_val << 8);
        }
        s->reg_page0[addr] = byte_val;
        break;
    case 0x0a: /* RCNTLO */
        s->dma_count = (s->dma_count & 0xFF00) | byte_val;
        s->reg_page0[addr] = byte_val;
        break;
    case 0x0b: /* RCNTHI */
        s->dma_count = (s->dma_count & 0x00FF) | (byte_val << 8);
        s->reg_page0[addr] = byte_val;
        break;
    case 0x1f:
        pcibase_reset(DEVICE(s));
        break;
    default:
        if (s->current_page == 0) {
            s->reg_page0[addr] = byte_val;
        } else {
            s->reg_page1[addr] = byte_val;
        }
        break;
    }
}

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* Reset device state to initial values */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    s->current_page = 0;
    s->isr = ENISR_RESET;
    s->imr = 0;
    s->dma_addr = 0;
    s->dma_count = 0;
    s->dma_ptr = 0;
    s->dma_reading = false;
    memset(s->reg_page0, 0, sizeof(s->reg_page0));
    memset(s->reg_page1, 0, sizeof(s->reg_page1));
    s->reg_page0[0x00] = E8390_NODMA | E8390_PAGE0 | E8390_STOP;
    memset(s->prom, 0, sizeof(s->prom));
    s->prom[0] = 0x52;
    s->prom[1] = 0x54;
    s->prom[2] = 0x00;
    s->prom[3] = 0x12;
    s->prom[4] = 0x34;
    s->prom[5] = 0x56;
    memcpy(s->mac, s->prom, 6);

    pcibase_update_irq(s);
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_REALTEK);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_NE2K);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_ETHERNET);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = NE_IO_EXTENT;
    s->bar_info[0].name = "ne2k-pio";
    s->num_bars = 1;
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->current_page = 0;
    s->isr = 0;
    s->imr = 0;
    s->dma_addr = 0;
    s->dma_count = 0;
    s->dma_ptr = 0;
    s->dma_reading = false;
    memset(s->reg_page0, 0, sizeof(s->reg_page0));
    memset(s->reg_page1, 0, sizeof(s->reg_page1));
    s->reg_page0[0x00] = E8390_NODMA | E8390_PAGE0 | E8390_STOP;
    memset(s->prom, 0, sizeof(s->prom));
    s->prom[0] = 0x52;
    s->prom[1] = 0x54;
    s->prom[2] = 0x00;
    s->prom[3] = 0x12;
    s->prom[4] = 0x34;
    s->prom[5] = 0x56;
    memcpy(s->mac, s->prom, 6);

    pcibase_update_irq(s);
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
    .name = "ne2k_pci_pci",
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
