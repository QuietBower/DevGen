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

#define TYPE_PCIBASE_DEVICE "dwmac_motorcomm_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI_VENDOR_ID_MOTORCOMM 0x1f0a
#define PCI_DEVICE_ID_MOTORCOMM 0x6801

#define EPHY_CTRL 0x1004
#define EPHY_MDIO_PHY_RESET (1 << 0)
#define OOB_WOL_CTRL 0x1010
#define OOB_WOL_CTRL_DIS (1 << 0)
#define MGMT_INT_CTRL0 0x1100
#define INT_MODERATION 0x1108
#define INT_MODERATION_RX 0xfff
#define INT_MODERATION_TX (0xfff << 16)
#define EFUSE_OP_CTRL_0 0x1500
#define EFUSE_OP_MODE 0x3
#define EFUSE_OP_ROW_READ 0x1
#define EFUSE_OP_START (1 << 2)
#define EFUSE_OP_ADDR (0xff << 8)
#define EFUSE_OP_CTRL_1 0x1504
#define EFUSE_OP_DONE (1 << 1)
#define EFUSE_OP_RD_DATA (0xff << 24)
#define SYS_RESET 0x152c
#define SYS_RESET_RESET (1 << 31)
#define GMAC_OFFSET 0x2000
#define EFUSE_ADDR_MACA0LR 0x1520
#define EFUSE_ADDR_MACA0HR 0x1524

#define EFUSE_PATCH_REGION_OFFSET 18
#define EFUSE_PATCH_MAX_NUM 39

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
    
    uint32_t mgmt_int_ctrl0;
    uint32_t int_moderation;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t ephy_ctrl;
    uint32_t oob_wol_ctrl;
    uint32_t efuse_op_ctrl_0;
    uint32_t efuse_op_ctrl_1;
    uint32_t sys_reset;
    uint32_t efuse_addr_maca0lr;
    uint32_t efuse_addr_maca0hr;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case EPHY_CTRL:
        val = s->ephy_ctrl;
        break;
    case OOB_WOL_CTRL:
        val = s->oob_wol_ctrl;
        break;
    case MGMT_INT_CTRL0:
        val = s->mgmt_int_ctrl0;
        break;
    case INT_MODERATION:
        val = s->int_moderation;
        break;
    case EFUSE_OP_CTRL_0:
        val = s->efuse_op_ctrl_0;
        break;
    case EFUSE_OP_CTRL_1:
        val = s->efuse_op_ctrl_1;
        break;
    case SYS_RESET:
        val = s->sys_reset;
        break;
    default:
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case EPHY_CTRL:
        s->ephy_ctrl = val;
        break;
    case OOB_WOL_CTRL:
        s->oob_wol_ctrl = val;
        break;
    case MGMT_INT_CTRL0:
        s->mgmt_int_ctrl0 = val;
        break;
    case INT_MODERATION:
        s->int_moderation = val;
        break;
    case EFUSE_OP_CTRL_0:
        s->efuse_op_ctrl_0 = val;
        if (val & EFUSE_OP_START) {
            s->efuse_op_ctrl_1 |= EFUSE_OP_DONE;
        }
        break;
    case EFUSE_OP_CTRL_1:
        s->efuse_op_ctrl_1 = val;
        break;
    case SYS_RESET:
        s->sys_reset = val;
        break;
    default:
        break;
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
    
    s->ephy_ctrl = 0;
    s->oob_wol_ctrl = 0;
    s->mgmt_int_ctrl0 = 0;
    s->int_moderation = 0;
    s->efuse_op_ctrl_0 = 0;
    s->efuse_op_ctrl_1 = 0;
    s->sys_reset = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_MOTORCOMM );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x6801 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_ETHERNET );
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
    s->bar_info[0].size = 0x10000; /* Inferred to cover GMAC_OFFSET 0x2000 + GMAC registers */
    s->bar_info[0].name = "dwmac-motorcomm-bar0";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Interrupt Initialization */
    if (msix_init(pdev, 6, &s->bar_regions[0], 0, 0x8000, &s->bar_regions[0], 0, 0x8100, 0, errp) == 0) {
        s->has_msix = true;
    } else if (msi_init(pdev, 0, 1, true, false, errp) == 0) {
        s->has_msi = true;
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, &s->bar_regions[0], &s->bar_regions[0]);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "dwmac_motorcomm_pci",
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
