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

#define TYPE_PCIBASE_DEVICE "i3200_edac_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_DEVICE_ID_INTEL_3200_HB    0x29f0
#define I3200_DIMMS		4
#define I3200_RANKS		8
#define I3200_RANKS_PER_CHANNEL	4
#define I3200_CHANNELS		2
#define I3200_MCHBAR_LOW	0x48
#define I3200_MCHBAR_HIGH	0x4c
#define I3200_MCHBAR_MASK	0xfffffc000ULL
#define I3200_MMR_WINDOW_SIZE	16384
#define I3200_TOM		0xa0
#define I3200_TOM_MASK		0x3ff
#define I3200_TOM_SHIFT		26
#define I3200_ERRSTS		0xc8
#define I3200_ERRSTS_UE		0x0002
#define I3200_ERRSTS_CE		0x0001
#define I3200_ERRSTS_BITS	(I3200_ERRSTS_UE | I3200_ERRSTS_CE)
#define I3200_C0DRB	0x200
#define I3200_C1DRB	0x600
#define I3200_DRB_MASK	0x3ff
#define I3200_DRB_SHIFT	26
#define I3200_C0ECCERRLOG	0x280
#define I3200_C1ECCERRLOG		0x680
#define I3200_ECCERRLOG_CE		0x1
#define I3200_ECCERRLOG_UE		0x2
#define I3200_ECCERRLOG_RANK_BITS	0x18000000
#define I3200_ECCERRLOG_RANK_SHIFT	27
#define I3200_ECCERRLOG_SYNDROME_BITS	0xff0000
#define I3200_ECCERRLOG_SYNDROME_SHIFT	16
#define I3200_CAPID0			0xe0

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
    uint32_t mchbar_low;
    uint32_t mchbar_high;
    uint16_t tom;
    uint16_t errsts;
    uint32_t capid0;
    uint16_t c0drb;
    uint16_t c1drb;
    uint64_t c0eccerrlog;
    uint64_t c1eccerrlog;
};

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= I3200_C0DRB && addr < I3200_C0DRB + 8) {
        int rank = (addr - I3200_C0DRB) / 2;
        val = (rank + 1) * 0x40; /* 64MB, 128MB, 192MB, 256MB */
    } else if (addr >= I3200_C1DRB && addr < I3200_C1DRB + 8) {
        int rank = (addr - I3200_C1DRB) / 2;
        val = (rank + 1) * 0x40;
    } else if (addr == I3200_C0ECCERRLOG) {
        val = s->c0eccerrlog;
    } else if (addr == I3200_C1ECCERRLOG) {
        val = s->c1eccerrlog;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr == I3200_C0ECCERRLOG) {
        s->c0eccerrlog = val;
    } else if (addr == I3200_C1ECCERRLOG) {
        s->c1eccerrlog = val;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
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

    s->c0eccerrlog = 0;
    s->c1eccerrlog = 0;
    
    PCIDevice *pdev = PCI_DEVICE(dev);
    
    /* MCHBAR: 0xfed14000 */
    pci_set_long(pdev->config + I3200_MCHBAR_LOW, 0xfed14000);
    pci_set_long(pdev->config + I3200_MCHBAR_HIGH, 0x00000000);
    
    /* TOM: matches the highest DRB (0x100) */
    pci_set_word(pdev->config + I3200_TOM, 0x0100);
    
    /* CAPID0 + 8 (0xe8): 0x00 (Dual channel, 2 DIMMs/channel enabled) */
    pdev->config[I3200_CAPID0 + 8] = 0x00;
    
    /* ERRSTS: 0 */
    pci_set_word(pdev->config + I3200_ERRSTS, 0x0000);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_INTEL );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_INTEL_3200_HB );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_BRIDGE_HOST );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    
    /* Explicitly place PM capability at 0x60 to avoid overlapping with ERRSTS at 0xc8 */
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0x60, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 0;  
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Setup W1C mask for ERRSTS and ensure wmask is clear to prevent QEMU assertion */
    pci_set_word(pdev->wmask + I3200_ERRSTS, 0);
    pci_set_word(pdev->w1cmask + I3200_ERRSTS, I3200_ERRSTS_BITS);

    /* Map MCHBAR into PCI address space */
    memory_region_init_io(&s->bar_regions[0], OBJECT(s), &pcibase_mmio_ops, s, "mchbar", I3200_MMR_WINDOW_SIZE);
    memory_region_add_subregion(pci_address_space(pdev), 0xfed14000, &s->bar_regions[0]);
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
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "i3200_edac_pci",
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
