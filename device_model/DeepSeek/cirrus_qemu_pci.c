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

#define TYPE_PCIBASE_DEVICE "cirrus_qemu_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
/* PCI identification: Vendor, Device, Subsystem IDs */
#define PCI_VENDOR_ID_CIRRUS             0x1013
#define PCI_DEVICE_ID_CIRRUS_5446        0x00b8
#define PCI_SUBVENDOR_ID_REDHAT_QUMRANET 0x1af4
#define PCI_SUBDEVICE_ID_QEMU            0x1100
#define PCI_CLASS_DISPLAY_VGA            0x0300

/* VRAM size from driver */
#define CIRRUS_VRAM_SIZE (4 * 1024 * 1024)  /* 4 MB */

/* Placeholder for register offsets (to be provided) */
#define SEQ_INDEX  0x04
#define SEQ_DATA   0x05
#define GFX_INDEX  0x0e
#define GFX_DATA   0x0f
#define CRT_INDEX  0x14
#define CRT_DATA   0x15
#define VGA_DAC_MASK 0x06

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
    /* No interrupt structure needed at this stage */

    /* Hardware Register Shadows (The 'Identity' of the device) */
    /* VGA register storage */
    uint8_t seq_index;
    uint8_t seq[256];
    uint8_t crt_index;
    uint8_t crt[256];
    uint8_t gfx_index;
    uint8_t gfx[256];
    uint8_t dac_mask;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    /* No IRQ logic defined */
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /* No DMA transfers defined */
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* #MMIO_Read_Func# */
    switch (addr) {
    case SEQ_INDEX:
        val = s->seq_index;
        break;
    case SEQ_DATA:
        val = s->seq[s->seq_index];
        break;
    case GFX_INDEX:
        val = s->gfx_index;
        break;
    case GFX_DATA:
        val = s->gfx[s->gfx_index];
        break;
    case CRT_INDEX:
        val = s->crt_index;
        break;
    case CRT_DATA:
        val = s->crt[s->crt_index];
        break;
    case VGA_DAC_MASK:
        val = s->dac_mask;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "cirrus_qemu_pci: unimplemented MMIO read at 0x%" HWADDR_PRIx "\n", addr);
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* #MMIO_Write_Func# */
    switch (addr) {
    case SEQ_INDEX:
        s->seq_index = (uint8_t)val;
        break;
    case SEQ_DATA:
        s->seq[s->seq_index] = (uint8_t)val;
        break;
    case GFX_INDEX:
        s->gfx_index = (uint8_t)val;
        break;
    case GFX_DATA:
        s->gfx[s->gfx_index] = (uint8_t)val;
        break;
    case CRT_INDEX:
        s->crt_index = (uint8_t)val;
        break;
    case CRT_DATA:
        s->crt[s->crt_index] = (uint8_t)val;
        break;
    case VGA_DAC_MASK:
        s->dac_mask = (uint8_t)val;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "cirrus_qemu_pci: unimplemented MMIO write at 0x%" HWADDR_PRIx "\n", addr);
        break;
    }
}

/* PIO handlers removed as driver does not use PIO BARs */

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

    /* #Reset_Func# */
    /* Clear all VGA registers */
    s->seq_index = 0;
    memset(s->seq, 0, sizeof(s->seq));
    s->crt_index = 0;
    memset(s->crt, 0, sizeof(s->crt));
    s->gfx_index = 0;
    memset(s->gfx, 0, sizeof(s->gfx));
    s->dac_mask = 0;
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
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
    /* PIO type not used, so branch removed */
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_CIRRUS);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_CIRRUS_5446);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_DISPLAY_VGA);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);
    /* Set subsystem IDs to match driver expectation */
    pci_set_word(pci_conf + PCI_SUBSYSTEM_VENDOR_ID, PCI_SUBVENDOR_ID_REDHAT_QUMRANET);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_ID, PCI_SUBDEVICE_ID_QEMU);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 2;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_RAM;
    s->bar_info[0].size = CIRRUS_VRAM_SIZE;
    s->bar_info[0].name = "vram";

    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = 0x20;
    s->bar_info[1].name = "mmio";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No MSI/MSI-X init, no DMA, no timers, no additional init */
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
    /* No other cleanup needed */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "cirrus_qemu_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT8(seq_index, PCIBaseState),
        VMSTATE_BUFFER(seq, PCIBaseState),
        VMSTATE_UINT8(crt_index, PCIBaseState),
        VMSTATE_BUFFER(crt, PCIBaseState),
        VMSTATE_UINT8(gfx_index, PCIBaseState),
        VMSTATE_BUFFER(gfx, PCIBaseState),
        VMSTATE_UINT8(dac_mask, PCIBaseState),
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
