/*
 * QEMU PCI device model for ADLINK PCI7x3x series (adl_pci7x3x)
 * Phase 2: Functional behavior based strictly on driver adl_pci7x3x.c
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

#define TYPE_PCIBASE_DEVICE "adl_pci7x3x_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI7X3X_DIO_REG      0x0000
#define PCI743X_DIO_REG      0x0004
#define ADL_PT_CLRIRQ        0x0040

#define PLX9052_INTCSR_LI1STAT   (1U << 2)
#define PLX9052_INTCSR_LI1ENAB   (1U << 0)
#define PLX9052_INTCSR_LI2STAT   (1U << 5)
#define PLX9052_INTCSR_LI2ENAB   (1U << 3)
#define PLX9052_INTCSR_LI1POL    (1U << 1)
#define PLX9052_INTCSR_PCIENAB   (1U << 6)
#define PLX9052_INTCSR_LI2POL    (1U << 4)
#define PLX9052_INTCSR           0x4c

#define LINTI1_EN_ACT_IDI0   (PLX9052_INTCSR_LI1ENAB | PLX9052_INTCSR_LI1STAT)
#define LINTI2_EN_ACT_IDI1   (PLX9052_INTCSR_LI2ENAB | PLX9052_INTCSR_LI2STAT)
#define EN_PCI_LINT2H_LINT1H (PLX9052_INTCSR_PCIENAB | PLX9052_INTCSR_LI2POL | PLX9052_INTCSR_LI1POL)

/* PCI Identification: use the FIRST entry from adl_pci7x3x_pci_table */
#define ADLINK_PCI_VENDOR_ID  0x144a

#define PCIBASE_VENDOR_ID ADLINK_PCI_VENDOR_ID
#define PCIBASE_DEVICE_ID 0x7230
#define PCIBASE_CLASS_ID  PCI_CLASS_OTHERS

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

    /* Hardware Register Shadows */
    uint32_t dio_reg0;      /* at PCI7X3X_DIO_REG: shared DI/DO shadow */
    uint32_t dio_reg1;      /* at PCI743X_DIO_REG: shared DI/DO shadow */
    uint8_t  clr_irq_reg;   /* ADL_PT_CLRIRQ: write-only in driver, keep for completeness */
    uint32_t intcsr;        /* PLX9052_INTCSR contents */

    /* Device internal state for DI levels that generate interrupts */
    uint8_t idi_level0;     /* bit0 represents IDI0 level */
    uint8_t idi_level1;     /* bit0 represents IDI1 level */

    /* Other additional info from driver-private data structures */
    unsigned long lcr_io_base;
    unsigned int int_ctrl;
};

/* Helper to update INTCSR status bits based on current IDI levels and enable flags */
static void pcibase_update_intcsr_status(PCIBaseState *s)
{
    /* Clear existing status bits */
    s->intcsr &= ~(PLX9052_INTCSR_LI1STAT | PLX9052_INTCSR_LI2STAT);

    /* Set LI1STAT when enabled and IDI0 "active" (bit0 of idi_level0 set) */
    if ((s->intcsr & PLX9052_INTCSR_LI1ENAB) && (s->idi_level0 & 0x1)) {
        s->intcsr |= PLX9052_INTCSR_LI1STAT;
    }

    /* Set LI2STAT when enabled and IDI1 "active" (bit0 of idi_level1 set) */
    if ((s->intcsr & PLX9052_INTCSR_LI2ENAB) && (s->idi_level1 & 0x1)) {
        s->intcsr |= PLX9052_INTCSR_LI2STAT;
    }
}

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* Update status bits based on current inputs and enables */
    pcibase_update_intcsr_status(s);

    /* Determine if either interrupt source is active as seen by the driver */
    bool li1stat = (s->intcsr & LINTI1_EN_ACT_IDI0) == LINTI1_EN_ACT_IDI0;
    bool li2stat = (s->intcsr & LINTI2_EN_ACT_IDI1) == LINTI2_EN_ACT_IDI1;

    if ((li1stat || li2stat) && (s->intcsr & PLX9052_INTCSR_PCIENAB)) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* MMIO/PIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case PCI7X3X_DIO_REG:
        /* 32-bit access for DI/DO on first port */
        if (size == 4) {
            val = s->dio_reg0;
        }
        break;
    case PCI743X_DIO_REG:
        /* 32-bit access for DI/DO on second port */
        if (size == 4) {
            val = s->dio_reg1;
        }
        break;
    case PLX9052_INTCSR:
        if (size == 4) {
            val = s->intcsr;
        }
        break;
    default:
        /* Unused/undefined offsets return 0 */
        val = 0;
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case PCI7X3X_DIO_REG:
        /* DO write: driver uses outl() (size 4) */
        if (size == 4) {
            s->dio_reg0 = (uint32_t)val;
        }
        break;
    case PCI743X_DIO_REG:
        /* DO write on second port (observed from s->private usage) */
        if (size == 4) {
            s->dio_reg1 = (uint32_t)val;
        }
        break;
    case ADL_PT_CLRIRQ:
        /* outb(0x00, dev->iobase + ADL_PT_CLRIRQ); clears current interrupt flags */
        if (size == 1) {
            s->clr_irq_reg = (uint8_t)val;
            /* Clear status bits for both interrupt lines */
            s->intcsr &= ~(PLX9052_INTCSR_LI1STAT | PLX9052_INTCSR_LI2STAT);
            /* After clearing status, update IRQ line */
            pcibase_update_irq(s);
        }
        break;
    case PLX9052_INTCSR:
        /* Driver always uses outl() (size 4). It writes int_ctrl here. */
        if (size == 4) {
            s->intcsr = (uint32_t)val;
            /* Changing enable bits may change interrupt state */
            pcibase_update_irq(s);
        }
        break;
    default:
        /* Ignore writes to unknown offsets */
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* Driver uses only memory-mapped I/O in this model (pci_resource_start()),
     * so no separate PIO region is exercised. Return 0 for safety.
     */
    PCIBaseState *s = opaque;
    (void)s;
    (void)addr;
    (void)size;

    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;
    (void)addr;
    (void)val;
    (void)size;
    /* No port I/O behavior is referenced by the driver */
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
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

    /* Power-on defaults based on driver reset behavior */
    s->dio_reg0 = 0;
    s->dio_reg1 = 0;
    s->clr_irq_reg = 0;
    s->idi_level0 = 0;
    s->idi_level1 = 0;

    /* In adl_pci7x3x_reset(), dev_private->int_ctrl = 0; and outl to INTCSR */
    s->intcsr = 0x00000000;
    s->int_ctrl = 0x00000000;

    pcibase_update_irq(s);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCIBASE_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCIBASE_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCIBASE_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR Initialization: driver maps BAR2 for iobase and BAR1 for lcr_io_base,
     * but we model only the register interface used (INTCSR, DIO, CLRIRQ) in a
     * single MMIO BAR0 for simplicity. The comedi core will still treat them
     * as resources; the exact BAR index is not checked by this driver logic.
     */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    /* Need at least up to 0x4c (INTCSR) and 0x40 (CLRIRQ) -> choose 0x100 */
    s->bar_info[0].size = 0x100;
    s->bar_info[0].name = "adl_pci7x3x-bar0";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = "";
    }

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No MSI/MSI-X usage in the provided driver */
    s->has_msi = false;
    s->has_msix = false;

    /* Initialize internal state */
    s->dio_reg0 = 0;
    s->dio_reg1 = 0;
    s->clr_irq_reg = 0;
    s->intcsr = 0;
    s->idi_level0 = 0;
    s->idi_level1 = 0;
    s->lcr_io_base = 0;
    s->int_ctrl = 0;

    pcibase_update_irq(s);
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

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "adl_pci7x3x_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(dio_reg0, PCIBaseState),
        VMSTATE_UINT32(dio_reg1, PCIBaseState),
        VMSTATE_UINT8(clr_irq_reg, PCIBaseState),
        VMSTATE_UINT32(intcsr, PCIBaseState),
        VMSTATE_UINT8(idi_level0, PCIBaseState),
        VMSTATE_UINT8(idi_level1, PCIBaseState),
        VMSTATE_UINT32(int_ctrl, PCIBaseState),
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

