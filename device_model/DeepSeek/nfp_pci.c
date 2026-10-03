/*
 * QEMU PCI device model for Netronome NFP (Phase 1: Structure)
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

#define TYPE_PCIBASE_DEVICE "nfp_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */

/* PCI IDs (to be defined via needed_sources) */
#define NFP_VENDOR_ID 0x0000 /* FIXME: request define:NFP_VENDOR_ID */
#define NFP_DEVICE_ID 0x3800
#define NFP_CLASS_ID  0x020000 /* Not used directly; class set via pci_config_set_class */

/* BAR size from driver: NFP_NET_CFG_BAR_SZ = 32KB */
#define NFP_NET_CFG_BAR_SZ (32 * 1024)

/* Register offsets (extracted from driver macros) */
#define NFP_NET_CFG_CTRL            0x0000
#define NFP_NET_CFG_UPDATE          0x0004
#define NFP_NET_CFG_STS             0x0034
#define NFP_NET_CFG_CAP             0x0038
#define NFP_NET_CFG_START_TXQ       0x0048
#define NFP_NET_CFG_START_RXQ       0x004c
#define NFP_NET_CFG_MAX_TXRINGS     0x003c
#define NFP_NET_CFG_MAX_RXRINGS     0x0040
#define NFP_NET_CFG_TXRS_ENABLE     0x0008
#define NFP_NET_CFG_RXRS_ENABLE     0x0010
#define NFP_NET_CFG_FLBUFSZ         0x001c
#define NFP_NET_CFG_MTU             0x0018
#define NFP_NET_CFG_MACADDR         0x0024
#define NFP_NET_CFG_EXN             0x001f
#define NFP_NET_CFG_LSC             0x0020
#define NFP_NET_CFG_CTRL_WORD1      0x0098
#define NFP_NET_CFG_CAP_WORD1       0x00a4
#define NFP_NET_CFG_RSS_CAP         0x0054
#define NFP_NET_CFG_RSS_CTRL        NFP_NET_CFG_RSS_BASE
#define NFP_NET_CFG_RSS_BASE        0x0100
#define NFP_NET_CFG_RSS_KEY         (NFP_NET_CFG_RSS_BASE + 0x4)
#define NFP_NET_CFG_RSS_ITBL        (NFP_NET_CFG_RSS_BASE + 0x4 + NFP_NET_CFG_RSS_KEY_SZ)
#define NFP_NET_CFG_RSS_KEY_SZ      0x28
#define NFP_NET_CFG_RSS_ITBL_SZ     0x80
#define NFP_NET_CFG_TLV_BASE        0x0058
#define NFP_NET_CFG_TLV_TYPE_END    2
#define NFP_NET_CFG_TLV_TYPE_MBOX   4
#define NFP_NET_CFG_TLV_HEADER_TYPE 0x7fff0000
#define NFP_NET_CFG_TLV_HEADER_LENGTH 0x0000ffff
#define NFP_NET_CFG_TLV_LENGTH_INC  4
#define NFP_NET_CFG_TXR_BASE        0x0200
#define NFP_NET_CFG_TXR_ADDR(_x)    (NFP_NET_CFG_TXR_BASE + ((_x) * 0x8))
#define NFP_NET_CFG_TXR_WB_ADDR(_x) (NFP_NET_CFG_TXR_BASE + 0x200 + ((_x) * 0x8))
#define NFP_NET_CFG_TXR_SZ(_x)      (NFP_NET_CFG_TXR_BASE + 0x400 + (_x))
#define NFP_NET_CFG_TXR_VEC(_x)     (NFP_NET_CFG_TXR_BASE + 0x440 + (_x))
#define NFP_NET_CFG_TXR_IRQ_MOD(_x) (NFP_NET_CFG_TXR_BASE + 0x500 + ((_x) * 0x4))
#define NFP_NET_CFG_RXR_BASE        0x0800
#define NFP_NET_CFG_RXR_ADDR(_x)    (NFP_NET_CFG_RXR_BASE + ((_x) * 0x8))
#define NFP_NET_CFG_RXR_SZ(_x)      (NFP_NET_CFG_RXR_BASE + 0x200 + (_x))
#define NFP_NET_CFG_RXR_VEC(_x)     (NFP_NET_CFG_RXR_BASE + 0x240 + (_x))
#define NFP_NET_CFG_RXR_IRQ_MOD(_x) (NFP_NET_CFG_RXR_BASE + 0x300 + ((_x) * 0x4))
#define NFP_NET_CFG_MBOX_BASE       0x1800
#define NFP_NET_CFG_MBOX_VAL_MAX_SZ 0x1F8
#define NFP_NET_CFG_ICR_BASE        0x0c00
#define NFP_NET_CFG_ICR(_x)         (NFP_NET_CFG_ICR_BASE + (_x))
#define NFP_NET_CFG_STATS_BASE      0x0d00
#define NFP_NET_CFG_TXR_STATS_BASE  0x1000
#define NFP_NET_CFG_RXR_STATS_BASE  0x1400
#define NFP_NET_CFG_STATS_RX_DISCARDS  (NFP_NET_CFG_STATS_BASE + 0x00)
#define NFP_NET_CFG_STATS_RX_ERRORS    (NFP_NET_CFG_STATS_BASE + 0x08)
#define NFP_NET_CFG_STATS_RX_OCTETS    (NFP_NET_CFG_STATS_BASE + 0x10)
#define NFP_NET_CFG_STATS_RX_UC_OCTETS (NFP_NET_CFG_STATS_BASE + 0x18)
#define NFP_NET_CFG_STATS_RX_MC_OCTETS (NFP_NET_CFG_STATS_BASE + 0x20)
#define NFP_NET_CFG_STATS_RX_BC_OCTETS (NFP_NET_CFG_STATS_BASE + 0x28)
#define NFP_NET_CFG_STATS_RX_FRAMES    (NFP_NET_CFG_STATS_BASE + 0x30)
#define NFP_NET_CFG_STATS_RX_MC_FRAMES (NFP_NET_CFG_STATS_BASE + 0x38)
#define NFP_NET_CFG_STATS_RX_BC_FRAMES (NFP_NET_CFG_STATS_BASE + 0x40)
#define NFP_NET_CFG_STATS_TX_DISCARDS  (NFP_NET_CFG_STATS_BASE + 0x48)
#define NFP_NET_CFG_STATS_TX_ERRORS    (NFP_NET_CFG_STATS_BASE + 0x50)
#define NFP_NET_CFG_STATS_TX_OCTETS    (NFP_NET_CFG_STATS_BASE + 0x58)
#define NFP_NET_CFG_STATS_TX_UC_OCTETS (NFP_NET_CFG_STATS_BASE + 0x60)
#define NFP_NET_CFG_STATS_TX_MC_OCTETS (NFP_NET_CFG_STATS_BASE + 0x68)
#define NFP_NET_CFG_STATS_TX_BC_OCTETS (NFP_NET_CFG_STATS_BASE + 0x70)
#define NFP_NET_CFG_STATS_TX_FRAMES    (NFP_NET_CFG_STATS_BASE + 0x78)
#define NFP_NET_CFG_STATS_TX_MC_FRAMES (NFP_NET_CFG_STATS_BASE + 0x80)
#define NFP_NET_CFG_STATS_TX_BC_FRAMES (NFP_NET_CFG_STATS_BASE + 0x88)
#define NFP_NET_CFG_STATS_APP0_FRAMES  (NFP_NET_CFG_STATS_BASE + 0x90)
#define NFP_NET_CFG_STATS_APP0_BYTES   (NFP_NET_CFG_STATS_BASE + 0x98)
#define NFP_NET_CFG_STATS_APP1_FRAMES  (NFP_NET_CFG_STATS_BASE + 0xa0)
#define NFP_NET_CFG_STATS_APP1_BYTES   (NFP_NET_CFG_STATS_BASE + 0xa8)
#define NFP_NET_CFG_STATS_APP2_FRAMES  (NFP_NET_CFG_STATS_BASE + 0xb0)
#define NFP_NET_CFG_STATS_APP2_BYTES   (NFP_NET_CFG_STATS_BASE + 0xb8)
#define NFP_NET_CFG_STATS_APP3_FRAMES  (NFP_NET_CFG_STATS_BASE + 0xc0)
#define NFP_NET_CFG_STATS_APP3_BYTES   (NFP_NET_CFG_STATS_BASE + 0xc8)
#define NFP_NET_CFG_TXR_STATS(_x)      (NFP_NET_CFG_TXR_STATS_BASE + ((_x) * 0x10))
#define NFP_NET_CFG_RXR_STATS(_x)      (NFP_NET_CFG_RXR_STATS_BASE + ((_x) * 0x10))
#define NFP_NET_CFG_VXLAN_PORT         0x0060
#define NFP_NET_CFG_VXLAN_SZ           0x0008
#define NFP_NET_CFG_FS_SZ              0x0054
#define NFP_NET_CFG_RX_OFFSET          0x0050
#define NFP_NET_CFG_MAX_MTU            0x0044
#define NFP_MBOX_CMD                   0x00
#define NFP_MBOX_RET                   0x04
#define NFP_MBOX_DATA_LEN              0x08
#define NFP_MBOX_DATA                  0x10
#define NFP_MBOX_SYM_MIN_SIZE          16
#define NFP_MBOX_SIMPLE_CMD            0x0
#define NFP_MBOX_SIMPLE_RET            0x4
#define NFP_MBOX_SIMPLE_VAL            0x8
#define NFP_QCP_QUEUE_ADDR_SZ          0x800
#define NFP_QCP_QUEUE_OFF(_x)          ((_x) * NFP_QCP_QUEUE_ADDR_SZ)
#define NFP_QCP_QUEUE_ADD_WPTR         0x0004
#define NFP_NET_CFG_STS_LINK_RATE      0x0036

/* Bit definitions (not offsets, but useful for register fields) */
#define NFP_NET_CFG_CTRL_ENABLE         (0x1 << 0)
#define NFP_NET_CFG_CTRL_PROMISC        (0x1 << 1)
#define NFP_NET_CFG_CTRL_L2BC           (0x1 << 2)
#define NFP_NET_CFG_CTRL_L2MC           (0x1 << 3)
#define NFP_NET_CFG_CTRL_RXCSUM         (0x1 << 4)
#define NFP_NET_CFG_CTRL_TXCSUM         (0x1 << 5)
#define NFP_NET_CFG_CTRL_RXVLAN         (0x1 << 6)
#define NFP_NET_CFG_CTRL_TXVLAN         (0x1 << 7)
#define NFP_NET_CFG_CTRL_SCATTER        (0x1 << 8)
#define NFP_NET_CFG_CTRL_GATHER         (0x1 << 9)
#define NFP_NET_CFG_CTRL_LSO            (0x1 << 10)
#define NFP_NET_CFG_CTRL_CTAG_FILTER    (0x1 << 11)
#define NFP_NET_CFG_CTRL_CMSG_DATA      (0x1 << 12)
#define NFP_NET_CFG_CTRL_RXQINQ         (0x1 << 13)
#define NFP_NET_CFG_CTRL_RXVLAN_V2      (0x1 << 15)
#define NFP_NET_CFG_CTRL_RINGCFG        (0x1 << 16)
#define NFP_NET_CFG_CTRL_RSS            (0x1 << 17)
#define NFP_NET_CFG_CTRL_IRQMOD         (0x1 << 18)
#define NFP_NET_CFG_CTRL_MSIXAUTO       (0x1 << 20)
#define NFP_NET_CFG_CTRL_TXRWB          (0x1 << 21)
#define NFP_NET_CFG_CTRL_VEPA           (0x1 << 22)
#define NFP_NET_CFG_CTRL_TXVLAN_V2      (0x1 << 23)
#define NFP_NET_CFG_CTRL_VXLAN          (0x1 << 24)
#define NFP_NET_CFG_CTRL_NVGRE          (0x1 << 25)
#define NFP_NET_CFG_CTRL_LSO2           (0x1 << 28)
#define NFP_NET_CFG_CTRL_RSS2           (0x1 << 29)
#define NFP_NET_CFG_CTRL_CSUM_COMPLETE  (0x1 << 30)
#define NFP_NET_CFG_CTRL_LIVE_ADDR      (0x1 << 31)
#define NFP_NET_CFG_UPDATE_GEN          (0x1 << 0)
#define NFP_NET_CFG_UPDATE_RING         (0x1 << 1)
#define NFP_NET_CFG_UPDATE_RSS          (0x1 << 2)
#define NFP_NET_CFG_UPDATE_MSIX         (0x1 << 5)
#define NFP_NET_CFG_UPDATE_IRQMOD       (0x1 << 8)
#define NFP_NET_CFG_UPDATE_VXLAN        (0x1 << 9)
#define NFP_NET_CFG_UPDATE_MACADDR      (0x1 << 11)
#define NFP_NET_CFG_UPDATE_MBOX         (0x1 << 12)
#define NFP_NET_CFG_UPDATE_VF           (0x1 << 13)
#define NFP_NET_CFG_UPDATE_CRYPTO       (0x1 << 14)
#define NFP_NET_CFG_UPDATE_ERR          (0x1 << 31)
#define NFP_NET_CFG_STS_LINK            (0x1 << 0)

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
    uint32_t intr_status;
    uint32_t intr_mask;

    uint8_t regs[NFP_NET_CFG_BAR_SZ]; /* Full register file */
};

G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
}

G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr + size > NFP_NET_CFG_BAR_SZ) {
        val = 0;
    } else {
        switch (size) {
        case 1:
            val = s->regs[addr];
            break;
        case 2:
            val = lduw_le_p(&s->regs[addr]);
            break;
        case 4:
            val = ldl_le_p(&s->regs[addr]);
            break;
        case 8:
            val = ldq_le_p(&s->regs[addr]);
            break;
        default:
            val = 0;
            break;
        }
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr + size <= NFP_NET_CFG_BAR_SZ) {
        switch (size) {
        case 1:
            s->regs[addr] = (uint8_t)val;
            break;
        case 2:
            stw_le_p(&s->regs[addr], (uint16_t)val);
            break;
        case 4:
            stl_le_p(&s->regs[addr], (uint32_t)val);
            break;
        case 8:
            stq_le_p(&s->regs[addr], val);
            break;
        }
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

    memset(s->regs, 0, NFP_NET_CFG_BAR_SZ);
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  NFP_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  NFP_DEVICE_ID);
    pci_config_set_class(pci_conf, 0x020000);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR Initialization: single MMIO BAR0 with known size */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = NFP_NET_CFG_BAR_SZ;
    s->bar_info[0].name = "nfp-mmio";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI-X initialization requires NFP_PF_MSIX_VEC_COUNT definition */
    /* Temporarily disabled: if (msix_init(pdev, NFP_PF_MSIX_VEC_COUNT, &s->bar_regions[0], 0, 0, errp)) { return; } */
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

static const VMStateDescription vmstate_pcibase = {
    .name = "nfp_pci",
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
