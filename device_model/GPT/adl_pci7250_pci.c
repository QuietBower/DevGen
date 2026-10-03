/*
 * QEMU PCI device model for adl_pci7250_pci
 * Phase 2: Functional behavior for MMIO/PIO byte-wide access window.
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

#define TYPE_PCIBASE_DEVICE "adl_pci7250_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define ADL_PCI7250_VENDOR_ID 0x144a
#define ADL_PCI7250_DEVICE_ID 0x9050
#define ADL_PCI7250_CLASS_ID  PCI_CLASS_OTHERS

/* BAR index 2 is used by the driver; it requires at least 8 bytes. */
#define ADL_PCI7250_BAR_INDEX   2
#define ADL_PCI7250_BAR_SIZE    8

/* The driver accesses byte-wide registers at even/odd offsets 0..7. */
#define ADL_PCI7250_REG_DO_BASE 0x00 /* even offsets: relay DO */
#define ADL_PCI7250_REG_DI_BASE 0x01 /* odd offsets: isolated DI */

/* The driver chooses MMIO vs Port I/O at runtime based on BAR2 flags. */

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

    /* Hardware Register Shadows */
    /* 32 relay digital output channels (4 bytes at even offsets 0,2,4,6). */
    uint8_t relay_do[4];
    /* 32 isolated digital input channels (4 bytes at odd offsets 1,3,5,7). */
    uint8_t iso_di[4];
};

/* No interrupt logic is used by the driver, keep this as a no-op helper. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    (void)pdev;
}

/* Device-initiated DMA logic: not used by the driver. */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    (void)pdev;
    (void)is_write;
}

/* MMIO/PIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Driver uses byte accesses via readb() only. */
    if (size != 1) {
        return 0xff; /* undefined for this device, but non-fatal */
    }

    /* Only first 8 bytes are valid. */
    if (addr >= ADL_PCI7250_BAR_SIZE) {
        return 0xff;
    }

    /* Even offsets: relay DO (output). Odd offsets: isolated DI (input). */
    if ((addr & 0x1) == 0) {
        /* Even offset: return current relay output latch. */
        unsigned index = (addr >> 1) & 0x3; /* 0..3 */
        val = s->relay_do[index];
    } else {
        /* Odd offset: return isolated digital input. */
        unsigned index = (addr >> 1) & 0x3; /* 0..3 */
        val = s->iso_di[index];
    }

    return val & 0xff;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* Driver uses byte accesses via writeb() only. */
    if (size != 1) {
        return;
    }

    /* Only first 8 bytes are valid. */
    if (addr >= ADL_PCI7250_BAR_SIZE) {
        return;
    }

    if ((addr & 0x1) == 0) {
        /* Even offset: relay DO write. Each byte controls 8 outputs. */
        unsigned index = (addr >> 1) & 0x3; /* 0..3 */
        s->relay_do[index] = (uint8_t)(val & 0xff);
    } else {
        /* Odd offsets are DI and are read-only from the driver's POV. */
        /* Ignore writes to DI registers. */
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* The driver uses inb/outb for I/O port access, which are 1-byte. */
    return pcibase_mmio_read(opaque, addr, size);
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    pcibase_mmio_write(opaque, addr, val, size);
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

    /* Clear relay outputs; inputs remain whatever they were (default 0). */
    memset(s->relay_do, 0, sizeof(s->relay_do));
    /* iso_di[] stays unchanged; initialized in realize. */
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    /* PCI requires BAR sizes to be a power of 2. */
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x10b5 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x9050 );
    pci_set_word(pci_conf + PCI_SUBSYSTEM_VENDOR_ID, 0x9999);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_ID, 0x7250);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, ADL_PCI7250_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Provide both conventional and PCIe capabilities (as in skeleton). */
    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR Initialization */
    s->num_bars = 1;
    for (int i = 0; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = "adl_pci7250-bar";
    }

    /* Configure BAR2 as an 8-byte memory region; the driver may also treat
     * this BAR as a port I/O region depending on resource flags. We expose
     * it as MMIO; the driver path using MMIO will then be taken.
     */
    s->bar_info[0].index = ADL_PCI7250_BAR_INDEX;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    s->bar_info[0].size  = ADL_PCI7250_BAR_SIZE;
    s->bar_info[0].name  = "adl_pci7250-bar2";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI/MSI-X: not used by driver. */
    s->has_msi = false;
    s->has_msix = false;

    /* Initialize shadow registers: relay outputs off, inputs low. */
    memset(s->relay_do, 0, sizeof(s->relay_do));
    memset(s->iso_di, 0, sizeof(s->iso_di));
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
    .name = "adl_pci7250_pci",
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

