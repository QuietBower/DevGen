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

#define TYPE_PCIBASE_DEVICE "pata_hpt3x2n_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI_VENDOR_ID_TTI        0x1103
#define PCI_DEVICE_ID_TTI_HPT366 0x0004

/* PCI Configuration Space Registers */
#define HPT_CFG_TIMING_PM        0x40
#define HPT_CFG_TIMING_PS        0x44
#define HPT_CFG_TIMING_SM        0x48
#define HPT_CFG_TIMING_SS        0x4C
#define HPT_CFG_MCR1_P           0x50
#define HPT_CFG_MCR2_P           0x51
#define HPT_CFG_MCR1_S           0x54
#define HPT_CFG_MCR2_S           0x55
#define HPT_CFG_IRQ_CBL          0x5A
#define HPT_CFG_PLL_CBL          0x5B
#define HPT_CFG_DPLL             0x5C
#define HPT_CFG_BWSR             0x6A
#define HPT_CFG_BIOS_CLK         0x78

/* I/O Space Registers (BAR 4) */
#define HPT_IO_STATE_P           0x70
#define HPT_IO_TRISTATE_P        0x73
#define HPT_IO_STATE_S           0x74
#define HPT_IO_TRISTATE_S        0x77
#define HPT_IO_RESET             0x79
#define HPT_IO_CLK               0x7B
#define HPT_IO_FCNT              0x90
#define HPT_IO_MISC              0x9C

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

    /* I/O State */
    uint8_t state_p;
    uint8_t tristate_p;
    uint8_t state_s;
    uint8_t tristate_s;
    uint8_t reset_reg;
    uint8_t clk;
    uint8_t misc;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case HPT_IO_STATE_P:
        val = s->state_p;
        break;
    case HPT_IO_STATE_S:
        val = s->state_s;
        break;
    case HPT_IO_FCNT:
        if (size == 4) {
            val = 0xABCDE000 | 82; /* 82 gives ~33MHz */
        }
        break;
    case HPT_IO_MISC:
        val = s->misc;
        break;
    default:
        val = 0;
        break;
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case HPT_IO_STATE_P:
        s->state_p = val;
        break;
    case HPT_IO_TRISTATE_P:
        s->tristate_p = val;
        break;
    case HPT_IO_STATE_S:
        s->state_s = val;
        break;
    case HPT_IO_TRISTATE_S:
        s->tristate_s = val;
        break;
    case HPT_IO_RESET:
        s->reset_reg = val;
        break;
    case HPT_IO_CLK:
        s->clk = val;
        break;
    case HPT_IO_MISC:
        s->misc = val;
        break;
    }
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

    s->state_p = 0;
    s->tristate_p = 0;
    s->state_s = 0;
    s->tristate_s = 0;
    s->reset_reg = 0;
    s->clk = 0;
    s->misc = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_TTI);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_TTI_HPT366);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_STORAGE_IDE);
    pci_set_byte(pci_conf + PCI_CLASS_PROG, 0x85); /* Native mode, Bus Master */
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x06); /* rev >= 6 required for HPT366 to be treated as 372N */
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Initialize config space for driver expectations */
    pci_conf[HPT_CFG_MCR1_P] = 0x04; /* MCR1_P: enable bit */
    pci_conf[HPT_CFG_MCR1_S] = 0x04; /* MCR1_S: enable bit */
    pci_conf[HPT_CFG_BWSR] = 0x03;   /* BWSR: both ports active */
    pci_conf[HPT_CFG_IRQ_CBL] = 0x03; /* ata66 cable detect */

    /* BAR Initialization */
    s->num_bars = 5;
    
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 8;
    s->bar_info[0].name = "ide0-cmd";

    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_PIO;
    s->bar_info[1].size = 4;
    s->bar_info[1].name = "ide0-ctrl";

    s->bar_info[2].index = 2;
    s->bar_info[2].type = BAR_TYPE_PIO;
    s->bar_info[2].size = 8;
    s->bar_info[2].name = "ide1-cmd";

    s->bar_info[3].index = 3;
    s->bar_info[3].type = BAR_TYPE_PIO;
    s->bar_info[3].size = 4;
    s->bar_info[3].name = "ide1-ctrl";

    s->bar_info[4].index = 4;
    s->bar_info[4].type = BAR_TYPE_PIO;
    s->bar_info[4].size = 0x100;
    s->bar_info[4].name = "hpt3x2n-bmdma";

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

static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int l)
{
    uint32_t val = pci_default_read_config(pdev, addr, l);
    
    if (addr == HPT_CFG_PLL_CBL && l == 1) {
        val |= 0x80; /* DPLL stable */
    }
    
    return val;
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "pata_hpt3x2n_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT8(state_p, PCIBaseState),
        VMSTATE_UINT8(tristate_p, PCIBaseState),
        VMSTATE_UINT8(state_s, PCIBaseState),
        VMSTATE_UINT8(tristate_s, PCIBaseState),
        VMSTATE_UINT8(reset_reg, PCIBaseState),
        VMSTATE_UINT8(clk, PCIBaseState),
        VMSTATE_UINT8(misc, PCIBaseState),
        VMSTATE_END_OF_LIST()
    }
};

static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pcibase_realize;
    k->exit    = pcibase_uninit;
    k->config_read = pcibase_config_read;
    dc->reset  = pcibase_reset;
    dc->vmsd   = &vmstate_pcibase;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
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
