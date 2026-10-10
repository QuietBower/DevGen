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


#define TYPE_PCIBASE_DEVICE "pch_uart_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCH_UART_RBR		0x00
#define PCH_UART_THR		0x00
#define PCH_UART_IER_MASK	(PCH_UART_IER_ERBFI|PCH_UART_IER_ETBEI|PCH_UART_IER_ELSI|PCH_UART_IER_EDSSI)
#define PCH_UART_IER_ERBFI	0x00000001
#define PCH_UART_IER_ETBEI	0x00000002
#define PCH_UART_IER_ELSI	0x00000004
#define PCH_UART_IER_EDSSI	0x00000008
#define PCH_UART_IIR_IP			0x00000001
#define PCH_UART_IIR_IID		0x00000006
#define PCH_UART_IIR_MSI		0x00000000
#define PCH_UART_IIR_TRI		0x00000002
#define PCH_UART_IIR_RRI		0x00000004
#define PCH_UART_IIR_REI		0x00000006
#define PCH_UART_IIR_TOI		0x00000008
#define PCH_UART_IIR_FIFO256		0x00000020
#define PCH_UART_IIR_FIFO64		PCH_UART_IIR_FIFO256
#define PCH_UART_IIR_FE			0x000000C0
#define PCH_UART_FCR_FIFOE		0x00000001
#define PCH_UART_FCR_RFR		0x00000002
#define PCH_UART_FCR_TFR		0x00000004
#define PCH_UART_FCR_DMS		0x00000008
#define PCH_UART_FCR_FIFO256		0x00000020
#define PCH_UART_FCR_RFTL		0x000000C0
#define PCH_UART_FCR_RFTL1		0x00000000
#define PCH_UART_FCR_RFTL64		0x00000040
#define PCH_UART_FCR_RFTL128		0x00000080
#define PCH_UART_FCR_RFTL224		0x000000C0
#define PCH_UART_FCR_RFTL16		PCH_UART_FCR_RFTL64
#define PCH_UART_FCR_RFTL32		PCH_UART_FCR_RFTL128
#define PCH_UART_FCR_RFTL56		PCH_UART_FCR_RFTL224
#define PCH_UART_FCR_RFTL4		PCH_UART_FCR_RFTL64
#define PCH_UART_FCR_RFTL8		PCH_UART_FCR_RFTL128
#define PCH_UART_FCR_RFTL14		PCH_UART_FCR_RFTL224
#define PCH_UART_FCR_RFTL_SHIFT		6
#define PCH_UART_LCR_WLS	0x00000003
#define PCH_UART_LCR_STB	0x00000004
#define PCH_UART_LCR_PEN	0x00000008
#define PCH_UART_LCR_EPS	0x00000010
#define PCH_UART_LCR_SP		0x00000020
#define PCH_UART_LCR_SB		0x00000040
#define PCH_UART_LCR_DLAB	0x00000080
#define PCH_UART_LCR_NP		0x00000000
#define PCH_UART_LCR_OP		PCH_UART_LCR_PEN
#define PCH_UART_LCR_EP		(PCH_UART_LCR_PEN | PCH_UART_LCR_EPS)
#define PCH_UART_LCR_1P		(PCH_UART_LCR_PEN | PCH_UART_LCR_SP)
#define PCH_UART_LCR_0P		(PCH_UART_LCR_PEN | PCH_UART_LCR_EPS | PCH_UART_LCR_SP)
#define PCH_UART_LCR_5BIT	0x00000000
#define PCH_UART_LCR_6BIT	0x00000001
#define PCH_UART_LCR_7BIT	0x00000002
#define PCH_UART_LCR_8BIT	0x00000003
#define PCH_UART_MCR_DTR	0x00000001
#define PCH_UART_MCR_RTS	0x00000002
#define PCH_UART_MCR_OUT	0x0000000C
#define PCH_UART_MCR_LOOP	0x00000010
#define PCH_UART_MCR_AFE	0x00000020
#define PCH_UART_LSR_DR		0x00000001
#define PCH_UART_LSR_ERR	(1<<7)
#define PCH_UART_MSR_DCTS	0x00000001
#define PCH_UART_MSR_DDSR	0x00000002
#define PCH_UART_MSR_TERI	0x00000004
#define PCH_UART_MSR_DDCD	0x00000008
#define PCH_UART_MSR_CTS	0x00000010
#define PCH_UART_MSR_DSR	0x00000020
#define PCH_UART_MSR_RI		0x00000040
#define PCH_UART_MSR_DCD	0x00000080
#define PCH_UART_MSR_DELTA	(PCH_UART_MSR_DCTS | PCH_UART_MSR_DDSR | PCH_UART_MSR_TERI | PCH_UART_MSR_DDCD)
#define PCH_UART_DLL		0x00
#define PCH_UART_DLM		0x01
#define PCH_UART_BRCSR		0x0E

#define PCH_UART_IID_RLS	(PCH_UART_IIR_REI)
#define PCH_UART_IID_RDR	(PCH_UART_IIR_RRI)
#define PCH_UART_IID_RDR_TO	(PCH_UART_IIR_RRI | PCH_UART_IIR_TOI)
#define PCH_UART_IID_THRE	(PCH_UART_IIR_TRI)
#define PCH_UART_IID_MS		(PCH_UART_IIR_MSI)
#define PCH_UART_HAL_RX_INT		(PCH_UART_IER_ERBFI)
#define PCH_UART_HAL_RX_ERR_INT		(PCH_UART_IER_ELSI)
#define PCH_UART_HAL_TRIGGER_M		(PCH_UART_FCR_RFTL128)
#define PORT_PCH_8LINE	44
#define PORT_PCH_2LINE	45

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
    uint8_t rbr;
    uint8_t thr;
    uint8_t ier;
    uint8_t iir;
    uint8_t fcr;
    uint8_t lcr;
    uint8_t mcr;
    uint8_t lsr;
    uint8_t msr;
    uint8_t dll;
    uint8_t dlm;
    uint8_t brcsr;

    /* DMA Context */
    unsigned int use_dma;
    int tx_dma_use;

    int tx_empty;
    int trigger;
    int trigger_level;
    int fifo_size;
    
    unsigned int uartclk;
    int port_type;
};

#define PCH_UART_NR 4

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool raise = false;

    if ((s->lsr & 0x1E) && (s->ier & PCH_UART_IER_ELSI)) {
        s->iir = PCH_UART_IID_RLS;
        raise = true;
    } else if ((s->lsr & 0x01) && (s->ier & PCH_UART_IER_ERBFI)) {
        s->iir = PCH_UART_IID_RDR;
        raise = true;
    } else if ((s->intr_status & 1) && (s->ier & PCH_UART_IER_ETBEI)) {
        s->iir = PCH_UART_IID_THRE;
        raise = true;
    } else if ((s->msr & PCH_UART_MSR_DELTA) && (s->ier & PCH_UART_IER_EDSSI)) {
        s->iir = PCH_UART_IID_MS;
        raise = true;
    } else {
        s->iir = PCH_UART_IIR_IP;
    }

    if (raise) {
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

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case 0x00:
        if (s->lcr & PCH_UART_LCR_DLAB) {
            val = s->dll;
        } else {
            val = s->rbr;
            s->lsr &= ~0x01;
            pcibase_update_irq(s);
        }
        break;
    case 0x01:
        if (s->lcr & PCH_UART_LCR_DLAB) {
            val = s->dlm;
        } else {
            val = s->ier;
        }
        break;
    case 0x02:
        val = s->iir;
        if ((s->iir & 0x0F) == PCH_UART_IID_THRE) {
            s->intr_status &= ~1;
            pcibase_update_irq(s);
        }
        break;
    case 0x03:
        val = s->lcr;
        break;
    case 0x04:
        val = s->mcr;
        break;
    case 0x05:
        val = s->lsr;
        s->lsr &= ~0x1E;
        pcibase_update_irq(s);
        break;
    case 0x06:
        val = s->msr;
        s->msr &= ~PCH_UART_MSR_DELTA;
        pcibase_update_irq(s);
        break;
    case PCH_UART_BRCSR:
        val = s->brcsr;
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

    switch (addr) {
    case 0x00:
        if (s->lcr & PCH_UART_LCR_DLAB) {
            s->dll = val;
        } else {
            s->thr = val;
            s->lsr |= 0x60;
            s->intr_status |= 1;
            pcibase_update_irq(s);
        }
        break;
    case 0x01:
        if (s->lcr & PCH_UART_LCR_DLAB) {
            s->dlm = val;
        } else {
            if ((val & PCH_UART_IER_ETBEI) && !(s->ier & PCH_UART_IER_ETBEI) && (s->lsr & 0x20)) {
                s->intr_status |= 1;
            }
            s->ier = val;
            pcibase_update_irq(s);
        }
        break;
    case 0x02:
        s->fcr = val & ~0x06;
        break;
    case 0x03:
        s->lcr = val;
        break;
    case 0x04:
        s->mcr = val;
        break;
    case PCH_UART_BRCSR:
        s->brcsr = val;
        break;
    default:
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return pcibase_mmio_read(opaque, addr, size);
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    pcibase_mmio_write(opaque, addr, val, size);
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

    s->rbr = 0;
    s->thr = 0;
    s->ier = 0;
    s->iir = PCH_UART_IIR_IP;
    s->fcr = 0;
    s->lcr = 0;
    s->mcr = 0;
    s->lsr = 0x60;
    s->msr = 0xB0;
    s->dll = 0;
    s->dlm = 0;
    s->brcsr = 0;
    s->intr_status = 1;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_INTEL );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x8811 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0700 );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    if (msi_init(pdev, 0, 1, true, false, errp)) {
        /* MSI init failed, continue with INTx */
    }

    /* BAR Initialization */
    s->num_bars = 2;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 16;
    s->bar_info[0].name = "pch_uart_pio";

    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = 4096;
    s->bar_info[1].name = "pch_uart_mmio";
      
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
    .name = "pch_uart_pci",
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
