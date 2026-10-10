/*
 * Integrated QEMU PCI device template (QEMU 8.2.10).
 * Designed for register-level modeling and PCIe driver probing.
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

#define PCI_VENDOR_ID_NS		0x100b
#define PCI_DEVICE_ID_NS_87415		0x0002

#define NS87415_IORDY_DMA_REG 0x42
#define NS87415_STATUS_REG    0x43
#define NS87415_TIMING_REG    0x44

/* Standard ATA BMDMA Offsets used by driver */
#define ATA_DMA_CMD         0x00
#define ATA_DMA_TABLE_OFS   0x04
#define ATA_DMA_START       0x01
#define ATA_DMA_ERR         0x02
#define ATA_DMA_INTR        0x04
#define ATA_DMA_WR          0x08

#define TYPE_PCIBASE_DEVICE "pata_ns87415_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_PIO_IDE,
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

    /* BMDMA State */
    uint8_t bmdma_cmd[2];
    uint32_t bmdma_prd[2];
};

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static uint64_t pcibase_ide_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_ide_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    int port = (addr & 0x08) >> 3;
    hwaddr offset = addr & 0x07;

    if (offset == ATA_DMA_CMD && size == 1) {
        return s->bmdma_cmd[port];
    }
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    int port = (addr & 0x08) >> 3;
    hwaddr offset = addr & 0x07;

    if (offset == ATA_DMA_CMD && size == 1) {
        /* Driver quirk: writes INTR and ERR to CMD to clear them */
        uint8_t clear_mask = val & (ATA_DMA_INTR | ATA_DMA_ERR);
        s->bmdma_cmd[port] &= ~clear_mask;

        /* Update other bits */
        uint8_t set_mask = val & ~(ATA_DMA_INTR | ATA_DMA_ERR);
        s->bmdma_cmd[port] = (s->bmdma_cmd[port] & (ATA_DMA_INTR | ATA_DMA_ERR)) | set_mask;
    } else if (offset == ATA_DMA_TABLE_OFS && size == 4) {
        s->bmdma_prd[port] = val;
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static const MemoryRegionOps pcibase_ide_ops = {
    .read = pcibase_ide_read,
    .write = pcibase_ide_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
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
    s->bmdma_cmd[1] = 0;
    s->bmdma_prd[0] = 0;
    s->bmdma_prd[1] = 0;
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
    } else if (bi->type == BAR_TYPE_PIO_IDE) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_ide_ops, s, bi->name, aligned_size);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_NS );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_NS_87415 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0101 );
    pci_set_byte(pci_conf + PCI_CLASS_PROG, 0x8f); /* Native mode, bus master */
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Allow driver to write to specific config registers */
    pdev->wmask[0x42] = 0xFF;
    pdev->wmask[0x44] = 0xFF;
    pdev->wmask[0x45] = 0xFF;
    pdev->wmask[0x46] = 0xFF;
    pdev->wmask[0x47] = 0xFF;
    pdev->wmask[0x48] = 0xFF;
    pdev->wmask[0x49] = 0xFF;
    pdev->wmask[0x4A] = 0xFF;
    pdev->wmask[0x4B] = 0xFF;
    pdev->wmask[0x54] = 0xFF;
    pdev->wmask[0x55] = 0xFF;

    /* Ensure status register 0x43 has bits 0 and 1 clear so driver doesn't hang */
    pci_conf[0x43] = 0x00;

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* Setup BARs */
    s->num_bars = 5;
    
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_PIO_IDE;
    s->bar_info[0].size = 8;
    s->bar_info[0].name = "ide0-cmd";

    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_PIO_IDE;
    s->bar_info[1].size = 4;
    s->bar_info[1].name = "ide0-ctrl";

    s->bar_info[2].index = 2;
    s->bar_info[2].type = BAR_TYPE_PIO_IDE;
    s->bar_info[2].size = 8;
    s->bar_info[2].name = "ide1-cmd";

    s->bar_info[3].index = 3;
    s->bar_info[3].type = BAR_TYPE_PIO_IDE;
    s->bar_info[3].size = 4;
    s->bar_info[3].name = "ide1-ctrl";

    s->bar_info[4].index = 4;
    s->bar_info[4].type = BAR_TYPE_PIO;
    s->bar_info[4].size = 16;
    s->bar_info[4].name = "bmdma";

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
    .name = "pata_ns87415_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT8_ARRAY(bmdma_cmd, PCIBaseState, 2),
        VMSTATE_UINT32_ARRAY(bmdma_prd, PCIBaseState, 2),
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
