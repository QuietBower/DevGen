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


#define TYPE_PCIBASE_DEVICE "loongson_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_LOONGSON 0x0014
#define LSDC_DEVICE_ID_7A1000  0x7a06

#define LSDC_NUM_CRTC           2

enum loongson_chip_id {
    CHIP_LS7A1000 = 0,
    CHIP_LS7A2000 = 1,
    CHIP_LS_LAST,
};

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
    uint32_t irq_status;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    
};

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
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

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE || bi->size == 0) {
        return;
    }
    
    /* CRITICAL: PCI requires BAR sizes to be a power of 2. Prevent QEMU assert crash. */
    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

#define TYPE_LOONGSON_GPU "loongson_gpu"
typedef struct LoongsonGPUState {
    PCIDevice parent_obj;
    MemoryRegion vram;
} LoongsonGPUState;

static void loongson_gpu_realize(PCIDevice *pdev, Error **errp)
{
    LoongsonGPUState *s = (LoongsonGPUState *)pdev;
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_LOONGSON);
    pci_set_word(pci_conf + PCI_DEVICE_ID, 0x7a15);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_VENDOR_ID, PCI_VENDOR_ID_LOONGSON);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_ID, 0x7a15);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_DISPLAY_VGA);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);

    /* 
     * Do NOT set QEMU_PCI_CAP_EXPRESS for the GPU device.
     * The driver strictly requires the GPU to be at PCI_DEVFN(6, 0) on the same bus.
     * If the bus is a conventional PCI bus (e.g. pcie-pci-bridge), marking this as PCIe
     * will cause QEMU to reject it. By keeping it as a conventional PCI device, it can
     * successfully realize at slot 6 and be enumerated by the guest kernel.
     */

    memory_region_init_ram(&s->vram, OBJECT(s), "gpu-vram", 16 * MiB, errp);
    /* Use 32-bit memory BAR to ensure successful allocation on all bus types */
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->vram);
}

static void loongson_gpu_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = loongson_gpu_realize;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo loongson_gpu_info = {
    .name = TYPE_LOONGSON_GPU,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(LoongsonGPUState),
    .class_init = loongson_gpu_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_PCIE_DEVICE },
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_LOONGSON );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  LSDC_DEVICE_ID_7A1000 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_DISPLAY_VGA );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 1;  
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x10000; /* Default size */
    s->bar_info[0].name = "lsdc";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Create the GPU device at devfn 6.0 (0x30) if it doesn't exist */
    PCIBus *bus = pci_get_bus(pdev);
    PCIDevice *gpu = pci_new(PCI_DEVFN(6, 0), TYPE_LOONGSON_GPU);
    if (!pci_realize_and_unref(gpu, bus, NULL)) {
        /* Fallback if bus rejects slot 6 (e.g. PCIe Root Port) */
        PCIBus *root = pci_device_root_bus(pdev);
        if (root && root != bus) {
            gpu = pci_new(PCI_DEVFN(6, 0), TYPE_LOONGSON_GPU);
            pci_realize_and_unref(gpu, root, NULL);
        }
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

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "loongson_pci",
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
    type_register_static(&loongson_gpu_info);
}

type_init(pcibase_register_types);
