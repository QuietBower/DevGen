/*
 * QEMU PCI device model for Intel HSU DMA (minimal behavior for driver probe)
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

#define TYPE_PCIBASE_DEVICE "hsu_dma_pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define HSU_PCI_DMASR              0x00
#define HSU_PCI_DMAISR             0x04
#define HSU_PCI_CHAN_OFFSET        0x100
#define PCI_DEVICE_ID_INTEL_MFLD_HSU_DMA 0x081e
#define PCI_DEVICE_ID_INTEL_MRFLD_HSU_DMA 0x1192
#define HSU_CH_SR                  0x00
#define HSU_DMA_CHAN_LENGTH        0x40
#define HSU_CH_DxSAR(x)            (0x20 + 8 * (x))
#define HSU_CH_BSR                 0x10
#define HSU_CH_DCR                 0x08
#define HSU_CH_MTSR                0x14
#define HSU_DMA_CHAN_NR_DESC       4
#define HSU_CH_DxTSR(x)            (0x24 + 8 * (x))
#define HSU_CH_CR                  0x04

#define PCI_VENDOR_ID_INTEL        0x8086
#define PCI_CLASS_SYSTEM_DMA       0x0801

#define PCIBASE_VENDOR_ID          PCI_VENDOR_ID_INTEL
#define PCIBASE_DEVICE_ID          PCI_DEVICE_ID_INTEL_MFLD_HSU_DMA
#define PCIBASE_CLASS_ID           PCI_CLASS_SYSTEM_DMA

/* simple guess: number of DMA channels used by generic HSU driver
 * The driver references chip->hsu->nr_channels but we don't see its
 * definition here; however, for register space layout we can expose
 * a small fixed number. This only affects how the driver scans bits
 * in DMAISR. To avoid relying on unknown value, we will just provide
 * 32 bits in DMAISR/DMASR, which is sufficient for for_each_set_bit
 * regardless of actual nr_channels.
 */

/* Per-channel software-visible status register bits are now defined
 * by the supplementary driver source. We mirror these defines so that
 * we can construct synthetic status values when needed.
 */
#ifndef GENMASK
#define GENMASK(h, l) (((~0U) - (1U << (l)) + 1) & (~0U >> (31 - (h))))
#endif
#ifndef BIT
#define BIT(nr) (1U << (nr))
#endif

#define HSU_CH_SR_DESCE_ANY GENMASK(19, 16)
#define HSU_CH_SR_CDESC_ANY GENMASK(31, 30)
#define HSU_CH_SR_DESCTO_ANY GENMASK(11, 8)
#define HSU_CH_SR_CHE BIT(15)
#define HSU_CH_DxTSR_MASK GENMASK(15, 0)

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

    /* Simple interrupt status shadow: mirror of HSU_PCI_DMAISR */
    uint32_t dmaisr;
    uint32_t dmasr;

    /* Per-channel status shadow for HSU_CH_SR. The Linux driver
     * computes hsu->nr_channels from the mapped IO length and then
     * accesses per-channel registers using:
     *   hsuc->reg = base + i * HSU_DMA_CHAN_LENGTH;
     *   hsu_chan_readl(hsuc, HSU_CH_SR);
     * Without any state here, all reads would return 0 and
     * hsu_dma_get_status() would fail with -EIO, preventing further
     * progress. We therefore maintain a simple software-visible SR
     * value per channel that can be set by the guest (e.g. test
     * code) via channel-specific registers or, in the absence of such
     * writes, remain 0.
     */
    uint32_t chan_sr[32];

    /* Number of channels derived from BAR size: this mirrors the
     * driver's computation (chip->length - chip->offset) / HSU_DMA_CHAN_LENGTH.
     * We know BAR0 size is 0x1000, offset is HSU_PCI_CHAN_OFFSET, so
     * nr_channels = (0x1000 - 0x100) / 0x40 = 56 / 4 = 22. But the
     * exact dynamic value is only used by the driver; for our emulation
     * we expose a fixed maximum and compute the same formula on the
     * guest side.
     */
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* IRQ line is asserted whenever any DMA interrupt status bit is set. */
    if (s->dmaisr) {
        if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!msi_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

/* Device-initiated DMA logic based on driver access patterns
 * The provided driver code does not program any global DMA engine
 * registers in this PCI wrapper, so we leave DMA inactive here.
 */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    (void)pdev;
    (void)is_write;
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = 0;

    /* Align to 32-bit accesses; driver uses readl/writel */
    switch (addr) {
    case HSU_PCI_DMASR:
        val = s->dmasr;
        break;
    case HSU_PCI_DMAISR:
        val = s->dmaisr;
        break;
    default:
        /* Channel-specific registers: the driver sets
         *   addr = chip->regs + chip->offset;
         *   hsuc->reg = addr + i * HSU_DMA_CHAN_LENGTH;
         *   hsu_chan_readl(hsuc, HSU_CH_SR);
         * So HSU_CH_SR is accessed at
         *   offset = HSU_PCI_CHAN_OFFSET + i * HSU_DMA_CHAN_LENGTH + HSU_CH_SR.
         * We emulate only HSU_CH_SR reads and keep other registers
         * returning 0 (idle) because their exact semantics are not
         * specified in the provided snippets.
         */
        if (addr >= HSU_PCI_CHAN_OFFSET) {
            hwaddr rel = addr - HSU_PCI_CHAN_OFFSET;
            unsigned int chan = rel / HSU_DMA_CHAN_LENGTH;
            hwaddr coff = rel % HSU_DMA_CHAN_LENGTH;

            if (chan < (sizeof(s->chan_sr) / sizeof(s->chan_sr[0])) && coff == HSU_CH_SR) {
                val = s->chan_sr[chan];
            } else {
                val = 0;
            }
        } else {
            /* For non-channel unknown offsets, return 0. */
            val = 0;
        }
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t v32 = (uint32_t)val;

    switch (addr) {
    case HSU_PCI_DMASR:
        /* treat DMASR as a writable shadow status/control register */
        s->dmasr = v32;
        break;
    case HSU_PCI_DMAISR:
        /* Typical W1C interrupt status: clear bits which are 1 in write */
        s->dmaisr &= ~v32;
        pcibase_update_irq(s);
        break;
    default:
        /* For channel-specific registers we generally do not implement
         * behavior since their semantics (e.g. descriptor programming,
         * transfer sizes) are not given here. However, to allow the
         * guest or test environment to inject interrupt-like events
         * that hsu_dma_get_status() can observe, one could in
         * principle define writes to a synthetic per-channel register
         * to update chan_sr[]. As the driver never writes HSU_CH_SR
         * itself, and the actual control registers are not used in the
         * snippets, we keep channel writes as no-ops to avoid
         * fabricating hardware behavior.
         */
        if (addr >= HSU_PCI_CHAN_OFFSET) {
            /* Ignore writes to channel space by default. */
            (void)v32;
        }
        break;
    }
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
    pci_device_reset(PCI_DEVICE(dev));

    /* Clear interrupt and status shadows */
    s->dmaisr = 0;
    s->dmasr = 0;

    /* Clear all per-channel status registers to idle. */
    memset(s->chan_sr, 0, sizeof(s->chan_sr));
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCIBASE_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCIBASE_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCIBASE_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* Initialize interrupt-related state */
    s->dmaisr = 0;
    s->dmasr = 0;

    /* Initialize per-channel status to idle (0). */
    memset(s->chan_sr, 0, sizeof(s->chan_sr));

    /* BAR Initialization: driver only requests BAR0 via BIT(0) */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x1000;
    s->bar_info[0].name = "hsu-dma-bar0";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = NULL;
    }

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
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
    .name = "hsu_dma_pci_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(dmaisr, PCIBaseState),
        VMSTATE_UINT32(dmasr, PCIBaseState),
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
