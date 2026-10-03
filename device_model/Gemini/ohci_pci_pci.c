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

#define TYPE_PCIBASE_DEVICE "ohci_pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_STMICRO 0x104A
#define PCI_DEVICE_ID_STMICRO_USB_OHCI 0xCC01
#define PCI_CLASS_SERIAL_USB_OHCI 0x0c0310

#define OHCI_INTR_MIE   (1 << 31)
#define OHCI_CTRL_IR    (1 << 8)
#define OHCI_CTRL_HCFS  (3 << 6)
#define OHCI_USB_OPER   (2 << 6)
#define OHCI_USB_RESET  (0 << 6)
#define RH_PS_PPS       (1 << 8)

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
    uint32_t intrstatus;
    uint32_t intrenable;
    uint32_t intrdisable;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t revision;
    uint32_t control;
    uint32_t cmdstatus;
    uint32_t hcca;
    uint32_t ed_periodcurrent;
    uint32_t ed_controlhead;
    uint32_t ed_controlcurrent;
    uint32_t ed_bulkhead;
    uint32_t ed_bulkcurrent;
    uint32_t donehead;
    uint32_t fminterval;
    uint32_t fmremaining;
    uint32_t fmnumber;
    uint32_t periodicstart;
    uint32_t lsthresh;
    uint32_t roothub_a;
    uint32_t roothub_b;
    uint32_t roothub_status;
    uint32_t roothub_portstatus[15];

    /* DMA Context */
    uint32_t hcca_dma;

    uint32_t rh_state;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->intrstatus & s->intrenable) {
        if (s->has_msi) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!s->has_msi) {
            pci_set_irq(pdev, 0);
        }
    }
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    switch (addr) {
    case 0x00: val = s->revision; break;
    case 0x04: val = s->control; break;
    case 0x08: val = s->cmdstatus; break;
    case 0x0c: val = s->intrstatus; break;
    case 0x10: val = s->intrenable; break;
    case 0x14: val = s->intrdisable; break;
    case 0x18: val = s->hcca; break;
    case 0x1c: val = s->ed_periodcurrent; break;
    case 0x20: val = s->ed_controlhead; break;
    case 0x24: val = s->ed_controlcurrent; break;
    case 0x28: val = s->ed_bulkhead; break;
    case 0x2c: val = s->ed_bulkcurrent; break;
    case 0x30: val = s->donehead; break;
    case 0x34: val = s->fminterval; break;
    case 0x38: val = s->fmremaining; break;
    case 0x3c: val = s->fmnumber; break;
    case 0x40: val = s->periodicstart; break;
    case 0x44: val = s->lsthresh; break;
    case 0x48: val = s->roothub_a; break;
    case 0x4c: val = s->roothub_b; break;
    case 0x50: val = s->roothub_status; break;
    default:
        if (addr >= 0x54 && addr < 0x54 + 15 * 4) {
            int port = (addr - 0x54) / 4;
            val = s->roothub_portstatus[port];
        }
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    switch (addr) {
    case 0x00: s->revision = val; break;
    case 0x04: s->control = val; break;
    case 0x08: 
        s->cmdstatus = val; 
        /* Auto-clear command status to acknowledge commands like HC Reset */
        s->cmdstatus = 0;
        break;
    case 0x0c: 
        s->intrstatus &= ~val; 
        pcibase_update_irq(s);
        break;
    case 0x10: 
        s->intrenable |= val; 
        pcibase_update_irq(s);
        break;
    case 0x14: 
        s->intrenable &= ~val; 
        pcibase_update_irq(s);
        break;
    case 0x18: s->hcca = val; break;
    case 0x1c: s->ed_periodcurrent = val; break;
    case 0x20: s->ed_controlhead = val; break;
    case 0x24: s->ed_controlcurrent = val; break;
    case 0x28: s->ed_bulkhead = val; break;
    case 0x2c: s->ed_bulkcurrent = val; break;
    case 0x30: s->donehead = val; break;
    case 0x34: s->fminterval = val; break;
    case 0x38: s->fmremaining = val; break;
    case 0x3c: s->fmnumber = val; break;
    case 0x40: s->periodicstart = val; break;
    case 0x44: s->lsthresh = val; break;
    case 0x48: s->roothub_a = val; break;
    case 0x4c: s->roothub_b = val; break;
    case 0x50: s->roothub_status = val; break;
    default:
        if (addr >= 0x54 && addr < 0x54 + 15 * 4) {
            int port = (addr - 0x54) / 4;
            s->roothub_portstatus[port] = val;
        }
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
    
    s->control = 0;
    s->cmdstatus = 0;
    s->intrstatus = 0;
    s->intrenable = 0;
    s->intrdisable = 0;
    s->hcca = 0;
    s->ed_periodcurrent = 0;
    s->ed_controlhead = 0;
    s->ed_controlcurrent = 0;
    s->ed_bulkhead = 0;
    s->ed_bulkcurrent = 0;
    s->donehead = 0;
    s->fminterval = 0x3fff0000 | 0x2edf;
    s->fmremaining = 0;
    s->fmnumber = 0;
    s->periodicstart = 0x2edf;
    s->lsthresh = 0;
    s->roothub_a = 2;
    s->roothub_b = 0;
    s->roothub_status = 0;
    for (int i = 0; i < 15; i++) {
        s->roothub_portstatus[i] = 0;
    }
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_STMICRO );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_STMICRO_USB_OHCI );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_SERIAL_USB_OHCI );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    if (msi_init(pdev, 0, 1, true, false, errp) == 0) {
        s->has_msi = true;
    }

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x1000;
    s->bar_info[0].name = "ohci-regs";
      
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
    .name = "ohci_pci_pci",
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
