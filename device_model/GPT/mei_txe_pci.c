/*
 * QEMU PCI device model for Intel MEI TXE (behavioral skeleton)
 * Phase 2: Basic functional behavior for driver probing
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

#include "hw/pci/pci_ids.h"

#define TYPE_PCIBASE_DEVICE "mei_txe_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Hardware Identifiers */
#define MEI_TXE_VENDOR_ID PCI_VENDOR_ID_INTEL
#define MEI_TXE_DEVICE_ID 0x0F18
#define MEI_TXE_CLASS_ID  PCI_CLASS_OTHERS

/* From txe driver bits we were given */
#define SICR_HOST_ALIVENESS_REQ_REG      0x214C
#define HICR_HOST_ALIVENESS_RESP_REG     0x2044

#define HOST_START_REQ_CMD               0x01
#define HICR_HOST_ALIVENESS_RESP_ACK     BIT(0)

/* BAR description */
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

    /* Simple register shadow for BAR0 MMIO */
    uint32_t *mmio_regs;
    uint32_t mmio_regs_size;

    /* Interrupt related shadow state (minimal) */
    uint32_t irq_status;
};

/* Internal helper for status-triggered signaling. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /*
     * The real TXE hardware has complex interrupt logic; here we only
     * provide a minimal stub: if irq_status is non-zero, assert INTx/MSI,
     * otherwise deassert.  No driver-visible register actually sets
     * irq_status in our minimal model, so interrupts will not be used
     * functionally, but the helper is present for future extension.
     */
    if (s->irq_status) {
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

/* Device-initiated DMA logic placeholder (unused by provided snippet) */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /* No explicit DMA engine registers or ops are visible in the provided
     * pci-txe.c snippet. DMA is only configured at the PCI/device level
     * (dma_set_mask), which QEMU handles via the generic PCI subsystem,
     * so we do not implement any device-side DMA engine here.
     */
    (void)s;
    (void)is_write;
}

/* Helper: translate MMIO byte address to dword index */
static inline unsigned pcibase_mmio_index(hwaddr addr)
{
    return (unsigned)(addr >> 2);
}

/* MMIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0xffffffffu;

    /* We only implement 32-bit accesses as used by typical mei drivers. */
    if (size != 4 || !s->mmio_regs || addr + 4 > s->mmio_regs_size) {
        return val;
    }

    switch (addr) {
    case HICR_HOST_ALIVENESS_RESP_REG:
        /* The driver will poll for ACK when using aliveness. Provide
         * a constant ACK to keep the state machine progressing.
         */
        val = HICR_HOST_ALIVENESS_RESP_ACK;
        break;
    default:
        val = s->mmio_regs[pcibase_mmio_index(addr)];
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4 || !s->mmio_regs || addr + 4 > s->mmio_regs_size) {
        return;
    }

    switch (addr) {
    case SICR_HOST_ALIVENESS_REQ_REG:
        /* Accept host start requests and store value; the corresponding
         * response register always reports ACK on read, so no extra action.
         */
        s->mmio_regs[pcibase_mmio_index(addr)] = (uint32_t)val;
        break;
    default:
        s->mmio_regs[pcibase_mmio_index(addr)] = (uint32_t)val;
        break;
    }
}

/* PIO handlers: TXE driver uses MMIO (pcim_iomap_regions), not PIO */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    (void)opaque;
    (void)addr;
    (void)size;
    return 0xffffffffu;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    (void)opaque;
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

    /* Clear simple register/IRQ state on reset */
    if (s->mmio_regs && s->mmio_regs_size) {
        memset(s->mmio_regs, 0, s->mmio_regs_size);
    }
    s->irq_status = 0;
}

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

        /* Allocate shadow register backing store for MMIO BAR0 only once. */
        if (bi->index == 0 && !s->mmio_regs) {
            s->mmio_regs_size = aligned_size;
            s->mmio_regs = g_malloc0(s->mmio_regs_size);
        }
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  MEI_TXE_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  MEI_TXE_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, MEI_TXE_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Basic PCIe capability */
    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);

    /* PM capability (minimal) */
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR Initialization: provide two MMIO BARs so that
     * the driver's SEC_BAR and BRIDGE_BAR mask can succeed.
     * Exact sizes are not specified in the provided snippet, so
     * we choose 64 KiB for each, which is sufficiently large for
     * the known offsets we use (around 0x2000).
     */
    s->num_bars = 2;

    s->bar_info[0].index = 0;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    s->bar_info[0].size  = 64 * KiB;
    s->bar_info[0].name  = "mei-txe-sec-mmio";

    s->bar_info[1].index = 1;
    s->bar_info[1].type  = BAR_TYPE_MMIO;
    s->bar_info[1].size  = 64 * KiB;
    s->bar_info[1].name  = "mei-txe-bridge-mmio";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Enable MSI capability so pci_enable_msi() in the guest can succeed. */
    Error *local_err = NULL;
    if (msi_init(pdev, 0, 1, true, false, &local_err) == 0) {
        s->has_msi = true;
    } else {
        s->has_msi = false;
        if (local_err) {
            error_propagate(errp, local_err);
        }
    }

    s->has_msix = false;

    /* Initialize internal state */
    s->irq_status = 0;
    if (s->mmio_regs && s->mmio_regs_size) {
        memset(s->mmio_regs, 0, s->mmio_regs_size);
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

    if (s->mmio_regs) {
        g_free(s->mmio_regs);
        s->mmio_regs = NULL;
        s->mmio_regs_size = 0;
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "mei_txe_pci",
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
