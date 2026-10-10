/*
 * QEMU PCI device model for PLX DMA (implementation phase).
 * Auto-generated based on Linux driver static and behavioral information.
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

#define TYPE_PCIBASE_DEVICE "plx_dma_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */

/* PLX DMA register offsets */
#define PLX_REG_DESC_RING_ADDR            0x214
#define PLX_REG_DESC_RING_ADDR_HI         0x218
#define PLX_REG_DESC_RING_NEXT_ADDR       0x21C
#define PLX_REG_DESC_RING_COUNT           0x220
#define PLX_REG_DESC_RING_LAST_ADDR       0x224
#define PLX_REG_DESC_RING_LAST_SIZE       0x228
#define PLX_REG_PREF_LIMIT                0x234
#define PLX_REG_CTRL                      0x238
#define PLX_REG_CTRL2                     0x23A
#define PLX_REG_INTR_CTRL                 0x23C
#define PLX_REG_INTR_STATUS               0x23E

#define PLX_REG_PREF_LIMIT_PREF_FOUR      8

#define PLX_REG_CTRL_GRACEFUL_PAUSE       (1U << 0)
#define PLX_REG_CTRL_ABORT                (1U << 1)
#define PLX_REG_CTRL_WRITE_BACK_EN        (1U << 2)
#define PLX_REG_CTRL_START                (1U << 3)
#define PLX_REG_CTRL_RING_STOP_MODE       (1U << 4)
#define PLX_REG_CTRL_DESC_MODE_BLOCK      (0U << 5)
#define PLX_REG_CTRL_DESC_MODE_ON_CHIP    (1U << 5)
#define PLX_REG_CTRL_DESC_MODE_OFF_CHIP   (2U << 5)
#define PLX_REG_CTRL_DESC_INVALID         (1U << 8)
#define PLX_REG_CTRL_GRACEFUL_PAUSE_DONE  (1U << 9)
#define PLX_REG_CTRL_ABORT_DONE           (1U << 10)
#define PLX_REG_CTRL_IMM_PAUSE_DONE       (1U << 12)
#define PLX_REG_CTRL_IN_PROGRESS          (1U << 30)

#define PLX_REG_CTRL_RESET_VAL ( \
    PLX_REG_CTRL_DESC_INVALID | \
    PLX_REG_CTRL_GRACEFUL_PAUSE_DONE | \
    PLX_REG_CTRL_ABORT_DONE | \
    PLX_REG_CTRL_IMM_PAUSE_DONE)

#define PLX_REG_CTRL_START_VAL ( \
    PLX_REG_CTRL_WRITE_BACK_EN | \
    PLX_REG_CTRL_DESC_MODE_OFF_CHIP | \
    PLX_REG_CTRL_START | \
    PLX_REG_CTRL_RESET_VAL)

#define PLX_REG_CTRL2_MAX_TXFR_SIZE_64B   0
#define PLX_REG_CTRL2_MAX_TXFR_SIZE_128B  1
#define PLX_REG_CTRL2_MAX_TXFR_SIZE_256B  2
#define PLX_REG_CTRL2_MAX_TXFR_SIZE_512B  3
#define PLX_REG_CTRL2_MAX_TXFR_SIZE_1KB   4
#define PLX_REG_CTRL2_MAX_TXFR_SIZE_2KB   5
#define PLX_REG_CTRL2_MAX_TXFR_SIZE_4B    7

#define PLX_REG_INTR_CRTL_ERROR_EN            (1U << 0)
#define PLX_REG_INTR_CRTL_INV_DESC_EN         (1U << 1)
#define PLX_REG_INTR_CRTL_ABORT_DONE_EN       (1U << 3)
#define PLX_REG_INTR_CRTL_PAUSE_DONE_EN       (1U << 4)
#define PLX_REG_INTR_CRTL_IMM_PAUSE_DONE_EN   (1U << 5)

#define PLX_REG_INTR_STATUS_ERROR         (1U << 0)
#define PLX_REG_INTR_STATUS_INV_DESC      (1U << 1)
#define PLX_REG_INTR_STATUS_DESC_DONE     (1U << 2)
#define PLX_REG_INTR_CRTL_ABORT_DONE      (1U << 3)

#define PLX_DESC_SIZE_MASK        0x7ffffffU
#define PLX_DESC_FLAG_VALID       (1U << 31)
#define PLX_DESC_FLAG_INT_WHEN_DONE   (1U << 30)
#define PLX_DESC_WB_SUCCESS       (1U << 30)
#define PLX_DESC_WB_RD_FAIL       (1U << 29)
#define PLX_DESC_WB_WR_FAIL       (1U << 28)

#define PLX_DMA_RING_COUNT        2048

/* Hardware descriptor layout from driver */
struct plx_dma_hw_std_desc {
    uint32_t flags_and_size;
    uint16_t dst_addr_hi;
    uint16_t src_addr_hi;
    uint32_t dst_addr_lo;
    uint32_t src_addr_lo;
};

/* PCI IDs - first entry of plx_dma_pci_tbl[] */
/* Vendor is PCI_VENDOR_ID_PLX from kernel headers; device is 0x87D0. */
#define PLX_DMA_VENDOR_ID  0x10b5
#define PLX_DMA_DEVICE_ID  0x87d0
#define PLX_DMA_CLASS_ID   0x0880  /* PCI_CLASS_SYSTEM_OTHER */

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
    uint32_t reg_desc_ring_addr;
    uint32_t reg_desc_ring_addr_hi;
    uint32_t reg_desc_ring_next_addr;
    uint32_t reg_desc_ring_count;
    uint32_t reg_desc_ring_last_addr;
    uint32_t reg_desc_ring_last_size;
    uint32_t reg_pref_limit;
    uint32_t reg_ctrl;
    uint16_t reg_ctrl2;
    uint16_t reg_intr_ctrl;
    uint16_t reg_intr_status;

    /* DMA ring state */
    dma_addr_t ring_dma_addr;
    dma_addr_t ring_dma_next;
    uint32_t   ring_entries;

    /* QEMU timer to simulate DMA completion */
    QEMUTimer dma_timer;
    bool dma_running;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    if (!s->reg_intr_status) {
        pci_set_irq(pdev, 0);
        return;
    }

    if (msix_enabled(pdev)) {
        msix_notify(pdev, 0);
    } else if (msi_enabled(pdev)) {
        msi_notify(pdev, 0);
    } else {
        pci_set_irq(pdev, 1);
    }
}

/* Device-initiated DMA logic based on driver access patterns */
static void pcibase_dma_timer_cb(void *opaque)
{
    PCIBaseState *s = opaque;
    PCIDevice *pdev = PCI_DEVICE(s);

    if (!s->dma_running) {
        return;
    }

    if (!s->ring_dma_addr || !s->ring_entries) {
        s->dma_running = false;
        return;
    }

    /* Map ring as an array of plx_dma_hw_std_desc */
    size_t ring_sz = (size_t)s->ring_entries * sizeof(struct plx_dma_hw_std_desc);
    struct plx_dma_hw_std_desc *ring = g_malloc0(ring_sz);

    if (!ring) {
        s->dma_running = false;
        return;
    }

    pci_dma_read(pdev, s->ring_dma_addr, ring, ring_sz);

    bool any_int = false;

    for (uint32_t i = 0; i < s->ring_entries; i++) {
        struct plx_dma_hw_std_desc *d = &ring[i];
        uint32_t flags_size = le32_to_cpu(d->flags_and_size);

        if (!(flags_size & PLX_DESC_FLAG_VALID)) {
            continue;
        }

        uint32_t size = flags_size & PLX_DESC_SIZE_MASK;
        if (!size) {
            continue;
        }

        uint64_t src = ((uint64_t)le16_to_cpu(d->src_addr_hi) << 32) |
                        le32_to_cpu(d->src_addr_lo);
        uint64_t dst = ((uint64_t)le16_to_cpu(d->dst_addr_hi) << 32) |
                        le32_to_cpu(d->dst_addr_lo);

        uint8_t *buf = g_malloc(size);
        if (!buf) {
            continue;
        }

        pci_dma_read(pdev, src, buf, size);
        pci_dma_write(pdev, dst, buf, size);
        g_free(buf);

        /* Clear VALID and set SUCCESS */
        flags_size &= ~PLX_DESC_FLAG_VALID;
        flags_size &= ~PLX_DESC_SIZE_MASK;
        flags_size |= (PLX_DESC_WB_SUCCESS | size);
        d->flags_and_size = cpu_to_le32(flags_size);

        if (flags_size & PLX_DESC_FLAG_INT_WHEN_DONE) {
            any_int = true;
        }
    }

    pci_dma_write(pdev, s->ring_dma_addr, ring, ring_sz);
    g_free(ring);

    s->dma_running = false;

    if (any_int) {
        s->reg_intr_status |= PLX_REG_INTR_STATUS_DESC_DONE;
        pcibase_update_irq(s);
    }
}

G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /* In this model, DMA is always read+write copy; direction parameter unused. */
    (void)is_write;

    if (!s->ring_dma_addr || !s->ring_entries) {
        return;
    }

    if (s->dma_running) {
        return;
    }

    s->dma_running = true;
    timer_mod(&s->dma_timer,
              qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 1);
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case PLX_REG_DESC_RING_ADDR:
        val = s->reg_desc_ring_addr;
        break;
    case PLX_REG_DESC_RING_ADDR_HI:
        val = s->reg_desc_ring_addr_hi;
        break;
    case PLX_REG_DESC_RING_NEXT_ADDR:
        val = s->reg_desc_ring_next_addr;
        break;
    case PLX_REG_DESC_RING_COUNT:
        val = s->reg_desc_ring_count;
        break;
    case PLX_REG_DESC_RING_LAST_ADDR:
        val = s->reg_desc_ring_last_addr;
        break;
    case PLX_REG_DESC_RING_LAST_SIZE:
        val = s->reg_desc_ring_last_size;
        break;
    case PLX_REG_PREF_LIMIT:
        val = s->reg_pref_limit;
        break;
    case PLX_REG_CTRL:
        /* CTRL is 32-bit; driver uses readl */
        val = s->reg_ctrl;
        break;
    case PLX_REG_CTRL2:
        /* CTRL2 is 16-bit (writew/readw not seen but declared as 0x23A) */
        val = s->reg_ctrl2;
        break;
    case PLX_REG_INTR_CTRL:
        val = s->reg_intr_ctrl;
        break;
    case PLX_REG_INTR_STATUS:
        val = s->reg_intr_status;
        break;
    default:
        val = 0;
        break;
    }

    /* Mask value according to access size */
    if (size == 1) {
        val &= 0xff;
    } else if (size == 2) {
        val &= 0xffff;
    } else if (size == 4) {
        val &= 0xffffffffU;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    uint32_t v32 = (uint32_t)val;
    uint16_t v16 = (uint16_t)val;

    switch (addr) {
    case PLX_REG_DESC_RING_ADDR:
        s->reg_desc_ring_addr = v32;
        s->ring_dma_addr = (s->ring_dma_addr & 0xffffffff00000000ULL) |
                           (uint64_t)s->reg_desc_ring_addr;
        break;
    case PLX_REG_DESC_RING_ADDR_HI:
        s->reg_desc_ring_addr_hi = v32;
        s->ring_dma_addr = ((uint64_t)s->reg_desc_ring_addr_hi << 32) |
                           (uint64_t)s->reg_desc_ring_addr;
        break;
    case PLX_REG_DESC_RING_NEXT_ADDR:
        s->reg_desc_ring_next_addr = v32;
        s->ring_dma_next = ((uint64_t)s->reg_desc_ring_addr_hi << 32) |
                           (uint64_t)s->reg_desc_ring_next_addr;
        break;
    case PLX_REG_DESC_RING_COUNT:
        s->reg_desc_ring_count = v32;
        s->ring_entries = v32;
        break;
    case PLX_REG_DESC_RING_LAST_ADDR:
        s->reg_desc_ring_last_addr = v32;
        break;
    case PLX_REG_DESC_RING_LAST_SIZE:
        s->reg_desc_ring_last_size = v32;
        break;
    case PLX_REG_PREF_LIMIT:
        s->reg_pref_limit = v32;
        break;
    case PLX_REG_CTRL:
        /* CTRL is 32-bit and accessed with writel and writew. */
        if (size == 2) {
            /* writew: lower 16 bits only (as used for START_VAL) */
            s->reg_ctrl &= ~0xffffU;
            s->reg_ctrl |= v16;
        } else {
            s->reg_ctrl = v32;
        }

        /* Handle graceful pause: when driver writes GRACEFUL_PAUSE, set DONE. */
        if (s->reg_ctrl & PLX_REG_CTRL_GRACEFUL_PAUSE) {
            s->reg_ctrl |= PLX_REG_CTRL_GRACEFUL_PAUSE_DONE;
        }

        /* START bit: trigger DMA */
        if (s->reg_ctrl & PLX_REG_CTRL_START) {
            pcibase_do_dma(s, false);
        }
        break;
    case PLX_REG_CTRL2:
        s->reg_ctrl2 = v16;
        break;
    case PLX_REG_INTR_CTRL:
        s->reg_intr_ctrl = v16;
        break;
    case PLX_REG_INTR_STATUS:
        /* ISR writes status back: treat as W1C */
        s->reg_intr_status &= ~v16;
        pcibase_update_irq(s);
        break;
    default:
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

    s->reg_desc_ring_addr = 0;
    s->reg_desc_ring_addr_hi = 0;
    s->reg_desc_ring_next_addr = 0;
    s->reg_desc_ring_count = 0;
    s->reg_desc_ring_last_addr = 0;
    s->reg_desc_ring_last_size = 0;
    s->reg_pref_limit = 0;
    s->reg_ctrl = PLX_REG_CTRL_RESET_VAL;
    s->reg_ctrl2 = 0;
    s->reg_intr_ctrl = 0;
    s->reg_intr_status = 0;

    s->ring_dma_addr = 0;
    s->ring_dma_next = 0;
    s->ring_entries = 0;

    s->dma_running = false;
    timer_del(&s->dma_timer);

    pcibase_update_irq(s);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PLX_DMA_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PLX_DMA_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PLX_DMA_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* Initialize MSI/MSI-X as the driver uses pci_alloc_irq_vectors(PCI_IRQ_ALL_TYPES). */
    if (msix_init_exclusive_bar(pdev, 1, 1, errp) == 0) {
        s->has_msix = true;
    } else if (msi_init(pdev, 0, 1, true, false, errp) == 0) {
        s->has_msi = true;
    }

    /* BAR Initialization */
    /* Driver uses a single MMIO BAR via pci_iomap(), but size is not explicit. */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    s->bar_info[0].size  = 0x1000; /* conservative page-sized BAR; may be updated later if needed */
    s->bar_info[0].name  = "plx-dma-bar0";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize internal state */
    s->reg_desc_ring_addr = 0;
    s->reg_desc_ring_addr_hi = 0;
    s->reg_desc_ring_next_addr = 0;
    s->reg_desc_ring_count = 0;
    s->reg_desc_ring_last_addr = 0;
    s->reg_desc_ring_last_size = 0;
    s->reg_pref_limit = 0;
    s->reg_ctrl = PLX_REG_CTRL_RESET_VAL;
    s->reg_ctrl2 = 0;
    s->reg_intr_ctrl = 0;
    s->reg_intr_status = 0;

    s->ring_dma_addr = 0;
    s->ring_dma_next = 0;
    s->ring_entries = 0;

    s->dma_running = false;
    timer_init_ms(&s->dma_timer, QEMU_CLOCK_VIRTUAL, pcibase_dma_timer_cb, s);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    timer_del(&s->dma_timer);

    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "plx_dma_pci",
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
