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

#define TYPE_PCIBASE_DEVICE "snd_pci_acp6x_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_AMD 0x1022
#define ACP_DEVICE_ID 0x15E2
#define PCI_CLASS_MULTIMEDIA_OTHER 0x048000
#define ACP_PGFSM_CONTROL                             0x1241024
#define ACP_PGFSM_CNTL_POWER_ON_MASK	1
#define ACP_PGFSM_STATUS                              0x1241028
#define ACP_PGFSM_STATUS_MASK		3
#define ACP_POWER_ON_IN_PROGRESS	1
#define ACP_SOFT_RESET                                0x1241000
#define ACP_SOFT_RESET_SOFTRESET_AUDDONE_MASK	0x00010001
#define ACP_EXTERNAL_INTR_ENB                         0x1241A00
#define ACP_EXTERNAL_INTR_CNTL                        0x1241A04
#define ACP_EXT_INTR_STAT_CLEAR_MASK 0xFFFFFFFF
#define ACP_EXTERNAL_INTR_STAT                        0x1241A0C
#define ACP_CONTROL                                   0x1241004
#define ACP_CLKMUX_SEL                                0x124102C
#define PDM_DMA_STAT 0x10
#define ACP_PIN_CONFIG                                0x1241440
#define ACP6x_PDM_MODE		1

/* Offsets relative to BAR base (assumed base = 0x1241000) */
#define ACP_SOFT_RESET_OFF        (0x1241000 - 0x1241000) /* 0x0000 */
#define ACP_CONTROL_OFF           (0x1241004 - 0x1241000) /* 0x0004 */
#define ACP_PGFSM_CONTROL_OFF     (0x1241024 - 0x1241000) /* 0x0024 */
#define ACP_PGFSM_STATUS_OFF      (0x1241028 - 0x1241000) /* 0x0028 */
#define ACP_CLKMUX_SEL_OFF        (0x124102C - 0x1241000) /* 0x002C */
#define ACP_PIN_CONFIG_OFF        (0x1241440 - 0x1241000) /* 0x0440 */
#define ACP_EXTERNAL_INTR_ENB_OFF (0x1241A00 - 0x1241000) /* 0x0A00 */
#define ACP_EXTERNAL_INTR_CNTL_OFF (0x1241A04 - 0x1241000) /* 0x0A04 */
#define ACP_EXTERNAL_INTR_STAT_OFF (0x1241A0C - 0x1241000) /* 0x0A0C */

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
    /* Legacy IRQ status/mask */
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    struct {
        uint32_t acp_pgfsm_control;
        uint32_t acp_pgfsm_status;
        uint32_t acp_soft_reset;
        uint32_t acp_external_intr_enb;
        uint32_t acp_external_intr_cntl;
        uint32_t acp_external_intr_stat;
        uint32_t acp_control;
        uint32_t acp_clkmux_sel;
        uint32_t pdm_dma_stat;
        uint32_t acp_pin_config;
    } regs;

    /* Operational status flags */
    uint32_t status; // e.g., power state

    /* State used to handle reset sequences */
    bool reset_done;

    /* Power management state (D0-D3) */
    uint8_t power_state; // D0=0, D3=3
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t active = s->regs.acp_external_intr_stat & s->regs.acp_external_intr_enb;
    if (active) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case ACP_SOFT_RESET_OFF:
        val = s->regs.acp_soft_reset;
        break;
    case ACP_CONTROL_OFF:
        val = s->regs.acp_control;
        break;
    case ACP_PGFSM_CONTROL_OFF:
        val = s->regs.acp_pgfsm_control;
        break;
    case ACP_PGFSM_STATUS_OFF:
        val = s->regs.acp_pgfsm_status;
        break;
    case ACP_CLKMUX_SEL_OFF:
        val = s->regs.acp_clkmux_sel;
        break;
    case ACP_PIN_CONFIG_OFF:
        val = s->regs.acp_pin_config;
        break;
    case ACP_EXTERNAL_INTR_ENB_OFF:
        val = s->regs.acp_external_intr_enb;
        break;
    case ACP_EXTERNAL_INTR_CNTL_OFF:
        val = s->regs.acp_external_intr_cntl;
        break;
    case ACP_EXTERNAL_INTR_STAT_OFF:
        val = s->regs.acp_external_intr_stat;
        break;
    default:
        val = ~0ULL;
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case ACP_SOFT_RESET_OFF:
        if (val == 1) {
            s->regs.acp_soft_reset = ACP_SOFT_RESET_SOFTRESET_AUDDONE_MASK;
        } else if ((val & ACP_SOFT_RESET_SOFTRESET_AUDDONE_MASK) == 0) {
            s->regs.acp_soft_reset = 0;
        }
        break;
    case ACP_CONTROL_OFF:
        s->regs.acp_control = val;
        break;
    case ACP_PGFSM_CONTROL_OFF:
        s->regs.acp_pgfsm_control = val;
        /* If power on mask, could start power sequence, but status 0 avoids it */
        break;
    case ACP_PGFSM_STATUS_OFF:
        /* Read-only, ignore writes */
        break;
    case ACP_CLKMUX_SEL_OFF:
        s->regs.acp_clkmux_sel = val;
        break;
    case ACP_PIN_CONFIG_OFF:
        s->regs.acp_pin_config = val;
        break;
    case ACP_EXTERNAL_INTR_ENB_OFF:
        s->regs.acp_external_intr_enb = val;
        pcibase_update_irq(s);
        break;
    case ACP_EXTERNAL_INTR_CNTL_OFF:
        s->regs.acp_external_intr_cntl = val;
        break;
    case ACP_EXTERNAL_INTR_STAT_OFF:
        /* Write-1-to-clear */
        s->regs.acp_external_intr_stat &= ~((uint32_t)val);
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
    /* No PIO */
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    /* No PIO */
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

    /* Initialize registers to defaults for successful probe */
    s->regs.acp_soft_reset = 0;
    s->regs.acp_control = 0;
    s->regs.acp_pgfsm_control = 0;
    s->regs.acp_pgfsm_status = 0;
    s->regs.acp_clkmux_sel = 0;
    s->regs.acp_pin_config = 0xFFFFFFFF; /* non-matching config to trigger default path */
    s->regs.acp_external_intr_enb = 0;
    s->regs.acp_external_intr_cntl = 0;
    s->regs.acp_external_intr_stat = 0;

    s->reset_done = false;
    s->power_state = 0;
    pcibase_update_irq(s);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_AMD );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  ACP_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x8000 );
    pci_set_byte(pci_conf + 0x09, 0x04); // base class multimedia
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
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x1000; /* 4KB covers all known registers */
    s->bar_info[0].name = "acp6x-mmio";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No MSI/MSI-X */
    s->has_msi = false;
    s->has_msix = false;

    /* Initialize device state */
    s->status = 0;
    s->power_state = 0; // D0
    s->reset_done = false;
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
    .name = "snd_pci_acp6x_pci",
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