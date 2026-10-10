/*
 * QEMU PCI device model for VIA AGP (agpgart-via)
 * Phase 2: Functional behavior for PCI config space accesses only.
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
/* #HeadFile# - no additional kernel headers are included in QEMU model */

#define TYPE_PCIBASE_DEVICE "agpgart_via_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
/* #Related_Config_Info# */

/* PCI IDs: use first entry from agp_via_pci_table (via ID(x) macro) */
#define PCI_VENDOR_ID_VIA        0x1106
#define PCIBASE_VENDOR_ID   PCI_VENDOR_ID_VIA
/* First device in via_agp_device_ids[] is PCI_DEVICE_ID_VIA_82C597_0 */
#define PCI_DEVICE_ID_VIA_82C597_0   0x0597
#define PCIBASE_DEVICE_ID   PCI_DEVICE_ID_VIA_82C597_0

/* Class from ID(x) macro: (PCI_CLASS_BRIDGE_HOST << 8) */
#define PCI_CLASS_BRIDGE_HOST       0x0600
#define PCIBASE_CLASS_ID    PCI_CLASS_BRIDGE_HOST

/* VIA-specific AGP/bridge configuration registers (PCI config space offsets) */
#define VIA_GARTCTRL        0x80
#define VIA_APSIZE          0x84
#define VIA_ATTBASE         0x88
#define VIA_AGP3_GARTCTRL   0x90
#define VIA_AGP3_APSIZE     0x94
#define VIA_AGP3_ATTBASE    0x98
#define VIA_AGPSEL          0xfd

/* Generic AGP capability register offsets (relative to AGP cap pointer) */
#define AGPSTAT             0x04
#define AGPCMD              0x08
#define AGPNISTAT           0x0c
#define AGPNICMD            0x20

/* AGP status/control bits from driver */
#define AGPSTAT_MODE_3_0    (1 << 3)
#define AGPSTAT_FW          (1 << 4)
#define AGPSTAT_AGP_ENABLE  (1 << 8)
#define AGPSTAT_SBA         (1 << 9)
#define AGPSTAT_CAL_MASK    ((1 << 12) | (1 << 11) | (1 << 10))
#define AGPSTAT_ARQSZ       ((1 << 15) | (1 << 14) | (1 << 13))
#define AGPSTAT_RQ_DEPTH    0xff000000U

#define AGPSTAT2_1X         (1 << 0)
#define AGPSTAT2_2X         (1 << 1)
#define AGPSTAT2_4X         (1 << 2)
#define AGPSTAT3_RSVD       (1 << 2)
#define AGPSTAT3_4X         (1 << 0)
#define AGPSTAT3_8X         (1 << 1)

#define AGP3_RESERVED_MASK  0x00ff00c4U
#define AGP2_RESERVED_MASK  0x00fffcc8U

/* Misc driver constants */
#define AGP_APERTURE_BAR    0
#define AGP_MAJOR_VERSION_SHIFT 20
#define AGP_MINOR_VERSION_SHIFT 16
#define MAXKEY              (4096 * 32)

/* Error/errata flags */
#define AGP_ERRATA_FASTWRITES (1 << 0)
#define AGP_ERRATA_SBA        (1 << 1)
#define AGP_ERRATA_1X         (1 << 2)

/* AGPGART version from driver */
#define AGPGART_VERSION_MAJOR 0
#define AGPGART_VERSION_MINOR 103

/* Aperture sizing structures (layout copied from driver for reference) */
struct aper_size_info_8 {
    int size;
    int num_entries;
    int page_order;
    uint8_t size_value;
};

struct aper_size_info_16 {
    int size;
    int num_entries;
    int page_order;
    uint16_t size_value;
};

struct aper_size_info_32 {
    int size;
    int num_entries;
    int page_order;
    uint32_t size_value;
};

struct aper_size_info_fixed {
    int size;
    int num_entries;
    int page_order;
};

struct aper_size_info_lvl2 {
    int size;
    int num_entries;
    uint32_t size_value;
};

/* Aperture size type enumeration from driver */
enum aper_size_type {
    U8_APER_SIZE,
    U16_APER_SIZE,
    U32_APER_SIZE,
    LVL2_APER_SIZE,
    FIXED_APER_SIZE,
};

/* Generic aperture size table used by via_driver */
static const struct aper_size_info_8 via_generic_sizes[9] __attribute__((unused)) = {
    {256, 65536, 6, 0},
    {128, 32768, 5, 128},
    {64, 16384, 4, 192},
    {32, 8192, 3, 224},
    {16, 4096, 2, 240},
    {8, 2048, 1, 248},
    {4, 1024, 0, 252},
    {2, 512, 0, 254},
    {1, 256, 0, 255},
};

/* Forward declarations for driver-related structs from supplementary source */
struct agp_bridge_data;
struct agp_memory;
struct page;
struct pci_dev;
struct vm_operations_struct;
struct module;
struct gatt_mask;
struct agp_3_5_dev;
struct list_head;
struct scatterlist;
struct vm_area_struct;
struct vm_fault;
struct mempolicy;
struct address_space;
struct page_pool;
struct llist_node;
struct rcu_head;
struct pci_bus;
struct proc_dir_entry;
struct pci_slot;
struct aer_info;
struct rcec_ea;
struct pci_driver;
struct device_dma_parameters;
struct device;
struct resource;
struct pcie_link_state;
struct pci_p2pdma;
struct xarray;
struct npem;
struct pci_vpd;
struct aer_info;
struct agp_version;
struct agp_bridge_driver;

typedef unsigned long dma_addr_t;

typedef struct { int counter; } atomic_t;

typedef struct list_head {
    struct list_head *next;
    struct list_head *prev;
} list_head;

typedef struct {
    int dummy;
} spinlock_t;

typedef enum chipset_type {
    NOT_SUPPORTED,
    SUPPORTED,
} chipset_type;

/* Simplified AGP bridge data structure (subset needed for identification) */
struct agp_bridge_data {
    const struct agp_version *version;
    const struct agp_bridge_driver *driver;
    const struct vm_operations_struct *vm_ops;
    void *previous_size;
    void *current_size;
    void *dev_private_data;
    struct pci_dev *dev;
    uint32_t *gatt_table;
    uint32_t *gatt_table_real;
    unsigned long scratch_page;
    struct page *scratch_page_page;
    dma_addr_t scratch_page_dma;
    unsigned long gart_bus_addr;
    unsigned long gatt_bus_addr;
    uint32_t mode;
    unsigned long *key_list;
    atomic_t current_memory_agp;
    atomic_t agp_in_use;
    int max_memory_agp;
    int aperture_size_idx;
    int capndx;
    int flags;
    char major_version;
    char minor_version;
    struct list_head list;
    uint32_t apbase_config;
    struct list_head mapped_list;
    spinlock_t mapped_lock;
};

/* AGP bridge driver description (structure layout) */
struct gatt_mask {
    unsigned long mask;
    uint32_t type;
};

struct agp_bridge_driver {
    struct module *owner;
    const void *aperture_sizes;
    int num_aperture_sizes;
    enum aper_size_type size_type;
    bool cant_use_aperture;
    bool needs_scratch_page;
    const struct gatt_mask *masks;
    int (*fetch_size)(void);
    int (*configure)(void);
    void (*agp_enable)(struct agp_bridge_data *, uint32_t);
    void (*cleanup)(void);
    void (*tlb_flush)(struct agp_memory *);
    unsigned long (*mask_memory)(struct agp_bridge_data *, dma_addr_t, int);
    void (*cache_flush)(void);
    int (*create_gatt_table)(struct agp_bridge_data *);
    int (*free_gatt_table)(struct agp_bridge_data *);
    int (*insert_memory)(struct agp_memory *, off_t, int);
    int (*remove_memory)(struct agp_memory *, off_t, int);
    struct agp_memory *(*alloc_by_type)(size_t, int);
    void (*free_by_type)(struct agp_memory *);
    struct page *(*agp_alloc_page)(struct agp_bridge_data *);
    int (*agp_alloc_pages)(struct agp_bridge_data *, struct agp_memory *, size_t);
    void (*agp_destroy_page)(struct page *, int flags);
    void (*agp_destroy_pages)(struct agp_memory *);
    int (*agp_type_to_mask_type)(struct agp_bridge_data *, int);
};

/* AGP version structure from driver */
struct agp_version {
    uint16_t major;
    uint16_t minor;
};

/* agp_device_ids used to match chipset variants */
struct agp_device_ids {
    unsigned short device_id;
    enum chipset_type chipset;
    const char *chipset_name;
    int (*chipset_setup)(struct pci_dev *pdev);
};

/* Example VIA AGP chipset table from driver (for reference only) */
static struct agp_device_ids via_agp_device_ids[] __attribute__((unused)) = {
    {
        .device_id   = PCI_DEVICE_ID_VIA_82C597_0,
        .chipset_name = "Apollo VP3",
    },
    {
        .device_id   = 0x0598,
        .chipset_name = "Apollo MVP3",
    },
    {
        .device_id   = 0x0501,
        .chipset_name = "Apollo MVP4",
    },
    {
        .device_id   = 0x0601,
        .chipset_name = "Apollo ProMedia/PLE133Ta",
    },
    {
        .device_id   = 0x0691,
        .chipset_name = "Apollo Pro 133",
    },
    {
        .device_id   = 0x0391,
        .chipset_name = "KX133",
    },
    {
        .device_id   = 0x3091,
        .chipset_name = "Pro 266",
    },
    {
        .device_id   = 0x3156,
        .chipset_name = "Apollo Pro266",
    },
    {
        .device_id   = 0x3112,
        .chipset_name = "KLE133",
    },
    {
        .device_id   = 0x0305,
        .chipset_name = "Twister-K/KT133x/KM133",
    },
    {
        .device_id   = 0x3128,
        .chipset_name = "P4X266",
    },
    {
        .device_id   = 0x3099,
        .chipset_name = "KT266/KY266x/KT333",
    },
    {
        .device_id   = 0x3101,
        .chipset_name = "Pro266T",
    },
    {
        .device_id   = 0x3116,
        .chipset_name = "PM266/KM266",
    },
    {
        .device_id   = 0x3123,
        .chipset_name = "CLE266",
    },
    {
        .device_id   = 0x3189,
        .chipset_name = "KT400/KT400A/KT600",
    },
    {
        .device_id   = 0x0605,
        .chipset_name = "ProSavage PM133/PL133/PN133",
    },
    {
        .device_id   = 0x3148,
        .chipset_name = "P4M266x/P4N266",
    },
    {
        .device_id   = 0x3168,
        .chipset_name = "PT800",
    },
    {
        .device_id   = 0x0198,
        .chipset_name = "P4X600",
    },
    {
        .device_id   = 0x3205,
        .chipset_name = "KM400/KM400A",
    },
    {
        .device_id   = 0x0258,
        .chipset_name = "PT880",
    },
    {
        .device_id   = 0x0308,
        .chipset_name = "PT880 Ultra",
    },
    {
        .device_id   = 0x3208,
        .chipset_name = "PT890",
    },
    {
        .device_id   = 0x0259,
        .chipset_name = "PM800/PN800/PM880/PN880",
    },
    {
        .device_id   = 0x0269,
        .chipset_name = "KT880",
    },
    {
        .device_id   = 0xB198,
        .chipset_name = "VT83xx/VT87xx/KTxxx/Px8xx",
    },
    {
        .device_id   = 0x0296,
        .chipset_name = "P4M800",
    },
    {
        .device_id   = 0x0314,
        .chipset_name = "VT3314",
    },
    {
        .device_id   = 0x0324,
        .chipset_name = "CX700",
    },
    {
        .device_id   = 0x0327,
        .chipset_name = "P4M890",
    },
    {
        .device_id   = 0x0364,
        .chipset_name = "P4M900",
    },
};

/* Simple maxes table from driver */
static const struct { int mem; int agp; } maxes_table[] __attribute__((unused)) = {
    {0, 0},
    {32, 4},
    {64, 28},
    {128, 96},
    {256, 204},
    {512, 440},
    {1024, 942},
    {2048, 1920},
    {4096, 3932},
};


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
    /* #Interrupt_Stru# - VIA AGP driver does not define device-specific IRQ regs */

    /* Hardware Register Shadows (The 'Identity' of the device) */
    /* #Reg_Stru# - no explicit MMIO register structs in via-agp driver */

    /* DMA Context */
    /* #DMA_Info_Stru# - VIA AGP core uses system memory/DMA via generic AGP, not device-specific DMA regs */

    /* #Status_Stru#      - no additional operational status flags in driver */
    /* #Probe_Reset_Stru# - no dedicated reset state structure in driver */
    /* #Pow_Man_Stru#     - power management handled by generic PCI/PM, no extra state */
    /* #Other_Addition_Info_Stru# */
};

/* #Other_Addition_Info_Defin# - no extra helper structs required for static skeleton */

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* No device-local IRQ status registers are manipulated by via-agp driver. */
    (void)pdev;
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* VIA AGP driver does not expose explicit device DMA engine registers. */
    (void)s;
    (void)pdev;
    (void)is_write;
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* via-agp driver only uses PCI config space; no MMIO BARs are accessed. */
    (void)s;
    (void)addr;
    (void)size;
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* via-agp driver only uses PCI config space; no MMIO BARs are accessed. */
    (void)s;
    (void)addr;
    (void)val;
    (void)size;
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Port I/O is not used by via-agp driver. */
    (void)s;
    (void)addr;
    (void)size;
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    /* Port I/O is not used by via-agp driver. */
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

    /* No additional device-specific reset state beyond generic PCI config. */
    (void)s;
}

static hwaddr pcibase_pow2ceil(hwaddr size)
{
    if (size <= 1) {
        return 1;
    }
    size--;
    size |= size >> 1;
    size |= size >> 2;
    size |= size >> 4;
    size |= size >> 8;
    size |= size >> 16;
#if HOST_LONG_BITS >= 64
    size |= (hwaddr)size >> 32;
#endif
    size++;
    return size;
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }
    
    /* CRITICAL: PCI requires BAR sizes to be a power of 2. Prevent QEMU assert crash. */
    hwaddr aligned_size = pcibase_pow2ceil(bi->size);
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

    /* Expose PCI Express capability at 0x80 (location chosen in skeleton). */
    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);

    /* Add Power Management capability; driver does not inspect its contents. */
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    int agp_pos = pci_add_capability(pdev, PCI_CAP_ID_AGP, 0, 12, errp);
    if (agp_pos > 0) {
        /* AGP offset 2: Minor version, offset 3: Major version. 
         * Forcing AGP 3.0 here, but adjust to 2.0 if you are emulating an older board. */
        pci_set_byte(pci_conf + agp_pos + 2, 0x00); 
        pci_set_byte(pci_conf + agp_pos + 3, 0x03); 

        /* AGP offset 4: AGP Status Register (AGPSTAT).
         * Provide a dummy status showing AGP 3.0 support and 8x speed so the driver sees valid modes. */
        pci_set_long(pci_conf + agp_pos + 4, AGPSTAT_MODE_3_0 | AGPSTAT3_8X | AGPSTAT_AGP_ENABLE);
    }
    
    /* BAR Initialization */
    /* VIA AGP driver uses PCI config space only; no explicit BAR sizes are defined. */
    s->num_bars = 0;
    for (int i = 0; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = "unused";
    }

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No MSI/MSI-X are used by via-agp driver. */
    s->has_msi = false;
    s->has_msix = false;

    /* No device-local DMA engine is exposed; AGP memory ops are CPU-driven. */

    /* No device-local timers are referenced in the driver. */

    /* No additional field initialization required beyond generic PCI config. */
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, 0);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }

    /* No dynamically allocated device-local resources to free. */
    (void)s;
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "agpgart_via_pci",
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

