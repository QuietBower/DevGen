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

/* Additional include files retrieved from driver context */

#define TYPE_PCIBASE_DEVICE "adl_pci9111_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_ADLINK 0x144a
#define PCI_DEVICE_ID_ADL9111 0x9111
#define PCI_CLASS_ID 0x0ff00

#define PCI9111_AI_FIFO_REG		0x00
#define PCI9111_AO_REG			0x00
#define PCI9111_DIO_REG			0x02
#define PCI9111_EDIO_REG		0x04
#define PCI9111_AI_CHANNEL_REG		0x06
#define PCI9111_AI_RANGE_STAT_REG	0x08
#define PCI9111_AI_STAT_AD_BUSY		BIT(7)
#define PCI9111_AI_STAT_FF_FF		BIT(6)
#define PCI9111_AI_STAT_FF_HF		BIT(5)
#define PCI9111_AI_STAT_FF_EF		BIT(4)
#define PCI9111_AI_RANGE(x)		(((x) & 0x7) << 0)
#define PCI9111_AI_RANGE_MASK		PCI9111_AI_RANGE(7)
#define PCI9111_AI_TRIG_CTRL_REG	0x0a
#define PCI9111_AI_TRIG_CTRL_TRGEVENT	BIT(5)
#define PCI9111_AI_TRIG_CTRL_POTRG	BIT(4)
#define PCI9111_AI_TRIG_CTRL_PTRG	BIT(3)
#define PCI9111_AI_TRIG_CTRL_ETIS	BIT(2)
#define PCI9111_AI_TRIG_CTRL_TPST	BIT(1)
#define PCI9111_AI_TRIG_CTRL_ASCAN	BIT(0)
#define PCI9111_INT_CTRL_REG		0x0c
#define PCI9111_INT_CTRL_ISC2		BIT(3)
#define PCI9111_INT_CTRL_FFEN		BIT(2)
#define PCI9111_INT_CTRL_ISC1		BIT(1)
#define PCI9111_INT_CTRL_ISC0		BIT(0)
#define PCI9111_SOFT_TRIG_REG		0x0e
#define PCI9111_8254_BASE_REG		0x40
#define PCI9111_INT_CLR_REG		0x48
#define PLX9052_INTCSR			0x4c
#define PLX9052_INTCSR_LI1ENAB		BIT(0)
#define PLX9052_INTCSR_LI1STAT		BIT(2)
#define PLX9052_INTCSR_LI2ENAB		BIT(3)
#define PLX9052_INTCSR_LI2STAT		BIT(5)
#define PLX9052_INTCSR_LI2POL		BIT(4)
#define PLX9052_INTCSR_LI1POL		BIT(1)
#define PLX9052_INTCSR_PCIENAB		BIT(6)
#define PCI9111_LI1_ACTIVE	(PLX9052_INTCSR_LI1ENAB | PLX9052_INTCSR_LI1STAT)
#define PCI9111_LI2_ACTIVE	(PLX9052_INTCSR_LI2ENAB | PLX9052_INTCSR_LI2STAT)

enum pci9111_ISC0_sources {
	irq_on_eoc,
	irq_on_fifo_half_full
};
enum pci9111_ISC1_sources {
	irq_on_timer_tick,
	irq_on_external_trigger
};

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

    /* PLX9050 interrupt control register */
    uint8_t intcsr;

    /* PCI-9111 register space shadow (BAR2) */
    uint8_t io_regs[256];
};

static uint64_t pcibase_lcr_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    switch (addr) {
    case PLX9052_INTCSR: /* 0x4c */
        if (size == 1) {
            val = s->intcsr;
        }
        break;
    default:
        /* other LCR registers return 0 */
        break;
    }
    return val;
}

static void pcibase_lcr_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    switch (addr) {
    case PLX9052_INTCSR:
        if (size == 1) {
            /* Update only writable control bits, preserve status bits */
            uint8_t writable_mask = (1 << 0) | (1 << 1) | (1 << 3) | (1 << 4) | (1 << 6);
            s->intcsr = (s->intcsr & ~writable_mask) | (val & writable_mask);
        }
        break;
    default:
        /* ignore */
        break;
    }
}

static const MemoryRegionOps pcibase_lcr_ops = {
    .read = pcibase_lcr_read,
    .write = pcibase_lcr_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 1 },
    .impl  = { .min_access_size = 1, .max_access_size = 1 },
};

static uint64_t pcibase_io_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    switch (size) {
    case 1:
        val = s->io_regs[addr];
        break;
    case 2:
        val = lduw_le_p(&s->io_regs[addr]);
        break;
    case 4:
        val = ldl_le_p(&s->io_regs[addr]);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "pcibase_io_read: unexpected size %u addr 0x%" HWADDR_PRIx "\n", size, addr);
        break;
    }
    return val;
}

static void pcibase_io_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    switch (size) {
    case 1:
        s->io_regs[addr] = val;
        /* Special handling for INT_CLR_REG (0x48) */
        if (addr == PCI9111_INT_CLR_REG) {
            s->intcsr &= ~(PLX9052_INTCSR_LI1STAT | PLX9052_INTCSR_LI2STAT);
        }
        break;
    case 2:
        stw_le_p(&s->io_regs[addr], val);
        break;
    case 4:
        stl_le_p(&s->io_regs[addr], val);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "pcibase_io_write: unexpected size %u addr 0x%" HWADDR_PRIx "\n", size, addr);
        break;
    }
}

static const MemoryRegionOps pcibase_io_ops = {
    .read = pcibase_io_read,
    .write = pcibase_io_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    s->intcsr = 0;
    memset(s->io_regs, 0, sizeof(s->io_regs));
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_ADLINK );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_ADL9111 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* BAR1: PLX9050 local configuration registers */
    memory_region_init_io(&s->bar_regions[1], OBJECT(s), &pcibase_lcr_ops, s,
                          "pci9111-lcr", 256);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_IO, &s->bar_regions[1]);

    /* BAR2: PCI-9111 device registers */
    memory_region_init_io(&s->bar_regions[2], OBJECT(s), &pcibase_io_ops, s,
                          "pci9111-io", 256);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_IO, &s->bar_regions[2]);
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

/* Minimal VMState */
static const VMStateDescription vmstate_pcibase = {
    .name = "adl_pci9111_pci",
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
