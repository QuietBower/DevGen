/*
 * QEMU PCI device model for Intel X38 Memory Controller EDAC
 * Based on Linux driver drivers/edac/x38_edac.c
 *
 * This device provides the MCHBAR MMIO region (BAR0) and
 * custom PCI config registers (MCHBAR, ERRSTS, TOM, CAPID0)
 * required for the x38_edac driver to probe successfully.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/module.h"
#include "qemu/log.h"
#include "qemu/timer.h"
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

#define TYPE_PCIBASE_DEVICE "x38_edac_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI Device ID (first entry in driver's pci_device_id table) */
#define PCI_DEVICE_ID_INTEL_X38_HB 0x29e0

/* Register offsets within MCHBAR (MMIO) - driver accesses via ioremap */
#define X38_TOM                  0xa0
#define X38_ERRSTS               0xc8
#define X38_C0DRB                0x200
#define X38_C1DRB                0x600
#define X38_C0ECCERRLOG          0x280
#define X38_C1ECCERRLOG          0x680
#define X38_CAPID0               0xe0

/* Register value masks/shifts */
#define X38_ERRSTS_UE            0x0002
#define X38_ERRSTS_CE            0x0001
#define X38_ERRSTS_BITS          (X38_ERRSTS_UE | X38_ERRSTS_CE)
#define X38_ECCERRLOG_CE         0x1
#define X38_ECCERRLOG_UE         0x2
#define X38_ECCERRLOG_RANK_BITS  0x18000000
#define X38_ECCERRLOG_SYNDROME_BITS 0xff0000
#define X38_DRB_MASK             0x3ff
#define X38_DRB_SHIFT            26
#define X38_TOM_MASK             0x3ff
#define X38_TOM_SHIFT            26

/* MCHBAR window size (driver uses 0x4000) */
#define X38_MMR_WINDOW_SIZE      16384

/* Custom PCI config register offsets (from driver source) */
#define X38_MCHBAR_LOW    0x48
#define X38_MCHBAR_HIGH   0x4c
#define X38_MCHBAR_MASK   0xfffffc000ULL

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Memory regions for BARs */
    MemoryRegion bar_regions[6];
    int num_bars;

    /* MMIO register file (the MCHBAR region) */
    uint8_t regs[X38_MMR_WINDOW_SIZE];
};

/* Forward declarations for config handlers */
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len);
static void pcibase_config_write(PCIDevice *pdev, uint32_t addr,
                                 uint32_t val, int len);

/* MMIO read handler for BAR0 (MCHBAR) */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Bounds check */
    if (addr + size > X38_MMR_WINDOW_SIZE) {
        return ~0ULL;
    }

    /* Perform little-endian read */
    switch (size) {
    case 1:
        val = *(uint8_t *)(s->regs + addr);
        break;
    case 2:
        val = lduw_le_p(s->regs + addr);
        break;
    case 4:
        val = ldl_le_p(s->regs + addr);
        break;
    case 8:
        val = ldq_le_p(s->regs + addr);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: unsupported read size %u at 0x%" HWADDR_PRIx "\n",
                      __func__, size, addr);
        val = ~0ULL;
    }
    return val;
}

/* MMIO write handler for BAR0 (MCHBAR) */
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                               unsigned size)
{
    PCIBaseState *s = opaque;

    /* Bounds check */
    if (addr + size > X38_MMR_WINDOW_SIZE) {
        return;
    }

    switch (size) {
    case 1:
        stb_p(s->regs + addr, val);
        break;
    case 2:
        stw_le_p(s->regs + addr, val);
        break;
    case 4:
        stl_le_p(s->regs + addr, val);
        break;
    case 8:
        stq_le_p(s->regs + addr, val);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: unsupported write size %u at 0x%" HWADDR_PRIx "\n",
                      __func__, size, addr);
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

/* PIO handlers (unused by driver) */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val,
                              unsigned size)
{
    /* no-op */
}

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* Interrupts: not used by the driver (polling only) */
/* DMA: not used by the driver */

/* Reset handler */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    pci_device_reset(pdev);
    memset(s->regs, 0, sizeof(s->regs));

    /* Configure defaults for custom config registers */
    pci_set_word(pdev->config + X38_TOM, 0x0000);        /* TOM = 0 */
    pci_set_word(pdev->config + X38_ERRSTS, 0x0000);     /* no errors */
    pci_set_byte(pdev->config + X38_CAPID0 + 8, 0x00);   /* dual channel enabled (DCD=0) */
    /* MCHBAR enable bit initially 0 */
    pci_set_long(pdev->config + X38_MCHBAR_LOW, 0x00000000);
    pci_set_long(pdev->config + X38_MCHBAR_HIGH, 0x00000000);
}

/* Custom PCI config read handler */
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    uint32_t val;

    /* Intercept MCHBAR registers to reflect BAR0 address */
    if (ranges_overlap(addr, len, X38_MCHBAR_LOW, 4)) {
        /* MCHBAR_LOW: return BAR0 base address with enable bit */
        uint64_t bar0_addr = pdev->io_regions[0].addr;
        uint32_t stored = pci_get_long(pdev->config + X38_MCHBAR_LOW);
        val = (bar0_addr & X38_MCHBAR_MASK) | (stored & 0x1);
        /* Adjust for the actual offset and length requested */
        if (addr == X38_MCHBAR_LOW && len == 4) {
            return val;
        } else if (addr == X38_MCHBAR_LOW && len == 2) {
            return val & 0xffff;
        } else if (addr == X38_MCHBAR_LOW && len == 1) {
            return val & 0xff;
        } else if (addr == X38_MCHBAR_LOW+2 && len == 2) {
            return val >> 16;
        }
        /* Other sizes: fall through to default */
    }
    if (ranges_overlap(addr, len, X38_MCHBAR_HIGH, 4)) {
        /* MCHBAR_HIGH always 0 */
        return 0;
    }

    /* For all other config space, use default handler */
    return pci_default_read_config(pdev, addr, len);
}

/* Custom PCI config write handler */
static void pcibase_config_write(PCIDevice *pdev, uint32_t addr,
                                 uint32_t val, int len)
{
    /* Handle ERRSTS write-1-to-clear semantics */
    if (addr == X38_ERRSTS && len == 2) {
        uint16_t old = pci_get_word(pdev->config + X38_ERRSTS);
        uint16_t new = old & ~val;  /* writing 1 clears */
        pci_set_word(pdev->config + X38_ERRSTS, new);
        return;
    }

    /* MCHBAR_LOW: only bit 0 (enable) is writable */
    if (ranges_overlap(addr, len, X38_MCHBAR_LOW, 4)) {
        if (addr == X38_MCHBAR_LOW && len == 4) {
            uint32_t old = pci_get_long(pdev->config + X38_MCHBAR_LOW);
            uint32_t new = (old & ~0x1) | (val & 0x1);
            pci_set_long(pdev->config + X38_MCHBAR_LOW, new);
            return;
        } else if (addr == X38_MCHBAR_LOW && len == 2) {
            uint16_t old = pci_get_word(pdev->config + X38_MCHBAR_LOW);
            uint16_t new = (old & ~0x1) | (val & 0x1);
            pci_set_word(pdev->config + X38_MCHBAR_LOW, new);
            return;
        } else if (addr == X38_MCHBAR_LOW && len == 1) {
            uint8_t old = pci_get_byte(pdev->config + X38_MCHBAR_LOW);
            uint8_t new = (old & ~0x1) | (val & 0x1);
            pci_set_byte(pdev->config + X38_MCHBAR_LOW, new);
            return;
        }
        /* Other offsets in low dword: read-only base address bits */
        return;
    }

    /* MCHBAR_HIGH: read-only */
    if (ranges_overlap(addr, len, X38_MCHBAR_HIGH, 4)) {
        return;
    }

    /* Default handling for all other registers */
    pci_default_write_config(pdev, addr, val, len);
}

/* BAR registration helper */
static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s,
                                 int index, int type, hwaddr size,
                                 const char *name, Error **errp)
{
    hwaddr aligned_size = pow2ceil(size);
    MemoryRegion *mr = &s->bar_regions[index];

    if (type == PCI_BASE_ADDRESS_SPACE_MEMORY) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s,
                              name, aligned_size);
        pci_register_bar(pdev, index, type, mr);
    } else if (type == PCI_BASE_ADDRESS_SPACE_IO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s,
                              name, aligned_size);
        pci_register_bar(pdev, index, type, mr);
    } else {
        /* RAM type not used in this device */
        g_assert_not_reached();
    }
}

/* Realize: called when device is instantiated */
static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Set PCI IDs */
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_INTEL);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_INTEL_X38_HB);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_MEMORY_RAM);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Express endpoint */
    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    /* PM capability */
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR0: MCHBAR MMIO region */
    s->num_bars = 1;
    pcibase_register_bar(pdev, s, 0, PCI_BASE_ADDRESS_SPACE_MEMORY,
                         X38_MMR_WINDOW_SIZE, "bar0", errp);

    /* Initialize MMIO register file */
    memset(s->regs, 0, sizeof(s->regs));

    /* Set default values for custom config registers */
    pci_set_word(pci_conf + X38_TOM, 0x0000);
    pci_set_word(pci_conf + X38_ERRSTS, 0x0000);
    pci_set_byte(pci_conf + X38_CAPID0 + 8, 0x00);
    pci_set_long(pci_conf + X38_MCHBAR_LOW, 0x00000000);
    pci_set_long(pci_conf + X38_MCHBAR_HIGH, 0x00000000);
}

/* Uninit */
static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    /* No MSI/etc to clean up */
}

/* VMState (minimal to satisfy migration) */
static const VMStateDescription vmstate_pcibase = {
    .name = "x38_edac_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT8_ARRAY(regs, PCIBaseState, X38_MMR_WINDOW_SIZE),
        VMSTATE_END_OF_LIST()
    }
};

/* Class initialization */
static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pcibase_realize;
    k->exit = pcibase_uninit;
    k->config_read = pcibase_config_read;
    k->config_write = pcibase_config_write;
    dc->reset = pcibase_reset;
    dc->vmsd = &vmstate_pcibase;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

/* Type registration */
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
