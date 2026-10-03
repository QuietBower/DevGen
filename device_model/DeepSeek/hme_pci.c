/*
 * QEMU model of Sun Happy Meal Ethernet (sunhme)
 * Extracted from Linux driver sunhme.c
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

#define TYPE_PCIBASE_DEVICE "hme_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/*
 * Register offsets and hardware identifiers from sunhme.c
 */

/* Global Registers */
#define GREG_SWRESET     0x000UL
#define GREG_CFG         0x004UL
#define GREG_CFG_BURST16 0x00
#define GREG_CFG_BURST32 0x01
#define GREG_CFG_BURST64 0x02
#define GREG_CFG_64BIT   0x04
#define GREG_STAT        0x100UL
#define GREG_IMASK       0x104UL
#define GREG_IMASK_GOTFRAME    0x00000001
#define GREG_IMASK_RCNTEXP     0x00000002
#define GREG_IMASK_SENTFRAME   0x00000100
#define GREG_IMASK_TXPERR      0x10000000

/* Global Status Bits */
#define GREG_STAT_SLVPERR      0x80000000
#define GREG_STAT_SLVERR       0x40000000
#define GREG_STAT_TXTERR       0x20000000
#define GREG_STAT_TXPERR       0x10000000
#define GREG_STAT_TXLERR       0x08000000
#define GREG_STAT_TXEACK       0x04000000
#define GREG_STAT_RXTERR       0x00200000
#define GREG_STAT_RXPERR       0x00100000
#define GREG_STAT_MIFIRQ       0x00800000
#define GREG_STAT_EOPERR       0x00400000
#define GREG_STAT_TXALL        0x02000000
#define GREG_STAT_RXTOHOST     0x00010000
#define GREG_STAT_NORXD        0x00020000
#define GREG_STAT_RXERR        0x00040000
#define GREG_STAT_MAXPKTERR    0x00000400
#define GREG_STAT_TFIFO_UND    0x00000200
#define GREG_STAT_STSTERR      0x00000080
#define GREG_STAT_RFIFOVF      0x00000020
#define GREG_STAT_ERRORS       0xfc7efefc

#define GREG_RESET_ALL         0x03

/* Transmitter Registers */
#define ETX_CFG           0x04UL
#define ETX_CFG_DMAENABLE    0x00000001
#define ETX_RSIZE         0x2cUL
#define ETX_RSIZE_SHIFT   4
#define ETX_RING          0x08UL
#define ETX_PENDING       0x00UL
#define ETX_TP_DMAWAKEUP  0x00000001

/* Receiver Registers */
#define ERX_CFG           0x00UL
#define ERX_CFG_DMAENABLE    0x00000001
#define ERX_CFG_SIZE32       0x00000000
#define ERX_CFG_DEFAULT(off) (ERX_CFG_DMAENABLE|((off)<<3)|ERX_CFG_SIZE32|((14/2)<<16))
#define ERX_RING          0x04UL

/* BMAC Registers */
#define BMAC_XIFCFG       0x0000UL
#define BMAC_TXCFG        0x20cUL
#define BIGMAC_TXCFG_FULLDPLX 0x00000200
#define BIGMAC_TXCFG_ENABLE   0x00000001
#define BMAC_TXSWRESET    0x208UL
#define BMAC_RXSWRESET    0x308UL
#define BMAC_RXCFG        0x30cUL
#define BIGMAC_RXCFG_ENABLE   0x00000001
#define BIGMAC_RXCFG_PMISC    0x00000040
#define BIGMAC_RXCFG_HENABLE  0x00000800
#define BIGMAC_RXCFG_REJME    0x00000200
#define BMAC_RXMAX        0x310UL
#define BMAC_TXMAX        0x230UL
#define BMAC_ALIMIT       0x218UL
#define BMAC_IGAP1        0x210UL
#define BMAC_IGAP2        0x214UL
#define BMAC_JSIZE        0x22cUL
#define BMAC_LTCTR        0x24cUL
#define BMAC_EXCTR        0x248UL
#define BMAC_RSEED        0x250UL
#define BMAC_MACADDR0     0x320UL
#define BMAC_MACADDR1     0x31cUL
#define BMAC_MACADDR2     0x318UL
#define BMAC_HTABLE0      0x34cUL
#define BMAC_HTABLE1      0x348UL
#define BMAC_HTABLE2      0x344UL
#define BMAC_HTABLE3      0x340UL
#define BMAC_RCRCECTR     0x330UL
#define BMAC_UNALECTR     0x32cUL
#define BMAC_GLECTR       0x328UL

/* BigMAC XCFG */
#define BIGMAC_XCFG_MIIDISAB  0x00000008
#define BIGMAC_XCFG_LANCE     0x00000010
#define BIGMAC_XCFG_ODENABLE  0x00000001

/* Transceiver Registers */
#define TCVR_BBCLOCK      0x00UL
#define TCVR_BBDATA       0x04UL
#define TCVR_BBOENAB      0x08UL
#define TCVR_FRAME        0x0cUL
#define TCVR_CFG          0x10UL
#define TCV_CFG_MDIO1         0x00000200
#define TCV_CFG_MDIO0         0x00000100
#define TCV_CFG_PSELECT       0x00000001
#define TCV_CFG_BENABLE       0x00000004
#define TCV_PADDR_ETX         0
#define TCV_PADDR_ITX         1

/* Misc hardware config values */
#define FRAME_READ            0x60020000
#define FRAME_WRITE           0x50020000
#define DP83840_CSCONFIG      0x17
#define CSCONFIG_TCVDISAB     0x0010
#define CSCONFIG_DFBYPASS     0x0020

/* Driver flags, not hardware registers, but referenced for completeness */
#define HFLAG_FENABLE             0x00000002
#define HFLAG_LANCE               0x00000004
#define HFLAG_FULL                0x00000020
#define HFLAG_RXCV                0x00000100
#define HFLAG_INIT                0x00000200
#define HFLAG_PCI                 0x00000800
#define HFLAG_QUATTRO         0x00001000
#define HFLAG_NOT_A0 (HFLAG_FENABLE | HFLAG_LANCE | HFLAG_RXCV)
#define HFLAG_20_21  HFLAG_FENABLE

/* Ring sizes and defaults */
#define RX_RING_SIZE       32
#define TX_RING_SIZE       32
#define RX_RING_MAXSIZE    256
#define TX_RING_MAXSIZE    256
#define RX_BUF_ALLOC_SIZE  (1546 + RX_OFFSET + 64)
#define RX_OFFSET          2

/* Flag bits for TX / RX descriptors (async hardware interface) */
#define TXFLAG_SIZE        0x00003fff
#define TXFLAG_OWN         0x80000000
#define TXFLAG_SOP         0x40000000
#define TXFLAG_EOP         0x20000000
#define TXFLAG_CSENABLE    0x10000000
#define TXFLAG_CSLOCATION  0x0ff00000
#define TXFLAG_CSBUFBEGIN  0x000fc000

#define RXFLAG_OWN         0x80000000
#define RXFLAG_OVERFLOW    0x40000000
#define RXFLAG_CSUM        0x0000ffff

/* Tx / Rx ring index macros */
#define NEXT_TX(num)       (((num) + 1) & (TX_RING_SIZE - 1))
#define NEXT_RX(num)       (((num) + 1) & (RX_RING_SIZE - 1))

#define TCVR_FAILURE      0x80000000
#define TCVR_READ_TRIES   16
#define TCVR_WRITE_TRIES  16
#define TX_RESET_TRIES     32
#define RX_RESET_TRIES     32
#define STOP_TRIES         16
#define TCVR_RESET_TRIES       16
#define TCVR_UNISOLATE_TRIES   32

/* Placeholder vendor/device IDs - will be replaced with real values later */
#define PCI_VENDOR_ID_SUN          0x108e  /* commonly known, still we list it */
#define PCI_DEVICE_ID_SUN_HAPPYMEAL 0x1001

/* Class code: network controller, ethernet */
#define HME_CLASS_ID 0x0200

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

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Interrupt state */
    uint32_t intr_status;   /* GREG_STAT reflection */
    uint32_t intr_mask;     /* GREG_IMASK reflection */

    /* DMA descriptor ring base addresses and current indexes (to be filled in later phases) */
    struct {
        hwaddr tx_ring;
        hwaddr rx_ring;
        uint32_t tx_index;
        uint32_t rx_index;
    } dma;

    /* Global control & status */
    uint32_t global_cfg;     /* GREG_CFG */
    uint32_t global_status;  /* GREG_STAT */

    /* Full register space: 32K bytes, accessed as 32-bit little-endian */
    uint32_t regs[0x2000];

    /* MII transceiver registers (lower 16 bits of each) */
    uint16_t mii_regs[32];

    /* MII frame access state */
    bool mii_read_pending;
    uint8_t mii_pending_reg;
};

/* Internal helper for status-triggered signaling. */
static void hme_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->intr_status & s->intr_mask) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* Device-initiated DMA logic based on driver access patterns (not used) */

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t hme_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    uint32_t offset = addr & 0x7FFF;

    /* Only support aligned 32-bit reads */
    if (size != 4) {
        return ~0ULL;
    }

    switch (offset) {
    case GREG_SWRESET:
        /* Always reads back 0 after any write */
        val = 0;
        break;
    case GREG_STAT:
        val = s->intr_status;
        /* Clear-on-read for all status bits (simplified) */
        s->intr_status = 0;
        hme_update_irq(s);
        break;
    case 0x6208: /* BMAC_TXSWRESET */
        val = 0;
        break;
    case 0x6308: /* BMAC_RXSWRESET */
        val = 0;
        break;
    case 0x700c: /* TCVR_FRAME */
        if (s->mii_read_pending) {
            val = (uint32_t)s->mii_regs[s->mii_pending_reg] | 0x10000;
            s->mii_read_pending = false;
        } else {
            val = 0x10000; /* Just completion for writes */
        }
        break;
    default:
        val = s->regs[offset / 4];
        break;
    }

    return val;
}

static void hme_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t offset = addr & 0x7FFF;

    if (size != 4) {
        return;
    }

    switch (offset) {
    case GREG_SWRESET:
        /* Write of GREG_RESET_ALL triggers a global reset eventually,
         * but immediate effect: subsequent reads return 0.
         * For simplicity, we ignore the write and always return 0 on read.
         */
        break;
    case GREG_IMASK:
        s->intr_mask = val;
        hme_update_irq(s);
        break;
    case GREG_STAT:
        /* Not writeable? The driver only reads this; ignore writes */
        break;
    case 0x6208: /* BMAC_TXSWRESET */
        /* Writing 0 triggers reset; reads return 0 */
        s->regs[offset / 4] = val;
        break;
    case 0x6308: /* BMAC_RXSWRESET */
        s->regs[offset / 4] = val;
        break;
    case 0x700c: /* TCVR_FRAME */
    {
        /* Decode MII command */
        uint32_t reg = (val >> 18) & 0x1f;
        if (val & 0x10000000) {
            /* Read command */
            s->mii_read_pending = true;
            s->mii_pending_reg = reg;
        } else {
            /* Write command */
            uint16_t data = val & 0xffff;
            if (reg == 0) { /* MII_BMCR */
                /* Handle BMCR_RESET: clear reset bit immediately */
                if (data & 0x8000) {
                    s->mii_regs[0] = data & ~0x8000;
                } else {
                    s->mii_regs[0] = data;
                }
            } else {
                s->mii_regs[reg] = data;
            }
            s->mii_read_pending = false;
        }
        break;
    }
    default:
        s->regs[offset / 4] = val;
        break;
    }
}

static const MemoryRegionOps hme_mmio_ops = {
    .read = hme_mmio_read,
    .write = hme_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

static void hme_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Reset all shadow registers to zero */
    memset(s->regs, 0, sizeof(s->regs));
    s->intr_status = 0;
    s->intr_mask = 0;
    s->global_cfg = 0;
    s->global_status = 0;

    /* Set TCVR_CFG to indicate internal transceiver (MDIO0=1) */
    s->regs[0x7010 >> 2] = TCV_CFG_MDIO0;

    /* Initialize MII registers with plausible defaults for probe success */
    s->mii_regs[0] = 0x0000;      /* BMCR */
    s->mii_regs[1] = 0x786d;      /* BMSR: link up, 100/10 half/full, autoneg capable */
    s->mii_regs[2] = 0x2000;      /* PHYID1 */
    s->mii_regs[3] = 0x5c01;      /* PHYID2 (National DP83840) */
    s->mii_regs[4] = 0x0000;      /* ADVERTISE */
    s->mii_regs[5] = 0x0000;      /* LPA */
    s->mii_regs[DP83840_CSCONFIG] = 0x0000; /* DP83840_CSCONFIG */

    s->mii_read_pending = false;
    s->mii_pending_reg = 0;
}

static void hme_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    /* CRITICAL: PCI requires BAR sizes to be a power of 2. Prevent QEMU assert crash. */
    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &hme_mmio_ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &hme_mmio_ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

static void hme_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_SUN);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_SUN_HAPPYMEAL);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, HME_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR initialization: based on happy_meal_pci_probe() - single MMIO BAR0 with size 0x8000 */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x8000;
    s->bar_info[0].name = "hme-mmio";

    for (int i = 0; i < s->num_bars; i++) {
        hme_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize device state */
    hme_reset(DEVICE(pdev));
}

static void hme_uninit(PCIDevice *pdev)
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
static const VMStateDescription vmstate_hme = {
    .name = "hme_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_END_OF_LIST()
    }
};

static void hme_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = hme_realize;
    k->exit    = hme_uninit;
    dc->reset  = hme_reset;
    dc->vmsd   = &vmstate_hme;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void hme_register_types(void)
{
    static InterfaceInfo interfaces[] = {
        { INTERFACE_PCIE_DEVICE },
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    };

    static const TypeInfo hme_info = {
        .name = TYPE_PCIBASE_DEVICE,
        .parent = TYPE_PCI_DEVICE,
        .instance_size = sizeof(PCIBaseState),
        .class_init = hme_class_init,
        .interfaces = interfaces,
    };

    type_register_static(&hme_info);
}

type_init(hme_register_types);
