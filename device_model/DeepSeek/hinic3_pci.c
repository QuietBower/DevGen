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

#define HINIC3_CFG_REGS_FLAG                  0x40000000
#define HINIC3_CSR_FUNC_ATTR6_ADDR            (HINIC3_CFG_REGS_FLAG + 0x18)

enum hinic3_pf_status {
	HINIC3_PF_STATUS_INIT            = 0x0,
	HINIC3_PF_STATUS_ACTIVE_FLAG     = 0x11,
	HINIC3_PF_STATUS_FLR_START_FLAG  = 0x12,
	HINIC3_PF_STATUS_FLR_FINISH_FLAG = 0x13,
};

#define HINIC3_AF6_PF_STATUS_MASK     0xFFFF
#define HINIC3_CHIP_PRESENT          1

#define TYPE_PCIBASE_DEVICE "hinic3_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define VENDOR_ID 0x19e5
#define DEVICE_ID 0x0222
#define CLASS_ID  0x0200

#define HINIC3_PF_PCI_CFG_REG_BAR  1
#define HINIC3_PCI_INTR_REG_BAR    2
#define HINIC3_PCI_MGMT_REG_BAR    3
#define HINIC3_PCI_DB_BAR          4

#define HINIC3_PF_CFG_BAR_SIZE  0
#define HINIC3_INTR_BAR_SIZE    0
#define HINIC3_MGMT_BAR_SIZE    0
#define HINIC3_DB_BAR_SIZE      0

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
    uint32_t pf_status;
    struct {
        void *dma_buf_vaddr;
        dma_addr_t dma_buf_paddr;
        uint32_t dma_cnt;
        uint32_t dma_status;
    } dma;
    uint32_t status;
    bool reset_active;
};

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    qemu_log_mask(LOG_UNIMP, "hinic3: MMIO read from unknown addr 0x%"PRIx64" size %u\n", addr, size);
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    qemu_log_mask(LOG_UNIMP, "hinic3: MMIO write to unknown addr 0x%"PRIx64" size %u, value 0x%"PRIx64"\n", addr, size, val);
}

static uint64_t pf_cfg_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    if (addr == HINIC3_CSR_FUNC_ATTR6_ADDR) {
        if (size == 4) {
            val = s->pf_status & HINIC3_AF6_PF_STATUS_MASK;
        }
    } else {
        qemu_log_mask(LOG_UNIMP, "hinic3: PF_CFG read from unknown addr 0x%"PRIx64" size %u\n", addr, size);
    }
    return val;
}

static void pf_cfg_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr == HINIC3_CSR_FUNC_ATTR6_ADDR) {
        if (size == 4) {
            s->pf_status = val & HINIC3_AF6_PF_STATUS_MASK;
        }
    } else {
        qemu_log_mask(LOG_UNIMP, "hinic3: PF_CFG write to unknown addr 0x%"PRIx64" size %u, value 0x%"PRIx64"\n", addr, size, val);
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    qemu_log_mask(LOG_UNIMP, "hinic3: PIO read from addr 0x%"PRIx64" size %u\n", addr, size);
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    qemu_log_mask(LOG_UNIMP, "hinic3: PIO write to addr 0x%"PRIx64" size %u, value 0x%"PRIx64"\n", addr, size, val);
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static const MemoryRegionOps pf_cfg_mmio_ops = {
    .read = pf_cfg_mmio_read,
    .write = pf_cfg_mmio_write,
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
    s->intr_status = 0;
    s->intr_mask = 0;
    s->status = 0;
    s->reset_active = false;
    s->pf_status = HINIC3_PF_STATUS_INIT;
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }
    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];
    if (bi->type == BAR_TYPE_MMIO) {
        if (bi->index == HINIC3_PF_PCI_CFG_REG_BAR) {
            memory_region_init_io(mr, OBJECT(s), &pf_cfg_mmio_ops, s, bi->name, aligned_size);
        } else {
            memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, aligned_size);
        }
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);
    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }
    s->num_bars = 4;
    s->bar_info[0].index = HINIC3_PF_PCI_CFG_REG_BAR;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = HINIC3_PF_CFG_BAR_SIZE;
    s->bar_info[0].name = "PF_CFG";
    s->bar_info[1].index = HINIC3_PCI_INTR_REG_BAR;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = HINIC3_INTR_BAR_SIZE;
    s->bar_info[1].name = "INTR";
    s->bar_info[2].index = HINIC3_PCI_MGMT_REG_BAR;
    s->bar_info[2].type = BAR_TYPE_MMIO;
    s->bar_info[2].size = HINIC3_MGMT_BAR_SIZE;
    s->bar_info[2].name = "MGMT";
    s->bar_info[3].index = HINIC3_PCI_DB_BAR;
    s->bar_info[3].type = BAR_TYPE_MMIO;
    s->bar_info[3].size = HINIC3_DB_BAR_SIZE;
    s->bar_info[3].name = "DB";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
    s->has_msix = true;
    s->has_msi = false;
    s->dma.dma_buf_vaddr = NULL;
    s->dma.dma_buf_paddr = 0;
    s->dma.dma_cnt = 0;
    s->dma.dma_status = 0;
    s->status = 0;
    s->reset_active = false;
    s->intr_status = 0;
    s->intr_mask = 0;
    s->pf_status = HINIC3_PF_STATUS_INIT;
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

static const VMStateDescription vmstate_pcibase = {
    .name = "hinic3_pci",
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
