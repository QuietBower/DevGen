/*
 * QEMU PCI device model for Intel i3200 EDAC-compatible controller
 * Behavioral implementation based on Linux driver drivers/edac/i3200_edac.c
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

#define TYPE_PCIBASE_DEVICE "i3200_edac_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define EDAC_MOD_STR        "i3200_edac"
#define PCI_VENDOR_ID_INTEL        0x8086
#define PCI_CLASS_BRIDGE_HOST        0x0600
#define PCI_DEVICE_ID_INTEL_3200_HB    0x29f0
#define I3200_DIMMS        4
#define I3200_RANKS        8
#define I3200_RANKS_PER_CHANNEL    4
#define I3200_CHANNELS        2
#define I3200_MCHBAR_LOW    0x48
#define I3200_MCHBAR_HIGH    0x4c
#define I3200_MCHBAR_MASK    0xfffffc000ULL
#define I3200_MMR_WINDOW_SIZE    16384
#define I3200_TOM        0xa0
#define I3200_TOM_MASK        0x3ff
#define I3200_TOM_SHIFT        26
#define I3200_ERRSTS        0xc8
#define I3200_ERRSTS_UE        0x0002
#define I3200_ERRSTS_CE        0x0001
#define I3200_ERRSTS_BITS    (I3200_ERRSTS_UE | I3200_ERRSTS_CE)
#define I3200_C0DRB    0x200
#define I3200_C1DRB    0x600
#define I3200_DRB_MASK    0x3ff
#define I3200_DRB_SHIFT    26
#define I3200_C0ECCERRLOG    0x280
#define I3200_C1ECCERRLOG        0x680
#define I3200_ECCERRLOG_CE        0x1
#define I3200_ECCERRLOG_UE        0x2
#define I3200_ECCERRLOG_RANK_BITS    0x18000000
#define I3200_ECCERRLOG_RANK_SHIFT    27
#define I3200_ECCERRLOG_SYNDROME_BITS    0xff0000
#define I3200_ECCERRLOG_SYNDROME_SHIFT    16
#define I3200_CAPID0            0xe0

/* PCI identification: first entry of i3200_pci_tbl[] */
#define I3200_PCI_VENDOR_ID PCI_VENDOR_ID_INTEL
#define I3200_PCI_DEVICE_ID PCI_DEVICE_ID_INTEL_3200_HB
#define I3200_PCI_CLASS_ID  PCI_CLASS_BRIDGE_HOST

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
    struct {
        uint64_t mchbar; /* combined from I3200_MCHBAR_LOW/HIGH */
        uint16_t errsts; /* I3200_ERRSTS */
        uint16_t capid0; /* I3200_CAPID0, lower 16 bits used */
        uint16_t c0drb[I3200_RANKS_PER_CHANNEL]; /* base from I3200_C0DRB */
        uint16_t c1drb[I3200_RANKS_PER_CHANNEL]; /* base from I3200_C1DRB */
        uint64_t eccerrlog[I3200_CHANNELS]; /* C0/C1 ECCERRLOG */
        uint32_t tom; /* I3200_TOM */
    } regs;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    (void)pdev;
    /*
     * The driver uses polling (i3200_check() is called from EDAC core),
     * not interrupts, so we do not assert any IRQ lines here.
     */
}

/* Device-initiated DMA logic based on driver access patterns
 * The i3200 EDAC driver does not program any DMA engine, so this is unused.
 */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    (void)s;
    (void)pdev;
    (void)is_write;
}

/* Helper: read 16-bit DRB register for a given channel/rank */
static uint16_t i3200_get_drb(PCIBaseState *s, int channel, int rank)
{
    if (channel == 0 && rank >= 0 && rank < I3200_RANKS_PER_CHANNEL) {
        return s->regs.c0drb[rank] & I3200_DRB_MASK;
    } else if (channel == 1 && rank >= 0 && rank < I3200_RANKS_PER_CHANNEL) {
        return s->regs.c1drb[rank] & I3200_DRB_MASK;
    }
    return 0;
}

/* Helper: read 16-bit TOM register */
static uint16_t i3200_get_tom(PCIBaseState *s)
{
    return s->regs.tom & I3200_TOM_MASK;
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* The driver only uses 16-bit (readw) and 64-bit (readq) accesses.
     * We allow any size but only special-case what is required.
     */

    /* ECC error log registers: 64-bit reads */
    if (addr == I3200_C0ECCERRLOG && size == 8) {
        val = s->regs.eccerrlog[0];
        return val;
    }
    if (addr == I3200_C1ECCERRLOG && size == 8) {
        val = s->regs.eccerrlog[1];
        return val;
    }

    /* DRB registers: readw(window + I3200_CxDRB + 2*i) & I3200_DRB_MASK */
    if (addr >= I3200_C0DRB && addr < I3200_C0DRB + 2 * I3200_RANKS_PER_CHANNEL && size == 2) {
        int index = (addr - I3200_C0DRB) >> 1; /* each reg is 2 bytes */
        if (index >= 0 && index < I3200_RANKS_PER_CHANNEL) {
            val = i3200_get_drb(s, 0, index) & 0xFFFF;
            return val;
        }
    }
    if (addr >= I3200_C1DRB && addr < I3200_C1DRB + 2 * I3200_RANKS_PER_CHANNEL && size == 2) {
        int index = (addr - I3200_C1DRB) >> 1;
        if (index >= 0 && index < I3200_RANKS_PER_CHANNEL) {
            val = i3200_get_drb(s, 1, index) & 0xFFFF;
            return val;
        }
    }

    /* All other MMIO locations are not used by the driver, return 0. */
    (void)s;
    (void)addr;
    (void)size;
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* The driver never writes to the MCHBAR window, so we ignore writes. */
    (void)s;
    (void)addr;
    (void)val;
    (void)size;
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    (void)s;
    (void)addr;
    (void)size;
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    (void)s;
    (void)addr;
    (void)val;
    (void)size;
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
    int i;

    pci_device_reset(PCI_DEVICE(dev));

    /* Revert registers to power-on defaults / simple values the driver can use. */
    memset(&s->regs, 0, sizeof(s->regs));

    /* Provide a simple, consistent memory configuration so that
     * drb_to_nr_pages() yields non-zero for some DIMMs.
     * Use 64MB granularity values: e.g., ranks at 256MB increments.
     */
    for (i = 0; i < I3200_RANKS_PER_CHANNEL; i++) {
        /* cumulative boundaries in units of 64MB */
        uint16_t boundary = (i + 1) * 4; /* 4 * 64MB = 256MB per rank */
        s->regs.c0drb[i] = boundary & I3200_DRB_MASK;
        s->regs.c1drb[i] = boundary & I3200_DRB_MASK;
    }

    /* Top of memory is the last DRB value on channel 1 */
    s->regs.tom = s->regs.c1drb[I3200_RANKS_PER_CHANNEL - 1] & I3200_TOM_MASK;

    /* CAPID0 upper byte used by how_many_channels():
     * bit 5 (0x20) = DCD (Dual Channel Disable), 0 for dual channel
     * bit 4 (0x10) = 2DPC disable. We choose 0 meaning "2 DIMMS per channel enabled".
     * We only store 16 bits locally; the driver reads CAPID0+8 from config space,
     * so this shadow is informational only.
     */
    s->regs.capid0 = 0x0000;

    /* No ECC errors pending at reset. */
    s->regs.errsts = 0x0000;
    s->regs.eccerrlog[0] = 0;
    s->regs.eccerrlog[1] = 0;

    /* MCHBAR: point to BAR0 base; this will be synthesized via config reads. */
    s->regs.mchbar = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  I3200_PCI_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  I3200_PCI_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, I3200_PCI_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Express + PM capabilities to look like a modern host bridge */
    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization: driver uses MCHBAR pointing to MMIO window of size I3200_MMR_WINDOW_SIZE */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = I3200_MMR_WINDOW_SIZE;
    s->bar_info[0].name = "i3200-mmio";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = NULL;
    }

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* The driver does not enable MSI/MSI-X. Keep them disabled. */
    s->has_msi = false;
    s->has_msix = false;

    /* Initialize register shadows and reset-derived config values. */
    memset(&s->regs, 0, sizeof(s->regs));

    /* Provide a sane reset state by calling the reset handler explicitly. */
    pcibase_reset(DEVICE(pdev));

    /* Program MCHBAR config registers so that i3200_map_mchbar() finds
     * a valid, ioremap-able window corresponding to BAR0.
     */
    uint64_t bar0_addr = pci_get_bar_addr(pdev, 0);
    s->regs.mchbar = bar0_addr & I3200_MCHBAR_MASK;

    /* Split into low/high dwords as the driver does: */
    uint32_t mchbar_low = (uint32_t)(s->regs.mchbar & 0xffffffffULL);
    uint32_t mchbar_high = (uint32_t)((s->regs.mchbar >> 32) & 0xffffffffULL);

    pci_set_long(pci_conf + I3200_MCHBAR_LOW, mchbar_low);
    pci_set_long(pci_conf + I3200_MCHBAR_HIGH, mchbar_high);

    /* Program TOM config word from shadow. */
    pci_set_word(pci_conf + I3200_TOM, i3200_get_tom(s));

    /* CAPID0: only the upper byte is consulted by how_many_channels().
     * We choose dual-channel enabled and 2 DIMMs per channel enabled (0x00).
     */
    {
        uint32_t capid0_val = 0;
        s->regs.capid0 = (uint16_t)(capid0_val & 0xFFFF);
        pci_set_long(pci_conf + I3200_CAPID0, capid0_val);
    }

    /* Error status register initial value is 0 (no errors). */
    pci_set_word(pci_conf + I3200_ERRSTS, s->regs.errsts);
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

    (void)s;
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "i3200_edac_pci",
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
