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
#include "qemu/bitops.h"

#define TYPE_PCIBASE_DEVICE "addi_apci_1564_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_ADDIDATA			0x15B8
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
    uint32_t eeprom_reg;
    uint32_t di_reg;
    uint32_t di_int_mode1_reg;
    uint32_t di_int_mode2_reg;
    uint32_t di_int_status_reg;
    uint32_t di_irq_reg;
    uint32_t do_reg;
    uint32_t do_int_ctrl_reg;
    uint32_t do_int_status_reg;
    uint32_t do_irq_reg;
    uint32_t tcw_reload_reg;
    uint32_t tcw_ctrl_reg;
    uint32_t tcw_irq_reg;
    uint32_t tcw_status_reg;
    uint32_t tcw_timebase_reg;
    uint32_t tcw_val_reg;
    
    unsigned long eeprom;
    unsigned long timer;
    unsigned long counters;
    unsigned int mode1;
    unsigned int mode2;
    unsigned int ctrl;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool irq_active = false;

    if ((s->di_irq_reg & APCI1564_DI_IRQ_ENA) && (s->di_int_status_reg != 0)) {
        irq_active = true;
    }
    if ((s->tcw_ctrl_reg & ADDI_TCW_CTRL_IRQ_ENA) && (s->tcw_irq_reg & ADDI_TCW_IRQ)) {
        irq_active = true;
    }

    pci_set_irq(pdev, irq_active ? 1 : 0);
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    BARInfo *bi = (BARInfo *)opaque;
    PCIBaseState *s = (PCIBaseState *)((uint8_t *)(bi - bi->index) - offsetof(PCIBaseState, bar_info));
    uint64_t val = 0;
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    BARInfo *bi = (BARInfo *)opaque;
    PCIBaseState *s = (PCIBaseState *)((uint8_t *)(bi - bi->index) - offsetof(PCIBaseState, bar_info));
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    BARInfo *bi = (BARInfo *)opaque;
    PCIBaseState *s = (PCIBaseState *)((uint8_t *)(bi - bi->index) - offsetof(PCIBaseState, bar_info));
    uint64_t val = 0;

    if (bi->index == 0) {
        switch (addr) {
            case 0x00: val = s->eeprom_reg; break; /* EEPROM: Rev 2.x */
            case 0x04: val = s->di_reg; break;
            case 0x10: val = s->di_int_status_reg; break;
            case 0x14: val = s->di_irq_reg; break;
            case 0x18: val = s->do_reg; break;
            case 0x20: val = s->do_int_status_reg; break;
            case 0x24: val = s->do_irq_reg; break;
            case 0x48: val = s->tcw_val_reg; break;
            case 0x54: val = s->tcw_ctrl_reg; break;
            case 0x58: val = s->tcw_status_reg; break;
            case 0x5C: val = s->tcw_irq_reg; break;
            default: break;
        }
    } else if (bi->index == 1) {
        /* Counters */
        uint32_t offset = addr % 0x20;
        switch (offset) {
            case 0x00: val = s->tcw_val_reg; break;
            case 0x0C: val = s->tcw_ctrl_reg; break;
            case 0x10: val = s->tcw_status_reg; break;
            case 0x14: val = s->tcw_irq_reg; break;
            default: break;
        }
    }
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    BARInfo *bi = (BARInfo *)opaque;
    PCIBaseState *s = (PCIBaseState *)((uint8_t *)(bi - bi->index) - offsetof(PCIBaseState, bar_info));
    
    if (bi->index == 0) {
        switch (addr) {
            case 0x08: s->di_int_mode1_reg = val; break;
            case 0x0C: s->di_int_mode2_reg = val; break;
            case 0x14: 
                s->di_irq_reg = val; 
                pcibase_update_irq(s);
                break;
            case 0x18: s->do_reg = val; break;
            case 0x1C: s->do_int_ctrl_reg = val; break;
            case 0x24: s->do_irq_reg = val; break;
            case 0x4C: s->tcw_reload_reg = val; break;
            case 0x50: s->tcw_timebase_reg = val; break;
            case 0x54: 
                s->tcw_ctrl_reg = val; 
                pcibase_update_irq(s);
                break;
            default: break;
        }
    } else if (bi->index == 1) {
        uint32_t offset = addr % 0x20;
        switch (offset) {
            case 0x04: s->tcw_reload_reg = val; break;
            case 0x08: s->tcw_timebase_reg = val; break;
            case 0x0C: 
                s->tcw_ctrl_reg = val; 
                pcibase_update_irq(s);
                break;
            default: break;
        }
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

    s->eeprom_reg = 0x10; /* Emulate Rev 2.x */
    s->di_reg = 0;
    s->di_int_mode1_reg = 0;
    s->di_int_mode2_reg = 0;
    s->di_int_status_reg = 0;
    s->di_irq_reg = 0;
    s->do_reg = 0;
    s->do_int_ctrl_reg = 0;
    s->do_int_status_reg = 0;
    s->do_irq_reg = 0;
    s->tcw_reload_reg = 0;
    s->tcw_ctrl_reg = 0;
    s->tcw_irq_reg = 0;
    s->tcw_status_reg = 0;
    s->tcw_timebase_reg = 0;
    s->tcw_val_reg = 0;
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
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, bi, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, bi, bi->name, aligned_size);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_ADDIDATA );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x1006 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_OTHERS );
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
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_PIO, .size = 0x100, .name = "apci1564-bar0" };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_PIO, .size = 0x100, .name = "apci1564-bar1" };
      
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
