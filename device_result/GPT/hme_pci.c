/*
 * QEMU PCI device model for Sun Happy Meal (sunhme)
 * Phase 2: Functional behavior implementation based strictly on driver source.
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

#include "hw/pci/pci_ids.h" /* For PCI_VENDOR_ID_SUN and PCI_DEVICE_ID_SUN_HAPPYMEAL */

#define TYPE_PCIBASE_DEVICE "hme_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register offsets and identifiers copied from driver */
#define GREG_SWRESET              0x000UL
#define GREG_CFG                  0x004UL
#define GREG_IMASK                0x104UL
#define GREG_STAT                 0x100UL
#define BMAC_XIFCFG               0x6000UL
#define BMAC_TXSWRESET            0x6208UL
#define BMAC_RXSWRESET            0x6308UL
#define BMAC_TXCFG                0x620cUL
#define BMAC_RXCFG                0x630cUL
#define BMAC_TXMAX                0x6230UL
#define BMAC_JSIZE                0x622cUL
#define BMAC_ALIMIT               0x6218UL
#define BMAC_IGAP1                0x6210UL
#define BMAC_IGAP2                0x6214UL
#define BMAC_RSEED                0x6250UL
#define BMAC_RXMAX                0x6310UL
#define BMAC_EXCTR                0x6248UL
#define BMAC_LTCTR                0x624cUL
#define BMAC_GLECTR               0x6328UL
#define BMAC_UNALECTR             0x632cUL
#define BMAC_RCRCECTR             0x6330UL
#define BMAC_MACADDR0             0x6320UL
#define BMAC_MACADDR1             0x631cUL
#define BMAC_MACADDR2             0x6318UL
#define BMAC_HTABLE0              0x634cUL
#define BMAC_HTABLE1              0x6348UL
#define BMAC_HTABLE2              0x6344UL
#define BMAC_HTABLE3              0x6340UL

#define ERX_CFG                   0x4000UL
#define ERX_RING                  0x4004UL
#define ETX_RING                  0x2008UL
#define ETX_CFG                   0x2004UL
#define ETX_PENDING               0x2000UL
#define ETX_RSIZE                 0x202cUL

#define TCVR_BBCLOCK              0x7000UL
#define TCVR_BBDATA               0x7004UL
#define TCVR_BBOENAB              0x7008UL
#define TCVR_FRAME                0x700cUL
#define TCVR_CFG                  0x7010UL

/* Various bit definitions used by the driver */
#define GREG_RESET_ALL            0x00000003
#define GREG_CFG_BURST16          0x00000000
#define GREG_CFG_BURST32          0x00000001
#define GREG_CFG_BURST64          0x00000002
#define GREG_CFG_64BIT            0x00000004

#define GREG_IMASK_GOTFRAME       0x00000001
#define GREG_IMASK_RCNTEXP        0x00000002
#define GREG_IMASK_SENTFRAME      0x00000100
#define GREG_IMASK_TXPERR         0x10000000

#define GREG_STAT_GOTFRAME        0x00000001
#define GREG_STAT_RCNTEXP         0x00000002
#define GREG_STAT_RFIFOVF         0x00000020
#define GREG_STAT_STSTERR         0x00000080
#define GREG_STAT_RXTOHOST        0x00010000
#define GREG_STAT_NORXD           0x00020000
#define GREG_STAT_RXERR           0x00040000
#define GREG_STAT_MAXPKTERR       0x00000400
#define GREG_STAT_TFIFO_UND       0x00000200
#define GREG_STAT_EOPERR          0x00400000
#define GREG_STAT_RXPERR          0x00100000
#define GREG_STAT_RXTERR          0x00200000
#define GREG_STAT_TXEACK          0x04000000
#define GREG_STAT_TXLERR          0x08000000
#define GREG_STAT_TXALL           0x02000000
#define GREG_STAT_TXPERR          0x10000000
#define GREG_STAT_TXTERR          0x20000000
#define GREG_STAT_MIFIRQ          0x00800000
#define GREG_STAT_SLVPERR         0x80000000
#define GREG_STAT_SLVERR          0x40000000
#define GREG_STAT_ERRORS          0xfc7efefc

#define BIGMAC_TXCFG_ENABLE       0x00000001
#define BIGMAC_TXCFG_FULLDPLX     0x00000200

#define BIGMAC_RXCFG_ENABLE       0x00000001
#define BIGMAC_RXCFG_REJME        0x00000200
#define BIGMAC_RXCFG_PMISC        0x00000040
#define BIGMAC_RXCFG_HENABLE      0x00000800

#define BIGMAC_XCFG_ODENABLE      0x00000001
#define BIGMAC_XCFG_MIIDISAB      0x00000008
#define BIGMAC_XCFG_LANCE         0x00000010

#define TCV_CFG_PSELECT           0x00000001
#define TCV_CFG_BENABLE           0x00000004
#define TCV_CFG_MDIO0             0x00000100
#define TCV_CFG_MDIO1             0x00000200

#define FRAME_READ                0x60020000
#define FRAME_WRITE               0x50020000

#define HFLAG_FENABLE             0x00000002
#define HFLAG_LANCE               0x00000004
#define HFLAG_RXCV                0x00000100
#define HFLAG_INIT                0x00000200
#define HFLAG_PCI                 0x00000800
#define HFLAG_QUATTRO             0x00001000
#define HFLAG_FULL                0x00000020
#define HFLAG_NOT_A0              (HFLAG_FENABLE | HFLAG_LANCE | HFLAG_RXCV)
#define HFLAG_20_21               HFLAG_FENABLE

#define ETX_CFG_DMAENABLE         0x00000001
#define ERX_CFG_DMAENABLE         0x00000001
#define ERX_CFG_SIZE32            0x00000000
#define ERX_CFG_DEFAULT(off)      (ERX_CFG_DMAENABLE | ((off) << 3) | ERX_CFG_SIZE32 | ((14 / 2) << 16))

#define ETX_TP_DMAWAKEUP          0x00000001

#define TX_RING_SIZE              32
#define RX_RING_SIZE              32
#define TX_RING_MAXSIZE           256
#define RX_RING_MAXSIZE           256

#define TXFLAG_OWN                0x80000000
#define TXFLAG_EOP                0x20000000
#define TXFLAG_SOP                0x40000000
#define TXFLAG_CSENABLE           0x10000000
#define TXFLAG_CSLOCATION         0x0ff00000
#define TXFLAG_CSBUFBEGIN         0x000fc000
#define TXFLAG_SIZE               0x00003fff

#define RXFLAG_OWN                0x80000000
#define RXFLAG_OVERFLOW           0x40000000
#define RXFLAG_CSUM               0x0000ffff

#define RX_OFFSET                 2
#define RX_BUF_ALLOC_SIZE         (1546 + RX_OFFSET + 64)
#define RX_COPY_THRESHOLD         256

#define DEFAULT_IPG0              16
#define DEFAULT_IPG1              8
#define DEFAULT_IPG2              4
#define DEFAULT_JAMSIZE           4

#define TCVR_FAILURE              0x80000000
#define TCVR_READ_TRIES           16
#define TCVR_WRITE_TRIES          16
#define TCVR_RESET_TRIES          16
#define TCVR_UNISOLATE_TRIES      32

#define TX_RESET_TRIES            32
#define RX_RESET_TRIES            32
#define STOP_TRIES                16

#define GREG_IMASK_RCNTEXP        0x00000002
#define GREG_IMASK_SENTFRAME      0x00000100
#define GREG_IMASK_TXPERR         0x10000000

#define GREG_STAT_TXALL_LOCAL     0x02000000

#define HME_PCI_VENDOR_ID   0x108e
#define HME_PCI_DEVICE_ID   0x1001
#define HME_PCI_CLASS_ID    0x0200

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

    /* Resources */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;

    uint32_t intr_status;
    uint32_t intr_mask;

    /* Global registers (offset 0x0000 area) */
    uint32_t greg_swreset;
    uint32_t greg_cfg;
    uint32_t greg_imask;
    uint32_t greg_stat;

    /* BigMAC registers (0x6000 area) */
    uint32_t bmac_xifcfg;
    uint32_t bmac_txcfg;
    uint32_t bmac_rxcfg;
    uint32_t bmac_txmax;
    uint32_t bmac_jsize;
    uint32_t bmac_alimit;
    uint32_t bmac_igap1;
    uint32_t bmac_igap2;
    uint32_t bmac_rseed;
    uint32_t bmac_rxmax;
    uint32_t bmac_exctr;
    uint32_t bmac_ltctr;
    uint32_t bmac_glectr;
    uint32_t bmac_unalectr;
    uint32_t bmac_rcrcectr;
    uint32_t bmac_macaddr0;
    uint32_t bmac_macaddr1;
    uint32_t bmac_macaddr2;
    uint32_t bmac_htable0;
    uint32_t bmac_htable1;
    uint32_t bmac_htable2;
    uint32_t bmac_htable3;

    /* ERX / ETX (0x4000 / 0x2000) */
    uint32_t erx_cfg;
    uint32_t erx_ring;
    uint32_t etx_ring;
    uint32_t etx_cfg;
    uint32_t etx_rsize;
    uint32_t etx_pending;

    /* Transceiver (0x7000) */
    uint32_t tcvr_cfg;
    uint32_t tcvr_frame;
    uint32_t tcvr_bbdata;
    uint32_t tcvr_bbclock;
    uint32_t tcvr_bboenab;

    uint8_t macaddr[6];

    /* Internal pseudo PHY state to satisfy MII accesses */
    uint16_t phy_bmcr;
    uint16_t phy_bmsr;
    uint16_t phy_advertise;
    uint16_t phy_lpa;
    uint16_t phy_physid1;
    uint16_t phy_physid2;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t pending = s->greg_stat & s->greg_imask;

    if (pending) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /* The real hardware is bus mastering with descriptor rings.
     * The driver manages descriptors entirely in system memory and
     * only checks for completion based on descriptor OWN bits and
     * interrupt status. Implementing real data movement is not
     * necessary for probe/bind and basic operation, so we leave
     * this empty.
     */
    (void)s;
    (void)is_write;
}

static uint32_t pcibase_mmio_read32(PCIBaseState *s, hwaddr addr)
{
    switch (addr) {
    /* Global registers */
    case GREG_SWRESET:
        return s->greg_swreset;
    case GREG_CFG:
        return s->greg_cfg;
    case GREG_STAT:
        return s->greg_stat;
    case GREG_IMASK:
        return s->greg_imask;

    /* ETX block */
    case ETX_RING:
        return s->etx_ring;
    case ETX_CFG:
        return s->etx_cfg;
    case ETX_RSIZE:
        return s->etx_rsize;
    case ETX_PENDING:
        return s->etx_pending;

    /* ERX block */
    case ERX_CFG:
        return s->erx_cfg;
    case ERX_RING:
        return s->erx_ring;

    /* BigMAC counters and config */
    case BMAC_XIFCFG:
        return s->bmac_xifcfg;
    case BMAC_TXSWRESET:
        return s->bmac_txcfg & 1; /* emulate reset bit as part of txcfg */
    case BMAC_RXSWRESET:
        return s->bmac_rxcfg & 1;
    case BMAC_TXCFG:
        return s->bmac_txcfg;
    case BMAC_RXCFG:
        return s->bmac_rxcfg;
    case BMAC_TXMAX:
        return s->bmac_txmax;
    case BMAC_JSIZE:
        return s->bmac_jsize;
    case BMAC_ALIMIT:
        return s->bmac_alimit;
    case BMAC_IGAP1:
        return s->bmac_igap1;
    case BMAC_IGAP2:
        return s->bmac_igap2;
    case BMAC_RSEED:
        return s->bmac_rseed;
    case BMAC_RXMAX:
        return s->bmac_rxmax;
    case BMAC_EXCTR: {
        uint32_t v = s->bmac_exctr; /* driver treats as counter, sometimes read only */
        return v;
    }
    case BMAC_LTCTR: {
        uint32_t v = s->bmac_ltctr;
        return v;
    }
    case BMAC_GLECTR: {
        uint32_t v = s->bmac_glectr;
        return v;
    }
    case BMAC_UNALECTR: {
        uint32_t v = s->bmac_unalectr;
        return v;
    }
    case BMAC_RCRCECTR: {
        uint32_t v = s->bmac_rcrcectr;
        return v;
    }
    case BMAC_MACADDR0:
        return s->bmac_macaddr0;
    case BMAC_MACADDR1:
        return s->bmac_macaddr1;
    case BMAC_MACADDR2:
        return s->bmac_macaddr2;
    case BMAC_HTABLE0:
        return s->bmac_htable0;
    case BMAC_HTABLE1:
        return s->bmac_htable1;
    case BMAC_HTABLE2:
        return s->bmac_htable2;
    case BMAC_HTABLE3:
        return s->bmac_htable3;

    /* Transceiver */
    case TCVR_CFG:
        return s->tcvr_cfg;
    case TCVR_FRAME:
        return s->tcvr_frame;
    case TCVR_BBDATA:
        return s->tcvr_bbdata;
    case TCVR_BBCLOCK:
        return s->tcvr_bbclock;
    case TCVR_BBOENAB:
        return s->tcvr_bboenab;

    default:
        return 0;
    }
}

static void pcibase_mmio_write32(PCIBaseState *s, hwaddr addr, uint32_t val)
{
    switch (addr) {
    /* Global regs */
    case GREG_SWRESET:
        s->greg_swreset = val;
        if (val & GREG_RESET_ALL) {
            /* emulate stop: clear core state */
            s->greg_stat = 0;
            s->etx_cfg &= ~ETX_CFG_DMAENABLE;
            s->erx_cfg &= ~ERX_CFG_DMAENABLE;
            s->bmac_txcfg &= ~BIGMAC_TXCFG_ENABLE;
            s->bmac_rxcfg &= ~BIGMAC_RXCFG_ENABLE;
        }
        break;
    case GREG_CFG:
        s->greg_cfg = val;
        break;
    case GREG_IMASK:
        s->greg_imask = val;
        pcibase_update_irq(s);
        break;
    case GREG_STAT:
        /* driver never writes here; ignore */
        break;

    /* ETX */
    case ETX_RING:
        s->etx_ring = val;
        break;
    case ETX_CFG:
        s->etx_cfg = val;
        break;
    case ETX_RSIZE:
        s->etx_rsize = val;
        break;
    case ETX_PENDING:
        s->etx_pending = val;
        if (val & ETX_TP_DMAWAKEUP) {
            /* transmit DMA kick; complete TX immediately */
            s->greg_stat |= GREG_STAT_TXALL;
            pcibase_update_irq(s);
        }
        break;

    /* ERX */
    case ERX_CFG:
        s->erx_cfg = val;
        break;
    case ERX_RING:
        s->erx_ring = val;
        break;

    /* BigMAC */
    case BMAC_XIFCFG:
        s->bmac_xifcfg = val;
        break;
    case BMAC_TXSWRESET:
        /* writing zero triggers reset, then bit clears when done */
        s->bmac_txcfg &= ~BIGMAC_TXCFG_ENABLE;
        break;
    case BMAC_RXSWRESET:
        s->bmac_rxcfg &= ~BIGMAC_RXCFG_ENABLE;
        break;
    case BMAC_TXCFG:
        s->bmac_txcfg = val;
        break;
    case BMAC_RXCFG:
        s->bmac_rxcfg = val;
        break;
    case BMAC_TXMAX:
        s->bmac_txmax = val;
        break;
    case BMAC_JSIZE:
        s->bmac_jsize = val;
        break;
    case BMAC_ALIMIT:
        s->bmac_alimit = val;
        break;
    case BMAC_IGAP1:
        s->bmac_igap1 = val;
        break;
    case BMAC_IGAP2:
        s->bmac_igap2 = val;
        break;
    case BMAC_RSEED:
        s->bmac_rseed = val;
        break;
    case BMAC_RXMAX:
        s->bmac_rxmax = val;
        break;
    case BMAC_EXCTR:
        s->bmac_exctr = 0;
        break;
    case BMAC_LTCTR:
        s->bmac_ltctr = 0;
        break;
    case BMAC_GLECTR:
        s->bmac_glectr = 0;
        break;
    case BMAC_UNALECTR:
        s->bmac_unalectr = 0;
        break;
    case BMAC_RCRCECTR:
        s->bmac_rcrcectr = 0;
        break;
    case BMAC_MACADDR0:
        s->bmac_macaddr0 = val;
        break;
    case BMAC_MACADDR1:
        s->bmac_macaddr1 = val;
        break;
    case BMAC_MACADDR2:
        s->bmac_macaddr2 = val;
        break;
    case BMAC_HTABLE0:
        s->bmac_htable0 = val;
        break;
    case BMAC_HTABLE1:
        s->bmac_htable1 = val;
        break;
    case BMAC_HTABLE2:
        s->bmac_htable2 = val;
        break;
    case BMAC_HTABLE3:
        s->bmac_htable3 = val;
        break;

    /* Transceiver block */
    case TCVR_CFG:
        s->tcvr_cfg = val;
        break;
    case TCVR_BBDATA:
        s->tcvr_bbdata = val & 1;
        break;
    case TCVR_BBCLOCK:
        /* Bit-bang interface toggles clock; we do not emulate PHY here. */
        s->tcvr_bbclock = val & 1;
        break;
    case TCVR_BBOENAB:
        s->tcvr_bboenab = val;
        break;
    case TCVR_FRAME:
        /* MII frame access: the driver uses this to talk to the PHY
         * when HFLAG_FENABLE is set. Format is FRAME_READ or FRAME_WRITE
         * plus PHY address and reg num; we only need to emulate completion
         * and provide plausible data for the limited set of regs accessed
         * in the driver.
         */
        s->tcvr_frame = val;
        if (val & FRAME_READ) {
            uint32_t reg = (val >> 18) & 0x1f;
            uint16_t rval = 0;
            switch (reg) {
            case 0: /* BMCR */
                rval = s->phy_bmcr;
                break;
            case 1: /* BMSR */
                rval = s->phy_bmsr;
                break;
            case 4: /* ADVERTISE */
                rval = s->phy_advertise;
                break;
            case 5: /* LPA */
                rval = s->phy_lpa;
                break;
            case 2: /* PHYSID1 */
                rval = s->phy_physid1;
                break;
            case 3: /* PHYSID2 */
                rval = s->phy_physid2;
                break;
            case 0x17: /* DP83840_CSCONFIG */
                /* Just return 0; driver sets bits it cares about. */
                rval = 0;
                break;
            default:
                rval = 0;
                break;
            }
            s->tcvr_frame = (val & ~0xffffu) | rval;
            /* set completion bit */
            s->tcvr_frame |= 0x00010000;
        } else if (val & FRAME_WRITE) {
            uint32_t reg = (val >> 18) & 0x1f;
            uint16_t wval = val & 0xffff;
            switch (reg) {
            case 0: /* BMCR */
                s->phy_bmcr = wval;
                break;
            case 4: /* ADVERTISE */
                s->phy_advertise = wval;
                break;
            case 0x17: /* DP83840_CSCONFIG */
                /* ignore */
                break;
            default:
                break;
            }
            /* complete write */
            s->tcvr_frame |= 0x00010000;
        }
        break;

    default:
        break;
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size == 4) {
        return pcibase_mmio_read32(s, addr);
    } else if (size == 2) {
        uint32_t v = pcibase_mmio_read32(s, addr & ~3u);
        if (addr & 2) {
            return (v >> 16) & 0xffff;
        } else {
            return v & 0xffff;
        }
    } else if (size == 1) {
        uint32_t v = pcibase_mmio_read32(s, addr & ~3u);
        unsigned shift = (addr & 3) * 8;
        return (v >> shift) & 0xff;
    } else {
        /* 8-byte and others: compose from two 32-bit accesses */
        uint64_t lo = pcibase_mmio_read32(s, addr);
        uint64_t hi = pcibase_mmio_read32(s, addr + 4);
        return lo | (hi << 32);
    }
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size == 4) {
        pcibase_mmio_write32(s, addr, (uint32_t)val);
    } else if (size == 2) {
        hwaddr base = addr & ~3u;
        uint32_t cur = pcibase_mmio_read32(s, base);
        uint32_t mask = 0xffff;
        unsigned shift = (addr & 2) ? 16 : 0;
        uint32_t nv = (cur & ~(mask << shift)) | (((uint32_t)val & mask) << shift);
        pcibase_mmio_write32(s, base, nv);
    } else if (size == 1) {
        hwaddr base = addr & ~3u;
        uint32_t cur = pcibase_mmio_read32(s, base);
        uint32_t mask = 0xff;
        unsigned shift = (addr & 3) * 8;
        uint32_t nv = (cur & ~(mask << shift)) | (((uint32_t)val & mask) << shift);
        pcibase_mmio_write32(s, base, nv);
    } else if (size == 8) {
        pcibase_mmio_write32(s, addr, (uint32_t)(val & 0xffffffffu));
        pcibase_mmio_write32(s, addr + 4, (uint32_t)(val >> 32));
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    (void)opaque;
    (void)addr;
    (void)size;
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    (void)opaque;
    (void)addr;
    (void)val;
    (void)size;
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

    s->greg_swreset = 0;
    s->greg_cfg = 0;
    s->greg_imask = 0;
    s->greg_stat = 0;

    s->bmac_xifcfg = 0;
    s->bmac_txcfg = 0;
    s->bmac_rxcfg = 0;
    s->bmac_txmax = 0;
    s->bmac_jsize = 0;
    s->bmac_alimit = 0;
    s->bmac_igap1 = 0;
    s->bmac_igap2 = 0;
    s->bmac_rseed = 0;
    s->bmac_rxmax = 0;
    s->bmac_exctr = 0;
    s->bmac_ltctr = 0;
    s->bmac_glectr = 0;
    s->bmac_unalectr = 0;
    s->bmac_rcrcectr = 0;
    s->bmac_macaddr0 = 0;
    s->bmac_macaddr1 = 0;
    s->bmac_macaddr2 = 0;
    s->bmac_htable0 = 0;
    s->bmac_htable1 = 0;
    s->bmac_htable2 = 0;
    s->bmac_htable3 = 0;

    s->erx_cfg = 0;
    s->erx_ring = 0;
    s->etx_ring = 0;
    s->etx_cfg = 0;
    s->etx_rsize = 0;
    s->etx_pending = 0;

    s->tcvr_cfg = 0;
    s->tcvr_frame = 0;
    s->tcvr_bbdata = 0;
    s->tcvr_bbclock = 0;
    s->tcvr_bboenab = 0;

    memset(s->macaddr, 0, sizeof(s->macaddr));

    s->intr_status = 0;
    s->intr_mask = 0;

    /* Initialize pseudo PHY with reasonable defaults so that
     * autoneg/link routines in driver see a valid PHY.
     */
    s->phy_bmcr = 0; /* no reset, 10Mbps half */
    /* BMSR: set capabilities and link up, autoneg complete */
    s->phy_bmsr = 0;
    /* These bits (values) are kernel MII constants; we only mirror
     * behavior conceptually: advertise all and show link up.
     */
    s->phy_bmsr |= (1 << 5);  /* arbitrary capability bit for 10HALF */
    s->phy_bmsr |= (1 << 6);  /* 10FULL */
    s->phy_bmsr |= (1 << 7);  /* 100HALF */
    s->phy_bmsr |= (1 << 8);  /* 100FULL */
    s->phy_bmsr |= (1 << 12); /* Autoneg capable */
    s->phy_bmsr |= (1 << 5);  /* reuse as link up flag surrogate */

    s->phy_advertise = 0x01e1; /* arbitrary: advertise common modes */
    s->phy_lpa = s->phy_advertise; /* partner similar */
    s->phy_physid1 = 0x2000; /* made-up but constant */
    s->phy_physid2 = 0x5c00; /* made-up but constant */
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  HME_PCI_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  HME_PCI_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, HME_PCI_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    s->bar_info[0].size  = 0x8000; /* driver maps 0x8000 bytes */
    s->bar_info[0].name  = "hme-mmio";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type  = BAR_TYPE_NONE;
        s->bar_info[i].size  = 0;
        s->bar_info[i].name  = NULL;
    }

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = false;
    s->has_msix = false;

    pcibase_reset(DEVICE(pdev));
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

static const VMStateDescription vmstate_pcibase = {
    .name = "hme_pci",
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
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
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
