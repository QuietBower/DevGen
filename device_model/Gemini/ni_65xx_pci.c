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
struct ni_65xx_board {
	const char *name;
	unsigned int num_dio_ports;
	unsigned int num_di_ports;
	unsigned int num_do_ports;
	unsigned int legacy_invert:1;
};

#define TYPE_PCIBASE_DEVICE "ni_65xx_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x1093
#define DEVICE_ID 0x1710
#define CLASS_ID PCI_CLASS_OTHERS

#define MITE_IODWBSR	0xc0
#define WENAB			BIT(7)
#define NI_65XX_ID_REG			0x00
#define NI_65XX_CLR_REG			0x01
#define NI_65XX_CLR_WDOG_INT		BIT(6)
#define NI_65XX_CLR_WDOG_PING		BIT(5)
#define NI_65XX_CLR_WDOG_EXP		BIT(4)
#define NI_65XX_CLR_EDGE_INT		BIT(3)
#define NI_65XX_CLR_OVERFLOW_INT	BIT(2)
#define NI_65XX_STATUS_REG		0x02
#define NI_65XX_STATUS_WDOG_INT		BIT(5)
#define NI_65XX_STATUS_FALL_EDGE	BIT(4)
#define NI_65XX_STATUS_RISE_EDGE	BIT(3)
#define NI_65XX_STATUS_INT		BIT(2)
#define NI_65XX_STATUS_OVERFLOW_INT	BIT(1)
#define NI_65XX_STATUS_EDGE_INT		BIT(0)
#define NI_65XX_CTRL_REG		0x03
#define NI_65XX_CTRL_WDOG_ENA		BIT(5)
#define NI_65XX_CTRL_FALL_EDGE_ENA	BIT(4)
#define NI_65XX_CTRL_RISE_EDGE_ENA	BIT(3)
#define NI_65XX_CTRL_INT_ENA		BIT(2)
#define NI_65XX_CTRL_OVERFLOW_ENA	BIT(1)
#define NI_65XX_CTRL_EDGE_ENA		BIT(0)
#define NI_65XX_REV_REG			0x04
#define NI_65XX_FILTER_REG		0x08
#define NI_65XX_RTSI_ROUTE_REG		0x0c
#define NI_65XX_RTSI_EDGE_REG		0x0e
#define NI_65XX_RTSI_WDOG_REG		0x10
#define NI_65XX_RTSI_TRIG_REG		0x12
#define NI_65XX_AUTO_CLK_SEL_REG	0x14
#define NI_65XX_AUTO_CLK_SEL_STATUS	BIT(1)
#define NI_65XX_AUTO_CLK_SEL_DISABLE	BIT(0)
#define NI_65XX_WDOG_CTRL_REG		0x15
#define NI_65XX_WDOG_CTRL_ENA		BIT(0)
#define NI_65XX_RTSI_CFG_REG		0x16
#define NI_65XX_RTSI_CFG_RISE_SENSE	BIT(2)
#define NI_65XX_RTSI_CFG_FALL_SENSE	BIT(1)
#define NI_65XX_RTSI_CFG_SYNC_DETECT	BIT(0)
#define NI_65XX_WDOG_STATUS_REG		0x17
#define NI_65XX_WDOG_STATUS_EXP		BIT(0)
#define NI_65XX_WDOG_INTERVAL_REG	0x18
#define NI_65XX_PORT(x)			((x) * 0x10)
#define NI_65XX_IO_DATA_REG(x)		(0x40 + NI_65XX_PORT(x))
#define NI_65XX_IO_SEL_REG(x)		(0x41 + NI_65XX_PORT(x))
#define NI_65XX_IO_SEL_OUTPUT		0
#define NI_65XX_IO_SEL_INPUT		BIT(0)
#define NI_65XX_RISE_EDGE_ENA_REG(x)	(0x42 + NI_65XX_PORT(x))
#define NI_65XX_FALL_EDGE_ENA_REG(x)	(0x43 + NI_65XX_PORT(x))
#define NI_65XX_FILTER_ENA(x)		(0x44 + NI_65XX_PORT(x))
#define NI_65XX_WDOG_HIZ_REG(x)		(0x46 + NI_65XX_PORT(x))
#define NI_65XX_WDOG_ENA(x)		(0x47 + NI_65XX_PORT(x))
#define NI_65XX_WDOG_HI_LO_REG(x)	(0x48 + NI_65XX_PORT(x))
#define NI_65XX_RTSI_ENA(x)		(0x49 + NI_65XX_PORT(x))
#define NI_65XX_PORT_TO_CHAN(x)		((x) * 8)
#define NI_65XX_CHAN_TO_PORT(x)		((x) / 8)
#define NI_65XX_CHAN_TO_MASK(x)		(1 << ((x) % 8))

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
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint8_t regs[0x1000];
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool irq_active = false;
    uint8_t status = s->regs[NI_65XX_STATUS_REG];
    uint8_t ctrl = s->regs[NI_65XX_CTRL_REG];

    if ((status & NI_65XX_STATUS_EDGE_INT) && (ctrl & NI_65XX_CTRL_EDGE_ENA)) {
        irq_active = true;
    }
    if ((status & NI_65XX_STATUS_OVERFLOW_INT) && (ctrl & NI_65XX_CTRL_OVERFLOW_ENA)) {
        irq_active = true;
    }

    if (irq_active && (ctrl & NI_65XX_CTRL_INT_ENA)) {
        s->regs[NI_65XX_STATUS_REG] |= NI_65XX_STATUS_INT;
        pci_set_irq(pdev, 1);
    } else {
        s->regs[NI_65XX_STATUS_REG] &= ~NI_65XX_STATUS_INT;
        pci_set_irq(pdev, 0);
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr + size <= sizeof(s->regs)) {
        for (unsigned i = 0; i < size; i++) {
            val |= ((uint64_t)s->regs[addr + i]) << (i * 8);
        }
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr + size <= sizeof(s->regs)) {
        uint64_t v = val;
        for (unsigned i = 0; i < size; i++) {
            s->regs[addr + i] = v & 0xff;
            v >>= 8;
        }
    }

    if (addr == NI_65XX_CLR_REG && size == 1) {
        if (val & NI_65XX_CLR_EDGE_INT) {
            s->regs[NI_65XX_STATUS_REG] &= ~NI_65XX_STATUS_EDGE_INT;
        }
        if (val & NI_65XX_CLR_OVERFLOW_INT) {
            s->regs[NI_65XX_STATUS_REG] &= ~NI_65XX_STATUS_OVERFLOW_INT;
        }
        if (val & NI_65XX_CLR_WDOG_INT) {
            s->regs[NI_65XX_STATUS_REG] &= ~NI_65XX_STATUS_WDOG_INT;
        }
        pcibase_update_irq(s);
    } else if (addr == NI_65XX_CTRL_REG && size == 1) {
        pcibase_update_irq(s);
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
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

    memset(s->regs, 0, sizeof(s->regs));
    s->regs[NI_65XX_ID_REG] = 0x00;
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
    s->num_bars = 2;  
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = 0x1000, .name = "mite" };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_MMIO, .size = 0x1000, .name = "regs" };
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
    .name = "ni_65xx_pci",
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
