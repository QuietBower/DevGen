/*
 * QEMU PCI device model for addi_apci_1032
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

#define TYPE_PCIBASE_DEVICE "addi_apci_1032_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define APCI1032_DI_REG              0x00
#define APCI1032_MODE1_REG           0x04
#define APCI1032_MODE2_REG           0x08
#define APCI1032_STATUS_REG          0x0c
#define APCI1032_CTRL_REG            0x10
#define APCI1032_CTRL_INT_MODE(x)    (((x) & 0x1) << 1)
#define APCI1032_CTRL_INT_OR         APCI1032_CTRL_INT_MODE(0)
#define APCI1032_CTRL_INT_AND        APCI1032_CTRL_INT_MODE(1)
#define BIT(nr)                      (1UL << (nr))
#define APCI1032_CTRL_INT_ENA        BIT(2)
#define INTCSR_INTR_ASSERTED         0x00800000
#define AMCC_OP_REG_INTCSR           0x38

/* From pci_device_id table: first entry */
#define PCI_VENDOR_ID_ADDIDATA       0x15B8
#define APCI1032_VENDOR_ID           PCI_VENDOR_ID_ADDIDATA
#define APCI1032_DEVICE_ID           0x1003
/* Comedi digital I/O board; use generic PCI class */
#define APCI1032_CLASS_ID            PCI_CLASS_OTHERS


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
    uint32_t mode1;
    uint32_t mode2;
    uint32_t ctrl;

    /* Additional internal state to satisfy driver behavior */
    uint32_t di_reg;        /* digital input lines */
    uint32_t status_reg;    /* status / COS register */
    uint32_t intcsr;        /* AMCC INTCSR register in BAR0 */
    bool     irq_asserted;  /* current IRQ line state */
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* The driver checks INTCSR_INTR_ASSERTED in AMCC_OP_REG_INTCSR and
     * uses the PCI interrupt line. Model a single interrupt source that
     * is asserted when INTCSR_INTR_ASSERTED bit is set and CTRL interrupt
     * enable bit is set.
     */
    bool want_irq = (s->intcsr & INTCSR_INTR_ASSERTED) && (s->ctrl & APCI1032_CTRL_INT_ENA);

    if (want_irq && !s->irq_asserted) {
        if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
        s->irq_asserted = true;
    } else if (!want_irq && s->irq_asserted) {
        if (!msi_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
        s->irq_asserted = false;
    }
}

/* MMIO/PIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* No MMIO BARs are used for this device in the driver. */
    (void)s;
    (void)addr;
    (void)size;
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* No MMIO BARs are used */
    (void)s;
    (void)addr;
    (void)val;
    (void)size;
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = 0;

    /* BAR0: AMCC I/O regs (INTCSR at 0x38) */
    if (addr < 0x40) {
        switch (addr) {
        case AMCC_OP_REG_INTCSR:
            val = s->intcsr;
            break;
        default:
            /* other AMCC regs unused by driver */
            val = 0;
            break;
        }
        return val;
    }

    /* BAR1: board registers (dev->iobase) */
    switch (addr) {
    case APCI1032_DI_REG:
        /* digital input register, driver reads via apci1032_di_insn_bits */
        val = s->di_reg;
        break;
    case APCI1032_MODE1_REG:
        val = s->mode1;
        break;
    case APCI1032_MODE2_REG:
        val = s->mode2;
        break;
    case APCI1032_STATUS_REG:
        /* reading status register in ISR also resets it in driver comment.
         * The driver calls inl() here and doesn't explicitly clear it, so we
         * model read-to-clear of pending change-of-state bits.
         */
        val = s->status_reg;
        s->status_reg = 0;
        /* Clear INTCSR interrupt asserted when status has been read */
        s->intcsr &= ~INTCSR_INTR_ASSERTED;
        pcibase_update_irq(s);
        break;
    case APCI1032_CTRL_REG:
        val = s->ctrl;
        break;
    default:
        val = 0;
        break;
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t v = (uint32_t)val;

    /* BAR0: AMCC I/O regs */
    if (addr < 0x40) {
        switch (addr) {
        case AMCC_OP_REG_INTCSR:
            /* Driver only reads this; writes are not used. Accept value but
             * only keep interrupt-related bit if set.
             */
            if (v & INTCSR_INTR_ASSERTED) {
                s->intcsr |= INTCSR_INTR_ASSERTED;
            } else {
                s->intcsr &= ~INTCSR_INTR_ASSERTED;
            }
            pcibase_update_irq(s);
            break;
        default:
            /* ignore other writes */
            break;
        }
        return;
    }

    /* BAR1: board registers */
    switch (addr) {
    case APCI1032_DI_REG:
        /* read-only from driver perspective, ignore writes */
        break;
    case APCI1032_MODE1_REG:
        s->mode1 = v;
        break;
    case APCI1032_MODE2_REG:
        s->mode2 = v;
        break;
    case APCI1032_STATUS_REG:
        /* driver never writes status explicitly; ignore or clear */
        s->status_reg = 0;
        break;
    case APCI1032_CTRL_REG:
        /* writing ctrl enables/disables interrupt; model shadow and IRQ */
        s->ctrl = v;
        pcibase_update_irq(s);
        break;
    default:
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

    /* Power-on defaults: all zeros */
    s->mode1 = 0;
    s->mode2 = 0;
    s->ctrl = 0;
    s->di_reg = 0;
    s->status_reg = 0;
    s->intcsr = 0;
    s->irq_asserted = false;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  APCI1032_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  APCI1032_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, APCI1032_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR Initialization */
    s->num_bars = 2;

    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 0x40;
    s->bar_info[0].name = "addi_apci_1032-amcc-bar0";

    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_PIO;
    s->bar_info[1].size = 0x20;
    s->bar_info[1].name = "addi_apci_1032-io-bar1";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = false;
    s->has_msix = false;

    /* Initialize register shadows */
    s->mode1 = 0;
    s->mode2 = 0;
    s->ctrl = 0;
    s->di_reg = 0;
    s->status_reg = 0;
    s->intcsr = 0;
    s->irq_asserted = false;
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

    (void)s;
}

static const VMStateDescription vmstate_pcibase = {
    .name = "addi_apci_1032_pci",
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

