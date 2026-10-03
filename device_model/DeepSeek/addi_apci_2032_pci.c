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

#define TYPE_PCIBASE_DEVICE "addi_apci_2032_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI_VENDOR_ID_ADDIDATA 0x1023
#define PCI_DEVICE_ID_APCI2032 0x1004

#define APCI2032_DO_REG         0x00
#define APCI2032_INT_CTRL_REG   0x04
#define APCI2032_INT_STATUS_REG 0x08
#define APCI2032_STATUS_REG     0x0c
#define APCI2032_WDOG_REG       0x10

#define ADDI_TCW_CTRL_REG	0x0c
#define ADDI_TCW_RELOAD_REG	0x04

/* bit masks */
#define APCI2032_INT_CTRL_VCC_ENA   (1 << 0)
#define APCI2032_INT_CTRL_CC_ENA    (1 << 1)
#define APCI2032_INT_STATUS_VCC     (1 << 0)
#define APCI2032_INT_STATUS_CC      (1 << 1)
#define APCI2032_STATUS_IRQ         (1 << 0)

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
    uint32_t intr_status; uint32_t intr_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    struct {
        uint32_t do_reg;
        uint32_t int_ctrl;
        uint32_t int_status;
        uint32_t status;
        uint32_t wdog;
    } regs;
};


/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool have_irq = (s->regs.status & APCI2032_STATUS_IRQ) != 0;
    /* Only level-sensitive legacy IRQ supported */
    pci_set_irq(pdev, have_irq ? 1 : 0);
}

/* MMIO/PIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* No MMIO BAR implemented; return 0 if accessed */
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* No MMIO BAR implemented; ignore */
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case APCI2032_DO_REG:
        val = s->regs.do_reg;
        break;
    case APCI2032_INT_CTRL_REG:
        val = s->regs.int_ctrl;
        break;
    case APCI2032_INT_STATUS_REG:
        val = s->regs.int_status & 3; /* VCC and CC only */
        break;
    case APCI2032_STATUS_REG:
        val = s->regs.status;
        break;
    case APCI2032_WDOG_REG:
        val = s->regs.wdog;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: unexpected read at offset 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        break;
    }
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t new_val = (uint32_t)val; /* driver uses 32-bit accesses */

    switch (addr) {
    case APCI2032_DO_REG:
        s->regs.do_reg = new_val;
        break;
    case APCI2032_INT_CTRL_REG:
        /* Write to INT_CTRL enables/disables interrupt sources and
         * also clears active interrupt status for those sources */
        s->regs.int_ctrl = new_val & 3; /* only VCC/CC bits */
        /* If a source is disabled, clear its status */
        s->regs.int_status &= s->regs.int_ctrl;
        /* Update combined status */
        s->regs.status = (s->regs.int_status & s->regs.int_ctrl) ?
                         APCI2032_STATUS_IRQ : 0;
        pcibase_update_irq(s);
        break;
    case APCI2032_INT_STATUS_REG:
        /* Driver never writes to this; ignore or treat as read-only */
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: unexpected write to INT_STATUS (0x%" HWADDR_PRIx ") val=0x%x\n",
                      __func__, addr, (unsigned)val);
        break;
    case APCI2032_STATUS_REG:
        /* STATUS_REG is read-only; ignore */
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: unexpected write to STATUS (0x%" HWADDR_PRIx ") val=0x%x\n",
                      __func__, addr, (unsigned)val);
        break;
    case APCI2032_WDOG_REG:
        s->regs.wdog = new_val;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: unexpected write at offset 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
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

    /* Reset all shadow registers to 0 */
    memset(&s->regs, 0, sizeof(s->regs));
    s->intr_status = 0;
    s->intr_mask = 0;

    /* Update IRQ line */
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_ADDIDATA);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_APCI2032);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0xFF00);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
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

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "addi_apci_2032_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_END_OF_LIST()
    }
};

static void pcibase_instance_init(Object *obj)
{
    PCIBaseState *s = PCIBASE_DEVICE(obj);
    /* Configure the single BAR (index 1) used by the driver */
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo) {
        .index = 1,
        .type = BAR_TYPE_PIO,
        .size = 0x20,  /* Minimal size covering register offsets 0x00-0x10 */
        .name = "apci2032-io"
    };
}

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
        .instance_init = pcibase_instance_init,
        .class_init = pcibase_class_init,
        .interfaces = interfaces,
    };

    type_register_static(&pcibase_info);
}

type_init(pcibase_register_types);
