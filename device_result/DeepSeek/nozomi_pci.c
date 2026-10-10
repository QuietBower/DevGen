/*
 * QEMU 8.2.10 PCI device model for Nozomi HSDPA card (driver: nozomi.c)
 * Implements MMIO register interface required for driver probing/init.
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

#define TYPE_PCIBASE_DEVICE "nozomi_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI Identifiers from driver's pci_device_id table */
#define PCI_VENDOR_ID_NOZOMI     0x1931
#define PCI_DEVICE_ID_NOZOMI     0x000c
#define PCI_CLASS_ID_NOZOMI      0x0700  /* Communications - serial */

/* Register offsets (aligned with driver's nozomi_setup_private_data) */
#define REG_IIR  0x1000  /* Interrupt Identification Register (read-only) */
#define REG_FCR  0x1000  /* Flow Control Register (write-only) */
#define REG_IER  0x1004  /* Interrupt Enable Register */

/* Device-specific interrupt masks */
#define RESET      0x4000
#define TOGGLE_VALID 0

/* Configuration table signature */
#define NOZOMI_CONFIG_MAGIC  0xEFEFFEFE

/* Channel masks (from driver) */
#define MDM_DL1   0x0001
#define MDM_UL1   0x0002
#define MDM_DL2   0x0004
#define MDM_UL2   0x0008
#define DIAG_DL1  0x0010
#define DIAG_DL2  0x0020
#define DIAG_UL   0x0040
#define APP1_DL   0x0080
#define APP1_UL   0x0100
#define APP2_DL   0x0200
#define APP2_UL   0x0400
#define CTRL_DL   0x0800
#define CTRL_UL   0x1000

/* Control bits */
#define CTRL_DSR  0x0001
#define CTRL_DCD  0x0002
#define CTRL_RI   0x0004
#define CTRL_CTS  0x0008
#define CTRL_DTR  0x0001
#define CTRL_RTS  0x0002

#define MAX_PORT  4
#define NOZOMI_MAX_PORTS  5

/* Configuration table structure (packed, matching driver) */
#pragma pack(push, 1)
typedef struct {
    uint32_t signature;
    uint16_t version;
    uint16_t product_information;
    struct {
        unsigned int mdm_ul:1;
        unsigned int mdm_dl:1;
        unsigned int diag_dl:1;
        unsigned int enabled:5;  /* toggle fields valid if enabled=0 */
        unsigned int pad:24;
    } toggle;
    uint8_t pad1[7];
    uint16_t dl_start;
    uint16_t dl_mdm_len1;
    uint16_t dl_mdm_len2;
    uint16_t dl_diag_len1;
    uint16_t dl_diag_len2;
    uint16_t dl_app1_len;
    uint16_t dl_app2_len;
    uint16_t dl_ctrl_len;
    uint8_t pad2[16];
    uint16_t ul_start;
    uint16_t ul_mdm_len2;
    uint16_t ul_mdm_len1;
    uint16_t ul_diag_len;
    uint16_t ul_app1_len;
    uint16_t ul_app2_len;
    uint16_t ul_ctrl_len;
} ConfigTable;
#pragma pack(pop)

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

enum card_state {
    NOZOMI_STATE_UNKNOWN    = 0,
    NOZOMI_STATE_ENABLED    = 1,
    NOZOMI_STATE_ALLOCATED  = 2,
    NOZOMI_STATE_READY      = 3,
};

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;
    uint16_t irq_status;  /* pending interrupts (bits from IIR) */
    uint16_t ier;          /* last written IER */

    ConfigTable config_table;  /* configuration table at BAR+0 */
    uint16_t fcr_shadow;       /* last written FCR (unused) */
    enum card_state state;
};

enum card_type {
    F32_2 = 2048,
    F32_8 = 8192,
};

/* Update IRQ line based on enabled and pending interrupts */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->irq_status & s->ier) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* MMIO read handler: decodes address to config table, registers, or data area */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Config table region (0x0000 - 0x0042) */
    if (addr < sizeof(ConfigTable)) {
        if (size == 4 && (addr & 3) == 0) {
            val = ldl_le_p((const void *)((const uint8_t *)&s->config_table + addr));
        } else if (size == 2 && (addr & 1) == 0) {
            val = lduw_le_p((const void *)((const uint8_t *)&s->config_table + addr));
        } else if (size == 1) {
            val = *((const uint8_t *)&s->config_table + addr);
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "Bad read access to config table: addr=0x%" HWADDR_PRIx " size=%d\n",
                          addr, size);
        }
        return val;
    }

    /* Register area */
    if (addr >= REG_IIR && addr < REG_IER + 4) {
        switch (addr) {
        case REG_IIR:  /* IIR read (16-bit) */
            val = s->irq_status;
            break;
        case REG_IER:  /* IER read (16-bit) */
            val = s->ier;
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                          "Bad read at register region: addr=0x%" HWADDR_PRIx "\n", addr);
            break;
        }
        return val;
    }

    /* Data buffer area (dl_start=0x1000, ul_start=0x2000); return 0 */
    return 0;
}

/* MMIO write handler */
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* Config table writes ignored (driver never writes here) */
    if (addr < sizeof(ConfigTable)) {
        return;
    }

    /* Register area */
    if (addr >= REG_IIR && addr < REG_IER + 4) {
        switch (addr) {
        case REG_FCR:  /* FCR write (16-bit): clear bits from irq_status */
            s->irq_status &= ~val;
            pcibase_update_irq(s);
            break;
        case REG_IER:  /* IER write (16-bit) */
            s->ier = val & 0xFFFF;
            /* If RESET bit enabled and not yet pending, generate RESET interrupt */
            if ((s->ier & RESET) && !(s->irq_status & RESET)) {
                s->irq_status |= RESET;
            }
            pcibase_update_irq(s);
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                          "Bad write at register region: addr=0x%" HWADDR_PRIx "\n", addr);
            break;
        }
        return;
    }

    /* Data buffer area: ignored */
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

    /* Reset registers and config table to power-on defaults */
    memset(&s->config_table, 0, sizeof(s->config_table));
    s->config_table.signature = NOZOMI_CONFIG_MAGIC;
    s->config_table.version = 0x0001;
    s->config_table.product_information = 0x0000;
    s->config_table.toggle.mdm_ul = 0;
    s->config_table.toggle.mdm_dl = 0;
    s->config_table.toggle.diag_dl = 0;
    s->config_table.toggle.enabled = 0;  /* TOGGLE_VALID = 0 */
    s->config_table.toggle.pad = 0;
    s->config_table.dl_start = 0x1000;
    s->config_table.dl_mdm_len1 = 64;
    s->config_table.dl_mdm_len2 = 64;
    s->config_table.dl_diag_len1 = 64;
    s->config_table.dl_diag_len2 = 64;
    s->config_table.dl_app1_len = 64;
    s->config_table.dl_app2_len = 64;
    s->config_table.dl_ctrl_len = 64;
    s->config_table.ul_start = 0x2000;
    s->config_table.ul_mdm_len2 = 64;
    s->config_table.ul_mdm_len1 = 64;
    s->config_table.ul_diag_len = 64;
    s->config_table.ul_app1_len = 64;
    s->config_table.ul_app2_len = 64;
    s->config_table.ul_ctrl_len = 64;

    s->irq_status = 0;
    s->ier = 0;
    s->fcr_shadow = 0;
    s->state = NOZOMI_STATE_UNKNOWN;
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }
    
    /* PCI requires BAR sizes to be a power of 2 */
    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        /* Not needed, but declaration kept for completeness */
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, aligned_size);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_NOZOMI);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_NOZOMI);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_ID_NOZOMI);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo){
        .index = 0,
        .type = BAR_TYPE_MMIO,
        .size = 0x10000,  /* 64KB; driver will compute card_type=F32_8 (8192) */
        .name = "nozomi-mmio"
    };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No MSI/MSI-X initialization (driver uses legacy INTx) */
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

static const VMStateDescription vmstate_pcibase = {
    .name = "nozomi_pci",
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
