/*
 * QEMU PCI device model skeleton for agpgart_intel_pci (intel-agp)
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
#include "hw/pci/pci_ids.h"

/* Minimal kernel-style type definitions and forward declarations to satisfy compiler */
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t  u8;

typedef struct page page;

typedef struct {
    int counter;
} atomic_t;

struct list_head {
    struct list_head *next;
    struct list_head *prev;
};

typedef struct {
    int lock;
} spinlock_t;

struct agp_memory;
struct vm_operations_struct;
struct pci_dev;
struct module;

typedef uint64_t dma_addr_t;

#define __iomem

#define KB(x)                     ((x) * 1024)
#define MB(x)                     (KB(KB(x)))

/* MiB macro already provided by qemu/units.h; avoid redefinition. */

#define TYPE_PCIBASE_DEVICE "agpgart_intel_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
/* Define VENDOR_ID, DEVICE_ID, CLASS_ID, and register offsets here */
#define PCIBASE_VENDOR_ID PCI_VENDOR_ID_INTEL
#define PCIBASE_DEVICE_ID 0x7180
#define PCIBASE_CLASS_ID  (PCI_CLASS_BRIDGE_HOST)

#define INTEL_APSIZE              0xb4
#define INTEL_AGPCTRL             0xb0
#define INTEL_NBXCFG              0x50
#define INTEL_ATTBASE             0xb8
#define AGP_APERTURE_BAR          0
#define INTEL_ERRSTS              0x91
#define INTEL_815_APCONT          0x51
#define INTEL_815_ATTBASE_MASK    ~0x1FFFFFFF
#define INTEL_I820_RDCR           0x51
#define INTEL_I820_ERRSTS         0xc8
#define INTEL_I840_MCHCFG         0x50
#define INTEL_I840_ERRSTS         0xc8
#define AGP_APBASE                0x10
#define INTEL_I845_ERRSTS         0xc8
#define INTEL_I845_AGPM           0x51
#define INTEL_I850_MCHCFG         0x50
#define INTEL_I850_ERRSTS         0xc8
#define INTEL_I860_ERRSTS         0xc8
#define INTEL_I860_MCHCFG         0x50
#define INTEL_I830_ERRSTS         0x92
#define INTEL_I7505_MCHCFG        0x50
#define AGP_APBASE_REG            AGP_APBASE
#define AGPCTRL_REG               0x10
#define AGPSTAT                   0x4
#define AGPCMD                    0x8
#define AGPNICMD                  0x20
#define AGPNISTAT                 0xc
#define INTEL_I830_GMCH_CTRL      0x52
#define I810_PGETBL_CTL           0x2020
#define I965_PGETBL_CTL2          0x20c4
#define GFX_FLSH_CNTL             0x2170
#define I810_SMRAM_MISCC          0x70

#define AGPSTAT_MODE_3_0          (1 << 3)
#define AGPSTAT_AGP_ENABLE        (1 << 8)
#define AGPSTAT_RQ_DEPTH          0xff000000
#define AGPSTAT_FW                (1 << 4)
#define AGPSTAT_SBA               (1 << 9)
#define AGPSTAT3_RSVD             (1 << 2)
#define AGPSTAT2_1X               (1 << 0)
#define AGPSTAT2_2X               (1 << 1)
#define AGPSTAT2_4X               (1 << 2)
#define AGPSTAT3_4X               (1 << 0)
#define AGPSTAT3_8X               (1 << 1)
#define AGPSTAT_CAL_MASK          ((1 << 12) | (1 << 11) | (1 << 10))
#define AGPSTAT_ARQSZ             ((1 << 15) | (1 << 14) | (1 << 13))

#define AGP3_RESERVED_MASK        0x00ff00c4
#define AGP2_RESERVED_MASK        0x00fffcc8

#define AGP_ERRATA_FASTWRITES     (1 << 0)
#define AGP_ERRATA_SBA            (1 << 1)
#define AGP_ERRATA_1X             (1 << 2)

#define I810_PGETBL_ENABLED       0x00000001

#define I965_PGETBL_SIZE_MASK     0x0000000e
#define I965_PGETBL_SIZE_128KB    (2 << 1)
#define I965_PGETBL_SIZE_256KB    (1 << 1)
#define I965_PGETBL_SIZE_512KB    (0 << 1)
#define I965_PGETBL_SIZE_1MB      (3 << 1)
#define I965_PGETBL_SIZE_1_5MB    (5 << 1)
#define I965_PGETBL_SIZE_2MB      (4 << 1)

#define I810_GFX_MEM_WIN_SIZE     0x00010000
#define I810_GFX_MEM_WIN_32M      0x00010000

#define I830_GMCH_MEM_64M         0x1
#define I830_GMCH_MEM_MASK        0x1
#define I830_GMCH_ENABLED         0x4
#define I830_GMCH_GMS_LOCAL       0x10
#define I830_GMCH_GMS_MASK        0x70
#define I830_GMCH_GMS_STOLEN_1024 0x30
#define I830_GMCH_GMS_STOLEN_8192 0x40

#define I855_GMCH_GMS_MASK        0xF0
#define I855_GMCH_GMS_STOLEN_1M   (0x1 << 4)
#define I855_GMCH_GMS_STOLEN_4M   (0x2 << 4)
#define I855_GMCH_GMS_STOLEN_8M   (0x3 << 4)
#define I855_GMCH_GMS_STOLEN_16M  (0x4 << 4)
#define I855_GMCH_GMS_STOLEN_32M  (0x5 << 4)

#define INTEL_GMCH_GMS_STOLEN_96M   (0xa << 4)
#define INTEL_GMCH_GMS_STOLEN_160M  (0xb << 4)
#define INTEL_GMCH_GMS_STOLEN_224M  (0xc << 4)
#define INTEL_GMCH_GMS_STOLEN_352M  (0xd << 4)

#define G33_GMCH_GMS_STOLEN_128M  (0x8 << 4)
#define G33_GMCH_GMS_STOLEN_256M  (0x9 << 4)

#define I915_GMCH_GMS_STOLEN_48M  (0x6 << 4)
#define I915_GMCH_GMS_STOLEN_64M  (0x7 << 4)

#define I830_RDRAM_CHANNEL_TYPE   0x03010
#define I830_RDRAM_DDT(x)         (((x) & 0x18) >> 3)
#define I830_RDRAM_ND(x)          (((x) & 0x20) >> 5)

#define AGP_MAJOR_VERSION_SHIFT   20
#define AGP_MINOR_VERSION_SHIFT   16

#define MAXKEY                    (4096 * 32)

#define I915_GMADR_BAR            2
#define I810_GMADR_BAR            0

#define PFX "agpgart: "
#define DBG(x, y...) do { } while (0)

#define PGE_EMPTY(b, p) (!(p) || (p) == (unsigned long)(b)->scratch_page)

#define A_SIZE_16(x) ((struct aper_size_info_16 *)(x))
#define A_SIZE_8(x)  ((struct aper_size_info_8 *)(x))
#define A_SIZE_32(x) ((struct aper_size_info_32 *)(x))
#define A_SIZE_LVL2(x) ((struct aper_size_info_lvl2 *)(x))
#define A_SIZE_FIX(x) ((struct aper_size_info_fixed *)(x))

#define A_IDX16(bridge) (A_SIZE_16((bridge)->driver->aperture_sizes) + i)
#define A_IDX8(bridge)  (A_SIZE_8((bridge)->driver->aperture_sizes) + i)
#define A_IDX32(bridge) (A_SIZE_32((bridge)->driver->aperture_sizes) + i)

#define alloc_gatt_pages(order) ((char *)__get_free_pages(GFP_KERNEL, (order)))
#define free_gatt_pages(table, order) free_pages((unsigned long)(table), (order))

#define AGP_PAGE_DESTROY_UNMAP 1
#define AGP_PAGE_DESTROY_FREE  2

#define I830_GMCH_MEM_64M       0x1
#define I830_GMCH_MEM_MASK      0x1

#define I810_GFX_MEM_WIN_SIZE   0x00010000

#define GFX_FLSH_CNTL           0x2170

#define I965_PGETBL_SIZE_128KB  (2 << 1)
#define I965_PGETBL_SIZE_512KB  (0 << 1)
#define I965_PGETBL_SIZE_1_5MB  (5 << 1)
#define I965_PGETBL_SIZE_1MB    (3 << 1)
#define I965_PGETBL_SIZE_2MB    (4 << 1)
#define I965_PGETBL_SIZE_256KB  (1 << 1)
#define I965_PGETBL_SIZE_MASK   0x0000000e

#define I965_PGETBL_CTL2        0x20c4

struct agp_version;

struct agp_bridge_data {
    const struct agp_version *version;
    const struct agp_bridge_driver *driver;
    const struct vm_operations_struct *vm_ops;
    void *previous_size;
    void *current_size;
    void *dev_private_data;
    struct pci_dev *dev;
    u32 __iomem *gatt_table;
    u32 *gatt_table_real;
    unsigned long scratch_page;
    struct page *scratch_page_page;
    dma_addr_t scratch_page_dma;
    unsigned long gart_bus_addr;
    unsigned long gatt_bus_addr;
    u32 mode;
    unsigned long *key_list;
    atomic_t current_memory_agp;
    atomic_t agp_in_use;
    int max_memory_agp; /* in number of pages */
    int aperture_size_idx;
    int capndx;
    int flags;
    char major_version;
    char minor_version;
    struct list_head list;
    u32 apbase_config;
    /* list of agp_memory mapped to the aperture */
    struct list_head mapped_list;
    spinlock_t mapped_lock;
};

enum aper_size_type {
    U8_APER_SIZE,
    U16_APER_SIZE,
    U32_APER_SIZE,
    LVL2_APER_SIZE,
    FIXED_APER_SIZE
};

struct gatt_mask;

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
    void (*agp_enable)(struct agp_bridge_data *, u32);
    void (*cleanup)(void);
    void (*tlb_flush)(struct agp_memory *);
    unsigned long (*mask_memory)(struct agp_bridge_data *, dma_addr_t, int);
    void (*cache_flush)(void);
    int (*create_gatt_table)(struct agp_bridge_data *);
    int (*free_gatt_table)(struct agp_bridge_data *);
    int (*insert_memory)(struct agp_memory *, off_t, int);
    int (*remove_memory)(struct agp_memory *, off_t, int);
    struct agp_memory *(*alloc_by_type) (size_t, int);
    void (*free_by_type)(struct agp_memory *);
    struct page *(*agp_alloc_page)(struct agp_bridge_data *);
    int (*agp_alloc_pages)(struct agp_bridge_data *, struct agp_memory *, size_t);
    void (*agp_destroy_page)(struct page *, int flags);
    void (*agp_destroy_pages)(struct agp_memory *);
    int (*agp_type_to_mask_type) (struct agp_bridge_data *, int);
};

struct gatt_mask {
    unsigned long mask;
    u32 type;
    /* totally device specific, for integrated chipsets that
     * might have different types of memory masks.  For other
     * devices this will probably be ignored */
};

struct aper_size_info_8 {
    int size;
    int num_entries;
    int page_order;
    u8 size_value;
};

struct aper_size_info_16 {
    int size;
    int num_entries;
    int page_order;
    u16 size_value;
};

struct aper_size_info_32 {
    int size;
    int num_entries;
    int page_order;
    u32 size_value;
};

struct aper_size_info_fixed {
    int size;
    int num_entries;
    int page_order;
};

struct aper_size_info_lvl2 {
    int size;
    int num_entries;
    u32 size_value;
};

struct intel_agp_driver_description {
    unsigned int chip_id;
    char *name;
    const struct agp_bridge_driver *driver;
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
    /* Recommended: uint32_t intr_status; uint32_t intr_mask; */

    /* Hardware Register Shadows (The 'Identity' of the device) */
    /* Struct or array holding values for Version, ID, and Control regs */

    /* PCI config space shadows for important registers we know the driver touches */
    uint16_t apsize_reg;      /* INTEL_APSIZE (0xb4) */
    uint32_t agpctrl_reg;     /* INTEL_AGPCTRL (0xb0) */
    uint16_t nbxcfg_reg;      /* INTEL_NBXCFG (0x50) */
    uint32_t attbase_reg;     /* INTEL_ATTBASE (0xb8) */
    uint16_t errsts_reg;      /* generic error status (various offsets, modeled simply) */

    /* DMA Context */
    /* No explicit DMA engine is used by this driver, so none is modeled. */

      /* Operational status flags */
      /* State used to handle reset sequences */
      /* Power management state (D0-D3) */
      
};


/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* The intel-agp driver does not use interrupts from this device.
     * Keep IRQ line deasserted. */
    pci_set_irq(pdev, 0);
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* The intel-agp driver does not program any DMA engine on this host bridge.
     * No DMA behavior is required here. Suppress unused variable warning. */
    (void)pdev;
    (void)is_write;
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* The intel-agp driver only accesses PCI config space registers via
     * pci_read_config_* helpers, not MMIO BAR regions. Provide a quiet
     * MMIO region that always reads as zero. */
    (void)s;
    (void)addr;
    (void)size;

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* No MMIO semantics are required for this device in the intel-agp driver. */
    (void)s;
    (void)addr;
    (void)val;
    (void)size;
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Logic for Port I/O (Legacy support) - not used by intel-agp driver. */
    (void)s;
    (void)addr;
    (void)size;
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    /* Logic for Port I/O (Legacy support) - not used by intel-agp driver. */
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
    PCIDevice *pdev = PCI_DEVICE(dev);
    uint8_t *pci_conf = pdev->config;

    pci_device_reset(pdev);

    /* Revert registers to power-on defaults that are relevant to the driver. */

    /* INTEL_APSIZE: choose a value that will match one entry in aperture_sizes
     * tables in the kernel. Since we do not have those tables here, we keep a
     * constant non-zero default and keep shadow/config in sync so that
     * intel_fetch_size/intel_8xx_fetch_size see a stable value. */
    s->apsize_reg = 0x0000;
    pci_set_word(pci_conf + INTEL_APSIZE, s->apsize_reg);

    /* INTEL_AGPCTRL: default 0 */
    s->agpctrl_reg = 0x00000000;
    pci_set_long(pci_conf + INTEL_AGPCTRL, s->agpctrl_reg);

    /* INTEL_NBXCFG (0x50) used by multiple configure/cleanup helpers. 0 at reset. */
    s->nbxcfg_reg = 0x0000;
    pci_set_word(pci_conf + INTEL_NBXCFG, s->nbxcfg_reg);

    /* INTEL_ATTBASE: address of GATT; 0 at reset, driver will program it. */
    s->attbase_reg = 0x00000000;
    pci_set_long(pci_conf + INTEL_ATTBASE, s->attbase_reg);

    /* Generic error status field; cleared on reset. */
    s->errsts_reg = 0x0000;
    /* INTEL_ERRSTS is 0x91 (byte + following byte); set to 0. */
    pci_conf[INTEL_ERRSTS] = 0x00;
    pci_conf[INTEL_ERRSTS + 1] = 0x00;

    /* Also clear other chipset-specific ERRSTS words we know about. */
    pci_set_word(pci_conf + INTEL_I820_ERRSTS, 0x0000);
    pci_set_word(pci_conf + INTEL_I840_ERRSTS, 0x0000);
    pci_set_word(pci_conf + INTEL_I845_ERRSTS, 0x0000);
    pci_set_word(pci_conf + INTEL_I850_ERRSTS, 0x0000);
    pci_set_word(pci_conf + INTEL_I860_ERRSTS, 0x0000);
    pci_set_word(pci_conf + INTEL_I830_ERRSTS, 0x0000);

    pcibase_update_irq(s);
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

    /* Expose an AGP capability at some capability pointer. The intel-agp
     * driver uses pci_find_capability(pdev, PCI_CAP_ID_AGP) to locate it.
     * We create a minimal dummy capability structure with the required ID. */
    int cap_pos = pci_add_capability(pdev, PCI_CAP_ID_AGP, 0, 8, errp);
    if (cap_pos > 0) {
        pci_conf[cap_pos + PCI_CAP_LIST_ID] = PCI_CAP_ID_AGP;
        /* No further AGP capability fields are interpreted by intel-agp
         * beyond reading PCI_AGP_STATUS at bridge->capndx+PCI_AGP_STATUS.
         * We can leave it as zero, which simply means "AGP disabled". */
    }

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* Initialize BAR0 as an MMIO aperture-like region since the driver
     * expects an AGP aperture BAR resource[0] and may call pci_assign_resource
     * and pci_bus_address on it. The content is not accessed directly. */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    /* Choose a reasonable aperture size (e.g., 64MB). */
    s->bar_info[0].size  = 64 * MiB;
    s->bar_info[0].name  = "agpgart-intel-aperture";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No MSI/MSI-X used by this driver; keep disabled. */
    s->has_msi = false;
    s->has_msix = false;

    /* Initialize internal register shadows and config space defaults. */
    pcibase_reset(DEVICE(pdev));

      /* msi_init or msix_init calls */  
      /* Set DMA masks or ring buffer limits */   
      /* Initialize internal hardware timers if used */ 
      /* Final state initialization before the device is 'live' */   
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

    /* Free buffers, stop timers, etc. Currently nothing to clean up. */
    (void)s;
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
