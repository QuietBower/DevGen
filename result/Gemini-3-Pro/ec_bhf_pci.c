/*
 * Generic QEMU PCI device template (QEMU 8.2.x)
 *
 * Phase 1: fill register macros, BAR list, BAR sizes, structs, enums.
 * Phase 2: implement MMIO/PIO read/write, DMA init, IRQ init, reset, config hooks
 * Phase 3 (Repair Phase)：correct syntax, types, includes, missing symbols, remove dead code
 * Phase 4 (Debug & Update Phase)：Based on actual kernel logs from QEMU boot, repair the virtual hardware behavior
 *
 * Replace #PLACEHOLDER# blocks with device-specific code/data.
 * NOTE:
 * - Only essential fixes added (TYPE macro, PIO BAR handling, MMIO impl, DMA init, IRQ).
 * - No optional or advanced features added.
 * - Do not directly fill this file with the Linux kernel source code
 */

#include "qemu/osdep.h"
#include <inttypes.h>
#include <string.h>
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "sysemu/dma.h"
#include "sysemu/reset.h"
#include "hw/irq.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/pci/pcie.h"
#include "qom/object.h"
#include "qapi/visitor.h"
#include "hw/qdev-properties.h"

/* ------------------------------------------------------------------ */
/* Additional include files (Stage1 optional)                          */
/* ------------------------------------------------------------------ */


#define TYPE_PCIBASE_DEVICE "ec_bhf_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Related register/macros/constants from driver (Stage1) */
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

/* ------------------------------------------------------------------ */
/* BAR metadata definition                                             */
/* ------------------------------------------------------------------ */
typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM 
} BARType;

typedef struct {
    int index;
    BARType type;
    hwaddr size;
    const char *name;
    bool sparse;
} BARInfo;

/* ------------------------------------------------------------------ */
/* Device State                                                        */
/* ------------------------------------------------------------------ */
#define RX_FIFO_CAPACITY 256

typedef struct {
    uint32_t base;
    uint32_t mask;
} DMAChannel;

struct PCIBaseState {
    PCIDevice parent_obj;

    /* BAR memory regions (MMIO/PIO unified handling fix) */
    MemoryRegion bar_regions[6];

    /* optional linear backing */
    uint8_t *mmio_backing;
    size_t mmio_backing_size;

    /* BAR table */
    BARInfo bar_info[6];
    int num_bars;

    /* interrupt state feature flags */
    bool has_msi;
    bool has_msix;
    
    /* Device Specific State */
    uint8_t mac_addr[6];
    
    /* MAC counters */
    uint32_t tx_frames;
    uint32_t rx_frames;
    uint8_t rx_err;
    uint8_t crc_err;
    uint8_t frame_err;
    uint8_t dropped_frames;

    /* DMA Channels (BAR 2) */
    DMAChannel dma_ch[2]; /* 0=TX, 1=RX */

    /* RX Descriptor FIFO (offsets in RX DMA buffer) */
    uint32_t rx_fifo[RX_FIFO_CAPACITY];
    int rx_fifo_head;
    int rx_fifo_tail;
    int rx_fifo_count;
};

/* Offsets within BAR 0 */
#define OFF_INFO_BLOCK      0x00
#define OFF_EC_BASE         0x100
#define OFF_MII             0x200 /* Relative to EC_BASE */
#define OFF_FIFO            0x300 /* Relative to EC_BASE */
#define OFF_MAC             0x400 /* Relative to EC_BASE */

/* ------------------------------------------------------------------ */
/* Forward declarations                                                */
/* ------------------------------------------------------------------ */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint64_t pcibase_dma_mmio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_dma_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len);
static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len);
static void pcibase_reset(DeviceState *dev);
static void pcibase_realize(PCIDevice *pdev, Error **errp);
static void pcibase_uninit(PCIDevice *pdev);

/* ------------------------------------------------------------------ */
/* MemoryRegionOps                                                      */
/* ------------------------------------------------------------------ */
static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static const MemoryRegionOps pcibase_dma_mmio_ops = {
    .read = pcibase_dma_mmio_read,
    .write = pcibase_dma_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* ------------------------------------------------------------------ */
/* Helper: register a BAR (MMIO or PIO)                                 */
/* ------------------------------------------------------------------ */
static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE)
        return;

    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        if (bi->index == 2) {
             memory_region_init_io(mr, OBJECT(s), &pcibase_dma_mmio_ops, s, bi->name, bi->size);
        } else {
             memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, bi->size);
        }
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    }else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, bi->size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

/* ------------------------------------------------------------------ */
/* MMIO / PIO handlers                                                */
/* ------------------------------------------------------------------ */

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Info Block (0x0 - 0x10) */
    if (addr < 0x10) {
        switch (addr) {
        case INFO_BLOCK_TYPE: /* 0x0 */
            val = ETHERCAT_MASTER_ID; /* 0x14 */
            break;
        case INFO_BLOCK_BLK_CNT: /* 0x4 */
            if (size == 1) val = 1;
            else if (size == 2) val = 0; /* TX Chan is at 0x4 in block? No, 0x4 is BLK_CNT global? */
            /* Driver: block_count = ioread8(priv->io + INFO_BLOCK_BLK_CNT); */
            /* Then iterates blocks. Block 0 starts at 0x0? No. */
            /* Driver: ioread16(priv->io + i * INFO_BLOCK_SIZE + INFO_BLOCK_TYPE) */
            /* If i=0, addr=0. */
            /* Wait, INFO_BLOCK_BLK_CNT is 0x4. INFO_BLOCK_TYPE is 0x0. */
            /* If block 0 is at 0x0, then BLK_CNT at 0x4 overlaps with Block 0 data? */
            /* Driver logic: block_count = ioread8(priv->io + 4). */
            /* Then loop i=0..count. type = ioread16(priv->io + i*16 + 0). */
            /* If i=0, type is at 0x0. */
            /* This implies Block 0 is at 0x0. But 0x4 is BLK_CNT. */
            /* This suggests the blocks start AFTER the header? */
            /* But driver says: ec_info = priv->io + i * INFO_BLOCK_SIZE; */
            /* If i=0, ec_info = priv->io. */
            /* So Block 0 is at 0x0. */
            /* And BLK_CNT is at 0x4. */
            /* So Block 0 Offset 0x4 (INFO_BLOCK_TX_CHAN) overlaps with BLK_CNT? */
            /* Let's look at defines: INFO_BLOCK_TX_CHAN 0x4. INFO_BLOCK_BLK_CNT 0x4. */
            /* This is a conflict in the driver defines or layout. */
            /* However, the driver reads BLK_CNT first. */
            /* Maybe Block 0 is NOT at 0x0? */
            /* Driver: for (i = 0; i < block_count; i++) ... */
            /* If i=0, it reads type at 0x0. */
            /* If type == MASTER_ID (0x14), it breaks. */
            /* Then ec_info = priv->io + i * 0x10. */
            /* Then tx_chan = ioread8(ec_info + 0x4). */
            /* If i=0, tx_chan is at 0x4. */
            /* But 0x4 is also BLK_CNT. */
            /* This implies that for the Master block, byte 4 is TX_CHAN. */
            /* But globally byte 4 is BLK_CNT. */
            /* Maybe the Master block is NOT block 0? */
            /* Or maybe BLK_CNT is only valid before we interpret it as a block? */
            /* Let's assume Block 0 is the Master block. */
            /* So at 0x4 we return 1 (BLK_CNT) AND 0 (TX_CHAN)? */
            /* TX_CHAN is 1 byte. BLK_CNT is 1 byte. */
            /* If I return 1 at 0x4. */
            /* Driver reads BLK_CNT=1. Loop runs once. */
            /* Driver reads Type at 0x0 -> 0x14. Match. */
            /* Driver reads TX_CHAN at 0x4 -> 1. */
            /* So TX_CHAN = 1. */
            /* Driver reads RX_CHAN at 0x5 -> 1? */
            /* Let's set TX=0, RX=1. */
            /* So at 0x4, return 1 (BLK_CNT). But wait, if TX_CHAN=0, then 0x4 must be 0? */
            /* If 0x4 is 0, BLK_CNT=0, loop doesn't run. Fail. */
            /* So Block 0 CANNOT be the Master block if TX_CHAN=0. */
            /* Let's make Block 0 a dummy block, and Block 1 the Master block. */
            /* Block 0: Type=0xFFFF. */
            /* Block 1 (0x10): Type=0x14. */
            /* BLK_CNT (0x4) = 2. */
            if (addr == INFO_BLOCK_BLK_CNT) val = 2;
            else if (addr == INFO_BLOCK_TYPE) val = 0xFFFF;
            break;
        }
    } else if (addr >= 0x10 && addr < 0x20) {
        /* Block 1 (0x10) - The Master Block */
        uint32_t rel = addr - 0x10;
        switch (rel) {
        case INFO_BLOCK_TYPE: val = ETHERCAT_MASTER_ID; break;
        case INFO_BLOCK_TX_CHAN: val = 0; break;
        case INFO_BLOCK_RX_CHAN: val = 1; break;
        case INFO_BLOCK_OFFSET: val = OFF_EC_BASE; break;
        }
    }

    /* EC Header (OFF_EC_BASE = 0x100) */
    if (addr >= OFF_EC_BASE && addr < OFF_EC_BASE + 0x10) {
        uint32_t rel = addr - OFF_EC_BASE;
        switch (rel) {
        case EC_MII_OFFSET: val = OFF_MII; break;
        case EC_FIFO_OFFSET: val = OFF_FIFO; break;
        case EC_MAC_OFFSET: val = OFF_MAC; break;
        }
    }

    /* MII (OFF_EC_BASE + OFF_MII = 0x300) */
    if (addr >= (OFF_EC_BASE + OFF_MII) && addr < (OFF_EC_BASE + OFF_MII + 0x10)) {
        uint32_t rel = addr - (OFF_EC_BASE + OFF_MII);
        if (rel >= MII_MAC_ADDR && rel < MII_MAC_ADDR + 6) {
            val = s->mac_addr[rel - MII_MAC_ADDR];
        }
    }

    /* MAC Counters (OFF_EC_BASE + OFF_MAC = 0x500) */
    if (addr >= (OFF_EC_BASE + OFF_MAC) && addr < (OFF_EC_BASE + OFF_MAC + 0x80)) {
        uint32_t rel = addr - (OFF_EC_BASE + OFF_MAC);
        switch (rel) {
        case MAC_FRAME_ERR_CNT: val = s->frame_err; break;
        case MAC_RX_ERR_CNT: val = s->rx_err; break;
        case MAC_CRC_ERR_CNT: val = s->crc_err; break;
        case MAC_LNK_LST_ERR_CNT: val = 0; break;
        case MAC_TX_FRAME_CNT: val = s->tx_frames; break;
        case MAC_RX_FRAME_CNT: val = s->rx_frames; break;
        case MAC_DROPPED_FRMS: val = s->dropped_frames; break;
        }
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* FIFO (OFF_EC_BASE + OFF_FIFO = 0x400) */
    if (addr >= (OFF_EC_BASE + OFF_FIFO) && addr < (OFF_EC_BASE + OFF_FIFO + 0x20)) {
        uint32_t rel = addr - (OFF_EC_BASE + OFF_FIFO);
        if (rel == FIFO_TX_REG) {
            /* TX Packet */
            /* val = (ALIGN(len, 8) << 24) | addr */
            uint32_t tx_addr = val & 0xFFFFFF;
            uint32_t len = val >> 24;
            (void)len;

            /* Process TX: Read Desc, Set Sent, Write Back */
            dma_addr_t desc_addr = s->dma_ch[0].base + tx_addr;
            struct tx_desc desc;
            if (pci_dma_read(&s->parent_obj, desc_addr, &desc, sizeof(desc))) {
                return;
            }

            /* Loopback Logic: Send to RX if possible */
            if (s->rx_fifo_count > 0) {
                uint32_t rx_offset = s->rx_fifo[s->rx_fifo_head];
                s->rx_fifo_head = (s->rx_fifo_head + 1) % RX_FIFO_CAPACITY;
                s->rx_fifo_count--;

                dma_addr_t rx_desc_addr = s->dma_ch[1].base + rx_offset;
                struct rx_desc rx_desc;
                if (pci_dma_read(&s->parent_obj, rx_desc_addr, &rx_desc, sizeof(rx_desc)) == 0) {
                    /* Copy data */
                    memcpy(rx_desc.data, desc.data, PKT_PAYLOAD_SIZE);
                    /* Update header */
                    rx_desc.header.len = desc.header.len;
                    rx_desc.header.port = 0;
                    rx_desc.header.recv = cpu_to_le32(RXHDR_NEXT_RECV_FLAG);
                    
                    pci_dma_write(&s->parent_obj, rx_desc_addr, &rx_desc, sizeof(rx_desc));
                    s->rx_frames++;
                }
            } else {
                s->dropped_frames++;
            }

            /* Mark TX as sent */
            desc.header.sent = cpu_to_le32(TX_HDR_SENT);
            pci_dma_write(&s->parent_obj, desc_addr, &desc, sizeof(desc));
            s->tx_frames++;

        } else if (rel == FIFO_RX_REG) {
            /* Add RX Desc */
            /* val = FIFO_RX_ADDR_VALID | addr */
            if (val & FIFO_RX_ADDR_VALID) {
                uint32_t rx_addr = val & ~FIFO_RX_ADDR_VALID;
                if (s->rx_fifo_count < RX_FIFO_CAPACITY) {
                    s->rx_fifo[s->rx_fifo_tail] = rx_addr;
                    s->rx_fifo_tail = (s->rx_fifo_tail + 1) % RX_FIFO_CAPACITY;
                    s->rx_fifo_count++;
                }
            }
        } else if (rel == FIFO_TX_RESET || rel == FIFO_RX_RESET) {
            if (rel == FIFO_TX_RESET) {
                /* Reset TX logic if needed */
            } else {
                s->rx_fifo_head = 0;
                s->rx_fifo_tail = 0;
                s->rx_fifo_count = 0;
            }
        }
    }

    /* MAC Counters Reset */
    if (addr >= (OFF_EC_BASE + OFF_MAC) && addr < (OFF_EC_BASE + OFF_MAC + 0x80)) {
        uint32_t rel = addr - (OFF_EC_BASE + OFF_MAC);
        switch (rel) {
        case MAC_FRAME_ERR_CNT: s->frame_err = 0; break;
        case MAC_RX_ERR_CNT: s->rx_err = 0; break;
        case MAC_CRC_ERR_CNT: s->crc_err = 0; break;
        case MAC_TX_FRAME_CNT: s->tx_frames = 0; break;
        case MAC_RX_FRAME_CNT: s->rx_frames = 0; break;
        case MAC_DROPPED_FRMS: s->dropped_frames = 0; break;
        }
    }
}

static uint64_t pcibase_dma_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    /* addr is relative to BAR 2 base */
    /* Channel 0: 0x1000. Channel 1: 0x1008. */
    /* Each channel has 8 bytes. First 4 bytes: Address/Mask. Next 4: Control. */
    
    int channel = -1;
    if (addr >= 0x1000 && addr < 0x1008) channel = 0;
    else if (addr >= 0x1008 && addr < 0x1010) channel = 1;

    if (channel != -1) {
        uint32_t offset_in_ch = addr & 0x7;
        if (offset_in_ch == 0) {
            /* Return mask (size) always, as driver writes -1 then reads. */
            /* We enforce a 4MB window for simplicity (mask 0xFFC00000) */
            /* But we must also return the base if set? */
            /* Driver logic: mask = ioread32(). mask &= DMA_WINDOW_SIZE_MASK. */
            /* If we return 0xFFC00000, driver sees mask 0xFFC00000. */
            /* Size = ~mask + 1 = 0x400000 (4MB). */
            return s->dma_ch[channel].base | 0xFFC00000;
        }
    }
    return 0;
}

static void pcibase_dma_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    int channel = -1;
    if (addr >= 0x1000 && addr < 0x1008) channel = 0;
    else if (addr >= 0x1008 && addr < 0x1010) channel = 1;

    if (channel != -1) {
        uint32_t offset_in_ch = addr & 0x7;
        if (offset_in_ch == 0) {
            /* If val is 0xFFFFFFFF, it's a probe. We don't change base. */
            if ((uint32_t)val != 0xFFFFFFFF) {
                s->dma_ch[channel].base = val & 0xFFC00000;
            }
        }
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

/* ------------------------------------------------------------------ */
/* Reset       */
/* ------------------------------------------------------------------ */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    pci_device_reset(pdev);

    s->tx_frames = 0;
    s->rx_frames = 0;
    s->rx_err = 0;
    s->crc_err = 0;
    s->frame_err = 0;
    s->dropped_frames = 0;

    s->dma_ch[0].base = 0;
    s->dma_ch[1].base = 0;

    s->rx_fifo_head = 0;
    s->rx_fifo_tail = 0;
    s->rx_fifo_count = 0;
}

/* ------------------------------------------------------------------ */
/* PCI config space access (class-level hooks)                         */
/* ------------------------------------------------------------------ */
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    return pci_default_read_config(pdev, addr, len);
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    pci_default_write_config(pdev, addr, val, len);
}

/* ------------------------------------------------------------------ */
/* Realize (device init)                                                */
/* ------------------------------------------------------------------ */
static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x15ec );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x5000 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_ETHERNET );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);
    
    /* Default MAC Address */
    s->mac_addr[0] = 0x52;
    s->mac_addr[1] = 0x54;
    s->mac_addr[2] = 0x00;
    s->mac_addr[3] = 0x12;
    s->mac_addr[4] = 0x34;
    s->mac_addr[5] = 0x56;

    /* BAR 0: Main MMIO (Registers) */
    s->bar_info[0] = (BARInfo){
        .index = 0,
        .type = BAR_TYPE_MMIO,
        .size = 0x1000, 
        .name = "ec_bhf.mmio"
    };

    /* BAR 2: DMA MMIO */
    s->bar_info[2] = (BARInfo){
        .index = 2,
        .type = BAR_TYPE_MMIO,
        .size = 0x2000, 
        .name = "ec_bhf.dma"
    };
    
    s->num_bars = 3;

    /* register BARs */
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
}

/* ------------------------------------------------------------------ */
/* Uninit/cleanup                                                      */
/* ------------------------------------------------------------------ */
static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (s->has_msix) {
        msix_uninit(pdev,NULL,0);
        s->has_msix = false;
    }
    if (s->has_msi) {
        msi_uninit(pdev);
        s->has_msi = false;
    }
}

/* ------------------------------------------------------------------ */
/* Class init / type registration                                      */
/* ------------------------------------------------------------------ */
static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->config_read  = pcibase_config_read;
    k->config_write = pcibase_config_write;

    k->realize = pcibase_realize;
    k->exit    = pcibase_uninit;
    dc->reset  = pcibase_reset;

    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void pcibase_register_types(void)
{
    static InterfaceInfo interfaces[] = {
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