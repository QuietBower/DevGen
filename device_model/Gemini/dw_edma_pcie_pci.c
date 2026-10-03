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

#ifndef GENMASK
#define GENMASK(h, l) (((~0ULL) >> (63 - (h))) & ~((1ULL << (l)) - 1))
#endif

#define PCI_VENDOR_ID_SYNOPSYS		0x16c3
#define PCI_DEVICE_ID_SYNOPSYS_EDDA	0xedda
#define PCI_CLASS_SYSTEM_DMA		0x0801

#define TYPE_PCIBASE_DEVICE "dw_edma_pcie_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define DW_PCIE_XILINX_MDB_DEVMEM_OFF_REG_HIGH 0xc
#define DW_PCIE_XILINX_MDB_DEVMEM_OFF_REG_LOW  0x8

#define DW_PCIE_SYNOPSYS_VSEC_DMA_ID		0x6
#define DW_PCIE_SYNOPSYS_VSEC_DMA_MAP		GENMASK(2, 0)
#define DW_PCIE_SYNOPSYS_VSEC_DMA_BAR		GENMASK(10, 8)
#define DW_PCIE_SYNOPSYS_VSEC_DMA_WR_CH		GENMASK(9, 0)
#define DW_PCIE_SYNOPSYS_VSEC_DMA_RD_CH		GENMASK(25, 16)

#define PCI_VENDOR_ID_XILINX		0x10ee
#define DW_PCIE_XILINX_MDB_VSEC_DMA_ID		0x6
#define DW_PCIE_XILINX_MDB_VSEC_DMA_MAP		GENMASK(2, 0)
#define DW_PCIE_XILINX_MDB_VSEC_DMA_BAR		GENMASK(10, 8)
#define DW_PCIE_XILINX_MDB_VSEC_DMA_WR_CH	GENMASK(9, 0)
#define DW_PCIE_XILINX_MDB_VSEC_DMA_RD_CH	GENMASK(25, 16)
#define DW_PCIE_XILINX_MDB_VSEC_ID		0x20
#define DW_PCIE_XILINX_MDB_INVALID_ADDR		(~0ULL)
#define DW_PCIE_XILINX_MDB_LL_OFF_GAP		0x200000
#define DW_PCIE_XILINX_MDB_LL_SIZE		0x800
#define DW_PCIE_XILINX_MDB_DT_OFF_GAP		0x100000
#define DW_PCIE_XILINX_MDB_DT_SIZE		0x800

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

    uint32_t devmem_off_high;
    uint32_t devmem_off_low;

    dma_addr_t sar;
    dma_addr_t dar;
    uint32_t sz;

    uint32_t status;
};

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
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
    pci_device_reset(PCI_DEVICE(dev));
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_SYNOPSYS );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_SYNOPSYS_EDDA );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_SYSTEM_DMA );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    s->num_bars = 3;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = 0x4000, .name = "dw-edma-bar0" };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_NONE };
    s->bar_info[2] = (BARInfo){ .index = 2, .type = BAR_TYPE_MMIO, .size = 0x1000000, .name = "dw-edma-bar2" };
  
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    msi_init(pdev, 0, 1, true, false, errp);
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
    .name = "dw_edma_pcie_pci",
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
