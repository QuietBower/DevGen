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
/* (none) */

#define TYPE_PCIBASE_DEVICE "Marvell_PTP_Driver_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_CAVIUM 0x177d
#define PCI_DEVICE_ID_OCTEONTX2_PTP 0xA00C
#define PCI_PTP_CLASS_ID 0 /* TODO: request actual class ID */
#define PCI_PTP_BAR_NO 0
#define PCI_PTP_BAR_SIZE 0x2000

#define PTP_CLOCK_CFG 0xF00ULL
#define PTP_CLOCK_CFG_PTP_EN BIT_ULL(0)
#define PTP_CLOCK_CFG_EXT_CLK_EN BIT_ULL(1)
#define PTP_CLOCK_CFG_EXT_CLK_IN_MASK GENMASK_ULL(7, 2)
#define PTP_CLOCK_CFG_TSTMP_EDGE BIT_ULL(9)
#define PTP_CLOCK_CFG_TSTMP_EN BIT_ULL(8)
#define PTP_CLOCK_CFG_TSTMP_IN_MASK GENMASK_ULL(15, 10)
#define PTP_CLOCK_CFG_ATOMIC_OP_MASK GENMASK_ULL(28, 26)
#define PTP_CLOCK_CFG_PPS_EN BIT_ULL(30)
#define PTP_CLOCK_CFG_PPS_INV BIT_ULL(31)
#define PTP_CLOCK_LO 0xF08ULL
#define PTP_CLOCK_HI 0xF10ULL
#define PTP_CLOCK_COMP 0xF18ULL
#define PTP_PPS_HI_INCR 0xF60ULL
#define PTP_PPS_LO_INCR 0xF68ULL
#define PTP_PPS_THRESH_LO 0xF50ULL
#define PTP_PPS_THRESH_HI 0xF58ULL
#define PTP_TIMESTAMP 0xF20ULL
#define PTP_CLOCK_SEC 0xFD0ULL
#define PTP_SEC_ROLLOVER 0xFD8ULL
#define PTP_FRNS_TIMESTAMP 0xFE0ULL
#define PTP_NXT_ROLLOVER_SET 0xFE8ULL
#define PTP_CURR_ROLLOVER_SET 0xFF0ULL
#define PTP_NANO_TIMESTAMP 0xFF8ULL
#define PTP_SEC_TIMESTAMP 0x1000ULL

typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM 
} BARType;

typedef struct {
    int     index;
    BARType type;
    hwaddr  size;
    const char *name;
} BARInfo;

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;
    /* Interrupt state: not used as no MSI/MSI-X evidence */
    /* Hardware Register Shadows */
    uint64_t *regs; /* dynamically allocated array for MMIO registers */

    /* DMA, Status, Reset, PM, etc. not used, placeholders removed */
};

/* Internal helper for status-triggered signaling. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    /* No IRQ signaling needed */
}

/* Device-initiated DMA logic - not used */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /* No DMA */
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Convert address to register index */
    hwaddr reg_idx = addr >> 3;
    if (reg_idx < (PCI_PTP_BAR_SIZE >> 3)) {
        val = s->regs[reg_idx];
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    hwaddr reg_idx = addr >> 3;
    if (reg_idx < (PCI_PTP_BAR_SIZE >> 3)) {
        if (size == 8) {
            s->regs[reg_idx] = val;
        } else {
            /* Partial write: mask and update */
            unsigned byte_offset = addr & 0x7;
            uint64_t mask = ((1ULL << (size * 8)) - 1) << (byte_offset * 8);
            s->regs[reg_idx] = (s->regs[reg_idx] & ~mask) | (val & mask);
        }
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* No PIO used */
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    /* No PIO used */
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

    /* Reset registers to zero */
    memset(s->regs, 0, PCI_PTP_BAR_SIZE);
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }
    
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

    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_CAVIUM);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_OCTEONTX2_PTP);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_PTP_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    s->num_bars = 1;
    s->bar_info[0] = (BARInfo){ .index = PCI_PTP_BAR_NO, .type = BAR_TYPE_MMIO, .size = PCI_PTP_BAR_SIZE, .name = "ptp-mmio" };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No MSI/MSI-X init */
    /* No DMA config */
    /* No timers */
    /* Final state initialization: allocate register array */
    s->regs = g_new0(uint64_t, PCI_PTP_BAR_SIZE / sizeof(uint64_t));
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
    g_free(s->regs);
}

static const VMStateDescription vmstate_pcibase = {
    .name = "Marvell_PTP_Driver_pci",
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
