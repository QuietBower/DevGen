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

#define TYPE_PCIBASE_DEVICE "pch_uart_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define VENDOR_ID       0x8086
#define DEVICE_ID       0x8811
#define CLASS_ID        0x0700

#define PCH_UART_RBR    0x00
#define PCH_UART_THR    0x00
#define PCH_UART_IER    0x01
#define PCH_UART_IIR    0x02
#define PCH_UART_LCR    0x03
#define PCH_UART_MCR    0x04
#define PCH_UART_LSR    0x05
#define PCH_UART_MSR    0x06
#define PCH_UART_DLL    0x00
#define PCH_UART_DLM    0x01
#define PCH_UART_BRCSR  0x0E
#define UART_FCR        0x02

#define PCH_UART_IER_ERBFI  0x01
#define PCH_UART_IER_ETBEI  0x02
#define PCH_UART_IER_ELSI   0x04
#define PCH_UART_IER_EDSSI  0x08
#define PCH_UART_IER_MASK   (PCH_UART_IER_ERBFI|PCH_UART_IER_ETBEI|PCH_UART_IER_ELSI|PCH_UART_IER_EDSSI)

#define PCH_UART_IIR_IP     0x01
#define PCH_UART_IIR_IID    0x06
#define PCH_UART_IIR_MSI    0x00
#define PCH_UART_IIR_TRI    0x02
#define PCH_UART_IIR_RRI    0x04
#define PCH_UART_IIR_REI    0x06
#define PCH_UART_IIR_TOI    0x08
#define PCH_UART_IIR_FIFO256 0x20
#define PCH_UART_IIR_FIFO64  PCH_UART_IIR_FIFO256
#define PCH_UART_IIR_FE     0xC0

#define PCH_UART_FCR_FIFOE  0x01
#define PCH_UART_FCR_RFR    0x02
#define PCH_UART_FCR_TFR    0x04
#define PCH_UART_FCR_DMS    0x08
#define PCH_UART_FCR_FIFO256 0x20
#define PCH_UART_FCR_RFTL   0xC0
#define PCH_UART_FCR_RFTL1  0x00
#define PCH_UART_FCR_RFTL64 0x40
#define PCH_UART_FCR_RFTL128 0x80
#define PCH_UART_FCR_RFTL224 0xC0

#define PCH_UART_LCR_WLS    0x03
#define PCH_UART_LCR_STB    0x04
#define PCH_UART_LCR_PEN    0x08
#define PCH_UART_LCR_EPS    0x10
#define PCH_UART_LCR_SP     0x20
#define PCH_UART_LCR_SB     0x40
#define PCH_UART_LCR_DLAB   0x80

#define PCH_UART_MCR_DTR    0x01
#define PCH_UART_MCR_RTS    0x02
#define PCH_UART_MCR_OUT    0x0C
#define PCH_UART_MCR_LOOP   0x10
#define PCH_UART_MCR_AFE    0x20

#define PCH_UART_LSR_DR     0x01
#define PCH_UART_LSR_ERR    (1<<7)

#define UART_LSR_DR         0x01
#define UART_LSR_OE         0x02
#define UART_LSR_PE         0x04
#define UART_LSR_FE         0x08
#define UART_LSR_BI         0x10
#define UART_LSR_THRE       0x20
#define UART_LSR_TEMT       0x40

#define PCH_UART_MSR_DCTS   0x01
#define PCH_UART_MSR_DDSR   0x02
#define PCH_UART_MSR_TERI   0x04
#define PCH_UART_MSR_DDCD   0x08
#define PCH_UART_MSR_CTS    0x10
#define PCH_UART_MSR_DSR    0x20
#define PCH_UART_MSR_RI     0x40
#define PCH_UART_MSR_DCD    0x80
#define PCH_UART_MSR_DELTA  (PCH_UART_MSR_DCTS | PCH_UART_MSR_DDSR | PCH_UART_MSR_TERI | PCH_UART_MSR_DDCD)

#define PCH_UART_IID_RLS    0x06
#define PCH_UART_IID_RDR    0x04
#define PCH_UART_IID_RDR_TO 0x0C
#define PCH_UART_IID_THRE   0x02
#define PCH_UART_IID_MS     0x00

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
        uint8_t rbr_thr;
        uint8_t ier;
        uint8_t fcr;
        uint8_t lcr;
        uint8_t mcr;
        uint8_t lsr;
        uint8_t msr;
        uint8_t scr;
        uint8_t dll;
        uint8_t dlm;
        uint8_t brcsr;
    } regs;

    bool dma_enabled;
    dma_addr_t dma_rx_buf;
    dma_addr_t dma_tx_buf;
    uint32_t dma_cnt;
    uint32_t dma_status;

    bool tx_busy;
    bool rx_busy;
    bool loopback;

    bool reset_done;

    uint8_t pm_state;
};

static uint8_t pch_uart_get_iir(PCIBaseState *s)
{
    uint8_t ier = s->regs.ier;
    uint8_t lsr = s->regs.lsr;
    uint8_t msr = s->regs.msr;
    uint8_t iir = 0;
    bool pending = false;

    if (s->regs.fcr & PCH_UART_FCR_FIFOE) {
        iir |= 0xC0; /* FIFO enabled, 256 bytes */
    }

    if (ier & PCH_UART_IER_ELSI) {
        if (lsr & (UART_LSR_BI | UART_LSR_FE | UART_LSR_PE | UART_LSR_OE)) {
            iir |= PCH_UART_IID_RLS;
            pending = true;
        }
    }
    if (!pending && (ier & PCH_UART_IER_ERBFI)) {
        if (lsr & UART_LSR_DR) {
            iir |= PCH_UART_IID_RDR;
            pending = true;
        }
    }
    if (!pending && (ier & PCH_UART_IER_ETBEI)) {
        if (lsr & UART_LSR_THRE) {
            iir |= PCH_UART_IID_THRE;
            pending = true;
        }
    }
    if (!pending && (ier & PCH_UART_IER_EDSSI)) {
        if (msr & PCH_UART_MSR_DELTA) {
            iir |= PCH_UART_IID_MS;
            pending = true;
        }
    }
    if (!pending) {
        iir |= PCH_UART_IIR_IP;
    }

    return iir;
}

G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint8_t iir = pch_uart_get_iir(s);
    bool pending = !(iir & PCH_UART_IIR_IP);

    if (pending) {
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

G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* No DMA logic required; external DMA controller handles transfers. */
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size != 1) {
        return 0;
    }

    switch (addr) {
    case 0x00:
        if (s->regs.lcr & PCH_UART_LCR_DLAB) {
            val = s->regs.dll;
        } else {
            val = s->regs.rbr_thr;
            s->regs.lsr &= ~UART_LSR_DR;
            pcibase_update_irq(s);
        }
        break;
    case 0x01:
        if (s->regs.lcr & PCH_UART_LCR_DLAB) {
            val = s->regs.dlm;
        } else {
            val = s->regs.ier;
        }
        break;
    case 0x02:
        val = pch_uart_get_iir(s);
        break;
    case 0x03:
        val = s->regs.lcr;
        break;
    case 0x04:
        val = s->regs.mcr;
        break;
    case 0x05:
        val = s->regs.lsr;
        break;
    case 0x06:
        val = s->regs.msr;
        s->regs.msr &= ~PCH_UART_MSR_DELTA;
        pcibase_update_irq(s);
        break;
    case 0x07:
        val = s->regs.scr;
        break;
    case 0x0E:
        val = s->regs.brcsr;
        break;
    default:
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
    case 0x00:
        if (s->regs.lcr & PCH_UART_LCR_DLAB) {
            s->regs.dll = val;
        } else {
            s->regs.rbr_thr = val;
        }
        break;
    case 0x01:
        if (s->regs.lcr & PCH_UART_LCR_DLAB) {
            s->regs.dlm = val;
        } else {
            s->regs.ier = val & PCH_UART_IER_MASK;
            pcibase_update_irq(s);
        }
        break;
    case 0x02:
        s->regs.fcr = val;
        break;
    case 0x03:
        s->regs.lcr = val;
        break;
    case 0x04:
        s->regs.mcr = val;
        break;
    case 0x07:
        s->regs.scr = val;
        break;
    default:
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* No PIO logic required; device uses MMIO. */
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* No PIO logic required; device uses MMIO. */
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

    /* Revert registers to power-on defaults */
    s->regs.rbr_thr = 0;
    s->regs.ier = 0;
    s->regs.fcr = 0;
    s->regs.lcr = 0;
    s->regs.mcr = 0;
    s->regs.lsr = UART_LSR_THRE | UART_LSR_TEMT;
    s->regs.msr = 0;
    s->regs.scr = 0;
    s->regs.dll = 0;
    s->regs.dlm = 0;
    s->regs.brcsr = 0;

    pcibase_update_irq(s);
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x8086 );
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

    s->num_bars = 2;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_PIO, .size = 256, .name = "pch-uart-pio" };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_MMIO, .size = 256, .name = "pch-uart-mmio" };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    msi_init(pdev, 0, 1, true, false, errp);
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
