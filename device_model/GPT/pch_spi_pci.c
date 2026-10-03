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
#include "hw/pci/pci_ids.h"

#define TYPE_PCIBASE_DEVICE "pch_spi_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCH_SPCR               0x00
#define PCH_SPBRR              0x04
#define PCH_SPSR               0x08
#define PCH_SPDWR              0x0C
#define PCH_SPDRR              0x10
#define PCH_SSNXCR             0x18
#define PCH_SRST               0x1C
#define PCH_ADDRESS_SIZE       0x20

#define PCH_SPSR_TFD           0x000007C0
#define PCH_SPSR_RFD           0x0000F800
#define PCH_RX_THOLD           7
#define PCH_RX_THOLD_MAX       15
#define PCH_TX_THOLD           2
#define PCH_MAX_BAUDRATE       5000000
#define PCH_MAX_FIFO_DEPTH     16
#define STATUS_RUNNING         1
#define STATUS_EXITING         2
#define PCH_SLEEP_TIME         10
#define SSN_LOW                0x02U
#define SSN_HIGH               0x03U
#define SSN_NO_CONTROL         0x00U
#define PCH_MAX_CS             0xFF
#define PCI_DEVICE_ID_GE_SPI   0x8816

#define SPCR_SPE_BIT           (1 << 0)
#define SPCR_MSTR_BIT          (1 << 1)
#define SPCR_LSBF_BIT          (1 << 4)
#define SPCR_CPHA_BIT          (1 << 5)
#define SPCR_CPOL_BIT          (1 << 6)
#define SPCR_TFIE_BIT          (1 << 8)
#define SPCR_RFIE_BIT          (1 << 9)
#define SPCR_FIE_BIT           (1 << 10)
#define SPCR_ORIE_BIT          (1 << 11)
#define SPCR_MDFIE_BIT         (1 << 12)
#define SPCR_FICLR_BIT         (1 << 24)

#define SPSR_TFI_BIT           (1 << 0)
#define SPSR_RFI_BIT           (1 << 1)
#define SPSR_FI_BIT            (1 << 2)
#define SPSR_ORF_BIT           (1 << 3)

#define SPBRR_SIZE_BIT         (1 << 10)

#define PCH_ALL                (SPCR_TFIE_BIT | SPCR_RFIE_BIT | SPCR_FIE_BIT | \
                                SPCR_ORIE_BIT | SPCR_MDFIE_BIT)

#define SPCR_RFIC_FIELD        20
#define SPCR_TFIC_FIELD        16

#define MASK_SPBRR_SPBR_BITS   ((1 << 10) - 1)
#define MASK_RFIC_SPCR_BITS    (0xf << SPCR_RFIC_FIELD)
#define MASK_TFIC_SPCR_BITS    (0xf << SPCR_TFIC_FIELD)

#define PCH_CLOCK_HZ           50000000
#define PCH_MAX_SPBR           1023

#define PCH_SPI_MAX_DEV        2
#define PCH_BUF_SIZE           4096
#define PCH_DMA_TRANS_SIZE     12

/* Define missing PCI class code used by this device */
#ifndef PCI_CLASS_SERIAL_SPI
#define PCI_CLASS_SERIAL_SPI   0x0702
#endif

/* PCI identification: use first entry from pch_spi_pcidev_id table */
#define PCIBASE_VENDOR_ID      PCI_VENDOR_ID_INTEL
#define PCIBASE_DEVICE_ID      PCI_DEVICE_ID_GE_SPI
#define PCIBASE_CLASS_ID       PCI_CLASS_SERIAL_SPI

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
    struct {
        uint32_t spcr;
        uint32_t spbrr;
        uint32_t spsr;
        uint32_t spdwr;
        uint32_t spdrr;
        uint32_t ssnxcr;
        uint32_t srst;
        uint32_t address_size;
    } regs;

    /* Simple SPI transfer emulation state */
    uint16_t tx_fifo[PCH_MAX_FIFO_DEPTH];
    uint16_t rx_fifo[PCH_MAX_FIFO_DEPTH];
    unsigned int tx_count;
    unsigned int rx_count;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool pending = (s->regs.spsr & (SPSR_TFI_BIT | SPSR_RFI_BIT | SPSR_FI_BIT | SPSR_ORF_BIT)) != 0;
    bool enabled = (s->regs.spcr & (SPCR_TFIE_BIT | SPCR_RFIE_BIT | SPCR_FIE_BIT | SPCR_ORIE_BIT)) != 0;

    if (pending && enabled) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

static uint32_t pcibase_calc_spsr(PCIBaseState *s)
{
    uint32_t val = s->regs.spsr;

    /* Generate RFI when RX FIFO has data */
    if (s->rx_count > 0) {
        val |= SPSR_RFI_BIT;
    } else {
        val &= ~SPSR_RFI_BIT;
    }

    /* Generate TFI when TX FIFO is empty */
    if (s->tx_count == 0) {
        val |= SPSR_TFI_BIT;
    } else {
        val &= ~SPSR_TFI_BIT;
    }

    /* Generate FI when we have done at least one transfer (very simple) */
    if ((s->tx_count == 0) && (s->rx_count > 0)) {
        val |= SPSR_FI_BIT;
    }

    s->regs.spsr = val;
    return val;
}

static void pcibase_push_rx(PCIBaseState *s, uint16_t v)
{
    if (s->rx_count < PCH_MAX_FIFO_DEPTH) {
        s->rx_fifo[s->rx_count++] = v;
    } else {
        s->regs.spsr |= SPSR_ORF_BIT;
    }
}

static void pcibase_process_tx(PCIBaseState *s)
{
    /* Very small loopback implementation: every write to SPDWR appears in RX. */
    while (s->tx_count > 0) {
        uint16_t v = s->tx_fifo[0];
        unsigned int i;
        for (i = 1; i < s->tx_count; ++i) {
            s->tx_fifo[i - 1] = s->tx_fifo[i];
        }
        s->tx_count--;
        pcibase_push_rx(s, v);
    }

    pcibase_calc_spsr(s);
    pcibase_update_irq(s);
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case PCH_SPCR:
        val = s->regs.spcr;
        break;
    case PCH_SPBRR:
        val = s->regs.spbrr;
        break;
    case PCH_SPSR:
        /* Reading SPSR returns current status */
        val = pcibase_calc_spsr(s);
        break;
    case PCH_SPDWR:
        /* Not used by driver */
        val = s->regs.spdwr;
        break;
    case PCH_SPDRR:
        if (s->rx_count > 0) {
            uint16_t v = s->rx_fifo[0];
            unsigned int i;
            for (i = 1; i < s->rx_count; ++i) {
                s->rx_fifo[i - 1] = s->rx_fifo[i];
            }
            s->rx_count--;
            val = v;
        } else {
            val = 0;
        }
        pcibase_calc_spsr(s);
        pcibase_update_irq(s);
        s->regs.spdrr = (uint32_t)val;
        break;
    case PCH_SSNXCR:
        val = s->regs.ssnxcr;
        break;
    case PCH_SRST:
        val = s->regs.srst;
        break;
    default:
        val = 0;
        break;
    }

    /* Truncate to requested size */
    if (size == 1) {
        val &= 0xffu;
    } else if (size == 2) {
        val &= 0xffffu;
    } else if (size == 4) {
        val &= 0xffffffffu;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t v32 = (uint32_t)val;

    switch (addr) {
    case PCH_SPCR:
        /* Generic RMW semantics from driver helper pch_spi_setclr_reg */
        s->regs.spcr = v32;
        /* FIFO clear when FICLR bit toggled */
        if (s->regs.spcr & SPCR_FICLR_BIT) {
            s->tx_count = 0;
            s->rx_count = 0;
            s->regs.spsr &= ~(SPSR_ORF_BIT | SPSR_TFI_BIT | SPSR_RFI_BIT | SPSR_FI_BIT);
            s->regs.spcr &= ~SPCR_FICLR_BIT;
        }
        pcibase_calc_spsr(s);
        pcibase_update_irq(s);
        break;
    case PCH_SPBRR:
        /* Driver uses set/clear helper so we just store raw value */
        s->regs.spbrr = (v32 & (MASK_SPBRR_SPBR_BITS | SPBRR_SIZE_BIT));
        break;
    case PCH_SPSR:
        /* Driver writes back read SPSR to clear interrupt status.
         * Implement write-1-to-clear for all interrupt bits. */
        if (v32 & SPSR_TFI_BIT)
            s->regs.spsr &= ~SPSR_TFI_BIT;
        if (v32 & SPSR_RFI_BIT)
            s->regs.spsr &= ~SPSR_RFI_BIT;
        if (v32 & SPSR_FI_BIT)
            s->regs.spsr &= ~SPSR_FI_BIT;
        if (v32 & SPSR_ORF_BIT)
            s->regs.spsr &= ~SPSR_ORF_BIT;
        pcibase_update_irq(s);
        break;
    case PCH_SPDWR:
        s->regs.spdwr = v32;
        /* Push into TX FIFO as 16-bit words and process loopback */
        if (s->tx_count < PCH_MAX_FIFO_DEPTH) {
            s->tx_fifo[s->tx_count++] = (uint16_t)v32;
        }
        pcibase_process_tx(s);
        break;
    case PCH_SPDRR:
        /* Not used for writes, but keep shadow */
        s->regs.spdrr = v32;
        break;
    case PCH_SSNXCR:
        s->regs.ssnxcr = v32;
        break;
    case PCH_SRST:
        /* Simple reset toggle: when 1 then 0, clear FIFOs and status */
        s->regs.srst = v32;
        if (v32 == 0x1) {
            /* nothing immediate; driver clears to 0 afterwards */
        } else if (v32 == 0x0) {
            s->tx_count = 0;
            s->rx_count = 0;
            s->regs.spsr = 0;
            pcibase_update_irq(s);
        }
        break;
    default:
        break;
    }

    (void)size;
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    (void)s;
    (void)addr;
    (void)size;
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    (void)s;
    (void)addr;
    (void)val;
    (void)size;
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

    s->regs.spcr = 0;
    s->regs.spbrr = 0;
    s->regs.spsr = 0;
    s->regs.spdwr = 0;
    s->regs.spdrr = 0;
    s->regs.ssnxcr = 0;
    s->regs.srst = 0;
    s->regs.address_size = PCH_ADDRESS_SIZE;

    s->tx_count = 0;
    s->rx_count = 0;

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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCIBASE_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCIBASE_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCIBASE_CLASS_ID );
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
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x100;
    s->bar_info[0].name = "pch-spi-mmio";

    /* BAR1 is used by the driver for pci_resource_start(pdev, 1) */
    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = 0x1000;
    s->bar_info[1].name = "pch-spi-mmio-1";

    for (int i = 2; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = NULL;
    }
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = false;
    s->has_msix = false;

    /* Initialize internal state */
    s->tx_count = 0;
    s->rx_count = 0;
    s->regs.address_size = PCH_ADDRESS_SIZE;
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
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "pch_spi_pci",
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
