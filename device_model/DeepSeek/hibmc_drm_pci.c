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

#define TYPE_PCIBASE_DEVICE "hibmc_drm_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI_VENDOR_ID_HIBMC 0x19e5
#define PCI_DEVICE_ID_HIBMC 0x1711
#define PCI_CLASS_HIBMC PCI_CLASS_DISPLAY_VGA

#define BAR0_SIZE (16 * 1024 * 1024)
#define BAR1_SIZE (2 * 1024 * 1024)

#define HIBMC_MISC_CTRL                 0x4
#define HIBMC_CURRENT_GATE              0x000040
#define HIBMC_MODE0_GATE                0x000044
#define HIBMC_MODE1_GATE                0x000048
#define HIBMC_POWER_MODE_CTRL           0x00004C
#define HIBMC_RAW_INTERRUPT             0x80290
#define HIBMC_DP_INTSTAT                0x1e0724
#define HIBMC_DP_INTCLR                 0x1e0728
#define HIBMC_DP_HOST_SERDES_CTRL       0x1f001c
#define HIBMC_DP_HOST_OFFSET            0x10000
#define HIBMC_DP_VIDEO_CTRL             0x100
#define HIBMC_DP_DPTX_GCTL0            0x708
#define HIBMC_DP_DPTX_RST_CTRL         0x700
#define HIBMC_DP_INTR_ORIGINAL_STATUS  0x728
#define HIBMC_DP_COLOR_BAR_CTRL        0x260
#define HIBMC_DP_DPTX_CLK_CTRL         0x704
#define HIBMC_DP_HDCP_CFG              0x600
#define HIBMC_DP_INTR_ENABLE           0x720
#define HIBMC_DP_AUX_REQ               0x74
#define HIBMC_DP_AUX_WR_DATA0           0x54
#define HIBMC_DP_AUX_WR_DATA2           0x5c
#define HIBMC_DP_AUX_CMD_ADDR           0x50
#define HIBMC_DP_AUX_WR_DATA3           0x60
#define HIBMC_DP_AUX_WR_DATA1           0x58
#define HIBMC_DP_AUX_STATUS             0x78
#define HIBMC_DP_AUX_RD_DATA0           0x64
#define HIBMC_DP_TIMING_SYNC_CTRL       0xFF0
#define HIBMC_DP_PMA_LANE0_OFFSET       0x18
#define HIBMC_DP_PMA_LANE1_OFFSET       0x1c
#define HIBMC_DP_LANE0_RATE_OFFSET      0x4
#define HIBMC_DP_LANE1_RATE_OFFSET      0xc
#define HIBMC_DP_LANE_STATUS_OFFSET     0x10
#define DP_SERDES_BW_8_1               0x3
#define DP_SERDES_VOL0_PRE0            0x280
#define DP_SERDES_DONE                 0x3

#define HIBMC_MIN_VECTORS  1
#define HIBMC_MAX_VECTORS  2

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

    /* device-specific registers */
    uint32_t misc_ctrl;
    uint32_t current_gate;
    uint32_t mode0_gate;
    uint32_t mode1_gate;
    uint32_t power_mode_ctrl;
    uint32_t raw_interrupt;
    uint32_t dp_intstat;
};

G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    /* No interrupts generated during probe; only needed for runtime. */
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size != 4) {
        return 0;
    }

    switch (addr) {
    case HIBMC_MISC_CTRL:
        val = s->misc_ctrl;
        break;
    case HIBMC_CURRENT_GATE:
        val = s->current_gate;
        break;
    case HIBMC_MODE0_GATE:
        val = s->mode0_gate;
        break;
    case HIBMC_MODE1_GATE:
        val = s->mode1_gate;
        break;
    case HIBMC_POWER_MODE_CTRL:
        val = s->power_mode_ctrl;
        break;
    case HIBMC_RAW_INTERRUPT:
        val = s->raw_interrupt;
        break;
    case HIBMC_DP_INTSTAT:
        val = s->dp_intstat;
        break;
    case HIBMC_DP_HOST_SERDES_CTRL:
        /* no DP hardware, return 0 */
        val = 0;
        break;
    default:
        val = 0;
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        return;
    }

    switch (addr) {
    case HIBMC_MISC_CTRL:
        s->misc_ctrl = val;
        break;
    case HIBMC_CURRENT_GATE:
        s->current_gate = val;
        break;
    case HIBMC_MODE0_GATE:
        s->mode0_gate = val;
        break;
    case HIBMC_MODE1_GATE:
        s->mode1_gate = val;
        break;
    case HIBMC_POWER_MODE_CTRL:
        s->power_mode_ctrl = val;
        break;
    case HIBMC_RAW_INTERRUPT:
        /* write-1-to-clear */
        s->raw_interrupt &= ~val;
        break;
    case HIBMC_DP_INTSTAT:
        s->dp_intstat &= ~val;
        break;
    case HIBMC_DP_INTCLR:
        s->dp_intstat &= ~val;
        break;
    default:
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    return;
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

    s->intr_status = 0;
    s->intr_mask = 0;

    s->misc_ctrl = 0;
    s->current_gate = 0;
    s->mode0_gate = 0;
    s->mode1_gate = 0;
    s->power_mode_ctrl = 0;
    s->raw_interrupt = 0;
    s->dp_intstat = 0;
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_HIBMC);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_HIBMC);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_HIBMC);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    s->num_bars = 2;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_RAM, .size = BAR0_SIZE, .name = "hibmc-vram" };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_MMIO, .size = BAR1_SIZE, .name = "hibmc-mmio" };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = true;
    s->has_msix = false;
    if (msi_init(pdev, 0, HIBMC_MAX_VECTORS, true, false, errp)) {
        return;
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
    .name = "hibmc_drm_pci",
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
