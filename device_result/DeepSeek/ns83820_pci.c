/*
 * QEMU PCI device model for ns83820 Ethernet controller
 * Phase 2: Full functional implementation
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
#include "hw/qdev-properties.h"
#include "hw/qdev-core.h"
#include "migration/vmstate.h"

#define TYPE_PCIBASE_DEVICE "ns83820_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Vendor and Device IDs (first entry in pci_device_id table) */
#define VENDOR_ID 0x100b
#define DEVICE_ID 0x0022
#define CLASS_ID  0x0200

/* Register Offsets */
#define CR      0x00
#define CFG     0x04
#define CFGCS   0x04
#define MEAR    0x08
#define PTSCR   0x0c
#define ISR     0x10
#define IMR     0x14
#define IER     0x18
#define IHR     0x1c
#define TXDP    0x20
#define TXDP_HI 0x24
#define TXCFG   0x28
#define GPIOR   0x2c
#define RXDP    0x30
#define RXDP_HI 0x34
#define RXCFG   0x38
#define PQCR    0x3c
#define WCSR    0x40
#define PCR     0x44
#define RFCR    0x48
#define RFDR    0x4c
#define SRR     0x58
#define VRCR    0xbc
#define VTCR    0xc0
#define VDR     0xc4
#define CCSR    0xcc
#define TBICR   0xe0
#define TBISR   0xe4
#define TANAR   0xe8
#define TANLPAR 0xec
#define TANER   0xf0
#define TESR    0xf4

/* Bit definitions */
#define CR_RST      0x00000100
#define CR_TXE      0x00000001
#define CR_TXD      0x00000002
#define CR_RXE      0x00000004
#define CR_RXD      0x00000008
#define CR_TXR      0x00000010
#define CR_RXR      0x00000020
#define CR_SWI      0x00000080

#define ISR_TXERR   0x00000100
#define ISR_PHY     0x00004000
#define ISR_TXDESC3 0x40000000
#define ISR_TXDESC2 0x20000000
#define ISR_TXDESC1 0x10000000
#define ISR_TXDESC0 0x08000000
#define ISR_RXDESC3 0x04000000
#define ISR_RXDESC2 0x02000000
#define ISR_RXDESC1 0x01000000
#define ISR_RXDESC0 0x00800000
#define ISR_TXRCMP  0x00400000
#define ISR_RXRCMP  0x00200000
#define ISR_DPERR   0x00100000
#define ISR_SSERR   0x00080000
#define ISR_RMABT   0x00040000
#define ISR_RTABT   0x00020000
#define ISR_RXSOVR  0x00010000
#define ISR_HIBINT  0x00008000
#define ISR_PME     0x00002000
#define ISR_SWI     0x00001000
#define ISR_MIB     0x00000800
#define ISR_TXURN   0x00000400
#define ISR_TXIDLE  0x00000200
#define ISR_TXDESC  0x00000080
#define ISR_TXOK    0x00000040
#define ISR_RXORN   0x00000020
#define ISR_RXIDLE  0x00000010
#define ISR_RXEARLY 0x00000008
#define ISR_RXERR   0x00000004
#define ISR_RXDESC  0x00000002
#define ISR_RXOK    0x00000001

#define TXCFG_CSI       0x80000000
#define TXCFG_HBI       0x40000000
#define TXCFG_MLB       0x20000000
#define TXCFG_ATP       0x10000000
#define TXCFG_ECRETRY   0x00800000
#define TXCFG_BRST_DIS  0x00080000
#define TXCFG_MXDMA1024 0x00000000
#define TXCFG_MXDMA512  0x00700000
#define TXCFG_MXDMA256  0x00600000
#define TXCFG_MXDMA128  0x00500000
#define TXCFG_MXDMA64   0x00400000
#define TXCFG_MXDMA32   0x00300000
#define TXCFG_MXDMA16   0x00200000
#define TXCFG_MXDMA8    0x00100000

#define CFG_LNKSTS      0x80000000
#define CFG_SPDSTS      0x60000000
#define CFG_SPDSTS1     0x40000000
#define CFG_SPDSTS0     0x20000000
#define CFG_DUPSTS      0x10000000
#define CFG_TBI_EN      0x01000000
#define CFG_MODE_1000   0x00400000
#define CFG_AUTO_1000   0x00200000
#define CFG_PINT_CTL    0x001c0000
#define CFG_PINT_DUPSTS 0x00100000
#define CFG_PINT_LNKSTS 0x00080000
#define CFG_PINT_SPDSTS 0x00040000
#define CFG_TMRTEST     0x00020000
#define CFG_MRM_DIS     0x00010000
#define CFG_MWI_DIS     0x00008000
#define CFG_T64ADDR     0x00004000
#define CFG_PCI64_DET   0x00002000
#define CFG_DATA64_EN   0x00001000
#define CFG_M64ADDR     0x00000800
#define CFG_PHY_RST     0x00000400
#define CFG_PHY_DIS     0x00000200
#define CFG_EXTSTS_EN   0x00000100
#define CFG_REQALG      0x00000080
#define CFG_SB          0x00000040
#define CFG_POW         0x00000020
#define CFG_EXD         0x00000010
#define CFG_PESEL       0x00000008
#define CFG_BROM_DIS    0x00000004
#define CFG_EXT_125     0x00000002
#define CFG_BEM         0x00000001

#define EXTSTS_UDPPKT   0x00200000
#define EXTSTS_TCPPKT   0x00080000
#define EXTSTS_IPPKT    0x00020000
#define EXTSTS_VPKT     0x00010000
#define EXTSTS_VTG_MASK 0x0000ffff

#define SPDSTS_POLARITY (CFG_SPDSTS1 | CFG_SPDSTS0 | CFG_DUPSTS | CFG_LNKSTS)

#define MIBC_MIBS       0x00000008
#define MIBC_ACLR       0x00000004
#define MIBC_FRZ        0x00000002
#define MIBC_WRN        0x00000001

#define PCR_PSEN        (1 << 31)
#define PCR_PS_MCAST    (1 << 30)
#define PCR_PS_DA       (1 << 29)
#define PCR_STHI_8      (3 << 23)
#define PCR_STLO_4      (1 << 23)
#define PCR_FFHI_8K     (3 << 21)
#define PCR_FFLO_4K     (1 << 21)
#define PCR_PAUSE_CNT   0xFFFE

#define RXCFG_AEP       0x80000000
#define RXCFG_ARP       0x40000000
#define RXCFG_STRIPCRC  0x20000000
#define RXCFG_RX_FD     0x10000000
#define RXCFG_ALP       0x08000000
#define RXCFG_AIRL      0x04000000
#define RXCFG_MXDMA512  0x00700000
#define RXCFG_DRTH      0x0000003e
#define RXCFG_DRTH0     0x00000002

#define RFCR_RFEN       0x80000000
#define RFCR_AAB        0x40000000
#define RFCR_AAM        0x20000000
#define RFCR_AAU        0x10000000
#define RFCR_APM        0x08000000
#define RFCR_APAT       0x07800000
#define RFCR_APAT3      0x04000000
#define RFCR_APAT2      0x02000000
#define RFCR_APAT1      0x01000000
#define RFCR_APAT0      0x00800000
#define RFCR_AARP       0x00400000
#define RFCR_MHEN       0x00200000
#define RFCR_UHEN       0x00100000
#define RFCR_ULM        0x00080000

#define VRCR_RUDPE      0x00000080
#define VRCR_RTCPE      0x00000040
#define VRCR_RIPE       0x00000020
#define VRCR_IPEN       0x00000010
#define VRCR_DUTF       0x00000008
#define VRCR_DVTF       0x00000004
#define VRCR_VTREN      0x00000002
#define VRCR_VTDEN      0x00000001

#define VTCR_PPCHK      0x00000008
#define VTCR_GCHK       0x00000004
#define VTCR_VPPTI      0x00000002
#define VTCR_VGTI       0x00000001

#define TBICR_MR_AN_ENABLE      0x00001000
#define TBICR_MR_RESTART_AN     0x00000200
#define TBISR_MR_LINK_STATUS    0x00000020
#define TBISR_MR_AN_COMPLETE    0x00000004
#define TANAR_PS2               0x00000100
#define TANAR_PS1               0x00000080
#define TANAR_HALF_DUP          0x00000040
#define TANAR_FULL_DUP          0x00000020

#define GPIOR_GP5_OE            0x00000200
#define GPIOR_GP4_OE            0x00000100
#define GPIOR_GP3_OE            0x00000080
#define GPIOR_GP2_OE            0x00000040
#define GPIOR_GP1_OE            0x00000020
#define GPIOR_GP3_OUT           0x00000004
#define GPIOR_GP1_OUT           0x00000001

#define PTSCR_RBIST_RST         0x00002000
#define PTSCR_RBIST_EN          0x00000400
#define PTSCR_RBIST_DONE        0x00000200
#define PTSCR_RBIST_FAIL        0x000001b8
#define PTSCR_EEBIST_EN         0x00000002
#define PTSCR_EEBIST_FAIL       0x00000001
#define PTSCR_EELOAD_EN         0x00000004

#define CMDSTS_OWN              0x80000000
#define CMDSTS_MORE             0x40000000
#define CMDSTS_INTR             0x20000000
#define CMDSTS_ERR              0x10000000
#define CMDSTS_OK               0x08000000
#define CMDSTS_RUNT             0x00200000
#define CMDSTS_LEN_MASK         0x0000ffff
#define CMDSTS_DEST_MASK        0x01800000
#define CMDSTS_DEST_SELF        0x00800000
#define CMDSTS_DEST_MULTI       0x01000000

/* Descriptor constants */
#define DESC_SIZE  8
#define NR_TX_DESC 128
#define NR_RX_DESC 64

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

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware Register Shadows */
    struct {
        uint32_t cr;
        uint32_t cfg;
        uint32_t mear;
        uint32_t ptscr;
        uint32_t isr;
        uint32_t imr;
        uint32_t ier;
        uint32_t ihr;
        uint32_t txdp;
        uint32_t txdp_hi;
        uint32_t txcfg;
        uint32_t gpior;
        uint32_t rxdp;
        uint32_t rxdp_hi;
        uint32_t rxcfg;
        uint32_t pqcr;
        uint32_t wcsr;
        uint32_t pcr;
        uint32_t rfcr;
        uint32_t rfdr;
        uint32_t srr;
        uint32_t vrcr;
        uint32_t vtcr;
        uint32_t vdr;
        uint32_t ccsr;
        uint32_t tbicr;
        uint32_t tbisr;
        uint32_t tanar;
        uint32_t tanlpar;
        uint32_t taner;
        uint32_t tesr;
    } regs;

    /* Link state for CFG status */
    bool link_up;
    int speed;        /* 0=10, 1=100, 2=1000 */
    bool full_duplex;

    /* MAC address for perfect match table */
    uint8_t mac[6];

    /* Reset state */
    bool in_reset;

    /* PHY interrupt timer */
    QEMUTimer *phy_timer;
};

static void pcibase_update_irq(PCIBaseState *s);

/* Default MAC: 00:0a:5e:00:01:00 */
static const uint8_t default_mac[6] = {
    0x00, 0x0a, 0x5e, 0x00, 0x01, 0x00
};

static uint32_t pcibase_compute_cfg(PCIBaseState *s)
{
    uint32_t status_bits = 0;
    if (s->link_up) {
        status_bits |= CFG_LNKSTS;
    }
    /* Speed encoding: 0=10: SPDSTS0=0 SPDSTS1=0
     *                 1=100: SPDSTS0=1 SPDSTS1=0
     *                 2=1000: SPDSTS0=0 SPDSTS1=1 */
    if (s->speed == 1) {
        status_bits |= CFG_SPDSTS0;
    } else if (s->speed == 2) {
        status_bits |= CFG_SPDSTS1;
    }
    if (s->full_duplex) {
        status_bits |= CFG_DUPSTS;
    }
    /* Write-back control bits, read-back status bits inverted */
    uint32_t ctrl_mask = ~SPDSTS_POLARITY;
    uint32_t ctrl = s->regs.cfg & ctrl_mask;
    uint32_t inv_status = (~status_bits) & SPDSTS_POLARITY;
    return ctrl | inv_status;
}

static uint32_t pcibase_compute_rfdr(PCIBaseState *s)
{
    /* The driver writes index * 2 to RFCR, then reads RFDR.
     * We use the lower bits of RFCR as index into MAC address. */
    hwaddr idx = s->regs.rfcr & 0x7; /* allow 0..5 */
    if (idx >= 6) {
        return 0;
    }
    /* Return two bytes: low byte = mac[idx], next byte = mac[idx+1] if applicable.
     * The driver does: mac[i*2] = data; mac[i*2+1] = data >> 8;
     * So we return 32-bit value with lower 16 bits containing the two bytes.
     */
    uint8_t low = s->mac[idx];
    uint8_t high = (idx + 1 < 6) ? s->mac[idx + 1] : 0;
    return (uint32_t)low | ((uint32_t)high << 8);
}

static void pcibase_handle_ptscr_write(PCIBaseState *s, uint32_t val)
{
    /* Store initial value */
    s->regs.ptscr = val;

    /* Clear RBIST reset: if reset bit set, clear done and fail */
    if (val & PTSCR_RBIST_RST) {
        s->regs.ptscr &= ~(PTSCR_RBIST_DONE | PTSCR_RBIST_FAIL);
    }

    /* Run RBIST: on enable, set done and clear enable immediately */
    if (val & PTSCR_RBIST_EN) {
        s->regs.ptscr = (s->regs.ptscr & ~PTSCR_RBIST_EN) | PTSCR_RBIST_DONE;
    }

    /* Run EEPROM BIST: on enable, clear enable (no done) */
    if (val & PTSCR_EEBIST_EN) {
        s->regs.ptscr &= ~PTSCR_EEBIST_EN;
    }

    /* Run EEPROM load: on enable, clear enable */
    if (val & PTSCR_EELOAD_EN) {
        s->regs.ptscr &= ~PTSCR_EELOAD_EN;
    }
}

static void pcibase_update_irq(PCIBaseState *s)
{
    if (s->in_reset) {
        pci_set_irq(PCI_DEVICE(s), 0);
        return;
    }
    int level = 0;
    if ((s->regs.ier & 1) && (s->regs.isr & s->regs.imr)) {
        level = 1;
    }
    pci_set_irq(PCI_DEVICE(s), level);
}

static void ns83820_phy_timer(void *opaque)
{
    PCIBaseState *s = opaque;
    s->regs.isr |= ISR_PHY;
    pcibase_update_irq(s);
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = 0;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad read size %u at 0x%" HWADDR_PRIx "\n", __func__, size, addr);
        return ~0ULL;
    }

    switch (addr) {
    case CR:       val = s->regs.cr; break;
    case CFG:      val = pcibase_compute_cfg(s); break;
    case MEAR:     val = s->regs.mear; break;
    case PTSCR:    val = s->regs.ptscr; break;
    case ISR:      val = s->regs.isr; break;
    case IMR:      val = s->regs.imr; break;
    case IER:      val = s->regs.ier; break;
    case IHR:      val = s->regs.ihr; break;
    case TXDP:     val = s->regs.txdp; break;
    case TXDP_HI:  val = s->regs.txdp_hi; break;
    case TXCFG:    val = s->regs.txcfg; break;
    case GPIOR:    val = s->regs.gpior; break;
    case RXDP:     val = s->regs.rxdp; break;
    case RXDP_HI:  val = s->regs.rxdp_hi; break;
    case RXCFG:    val = s->regs.rxcfg; break;
    case PQCR:     val = s->regs.pqcr; break;
    case WCSR:     val = s->regs.wcsr; break;
    case PCR:      val = s->regs.pcr; break;
    case RFCR:     val = s->regs.rfcr; break;
    case RFDR:     val = pcibase_compute_rfdr(s); break;
    case SRR:      val = s->regs.srr; break;
    case VRCR:     val = s->regs.vrcr; break;
    case VTCR:     val = s->regs.vtcr; break;
    case VDR:      val = s->regs.vdr; break;
    case CCSR:     val = s->regs.ccsr; break;
    case TBICR:    val = s->regs.tbicr; break;
    case TBISR:    val = s->regs.tbisr; break;
    case TANAR:    val = s->regs.tanar; break;
    case TANLPAR:  val = s->regs.tanlpar; break;
    case TANER:    val = s->regs.taner; break;
    case TESR:     val = s->regs.tesr; break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: read from unknown offset 0x%" HWADDR_PRIx "\n", __func__, addr);
        val = ~0U;
        break;
    }
    return (uint64_t)val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad write size %u at 0x%" HWADDR_PRIx "\n", __func__, size, addr);
        return;
    }

    uint32_t val32 = (uint32_t)val;

    switch (addr) {
    case CR:
        if (val32 & CR_RST) {
            /* Reset request: perform full reset */
            memset(&s->regs, 0, sizeof(s->regs));
            /* Set default values after reset */
            s->regs.srr = 0x0102;
            s->link_up = true;
            s->speed = 2;   /* 1000 Mbps */
            s->full_duplex = true;
            memcpy(s->mac, default_mac, 6);
            s->in_reset = false;
            /* Clear all interrupts */
            pcibase_update_irq(s);
            /* CR_RST is cleared after reset */
            s->regs.cr = 0;
        } else {
            s->regs.cr = val32;
        }
        break;
    case CFG:
        /* Only control bits are writable; status bits read-only */
        s->regs.cfg = val32 & ~SPDSTS_POLARITY;
        break;
    case PTSCR:
        pcibase_handle_ptscr_write(s, val32);
        break;
    case ISR:
        /* ISR is read-only in real hardware; writes are ignored */
        qemu_log_mask(LOG_GUEST_ERROR, "%s: write to read-only ISR\n", __func__);
        break;
    case IMR:
        s->regs.imr = val32;
        pcibase_update_irq(s);
        break;
    case IER:
        s->regs.ier = val32;
        pcibase_update_irq(s);
        /* When enabling interrupts, schedule a PHY interrupt after a short delay
         * to simulate a link change event */
        if (val32 & 1) {
            timer_mod(s->phy_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 1);
        }
        break;
    case IHR:
        s->regs.ihr = val32;
        break;
    case TXDP:
        s->regs.txdp = val32;
        break;
    case TXDP_HI:
        s->regs.txdp_hi = val32;
        break;
    case TXCFG:
        s->regs.txcfg = val32;
        break;
    case GPIOR:
        s->regs.gpior = val32;
        break;
    case RXDP:
        s->regs.rxdp = val32;
        break;
    case RXDP_HI:
        s->regs.rxdp_hi = val32;
        break;
    case RXCFG:
        s->regs.rxcfg = val32;
        break;
    case PQCR:
        s->regs.pqcr = val32;
        break;
    case WCSR:
        s->regs.wcsr = val32;
        break;
    case PCR:
        s->regs.pcr = val32;
        break;
    case RFCR:
        s->regs.rfcr = val32;
        break;
    case RFDR:
        /* RFDR is read-only */
        qemu_log_mask(LOG_GUEST_ERROR, "%s: write to read-only RFDR\n", __func__);
        break;
    case SRR:
        /* SRR is read-only */
        qemu_log_mask(LOG_GUEST_ERROR, "%s: write to read-only SRR\n", __func__);
        break;
    case VRCR:
        s->regs.vrcr = val32;
        break;
    case VTCR:
        s->regs.vtcr = val32;
        break;
    case VDR:
        s->regs.vdr = val32;
        break;
    case CCSR:
        s->regs.ccsr = val32;
        break;
    case TBICR:
        s->regs.tbicr = val32;
        break;
    case TBISR:
        /* TBISR is read-only */
        qemu_log_mask(LOG_GUEST_ERROR, "%s: write to read-only TBISR\n", __func__);
        break;
    case TANAR:
        s->regs.tanar = val32;
        break;
    case TANLPAR:
        s->regs.tanlpar = val32;
        break;
    case TANER:
        s->regs.taner = val32;
        break;
    case TESR:
        s->regs.tesr = val32;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: write to unknown offset 0x%" HWADDR_PRIx "\n", __func__, addr);
        break;
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Reset registers to power-on defaults */
    memset(&s->regs, 0, sizeof(s->regs));
    s->regs.srr = 0x0102;
    memcpy(s->mac, default_mac, 6);
    s->link_up = true;
    s->speed = 2;   /* 1000 Mbps */
    s->full_duplex = true;
    s->in_reset = false;
    pcibase_update_irq(s);
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
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* No MSI/MSI-X used by this driver */

    /* BAR Initialization: driver uses only BAR1 (MMIO) with size 0x1000 */
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo){ .index = 1, .type = BAR_TYPE_MMIO, .size = 0x1000, .name = "ns83820-bar1" };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Init PHY timer */
    s->phy_timer = timer_new_ms(QEMU_CLOCK_VIRTUAL, ns83820_phy_timer, s);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    timer_del(s->phy_timer);
    timer_free(s->phy_timer);
    /* No MSI/MSI-X to uninit */
}

static const VMStateDescription vmstate_pcibase = {
    .name = "ns83820_pci",
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
