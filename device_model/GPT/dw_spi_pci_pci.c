/*
 * QEMU PCI device model for DesignWare SPI PCI (minimal behavioral model)
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
#include "hw/misc/unimp.h"

#define TYPE_PCIBASE_DEVICE "dw_spi_pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define DW_SPI_PCI_VENDOR_ID 0x8086
#define DW_SPI_PCI_DEVICE_ID 0x0800
#define DW_SPI_PCI_CLASS_ID  0x0c05

#define DW_SPI_DR                0x60
#define DW_SPI_SER               0x10
#define DW_SPI_SSIENR            0x08
#define DW_SPI_CTRLR0            0x00
#define DW_SPI_TXFTLR            0x18
#define DW_SPI_CS_OVERRIDE       0xf4
#define DW_SPI_VERSION           0x5c
#define DW_SPI_DMACR             0x4c
#define DW_SPI_ICR               0x48
#define DW_SPI_IMR               0x2c
#define DW_SPI_CTRLR1            0x04
#define DW_SPI_RX_SAMPLE_DLY     0xf0
#define DW_SPI_RXFTLR            0x1c
#define DW_SPI_RXFLR             0x24
#define DW_SPI_ISR               0x30
#define DW_SPI_DMARDLR           0x54
#define DW_SPI_SR                0x28
#define DW_SPI_BAUDR             0x14
#define DW_SPI_TXFLR             0x20
#define DW_SPI_DMATDLR           0x50

#define DW_SPI_INT_TXEI          (1U << 0)
#define DW_SPI_INT_TXOI          (1U << 1)
#define DW_SPI_INT_RXUI          (1U << 2)
#define DW_SPI_INT_RXOI          (1U << 3)
#define DW_SPI_INT_RXFI          (1U << 4)

#define DW_SPI_SR_RF_NOT_EMPT    (1U << 3)
#define DW_SPI_SR_TF_EMPT        (1U << 2)
#define DW_SPI_SR_BUSY           (1U << 0)

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
    uint32_t regs[0x200 / 4];

};


/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* Simple level-triggered interrupt generation based on ISR & IMR */
    uint32_t isr = s->regs[DW_SPI_ISR / 4];
    uint32_t imr = s->regs[DW_SPI_IMR / 4];
    bool pending = (isr & imr) != 0;

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

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    (void)s;
    (void)pdev;
    (void)is_write;
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= sizeof(s->regs)) {
        return 0;
    }

    /* enforce 32-bit aligned accesses for simplicity */
    hwaddr offset = addr & ~0x3ULL;
    uint32_t *regs = s->regs;

    switch (offset) {
    case DW_SPI_SR: {
        /* Update basic status flags based on simple FIFO model */
        uint32_t txflr = regs[DW_SPI_TXFLR / 4];
        uint32_t rxflr = regs[DW_SPI_RXFLR / 4];
        uint32_t sr = 0;
        if (rxflr != 0) {
            sr |= DW_SPI_SR_RF_NOT_EMPT;
        }
        if (txflr == 0) {
            sr |= DW_SPI_SR_TF_EMPT;
        }
        if (txflr != 0) {
            sr |= DW_SPI_SR_BUSY;
        }
        regs[DW_SPI_SR / 4] = sr;
        val = sr;
        break;
    }
    case DW_SPI_RXFLR:
    case DW_SPI_TXFLR:
    case DW_SPI_IMR:
    case DW_SPI_ISR:
    case DW_SPI_ICR:
    case DW_SPI_SSIENR:
    case DW_SPI_SER:
    case DW_SPI_CTRLR0:
    case DW_SPI_CTRLR1:
    case DW_SPI_TXFTLR:
    case DW_SPI_RXFTLR:
    case DW_SPI_BAUDR:
    case DW_SPI_DMACR:
    case DW_SPI_DMARDLR:
    case DW_SPI_DMATDLR:
    case DW_SPI_RX_SAMPLE_DLY:
    case DW_SPI_VERSION:
    case DW_SPI_CS_OVERRIDE:
        val = regs[offset / 4];
        break;
    case DW_SPI_DR: {
        /* Pop one word from RX FIFO if available */
        uint32_t rxflr = regs[DW_SPI_RXFLR / 4];
        if (rxflr > 0) {
            val = regs[DW_SPI_DR / 4];
            rxflr--;
            regs[DW_SPI_RXFLR / 4] = rxflr;

            /* If RX FIFO became empty, clear RF_NOT_EMPT and RXFI interrupt */
            if (rxflr == 0) {
                regs[DW_SPI_SR / 4] &= ~DW_SPI_SR_RF_NOT_EMPT;
                regs[DW_SPI_ISR / 4] &= ~DW_SPI_INT_RXFI;
                pcibase_update_irq(s);
            }
        } else {
            val = regs[DW_SPI_DR / 4];
        }
        break;
    }
    default:
        val = regs[offset / 4];
        break;
    }

    /* shrink to requested size (device is little-endian) */
    if (size == 1) {
        val &= 0xffu;
    } else if (size == 2) {
        val &= 0xffffu;
    } else {
        val &= 0xffffffffu;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= sizeof(s->regs)) {
        return;
    }

    hwaddr offset = addr & ~0x3ULL;
    uint32_t *regs = s->regs;
    uint32_t wval = (uint32_t)val;

    switch (offset) {
    case DW_SPI_IMR:
        /* Interrupt mask register: simple read/write */
        regs[DW_SPI_IMR / 4] = wval;
        pcibase_update_irq(s);
        break;
    case DW_SPI_ICR:
        /* Clear interrupt status bits: write-1-to-clear semantics */
        regs[DW_SPI_ISR / 4] &= ~wval;
        pcibase_update_irq(s);
        break;
    case DW_SPI_SSIENR:
        /* Enable/disable controller */
        regs[DW_SPI_SSIENR / 4] = wval & 0x1;
        break;
    case DW_SPI_SER:
        /* Chip select enable bits */
        regs[DW_SPI_SER / 4] = wval;
        break;
    case DW_SPI_BAUDR:
    case DW_SPI_CTRLR0:
    case DW_SPI_CTRLR1:
    case DW_SPI_TXFTLR:
    case DW_SPI_RXFTLR:
    case DW_SPI_DMACR:
    case DW_SPI_DMARDLR:
    case DW_SPI_DMATDLR:
    case DW_SPI_RX_SAMPLE_DLY:
    case DW_SPI_CS_OVERRIDE:
        regs[offset / 4] = wval;
        break;
    case DW_SPI_DR: {
        /* Simple TX/RX loopback model: each write produces one RX word */
        regs[DW_SPI_DR / 4] = wval;

        uint32_t txflr = regs[DW_SPI_TXFLR / 4];
        uint32_t rxflr = regs[DW_SPI_RXFLR / 4];

        txflr++;
        regs[DW_SPI_TXFLR / 4] = txflr;

        /* Immediately complete transfer and move to RX FIFO */
        if (txflr > 0) {
            txflr--;
            regs[DW_SPI_TXFLR / 4] = txflr;
        }

        /* Put received data into RX FIFO */
        rxflr++;
        regs[DW_SPI_RXFLR / 4] = rxflr;
        regs[DW_SPI_DR / 4] = wval; /* last received word */

        /* Update status register */
        uint32_t sr = regs[DW_SPI_SR / 4];
        sr |= DW_SPI_SR_RF_NOT_EMPT;
        if (txflr == 0) {
            sr |= DW_SPI_SR_TF_EMPT;
            sr &= ~DW_SPI_SR_BUSY;
        } else {
            sr |= DW_SPI_SR_BUSY;
        }
        regs[DW_SPI_SR / 4] = sr;

        /* Trigger RX FIFO interrupt if unmasked */
        regs[DW_SPI_ISR / 4] |= DW_SPI_INT_RXFI;
        pcibase_update_irq(s);
        break;
    }
    default:
        regs[offset / 4] = wval;
        break;
    }
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

    /* Initialize important registers to sane defaults */
    memset(s->regs, 0, sizeof(s->regs));

    /* Version register: arbitrary non-zero value so driver can detect HW */
    s->regs[DW_SPI_VERSION / 4] = 0x2a535353; /* "*SSS"-like pattern, non-zero */

    /* FIFOs empty */
    s->regs[DW_SPI_TXFLR / 4] = 0;
    s->regs[DW_SPI_RXFLR / 4] = 0;

    /* Status: TX empty, not busy, RX empty */
    s->regs[DW_SPI_SR / 4] = DW_SPI_SR_TF_EMPT;

    /* Mask all interrupts by default */
    s->regs[DW_SPI_IMR / 4] = 0;
    s->regs[DW_SPI_ISR / 4] = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  DW_SPI_PCI_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DW_SPI_PCI_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, DW_SPI_PCI_CLASS_ID );
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
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x1000;
    s->bar_info[0].name = "dw_spi_pci-bar0";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = "";
    }
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No MSI/MSI-X explicitly referenced in the provided driver snippet */

    /* No DMA configuration in Phase 1 (structure only) */

    /* No timer configuration in Phase 1 (structure only) */

    /* No additional field initialization beyond zeroed defaults in Phase 1 */
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
    .name = "dw_spi_pci_pci",
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
