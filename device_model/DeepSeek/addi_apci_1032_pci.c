/*
 * QEMU model for ADDI-DATA APCI-1032 PCI device
 * Based on driver: drivers/comedi/drivers/addi_apci_1032.c
 * QEMU 8.2.10 compatible
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

#define PCI_VENDOR_ID_ADDIDATA         0x10b5
#define PCI_DEVICE_ID_APCI1032         0x1003
#define APCI1032_DI_REG               0x00
#define APCI1032_MODE1_REG            0x04
#define APCI1032_MODE2_REG            0x08
#define APCI1032_STATUS_REG           0x0c
#define APCI1032_CTRL_REG             0x10
#define APCI1032_CTRL_INT_MODE(x)     (((x) & 0x1) << 1)
#define APCI1032_CTRL_INT_OR          APCI1032_CTRL_INT_MODE(0)
#define APCI1032_CTRL_INT_AND         APCI1032_CTRL_INT_MODE(1)
#define APCI1032_CTRL_INT_ENA         BIT(2)
#define INTCSR_INTR_ASSERTED          0x800000
#define AMCC_OP_REG_INTCSR            0x38
#define APCI1032_AMCC_BAR_SIZE        256
#define APCI1032_DEVICE_BAR_SIZE      256

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

    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;
    uint32_t irq_status;

    uint32_t di;
    uint32_t mode1;
    uint32_t mode2;
    uint32_t status;
    uint32_t ctrl;
    uint32_t intcsr;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->irq_status & INTCSR_INTR_ASSERTED) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size != 4) {
        return -1;
    }

    /* BAR0: AMCC I/O space */
    if (addr >= 0 && addr < APCI1032_AMCC_BAR_SIZE) {
        if (addr == AMCC_OP_REG_INTCSR) {
            val = s->intcsr;
        }
        return val;
    }

    /* BAR1: device registers */
    if (addr >= 0 && addr < APCI1032_DEVICE_BAR_SIZE) {
        switch (addr) {
        case APCI1032_DI_REG:
            val = s->di;
            break;
        case APCI1032_MODE1_REG:
            val = s->mode1;
            break;
        case APCI1032_MODE2_REG:
            val = s->mode2;
            break;
        case APCI1032_STATUS_REG:
            val = s->status;
            /* reading status clears interrupt */
            s->irq_status = 0;
            pcibase_update_irq(s);
            break;
        case APCI1032_CTRL_REG:
            val = s->ctrl;
            break;
        default:
            break;
        }
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        return;
    }

    /* BAR0: AMCC I/O space, no writes expected */
    if (addr >= 0 && addr < APCI1032_AMCC_BAR_SIZE) {
        if (addr == AMCC_OP_REG_INTCSR) {
            s->intcsr = val;
        }
        return;
    }

    /* BAR1: device registers */
    if (addr >= 0 && addr < APCI1032_DEVICE_BAR_SIZE) {
        switch (addr) {
        case APCI1032_DI_REG:
            s->di = val;
            break;
        case APCI1032_MODE1_REG:
            s->mode1 = val;
            break;
        case APCI1032_MODE2_REG:
            s->mode2 = val;
            break;
        case APCI1032_STATUS_REG:
            s->status = val;
            break;
        case APCI1032_CTRL_REG:
            s->ctrl = val;
            break;
        default:
            break;
        }
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
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

    s->di = 0;
    s->mode1 = 0;
    s->mode2 = 0;
    s->status = 0;
    s->ctrl = 0;
    s->intcsr = 0;
    s->irq_status = 0;
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_ADDIDATA );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_APCI1032 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_OTHERS );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    s->num_bars = 2;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = APCI1032_AMCC_BAR_SIZE;
    s->bar_info[0].name = "apci1032-amcc";
    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_PIO;
    s->bar_info[1].size = APCI1032_DEVICE_BAR_SIZE;
    s->bar_info[1].name = "apci1032-device";

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

static const VMStateDescription vmstate_pcibase = {
    .name = "addi_apci_1032_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(di, PCIBaseState),
        VMSTATE_UINT32(mode1, PCIBaseState),
        VMSTATE_UINT32(mode2, PCIBaseState),
        VMSTATE_UINT32(status, PCIBaseState),
        VMSTATE_UINT32(ctrl, PCIBaseState),
        VMSTATE_UINT32(intcsr, PCIBaseState),
        VMSTATE_UINT32(irq_status, PCIBaseState),
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