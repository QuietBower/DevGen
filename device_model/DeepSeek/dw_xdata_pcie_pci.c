/*
 * QEMU PCIe device model for dw-xdata-pcie driver.
 * Stage 2: Implementation with MMIO register handlers.
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

/* PCI ID placeholders; values extracted from driver defines */
#define VENDOR_ID       (0x16c3) /* PCI_VENDOR_ID_SYNOPSYS */
#define DEVICE_ID       (0xedda) /* PCI_DEVICE_ID_SYNOPSYS_EDDA */
#define CLASS_ID        PCI_CLASS_OTHERS

/* Register offsets extracted from struct dw_xdata_regs */
#define REG_ADDR_LSB    0x000
#define REG_ADDR_MSB    0x004
#define REG_BURST_CNT   0x008
#define REG_CONTROL     0x00c
#define REG_PATTERN     0x010
#define REG_STATUS      0x014
#define REG_RAM_ADDR    0x018
#define REG_RAM_PORT    0x01c
#define REG_PERF_CONTROL 0x058
#define REG_WR_CNT_LSB  0x100
#define REG_WR_CNT_MSB  0x104
#define REG_RD_CNT_LSB  0x108
#define REG_RD_CNT_MSB  0x10c

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

    /* Hardware Register Shadows (The 'Identity' of the device) */
    struct {
        uint32_t addr_lsb;
        uint32_t addr_msb;
        uint32_t burst_cnt;
        uint32_t control;
        uint32_t pattern;
        uint32_t status;
        uint32_t RAM_addr;
        uint32_t RAM_port;
        uint32_t reserved0[14];
        uint32_t perf_control;
        uint32_t reserved1[41];
        uint32_t wr_cnt_lsb;
        uint32_t wr_cnt_msb;
        uint32_t rd_cnt_lsb;
        uint32_t rd_cnt_msb;
    } regs;
};

/* Bit definitions extracted from driver macros */
#define STATUS_DONE                 (1 << 0)
#define CONTROL_DOORBELL             (1 << 0)
#define CONTROL_IS_WRITE             (1 << 1)
#define CONTROL_LENGTH_MASK          (0x3FFC)  /* GENMASK(13, 2) -> 0x3FFC */
#define CONTROL_PATTERN_INC          (1 << 16)
#define CONTROL_NO_ADDR_INC          (1 << 18)
#define XPERF_CONTROL_ENABLE         (1 << 5)
#define BURST_REPEAT                 (1 << 31)
#define BURST_VALUE                  0x1001
#define PATTERN_VALUE                0x0

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case REG_ADDR_LSB:
        val = s->regs.addr_lsb;
        break;
    case REG_ADDR_MSB:
        val = s->regs.addr_msb;
        break;
    case REG_BURST_CNT:
        val = s->regs.burst_cnt;
        break;
    case REG_CONTROL:
        val = s->regs.control;
        break;
    case REG_PATTERN:
        val = s->regs.pattern;
        break;
    case REG_STATUS:
        val = s->regs.status;
        break;
    case REG_RAM_ADDR:
        val = s->regs.RAM_addr;
        break;
    case REG_RAM_PORT:
        val = s->regs.RAM_port;
        break;
    case REG_PERF_CONTROL:
        val = s->regs.perf_control;
        break;
    case REG_WR_CNT_LSB:
        val = s->regs.wr_cnt_lsb;
        break;
    case REG_WR_CNT_MSB:
        val = s->regs.wr_cnt_msb;
        break;
    case REG_RD_CNT_LSB:
        val = s->regs.rd_cnt_lsb;
        break;
    case REG_RD_CNT_MSB:
        val = s->regs.rd_cnt_msb;
        break;
    default:
        /* Reserved registers read as 0 */
        val = 0;
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case REG_ADDR_LSB:
        s->regs.addr_lsb = (uint32_t)val;
        break;
    case REG_ADDR_MSB:
        s->regs.addr_msb = (uint32_t)val;
        break;
    case REG_BURST_CNT:
        s->regs.burst_cnt = (uint32_t)val;
        break;
    case REG_CONTROL:
        s->regs.control = (uint32_t)val;
        break;
    case REG_PATTERN:
        s->regs.pattern = (uint32_t)val;
        break;
    case REG_STATUS:
        s->regs.status = (uint32_t)val;
        break;
    case REG_RAM_ADDR:
        s->regs.RAM_addr = (uint32_t)val;
        break;
    case REG_RAM_PORT:
        s->regs.RAM_port = (uint32_t)val;
        break;
    case REG_PERF_CONTROL:
        s->regs.perf_control = (uint32_t)val;
        break;
    case REG_WR_CNT_LSB:
        s->regs.wr_cnt_lsb = (uint32_t)val;
        break;
    case REG_WR_CNT_MSB:
        s->regs.wr_cnt_msb = (uint32_t)val;
        break;
    case REG_RD_CNT_LSB:
        s->regs.rd_cnt_lsb = (uint32_t)val;
        break;
    case REG_RD_CNT_MSB:
        s->regs.rd_cnt_msb = (uint32_t)val;
        break;
    default:
        /* Reserved registers, ignore writes */
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
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

    memset(&s->regs, 0, sizeof(s->regs));
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

    /* BAR Initialization */
    /* BAR0 is the only BAR used by driver; size must be enough for all registers (0x110) */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x200;  /* 512 bytes (power of 2, covers all regs) */
    s->bar_info[0].name = "bar0";

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
