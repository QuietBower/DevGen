/*
 * QEMU 8.2.10 PCI device model for qtnfmac_pcie driver
 * Based on vanilla QEMU PCI template. All placeholders replaced.
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

/* BAR indices */
#define QTN_SYSCTL_BAR 0
#define QTN_SHMEM_BAR   2
#define QTN_DMA_BAR     3

/* Timing constants */
#define QTN_EP_RESET_WAIT_MS  1000
#define QTN_FW_DL_TIMEOUT_MS  3000

#define TYPE_PCIBASE_DEVICE "qtnfmac_pcie_pci"
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

/* Chip ID values (derived from QTN_CHIP_ID_MASK) */
#define QTN_CHIP_ID_TOPAZ  0x04

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

    /* DMA: tx/rx ring base addresses, indices, and control registers */
    struct {
        hwaddr tx_ring_base;
        hwaddr rx_ring_base;
        uint32_t tx_widx;
        uint32_t tx_ridx;
        uint32_t rx_widx;
        uint32_t rx_ridx;
    } dma;

    uint32_t hw_status; /* Status register */
    uint32_t chip_id;   /* Chip ID (TOPAZ = 0x04) */
    bool reset_active;  /* Reset state flag */
    bool flashboot;     /* Whether to boot from flash */
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->intr_status & ~s->intr_mask) {
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

/* MMIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case 0x00: /* Chip ID register (formerly hardcoded, now consistent with 0x14) */
        val = (uint64_t)s->chip_id << 4;
        break;
    case 0x10: /* Interrupt Status */
        val = s->intr_status;
        break;
    case 0x14: /* QTN_REG_SYS_CTRL_CSR: chip ID register, reads (id << 4) */
        val = (uint64_t)s->chip_id << 4;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "MMIO read at " HWADDR_FMT_plx " size %d\n", addr, size);
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case 0x04: /* Command register - non-posted write flush */
        /* Simply acknowledge the write */
        break;
    case 0x10: /* Interrupt Status - W1C */
        s->intr_status &= ~(val & 0xFFFFFFFF);
        pcibase_update_irq(s);
        break;
    case 0x14: /* Interrupt Mask */
        s->intr_mask = val;
        pcibase_update_irq(s);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "MMIO write at " HWADDR_FMT_plx " size %d val 0x%"PRIx64"\n", addr, size, val);
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
    s->intr_status = 0;
    s->intr_mask = 0;
    s->chip_id = QTN_CHIP_ID_TOPAZ; /* remain TOPAZ */
    s->reset_active = false;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x1bb5);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x0008);
    /* Set class code: network controller (0x02), other (0x80), programming interface 0x00 */
    pci_set_byte(pci_conf + PCI_CLASS_DEVICE - 1, 0x00); /* prog-if */
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0280); /* sub=0x80, base=0x02 */
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR Initialization */
    s->num_bars = 3;
    s->bar_info[0] = (BARInfo){ .index = QTN_SYSCTL_BAR, .type = BAR_TYPE_MMIO, .size = 4096, .name = "sysctl" };
    s->bar_info[1] = (BARInfo){ .index = QTN_SHMEM_BAR, .type = BAR_TYPE_RAM, .size = 1024*1024, .name = "shmem" };
    s->bar_info[2] = (BARInfo){ .index = QTN_DMA_BAR, .type = BAR_TYPE_MMIO, .size = 4096, .name = "dmareg" };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI initialization */
    s->has_msi = true;
    s->has_msix = false;
    if (msi_init(pdev, 0, 1, true, false, errp)) {
        return;
    }

    s->flashboot = 1; /* boot from flash */
    s->chip_id = QTN_CHIP_ID_TOPAZ; /* TOPAZ chip ID */
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

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "qtnfmac_pcie_pci",
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
