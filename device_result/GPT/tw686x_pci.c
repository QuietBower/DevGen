/*
 * QEMU PCI device model for Techwell TW686x (minimal MMIO/IRQ behavior
 * sufficient for Linux tw686x driver probing and basic operation).
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

/* NOTE: kernel headers are not actually used at build time in QEMU, but were
 * present in the template. Remove them to keep this file self-contained. */

#define TYPE_PCIBASE_DEVICE "tw686x_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define TW686X_DMA_MODE_CONTIG        1
#define TW686X_DMA_MODE_SG            2
#define TW686X_DMA_MODE_MEMCPY        0
#define DMA_CMD                       0x02
#define DMA_CHANNEL_ENABLE            0x0a
#define DMA_CMD_ENABLE                (1U << 31)
#define TW686X_FIFO_ERROR(x)          ((x) & ~(0xff))
#define INT_STATUS                    0x00
#define INT_STATUS_DMA_TOUT           (1U << 17)
#define VIDEO_FIFO_STATUS             0x03
#define PB_STATUS                     0x01
#define DMA_CONFIG                    0x0b
#define SYS_SOFT_RST                  0x06
#define DMA_TIMER_INTERVAL            0x0c
#define DMA_CHANNEL_TIMEOUT           0x0d
#define TYPE_SECOND_GEN               0x10
#define AUDIO_CHANNEL_OFFSET          8
#define TYPE_MAX_CHANNELS             0x0f
#define TW686X_VIDEO_WIDTH            720
#define TW686X_DEF_PHASE_REF          0x1518
#define TW686X_AUDIO_PAGE_MAX         16
#define TW686X_MAX_SG_DESC_COUNT      256
#define TW686X_FRAME_MODE             0x2
#define SYS_MODE_DMA_SHIFT            13
#define TW686X_SG_MODE                0x0
#define AUDIO_DMA_SIZE_MAX            (4 * 1024)
#define TW686X_VIDSTAT_VDLOSS         (1U << 7)
#define TW686X_INPUTS_PER_CH          4
#define TW686X_VIDSTAT_HLOCK          (1U << 6)
#define TW686X_STD_NTSC_443           3
#define TW686X_STD_PAL                1
#define TW686X_STD_PAL_60             6
#define TW686X_STD_SECAM              2
#define TW686X_STD_PAL_M              4
#define TW686X_STD_PAL_CN             5
#define TW686X_STD_NTSC_M             0
#define TW686X_MAX_SG_ENTRY_SIZE      4096

/* Minimal subset of other register offsets used in the driver. These are
 * taken from driver code patterns but their numeric values are not given in
 * the provided snippet, so we only model those that have explicit numeric
 * constants above. All indexed array-style registers (e.g. SRST[x],
 * VDELAY_LO[ch]) are left unmapped here as their actual offsets are not
 * defined in the snippet. The driver will still be able to probe since these
 * accesses occur after probe. */

/* First pci_device_id entry: PCI_VENDOR_ID_TECHWELL, 0x6864
 * The actual numeric value of PCI_VENDOR_ID_TECHWELL is defined in Linux,
 * but QEMU does not include that header. Replicate it here explicitly.
 */
#define PCI_VENDOR_ID_TECHWELL        0x1797
#define TW686X_VENDOR_ID              PCI_VENDOR_ID_TECHWELL
#define TW686X_DEVICE_ID              0x6864
#define TW686X_CLASS_ID               0x0400 /* PCI_CLASS_MULTIMEDIA_VIDEO */

/* Simple mask helper from kernel-like macro GENMASK, for internal use if
 * needed. Not used by the MMIO side but kept for completeness. */
#ifndef GENMASK
#define GENMASK(h, l) (((~0U) - (1U << (l)) + 1) & (~0U >> (31 - (h))))
#endif

/* BAR info */
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
    uint32_t intr_status; /* internal interrupt status */
    uint32_t intr_mask;   /* not used by driver snippet */

    /* Hardware Register Shadows (subset explicitly visible in driver) */
    uint32_t reg_int_status;          /* INT_STATUS (read-clear) */
    uint32_t reg_dma_cmd;             /* DMA_CMD */
    uint32_t reg_dma_channel_enable;  /* DMA_CHANNEL_ENABLE */
    uint32_t reg_dma_config;          /* DMA_CONFIG */
    uint32_t reg_sys_soft_rst;        /* SYS_SOFT_RST */
    uint32_t reg_dma_timer_interval;  /* DMA_TIMER_INTERVAL */
    uint32_t reg_dma_channel_timeout; /* DMA_CHANNEL_TIMEOUT */

    /* FIFO / PB status registers used in IRQ handler */
    uint32_t reg_video_fifo_status;   /* VIDEO_FIFO_STATUS */
    uint32_t reg_pb_status;           /* PB_STATUS */
};

/* Internal helper for status-triggered signaling. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* Simple level-triggered INTx based on any pending interrupt bit.
     * The driver does not program interrupt masks in the snippet provided,
     * so we consider all bits in intr_status as unmasked.
     */
    if (s->intr_status) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* Device-initiated DMA logic based on driver access patterns.
 * The provided driver snippet shows heavy usage of system-side copying and
 * SG/contig DMA descriptors, but the actual hardware descriptor format and
 * when the hardware performs DMA is not fully defined in the snippet.
 * To respect the "no hallucination" rule, we do not implement real DMA
 * transfers here. The driver primarily uses CPU copies from coherent buffers
 * it manages; hardware DMA is only needed to move data from the capture
 * engine to system memory. For probe and basic v4l2 registration to succeed,
 * no DMA is required. Thus this function is left as a stub.
 */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    (void)s;
    (void)is_write;
}

/* Helper to map byte address to 32-bit register offset index. All directly
 * defined constants in this file are in units assumed by the driver as
 * raw offsets passed to readl/writel. The driver uses small hex constants
 * like 0x02, 0x0a, etc., which in a real chip are byte offsets; here we
 * interpret MMIO address as byte offset and use these directly.
 */

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val32 = 0;

    /* The driver always uses 32-bit readl(), but accept 1/2/4/8 for safety. */
    if (size != 4 && size != 1 && size != 2 && size != 8) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "tw686x_pci: invalid read size %u at 0x%" HWADDR_PRIx "\n",
                      size, addr);
        return 0;
    }

    switch (addr) {
    case INT_STATUS:   /* 0x00 */
        /* INT_STATUS is documented by the driver as "cleared on read". */
        val32 = s->reg_int_status;
        s->reg_int_status = 0;
        /* Clear internal interrupt status as well since they are mapped. */
        s->intr_status = 0;
        pcibase_update_irq(s);
        break;
    case PB_STATUS:    /* 0x01 */
        val32 = s->reg_pb_status;
        break;
    case DMA_CMD:      /* 0x02 */
        val32 = s->reg_dma_cmd;
        break;
    case VIDEO_FIFO_STATUS: /* 0x03 */
        val32 = s->reg_video_fifo_status;
        break;
    case SYS_SOFT_RST: /* 0x06 */
        val32 = s->reg_sys_soft_rst;
        break;
    case DMA_CHANNEL_ENABLE: /* 0x0a */
        val32 = s->reg_dma_channel_enable;
        break;
    case DMA_CONFIG:   /* 0x0b */
        val32 = s->reg_dma_config;
        break;
    case DMA_TIMER_INTERVAL: /* 0x0c */
        val32 = s->reg_dma_timer_interval;
        break;
    case DMA_CHANNEL_TIMEOUT: /* 0x0d */
        val32 = s->reg_dma_channel_timeout;
        break;
    default:
        /* For all other addresses in BAR0, return 0. This covers the many
         * other registers used by the driver (e.g. SDT[x], VDMA_* etc.).
         * Their actual offsets are not provided, but returning 0 is safe and
         * avoids crashes. */
        val32 = 0;
        break;
    }

    /* Adjust return value for smaller or larger accesses. We model as
     * little-endian. */
    if (size == 1) {
        return (uint8_t)val32;
    } else if (size == 2) {
        return (uint16_t)val32;
    } else if (size == 4) {
        return val32;
    } else if (size == 8) {
        /* Hardware returns 32-bit; replicate in low dword, 0 in high. */
        return (uint64_t)val32;
    }

    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                               unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t v = (uint32_t)val;

    if (size != 4 && size != 1 && size != 2 && size != 8) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "tw686x_pci: invalid write size %u at 0x%" HWADDR_PRIx "\n",
                      size, addr);
        return;
    }

    /* For partial writes, update only the relevant portion of the 32-bit
     * register. This is conservative; the driver uses 32-bit writel(). */
    switch (addr) {
    case SYS_SOFT_RST: /* 0x06 */
        /* Driver writes 0x0f to reset subsystems. We just store the value. */
        s->reg_sys_soft_rst = v;
        break;
    case DMA_CMD:      /* 0x02 */
        s->reg_dma_cmd = v;
        break;
    case DMA_CHANNEL_ENABLE: /* 0x0a */
        s->reg_dma_channel_enable = v;
        break;
    case DMA_CONFIG:   /* 0x0b */
        s->reg_dma_config = v;
        break;
    case DMA_TIMER_INTERVAL: /* 0x0c */
        s->reg_dma_timer_interval = v;
        break;
    case DMA_CHANNEL_TIMEOUT: /* 0x0d */
        s->reg_dma_channel_timeout = v;
        break;
    case PB_STATUS:    /* 0x01 */
        /* Treat as a simple writable shadow, though driver only reads it.
         * This can be used by test code to inject conditions. */
        s->reg_pb_status = v;
        break;
    case VIDEO_FIFO_STATUS: /* 0x03 */
        /* Writable for test flexibility, although driver only reads. */
        s->reg_video_fifo_status = v;
        break;
    case INT_STATUS:   /* 0x00 */
        /* The driver never writes INT_STATUS in the provided snippet.
         * In case it does, allow software to clear specific bits by writing
         * 1s (W1C style). */
        s->reg_int_status &= ~v;
        s->intr_status &= ~v;
        pcibase_update_irq(s);
        break;
    default:
        /* All other register writes are ignored (write-only or unmodeled
         * registers). This is sufficient for driver probe and for most
         * configuration operations that are opaque to hardware here. */
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    (void)opaque;
    (void)addr;
    (void)size;
    /* Device does not expose legacy PIO in the snippet; return 0. */
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val,
                              unsigned size)
{
    (void)opaque;
    (void)addr;
    (void)val;
    (void)size;
    /* No legacy PIO behavior modeled. */
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

    /* Revert registers to power-on defaults (all zeros). */
    s->reg_int_status = 0;
    s->reg_dma_cmd = 0;
    s->reg_dma_channel_enable = 0;
    s->reg_dma_config = 0;
    s->reg_sys_soft_rst = 0;
    s->reg_dma_timer_interval = 0;
    s->reg_dma_channel_timeout = 0;
    s->reg_video_fifo_status = 0;
    s->reg_pb_status = 0;
    s->intr_status = 0;
    s->intr_mask = 0;

    pcibase_update_irq(s);
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s,
                                 BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    /* PCI requires BAR sizes to be a power of 2. Prevent QEMU assert crash. */
    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s,
                              bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s,
                              bi->name, aligned_size);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  TW686X_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  TW686X_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, TW686X_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1); /* INTA# */

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0,
                                    PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR Initialization: one MMIO BAR used by driver. Size 0x1000 is
     * sufficient for all documented registers in the snippet. */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x1000;
    s->bar_info[0].name = "tw686x-bar0";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = "";
    }
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No MSI/MSI-X used by the driver snippet; keep INTx only. */
    s->has_msi = false;
    s->has_msix = false;

    /* Initialize interrupt and register state. */
    s->intr_status = 0;
    s->intr_mask = 0;
    s->reg_int_status = 0;
    s->reg_dma_cmd = 0;
    s->reg_dma_channel_enable = 0;
    s->reg_dma_config = 0;
    s->reg_sys_soft_rst = 0;
    s->reg_dma_timer_interval = 0;
    s->reg_dma_channel_timeout = 0;
    s->reg_video_fifo_status = 0;
    s->reg_pb_status = 0;
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    (void)s;

    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }

    /* No additional dynamic resources to free. */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "tw686x_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(reg_int_status, PCIBaseState),
        VMSTATE_UINT32(reg_dma_cmd, PCIBaseState),
        VMSTATE_UINT32(reg_dma_channel_enable, PCIBaseState),
        VMSTATE_UINT32(reg_dma_config, PCIBaseState),
        VMSTATE_UINT32(reg_sys_soft_rst, PCIBaseState),
        VMSTATE_UINT32(reg_dma_timer_interval, PCIBaseState),
        VMSTATE_UINT32(reg_dma_channel_timeout, PCIBaseState),
        VMSTATE_UINT32(reg_video_fifo_status, PCIBaseState),
        VMSTATE_UINT32(reg_pb_status, PCIBaseState),
        VMSTATE_UINT32(intr_status, PCIBaseState),
        VMSTATE_UINT32(intr_mask, PCIBaseState),
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

