/*
 * QEMU PCI Device Model for Microsoft MANA Ethernet Driver
 * Based on Linux driver gdma_main.c
 * Phase 2: Implementation
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
#include "hw/pci/pci_ids.h"

#define TYPE_PCIBASE_DEVICE "mana_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI Vendor and Device IDs */
#define PCI_VENDOR_ID_MICROSOFT  0x1414
#define MANA_PF_DEVICE_ID        0x00B9

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_CLASS_NETWORK_ETHERNET 0x0200

/* Doorbell offsets within a doorbell page */
#define DOORBELL_OFFSET_SQ       0x0
#define DOORBELL_OFFSET_RQ       0x400
#define DOORBELL_OFFSET_CQ       0x800
#define DOORBELL_OFFSET_EQ       0xFF8

/*
 * Register offsets for PF (Physical Function) - updated from driver source.
 */
#define GDMA_PF_REG_DB_PAGE_SIZE      0xD0
#define GDMA_PF_REG_DB_PAGE_OFF       0xC8
#define GDMA_SRIOV_REG_CFG_BASE_OFF   0x108
#define GDMA_PF_REG_SHM_OFF           0x70

/*
 * VF register offsets - from driver source.
 */
#define GDMA_REG_DB_PAGE_SIZE         0x10
#define GDMA_REG_DB_PAGE_OFFSET       8
#define GDMA_REG_SHM_OFFSET           0x18

/* Constants from SMC shared memory protocol */
#define SMC_APERTURE_BITS             256
#define SMC_APERTURE_SIZE             0x10000  /* Placeholder: real value depends on SMC_APERTURE_BITS */

/* Doorbell page configuration return values to pass driver checks */
#define DB_PAGE_SIZE_VAL              0x1000  /* 4KB */
#define DB_PAGE_OFF_VAL               0x1000  /* offset within BAR0 */
#define SRIOV_BASE_OFF_VAL            0x2000
#define SRIOV_SHM_OFF_VAL             0x4000

#define BAR0_SIZE                     (1 * MiB)
#define MAX_MSIX_VECTORS              16

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
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    /* Not used with MSI-X */
}

/* Device-initiated DMA logic based on driver access patterns */
static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /* Not used by driver MMIO */
}

/* MMIO Read Handler */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case GDMA_PF_REG_DB_PAGE_SIZE:
        val = DB_PAGE_SIZE_VAL;
        break;
    case GDMA_PF_REG_DB_PAGE_OFF:
        val = DB_PAGE_OFF_VAL;
        break;
    case GDMA_SRIOV_REG_CFG_BASE_OFF:
        val = SRIOV_BASE_OFF_VAL;
        break;
    case GDMA_PF_REG_SHM_OFF:
        val = SRIOV_SHM_OFF_VAL;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "mana: read from unimplemented reg 0x%" HWADDR_PRIx "\n", addr);
        break;
    }
    return val;
}

/* MMIO Write Handler */
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* The driver writes doorbell entries within the doorbell page region.
     * We log these writes for now; full doorbell emulation requires HWC protocol.
     */
    qemu_log_mask(LOG_UNIMP, "mana: write to reg 0x%" HWADDR_PRIx " val=0x%" PRIx64 "\n", addr, val);
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* Not used */
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Not used */
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
    /* Reset registers to defaults */
    s->intr_status = 0;
    s->intr_mask = 0;
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
    int ret;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_MICROSOFT);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  MANA_PF_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_ETHERNET);
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
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = BAR0_SIZE;
    s->bar_info[0].name = "mana-bar0";

    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_RAM;
    s->bar_info[1].size = 0x1000;
    s->bar_info[1].name = "mana-msix";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Interrupt setup: MSI-X */
    ret = msix_init(pdev, MAX_MSIX_VECTORS,
                    &s->bar_regions[1], /* BAR 1 for MSI-X table */
                    1, /* bar_nr */
                    0, /* msix_bar_offset */
                    &s->bar_regions[1], /* PBA in same BAR */
                    1, /* pba_bar_nr */
                    0, /* pba_offset */
                    0, /* cap_pos */
                    errp);
    if (ret < 0) {
        return;
    }
    s->has_msix = true;

    /* DMA configuration: none needed, driver uses shared memory */
    /* Timer configuration: none */
    /* Final state initialization: reset will handle */
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
    /* Additional cleanup: none */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "mana_pci",
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