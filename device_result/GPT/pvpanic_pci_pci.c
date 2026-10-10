/*
 * QEMU PCI device model for pvpanic-pci driver
 * Phase 2: Implementation based strictly on provided driver source
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

#define TYPE_PCIBASE_DEVICE "pvpanic_pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_REDHAT        0x1b36
#define PCI_CLASS_OTHERS            0xff

#define PVPANIC_PCI_VENDOR_ID   PCI_VENDOR_ID_REDHAT
#define PVPANIC_PCI_DEVICE_ID   0x0011
#define PVPANIC_PCI_CLASS_ID    PCI_CLASS_OTHERS

/* pvpanic capability bits (from include/uapi/linux/pvpanic.h in kernel):
 * We only model bits that are explicitly used by the driver snippet.
 */
#define PVPANIC_PANICKED       0x01
#define PVPANIC_CRASH_LOADED   0x02
#define PVPANIC_SHUTDOWN       0x04

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

    /* pvpanic capability register state visible at BAR0 offset 0 */
    uint8_t capability_reg;
};


/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    (void)pdev;
    /*
     * The provided pvpanic-pci driver does not set up or use interrupts for
     * this device. Leave IRQ logic empty as there are no driver-visible
     * interrupt registers or ISRs.
     */
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    (void)pdev;
    (void)is_write;
    /*
     * The pvpanic-pci driver does not perform any DMA operations, nor does it
     * configure bus mastering or program DMA-related registers. No DMA logic
     * is required here.
     */
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /*
     * The new driver snippet shows:
     *   pi->capability = PVPANIC_PANICKED | PVPANIC_CRASH_LOADED | PVPANIC_SHUTDOWN;
     *   pi->capability &= ioread8(base);
     *   pi->events = pi->capability;
     *
     * ioread8(base) reads a single byte from BAR0 offset 0. To advertise
     * support for all these events, we return a capability byte with
     * exactly these bits set from offset 0. Other offsets remain 0.
     */

    switch (size) {
    case 1:
        if (addr == 0) {
            val = s->capability_reg;
        } else {
            val = 0;
        }
        break;
    case 2:
    case 4:
    case 8:
        /* Driver only uses ioread8, so larger reads are not expected. */
        val = 0;
        break;
    default:
        /* Unsupported access size; return 0 */
        val = 0;
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    (void)s;

    /*
     * The provided pvpanic-pci driver snippet does not show any explicit
     * writes to the BAR0 MMIO space. Any behavior inside devm_pvpanic_probe()
     * beyond the initial ioread8(base) is unknown from the given context,
     * so we cannot model specific register side effects. Accept writes but
     * ignore them to keep behavior defined without inventing hardware
     * semantics.
     */

    (void)addr;
    (void)val;

    switch (size) {
    case 1:
    case 2:
    case 4:
    case 8:
        /* Writes are silently ignored */
        break;
    default:
        /* Ignore unsupported access sizes as well */
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    (void)s;
    (void)addr;

    /*
     * The pvpanic-pci driver uses pcim_iomap() on BAR0, which is a memory
     * BAR, not an I/O port BAR. There is no driver-visible port I/O usage,
     * so we simply return 0 for any PIO reads.
     */

    switch (size) {
    case 1:
    case 2:
    case 4:
        /* val is already 0 */
        break;
    default:
        val = 0;
        break;
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;
    (void)addr;
    (void)val;

    /*
     * No PIO usage by the driver; accept and ignore writes.
     */

    switch (size) {
    case 1:
    case 2:
    case 4:
        /* ignore */
        break;
    default:
        /* ignore unsupported size as well */
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

    /* Reinitialize capability register to advertise all supported events */
    s->capability_reg = PVPANIC_PANICKED | PVPANIC_CRASH_LOADED | PVPANIC_SHUTDOWN;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PVPANIC_PCI_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PVPANIC_PCI_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PVPANIC_PCI_CLASS_ID );
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
     * The driver calls pcim_iomap(pdev, 0, 0) which expects BAR0 to be
     * present and memory-mappable. No size is specified there, so we choose
     * a small non-zero size while not inferring register layout. 4KB is
     * a conventional minimal page-sized MMIO region. Only offset 0 has
     * defined semantics (capability byte) per the driver snippet.
     */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    s->bar_info[0].size  = 0x1000; /* 4 KiB, minimal functional region */
    s->bar_info[0].name  = "pvpanic-pci-bar0";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize capability register so ioread8(base) returns supported bits */
    s->capability_reg = PVPANIC_PANICKED | PVPANIC_CRASH_LOADED | PVPANIC_SHUTDOWN;

    s->has_msi = false;
    s->has_msix = false;

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
    .name = "pvpanic_pci_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT8(capability_reg, PCIBaseState),
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