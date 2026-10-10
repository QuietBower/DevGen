/*
 * QEMU PCI device model for Realtek 8139cp-compatible NIC
 *
 * NOTE: This implementation is intentionally minimal and only provides
 * enough visible behaviour for the Linux 8139cp driver to probe and bind.
 * Register layout and behaviour are constrained strictly to what can be
 * seen in the provided driver source (8139cp.c) and basic 8139C+
 * knowledge such as register names referenced there.
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

#define TYPE_PCIBASE_DEVICE "8139cp_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* ------------------------------------------------------------------ */
/* Minimal register/address definitions as referenced by driver       */
/* ------------------------------------------------------------------ */

/* These offsets are deduced solely from their use as byte offsets in
 * cpw8/cpw16/cpw32, cpr8/cpr16/cpr32 macros within the driver.
 * Only the offsets actually referenced in the provided source are
 * defined here.
 */

/* MAC address base */
#define MAC0              0x00

/* Command register (8-bit) */
#define Cmd               0x37
#define CmdReset          0x10
#define RxOn              0x08
#define TxOn              0x04

/* 93C46/93C56 EEPROM and config */
#define Cfg9346           0x50
#define Cfg9346_Unlock    0xC0
#define Cfg9346_Lock      0x00

/* Interrupt registers */
#define IntrStatus        0x3E
#define IntrMask          0x3C
#define MultiIntr         0x5C

/* C+ Command register and related */
#define CpCmd             0xE0
#define RxRingAddr        0xE4
#define TxRingAddr        0xE8
#define HiTxRingAddr      0xEC

/* Rx/Tx config and control */
#define RxConfig          0x44
#define TxConfig          0x40
#define TxPoll            0x38
#define TxThresh          0xEC /* note: TxThresh overlaps with HiTxRingAddr in 8139C+, but in driver it uses TxThresh as 0xEC, we just accept writes */

/* Misc config */
#define Config1           0x52
#define Config3           0x59
#define Config5           0xD8

/* WOL bits/macros referenced in driver */
#define LinkUp            0x20
#define MagicPacket       0x10
#define UWF               0x02
#define MWF               0x04
#define BWF               0x08
#define DriverLoaded      0x04
#define PMEnable          0x01
#define PARMEnable        0x40
#define PMEStatus         0x01

/* Statistics */
#define RxMissed          0x4C
#define StatsAddr         0xE0 /* base, low dword; high at +4, driver uses StatsAddr and StatsAddr+4 */
#define DumpStats         0x00000008

/* Tx diagnostic register referenced in timeout path */
#define TxDmaOkLowDesc    0xC0

/* Rx/Tx interrupt bits (subset used by driver) */
#define RxOK              0x0001
#define RxErr             0x0002
#define TxOK              0x0004
#define TxErr             0x0008
#define RxEmpty           0x0010
#define RxFIFOOvr         0x0040
#define TxEmpty           0x0020
#define SWInt             0x0100
#define LinkChg           0x0020 /* NOTE: True value on real hw differs, but we never expose non-zero LinkChg except via software */
#define PciErr            0x8000

/* Descriptor bits and masks as used in driver */
#define DescOwn           0x80000000U
#define RingEnd           0x02000000U
#define FirstFrag         0x20000000U
#define LastFrag          0x10000000U
#define LargeSend         0x08000000U
#define MSSShift          16
#define MSSMask           0x07FF
#define IPCS              0x40000000U
#define TCPCS             0x20000000U
#define UDPCS             0x10000000U

#define RxError           0x00008000U
#define RxErrFrame        0x00000040U
#define RxErrCRC          0x00000008U
#define RxErrRunt         0x00000010U
#define RxErrLong         0x00000020U
#define RxErrFIFO         0x00000080U

#define RxVlanTagged      0x00010000U
#define RxProtoTCP        0x2
#define RxProtoUDP        0x3
#define TCPFail           0x00008000U
#define UDPFail           0x00004000U

#define TxError           0x00008000U
#define TxFIFOUnder       0x00004000U
#define TxOWC             0x00000400U
#define TxMaxCol          0x00000200U
#define TxLinkFail        0x00000800U
#define TxColCntShift     24
#define TxColCntMask      0x0F

#define RxChkSum          0x0002
#define RxVlanOn          0x0040
#define CpRxOn            0x0008
#define CpTxOn            0x0004
#define PCIDAC            0x0001
#define PCIMulRW          0x0002

/* TxConfig bits referenced */
#define IFG               0x03000000U
#define TxDMAShift        8

#define NormalTxPoll      0x01

/* EEPROM helper macros from driver */
#define __NETIF_MSG_BIT(bit)   ((u32)1 << (bit))
#define NETIF_MSG_DRV        __NETIF_MSG_BIT(0)
#define NETIF_MSG_PROBE      __NETIF_MSG_BIT(1)
#define NETIF_MSG_LINK       __NETIF_MSG_BIT(2)

#define PCI_VENDOR_ID_REALTEK        0x10ec
#define PCI_DEVICE_ID_REALTEK_8139   0x8139
#define PCI_VENDOR_ID_TTTECH         0x0357
#define PCI_DEVICE_ID_TTTECH_MC322   0x000a
#define __USER_HZ    100
#define HZ __USER_HZ
#define DRV_NAME        "8139cp"
#define DRV_VERSION     "1.3"
#define TX_TIMEOUT      (6*HZ)
#define PKT_BUF_SZ      1536
#define DRV_RELDATE     "Mar 22, 2004"
#define EE_SHIFT_CLK    0x04
#define EE_CS           0x08
#define EE_DATA_WRITE   0x02
#define EE_DATA_READ    0x01
#define EE_ENB          (0x80 | EE_CS)
#define EE_WRITE_CMD    (5)
#define EE_READ_CMD     (6)
#define EE_ERASE_CMD    (7)
#define CP_DEF_MSG_ENABLE   (NETIF_MSG_DRV | NETIF_MSG_PROBE | NETIF_MSG_LINK)
#define CP_NUM_STATS        14
#define CP_STATS_SIZE       64
#define CP_REGS_SIZE        (0xff + 1)
#define CP_REGS_VER         1
#define CP_RX_RING_SIZE     64
#define CP_TX_RING_SIZE     64
#define CP_RING_BYTES       \
        ((sizeof(struct cp_desc) * CP_RX_RING_SIZE) + \
         (sizeof(struct cp_desc) * CP_TX_RING_SIZE) + \
         CP_STATS_SIZE)
#define CP_INTERNAL_PHY     32
#define RX_FIFO_THRESH      5
#define RX_DMA_BURST        4
#define TX_DMA_BURST        6
#define TX_EARLY_THRESH     256
#define CP_MIN_MTU          60
#define CP_MAX_MTU          4096
#define EE_WRITE_0      0x00
#define EE_WRITE_1      0x02
#define EE_EXTEND_CMD   (4)
#define EE_EWDS_ADDR    (0)
#define EE_WRAL_ADDR    (1)
#define EE_ERAL_ADDR    (2)
#define EE_EWEN_ADDR    (3)

/* Additional definitions from new driver source */
#define CP_EEPROM_MAGIC PCI_DEVICE_ID_REALTEK_8139
#define MAR0            0x60

/* First PCI ID entry from cp_pci_tbl[] */
#define CP_PCI_VENDOR_ID   PCI_VENDOR_ID_REALTEK
#define CP_PCI_DEVICE_ID   PCI_DEVICE_ID_REALTEK_8139

/* ------------------------------------------------------------------ */
/* Driver-visible DMA-related structs (static layout only)            */
/* ------------------------------------------------------------------ */
struct cp_desc {
    uint32_t    opts1;
    uint32_t    opts2;
    uint64_t    addr;
};

struct cp_dma_stats {
    uint64_t            tx_ok;
    uint64_t            rx_ok;
    uint64_t            tx_err;
    uint32_t            rx_err;
    uint16_t            rx_fifo;
    uint16_t            frame_align;
    uint32_t            tx_ok_1col;
    uint32_t            tx_ok_mcol;
    uint64_t            rx_ok_phys;
    uint64_t            rx_ok_bcast;
    uint32_t            rx_ok_mcast;
    uint16_t            tx_abort;
    uint16_t            tx_underrun;
};

struct cp_extra_stats {
    unsigned long       rx_frags;
};

/* Forward declarations for driver-specific kernel types used only in
 * struct cp_private; their layout is not required for this model.
 */
struct net_device;
struct napi_struct;
struct pci_dev;
struct sk_buff;

struct mii_if_info {
    int phy_id;
    int advertising;
    int phy_id_mask;
    int reg_num_mask;

    unsigned int full_duplex : 1;  /* is full duplex? */
    unsigned int force_media : 1;  /* is autoneg. disabled? */
    unsigned int supports_gmii : 1; /* are GMII registers supported? */

    struct net_device *dev;
    int (*mdio_read) (struct net_device *dev, int phy_id, int location);
    void (*mdio_write) (struct net_device *dev, int phy_id, int location, int val);
};

struct cp_private {
    void            *regs;
    struct net_device   *dev;
    unsigned long       lock;
    uint32_t        msg_enable;

    struct napi_struct  *napi;

    struct pci_dev      *pdev;
    uint32_t        rx_config;
    uint16_t        cpcmd;

    struct cp_extra_stats   cp_stats;

    unsigned        rx_head;
    unsigned        rx_tail;
    struct cp_desc      *rx_ring;
    struct sk_buff      *rx_skb[CP_RX_RING_SIZE];

    unsigned        tx_head;
    unsigned        tx_tail;
    struct cp_desc      *tx_ring;
    struct sk_buff      *tx_skb[CP_TX_RING_SIZE];
    uint32_t        tx_opts[CP_TX_RING_SIZE];

    unsigned        rx_buf_sz;
    unsigned        wol_enabled : 1; /* Is Wake-on-LAN enabled? */

    uint64_t        ring_dma;

    struct mii_if_info mii_if;
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

    /* simple internal register backing for CP_REGS_SIZE */
    uint8_t regs[CP_REGS_SIZE];

    /* interrupt state */
    uint16_t intr_status;
    uint16_t intr_mask;

    /* descriptor ring state from device registers */
    uint32_t rx_ring_addr_lo;
    uint32_t rx_ring_addr_hi;
    uint32_t tx_ring_addr_lo;
    uint32_t tx_ring_addr_hi;

    /* stats accumulation for DumpStats */
    struct cp_dma_stats stats;
};

/* ------------------------------------------------------------------ */
/* Forward declarations                                                */
/* ------------------------------------------------------------------ */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
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

/* ------------------------------------------------------------------ */
/* Helper: register a BAR (MMIO or PIO)                                 */
/* ------------------------------------------------------------------ */
static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE)
        return;

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

/* ------------------------------------------------------------------ */
/* MMIO / PIO handlers                                                  */
/* ------------------------------------------------------------------ */

static uint16_t pcibase_getw(PCIBaseState *s, hwaddr addr)
{
    if (addr + 1 >= CP_REGS_SIZE) {
        return 0xffff;
    }
    return s->regs[addr] | (s->regs[addr + 1] << 8);
}

static void pcibase_putw(PCIBaseState *s, hwaddr addr, uint16_t val)
{
    if (addr + 1 >= CP_REGS_SIZE) {
        return;
    }
    s->regs[addr] = val & 0xff;
    s->regs[addr + 1] = (val >> 8) & 0xff;
}

static uint32_t pcibase_getl(PCIBaseState *s, hwaddr addr)
{
    if (addr + 3 >= CP_REGS_SIZE) {
        return 0xffffffffU;
    }
    return s->regs[addr] | (s->regs[addr + 1] << 8) |
           (s->regs[addr + 2] << 16) | (s->regs[addr + 3] << 24);
}

static void pcibase_putl(PCIBaseState *s, hwaddr addr, uint32_t val)
{
    if (addr + 3 >= CP_REGS_SIZE) {
        return;
    }
    s->regs[addr] = val & 0xff;
    s->regs[addr + 1] = (val >> 8) & 0xff;
    s->regs[addr + 2] = (val >> 16) & 0xff;
    s->regs[addr + 3] = (val >> 24) & 0xff;
}

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool active = (s->intr_status & s->intr_mask) != 0;
    pci_set_irq(pdev, active ? 1 : 0);
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= CP_REGS_SIZE) {
        return 0xffffffffULL;
    }

    switch (size) {
    case 1:
        /* special cases where we maintain separate shadow */
        if (addr == Cmd) {
            return s->regs[Cmd];
        }
        return s->regs[addr];
    case 2:
        if (addr == IntrStatus) {
            return s->intr_status;
        }
        if (addr == IntrMask) {
            return s->intr_mask;
        }
        return pcibase_getw(s, addr);
    case 4:
        if (addr == RxRingAddr) {
            return s->rx_ring_addr_lo;
        }
        if (addr == RxRingAddr + 4) {
            return s->rx_ring_addr_hi;
        }
        if (addr == TxRingAddr) {
            return s->tx_ring_addr_lo;
        }
        if (addr == TxRingAddr + 4) {
            return s->tx_ring_addr_hi;
        }
        if (addr == StatsAddr) {
            /* Reading StatsAddr clears DumpStats in hw; here just
             * return current low dword backing.
             */
            return pcibase_getl(s, addr);
        }
        if (addr == StatsAddr + 4) {
            return pcibase_getl(s, addr);
        }
        return pcibase_getl(s, addr);
    default:
        break;
    }

    return 0xffffffffULL;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= CP_REGS_SIZE) {
        return;
    }

    switch (size) {
    case 1: {
        uint8_t b = val & 0xff;
        if (addr == Cmd) {
            /* handle reset bit */
            if (b & CmdReset) {
                /* emulate immediate reset completion */
                s->regs[Cmd] = 0;
                s->intr_status = 0;
                s->intr_mask = 0;
                pcibase_update_irq(s);
                return;
            }
            /* Store enable bits RxOn/TxOn */
            s->regs[Cmd] = b & (RxOn | TxOn);
            return;
        }
        if (addr == Cfg9346 || addr == Config1 || addr == Config3 ||
            addr == Config5 || addr == TxThresh) {
            /* store configuration, no side effects */
            s->regs[addr] = b;
            return;
        }
        if (addr == TxPoll) {
            /* Kick Tx; we don't emulate real DMA, so no-op */
            s->regs[addr] = b;
            return;
        }
        s->regs[addr] = b;
        break;
    }
    case 2: {
        uint16_t w = val & 0xffff;
        if (addr == IntrMask) {
            s->intr_mask = w;
            pcibase_update_irq(s);
            return;
        }
        if (addr == IntrStatus) {
            /* writing 1 clears that bit */
            s->intr_status &= ~(w & 0xffff);
            pcibase_update_irq(s);
            return;
        }
        if (addr == CpCmd) {
            /* store C+ command */
            pcibase_putw(s, addr, w);
            return;
        }
        if (addr == MultiIntr) {
            pcibase_putw(s, addr, w);
            return;
        }
        if (addr == RxMissed) {
            /* write any value clears counters; we don't track */
            pcibase_putw(s, addr, 0);
            return;
        }
        pcibase_putw(s, addr, w);
        break;
    }
    case 4: {
        uint32_t l = val & 0xffffffffU;
        if (addr == RxRingAddr) {
            s->rx_ring_addr_lo = l;
            pcibase_putl(s, addr, l);
            return;
        }
        if (addr == RxRingAddr + 4) {
            s->rx_ring_addr_hi = l;
            pcibase_putl(s, addr, l);
            return;
        }
        if (addr == TxRingAddr) {
            s->tx_ring_addr_lo = l;
            pcibase_putl(s, addr, l);
            return;
        }
        if (addr == TxRingAddr + 4) {
            s->tx_ring_addr_hi = l;
            pcibase_putl(s, addr, l);
            return;
        }
        if (addr == TxConfig || addr == RxConfig) {
            pcibase_putl(s, addr, l);
            return;
        }
        if (addr == StatsAddr) {
            /* Start/stop DumpStats per driver logic. Driver writes
             * ((u64)dma & DMA_BIT_MASK(32)) | DumpStats, then polls
             * until DumpStats bit clears, then writes 0 to StatsAddr
             * and StatsAddr+4.
             * We don't perform DMA; we simply clear DumpStats
             * immediately when it's set.
             */
            pcibase_putl(s, addr, l & ~DumpStats);
            return;
        }
        if (addr == StatsAddr + 4) {
            pcibase_putl(s, addr, l);
            return;
        }
        if (addr == RxMissed) {
            /* treat also 32-bit writes as clear */
            pcibase_putl(s, addr, 0);
            return;
        }
        pcibase_putl(s, addr, l);
        break;
    }
    default:
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* The 8139cp driver only uses BAR1 MMIO; PIO BAR0 is not used. */
    (void)opaque;
    (void)addr;

    switch (size) {
    case 1:
        return 0xff;
    case 2:
        return 0xffff;
    case 4:
        return 0xffffffffU;
    default:
        break;
    }
    return 0xffffffffU;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* PIO not used by driver; ignore writes */
    (void)opaque;
    (void)addr;
    (void)val;
    (void)size;
}

/* ------------------------------------------------------------------ */
/* Reset       */
/* ------------------------------------------------------------------ */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    pci_device_reset(pdev);

    memset(s->regs, 0, sizeof(s->regs));
    s->intr_status = 0;
    s->intr_mask = 0;
    s->rx_ring_addr_lo = 0;
    s->rx_ring_addr_hi = 0;
    s->tx_ring_addr_lo = 0;
    s->tx_ring_addr_hi = 0;
    memset(&s->stats, 0, sizeof(s->stats));

    if (s->mmio_backing && s->mmio_backing_size) {
        memset(s->mmio_backing, 0, s->mmio_backing_size);
    }
}

/* ------------------------------------------------------------------ */
/* DMA initialize (called from realize)                                */
/* ------------------------------------------------------------------ */
static void pcibase_dma_device_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    (void)s;
    (void)errp;
    /* Driver sets DMA mask/coherent via PCI core; device does not
     * need to perform additional initialization here.
     */
}

/* ------------------------------------------------------------------ */
/* PCI config space access (class-level hooks)                         */
/* ------------------------------------------------------------------ */
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

/* ------------------------------------------------------------------ */
/* Realize (device init)                                                */
/* ------------------------------------------------------------------ */
static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Initialize BAR metadata: driver uses BAR1 as MMIO of CP_REGS_SIZE */
    s->num_bars = 0;

    /* BAR0: optional PIO (unused by driver, small size) */
    s->bar_info[s->num_bars].index = 0;
    s->bar_info[s->num_bars].type = BAR_TYPE_PIO;
    s->bar_info[s->num_bars].size = 0x100;
    s->bar_info[s->num_bars].name = "8139cp-io";
    s->bar_info[s->num_bars].sparse = false;
    s->num_bars++;

    /* BAR1: MMIO registers */
    s->bar_info[s->num_bars].index = 1;
    s->bar_info[s->num_bars].type = BAR_TYPE_MMIO;
    s->bar_info[s->num_bars].size = CP_REGS_SIZE;
    s->bar_info[s->num_bars].name = "8139cp-mmio";
    s->bar_info[s->num_bars].sparse = false;
    s->num_bars++;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  CP_PCI_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  CP_PCI_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_ETHERNET );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x20); /* >= 0x20 to satisfy check */
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* register BARs */
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
        if (errp && *errp) {
            return;
        }
    }

    /* DMA init */
    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) {
        return;
    }

    /* Legacy INTx only; MSI/MSI-X not used in driver. */

    qemu_log_mask(LOG_UNIMP, "[%s] device realized\n", TYPE_PCIBASE_DEVICE);
}

/* ------------------------------------------------------------------ */
/* Uninit/cleanup                                                      */
/* ------------------------------------------------------------------ */
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
        s->mmio_backing_size = 0;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] device uninit\n", TYPE_PCIBASE_DEVICE);
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

