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

#define TYPE_PCIBASE_DEVICE "ns83820_pci"

typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* ns83820 register offsets */
#define NS83820_CR      0x00
#define NS83820_CFG     0x04
#define NS83820_MEAR    0x08
#define NS83820_PTSCR   0x0c
#define NS83820_ISR     0x10
#define NS83820_IMR     0x14
#define NS83820_IER     0x18
#define NS83820_IHR     0x1c
#define NS83820_TXDP    0x20
#define NS83820_TXDP_HI 0x24
#define NS83820_TXCFG   0x28
#define NS83820_GPIOR   0x2c
#define NS83820_RXDP    0x30
#define NS83820_RXDP_HI 0x34
#define NS83820_RXCFG   0x38
#define NS83820_PQCR    0x3c
#define NS83820_WCSR    0x40
#define NS83820_PCR     0x44
#define NS83820_RFCR    0x48
#define NS83820_RFDR    0x4c
#define NS83820_SRR     0x58
#define NS83820_VRCR    0xbc
#define NS83820_VTCR    0xc0
#define NS83820_VDR     0xc4
#define NS83820_CCSR    0xcc
#define NS83820_TBICR   0xe0
#define NS83820_TBISR   0xe4
#define NS83820_TANAR   0xe8
#define NS83820_TANLPAR 0xec
#define NS83820_TANER   0xf0
#define NS83820_TESR    0xf4

/* Interrupt Status Register bits */
#define NS83820_ISR_RXOK      0x00000001
#define NS83820_ISR_RXDESC    0x00000002
#define NS83820_ISR_RXERR     0x00000004
#define NS83820_ISR_RXEARLY   0x00000008
#define NS83820_ISR_RXIDLE    0x00000010
#define NS83820_ISR_RXORN     0x00000020
#define NS83820_ISR_TXOK      0x00000040
#define NS83820_ISR_TXDESC    0x00000080
#define NS83820_ISR_TXERR     0x00000100
#define NS83820_ISR_TXURN     0x00000400
#define NS83820_ISR_MIB       0x00000800
#define NS83820_ISR_SWI       0x00001000
#define NS83820_ISR_PME       0x00002000
#define NS83820_ISR_PHY       0x00004000
#define NS83820_ISR_HIBINT    0x00008000
#define NS83820_ISR_RXSOVR    0x00010000
#define NS83820_ISR_RTABT     0x00020000
#define NS83820_ISR_RMABT     0x00040000
#define NS83820_ISR_SSERR     0x00080000
#define NS83820_ISR_DPERR     0x00100000
#define NS83820_ISR_RXRCMP    0x00200000
#define NS83820_ISR_TXRCMP    0x00400000
#define NS83820_ISR_RXDESC0   0x00800000
#define NS83820_ISR_RXDESC1   0x01000000
#define NS83820_ISR_RXDESC2   0x02000000
#define NS83820_ISR_RXDESC3   0x04000000
#define NS83820_ISR_TXDESC0   0x08000000
#define NS83820_ISR_TXDESC1   0x10000000
#define NS83820_ISR_TXDESC2   0x20000000
#define NS83820_ISR_TXDESC3   0x40000000

/* Control register bits */
#define NS83820_CR_TXE    0x00000001
#define NS83820_CR_TXD    0x00000002
#define NS83820_CR_RXE    0x00000004
#define NS83820_CR_RXD    0x00000008
#define NS83820_CR_TXR    0x00000010
#define NS83820_CR_RXR    0x00000020
#define NS83820_CR_SWI    0x00000080
#define NS83820_CR_RST    0x00000100

/* CFG register bits */
#define NS83820_CFG_LNKSTS      0x80000000
#define NS83820_CFG_SPDSTS      0x60000000
#define NS83820_CFG_SPDSTS1     0x40000000
#define NS83820_CFG_SPDSTS0     0x20000000
#define NS83820_CFG_DUPSTS      0x10000000
#define NS83820_CFG_TBI_EN      0x01000000
#define NS83820_CFG_MODE_1000   0x00400000
#define NS83820_CFG_AUTO_1000   0x00200000
#define NS83820_CFG_PINT_CTL    0x001c0000
#define NS83820_CFG_PINT_DUPSTS 0x00100000
#define NS83820_CFG_PINT_LNKSTS 0x00080000
#define NS83820_CFG_PINT_SPDSTS 0x00040000
#define NS83820_CFG_TMRTEST     0x00020000
#define NS83820_CFG_MRM_DIS     0x00010000
#define NS83820_CFG_MWI_DIS     0x00008000
#define NS83820_CFG_T64ADDR     0x00004000
#define NS83820_CFG_PCI64_DET   0x00002000
#define NS83820_CFG_DATA64_EN   0x00001000
#define NS83820_CFG_M64ADDR     0x00000800
#define NS83820_CFG_PHY_RST     0x00000400
#define NS83820_CFG_PHY_DIS     0x00000200
#define NS83820_CFG_EXTSTS_EN   0x00000100
#define NS83820_CFG_REQALG      0x00000080
#define NS83820_CFG_SB          0x00000040
#define NS83820_CFG_POW         0x00000020
#define NS83820_CFG_EXD         0x00000010
#define NS83820_CFG_PESEL       0x00000008
#define NS83820_CFG_BROM_DIS    0x00000004
#define NS83820_CFG_EXT_125     0x00000002
#define NS83820_CFG_BEM         0x00000001

/* MEAR bits */
#define NS83820_MEAR_EEDI   0x00000001
#define NS83820_MEAR_EEDO   0x00000002
#define NS83820_MEAR_EECLK  0x00000004
#define NS83820_MEAR_EESEL  0x00000008
#define NS83820_MEAR_MDIO   0x00000010
#define NS83820_MEAR_MDDIR  0x00000020
#define NS83820_MEAR_MDC    0x00000040

/* PTSCR bits */
#define NS83820_PTSCR_EEBIST_FAIL 0x00000001
#define NS83820_PTSCR_EEBIST_EN   0x00000002
#define NS83820_PTSCR_EELOAD_EN   0x00000004
#define NS83820_PTSCR_RBIST_FAIL  0x000001b8
#define NS83820_PTSCR_RBIST_DONE  0x00000200
#define NS83820_PTSCR_RBIST_EN    0x00000400
#define NS83820_PTSCR_RBIST_RST   0x00002000

/* TXCFG bits */
#define NS83820_TXCFG_CSI        0x80000000
#define NS83820_TXCFG_HBI        0x40000000
#define NS83820_TXCFG_MLB        0x20000000
#define NS83820_TXCFG_ATP        0x10000000
#define NS83820_TXCFG_ECRETRY    0x00800000
#define NS83820_TXCFG_BRST_DIS   0x00080000
#define NS83820_TXCFG_MXDMA1024  0x00000000
#define NS83820_TXCFG_MXDMA512   0x00700000
#define NS83820_TXCFG_MXDMA256   0x00600000
#define NS83820_TXCFG_MXDMA128   0x00500000
#define NS83820_TXCFG_MXDMA64    0x00400000
#define NS83820_TXCFG_MXDMA32    0x00300000
#define NS83820_TXCFG_MXDMA16    0x00200000
#define NS83820_TXCFG_MXDMA8     0x00100000

/* RXCFG bits */
#define NS83820_RXCFG_AEP        0x80000000
#define NS83820_RXCFG_ARP        0x40000000
#define NS83820_RXCFG_STRIPCRC   0x20000000
#define NS83820_RXCFG_RX_FD      0x10000000
#define NS83820_RXCFG_ALP        0x08000000
#define NS83820_RXCFG_AIRL       0x04000000
#define NS83820_RXCFG_MXDMA512   0x00700000
#define NS83820_RXCFG_DRTH       0x0000003e
#define NS83820_RXCFG_DRTH0      0x00000002

/* RFCR bits */
#define NS83820_RFCR_RFEN    0x80000000
#define NS83820_RFCR_AAB     0x40000000
#define NS83820_RFCR_AAM     0x20000000
#define NS83820_RFCR_AAU     0x10000000
#define NS83820_RFCR_APM     0x08000000
#define NS83820_RFCR_APAT    0x07800000
#define NS83820_RFCR_APAT3   0x04000000
#define NS83820_RFCR_APAT2   0x02000000
#define NS83820_RFCR_APAT1   0x01000000
#define NS83820_RFCR_APAT0   0x00800000
#define NS83820_RFCR_AARP    0x00400000
#define NS83820_RFCR_MHEN    0x00200000
#define NS83820_RFCR_UHEN    0x00100000
#define NS83820_RFCR_ULM     0x00080000

/* VRCR bits */
#define NS83820_VRCR_RUDPE   0x00000080
#define NS83820_VRCR_RTCPE   0x00000040
#define NS83820_VRCR_RIPE    0x00000020
#define NS83820_VRCR_IPEN    0x00000010
#define NS83820_VRCR_DUTF    0x00000008
#define NS83820_VRCR_DVTF    0x00000004
#define NS83820_VRCR_VTREN   0x00000002
#define NS83820_VRCR_VTDEN   0x00000001

/* VTCR bits */
#define NS83820_VTCR_PPCHK   0x00000008
#define NS83820_VTCR_GCHK    0x00000004
#define NS83820_VTCR_VPPTI   0x00000002
#define NS83820_VTCR_VGTI    0x00000001

/* MIBC bits */
#define NS83820_MIBC_MIBS    0x00000008
#define NS83820_MIBC_ACLR    0x00000004
#define NS83820_MIBC_FRZ     0x00000002
#define NS83820_MIBC_WRN     0x00000001

/* PCR bits */
#define NS83820_PCR_PSEN       (1u << 31)
#define NS83820_PCR_PS_MCAST   (1u << 30)
#define NS83820_PCR_PS_DA      (1u << 29)
#define NS83820_PCR_STHI_8     (3u << 23)
#define NS83820_PCR_STLO_4     (1u << 23)
#define NS83820_PCR_FFHI_8K    (3u << 21)
#define NS83820_PCR_FFLO_4K    (1u << 21)
#define NS83820_PCR_PAUSE_CNT  0xFFFE

/* TBICR/TBISR/TANAR bits */
#define NS83820_TBICR_MR_AN_ENABLE   0x00001000
#define NS83820_TBICR_MR_RESTART_AN  0x00000200
#define NS83820_TBISR_MR_LINK_STATUS 0x00000020
#define NS83820_TBISR_MR_AN_COMPLETE 0x00000004
#define NS83820_TANAR_PS2            0x00000100
#define NS83820_TANAR_PS1            0x00000080
#define NS83820_TANAR_HALF_DUP       0x00000040
#define NS83820_TANAR_FULL_DUP       0x00000020

/* GPIOR bits */
#define NS83820_GPIOR_GP5_OE  0x00000200
#define NS83820_GPIOR_GP4_OE  0x00000100
#define NS83820_GPIOR_GP3_OE  0x00000080
#define NS83820_GPIOR_GP2_OE  0x00000040
#define NS83820_GPIOR_GP1_OE  0x00000020
#define NS83820_GPIOR_GP3_OUT 0x00000004
#define NS83820_GPIOR_GP1_OUT 0x00000001

/* Descriptor layout helpers */
#define NS83820_DESC_SIZE      8
#define NS83820_DESC_LINK      0
#define NS83820_DESC_BUFPTR    (NS83820_DESC_LINK + sizeof(dma_addr_t)/4)
#define NS83820_DESC_CMDSTS    (NS83820_DESC_BUFPTR + sizeof(dma_addr_t)/4)
#define NS83820_DESC_EXTSTS    (NS83820_DESC_CMDSTS + 4/4)

/* CMDSTS bits */
#define NS83820_CMDSTS_OWN        0x80000000
#define NS83820_CMDSTS_MORE       0x40000000
#define NS83820_CMDSTS_INTR       0x20000000
#define NS83820_CMDSTS_ERR        0x10000000
#define NS83820_CMDSTS_OK         0x08000000
#define NS83820_CMDSTS_RUNT       0x00200000
#define NS83820_CMDSTS_DEST_MASK  0x01800000
#define NS83820_CMDSTS_DEST_SELF  0x00800000
#define NS83820_CMDSTS_DEST_MULTI 0x01000000
#define NS83820_CMDSTS_LEN_MASK   0x0000ffff

#define NS83820_NR_RX_DESC       64
#define NS83820_NR_TX_DESC       128
#define NS83820_RX_BUF_SIZE      1500
#define NS83820_REAL_RX_BUF_SIZE (NS83820_RX_BUF_SIZE + 14)
#define NS83820_MIN_TX_DESC_FREE 8

#define NS83820_VRCR_INIT_VALUE (NS83820_VRCR_IPEN | NS83820_VRCR_VTDEN | NS83820_VRCR_VTREN)
#define NS83820_VTCR_INIT_VALUE (NS83820_VTCR_PPCHK | NS83820_VTCR_VPPTI)

#define NS83820_LINK_AUTONEGOTIATE 0x01
#define NS83820_LINK_DOWN          0x02

#define NS83820_HW_ADDR_LEN sizeof(dma_addr_t)

typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
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

    uint32_t CFG_cache;
    uint32_t MEAR_cache;
    uint32_t IMR_cache;
    unsigned int ihr_shadow;

    /* core registers shadow */
    uint32_t cr;
    uint32_t mear;
    uint32_t ptscr;
    uint32_t isr;
    uint32_t imr;
    uint32_t ier;
    uint32_t ihr;
    uint32_t txdp_lo;
    uint32_t txdp_hi;
    uint32_t txcfg;
    uint32_t gpior;
    uint32_t rxdp_lo;
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

    /* interrupt line */
    qemu_irq irq;

    /* simple DMA emulation: host memory is system memory */
    bool rx_running;
    bool tx_running;

    /* for simplicity, store link state */
    uint32_t linkstate;
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
    .impl  = { .min_access_size = 1, .max_access_size = 1 },
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
    }
}

static void ns83820_update_irq(PCIBaseState *s)
{
    uint32_t pending = s->isr & s->imr;
    if (pending && s->ier) {
        pci_set_irq(PCI_DEVICE(s), 1);
    } else {
        pci_set_irq(PCI_DEVICE(s), 0);
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = 0;

    switch (addr) {
    case NS83820_CR:
        val = s->cr;
        break;
    case NS83820_CFG:
        val = s->CFG_cache;
        break;
    case NS83820_MEAR:
        val = s->mear;
        break;
    case NS83820_PTSCR:
        val = s->ptscr;
        break;
    case NS83820_ISR:
        val = s->isr;
        break;
    case NS83820_IMR:
        val = s->imr;
        break;
    case NS83820_IER:
        val = s->ier;
        break;
    case NS83820_IHR:
        val = s->ihr;
        break;
    case NS83820_TXDP:
        val = s->txdp_lo;
        break;
    case NS83820_TXDP_HI:
        val = s->txdp_hi;
        break;
    case NS83820_TXCFG:
        val = s->txcfg;
        break;
    case NS83820_GPIOR:
        val = s->gpior;
        break;
    case NS83820_RXDP:
        val = s->rxdp_lo;
        break;
    case NS83820_RXDP_HI:
        val = s->rxdp_hi;
        break;
    case NS83820_RXCFG:
        val = s->rxcfg;
        break;
    case NS83820_PQCR:
        val = s->pqcr;
        break;
    case NS83820_WCSR:
        val = s->wcsr;
        break;
    case NS83820_PCR:
        val = s->pcr;
        break;
    case NS83820_RFCR:
        val = s->rfcr;
        break;
    case NS83820_RFDR:
        val = s->rfdr;
        break;
    case NS83820_SRR:
        val = s->srr;
        break;
    case NS83820_VRCR:
        val = s->vrcr;
        break;
    case NS83820_VTCR:
        val = s->vtcr;
        break;
    case NS83820_VDR:
        val = s->vdr;
        break;
    case NS83820_CCSR:
        val = s->ccsr;
        break;
    case NS83820_TBICR:
        val = s->tbicr;
        break;
    case NS83820_TBISR:
        val = s->tbisr;
        break;
    case NS83820_TANAR:
        val = s->tanar;
        break;
    case NS83820_TANLPAR:
        val = s->tanlpar;
        break;
    case NS83820_TANER:
        val = s->taner;
        break;
    case NS83820_TESR:
        val = s->tesr;
        break;
    default:
        if (addr + size <= s->mmio_backing_size) {
            if (size == 1) {
                val = s->mmio_backing[addr];
            } else if (size == 2) {
                uint16_t tmp16;
                memcpy(&tmp16, s->mmio_backing + addr, sizeof(tmp16));
                val = le16_to_cpu(tmp16);
            } else if (size == 4) {
                uint32_t tmp32;
                memcpy(&tmp32, s->mmio_backing + addr, sizeof(tmp32));
                val = le32_to_cpu(tmp32);
            }
        }
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t v = (uint32_t)val;

    switch (addr) {
    case NS83820_CR:
        /* implement basic control bits and software interrupt and reset */
        if (v & NS83820_CR_RST) {
            s->cr |= NS83820_CR_RST;
            pcibase_reset(DEVICE(&s->parent_obj));
            s->cr &= ~NS83820_CR_RST;
        }
        if (v & NS83820_CR_TXE) {
            s->cr |= NS83820_CR_TXE;
            s->tx_running = true;
        }
        if (v & NS83820_CR_TXD) {
            s->cr &= ~NS83820_CR_TXE;
            s->tx_running = false;
        }
        if (v & NS83820_CR_RXE) {
            s->cr |= NS83820_CR_RXE;
            s->rx_running = true;
            /* when RX is enabled, hardware starts consuming RX descriptors
             * and will eventually raise RX interrupts; we do not emulate
             * descriptor walks here as format is not fully specified. */
        }
        if (v & NS83820_CR_RXD) {
            s->cr &= ~NS83820_CR_RXE;
            s->rx_running = false;
        }
        if (v & NS83820_CR_SWI) {
            /* software interrupt: set SWI bit in ISR */
            s->isr |= NS83820_ISR_SWI;
            ns83820_update_irq(s);
        }
        break;
    case NS83820_CFG:
        /* cache configuration register; link bits are updated by driver */
        s->CFG_cache = v;
        break;
    case NS83820_MEAR:
        s->mear = v;
        break;
    case NS83820_PTSCR:
        s->ptscr = v;
        break;
    case NS83820_ISR:
        /* write-1-to-clear for interrupt status bits */
        s->isr &= ~v;
        ns83820_update_irq(s);
        break;
    case NS83820_IMR:
        s->imr = v;
        s->IMR_cache = v;
        ns83820_update_irq(s);
        break;
    case NS83820_IER:
        /* global interrupt enable; driver writes 0 to disable */
        s->ier = v;
        ns83820_update_irq(s);
        break;
    case NS83820_IHR:
        /* interrupt holdoff register */
        s->ihr = v;
        s->ihr_shadow = v;
        break;
    case NS83820_TXDP:
        s->txdp_lo = v;
        break;
    case NS83820_TXDP_HI:
        s->txdp_hi = v;
        break;
    case NS83820_TXCFG:
        s->txcfg = v;
        break;
    case NS83820_GPIOR:
        s->gpior = v;
        break;
    case NS83820_RXDP:
        /* driver uses this to program first RX descriptor when kicking RX */
        s->rxdp_lo = v;
        break;
    case NS83820_RXDP_HI:
        s->rxdp_hi = v;
        break;
    case NS83820_RXCFG:
        s->rxcfg = v;
        break;
    case NS83820_PQCR:
        s->pqcr = v;
        break;
    case NS83820_WCSR:
        s->wcsr = v;
        break;
    case NS83820_PCR:
        s->pcr = v;
        break;
    case NS83820_RFCR:
        s->rfcr = v;
        break;
    case NS83820_RFDR:
        /* RFDR is used to access filter data; writes are cached */
        s->rfdr = v;
        break;
    case NS83820_VRCR:
        s->vrcr = v;
        break;
    case NS83820_VTCR:
        s->vtcr = v;
        break;
    case NS83820_VDR:
        s->vdr = v;
        break;
    case NS83820_CCSR:
        s->ccsr = v;
        break;
    case NS83820_TBICR:
        s->tbicr = v;
        break;
    case NS83820_TBISR:
        s->tbisr = v;
        break;
    case NS83820_TANAR:
        s->tanar = v;
        break;
    case NS83820_TANLPAR:
        s->tanlpar = v;
        break;
    case NS83820_TANER:
        s->taner = v;
        break;
    case NS83820_TESR:
        s->tesr = v;
        break;
    default:
        if (addr + size <= s->mmio_backing_size) {
            if (size == 1) {
                s->mmio_backing[addr] = (uint8_t)v;
            } else if (size == 2) {
                uint16_t tmp16 = cpu_to_le16((uint16_t)v);
                memcpy(s->mmio_backing + addr, &tmp16, sizeof(tmp16));
            } else if (size == 4) {
                uint32_t tmp32 = cpu_to_le32(v);
                memcpy(s->mmio_backing + addr, &tmp32, sizeof(tmp32));
            }
        }
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* driver does not use PIO registers; return zero */
    (void)opaque;
    (void)addr;
    (void)size;
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* no PIO behavior is described in the driver */
    (void)opaque;
    (void)addr;
    (void)val;
    (void)size;
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = &s->parent_obj;

    pci_device_reset(pdev);

    if (s->mmio_backing && s->mmio_backing_size) {
        memset(s->mmio_backing, 0, s->mmio_backing_size);
    }

    s->cr = 0;
    s->CFG_cache = 0;
    s->mear = 0;
    s->ptscr = 0;
    s->isr = 0;
    s->imr = 0;
    s->IMR_cache = 0;
    s->ier = 0;
    s->ihr = 0;
    s->ihr_shadow = 0;
    s->txdp_lo = 0;
    s->txdp_hi = 0;
    s->txcfg = 0;
    s->gpior = 0;
    s->rxdp_lo = 0;
    s->rxdp_hi = 0;
    s->rxcfg = 0;
    s->pqcr = 0;
    s->wcsr = 0;
    s->pcr = 0;
    s->rfcr = 0;
    s->rfdr = 0;
    s->srr = 0;
    s->vrcr = NS83820_VRCR_INIT_VALUE;
    s->vtcr = NS83820_VTCR_INIT_VALUE;
    s->vdr = 0;
    s->ccsr = 0;
    s->tbicr = 0;
    s->tbisr = 0;
    s->tanar = 0;
    s->tanlpar = 0;
    s->taner = 0;
    s->tesr = 0;
    s->rx_running = false;
    s->tx_running = false;
    s->linkstate = NS83820_LINK_DOWN;
}

static void pcibase_dma_device_realize(PCIDevice *pdev, Error **errp)
{
  
    
}

static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    uint32_t val = pci_default_read_config(pdev, addr, len);
    return val;
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    pci_default_write_config(pdev, addr, val, len);
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    s->num_bars = 1;
    s->bar_info[0].index = 1;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x1000;
    s->bar_info[0].name = "ns83820-mmio";
    s->bar_info[0].sparse = false;

    pci_config_set_vendor_id(pci_conf, 0x100b);
    pci_config_set_device_id(pci_conf, 0x0022);
    pci_config_set_class(pci_conf, PCI_CLASS_NETWORK_ETHERNET);
    pci_config_set_interrupt_pin(pci_conf, 1);

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
        if (errp && *errp) {
            return;
        }
    }

    s->mmio_backing_size = 0x1000;
    s->mmio_backing = g_malloc0(s->mmio_backing_size);

    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) {
        return;
    }

    s->has_msi = false;
    s->has_msix = false;
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (s->has_msix) {
        msix_uninit(pdev, NULL, 0);
    }
    if (s->has_msi) {
        msi_uninit(pdev);
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

    k->vendor_id = 0x100b;
    k->device_id = 0x0022;
    k->revision = 0x00;
    k->class_id = PCI_CLASS_NETWORK_ETHERNET;

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
