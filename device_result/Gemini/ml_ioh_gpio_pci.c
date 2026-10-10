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

#define TYPE_PCIBASE_DEVICE "ml_ioh_gpio_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_ROHM 0x10db

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

#define IOH_EDGE_FALLING    0
#define IOH_EDGE_RISING     BIT(0)
#define IOH_LEVEL_L         BIT(1)
#define IOH_LEVEL_H         (BIT(0) | BIT(1))
#define IOH_EDGE_BOTH       BIT(2)
#define IOH_IM_MASK         (BIT(0) | BIT(1) | BIT(2))
#define IOH_IRQ_BASE        0

struct ioh_reg_comn {
    uint32_t ien;
    uint32_t istatus;
    uint32_t idisp;
    uint32_t iclr;
    uint32_t imask;
    uint32_t imaskclr;
    uint32_t po;
    uint32_t pi;
    uint32_t pm;
    uint32_t im_0;
    uint32_t im_1;
    uint32_t reserved;
};

struct ioh_regs {
    struct ioh_reg_comn regs[8];
    uint32_t reserve1[16];
    uint32_t ioh_sel_reg[4];
    uint32_t reserve2[11];
    uint32_t srst;
};

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
    struct ioh_regs reg;
};

static void pcibase_reset(DeviceState *dev);

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool irq_active = false;

    for (int i = 0; i < 8; i++) {
        if (s->reg.regs[i].istatus & s->reg.regs[i].ien) {
            irq_active = true;
            break;
        }
    }

    pci_set_irq(pdev, irq_active ? 1 : 0);
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr < 0x180) {
        int ch = addr / 0x30;
        int reg = addr % 0x30;
        switch (reg) {
            case 0x00: val = s->reg.regs[ch].ien; break;
            case 0x04: val = s->reg.regs[ch].istatus; break;
            case 0x08: val = s->reg.regs[ch].idisp; break;
            case 0x0C: val = s->reg.regs[ch].iclr; break;
            case 0x10: val = s->reg.regs[ch].imask; break;
            case 0x14: val = s->reg.regs[ch].imaskclr; break;
            case 0x18: val = s->reg.regs[ch].po; break;
            case 0x1C: val = s->reg.regs[ch].pi; break;
            case 0x20: val = s->reg.regs[ch].pm; break;
            case 0x24: val = s->reg.regs[ch].im_0; break;
            case 0x28: val = s->reg.regs[ch].im_1; break;
            default: val = 0; break;
        }
    } else if (addr >= 0x1C0 && addr < 0x1D0) {
        int idx = (addr - 0x1C0) / 4;
        val = s->reg.ioh_sel_reg[idx];
    } else if (addr == 0x1FC) {
        val = s->reg.srst;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < 0x180) {
        int ch = addr / 0x30;
        int reg = addr % 0x30;
        switch (reg) {
            case 0x00: 
                s->reg.regs[ch].ien = val; 
                pcibase_update_irq(s);
                break;
            case 0x04: 
                s->reg.regs[ch].istatus = val; 
                pcibase_update_irq(s);
                break;
            case 0x08: 
                s->reg.regs[ch].idisp = val; 
                break;
            case 0x0C: 
                s->reg.regs[ch].iclr = val; 
                s->reg.regs[ch].istatus &= ~val;
                pcibase_update_irq(s);
                break;
            case 0x10: 
                s->reg.regs[ch].imask = val; 
                break;
            case 0x14: 
                s->reg.regs[ch].imaskclr = val; 
                break;
            case 0x18: 
                s->reg.regs[ch].po = val; 
                break;
            case 0x1C: 
                s->reg.regs[ch].pi = val; 
                break;
            case 0x20: 
                s->reg.regs[ch].pm = val; 
                break;
            case 0x24: 
                s->reg.regs[ch].im_0 = val; 
                break;
            case 0x28: 
                s->reg.regs[ch].im_1 = val; 
                break;
        }
    } else if (addr >= 0x1C0 && addr < 0x1D0) {
        int idx = (addr - 0x1C0) / 4;
        s->reg.ioh_sel_reg[idx] = val;
    } else if (addr == 0x1FC) {
        s->reg.srst = val;
        if (val == 1) {
            pcibase_reset(DEVICE(s));
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

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    memset(&s->reg, 0, sizeof(s->reg));
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_ROHM );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x802E );
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
    s->bar_info[0].type = BAR_TYPE_NONE;
    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = sizeof(struct ioh_regs);
    s->bar_info[1].name = "ioh_gpio_bar1";
    
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "ml_ioh_gpio_pci",
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
