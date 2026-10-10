/*
 * QEMU PCI device model for HPE iLO2+ HW Watchdog Timer (hpwdt)
 *
 * Generated to satisfy linux-6.18/drivers/watchdog/hpwdt.c
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "qemu/host-utils.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "standard-headers/linux/pci_regs.h"
#include "sysemu/dma.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/pci/pcie.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-core.h"
#include "migration/vmstate.h"

/* Additional include files retrieved from driver context */
#include "hw/pci/pci_ids.h"

#define TYPE_PCIBASE_DEVICE "hpwdt_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#ifndef PCI_VENDOR_ID_COMPAQ
#define PCI_VENDOR_ID_COMPAQ 0x0E11
#endif
#define HPWDT_PCI_VENDOR_ID   PCI_VENDOR_ID_COMPAQ
#define HPWDT_PCI_DEVICE_ID   0xB203
#define HPWDT_PCI_CLASS_ID    PCI_CLASS_OTHERS

/* BAR index used by driver: pci_iomap(dev, 1, 0x80) */
#define HPWDT_PCI_BAR_INDEX        1
#define HPWDT_PCI_BAR_SIZE         0x80

/* Offsets inside BAR as used by driver */
#define HPWDT_REG_NMISTAT_OFFSET   0x6e
#define HPWDT_REG_TIMER_OFFSET     0x70
#define HPWDT_REG_TIMER_CON_OFFSET 0x72

/* Bit definitions seen in driver */
#define HPWDT_TIMER_CON_ENABLE_BIT 0x01


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
    uint8_t nmistat_reg;
    uint16_t timer_reg;
    uint8_t timer_con_reg;

    /* DMA Context */
    

};

 

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    (void)pdev;
    /* hpwdt driver does not use interrupts from this device, so no IRQ logic */
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    (void)s;
    (void)pdev;
    (void)is_write;
    /* hpwdt driver does not use DMA, keep empty */
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Only the low BAR range (0x00-0x7f) is defined; any size is accepted but
     * we only implement byte/word accesses that the driver performs.
     */
    switch (addr) {
    case HPWDT_REG_NMISTAT_OFFSET:
        /* ioread8(hpwdt_nmistat) & 0x6 in hpwdt_my_nmi() */
        if (size == 1) {
            val = s->nmistat_reg;
        }
        break;
    case HPWDT_REG_TIMER_OFFSET:
        /* ioread16(hpwdt_timer_reg) in hpwdt_gettimeleft() */
        if (size == 2) {
            val = s->timer_reg;
        } else if (size == 1) {
            /* Allow byte access, return low byte */
            val = s->timer_reg & 0xff;
        }
        break;
    case HPWDT_REG_TIMER_OFFSET + 1:
        /* Upper byte of 16-bit timer register */
        if (size == 1) {
            val = (s->timer_reg >> 8) & 0xff;
        }
        break;
    case HPWDT_REG_TIMER_CON_OFFSET:
        /* ioread8(hpwdt_timer_con) in hpwdt_hw_is_running() and stop() */
        if (size == 1) {
            val = s->timer_con_reg;
        }
        break;
    default:
        /* Unused addresses return 0 */
        val = 0;
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case HPWDT_REG_TIMER_OFFSET:
        /* iowrite16(reload, hpwdt_timer_reg); and hpwdt_ping_ticks() */
        if (size == 2) {
            s->timer_reg = (uint16_t)val;
        } else if (size == 1) {
            /* byte write to low byte */
            s->timer_reg = (s->timer_reg & 0xff00) | (val & 0xff);
        }
        break;
    case HPWDT_REG_TIMER_OFFSET + 1:
        if (size == 1) {
            /* byte write to high byte */
            s->timer_reg = (s->timer_reg & 0x00ff) | ((val & 0xff) << 8);
        }
        break;
    case HPWDT_REG_TIMER_CON_OFFSET:
        /* iowrite8(control, hpwdt_timer_con); and iowrite8(data, ...) */
        if (size == 1) {
            s->timer_con_reg = (uint8_t)val;
        }
        break;
    case HPWDT_REG_NMISTAT_OFFSET:
        /* Driver never writes NMISTAT; ignore writes but store if they occur */
        if (size == 1) {
            s->nmistat_reg = (uint8_t)val;
        }
        break;
    default:
        /* Ignore writes to undefined offsets */
        break;
    }

    /* No IRQ or DMA side effects defined in driver */
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    (void)s;
    (void)addr;
    (void)size;
    /* Logic for Port I/O (Legacy support) - not used by driver */
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    (void)s;
    (void)addr;
    (void)val;
    (void)size;
    /* Logic for Port I/O (Legacy support) - not used by driver */
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

    /* Revert registers to power-on defaults consistent with driver expectations */
    s->nmistat_reg = 0x00;
    s->timer_reg = 0x0000;
    /* hw_is_running() checks bit0; default to stopped (bit0 clear) */
    s->timer_con_reg = 0x00;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  HPWDT_PCI_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  HPWDT_PCI_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, HPWDT_PCI_CLASS_ID );
    pci_set_word(pci_conf + PCI_SUBSYSTEM_VENDOR_ID, PCI_VENDOR_ID_HP);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0].index = HPWDT_PCI_BAR_INDEX;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = HPWDT_PCI_BAR_SIZE;
    s->bar_info[0].name = "hpwdt-mmio";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No MSI/MSI-X or DMA setup required by driver */

    /* Initialize internal register state */
    s->nmistat_reg = 0x00;
    s->timer_reg = 0x0000;
    s->timer_con_reg = 0x00;
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
    /* Free buffers, stop timers, etc. - none used */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "hpwdt_pci",
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
