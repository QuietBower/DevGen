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


#define TYPE_PCIBASE_DEVICE "hisi_ptt_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#ifndef PCI_VENDOR_ID_HUAWEI
#define PCI_VENDOR_ID_HUAWEI 0x19e5
#endif

#define HISI_PTT_TUNING_CTRL		0x0000
#define HISI_PTT_TUNING_DATA		0x0004
#define HISI_PTT_TRACE_CTRL		0x0850
#define HISI_PTT_TRACE_ADDR_SIZE	0x0800
#define HISI_PTT_TRACE_ADDR_BASE_LO_0	0x0810
#define HISI_PTT_TRACE_ADDR_BASE_HI_0	0x0814
#define HISI_PTT_TRACE_INT_STAT		0x0890
#define HISI_PTT_TRACE_INT_MASK		0x0894
#define HISI_PTT_TUNING_INT_STAT	0x0898
#define HISI_PTT_TRACE_WR_STS		0x08a0
#define HISI_PTT_TRACE_STS		0x08b0
#define HISI_PTT_DEVICE_RANGE		0x0fe0
#define HISI_PTT_LOCATION		0x0fe8

#define HISI_PTT_TRACE_CTRL_RST         (1 << 1)
#define HISI_PTT_TRACE_BUF_CNT          4
#define HISI_PTT_TRACE_BUF_SIZE         (4 * 1024 * 1024)
#define HISI_PTT_TRACE_INT_STAT_MASK    0xf
#define HISI_PTT_TRACE_CTRL_TYPE_SEL    0xf0
#define HISI_PTT_TRACE_CTRL_RXTX_SEL    0xc
#define HISI_PTT_TRACE_CTRL_DATA_FORMAT (1 << 14)
#define HISI_PTT_TRACE_CTRL_TARGET_SEL  0xffff0000
#define HISI_PTT_TRACE_CTRL_FILTER_MODE (1 << 15)
#define HISI_PTT_TRACE_CTRL_EN          (1 << 0)
#define HISI_PTT_TRACE_INT_MASK_ALL     0xf
#define HISI_PTT_TRACE_DMA_IRQ          0
#define HISI_PTT_TRACE_IDLE             (1 << 0)

#define HISI_PTT_TRACE_ADDR_STRIDE	0x8
#define HISI_PTT_DEVICE_RANGE_UPPER	0xffff0000
#define HISI_PTT_DEVICE_RANGE_LOWER	0x0000ffff
#define HISI_PTT_CORE_ID		0x0000ffff
#define HISI_PTT_SICL_ID		0xffff0000
#define HISI_PTT_TUNING_INT_STAT_MASK	(1 << 0)
#define HISI_PTT_TUNING_CTRL_CODE	0x0000ffff
#define HISI_PTT_TUNING_CTRL_SUB	0x00ff0000
#define HISI_PTT_TUNING_DATA_VAL_MASK	0x0000ffff
#define HISI_PTT_TRACE_WR_STS_WRITE	0x0fffffff
#define HISI_PTT_PMU_FILTER_IS_PORT	(1 << 19)
#define HISI_PTT_PMU_FILTER_VAL_MASK	0x0000ffff
#define HISI_PTT_PMU_DIRECTION_MASK	0x00f00000
#define HISI_PTT_PMU_TYPE_MASK		0xff000000
#define HISI_PTT_PMU_FORMAT_MASK	0x0000000f00000000ULL

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
    uint32_t trace_int_mask;
    uint32_t trace_int_stat;
    uint32_t tuning_int_stat;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t tuning_ctrl;
    uint32_t tuning_data;
    uint32_t trace_ctrl;
    uint32_t trace_addr_size;
    uint32_t trace_addr_base_lo_0;
    uint32_t trace_addr_base_hi_0;
    uint32_t trace_wr_sts;
    uint32_t trace_sts;
    uint32_t device_range;
    uint32_t location;

    /* DMA Context */
    uint64_t dma_base;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool should_interrupt = false;

    if (s->trace_int_stat & ~s->trace_int_mask & HISI_PTT_TRACE_INT_STAT_MASK) {
        should_interrupt = true;
    }

    if (should_interrupt) {
        if (s->has_msi) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!s->has_msi) {
            pci_set_irq(pdev, 0);
        }
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case HISI_PTT_TUNING_CTRL:
        val = s->tuning_ctrl;
        break;
    case HISI_PTT_TUNING_DATA:
        val = s->tuning_data;
        break;
    case HISI_PTT_TRACE_CTRL:
        val = s->trace_ctrl;
        break;
    case HISI_PTT_TRACE_ADDR_SIZE:
        val = s->trace_addr_size;
        break;
    case HISI_PTT_TRACE_ADDR_BASE_LO_0:
        val = s->trace_addr_base_lo_0;
        break;
    case HISI_PTT_TRACE_ADDR_BASE_HI_0:
        val = s->trace_addr_base_hi_0;
        break;
    case HISI_PTT_TRACE_INT_STAT:
        val = s->trace_int_stat;
        break;
    case HISI_PTT_TRACE_INT_MASK:
        val = s->trace_int_mask;
        break;
    case HISI_PTT_TUNING_INT_STAT:
        val = 0; /* 0 indicates tuning finished */
        break;
    case HISI_PTT_TRACE_WR_STS:
        val = s->trace_wr_sts;
        break;
    case HISI_PTT_TRACE_STS:
        val = HISI_PTT_TRACE_IDLE; /* Always idle */
        break;
    case HISI_PTT_DEVICE_RANGE:
        val = s->device_range;
        break;
    case HISI_PTT_LOCATION:
        val = s->location;
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

    switch (addr) {
    case HISI_PTT_TUNING_CTRL:
        s->tuning_ctrl = val;
        break;
    case HISI_PTT_TUNING_DATA:
        s->tuning_data = val;
        break;
    case HISI_PTT_TRACE_CTRL:
        s->trace_ctrl = val;
        if (val & HISI_PTT_TRACE_CTRL_RST) {
            s->trace_wr_sts = 0; /* Reset clears WR_STS */
        }
        break;
    case HISI_PTT_TRACE_ADDR_SIZE:
        s->trace_addr_size = val;
        break;
    case HISI_PTT_TRACE_ADDR_BASE_LO_0:
        s->trace_addr_base_lo_0 = val;
        break;
    case HISI_PTT_TRACE_ADDR_BASE_HI_0:
        s->trace_addr_base_hi_0 = val;
        break;
    case HISI_PTT_TRACE_INT_STAT:
        s->trace_int_stat &= ~val; /* W1C */
        pcibase_update_irq(s);
        break;
    case HISI_PTT_TRACE_INT_MASK:
        s->trace_int_mask = val;
        pcibase_update_irq(s);
        break;
    default:
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

    s->tuning_ctrl = 0;
    s->tuning_data = 0;
    s->trace_ctrl = 0;
    s->trace_addr_size = 0;
    s->trace_addr_base_lo_0 = 0;
    s->trace_addr_base_hi_0 = 0;
    s->trace_int_stat = 0;
    s->trace_int_mask = HISI_PTT_TRACE_INT_MASK_ALL;
    s->trace_wr_sts = 0;
    s->device_range = 0;
    s->location = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_HUAWEI );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0xa12e );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_OTHERS );
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
    s->bar_info[0].index = 2;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x1000;
    s->bar_info[0].name = "hisi-ptt-mmio";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    if (msi_init(pdev, 0, 1, true, false, errp)) {
        return;
    }
    s->has_msi = true;
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
    .name = "hisi_ptt_pci",
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
