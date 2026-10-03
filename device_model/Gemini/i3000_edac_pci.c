/*
 * Integrated QEMU PCI device template (QEMU 8.2.10).
 * Designed for register-level modeling and PCIe driver probing.
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

#define TYPE_PCIBASE_DEVICE "i3000_edac_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_DEVICE_ID_INTEL_3000_HB	0x2778
#define I3000_RANKS		8
#define I3000_RANKS_PER_CHANNEL	4
#define I3000_CHANNELS		2
#define I3000_MCHBAR		0x44
#define I3000_MCHBAR_MASK	0xffffc000
#define I3000_MMR_WINDOW_SIZE	16384
#define I3000_EDEAP	0x70
#define I3000_DEAP	0x58
#define I3000_DEAP_GRAIN 		(1 << 7)
#define I3000_DERRSYN	0x5c
#define I3000_ERRSTS	0xc8
#define I3000_ERRSTS_BITS	0x0b03
#define I3000_ERRSTS_UE		0x0002
#define I3000_ERRSTS_CE		0x0001
#define I3000_ERRCMD	0xca
#define I3000_DRB_SHIFT 25
#define I3000_C0DRB	0x100
#define I3000_C1DRB	0x180
#define I3000_C0DRA	0x108
#define I3000_C1DRA	0x188
#define I3000_C0DRC0	0x120
#define I3000_C0DRC1	0x124

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
    uint32_t mchbar;
    uint8_t edeap;
    uint32_t deap;
    uint8_t derrsyn;
    uint16_t errsts;
    uint16_t errcmd;
    uint8_t c0drb[4];
    uint8_t c1drb[4];
    uint8_t c0dra[2];
    uint8_t c1dra[2];
    uint32_t c0drc0;
    uint32_t c0drc1;
};

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= I3000_C0DRB && addr < I3000_C0DRB + 4) {
        val = s->c0drb[addr - I3000_C0DRB];
    } else if (addr >= I3000_C1DRB && addr < I3000_C1DRB + 4) {
        val = s->c1drb[addr - I3000_C1DRB];
    } else if (addr >= I3000_C0DRA && addr < I3000_C0DRA + 2) {
        val = s->c0dra[addr - I3000_C0DRA];
    } else if (addr >= I3000_C1DRA && addr < I3000_C1DRA + 2) {
        val = s->c1dra[addr - I3000_C1DRA];
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Driver only reads from MMIO space during probe */
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
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

    /* Initialize DRB (DRAM Rank Boundary) registers to simulate populated memory.
     * Values must be increasing to simulate valid cumulative sizes. */
    for (int i = 0; i < 4; i++) {
        s->c0drb[i] = i + 1;
        s->c1drb[i] = i + 1;
    }
    
    /* Initialize DRA (DRAM Rank Attribute) registers to simulate interleaved mode */
    for (int i = 0; i < 2; i++) {
        s->c0dra[i] = 0x11;
        s->c1dra[i] = 0x11;
    }
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
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_INTEL_3000_HB );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_BRIDGE_HOST );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0x60, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* Setup W1C (Write-1-to-Clear) mask for ERRSTS register */
    pci_set_word(pdev->wmask + I3000_ERRSTS, 0);
    pci_set_word(pdev->w1cmask + I3000_ERRSTS, I3000_ERRSTS_BITS);

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = I3000_MMR_WINDOW_SIZE;
    s->bar_info[0].name = "mchbar";
      
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
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

/* Intercept config reads to map MCHBAR to the dynamically assigned BAR0 address */
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int l)
{
    if (addr >= I3000_MCHBAR && addr < I3000_MCHBAR + 4) {
        uint32_t bar0 = pci_default_read_config(pdev, PCI_BASE_ADDRESS_0, 4);
        int offset = addr - I3000_MCHBAR;
        return (bar0 >> (offset * 8)) & ((1ULL << (l * 8)) - 1);
    }
    return pci_default_read_config(pdev, addr, l);
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
    k->config_read = pcibase_config_read;
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
