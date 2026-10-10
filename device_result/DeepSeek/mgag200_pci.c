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

#define TYPE_PCIBASE_DEVICE "mgag200_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI_VENDOR_ID_MATROX 0x102B
#define DEVICE_ID 0x520
#define CLASS_ID 0x030000

#define BAR0_SIZE (8 * MiB)
#define BAR1_SIZE (16 * KiB)

#define MGA_CRTC_INDEX      0x1fd4
#define MGA_CRTC_DATA       0x1fd5
#define MGA_CRTCEXT_INDEX   0x1fde
#define MGA_CRTCEXT_DATA    0x1fdf
#define MGA_SEQ_INDEX       0x1fc4
#define MGA_SEQ_DATA        0x1fc5
#define MGA_DAC_INDEX       0x3c00
#define MGA_DAC_DATA        0x3c0a
#define MGA_MISC_IN         0x1fcc
#define MGA_MISC_OUT        0x1fc2
#define MGA_IEN             0x1e1c

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

    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;

    uint8_t mmio[BAR1_SIZE];

    uint8_t crtc_index;
    uint8_t crtcext_index;
    uint8_t seq_index;
    uint8_t dac_index;
    uint8_t crtc_regs[256];
    uint8_t crtcext_regs[256];
    uint8_t seq_regs[256];
    uint8_t dac_regs[256];
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    /* Driver disables interrupts via IEN=0, no IRQ logic required */
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= BAR1_SIZE) {
        return ~0ULL;
    }

    if (size == 1) {
        switch (addr) {
        case MGA_CRTC_DATA:
            return s->crtc_regs[s->crtc_index];
        case MGA_CRTCEXT_DATA:
            return s->crtcext_regs[s->crtcext_index];
        case MGA_SEQ_DATA:
            return s->seq_regs[s->seq_index];
        case MGA_DAC_DATA:
            return s->dac_regs[s->dac_index];
        default:
            return s->mmio[addr];
        }
    } else {
        if (addr + size > BAR1_SIZE) {
            return ~0ULL;
        }
        memcpy(&val, &s->mmio[addr], MIN(size, sizeof(val)));
        return val;
    }
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= BAR1_SIZE) {
        return;
    }

    if (size == 1) {
        switch (addr) {
        case MGA_CRTC_INDEX:
            s->crtc_index = val & 0xFF;
            break;
        case MGA_CRTC_DATA:
            s->crtc_regs[s->crtc_index] = val & 0xFF;
            break;
        case MGA_CRTCEXT_INDEX:
            s->crtcext_index = val & 0xFF;
            break;
        case MGA_CRTCEXT_DATA:
            s->crtcext_regs[s->crtcext_index] = val & 0xFF;
            break;
        case MGA_SEQ_INDEX:
            s->seq_index = val & 0xFF;
            break;
        case MGA_SEQ_DATA:
            s->seq_regs[s->seq_index] = val & 0xFF;
            break;
        case MGA_DAC_INDEX:
            s->dac_index = val & 0xFF;
            break;
        case MGA_DAC_DATA:
            s->dac_regs[s->dac_index] = val & 0xFF;
            break;
        default:
            s->mmio[addr] = val & 0xFF;
            break;
        }
    } else {
        if (addr + size > BAR1_SIZE) {
            return;
        }
        uint8_t buf[8];
        memcpy(buf, &val, MIN(size, sizeof(buf)));
        memcpy(&s->mmio[addr], buf, size);
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
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
    memset(s->mmio, 0, sizeof(s->mmio));
    s->crtc_index = 0;
    s->crtcext_index = 0;
    s->seq_index = 0;
    s->dac_index = 0;
    memset(s->crtc_regs, 0, sizeof(s->crtc_regs));
    memset(s->crtcext_regs, 0, sizeof(s->crtcext_regs));
    memset(s->seq_regs, 0, sizeof(s->seq_regs));
    memset(s->dac_regs, 0, sizeof(s->dac_regs));
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

    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_MATROX);
    pci_set_word(pci_conf + PCI_DEVICE_ID, DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    s->num_bars = 2;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_RAM, .size = BAR0_SIZE, .name = "mgag200-vram" };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_MMIO, .size = BAR1_SIZE, .name = "mgag200-mmio" };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
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
    .name = "mgag200_pci",
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