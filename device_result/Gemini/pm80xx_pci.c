/*
 * This template provides a robust skeleton for hardware emulation.
 * Integrated QEMU PCI device template (QEMU 8.2.10).
 * Replace #PLACEHOLDER# blocks with driver-specific definitions.
 * Designed for register-level modeling and PCIe driver probing.
 * NOTE: Do not directly fill this file with the Linux kernel source code.
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

/* Additional include files retrieved from driver context */

#define TYPE_PCIBASE_DEVICE "pm80xx_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PM8001_VENDOR_ID 0x11f8
#define PM8001_DEVICE_ID 0x8001
#define PM8001_CLASS_ID  0x0107
#define PM8001_MAX_MSIX_VEC 64

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

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t reg_33c0;
    uint32_t reg_33c4;
    
    uint32_t scratch_pad_0;
    uint32_t scratch_pad_1;
    uint32_t scratch_pad_2;
    uint32_t scratch_pad_3;
    uint32_t odcr;
    uint32_t odmr;
    uint32_t pcie_event_interrupt_enable;
    uint32_t pcie_event_interrupt;
    uint32_t pcie_error_interrupt_enable;
    uint32_t pcie_error_interrupt;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->has_msix) {
        msix_notify(pdev, 0);
    } else if (s->has_msi) {
        msi_notify(pdev, 0);
    } else {
        pci_set_irq(pdev, !!(s->intr_status & s->intr_mask));
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case 0x28:
        val = s->odcr;
        break;
    case 0x30:
        val = s->odmr;
        break;
    case 0x44:
        val = s->scratch_pad_0;
        break;
    case 0x48:
        val = s->scratch_pad_1;
        break;
    case 0x4C:
        val = s->scratch_pad_2;
        break;
    case 0x50:
        val = s->scratch_pad_3;
        break;
    case 0x3040:
        val = s->pcie_event_interrupt_enable;
        break;
    case 0x3044:
        val = s->pcie_event_interrupt;
        break;
    case 0x3048:
        val = s->pcie_error_interrupt_enable;
        break;
    case 0x304C:
        val = s->pcie_error_interrupt;
        break;
    case 0x33c0:
        val = s->reg_33c0;
        break;
    case 0x33c4:
        val = s->reg_33c4;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented read from offset 0x%" HWADDR_PRIx "\n", __func__, addr);
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case 0x28:
        s->odcr = val;
        break;
    case 0x30:
        s->odmr = val;
        break;
    case 0x44:
        s->scratch_pad_0 = val;
        break;
    case 0x48:
        s->scratch_pad_1 = val;
        break;
    case 0x4C:
        s->scratch_pad_2 = val;
        break;
    case 0x50:
        s->scratch_pad_3 = val;
        break;
    case 0x3040:
        s->pcie_event_interrupt_enable = val;
        break;
    case 0x3044:
        s->pcie_event_interrupt = val;
        break;
    case 0x3048:
        s->pcie_error_interrupt_enable = val;
        break;
    case 0x304C:
        s->pcie_error_interrupt = val;
        break;
    case 0x33c0:
        s->reg_33c0 = val;
        break;
    case 0x33c4:
        s->reg_33c4 = val;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented write to offset 0x%" HWADDR_PRIx "\n", __func__, addr);
        break;
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
    
    s->reg_33c0 = 0;
    s->reg_33c4 = 0;
    s->scratch_pad_0 = 0;
    s->scratch_pad_1 = 0;
    s->scratch_pad_2 = 0;
    s->scratch_pad_3 = 0;
    s->odcr = 0;
    s->odmr = 0;
    s->pcie_event_interrupt_enable = 0;
    s->pcie_event_interrupt = 0;
    s->pcie_error_interrupt_enable = 0;
    s->pcie_error_interrupt = 0;
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }
    
    /* CRITICAL: PCI requires BAR sizes to be a power of 2. Prevent QEMU assert crash. */
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PM8001_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PM8001_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PM8001_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 6;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = 0x1000, .name = "bar0" };
    s->bar_info[2] = (BARInfo){ .index = 2, .type = BAR_TYPE_MMIO, .size = 0x1000, .name = "bar2" };
    s->bar_info[4] = (BARInfo){ .index = 4, .type = BAR_TYPE_MMIO, .size = 0x1000, .name = "bar4" };
    s->bar_info[5] = (BARInfo){ .index = 5, .type = BAR_TYPE_MMIO, .size = 0x1000, .name = "bar5" };
      
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* msi_init or msix_init calls */
    s->has_msix = true;
    if (s->has_msix) {
        msix_init(pdev, PM8001_MAX_MSIX_VEC, &s->bar_regions[0], 0, 0x800, &s->bar_regions[0], 0, 0xC00, 0, errp);
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, &s->bar_regions[0], &s->bar_regions[0]);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "pm80xx_pci",
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
