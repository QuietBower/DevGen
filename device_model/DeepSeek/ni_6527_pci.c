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
#include "hw/pci/pci_device.h"
#include "hw/qdev-core.h"
#include "migration/vmstate.h"

#define TYPE_PCIBASE_DEVICE "ni_6527_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_NI 0x1093
#define VENDOR_ID PCI_VENDOR_ID_NI
#define DEVICE_ID 0x2b10

/* Number of register ports as used by the driver */
#define NI6527_NUM_DI_PORTS     3
#define NI6527_NUM_DO_PORTS     3
#define NI6527_NUM_FILT_INTERVAL 3
#define NI6527_NUM_FILT_ENABLE   3
#define NI6527_NUM_EDGE_PORTS    2

/* Register Offsets */
#define NI6527_DI_REG(x)		(0x00 + (x))
#define NI6527_DO_REG(x)		(0x03 + (x))
#define NI6527_ID_REG			0x06
#define NI6527_CLR_REG			0x07
#define NI6527_CLR_EDGE			BIT(3)
#define NI6527_CLR_OVERFLOW		BIT(2)
#define NI6527_CLR_FILT			BIT(1)
#define NI6527_CLR_INTERVAL		BIT(0)
#define NI6527_CLR_IRQS			(NI6527_CLR_EDGE | NI6527_CLR_OVERFLOW)
#define NI6527_CLR_RESET_FILT		(NI6527_CLR_FILT | NI6527_CLR_INTERVAL)
#define NI6527_FILT_INTERVAL_REG(x)	(0x08 + (x))
#define NI6527_FILT_ENA_REG(x)		(0x0c + (x))
#define NI6527_STATUS_REG		0x14
#define NI6527_STATUS_IRQ		BIT(2)
#define NI6527_STATUS_OVERFLOW		BIT(1)
#define NI6527_STATUS_EDGE		BIT(0)
#define NI6527_CTRL_REG			0x15
#define NI6527_CTRL_FALLING		BIT(4)
#define NI6527_CTRL_RISING		BIT(3)
#define NI6527_CTRL_IRQ			BIT(2)
#define NI6527_CTRL_OVERFLOW		BIT(1)
#define NI6527_CTRL_EDGE		BIT(0)
#define NI6527_CTRL_DISABLE_IRQS	0
#define NI6527_CTRL_ENABLE_IRQS		(NI6527_CTRL_FALLING | \
					 NI6527_CTRL_RISING | \
					 NI6527_CTRL_IRQ | NI6527_CTRL_EDGE)
#define NI6527_RISING_EDGE_REG(x)	(0x18 + (x))
#define NI6527_FALLING_EDGE_REG(x)	(0x20 + (x))

#define NI6527_BAR1_SIZE 0x40
#define NI6527_CLASS_ID PCI_CLASS_OTHERS

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
    uint8_t status;
    uint8_t ctrl;
    uint8_t id_reg;         /* NI6527_ID_REG, fixed value 0x27 */

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint8_t di[NI6527_NUM_DI_PORTS];
    uint8_t do_[NI6527_NUM_DO_PORTS];
    uint8_t filter_interval[NI6527_NUM_FILT_INTERVAL];
    uint8_t filter_enable[NI6527_NUM_FILT_ENABLE];
    uint8_t rising_edge[NI6527_NUM_EDGE_PORTS];
    uint8_t falling_edge[NI6527_NUM_EDGE_PORTS];
};

/* Internal helper for status-triggered signaling. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* Compute the IRQ summary bit based on control and pending sources */
    if (s->ctrl & NI6527_CTRL_IRQ) {
        if (s->status & (NI6527_STATUS_EDGE | NI6527_STATUS_OVERFLOW)) {
            s->status |= NI6527_STATUS_IRQ;
        } else {
            s->status &= ~NI6527_STATUS_IRQ;
        }
    } else {
        s->status &= ~NI6527_STATUS_IRQ;
    }

    /* Raise or lower the PCI interrupt line */
    if (s->status & NI6527_STATUS_IRQ) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* The driver accesses registers as bytes; we handle byte-sized reads. */
    if (size != 1) {
        /* No other access sizes expected; return 0 for safety. */
        return 0;
    }

    switch (addr) {
    case 0x00:
    case 0x01:
    case 0x02:
        val = s->di[addr - 0x00];
        break;
    case 0x03:
    case 0x04:
    case 0x05:
        val = s->do_[addr - 0x03];
        break;
    case 0x06:
        val = s->id_reg;
        break;
    case 0x07:
        val = 0;  /* write-only register */
        break;
    case 0x08:
    case 0x09:
    case 0x0a:
        val = s->filter_interval[addr - 0x08];
        break;
    case 0x0c:
    case 0x0d:
    case 0x0e:
        val = s->filter_enable[addr - 0x0c];
        break;
    case 0x14:
        /* Recompute IRQ bit before returning status */
        pcibase_update_irq(s);
        val = s->status;
        break;
    case 0x15:
        val = s->ctrl;
        break;
    case 0x18:
    case 0x19:
        val = s->rising_edge[addr - 0x18];
        break;
    case 0x20:
    case 0x21:
        val = s->falling_edge[addr - 0x20];
        break;
    default:
        val = 0;
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 1) {
        return;
    }

    switch (addr) {
    case 0x00 ... 0x02:
        /* Digital input is read-only; ignore writes */
        break;
    case 0x03:
    case 0x04:
    case 0x05:
        s->do_[addr - 0x03] = val & 0xff;
        break;
    case 0x06:
        /* ID register is read-only */
        break;
    case 0x07:
        /* Clear status bits */
        if (val & NI6527_CLR_EDGE) {
            s->status &= ~NI6527_STATUS_EDGE;
        }
        if (val & NI6527_CLR_OVERFLOW) {
            s->status &= ~NI6527_STATUS_OVERFLOW;
        }
        /* FILT and INTERVAL clears are acknowledged but no further action needed */
        pcibase_update_irq(s);
        break;
    case 0x08:
    case 0x09:
    case 0x0a:
        s->filter_interval[addr - 0x08] = val & 0xff;
        break;
    case 0x0c:
    case 0x0d:
    case 0x0e:
        s->filter_enable[addr - 0x0c] = val & 0xff;
        break;
    case 0x14:
        /* STATUS_REG is read-only, ignore writes */
        break;
    case 0x15:
        s->ctrl = val & (NI6527_CTRL_FALLING | NI6527_CTRL_RISING |
                         NI6527_CTRL_IRQ | NI6527_CTRL_OVERFLOW |
                         NI6527_CTRL_EDGE);
        pcibase_update_irq(s);
        break;
    case 0x18:
    case 0x19:
        s->rising_edge[addr - 0x18] = val & 0xff;
        break;
    case 0x20:
    case 0x21:
        s->falling_edge[addr - 0x20] = val & 0xff;
        break;
    default:
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
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

    /* Initialize device registers to default state */
    s->id_reg = 0x27;
    s->status = 0;
    s->ctrl = 0;
    memset(s->di, 0, sizeof(s->di));
    memset(s->do_, 0, sizeof(s->do_));
    memset(s->filter_interval, 0, sizeof(s->filter_interval));
    memset(s->filter_enable, 0, sizeof(s->filter_enable));
    memset(s->rising_edge, 0, sizeof(s->rising_edge));
    memset(s->falling_edge, 0, sizeof(s->falling_edge));

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
    pci_set_word(pci_conf + PCI_VENDOR_ID, VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, NI6527_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* BAR Initialization - driver maps BAR1 for MMIO */
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo) {
        .index = 1,
        .type = BAR_TYPE_MMIO,
        .size = NI6527_BAR1_SIZE,
        .name = "ni6527-bar1"
    };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    /* MSI/MSI-X not used by this driver */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "ni_6527_pci",
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
