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

/* Register Layout and Hardware Identifiers extracted from driver source */
#define BIT(n) (1 << (n))

#define PCI_VENDOR_ID_PLX 0x10b5

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

#define PLX_REG_CTRL_GRACEFUL_PAUSE       BIT(0)
#define PLX_REG_CTRL_ABORT                BIT(1)
#define PLX_REG_CTRL_WRITE_BACK_EN        BIT(2)
#define PLX_REG_CTRL_START                BIT(3)
#define PLX_REG_CTRL_RING_STOP_MODE       BIT(4)
#define PLX_REG_CTRL_DESC_MODE_BLOCK      (0 << 5)
#define PLX_REG_CTRL_DESC_MODE_ON_CHIP    (1 << 5)
#define PLX_REG_CTRL_DESC_MODE_OFF_CHIP   (2 << 5)
#define PLX_REG_CTRL_DESC_INVALID         BIT(8)
#define PLX_REG_CTRL_GRACEFUL_PAUSE_DONE  BIT(9)
#define PLX_REG_CTRL_ABORT_DONE           BIT(10)
#define PLX_REG_CTRL_IMM_PAUSE_DONE       BIT(12)
#define PLX_REG_CTRL_IN_PROGRESS          BIT(30)
#define PLX_REG_CTRL_RESET_VAL            (PLX_REG_CTRL_DESC_INVALID | \
                                         PLX_REG_CTRL_GRACEFUL_PAUSE_DONE | \
                                         PLX_REG_CTRL_ABORT_DONE | \
                                         PLX_REG_CTRL_IMM_PAUSE_DONE)
#define PLX_REG_CTRL_START_VAL            (PLX_REG_CTRL_WRITE_BACK_EN | \
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

#define PLX_REG_INTR_CRTL_ERROR_EN        BIT(0)
#define PLX_REG_INTR_CRTL_INV_DESC_EN     BIT(1)
#define PLX_REG_INTR_CRTL_ABORT_DONE_EN   BIT(3)
#define PLX_REG_INTR_CRTL_PAUSE_DONE_EN   BIT(4)
#define PLX_REG_INTR_CRTL_IMM_PAUSE_DONE_EN BIT(5)
#define PLX_REG_INTR_STATUS_ERROR         BIT(0)
#define PLX_REG_INTR_STATUS_INV_DESC      BIT(1)
#define PLX_REG_INTR_STATUS_DESC_DONE     BIT(2)
#define PLX_REG_INTR_CRTL_ABORT_DONE      BIT(3)

#define PLX_DESC_SIZE_MASK                0x7ffffff
#define PLX_DESC_FLAG_VALID               BIT(31)
#define PLX_DESC_FLAG_INT_WHEN_DONE       BIT(30)
#define PLX_DESC_WB_SUCCESS               BIT(30)
#define PLX_DESC_WB_RD_FAIL               BIT(29)
#define PLX_DESC_WB_WR_FAIL               BIT(28)
#define PLX_DMA_RING_COUNT                2048

#define TYPE_PCIBASE_DEVICE "plx_dma_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

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
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    struct PlxRegs {
        uint32_t desc_ring_addr;
        uint32_t desc_ring_addr_hi;
        uint32_t desc_ring_next_addr;
        uint32_t desc_ring_count;
        uint32_t desc_ring_last_addr;
        uint32_t desc_ring_last_size;
        uint32_t pref_limit;
        uint32_t ctrl;
        uint16_t ctrl2;
        uint16_t intr_ctrl_reg;
        uint16_t intr_status_reg;
    } regs;

    /* DMA Engine State */
    dma_addr_t ring_base_addr;     /* 64-bit base physical address */
    uint32_t ring_count;           /* number of descriptors */
    dma_addr_t current_desc_addr;  /* next descriptor to process */
    bool engine_running;           /* start bit active */
    bool pause_requested;
    bool abort_requested;
    QEMUTimer *dma_timer;          /* timer for descriptor processing */

    /* Operational status flags */
    uint32_t status;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    if (s->regs.intr_status_reg) {
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

static void plx_dma_timer(void *opaque)
{
    PCIBaseState *s = opaque;
    PCIDevice *pdev = PCI_DEVICE(s);

    struct plx_desc_hw {
        uint32_t dst_addr_lo;
        uint16_t dst_addr_hi;
        uint32_t src_addr_lo;
        uint16_t src_addr_hi;
        uint32_t flags_and_size;
    } __attribute__((packed)) desc;

    if (!s->engine_running) {
        return;
    }

    if (s->abort_requested) {
        s->engine_running = false;
        s->regs.ctrl &= ~PLX_REG_CTRL_IN_PROGRESS;
        s->regs.ctrl |= PLX_REG_CTRL_ABORT_DONE;
        s->abort_requested = false;
        pcibase_update_irq(s);
        return;
    }

    /* Read descriptor from guest memory */
    pci_dma_read(pdev, s->current_desc_addr, &desc, sizeof(desc));
    le32_to_cpus(&desc.flags_and_size);

    if (!(desc.flags_and_size & PLX_DESC_FLAG_VALID)) {
        /* No valid descriptor, stop engine */
        s->engine_running = false;
        s->regs.ctrl &= ~PLX_REG_CTRL_IN_PROGRESS;
        if (s->pause_requested) {
            s->regs.ctrl |= PLX_REG_CTRL_GRACEFUL_PAUSE_DONE;
            s->pause_requested = false;
        }
        return;
    }

    uint32_t size = desc.flags_and_size & PLX_DESC_SIZE_MASK;
    bool int_on_done = !!(desc.flags_and_size & PLX_DESC_FLAG_INT_WHEN_DONE);

    dma_addr_t src = ((uint64_t)desc.src_addr_hi << 32) | desc.src_addr_lo;
    dma_addr_t dst = ((uint64_t)desc.dst_addr_hi << 32) | desc.dst_addr_lo;

    /* Perform DMA copy */
    uint8_t *buf = g_malloc(size);
    pci_dma_read(pdev, src, buf, size);
    pci_dma_write(pdev, dst, buf, size);
    g_free(buf);

    /* Update descriptor in guest memory: clear valid, set success, residual 0 */
    desc.flags_and_size = cpu_to_le32(PLX_DESC_WB_SUCCESS);
    pci_dma_write(pdev, s->current_desc_addr + offsetof(struct plx_desc_hw, flags_and_size),
                  &desc.flags_and_size, sizeof(desc.flags_and_size));

    /* Advance current_desc_addr */
    dma_addr_t ring_end = s->ring_base_addr + (uint64_t)s->ring_count * sizeof(desc);
    s->current_desc_addr += sizeof(desc);
    if (s->current_desc_addr >= ring_end) {
        s->current_desc_addr = s->ring_base_addr;
    }

    /* Interrupt on done if requested */
    if (int_on_done) {
        s->regs.intr_status_reg |= PLX_REG_INTR_STATUS_DESC_DONE;
        pcibase_update_irq(s);
    }

    /* Handle graceful pause */
    if (s->pause_requested) {
        s->engine_running = false;
        s->regs.ctrl &= ~PLX_REG_CTRL_IN_PROGRESS;
        s->regs.ctrl |= PLX_REG_CTRL_GRACEFUL_PAUSE_DONE;
        s->pause_requested = false;
        return;
    }

    /* Continue processing if engine still running */
    if (s->engine_running) {
        timer_mod(s->dma_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 100);
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case PLX_REG_DESC_RING_ADDR:
        val = s->regs.desc_ring_addr;
        break;
    case PLX_REG_DESC_RING_ADDR_HI:
        val = s->regs.desc_ring_addr_hi;
        break;
    case PLX_REG_DESC_RING_NEXT_ADDR:
        val = (uint32_t)(s->current_desc_addr & 0xFFFFFFFF);
        break;
    case PLX_REG_DESC_RING_COUNT:
        val = s->regs.desc_ring_count;
        break;
    case PLX_REG_DESC_RING_LAST_ADDR:
        val = s->regs.desc_ring_last_addr;
        break;
    case PLX_REG_DESC_RING_LAST_SIZE:
        val = s->regs.desc_ring_last_size;
        break;
    case PLX_REG_PREF_LIMIT:
        val = s->regs.pref_limit;
        break;
    case PLX_REG_CTRL:
        if (size == 4) {
            val = s->regs.ctrl;
        } else {
            /* 16-bit access? Not expected, but return lower 16 bits */
            val = s->regs.ctrl & 0xFFFF;
        }
        break;
    case PLX_REG_CTRL2:
        val = s->regs.ctrl2;
        break;
    case PLX_REG_INTR_CTRL:
        val = s->regs.intr_ctrl_reg;
        break;
    case PLX_REG_INTR_STATUS:
        val = s->regs.intr_status_reg;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "plx_dma: unknown MMIO read at 0x%" HWADDR_PRIx "\n", addr);
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case PLX_REG_DESC_RING_ADDR:
        if (size == 4) {
            s->regs.desc_ring_addr = val;
            s->ring_base_addr = (s->ring_base_addr & ~0xFFFFFFFFULL) | val;
        }
        break;
    case PLX_REG_DESC_RING_ADDR_HI:
        if (size == 4) {
            s->regs.desc_ring_addr_hi = val;
            s->ring_base_addr = (s->ring_base_addr & 0xFFFFFFFFULL) | ((uint64_t)val << 32);
        }
        break;
    case PLX_REG_DESC_RING_NEXT_ADDR:
        if (size == 4) {
            s->regs.desc_ring_next_addr = val;
            s->current_desc_addr = (s->current_desc_addr & ~0xFFFFFFFFULL) | val;
        }
        break;
    case PLX_REG_DESC_RING_COUNT:
        if (size == 4) {
            s->regs.desc_ring_count = val;
            s->ring_count = val;
        }
        break;
    case PLX_REG_DESC_RING_LAST_ADDR:
        if (size == 4) {
            s->regs.desc_ring_last_addr = val;
        }
        break;
    case PLX_REG_DESC_RING_LAST_SIZE:
        if (size == 4) {
            s->regs.desc_ring_last_size = val;
        }
        break;
    case PLX_REG_PREF_LIMIT:
        if (size == 4) {
            s->regs.pref_limit = val;
        }
        break;
    case PLX_REG_CTRL:
        if (size == 4) {
            /* Write-1-to-clear status bits */
            if (val & PLX_REG_CTRL_DESC_INVALID)        s->regs.ctrl &= ~PLX_REG_CTRL_DESC_INVALID;
            if (val & PLX_REG_CTRL_GRACEFUL_PAUSE_DONE) s->regs.ctrl &= ~PLX_REG_CTRL_GRACEFUL_PAUSE_DONE;
            if (val & PLX_REG_CTRL_ABORT_DONE)          s->regs.ctrl &= ~PLX_REG_CTRL_ABORT_DONE;
            if (val & PLX_REG_CTRL_IMM_PAUSE_DONE)      s->regs.ctrl &= ~PLX_REG_CTRL_IMM_PAUSE_DONE;

            /* Start bit: 0->1 starts engine, 1->0 is ignored while running */
            if (val & PLX_REG_CTRL_START) {
                if (!(s->regs.ctrl & PLX_REG_CTRL_START)) {
                    s->regs.ctrl |= PLX_REG_CTRL_START | PLX_REG_CTRL_IN_PROGRESS;
                    s->engine_running = true;
                    s->pause_requested = false;
                    s->abort_requested = false;
                    timer_mod(s->dma_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 100);
                }
            } else {
                /* Writing 0 to start bit doesn't stop engine if pause/abort pending */
                s->regs.ctrl &= ~PLX_REG_CTRL_START;
            }

            /* Abort: writing 1 triggers abort */
            if (val & PLX_REG_CTRL_ABORT) {
                s->abort_requested = true;
            }

            /* Graceful pause: writing 1 triggers pause */
            if (val & PLX_REG_CTRL_GRACEFUL_PAUSE) {
                s->pause_requested = true;
            }

            /* Other writable bits (WRITE_BACK_EN, RING_STOP_MODE, DESC_MODE) */
            s->regs.ctrl = (s->regs.ctrl & ~0xFF) | (val & 0xFF);
        }
        break;
    case PLX_REG_CTRL2:
        if (size == 2) {
            s->regs.ctrl2 = val;
        }
        break;
    case PLX_REG_INTR_CTRL:
        if (size == 2) {
            s->regs.intr_ctrl_reg = val;
            pcibase_update_irq(s);
        }
        break;
    case PLX_REG_INTR_STATUS:
        if (size == 2) {
            s->regs.intr_status_reg &= ~(val & 0xFFFF);
            pcibase_update_irq(s);
        }
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "plx_dma: unknown MMIO write at 0x%" HWADDR_PRIx "\n", addr);
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

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    memset(&s->regs, 0, sizeof(s->regs));
    s->regs.ctrl = PLX_REG_CTRL_RESET_VAL;
    s->ring_base_addr = 0;
    s->ring_count = 0;
    s->current_desc_addr = 0;
    s->engine_running = false;
    s->pause_requested = false;
    s->abort_requested = false;
    timer_del(s->dma_timer);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x10b5 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x87D0 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x8000 );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* Set base class separately (System Other, 0x08) */
    pci_set_byte(pci_conf + PCI_CLASS_DEVICE + 1, 0x08);

    /* BAR 0: MMIO, 4KB, required by driver */
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo) {
        .index = 0,
        .type = BAR_TYPE_MMIO,
        .size = 4096,
        .name = "plx-dma-bar0"
    };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* DMA Configuration: enable MSI */
    if (msi_init(pdev, 0, 1, true, false, errp) < 0) {
        /* INTx fallback - already enabled with pin */
    }

    /* Create DMA timer */
    s->dma_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, plx_dma_timer, s);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    timer_del(s->dma_timer);
    timer_free(s->dma_timer);

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
