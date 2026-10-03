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


#define TYPE_PCIBASE_DEVICE "ec_bhf_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define FIFO_SIZE		64
#define TIMER_INTERVAL_NSEC	20000
#define INFO_BLOCK_SIZE		0x10
#define INFO_BLOCK_TYPE		0x0
#define INFO_BLOCK_REV		0x2
#define INFO_BLOCK_BLK_CNT	0x4
#define INFO_BLOCK_TX_CHAN	0x4
#define INFO_BLOCK_RX_CHAN	0x5
#define INFO_BLOCK_OFFSET	0x8
#define EC_MII_OFFSET		0x4
#define EC_FIFO_OFFSET		0x8
#define EC_MAC_OFFSET		0xc
#define MAC_FRAME_ERR_CNT	0x0
#define MAC_RX_ERR_CNT		0x1
#define MAC_CRC_ERR_CNT		0x2
#define MAC_LNK_LST_ERR_CNT	0x3
#define MAC_TX_FRAME_CNT	0x10
#define MAC_RX_FRAME_CNT	0x14
#define MAC_TX_FIFO_LVL		0x20
#define MAC_DROPPED_FRMS	0x28
#define MAC_CONNECTED_CCAT_FLAG	0x78
#define MII_MAC_ADDR		0x8
#define MII_MAC_FILT_FLAG	0xe
#define MII_LINK_STATUS		0xf
#define FIFO_TX_REG		0x0
#define FIFO_TX_RESET		0x8
#define FIFO_RX_REG		0x10
#define FIFO_RX_ADDR_VALID	(1u << 31)
#define FIFO_RX_RESET		0x18
#define DMA_CHAN_OFFSET		0x1000
#define DMA_CHAN_SIZE		0x8
#define DMA_WINDOW_SIZE_MASK	0xfffffffc
#define ETHERCAT_MASTER_ID	0x14
#define RXHDR_NEXT_ADDR_MASK	0xffffffu
#define RXHDR_NEXT_VALID	(1u << 31)
#define RXHDR_NEXT_RECV_FLAG	0x1
#define RXHDR_LEN_MASK		0xfffu
#define PKT_PAYLOAD_SIZE	0x7e8
#define TX_HDR_PORT_0		0x1
#define TX_HDR_PORT_1		0x2
#define TX_HDR_SENT		0x1 

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

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t ec_io_offset;
    uint32_t mii_io_offset;
    uint32_t fifo_io_offset;
    uint32_t mac_io_offset;

    /* DMA Context */
    int tx_dma_chan;
    int rx_dma_chan;
    uint32_t dma_chan_phys[2];
    uint32_t dma_chan_mask[2];

    /* Added State */
    bool dma_chan_mask_written[2];
    uint8_t mac_rx_err_cnt;
    uint8_t mac_crc_err_cnt;
    uint8_t mac_frame_err_cnt;
    uint8_t mac_lnk_lst_err_cnt;
    uint32_t mac_tx_frame_cnt;
    uint32_t mac_rx_frame_cnt;
    uint8_t mac_dropped_frms;
    uint8_t mac_tx_fifo_lvl;
};

struct rx_header {
	uint32_t next;
	uint32_t recv;
	uint16_t len;
	uint16_t port;
	uint32_t reserved;
	uint8_t timestamp[8];
};
struct tx_header {
	uint16_t len;
	uint8_t port;
	uint8_t ts_enable;
	uint32_t sent;
	uint8_t timestamp[8];
};
struct tx_desc {
	struct tx_header header;
	uint8_t data[PKT_PAYLOAD_SIZE];
};
struct rx_desc {
	struct rx_header header;
	uint8_t data[PKT_PAYLOAD_SIZE];
};

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= DMA_CHAN_OFFSET && addr < DMA_CHAN_OFFSET + 0x10) {
        int chan = (addr - DMA_CHAN_OFFSET) / DMA_CHAN_SIZE;
        int reg = (addr - DMA_CHAN_OFFSET) % DMA_CHAN_SIZE;
        if (reg == 0) {
            if (s->dma_chan_mask_written[chan]) {
                val = s->dma_chan_mask[chan];
            } else {
                val = s->dma_chan_phys[chan];
            }
        }
        return val;
    }

    if (addr >= 0x208 && addr < 0x20E) {
        static const uint8_t mac[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
        for (unsigned i = 0; i < size; i++) {
            val |= ((uint64_t)mac[addr - 0x208 + i]) << (i * 8);
        }
        return val;
    }

    switch (addr) {
        case 0x00: val = 0x0000; break;
        case 0x04: val = 0x02; break;
        case 0x10: val = ETHERCAT_MASTER_ID; break;
        case 0x14: val = 0x00; break;
        case 0x15: val = 0x01; break;
        case 0x18: val = 0x100; break;
        case 0x104: val = 0x100; break;
        case 0x108: val = 0x200; break;
        case 0x10C: val = 0x300; break;
        case 0x20E: val = 0x00; break;
        case 0x400 + MAC_FRAME_ERR_CNT: val = s->mac_frame_err_cnt; break;
        case 0x400 + MAC_RX_ERR_CNT: val = s->mac_rx_err_cnt; break;
        case 0x400 + MAC_CRC_ERR_CNT: val = s->mac_crc_err_cnt; break;
        case 0x400 + MAC_LNK_LST_ERR_CNT: val = s->mac_lnk_lst_err_cnt; break;
        case 0x400 + MAC_TX_FRAME_CNT: val = s->mac_tx_frame_cnt; break;
        case 0x400 + MAC_RX_FRAME_CNT: val = s->mac_rx_frame_cnt; break;
        case 0x400 + MAC_TX_FIFO_LVL: val = s->mac_tx_fifo_lvl; break;
        case 0x400 + MAC_DROPPED_FRMS: val = s->mac_dropped_frms; break;
    }
     
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    PCIDevice *pdev = PCI_DEVICE(s);

    if (addr >= DMA_CHAN_OFFSET && addr < DMA_CHAN_OFFSET + 0x10) {
        int chan = (addr - DMA_CHAN_OFFSET) / DMA_CHAN_SIZE;
        int reg = (addr - DMA_CHAN_OFFSET) % DMA_CHAN_SIZE;
        if (reg == 0) {
            if (val == 0xFFFFFFFF) {
                s->dma_chan_mask_written[chan] = true;
            } else {
                s->dma_chan_phys[chan] = val;
                s->dma_chan_mask_written[chan] = false;
            }
        }
        return;
    }

    switch (addr) {
        case 0x300 + FIFO_TX_REG: {
            uint32_t desc_addr = val & 0xFFFFFF;
            uint32_t phys_addr = s->dma_chan_phys[0] + desc_addr;
            uint32_t sent_flag = cpu_to_le32(TX_HDR_SENT);
            pci_dma_write(pdev, phys_addr + offsetof(struct tx_desc, header.sent), &sent_flag, 4);
            s->mac_tx_frame_cnt++;
            break;
        }
        case 0x400 + MAC_FRAME_ERR_CNT: s->mac_frame_err_cnt = val; break;
        case 0x400 + MAC_RX_ERR_CNT: s->mac_rx_err_cnt = val; break;
        case 0x400 + MAC_CRC_ERR_CNT: s->mac_crc_err_cnt = val; break;
        case 0x400 + MAC_LNK_LST_ERR_CNT: s->mac_lnk_lst_err_cnt = val; break;
        case 0x400 + MAC_TX_FRAME_CNT: s->mac_tx_frame_cnt = val; break;
        case 0x400 + MAC_RX_FRAME_CNT: s->mac_rx_frame_cnt = val; break;
        case 0x400 + MAC_TX_FIFO_LVL: s->mac_tx_fifo_lvl = val; break;
        case 0x400 + MAC_DROPPED_FRMS: s->mac_dropped_frms = val; break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
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

    s->mac_rx_err_cnt = 0;
    s->mac_crc_err_cnt = 0;
    s->mac_frame_err_cnt = 0;
    s->mac_lnk_lst_err_cnt = 0;
    s->mac_tx_frame_cnt = 0;
    s->mac_rx_frame_cnt = 0;
    s->mac_dropped_frms = 0;
    s->mac_tx_fifo_lvl = 0;
    s->dma_chan_mask_written[0] = false;
    s->dma_chan_mask_written[1] = false;
    s->dma_chan_phys[0] = 0;
    s->dma_chan_phys[1] = 0;
    s->dma_chan_mask[0] = 0xFFF00000;
    s->dma_chan_mask[1] = 0xFFF00000;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x15ec );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x5000 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_ETHERNET );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    s->num_bars = 3;
    s->bar_info[0] = (BARInfo){0, BAR_TYPE_MMIO, 0x4000, "ec_bhf_bar0"};
    s->bar_info[1] = (BARInfo){1, BAR_TYPE_NONE, 0, NULL};
    s->bar_info[2] = (BARInfo){2, BAR_TYPE_MMIO, 0x4000, "ec_bhf_bar2"};
      
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
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

/* Minimal VMState to satisfy QEMU migration subsystems */
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
