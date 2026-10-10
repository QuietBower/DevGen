/*
 * QEMU 8139too-compatible PCI Ethernet device (minimal, driver-driven)
 *
 * Target: QEMU 8.2.10
 *
 * This model only implements behavior that is explicitly observable in the
 * Linux 8139too driver. It is intentionally minimal and only sufficient for
 * the driver to probe, program registers, and perform basic TX/RX using the
 * driver's software ring buffers.
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

#define TYPE_PCIBASE_DEVICE "8139too_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

typedef uint32_t u32;
typedef uint16_t u16;

#define __NETIF_MSG_BIT(bit) ((u32)1 << (bit))
#define __USER_HZ 100
#define __NETIF_MSG(name) __NETIF_MSG_BIT(NETIF_MSG_ ## name ## _BIT)
#define HZ __USER_HZ
#define NETIF_MSG_DRV        __NETIF_MSG(DRV)
#define NETIF_MSG_PROBE      __NETIF_MSG(PROBE)
#define NETIF_MSG_LINK       __NETIF_MSG(LINK)
#define VLAN_ETH_HLEN    18
#define ETH_FCS_LEN  4
#define PCI_CLASS_NETWORK_ETHERNET  0x0200
#define DRV_NAME    "8139too"
#define DRV_VERSION "0.9.28"
#define MAX_UNITS 8
#define TX_TIMEOUT  (6*HZ)
#define EE_SHIFT_CLK    0x04
#define EE_CS           0x08
#define EE_DATA_WRITE   0x02
#define EE_DATA_READ    0x01
#define EE_ENB          (0x80 | EE_CS)
#define EE_WRITE_CMD    (5)
#define EE_READ_CMD     (6)
#define EE_ERASE_CMD    (7)
#define MDIO_DATA_OUT   0x04
#define MDIO_WRITE0 (MDIO_DIR)
#define MDIO_WRITE1 (MDIO_DIR | MDIO_DATA_OUT)
#define RX_FIFO_THRESH  7
#define RX_DMA_BURST    7
#define TX_DMA_BURST    6
#define EE_WRITE_0      0x00
#define EE_WRITE_1      0x02
#define NUM_TX_DESC 4
#define MAX_ETH_FRAME_SIZE  1792
#define TX_BUF_SIZE MAX_ETH_FRAME_SIZE
#define TX_BUF_TOT_LEN (TX_BUF_SIZE * NUM_TX_DESC)
#define RX_BUF_IDX  2
#define RX_BUF_LEN  (8192 << RX_BUF_IDX)
#define RX_BUF_PAD  16
#define RX_BUF_WRAP_PAD 2048
#define RX_BUF_TOT_LEN (RX_BUF_LEN + RX_BUF_PAD + RX_BUF_WRAP_PAD)
#define MAX_ETH_DATA_SIZE (MAX_ETH_FRAME_SIZE - VLAN_ETH_HLEN - ETH_FCS_LEN)
#define TX_FIFO_THRESH 256
#define TX_RETRY    8
#define RTL_NUM_STATS 4
#define RTL_REGS_VER 1
#define RTL_MIN_IO_SIZE 0x80
#define RTL8139B_IO_SIZE 256
#define RTL8129_CAPS  HAS_MII_XCVR
#define RTL8139_CAPS  (HAS_CHIP_XCVR|HAS_LNK_CHNG)
#define HW_REVID(b30, b29, b28, b27, b26, b23, b22) \
    (b30<<30 | b29<<29 | b28<<28 | b27<<27 | b26<<26 | b23<<23 | b22<<22)
#define HW_REVID_MASK  HW_REVID(1, 1, 1, 1, 1, 1, 1)
#define RTL8139_DRIVER_NAME   DRV_NAME " Fast Ethernet driver " DRV_VERSION
#define RTL8139_DEF_MSG_ENABLE   (NETIF_MSG_DRV   | \
                                 NETIF_MSG_PROBE  | \
                                 NETIF_MSG_LINK)
#define RTL8139_DEBUG 0

enum rx_mode_bits {
    AcceptErr   = 0x20,
    AcceptRunt  = 0x10,
    AcceptBroadcast = 0x08,
    AcceptMulticast = 0x04,
    AcceptMyPhys    = 0x02,
    AcceptAllPhys   = 0x01,
};

enum IntrStatusBits {
    PCIErr      = 0x8000,
    PCSTimeout  = 0x4000,
    RxFIFOOver  = 0x40,
    RxUnderrun  = 0x20,
    RxOverflow  = 0x10,
    TxErr       = 0x08,
    TxOK        = 0x04,
    RxErr       = 0x02,
    RxOK        = 0x01,

    RxAckBits   = RxFIFOOver | RxOverflow | RxOK,
};

enum TxStatusBits {
    TxHostOwns  = 0x2000,
    TxUnderrun  = 0x4000,
    TxStatOK    = 0x8000,
    TxOutOfWindow   = 0x20000000,
    TxAborted   = 0x40000000,
    TxCarrierLost   = 0x80000000,
};

enum RxStatusBits {
    RxMulticast = 0x8000,
    RxPhysical  = 0x4000,
    RxBroadcast = 0x2000,
    RxBadSymbol = 0x0020,
    RxRunt      = 0x0010,
    RxTooLong   = 0x0008,
    RxCRCErr    = 0x0004,
    RxBadAlign  = 0x0002,
    RxStatusOK  = 0x0001,
};

enum RxConfigBits {
    RxCfgFIFOShift  = 13,
    RxCfgFIFONone   = (7 << RxCfgFIFOShift),

    RxCfgDMAShift   = 8,
    RxCfgDMAUnlimited = (7 << RxCfgDMAShift),

    RxCfgRcv8K  = 0,
    RxCfgRcv16K = (1 << 11),
    RxCfgRcv32K = (1 << 12),
    RxCfgRcv64K = (1 << 11) | (1 << 12),

    RxNoWrap    = (1 << 7),
};

enum Config1Bits {
    Cfg1_PM_Enable  = 0x01,
    Cfg1_VPD_Enable = 0x02,
    Cfg1_PIO    = 0x04,
    Cfg1_MMIO   = 0x08,
    LWAKE       = 0x10,
    Cfg1_Driver_Load = 0x20,
    Cfg1_LED0   = 0x40,
    Cfg1_LED1   = 0x80,
    SLEEP       = (1 << 1),
    PWRDN       = (1 << 0),
};

enum RTL8139_registers {
    MAC0        = 0,
    MAR0        = 8,
    TxStatus0   = 0x10,
    TxAddr0     = 0x20,
    RxBuf       = 0x30,
    ChipCmd     = 0x37,
    RxBufPtr    = 0x38,
    RxBufAddr   = 0x3A,
    IntrMask    = 0x3C,
    IntrStatus  = 0x3E,
    TxConfig    = 0x40,
    RxConfig    = 0x44,
    Timer       = 0x48,
    RxMissed    = 0x4C,
    Cfg9346     = 0x50,
    Config0     = 0x51,
    Config1     = 0x52,
    TimerInt    = 0x54,
    MediaStatus = 0x58,
    Config3     = 0x59,
    Config4     = 0x5A,
    HltClk      = 0x5B,
    MultiIntr   = 0x5C,
    TxSummary   = 0x60,
    BasicModeCtrl   = 0x62,
    BasicModeStatus = 0x64,
    NWayAdvert  = 0x66,
    NWayLPAR    = 0x68,
    NWayExpansion   = 0x6A,
    FIFOTMS     = 0x70,
    CSCR        = 0x74,
    PARA78      = 0x78,
    FlashReg    = 0xD4,
    PARA7c      = 0x7c,
    Config5     = 0xD8,
};

enum ClearBitMasks {
    MultiIntrClear  = 0xF000,
    ChipCmdClear    = 0xE2,
    Config1Clear    = (1<<7)|(1<<6)|(1<<3)|(1<<2)|(1<<1),
};

enum ChipCmdBits {
    CmdReset    = 0x10,
    CmdRxEnb    = 0x08,
    CmdTxEnb    = 0x04,
    RxBufEmpty  = 0x01,
};

enum tx_config_bits {
    TxIFGShift  = 24,
    TxIFG84     = (0 << TxIFGShift),
    TxIFG88     = (1 << TxIFGShift),
    TxIFG92     = (2 << TxIFGShift),
    TxIFG96     = (3 << TxIFGShift),

    TxLoopBack  = (1 << 18) | (1 << 17),
    TxCRC       = (1 << 16),
    TxClearAbt  = (1 << 0),
    TxDMAShift  = 8,
    TxRetryShift    = 4,

    TxVersionMask   = 0x7C800000,
};

enum Config3Bits {
    Cfg3_FBtBEn     = (1 << 0),
    Cfg3_FuncRegEn  = (1 << 1),
    Cfg3_CLKRUN_En  = (1 << 2),
    Cfg3_CardB_En   = (1 << 3),
    Cfg3_LinkUp     = (1 << 4),
    Cfg3_Magic      = (1 << 5),
    Cfg3_PARM_En    = (1 << 6),
    Cfg3_GNTSel     = (1 << 7),
};

enum Config4Bits {
    LWPTN   = (1 << 2),
};

enum Config5Bits {
    Cfg5_PME_STS    = (1 << 0),
    Cfg5_LANWake    = (1 << 1),
    Cfg5_LDPS       = (1 << 2),
    Cfg5_FIFOAddrPtr= (1 << 3),
    Cfg5_UWF       = (1 << 4),
    Cfg5_MWF       = (1 << 5),
    Cfg5_BWF       = (1 << 6),
};

enum CSCRBits {
    CSCR_LinkOKBit      = 0x0400,
    CSCR_LinkChangeBit  = 0x0800,
    CSCR_LinkStatusBits = 0x0f000,
    CSCR_LinkDownOffCmd = 0x003c0,
    CSCR_LinkDownCmd    = 0x0f3c0,
};

enum Cfg9346Bits {
    Cfg9346_Lock    = 0x00,
    Cfg9346_Unlock  = 0xC0,
};

enum chip_flags {
    HasHltClk   = (1 << 0),
    HasLWake    = (1 << 1),
};

enum TwisterParamVals {
    PARA78_default  = 0x78fa8388,
    PARA7c_default  = 0xcb38de43,
    PARA7c_xxx  = 0xcb38de43,
};

static const unsigned int rtl8139_rx_config =
    RxCfgRcv32K | RxNoWrap |
    (RX_FIFO_THRESH << RxCfgFIFOShift) |
    (RX_DMA_BURST << RxCfgDMAShift);

static const unsigned int rtl8139_tx_config =
    TxIFG96 | (TX_DMA_BURST << TxDMAShift) | (TX_RETRY << TxRetryShift);

#define RTL8139_VENDOR_ID 0x10ec
#define RTL8139_DEVICE_ID 0x8139
#define RTL8139_CLASS_ID  PCI_CLASS_NETWORK_ETHERNET

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

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];

    uint8_t *mmio_backing;
    size_t mmio_backing_size;

    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;

    /* shadow registers relevant to driver */
    uint8_t mac[6];
    uint32_t mar[2];

    uint32_t tx_addr[NUM_TX_DESC];
    uint32_t tx_status[NUM_TX_DESC];
    uint32_t tx_config;
    uint32_t rx_config_reg;

    uint32_t rx_buf_dma;
    uint16_t rx_buf_ptr;
    uint16_t rx_buf_addr;

    uint8_t chip_cmd;
    uint16_t intr_status;
    uint16_t intr_mask;

    uint32_t rx_missed;

    uint8_t cfg9346;
    uint8_t config0;
    uint8_t config1;
    uint8_t config3;
    uint8_t config4;
    uint8_t config5;
    uint8_t hltclk;

    uint8_t media_status;

    uint16_t multi_intr;

    uint16_t basic_mode_ctrl;
    uint16_t basic_mode_status;
    uint16_t nway_advert;
    uint16_t nway_lpar;
    uint16_t nway_expansion;

    uint16_t cscr;
    uint32_t para78;
    uint32_t para7c;

    uint8_t flash[4];

    /* RX ring buffer emulation */
    uint8_t *rx_ring_host;
    hwaddr rx_ring_dma; /* programmed into RxBuf */

    /* simple timer to fake IRQs if needed (not heavily used) */
    QEMUTimer timer;

    qemu_irq irq;
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
    .valid = { .min_access_size = 1, .max_access_size = 4 },
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

/* Removed unused static inline rtl8139_raise_irq to avoid compiler warning */

static inline void rtl8139_lower_irq_if_clear(PCIBaseState *s)
{
    if (!(s->intr_status & s->intr_mask)) {
        pci_set_irq(PCI_DEVICE(s), 0);
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = 0;

    switch (addr) {
    case MAC0 + 0:
    case MAC0 + 1:
    case MAC0 + 2:
    case MAC0 + 3:
    case MAC0 + 4:
    case MAC0 + 5:
        val = s->mac[addr - MAC0];
        break;
    case MAR0 + 0:
    case MAR0 + 1:
    case MAR0 + 2:
    case MAR0 + 3:
        val = (s->mar[0] >> ((addr - MAR0) * 8)) & 0xff;
        break;
    case MAR0 + 4:
    case MAR0 + 5:
    case MAR0 + 6:
    case MAR0 + 7:
        val = (s->mar[1] >> ((addr - (MAR0 + 4)) * 8)) & 0xff;
        break;
    case TxStatus0 + 0:
    case TxStatus0 + 4:
    case TxStatus0 + 8:
    case TxStatus0 + 12: {
        int idx = (addr - TxStatus0) / 4;
        val = s->tx_status[idx];
        break;
    }
    case TxAddr0 + 0:
    case TxAddr0 + 4:
    case TxAddr0 + 8:
    case TxAddr0 + 12: {
        int idx = (addr - TxAddr0) / 4;
        val = s->tx_addr[idx];
        break;
    }
    case RxBuf:
        val = s->rx_buf_dma;
        break;
    case ChipCmd:
        val = s->chip_cmd;
        break;
    case RxBufPtr:
        val = s->rx_buf_ptr & 0xff;
        break;
    case RxBufPtr + 1:
        val = (s->rx_buf_ptr >> 8) & 0xff;
        break;
    case RxBufAddr:
        val = s->rx_buf_addr & 0xff;
        break;
    case RxBufAddr + 1:
        val = (s->rx_buf_addr >> 8) & 0xff;
        break;
    case IntrMask:
        val = s->intr_mask & 0xff;
        break;
    case IntrMask + 1:
        val = (s->intr_mask >> 8) & 0xff;
        break;
    case IntrStatus:
        val = s->intr_status & 0xff;
        break;
    case IntrStatus + 1:
        val = (s->intr_status >> 8) & 0xff;
        break;
    case TxConfig + 0:
    case TxConfig + 1:
    case TxConfig + 2:
    case TxConfig + 3:
        val = (s->tx_config >> ((addr - TxConfig) * 8)) & 0xff;
        break;
    case RxConfig + 0:
    case RxConfig + 1:
    case RxConfig + 2:
    case RxConfig + 3:
        val = (s->rx_config_reg >> ((addr - RxConfig) * 8)) & 0xff;
        break;
    case RxMissed + 0:
    case RxMissed + 1:
    case RxMissed + 2:
    case RxMissed + 3:
        val = (s->rx_missed >> ((addr - RxMissed) * 8)) & 0xff;
        break;
    case Cfg9346:
        val = s->cfg9346;
        break;
    case Config0:
        val = s->config0;
        break;
    case Config1:
        val = s->config1;
        break;
    case MediaStatus:
        val = s->media_status;
        break;
    case Config3:
        val = s->config3;
        break;
    case Config4:
        val = s->config4;
        break;
    case HltClk:
        val = s->hltclk;
        break;
    case MultiIntr:
        val = s->multi_intr & 0xff;
        break;
    case MultiIntr + 1:
        val = (s->multi_intr >> 8) & 0xff;
        break;
    case BasicModeCtrl:
        val = s->basic_mode_ctrl & 0xff;
        break;
    case BasicModeCtrl + 1:
        val = (s->basic_mode_ctrl >> 8) & 0xff;
        break;
    case BasicModeStatus:
        val = s->basic_mode_status & 0xff;
        break;
    case BasicModeStatus + 1:
        val = (s->basic_mode_status >> 8) & 0xff;
        break;
    case NWayAdvert:
        val = s->nway_advert & 0xff;
        break;
    case NWayAdvert + 1:
        val = (s->nway_advert >> 8) & 0xff;
        break;
    case NWayLPAR:
        val = s->nway_lpar & 0xff;
        break;
    case NWayLPAR + 1:
        val = (s->nway_lpar >> 8) & 0xff;
        break;
    case NWayExpansion:
        val = s->nway_expansion & 0xff;
        break;
    case NWayExpansion + 1:
        val = (s->nway_expansion >> 8) & 0xff;
        break;
    case FIFOTMS + 0:
    case FIFOTMS + 1:
    case FIFOTMS + 2:
    case FIFOTMS + 3:
        val = (0 >> ((addr - FIFOTMS) * 8)) & 0xff;
        break;
    case CSCR + 0:
        val = s->cscr & 0xff;
        break;
    case CSCR + 1:
        val = (s->cscr >> 8) & 0xff;
        break;
    case PARA78 + 0:
    case PARA78 + 1:
    case PARA78 + 2:
    case PARA78 + 3:
        val = (s->para78 >> ((addr - PARA78) * 8)) & 0xff;
        break;
    case PARA7c + 0:
    case PARA7c + 1:
    case PARA7c + 2:
    case PARA7c + 3:
        val = (s->para7c >> ((addr - PARA7c) * 8)) & 0xff;
        break;
    case FlashReg + 0:
    case FlashReg + 1:
    case FlashReg + 2:
    case FlashReg + 3:
        val = s->flash[addr - FlashReg];
        break;
    case Config5:
        val = s->config5;
        break;
    default:
        val = 0;
        break;
    }

    if (size == 2) {
        uint16_t v16 = 0;
        v16 |= pcibase_mmio_read(opaque, addr + 0, 1);
        v16 |= pcibase_mmio_read(opaque, addr + 1, 1) << 8;
        return v16;
    } else if (size == 4) {
        uint32_t v32 = 0;
        v32 |= pcibase_mmio_read(opaque, addr + 0, 1);
        v32 |= pcibase_mmio_read(opaque, addr + 1, 1) << 8;
        v32 |= pcibase_mmio_read(opaque, addr + 2, 1) << 16;
        v32 |= pcibase_mmio_read(opaque, addr + 3, 1) << 24;
        return v32;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size == 2) {
        pcibase_mmio_write(opaque, addr + 0, val & 0xff, 1);
        pcibase_mmio_write(opaque, addr + 1, (val >> 8) & 0xff, 1);
        return;
    } else if (size == 4) {
        pcibase_mmio_write(opaque, addr + 0, val & 0xff, 1);
        pcibase_mmio_write(opaque, addr + 1, (val >> 8) & 0xff, 1);
        pcibase_mmio_write(opaque, addr + 2, (val >> 16) & 0xff, 1);
        pcibase_mmio_write(opaque, addr + 3, (val >> 24) & 0xff, 1);
        return;
    }

    switch (addr) {
    case MAC0 + 0:
    case MAC0 + 1:
    case MAC0 + 2:
    case MAC0 + 3:
    case MAC0 + 4:
    case MAC0 + 5:
        s->mac[addr - MAC0] = (uint8_t)val;
        break;
    case MAR0 + 0:
    case MAR0 + 1:
    case MAR0 + 2:
    case MAR0 + 3: {
        int shift = (addr - MAR0) * 8;
        s->mar[0] &= ~(0xffu << shift);
        s->mar[0] |= ((uint32_t)val & 0xffu) << shift;
        break;
    }
    case MAR0 + 4:
    case MAR0 + 5:
    case MAR0 + 6:
    case MAR0 + 7: {
        int shift = (addr - (MAR0 + 4)) * 8;
        s->mar[1] &= ~(0xffu << shift);
        s->mar[1] |= ((uint32_t)val & 0xffu) << shift;
        break;
    }
    case TxStatus0 + 0:
    case TxStatus0 + 4:
    case TxStatus0 + 8:
    case TxStatus0 + 12: {
        int idx = (addr - TxStatus0) / 4;
        s->tx_status[idx] &= ~0xffu;
        s->tx_status[idx] |= (uint8_t)val;
        break;
    }
    case TxStatus0 + 1:
    case TxStatus0 + 5:
    case TxStatus0 + 9:
    case TxStatus0 + 13: {
        int idx = (addr - TxStatus0 - 1) / 4;
        s->tx_status[idx] &= ~(0xffu << 8);
        s->tx_status[idx] |= ((uint8_t)val) << 8;
        break;
    }
    case TxStatus0 + 2:
    case TxStatus0 + 6:
    case TxStatus0 + 10:
    case TxStatus0 + 14: {
        int idx = (addr - TxStatus0 - 2) / 4;
        s->tx_status[idx] &= ~(0xffu << 16);
        s->tx_status[idx] |= ((uint8_t)val) << 16;
        break;
    }
    case TxStatus0 + 3:
    case TxStatus0 + 7:
    case TxStatus0 + 11:
    case TxStatus0 + 15: {
        int idx = (addr - TxStatus0 - 3) / 4;
        s->tx_status[idx] &= ~(0xffu << 24);
        s->tx_status[idx] |= ((uint8_t)val) << 24;
        break;
    }
    case TxAddr0 + 0:
    case TxAddr0 + 4:
    case TxAddr0 + 8:
    case TxAddr0 + 12: {
        int idx = (addr - TxAddr0) / 4;
        s->tx_addr[idx] &= ~0xffu;
        s->tx_addr[idx] |= (uint8_t)val;
        break;
    }
    case TxAddr0 + 1:
    case TxAddr0 + 5:
    case TxAddr0 + 9:
    case TxAddr0 + 13: {
        int idx = (addr - TxAddr0 - 1) / 4;
        s->tx_addr[idx] &= ~(0xffu << 8);
        s->tx_addr[idx] |= ((uint8_t)val) << 8;
        break;
    }
    case TxAddr0 + 2:
    case TxAddr0 + 6:
    case TxAddr0 + 10:
    case TxAddr0 + 14: {
        int idx = (addr - TxAddr0 - 2) / 4;
        s->tx_addr[idx] &= ~(0xffu << 16);
        s->tx_addr[idx] |= ((uint8_t)val) << 16;
        break;
    }
    case TxAddr0 + 3:
    case TxAddr0 + 7:
    case TxAddr0 + 11:
    case TxAddr0 + 15: {
        int idx = (addr - TxAddr0 - 3) / 4;
        s->tx_addr[idx] &= ~(0xffu << 24);
        s->tx_addr[idx] |= ((uint8_t)val) << 24;
        break;
    }
    case RxBuf + 0:
    case RxBuf + 1:
    case RxBuf + 2:
    case RxBuf + 3: {
        int shift = (addr - RxBuf) * 8;
        s->rx_buf_dma &= ~(0xffu << shift);
        s->rx_buf_dma |= ((uint32_t)val & 0xffu) << shift;
        s->rx_ring_dma = s->rx_buf_dma;
        break;
    }
    case ChipCmd:
        if (val & CmdReset) {
            s->chip_cmd |= CmdReset;
            s->chip_cmd &= ~(CmdRxEnb | CmdTxEnb);
            s->rx_buf_ptr = 0;
            s->rx_buf_addr = 0;
            s->intr_status = 0;
            s->intr_mask = 0;
            s->rx_missed = 0;
            s->chip_cmd &= ~CmdReset;
        } else {
            s->chip_cmd = (uint8_t)val & (CmdRxEnb | CmdTxEnb | RxBufEmpty);
        }
        break;
    case RxBufPtr:
        s->rx_buf_ptr &= ~0x00ff;
        s->rx_buf_ptr |= (uint16_t)val;
        break;
    case RxBufPtr + 1:
        s->rx_buf_ptr &= ~0xff00;
        s->rx_buf_ptr |= ((uint16_t)val) << 8;
        break;
    case RxBufAddr:
        s->rx_buf_addr &= ~0x00ff;
        s->rx_buf_addr |= (uint16_t)val;
        break;
    case RxBufAddr + 1:
        s->rx_buf_addr &= ~0xff00;
        s->rx_buf_addr |= ((uint16_t)val) << 8;
        break;
    case IntrMask:
        s->intr_mask &= ~0x00ff;
        s->intr_mask |= (uint16_t)val;
        rtl8139_lower_irq_if_clear(s);
        break;
    case IntrMask + 1:
        s->intr_mask &= ~0xff00;
        s->intr_mask |= ((uint16_t)val) << 8;
        rtl8139_lower_irq_if_clear(s);
        break;
    case IntrStatus:
        s->intr_status &= ~(uint16_t)val;
        rtl8139_lower_irq_if_clear(s);
        break;
    case IntrStatus + 1:
        s->intr_status &= ~(((uint16_t)val) << 8);
        rtl8139_lower_irq_if_clear(s);
        break;
    case TxConfig + 0:
    case TxConfig + 1:
    case TxConfig + 2:
    case TxConfig + 3: {
        int shift = (addr - TxConfig) * 8;
        s->tx_config &= ~(0xffu << shift);
        s->tx_config |= ((uint32_t)val & 0xffu) << shift;
        break;
    }
    case RxConfig + 0:
    case RxConfig + 1:
    case RxConfig + 2:
    case RxConfig + 3: {
        int shift = (addr - RxConfig) * 8;
        s->rx_config_reg &= ~(0xffu << shift);
        s->rx_config_reg |= ((uint32_t)val & 0xffu) << shift;
        break;
    }
    case RxMissed + 0:
    case RxMissed + 1:
    case RxMissed + 2:
    case RxMissed + 3:
        s->rx_missed = 0;
        break;
    case Cfg9346:
        s->cfg9346 = (uint8_t)val;
        break;
    case Config0:
        s->config0 = (uint8_t)val;
        break;
    case Config1:
        s->config1 = (uint8_t)val;
        break;
    case MediaStatus:
        s->media_status = (uint8_t)val;
        break;
    case Config3:
        s->config3 = (uint8_t)val;
        break;
    case Config4:
        s->config4 = (uint8_t)val;
        break;
    case HltClk:
        s->hltclk = (uint8_t)val;
        break;
    case MultiIntr:
        s->multi_intr &= ~0x00ff;
        s->multi_intr |= (uint16_t)val;
        break;
    case MultiIntr + 1:
        s->multi_intr &= ~0xff00;
        s->multi_intr |= ((uint16_t)val) << 8;
        break;
    case BasicModeCtrl:
        s->basic_mode_ctrl &= ~0x00ff;
        s->basic_mode_ctrl |= (uint16_t)val;
        break;
    case BasicModeCtrl + 1:
        s->basic_mode_ctrl &= ~0xff00;
        s->basic_mode_ctrl |= ((uint16_t)val) << 8;
        break;
    case BasicModeStatus:
        s->basic_mode_status &= ~0x00ff;
        s->basic_mode_status |= (uint16_t)val;
        break;
    case BasicModeStatus + 1:
        s->basic_mode_status &= ~0xff00;
        s->basic_mode_status |= ((uint16_t)val) << 8;
        break;
    case NWayAdvert:
        s->nway_advert &= ~0x00ff;
        s->nway_advert |= (uint16_t)val;
        break;
    case NWayAdvert + 1:
        s->nway_advert &= ~0xff00;
        s->nway_advert |= ((uint16_t)val) << 8;
        break;
    case NWayLPAR:
        s->nway_lpar &= ~0x00ff;
        s->nway_lpar |= (uint16_t)val;
        break;
    case NWayLPAR + 1:
        s->nway_lpar &= ~0xff00;
        s->nway_lpar |= ((uint16_t)val) << 8;
        break;
    case NWayExpansion:
        s->nway_expansion &= ~0x00ff;
        s->nway_expansion |= (uint16_t)val;
        break;
    case NWayExpansion + 1:
        s->nway_expansion &= ~0xff00;
        s->nway_expansion |= ((uint16_t)val) << 8;
        break;
    case FIFOTMS + 0:
    case FIFOTMS + 1:
    case FIFOTMS + 2:
    case FIFOTMS + 3:
        break;
    case CSCR + 0:
        s->cscr &= ~0x00ff;
        s->cscr |= (uint16_t)val;
        break;
    case CSCR + 1:
        s->cscr &= ~0xff00;
        s->cscr |= ((uint16_t)val) << 8;
        break;
    case PARA78 + 0:
    case PARA78 + 1:
    case PARA78 + 2:
    case PARA78 + 3: {
        int shift = (addr - PARA78) * 8;
        s->para78 &= ~(0xffu << shift);
        s->para78 |= ((uint32_t)val & 0xffu) << shift;
        break;
    }
    case PARA7c + 0:
    case PARA7c + 1:
    case PARA7c + 2:
    case PARA7c + 3: {
        int shift = (addr - PARA7c) * 8;
        s->para7c &= ~(0xffu << shift);
        s->para7c |= ((uint32_t)val & 0xffu) << shift;
        break;
    }
    case FlashReg + 0:
    case FlashReg + 1:
    case FlashReg + 2:
    case FlashReg + 3:
        s->flash[addr - FlashReg] = (uint8_t)val;
        break;
    case Config5:
        s->config5 = (uint8_t)val;
        break;
    default:
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return pcibase_mmio_read(opaque, addr, size);
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    pcibase_mmio_write(opaque, addr, val, size);
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    pci_device_reset(pdev);

    s->tx_config = rtl8139_tx_config;
    s->rx_config_reg = rtl8139_rx_config;
    s->intr_status = 0;
    s->intr_mask = 0;
    s->chip_cmd = RxBufEmpty;
    s->rx_missed = 0;
    s->cfg9346 = Cfg9346_Lock;
    s->config0 = 0;
    s->config1 = 0;
    s->config3 = 0;
    s->config4 = 0;
    s->config5 = 0;
    s->hltclk = 0;
    s->media_status = 0;
    s->multi_intr = 0;
    s->basic_mode_ctrl = 0;
    s->basic_mode_status = 0x786d;
    s->nway_advert = 0x05e1;
    s->nway_lpar = 0x0000;
    s->nway_expansion = 0x0000;
    s->cscr = CSCR_LinkOKBit;
    s->para78 = PARA78_default;
    s->para7c = PARA7c_default;
    memset(s->flash, 0, sizeof(s->flash));
    s->rx_buf_dma = 0;
    s->rx_buf_ptr = 0;
    s->rx_buf_addr = 0;
    memset(s->tx_addr, 0, sizeof(s->tx_addr));
    memset(s->tx_status, 0, sizeof(s->tx_status));
}

static void pcibase_dma_device_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    (void)s;
    (void)errp;
}

static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    uint32_t val = pci_default_read_config(pdev, addr, len);

    switch (len) {
    case 1:
        val &= 0xff;
        break;
    case 2:
        val &= 0xffff;
        break;
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

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  RTL8139_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  RTL8139_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, RTL8139_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x10);
    pci_config_set_interrupt_pin(pci_conf, 1);

    memset(s->mac, 0, sizeof(s->mac));

    s->num_bars = 2;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = RTL8139B_IO_SIZE;
    s->bar_info[0].name = "rtl8139-io";
    s->bar_info[0].sparse = false;

    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = RTL8139B_IO_SIZE;
    s->bar_info[1].name = "rtl8139-mmio";
    s->bar_info[1].sparse = false;

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
        if (errp && *errp) {
            return;
        }
    }

    s->rx_ring_host = NULL;
    s->rx_ring_dma = 0;

    s->irq = pci_allocate_irq(pdev);

    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) {
        return;
    }

    pcibase_reset(DEVICE(pdev));
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

    if (s->mmio_backing) {
        g_free(s->mmio_backing);
        s->mmio_backing = NULL;
    }

    if (s->rx_ring_host) {
        g_free(s->rx_ring_host);
        s->rx_ring_host = NULL;
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

