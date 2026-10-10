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

#define TYPE_PCIBASE_DEVICE "dw_xdata_pcie_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define DW_XDATA_EP_MEM_OFFSET      0x8000000
#define STATUS_DONE                 (1 << 0)
#define CONTROL_DOORBELL            (1 << 0)
#define CONTROL_IS_WRITE            (1 << 1)
#define CONTROL_PATTERN_INC         (1 << 16)
#define CONTROL_NO_ADDR_INC         (1 << 18)
#define XPERF_CONTROL_ENABLE        (1 << 5)
#define BURST_REPEAT                (1 << 31)
#define BURST_VALUE                 0x1001
#define PATTERN_VALUE               0x0
#define BAR_0                       0

#define REG_ADDR_LSB                0x000
#define REG_ADDR_MSB                0x004
#define REG_BURST_CNT               0x008
#define REG_CONTROL                 0x00c
#define REG_PATTERN                 0x010
#define REG_STATUS                  0x014
#define REG_RAM_ADDR                0x018
#define REG_RAM_PORT                0x01c
#define REG_PERF_CONTROL            0x058
#define REG_WR_CNT_LSB              0x100
#define REG_WR_CNT_MSB              0x104
#define REG_RD_CNT_LSB              0x108
#define REG_RD_CNT_MSB              0x10c

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

    uint32_t addr_lsb;
    uint32_t addr_msb;
    uint32_t burst_cnt;
    uint32_t control;
    uint32_t pattern;
    uint32_t status;
    uint32_t RAM_addr;
    uint32_t RAM_port;
    uint32_t perf_control;
    uint32_t wr_cnt_lsb;
    uint32_t wr_cnt_msb;
    uint32_t rd_cnt_lsb;
    uint32_t rd_cnt_msb;
};

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case REG_ADDR_LSB:
        val = s->addr_lsb;
        break;
    case REG_ADDR_MSB:
        val = s->addr_msb;
        break;
    case REG_BURST_CNT:
        val = s->burst_cnt;
        break;
    case REG_CONTROL:
        val = s->control;
        break;
    case REG_PATTERN:
        val = s->pattern;
        break;
    case REG_STATUS:
        val = s->status;
        break;
    case REG_RAM_ADDR:
        val = s->RAM_addr;
        break;
    case REG_RAM_PORT:
        val = s->RAM_port;
        break;
    case REG_PERF_CONTROL:
        val = s->perf_control;
        break;
    case REG_WR_CNT_LSB:
        val = s->wr_cnt_lsb++;
        break;
    case REG_WR_CNT_MSB:
        val = s->wr_cnt_msb;
        break;
    case REG_RD_CNT_LSB:
        val = s->rd_cnt_lsb++;
        break;
    case REG_RD_CNT_MSB:
        val = s->rd_cnt_msb;
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
    case REG_ADDR_LSB:
        s->addr_lsb = val;
        break;
    case REG_ADDR_MSB:
        s->addr_msb = val;
        break;
    case REG_BURST_CNT:
        s->burst_cnt = val;
        break;
    case REG_CONTROL:
        s->control = val;
        s->status |= STATUS_DONE;
        break;
    case REG_PATTERN:
        s->pattern = val;
        break;
    case REG_STATUS:
        s->status = val;
        break;
    case REG_RAM_ADDR:
        s->RAM_addr = val;
        break;
    case REG_RAM_PORT:
        s->RAM_port = val;
        break;
    case REG_PERF_CONTROL:
        s->perf_control = val;
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

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    s->addr_lsb = 0;
    s->addr_msb = 0;
    s->burst_cnt = 0;
    s->control = 0;
    s->pattern = 0;
    s->status = 0;
    s->RAM_addr = 0;
    s->RAM_port = 0;
    s->perf_control = 0;
    s->wr_cnt_lsb = 0;
    s->wr_cnt_msb = 0;
    s->rd_cnt_lsb = 0;
    s->rd_cnt_msb = 0;
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
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x16c3 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0xedda );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_OTHERS );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    s->num_bars = 1;
    s->bar_info[0].index = BAR_0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x1000;
    s->bar_info[0].name = "dw_xdata_bar0";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
}

static const VMStateDescription vmstate_pcibase = {
    .name = "dw_xdata_pcie_pci",
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
