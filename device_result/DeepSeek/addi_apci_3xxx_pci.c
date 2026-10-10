/*
 * QEMU model of the ADDI-DATA APCI-3xxx PCI device.
 * Generated for Linux driver addi_apci_3xxx.c
 * Implements MMIO (BAR3) and PIO (BAR2) register interfaces.
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

/* PCI Vendor and Device IDs from driver */
#define PCI_VENDOR_ID_ADDIDATA 0x15B8
#define DEVICE_ID               0x3010
#define CLASS_ID                0x0

#define TYPE_PCIBASE_DEVICE "addi_apci_3xxx_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */

/* BAR types */
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
    
    /* Hardware Register Shadows */
    /* MMIO registers */
    uint32_t chan_config;   /* offset 0x00 */
    uint32_t delay_mode;    /* offset 0x04 */
    uint32_t start_conv;    /* offset 0x08 */
    /* offset 0x0C: FIFO clear (write-only, no value stored) */
    uint32_t irq_status;    /* offset 0x10 */
    uint32_t eoc_status;    /* offset 0x14 */
    uint32_t adc_data;      /* offset 0x1C */
    uint32_t conv_timer;    /* offset 0x20 */
    uint32_t time_base;     /* offset 0x24 */
    uint32_t seq_count;     /* offset 0x30 */
    uint32_t ao_status;     /* offset 0x60 */
    uint32_t ao_data;       /* offset 0x64 */

    /* PIO registers (BAR2) */
    uint32_t di_state;      /* offset 0x20 (32): digital input (4 bits) */
    uint32_t do_state;      /* offset 0x30 (48): digital output (4 bits) */
    uint32_t port1_in;      /* offset 0x40 (64): TTL port 1 input (8 bits) */
    uint32_t port0_out;     /* offset 0x50 (80): TTL port 0 output (8 bits) */
    uint32_t port2_in;      /* offset 0x60 (96): TTL port 2 input (8 bits) */
    uint32_t port2_out;     /* offset 0x70 (112): TTL port 2 output (8 bits) */
    uint32_t port2_dir;     /* offset 0xE0 (224): TTL port 2 direction (8 bits, write-only) */
};

/* Interrupt update helper */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    /* Raise INTx if any interrupt status bit is set */
    pci_set_irq(pdev, (s->irq_status != 0) ? 1 : 0);
}

/* MMIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = 0;

    if (size != 4) {
        return ~0ULL;
    }

    switch (addr) {
    case 0x00:
        val = s->chan_config;
        break;
    case 0x04:
        val = s->delay_mode;
        break;
    case 0x08:
        val = s->start_conv;
        break;
    case 0x10:
        val = s->irq_status;
        break;
    case 0x14:
        val = s->eoc_status;
        /* Read clears EOC bit (bit 0) */
        s->eoc_status &= ~0x1;
        break;
    case 0x1C:
        val = s->adc_data;
        break;
    case 0x20:
        val = s->conv_timer;
        break;
    case 0x24:
        val = s->time_base;
        break;
    case 0x30:
        val = s->seq_count;
        break;
    case 0x60:
        val = s->ao_status;
        break;
    case 0x64:
        val = s->ao_data;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented MMIO read at 0x%"HWADDR_PRIx"\n",
                      __func__, addr);
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        return;
    }

    switch (addr) {
    case 0x00:
        s->chan_config = val;
        break;
    case 0x04:
        s->delay_mode = val;
        break;
    case 0x08:
        s->start_conv = val;
        if (val & 0x80000) { /* single conversion start? */
            /* Instantly set end-of-conversion status */
            s->eoc_status |= 0x1;
            if (val & 0x100000) { /* continuous mode */
                s->irq_status |= 0x2;
                pcibase_update_irq(s);
            }
        } else {
            /* Writing 0 stops conversion */
            s->eoc_status &= ~0x1;
        }
        break;
    case 0x0C:
        /* FIFO clear: accept write but ignore */
        break;
    case 0x10:
        /* Interrupt status: write-1-to-clear */
        s->irq_status &= ~val;
        pcibase_update_irq(s);
        break;
    case 0x14:
        /* EOC status: driver reads it, no write expected */
        break;
    case 0x1C:
        /* ADC data is read-only */
        break;
    case 0x20:
        s->conv_timer = val;
        break;
    case 0x24:
        s->time_base = val;
        break;
    case 0x30:
        s->seq_count = val;
        break;
    case 0x60:
        s->ao_status = val;   /* range selection and status */
        /* Clear EOC bit? Possibly, writing resets it. */
        s->ao_status &= ~0x100;
        break;
    case 0x64:
        s->ao_data = val;
        /* Immediately signal AO transfer complete */
        s->ao_status |= 0x100;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented MMIO write at 0x%"HWADDR_PRIx"\n",
                      __func__, addr);
        break;
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

/* PIO Handlers for BAR2 */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = 0;

    if (size != 4) {
        return ~0ULL;
    }

    switch (addr) {
    case 0x20:  /* digital input */
        val = s->di_state & 0xf;
        break;
    case 0x30:  /* digital output */
        val = s->do_state & 0xf;
        break;
    case 0x40:  /* TTL port 1 input */
        val = s->port1_in;
        break;
    case 0x50:  /* TTL port 0 output */
        val = s->port0_out;
        break;
    case 0x60:  /* TTL port 2 input */
        val = s->port2_in;
        break;
    case 0x70:  /* TTL port 2 output */
        val = s->port2_out;
        break;
    case 0xE0:  /* TTL port 2 direction (write-only, return 0) */
        val = 0;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented PIO read at 0x%"HWADDR_PRIx"\n",
                      __func__, addr);
        break;
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        return;
    }

    switch (addr) {
    case 0x30:
        s->do_state = val & 0xf;
        break;
    case 0x50:
        s->port0_out = val & 0xff;
        break;
    case 0x70:
        s->port2_out = val & 0xff;
        break;
    case 0xE0:
        s->port2_dir = val & 0xff;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented PIO write at 0x%"HWADDR_PRIx"\n",
                      __func__, addr);
        break;
    }
}

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Reset all shadow registers to default state */
    s->chan_config = 0;
    s->delay_mode = 0;
    s->start_conv = 0;
    s->irq_status = 0;
    s->eoc_status = 0;
    s->adc_data = 0;
    s->conv_timer = 0;
    s->time_base = 0;
    s->seq_count = 0;
    s->ao_status = 0x100;  /* AO ready after reset */
    s->ao_data = 0;
    s->di_state = 0;
    s->do_state = 0;
    s->port1_in = 0;
    s->port0_out = 0;
    s->port2_in = 0;
    s->port2_out = 0;
    s->port2_dir = 0;

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
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_ADDIDATA);
    pci_set_word(pci_conf + PCI_DEVICE_ID, DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 6; /* Supports up to 6 BARs, we only use 2 and 3 */
    for (int i = 0; i < s->num_bars; i++) {
        s->bar_info[i].type = BAR_TYPE_NONE;
    }

    /* BAR2: I/O for digital I/O and TTL */
    s->bar_info[2].index = 2;
    s->bar_info[2].type = BAR_TYPE_PIO;
    s->bar_info[2].size = 256;
    s->bar_info[2].name = "addi_apci_3xxx_pio";

    /* BAR3: MMIO for analog and configuration */
    s->bar_info[3].index = 3;
    s->bar_info[3].type = BAR_TYPE_MMIO;
    s->bar_info[3].size = 256;
    s->bar_info[3].name = "addi_apci_3xxx_mmio";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No MSI/MSI-X used by driver; rely on INTx */
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    /* No MSI/MSI-X cleanup necessary */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "addi_apci_3xxx_pci",
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
