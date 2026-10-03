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

#define TYPE_PCIBASE_DEVICE "pata_hpt3x3_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI_VENDOR_ID_TTI 0x1103
#define PCI_DEVICE_ID_TTI_HPT343 0x0003

#define HPT3X3_CFG_INIT 0x80
#define HPT3X3_CFG_PIO_TIMING 0x44
#define HPT3X3_CFG_DMA_TIMING 0x48

#define HPT3X3_PORT0_BMDMA 0x00
#define HPT3X3_PORT1_BMDMA 0x08
#define HPT3X3_PORT0_CMD 0x20
#define HPT3X3_PORT1_CMD 0x28
#define HPT3X3_PORT0_CTL 0x36
#define HPT3X3_PORT1_CTL 0x3E

#define ATA_DMA_CMD 0
#define ATA_DMA_STATUS 2

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

    uint8_t bmdma_cmd[2];
    uint8_t bmdma_status[2];
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    uint64_t val = 0;

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case HPT3X3_PORT0_BMDMA + ATA_DMA_CMD:
        val = s->bmdma_cmd[0];
        break;
    case HPT3X3_PORT0_BMDMA + ATA_DMA_STATUS:
        val = s->bmdma_status[0];
        break;
    case HPT3X3_PORT1_BMDMA + ATA_DMA_CMD:
        val = s->bmdma_cmd[1];
        break;
    case HPT3X3_PORT1_BMDMA + ATA_DMA_STATUS:
        val = s->bmdma_status[1];
        break;
    default:
        break;
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case HPT3X3_PORT0_BMDMA + ATA_DMA_CMD:
        s->bmdma_cmd[0] = val;
        break;
    case HPT3X3_PORT0_BMDMA + ATA_DMA_STATUS:
        s->bmdma_status[0] = val;
        break;
    case HPT3X3_PORT1_BMDMA + ATA_DMA_CMD:
        s->bmdma_cmd[1] = val;
        break;
    case HPT3X3_PORT1_BMDMA + ATA_DMA_STATUS:
        s->bmdma_status[1] = val;
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

    s->bmdma_cmd[0] = 0;
    s->bmdma_status[0] = 0;
    s->bmdma_cmd[1] = 0;
    s->bmdma_status[1] = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_TTI);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_TTI_HPT343);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_STORAGE_IDE);
    pci_set_byte(pci_conf + PCI_CLASS_PROG, 0x85); /* Native mode */
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pci_set_word(pdev->wmask + HPT3X3_CFG_INIT, 0xFFFF);
    pci_set_long(pdev->wmask + HPT3X3_CFG_PIO_TIMING, 0xFFFFFFFF);
    pci_set_long(pdev->wmask + HPT3X3_CFG_DMA_TIMING, 0xFFFFFFFF);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    s->num_bars = 5;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_PIO, .size = 8, .name = "hpt3x3-bar0" };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_PIO, .size = 4, .name = "hpt3x3-bar1" };
    s->bar_info[2] = (BARInfo){ .index = 2, .type = BAR_TYPE_PIO, .size = 8, .name = "hpt3x3-bar2" };
    s->bar_info[3] = (BARInfo){ .index = 3, .type = BAR_TYPE_PIO, .size = 4, .name = "hpt3x3-bar3" };
    s->bar_info[4] = (BARInfo){ .index = 4, .type = BAR_TYPE_PIO, .size = 256, .name = "hpt3x3-bar4" };

    /* BAR Initialization */
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
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
    .name = "pata_hpt3x3_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT8_ARRAY(bmdma_cmd, PCIBaseState, 2),
        VMSTATE_UINT8_ARRAY(bmdma_status, PCIBaseState, 2),
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
