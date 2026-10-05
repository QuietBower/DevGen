/*
 * QEMU FEALNX PCI Ethernet Device Emulation
 *
 * Based on Linux driver: /linux-6.18/drivers/net/ethernet/fealnx.c
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

/* Related register/macros/constants from driver (Stage1) */
#define VENDOR_ID 0x1516
#define DEVICE_ID 0x0800
#define CLASS_ID   PCI_CLASS_NETWORK_ETHERNET
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
#define BPT 1022
enum chip_capability_flags {
	HAS_MII_XCVR,
	HAS_CHIP_XCVR,
};
enum phy_type_flags {
	MysonPHY = 1,
	AhdocPHY = 2,
	SeeqPHY = 3,
	MarvellPHY = 4,
	Myson981 = 5,
	LevelOnePHY = 6,
	OtherPHY = 10,
};
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
     /* CR_W_RXBURSTMASK= 0x00000e00, Im unsure about this */
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
struct chip_info {
	char *chip_name;
	int flags;
};
/* Removed struct fealnx_desc as it contains Linux kernel types that are not needed for QEMU compilation */

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
    
    /* DMA info placeholder */
    uint32_t rx_ring_dma;
    uint32_t tx_ring_dma;
    
    /* Register shadows */
    uint32_t tcr_rcr;
    uint32_t bcr;
    uint32_t isr;
    uint32_t imr;
    uint32_t crvalue;
    uint32_t bcrvalue;
    uint32_t imrvalue;
    uint32_t phy_identifier;
    uint32_t tally;
    uint32_t tsr;
    uint32_t bmcrsr;
    uint32_t anaranlpar;
    uint32_t anerocr;
    uint32_t bpremrpsr;
    uint8_t mac_addr[6];
    
    /* Status fields */
    bool linkok;
    int phy_type;
    int line_speed;
    int duplexmode;
    
    /* reset/probe state */
    bool initialized;
    
    /* power mgmt */
    
    /* other fields */
    QEMUTimer timer;
    QEMUTimer reset_timer;
    bool reset_timer_armed;
    
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
/* MMIO / PIO handlers (device-specific code goes into placeholders)   */
/* ------------------------------------------------------------------ */

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case PAR0:
        val = (s->mac_addr[0] << 0) | (s->mac_addr[1] << 8) |
              (s->mac_addr[2] << 16) | (s->mac_addr[3] << 24);
        break;
    case PAR1:
        val = (s->mac_addr[4] << 0) | (s->mac_addr[5] << 8);
        break;
    case TCRRCR:
        val = s->tcr_rcr;
        break;
    case BCR:
        val = s->bcr;
        break;
    case ISR:
        val = s->isr;
        break;
    case IMR:
        val = s->imr;
        break;
    case MANAGEMENT:
        /* Simulate MII read operation */
        val = MASK_MIIR_MII_MDI; /* Always return MDI high for simplicity */
        break;
    case TALLY:
        val = s->tally;
        break;
    case TSR:
        val = s->tsr;
        break;
    case BMCRSR:
        val = s->bmcrsr;
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
        val = s->bpremrpsr;
        break;
    default:
        if (addr >= MAR0 && addr <= MAR1) {
            /* Multicast address registers - return 0 */
            val = 0;
        } else if (addr >= FAR0 && addr <= FAR1) {
            /* Flow control address - return 0 */
            val = 0;
        } else if (addr == RXCWP) {
            /* Receive current word pointer - return 0 */
            val = 0;
        } else if (addr == FTH) {
            /* Flow control threshold - return 0 */
            val = 0;
        } else {
            qemu_log_mask(LOG_UNIMP, "[%s] mmio_read unknown addr=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
        }
        break;
    }

    /* Mask based on size */
    if (size == 1) {
        val &= 0xFF;
    } else if (size == 2) {
        val &= 0xFFFF;
    } else if (size == 4) {
        /* Full 32-bit value */
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case PAR0:
        if (size == 4) {
            s->mac_addr[0] = (val >> 0) & 0xFF;
            s->mac_addr[1] = (val >> 8) & 0xFF;
            s->mac_addr[2] = (val >> 16) & 0xFF;
            s->mac_addr[3] = (val >> 24) & 0xFF;
        }
        break;
    case PAR1:
        if (size == 2) {
            s->mac_addr[4] = (val >> 0) & 0xFF;
            s->mac_addr[5] = (val >> 8) & 0xFF;
        }
        break;
    case TCRRCR:
        if (size == 4) {
            s->tcr_rcr = val;
            s->crvalue = val;
            
            /* Update link status based on register values */
            if (s->phy_type == MysonPHY) {
                if (val & CR_R_FD) {
                    s->duplexmode = 2; /* full duplex */
                } else {
                    s->duplexmode = 1; /* half duplex */
                }
                if (val & CR_R_PS10) {
                    s->line_speed = 1; /* 10M */
                } else {
                    s->line_speed = 2; /* 100M */
                }
            }
        }
        break;
    case BCR:
        if (size == 4) {
            s->bcr = val;
            s->bcrvalue = val;
            
            /* Handle reset */
            if (val & 0x1) {
                pcibase_reset(DEVICE(s));
            }
        }
        break;
    case TXPDR:
        /* Transmit polling demand - trigger TX */
        if (s->imr & TI) {
            s->isr |= TI;
            pci_set_irq(&s->parent_obj, 1);
        }
        break;
    case RXPDR:
        /* Receive polling demand - trigger RX */
        if (s->imr & RI) {
            s->isr |= RI;
            pci_set_irq(&s->parent_obj, 1);
        }
        break;
    case RXLBA:
        if (size == 4) {
            s->rx_ring_dma = val;
        }
        break;
    case TXLBA:
        if (size == 4) {
            s->tx_ring_dma = val;
        }
        break;
    case ISR:
        if (size == 4) {
            /* Clear interrupts */
            s->isr &= ~val;
            if (s->isr == 0) {
                pci_set_irq(&s->parent_obj, 0);
            }
        }
        break;
    case IMR:
        if (size == 4) {
            s->imr = val;
            s->imrvalue = val;
        }
        break;
    case MANAGEMENT:
        /* Handle MII write operations */
        if (size == 4) {
            /* No-op for simulation */
        }
        break;
    case MAR0:
    case MAR1:
        /* Multicast address registers - ignore writes */
        break;
    case TALLY:
        if (size == 4) {
            s->tally = val;
        }
        break;
    case TSR:
        if (size == 4) {
            s->tsr = val;
        }
        break;
    case BMCRSR:
        if (size == 4) {
            s->bmcrsr = val;
            
            /* Update link status */
            if (s->phy_type == MysonPHY) {
                if (val & LinkIsUp2) {
                    s->linkok = 1;
                } else {
                    s->linkok = 0;
                }
            }
        }
        break;
    case ANARANLPAR:
        if (size == 4) {
            s->anaranlpar = val;
        }
        break;
    case ANEROCR:
        if (size == 4) {
            s->anerocr = val;
        }
        break;
    case BPREMRPSR:
        if (size == 4) {
            s->bpremrpsr = val;
        }
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "[%s] mmio_write unknown addr=%" PRIx64 " val=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, (uint64_t)val, size);
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "[%s] pio_read addr=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "[%s] pio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, (uint64_t)val, size);
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

    /* Reset register shadows */
    s->tcr_rcr = 0;
    s->bcr = 0;
    s->isr = 0;
    s->imr = 0;
    s->crvalue = 0;
    s->bcrvalue = 0;
    s->imrvalue = 0;
    s->phy_identifier = MysonPHYID;
    s->tally = 0;
    s->tsr = 0;
    s->bmcrsr = 0;
    s->anaranlpar = 0;
    s->anerocr = 0;
    s->bpremrpsr = 0;
    
    /* Set default MAC address */
    s->mac_addr[0] = 0x00;
    s->mac_addr[1] = 0x11;
    s->mac_addr[2] = 0x22;
    s->mac_addr[3] = 0x33;
    s->mac_addr[4] = 0x44;
    s->mac_addr[5] = 0x55;
    
    /* Status fields */
    s->linkok = 1; /* Default to link up */
    s->phy_type = MysonPHY;
    s->line_speed = 2; /* 100M */
    s->duplexmode = 2; /* Full duplex */
    
    /* Timer state */
    s->reset_timer_armed = false;
    
    if (s->mmio_backing && s->mmio_backing_size)
        memset(s->mmio_backing, 0, s->mmio_backing_size);
}

/* ------------------------------------------------------------------ */
/* DMA initialize (called from realize)                                */
/* ------------------------------------------------------------------ */
static void pcibase_dma_device_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    /* Initialize DMA addresses */
    s->rx_ring_dma = 0;
    s->tx_ring_dma = 0;
    
    (void)s;
    (void)errp;
}

/* ------------------------------------------------------------------ */
/* PCI config space access (class-level hooks)                         */
/* ------------------------------------------------------------------ */
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    uint32_t val = pci_default_read_config(pdev, addr, len);
    /* optional: override certain config offsets if needed */
    

    switch (len) {
        case 1: val &= 0xFF; break;
        case 2: val &= 0xFFFF; break;
        case 4: break;
        default: break;
    }
    return val;
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    if (addr >= PCI_BASE_ADDRESS_0 && addr <= PCI_BASE_ADDRESS_5) {
        pci_default_write_config(pdev, addr, val, len);
        return;
    }
}

/* ------------------------------------------------------------------ */
/* Realize (device init)                                                */
/* ------------------------------------------------------------------ */
static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);
    
    /* Set BAR0 as MMIO with size 256 bytes */
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 256;
    s->bar_info[0].name = "fealnx-mmio";
    s->num_bars = 1;
    
    /* register BARs */
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Interrupt config (placeholders) */
    
    /* DMA init (called here so device can set DMA masks/etc) */
    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) return;

    /* Initialize timers */
    timer_init_ns(&s->timer, QEMU_CLOCK_VIRTUAL, NULL, s);
    timer_init_ns(&s->reset_timer, QEMU_CLOCK_VIRTUAL, NULL, s);
    
    /* Initialize device state */
    pcibase_reset(DEVICE(s));
    
    qemu_log_mask(LOG_UNIMP, "[%s] device realized\n", TYPE_PCIBASE_DEVICE);
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

    /* free mmio_backing or other allocations */
    

    if (s->mmio_backing) {
        g_free(s->mmio_backing);
        s->mmio_backing = NULL;
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

    /* optional */
    
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
