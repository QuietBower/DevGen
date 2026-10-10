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

#define TYPE_PCIBASE_DEVICE "ptdma_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define CMD_CLK_GATE_CTL_OFFSET		0x6004
#define CMD_REQID_CONFIG_OFFSET		0x04
#define CMD_QUEUE_PRIO_OFFSET		0x00
#define CMD_TIMEOUT_OFFSET		0x08
#define CMD_CONFIG_OFFSET		0x1120

#define CMD_Q_STATUS_INCR		0x1000
#define CMD_CONFIG_VHB_EN		(1 << 0)
#define CMD_TIMEOUT_DISABLE		0
#define CMD_CONFIG_REQID		0
#define CMD_DESC_DW0_VAL		0x500012

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
    uint32_t int_status;
    uint32_t q_int_status;
    uint32_t q_int_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t cmd_clk_gate_ctl;
    uint32_t cmd_reqid_config;
    uint32_t cmd_queue_prio;
    uint32_t cmd_timeout;
    uint32_t cmd_config;
    uint32_t qcontrol;
    uint32_t q_status;
    uint32_t cmd_error;
    uint32_t qdma_tail_lo;
    uint32_t qdma_head_lo;

    /* DMA Context */
    uint64_t qbase_dma;
    uint64_t qdma_tail;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->has_msix) {
        msix_notify(pdev, 0);
    } else if (s->has_msi) {
        msi_notify(pdev, 0);
    } else {
        pci_set_irq(pdev, 1);
    }
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case CMD_CLK_GATE_CTL_OFFSET:
        val = s->cmd_clk_gate_ctl;
        break;
    case CMD_REQID_CONFIG_OFFSET:
        val = s->cmd_reqid_config;
        break;
    case CMD_QUEUE_PRIO_OFFSET:
        val = s->cmd_queue_prio;
        break;
    case CMD_TIMEOUT_OFFSET:
        val = s->cmd_timeout;
        break;
    case CMD_CONFIG_OFFSET:
        val = s->cmd_config;
        break;
    case CMD_Q_STATUS_INCR:
        val = s->qcontrol;
        break;
    case CMD_Q_STATUS_INCR + 0x0004:
        val = s->qdma_tail_lo;
        break;
    case CMD_Q_STATUS_INCR + 0x0008:
        val = s->qdma_head_lo;
        break;
    case CMD_Q_STATUS_INCR + 0x000C:
        val = s->q_int_mask;
        break;
    case CMD_Q_STATUS_INCR + 0x0010:
        val = s->q_int_status;
        break;
    case CMD_Q_STATUS_INCR + 0x0100:
        val = s->q_status;
        break;
    case CMD_Q_STATUS_INCR + 0x0104:
        val = s->cmd_error;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad read at offset 0x%" HWADDR_PRIx "\n", __func__, addr);
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case CMD_CLK_GATE_CTL_OFFSET:
        s->cmd_clk_gate_ctl = val;
        break;
    case CMD_REQID_CONFIG_OFFSET:
        s->cmd_reqid_config = val;
        break;
    case CMD_QUEUE_PRIO_OFFSET:
        s->cmd_queue_prio = val;
        break;
    case CMD_TIMEOUT_OFFSET:
        s->cmd_timeout = val;
        break;
    case CMD_CONFIG_OFFSET:
        s->cmd_config = val;
        break;
    case CMD_Q_STATUS_INCR:
        s->qcontrol = val;
        break;
    case CMD_Q_STATUS_INCR + 0x0004:
        s->qdma_tail_lo = val;
        break;
    case CMD_Q_STATUS_INCR + 0x0008:
        s->qdma_head_lo = val;
        break;
    case CMD_Q_STATUS_INCR + 0x000C:
        s->q_int_mask = val;
        break;
    case CMD_Q_STATUS_INCR + 0x0010:
        s->q_int_status &= ~val; /* W1C */
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad write at offset 0x%" HWADDR_PRIx "\n", __func__, addr);
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
    
    s->cmd_clk_gate_ctl = 0;
    s->cmd_reqid_config = 0;
    s->cmd_queue_prio = 0;
    s->cmd_timeout = 0;
    s->cmd_config = 0;
    s->qcontrol = 0;
    s->q_status = 0;
    s->cmd_error = 0;
    s->qdma_tail_lo = 0;
    s->qdma_head_lo = 0;
    s->q_int_status = 0;
    s->q_int_mask = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x1022 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x1498 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0000 );
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
    s->bar_info[0].size = 0x10000; /* Placeholder size */
    s->bar_info[0].name = "ptdma-bar2";
  
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    if (msix_init(pdev, 1, &s->bar_regions[2], 2, 0x8000, &s->bar_regions[2], 2, 0x9000, 0, errp) == 0) {
        s->has_msix = true;
    } else if (msi_init(pdev, 0, 1, true, false, errp) == 0) {
        s->has_msi = true;
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, &s->bar_regions[2], &s->bar_regions[2]);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "ptdma_pci",
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
