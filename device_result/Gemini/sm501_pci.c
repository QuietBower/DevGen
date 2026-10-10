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

#define TYPE_PCIBASE_DEVICE "sm501_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define SM501_PCI_VENDOR_ID 0x126f
#define SM501_PCI_DEVICE_ID 0x0501

#define SM501_MISC_PNL_24BIT		(1<<25)
#define SM501_USE_ALL		(0xffffffff)
#define SM501FB_FLAG_USE_INIT_MODE	(1<<0)
#define SM501FB_FLAG_USE_HWCURSOR	(1<<2)
#define SM501FB_FLAG_USE_HWACCEL	(1<<3)
#define SM501FB_FLAG_DISABLE_AT_EXIT	(1<<1)

#define SM501_DEVICEID			(0x000060)
#define SM501_DEVICEID_SM501		(0x05010000)
#define SM501_IRQ_MASK			(0x000030)
#define SM501_DRAM_CONTROL		(0x000010)
#define SM501_MISC_CONTROL		(0x000004)
#define SM501_MISC_TIMING		(0x000068)
#define SM501_POWER_MODE_CONTROL	(0x000054)
#define SM501_CURRENT_GATE		(0x000038)
#define SM501_CURRENT_CLOCK		(0x00003C)
#define SM501_POWER_MODE_0_GATE		(0x000040)
#define SM501_POWER_MODE_0_CLOCK	(0x000044)
#define SM501_POWER_MODE_1_GATE		(0x000048)
#define SM501_POWER_MODE_1_CLOCK	(0x00004C)
#define SM501_PROGRAMMABLE_PLL_CONTROL	(0x000074)
#define SM501_GPIO_DATA_LOW		(0x00)
#define SM501_GPIO_DDR_LOW		(0x08)
#define SM501_GPIO31_0_CONTROL		(0x000008)
#define SM501_GPIO_DATA_HIGH		(0x04)
#define SM501_GPIO_DDR_HIGH		(0x0C)
#define SM501_GPIO63_32_CONTROL		(0x00000C)

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
    uint32_t deviceid;
    uint32_t irq_mask;
    uint32_t dram_control;
    uint32_t misc_timing;
    uint32_t misc_control;
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
    uint32_t gpio_31_0_control;
    uint32_t gpio_data_high;
    uint32_t gpio_ddr_high;
    uint32_t gpio_63_32_control;
};

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case SM501_DEVICEID:
        val = s->deviceid;
        break;
    case SM501_IRQ_MASK:
        val = s->irq_mask;
        break;
    case SM501_DRAM_CONTROL:
        val = s->dram_control;
        break;
    case SM501_MISC_CONTROL:
        val = s->misc_control;
        break;
    case SM501_MISC_TIMING:
        val = s->misc_timing;
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
    case SM501_GPIO_DATA_LOW:
        val = s->gpio_data_low;
        break;
    case SM501_GPIO_DDR_LOW:
        val = s->gpio_ddr_low;
        break;
    case SM501_GPIO_DDR_HIGH:
        val = s->gpio_ddr_high;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "sm501_pci: read from undefined register 0x%" HWADDR_PRIx "\n", addr);
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case SM501_IRQ_MASK:
        s->irq_mask = val;
        break;
    case SM501_DRAM_CONTROL:
        s->dram_control = val;
        break;
    case SM501_MISC_CONTROL:
        s->misc_control = val;
        break;
    case SM501_MISC_TIMING:
        s->misc_timing = val;
        break;
    case SM501_POWER_MODE_CONTROL:
        s->power_mode_control = val;
        break;
    case SM501_CURRENT_GATE:
        s->current_gate = val;
        break;
    case SM501_CURRENT_CLOCK:
        s->current_clock = val;
        break;
    case SM501_POWER_MODE_0_GATE:
        s->power_mode_0_gate = val;
        break;
    case SM501_POWER_MODE_0_CLOCK:
        s->power_mode_0_clock = val;
        break;
    case SM501_POWER_MODE_1_GATE:
        s->power_mode_1_gate = val;
        break;
    case SM501_POWER_MODE_1_CLOCK:
        s->power_mode_1_clock = val;
        break;
    case SM501_PROGRAMMABLE_PLL_CONTROL:
        s->programmable_pll_control = val;
        break;
    case SM501_GPIO_DATA_LOW:
        s->gpio_data_low = val;
        break;
    case SM501_GPIO_DDR_LOW:
        s->gpio_ddr_low = val;
        break;
    case SM501_GPIO_DDR_HIGH:
        s->gpio_ddr_high = val;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "sm501_pci: write to undefined register 0x%" HWADDR_PRIx "\n", addr);
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
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

    s->deviceid = SM501_DEVICEID_SM501;
    s->irq_mask = 0;
    s->dram_control = 0;
    s->misc_timing = 0;
    s->misc_control = 0;
    s->power_mode_control = 0;
    s->current_gate = 0;
    s->current_clock = 0;
    s->power_mode_0_gate = 0;
    s->power_mode_0_clock = 0;
    s->power_mode_1_gate = 0;
    s->power_mode_1_clock = 0;
    s->programmable_pll_control = 0;
    s->gpio_data_low = 0;
    s->gpio_ddr_low = 0;
    s->gpio_31_0_control = 0;
    s->gpio_data_high = 0;
    s->gpio_ddr_high = 0;
    s->gpio_63_32_control = 0;
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE || bi->size == 0) {
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x126f );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x0501 );
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
    s->num_bars = 2;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = 0x800000, .name = "sm501-mem" };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_MMIO, .size = 0x100, .name = "sm501-regs" };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "sm501_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(deviceid, PCIBaseState),
        VMSTATE_UINT32(irq_mask, PCIBaseState),
        VMSTATE_UINT32(dram_control, PCIBaseState),
        VMSTATE_UINT32(misc_timing, PCIBaseState),
        VMSTATE_UINT32(misc_control, PCIBaseState),
        VMSTATE_UINT32(power_mode_control, PCIBaseState),
        VMSTATE_UINT32(current_gate, PCIBaseState),
        VMSTATE_UINT32(current_clock, PCIBaseState),
        VMSTATE_UINT32(power_mode_0_gate, PCIBaseState),
        VMSTATE_UINT32(power_mode_0_clock, PCIBaseState),
        VMSTATE_UINT32(power_mode_1_gate, PCIBaseState),
        VMSTATE_UINT32(power_mode_1_clock, PCIBaseState),
        VMSTATE_UINT32(programmable_pll_control, PCIBaseState),
        VMSTATE_UINT32(gpio_data_low, PCIBaseState),
        VMSTATE_UINT32(gpio_ddr_low, PCIBaseState),
        VMSTATE_UINT32(gpio_31_0_control, PCIBaseState),
        VMSTATE_UINT32(gpio_data_high, PCIBaseState),
        VMSTATE_UINT32(gpio_ddr_high, PCIBaseState),
        VMSTATE_UINT32(gpio_63_32_control, PCIBaseState),
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
