/* QEMU 8.2.10 PCI device model for Asiliant 69000 framebuffer */

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

#define TYPE_PCIBASE_DEVICE "asiliantfb_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define VENDOR_ID 0x102c
#define DEVICE_ID 0x00c0
#define CLASS_ID   0x0300

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_container;
    MemoryRegion reg_mmio;
    MemoryRegion reg_mmio_alias;
    MemoryRegion framebuffer_mem;

    uint8_t cr_regs[256], cr_index;
    uint8_t fr_regs[256], fr_index;
    uint8_t gr_regs[256], gr_index;
    uint8_t sr_regs[256], sr_index;
    uint8_t xr_regs[256], xr_index;
    uint8_t ar_regs[32], ar_index;
    bool ar_toggle;
    uint8_t palette[256][3];
    uint8_t palette_index;
    int palette_write_count;
    uint8_t misc_784, misc_78c;
};

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    switch (addr) {
    case 0x7a9: return s->cr_regs[s->cr_index];
    case 0x7a1: return s->fr_regs[s->fr_index];
    case 0x79d: return s->gr_regs[s->gr_index];
    case 0x789: return s->sr_regs[s->sr_index];
    case 0x7ad: return s->xr_regs[s->xr_index];
    case 0x780:
        return s->ar_regs[s->ar_index];
    case 0x7b4:
        s->ar_toggle = false;
        return 0;
    case 0x784: return s->misc_784;
    case 0x78c: return s->misc_78c;
    case 0x790: return s->palette_index;
    case 0x791: return s->palette[s->palette_index][0];
    default: return 0;
    }
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    switch (addr) {
    case 0x7a8:
        s->cr_index = val & 0xFF; break;
    case 0x7a9:
        s->cr_regs[s->cr_index] = val; break;
    case 0x7a0:
        s->fr_index = val & 0xFF; break;
    case 0x7a1:
        s->fr_regs[s->fr_index] = val; break;
    case 0x79c:
        s->gr_index = val & 0xFF; break;
    case 0x79d:
        s->gr_regs[s->gr_index] = val; break;
    case 0x788:
        s->sr_index = val & 0xFF; break;
    case 0x789:
        s->sr_regs[s->sr_index] = val; break;
    case 0x7ac:
        s->xr_index = val & 0xFF; break;
    case 0x7ad:
        s->xr_regs[s->xr_index] = val; break;
    case 0x780:
        if (s->ar_toggle) {
            s->ar_regs[s->ar_index] = val;
            s->ar_toggle = false;
        } else {
            s->ar_index = val & 0x1F;
            s->ar_toggle = true;
        }
        break;
    case 0x784:
        s->misc_784 = val; break;
    case 0x78c:
        s->misc_78c = val; break;
    case 0x790:
        s->palette_index = val & 0xFF;
        s->palette_write_count = 0;
        break;
    case 0x791:
        if (s->palette_write_count < 3) {
            s->palette[s->palette_index][s->palette_write_count] = val;
            s->palette_write_count++;
        }
        break;
    default:
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
    memset(s->cr_regs, 0, sizeof(s->cr_regs));
    memset(s->fr_regs, 0, sizeof(s->fr_regs));
    memset(s->gr_regs, 0, sizeof(s->gr_regs));
    memset(s->sr_regs, 0, sizeof(s->sr_regs));
    memset(s->xr_regs, 0, sizeof(s->xr_regs));
    memset(s->ar_regs, 0, sizeof(s->ar_regs));
    memset(s->palette, 0, sizeof(s->palette));
    s->cr_index = 0;
    s->fr_index = 0;
    s->gr_index = 0;
    s->sr_index = 0;
    s->xr_index = 0;
    s->ar_index = 0;
    s->ar_toggle = false;
    s->palette_index = 0;
    s->palette_write_count = 0;
    s->misc_784 = 0;
    s->misc_78c = 0;
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

    memory_region_init(&s->bar_container, OBJECT(s), "asiliantfb-bar0-container", 0x800000);
    memory_region_init_io(&s->reg_mmio, OBJECT(s), &pcibase_mmio_ops, s, "asiliantfb-regs", 0x1000);
    memory_region_init_ram(&s->framebuffer_mem, OBJECT(s), "asiliantfb-framebuffer", 0x200000, &error_abort);

    memory_region_add_subregion(&s->bar_container, 0x0, &s->framebuffer_mem);
    memory_region_add_subregion_overlap(&s->bar_container, 0x0, &s->reg_mmio, 1);
    memory_region_init_alias(&s->reg_mmio_alias, OBJECT(s), "asiliantfb-regs-alias", &s->reg_mmio, 0, 0x1000);
    memory_region_add_subregion(&s->bar_container, 0x400000, &s->reg_mmio_alias);

    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_container);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    /* No MSI/MSI-X used, nothing to clean up */
}

static const VMStateDescription vmstate_pcibase = {
    .name = "asiliantfb_pci",
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

type_init(pcibase_register_types)