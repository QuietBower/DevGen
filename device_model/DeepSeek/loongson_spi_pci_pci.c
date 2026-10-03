/*
 * QEMU PCI device model for Loongson SPI controller (loongson-spi-pci)
 * Based on driver: drivers/spi/spi-loongson-pci.c
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

#define TYPE_PCIBASE_DEVICE "loongson_spi_pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_LOONGSON        0x0014
#define SPI_DEVICE_ID                 0x7a0b
#define PCI_CLASS_SPI                 0x0c80

#define LOONGSON_SPI_SPCR_REG         0x00
#define LOONGSON_SPI_SPSR_REG         0x01
#define LOONGSON_SPI_FIFO_REG         0x02
#define LOONGSON_SPI_SPER_REG         0x03
#define LOONGSON_SPI_PARA_REG         0x04
#define LOONGSON_SPI_SFCS_REG         0x05
#define LOONGSON_SPI_TIMI_REG         0x06

#define LOONGSON_SPI_SPSR_SPIF         BIT(7)
#define LOONGSON_SPI_SPCR_SPE          BIT(6)
#define LOONGSON_SPI_SPSR_WCOL         BIT(6)
#define LOONGSON_SPI_SPCR_CPHA         BIT(2)
#define LOONGSON_SPI_SPCR_CPOL         BIT(3)
#define LOONGSON_SPI_PARA_MEM_EN       BIT(0)
#define LOONGSON_SPI_SPSR_RFEMPTY      BIT(0)

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
    uint8_t spcr;   /* 0x00 */
    uint8_t spsr;   /* 0x01 */
    uint8_t fifo;   /* 0x02 */
    uint8_t sper;   /* 0x03 */
    uint8_t para;   /* 0x04 */
    uint8_t sfcs;   /* 0x05 */
    uint8_t timi;   /* 0x06 */

    /* Power management state (D0-D3) */
    uint8_t power_state;
};

/* Helper to read a single byte from the BAR region at the given offset */
static uint8_t pcibase_reg_read_byte(PCIBaseState *s, hwaddr addr)
{
    switch (addr) {
    case LOONGSON_SPI_SPCR_REG: return s->spcr;
    case LOONGSON_SPI_SPSR_REG: return s->spsr;
    case LOONGSON_SPI_FIFO_REG: return s->fifo;
    case LOONGSON_SPI_SPER_REG: return s->sper;
    case LOONGSON_SPI_PARA_REG: return s->para;
    case LOONGSON_SPI_SFCS_REG: return s->sfcs;
    case LOONGSON_SPI_TIMI_REG: return s->timi;
    default:
        /* Outside known register space, return 0 */
        return 0;
    }
}

/* Helper to write a single byte to the BAR region at the given offset */
static void pcibase_reg_write_byte(PCIBaseState *s, hwaddr addr, uint8_t val)
{
    switch (addr) {
    case LOONGSON_SPI_SPCR_REG: s->spcr = val; break;
    case LOONGSON_SPI_SPSR_REG: s->spsr = val; break;
    case LOONGSON_SPI_FIFO_REG: s->fifo = val; break;
    case LOONGSON_SPI_SPER_REG: s->sper = val; break;
    case LOONGSON_SPI_PARA_REG: s->para = val; break;
    case LOONGSON_SPI_SFCS_REG: s->sfcs = val; break;
    case LOONGSON_SPI_TIMI_REG: s->timi = val; break;
    default:
        /* Silently ignore writes to unimplemented registers */
        break;
    }
}

/* MMIO Read Handler */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Compose the value from successive bytes, little-endian */
    for (unsigned i = 0; i < size; i++) {
        val |= (uint64_t)pcibase_reg_read_byte(s, addr + i) << (i * 8);
    }
    return val;
}

/* MMIO Write Handler */
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* Write the value byte-by-byte, little-endian */
    for (unsigned i = 0; i < size; i++) {
        pcibase_reg_write_byte(s, addr + i, (uint8_t)(val >> (i * 8)));
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

    /* Revert registers to power-on defaults (all zero) */
    s->spcr = 0;
    s->spsr = 0;
    s->fifo = 0;
    s->sper = 0;
    s->para = 0;
    s->sfcs = 0;
    s->timi = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x0014 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x7a0b );
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
    s->bar_info[0].size = 0x100;  /* Decent size for a SPI controller register bank */
    s->bar_info[0].name = "bar0";
    for (int i = 0; i < s->num_bars; i++) {
        if (s->bar_info[i].size == 0) {
            continue; /* skip until size defined */
        }
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Final state initialization before the device is 'live' */
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

    /* Free buffers, stop timers, etc. */
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
