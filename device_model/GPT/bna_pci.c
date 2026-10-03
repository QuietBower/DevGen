/*
 * QEMU PCI device model for Brocade bna NIC (behavioral skeleton)
 *
 * This model is minimal and only implements the MMIO / IRQ surface
 * that the bnad driver touches directly in bnad.c. All deeper
 * functionality implemented in the bna core (bna_*) is left to the
 * guest driver and not modeled here.
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

#include <stdint.h>

#define TYPE_PCIBASE_DEVICE "bna_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Basic identifiers derived from driver */
#define PCI_VENDOR_ID_BROCADE        0x1657
#define PCI_DEVICE_ID_BROCADE_CT     0x0014
#define PCIBASE_VENDOR_ID PCI_VENDOR_ID_BROCADE
#define PCIBASE_DEVICE_ID PCI_DEVICE_ID_BROCADE_CT
#define PCIBASE_CLASS_ID  PCI_CLASS_NETWORK_ETHERNET

/* Interrupt related registers (subset, from driver macros) */
#define HOSTFN0_INT_STATUS   0x00014000
#define HOSTFN0_INT_MSK      0x00014004
#define HOSTFN1_INT_STATUS   0x00014100
#define HOSTFN1_INT_MSK      0x00014104
#define HOSTFN2_INT_STATUS   0x00014300
#define HOSTFN2_INT_MSK      0x00014304
#define HOSTFN3_INT_STATUS   0x00014400
#define HOSTFN3_INT_MSK      0x00014404

#define CT2_PCI_APP_BASE           0x00030100
#define CT2_HOSTFN_INT_STATUS      (CT2_PCI_APP_BASE + 0x00)
#define CT2_HOSTFN_INTR_MASK       (CT2_PCI_APP_BASE + 0x04)

/* Only a very small subset of the register set is needed for bnad.c:
 * - Interrupt status / mask
 * - Nothing from the huge list of other macros is actually accessed
 *   directly in this bnad.c fragment via readl/writel, so we leave
 *   them unimplemented here to avoid inventing behavior.
 */

/* Minimal BAR description */
typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM
} BARType;

typedef struct {
    int     index;
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
    uint32_t intr_status; /* aggregate logical status */
    uint32_t intr_mask;   /* aggregate logical mask (1=enabled) */

    /* Hardware Register Shadows (subset actually used) */
    struct {
        uint32_t fn0_int_status;
        uint32_t fn0_int_mask;
        uint32_t ct2_int_status;
        uint32_t ct2_int_mask;
    } reg_shadows;
};

/* Internal helper for status-triggered signaling. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* Simple wired-OR of interrupt sources: if any status bit is set
     * and not masked, present an interrupt to the guest.
     */
    uint32_t pending = 0;

    /* For now only model fn0/ct2 hostfn status/mask as a single line. */
    if (s->reg_shadows.fn0_int_status & s->reg_shadows.fn0_int_mask) {
        pending |= 1;
    }
    if (s->reg_shadows.ct2_int_status & s->reg_shadows.ct2_int_mask) {
        pending |= 1;
    }

    /* Also mirror into the generic intr_status / intr_mask for possible
     * future extension.
     */
    s->intr_status = pending;
    s->intr_mask = 1; /* we always allow delivery if pending != 0 */

    if (pending) {
        if (msix_enabled(pdev)) {
            /* The real device has many MSI-X vectors, but bnad.c does
             * not touch MSIX table contents directly. We present a
             * single vector (0) for mailbox / data interrupts.
             */
            msix_notify(pdev, 0);
        } else if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!msi_enabled(pdev) && !msix_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

/* No device-initiated DMA is visible in bnad.c; all DMA is initiated
 * by the driver via descriptor rings managed in the guest. We therefore
 * do not implement any pci_dma_read/write based engine here.
 */

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* We only handle 32-bit accesses for our registers; other sizes
     * return 0 and are ignored.
     */
    if (size != 4) {
        return 0;
    }

    switch (addr) {
    case HOSTFN0_INT_STATUS:
        val = s->reg_shadows.fn0_int_status;
        break;
    case HOSTFN0_INT_MSK:
        val = s->reg_shadows.fn0_int_mask;
        break;
    case CT2_HOSTFN_INT_STATUS:
        val = s->reg_shadows.ct2_int_status;
        break;
    case CT2_HOSTFN_INTR_MASK:
        val = s->reg_shadows.ct2_int_mask;
        break;
    default:
        /* Unimplemented registers read as 0 */
        val = 0;
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        return;
    }

    switch (addr) {
    case HOSTFN0_INT_STATUS:
        /* Treat as write-1-to-clear on bits written as 1. */
        s->reg_shadows.fn0_int_status &= ~(uint32_t)val;
        break;
    case HOSTFN0_INT_MSK:
        s->reg_shadows.fn0_int_mask = (uint32_t)val;
        break;
    case CT2_HOSTFN_INT_STATUS:
        s->reg_shadows.ct2_int_status &= ~(uint32_t)val;
        break;
    case CT2_HOSTFN_INTR_MASK:
        s->reg_shadows.ct2_int_mask = (uint32_t)val;
        break;
    default:
        /* ignore writes to unknown offsets */
        break;
    }

    pcibase_update_irq(s);
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* The driver does not use port I/O in bnad.c, so we return 0. */
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Legacy I/O space not used by this driver; ignore. */
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

    /* Reset register shadows to power-on defaults (all zero). */
    memset(&s->reg_shadows, 0, sizeof(s->reg_shadows));
    s->intr_status = 0;
    s->intr_mask = 0;

    pcibase_update_irq(s);
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    hwaddr aligned_size = pow2ceil(bi->size ? bi->size : 1);
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

    /* Explicitly program IDs so the in-guest probe matches bnad's
     * pci_device_id table without relying on the QEMU command line.
     */
    pci_config_set_vendor_id(pci_conf, PCIBASE_VENDOR_ID);
    pci_config_set_device_id(pci_conf, PCIBASE_DEVICE_ID);
    pci_config_set_class(pci_conf, PCIBASE_CLASS_ID);

    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    Error *local_err = NULL;
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* Single MMIO BAR (BAR0) of 1 MiB, as used by the driver. */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 1 * MiB;
    s->bar_info[0].name = "bna-bar0";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = NULL;
    }

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize MSI-X with at least one vector; the driver will call
     * pci_enable_msix_range() and expect success for one vector.
     * We also support MSI as a fallback.
     */
    s->has_msix = true;
    s->has_msi = true;

    if (s->has_msix) {
        int nvec = 1;
        if (msix_init_exclusive_bar(pdev, nvec, 1, errp) < 0) {
            s->has_msix = false;
        }
    }

    if (s->has_msi) {
        if (msi_init(pdev, 0, 1, true, false, errp) < 0) {
            s->has_msi = false;
        }
    }

    memset(&s->reg_shadows, 0, sizeof(s->reg_shadows));
    s->intr_status = 0;
    s->intr_mask = 0;
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    (void)s; /* currently unused */

    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, 0);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "bna_pci",
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
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
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
