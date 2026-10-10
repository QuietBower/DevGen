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

#define TYPE_PCIBASE_DEVICE "dw_spi_pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_INTEL 0x8086
#define PCI_DEVICE_ID_INTEL_SPI 0x0800

#define DW_SPI_CTRLR0          0x00
#define DW_SPI_CTRLR1          0x04
#define DW_SPI_SSIENR          0x08
#define DW_SPI_SER             0x10
#define DW_SPI_BAUDR           0x14
#define DW_SPI_TXFTLR          0x18
#define DW_SPI_RXFTLR          0x1c
#define DW_SPI_TXFLR           0x20
#define DW_SPI_RXFLR           0x24
#define DW_SPI_SR              0x28
#define DW_SPI_IMR             0x2c
#define DW_SPI_ISR             0x30
#define DW_SPI_RISR            0x34
#define DW_SPI_ICR             0x48
#define DW_SPI_DMACR           0x4c
#define DW_SPI_DMATDLR         0x50
#define DW_SPI_DMARDLR         0x54
#define DW_SPI_VERSION         0x5c
#define DW_SPI_DR              0x60
#define DW_SPI_RX_SAMPLE_DLY   0xf0
#define DW_SPI_CS_OVERRIDE     0xf4

#define MRST_CLK_SPI_REG       0xff11d86c
#define CLK_SPI_CDIV_MASK      0x00000e00
#define CLK_SPI_CDIV_OFFSET    9
#define MRST_SPI_CLK_BASE      100000000

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
    uint32_t imr;
    uint32_t isr;
    uint32_t risr;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t ctrlr0;
    uint32_t ctrlr1;
    uint32_t ssienr;
    uint32_t ser;
    uint32_t baudr;
    uint32_t txftlr;
    uint32_t rxftlr;
    uint32_t txflr;
    uint32_t rxflr;
    uint32_t sr;
    uint32_t icr;
    uint32_t dmacr;
    uint32_t dmatdlr;
    uint32_t dmardlr;
    uint32_t version;
    uint32_t dr;
    uint32_t rx_sample_dly;
    uint32_t cs_override;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool level = (s->isr & s->imr) != 0;
    
    if (s->has_msi) {
        if (level) {
            msi_notify(pdev, 0);
        }
    } else {
        pci_set_irq(pdev, level);
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case DW_SPI_CTRLR0: val = s->ctrlr0; break;
    case DW_SPI_CTRLR1: val = s->ctrlr1; break;
    case DW_SPI_SSIENR: val = s->ssienr; break;
    case DW_SPI_SER: val = s->ser; break;
    case DW_SPI_BAUDR: val = s->baudr; break;
    case DW_SPI_TXFTLR: val = s->txftlr; break;
    case DW_SPI_RXFTLR: val = s->rxftlr; break;
    case DW_SPI_TXFLR: val = s->txflr; break;
    case DW_SPI_RXFLR: val = s->rxflr; break;
    case DW_SPI_SR: val = s->sr; break;
    case DW_SPI_IMR: val = s->imr; break;
    case DW_SPI_ISR: val = s->isr; break;
    case DW_SPI_RISR: val = s->risr; break;
    case DW_SPI_ICR:
        val = s->icr;
        /* Reading ICR clears interrupts */
        s->isr = 0;
        s->risr = 0;
        pcibase_update_irq(s);
        break;
    case DW_SPI_DMACR: val = s->dmacr; break;
    case DW_SPI_DMATDLR: val = s->dmatdlr; break;
    case DW_SPI_DMARDLR: val = s->dmardlr; break;
    case DW_SPI_VERSION: val = s->version; break;
    case DW_SPI_DR: val = s->dr; break;
    case DW_SPI_RX_SAMPLE_DLY: val = s->rx_sample_dly; break;
    case DW_SPI_CS_OVERRIDE: val = s->cs_override; break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad read at offset 0x%" HWADDR_PRIx "\n", __func__, addr);
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case DW_SPI_CTRLR0: s->ctrlr0 = val; break;
    case DW_SPI_CTRLR1: s->ctrlr1 = val; break;
    case DW_SPI_SSIENR: s->ssienr = val; break;
    case DW_SPI_SER: s->ser = val; break;
    case DW_SPI_BAUDR: s->baudr = val; break;
    case DW_SPI_TXFTLR: s->txftlr = val; break;
    case DW_SPI_RXFTLR: s->rxftlr = val; break;
    case DW_SPI_TXFLR: s->txflr = val; break;
    case DW_SPI_RXFLR: s->rxflr = val; break;
    case DW_SPI_SR: s->sr = val; break;
    case DW_SPI_IMR:
        s->imr = val;
        pcibase_update_irq(s);
        break;
    case DW_SPI_ISR: s->isr = val; break;
    case DW_SPI_RISR: s->risr = val; break;
    case DW_SPI_ICR: s->icr = val; break;
    case DW_SPI_DMACR: s->dmacr = val; break;
    case DW_SPI_DMATDLR: s->dmatdlr = val; break;
    case DW_SPI_DMARDLR: s->dmardlr = val; break;
    case DW_SPI_VERSION: s->version = val; break;
    case DW_SPI_DR: s->dr = val; break;
    case DW_SPI_RX_SAMPLE_DLY: s->rx_sample_dly = val; break;
    case DW_SPI_CS_OVERRIDE: s->cs_override = val; break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad write at offset 0x%" HWADDR_PRIx "\n", __func__, addr);
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* PIO not used by driver */
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* PIO not used by driver */
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

    s->ctrlr0 = 0;
    s->ctrlr1 = 0;
    s->ssienr = 0;
    s->ser = 0;
    s->baudr = 0;
    s->txftlr = 0;
    s->rxftlr = 0;
    s->txflr = 0;
    s->rxflr = 0;
    s->sr = 0;
    s->imr = 0;
    s->isr = 0;
    s->risr = 0;
    s->icr = 0;
    s->dmacr = 0;
    s->dmatdlr = 0;
    s->dmardlr = 0;
    s->version = 0;
    s->dr = 0;
    s->rx_sample_dly = 0;
    s->cs_override = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x8086 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x0800 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0c80 );
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
    s->bar_info[0].name = "dw_spi_bar0";
    s->bar_info[0].size = 0x1000; /* Placeholder size, needs probe function */
    
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    if (msi_init(pdev, 0, 1, true, false, errp) == 0) {
        s->has_msi = true;
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
