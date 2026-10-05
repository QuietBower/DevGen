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
#include "exec/memory.h"

#define TYPE_PCIBASE_DEVICE "ec_bhf_pci"

typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define FIFO_SIZE               64
#define TIMER_INTERVAL_NSEC     20000
#define INFO_BLOCK_SIZE         0x10
#define INFO_BLOCK_TYPE         0x0
#define INFO_BLOCK_REV          0x2
#define INFO_BLOCK_BLK_CNT      0x4
#define INFO_BLOCK_TX_CHAN      0x4
#define INFO_BLOCK_RX_CHAN      0x5
#define INFO_BLOCK_OFFSET       0x8
#define EC_MII_OFFSET           0x4
#define EC_FIFO_OFFSET          0x8
#define EC_MAC_OFFSET           0xc
#define MAC_FRAME_ERR_CNT       0x0
#define MAC_RX_ERR_CNT          0x1
#define MAC_CRC_ERR_CNT         0x2
#define MAC_LNK_LST_ERR_CNT     0x3
#define MAC_TX_FRAME_CNT        0x10
#define MAC_RX_FRAME_CNT        0x14
#define MAC_TX_FIFO_LVL         0x20
#define MAC_DROPPED_FRMS        0x28
#define MAC_CONNECTED_CCAT_FLAG 0x78
#define MII_MAC_ADDR            0x8
#define MII_MAC_FILT_FLAG       0xe
#define MII_LINK_STATUS         0xf
#define FIFO_TX_REG             0x0
#define FIFO_TX_RESET           0x8
#define FIFO_RX_REG             0x10
#define FIFO_RX_ADDR_VALID      (1u << 31)
#define FIFO_RX_RESET           0x18
#define DMA_CHAN_OFFSET         0x1000
#define DMA_CHAN_SIZE           0x8
#define DMA_WINDOW_SIZE_MASK    0xfffffffcU
#define ETHERCAT_MASTER_ID      0x14
#define RXHDR_NEXT_ADDR_MASK    0xffffffu
#define RXHDR_NEXT_VALID        (1u << 31)
#define RXHDR_NEXT_RECV_FLAG    0x1
#define RXHDR_LEN_MASK          0xfffu
#define PKT_PAYLOAD_SIZE        0x7e8
#define TX_HDR_PORT_0           0x1
#define TX_HDR_PORT_1           0x2
#define TX_HDR_SENT             0x1

/* Emulated descriptor header layouts as seen by the driver */

typedef struct ECTxHeader {
    uint16_t len;   /* little-endian length */
    uint8_t  port;  /* TX_HDR_PORT_* */
    uint8_t  rsvd;
    uint32_t sent;  /* TX_HDR_SENT flag */
} ECTxHeader;

typedef struct ECRxHeader {
    uint32_t next;  /* RXHDR_NEXT_VALID + addr */
    uint32_t recv;  /* RXHDR_NEXT_RECV_FLAG */
    uint16_t len;   /* payload length incl header+CRC as used by driver */
    uint16_t rsvd;
} ECRxHeader;

/* BAR metadata and device state definition */

typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
} BARType;

typedef struct {
    int index;
    BARType type;
    hwaddr size;
    const char *name;
    bool sparse;
} BARInfo;

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];

    uint8_t *mmio_backing;
    size_t mmio_backing_size;

    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;

    /* DMA/descriptor related host-side buffers */
    uint8_t *tx_buf_host;
    uint64_t tx_buf_phys;
    uint32_t tx_buf_len;
    uint32_t tx_dcount;
    uint32_t tx_dnext;

    uint8_t *rx_buf_host;
    uint64_t rx_buf_phys;
    uint32_t rx_buf_len;
    uint32_t rx_dcount;
    uint32_t rx_dnext;

    /* DMA channel registers as seen via BAR2 */
    uint32_t dma_chan_mask[8];
    uint32_t dma_chan_base[8];

    /* Base register regions derived from BAR0 info block */
    hwaddr ec_base;
    hwaddr mii_base;
    hwaddr fifo_base;
    hwaddr mac_base;

    uint8_t info_block_count;
    uint8_t info_block_type0;
    uint8_t tx_dma_chan;
    uint8_t rx_dma_chan;

    /* MAC statistics and fields */
    uint8_t mac_frame_err_cnt;
    uint8_t mac_rx_err_cnt;
    uint8_t mac_crc_err_cnt;
    uint8_t mac_lnk_lst_err_cnt;
    uint32_t mac_tx_frame_cnt;
    uint32_t mac_rx_frame_cnt;
    uint8_t mac_dropped_frms;
    uint8_t mac_tx_fifo_lvl;

    uint8_t mii_mac_addr[6];
    uint8_t mii_mac_filt_flag;
    uint8_t mii_link_status;

    /* FIFO registers */
    uint32_t fifo_tx_reg;
    uint8_t fifo_tx_reset;
    uint32_t fifo_rx_reg;
    uint8_t fifo_rx_reset;

    /* Timer for polling TX completion and generating RX packets */
    QEMUTimer timer;
    bool timer_started;

    /* Simple random/pseudo payload counter */
    uint32_t rx_payload_counter;
};

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len);
static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len);
static void pcibase_reset(DeviceState *dev);
static void pcibase_realize(PCIDevice *pdev, Error **errp);
static void pcibase_uninit(PCIDevice *pdev);

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 1 },
};

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    }
}

static inline bool addr_in_range(hwaddr addr, hwaddr base, hwaddr offset, hwaddr size)
{
    if (addr < base) {
        return false;
    }
    hwaddr rel = addr - base;
    if (rel < offset) {
        return false;
    }
    if (rel - offset >= size) {
        return false;
    }
    return true;
}

static inline hwaddr reg_off(hwaddr addr, hwaddr base)
{
    return addr - base;
}

static void ec_bhf_timer_cb(void *opaque);

static void ec_bhf_schedule_timer(PCIBaseState *s)
{
    if (!s->timer_started) {
        timer_mod_ns(&s->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + TIMER_INTERVAL_NSEC);
        s->timer_started = true;
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    /* Info block region (BAR0 offset 0) */
    if (addr_in_range(addr, 0, 0, 0x1000)) {
        hwaddr off = addr;
        if (off == INFO_BLOCK_BLK_CNT && size == 1) {
            return s->info_block_count;
        }
        if (off < INFO_BLOCK_SIZE * s->info_block_count) {
            unsigned idx = off / INFO_BLOCK_SIZE;
            unsigned roff = off % INFO_BLOCK_SIZE;
            if (idx == 0) {
                switch (roff) {
                case INFO_BLOCK_TYPE:
                    if (size == 2) {
                        return ETHERCAT_MASTER_ID;
                    } else if (size == 1) {
                        return ETHERCAT_MASTER_ID & 0xff;
                    }
                    break;
                case INFO_BLOCK_TX_CHAN:
                    if (size == 1) {
                        return s->tx_dma_chan;
                    }
                    break;
                case INFO_BLOCK_RX_CHAN:
                    if (size == 1) {
                        return s->rx_dma_chan;
                    }
                    break;
                case INFO_BLOCK_OFFSET:
                    if (size == 4) {
                        return s->ec_base;
                    }
                    break;
                default:
                    break;
                }
            }
        }
    }

    /* EC base indirection registers */
    if (addr_in_range(addr, s->ec_base, EC_MII_OFFSET, 4)) {
        if (size == 4) {
            if (reg_off(addr, s->ec_base) == EC_MII_OFFSET) {
                return s->mii_base;
            }
            if (reg_off(addr, s->ec_base) == EC_FIFO_OFFSET) {
                return s->fifo_base;
            }
            if (reg_off(addr, s->ec_base) == EC_MAC_OFFSET) {
                return s->mac_base;
            }
        }
    }

    /* MII / MAC address registers */
    if (addr_in_range(addr, s->mii_base, 0, 0x100)) {
        hwaddr moff = addr - s->mii_base;
        if (moff >= MII_MAC_ADDR && moff < MII_MAC_ADDR + 6 && size == 1) {
            return s->mii_mac_addr[moff - MII_MAC_ADDR];
        }
        if (moff == MII_MAC_FILT_FLAG && size == 1) {
            return s->mii_mac_filt_flag;
        }
        if (moff == MII_LINK_STATUS && size == 1) {
            return s->mii_link_status;
        }
    }

    /* MAC statistics area */
    if (addr_in_range(addr, s->mac_base, 0, 0x100)) {
        hwaddr moff = addr - s->mac_base;
        switch (moff) {
        case MAC_FRAME_ERR_CNT:
            if (size == 1) return s->mac_frame_err_cnt;
            break;
        case MAC_RX_ERR_CNT:
            if (size == 1) return s->mac_rx_err_cnt;
            break;
        case MAC_CRC_ERR_CNT:
            if (size == 1) return s->mac_crc_err_cnt;
            break;
        case MAC_LNK_LST_ERR_CNT:
            if (size == 1) return s->mac_lnk_lst_err_cnt;
            break;
        case MAC_TX_FRAME_CNT:
            if (size == 4) return s->mac_tx_frame_cnt;
            break;
        case MAC_RX_FRAME_CNT:
            if (size == 4) return s->mac_rx_frame_cnt;
            break;
        case MAC_TX_FIFO_LVL:
            if (size == 1) return s->mac_tx_fifo_lvl;
            break;
        case MAC_DROPPED_FRMS:
            if (size == 1) return s->mac_dropped_frms;
            break;
        case MAC_CONNECTED_CCAT_FLAG:
            if (size == 1) return 1;
            break;
        default:
            break;
        }
    }

    /* FIFO area */
    if (addr_in_range(addr, s->fifo_base, 0, 0x100)) {
        hwaddr foff = addr - s->fifo_base;
        switch (foff) {
        case FIFO_TX_REG:
            if (size == 4) {
                return s->fifo_tx_reg;
            }
            break;
        case FIFO_RX_REG:
            if (size == 4) {
                return s->fifo_rx_reg;
            }
            break;
        case FIFO_TX_RESET:
            if (size == 1) {
                return s->fifo_tx_reset;
            }
            break;
        case FIFO_RX_RESET:
            if (size == 1) {
                return s->fifo_rx_reset;
            }
            break;
        default:
            break;
        }
    }

    /* DMA channel registers (BAR2) */
    if (addr_in_range(addr, 0, DMA_CHAN_OFFSET, DMA_CHAN_SIZE * 8)) {
        hwaddr doff = addr - DMA_CHAN_OFFSET;
        unsigned chan = doff / DMA_CHAN_SIZE;
        unsigned roff = doff % DMA_CHAN_SIZE;
        if (chan < 8 && size == 4) {
            if (roff == 0) {
                return s->dma_chan_mask[chan];
            } else if (roff == 4) {
                return s->dma_chan_base[chan];
            }
        }
    }

    qemu_log_mask(LOG_UNIMP, "[%s] mmio_read addr=%"PRIx64" size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
    return 0;
}

static void ec_bhf_handle_tx(PCIBaseState *s)
{
    if (!s->tx_buf_host || !s->tx_dcount) {
        return;
    }

    for (;;) {
        if (s->tx_dnext >= s->tx_dcount) {
            s->tx_dnext = 0;
        }
        uint32_t idx = s->tx_dnext;
        uint8_t *tdesc = s->tx_buf_host + idx * (sizeof(ECTxHeader) + PKT_PAYLOAD_SIZE);
        ECTxHeader *hdr = (ECTxHeader *)tdesc;
        uint32_t sent = le32_to_cpu(hdr->sent);
        if (sent & TX_HDR_SENT) {
            break;
        }
        uint16_t len = le16_to_cpu(hdr->len);
        (void)len;
        sent |= TX_HDR_SENT;
        hdr->sent = cpu_to_le32(sent);
        s->mac_tx_frame_cnt++;
        s->tx_dnext = (s->tx_dnext + 1) % s->tx_dcount;
    }
}

static void ec_bhf_handle_rx(PCIBaseState *s)
{
    if (!s->rx_buf_host || !s->rx_dcount) {
        return;
    }

    uint32_t idx = s->rx_dnext;
    uint8_t *rdesc = s->rx_buf_host + idx * (sizeof(ECRxHeader) + PKT_PAYLOAD_SIZE);
    ECRxHeader *hdr = (ECRxHeader *)rdesc;

    uint32_t recv = le32_to_cpu(hdr->recv);
    if (recv & RXHDR_NEXT_RECV_FLAG) {
        return;
    }

    uint8_t *data = rdesc + sizeof(ECRxHeader);
    uint32_t pkt_size = 60;
    if (pkt_size > PKT_PAYLOAD_SIZE) {
        pkt_size = PKT_PAYLOAD_SIZE;
    }
    for (uint32_t i = 0; i < pkt_size; i++) {
        data[i] = (uint8_t)(s->rx_payload_counter + i);
    }
    s->rx_payload_counter++;

    uint16_t hw_len = pkt_size + sizeof(ECRxHeader) + 4;
    hdr->len = cpu_to_le16(hw_len & RXHDR_LEN_MASK);
    hdr->recv = cpu_to_le32(recv | RXHDR_NEXT_RECV_FLAG);
    s->mac_rx_frame_cnt++;

    uint32_t next = le32_to_cpu(hdr->next);
    uint32_t next_addr = next & RXHDR_NEXT_ADDR_MASK;
    if (next_addr != 0) {
        s->rx_dnext = next_addr / (sizeof(ECRxHeader) + PKT_PAYLOAD_SIZE);
    }
}

static void ec_bhf_timer_cb(void *opaque)
{
    PCIBaseState *s = opaque;

    ec_bhf_handle_tx(s);
    ec_bhf_handle_rx(s);

    timer_mod_ns(&s->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + TIMER_INTERVAL_NSEC);
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* MAC statistics writable registers */
    if (addr_in_range(addr, s->mac_base, 0, 0x100)) {
        hwaddr moff = addr - s->mac_base;
        switch (moff) {
        case MAC_FRAME_ERR_CNT:
            if (size == 1) s->mac_frame_err_cnt = (uint8_t)val;
            return;
        case MAC_RX_ERR_CNT:
            if (size == 1) s->mac_rx_err_cnt = (uint8_t)val;
            return;
        case MAC_CRC_ERR_CNT:
            if (size == 1) s->mac_crc_err_cnt = (uint8_t)val;
            return;
        case MAC_LNK_LST_ERR_CNT:
            if (size == 1) s->mac_lnk_lst_err_cnt = (uint8_t)val;
            return;
        case MAC_TX_FRAME_CNT:
            if (size == 4) s->mac_tx_frame_cnt = (uint32_t)val;
            return;
        case MAC_RX_FRAME_CNT:
            if (size == 4) s->mac_rx_frame_cnt = (uint32_t)val;
            return;
        case MAC_TX_FIFO_LVL:
            if (size == 1) s->mac_tx_fifo_lvl = (uint8_t)val;
            return;
        case MAC_DROPPED_FRMS:
            if (size == 1) s->mac_dropped_frms = (uint8_t)val;
            return;
        default:
            break;
        }
    }

    /* MII flags */
    if (addr_in_range(addr, s->mii_base, 0, 0x100)) {
        hwaddr moff = addr - s->mii_base;
        if (moff == MII_MAC_FILT_FLAG && size == 1) {
            s->mii_mac_filt_flag = (uint8_t)val;
            return;
        }
    }

    /* FIFO region */
    if (addr_in_range(addr, s->fifo_base, 0, 0x100)) {
        hwaddr foff = addr - s->fifo_base;
        switch (foff) {
        case FIFO_TX_REG:
            if (size == 4) {
                s->fifo_tx_reg = (uint32_t)val;
                if (s->tx_buf_host && s->tx_dcount) {
                    uint32_t desc_off = (uint32_t)val & 0x00ffffffU;
                    (void)desc_off;
                    ec_bhf_schedule_timer(s);
                }
                return;
            }
            break;
        case FIFO_RX_REG:
            if (size == 4) {
                s->fifo_rx_reg = (uint32_t)val;
                ec_bhf_schedule_timer(s);
                return;
            }
            break;
        case FIFO_TX_RESET:
            if (size == 1) {
                s->fifo_tx_reset = (uint8_t)val;
                s->tx_dnext = 0;
                return;
            }
            break;
        case FIFO_RX_RESET:
            if (size == 1) {
                s->fifo_rx_reset = (uint8_t)val;
                s->rx_dnext = 0;
                return;
            }
            break;
        default:
            break;
        }
    }

    /* DMA channel register writes */
    if (addr_in_range(addr, 0, DMA_CHAN_OFFSET, DMA_CHAN_SIZE * 8)) {
        hwaddr doff = addr - DMA_CHAN_OFFSET;
        unsigned chan = doff / DMA_CHAN_SIZE;
        unsigned roff = doff % DMA_CHAN_SIZE;
        if (chan < 8 && size == 4) {
            if (roff == 0) {
                s->dma_chan_mask[chan] = (uint32_t)val;
                return;
            } else if (roff == 4) {
                s->dma_chan_base[chan] = (uint32_t)val;
                if (chan == s->rx_dma_chan) {
                    s->rx_buf_phys = s->dma_chan_base[chan];
                } else if (chan == s->tx_dma_chan) {
                    s->tx_buf_phys = s->dma_chan_base[chan];
                }
                return;
            }
        }
    }

    qemu_log_mask(LOG_UNIMP, "[%s] mmio_write addr=%"PRIx64" val=%"PRIx64" size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, (uint64_t)val, size);
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "[%s] pio_read addr=%"PRIx64" size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "[%s] pio_write addr=%"PRIx64" val=%"PRIx64" size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, (uint64_t)val, size);
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    pci_device_reset(pdev);
    msi_reset(pdev);
    msix_reset(pdev);

    if (s->mmio_backing && s->mmio_backing_size) {
        memset(s->mmio_backing, 0, s->mmio_backing_size);
    }

    s->mac_frame_err_cnt = 0;
    s->mac_rx_err_cnt = 0;
    s->mac_crc_err_cnt = 0;
    s->mac_lnk_lst_err_cnt = 0;
    s->mac_tx_frame_cnt = 0;
    s->mac_rx_frame_cnt = 0;
    s->mac_dropped_frms = 0;
    s->mac_tx_fifo_lvl = 0;

    s->fifo_tx_reg = 0;
    s->fifo_tx_reset = 0;
    s->fifo_rx_reg = 0;
    s->fifo_rx_reset = 0;

    s->tx_dnext = 0;
    s->rx_dnext = 0;

    s->mii_link_status = 1;

    s->rx_payload_counter = 0;

    if (s->timer_started) {
        timer_del(&s->timer);
        s->timer_started = false;
    }
}

static void pcibase_dma_device_realize(PCIDevice *pdev, Error **errp)
{
    
}

static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    uint32_t val = pci_default_read_config(pdev, addr, len);
    return val;
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    pci_default_write_config(pdev, addr, val, len);
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    s->num_bars = 3;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x2000;
    s->bar_info[0].name = "ec_bhf-bar0";
    s->bar_info[0].sparse = false;

    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_NONE;
    s->bar_info[1].size = 0;
    s->bar_info[1].name = NULL;
    s->bar_info[1].sparse = false;

    s->bar_info[2].index = 2;
    s->bar_info[2].type = BAR_TYPE_MMIO;
    s->bar_info[2].size = 0x2000;
    s->bar_info[2].name = "ec_bhf-bar2";
    s->bar_info[2].sparse = false;

    s->info_block_count = 1;
    s->info_block_type0 = ETHERCAT_MASTER_ID;
    s->tx_dma_chan = 0;
    s->rx_dma_chan = 1;

    s->ec_base = 0x100;
    s->mii_base = s->ec_base + 0x100;
    s->fifo_base = s->ec_base + 0x200;
    s->mac_base = s->ec_base + 0x300;

    s->mii_mac_addr[0] = 0x02;
    s->mii_mac_addr[1] = 0x00;
    s->mii_mac_addr[2] = 0x00;
    s->mii_mac_addr[3] = 0x00;
    s->mii_mac_addr[4] = 0x00;
    s->mii_mac_addr[5] = 0x01;

    pci_config_set_interrupt_pin(pci_conf, 0);
    pci_config_set_vendor_id(pci_conf, 0x15ec);
    pci_config_set_device_id(pci_conf, 0x5000);
    pci_config_set_class(pci_conf, PCI_CLASS_NETWORK_ETHERNET);

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
        if (errp && *errp) {
            return;
        }
    }

    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) {
        return;
    }

    s->has_msi = false;
    s->has_msix = false;

    s->mmio_backing = NULL;
    s->mmio_backing_size = 0;

    timer_init_ns(&s->timer, QEMU_CLOCK_VIRTUAL, ec_bhf_timer_cb, s);
    s->timer_started = false;

    pcibase_reset(DEVICE(pdev));

}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (s->has_msix) {
        msix_uninit(pdev, NULL, 0);
    }
    if (s->has_msi) {
        msi_uninit(pdev);
    }

    if (s->mmio_backing) {
        g_free(s->mmio_backing);
    }

    if (s->timer_started) {
        timer_del(&s->timer);
        s->timer_started = false;
    }
}

static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->config_read  = pcibase_config_read;
    k->config_write = pcibase_config_write;

    k->realize = pcibase_realize;
    k->exit    = pcibase_uninit;
    dc->reset  = pcibase_reset;

    k->vendor_id = 0x15ec;
    k->device_id = 0x5000;
    k->revision = 0x00;
    k->class_id = PCI_CLASS_NETWORK_ETHERNET;

    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
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
