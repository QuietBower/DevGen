/*
 * This template provides a robust skeleton for hardware emulation.
 * Integrated QEMU PCI device template (QEMU 8.2.10).
 * Behavior implemented based on amplc_dio200_pci.c driver.
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

#define TYPE_PCIBASE_DEVICE "amplc_dio200_pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Vendor ID: Amplicon */
#define PCI_VENDOR_ID_AMPLICON 0x14dc

/* Device ID: First PCIe entry in table: 0x0011 (pcie236_model) */
#define DIO200_DEVICE_ID       0x0011

/* Class ID: Miscellaneous */
#define DIO200_CLASS_ID        0x00ff00

/* BAR Definitions */
#define DIO200_BAR0_SIZE      0x4000
#define DIO200_BAR1_SIZE      0x40

/* Register offsets based on board sdinfo[] for pcie236 */
#define DIO200_8255_0_OFFSET  0x00
#define DIO200_8254_0_OFFSET  0x10
#define DIO200_8254_1_OFFSET  0x14
#define DIO200_INT_SCE_OFFSET 0x3F

/* Subdevice type enumerations */
enum dio200_sdtype {
    sd_none = 0,
    sd_intr,
    sd_8255,
    sd_8254,
    sd_timer
};

/* Static board information for the modeled device (pcie236) */
struct board_info {
    const char *name;
    unsigned char mainbar;
    unsigned short n_subdevs;
    unsigned char sdtype[8];
    unsigned char sdinfo[8];
    unsigned int has_int_sce:1;
    unsigned int has_clk_gat_sce:1;
    unsigned int is_pcie:1;
};

static const struct board_info board_info = {
    .name = "pcie236",
    .mainbar = 1,
    .n_subdevs = 8,
    .sdtype = { sd_8255, sd_none, sd_none, sd_none, sd_8254, sd_8254, sd_timer, sd_intr },
    .sdinfo = { 0x00, 0x00, 0x00, 0x00, 0x10, 0x14, 0x00, 0x3F },
    .has_int_sce = 1,
    .has_clk_gat_sce = 1,
    .is_pcie = 1,
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
    MemoryRegion bar0_mmio;
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;
    /* Bridge interrupt enable (BAR0+0x50) */
    uint32_t bridge_int_en;
    /* Interrupt status / mask registers (deduced from has_int_sce) */
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware Register Shadows */
    uint8_t reg_8255_0[4];    /* 8255 subdevice 0 at offset 0x00 */
    uint8_t reg_8254_0[4];    /* 8254 subdevice 4 at offset 0x10 */
    uint8_t reg_8254_1[4];    /* 8254 subdevice 5 at offset 0x14 */
    uint8_t reg_intr;         /* interrupt control/status at 0x3F */
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    int level = (s->bridge_int_en & 0x80) && s->reg_intr ? 1 : 0;
    pci_set_irq(pdev, level);
}

/* MMIO/PIO Handlers for BAR0 (PCIe bridge control) */
static uint64_t pcibase_bar0_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr == 0x50 && size == 4) {
        val = s->bridge_int_en;
    }
    return val;
}

static void pcibase_bar0_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr == 0x50 && size == 4) {
        s->bridge_int_en = val;
        pcibase_update_irq(s);
    }
}

static const MemoryRegionOps pcibase_bar0_ops = {
    .read = pcibase_bar0_mmio_read,
    .write = pcibase_bar0_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

/* MMIO/PIO Handlers for BAR1 (board registers) */
static uint64_t pcibase_bar1_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= 0x00 && addr < 0x04) {
        val = s->reg_8255_0[addr - 0x00];
    } else if (addr >= 0x10 && addr < 0x14) {
        val = s->reg_8254_0[addr - 0x10];
    } else if (addr >= 0x14 && addr < 0x18) {
        val = s->reg_8254_1[addr - 0x14];
    } else if (addr == 0x3F) {
        val = s->reg_intr;
    }
    return val;
}

static void pcibase_bar1_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= 0x00 && addr < 0x04) {
        s->reg_8255_0[addr - 0x00] = val;
    } else if (addr >= 0x10 && addr < 0x14) {
        s->reg_8254_0[addr - 0x10] = val;
    } else if (addr >= 0x14 && addr < 0x18) {
        s->reg_8254_1[addr - 0x14] = val;
    } else if (addr == 0x3F) {
        s->reg_intr = val;
        pcibase_update_irq(s);
    }
}

static const MemoryRegionOps pcibase_bar1_ops = {
    .read = pcibase_bar1_mmio_read,
    .write = pcibase_bar1_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }
    
    /* CRITICAL: PCI requires BAR sizes to be a power of 2. Prevent QEMU assert crash. */
    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr;

    if (bi->type == BAR_TYPE_MMIO) {
        if (bi->index == 0) {
            mr = &s->bar0_mmio;
            memory_region_init_io(mr, OBJECT(s), &pcibase_bar0_ops, s, bi->name, aligned_size);
        } else {
            mr = &s->bar_regions[bi->index];
            memory_region_init_io(mr, OBJECT(s), &pcibase_bar1_ops, s, bi->name, aligned_size);
        }
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        mr = &s->bar_regions[bi->index];
        memory_region_init_io(mr, OBJECT(s), &pcibase_bar1_ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    } else if (bi->type == BAR_TYPE_RAM) {
        mr = &s->bar_regions[bi->index];
        memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_AMPLICON );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DIO200_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, DIO200_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 2;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO,
                                .size = DIO200_BAR0_SIZE, .name = "bar0" };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_MMIO,
                                .size = DIO200_BAR1_SIZE, .name = "bar1" };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Final state initialization */
    memset(s->reg_8255_0, 0, sizeof(s->reg_8255_0));
    memset(s->reg_8254_0, 0, sizeof(s->reg_8254_0));
    memset(s->reg_8254_1, 0, sizeof(s->reg_8254_1));
    s->reg_intr = 0;
    s->bridge_int_en = 0;
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

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    memset(s->reg_8255_0, 0, sizeof(s->reg_8255_0));
    memset(s->reg_8254_0, 0, sizeof(s->reg_8254_0));
    memset(s->reg_8254_1, 0, sizeof(s->reg_8254_1));
    s->reg_intr = 0;
    s->bridge_int_en = 0;
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "amplc_dio200_pci_pci",
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
