/*
 * QEMU PCI device model for ec_bhf (EtherCAT master)
 * Auto-generated from Linux driver drivers/net/ethernet/ec_bhf.c
 * Target QEMU version: 8.2.10
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
#include "hw/qdev-core.h"
#include "hw/qdev-core.h"
#include "hw/qdev-properties.h"

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
#define EC_MAC_OFFSET           0xC
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
#define MII_MAC_FILT_FLAG       0xE
#define MII_LINK_STATUS         0xF
#define FIFO_TX_REG             0x0
#define FIFO_TX_RESET           0x8
#define FIFO_RX_REG             0x10
#define FIFO_RX_ADDR_VALID      (1u << 31)
#define FIFO_RX_RESET           0x18
#define DMA_CHAN_OFFSET         0x1000
#define DMA_CHAN_SIZE           0x8
#define DMA_WINDOW_SIZE_MASK    0xFFFFFFFC
#define ETHERCAT_MASTER_ID      0x14
#define RXHDR_NEXT_ADDR_MASK    0xFFFFFFu
#define RXHDR_NEXT_VALID        (1u << 31)
#define RXHDR_NEXT_RECV_FLAG    0x1
#define RXHDR_LEN_MASK          0xFFFu
#define PKT_PAYLOAD_SIZE        0x7E8
#define TX_HDR_PORT_0           0x1
#define TX_HDR_PORT_1           0x2
#define TX_HDR_SENT             0x1
#define PRIV_TO_DEV(priv) (&(priv)->dev->dev)

#define EC_BHF_VENDOR_ID 0x15ec
#define EC_BHF_DEVICE_ID 0x5000

/* BAR metadata */
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

/* RX/TX descriptor header layout is not defined in driver
 * and is handled only by guest DMA code, so we do not model
 * descriptor memory here. We only emulate the MMIO interface
 * and DMA window-size registers used during allocation.
 */

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];

    uint8_t *mmio_backing;
    size_t mmio_backing_size;

    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;

    /* Device-specific state derived from driver usage */

    /* BAR0 root pointer (priv->io) and sub-blocks */
    hwaddr ec_block_index;      /* which info-block is EtherCAT master */

    /* Offsets inside BAR0 space, as seen by driver after setup_offsets */
    uint32_t ec_base_offset;    /* value returned by ioread32(INFO_BLOCK_OFFSET) */
    uint32_t mii_rel_offset;    /* value returned by ioread32(ec_io + EC_MII_OFFSET) */
    uint32_t fifo_rel_offset;   /* value returned by ioread32(ec_io + EC_FIFO_OFFSET) */
    uint32_t mac_rel_offset;    /* value returned by ioread32(ec_io + EC_MAC_OFFSET) */

    /* MAC statistics / control registers */
    uint8_t mac_frame_err_cnt;
    uint8_t mac_rx_err_cnt;
    uint8_t mac_crc_err_cnt;
    uint8_t mac_lnk_lst_err_cnt;
    uint32_t mac_tx_frame_cnt;
    uint32_t mac_rx_frame_cnt;
    uint8_t mac_dropped_frms;
    uint8_t mac_tx_fifo_lvl;
    uint8_t mac_mac_filt_flag;
    uint8_t mac_connected_ccat_flag;

    /* MII block - MAC address and link status */
    uint8_t mac_addr[6];
    uint8_t mii_link_status;

    /* FIFO registers (store last values written) */
    uint32_t fifo_tx_reg;
    uint8_t fifo_tx_reset;
    uint32_t fifo_rx_reg;
    uint8_t fifo_rx_reset;

    /* DMA channel registers (BAR2, priv->dma_io) */
    uint32_t dma_chan_reg[2];        /* offset 0x1000, 0x1008 */
    uint32_t dma_chan_reg_aux[2];    /* offset 0x1004, 0x100C */

    /* Timer to emulate polling-based RX/TX completion */
    QEMUTimer *poll_timer;
    bool net_up;
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
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
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
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, bi->size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

/* Helper to locate sub-blocks inside BAR0 as used by driver */
static inline hwaddr ec_get_ec_base(PCIBaseState *s)
{
    return s->ec_base_offset;
}

static inline hwaddr ec_get_mii_base(PCIBaseState *s)
{
    return ec_get_ec_base(s) + s->mii_rel_offset;
}

static inline hwaddr ec_get_fifo_base(PCIBaseState *s)
{
    return ec_get_ec_base(s) + s->fifo_rel_offset;
}

static inline hwaddr ec_get_mac_base(PCIBaseState *s)
{
    return ec_get_ec_base(s) + s->mac_rel_offset;
}

/* Polling timer callback: model RX completion and TX desc "sent" flag
 * We only update MAC counters and ignore actual packet contents.
 */
static void ec_bhf_poll_cb(void *opaque)
{
    PCIBaseState *s = opaque;

    if (!s->net_up) {
        return;
    }

    /* For each TX notification, increment TX frame counter once. */
    if (s->fifo_tx_reg != 0) {
        s->mac_tx_frame_cnt++;
        /* In hardware, TX descriptor header.sent would be set by DMA engine.
         * We do not touch guest memory, but the driver only checks the bit
         * in its own descriptor, which it initialised to TX_HDR_SENT, so
         * TX queue wake logic will work without additional modelling.
         */
    }

    /* For RX, we do nothing unless guest writes FIFO_RX_REG, but
     * driver only re-adds descriptors; completion is driven by
     * device setting header.recv in DMA memory, which we do not
     * model here. Thus no RX traffic.
     */

    /* reschedule */
    timer_mod_ns(s->poll_timer,
                 qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + TIMER_INTERVAL_NSEC);
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* BAR0 layout according to driver ec_bhf_setup_offsets():
     * - INFO block area at base + i*INFO_BLOCK_SIZE
     * - Each block has at offsets: TYPE (16-bit), REV (16-bit), BLK_CNT (8-bit),
     *   TX_CHAN (8-bit), RX_CHAN (8-bit), OFFSET (32-bit)
     * - For the selected EtherCAT master block (type == ETHERCAT_MASTER_ID),
     *   OFFSET points to ec_io.
     * - At ec_io + EC_MII_OFFSET/EC_FIFO_OFFSET/EC_MAC_OFFSET are 32-bit
     *   offsets to respective sub-blocks.
     */

    /* emulate INFO blocks */
    if (addr == INFO_BLOCK_BLK_CNT && size == 1) {
        /* Only one info block is needed by driver */
        val = 1;
        return val;
    }

    /* Decode which info block and field */
    if (addr < INFO_BLOCK_SIZE) {
        hwaddr off = addr;
        if (off == INFO_BLOCK_TYPE) {
            if (size == 2) {
                val = ETHERCAT_MASTER_ID;
            } else if (size == 1) {
                val = ETHERCAT_MASTER_ID & 0xff;
            }
            return val;
        } else if (off == INFO_BLOCK_REV) {
            /* arbitrary revision 0 */
            if (size == 2) {
                val = 0;
            } else if (size == 1) {
                val = 0;
            }
            return val;
        } else if (off == INFO_BLOCK_TX_CHAN) {
            /* tx dma channel */
            val = 0; /* use channel 0 */
            if (size == 1) {
                return val;
            }
        } else if (off == INFO_BLOCK_RX_CHAN) {
            val = 1; /* use channel 1 */
            if (size == 1) {
                return val;
            }
        } else if (off == INFO_BLOCK_OFFSET) {
            /* 32-bit offset to ec_io within BAR0 */
            if (size == 4) {
                val = s->ec_base_offset;
                return val;
            }
        }
    }

    /* ec_io level offsets: ec_io + EC_MII_OFFSET etc. */
    if (addr == ec_get_ec_base(s) + EC_MII_OFFSET && size == 4) {
        val = s->mii_rel_offset;
        return val;
    }
    if (addr == ec_get_ec_base(s) + EC_FIFO_OFFSET && size == 4) {
        val = s->fifo_rel_offset;
        return val;
    }
    if (addr == ec_get_ec_base(s) + EC_MAC_OFFSET && size == 4) {
        val = s->mac_rel_offset;
        return val;
    }

    /* MII block */
    if (addr >= ec_get_mii_base(s) && addr < ec_get_mii_base(s) + 0x20) {
        hwaddr off = addr - ec_get_mii_base(s);
        if (off >= MII_MAC_ADDR && off < MII_MAC_ADDR + 6 && size == 1) {
            val = s->mac_addr[off - MII_MAC_ADDR];
            return val;
        }
        if (off == MII_MAC_FILT_FLAG && size == 1) {
            val = s->mac_mac_filt_flag;
            return val;
        }
        if (off == MII_LINK_STATUS && size == 1) {
            val = s->mii_link_status;
            return val;
        }
    }

    /* FIFO block */
    if (addr >= ec_get_fifo_base(s) && addr < ec_get_fifo_base(s) + 0x20) {
        hwaddr off = addr - ec_get_fifo_base(s);
        if (off == FIFO_TX_REG && size == 4) {
            val = s->fifo_tx_reg;
            return val;
        }
        if (off == FIFO_TX_RESET && size == 1) {
            val = s->fifo_tx_reset;
            return val;
        }
        if (off == FIFO_RX_REG && size == 4) {
            val = s->fifo_rx_reg;
            return val;
        }
        if (off == FIFO_RX_RESET && size == 1) {
            val = s->fifo_rx_reset;
            return val;
        }
    }

    /* MAC block */
    if (addr >= ec_get_mac_base(s) && addr < ec_get_mac_base(s) + 0x80) {
        hwaddr off = addr - ec_get_mac_base(s);
        switch (off) {
        case MAC_FRAME_ERR_CNT:
            if (size == 1) {
                val = s->mac_frame_err_cnt;
                return val;
            }
            break;
        case MAC_RX_ERR_CNT:
            if (size == 1) {
                val = s->mac_rx_err_cnt;
                return val;
            }
            break;
        case MAC_CRC_ERR_CNT:
            if (size == 1) {
                val = s->mac_crc_err_cnt;
                return val;
            }
            break;
        case MAC_LNK_LST_ERR_CNT:
            if (size == 1) {
                val = s->mac_lnk_lst_err_cnt;
                return val;
            }
            break;
        case MAC_TX_FRAME_CNT:
            if (size == 4) {
                val = s->mac_tx_frame_cnt;
                return val;
            }
            break;
        case MAC_RX_FRAME_CNT:
            if (size == 4) {
                val = s->mac_rx_frame_cnt;
                return val;
            }
            break;
        case MAC_TX_FIFO_LVL:
            if (size == 1) {
                val = s->mac_tx_fifo_lvl;
                return val;
            }
            break;
        case MAC_DROPPED_FRMS:
            if (size == 1) {
                val = s->mac_dropped_frms;
                return val;
            }
            break;
        case MAC_CONNECTED_CCAT_FLAG:
            if (size == 1) {
                val = s->mac_connected_ccat_flag;
                return val;
            }
            break;
        default:
            break;
        }
    }

    /* fallback */
    qemu_log_mask(LOG_UNIMP, "[%s] mmio_read addr=%" PRIx64 " size=%u\n",
                  TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* INFO block is read-only for driver; ignore writes */

    /* ec_io sub-offset writes not used by driver */

    /* MII block writes */
    if (addr >= ec_get_mii_base(s) && addr < ec_get_mii_base(s) + 0x20) {
        hwaddr off = addr - ec_get_mii_base(s);
        if (off == MII_MAC_FILT_FLAG && size == 1) {
            s->mac_mac_filt_flag = (uint8_t)val;
            return;
        }
        /* driver does not write MAC_ADDR or LINK_STATUS */
    }

    /* FIFO block writes */
    if (addr >= ec_get_fifo_base(s) && addr < ec_get_fifo_base(s) + 0x20) {
        hwaddr off = addr - ec_get_fifo_base(s);
        if (off == FIFO_TX_REG && size == 4) {
            s->fifo_tx_reg = (uint32_t)val;
            /* new TX notified; we increment counters in poll timer */
            return;
        }
        if (off == FIFO_TX_RESET && size == 1) {
            s->fifo_tx_reset = (uint8_t)val;
            /* reset logic: nothing else modeled */
            return;
        }
        if (off == FIFO_RX_REG && size == 4) {
            s->fifo_rx_reg = (uint32_t)val;
            /* driver writes descriptor address; no further modelling */
            return;
        }
        if (off == FIFO_RX_RESET && size == 1) {
            s->fifo_rx_reset = (uint8_t)val;
            return;
        }
    }

    /* MAC block writes: ec_bhf_reset zeroes various counters and tx fifo level */
    if (addr >= ec_get_mac_base(s) && addr < ec_get_mac_base(s) + 0x80) {
        hwaddr off = addr - ec_get_mac_base(s);
        switch (off) {
        case MAC_FRAME_ERR_CNT:
            if (size == 1) {
                s->mac_frame_err_cnt = (uint8_t)val;
                return;
            }
            break;
        case MAC_RX_ERR_CNT:
            if (size == 1) {
                s->mac_rx_err_cnt = (uint8_t)val;
                return;
            }
            break;
        case MAC_CRC_ERR_CNT:
            if (size == 1) {
                s->mac_crc_err_cnt = (uint8_t)val;
                return;
            }
            break;
        case MAC_LNK_LST_ERR_CNT:
            if (size == 1) {
                s->mac_lnk_lst_err_cnt = (uint8_t)val;
                return;
            }
            break;
        case MAC_TX_FRAME_CNT:
            if (size == 4) {
                s->mac_tx_frame_cnt = (uint32_t)val;
                return;
            }
            break;
        case MAC_RX_FRAME_CNT:
            if (size == 4) {
                s->mac_rx_frame_cnt = (uint32_t)val;
                return;
            }
            break;
        case MAC_TX_FIFO_LVL:
            if (size == 1) {
                s->mac_tx_fifo_lvl = (uint8_t)val;
                return;
            }
            break;
        case MAC_DROPPED_FRMS:
            if (size == 1) {
                s->mac_dropped_frms = (uint8_t)val;
                return;
            }
            break;
        default:
            break;
        }
    }

    qemu_log_mask(LOG_UNIMP, "[%s] mmio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n",
                  TYPE_PCIBASE_DEVICE, (uint64_t)addr, (uint64_t)val, size);
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "[%s] pio_read addr=%" PRIx64 " size=%u\n",
                  TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "[%s] pio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n",
                  TYPE_PCIBASE_DEVICE, (uint64_t)addr, (uint64_t)val, size);
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    pci_device_reset(pdev);

    if (s->mmio_backing && s->mmio_backing_size) {
        memset(s->mmio_backing, 0, s->mmio_backing_size);
    }

    /* Reset device-specific state to defaults consistent with driver expectations */
    s->mac_frame_err_cnt = 0;
    s->mac_rx_err_cnt = 0;
    s->mac_crc_err_cnt = 0;
    s->mac_lnk_lst_err_cnt = 0;
    s->mac_tx_frame_cnt = 0;
    s->mac_rx_frame_cnt = 0;
    s->mac_dropped_frms = 0;
    s->mac_tx_fifo_lvl = 0;
    s->mac_connected_ccat_flag = 1; /* arbitrary non-zero, not read by driver */

    s->fifo_tx_reg = 0;
    s->fifo_tx_reset = 0;
    s->fifo_rx_reg = 0;
    s->fifo_rx_reset = 0;

    s->dma_chan_reg[0] = 0xffffffff;
    s->dma_chan_reg[1] = 0xffffffff;
    s->dma_chan_reg_aux[0] = 0;
    s->dma_chan_reg_aux[1] = 0;

    s->net_up = false;

    if (s->poll_timer) {
        timer_del(s->poll_timer);
    }
}

static void pcibase_dma_device_realize(PCIDevice *pdev, Error **errp)
{
    /* DMA is only indirectly observed via register fields used to compute
     * window size. We implement only those MMIO registers and do not
     * perform actual DMA transfers, since descriptor format is not
     * described in the driver.
     */
    (void)pdev;
    (void)errp;
}

static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    uint32_t val = pci_default_read_config(pdev, addr, len);

    switch (len) {
    case 1:
        val &= 0xFF;
        break;
    case 2:
        val &= 0xFFFF;
        break;
    case 4:
    default:
        break;
    }
    return val;
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    if (addr >= PCI_BASE_ADDRESS_0 && addr <= PCI_BASE_ADDRESS_5) {
        pci_default_write_config(pdev, addr, val, len);
        return;
    }

    pci_default_write_config(pdev, addr, val, len);
}

static uint64_t ec_dma_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Channel 0 and 1 registers, spaced DMA_CHAN_SIZE apart, starting at DMA_CHAN_OFFSET */
    if (addr >= DMA_CHAN_OFFSET && addr < DMA_CHAN_OFFSET + 2 * DMA_CHAN_SIZE) {
        unsigned chan = (addr - DMA_CHAN_OFFSET) / DMA_CHAN_SIZE;
        hwaddr off = (addr - DMA_CHAN_OFFSET) % DMA_CHAN_SIZE;
        if (chan < 2) {
            if (off == 0 && size == 4) {
                val = s->dma_chan_reg[chan];
                return val;
            } else if (off == 4 && size == 4) {
                val = s->dma_chan_reg_aux[chan];
                return val;
            }
        }
    }

    qemu_log_mask(LOG_UNIMP, "[%s] dma_mmio_read addr=%" PRIx64 " size=%u\n",
                  TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
    return val;
}

static void ec_dma_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= DMA_CHAN_OFFSET && addr < DMA_CHAN_OFFSET + 2 * DMA_CHAN_SIZE) {
        unsigned chan = (addr - DMA_CHAN_OFFSET) / DMA_CHAN_SIZE;
        hwaddr off = (addr - DMA_CHAN_OFFSET) % DMA_CHAN_SIZE;
        if (chan < 2) {
            if (off == 0 && size == 4) {
                s->dma_chan_reg[chan] = (uint32_t)val;
                return;
            } else if (off == 4 && size == 4) {
                s->dma_chan_reg_aux[chan] = (uint32_t)val;
                return;
            }
        }
    }

    qemu_log_mask(LOG_UNIMP, "[%s] dma_mmio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n",
                  TYPE_PCIBASE_DEVICE, (uint64_t)addr, (uint64_t)val, size);
}

static const MemoryRegionOps ec_dma_mmio_ops = {
    .read = ec_dma_mmio_read,
    .write = ec_dma_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, EC_BHF_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, EC_BHF_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_ETHERNET);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* BAR layout chosen to satisfy driver:
     * - BAR0: MMIO space for INFO blocks + EC/MII/FIFO/MAC
     * - BAR2: MMIO space for DMA registers at DMA_CHAN_OFFSET
     */
    s->num_bars = 0;

    s->bar_info[s->num_bars].index = 0;
    s->bar_info[s->num_bars].type = BAR_TYPE_MMIO;
    s->bar_info[s->num_bars].size = 0x2000;
    s->bar_info[s->num_bars].name = "ec_bhf-bar0";
    s->bar_info[s->num_bars].sparse = false;
    s->num_bars++;

    s->bar_info[s->num_bars].index = 2;
    s->bar_info[s->num_bars].type = BAR_TYPE_MMIO;
    s->bar_info[s->num_bars].size = 0x2000;
    s->bar_info[s->num_bars].name = "ec_bhf-bar2";
    s->bar_info[s->num_bars].sparse = false;
    s->num_bars++;

    for (int i = 0; i < s->num_bars; i++) {
        if (s->bar_info[i].index == 2) {
            /* For BAR2 we use dedicated DMA ops */
            MemoryRegion *mr = &s->bar_regions[s->bar_info[i].index];
            memory_region_init_io(mr, OBJECT(s), &ec_dma_mmio_ops, s,
                                  s->bar_info[i].name, s->bar_info[i].size);
            pci_register_bar(pdev, s->bar_info[i].index,
                             PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
        } else {
            pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
        }
    }

    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) {
        return;
    }

    /* Initialise sub-block offsets within BAR0 */
    s->ec_base_offset = 0x100;   /* arbitrary but consistent within BAR0 */
    s->mii_rel_offset = 0x200;
    s->fifo_rel_offset = 0x300;
    s->mac_rel_offset = 0x400;

    /* Default MAC address (will be read by driver) */
    s->mac_addr[0] = 0x02; /* locally administered */
    s->mac_addr[1] = 0x00;
    s->mac_addr[2] = 0x00;
    s->mac_addr[3] = 0x00;
    s->mac_addr[4] = 0x00;
    s->mac_addr[5] = 0x01;

    s->mii_link_status = 1; /* link up */

    s->mac_frame_err_cnt = 0;
    s->mac_rx_err_cnt = 0;
    s->mac_crc_err_cnt = 0;
    s->mac_lnk_lst_err_cnt = 0;
    s->mac_tx_frame_cnt = 0;
    s->mac_rx_frame_cnt = 0;
    s->mac_dropped_frms = 0;
    s->mac_tx_fifo_lvl = 0;
    s->mac_connected_ccat_flag = 1;
    s->mac_mac_filt_flag = 0;

    s->fifo_tx_reg = 0;
    s->fifo_rx_reg = 0;
    s->fifo_tx_reset = 0;
    s->fifo_rx_reset = 0;

    s->dma_chan_reg[0] = 0xffffffff;
    s->dma_chan_reg[1] = 0xffffffff;
    s->dma_chan_reg_aux[0] = 0;
    s->dma_chan_reg_aux[1] = 0;

    s->poll_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, ec_bhf_poll_cb, s);
    s->net_up = true;
    timer_mod_ns(s->poll_timer,
                 qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + TIMER_INTERVAL_NSEC);

    qemu_log_mask(LOG_UNIMP, "[%s] device realized\n", TYPE_PCIBASE_DEVICE);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (s->has_msix) {
        msix_uninit(pdev, NULL, 0);
        s->has_msix = false;
    }
    if (s->has_msi) {
        msi_uninit(pdev);
        s->has_msi = false;
    }

    if (s->poll_timer) {
        timer_del(s->poll_timer);
        timer_free(s->poll_timer);
        s->poll_timer = NULL;
    }

    if (s->mmio_backing) {
        g_free(s->mmio_backing);
        s->mmio_backing = NULL;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] device uninit\n", TYPE_PCIBASE_DEVICE);
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

