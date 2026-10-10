/*
 * QEMU 8.2.10 virtual PCI device model for Intel 8250 LPSS UART (QRK)
 * Based on Linux driver: drivers/tty/serial/8250/8250_lpss.c
 * Emulates basic 16550A UART with LPSS extended registers.
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

#define TYPE_PCIBASE_DEVICE "8250_lpss_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI_VENDOR_ID_INTEL 0x8086
#define PCI_DEVICE_ID_INTEL_QRK_UARTx 0x0936
#define CLASS_ID 0x0700

/* LPSS-specific extended registers */
#define BYT_PRV_CLK            0x800
#define BYT_PRV_CLK_EN         BIT(0)
#define BYT_PRV_CLK_M_VAL_SHIFT 1
#define BYT_PRV_CLK_N_VAL_SHIFT 16
#define BYT_PRV_CLK_UPDATE     BIT(31)
#define BYT_TX_OVF_INT         0x820
#define BYT_TX_OVF_INT_MASK    BIT(1)

/* Standard 16550A UART registers (logical index, used after regshift=2) */
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

/* IER bits */
#define UART_IER_THRI  0x02
/* IIR bits */
#define UART_IIR_NO_INT 0x01
#define UART_IIR_THRI   0x02
/* LSR bits */
#define UART_LSR_THRE   0x20
#define UART_LSR_TEMT   0x40
/* MSR bits */
#define UART_MSR_DCD    0x80
#define UART_MSR_DSR    0x20

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

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;

    struct {
        uint32_t prv_clk;
        uint32_t tx_ovf_int;
        /* UART registers (8-bit, accessed at 32-bit offsets) */
        uint8_t thr;        /* Write-only */
        uint8_t rbr;        /* Read-only, shares addr with THR */
        uint8_t ier;
        uint8_t iir;        /* Read-only */
        uint8_t fcr;        /* Write-only */
        uint8_t lcr;
        uint8_t mcr;
        uint8_t lsr;
        uint8_t msr;
        uint8_t scr;
        uint8_t dll;        /* Divisor latch low */
        uint8_t dlm;        /* Divisor latch high */
    } regs;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool int_pending = false;
    uint8_t iir = UART_IIR_NO_INT;

    /* Only THRE interrupt implemented (highest priority for simplicity) */
    if ((s->regs.ier & UART_IER_THRI) && (s->regs.lsr & UART_LSR_THRE)) {
        int_pending = true;
        iir = UART_IIR_THRI;
    }

    /* If FIFOs are enabled, set FIFO status bits in IIR */
    if (s->regs.fcr & 0x01) {
        iir |= 0xC0;
    }
    s->regs.iir = iir;

    if (int_pending) {
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

    if (addr == BYT_PRV_CLK) {
        val = s->regs.prv_clk;
    } else if (addr == BYT_TX_OVF_INT) {
        val = s->regs.tx_ovf_int;
    } else if (addr < 0x20) {
        unsigned reg = (addr >> 2) & 0x7;
        if (s->regs.lcr & 0x80) {   /* Divisor latch access */
            if (reg == UART_DLL) {
                val = s->regs.dll;
            } else if (reg == UART_DLM) {
                val = s->regs.dlm;
            }
        } else {
            switch (reg) {
            case UART_RBR:
                val = s->regs.rbr;
                break;
            case UART_IER:
                val = s->regs.ier;
                break;
            case UART_IIR:
                val = s->regs.iir;
                break;
            case UART_LCR:
                val = s->regs.lcr;
                break;
            case UART_MCR:
                val = s->regs.mcr;
                break;
            case UART_LSR:
                val = s->regs.lsr;
                break;
            case UART_MSR:
                val = s->regs.msr;
                break;
            case UART_SCR:
                val = s->regs.scr;
                break;
            default:
                break;
            }
        }
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint8_t byte = val & 0xFF;

    if (addr == BYT_PRV_CLK) {
        s->regs.prv_clk = val;
    } else if (addr == BYT_TX_OVF_INT) {
        s->regs.tx_ovf_int = val;
    } else if (addr < 0x20) {
        unsigned reg = (addr >> 2) & 0x7;
        if (s->regs.lcr & 0x80) {   /* Divisor latch access */
            if (reg == UART_DLL) {
                s->regs.dll = byte;
            } else if (reg == UART_DLM) {
                s->regs.dlm = byte;
            }
        } else {
            switch (reg) {
            case UART_THR:
                s->regs.thr = byte;
                /* THR write clears THRE momentarily; we keep it asserted, so re-assert immediately. */
                s->regs.lsr |= (UART_LSR_THRE | UART_LSR_TEMT);
                break;
            case UART_IER:
                s->regs.ier = byte & 0x0F;
                break;
            case UART_FCR:
                s->regs.fcr = byte;
                break;
            case UART_LCR:
                s->regs.lcr = byte;
                break;
            case UART_MCR:
                s->regs.mcr = byte;
                break;
            case UART_LSR:
                /* LSR is read-only; ignore writes */
                break;
            case UART_MSR:
                /* MSR is read-only; ignore writes */
                break;
            case UART_SCR:
                s->regs.scr = byte;
                break;
            default:
                break;
            }
        }
        pcibase_update_irq(s);
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

    memset(&s->regs, 0, sizeof(s->regs));
    s->regs.lsr = UART_LSR_THRE | UART_LSR_TEMT;  /* Transmitter empty */
    s->regs.msr = UART_MSR_DCD | UART_MSR_DSR;    /* DCD and DSR active */
    s->regs.iir = UART_IIR_NO_INT;                /* No interrupt pending */
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
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_INTEL);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_INTEL_QRK_UARTx);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);

    /* Single MMIO BAR for UART and LPSS registers */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x1000;
    s->bar_info[0].name = "8250_lpss_mmio";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    if (msi_init(pdev, 0, 1, true, false, errp)) {
        return;
    }

    memset(&s->regs, 0, sizeof(s->regs));
    s->regs.lsr = UART_LSR_THRE | UART_LSR_TEMT;
    s->regs.msr = UART_MSR_DCD | UART_MSR_DSR;
    s->regs.iir = UART_IIR_NO_INT;
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
    .name = "8250_lpss_pci",
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

type_init(pcibase_register_types)
