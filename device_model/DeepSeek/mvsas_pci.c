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

/* Type definitions for kernel types used in driver structures */
typedef uint32_t u32;
typedef uint8_t u8;
typedef int irqreturn_t;

/* Forward declarations for kernel structs used as pointers */
struct scatterlist;
struct sas_identify_frame;
struct sas_phy_linkrates;
struct mvs_prv_info;

#define TYPE_PCIBASE_DEVICE "mvsas_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
/* PCI IDs: first entry from mvs_pci_table */
#define VENDOR_ID PCI_VENDOR_ID_MARVELL
#define DEVICE_ID 0x6320
#define CLASS_ID  PCI_CLASS_STORAGE_SCSI

/* Chip types enumeration (from mvs_chips array) */
enum chip_type {
    chip_6320,
    chip_6440,
    chip_6485,
    chip_9180,
    chip_9480,
    chip_9445,
    chip_9485,
    chip_1300,
    chip_1320
};

/* Dispatch table structure - exactly as in driver, with kernel types replaced by void* for unsupported structs */
struct mvs_dispatch {
    char *name;
    int (*chip_init)(void *);
    int (*spi_init)(void *);
    int (*chip_ioremap)(void *);
    void (*chip_iounmap)(void *);
    irqreturn_t (*isr)(void *, int, u32);
    u32 (*isr_status)(void *, int);
    void (*interrupt_enable)(void *);
    void (*interrupt_disable)(void *);
    u32 (*read_phy_ctl)(void *, u32);
    void (*write_phy_ctl)(void *, u32, u32);
    u32 (*read_port_cfg_data)(void *, u32);
    void (*write_port_cfg_data)(void *, u32, u32);
    void (*write_port_cfg_addr)(void *, u32, u32);
    u32 (*read_port_vsr_data)(void *, u32);
    void (*write_port_vsr_data)(void *, u32, u32);
    void (*write_port_vsr_addr)(void *, u32, u32);
    u32 (*read_port_irq_stat)(void *, u32);
    void (*write_port_irq_stat)(void *, u32, u32);
    u32 (*read_port_irq_mask)(void *, u32);
    void (*write_port_irq_mask)(void *, u32, u32);
    void (*command_active)(void *, u32);
    void (*clear_srs_irq)(void *, u8, u8);
    void (*issue_stop)(void *, int, u32);
    void (*start_delivery)(void *, u32);
    u32 (*rx_update)(void *);
    void (*int_full)(void *);
    u8 (*assign_reg_set)(void *, u8 *);
    void (*free_reg_set)(void *, u8 *);
    u32 (*prd_size)(void);
    u32 (*prd_count)(void);
    void (*make_prd)(struct scatterlist *, int, void *);
    void (*detect_porttype)(void *, int);
    int (*oob_done)(void *, int);
    void (*fix_phy_info)(void *, int, struct sas_identify_frame *);
    void (*phy_work_around)(void *, int);
    void (*phy_set_link_rate)(void *, u32, struct sas_phy_linkrates *);
    u32 (*phy_max_link_rate)(void);
    void (*phy_disable)(void *, u32);
    void (*phy_enable)(void *, u32);
    void (*phy_reset)(void *, u32, int);
    void (*stp_reset)(void *, u32);
    void (*clear_active_cmds)(void *);
    u32 (*spi_read_data)(void *);
    void (*spi_write_data)(void *, u32);
    int (*spi_buildcmd)(void *, u32 *, u8, u8, u8, u32);
    int (*spi_issuecmd)(void *, u32);
    int (*spi_waitdataready)(void *, u32);
    void (*dma_fix)(void *, u32, int, int, void *);
    void (*tune_interrupt)(void *, u32);
    void (*non_spec_ncq_error)(void *);
    int (*gpio_write)(struct mvs_prv_info *, u8, u8, u8, u8 *);
};

/* Chip info structure - exactly as in driver */
struct mvs_chip_info {
    u32 n_host;
    u32 n_phy;
    u32 fis_offs;
    u32 fis_count;
    u32 srs_sz;
    u32 sg_width;
    u32 slot_width;
    const struct mvs_dispatch *dispatch;
};

/* Static chip information table (from mvs_chips[]) */
static const struct mvs_chip_info mvs_chips[] = {
    [chip_6320] = { 1, 2, 0x400, 17, 16, 6,  9, NULL },
    [chip_6440] = { 1, 4, 0x400, 17, 16, 6,  9, NULL },
    [chip_6485] = { 1, 8, 0x800, 33, 32, 6, 10, NULL },
    [chip_9180] = { 2, 4, 0x800, 17, 64, 8,  9, NULL },
    [chip_9480] = { 2, 4, 0x800, 17, 64, 8,  9, NULL },
    [chip_9445] = { 1, 4, 0x800, 17, 64, 8, 11, NULL },
    [chip_9485] = { 2, 4, 0x800, 17, 64, 8, 11, NULL },
    [chip_1300] = { 1, 4, 0x400, 17, 16, 6,  9, NULL },
    [chip_1320] = { 2, 4, 0x800, 17, 64, 8,  9, NULL },
};

/* Command header structure extracted from supplementary source */
struct mvs_cmd_hdr {
    uint32_t   flags;      /* PRD tbl len; SAS, SATA ctl */
    uint32_t   lens;       /* cmd, max resp frame len */
    uint32_t   tags;       /* targ port xfer tag; tag */
    uint32_t   data_len;   /* data xfer len */
    uint64_t   cmd_tbl;    /* command table address */
    uint64_t   open_frame; /* open addr frame address */
    uint64_t   status_buf; /* status buffer address */
    uint64_t   prd_tbl;    /* PRD tbl address */
    uint32_t   reserved[4];
};

/* BAR type enum */
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
    uint32_t intr_status;  /* Interrupt status bits */
    uint32_t intr_mask;    /* Interrupt mask bits */

    /* Hardware Register Shadows (The 'Identity' of the device) */
    /* Pointer to MMIO register space (to be defined) */
    uint8_t *regs;

    /* DMA Context */
    /* TX delivery ring */
    void *tx;
    dma_addr_t tx_dma;
    uint32_t tx_prod;
    /* RX completion ring */
    void *rx;
    dma_addr_t rx_dma;
    uint32_t rx_cons;
    /* RX FIS area */
    void *rx_fis;
    dma_addr_t rx_fis_dma;
    /* Command header slots */
    struct mvs_cmd_hdr *slot;
    dma_addr_t slot_dma;
    /* Bulk data buffers */
    void *bulk_buffer;
    dma_addr_t bulk_buffer_dma;
    void *bulk_buffer1;
    dma_addr_t bulk_buffer_dma1;
    /* DMA pool */
    void *dma_pool;

    /* Operational status flags */
    unsigned long flags;

    /* Chip identity */
    uint32_t chip_id;
    const struct mvs_chip_info *chip;
};

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* Generic read: return 0 for any address. */
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Generic write: ignore. */
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    s->intr_status = 0;
    s->intr_mask = 0;
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
        /* Not used */
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* Chip identity: first entry -> chip_6320 */
    s->chip_id = 0;
    s->chip = &mvs_chips[chip_6320];

    /* BAR initialization: single BAR for MMIO registers mapped to BAR 0.
     * BAR size increased to 0x20000 to accommodate TRASH_BUCKET_SIZE offset
     * for regs_ex mapping, fixing probe failure -12 (ENOMEM). */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x20000;
    s->bar_info[0].name = "mvsas-bar0";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No MSI/MSI-X used. */
    /* No DMA configuration needed. */
    /* No timers needed. */
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
    .name = "mvsas_pci",
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
