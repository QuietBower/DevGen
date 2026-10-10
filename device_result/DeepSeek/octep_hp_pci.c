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

#define TYPE_PCIBASE_DEVICE "octep_hp_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI_VENDOR_ID_CAVIUM 0x177D
#define PCI_DEVICE_ID_CAVIUM_OCTEP_HP_CTLR 0xA0E3
#define OCTEP_HP_INTR_OFFSET(x) (0x20400 + ((x) << 4))
#define OCTEP_HP_INTR_VECTOR(x) (16 + (x))

typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM 
} BARType;

typedef enum {
    OCTEP_HP_INTR_INVALID = -1,
    OCTEP_HP_INTR_ENA = 0,
    OCTEP_HP_INTR_DIS = 1,
    OCTEP_HP_INTR_MAX = 2,
} OctepHpIntrType;

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

    uint64_t intr_pending[2];
    MemoryRegion msix_bar;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    /* No generic IRQ update needed for this device */
}

static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /* No DMA in this device */
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size != 8) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad read size %u at 0x%"HWADDR_PRIx"\n",
                      __func__, size, addr);
        return ~0ULL;
    }

    switch (addr) {
    case OCTEP_HP_INTR_OFFSET(OCTEP_HP_INTR_ENA):
        val = s->intr_pending[OCTEP_HP_INTR_ENA];
        break;
    case OCTEP_HP_INTR_OFFSET(OCTEP_HP_INTR_DIS):
        val = s->intr_pending[OCTEP_HP_INTR_DIS];
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: read from unknown addr 0x%"HWADDR_PRIx"\n",
                      __func__, addr);
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 8) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad write size %u at 0x%"HWADDR_PRIx"\n",
                      __func__, size, addr);
        return;
    }

    switch (addr) {
    case OCTEP_HP_INTR_OFFSET(OCTEP_HP_INTR_ENA):
        s->intr_pending[OCTEP_HP_INTR_ENA] &= ~val;
        break;
    case OCTEP_HP_INTR_OFFSET(OCTEP_HP_INTR_DIS):
        s->intr_pending[OCTEP_HP_INTR_DIS] &= ~val;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: write to unknown addr 0x%"HWADDR_PRIx"\n",
                      __func__, addr);
        break;
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 8, .max_access_size = 8 },
    .impl  = { .min_access_size = 8, .max_access_size = 8 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    s->intr_pending[OCTEP_HP_INTR_ENA] = 0;
    s->intr_pending[OCTEP_HP_INTR_DIS] = 0;
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
        error_setg(errp, "PIO BAR not supported");
        return;
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_CAVIUM );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_CAVIUM_OCTEP_HP_CTLR );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x00 );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    memory_region_init_ram(&s->msix_bar, OBJECT(s), "octep-hp-msix", 0x1000, errp);
    msix_init(pdev, 18, &s->msix_bar, 2, 0, &s->msix_bar, 2, 0x800, 0, errp);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->msix_bar);

    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x100000;
    s->bar_info[0].name = "octep-hp-bar0";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, &s->msix_bar, &s->msix_bar);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

static const VMStateDescription vmstate_pcibase = {
    .name = "octep_hp_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT64_ARRAY(intr_pending, PCIBaseState, 2),
        VMSTATE_MSIX(parent_obj, PCIBaseState),
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
