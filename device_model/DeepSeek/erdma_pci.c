/* Completed functional QEMU device model for ERDMA PCI device */
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
#include "migration/vmstate.h"

#define TYPE_PCIBASE_DEVICE "erdma_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI Identification */
#define PCI_VENDOR_ID_ALIBABA 0x1DED
#define ERDMA_DEVICE_ID 0x107F
#define ERDMA_PCI_CLASS 0x020000

/* Register offsets */
#define ERDMA_REGS_VERSION_REG         0x0
#define ERDMA_REGS_DEV_PROTO_REG       0xC
#define ERDMA_REGS_DEV_CTRL_REG        0x10
#define ERDMA_REGS_DEV_ST_REG          0x14
#define ERDMA_REGS_NETDEV_MAC_L_REG    0x18
#define ERDMA_REGS_NETDEV_MAC_H_REG    0x1C
#define ERDMA_REGS_CMDQ_SQ_ADDR_L_REG  0x20
#define ERDMA_REGS_CMDQ_SQ_ADDR_H_REG  0x24
#define ERDMA_REGS_CMDQ_CQ_ADDR_L_REG  0x28
#define ERDMA_REGS_CMDQ_CQ_ADDR_H_REG  0x2C
#define ERDMA_REGS_CMDQ_DEPTH_REG      0x30
#define ERDMA_REGS_CMDQ_EQ_DEPTH_REG   0x34
#define ERDMA_REGS_CMDQ_EQ_ADDR_L_REG  0x38
#define ERDMA_REGS_CMDQ_EQ_ADDR_H_REG  0x3C
#define ERDMA_REGS_AEQ_ADDR_L_REG      0x40
#define ERDMA_REGS_AEQ_ADDR_H_REG      0x44
#define ERDMA_REGS_AEQ_DEPTH_REG       0x48
#define ERDMA_REGS_AEQ_DB_REG          0x50
#define ERDMA_CMDQ_SQ_DB_HOST_ADDR_REG 0x60
#define ERDMA_CMDQ_CQ_DB_HOST_ADDR_REG 0x68
#define ERDMA_CMDQ_EQ_DB_HOST_ADDR_REG 0x70
#define ERDMA_AEQ_DB_HOST_ADDR_REG     0x78
#define ERDMA_REGS_CEQ_DB_BASE_REG     0x100
#define ERDMA_CMDQ_SQDB_REG            0x200
#define ERDMA_CMDQ_CQDB_REG            0x300

/* Control/Status bit definitions */
#define ERDMA_REG_DEV_CTRL_RESET_MASK   0x00000001
#define ERDMA_REG_DEV_CTRL_INIT_MASK    0x00000002
#define ERDMA_REG_DEV_ST_INIT_DONE_MASK 0x00000002U

/* Firmware version */
#define ERDMA_FW_VERSION 0x01000000

/* BAR sizes */
#define BAR0_SIZE 0x10000
#define BAR2_SIZE 0x2000
#define REG_BAR_SIZE BAR0_SIZE

/* Default protocol register value (RoCEv2 assumed) */
#define ERDMA_DEFAULT_PROTO 1

/* Default MAC address: 00:11:22:33:44:55 */
#define ERDMA_DEFAULT_MAC_L 0x33221100
#define ERDMA_DEFAULT_MAC_H 0x00005544

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

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;

    uint8_t regs[REG_BAR_SIZE];

    int hw_init_done;
    int reset_state;

    /* Properties */
    uint32_t protocol;   /* value for dev_proto register */
    uint32_t mac_l;      /* lower 32 bits of MAC */
    uint32_t mac_h;      /* upper 16 bits of MAC */
};

/* MMIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= REG_BAR_SIZE) {
        return ~0ULL;
    }

    switch (addr) {
    case ERDMA_REGS_VERSION_REG:
        val = ldl_le_p(s->regs + addr);
        break;
    case ERDMA_REGS_DEV_PROTO_REG:
        val = ldl_le_p(s->regs + addr);
        break;
    case ERDMA_REGS_DEV_ST_REG:
        val = ldl_le_p(s->regs + addr);
        break;
    case ERDMA_REGS_NETDEV_MAC_L_REG:
        val = ldl_le_p(s->regs + addr);
        break;
    case ERDMA_REGS_NETDEV_MAC_H_REG:
        val = ldl_le_p(s->regs + addr);
        break;
    default:
        if (addr + size <= REG_BAR_SIZE) {
            switch (size) {
            case 1:
                val = s->regs[addr];
                break;
            case 2:
                val = lduw_le_p(s->regs + addr);
                break;
            case 4:
                val = ldl_le_p(s->regs + addr);
                break;
            case 8:
                val = ldq_le_p(s->regs + addr);
                break;
            default:
                val = ~0ULL;
            }
        } else {
            val = ~0ULL;
        }
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= REG_BAR_SIZE) {
        return;
    }

    switch (addr) {
    case ERDMA_REGS_DEV_CTRL_REG:
        /* Handle reset and init bits */
        if (val & ERDMA_REG_DEV_CTRL_RESET_MASK) {
            s->hw_init_done = 0;
            stl_le_p(s->regs + ERDMA_REGS_DEV_ST_REG, 0);
        }
        if (val & ERDMA_REG_DEV_CTRL_INIT_MASK) {
            s->hw_init_done = 1;
            stl_le_p(s->regs + ERDMA_REGS_DEV_ST_REG, ERDMA_REG_DEV_ST_INIT_DONE_MASK);
        }
        stl_le_p(s->regs + addr, val);
        break;
    default:
        /* Generic shadow update */
        if (addr + size <= REG_BAR_SIZE) {
            switch (size) {
            case 1:
                s->regs[addr] = (uint8_t)val;
                break;
            case 2:
                stw_le_p(s->regs + addr, val);
                break;
            case 4:
                stl_le_p(s->regs + addr, val);
                break;
            case 8:
                stq_le_p(s->regs + addr, val);
                break;
            default:
                break;
            }
        }
        break;
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

/* PIO not used; empty stubs */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
}

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

    /* Reset registers to power-on defaults */
    memset(s->regs, 0, sizeof(s->regs));

    /* Firmware version */
    stl_le_p(s->regs + ERDMA_REGS_VERSION_REG, ERDMA_FW_VERSION);

    /* Protocol: apply property default */
    stl_le_p(s->regs + ERDMA_REGS_DEV_PROTO_REG, s->protocol);

    /* MAC address */
    stl_le_p(s->regs + ERDMA_REGS_NETDEV_MAC_L_REG, s->mac_l);
    stl_le_p(s->regs + ERDMA_REGS_NETDEV_MAC_H_REG, s->mac_h);

    s->hw_init_done = 0;
    s->reset_state = 0;
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_ALIBABA);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  ERDMA_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, ERDMA_PCI_CLASS);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos < 0) {
        return;
    }
    pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);

    /* BAR Initialization */
    s->num_bars = 2;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = BAR0_SIZE, .name = "bar0" };
    s->bar_info[2] = (BARInfo){ .index = 2, .type = BAR_TYPE_RAM,   .size = BAR2_SIZE, .name = "bar2" };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI-X initialization */
    if (msix_init(pdev, 32, &s->bar_regions[2], 2, 0, &s->bar_regions[2], 2, 4096, 0, errp) < 0) {
        return;
    }

    /* Field initialization */
    memset(s->regs, 0, sizeof(s->regs));
    stl_le_p(s->regs + ERDMA_REGS_VERSION_REG, ERDMA_FW_VERSION);
    stl_le_p(s->regs + ERDMA_REGS_DEV_PROTO_REG, s->protocol);
    stl_le_p(s->regs + ERDMA_REGS_NETDEV_MAC_L_REG, s->mac_l);
    stl_le_p(s->regs + ERDMA_REGS_NETDEV_MAC_H_REG, s->mac_h);
    s->hw_init_done = 0;
    s->reset_state = 0;
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

/* Minimal VMState for migration */
static const VMStateDescription vmstate_pcibase = {
    .name = "erdma_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_END_OF_LIST()
    }
};

/* Properties */
static Property pcibase_properties[] = {
    DEFINE_PROP_UINT32("protocol", PCIBaseState, protocol, ERDMA_DEFAULT_PROTO),
    DEFINE_PROP_UINT32("mac_l", PCIBaseState, mac_l, ERDMA_DEFAULT_MAC_L),
    DEFINE_PROP_UINT32("mac_h", PCIBaseState, mac_h, ERDMA_DEFAULT_MAC_H),
    DEFINE_PROP_END_OF_LIST(),
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
    device_class_set_props(dc, pcibase_properties);
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
