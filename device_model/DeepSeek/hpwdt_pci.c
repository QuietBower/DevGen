/*
 * QEMU PCI device model for HPE iLO2+ HW Watchdog Timer (hpwdt)
 * Generated based on Linux driver /home/eely/linux-7.1/drivers/watchdog/hpwdt.c
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

#define TYPE_PCIBASE_DEVICE "hpwdt_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x0E11
#define DEVICE_ID 0xB203
#define CLASS_ID  0x088000

/* Register offsets */
#define HPWDT_NMISTAT_OFFSET   0x6e
#define HPWDT_TIMER_REG_OFFSET 0x70
#define HPWDT_TIMER_CON_OFFSET 0x72

/* BAR configuration: the driver maps BAR 1 with size 0x80 */
#define BAR1_SIZE 0x80

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

struct HPWDTRegs {
    uint8_t  nmistat;    /* NMI status register, read-only */
    uint16_t timer_reg;  /* Watchdog timer countdown register */
    uint8_t  timer_con;  /* Watchdog control register */
};

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;

    struct HPWDTRegs regs;

    uint8_t power_state;
};

/* Internal helper for status-triggered signaling - unused by this driver */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* No IRQ used by this driver */
}

/* Device-initiated DMA logic - not used by this driver */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* DMA not used */
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case HPWDT_NMISTAT_OFFSET:
        if (size == 1) {
            val = s->regs.nmistat;
        }
        break;
    case HPWDT_TIMER_REG_OFFSET:
        if (size == 2) {
            val = s->regs.timer_reg;
        }
        break;
    case HPWDT_TIMER_CON_OFFSET:
        if (size == 1) {
            val = s->regs.timer_con;
        }
        break;
    default:
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case HPWDT_TIMER_REG_OFFSET:
        if (size == 2) {
            s->regs.timer_reg = (uint16_t)val;
        }
        break;
    case HPWDT_TIMER_CON_OFFSET:
        if (size == 1) {
            s->regs.timer_con = (uint8_t)val;
        }
        break;
    /* NMISTAT is read-only; ignore writes */
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

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Reset registers to initial state */
    s->regs.nmistat = 0;
    s->regs.timer_reg = 0;
    s->regs.timer_con = 0;
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
        /* PIO not used by this driver */
    } else if (bi->type == BAR_TYPE_RAM) {
        /* RAM BARs not used */
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID, 0x0E11);
    pci_set_word(pci_conf + PCI_DEVICE_ID, 0xB203);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x088000);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 0);

    /* Set subsystem vendor to HP, as required by driver */
    pci_set_word(pci_conf + PCI_SUBSYSTEM_VENDOR_ID, 0x103C);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR 1: MMIO at size 0x80, as used by the driver */
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_MMIO, .size = BAR1_SIZE, .name = "hpwdt-mmio" };
    pcibase_register_bar(pdev, s, &s->bar_info[1], errp);

    /* Initial register state */
    s->regs.nmistat = 0;
    s->regs.timer_reg = 0;
    s->regs.timer_con = 0;
    s->power_state = 0; /* D0 */
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

    /* No additional cleanup needed */
}

static const VMStateDescription vmstate_pcibase = {
    .name = "hpwdt_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT8(regs.nmistat, PCIBaseState),
        VMSTATE_UINT16(regs.timer_reg, PCIBaseState),
        VMSTATE_UINT8(regs.timer_con, PCIBaseState),
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
