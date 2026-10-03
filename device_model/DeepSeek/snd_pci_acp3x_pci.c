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

/**
 * ========================================================================
 * PCI Identification and Register Definitions (Extracted from driver)
 * ========================================================================
 */
#define VENDOR_ID        0x1022   /* PCI_VENDOR_ID_AMD */
#define DEVICE_ID        0x15e2   /* ACP3x Audio Co-Processor */
#define CLASS_ID         0x0480   /* PCI_CLASS_MULTIMEDIA_OTHER */
#define ACP3x_DEVS       4

/* Register offsets (MMIO offsets from BAR0 base) */
#define mmACP_PGFSM_STATUS                   0x1241420
#define mmACP_PGFSM_CONTROL                  0x1241424
#define mmACP_SOFT_RESET                     0x1241000
#define mmACP_PME_EN                         0x1241400
#define mmACP_EXTERNAL_INTR_ENB              0x1241500
#define mmACP_EXTERNAL_INTR_STAT             0x1241504
#define mmACP_EXTERNAL_INTR_CNTL             0x1241508
#define mmACP_I2S_PIN_CONFIG                 0x1241600

/* Register bit masks */
#define ACP_PGFSM_CNTL_POWER_ON_MASK   0x01
#define ACP_PGFSM_STATUS_MASK         0x03
#define ACP_POWER_ON_IN_PROGRESS       0x01
#define ACP_POWER_ON_DONE              0x02
#define SOFT_RESET_START               0x01
#define SOFT_RESET_DONE                0x02
#define I2S_MODE                       0x04
#define ACP3x_I2S_MODE                 0

#define ACP3x_SOFT_RESET__SoftResetAudDone_MASK  0x1

#define ACP_EXT_INTR_STAT_CLEAR_MASK    0xFFFFFFFF

#define TYPE_PCIBASE_DEVICE "snd_pci_acp3x_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

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

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware Register Shadows */
    uint32_t reg_pgfsm_status;
    uint32_t reg_pgfsm_control;
    uint32_t reg_soft_reset;
    uint32_t reg_pme_en;
    uint32_t reg_ext_intr_enb;
    uint32_t reg_ext_intr_stat;
    uint32_t reg_ext_intr_cntl;
    uint32_t reg_i2s_pin_config;

    /* Operational status */
    uint32_t status;
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t pending = s->reg_ext_intr_stat & s->reg_ext_intr_enb;
    if (pending) {
        if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else if (msix_enabled(pdev)) {
            msix_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!msi_enabled(pdev) && !msix_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

/* MMIO Read Handler */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case mmACP_PGFSM_STATUS:
        val = s->reg_pgfsm_status;
        break;
    case mmACP_PGFSM_CONTROL:
        val = s->reg_pgfsm_control;
        break;
    case mmACP_SOFT_RESET:
        val = s->reg_soft_reset;
        break;
    case mmACP_PME_EN:
        val = s->reg_pme_en;
        break;
    case mmACP_EXTERNAL_INTR_ENB:
        val = s->reg_ext_intr_enb;
        break;
    case mmACP_EXTERNAL_INTR_STAT:
        val = s->reg_ext_intr_stat;
        break;
    case mmACP_EXTERNAL_INTR_CNTL:
        val = s->reg_ext_intr_cntl;
        break;
    case mmACP_I2S_PIN_CONFIG:
        val = s->reg_i2s_pin_config;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad read addr 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        val = 0xffffffff;
        break;
    }
    return val;
}

/* MMIO Write Handler */
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case mmACP_PGFSM_STATUS:
        /* Status register is read-only; ignore writes */
        qemu_log_mask(LOG_GUEST_ERROR, "%s: write to read-only register mmACP_PGFSM_STATUS ignored\n", __func__);
        break;
    case mmACP_PGFSM_CONTROL:
        s->reg_pgfsm_control = val;
        if (val & ACP_PGFSM_CNTL_POWER_ON_MASK) {
            /* Start power-on sequence: complete immediately */
            s->reg_pgfsm_status = ACP_POWER_ON_DONE;
        }
        break;
    case mmACP_SOFT_RESET:
        if (val == SOFT_RESET_START) {
            /* Reset started, immediately complete */
            s->reg_soft_reset = SOFT_RESET_DONE;
        } else {
            s->reg_soft_reset = val;
        }
        break;
    case mmACP_PME_EN:
        s->reg_pme_en = val;
        break;
    case mmACP_EXTERNAL_INTR_ENB:
        s->reg_ext_intr_enb = val;
        pcibase_update_irq(s);
        break;
    case mmACP_EXTERNAL_INTR_STAT:
        /* W1C: write 1 to clear */
        s->reg_ext_intr_stat &= ~(val & ACP_EXT_INTR_STAT_CLEAR_MASK);
        pcibase_update_irq(s);
        break;
    case mmACP_EXTERNAL_INTR_CNTL:
        s->reg_ext_intr_cntl = val;
        break;
    case mmACP_I2S_PIN_CONFIG:
        s->reg_i2s_pin_config = val;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad write addr 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        break;
    }
}

/* PIO Read Handler (not used by driver, but kept for completeness) */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0xffffffff;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
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

    /* Initialize register shadows to default reset values */
    s->reg_pgfsm_status = ACP_POWER_ON_DONE;   /* ensure immediate power-on success */
    s->reg_pgfsm_control = 0x0;
    s->reg_soft_reset = 0x0;
    s->reg_pme_en = 0x0;
    s->reg_ext_intr_enb = 0x0;
    s->reg_ext_intr_stat = 0x0;
    s->reg_ext_intr_cntl = 0x0;
    s->reg_i2s_pin_config = I2S_MODE; /* report I2S mode */
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

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x1022);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x15e2);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0480);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x00); /* driver checks revision 0x00 */
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR Initialization: BAR0 only, large MMIO region to cover all offsets */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x2000000;  /* 32 MB, covers up to offset 0x1FFFFFF */
    s->bar_info[0].name = "acp-mmio";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No MSI/MSI-X or DMA, interrupts via legacy IRQ pin */
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
    .name = "snd_pci_acp3x_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(reg_pgfsm_status, PCIBaseState),
        VMSTATE_UINT32(reg_pgfsm_control, PCIBaseState),
        VMSTATE_UINT32(reg_soft_reset, PCIBaseState),
        VMSTATE_UINT32(reg_pme_en, PCIBaseState),
        VMSTATE_UINT32(reg_ext_intr_enb, PCIBaseState),
        VMSTATE_UINT32(reg_ext_intr_stat, PCIBaseState),
        VMSTATE_UINT32(reg_ext_intr_cntl, PCIBaseState),
        VMSTATE_UINT32(reg_i2s_pin_config, PCIBaseState),
        VMSTATE_UINT32(status, PCIBaseState),
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
