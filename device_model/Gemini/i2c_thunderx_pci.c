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


#define TYPE_PCIBASE_DEVICE "i2c_thunderx_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_CAVIUM 0x177d
#define PCI_CLASS_SERIAL_SMBUS 0x0c05
#define PCI_DEVICE_ID_THUNDER_TWSI 0xa012
#define TWSI_INT_ENA_W1C 0x1028
#define TWSI_INT_ENA_W1S 0x1030
#define TWSI_INT_CORE_INT BIT_ULL(2)
#define TWSI_INT_ST_INT BIT_ULL(0)
#define TWSI_INT_TS_INT BIT_ULL(1)
#define STAT_IDLE 0xF8
#define TWSI_CTL_ENAB 0x40
#define TWSI_CTL_CE 0x80
#define TWSI_CTL_IFLG 0x08
#define TWSI_CTL_STP 0x10
#define TWSI_CTL_AAK 0x04
#define TWSI_CTL_STA 0x20
#define SW_TWSI_V BIT_ULL(63)
#define SW_TWSI_R BIT_ULL(56)
#define SW_TWSI_SOVR BIT_ULL(55)
#define SW_TWSI_EIA BIT_ULL(61)
#define TWSX_MODE_REFCLK_SRC BIT(4)
#define TWSX_MODE_HS_MODE BIT(0)
#define TWSX_MODE_BLOCK_MODE BIT(2)
#define TWSX_BLOCK_STS_RESET_PTR BIT(0)
#define BUS_MON_RST_MASK BIT(3)

#define ROFF_SW_TWSI 0x1000
#define ROFF_TWSI_INT 0x1010
#define ROFF_SW_TWSI_EXT 0x1018
#define ROFF_MODE 0x1038
#define ROFF_BLOCK_CTL 0x1048
#define ROFF_BLOCK_STS 0x1050
#define ROFF_BLOCK_FIFO 0x1058

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
    uint64_t twsi_int;
    uint64_t twsi_int_ena;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint64_t sw_twsi;
    uint64_t sw_twsi_ext;
    uint64_t mode;
    uint64_t block_ctl;
    uint64_t block_sts;
    uint64_t block_fifo;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool level = (s->twsi_int & s->twsi_int_ena) != 0;

    if (msix_enabled(pdev)) {
        if (level) {
            msix_notify(pdev, 0);
        }
    } else if (msi_enabled(pdev)) {
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
    case ROFF_SW_TWSI:
        val = s->sw_twsi;
        break;
    case ROFF_TWSI_INT:
        val = s->twsi_int;
        break;
    case ROFF_SW_TWSI_EXT:
        val = s->sw_twsi_ext;
        break;
    case TWSI_INT_ENA_W1C:
    case TWSI_INT_ENA_W1S:
        val = s->twsi_int_ena;
        break;
    case ROFF_MODE:
        val = s->mode;
        break;
    case ROFF_BLOCK_CTL:
        val = s->block_ctl;
        break;
    case ROFF_BLOCK_STS:
        val = s->block_sts;
        break;
    case ROFF_BLOCK_FIFO:
        val = s->block_fifo;
        break;
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
    case ROFF_SW_TWSI:
        /* Hardware clears SW_TWSI_V when done processing the command.
         * In HLC mode, the lower 8 bits contain the status. Set to STAT_IDLE to simulate success.
         */
        s->sw_twsi = (val & ~SW_TWSI_V & ~0xFFULL) | STAT_IDLE;
        break;
    case ROFF_TWSI_INT:
        s->twsi_int &= ~val; /* Assuming W1C */
        pcibase_update_irq(s);
        break;
    case ROFF_SW_TWSI_EXT:
        s->sw_twsi_ext = val;
        break;
    case TWSI_INT_ENA_W1C:
        s->twsi_int_ena &= ~val;
        pcibase_update_irq(s);
        break;
    case TWSI_INT_ENA_W1S:
        s->twsi_int_ena |= val;
        pcibase_update_irq(s);
        break;
    case ROFF_MODE:
        s->mode = val;
        break;
    case ROFF_BLOCK_CTL:
        s->block_ctl = val;
        break;
    case ROFF_BLOCK_STS:
        /* TWSX_BLOCK_STS_RESET_PTR is a self-clearing reset command */
        s->block_sts = val & ~TWSX_BLOCK_STS_RESET_PTR;
        break;
    case ROFF_BLOCK_FIFO:
        s->block_fifo = val;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad write at offset 0x%" HWADDR_PRIx "\n", __func__, addr);
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

    s->sw_twsi = STAT_IDLE;
    s->sw_twsi_ext = 0;
    s->twsi_int = 0;
    s->twsi_int_ena = 0;
    s->mode = 0;
    s->block_ctl = 0;
    s->block_sts = 0;
    s->block_fifo = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_CAVIUM );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_THUNDER_TWSI );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_SERIAL_SMBUS );
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
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = 0x2000, .name = "twsi_base" };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    msix_init(pdev, 1, &s->bar_regions[0], 0, 0x1800, &s->bar_regions[0], 0, 0x1810, 0, errp);
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
    .name = "i2c_thunderx_pci",
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
