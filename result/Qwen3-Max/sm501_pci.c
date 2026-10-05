/*
 * QEMU SM501 PCI Device Emulation
 *
 * Copyright (c) 2025 QEMU contributors
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 */

#include "qemu/osdep.h"
#include <inttypes.h>
#include <string.h>
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "sysemu/dma.h"
#include "sysemu/reset.h"
#include "hw/irq.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/pci/pcie.h"
#include "qom/object.h"
#include "qapi/visitor.h"
#include "hw/qdev-properties.h"
#include "qemu/units.h"

#define TYPE_PCIBASE_DEVICE "sm501_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register offsets and constants from driver */
#define VENDOR_ID 0x126f
#define DEVICE_ID 0x0501
#define CLASS_ID  PCI_CLASS_DISPLAY_OTHER

#define SM501_MISC_PNL_24BIT		(1<<25)

#define SM501_DEVICEID			0x00
#define SM501_DEVICEID_SM501		0x0501
#define SM501_DEVICEID_IDMASK		0xFFFF
#define SM501_DEVICEID_REVMASK		0xFF000000

#define SM501_DRAM_CONTROL		0x04
#define SM501_MISC_CONTROL		0x10
#define SM501_GPIO31_0_CONTROL		0x2C
#define SM501_GPIO63_32_CONTROL		0x30
#define SM501_MISC_TIMING		0x1C
#define SM501_IRQ_MASK			0x44

#define SM501_POWER_MODE_CONTROL	0x40
#define SM501_CURRENT_GATE		0x48
#define SM501_CURRENT_CLOCK		0x4C
#define SM501_POWER_MODE_0_GATE		0x50
#define SM501_POWER_MODE_0_CLOCK	0x54
#define SM501_POWER_MODE_1_GATE		0x58
#define SM501_POWER_MODE_1_CLOCK	0x5C
#define SM501_PROGRAMMABLE_PLL_CONTROL	0x64

#define SM501_GPIO_DATA_LOW		0x00
#define SM501_GPIO_DDR_LOW		0x04
#define SM501_GPIO_DATA_HIGH		0x08
#define SM501_GPIO_DDR_HIGH		0x0C

#define SM501_GPIO			0x80000

#define SM501_POWERMODE_M_SRC		(1 << 16)
#define SM501_POWERMODE_M1_SRC		(1 << 20)

struct sm501_reg_init {
    unsigned long		set;
    unsigned long		mask;
};

/* ------------------------------------------------------------------ */
/* BAR metadata definition                                             */
/* ------------------------------------------------------------------ */
typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM 
} BARType;

typedef struct {
    int index;
    BARType type;
    hwaddr size;
    const char *name;
    bool sparse;
} BARInfo;

/* ------------------------------------------------------------------ */
/* Device State                                                        */
/* ------------------------------------------------------------------ */
struct PCIBaseState {
    PCIDevice parent_obj;

    /* BAR memory regions */
    MemoryRegion bar_regions[6];

    /* optional linear backing */
    uint8_t *mmio_backing;
    size_t mmio_backing_size;

    /* BAR table */
    BARInfo bar_info[6];
    int num_bars;

    /* interrupt state feature flags */
    bool has_msi;
    bool has_msix;
    
    /* Register shadows */
    uint32_t device_id;
    uint32_t dram_control;
    uint32_t misc_control;
    uint32_t gpio31_0_control;
    uint32_t gpio63_32_control;
    uint32_t misc_timing;
    uint32_t irq_mask;
    
    uint32_t power_mode_control;
    uint32_t current_gate;
    uint32_t current_clock;
    uint32_t power_mode_0_gate;
    uint32_t power_mode_0_clock;
    uint32_t power_mode_1_gate;
    uint32_t power_mode_1_clock;
    uint32_t programmable_pll_control;
    
    uint32_t gpio_data_low;
    uint32_t gpio_ddr_low;
    uint32_t gpio_data_high;
    uint32_t gpio_ddr_high;
    
    /* Status fields */
    
    /* reset/probe state */
    
    /* power mgmt */
    
    /* other fields */
};


/* ------------------------------------------------------------------ */
/* Forward declarations                                                */
/* ------------------------------------------------------------------ */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len);
static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len);
static void pcibase_reset(DeviceState *dev);
static void pcibase_realize(PCIDevice *pdev, Error **errp);
static void pcibase_uninit(PCIDevice *pdev);

/* ------------------------------------------------------------------ */
/* MemoryRegionOps                                                      */
/* ------------------------------------------------------------------ */
static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* ------------------------------------------------------------------ */
/* Helper: register a BAR (MMIO or PIO)                                 */
/* ------------------------------------------------------------------ */
static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE)
        return;

    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    }else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, bi->size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }

}

/* ------------------------------------------------------------------ */
/* MMIO / PIO handlers (device-specific code goes into placeholders)   */
/* ------------------------------------------------------------------ */

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case SM501_DEVICEID:
        val = s->device_id;
        break;
    case SM501_DRAM_CONTROL:
        val = s->dram_control;
        break;
    case SM501_MISC_CONTROL:
        val = s->misc_control;
        break;
    case SM501_GPIO31_0_CONTROL:
        val = s->gpio31_0_control;
        break;
    case SM501_GPIO63_32_CONTROL:
        val = s->gpio63_32_control;
        break;
    case SM501_MISC_TIMING:
        val = s->misc_timing;
        break;
    case SM501_IRQ_MASK:
        val = s->irq_mask;
        break;
        
    case SM501_POWER_MODE_CONTROL:
        val = s->power_mode_control;
        break;
    case SM501_CURRENT_GATE:
        val = s->current_gate;
        break;
    case SM501_CURRENT_CLOCK:
        val = s->current_clock;
        break;
    case SM501_POWER_MODE_0_GATE:
        val = s->power_mode_0_gate;
        break;
    case SM501_POWER_MODE_0_CLOCK:
        val = s->power_mode_0_clock;
        break;
    case SM501_POWER_MODE_1_GATE:
        val = s->power_mode_1_gate;
        break;
    case SM501_POWER_MODE_1_CLOCK:
        val = s->power_mode_1_clock;
        break;
    case SM501_PROGRAMMABLE_PLL_CONTROL:
        val = s->programmable_pll_control;
        break;
        
    case SM501_GPIO:
        val = s->gpio_data_low;
        break;
    case SM501_GPIO + 0x04:
        val = s->gpio_ddr_low;
        break;
    case SM501_GPIO + 0x08:
        val = s->gpio_data_high;
        break;
    case SM501_GPIO + 0x0C:
        val = s->gpio_ddr_high;
        break;
        
    default:
        qemu_log_mask(LOG_UNIMP, "[sm501_pci] unhandled mmio_read addr=%" PRIx64 " size=%u\n", (uint64_t)addr, size);
        break;
    }

    if (size == 1) {
        val = (val >> ((addr & 3) * 8)) & 0xFF;
    } else if (size == 2) {
        val = (val >> ((addr & 2) * 8)) & 0xFFFF;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t full_val = val;

    if (size == 1) {
        uint32_t shift = (addr & 3) * 8;
        uint32_t mask = ~(0xFF << shift);
        full_val = (s->misc_control & mask) | ((val & 0xFF) << shift);
    } else if (size == 2) {
        uint32_t shift = (addr & 2) * 8;
        uint32_t mask = ~(0xFFFF << shift);
        full_val = (s->misc_control & mask) | ((val & 0xFFFF) << shift);
    }

    switch (addr) {
    case SM501_DEVICEID:
        /* Read-only */
        break;
    case SM501_DRAM_CONTROL:
        s->dram_control = full_val;
        break;
    case SM501_MISC_CONTROL:
        s->misc_control = full_val;
        break;
    case SM501_GPIO31_0_CONTROL:
        s->gpio31_0_control = full_val;
        break;
    case SM501_GPIO63_32_CONTROL:
        s->gpio63_32_control = full_val;
        break;
    case SM501_MISC_TIMING:
        s->misc_timing = full_val;
        break;
    case SM501_IRQ_MASK:
        s->irq_mask = full_val;
        break;
        
    case SM501_POWER_MODE_CONTROL:
        s->power_mode_control = full_val;
        break;
    case SM501_CURRENT_GATE:
        s->current_gate = full_val;
        break;
    case SM501_CURRENT_CLOCK:
        s->current_clock = full_val;
        break;
    case SM501_POWER_MODE_0_GATE:
        s->power_mode_0_gate = full_val;
        break;
    case SM501_POWER_MODE_0_CLOCK:
        s->power_mode_0_clock = full_val;
        break;
    case SM501_POWER_MODE_1_GATE:
        s->power_mode_1_gate = full_val;
        break;
    case SM501_POWER_MODE_1_CLOCK:
        s->power_mode_1_clock = full_val;
        break;
    case SM501_PROGRAMMABLE_PLL_CONTROL:
        s->programmable_pll_control = full_val;
        break;
        
    case SM501_GPIO:
        s->gpio_data_low = full_val;
        break;
    case SM501_GPIO + 0x04:
        s->gpio_ddr_low = full_val;
        break;
    case SM501_GPIO + 0x08:
        s->gpio_data_high = full_val;
        break;
    case SM501_GPIO + 0x0C:
        s->gpio_ddr_high = full_val;
        break;
        
    default:
        qemu_log_mask(LOG_UNIMP, "[sm501_pci] unhandled mmio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n", (uint64_t)addr, (uint64_t)val, size);
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "[sm501_pci] pio_read addr=%" PRIx64 " size=%u\n", (uint64_t)addr, size);
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "[sm501_pci] pio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n", (uint64_t)addr, (uint64_t)val, size);
}

/* ------------------------------------------------------------------ */
/* Reset       */
/* ------------------------------------------------------------------ */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    /* core resets */
    pci_device_reset(pdev);

    /* Initialize registers to default values */
    s->device_id = (0x01 << 24) | SM501_DEVICEID_SM501; /* rev 0x01 */
    s->dram_control = 0x00000000;
    s->misc_control = 0x00000000;
    s->gpio31_0_control = 0x00000000;
    s->gpio63_32_control = 0x00000000;
    s->misc_timing = 0x00000000;
    s->irq_mask = 0x00000000;
    
    s->power_mode_control = 0x00000000;
    s->current_gate = 0x00000000;
    s->current_clock = 0x00000000;
    s->power_mode_0_gate = 0x00000000;
    s->power_mode_0_clock = 0x00000000;
    s->power_mode_1_gate = 0x00000000;
    s->power_mode_1_clock = 0x00000000;
    s->programmable_pll_control = 0x00000000;
    
    s->gpio_data_low = 0x00000000;
    s->gpio_ddr_low = 0x00000000;
    s->gpio_data_high = 0x00000000;
    s->gpio_ddr_high = 0x00000000;
}

/* ------------------------------------------------------------------ */
/* DMA initialize (called from realize)                                */
/* ------------------------------------------------------------------ */
static void pcibase_dma_device_realize(PCIDevice *pdev, Error **errp)
{
    (void)errp;
}

/* ------------------------------------------------------------------ */
/* PCI config space access (class-level hooks)                         */
/* ------------------------------------------------------------------ */
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    uint32_t val = pci_default_read_config(pdev, addr, len);
    /* optional: override certain config offsets if needed */

    switch (len) {
        case 1: val &= 0xFF; break;
        case 2: val &= 0xFFFF; break;
        case 4: break;
        default: break;
    }
    return val;
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    if (addr >= PCI_BASE_ADDRESS_0 && addr <= PCI_BASE_ADDRESS_5) {
        pci_default_write_config(pdev, addr, val, len);
        return;
    }
}

/* ------------------------------------------------------------------ */
/* Realize (device init)                                                */
/* ------------------------------------------------------------------ */
static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);
    
    /* Set up BARs based on driver usage */
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_RAM;
    s->bar_info[0].size = 64 * MiB; /* From sm501_mem_local table, max 64MB */
    s->bar_info[0].name = "sm501-ram";
    
    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = 0x100000; /* Driver accesses up to 0x100000 */
    s->bar_info[1].name = "sm501-mmio";
    
    s->num_bars = 2;
    
    /* register BARs */
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Interrupt config - only legacy INTx used by driver */
    s->has_msi = false;
    s->has_msix = false;

    /* DMA init (called here so device can set DMA masks/etc) */
    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) return;

    /* Initialize device state */
    pcibase_reset(DEVICE(pdev));
    
    /* Apply initial configuration from sm501_pci_initdata */
    s->misc_control |= SM501_MISC_PNL_24BIT;
    s->misc_timing |= 0x010100;
    s->misc_timing &= ~0x1F1F00;
    s->gpio63_32_control |= 0x3F000000;
    
    qemu_log_mask(LOG_GUEST_ERROR, "[sm501_pci] device realized\n");
}

/* ------------------------------------------------------------------ */
/* Uninit/cleanup                                                      */
/* ------------------------------------------------------------------ */
static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (s->has_msix) {
        msix_uninit(pdev,NULL,0);
        s->has_msix = false;
    }
    if (s->has_msi) {
        msi_uninit(pdev);
        s->has_msi = false;
    }

    /* free mmio_backing or other allocations */

    if (s->mmio_backing) {
        g_free(s->mmio_backing);
        s->mmio_backing = NULL;
    }

    qemu_log_mask(LOG_GUEST_ERROR, "[sm501_pci] device uninit\n");
}

/* ------------------------------------------------------------------ */
/* Class init / type registration                                      */
/* ------------------------------------------------------------------ */
static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->config_read  = pcibase_config_read;
    k->config_write = pcibase_config_write;

    k->realize = pcibase_realize;
    k->exit    = pcibase_uninit;
    dc->reset  = pcibase_reset;

    set_bit(DEVICE_CATEGORY_DISPLAY, dc->categories);
}

static void pcibase_register_types(void)
{
    static InterfaceInfo interfaces[] = {
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
