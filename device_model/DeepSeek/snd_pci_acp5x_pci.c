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

#define TYPE_PCIBASE_DEVICE "snd_pci_acp5x_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register offsets extracted from driver source */
#define ACP_PGFSM_STATUS                              0x1241420
#define ACP_PGFSM_CONTROL                             0x124141C
#define ACP_SOFT_RESET                                0x1241000
#define ACP_CONTROL                                   0x1241004
#define ACP_CLKMUX_SEL                                0x1241424
#define ACP_EXTERNAL_INTR_ENB                         0x1241800
#define ACP_EXTERNAL_INTR_STAT                        0x1241808
#define ACP_EXTERNAL_INTR_CNTL                        0x1241804
#define ACP_PIN_CONFIG                                0x1241400

#define ACP_PGFSM_CNTL_POWER_ON_MASK	0x01
#define ACP_PGFSM_STATUS_MASK		0x03
#define ACP_POWER_ON_IN_PROGRESS	0x01
#define ACP_POWERED_ON			0x00
#define ACP_SOFT_RESET_SOFTRESET_AUDDONE_MASK	0x00010001
#define ACP_EXT_INTR_STAT_CLEAR_MASK 0xFFFFFFFF
#define I2S_MODE	0x04

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x1022
#define ACP_DEVICE_ID 0x15E2
#define DEVICE_ID ACP_DEVICE_ID
#define CLASS_ID PCI_CLASS_MULTIMEDIA_OTHER

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
    uint32_t intr_enb;        /* ACP_EXTERNAL_INTR_ENB */
    uint32_t intr_stat;       /* ACP_EXTERNAL_INTR_STAT */
    uint32_t intr_cntl;       /* ACP_EXTERNAL_INTR_CNTL */
    bool irq_asserted;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    struct {
        uint32_t pgfsm_status;    /* ACP_PGFSM_STATUS */
        uint32_t pgfsm_control;   /* ACP_PGFSM_CONTROL */
        uint32_t soft_reset;      /* ACP_SOFT_RESET */
        uint32_t control;         /* ACP_CONTROL */
        uint32_t clkmux_sel;      /* ACP_CLKMUX_SEL */
        uint32_t pin_config;      /* ACP_PIN_CONFIG */
    } regs;
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool pending = (s->intr_enb & s->intr_stat) != 0;
    if (pending) {
        if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else if (msix_enabled(pdev)) {
            msix_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
            s->irq_asserted = true;
        }
    } else {
        if (!msi_enabled(pdev) && !msix_enabled(pdev) && s->irq_asserted) {
            pci_set_irq(pdev, 0);
            s->irq_asserted = false;
        }
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case ACP_PGFSM_STATUS:
        val = s->regs.pgfsm_status;
        break;
    case ACP_PGFSM_CONTROL:
        val = s->regs.pgfsm_control;
        break;
    case ACP_SOFT_RESET:
        val = s->regs.soft_reset;
        break;
    case ACP_CONTROL:
        val = s->regs.control;
        break;
    case ACP_CLKMUX_SEL:
        val = s->regs.clkmux_sel;
        break;
    case ACP_EXTERNAL_INTR_ENB:
        val = s->intr_enb;
        break;
    case ACP_EXTERNAL_INTR_STAT:
        val = s->intr_stat;
        break;
    case ACP_EXTERNAL_INTR_CNTL:
        val = s->intr_cntl;
        break;
    case ACP_PIN_CONFIG:
        val = s->regs.pin_config;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "acp5x: unsupported read 0x%" HWADDR_PRIx "\n", addr);
        val = 0;
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case ACP_PGFSM_CONTROL:
        s->regs.pgfsm_control = val;
        if (val & ACP_PGFSM_CNTL_POWER_ON_MASK) {
            /* Simulate immediate power on */
            s->regs.pgfsm_status = ACP_POWERED_ON;
        }
        break;
    case ACP_PGFSM_STATUS:
        s->regs.pgfsm_status = val;
        break;
    case ACP_SOFT_RESET:
        s->regs.soft_reset = val;
        if (val & 1) {
            /* Start reset: set AUDDONE bit to indicate completion */
            s->regs.soft_reset |= ACP_SOFT_RESET_SOFTRESET_AUDDONE_MASK;
        } else {
            /* Clear reset: clear all bits including AUDDONE */
            s->regs.soft_reset = 0;
        }
        break;
    case ACP_CONTROL:
        s->regs.control = val;
        break;
    case ACP_CLKMUX_SEL:
        s->regs.clkmux_sel = val;
        break;
    case ACP_EXTERNAL_INTR_ENB:
        s->intr_enb = val;
        pcibase_update_irq(s);
        break;
    case ACP_EXTERNAL_INTR_STAT:
        /* Write-1-to-Clear: clear bits written as 1 */
        s->intr_stat &= ~val;
        pcibase_update_irq(s);
        break;
    case ACP_EXTERNAL_INTR_CNTL:
        s->intr_cntl = val;
        break;
    case ACP_PIN_CONFIG:
        /* readonly, ignore write */
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "acp5x: unsupported write 0x%" HWADDR_PRIx " = %" PRIx64 "\n", addr, val);
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

    s->regs.pgfsm_status = 0;        /* powered off */
    s->regs.pgfsm_control = 0;
    s->regs.soft_reset = 0;
    s->regs.control = 0;
    s->regs.clkmux_sel = 0;
    /* pin_config is set by realize */
    s->intr_enb = 0;
    s->intr_stat = 0;
    s->intr_cntl = 0;
    if (s->irq_asserted) {
        s->irq_asserted = false;
        pci_set_irq(PCI_DEVICE(s), 0);
    }
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

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x2000000; /* 32 MiB, enough for ACP_PIN_CONFIG */
    s->bar_info[0].name = "acp5x-mmio";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Final state initialization before the device is 'live' */
    s->regs.pin_config = I2S_MODE; /* to pass probe mode check */
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

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "snd_pci_acp5x_pci",
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

type_init(pcibase_register_types)
