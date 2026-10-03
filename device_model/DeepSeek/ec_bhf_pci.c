/*
 * QEMU PCI device model for EC-BHF EtherCAT master
 * Based on driver ec_bhf.c
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

#define TYPE_PCIBASE_DEVICE "ec_bhf_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define EC_BHF_VENDOR_ID 0x15ec
#define EC_BHF_DEVICE_ID 0x5000
#define EC_BHF_CLASS_ID  0x0200

#define BAR0_SIZE 0x1000
#define BAR2_SIZE 0x2000

/* Register offsets from driver */
#define INFO_BLOCK_SIZE        0x10
#define INFO_BLOCK_TYPE        0x0
#define INFO_BLOCK_REV         0x2
#define INFO_BLOCK_BLK_CNT     0x4
#define INFO_BLOCK_TX_CHAN     0x4
#define INFO_BLOCK_RX_CHAN     0x5
#define INFO_BLOCK_OFFSET      0x8
#define EC_MII_OFFSET          0x4
#define EC_FIFO_OFFSET         0x8
#define EC_MAC_OFFSET          0xc
#define MAC_FRAME_ERR_CNT      0x0
#define MAC_RX_ERR_CNT         0x1
#define MAC_CRC_ERR_CNT        0x2
#define MAC_LNK_LST_ERR_CNT    0x3
#define MAC_TX_FRAME_CNT       0x10
#define MAC_RX_FRAME_CNT       0x14
#define MAC_TX_FIFO_LVL        0x20
#define MAC_DROPPED_FRMS       0x28
#define MAC_CONNECTED_CCAT_FLAG 0x78
#define MII_MAC_ADDR           0x8
#define MII_MAC_FILT_FLAG      0xe
#define MII_LINK_STATUS        0xf
#define FIFO_TX_REG            0x0
#define FIFO_TX_RESET          0x8
#define FIFO_RX_REG            0x10
#define FIFO_RX_RESET          0x18
#define DMA_CHAN_OFFSET        0x1000
#define DMA_CHAN_SIZE          0x8
#define DMA_WINDOW_SIZE_MASK   0xfffffffc
#define ETHERCAT_MASTER_ID     0x14
#define RXHDR_NEXT_ADDR_MASK   0xffffffu
#define RXHDR_NEXT_VALID       (1u << 31)
#define RXHDR_NEXT_RECV_FLAG   0x1
#define RXHDR_LEN_MASK         0xfffu
#define PKT_PAYLOAD_SIZE       0x7e8
#define TX_HDR_PORT_0          0x1
#define TX_HDR_PORT_1          0x2
#define TX_HDR_SENT            0x1

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

    uint8_t bar0_regs[BAR0_SIZE];
    uint8_t bar2_regs[BAR2_SIZE];
};

static void ec_bhf_init_bar0(PCIBaseState *s)
{
    memset(s->bar0_regs, 0, BAR0_SIZE);

    /* Info block 0 at offset 0x00 */
    stw_le_p(s->bar0_regs + 0x00, ETHERCAT_MASTER_ID);   /* type */
    s->bar0_regs[0x04] = 1;                              /* block count / tx_chan */
    s->bar0_regs[0x05] = 0;                              /* rx_chan */
    stl_le_p(s->bar0_regs + 0x08, 0x100);                /* offset to EC block */

    /* EC block at 0x100 */
    stl_le_p(s->bar0_regs + 0x104, 0x100);               /* MII offset relative to EC */
    stl_le_p(s->bar0_regs + 0x108, 0x200);               /* FIFO offset */
    stl_le_p(s->bar0_regs + 0x10C, 0x300);               /* MAC offset */

    /* MII block at 0x200, MAC address 02:00:00:00:00:01 */
    s->bar0_regs[0x208] = 0x02;
    s->bar0_regs[0x209] = 0x00;
    s->bar0_regs[0x20A] = 0x00;
    s->bar0_regs[0x20B] = 0x00;
    s->bar0_regs[0x20C] = 0x00;
    s->bar0_regs[0x20D] = 0x01;
}

static uint64_t ec_bhf_bar0_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr >= BAR0_SIZE) {
        return ~0ULL;
    }
    return ldn_le_p(s->bar0_regs + addr, size);
}

static void ec_bhf_bar0_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr >= BAR0_SIZE) {
        return;
    }
    stn_le_p(s->bar0_regs + addr, size, val);
}

static const MemoryRegionOps ec_bhf_bar0_ops = {
    .read = ec_bhf_bar0_read,
    .write = ec_bhf_bar0_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static uint64_t ec_bhf_bar2_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr >= BAR2_SIZE) {
        return ~0ULL;
    }
    /* DMA channel base registers: always return mask */
    if (addr >= 0x1000 && addr < 0x2000 && (addr & 7) == 0 && size == 4) {
        return 0xFFFFF000;
    }
    return ldn_le_p(s->bar2_regs + addr, size);
}

static void ec_bhf_bar2_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr >= BAR2_SIZE) {
        return;
    }
    stn_le_p(s->bar2_regs + addr, size, val);
}

static const MemoryRegionOps ec_bhf_bar2_ops = {
    .read = ec_bhf_bar2_read,
    .write = ec_bhf_bar2_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    ec_bhf_init_bar0(s);
    memset(s->bar2_regs, 0, BAR2_SIZE);
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, EC_BHF_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, EC_BHF_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, EC_BHF_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    ec_bhf_init_bar0(s);
    memset(s->bar2_regs, 0, BAR2_SIZE);

    memory_region_init_io(&s->bar_regions[0], OBJECT(s), &ec_bhf_bar0_ops, s,
                          "ec_bhf_regs", BAR0_SIZE);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_regions[0]);

    memory_region_init_io(&s->bar_regions[2], OBJECT(s), &ec_bhf_bar2_ops, s,
                          "ec_bhf_dma", BAR2_SIZE);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_regions[2]);
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
    .name = "ec_bhf_pci",
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
