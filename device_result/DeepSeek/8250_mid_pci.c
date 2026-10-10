/*
 * QEMU 8.2.10 Virtual PCI Device for Intel Mid UART (8250_mid)
 * Auto-generated. Do not edit.
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

#define TYPE_PCIBASE_DEVICE "8250_mid_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_INTEL         0x8086
#define PCI_DEVICE_ID_INTEL_PNW_UART1 0x081b
#define PCI_CLASS_SERIAL            0x0700
#define BAR0_SIZE                   0x1000

/* Standard 8250 UART register offsets */
#define UART_RBR 0
#define UART_THR 0
#define UART_IER 1
#define UART_IIR 2
#define UART_FCR 2
#define UART_LCR 3
#define UART_MCR 4
#define UART_LSR 5
#define UART_MSR 6
#define UART_SCR 7
#define UART_DLL 0
#define UART_DLM 1

/* Intel MID UART specific registers */
#define INTEL_MID_UART_FISR         0x08
#define INTEL_MID_UART_PS           0x30
#define INTEL_MID_UART_MUL          0x34
#define INTEL_MID_UART_DIV          0x38
#define DNV_DMA_CHAN_OFFSET         0x80

/* Standard 16550 bit definitions */
#define UART_LSR_THRE  0x20
#define UART_LSR_TEMT  0x40
#define UART_LSR_DR    0x01
#define UART_IER_RDI   0x01
#define UART_IER_THRI  0x02
#define UART_IER_RLSI  0x04
#define UART_IER_MSI   0x08
#define UART_IIR_NO_INT 0x01
#define UART_IIR_THRI  0x02

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
    uint8_t regs[256];
};

/* Helper to compute UART IIR based on current IER and status */
static uint8_t pcibase_compute_iir(PCIBaseState *s)
{
    uint8_t ier = s->regs[UART_IER];
    uint8_t lsr = s->regs[UART_LSR];
    uint8_t msr = s->regs[UART_MSR];

    if ((lsr & 0x1E) && (ier & UART_IER_RLSI)) {
        return 0x06; /* Receive Line Status */
    } else if ((lsr & UART_LSR_DR) && (ier & UART_IER_RDI)) {
        return 0x04; /* Received Data Available */
    } else if ((lsr & UART_LSR_THRE) && (ier & UART_IER_THRI)) {
        return 0x02; /* THR Empty */
    } else if ((msr & 0x0F) && (ier & UART_IER_MSI)) {
        return 0x00; /* Modem Status */
    }
    return UART_IIR_NO_INT; /* No interrupt pending */
}

/* Update the device interrupt output line based on current state */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint8_t iir = pcibase_compute_iir(s);
    bool uart_pending = (iir & 1) == 0; /* bit 0 clear means interrupt pending */
    
    s->regs[INTEL_MID_UART_FISR] &= ~1; /* clear bit 0 */
    if (uart_pending) {
        s->regs[INTEL_MID_UART_FISR] |= 1;
    }

    /* Signal PCI interrupt: for MSI just notify, for INTx raise if pending, lower if not */
    if (uart_pending) {
        if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!msi_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr + size > sizeof(s->regs)) {
        return 0;
    }

    /* DLAB handling for standard UART registers (offsets 0..7) */
    if (addr < 8 && (s->regs[UART_LCR] & 0x80)) {
        /* DLAB set: offsets 0 and 1 are DLL and DLM */
        if (addr == UART_DLL && size == 1) {
            val = s->regs[UART_DLL];
            return val;
        } else if (addr == UART_DLM && size == 1) {
            val = s->regs[UART_DLM];
            return val;
        }
        /* Other offsets still use normal registers, but DLAB doesn't affect them */
    }

    /* Special handling: IIR is read-only, computed dynamically */
    if (addr == UART_IIR && size == 1) {
        return pcibase_compute_iir(s);
    }

    /* For all other registers, return the stored value.
     * Device is little-endian; memcpy works on little-endian hosts. */
    if (size == 1) {
        val = s->regs[addr];
    } else if (size == 2) {
        val = lduw_le_p(s->regs + addr);
    } else if (size == 4) {
        val = ldl_le_p(s->regs + addr);
    } else if (size == 8) {
        val = ldq_le_p(s->regs + addr);
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr + size > sizeof(s->regs)) {
        return;
    }

    /* DLAB handling for standard UART registers */
    if (addr < 8 && (s->regs[UART_LCR] & 0x80)) {
        if (addr == UART_DLL && size == 1) {
            s->regs[UART_DLL] = val;
            return;
        } else if (addr == UART_DLM && size == 1) {
            s->regs[UART_DLM] = val;
            return;
        }
    }

    /* Special handling for writable registers with side effects */
    if (addr == UART_IER && size == 1) {
        s->regs[UART_IER] = val;
        pcibase_update_irq(s);
        return;
    } else if (addr == UART_FCR && size == 1) {
        s->regs[UART_FCR] = val;
        return;
    } else if (addr == UART_LCR && size == 1) {
        s->regs[UART_LCR] = val;
        /* No IRQ change, but DLAB may have changed */
        return;
    } else if (addr == UART_MCR && size == 1) {
        s->regs[UART_MCR] = val;
        return;
    } else if (addr == UART_LSR || addr == UART_MSR) {
        /* LSR and MSR are read-only; ignore writes */
        return;
    }

    /* For all other registers (including SCR, MID-specific, DMA), store value */
    if (size == 1) {
        s->regs[addr] = val;
    } else if (size == 2) {
        stw_le_p(s->regs + addr, val);
    } else if (size == 4) {
        stl_le_p(s->regs + addr, val);
    } else if (size == 8) {
        stq_le_p(s->regs + addr, val);
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bar_info, Error **errp)
{
    MemoryRegion *mr;
    if (bar_info->type == BAR_TYPE_MMIO) {
        mr = &s->bar_regions[bar_info->index];
        memory_region_init_io(mr, OBJECT(pdev), &pcibase_mmio_ops, s, bar_info->name, bar_info->size);
        pci_register_bar(pdev, bar_info->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    memset(s->regs, 0, sizeof(s->regs));
    s->regs[UART_LSR] = UART_LSR_THRE | UART_LSR_TEMT;
    /* UART_IIR is computed, no stored value needed, but we can set initial */
    /* Ensure IRQ line is lowered after reset */
    pcibase_update_irq(s);
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_INTEL);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_INTEL_PNW_UART1);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_SERIAL);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* Enable MSI */
    if (msi_init(pdev, 0, 1, true, false, errp) < 0) {
        return;
    }

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = BAR0_SIZE;
    s->bar_info[0].name = "uart-mmio";
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
    .name = "8250_mid_pci",
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
