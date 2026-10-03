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

#define TYPE_PCIBASE_DEVICE "cp500_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI_VENDOR_ID_KEBA 0xCEBA
#define PCI_DEVICE_ID_KEBA_CP035 0x2706
#define PCI_CLASS_OTHERS 0xff

#define CP500_VERSION_REG 0x00
#define CP500_RECONFIG_REG 0x11
#define CP500_PRESENT_REG 0x20
#define CP500_AXI_REG 0x40

#define CP500_BUILD_TEST 0x8000
#define CP500_RECFG_REQ 0x01
#define CP500_PRESENT_FAN0 0x01

#define CP500_NUM_MSIX 8

/* BAR sizes */
#define CP500_SYS_BAR_SIZE (16 * KiB)
#define CP500_ECM_BAR_SIZE (4 * KiB)

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

    uint32_t version;
    uint8_t reconfig;
    uint32_t present;
    uint32_t axi;

    MemoryRegion sys_regs;
};

G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
}

static uint64_t pcibase_sys_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case CP500_VERSION_REG:
        if (size == 4) {
            val = s->version;
        }
        break;
    case CP500_RECONFIG_REG:
        if (size == 1) {
            val = s->reconfig;
        }
        break;
    case CP500_PRESENT_REG:
        if (size == 1) {
            val = s->present;
        }
        break;
    case CP500_AXI_REG:
        if (size == 4) {
            val = s->axi;
        }
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "cp500: unimplemented read at 0x%" HWADDR_PRIx " size %d\n", addr, size);
        break;
    }
    return val;
}

static void pcibase_sys_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case CP500_RECONFIG_REG:
        if (size == 1) {
            s->reconfig = val & 0xFF;
        }
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "cp500: unimplemented write at 0x%" HWADDR_PRIx " size %d, val 0x%" PRIx64 "\n", addr, size, val);
        break;
    }
}

static uint64_t pcibase_ecm_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_ecm_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static const MemoryRegionOps pcibase_sys_mmio_ops = {
    .read = pcibase_sys_mmio_read,
    .write = pcibase_sys_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static const MemoryRegionOps pcibase_ecm_mmio_ops = {
    .read = pcibase_ecm_mmio_read,
    .write = pcibase_ecm_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static const MemoryRegionOps pcibase_mmio_ops = {  /* kept for potential generic use */
    .read = NULL,
    .write = NULL,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static const MemoryRegionOps pcibase_pio_ops = {
    .read = NULL,
    .write = NULL,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    s->version = 0x00010000;
    s->reconfig = 0;
    s->present = CP500_PRESENT_FAN0;
    s->axi = 0;
}

G_GNUC_UNUSED static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
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

    pci_set_word(pci_conf + PCI_VENDOR_ID, 0xCEBA);
    pci_set_word(pci_conf + PCI_DEVICE_ID, 0x2706);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_OTHERS);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR0: system registers and MSI-X */
    memory_region_init(&s->bar_regions[0], OBJECT(s), "cp500-bar0", CP500_SYS_BAR_SIZE);
    memory_region_init_io(&s->sys_regs, OBJECT(s), &pcibase_sys_mmio_ops, s, "cp500-sys-regs", 0x1000);
    memory_region_add_subregion(&s->bar_regions[0], 0, &s->sys_regs);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_regions[0]);

    /* BAR1: ECM (dummy) */
    memory_region_init_io(&s->bar_regions[1], OBJECT(s), &pcibase_ecm_mmio_ops, s, "cp500-ecm-bar", CP500_ECM_BAR_SIZE);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_regions[1]);

    if (msix_init(pdev, CP500_NUM_MSIX, &s->bar_regions[0], 0, 0x1000, &s->bar_regions[0], 0, 0x2000, 0x80, errp)) {
        error_setg(errp, "msix_init failed");
        return;
    }

    /* Register defaults */
    s->version = 0x00010000;
    s->reconfig = 0;
    s->present = CP500_PRESENT_FAN0;
    s->axi = 0;
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
    .name = "cp500_pci",
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