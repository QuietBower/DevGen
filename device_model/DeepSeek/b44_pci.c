/* This template provides a robust skeleton for hardware emulation.
 * Integrated QEMU PCI device template (QEMU 8.2.10).
 * Replace #PLACEHOLDER# blocks with driver-specific definitions.
 * Designed for register-level modeling and PCIe driver probing.
 * NOTE: Do not directly fill this file with the Linux kernel source code.
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

#define TYPE_PCIBASE_DEVICE "b44_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_BROADCOM     0x14e4
#define PCI_DEVICE_ID_BCM4401      0x4401

/* Register offsets and bit definitions from b44.c */
#define B44_DEVCTRL         0x0000UL
#define DEVCTRL_IPP         0x00000400
#define DEVCTRL_EPR         0x00008000
#define DEVCTRL_PFE         0x00000080
#define DEVCTRL_MPM         0x00000040

#define B44_IMASK           0x0024UL
#define IMASK_DEF           (ISTAT_ERRORS | ISTAT_TO | ISTAT_RX | ISTAT_TX)

#define B44_ISTAT           0x0020UL
#define ISTAT_TX            0x01000000
#define ISTAT_RFO           0x00004000
#define ISTAT_RX            0x00010000
#define ISTAT_TO            0x00000080
#define ISTAT_ERRORS        (ISTAT_DSCE|ISTAT_DATAE|ISTAT_DPE|ISTAT_RDU|ISTAT_RFO|ISTAT_TFU)
#define ISTAT_TFU           0x00008000
#define ISTAT_RDU           0x00002000
#define ISTAT_DSCE          0x00000400
#define ISTAT_DATAE         0x00000800
#define ISTAT_DPE           0x00001000

#define B44_GPTIMER         0x0028UL
#define B44_WKUP_LEN        0x0010UL
#define WKUP_LEN_ENABLE_THREE 0x80000000
#define WKUP_LEN_DISABLE    0x80808080

#define B44_ADDR_LO         0x0088UL
#define B44_ADDR_HI         0x008CUL
#define B44_FILT_ADDR       0x0090UL
#define B44_FILT_DATA       0x0094UL

#define B44_MAC_CTRL        0x00A8UL
#define MAC_CTRL_PHY_PDOWN  0x00000004
#define MAC_CTRL_PHY_LEDCTRL 0x000000e0
#define MAC_CTRL_CRC32_ENAB 0x00000001

#define B44_MAC_FLOW        0x00ACUL
#define MAC_FLOW_RX_HI_WATER 0x000000ff
#define MAC_FLOW_PAUSE_ENAB 0x00008000

#define B44_MDC_RATIO       5000000

#define B44_RCV_LAZY        0x0100UL
#define RCV_LAZY_FC_SHIFT   24

#define B44_DMATX_CTRL      0x0200UL
#define DMATX_CTRL_ENABLE   0x00000001

#define B44_DMATX_ADDR      0x0204UL
#define B44_DMATX_STAT      0x020CUL
#define DMATX_STAT_CDMASK   0x00000fff

#define B44_DMARX_CTRL      0x0210UL
#define DMARX_CTRL_ENABLE   0x00000001
#define DMARX_CTRL_ROSHIFT  1

#define B44_DMARX_ADDR      0x0214UL
#define B44_DMARX_STAT      0x021CUL
#define DMARX_STAT_CDMASK   0x00000fff
#define DMARX_STAT_EMASK    0x000f0000
#define DMARX_STAT_SIDLE    0x00002000

#define B44_DMARX_PTR       0x0218UL
#define B44_DMATX_PTR       0x0208UL

#define B44_RXCONFIG        0x0400UL
#define RXCONFIG_FLOW       0x00000020
#define RXCONFIG_CAM_ABSENT 0x00000100
#define RXCONFIG_ALLMULTI   0x00000002
#define RXCONFIG_PROMISC    0x00000008

#define B44_RXMAXLEN        0x0404UL
#define B44_TXMAXLEN        0x0408UL

#define B44_MDIO_CTRL       0x0410UL
#define MDIO_CTRL_PREAMBLE  0x00000080
#define MDIO_CTRL_MAXF_MASK 0x0000007f

#define B44_MDIO_DATA       0x0414UL
#define MDIO_DATA_SB_START  0x40000000
#define MDIO_DATA_PMD_SHIFT 23
#define MDIO_DATA_OP_SHIFT  28
#define MDIO_OP_READ        2
#define MDIO_OP_WRITE       1
#define MDIO_TA_VALID       2
#define MDIO_DATA_TA_SHIFT  16
#define MDIO_DATA_RA_SHIFT  18
#define MDIO_DATA_DATA      0x0000ffff

#define B44_EMAC_ISTAT      0x041CUL
#define EMAC_INT_MII        0x00000001

#define B44_CAM_DATA_LO     0x0420UL
#define B44_CAM_DATA_HI     0x0424UL
#define B44_CAM_CTRL        0x0428UL
#define CAM_CTRL_WRITE      0x00000008
#define CAM_DATA_HI_VALID   0x00010000
#define CAM_CTRL_INDEX_SHIFT 16
#define CAM_CTRL_BUSY       0x80000000
#define CAM_CTRL_ENABLE     0x00000001

#define B44_ENET_CTRL       0x042CUL
#define ENET_CTRL_EPSEL     0x00000008
#define ENET_CTRL_DISABLE   0x00000002
#define ENET_CTRL_ENABLE    0x00000001

#define B44_TX_CTRL         0x0430UL
#define TX_CTRL_DUPLEX      0x00000001

#define B44_TX_WMARK        0x0434UL
#define B44_MIB_CTRL        0x0438UL
#define MIB_CTRL_CLR_ON_READ 0x00000001

#define B44_TX_GOOD_O       0x0500UL
#define B44_TX_PAUSE        0x055CUL
#define B44_RX_GOOD_O       0x0580UL
#define B44_RX_NPAUSE       0x05D8UL

#define B44_PATTERN_BASE    0x400
#define B44_PATTERN_SIZE    0x80
#define B44_PMASK_BASE      0x600
#define B44_PMASK_SIZE      0x10

#define B44_MCAST_TABLE_SIZE 32

#define RX_FLAG_ERRORS      (RX_FLAG_ODD | RX_FLAG_SERR | RX_FLAG_CRCERR | RX_FLAG_OFIFO)
#define RX_FLAG_SERR        0x00000004
#define RX_FLAG_CRCERR      0x00000002
#define RX_FLAG_OFIFO       0x00000001
#define RX_FLAG_ODD         0x00000008

#define DESC_CTRL_EOT       0x10000000
#define DESC_CTRL_EOF       0x40000000
#define DESC_CTRL_IOC       0x20000000
#define DESC_CTRL_SOF       0x80000000
#define DESC_CTRL_LEN       0x00001fff

#define B44_FLAG_B0_ANDLATER    0x00000001
#define B44_FLAG_BUGGY_TXPTR    0x00000002
#define B44_FLAG_REORDER_BUG    0x00000004
#define B44_FLAG_PAUSE_AUTO     0x00008000
#define B44_FLAG_FULL_DUPLEX    0x00010000
#define B44_FLAG_100_BASE_T     0x00020000
#define B44_FLAG_TX_PAUSE       0x00040000
#define B44_FLAG_RX_PAUSE       0x00080000
#define B44_FLAG_FORCE_LINK     0x00100000
#define B44_FLAG_ADV_10HALF     0x01000000
#define B44_FLAG_ADV_10FULL     0x02000000
#define B44_FLAG_ADV_100HALF    0x04000000
#define B44_FLAG_ADV_100FULL    0x08000000
#define B44_FLAG_EXTERNAL_PHY   0x10000000
#define B44_FLAG_RX_RING_HACK   0x20000000
#define B44_FLAG_TX_RING_HACK   0x40000000
#define B44_FLAG_WOL_ENABLE     0x80000000

#define B44_FULL_RESET          1
#define B44_FULL_RESET_SKIP_PHY 2
#define B44_PARTIAL_RESET       3
#define B44_CHIP_RESET_FULL     4
#define B44_CHIP_RESET_PARTIAL  5

/* SSB window control (offset as defined in driver: 0x80) */
#define SSB_BAR0_WIN         0x80
#define CHIPCOMMON_BASE      0x18000000
#define ETHERNET_BASE        0x18001000

/* SSB core identification values */
#define SSB_VENDOR_BROADCOM  0x14e4
#define SSB_CORE_CHIPCOMMON  0x800
#define SSB_CORE_ETHERNET    0x812

/* Computed SSB ID registers (vendor + coreid) */
#define SSB_ID_CHIPCOMMON   ((SSB_VENDOR_BROADCOM << 16) | SSB_CORE_CHIPCOMMON)
#define SSB_ID_ETHERNET     ((SSB_VENDOR_BROADCOM << 16) | SSB_CORE_ETHERNET)

/* MII register definitions (standard) */
#define MII_BMCR            0x00
#define MII_BMSR            0x01
#define MII_PHYSID1         0x02
#define MII_PHYSID2         0x03
#define MII_ADVERTISE       0x04
#define MII_LPA             0x05
#define MII_EXPANSION       0x06
#define MII_CTRL1000        0x09
#define MII_STAT1000        0x0a
#define MII_ESTATUS         0x0f
#define MII_DCOUNTER        0x12
#define MII_FCSCOUNTER      0x13
#define MII_NWAYTEST        0x14
#define MII_RERRCOUNTER     0x15
#define MII_SREVISION       0x16
#define MII_RESV1           0x17
#define MII_LBRERROR        0x18
#define MII_PHYADDR         0x19
#define MII_RESV2           0x1a
#define MII_TPISTATUS       0x1b
#define MII_NCONFIG         0x1c

#define BMCR_RESET          0x8000
#define BMCR_LOOPBACK       0x4000
#define BMCR_SPEED100       0x2000
#define BMCR_ANENABLE       0x1000
#define BMCR_PDOWN          0x0800
#define BMCR_ISOLATE        0x0400
#define BMCR_ANRESTART      0x0200
#define BMCR_FULLDPLX       0x0100

#define BMSR_100BT4         0x8000
#define BMSR_100BT_FDX      0x4000
#define BMSR_100BT_HDX      0x2000
#define BMSR_10BT_FDX       0x1000
#define BMSR_10BT_HDX       0x0800
#define BMSR_100BT2_FDX     0x0400
#define BMSR_100BT2_HDX     0x0200
#define BMSR_ESTATEN        0x0100
#define BMSR_ANEGCOMPLETE   0x0020
#define BMSR_RFAULT         0x0010
#define BMSR_ANEGCAPABLE    0x0008
#define BMSR_LSTATUS        0x0004
#define BMSR_JCD            0x0002
#define BMSR_EXTSTAT        0x0001

typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM 
} BARType;

typedef struct {
    int     index;    /* BAR index 0-5 */
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

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t mmio_regs[0x1000 / 4];  /* 4KB window, sufficient for SSB cores */

    /* SSB window base (raw value from config register) */
    uint32_t ssb_window_base;

    /* DMA Context */
    dma_addr_t rx_ring_dma;
    dma_addr_t tx_ring_dma;
    uint32_t rx_ring_ptr;
    uint32_t tx_ring_ptr;
    uint32_t dma_offset;

    /* Operational status flags */
    uint32_t flags;

    /* PHY state */
    uint16_t phy_regs[32];
    bool mdio_read_pending;
    uint16_t mdio_rdata;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t active = s->mmio_regs[B44_ISTAT >> 2] & s->mmio_regs[B44_IMASK >> 2];
    if (active) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    hwaddr core_base = s->ssb_window_base & 0xFFFFF000;  /* Mask to obtain base address */

    if (size != 4) {
        return 0;
    }

    /* SSB window control register at offset 0x80 is always accessible */
    if (addr == SSB_BAR0_WIN) {
        return s->ssb_window_base;
    }

    /* Windowed region (4KB from base) */
    if (addr < 0x1000) {
        if (core_base == ETHERNET_BASE) {
            /* Ethernet core registers */
            switch (addr) {
            case B44_DEVCTRL:
                val = s->mmio_regs[addr >> 2];
                break;
            case B44_IMASK:
                val = s->mmio_regs[addr >> 2];
                break;
            case B44_ISTAT:
                val = s->mmio_regs[addr >> 2];
                break;
            case B44_GPTIMER:
                val = s->mmio_regs[addr >> 2];
                break;
            case B44_WKUP_LEN:
                val = s->mmio_regs[addr >> 2];
                break;
            case B44_ADDR_LO:
            case B44_ADDR_HI:
            case B44_FILT_ADDR:
            case B44_FILT_DATA:
                val = s->mmio_regs[addr >> 2];
                break;
            case B44_MAC_CTRL:
                val = s->mmio_regs[addr >> 2];
                break;
            case B44_MAC_FLOW:
                val = s->mmio_regs[addr >> 2];
                break;
            case B44_RCV_LAZY:
                val = s->mmio_regs[addr >> 2];
                break;
            case B44_DMATX_CTRL:
                val = s->mmio_regs[addr >> 2];
                break;
            case B44_DMATX_ADDR:
                val = s->mmio_regs[addr >> 2];
                break;
            case B44_DMATX_STAT:
                val = s->mmio_regs[addr >> 2] & DMATX_STAT_CDMASK;
                break;
            case B44_DMARX_CTRL:
                val = s->mmio_regs[addr >> 2];
                break;
            case B44_DMARX_ADDR:
                val = s->mmio_regs[addr >> 2];
                break;
            case B44_DMARX_PTR:
                val = s->mmio_regs[addr >> 2];
                break;
            case B44_DMARX_STAT:
                val = s->mmio_regs[addr >> 2] & (DMARX_STAT_CDMASK | DMARX_STAT_EMASK | DMARX_STAT_SIDLE);
                break;
            case B44_DMATX_PTR:
                val = s->mmio_regs[addr >> 2];
                break;
            case B44_RXCONFIG:
                val = s->mmio_regs[addr >> 2];
                break;
            case B44_RXMAXLEN:
            case B44_TXMAXLEN:
                val = s->mmio_regs[addr >> 2];
                break;
            case B44_MDIO_CTRL:
                val = s->mmio_regs[addr >> 2];
                break;
            case B44_MDIO_DATA:
                if (s->mdio_read_pending) {
                    val = s->mdio_rdata;
                    s->mdio_read_pending = false;
                } else {
                    val = s->mmio_regs[addr >> 2];
                }
                break;
            case B44_EMAC_ISTAT:
                val = s->mmio_regs[addr >> 2];
                break;
            case B44_CAM_DATA_LO:
            case B44_CAM_DATA_HI:
                val = s->mmio_regs[addr >> 2];
                break;
            case B44_CAM_CTRL:
                val = s->mmio_regs[addr >> 2] & ~CAM_CTRL_BUSY; /* BUSY never set */
                break;
            case B44_ENET_CTRL:
                val = s->mmio_regs[addr >> 2];
                break;
            case B44_TX_CTRL:
                val = s->mmio_regs[addr >> 2];
                break;
            case B44_TX_WMARK:
                val = s->mmio_regs[addr >> 2];
                break;
            case B44_MIB_CTRL:
                val = s->mmio_regs[addr >> 2];
                break;
            default:
                if (addr >= B44_TX_GOOD_O && addr <= B44_RX_NPAUSE) {
                    val = s->mmio_regs[addr >> 2];
                } else {
                    val = 0;
                }
                break;
            }
        } else if (core_base == CHIPCOMMON_BASE) {
            /* Chipcommon core: minimal emulation to satisfy SSB bus scan */
            switch (addr) {
            case 0x0:
                val = SSB_ID_CHIPCOMMON;
                break;
            case 0x4:
                val = 0x00000001; /* core revision */
                break;
            case 0x8:
                val = 0x00000002; /* core control: clock enabled, not in reset */
                break;
            default:
                val = 0;
                break;
            }
        } else {
            /* Unknown window base */
            val = 0;
        }
    } else {
        /* Outside window, fallback to raw mmio_regs (should not happen) */
        val = s->mmio_regs[addr >> 2];
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t idx = addr >> 2;
    hwaddr core_base = s->ssb_window_base & 0xFFFFF000;  /* Mask to obtain base address */

    if (size != 4) {
        return;
    }

    /* SSB window control register at offset 0x80 is always accessible */
    if (addr == SSB_BAR0_WIN) {
        s->ssb_window_base = val;
        return;
    }

    /* Windowed region */
    if (addr < 0x1000) {
        if (core_base == ETHERNET_BASE) {
            /* Ethernet core writes */
            switch (addr) {
            case B44_DEVCTRL:
                s->mmio_regs[idx] = val;
                break;
            case B44_IMASK:
                s->mmio_regs[idx] = val;
                pcibase_update_irq(s);
                break;
            case B44_ISTAT:
                /* W1C: writing 1 clears bits */
                s->mmio_regs[idx] &= ~val;
                pcibase_update_irq(s);
                break;
            case B44_GPTIMER:
                s->mmio_regs[idx] = val;
                break;
            case B44_WKUP_LEN:
                s->mmio_regs[idx] = val;
                break;
            case B44_ADDR_LO:
            case B44_ADDR_HI:
            case B44_FILT_ADDR:
            case B44_FILT_DATA:
                s->mmio_regs[idx] = val;
                break;
            case B44_MAC_CTRL:
                s->mmio_regs[idx] = val;
                break;
            case B44_MAC_FLOW:
                s->mmio_regs[idx] = val;
                break;
            case B44_RCV_LAZY:
                s->mmio_regs[idx] = val;
                break;
            case B44_DMATX_CTRL:
                s->mmio_regs[idx] = val;
                break;
            case B44_DMATX_ADDR:
                s->mmio_regs[idx] = val;
                break;
            case B44_DMATX_STAT:
                /* read-only, ignore */
                break;
            case B44_DMARX_CTRL:
                s->mmio_regs[idx] = val;
                break;
            case B44_DMARX_ADDR:
                s->mmio_regs[idx] = val;
                break;
            case B44_DMARX_PTR:
                s->mmio_regs[idx] = val;
                break;
            case B44_DMARX_STAT:
                /* read-only, ignore */
                break;
            case B44_DMATX_PTR:
                s->mmio_regs[idx] = val;
                break;
            case B44_RXCONFIG:
                s->mmio_regs[idx] = val;
                break;
            case B44_RXMAXLEN:
            case B44_TXMAXLEN:
                s->mmio_regs[idx] = val;
                break;
            case B44_MDIO_CTRL:
                s->mmio_regs[idx] = val;
                break;
            case B44_MDIO_DATA:
                {
                    uint32_t op = (val >> MDIO_DATA_OP_SHIFT) & 3;
                    uint32_t reg = (val >> MDIO_DATA_RA_SHIFT) & 0x1f;
                    uint32_t data = val & MDIO_DATA_DATA;

                    if (op == MDIO_OP_WRITE) {
                        /* Write to PHY register; ignore BMCR_RESET (clear on read) */
                        s->phy_regs[reg] = data;
                        /* Set completion interrupt */
                        s->mmio_regs[B44_EMAC_ISTAT >> 2] |= EMAC_INT_MII;
                    } else if (op == MDIO_OP_READ) {
                        /* Read from PHY register; special handling for BMCR */
                        if (reg == MII_BMCR) {
                            /* BMCR_RESET is always cleared on read */
                            s->phy_regs[reg] &= ~BMCR_RESET;
                        }
                        s->mdio_rdata = s->phy_regs[reg];
                        s->mdio_read_pending = true;
                        /* Set completion interrupt */
                        s->mmio_regs[B44_EMAC_ISTAT >> 2] |= EMAC_INT_MII;
                    }
                    s->mmio_regs[idx] = val; /* store raw value */
                }
                break;
            case B44_EMAC_ISTAT:
                /* W1C for EMAC_INT_MII */
                s->mmio_regs[idx] &= ~val;
                break;
            case B44_CAM_DATA_LO:
            case B44_CAM_DATA_HI:
                s->mmio_regs[idx] = val;
                break;
            case B44_CAM_CTRL:
                s->mmio_regs[idx] = val;
                /* Software must wait for BUSY, but we clear it immediately */
                break;
            case B44_ENET_CTRL:
                if (val & ENET_CTRL_DISABLE) {
                    /* Auto-clear DISABLE bit to avoid timeout */
                    val &= ~ENET_CTRL_DISABLE;
                }
                s->mmio_regs[idx] = val;
                break;
            case B44_TX_CTRL:
                s->mmio_regs[idx] = val;
                break;
            case B44_TX_WMARK:
                s->mmio_regs[idx] = val;
                break;
            case B44_MIB_CTRL:
                s->mmio_regs[idx] = val;
                break;
            default:
                if (addr >= B44_TX_GOOD_O && addr <= B44_RX_NPAUSE) {
                    s->mmio_regs[idx] = val;
                }
                break;
            }
        } else if (core_base == CHIPCOMMON_BASE) {
            /* Minimal: store writes to the first few registers */
            if (addr <= 0x8) {
                s->mmio_regs[idx] = val; 
            }
            /* Ignore other addresses for now */
        } else {
            /* Unknown window base: ignore */
        }
    } else {
        /* Outside window, store to mmio_regs (should not happen) */
        s->mmio_regs[idx] = val;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* No PIO registers */
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

    memset(s->mmio_regs, 0, sizeof(s->mmio_regs));
    memset(s->phy_regs, 0, sizeof(s->phy_regs));
    s->mdio_read_pending = false;
    s->mdio_rdata = 0;
    s->ssb_window_base = CHIPCOMMON_BASE;  /* default to chipcommon */

    /* Set initial register values expected by driver */
    s->mmio_regs[B44_DEVCTRL >> 2] = DEVCTRL_IPP; /* Internal PHY present */
    /* No other special initial values needed */
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }
    
    /* CRITICAL: PCI requires BAR sizes to be a power of 2. Prevent QEMU assert crash. */
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

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_BROADCOM );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_BCM4401 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_ETHERNET );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization: 4KB window for SSB core register access */
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = 0x1000, .name = "ssb-window" };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No MSI/MSI-X */

    /* DMA Config */
    s->rx_ring_dma = 0;
    s->tx_ring_dma = 0;
    s->rx_ring_ptr = 0;
    s->tx_ring_ptr = 0;
    s->dma_offset = 0;

    /* Final state initialization */
    s->intr_status = 0;
    s->intr_mask = 0;
    s->flags = 0;
    memset(s->mmio_regs, 0, sizeof(s->mmio_regs));
    s->mmio_regs[B44_DEVCTRL >> 2] = DEVCTRL_IPP;
    s->mdio_read_pending = false;
    s->mdio_rdata = 0;
    memset(s->phy_regs, 0, sizeof(s->phy_regs));
    s->ssb_window_base = CHIPCOMMON_BASE;
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }

    /* Free any resources if needed */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "b44_pci",
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
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
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
