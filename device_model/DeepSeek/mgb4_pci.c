/*
 * QEMU PCI device model for mgb4 (Magenta Video Grabber 4)
 * Based on driver at /home/eely/linux-7.1/drivers/media/pci/mgb4/mgb4_core.c
 * Phase 4 runtime fix: separate XDMA BAR1 emulation and MSI-X PBA size correction
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

#define TYPE_PCIBASE_DEVICE "mgb4_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Vendor and Device IDs extracted from driver's pci_device_id table */
#define DIGITEQ_VID         0x1ed8
#define T100_DID            0x0101

/* BAR indices */
#define MGB4_MGB4_BAR_ID    0
#define MGB4_XDMA_BAR_ID    1

/* Video register offsets (BAR0) */
#define VIN_REG_ADDRESS      0x00
#define VIN_REG_CONFIG       0x04
#define VIN_REG_STATUS       0x08
#define VIN_REG_RESOLUTION   0x0C
#define VIN_REG_FRAME_PERIOD 0x10
#define VIN_REG_SYNC         0x14
#define VIN_REG_PCLK         0x18
#define VIN_REG_HSYNC        0x1C
#define VIN_REG_VSYNC        0x20
#define VIN_REG_PADDING      0x24
#define VIN_REG_TIMER        0x28
#define VIN_REG_MODULE_VERSION 0xD4

/* Video output register offsets */
#define VOUT_REG_ADDRESS      0x00
#define VOUT_REG_CONFIG       0x04
#define VOUT_REG_STATUS       0x08
#define VOUT_REG_RESOLUTION   0x0C
#define VOUT_REG_FRAME_LIMIT  0x10
#define VOUT_REG_HSYNC        0x14
#define VOUT_REG_VSYNC        0x18
#define VOUT_REG_PADDING      0x1C
#define VOUT_REG_TIMER        0x20

/* Interrupt registers */
#define INTR_STATUS(bank)   (0x410 + (bank) * 0x50)
#define INTR_MASK           BIT(16)

/* Device counts */
#define MGB4_VIN_DEVICES    2
#define MGB4_VOUT_DEVICES   2

/* MSI-X table offset within BAR0 (no longer used; moved to exclusive BAR) */
#define MSIX_TABLE_OFFSET    0x1800

/* XDMA register layout constants (based on typical Xilinx XDMA IP) */
#define XDMA_H2C_CHAN_BASE  0x0000
#define XDMA_C2H_CHAN_BASE  0x1000
#define XDMA_CHAN_STRIDE    0x100
#define XDMA_H2C_TARGET     0x00000000
#define XDMA_C2H_TARGET     0x00000001

/* BAR info types */
typedef enum {
    BAR_TYPE_NONE,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM,
} BARType;

typedef struct {
    int index;
    BARType type;
    uint64_t size;
    const char *name;
} BARInfo;

/* ------------------------------------------------------------------ */

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;

    /* BAR0 registers (video, cmt, etc.) - size 0x2000 */
    uint32_t regs[0x2000 / 4];
    /* BAR1 registers (XDMA) - size 0x10000 */
    uint32_t xdma_regs[0x10000 / 4];
};

/* ------------------------------------------------------------------ */

/* Initialize the XDMA register space with channel identifiers */
static void mgb4_init_xdma_regs(PCIBaseState *s)
{
    int i;

    /* H2C channels: set identifier to H2C target for two channels */
    for (i = 0; i < 2; i++) {
        uint32_t offset = XDMA_H2C_CHAN_BASE + i * XDMA_CHAN_STRIDE;
        s->xdma_regs[offset / 4] = XDMA_H2C_TARGET;
    }
    /* C2H channels: set identifier to C2H target for four channels */
    for (i = 0; i < 4; i++) {
        uint32_t offset = XDMA_C2H_CHAN_BASE + i * XDMA_CHAN_STRIDE;
        s->xdma_regs[offset / 4] = XDMA_C2H_TARGET;
    }

    /* Channel present registers */
    s->xdma_regs[0x200 / 4] = 0x3;      /* H2C: channels 0-1 present */
    s->xdma_regs[0x1200 / 4] = 0xF;     /* C2H: channels 0-3 present */
}

/* BAR0 MMIO handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= 0x2000) {
        return 0xFFFFFFFFFFFFFFFFULL;
    }
    if (size != 4) {
        return 0xFFFFFFFFFFFFFFFFULL;
    }

    switch (addr) {
    case 0xD0:  /* temperature sensor raw value */
        /* Return a raw value yielding approx 30 C */
        val = 0xABC00000;
        break;
    default:
        val = s->regs[addr / 4];
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= 0x2000) {
        return;
    }
    if (size != 4) {
        return;
    }

    s->regs[addr / 4] = val;
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

/* ------------------------------------------------------------------ */

/* BAR1 (XDMA) MMIO handlers */
static uint64_t xdma_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= 0x10000) {
        return 0xFFFFFFFFFFFFFFFFULL;
    }
    if (size != 4) {
        return 0xFFFFFFFFFFFFFFFFULL;
    }

    val = s->xdma_regs[addr / 4];
    return val;
}

static void xdma_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= 0x10000) {
        return;
    }
    if (size != 4) {
        return;
    }

    s->xdma_regs[addr / 4] = val;
}

static const MemoryRegionOps xdma_mmio_ops = {
    .read = xdma_mmio_read,
    .write = xdma_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

/* ------------------------------------------------------------------ */

/* Dummy PIO handlers (unused) */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* ------------------------------------------------------------------ */

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

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    memset(s->regs, 0, sizeof(s->regs));
    memset(s->xdma_regs, 0, sizeof(s->xdma_regs));
    mgb4_init_xdma_regs(s);
    s->regs[0xC4 / 4] = 0x01000000;  /* fw_version = 1 */
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  DIGITEQ_VID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  T100_DID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0280); /* Network controller (typical for video) */
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Enable PCIe capability */
    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* ------------------------------------------------------------------ */
    /* BAR0: mgb4 video & cmt registers (size 0x2000) */
    /* ------------------------------------------------------------------ */
    s->num_bars = 2;
    s->bar_info[0] = (BARInfo){
        .index = 0,
        .type = BAR_TYPE_MMIO,
        .size = 0x2000,
        .name = "mgb4-regs"
    };
    pcibase_register_bar(pdev, s, &s->bar_info[0], errp);

    /* ------------------------------------------------------------------ */
    /* BAR1: XDMA registers (size 0x10000) */
    /* ------------------------------------------------------------------ */
    s->bar_info[1] = (BARInfo){
        .index = 1,
        .type = BAR_TYPE_MMIO,
        .size = 0x10000,
        .name = "xdma"
    };
    /* Register BAR1 with its own memory region and handlers */
    memory_region_init_io(&s->bar_regions[1], OBJECT(s), &xdma_mmio_ops, s,
                          "xdma", 0x10000);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_regions[1]);

    /* ------------------------------------------------------------------ */
    /* MSI-X support: allocate an exclusive BAR on a free index (BAR2) */
    /* ------------------------------------------------------------------ */
    if (msix_init_exclusive_bar(pdev, 2, 2, errp)) {
        error_propagate(errp, errp);
        return;
    }

    /* Initialize register files */
    memset(s->regs, 0, sizeof(s->regs));
    memset(s->xdma_regs, 0, sizeof(s->xdma_regs));
    mgb4_init_xdma_regs(s);
    s->regs[0xC4 / 4] = 0x01000000;  /* fw_version = 1 for FPDL3 module */
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    msix_uninit_exclusive_bar(pdev);
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

static const VMStateDescription vmstate_pcibase = {
    .name = "mgb4_pci",
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
