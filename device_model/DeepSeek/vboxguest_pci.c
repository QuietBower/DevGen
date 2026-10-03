/*
 * QEMU PCI device model for VBoxGuest Linux driver (vboxguest.ko)
 * Generated for QEMU 8.2.10
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

#define TYPE_PCIBASE_DEVICE "vboxguest_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Vendor and Device IDs from the driver's pci_device_id table */
#define VBOX_VENDOR_ID  0x80ee
#define VBOX_DEVICE_ID  0xcafe
/* PCI class: System peripheral, other */
#define VBOX_CLASS_ID   0x088000

/* MMIO register offsets (struct vmmdev_memory layout) */
#define REG_OFF_SIZE              0x00
#define REG_OFF_VERSION           0x04
#define REG_OFF_HOST_EVENTS       0x08
#define REG_OFF_GUEST_EVENT_MASK  0x0C

/* I/O port offset (request port) */
#define REG_PORT_REQUEST          0x00

/* Memory version from supplementary source */
#define VMMDEV_MEMORY_VERSION   (1)

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

    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;

    /* Hardware register shadows */
    struct {
        uint32_t size;
        uint32_t version;
        uint32_t host_events;
        uint32_t guest_event_mask;
    } mmio_regs;
};

/* Update IRQ state based on enabled events */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t pending = s->mmio_regs.host_events & s->mmio_regs.guest_event_mask;
    if (pending) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* MMIO read handler */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR, "vboxguest: MMIO read with size %u\n", size);
        return 0;
    }

    switch (addr) {
    case REG_OFF_SIZE:
        val = s->mmio_regs.size;
        break;
    case REG_OFF_VERSION:
        val = s->mmio_regs.version;
        break;
    case REG_OFF_HOST_EVENTS:
        val = s->mmio_regs.host_events;
        break;
    case REG_OFF_GUEST_EVENT_MASK:
        val = s->mmio_regs.guest_event_mask;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "vboxguest: MMIO read at unknown 0x%" HWADDR_PRIx "\n", addr);
        break;
    }
    return val;
}

/* MMIO write handler */
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR, "vboxguest: MMIO write with size %u\n", size);
        return;
    }

    switch (addr) {
    case REG_OFF_SIZE:
        /* Read-only, ignore */
        break;
    case REG_OFF_VERSION:
        /* Read-only, ignore */
        break;
    case REG_OFF_HOST_EVENTS:
        /* Write-1-to-clear */
        s->mmio_regs.host_events &= ~val;
        pcibase_update_irq(s);
        break;
    case REG_OFF_GUEST_EVENT_MASK:
        s->mmio_regs.guest_event_mask = val;
        pcibase_update_irq(s);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "vboxguest: MMIO write at unknown 0x%" HWADDR_PRIx "\n", addr);
        break;
    }
}

/* I/O port read handler (request port) */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    /* No status defined, return 0 */
    return 0;
}

/* I/O port write handler (request port) */
static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    /* In a full emulation we'd process the request, but for probe it's not needed */
    qemu_log_mask(LOG_UNIMP, "vboxguest: IO port write 0x%" PRIx64 " ignored\n", val);
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
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

    /* Reset register defaults */
    s->mmio_regs.host_events = 0;
    s->mmio_regs.guest_event_mask = 0;
    /* size and version remain unchanged, set in realize */
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

    pci_set_word(pci_conf + PCI_VENDOR_ID, VBOX_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, VBOX_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, VBOX_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* PCIe capability */
    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);

    /* Power management capability (optional, but included for completeness) */
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR initialization */
    s->num_bars = 2;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_PIO, .size = 8, .name = "vboxguest-io" };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_MMIO, .size = 4096, .name = "vboxguest-mmio" };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize MMIO register defaults */
    s->mmio_regs.size = 4096;  /* Must be >= 32 */
    s->mmio_regs.version = VMMDEV_MEMORY_VERSION;
    s->mmio_regs.host_events = 0;
    s->mmio_regs.guest_event_mask = 0;
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    /* MSI/MSI-X not used, but ensure clean state */
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

static const VMStateDescription vmstate_pcibase = {
    .name = "vboxguest_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(mmio_regs.size, PCIBaseState),
        VMSTATE_UINT32(mmio_regs.version, PCIBaseState),
        VMSTATE_UINT32(mmio_regs.host_events, PCIBaseState),
        VMSTATE_UINT32(mmio_regs.guest_event_mask, PCIBaseState),
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
