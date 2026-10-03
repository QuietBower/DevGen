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
#include "hw/pci/pci_ids.h"

#define TYPE_PCIBASE_DEVICE "agpgart_intel_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define AGP_APERTURE_BAR	0
#define PCI_DEVICE_ID_INTEL_82441  0x1237 /* from pci_ids.h */
#define PCI_VENDOR_ID_INTEL        0x8086
#define PCI_CLASS_BRIDGE_HOST      0x0600
#define INTEL_APSIZE	0xb4
#define INTEL_AGPCTRL	0xb0
#define INTEL_NBXCFG	0x50
#define INTEL_ATTBASE	0xb8
#define INTEL_ERRSTS	0x91
#define INTEL_815_APCONT	0x51
#define INTEL_815_ATTBASE_MASK	~0x1FFFFFFF
#define INTEL_I820_RDCR		0x51
#define INTEL_I820_ERRSTS	0xc8
#define INTEL_I840_ERRSTS	0xc8
#define INTEL_I840_MCHCFG	0x50
#define AGP_APBASE	0x10
#define INTEL_I845_ERRSTS	0xc8
#define INTEL_I845_AGPM		0x51
#define INTEL_I850_MCHCFG	0x50
#define INTEL_I850_ERRSTS	0xc8
#define INTEL_I860_MCHCFG	0x50
#define INTEL_I860_ERRSTS	0xc8
#define INTEL_I830_ERRSTS	0x92
#define INTEL_I7505_MCHCFG	0x50
#define PCI_DEVICE_ID_INTEL_PINEVIEW_M_HB        0xA010
#define PCI_DEVICE_ID_INTEL_IRONLAKE_MA_HB	    0x0062
#define PCI_DEVICE_ID_INTEL_EAGLELAKE_HB        0x2E00
#define PCI_DEVICE_ID_INTEL_Q35_HB          0x29B0
#define PCI_DEVICE_ID_INTEL_IRONLAKE_MC2_HB    0x006a
#define PCI_DEVICE_ID_INTEL_IRONLAKE_D_HB	    0x0040
#define PCI_DEVICE_ID_INTEL_82965GME_HB     0x2A10
#define PCI_DEVICE_ID_INTEL_Q33_HB          0x29D0
#define PCI_DEVICE_ID_INTEL_E7221_HB	0x2588
#define PCI_DEVICE_ID_INTEL_GM45_HB         0x2A40
#define PCI_DEVICE_ID_INTEL_82946GZ_HB      0x2970
#define PCI_DEVICE_ID_INTEL_G45_HB          0x2E20
#define PCI_DEVICE_ID_INTEL_82965G_HB       0x29A0
#define PCI_DEVICE_ID_INTEL_G33_HB          0x29C0
#define PCI_DEVICE_ID_INTEL_B43_HB          0x2E40
#define PCI_DEVICE_ID_INTEL_IRONLAKE_M_HB	    0x0044
#define PCI_DEVICE_ID_INTEL_82965Q_HB       0x2990
#define PCI_DEVICE_ID_INTEL_B43_1_HB        0x2E90
#define PCI_DEVICE_ID_INTEL_PINEVIEW_HB         0xA000
#define PCI_DEVICE_ID_INTEL_G41_HB          0x2E30
#define PCI_DEVICE_ID_INTEL_82G35_HB     0x2980
#define PCI_DEVICE_ID_INTEL_Q45_HB          0x2E10
#define PCI_DEVICE_ID_INTEL_82965GM_HB      0x2A00
#define PCI_DEVICE_ID_INTEL_82945GME_HB     0x27AC
#define PCI_DEVICE_ID_INTEL_IRONLAKE_D2_HB	    0x0069

#define INTEL_APERTURE_BAR0_SIZE (256 * 1024 * 1024)

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

/* Additional static definitions from driver */
static const struct gatt_mask { unsigned long mask; uint32_t type; } intel_generic_masks[] =
{
    {.mask = 0x00000017, .type = 0}
};

enum aper_size_type {
    U8_APER_SIZE,
    U16_APER_SIZE,
    U32_APER_SIZE,
    LVL2_APER_SIZE,
    FIXED_APER_SIZE
};

struct aper_size_info_16 {
    int size;
    int num_entries;
    int page_order;
    uint16_t size_value;
};
struct aper_size_info_8 {
    int size;
    int num_entries;
    int page_order;
    uint8_t size_value;
};

static const struct aper_size_info_8 intel_815_sizes[2] =
{
    {64, 16384, 4, 0},
    {32, 8192, 3, 8},
};
static const struct aper_size_info_8 intel_8xx_sizes[7] =
{
    {256, 65536, 6, 0},
    {128, 32768, 5, 32},
    {64, 16384, 4, 48},
    {32, 8192, 3, 56},
    {16, 4096, 2, 60},
    {8, 2048, 1, 62},
    {4, 1024, 0, 63}
};
static const struct aper_size_info_16 intel_generic_sizes[7] =
{
    {256, 65536, 6, 0},
    {128, 32768, 5, 32},
    {64, 16384, 4, 48},
    {32, 8192, 3, 56},
    {16, 4096, 2, 60},
    {8, 2048, 1, 62},
    {4, 1024, 0, 63}
};
static const struct aper_size_info_8 intel_830mp_sizes[4] =
{
    {256, 65536, 6, 0},
    {128, 32768, 5, 32},
    {64, 16384, 4, 48},
    {32, 8192, 3, 56}
};

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    uint64_t val = 0;
    /* No registers accessed via MMIO; return 0 */
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* No registers accessed via MMIO; ignore writes */
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    uint64_t val = 0;
    /* No registers accessed via PIO; return 0 */
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* No registers accessed via PIO; ignore writes */
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
    pci_device_reset(PCI_DEVICE(dev));
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
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_INTEL_82441 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_BRIDGE_HOST );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* Initialize AGP-specific config space to a known state (256MB aperture) */
    pci_set_word(pci_conf + INTEL_APSIZE, 0x0000);    /* size_value for 256MB */
    pci_set_long(pci_conf + INTEL_ATTBASE, 0);       /* aperture base */
    pci_set_long(pci_conf + INTEL_AGPCTRL, 0);       /* AGP control */
    pci_set_word(pci_conf + INTEL_NBXCFG, (1 << 9)); /* enable aperture */
    /* Clear error status */
    pci_set_word(pci_conf + INTEL_ERRSTS, 0);
    pci_set_word(pci_conf + INTEL_ERRSTS + 1, 0);

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0].index = AGP_APERTURE_BAR;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = INTEL_APERTURE_BAR0_SIZE;  /* defined by driver, required */
    s->bar_info[0].name = "agp-aperture";
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

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "agpgart_intel_pci",
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
