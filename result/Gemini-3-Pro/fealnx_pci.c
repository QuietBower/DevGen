/* Generic QEMU PCI device template (QEMU 8.2.x) */

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

/* Related register/macros/constants from driver */
#define DRV_NAME	"fealnx"
#define MAX_UNITS 8
#define TX_RING_SIZE    6
#define RX_RING_SIZE    12
#define TX_TOTAL_SIZE	TX_RING_SIZE*sizeof(struct fealnx_desc)
#define RX_TOTAL_SIZE	RX_RING_SIZE*sizeof(struct fealnx_desc)
#define TX_TIMEOUT      (2*HZ)
#define PKT_BUF_SZ      1536
#define USE_IO_OPS

#define MASK_MIIR_MII_READ       0x00000000
#define MASK_MIIR_MII_WRITE      0x00000008
#define MASK_MIIR_MII_MDO        0x00000004
#define MASK_MIIR_MII_MDI        0x00000002
#define MASK_MIIR_MII_MDC        0x00000001
#define OP_READ             0x6000
#define OP_WRITE            0x5002
#define MysonPHYID      0xd0000302
#define MysonPHYID0     0x0302
#define StatusRegister  18
#define SPEED100        0x0400
#define FULLMODE        0x0800
#define SeeqPHYID0      0x0016
#define MIIRegister18   18
#define SPD_DET_100     0x80
#define DPLX_DET_FULL   0x40
#define AhdocPHYID0     0x0022
#define DiagnosticReg   18
#define DPLX_FULL       0x0800
#define Speed_100       0x0400
#define MarvellPHYID0           0x0141
#define LevelOnePHYID0		0x0013
#define MII1000BaseTControlReg  9
#define MII1000BaseTStatusReg   10
#define SpecificReg		17
#define PHYAbletoPerform1000FullDuplex  0x0200
#define PHYAbletoPerform1000HalfDuplex  0x0100
#define PHY1000AbilityMask              0x300
#define SpeedMask       0x0c000
#define Speed_1000M     0x08000
#define Speed_100M      0x4000
#define Speed_10M       0
#define Full_Duplex     0x2000
#define LXT1000_100M    0x08000
#define LXT1000_1000M   0x0c000
#define LXT1000_Full    0x200
#define LinkIsUp2	0x00040000
#define LinkIsUp        0x0004
#define one_buffer
#define BPT 1022

enum fealnx_offsets {
	PAR0 = 0x0,		/* physical address 0-3 */
	PAR1 = 0x04,		/* physical address 4-5 */
	MAR0 = 0x08,		/* multicast address 0-3 */
	MAR1 = 0x0C,		/* multicast address 4-7 */
	FAR0 = 0x10,		/* flow-control address 0-3 */
	FAR1 = 0x14,		/* flow-control address 4-5 */
	TCRRCR = 0x18,		/* receive & transmit configuration */
	BCR = 0x1C,		/* bus command */
	TXPDR = 0x20,		/* transmit polling demand */
	RXPDR = 0x24,		/* receive polling demand */
	RXCWP = 0x28,		/* receive current word pointer */
	TXLBA = 0x2C,		/* transmit list base address */
	RXLBA = 0x30,		/* receive list base address */
	ISR = 0x34,		/* interrupt status */
	IMR = 0x38,		/* interrupt mask */
	FTH = 0x3C,		/* flow control high/low threshold */
	MANAGEMENT = 0x40,	/* bootrom/eeprom and mii management */
	TALLY = 0x44,		/* tally counters for crc and mpa */
	TSR = 0x48,		/* tally counter for transmit status */
	BMCRSR = 0x4c,		/* basic mode control and status */
	PHYIDENTIFIER = 0x50,	/* phy identifier */
	ANARANLPAR = 0x54,	/* auto-negotiation advertisement and link
				   partner ability */
	ANEROCR = 0x58,		/* auto-negotiation expansion and pci conf. */
	BPREMRPSR = 0x5c,	/* bypass & receive error mask and phy status */
};

enum intr_status_bits {
	RFCON = 0x00020000,	/* receive flow control xon packet */
	RFCOFF = 0x00010000,	/* receive flow control xoff packet */
	LSCStatus = 0x00008000,	/* link status change */
	ANCStatus = 0x00004000,	/* autonegotiation completed */
	FBE = 0x00002000,	/* fatal bus error */
	FBEMask = 0x00001800,	/* mask bit12-11 */
	ParityErr = 0x00000000,	/* parity error */
	TargetErr = 0x00001000,	/* target abort */
	MasterErr = 0x00000800,	/* master error */
	TUNF = 0x00000400,	/* transmit underflow */
	ROVF = 0x00000200,	/* receive overflow */
	ETI = 0x00000100,	/* transmit early int */
	ERI = 0x00000080,	/* receive early int */
	CNTOVF = 0x00000040,	/* counter overflow */
	RBU = 0x00000020,	/* receive buffer unavailable */
	TBU = 0x00000010,	/* transmit buffer unavilable */
	TI = 0x00000008,	/* transmit interrupt */
	RI = 0x00000004,	/* receive interrupt */
	RxErr = 0x00000002,	/* receive error */
};

enum rx_mode_bits {
	CR_W_ENH	= 0x02000000,	/* enhanced mode (name?) */
	CR_W_FD		= 0x00100000,	/* full duplex */
	CR_W_PS10	= 0x00080000,	/* 10 mbit */
	CR_W_TXEN	= 0x00040000,	/* tx enable (name?) */
	CR_W_PS1000	= 0x00010000,	/* 1000 mbit */
	CR_W_RXMODEMASK	= 0x000000e0,
	CR_W_PROM	= 0x00000080,	/* promiscuous mode */
	CR_W_AB		= 0x00000040,	/* accept broadcast */
	CR_W_AM		= 0x00000020,	/* accept mutlicast */
	CR_W_ARP	= 0x00000008,	/* receive runt pkt */
	CR_W_ALP	= 0x00000004,	/* receive long pkt */
	CR_W_SEP	= 0x00000002,	/* receive error pkt */
	CR_W_RXEN	= 0x00000001,	/* rx enable (unicast?) (name?) */

	CR_R_TXSTOP	= 0x04000000,	/* tx stopped (name?) */
	CR_R_FD		= 0x00100000,	/* full duplex detected */
	CR_R_PS10	= 0x00080000,	/* 10 mbit detected */
	CR_R_RXSTOP	= 0x00008000,	/* rx stopped (name?) */
};

enum rx_desc_status_bits {
	RXOWN = 0x80000000,	/* own bit */
	FLNGMASK = 0x0fff0000,	/* frame length */
	FLNGShift = 16,
	MARSTATUS = 0x00004000,	/* multicast address received */
	BARSTATUS = 0x00002000,	/* broadcast address received */
	PHYSTATUS = 0x00001000,	/* physical address received */
	RXFSD = 0x00000800,	/* first descriptor */
	RXLSD = 0x00000400,	/* last descriptor */
	ErrorSummary = 0x80,	/* error summary */
	RUNTPKT = 0x40,		/* runt packet received */
	LONGPKT = 0x20,		/* long packet received */
	FAE = 0x10,		/* frame align error */
	CRC = 0x08,		/* crc error */
	RXER = 0x04,		/* receive error */
};

enum rx_desc_control_bits {
	RXIC = 0x00800000,	/* interrupt control */
	RBSShift = 0,
};

enum tx_desc_status_bits {
	TXOWN = 0x80000000,	/* own bit */
	JABTO = 0x00004000,	/* jabber timeout */
	CSL = 0x00002000,	/* carrier sense lost */
	LC = 0x00001000,	/* late collision */
	EC = 0x00000800,	/* excessive collision */
	UDF = 0x00000400,	/* fifo underflow */
	DFR = 0x00000200,	/* deferred */
	HF = 0x00000100,	/* heartbeat fail */
	NCRMask = 0x000000ff,	/* collision retry count */
	NCRShift = 0,
};

enum tx_desc_control_bits {
	TXIC = 0x80000000,	/* interrupt control */
	ETIControl = 0x40000000,	/* early transmit interrupt */
	TXLD = 0x20000000,	/* last descriptor */
	TXFD = 0x10000000,	/* first descriptor */
	CRCEnable = 0x08000000,	/* crc control */
	PADEnable = 0x04000000,	/* padding control */
	RetryTxLC = 0x02000000,	/* retry late collision */
	PKTSMask = 0x3ff800,	/* packet size bit21-11 */
	PKTSShift = 11,
	TBSMask = 0x000007ff,	/* transmit buffer bit 10-0 */
	TBSShift = 0,
};

struct fealnx_desc {
	int32_t status;
	int32_t control;
	uint32_t buffer;
	uint32_t next_desc;
	uint32_t next_desc_logical; /* struct fealnx_desc *next_desc_logical; */
	uint32_t skbuff;            /* struct sk_buff *skbuff; */
	uint32_t reserved1;
	uint32_t reserved2;
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

    /* BAR memory regions */
    MemoryRegion bar_regions[6];

    /* BAR table */
    BARInfo bar_info[6];
    int num_bars;

    /* Register Shadows */
    uint8_t mac_reg[6];      /* PAR0, PAR1 */
    uint8_t mar_reg[8];      /* MAR0, MAR1 */
    uint32_t far0;           /* FAR0 */
    uint32_t far1;           /* FAR1 */
    uint32_t tcrrcr;         /* TCRRCR */
    uint32_t bcr;            /* BCR */
    uint32_t txpdr;          /* TXPDR */
    uint32_t rxpdr;          /* RXPDR */
    uint32_t rxcwp;          /* RXCWP */
    uint32_t txlba;          /* TXLBA */
    uint32_t rxlba;          /* RXLBA */
    uint32_t isr;            /* ISR */
    uint32_t imr;            /* IMR */
    uint32_t fth;            /* FTH */
    uint32_t management;     /* MANAGEMENT */
    uint32_t tally;          /* TALLY */
    uint32_t tsr;            /* TSR */
    uint32_t bmcrsr;         /* BMCRSR */
    uint32_t phy_identifier; /* PHYIDENTIFIER */
    uint32_t anaranlpar;     /* ANARANLPAR */
    uint32_t anerocr;        /* ANEROCR */
    uint32_t bpremrsr;       /* BPREMRPSR */

    /* DMA State */
    /* Placeholders for DMA state */

    /* Other fields */
    qemu_irq irq;
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
    }else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, bi->size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

/* ------------------------------------------------------------------ */
/* Internal Helpers                                                    */
/* ------------------------------------------------------------------ */
static void fealnx_update_irq(PCIBaseState *s)
{
    int level = (s->isr & s->imr) ? 1 : 0;
    pci_set_irq(PCI_DEVICE(s), level);
}

static uint64_t fealnx_read_mac(PCIBaseState *s, hwaddr addr, unsigned size)
{
    uint64_t val = 0;
    for (unsigned i = 0; i < size; i++) {
        if (addr + i < 6) {
            val |= (uint64_t)s->mac_reg[addr + i] << (i * 8);
        }
    }
    return val;
}

static void fealnx_write_mac(PCIBaseState *s, hwaddr addr, uint64_t val, unsigned size)
{
    for (unsigned i = 0; i < size; i++) {
        if (addr + i < 6) {
            s->mac_reg[addr + i] = (val >> (i * 8)) & 0xFF;
        }
    }
}

static void fealnx_process_tx(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t desc_addr = s->txlba;
    struct fealnx_desc desc;
    int processed = 0;

    while (1) {
        if (pci_dma_read(pdev, desc_addr, &desc, 16)) {
            break;
        }

        if (!(desc.status & TXOWN)) {
            /* Owned by CPU, stop */
            break;
        }

        /* Process packet (stub: just mark done) */
        desc.status &= ~TXOWN;
        
        /* Write back status */
        pci_dma_write(pdev, desc_addr, &desc, 4);

        /* Raise TI interrupt */
        s->isr |= TI;
        
        /* Move to next descriptor */
        desc_addr = desc.next_desc;
        s->txlba = desc_addr;
        processed++;

        if (processed > TX_RING_SIZE) break; /* Safety break */
    }

    if (processed) {
        fealnx_update_irq(s);
    }
}

/* ------------------------------------------------------------------ */
/* MMIO / PIO handlers (device-specific code goes into placeholders)   */
/* ------------------------------------------------------------------ */

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* MMIO is not used if USE_IO_OPS is defined, but we map it to PIO logic just in case */
    return pcibase_pio_read(opaque, addr, size);
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    pcibase_pio_write(opaque, addr, val, size);
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case PAR0 ... PAR0 + 3:
        val = fealnx_read_mac(s, addr - PAR0, size);
        break;
    case PAR1 ... PAR1 + 3:
        val = fealnx_read_mac(s, addr - PAR0, size);
        break;
    case MAR0 ... MAR0 + 3:
        /* Multicast registers not fully implemented, return 0 */
        break;
    case MAR1 ... MAR1 + 3:
        break;
    case FAR0:
        val = s->far0;
        break;
    case FAR1:
        val = s->far1;
        break;
    case TCRRCR:
        val = s->tcrrcr;
        /* Force Full Duplex and 100Mbps bits for 3-in-1 detection */
        val |= CR_R_FD;
        val &= ~CR_R_PS10;
        break;
    case BCR:
        val = s->bcr;
        break;
    case TXPDR:
        val = s->txpdr;
        break;
    case RXPDR:
        val = s->rxpdr;
        break;
    case RXCWP:
        val = s->rxcwp;
        break;
    case TXLBA:
        val = s->txlba;
        break;
    case RXLBA:
        val = s->rxlba;
        break;
    case ISR:
        val = s->isr;
        break;
    case IMR:
        val = s->imr;
        break;
    case FTH:
        val = s->fth;
        break;
    case MANAGEMENT:
        val = s->management;
        break;
    case TALLY:
        val = s->tally;
        break;
    case TSR:
        val = s->tsr;
        break;
    case BMCRSR:
        val = s->bmcrsr;
        /* Set LinkIsUp2 to simulate link up */
        val |= LinkIsUp2;
        break;
    case PHYIDENTIFIER:
        val = s->phy_identifier;
        break;
    case ANARANLPAR:
        val = s->anaranlpar;
        break;
    case ANEROCR:
        val = s->anerocr;
        break;
    case BPREMRPSR:
        val = s->bpremrsr;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "[%s] pio_read addr=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
        break;
    }
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case PAR0 ... PAR0 + 3:
        fealnx_write_mac(s, addr - PAR0, val, size);
        break;
    case PAR1 ... PAR1 + 3:
        fealnx_write_mac(s, addr - PAR0, val, size);
        break;
    case MAR0 ... MAR0 + 3:
        break;
    case MAR1 ... MAR1 + 3:
        break;
    case FAR0:
        s->far0 = val;
        break;
    case FAR1:
        s->far1 = val;
        break;
    case TCRRCR:
        s->tcrrcr = val;
        break;
    case BCR:
        s->bcr = val;
        if (val & 1) {
            /* Reset */
            pcibase_reset(DEVICE(s));
        }
        break;
    case TXPDR:
        s->txpdr = val;
        /* Trigger TX processing */
        fealnx_process_tx(s);
        break;
    case RXPDR:
        s->rxpdr = val;
        break;
    case RXCWP:
        s->rxcwp = val;
        break;
    case TXLBA:
        s->txlba = val;
        break;
    case RXLBA:
        s->rxlba = val;
        break;
    case ISR:
        /* Write 1 to clear bits */
        s->isr &= ~val;
        fealnx_update_irq(s);
        break;
    case IMR:
        s->imr = val;
        fealnx_update_irq(s);
        break;
    case FTH:
        s->fth = val;
        break;
    case MANAGEMENT:
        s->management = val;
        break;
    case TALLY:
        s->tally = val;
        break;
    case TSR:
        s->tsr = val;
        break;
    case BMCRSR:
        s->bmcrsr = val;
        break;
    case PHYIDENTIFIER:
        /* Read only usually, but allow write for testing if needed */
        break;
    case ANARANLPAR:
        s->anaranlpar = val;
        break;
    case ANEROCR:
        s->anerocr = val;
        break;
    case BPREMRPSR:
        s->bpremrsr = val;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "[%s] pio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, (uint64_t)val, size);
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Reset       */
/* ------------------------------------------------------------------ */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    /* core resets */
    pci_device_reset(pdev);

    /* Reset registers */
    /* Default MAC: 52:54:00:12:34:56 */
    s->mac_reg[0] = 0x52;
    s->mac_reg[1] = 0x54;
    s->mac_reg[2] = 0x00;
    s->mac_reg[3] = 0x12;
    s->mac_reg[4] = 0x34;
    s->mac_reg[5] = 0x56;

    memset(s->mar_reg, 0, sizeof(s->mar_reg));
    s->far0 = 0;
    s->far1 = 0;
    s->tcrrcr = 0;
    s->bcr = 0x10; /* Default burst length */
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
    s->phy_identifier = MysonPHYID;
    s->anaranlpar = 0;
    s->anerocr = 0;
    s->bpremrsr = 0;
    
    fealnx_update_irq(s);
}

/* ------------------------------------------------------------------ */
/* DMA initialize (called from realize)                                */
/* ------------------------------------------------------------------ */
static void pcibase_dma_device_realize(PCIDevice *pdev, Error **errp)
{
    /* No explicit DMA init needed for PCI bus master in QEMU 8.2 */
}

/* ------------------------------------------------------------------ */
/* PCI config space access (class-level hooks)                         */
/* ------------------------------------------------------------------ */
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    uint32_t val = pci_default_read_config(pdev, addr, len);
    return val;
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x1516 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x0800 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_ETHERNET );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Define BAR 0 as PIO based on USE_IO_OPS */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 0x100; /* Standard size for Myson/Fealnx */
    s->bar_info[0].name = "fealnx-pio";

    /* register BARs */
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* DMA init */
    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) return;

    pcibase_reset(DEVICE(s));
    qemu_log_mask(LOG_UNIMP, "[%s] device realized\n", TYPE_PCIBASE_DEVICE);
}

/* ------------------------------------------------------------------ */
/* Uninit/cleanup                                                      */
/* ------------------------------------------------------------------ */
static void pcibase_uninit(PCIDevice *pdev)
{
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