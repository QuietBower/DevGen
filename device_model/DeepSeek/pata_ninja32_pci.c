/*
 * QEMU PCI device model for pata_ninja32
 * Generated from driver source analysis.
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

#define TYPE_PCIBASE_DEVICE "pata_ninja32_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define VENDOR_ID 0x10FC
#define DEVICE_ID 0x0003
#define CLASS_ID  0x0101

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

    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* BMDMA registers */
    uint8_t bmdma_cmd;        /* offset 0 */
    uint8_t bmdma_status;     /* offset 2 */
    uint32_t bmdma_prd;       /* offset 4 */
    /* Generic register store (0x00-0x1F) for non-BMDMA offsets */
    uint8_t regs[0x20];
};

/* IRQ update logic based on BMDMA interrupt */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    /* Interrupt condition: status bit 2 (Interrupt) set and command bit 3 (Interrupt Enable) set */
    if ((s->bmdma_status & 0x04) && (s->bmdma_cmd & 0x08)) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= 0x20) {
        return -1;
    }

    switch (addr) {
    case 0x00:
        if (size == 1) { val = s->bmdma_cmd; }
        break;
    case 0x02:
        if (size == 1) {
            val = s->bmdma_status | 0x02;  /* DMA capable bit */
        }
        break;
    case 0x04:
        if (size == 4) {
            val = s->bmdma_prd;
        } else if (size == 1) {
            int idx = addr & 3;
            val = (s->bmdma_prd >> (idx * 8)) & 0xff;
        }
        break;
    case 0x05:
    case 0x06:
    case 0x07:
        if (size == 1) {
            int idx = addr - 0x04;
            val = (s->bmdma_prd >> (idx * 8)) & 0xff;
        }
        break;
    default:
        val = s->regs[addr];
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= 0x20) {
        return;
    }

    switch (addr) {
    case 0x00:
        if (size == 1) {
            s->bmdma_cmd = val & 0xFF;
            pcibase_update_irq(s);
        }
        break;
    case 0x02:
        if (size == 1) {
            /* Write-1-to-clear for interrupt bit (bit 2) */
            if (val & 0x04) {
                s->bmdma_status &= ~0x04;
            }
            pcibase_update_irq(s);
        }
        break;
    case 0x04:
        if (size == 4) {
            s->bmdma_prd = val & 0xFFFFFFFF;
        } else if (size == 1) {
            /* byte access to PRD table pointer */
            int idx = addr & 3;
            s->bmdma_prd = (s->bmdma_prd & ~(0xFF << (idx * 8))) | ((val & 0xFF) << (idx * 8));
        }
        break;
    case 0x05:
    case 0x06:
    case 0x07:
        if (size == 1) {
            int idx = addr - 0x04;
            s->bmdma_prd = (s->bmdma_prd & ~(0xFF << (idx * 8))) | ((val & 0xFF) << (idx * 8));
        }
        break;
    default:
        s->regs[addr] = val & 0xFF;
        break;
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= 0x20) {
        return -1;
    }

    switch (addr) {
    case 0x00:
        if (size == 1) { val = s->bmdma_cmd; }
        break;
    case 0x02:
        if (size == 1) {
            val = s->bmdma_status | 0x02;  /* DMA capable bit */
        }
        break;
    case 0x04:
        if (size == 4) {
            val = s->bmdma_prd;
        } else if (size == 1) {
            int idx = addr & 3;
            val = (s->bmdma_prd >> (idx * 8)) & 0xff;
        }
        break;
    case 0x05:
    case 0x06:
    case 0x07:
        if (size == 1) {
            int idx = addr - 0x04;
            val = (s->bmdma_prd >> (idx * 8)) & 0xff;
        }
        break;
    default:
        val = s->regs[addr];
        break;
    }
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= 0x20) {
        return;
    }

    switch (addr) {
    case 0x00:
        if (size == 1) {
            s->bmdma_cmd = val & 0xFF;
            pcibase_update_irq(s);
        }
        break;
    case 0x02:
        if (size == 1) {
            if (val & 0x04) {
                s->bmdma_status &= ~0x04;
            }
            pcibase_update_irq(s);
        }
        break;
    case 0x04:
        if (size == 4) {
            s->bmdma_prd = val & 0xFFFFFFFF;
        } else if (size == 1) {
            int idx = addr & 3;
            s->bmdma_prd = (s->bmdma_prd & ~(0xFF << (idx * 8))) | ((val & 0xFF) << (idx * 8));
        }
        break;
    case 0x05:
    case 0x06:
    case 0x07:
        if (size == 1) {
            int idx = addr - 0x04;
            s->bmdma_prd = (s->bmdma_prd & ~(0xFF << (idx * 8))) | ((val & 0xFF) << (idx * 8));
        }
        break;
    default:
        s->regs[addr] = val & 0xFF;
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

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Reset shadow registers to initial state */
    s->bmdma_cmd = 0;
    s->bmdma_status = 0x02;  /* DMA capable */
    s->bmdma_prd = 0;
    memset(s->regs, 0, sizeof(s->regs));
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

    pci_set_word(pci_conf + PCI_VENDOR_ID, VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    s->num_bars = 1;
    s->bar_info[0] = (BARInfo){
        .index = 0,
        .type = BAR_TYPE_PIO,
        .size = 0x100,
        .name = "io"
    };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Add PCIe capability to allow IO BAR on PCIe buses */
    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    uint8_t pcie_cap = pcie_endpoint_cap_init(pdev, 0x80);
    if (pcie_cap) {
        /* Set IO Space Access Required bit in Device Capabilities */
        pci_set_word(pdev->config + pcie_cap + PCI_EXP_DEVCAP,
                     pci_get_word(pdev->config + pcie_cap + PCI_EXP_DEVCAP) | (1 << 3));
    }

    /* Initialize internal state */
    s->bmdma_cmd = 0;
    s->bmdma_status = 0x02;  /* DMA capable */
    s->bmdma_prd = 0;
    memset(s->regs, 0, sizeof(s->regs));
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
}

static const VMStateDescription vmstate_pcibase = {
    .name = "pata_ninja32_pci",
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
