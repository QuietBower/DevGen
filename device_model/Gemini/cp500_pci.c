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

#define TYPE_PCIBASE_DEVICE "cp500_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_KEBA		0xCEBA
#define PCI_DEVICE_ID_KEBA_CP035	0x2706

#define CP500_SYS_BAR		0
#define CP500_ECM_BAR		1

#define CP500_VERSION_REG	0x00
#define CP500_RECONFIG_REG	0x11
#define CP500_PRESENT_REG	0x20
#define CP500_AXI_REG		0x40
#define CP500_BUILD_TEST	0x8000

#define CP500_RECFG_REQ		0x01
#define CP500_PRESENT_FAN0	0x01

#define CP500_NUM_MSIX		8
#define CP500_NUM_MSIX_NO_MMI	2
#define CP500_NUM_MSIX_NO_AXI	3
#define CP500_AXI_MSIX		3

struct cp500_dev_info {
    off_t offset;
    size_t size;
    unsigned int msix;
};

/* Sizes not explicitly defined in this snippet, using placeholders */
#ifndef CP500_SYS_BAR_SIZE
#define CP500_SYS_BAR_SIZE 0x10000
#endif
#ifndef CP500_ECM_BAR_SIZE
#define CP500_ECM_BAR_SIZE 0x1000
#endif

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
    uint32_t version_reg;
    uint32_t reconfig_reg;
    uint32_t present_reg;
    uint32_t axi_reg;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    if (s->has_msix && s->intr_status) {
        msix_notify(pdev, CP500_AXI_MSIX);
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case CP500_VERSION_REG:
        val = s->version_reg;
        break;
    case CP500_RECONFIG_REG:
        val = s->reconfig_reg;
        break;
    case CP500_PRESENT_REG:
        val = s->present_reg;
        break;
    case CP500_AXI_REG:
        val = s->axi_reg;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad read at offset 0x%" HWADDR_PRIx "\n", __func__, addr);
        break;
    }
    
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case CP500_RECONFIG_REG:
        s->reconfig_reg = val & 0xFF;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad write at offset 0x%" HWADDR_PRIx "\n", __func__, addr);
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
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

    /* Initialize registers to default values expected by the driver */
    s->version_reg = 0x00010101; /* Major 1, Minor 1, Build 1 */
    s->reconfig_reg = 0x00;
    s->present_reg = CP500_PRESENT_FAN0; /* 0x01 - Indicates Fan0 is present */
    s->axi_reg = 0x00000000;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_KEBA );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_KEBA_CP035 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_OTHERS );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 2;
    s->bar_info[0].index = CP500_SYS_BAR;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = CP500_SYS_BAR_SIZE;
    s->bar_info[0].name = "cp500-sys";

    s->bar_info[1].index = CP500_ECM_BAR;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = CP500_ECM_BAR_SIZE;
    s->bar_info[1].name = "cp500-ecm";  
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    if (msix_init(pdev, CP500_NUM_MSIX, &s->bar_regions[CP500_SYS_BAR], CP500_SYS_BAR, 0, &s->bar_regions[CP500_SYS_BAR], CP500_SYS_BAR, 0x80, 0, errp) == 0) {
        s->has_msix = true;
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

/* Minimal VMState to satisfy QEMU migration subsystems */
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
