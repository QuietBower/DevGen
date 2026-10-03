/*
 * QEMU PCI device model for Loongson SPI PCI controller
 * Generated according to provided skeleton and Linux driver behavior.
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
/* Removed linux/pci_ids.h: not available in QEMU build environment */

#define TYPE_PCIBASE_DEVICE "loongson_spi_pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define LOONGSON_SPI_SPSR_SPIF      (1 << 7)
#define LOONGSON_SPI_SPCR_SPE       (1 << 6)
#define LOONGSON_SPI_SPSR_REG       0x01
#define LOONGSON_SPI_SPCR_REG       0x00
#define LOONGSON_SPI_SPSR_WCOL      (1 << 6)
#define LOONGSON_SPI_PARA_REG       0x04
#define LOONGSON_SPI_SFCS_REG       0x05
#define LOONGSON_SPI_PARA_MEM_EN    (1 << 0)
#define LOONGSON_SPI_TIMI_REG       0x06
#define LOONGSON_SPI_SPER_REG       0x03
#define LOONGSON_SPI_SPCR_CPHA      (1 << 2)
#define LOONGSON_SPI_SPCR_CPOL      (1 << 3)
#define LOONGSON_SPI_FIFO_REG       0x02
#define LOONGSON_SPI_SPSR_RFEMPTY   (1 << 0)

/* Local copies of constants normally provided by linux/pci_ids.h */
#define PCIBASE_VENDOR_ID   0x0014  /* PCI_VENDOR_ID_LOONGSON */
#define PCIBASE_DEVICE_ID   0x7a0b
#define PCIBASE_CLASS_ID    0x0c00  /* PCI_CLASS_SERIAL_BUS */

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
        uint8_t spcr;
        uint8_t spsr;
        uint8_t sper;
        uint8_t para;
        uint8_t sfcs;
        uint8_t timi;
    } regs;

};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    (void)pdev;
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

    /* The Linux driver only passes this BAR base to loongson_spi_init_controller().
     * No explicit register access pattern is provided, so we expose a simple
     * byte-addressable register file for the known offsets.
     */

    switch (addr) {
    case LOONGSON_SPI_SPCR_REG:
        val = s->regs.spcr;
        break;
    case LOONGSON_SPI_SPSR_REG:
        val = s->regs.spsr;
        break;
    case LOONGSON_SPI_SPER_REG:
        val = s->regs.sper;
        break;
    case LOONGSON_SPI_FIFO_REG:
        /* Simple dummy FIFO: always empty */
        val = 0x00;
        break;
    case LOONGSON_SPI_PARA_REG:
        val = s->regs.para;
        break;
    case LOONGSON_SPI_SFCS_REG:
        val = s->regs.sfcs;
        break;
    case LOONGSON_SPI_TIMI_REG:
        val = s->regs.timi;
        break;
    default:
        /* Unimplemented/unknown offsets return 0 */
        val = 0;
        break;
    }

    /* Truncate to requested size */
    if (size == 1) {
        val &= 0xFF;
    } else if (size == 2) {
        val &= 0xFFFF;
    } else if (size == 4) {
        val &= 0xFFFFFFFFU;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* Use only the low byte for 8-bit registers */
    uint8_t v8 = (uint8_t)(val & 0xFF);

    switch (addr) {
    case LOONGSON_SPI_SPCR_REG:
        s->regs.spcr = v8;
        break;
    case LOONGSON_SPI_SPSR_REG:
        /* Status register is typically W1C for some bits, but we have
         * no explicit behavior described, so just store the value.
         */
        s->regs.spsr = v8;
        break;
    case LOONGSON_SPI_SPER_REG:
        s->regs.sper = v8;
        break;
    case LOONGSON_SPI_FIFO_REG:
        /* Dummy FIFO: ignore data */
        break;
    case LOONGSON_SPI_PARA_REG:
        s->regs.para = v8;
        break;
    case LOONGSON_SPI_SFCS_REG:
        s->regs.sfcs = v8;
        break;
    case LOONGSON_SPI_TIMI_REG:
        s->regs.timi = v8;
        break;
    default:
        /* Ignore writes to unknown offsets */
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

    /* Initialize register defaults to safe zeros. No explicit reset values
     * are specified in the provided driver snippet.
     */
    s->regs.spcr = 0x00;
    s->regs.spsr = 0x00;
    s->regs.sper = 0x00;
    s->regs.para = 0x00;
    s->regs.sfcs = 0x00;
    s->regs.timi = 0x00;
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
    s->num_bars = 1;
    for (int i = 0; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = NULL;
    }
    /* BAR0 is used by the driver via pcim_iomap_region(pdev, 0, ...) */
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x100;
    s->bar_info[0].name = "loongson-spi-bar0";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No MSI/MSI-X explicitly referenced in the provided driver snippet */
    /* No DMA configuration referenced in the provided driver snippet */
    /* No timer configuration referenced in the provided driver snippet */
    /* Field/init defaults already zeroed by QEMU object allocation */
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
    .name = "loongson_spi_pci_pci",
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

