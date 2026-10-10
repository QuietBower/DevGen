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

#define BAR1_SIZE  0x100
#define BAR2_SIZE  0x100

#define PCI7X3X_DIO_REG         0x0000
#define PCI743X_DIO_REG         0x0004
#define ADL_PT_CLRIRQ           0x0040
#define PLX9052_INTCSR          0x004c

#define PLX9052_INTCSR_LI1ENAB   BIT(0)
#define PLX9052_INTCSR_LI1STAT   BIT(2)
#define PLX9052_INTCSR_LI2ENAB   BIT(3)
#define PLX9052_INTCSR_LI2STAT   BIT(5)
#define PLX9052_INTCSR_PCIENAB   BIT(6)
#define PLX9052_INTCSR_LI1POL    BIT(1)
#define PLX9052_INTCSR_LI2POL    BIT(4)

#define LINTI1_EN_ACT_IDI0 (PLX9052_INTCSR_LI1ENAB | PLX9052_INTCSR_LI1STAT)
#define LINTI2_EN_ACT_IDI1 (PLX9052_INTCSR_LI2ENAB | PLX9052_INTCSR_LI2STAT)
#define EN_PCI_LINT2H_LINT1H (PLX9052_INTCSR_PCIENAB | PLX9052_INTCSR_LI2POL | PLX9052_INTCSR_LI1POL)

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
    uint32_t intr_status;
    uint32_t intr_mask;

    uint32_t intcsr;
    uint32_t dio_reg_0;
    uint32_t dio_reg_4;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool li1_active = (s->intcsr & LINTI1_EN_ACT_IDI0) == LINTI1_EN_ACT_IDI0;
    bool li2_active = (s->intcsr & LINTI2_EN_ACT_IDI1) == LINTI2_EN_ACT_IDI1;
    bool pci_en = s->intcsr & PLX9052_INTCSR_PCIENAB;

    if (pci_en && (li1_active || li2_active)) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

static void plx_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr == PLX9052_INTCSR && size == 4) {
        s->intcsr = val;
        pcibase_update_irq(s);
    }
}

static uint64_t plx_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t ret = 0;

    if (addr == PLX9052_INTCSR && size == 4) {
        ret = s->intcsr;
    }
    return ret;
}

static void dio_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case PCI7X3X_DIO_REG:
        if (size == 4) {
            s->dio_reg_0 = val;
        } else if (size == 2) {
            uint16_t *p = (uint16_t *)&s->dio_reg_0;
            *p = (uint16_t)val;
        }
        break;
    case PCI743X_DIO_REG:
        if (size == 4) {
            s->dio_reg_4 = val;
        } else if (size == 2) {
            uint16_t *p = (uint16_t *)&s->dio_reg_4;
            *p = (uint16_t)val;
        }
        break;
    case ADL_PT_CLRIRQ:
        s->intcsr &= ~(LINTI1_EN_ACT_IDI0 | LINTI2_EN_ACT_IDI1);
        pcibase_update_irq(s);
        break;
    }
}

static uint64_t dio_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t ret = ~0ULL;

    switch (addr) {
    case PCI7X3X_DIO_REG:
        if (size == 4) {
            ret = s->dio_reg_0;
        } else if (size == 2) {
            ret = (uint16_t)s->dio_reg_0;
        } else if (size == 1) {
            ret = (uint8_t)s->dio_reg_0;
        }
        break;
    case PCI743X_DIO_REG:
        if (size == 4) {
            ret = s->dio_reg_4;
        } else if (size == 2) {
            ret = (uint16_t)s->dio_reg_4;
        } else if (size == 1) {
            ret = (uint8_t)s->dio_reg_4;
        }
        break;
    case ADL_PT_CLRIRQ:
        ret = 0; /* read returns 0 */
        break;
    }
    return ret;
}

static const MemoryRegionOps plx_pio_ops = {
    .read = plx_pio_read,
    .write = plx_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

static const MemoryRegionOps dio_pio_ops = {
    .read = dio_pio_read,
    .write = dio_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    s->intcsr = 0;
    s->dio_reg_0 = 0;
    s->dio_reg_4 = 0;
    pcibase_update_irq(s);
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }
    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_PIO) {
        if (bi->index == 1) {
            memory_region_init_io(mr, OBJECT(s), &plx_pio_ops, s, bi->name, aligned_size);
        } else {
            memory_region_init_io(mr, OBJECT(s), &dio_pio_ops, s, bi->name, aligned_size);
        }
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x144a);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x7230);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0680);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    s->bar_info[0] = (BARInfo){ .index = 1, .type = BAR_TYPE_PIO, .size = BAR1_SIZE, .name = "adl_pci7x3x-plx" };
    s->bar_info[1] = (BARInfo){ .index = 2, .type = BAR_TYPE_PIO, .size = BAR2_SIZE, .name = "adl_pci7x3x-dio" };
    s->num_bars = 2;
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
    .name = "adl_pci7x3x_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(intcsr, PCIBaseState),
        VMSTATE_UINT32(dio_reg_0, PCIBaseState),
        VMSTATE_UINT32(dio_reg_4, PCIBaseState),
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