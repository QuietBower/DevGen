/*
 * QEMU PCI device model for Intel X38 EDAC (behavioral implementation).
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
#include "hw/pci/pci_ids.h"

#define TYPE_PCIBASE_DEVICE "x38_edac_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */

/* PCI identifiers from x38_edac.c (first pci_device_id entry) */
#define X38_PCI_VENDOR_ID        0x8086
#define X38_PCI_DEVICE_ID        0x29e0

/* Memory controller hub BAR / register definitions */
#define X38_MCHBAR_LOW           0x48
#define X38_MCHBAR_HIGH          0x4c
#define X38_MCHBAR_MASK          0xfffffc000ULL
#define X38_MMR_WINDOW_SIZE      16384

#define X38_TOM                  0xa0
#define X38_TOM_MASK             0x3ff
#define X38_TOM_SHIFT            26

#define X38_ERRSTS               0xc8
#define X38_ERRSTS_UE            0x0002
#define X38_ERRSTS_CE            0x0001
#define X38_ERRSTS_BITS          (X38_ERRSTS_UE | X38_ERRSTS_CE)

#define X38_C0DRB                0x200
#define X38_C1DRB                0x600
#define X38_DRB_MASK             0x3ff
#define X38_DRB_SHIFT            26

#define X38_C0ECCERRLOG          0x280
#define X38_C1ECCERRLOG          0x680
#define X38_ECCERRLOG_CE         0x1
#define X38_ECCERRLOG_UE         0x2
#define X38_ECCERRLOG_RANK_BITS  0x18000000
#define X38_ECCERRLOG_SYNDROME_BITS 0xff0000

#define X38_CAPID0               0xe0

/* X38 specific constants */
#define X38_RANKS                8
#define X38_RANKS_PER_CHANNEL    4
#define X38_CHANNELS             2

/* Use generic memory controller class code */
#define X38_PCI_CLASS_ID         0x0500


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
    /* Minimal register shadow structure for key X38 registers */
    struct {
        uint32_t mchbar_low;
        uint32_t mchbar_high;
        uint32_t tom;
        uint16_t errsts;
        uint16_t capid0;
        uint16_t c0drb;
        uint16_t c1drb;
        uint64_t c0eccerrlog;
        uint64_t c1eccerrlog;
    } regs;
};


/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* x38_edac.c uses polling; it never requests or handles interrupts. */
    (void)pdev;
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* x38_edac.c does not initiate or manage DMA transactions */
    (void)pdev;
    (void)is_write;
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* X38 EDAC driver never uses device BAR MMIO directly; it uses
     * MCHBAR mapped from a PCI config register to host physical memory.
     * Our emulated device does not expose such a BAR window here.
     * Keep this as a dummy region returning zeros.
     */

    (void)s;
    (void)addr;
    (void)size;

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* As above, the driver does not touch any device BAR MMIO space. */

    (void)s;
    (void)addr;
    (void)val;
    (void)size;
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* No legacy Port I/O is used by x38_edac.c */
    (void)s;
    (void)addr;
    (void)size;

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    /* No legacy Port I/O is used by x38_edac.c */
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
    PCIDevice *pdev = PCI_DEVICE(dev);
    uint8_t *pci_conf = pdev->config;

    pci_device_reset(pdev);

    /* Revert registers to power-on defaults. The real hardware defaults
     * are not documented in the driver; we choose values consistent
     * with how the driver uses them.
     */

    /* MCHBAR: choose a low, aligned address in guest physical space so
     * that the driver's ioremap will succeed. This is purely to satisfy
     * the driver's mapping logic; the emulated device does not back this
     * with a real BAR window.
     */
    s->regs.mchbar_low = 0x00000001U; /* enable bit set, base 0 */
    s->regs.mchbar_high = 0x00000000U;
    pci_set_long(pci_conf + X38_MCHBAR_LOW, s->regs.mchbar_low);
    pci_set_long(pci_conf + X38_MCHBAR_HIGH, s->regs.mchbar_high);

    /* TOM: choose a small, non-zero value within mask for stacked check. */
    s->regs.tom = 0x0040U; /* arbitrary non-zero within 10 bits */
    pci_set_word(pci_conf + X38_TOM, s->regs.tom);

    /* ERRSTS: no errors pending. */
    s->regs.errsts = 0;
    pci_set_word(pci_conf + X38_ERRSTS, s->regs.errsts);

    /* CAPID0: eighth byte bit 0x20 indicates Dual Channel Disable.
     * Clear it so driver detects dual-channel (2 channels).
     */
    s->regs.capid0 = 0;
    pci_set_word(pci_conf + X38_CAPID0, s->regs.capid0);
    /* Ensure 8th byte (CAPID0+8) has bit 0x20 cleared. */
    pci_conf[X38_CAPID0 + 8] &= ~0x20;

    /* DRB registers: set simple increasing boundaries per channel. */
    s->regs.c0drb = 0x0010U; /* first rank boundary */
    s->regs.c1drb = 0x0010U;
    pci_set_word(pci_conf + X38_C0DRB, s->regs.c0drb);
    pci_set_word(pci_conf + X38_C1DRB, s->regs.c1drb);

    /* ECC error log registers: no errors. */
    s->regs.c0eccerrlog = 0;
    s->regs.c1eccerrlog = 0;

    /* Clear any shadowed ECC logs in a hypothetical MMIO space by
     * keeping them at zero; driver reads them via host memory window.
     */
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  X38_PCI_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  X38_PCI_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, X38_PCI_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization
     * The x38_edac driver uses MCHBAR (a system memory window) and does
     * not touch any PCI BARs of this host bridge directly. We keep the
     * BAR list empty so the guest sees no additional MMIO/PIO regions.
     */
    s->num_bars = 0;

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI / MSI-X not used in x38_edac.c */
    s->has_msi = false;
    s->has_msix = false;

    /* Initialize register shadows and reflect them into PCI config
     * space where the driver reads them.
     */

    /* MCHBAR: enabled at low address. */
    s->regs.mchbar_low = 0x00000001U;
    s->regs.mchbar_high = 0x00000000U;
    pci_set_long(pci_conf + X38_MCHBAR_LOW, s->regs.mchbar_low);
    pci_set_long(pci_conf + X38_MCHBAR_HIGH, s->regs.mchbar_high);

    /* TOM: small non-zero value within mask. */
    s->regs.tom = 0x0040U;
    pci_set_word(pci_conf + X38_TOM, s->regs.tom);

    /* ERRSTS: clear. */
    s->regs.errsts = 0;
    pci_set_word(pci_conf + X38_ERRSTS, s->regs.errsts);

    /* CAPID0: dual-channel enabled (bit 0x20 in CAPID0+8 cleared). */
    s->regs.capid0 = 0;
    pci_set_word(pci_conf + X38_CAPID0, s->regs.capid0);
    pci_conf[X38_CAPID0 + 8] &= ~0x20;

    /* DRB registers: arbitrary small boundary values, masked by driver. */
    s->regs.c0drb = 0x0010U;
    s->regs.c1drb = 0x0010U;
    pci_set_word(pci_conf + X38_C0DRB, s->regs.c0drb);
    pci_set_word(pci_conf + X38_C1DRB, s->regs.c1drb);

    /* ECC error logs: zero. Driver reads these via MCHBAR window, not
     * via PCI config space.
     */
    s->regs.c0eccerrlog = 0;
    s->regs.c1eccerrlog = 0;
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

    /* No dynamically allocated resources or timers to free. */
    (void)s;
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "x38_edac_pci",
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
