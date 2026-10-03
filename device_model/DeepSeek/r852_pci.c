
/*
 * QEMU model for Ricoh R852 PCI NAND controller
 * Auto-generated from driver source analysis
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
#include "qemu/bswap.h"

#define TYPE_PCIBASE_DEVICE "r852_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Vendor and Device IDs from r852_pci_id_tbl (first entry) */
#define VENDOR_ID  0x1180  /* Ricoh */
#define DEVICE_ID  0x0852
#define CLASS_ID   0x0501 /* MEMORY_FLASH, typical for NAND controllers */

/* Register offsets (from r852.c defines) */
#define R852_DATALINE          0x00
#define R852_CTL_COMMAND       0x01
#define R852_CTL               0x04
#define R852_CTL_DATA          0x02
#define R852_CTL_ON            0x04
#define R852_CTL_WRITE         0x80
#define R852_CTL_CARDENABLE    0x10
#define R852_CTL_ECC_ENABLE    0x20
#define R852_CTL_ECC_ACCESS    0x40
#define R852_CTL_RESET         0x08
#define R852_CARD_STA          0x05
#define R852_CARD_STA_BUSY     0x80
#define R852_CARD_STA_PRESENT  0x04
#define R852_CARD_STA_RO       0x02
#define R852_CARD_STA_CD       0x01
#define R852_CARD_IRQ_STA      0x06
#define R852_CARD_IRQ_GENABLE  0x80
#define R852_CARD_IRQ_REMOVE   0x04
#define R852_CARD_IRQ_INSERT   0x08
#define R852_CARD_IRQ_ENABLE   0x07
#define R852_CARD_IRQ_MASK     0x1D
#define R852_HW                0x08
#define R852_HW_ENABLED        0x01
#define R852_HW_UNKNOWN        0x80
#define R852_ECC_CORRECTABLE   0x20
#define R852_ECC_FAIL          0x40
#define R852_ECC_ERR_BIT_MSK   0x07
#define R852_SMBIT             0x20

/* DMA-related register offsets */
#define R852_DMA1              0x40
#define R852_DMA2              0x80
#define R852_DMA_CAP           0x09
#define R852_DMA_ADDR          0x0C
#define R852_DMA_READ          0x02
#define R852_DMA_IRQ_ERROR     0x02
#define R852_DMA_IRQ_INTERNAL  0x04
#define R852_DMA_SETTINGS      0x10
#define R852_DMA_MEMORY        0x01
#define R852_DMA_INTERNAL      0x04
#define R852_DMA_IRQ_MEMORY    0x01
#define R852_DMA_IRQ_ENABLE    0x18
#define R852_DMA_LEN           512
#define R852_DMA_IRQ_STA       0x14
#define R852_DMA_IRQ_MASK      0x07

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

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware Register Shadows */
    uint8_t regs[0x100];  /* covers all known offsets, actual map TBD */

    /* DMA Context */
    struct dma_state {
        dma_addr_t addr;
        uint32_t len;
        uint8_t buf[R852_DMA_LEN];
        int dir;       /* 0 = write, 1 = read */
        int state;     /* 0 = internal, 1 = memory */
        int error;
        bool usable;
    } dma;

    /* Operational status flags */
    bool card_present;
    bool card_readonly;

    /* Probe/Reset sequencing (currently unused) */
    int reset_state;

    /* Power management state */
    uint8_t power_state;  /* D0-D3 */
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool card_irq = false;
    bool dma_irq = false;

    card_irq = (s->regs[R852_CARD_IRQ_STA] & s->regs[R852_CARD_IRQ_ENABLE] & R852_CARD_IRQ_GENABLE) != 0;

    uint32_t dma_irq_sta = ldl_le_p(&s->regs[R852_DMA_IRQ_STA]);
    uint32_t dma_irq_enable = ldl_le_p(&s->regs[R852_DMA_IRQ_ENABLE]);
    dma_irq = (dma_irq_sta & dma_irq_enable & R852_DMA_IRQ_MASK) != 0;

    if (card_irq || dma_irq) {
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
    case R852_DATALINE:  /* 0x00 */
        /* Data port: allow byte and dword access */
        for (unsigned i = 0; i < size; i++) {
            val |= (uint64_t)s->regs[addr + i] << (i * 8);
        }
        break;
    case R852_CTL:  /* 0x04 */
        val = s->regs[addr];
        break;
    case R852_CARD_STA:  /* 0x05 */
        val = s->regs[addr];
        break;
    case R852_CARD_IRQ_STA:  /* 0x06 */
        val = s->regs[addr];
        break;
    case R852_CARD_IRQ_ENABLE:  /* 0x07 */
        val = s->regs[addr];
        break;
    case R852_HW:  /* 0x08 - dword */
        if (size == 4) {
            val = ldl_le_p(&s->regs[addr]);
        } else {
            for (unsigned i = 0; i < size; i++) {
                val |= (uint64_t)s->regs[addr + i] << (i * 8);
            }
        }
        break;
    case R852_DMA_CAP:  /* 0x09 */
        val = s->regs[addr];
        break;
    case R852_DMA_ADDR:  /* 0x0C - dword */
        if (size == 4) {
            val = ldl_le_p(&s->regs[addr]);
        } else {
            for (unsigned i = 0; i < size; i++) {
                val |= (uint64_t)s->regs[addr + i] << (i * 8);
            }
        }
        break;
    case R852_DMA_SETTINGS:  /* 0x10 - dword */
        if (size == 4) {
            val = ldl_le_p(&s->regs[addr]);
        } else {
            for (unsigned i = 0; i < size; i++) {
                val |= (uint64_t)s->regs[addr + i] << (i * 8);
            }
        }
        break;
    case R852_DMA_IRQ_STA:  /* 0x14 - dword */
        if (size == 4) {
            val = ldl_le_p(&s->regs[addr]);
        } else {
            for (unsigned i = 0; i < size; i++) {
                val |= (uint64_t)s->regs[addr + i] << (i * 8);
            }
        }
        break;
    case R852_DMA_IRQ_ENABLE:  /* 0x18 - dword */
        if (size == 4) {
            val = ldl_le_p(&s->regs[addr]);
        } else {
            for (unsigned i = 0; i < size; i++) {
                val |= (uint64_t)s->regs[addr + i] << (i * 8);
            }
        }
        break;
    default:
        /* Unknown register: return zero-extended bytes */
        for (unsigned i = 0; i < size; i++) {
            val |= (uint64_t)s->regs[addr + i] << (i * 8);
        }
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case R852_DATALINE:  /* 0x00 */
        for (unsigned i = 0; i < size; i++) {
            s->regs[addr + i] = (val >> (i * 8)) & 0xFF;
        }
        break;
    case R852_CTL:  /* 0x04 */
        s->regs[addr] = val & 0xFF;
        break;
    case R852_CARD_STA:  /* 0x05 - read-only, ignore write */
        break;
    case R852_CARD_IRQ_STA:  /* 0x06 - W1C */
        s->regs[addr] &= ~(val & 0xFF);
        pcibase_update_irq(s);
        break;
    case R852_CARD_IRQ_ENABLE:  /* 0x07 */
        s->regs[addr] = val & 0xFF;
        pcibase_update_irq(s);
        break;
    case R852_HW:  /* 0x08 - dword */
        if (size == 4) {
            stl_le_p(&s->regs[addr], val);
        } else {
            for (unsigned i = 0; i < size; i++) {
                s->regs[addr + i] = (val >> (i * 8)) & 0xFF;
            }
        }
        break;
    case R852_DMA_CAP:  /* 0x09 - read-only, ignore write */
        break;
    case R852_DMA_ADDR:  /* 0x0C - dword */
        if (size == 4) {
            stl_le_p(&s->regs[addr], val);
        } else {
            for (unsigned i = 0; i < size; i++) {
                s->regs[addr + i] = (val >> (i * 8)) & 0xFF;
            }
        }
        break;
    case R852_DMA_SETTINGS:  /* 0x10 - dword */
        if (size == 4) {
            stl_le_p(&s->regs[addr], val);
        } else {
            for (unsigned i = 0; i < size; i++) {
                s->regs[addr + i] = (val >> (i * 8)) & 0xFF;
            }
        }
        break;
    case R852_DMA_IRQ_STA:  /* 0x14 - dword, W1C */
        if (size == 4) {
            uint32_t current = ldl_le_p(&s->regs[addr]);
            stl_le_p(&s->regs[addr], current & ~(uint32_t)val);
            pcibase_update_irq(s);
        } else {
            /* Handle byte-sized writes to W1C register? For simplicity, ignore non-dword */
        }
        break;
    case R852_DMA_IRQ_ENABLE:  /* 0x18 - dword */
        if (size == 4) {
            stl_le_p(&s->regs[addr], val);
            pcibase_update_irq(s);
        } else {
            for (unsigned i = 0; i < size; i++) {
                s->regs[addr + i] = (val >> (i * 8)) & 0xFF;
            }
            pcibase_update_irq(s);
        }
        break;
    default:
        for (unsigned i = 0; i < size; i++) {
            s->regs[addr + i] = (val >> (i * 8)) & 0xFF;
        }
        break;
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
    /* Reset registers to power-on defaults */
    memset(s->regs, 0, sizeof(s->regs));
    s->regs[R852_DMA_CAP] = 0x00; /* Non-DMA capable to avoid complex emulation */
    s->intr_status = 0;
    s->intr_mask = 0;
    s->power_state = 0; /* D0 */
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
        /* PIO not supported in this driver; fallback to memory */
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, aligned_size);
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);

    /* BAR Initialization: single MMIO BAR at index 0, size 4K */
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = 0x1000, .name = "r852-mmio" };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Interrupts: driver uses legacy IRQ, no MSI/MSI-X */
    /* DMA configuration not implemented (non-DMA capable emulation) */

    /* Final state initialization */
    memset(s->regs, 0, sizeof(s->regs));
    s->regs[R852_DMA_CAP] = 0x00;
    s->card_present = false;
    s->card_readonly = false;
    s->power_state = 0; /* D0 */
}

static void pcibase_uninit(PCIDevice *pdev)
{
    /* No extra cleanup needed */
}

static const VMStateDescription vmstate_pcibase = {
    .name = "r852_pci",
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
