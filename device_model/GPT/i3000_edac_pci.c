/*
 * QEMU PCI device model for Intel i3000 EDAC host bridge
 * Generated to satisfy Linux i3000_edac driver probing and basic operation.
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
/* Removed linux/pci_ids.h: not available in QEMU build environment */

#define TYPE_PCIBASE_DEVICE "i3000_edac_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
/* i3000_edac hardware-related constants */
#define I3000_RANKS             8
#define I3000_RANKS_PER_CHANNEL 4
#define I3000_CHANNELS          2
#define I3000_MCHBAR            0x44
#define I3000_MCHBAR_MASK       0xffffc000
#define I3000_MMR_WINDOW_SIZE   16384
#define I3000_EDEAP             0x70
#define I3000_DEAP              0x58
#define I3000_DEAP_GRAIN        (1 << 7)
#define I3000_DERRSYN           0x5c
#define I3000_ERRSTS            0xc8
#define I3000_ERRSTS_BITS       0x0b03
#define I3000_ERRSTS_UE         0x0002
#define I3000_ERRSTS_CE         0x0001
#define I3000_ERRCMD            0xca
#define I3000_DRB_SHIFT         25
#define I3000_C0DRB             0x100
#define I3000_C1DRB             0x180
#define I3000_C0DRA             0x108
#define I3000_C1DRA             0x188
#define I3000_C0DRC0            0x120
#define I3000_C0DRC1            0x124

/* PCI identification (from first entry of i3000_pci_tbl) */
#define PCIBASE_VENDOR_ID   0x8086
#define PCIBASE_DEVICE_ID   0x2778
#define PCIBASE_CLASS_ID    0x0600


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
    /* Shadow copies of selected i3000 registers accessed by the driver */
    uint32_t mchbar;
    uint16_t errsts;
    uint16_t errcmd;
    uint8_t  derrsyn;
    uint8_t  edeap;
    uint32_t deap;
    uint8_t  c0dra;
    uint8_t  c1dra;
    uint8_t  c0drb;
    uint8_t  c1drb;
    uint8_t  c0drc0;
    uint8_t  c0drc1;

};


/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* The reference driver does not enable or handle interrupts
     * for this device explicitly, beyond error status bits in
     * PCI config space. No separate IRQ line behavior is modeled.
     */
    (void)pdev;
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* The i3000 EDAC driver does not program any DMA engine on
     * this host bridge; it only reads/writes PCI config space and
     * an MMIO window pointed to by MCHBAR. No DMA modeled.
     */
    (void)pdev;
    (void)is_write;
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    /* The driver does not map or access any BAR-based MMIO for this
     * PCI function; it only accesses an MMIO window in system memory
     * whose base address is given by the MCHBAR config register.
     * That window is provided by a separate MemoryRegion, not by this
     * device's BARs. Therefore, any accesses here can safely return 0.
     */
    (void)s;
    (void)addr;
    (void)size;
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* As above, the driver does not use BAR MMIO of this device. */
    (void)s;
    (void)addr;
    (void)val;
    (void)size;
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Port I/O not used by this driver. */
    (void)s;
    (void)addr;
    (void)size;
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    /* Port I/O not used by this driver. */
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

    /* Reset register shadows to power-on defaults (zeros). */
    s->mchbar = 0;
    s->errsts = 0;
    s->errcmd = 0;
    s->derrsyn = 0;
    s->edeap = 0;
    s->deap = 0;
    s->c0dra = 0;
    s->c1dra = 0;
    s->c0drb = 0;
    s->c1drb = 0;
    s->c0drc0 = 0;
    s->c0drc1 = 0;
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
    /* The driver accesses only PCI config space; no MMIO BARs are required. */
    s->num_bars = 0;
    for (int i = 0; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = "";
    }

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No MSI or MSI-X usage in the driver; leave disabled. */
    s->has_msi = false;
    s->has_msix = false;

    /* No DMA or internal timers used by the driver; nothing to configure here. */

    /* Initialize register shadows to reset defaults. */
    s->mchbar = 0;
    s->errsts = 0;
    s->errcmd = 0;
    s->derrsyn = 0;
    s->edeap = 0;
    s->deap = 0;
    s->c0dra = 0;
    s->c1dra = 0;
    s->c0drb = 0;
    s->c1drb = 0;
    s->c0drc0 = 0;
    s->c0drc1 = 0;
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

    /* No dynamic resources allocated; nothing to free. */
    (void)s;
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "i3000_edac_pci",
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
