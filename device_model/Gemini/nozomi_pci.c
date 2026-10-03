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

/* Additional include files retrieved from driver context */

#define TYPE_PCIBASE_DEVICE "nozomi_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define NOZOMI_VENDOR_ID 0x1931
#define NOZOMI_DEVICE_ID 0x000c

#define R_IIR 0x0000
#define R_FCR 0x0000
#define R_IER 0x0004
#define RESET 0x8000

#define NOZOMI_CONFIG_MAGIC 0xEFEFFEFE

#define TOGGLE_VALID		0x0000
#define MDM_DL1			0x0001
#define MDM_DL2			0x0004
#define DIAG_DL1		0x0010
#define DIAG_DL2		0x0020
#define DIAG_UL			0x0040
#define APP1_DL			0x0080
#define APP1_UL			0x0100
#define APP2_DL			0x0200
#define APP2_UL			0x0400
#define CTRL_DL			0x0800
#define CTRL_UL			0x1000

#define DIAG_DL			(DIAG_DL1 | DIAG_DL2)
#define MDM_DL			(MDM_DL1  | MDM_DL2)

#define PORT_CTRL	2
#define MAX_PORT		4
#define NOZOMI_MAX_PORTS	5

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

struct toggles {
    unsigned int mdm_ul:1;
    unsigned int mdm_dl:1;
    unsigned int diag_dl:1;
    unsigned int enabled:5;
};

struct config_table {
    uint32_t signature;
    uint16_t version;
    uint16_t product_information;
    struct toggles toggle;
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
};

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;
    uint16_t reg_iir;
    uint16_t reg_fcr;
    uint16_t reg_ier;

    uint32_t state;
    uint32_t open_ttys;      /* Operational status flags */
    struct config_table config_table;
};

enum card_type {
    F32_2 = 2048,
    F32_8 = 8192,
};

enum card_state {
    NOZOMI_STATE_UNKNOWN    = 0,
    NOZOMI_STATE_ENABLED    = 1,
    NOZOMI_STATE_ALLOCATED  = 2,
    NOZOMI_STATE_READY      = 3,
};

struct ctrl_dl {
    unsigned int DSR:1;
    unsigned int DCD:1;
    unsigned int RI:1;
    unsigned int CTS:1;
    unsigned int reserved:4;
    uint8_t port;
};

struct ctrl_ul {
    unsigned int DTR:1;
    unsigned int RTS:1;
    unsigned int reserved:6;
    uint8_t port;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->reg_iir & s->reg_ier) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr == 0x1000) {
        return s->reg_iir;
    } else if (addr == 0x1004) {
        return s->reg_ier;
    } else if (addr < sizeof(struct config_table)) {
        uint8_t *p = (uint8_t *)&s->config_table;
        for (unsigned i = 0; i < size; i++) {
            if (addr + i < sizeof(struct config_table)) {
                val |= ((uint64_t)p[addr + i]) << (i * 8);
            }
        }
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr == 0x1000) {
        s->reg_iir &= ~(val & 0xFFFF);
        pcibase_update_irq(s);
    } else if (addr == 0x1004) {
        s->reg_ier = val & 0xFFFF;
        pcibase_update_irq(s);
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    uint64_t val = 0;
    return val;
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

    s->reg_iir = RESET;
    s->reg_ier = 0;
    s->reg_fcr = 0;

    memset(&s->config_table, 0, sizeof(s->config_table));
    s->config_table.signature = cpu_to_le32(NOZOMI_CONFIG_MAGIC);
    s->config_table.version = cpu_to_le16(0);
    s->config_table.toggle.enabled = TOGGLE_VALID;
    
    s->config_table.dl_start = cpu_to_le16(0x0200);
    s->config_table.dl_mdm_len1 = cpu_to_le16(0x40);
    s->config_table.dl_mdm_len2 = cpu_to_le16(0x40);
    s->config_table.dl_diag_len1 = cpu_to_le16(0x40);
    s->config_table.dl_diag_len2 = cpu_to_le16(0x40);
    s->config_table.dl_app1_len = cpu_to_le16(0x40);
    s->config_table.dl_app2_len = cpu_to_le16(0x40);
    s->config_table.dl_ctrl_len = cpu_to_le16(0x40);

    s->config_table.ul_start = cpu_to_le16(0x0800);
    s->config_table.ul_mdm_len1 = cpu_to_le16(0x40);
    s->config_table.ul_mdm_len2 = cpu_to_le16(0x40);
    s->config_table.ul_diag_len = cpu_to_le16(0x40);
    s->config_table.ul_app1_len = cpu_to_le16(0x40);
    s->config_table.ul_app2_len = cpu_to_le16(0x40);
    s->config_table.ul_ctrl_len = cpu_to_le16(0x40);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x1931 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x000c );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0780 );
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
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 8192; /* F32_8 */
    s->bar_info[0].name = "nozomi-bar0";
      
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
