/*
 * QEMU fealnx-compatible PCI Ethernet device (minimal behavior)
 *
 * Generated for Linux drivers/net/ethernet/fealnx.c
 * Target QEMU: 8.2.10
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

#define TYPE_PCIBASE_DEVICE "fealnx_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define FEALNX_VENDOR_ID 0x1516
#define FEALNX_DEVICE_ID 0x0800

#define FEALNX_MAX_UNITS            8
#define FEALNX_TX_RING_SIZE         6
#define FEALNX_RX_RING_SIZE         12
#define FEALNX_PKT_BUF_SZ           1536

#define MASK_MIIR_MII_READ          0x00000000
#define MASK_MIIR_MII_WRITE         0x00000008
#define MASK_MIIR_MII_MDO           0x00000004
#define MASK_MIIR_MII_MDI           0x00000002
#define MASK_MIIR_MII_MDC           0x00000001

#define FEALNX_OP_READ              0x6000
#define FEALNX_OP_WRITE             0x5002
#define FEALNX_MYSON_PHYID          0xd0000302
#define FEALNX_MYSON_PHYID0         0x0302
#define FEALNX_STATUS_REGISTER      18
#define FEALNX_SPEED100             0x0400
#define FEALNX_FULLMODE             0x0800
#define FEALNX_SEEQ_PHYID0          0x0016
#define FEALNX_MII_REGISTER18       18
#define FEALNX_SPD_DET_100          0x80
#define FEALNX_DPLX_DET_FULL        0x40
#define FEALNX_AHDOC_PHYID0         0x0022
#define FEALNX_DIAGNOSTIC_REG       18
#define FEALNX_DPLX_FULL            0x0800
#define FEALNX_SPEED_100            0x0400
#define FEALNX_MARVELL_PHYID0       0x0141
#define FEALNX_LEVELONE_PHYID0      0x0013
#define FEALNX_MII_1000BASE_T_CTRL  9
#define FEALNX_MII_1000BASE_T_STAT  10
#define FEALNX_SPECIFIC_REG         17
#define FEALNX_PHY_1000_FULL        0x0200
#define FEALNX_PHY_1000_HALF        0x0100
#define FEALNX_PHY_1000_ABILITY_MASK 0x300
#define FEALNX_SPEED_MASK           0x0c000
#define FEALNX_SPEED_1000M          0x08000
#define FEALNX_SPEED_100M           0x4000
#define FEALNX_SPEED_10M            0x00000
#define FEALNX_FULL_DUPLEX          0x2000
#define FEALNX_LXT1000_100M         0x08000
#define FEALNX_LXT1000_1000M        0x0c000
#define FEALNX_LXT1000_FULL         0x0200
#define FEALNX_LINK_IS_UP2          0x00040000
#define FEALNX_LINK_IS_UP           0x0004
#define FEALNX_BPT                  1022

/* Register offsets (from driver enum, renamed to match) */
enum fealnx_offsets {
    FEALNX_PAR0          = 0x00,
    FEALNX_PAR1          = 0x04,
    FEALNX_MAR0          = 0x08,
    FEALNX_MAR1          = 0x0C,
    FEALNX_FAR0          = 0x10,
    FEALNX_FAR1          = 0x14,
    FEALNX_TCRRCR        = 0x18,
    FEALNX_BCR           = 0x1C,
    FEALNX_TXPDR         = 0x20,
    FEALNX_RXPDR         = 0x24,
    FEALNX_RXCWP         = 0x28,
    FEALNX_TXLBA         = 0x2C,
    FEALNX_RXLBA         = 0x30,
    FEALNX_ISR           = 0x34,
    FEALNX_IMR           = 0x38,
    FEALNX_FTH           = 0x3C,
    FEALNX_MANAGEMENT    = 0x40,
    FEALNX_TALLY         = 0x44,
    FEALNX_TSR           = 0x48,
    FEALNX_BMCRSR        = 0x4C,
    FEALNX_PHYIDENTIFIER = 0x50,
    FEALNX_ANARANLPAR    = 0x54,
    FEALNX_ANEROCR       = 0x58,
    FEALNX_BPREMRPSR     = 0x5C,
};

/* Interrupt status bits */
enum fealnx_intr_status_bits {
    FEALNX_RFCON      = 0x00020000,
    FEALNX_RFCOFF     = 0x00010000,
    FEALNX_LSCSTATUS  = 0x00008000,
    FEALNX_ANCSTATUS  = 0x00004000,
    FEALNX_FBE        = 0x00002000,
    FEALNX_FBEMASK    = 0x00001800,
    FEALNX_PARITYERR  = 0x00000000,
    FEALNX_TARGETERR  = 0x00001000,
    FEALNX_MASTERERR  = 0x00000800,
    FEALNX_TUNF       = 0x00000400,
    FEALNX_ROVF       = 0x00000200,
    FEALNX_ETI        = 0x00000100,
    FEALNX_ERI        = 0x00000080,
    FEALNX_CNTOVF     = 0x00000040,
    FEALNX_RBU        = 0x00000020,
    FEALNX_TBU        = 0x00000010,
    FEALNX_TI         = 0x00000008,
    FEALNX_RI         = 0x00000004,
    FEALNX_RXERR      = 0x00000002,
};

/* RX mode bits */
enum fealnx_rx_mode_bits {
    FEALNX_CR_W_ENH        = 0x02000000,
    FEALNX_CR_W_FD         = 0x00100000,
    FEALNX_CR_W_PS10       = 0x00080000,
    FEALNX_CR_W_TXEN       = 0x00040000,
    FEALNX_CR_W_PS1000     = 0x00010000,
    FEALNX_CR_W_RXMODEMASK = 0x000000e0,
    FEALNX_CR_W_PROM       = 0x00000080,
    FEALNX_CR_W_AB         = 0x00000040,
    FEALNX_CR_W_AM         = 0x00000020,
    FEALNX_CR_W_ARP        = 0x00000008,
    FEALNX_CR_W_ALP        = 0x00000004,
    FEALNX_CR_W_SEP        = 0x00000002,
    FEALNX_CR_W_RXEN       = 0x00000001,

    FEALNX_CR_R_TXSTOP     = 0x04000000,
    FEALNX_CR_R_FD         = 0x00100000,
    FEALNX_CR_R_PS10       = 0x00080000,
    FEALNX_CR_R_RXSTOP     = 0x00008000,
};

/* RX descriptor status bits */
enum fealnx_rx_desc_status_bits {
    FEALNX_RXOWN       = 0x80000000,
    FEALNX_FLNGMASK    = 0x0fff0000,
    FEALNX_FLNGSHIFT   = 16,
    FEALNX_MARSTATUS   = 0x00004000,
    FEALNX_BARSTATUS   = 0x00002000,
    FEALNX_PHYSTATUS   = 0x00001000,
    FEALNX_RXFSD       = 0x00000800,
    FEALNX_RXLSD       = 0x00000400,
    FEALNX_ERRORSUMMARY= 0x00000080,
    FEALNX_RUNTPKT     = 0x00000040,
    FEALNX_LONGPKT     = 0x00000020,
    FEALNX_FAE         = 0x00000010,
    FEALNX_CRC         = 0x00000008,
    FEALNX_RXER        = 0x00000004,
};

/* RX descriptor control bits */
enum fealnx_rx_desc_control_bits {
    FEALNX_RXIC      = 0x00800000,
    FEALNX_RBSSHIFT  = 0,
};

/* TX descriptor status bits */
enum fealnx_tx_desc_status_bits {
    FEALNX_TXOWN     = 0x80000000,
    FEALNX_JABTO     = 0x00004000,
    FEALNX_CSL       = 0x00002000,
    FEALNX_LC        = 0x00001000,
    FEALNX_EC        = 0x00000800,
    FEALNX_UDF       = 0x00000400,
    FEALNX_DFR       = 0x00000200,
    FEALNX_HF        = 0x00000100,
    FEALNX_NCRMASK   = 0x000000ff,
    FEALNX_NCRSHIFT  = 0,
};

/* TX descriptor control bits */
enum fealnx_tx_desc_control_bits {
    FEALNX_TXIC        = 0x80000000,
    FEALNX_ETICONTROL  = 0x40000000,
    FEALNX_TXLD        = 0x20000000,
    FEALNX_TXFD        = 0x10000000,
    FEALNX_CRCENABLE   = 0x08000000,
    FEALNX_PADENABLE   = 0x04000000,
    FEALNX_RETRYTXLC   = 0x02000000,
    FEALNX_PKTSMASK    = 0x003ff800,
    FEALNX_PKTSSHIFT   = 11,
    FEALNX_TBSMASK     = 0x000007ff,
    FEALNX_TBSSHIFT    = 0,
};

/* Chip capability flags */
enum fealnx_chip_capability_flags {
    FEALNX_HAS_MII_XCVR  = 0,
    FEALNX_HAS_CHIP_XCVR = 1,
};

/* PHY type flags */
enum fealnx_phy_type_flags {
    FEALNX_PHY_MYSON      = 1,
    FEALNX_PHY_AHDOC      = 2,
    FEALNX_PHY_SEEQ       = 3,
    FEALNX_PHY_MARVELL    = 4,
    FEALNX_PHY_MYSON981   = 5,
    FEALNX_PHY_LEVELONE   = 6,
    FEALNX_PHY_OTHER      = 10,
};

struct fealnx_chip_info {
    char *chip_name;
    int flags;
};

struct fealnx_desc {
    int32_t status;
    int32_t control;
    uint32_t buffer;
    uint32_t next_desc;
    struct fealnx_desc *next_desc_logical;
    void *skbuff;
    uint32_t reserved1;
    uint32_t reserved2;
};

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

    /* fealnx specific state */
    uint32_t par0;
    uint32_t par1;
    uint32_t mar0;
    uint32_t mar1;
    uint32_t far0;
    uint32_t far1;
    uint32_t tcrrcr;
    uint32_t bcr;
    uint32_t txpdr;
    uint32_t rxpdr;
    uint32_t rxcwp;
    uint32_t txlba;
    uint32_t rxlba;
    uint32_t isr;
    uint32_t imr;
    uint32_t fth;
    uint32_t management;
    uint32_t tally;
    uint32_t tsr;
    uint32_t bmcrsr;
    uint32_t phyidentifier;
    uint32_t anaranlpar;
    uint32_t anerocr;
    uint32_t bpremrpsr;

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

static void fealnx_update_irq(PCIBaseState *s)
{
    if (s->isr & s->imr) {
        qemu_irq_raise(s->irq);
    } else {
        qemu_irq_lower(s->irq);
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = 0;

    switch (addr) {
    case FEALNX_PAR0:
        val = s->par0;
        break;
    case FEALNX_PAR1:
        val = s->par1;
        break;
    case FEALNX_MAR0:
        val = s->mar0;
        break;
    case FEALNX_MAR1:
        val = s->mar1;
        break;
    case FEALNX_FAR0:
        val = s->far0;
        break;
    case FEALNX_FAR1:
        val = s->far1;
        break;
    case FEALNX_TCRRCR:
        val = s->tcrrcr;
        break;
    case FEALNX_BCR:
        val = s->bcr;
        break;
    case FEALNX_TXPDR:
        val = s->txpdr;
        break;
    case FEALNX_RXPDR:
        val = s->rxpdr;
        break;
    case FEALNX_RXCWP:
        val = s->rxcwp;
        break;
    case FEALNX_TXLBA:
        val = s->txlba;
        break;
    case FEALNX_RXLBA:
        val = s->rxlba;
        break;
    case FEALNX_ISR:
        val = s->isr;
        break;
    case FEALNX_IMR:
        val = s->imr;
        break;
    case FEALNX_FTH:
        val = s->fth;
        break;
    case FEALNX_MANAGEMENT:
        val = s->management;
        break;
    case FEALNX_TALLY:
        val = s->tally;
        break;
    case FEALNX_TSR:
        val = s->tsr;
        break;
    case FEALNX_BMCRSR:
        val = s->bmcrsr;
        break;
    case FEALNX_PHYIDENTIFIER:
        val = s->phyidentifier;
        break;
    case FEALNX_ANARANLPAR:
        val = s->anaranlpar;
        break;
    case FEALNX_ANEROCR:
        val = s->anerocr;
        break;
    case FEALNX_BPREMRPSR:
        val = s->bpremrpsr;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "[fealnx] mmio_read addr=%" PRIx64 " size=%u\n", (uint64_t)addr, size);
        break;
    }

    if (size == 1) {
        uint32_t shift = (addr & 3) * 8;
        val = (val >> shift) & 0xff;
    } else if (size == 2) {
        uint32_t shift = (addr & 2) * 8;
        val = (val >> shift) & 0xffff;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t v = val;

    if (size == 1) {
        uint32_t *regp = NULL;
        uint32_t shift = (addr & 3) * 8;
        uint32_t mask = 0xffu << shift;
        switch (addr & ~3u) {
        case FEALNX_PAR0: regp = &s->par0; break;
        case FEALNX_PAR1: regp = &s->par1; break;
        case FEALNX_MAR0: regp = &s->mar0; break;
        case FEALNX_MAR1: regp = &s->mar1; break;
        case FEALNX_FAR0: regp = &s->far0; break;
        case FEALNX_FAR1: regp = &s->far1; break;
        case FEALNX_TCRRCR: regp = &s->tcrrcr; break;
        case FEALNX_BCR: regp = &s->bcr; break;
        case FEALNX_TXPDR: regp = &s->txpdr; break;
        case FEALNX_RXPDR: regp = &s->rxpdr; break;
        case FEALNX_RXCWP: regp = &s->rxcwp; break;
        case FEALNX_TXLBA: regp = &s->txlba; break;
        case FEALNX_RXLBA: regp = &s->rxlba; break;
        case FEALNX_ISR: regp = &s->isr; break;
        case FEALNX_IMR: regp = &s->imr; break;
        case FEALNX_FTH: regp = &s->fth; break;
        case FEALNX_MANAGEMENT: regp = &s->management; break;
        case FEALNX_TALLY: regp = &s->tally; break;
        case FEALNX_TSR: regp = &s->tsr; break;
        case FEALNX_BMCRSR: regp = &s->bmcrsr; break;
        case FEALNX_PHYIDENTIFIER: regp = &s->phyidentifier; break;
        case FEALNX_ANARANLPAR: regp = &s->anaranlpar; break;
        case FEALNX_ANEROCR: regp = &s->anerocr; break;
        case FEALNX_BPREMRPSR: regp = &s->bpremrpsr; break;
        default:
            qemu_log_mask(LOG_UNIMP, "[fealnx] mmio_write8 addr=%" PRIx64 " val=%" PRIx64 "\n", (uint64_t)addr, val);
            return;
        }
        *regp = (*regp & ~mask) | ((v & 0xffu) << shift);
    } else if (size == 2) {
        uint32_t *regp = NULL;
        uint32_t shift = (addr & 2) * 8;
        uint32_t mask = 0xffffu << shift;
        switch (addr & ~3u) {
        case FEALNX_PAR0: regp = &s->par0; break;
        case FEALNX_PAR1: regp = &s->par1; break;
        case FEALNX_MAR0: regp = &s->mar0; break;
        case FEALNX_MAR1: regp = &s->mar1; break;
        case FEALNX_FAR0: regp = &s->far0; break;
        case FEALNX_FAR1: regp = &s->far1; break;
        case FEALNX_TCRRCR: regp = &s->tcrrcr; break;
        case FEALNX_BCR: regp = &s->bcr; break;
        case FEALNX_TXPDR: regp = &s->txpdr; break;
        case FEALNX_RXPDR: regp = &s->rxpdr; break;
        case FEALNX_RXCWP: regp = &s->rxcwp; break;
        case FEALNX_TXLBA: regp = &s->txlba; break;
        case FEALNX_RXLBA: regp = &s->rxlba; break;
        case FEALNX_ISR: regp = &s->isr; break;
        case FEALNX_IMR: regp = &s->imr; break;
        case FEALNX_FTH: regp = &s->fth; break;
        case FEALNX_MANAGEMENT: regp = &s->management; break;
        case FEALNX_TALLY: regp = &s->tally; break;
        case FEALNX_TSR: regp = &s->tsr; break;
        case FEALNX_BMCRSR: regp = &s->bmcrsr; break;
        case FEALNX_PHYIDENTIFIER: regp = &s->phyidentifier; break;
        case FEALNX_ANARANLPAR: regp = &s->anaranlpar; break;
        case FEALNX_ANEROCR: regp = &s->anerocr; break;
        case FEALNX_BPREMRPSR: regp = &s->bpremrpsr; break;
        default:
            qemu_log_mask(LOG_UNIMP, "[fealnx] mmio_write16 addr=%" PRIx64 " val=%" PRIx64 "\n", (uint64_t)addr, val);
            return;
        }
        *regp = (*regp & ~mask) | ((v & 0xffffu) << shift);
    } else {
        switch (addr) {
        case FEALNX_PAR0:
            s->par0 = v;
            break;
        case FEALNX_PAR1:
            s->par1 = v;
            break;
        case FEALNX_MAR0:
            s->mar0 = v;
            break;
        case FEALNX_MAR1:
            s->mar1 = v;
            break;
        case FEALNX_FAR0:
            s->far0 = v;
            break;
        case FEALNX_FAR1:
            s->far1 = v;
            break;
        case FEALNX_TCRRCR:
            s->tcrrcr = v;
            break;
        case FEALNX_BCR:
            /* BCR reset bit write; driver writes 0x1 to reset */
            s->bcr = v;
            break;
        case FEALNX_TXPDR:
            s->txpdr = v;
            break;
        case FEALNX_RXPDR:
            s->rxpdr = v;
            break;
        case FEALNX_RXCWP:
            s->rxcwp = v;
            break;
        case FEALNX_TXLBA:
            s->txlba = v;
            break;
        case FEALNX_RXLBA:
            s->rxlba = v;
            break;
        case FEALNX_ISR:
            /* driver writes interrupt bits to acknowledge */
            s->isr &= ~v;
            fealnx_update_irq(s);
            break;
        case FEALNX_IMR:
            s->imr = v;
            fealnx_update_irq(s);
            break;
        case FEALNX_FTH:
            s->fth = v;
            break;
        case FEALNX_MANAGEMENT:
            s->management = v;
            break;
        case FEALNX_TALLY:
            /* tally counters are read-only in driver usage; ignore writes */
            break;
        case FEALNX_TSR:
            s->tsr = v;
            break;
        case FEALNX_BMCRSR:
            s->bmcrsr = v;
            break;
        case FEALNX_PHYIDENTIFIER:
            s->phyidentifier = v;
            break;
        case FEALNX_ANARANLPAR:
            s->anaranlpar = v;
            break;
        case FEALNX_ANEROCR:
            s->anerocr = v;
            break;
        case FEALNX_BPREMRPSR:
            s->bpremrpsr = v;
            break;
        default:
            qemu_log_mask(LOG_UNIMP, "[fealnx] mmio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n", (uint64_t)addr, (uint64_t)val, size);
            break;
        }
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "[fealnx] pio_read addr=%" PRIx64 " size=%u\n", (uint64_t)addr, size);
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "[fealnx] pio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n", (uint64_t)addr, (uint64_t)val, size);
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    pci_device_reset(pdev);

    s->par0 = 0;
    s->par1 = 0;
    s->mar0 = 0;
    s->mar1 = 0;
    s->far0 = 0;
    s->far1 = 0;
    s->tcrrcr = 0;
    s->bcr = 0;
    s->txpdr = 0;
    s->rxpdr = 0;
    s->rxcwp = 0;
    s->txlba = 0;
    s->rxlba = 0;
    s->isr = 0;
    s->imr = 0;
    s->fth = 0;
    s->management = 0;
    s->tally = 0;
    s->tsr = 0;
    s->bmcrsr = 0;
    s->phyidentifier = FEALNX_MYSON_PHYID;
    s->anaranlpar = 0;
    s->anerocr = 0;
    s->bpremrpsr = 0;
}

static void pcibase_dma_device_realize(PCIDevice *pdev, Error **errp)
{
    (void)pdev;
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

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, FEALNX_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, FEALNX_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_ETHERNET);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* layout: BAR0 IO (256 bytes), BAR1 MMIO (256 bytes) */
    s->num_bars = 2;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 0x100; /* 256 bytes to satisfy driver region size check */
    s->bar_info[0].name = "fealnx-io";
    s->bar_info[0].sparse = false;

    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = 0x100; /* 256 bytes, includes all regs up to 0x5C */
    s->bar_info[1].name = "fealnx-mmio";
    s->bar_info[1].sparse = false;

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
        if (errp && *errp) {
            return;
        }
    }

    s->irq = pci_allocate_irq(pdev);

    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) {
        return;
    }
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

