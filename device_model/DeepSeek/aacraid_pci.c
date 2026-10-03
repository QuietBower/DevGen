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
#include "hw/pci/pci_ids.h"

#define TYPE_PCIBASE_DEVICE "aacraid_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define AAC_DEVICE_VENDOR_ID  0x1028
#define AAC_DEVICE_DEVICE_ID  0x0001
#define AAC_CLASS_ID          PCI_CLASS_STORAGE_RAID

#define AAC_OIMR_OFFSET       0x34
#define AAC_OISR_OFFSET       0x30
#define AAC_IDR_OFFSET        0x20
#define AAC_ODR_OFFSET        0x2C
#define AAC_OMRx_OFFSET       0x18
#define AAC_IMRx_OFFSET       0x10

#define KERNEL_UP_AND_RUNNING 0x00000080

#define AAC_INT_ENABLE_TYPE1_MSIX  0xfffffffa
#define AAC_INT_ENABLE_TYPE1_INTX  0xfffffffb
#define AAC_INT_DISABLE_ALL        0xffffffff

#define PMC_GLOBAL_INT_BIT0        0x00000001
#define PMC_GLOBAL_INT_BIT2        0x00000004

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

    #define INT_MODE_DISABLED 0
    #define INT_MODE_INTX     1
    #define INT_MODE_MSIX     2
    uint32_t int_mode;

    struct {
        uint32_t oimr;
        uint32_t oisr;
        uint32_t idr;
        uint32_t odr;
        uint32_t omrx[2];
        uint32_t imrx[2];
    } regs;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t pending = s->regs.oisr & ~s->regs.oimr;
    if (pending && s->int_mode != INT_MODE_DISABLED) {
        if (s->int_mode == INT_MODE_MSIX && msix_enabled(pdev)) {
            msix_notify(pdev, 0);
        } else if (s->int_mode == INT_MODE_INTX) {
            pci_set_irq(pdev, 1);
        }
    } else {
        pci_set_irq(pdev, 0);
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case AAC_OISR_OFFSET:
        val = s->regs.oisr;
        break;
    case AAC_OIMR_OFFSET:
        val = s->regs.oimr;
        break;
    case AAC_ODR_OFFSET:
        val = s->regs.odr;
        break;
    case AAC_IDR_OFFSET:
        val = s->regs.idr;
        break;
    case AAC_OMRx_OFFSET:
        val = s->regs.omrx[0];
        break;
    case AAC_OMRx_OFFSET + 4:
        val = s->regs.omrx[1];
        break;
    case AAC_IMRx_OFFSET:
        val = s->regs.imrx[0];
        break;
    case AAC_IMRx_OFFSET + 4:
        val = s->regs.imrx[1];
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "aacraid: unknown MMIO read at 0x%" HWADDR_PRIx "\n", addr);
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case AAC_OISR_OFFSET:
        s->regs.oisr &= ~(val & 0xFFFFFFFF); /* W1C */
        pcibase_update_irq(s);
        break;
    case AAC_OIMR_OFFSET:
        s->regs.oimr = (uint32_t)val;
        pcibase_update_irq(s);
        break;
    case AAC_IDR_OFFSET:
        s->regs.idr = (uint32_t)val;
        break;
    case AAC_IMRx_OFFSET:
        if (val == AAC_INT_ENABLE_TYPE1_MSIX) {
            s->int_mode = INT_MODE_MSIX;
        } else if (val == AAC_INT_ENABLE_TYPE1_INTX) {
            s->int_mode = INT_MODE_INTX;
        } else if (val == AAC_INT_DISABLE_ALL) {
            s->int_mode = INT_MODE_DISABLED;
        } else {
            s->regs.imrx[0] = (uint32_t)val;
        }
        break;
    case AAC_IMRx_OFFSET + 4:
        s->regs.imrx[1] = (uint32_t)val;
        break;
    default:
        if (addr >= AAC_ODR_OFFSET && addr < AAC_ODR_OFFSET + 4) {
            /* ODR is read-only, ignore writes */
        } else if (addr >= AAC_OMRx_OFFSET && addr < AAC_OMRx_OFFSET + 8) {
            /* OMRx read-only */
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "aacraid: unknown MMIO write at 0x%" HWADDR_PRIx "\n", addr);
        }
        break;
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    s->regs.oimr = 0xFFFFFFFF;
    s->regs.oisr = 0;
    s->regs.idr = 0;
    s->regs.odr = 0;
    s->regs.omrx[0] = KERNEL_UP_AND_RUNNING;
    s->regs.omrx[1] = 0;
    s->regs.imrx[0] = 0;
    s->regs.imrx[1] = 0;
    s->int_mode = INT_MODE_DISABLED;
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
        /* Not used */
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  AAC_DEVICE_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  AAC_DEVICE_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, AAC_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    s->num_bars = 2;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = 0x2000, .name = "aac-mmio" };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_MMIO, .size = 0x2000, .name = "aac-msix" };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = false;
    s->has_msix = true;
    msix_init(pdev, 32,
              &s->bar_regions[1], 1, 0,
              &s->bar_regions[1], 1, 0x1000,
              0, errp);
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
    .name = "aacraid_pci",
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
