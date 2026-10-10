/*
 * QEMU device model for addi_apci_1564
 * Generated from Linux driver: drivers/comedi/drivers/addi_apci_1564.c
 * Phase 2: Implementation
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

#define TYPE_PCIBASE_DEVICE "addi_apci_1564_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI Vendor/Device IDs from driver */
#define APCI1564_VENDOR_ID       0x15B8
#define APCI1564_DEVICE_ID       0x1006

/* BAR sizes */
#define BAR0_SIZE                0x100
#define BAR1_SIZE                0x100
#define TOTAL_REG_SIZE           (BAR0_SIZE + BAR1_SIZE)

/* Register offsets (from driver) */
#define APCI1564_EEPROM_REG			0x00
#define APCI1564_EEPROM_VCC_STATUS		BIT(8)
#define APCI1564_EEPROM_TO_REV(x)		(((x) >> 4) & 0xf)
#define APCI1564_EEPROM_DI			BIT(3)
#define APCI1564_EEPROM_DO			BIT(2)
#define APCI1564_EEPROM_CS			BIT(1)
#define APCI1564_EEPROM_CLK			BIT(0)
#define APCI1564_REV1_TIMER_IOBASE		0x04
#define APCI1564_REV2_MAIN_IOBASE		0x04
#define APCI1564_REV2_TIMER_IOBASE		0x48
#define APCI1564_REV1_MAIN_IOBASE		0x00
#define APCI1564_DI_REG				0x00
#define APCI1564_DI_INT_MODE1_REG		0x04
#define APCI1564_DI_INT_MODE2_REG		0x08
#define APCI1564_DI_INT_MODE_MASK		0x000ffff0
#define APCI1564_DI_INT_STATUS_REG		0x0c
#define APCI1564_DI_IRQ_REG			0x10
#define APCI1564_DI_IRQ_ENA			BIT(2)
#define APCI1564_DI_IRQ_MODE			BIT(1)
#define APCI1564_DO_REG				0x14
#define APCI1564_DO_INT_CTRL_REG		0x18
#define APCI1564_DO_INT_CTRL_CC_INT_ENA		BIT(1)
#define APCI1564_DO_INT_CTRL_VCC_INT_ENA	BIT(0)
#define APCI1564_DO_INT_STATUS_REG		0x1c
#define APCI1564_DO_INT_STATUS_CC		BIT(1)
#define APCI1564_DO_INT_STATUS_VCC		BIT(0)
#define APCI1564_DO_IRQ_REG			0x20
#define APCI1564_DO_IRQ_INTR			BIT(0)
#define APCI1564_WDOG_IOBASE			0x24
#define APCI1564_COUNTER(x)			((x) * 0x20)
#define APCI1564_EVENT_COS			BIT(31)
#define APCI1564_EVENT_TIMER			BIT(30)
#define APCI1564_EVENT_COUNTER(x)		BIT(27 + (x))
#define APCI1564_EVENT_MASK			0xfff0000f
#define ADDI_TCW_RELOAD_REG		0x04
#define ADDI_TCW_CTRL_REG		0x0c
#define ADDI_TCW_IRQ			BIT(0)
#define ADDI_TCW_IRQ_REG		0x14
#define ADDI_TCW_STATUS_REG		0x10
#define ADDI_TCW_TIMEBASE_REG		0x08
#define ADDI_TCW_CTRL_IRQ_ENA		BIT(1)
#define ADDI_TCW_STATUS_OVERFLOW	BIT(0)
#define ADDI_TCW_CTRL_TIMER_ENA		BIT(4)
#define ADDI_TCW_VAL_REG		0x00
#define ADDI_TCW_CTRL_CNTR_ENA		BIT(19)
#define ADDI_TCW_CTRL_ENA		BIT(0)
#define ADDI_TCW_CTRL_TRIG		BIT(9)

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

    /* Hardware Register Shadows (unified for all BARs) */
    uint32_t regs[TOTAL_REG_SIZE / 4];

    /* Interrupt state */
    uint32_t intr_status;
    uint32_t intr_mask;
};

/* Helper function to update IRQ line based on device state */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool irq = false;

    /* Digital input interrupts */
    uint32_t di_irq = s->regs[APCI1564_DI_IRQ_REG >> 2];
    uint32_t di_status = s->regs[APCI1564_DI_INT_STATUS_REG >> 2];
    if ((di_irq & APCI1564_DI_IRQ_ENA) && (di_status & APCI1564_DI_INT_MODE_MASK)) {
        irq = true;
    }

    /* Timer interrupt (located in BAR0 at offset 0x48) */
    uint32_t timer_ctrl_index = (APCI1564_REV2_TIMER_IOBASE + ADDI_TCW_CTRL_REG) >> 2;
    uint32_t timer_irq_index  = (APCI1564_REV2_TIMER_IOBASE + ADDI_TCW_IRQ_REG) >> 2;
    uint32_t timer_ctrl = s->regs[timer_ctrl_index];
    uint32_t timer_irq  = s->regs[timer_irq_index];
    if ((timer_ctrl & ADDI_TCW_CTRL_IRQ_ENA) && (timer_irq & ADDI_TCW_IRQ)) {
        irq = true;
    }

    /* Counter interrupts (located in BAR1, each at offset 0x00, 0x20, 0x40) */
    for (int i = 0; i < 3; i++) {
        uint32_t ctrl_index = (BAR0_SIZE >> 2) + (APCI1564_COUNTER(i) + ADDI_TCW_CTRL_REG) / 4;
        uint32_t irq_index   = (BAR0_SIZE >> 2) + (APCI1564_COUNTER(i) + ADDI_TCW_IRQ_REG) / 4;
        uint32_t cntrl = s->regs[ctrl_index];
        uint32_t cirq  = s->regs[irq_index];
        if ((cntrl & ADDI_TCW_CTRL_IRQ_ENA) && (cirq & ADDI_TCW_IRQ)) {
            irq = true;
        }
    }

    pci_set_irq(pdev, irq ? 1 : 0);
}

/* BAR0 PIO read handler */
static uint64_t bar0_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = 0;

    if (size != 4) {
        return ~0ULL;
    }

    uint32_t reg_off = addr >> 2;
    if (reg_off >= (BAR0_SIZE >> 2)) {
        return ~0ULL;
    }

    /* EEPROM register: returns fixed value indicating revision 2 */
    if (reg_off == (APCI1564_EEPROM_REG >> 2)) {
        val = 0x20; /* revision 2 */
    } else {
        val = s->regs[reg_off];
    }

    return val;
}

/* BAR0 PIO write handler */
static void bar0_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        return;
    }

    uint32_t reg_off = addr >> 2;
    if (reg_off >= (BAR0_SIZE >> 2)) {
        return;
    }

    /* EEPROM is read-only */
    if (reg_off == (APCI1564_EEPROM_REG >> 2)) {
        return;
    }

    /* DI IRQ register: side effects on clearing enable */
    if (reg_off == (APCI1564_DI_IRQ_REG >> 2)) {
        uint32_t di_irq_new = (uint32_t)val;
        s->regs[APCI1564_DI_IRQ_REG >> 2] = di_irq_new;
        if (!(di_irq_new & APCI1564_DI_IRQ_ENA)) {
            /* Clear digital input interrupt status */
            s->regs[APCI1564_DI_INT_STATUS_REG >> 2] = 0;
        }
    } else if (reg_off == (APCI1564_WDOG_IOBASE + ADDI_TCW_CTRL_REG) >> 2) {
        /* Watchdog control register write clears its IRQ */
        uint32_t irq_index = (APCI1564_WDOG_IOBASE + ADDI_TCW_IRQ_REG) >> 2;
        s->regs[irq_index] = 0;
        s->regs[reg_off] = (uint32_t)val;
    } else if (reg_off == (APCI1564_REV2_TIMER_IOBASE + ADDI_TCW_CTRL_REG) >> 2) {
        /* Timer control register write clears its IRQ */
        uint32_t irq_index = (APCI1564_REV2_TIMER_IOBASE + ADDI_TCW_IRQ_REG) >> 2;
        s->regs[irq_index] = 0;
        s->regs[reg_off] = (uint32_t)val;
    } else {
        s->regs[reg_off] = (uint32_t)val;
    }

    pcibase_update_irq(s);
}

/* BAR1 PIO read handler */
static uint64_t bar1_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        return ~0ULL;
    }

    uint32_t reg_off = (addr >> 2) + (BAR0_SIZE >> 2);
    if (reg_off >= (TOTAL_REG_SIZE >> 2)) {
        return ~0ULL;
    }

    return s->regs[reg_off];
}

/* BAR1 PIO write handler */
static void bar1_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        return;
    }

    uint32_t reg_off = (addr >> 2) + (BAR0_SIZE >> 2);
    if (reg_off >= (TOTAL_REG_SIZE >> 2)) {
        return;
    }

    /* Counter control register writes clear the corresponding IRQ */
    if ((reg_off % 8) == 3) { /* CTRL register offset within counter block is 0x0C, i.e., index 3 */
        uint32_t irq_index = reg_off + 2; /* IRQ at index 5 (CTRL+2) */
        if (irq_index < (TOTAL_REG_SIZE >> 2)) {
            s->regs[irq_index] = 0;
        }
    }

    s->regs[reg_off] = (uint32_t)val;
    pcibase_update_irq(s);
}

static const MemoryRegionOps bar0_pio_ops = {
    .read = bar0_pio_read,
    .write = bar0_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

static const MemoryRegionOps bar1_pio_ops = {
    .read = bar1_pio_read,
    .write = bar1_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    memset(s->regs, 0, sizeof(s->regs));
    s->intr_status = 0;
    s->intr_mask = 0;
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  APCI1564_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  APCI1564_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_OTHERS);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* BAR0 initialization */
    s->bar_info[0].index = 0;
    s->bar_info[0].type  = BAR_TYPE_PIO;
    s->bar_info[0].size  = BAR0_SIZE;
    s->bar_info[0].name  = "bar0";

    memory_region_init_io(&s->bar_regions[0], OBJECT(s), &bar0_pio_ops,
                          s, "bar0", pow2ceil(BAR0_SIZE));
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_IO, &s->bar_regions[0]);

    /* BAR1 initialization */
    s->bar_info[1].index = 1;
    s->bar_info[1].type  = BAR_TYPE_PIO;
    s->bar_info[1].size  = BAR1_SIZE;
    s->bar_info[1].name  = "bar1";

    memory_region_init_io(&s->bar_regions[1], OBJECT(s), &bar1_pio_ops,
                          s, "bar1", pow2ceil(BAR1_SIZE));
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_IO, &s->bar_regions[1]);

    s->num_bars = 2;
}

static void pcibase_uninit(PCIDevice *pdev)
{
    /* No MSI/MSI-X to tear down */
}

static const VMStateDescription vmstate_pcibase = {
    .name = "addi_apci_1564_pci",
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
