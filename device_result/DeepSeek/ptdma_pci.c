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

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x1022
#define DEVICE_ID 0x1498
#define CLASS_ID 0x080000

#define CMD_REQID_CONFIG_OFFSET 0x04
#define CMD_QUEUE_PRIO_OFFSET 0x00
#define CMD_TIMEOUT_OFFSET 0x08
#define CMD_CLK_GATE_CTL_OFFSET 0x6004
#define CMD_CONFIG_OFFSET 0x1120
#define CMD_Q_STATUS_INCR 0x1000

#define CMD_Q_LEN 32
#define Q_DESC_SIZE sizeof(struct ptdma_desc)
#define CMD_CONFIG_REQID 0
#define QUEUE_SIZE_VAL ((ffs(CMD_Q_LEN) - 2) & CMD_Q_SIZE_MASK)
#define Q_SIZE(n) (CMD_Q_LEN * (n))
#define CMD_CONFIG_VHB_EN BIT(0)
#define CMD_Q_SIZE GENMASK(7, 3)
#define PT_DMAPOOL_ALIGN BIT(5)
#define INT_COMPLETION BIT(0)
#define INT_ERROR BIT(1)
#define CMD_CLK_HW_GATE_MODE BIT(0)
#define CMD_CLK_GATE_ON_DELAY BIT(12)
#define CMD_CLK_GATE_CTL 0
#define CMD_CLK_DYN_GATING_EN BIT(0)
#define CMD_CLK_GATE_OFF_DELAY BIT(12)
#define CMD_Q_SIZE_MASK GENMASK(4, 0)
#define CMD_Q_RUN BIT(0)
#define MAX_CMD_QLEN 100
#define CMD_Q_ERROR(__qs) ((__qs) & 0x0000003f)
#define PT_ENGINE_PASSTHRU 5
#define CMD_DESC_DW0_VAL 0x500012
#define DWORD0_SOC BIT(0)
#define DWORD0_IOC BIT(1)

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

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t status;

    /* Additional register state for MMIO operations */
    uint32_t cmd_queue_prio;
    uint32_t cmd_reqid_config;
    uint32_t cmd_timeout;
    uint32_t cmd_config;
    uint32_t cmd_clk_gate_ctl;
    uint32_t qcontrol;
    uint64_t qdma_tail;
    bool qdma_lo_written;
};

struct ptdma_desc {
    uint32_t dw0;
    uint32_t length;
    uint32_t src_lo;
    struct dword3 {
        unsigned int src_hi:16;
        unsigned int src_mem:2;
        unsigned int lsb_cxt_id:8;
        unsigned int rsvd1:5;
        unsigned int fixed:1;
    } dw3;
    uint32_t dst_lo;
    struct dword5 {
        unsigned int dst_hi:16;
        unsigned int dst_mem:2;
        unsigned int rsvd1:13;
        unsigned int fixed:1;
    } dw5;
    uint32_t rsvd1;
    uint32_t rsvd2;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    if (s->intr_status & s->intr_mask) {
        if (msix_enabled(pdev)) {
            msix_notify(pdev, 0);
        } else if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!msix_enabled(pdev) && !msi_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    uint64_t val = 0;

    switch (addr) {
    case 0x1100:
    case 0x1104:
        /* Dummy reads, return 0 */
        val = 0;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "ptdma: MMIO read 0x%" HWADDR_PRIx " size %u\n", addr, size);
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case 0x00:
        s->cmd_queue_prio = val;
        break;
    case 0x04:
        s->cmd_reqid_config = val;
        break;
    case 0x08:
        s->cmd_timeout = val;
        break;
    case 0x1120:
        s->cmd_config = val;
        break;
    case 0x6004:
        s->cmd_clk_gate_ctl = val;
        break;
    case 0x1000:
        s->qcontrol = val;
        break;
    case 0x1004:
        s->qdma_tail = (s->qdma_tail & ~0xFFFFFFFFULL) | (uint32_t)val;
        s->qdma_lo_written = true;
        break;
    case 0x1008:
        s->qdma_tail = (s->qdma_tail & ~0xFFFFFFFFULL) | (uint32_t)val;
        s->qdma_lo_written = true;
        break;
    case 0x1010:
        s->intr_status &= ~((uint32_t)val);
        pcibase_update_irq(s);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "ptdma: MMIO write 0x%" HWADDR_PRIx " val 0x%" PRIx64 " size %u\n", addr, val, size);
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

    /* Revert registers to power-on defaults */
    s->intr_status = 0;
    s->intr_mask = 0;
    s->cmd_queue_prio = 0;
    s->cmd_reqid_config = 0;
    s->cmd_timeout = 0;
    s->cmd_config = 0;
    s->cmd_clk_gate_ctl = 0;
    s->qcontrol = 0;
    s->qdma_tail = 0;
    s->qdma_lo_written = false;
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
    pci_config_set_class(pci_conf, CLASS_ID);
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
    s->bar_info[0].size = 0x10000;
    s->bar_info[0].name = "ptdma-bar";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI or MSI-X init */
    if (msix_init(pdev, 1, &s->bar_regions[2], 2, 0, &s->bar_regions[2], 2, 0x1000, 0, errp)) {
        /* MSI-X failed, try MSI as fallback */
        if (msi_init(pdev, 0, 1, true, false, errp)) {
            error_propagate(errp, error_copy(NULL));
        }
    }

    /* DMA config */
    /* Nothing specific required */

    /* Final state initialization */
    s->intr_status = 0;
    s->intr_mask = 0;
    s->cmd_queue_prio = 0;
    s->cmd_reqid_config = 0;
    s->cmd_timeout = 0;
    s->cmd_config = 0;
    s->cmd_clk_gate_ctl = 0;
    s->qcontrol = 0;
    s->qdma_tail = 0;
    s->qdma_lo_written = false;
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

    /* Free buffers, stop timers, etc. */
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
