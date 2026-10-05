/*
 * Generic QEMU PCI device template (QEMU 8.2.x)
 *
 * Phase 1: fill register macros, BAR list, BAR sizes, structs, enums.
 * Phase 2: implement MMIO/PIO read/write, DMA init, IRQ init, reset, config hooks
 * Phase 3 (Repair Phase)：correct syntax, types, includes, missing symbols, remove dead code
 * Phase 4 (Debug & Update Phase)：Based on actual kernel logs from QEMU boot, repair the virtual hardware behavior
 *
 * Replace #PLACEHOLDER# blocks with device-specific code/data.
 * NOTE:
 * - Only essential fixes added (TYPE macro, PIO BAR handling, MMIO impl, DMA init, IRQ).
 * - No optional or advanced features added.
 * - Do not directly fill this file with the Linux kernel source code
 */

#include "qemu/osdep.h"
#include <inttypes.h>
#include <string.h>
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "qemu/units.h"
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

/* ------------------------------------------------------------------ */
/* Additional include files (Stage1 optional)                          */
/* ------------------------------------------------------------------ */


#define TYPE_PCIBASE_DEVICE "sm501_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Related register/macros/constants from driver (Stage1) */
#define SM501_VENDOR_ID 0x126f
#define SM501_DEVICE_ID 0x0501
#define MHZ (1000 * 1000)

#define SM501_MISC_PNL_24BIT        (1<<25)
#define SM501FB_FLAG_USE_INIT_MODE  (1<<0)
#define SM501_USE_ALL               (0xffffffff)

/* Missing definitions required for compilation */
#define SM501_SYSTEM_CONTROL        0x000000
#define SM501_MISC_CONTROL          0x000004
#define SM501_GPIO31_0_CONTROL      0x000008
#define SM501_GPIO63_32_CONTROL     0x00000C
#define SM501_DRAM_CONTROL          0x000010
#define SM501_ARB_CONTROL           0x000014
#define SM501_IRQ_MASK              0x000030
#define SM501_MISC_TIMING           0x000068
#define SM501_CURRENT_GATE          0x000038
#define SM501_CURRENT_CLOCK         0x00003C
#define SM501_POWER_MODE_0_GATE     0x000040
#define SM501_POWER_MODE_0_CLOCK    0x000044
#define SM501_POWER_MODE_1_GATE     0x000048
#define SM501_POWER_MODE_1_CLOCK    0x00004C
#define SM501_POWER_MODE_CONTROL    0x000054
#define SM501_DEVICEID              0x000060
#define SM501_PROGRAMMABLE_PLL_CONTROL 0x000074

#define SM501_GPIO                  0x010000
#define SM501_GPIO_DATA_LOW         0x00
#define SM501_GPIO_DATA_HIGH        0x04
#define SM501_GPIO_DDR_LOW          0x08
#define SM501_GPIO_DDR_HIGH         0x0C

#define SM501_DEVICEID_SM501        0x05010000
#define SM501_DEVICEID_IDMASK       0xFFFF0000
#define SM501_DEVICEID_REVMASK      0x000000FF

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

    /* BAR memory regions (MMIO/PIO unified handling fix) */
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
    
    
    /* DMA info placeholder */
    

    /* Register shadows */
    uint32_t system_control;
    uint32_t misc_control;
    uint32_t gpio_31_0_control;
    uint32_t gpio_63_32_control;
    uint32_t dram_control;
    uint32_t arb_control;
    uint32_t irq_mask;
    uint32_t misc_timing;
    uint32_t current_gate;
    uint32_t current_clock;
    uint32_t power_mode_0_gate;
    uint32_t power_mode_0_clock;
    uint32_t power_mode_1_gate;
    uint32_t power_mode_1_clock;
    uint32_t power_mode_control;
    uint32_t programmable_pll_control;

    /* GPIO registers */
    uint32_t gpio_data_low;
    uint32_t gpio_data_high;
    uint32_t gpio_ddr_low;
    uint32_t gpio_ddr_high;

    /* Status fields */
    

    /* reset/probe state */
    unsigned int rev;

    /* power mgmt */
    int unit_power[20];

    /* other fields */
    
};

struct sm501_clock {
    unsigned long mclk;
    int divider;
    int shift;
    unsigned int m, n, k;
};

struct sm501_reg_init {
    unsigned long       set;
    unsigned long       mask;
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

    /* Main Register Block */
    switch (addr) {
    case SM501_SYSTEM_CONTROL:
        return s->system_control;
    case SM501_MISC_CONTROL:
        return s->misc_control;
    case SM501_GPIO31_0_CONTROL:
        return s->gpio_31_0_control;
    case SM501_GPIO63_32_CONTROL:
        return s->gpio_63_32_control;
    case SM501_DRAM_CONTROL:
        return s->dram_control;
    case SM501_ARB_CONTROL:
        return s->arb_control;
    case SM501_IRQ_MASK:
        return s->irq_mask;
    case SM501_MISC_TIMING:
        return s->misc_timing;
    case SM501_CURRENT_GATE:
        return s->current_gate;
    case SM501_CURRENT_CLOCK:
        return s->current_clock;
    case SM501_POWER_MODE_0_GATE:
        return s->power_mode_0_gate;
    case SM501_POWER_MODE_0_CLOCK:
        return s->power_mode_0_clock;
    case SM501_POWER_MODE_1_GATE:
        return s->power_mode_1_gate;
    case SM501_POWER_MODE_1_CLOCK:
        return s->power_mode_1_clock;
    case SM501_POWER_MODE_CONTROL:
        return s->power_mode_control;
    case SM501_DEVICEID:
        return SM501_DEVICEID_SM501 | 0xA0; /* SM501 Rev A0 */
    case SM501_PROGRAMMABLE_PLL_CONTROL:
        return s->programmable_pll_control;
    }

    /* GPIO Block */
    if (addr >= SM501_GPIO && addr < SM501_GPIO + 0x20) {
        hwaddr gpio_offset = addr - SM501_GPIO;
        switch (gpio_offset) {
        case SM501_GPIO_DATA_LOW:
            return s->gpio_data_low;
        case SM501_GPIO_DATA_HIGH:
            return s->gpio_data_high;
        case SM501_GPIO_DDR_LOW:
            return s->gpio_ddr_low;
        case SM501_GPIO_DDR_HIGH:
            return s->gpio_ddr_high;
        default:
            return 0;
        }
    }

    /* Sub-devices (UART, USB, Display) - just return 0 for now */
    if ((addr >= 0x30000 && addr < 0x30040) || /* UARTs */
        (addr >= 0x40000 && addr < 0x60000) || /* USB */
        (addr >= 0x80000 && addr < 0x150000)) { /* Display */
        return 0;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] mmio_read addr=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* Main Register Block */
    switch (addr) {
    case SM501_SYSTEM_CONTROL:
        s->system_control = val;
        return;
    case SM501_MISC_CONTROL:
        s->misc_control = val;
        return;
    case SM501_GPIO31_0_CONTROL:
        s->gpio_31_0_control = val;
        return;
    case SM501_GPIO63_32_CONTROL:
        s->gpio_63_32_control = val;
        return;
    case SM501_DRAM_CONTROL:
        /* Read-only in this model, or writable? Driver reads it. */
        /* s->dram_control = val; */
        return;
    case SM501_ARB_CONTROL:
        s->arb_control = val;
        return;
    case SM501_IRQ_MASK:
        s->irq_mask = val;
        /* Update IRQ status if needed */
        return;
    case SM501_MISC_TIMING:
        s->misc_timing = val;
        return;
    case SM501_CURRENT_GATE:
        s->current_gate = val;
        return;
    case SM501_CURRENT_CLOCK:
        s->current_clock = val;
        return;
    case SM501_POWER_MODE_0_GATE:
        s->power_mode_0_gate = val;
        return;
    case SM501_POWER_MODE_0_CLOCK:
        s->power_mode_0_clock = val;
        return;
    case SM501_POWER_MODE_1_GATE:
        s->power_mode_1_gate = val;
        return;
    case SM501_POWER_MODE_1_CLOCK:
        s->power_mode_1_clock = val;
        return;
    case SM501_POWER_MODE_CONTROL:
        s->power_mode_control = val;
        return;
    case SM501_PROGRAMMABLE_PLL_CONTROL:
        s->programmable_pll_control = val;
        return;
    }

    /* GPIO Block */
    if (addr >= SM501_GPIO && addr < SM501_GPIO + 0x20) {
        hwaddr gpio_offset = addr - SM501_GPIO;
        switch (gpio_offset) {
        case SM501_GPIO_DATA_LOW:
            s->gpio_data_low = val;
            return;
        case SM501_GPIO_DATA_HIGH:
            s->gpio_data_high = val;
            return;
        case SM501_GPIO_DDR_LOW:
            s->gpio_ddr_low = val;
            return;
        case SM501_GPIO_DDR_HIGH:
            s->gpio_ddr_high = val;
            return;
        default:
            return;
        }
    }

    /* Sub-devices (UART, USB, Display) - ignore writes */
    if ((addr >= 0x30000 && addr < 0x30040) || /* UARTs */
        (addr >= 0x40000 && addr < 0x60000) || /* USB */
        (addr >= 0x80000 && addr < 0x150000)) { /* Display */
        return;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] mmio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, (uint64_t)val, size);
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;

    

    qemu_log_mask(LOG_UNIMP, "[%s] pio_read addr=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;

    

    qemu_log_mask(LOG_UNIMP, "[%s] pio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, (uint64_t)val, size);
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

    /* Reset registers to default values */
    s->system_control = 0;
    s->misc_control = 0;
    s->gpio_31_0_control = 0;
    s->gpio_63_32_control = 0;
    
    /* DRAM Control: Set to 64MB (Index 4 in sm501_mem_local) */
    /* Index 4 is 0x4. Shifted by 13 is 0x8000. */
    s->dram_control = 0x00008000;
    
    s->arb_control = 0;
    s->irq_mask = 0;
    s->misc_timing = 0;
    s->current_gate = 0;
    s->current_clock = 0;
    s->power_mode_0_gate = 0;
    s->power_mode_0_clock = 0;
    s->power_mode_1_gate = 0;
    s->power_mode_1_clock = 0;
    s->power_mode_control = 0;
    s->programmable_pll_control = 0;

    s->gpio_data_low = 0;
    s->gpio_data_high = 0;
    s->gpio_ddr_low = 0;
    s->gpio_ddr_high = 0;

    memset(s->unit_power, 0, sizeof(s->unit_power));

    if (s->mmio_backing && s->mmio_backing_size)
        memset(s->mmio_backing, 0, s->mmio_backing_size);
}

/* ------------------------------------------------------------------ */
/* DMA initialize (called from realize)                                */
/* ------------------------------------------------------------------ */
static void pcibase_dma_device_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    
    (void)s;
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  SM501_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  SM501_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_OTHERS );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);
    
    
    /* BAR0: Framebuffer (RAM) - Default to 64MB */
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_RAM;
    s->bar_info[0].size = 64 * MiB;
    s->bar_info[0].name = "sm501-fb";

    /* BAR1: Registers (MMIO) - 2MB to cover all sub-devices */
    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = 2 * MiB;
    s->bar_info[1].name = "sm501-mmio";

    s->num_bars = 2;

    /* register BARs */
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Interrupt config (placeholders) */
    

    /* DMA init (called here so device can set DMA masks/etc) */
    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) return;

    

    /* Example MSI:
     *   if (msi_init(pdev, 0, 1, true, false, errp) == 0) { s->has_msi = true; }
     * Example MSIX:
     *   if (msix_init(pdev, nvec, table_mem, table_bar_nr) == 0) { s->has_msix = true; }
     */

    /* optional: map legacy INTx behavior if driver expects it */
    

    

    /* Power management / other init */
    
    /* Example:
    *   pdev->pm_cap = true;
    */
    qemu_log_mask(LOG_UNIMP, "[%s] device realized\n", TYPE_PCIBASE_DEVICE);
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

    qemu_log_mask(LOG_UNIMP, "[%s] device uninit\n", TYPE_PCIBASE_DEVICE);
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

    set_bit(DEVICE_CATEGORY_MISC, dc->categories);

    /* optional */
    
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