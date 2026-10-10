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

#define TYPE_PCIBASE_DEVICE "pch_spi_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_DEVICE_ID_GE_SPI 0x8816
#define PCH_SPCR 0x00
#define PCH_SPBRR 0x04
#define PCH_SPSR 0x08
#define PCH_SPDWR 0x0C
#define PCH_SPDRR 0x10
#define PCH_SSNXCR 0x18
#define PCH_SRST 0x1C
#define PCH_ADDRESS_SIZE 0x20

#define SPSR_ORF_BIT        (1 << 3)
#define SPSR_FI_BIT         (1 << 2)
#define SPSR_RFI_BIT        (1 << 1)
#define SPCR_SPE_BIT        (1 << 0)
#define SPCR_RFIC_FIELD     20
#define MASK_RFIC_SPCR_BITS (0xf << SPCR_RFIC_FIELD)
#define SPCR_TFIC_FIELD     16
#define MASK_TFIC_SPCR_BITS (0xf << SPCR_TFIC_FIELD)
#define PCH_TX_THOLD        2
#define PCH_BUF_SIZE        4096
#define PCH_DMA_TRANS_SIZE  12
#define PCH_MAX_FIFO_DEPTH  16
#define SSN_LOW             0x02U

#define SPCR_MSTR_BIT		(1 << 1)
#define SPCR_FICLR_BIT		(1 << 24)
#define SPCR_RFIE_BIT		(1 << 9)
#define SPCR_FIE_BIT		(1 << 10)
#define SPCR_ORIE_BIT		(1 << 11)
#define SPCR_LSBF_BIT		(1 << 4)
#define SPCR_CPOL_BIT		(1 << 6)
#define SPCR_CPHA_BIT		(1 << 5)
#define MASK_SPBRR_SPBR_BITS	((1 << 10) - 1)
#define SPBRR_SIZE_BIT		(1 << 10)
#define SSN_NO_CONTROL		0x00U
#define SSN_HIGH		0x03U
#define PCH_RX_THOLD		7
#define PCH_RX_THOLD_MAX	15

#define SPCR_TFIE_BIT		(1 << 8)
#define SPCR_MDFIE_BIT		(1 << 12)
#define PCH_SPSR_RFD		0x0000F800

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
    uint32_t irq_reg_sts;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t spcr;
    uint32_t spbrr;
    uint32_t spsr;
    uint32_t spdwr;
    uint32_t spdrr;
    uint32_t ssnxcr;
    uint32_t srst;

    /* DMA Context */
    dma_addr_t tx_buf_dma;
    dma_addr_t rx_buf_dma;

    uint8_t status;
    uint8_t transfer_active;
    uint8_t transfer_complete;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool level = (s->spsr & (SPSR_FI_BIT | SPSR_RFI_BIT | SPSR_ORF_BIT)) != 0;
    pci_set_irq(pdev, level);
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case PCH_SPCR:
        val = s->spcr;
        break;
    case PCH_SPBRR:
        val = s->spbrr;
        break;
    case PCH_SPSR:
        val = s->spsr;
        break;
    case PCH_SPDWR:
        val = s->spdwr;
        break;
    case PCH_SPDRR:
        val = s->spdrr;
        break;
    case PCH_SSNXCR:
        val = s->ssnxcr;
        break;
    case PCH_SRST:
        val = s->srst;
        break;
    default:
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case PCH_SPCR:
        /* If SPI is being enabled, simulate immediate transfer completion */
        if ((val & SPCR_SPE_BIT) && !(s->spcr & SPCR_SPE_BIT)) {
            s->spsr |= (SPSR_FI_BIT | SPSR_RFI_BIT);
            pcibase_update_irq(s);
        }
        s->spcr = val;
        break;
    case PCH_SPBRR:
        s->spbrr = val;
        break;
    case PCH_SPSR:
        s->spsr &= ~val; /* W1C */
        pcibase_update_irq(s);
        break;
    case PCH_SPDWR:
        s->spdwr = val;
        s->spdrr = val; /* Simple loopback for read */
        break;
    case PCH_SSNXCR:
        s->ssnxcr = val;
        break;
    case PCH_SRST:
        s->srst = val;
        if (val & 0x1) {
            s->spcr = 0;
            s->spbrr = 0;
            s->spsr = 0;
            s->spdwr = 0;
            s->spdrr = 0;
            s->ssnxcr = 0;
            pcibase_update_irq(s);
        }
        break;
    default:
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

    s->spcr = 0;
    s->spbrr = 0;
    s->spsr = 0;
    s->spdwr = 0;
    s->spdrr = 0;
    s->ssnxcr = 0;
    s->srst = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_INTEL );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_GE_SPI );
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
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = PCH_ADDRESS_SIZE;
    s->bar_info[0].name = "pch-spi-mmio";
  
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
