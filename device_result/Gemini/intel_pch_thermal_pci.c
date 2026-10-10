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

#define TYPE_PCIBASE_DEVICE "intel_pch_thermal_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCH_THERMAL_DID_HSW_1   0x9C24
#define PCH_THERMAL_DID_HSW_2   0x8C24
#define PCH_THERMAL_DID_WPT     0x9CA4
#define PCH_THERMAL_DID_SKL     0x9D31
#define PCH_THERMAL_DID_SKL_H   0xA131
#define PCH_THERMAL_DID_CNL     0x9Df9
#define PCH_THERMAL_DID_CNL_H   0xA379
#define PCH_THERMAL_DID_CNL_LP  0x02F9
#define PCH_THERMAL_DID_CML_H   0X06F9
#define PCH_THERMAL_DID_LWB     0xA1B1
#define PCH_THERMAL_DID_WBG     0x8D24

#define WPT_TEMP        0x0000
#define WPT_TSC         0x04
#define WPT_TSS         0x06
#define WPT_TSEL        0x08
#define WPT_TSREL       0x0A
#define WPT_TSMIC       0x0C
#define WPT_CTT         0x0010
#define WPT_TSPM        0x001C
#define WPT_TAHV        0x0014
#define WPT_TALV        0x0018
#define WPT_TL          0x00000040
#define WPT_PHL         0x0060
#define WPT_PHLC        0x62
#define WPT_TAS         0x80
#define WPT_TSPIEN      0x82
#define WPT_TSGPEN      0x84

#define WPT_TEMP_TSR    0x01ff
#define WPT_TSC_CPDE    0x01
#define WPT_TSS_TSDSS   0x10
#define WPT_TSS_GPES    0x08
#define WPT_TSEL_ETS    0x01
#define WPT_TSEL_PLDB   0x80
#define WPT_TL_TOL      0x000001FF
#define WPT_TL_T1L      0x1ff00000
#define WPT_TL_TTEN     0x20000000

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

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint16_t wpt_temp;
    uint8_t wpt_tsc;
    uint8_t wpt_tss;
    uint8_t wpt_tsel;
    uint8_t wpt_tsrel;
    uint8_t wpt_tsmic;
    uint32_t wpt_ctt;
    uint32_t wpt_tahv;
    uint32_t wpt_talv;
    uint32_t wpt_tspm;
    uint32_t wpt_tl;
    uint16_t wpt_phl;
    uint8_t wpt_phlc;
    uint8_t wpt_tas;
    uint8_t wpt_tspien;
    uint8_t wpt_tsgpen;
};

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case WPT_TEMP:
        val = s->wpt_temp;
        break;
    case WPT_TSEL:
        val = s->wpt_tsel;
        break;
    case WPT_CTT:
        val = s->wpt_ctt;
        break;
    case WPT_PHL:
        val = s->wpt_phl;
        break;
    case WPT_TSPM:
        val = s->wpt_tspm;
        break;
    default:
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case WPT_TSEL:
        /* When TSEL's Policy Lock-Down bit is 1, TSEL become RO. */
        if (!(s->wpt_tsel & WPT_TSEL_PLDB)) {
            s->wpt_tsel = val & 0xFF;
        }
        break;
    default:
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* PCIBaseState *s = opaque; */
    uint64_t val = 0;

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* PCIBaseState *s = opaque; */
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

    /* Initialize registers to allow driver probe to succeed */
    s->wpt_temp = 0x0050; /* Arbitrary valid temperature */
    s->wpt_tsel = 0x00;   /* BIOS not enabled, not locked down */
    s->wpt_ctt  = 0x0064; /* Critical trip temp */
    s->wpt_phl  = 0x0050; /* Hot trip temp */
    s->wpt_tspm = 0x005A; /* Threshold temp */
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_INTEL );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCH_THERMAL_DID_HSW_1 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x1180 );
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
    s->bar_info[0].size = 0x1000;
    s->bar_info[0].name = "pch_thermal_mmio";
      
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

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "intel_pch_thermal_pci",
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
