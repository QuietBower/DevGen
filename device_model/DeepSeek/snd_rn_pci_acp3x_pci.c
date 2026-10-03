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

#define TYPE_PCIBASE_DEVICE "snd_rn_pci_acp3x_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x1022
#define DEVICE_ID 0x15E2
#define CLASS_ID  0x0480

/* Register offsets relative to BAR0 base */
#define ACP_SOFT_RESET_OFFSET       0x1000
#define ACP_CONTROL_OFFSET          0x1004
#define ACP_PGFSM_CONTROL_OFFSET    0x141C
#define ACP_PGFSM_STATUS_OFFSET     0x1420
#define ACP_CLKMUX_SEL_OFFSET       0x1424
#define ACP_EXTERNAL_INTR_ENB_OFFSET   0x1800
#define ACP_EXTERNAL_INTR_CNTL_OFFSET  0x1804
#define ACP_EXTERNAL_INTR_STAT_OFFSET  0x1808

/* Control/Status bit definitions */
#define ACP_PGFSM_CNTL_POWER_ON_MASK   0x01
#define ACP_PGFSM_CNTL_POWER_OFF_MASK  0x00
#define ACP_PGFSM_STATUS_MASK          0x03
#define ACP_POWER_ON_IN_PROGRESS       0x01
#define ACP_POWERED_OFF                0x02
#define ACP_SOFT_RESET_SOFTRESET_AUDDONE_MASK 0x00010001
#define ACP_ERROR_MASK 0x20000000
#define ACP_EXT_INTR_STAT_CLEAR_MASK 0xFFFFFFFF

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
    uint32_t acp_soft_reset;
    uint32_t acp_control;
    uint32_t pgfsm_control;
    uint32_t pgfsm_status;
    uint32_t clkmux_sel;
    uint32_t ext_intr_enb;
    uint32_t ext_intr_cntl;
    uint32_t ext_intr_stat;
};

/* MMIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case ACP_SOFT_RESET_OFFSET:
        val = s->acp_soft_reset;
        break;
    case ACP_CONTROL_OFFSET:
        val = s->acp_control;
        break;
    case ACP_PGFSM_CONTROL_OFFSET:
        val = s->pgfsm_control;
        break;
    case ACP_PGFSM_STATUS_OFFSET:
        val = s->pgfsm_status;
        break;
    case ACP_CLKMUX_SEL_OFFSET:
        val = s->clkmux_sel;
        break;
    case ACP_EXTERNAL_INTR_ENB_OFFSET:
        val = s->ext_intr_enb;
        break;
    case ACP_EXTERNAL_INTR_CNTL_OFFSET:
        val = s->ext_intr_cntl;
        break;
    case ACP_EXTERNAL_INTR_STAT_OFFSET:
        val = s->ext_intr_stat;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Unexpected read at offset 0x%" HWADDR_PRIx "\n", __func__, addr);
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case ACP_SOFT_RESET_OFFSET:
        s->acp_soft_reset = val;
        if (val == 1) {
            /* Hardware sets AUDDONE bits after reset completion, driver polls for it. */
            s->acp_soft_reset = ACP_SOFT_RESET_SOFTRESET_AUDDONE_MASK;
        } else if (val == 0) {
            s->acp_soft_reset = 0;
        }
        break;
    case ACP_CONTROL_OFFSET:
        s->acp_control = val;
        break;
    case ACP_PGFSM_CONTROL_OFFSET:
        s->pgfsm_control = val;
        if (val == ACP_PGFSM_CNTL_POWER_ON_MASK) {
            s->pgfsm_status = 0; /* power on */
        } else if (val == ACP_PGFSM_CNTL_POWER_OFF_MASK) {
            s->pgfsm_status = ACP_POWERED_OFF;
        }
        break;
    case ACP_PGFSM_STATUS_OFFSET:
        /* Status is read-only; writes are ignored. */
        break;
    case ACP_CLKMUX_SEL_OFFSET:
        s->clkmux_sel = val;
        break;
    case ACP_EXTERNAL_INTR_ENB_OFFSET:
        s->ext_intr_enb = val;
        break;
    case ACP_EXTERNAL_INTR_CNTL_OFFSET:
        s->ext_intr_cntl = val;
        break;
    case ACP_EXTERNAL_INTR_STAT_OFFSET:
        /* Write any value to clear the interrupt status. */
        s->ext_intr_stat = 0;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Unexpected write at offset 0x%" HWADDR_PRIx " with value 0x%" PRIx64 "\n", __func__, addr, val);
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
    s->acp_soft_reset = 0;
    s->acp_control = 0;
    s->pgfsm_control = 0;
    s->pgfsm_status = ACP_POWERED_OFF;
    s->clkmux_sel = 0;
    s->ext_intr_enb = 0;
    s->ext_intr_cntl = 0;
    s->ext_intr_stat = 0;
    s->intr_status = 0;
    s->intr_mask = 0;
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
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    if (msi_init(pdev, 0, 1, true, false, errp)) {
        return;
    }

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo){
        .index = 0,
        .type = BAR_TYPE_MMIO,
        .size = 0x10200,
        .name = "acp-mmio"
    };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Final state initialization */
    s->pgfsm_status = ACP_POWERED_OFF;
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
    .name = "snd_rn_pci_acp3x_pci",
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
