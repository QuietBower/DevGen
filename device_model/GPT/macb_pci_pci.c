/*
 * QEMU PCI device model for Cadence MACB PCI wrapper (minimal behavior)
 *
 * This model is intentionally minimal: the provided Linux driver
 * (macb_pci.c) only uses the PCI core to expose a single MMIO BAR and
 * one interrupt vector to a platform MACB driver. It does not touch
 * device-specific registers in this PCI function.
 *
 * Therefore we only need to provide:
 *  - One MMIO BAR (BAR0) of non-zero size
 *  - One MSI/MSI-X/INTx interrupt vector so pci_irq_vector(pdev, 0)
 *    returns a usable IRQ and IORESOURCE_IRQ is valid
 *  - Basic PCI configuration fields so pcim_enable_device() and
 *    pci_set_master() succeed
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
/* 'hw/net/macb.h' is not available in this QEMU build; remove to fix compile error. */

#define TYPE_PCIBASE_DEVICE "macb_pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_CDNS        0x17cd
#define PCI_CLASS_NETWORK_ETHERNET 0x0200

#define PCIBASE_VENDOR_ID   PCI_VENDOR_ID_CDNS
#define PCIBASE_DEVICE_ID   0xe007
#define PCIBASE_CLASS_ID    PCI_CLASS_NETWORK_ETHERNET

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
};


/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    /*
     * The macb_pci driver does not interact with this PCI function's
     * interrupts directly. It only requests an IRQ vector number via
     * pci_irq_vector() to pass as a resource to the platform MACB
     * driver. Therefore, we don't need to implement any status-based
     * IRQ logic here. Keep as a no-op.
     */

    (void)s;
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /*
     * The macb_pci wrapper itself does not perform DMA; all DMA is done
     * by the underlying MACB MAC through the platform device and its
     * own registers (not modeled here). Hence no DMA logic is required
     * in this PCI wrapper.
     */

    (void)s;
    (void)is_write;
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /*
     * The macb_pci wrapper driver never ioremaps or accesses this
     * function's registers directly. It only forwards the BAR0
     * resource to a separate platform MACB driver, which will in turn
     * use the MACB memory region created by hw/net/macb.c.
     *
     * For safety and debug purposes, log unexpected accesses but
     * otherwise return 0.
     */
    (void)s;

    qemu_log_mask(LOG_GUEST_ERROR,
                  "macb_pci_pci: unexpected MMIO read addr=0x%" HWADDR_PRIx
                  " size=%u\n", addr, size);

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* See comment in pcibase_mmio_read() */
    (void)s;

    qemu_log_mask(LOG_GUEST_ERROR,
                  "macb_pci_pci: unexpected MMIO write addr=0x%" HWADDR_PRIx
                  " size=%u val=0x%" PRIx64 "\n", addr, size, val);
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    (void)s;

    /* This device is not expected to use legacy PIO. */
    qemu_log_mask(LOG_GUEST_ERROR,
                  "macb_pci_pci: unexpected PIO read addr=0x%" HWADDR_PRIx
                  " size=%u\n", addr, size);
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    (void)s;

    /* This device is not expected to use legacy PIO. */
    qemu_log_mask(LOG_GUEST_ERROR,
                  "macb_pci_pci: unexpected PIO write addr=0x%" HWADDR_PRIx
                  " size=%u val=0x%" PRIx64 "\n", addr, size, val);
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

    /* No device-specific registers to reset in this wrapper. */
    (void)s;
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

    /* BAR Initialization */
    /*
     * The macb_pci driver only uses BAR0 as IORESOURCE_MEM, so we
     * create a single MMIO BAR with a generic size (e.g. 128KiB).
     * This is sufficient for the probe() path which only passes the
     * resource to the platform MACB device.
     */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    s->bar_info[0].size  = 128 * KiB;
    s->bar_info[0].name  = "macb_pci_mmio";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize MSI (single vector) so pci_irq_vector(pdev, 0) works. */
    s->has_msi = !msi_init(pdev, 0, 1, true, false, errp);
    s->has_msix = false;

    /* No additional timers or DMA engines required for this wrapper. */
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

    /* No additional cleanup required. */
    (void)s;
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "macb_pci_pci",
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
