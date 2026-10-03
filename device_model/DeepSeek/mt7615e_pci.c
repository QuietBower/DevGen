/*
 * This template provides a robust skeleton for hardware emulation.
 * Integrated QEMU PCI device template (QEMU 8.2.10).
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

#define TYPE_PCIBASE_DEVICE "mt7615e_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Related_Config_Info */
#define VENDOR_ID 0x14C3
#define DEVICE_ID 0x7615
#define CLASS_ID  0x0280

/* Supplementary Source: Register Base Address Offsets */
#define MT_HIF_BASE         0x4000
#define MT_LPON_BASE        0x24000
#define MT_WF_RMAC_BASE(_band)     ((_band) ? 0x820f5000 : 0x820e5000)
#define MT_WF_PHY_BASE      0x10000
#define MT_WF_ARB_BASE(_band)      ((_band) ? 0x820f3000 : 0x820e3000)
#define MT_WF_AGG_BASE(_band)      ((_band) ? 0x820f2000 : 0x820e2000)
#define MT_WF_TMAC_BASE(_band)     ((_band) ? 0x820f4000 : 0x820e4000)
#define MT_WF_MIB_BASE(_band)      ((_band) ? 0x820fd000 : 0x820ed000)

/* Newly provided register offsets from driver source */
#define MT_HW_CHIPID           0x70010200
#define MT_HW_REV              0x70010204
#define MT_INT_MASK_CSR        0x0204
/* MT_INT_STATUS_CSR offset not provided; using guessed 0x0208 (placeholder) */
#define MT_INT_STATUS_CSR      0x0208
/* MT_PCIE_IRQ_ENABLE defined using MT_HIF_BASE + 0x188; assumed MT_HIF2 expansion */
#define MT_PCIE_IRQ_ENABLE     (MT_HIF_BASE + 0x188)

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

    /* DMA Context */
    uint64_t dma_mask;

    uint32_t pdma_busy_status;
    uint32_t reset_state;
    uint32_t power_state;

    /* Register shadows for known registers */
    uint32_t int_mask_csr;        /* MT_INT_MASK_CSR */
    uint32_t int_status_csr;      /* MT_INT_STATUS_CSR */
    uint32_t pcie_irq_enable;     /* MT_PCIE_IRQ_ENABLE */
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t active = s->int_status_csr & s->int_mask_csr;
    if (active) {
        if (s->has_msi && msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!msi_enabled(pdev) || !s->has_msi) {
            pci_set_irq(pdev, 0);
        }
    }
}

/* Device-initiated DMA logic based on driver access patterns */
static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    /* DMA_Transfer_Logic: not implemented; missing register definitions */
    qemu_log_mask(LOG_UNIMP, "pcibase_do_dma: not implemented\n");
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Handle known register offsets */
    switch (addr) {
    case MT_HW_CHIPID:
        val = 0x7615;
        break;
    case MT_HW_REV:
        val = 0x0001;
        break;
    case MT_INT_MASK_CSR:
        val = s->int_mask_csr;
        break;
    case MT_INT_STATUS_CSR:
        val = s->int_status_csr;
        break;
    case MT_PCIE_IRQ_ENABLE:
        val = s->pcie_irq_enable;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unknown offset 0x%"HWADDR_PRIx" size %u\n",
                      __func__, addr, size);
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case MT_INT_MASK_CSR:
        s->int_mask_csr = val;
        pcibase_update_irq(s);
        break;
    case MT_INT_STATUS_CSR:
        /* W1C: write-1-to-clear */
        s->int_status_csr &= ~val;
        pcibase_update_irq(s);
        break;
    case MT_PCIE_IRQ_ENABLE:
        s->pcie_irq_enable = val;
        /* May affect interrupt generation */
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unknown offset 0x%"HWADDR_PRIx" value 0x%"PRIx64" size %u\n",
                      __func__, addr, val, size);
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* PIO not used by this device */
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* PIO not used by this device */
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

    /* Reset state variables to power-on defaults */
    s->pdma_busy_status = 0;
    s->reset_state = 0;
    s->power_state = 0;
    s->intr_status = 0;
    s->intr_mask = 0;
    s->int_mask_csr = 0;
    s->int_status_csr = 0;
    s->pcie_irq_enable = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);

    /* BAR Initialization */
    /* BAR0: used by driver (pcim_iomap_regions BIT(0)). Size increased to cover high register offsets */
    s->bar_info[0] = (BARInfo) {
        .index = 0,
        .type = BAR_TYPE_MMIO,
        .size = 0x80000000, /* 2 GB to cover large offsets like 0x70010200 */
        .name = "mt7615e-mmio"
    };
    s->num_bars = 1;

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Try to enable MSI; fallback to INTx if not available */
    if (msi_init(pdev, 0, 1, true, false, NULL) == 0) {
        s->has_msi = true;
    } else {
        s->has_msi = false;
    }
    s->has_msix = false;

    /* DMA configuration */
    s->dma_mask = (1ULL << 32) - 1;
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
    /* No additional cleanup needed */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "mt7615e_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(int_mask_csr, PCIBaseState),
        VMSTATE_UINT32(int_status_csr, PCIBaseState),
        VMSTATE_UINT32(pcie_irq_enable, PCIBaseState),
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

type_init(pcibase_register_types)
