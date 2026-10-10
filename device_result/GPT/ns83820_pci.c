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

#define DRV_NAME "ns83820"
#define VERSION "0.23"

#define CR              0x00
#define CFG             0x04
#define MEAR            0x08
#define PTSCR           0x0c
#define ISR             0x10
#define IMR             0x14
#define IER             0x18
#define IHR             0x1c
#define TXDP            0x20
#define TXDP_HI         0x24
#define TXCFG           0x28
#define GPIOR           0x2c
#define RXDP            0x30
#define RXDP_HI         0x34
#define RXCFG           0x38
#define PQCR            0x3c
#define WCSR            0x40
#define PCR             0x44
#define RFCR            0x48
#define RFDR            0x4c
#define SRR             0x58
#define VRCR            0xbc
#define VTCR            0xc0
#define VDR             0xc4
#define CCSR            0xcc
#define TBICR           0xe0
#define TBISR           0xe4
#define TANAR           0xe8
#define TANLPAR         0xec
#define TANER           0xf0
#define TESR            0xf4

#define LINK_UP         0x04
#define LINK_DOWN       0x02

#define CR_RST          0x00000100
#define CR_TXE          0x00000001
#define CR_TXD          0x00000002
#define CR_RXE          0x00000004
#define CR_RXD          0x00000008
#define CR_TXR          0x00000010
#define CR_RXR          0x00000020
#define CR_SWI          0x00000080

#define ISR_TXERR       0x00000100
#define ISR_PHY         0x00004000
#define ISR_TXDESC3     0x40000000
#define ISR_TXDESC2     0x20000000
#define ISR_TXDESC1     0x10000000
#define ISR_TXDESC0     0x08000000
#define ISR_RXDESC3     0x04000000
#define ISR_RXDESC2     0x02000000
#define ISR_RXDESC1     0x01000000
#define ISR_RXDESC0     0x00800000
#define ISR_TXRCMP      0x00400000
#define ISR_RXRCMP      0x00200000
#define ISR_DPERR       0x00100000
#define ISR_SSERR       0x00080000
#define ISR_RMABT       0x00040000
#define ISR_RTABT       0x00020000
#define ISR_RXSOVR      0x00010000
#define ISR_HIBINT      0x00008000
#define ISR_PME         0x00002000
#define ISR_SWI         0x00001000
#define ISR_MIB         0x00000800
#define ISR_TXURN       0x00000400
#define ISR_TXIDLE      0x00000200
#define ISR_TXDESC      0x00000080
#define ISR_TXOK        0x00000040
#define ISR_RXORN       0x00000020
#define ISR_RXIDLE      0x00000010
#define ISR_RXEARLY     0x00000008
#define ISR_RXERR       0x00000004
#define ISR_RXDESC      0x00000002
#define ISR_RXOK        0x00000001

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

#define TBICR_MR_AN_ENABLE   0x00001000
#define TBICR_MR_RESTART_AN  0x00000200

#define TBISR_MR_LINK_STATUS 0x00000020
#define TBISR_MR_AN_COMPLETE 0x00000004

#define TANAR_PS2       0x00000100
#define TANAR_PS1       0x00000080
#define TANAR_HALF_DUP  0x00000040
#define TANAR_FULL_DUP  0x00000020

#define GPIOR_GP5_OE    0x00000200
#define GPIOR_GP4_OE    0x00000100
#define GPIOR_GP3_OE    0x00000080
#define GPIOR_GP2_OE    0x00000040
#define GPIOR_GP1_OE    0x00000020
#define GPIOR_GP3_OUT   0x00000004
#define GPIOR_GP1_OUT   0x00000001

#define LINK_AUTONEGOTIATE 0x01

#define RX_BUF_SIZE     1500
#define DESC_SIZE       8
#define NR_RX_DESC      64
#define NR_TX_DESC      128
#define REAL_RX_BUF_SIZE (RX_BUF_SIZE + 14)
#define MIN_TX_DESC_FREE 8

#define CMDSTS_OWN      0x80000000
#define CMDSTS_MORE     0x40000000
#define CMDSTS_INTR     0x20000000
#define CMDSTS_ERR      0x10000000
#define CMDSTS_OK       0x08000000
#define CMDSTS_RUNT     0x00200000
#define CMDSTS_LEN_MASK 0x0000ffff
#define CMDSTS_DEST_MASK    0x01800000
#define CMDSTS_DEST_SELF    0x00800000
#define CMDSTS_DEST_MULTI   0x01000000

#define VRCR_INIT_VALUE (VRCR_IPEN|VRCR_VTDEN|VRCR_VTREN)
#define VTCR_INIT_VALUE (VTCR_PPCHK|VTCR_VPPTI)

#define VENDOR_ID 0x100b
#define DEVICE_ID 0x0022
#define CLASS_ID  PCI_CLASS_NETWORK_ETHERNET

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

    /* minimal register state */
    uint32_t reg_cr;
    uint32_t reg_cfg;
    uint32_t reg_mear;
    uint32_t reg_ptscr;
    uint32_t reg_isr;
    uint32_t reg_imr;
    uint32_t reg_ier;
    uint32_t reg_ihr;
    uint32_t reg_txdp;
    uint32_t reg_txdp_hi;
    uint32_t reg_txcfg;
    uint32_t reg_gpior;
    uint32_t reg_rxdp;
    uint32_t reg_rxdp_hi;
    uint32_t reg_rxcfg;
    uint32_t reg_pqcr;
    uint32_t reg_wcsr;
    uint32_t reg_pcr;
    uint32_t reg_rfcr;
    uint32_t reg_rfdr;
    uint32_t reg_srr;
    uint32_t reg_vrcr;
    uint32_t reg_vtcr;
    uint32_t reg_vdr;
    uint32_t reg_ccsr;
    uint32_t reg_tbicr;
    uint32_t reg_tbisr;
    uint32_t reg_tanar;
    uint32_t reg_tanlpar;
    uint32_t reg_taner;
    uint32_t reg_tesr;

    /* simple link state */
    uint32_t linkstate;

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

static void ns83820_update_irq(PCIBaseState *s)
{
    bool active = (s->reg_isr & s->reg_imr) != 0;
    if (active) {
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
    case CR:
        val = s->reg_cr;
        break;
    case CFG:
        val = s->reg_cfg;
        break;
    case MEAR:
        val = s->reg_mear;
        break;
    case PTSCR:
        val = s->reg_ptscr;
        break;
    case ISR:
        val = s->reg_isr;
        break;
    case IMR:
        val = s->reg_imr;
        break;
    case IER:
        val = s->reg_ier;
        break;
    case IHR:
        val = s->reg_ihr;
        break;
    case TXDP:
        val = s->reg_txdp;
        break;
    case TXDP_HI:
        val = s->reg_txdp_hi;
        break;
    case TXCFG:
        val = s->reg_txcfg;
        break;
    case GPIOR:
        val = s->reg_gpior;
        break;
    case RXDP:
        val = s->reg_rxdp;
        break;
    case RXDP_HI:
        val = s->reg_rxdp_hi;
        break;
    case RXCFG:
        val = s->reg_rxcfg;
        break;
    case PQCR:
        val = s->reg_pqcr;
        break;
    case WCSR:
        val = s->reg_wcsr;
        break;
    case PCR:
        val = s->reg_pcr;
        break;
    case RFCR:
        val = s->reg_rfcr;
        break;
    case RFDR:
        val = s->reg_rfdr;
        break;
    case SRR:
        val = s->reg_srr;
        break;
    case VRCR:
        val = s->reg_vrcr;
        break;
    case VTCR:
        val = s->reg_vtcr;
        break;
    case VDR:
        val = s->reg_vdr;
        break;
    case CCSR:
        val = s->reg_ccsr;
        break;
    case TBICR:
        val = s->reg_tbicr;
        break;
    case TBISR:
        val = s->reg_tbisr;
        break;
    case TANAR:
        val = s->reg_tanar;
        break;
    case TANLPAR:
        val = s->reg_tanlpar;
        break;
    case TANER:
        val = s->reg_taner;
        break;
    case TESR:
        val = s->reg_tesr;
        break;
    default:
        val = 0;
        break;
    }

    if (size == 1) {
        val &= 0xff;
    } else if (size == 2) {
        val &= 0xffff;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t v = (uint32_t)val;

    switch (addr) {
    case CR:
        if (v & CR_RST) {
            s->reg_cr |= (v & CR_RST);
            s->reg_cr &= ~CR_RST;
        } else {
            s->reg_cr = v;
        }
        break;
    case CFG:
        s->reg_cfg = v;
        break;
    case MEAR:
        s->reg_mear = v;
        break;
    case PTSCR:
        s->reg_ptscr = v;
        break;
    case ISR:
        s->reg_isr &= ~v;
        ns83820_update_irq(s);
        break;
    case IMR:
        s->reg_imr = v;
        ns83820_update_irq(s);
        break;
    case IER:
        s->reg_ier = v;
        break;
    case IHR:
        s->reg_ihr = v;
        break;
    case TXDP:
        s->reg_txdp = v;
        break;
    case TXDP_HI:
        s->reg_txdp_hi = v;
        break;
    case TXCFG:
        s->reg_txcfg = v;
        break;
    case GPIOR:
        s->reg_gpior = v;
        break;
    case RXDP:
        s->reg_rxdp = v;
        break;
    case RXDP_HI:
        s->reg_rxdp_hi = v;
        break;
    case RXCFG:
        s->reg_rxcfg = v;
        break;
    case PQCR:
        s->reg_pqcr = v;
        break;
    case WCSR:
        s->reg_wcsr = v;
        break;
    case PCR:
        s->reg_pcr = v;
        break;
    case RFCR:
        s->reg_rfcr = v;
        break;
    case RFDR:
        s->reg_rfdr = v;
        break;
    case SRR:
        s->reg_srr = v;
        break;
    case VRCR:
        s->reg_vrcr = v;
        break;
    case VTCR:
        s->reg_vtcr = v;
        break;
    case VDR:
        s->reg_vdr = v;
        break;
    case CCSR:
        s->reg_ccsr = v;
        break;
    case TBICR:
        s->reg_tbicr = v;
        break;
    case TBISR:
        s->reg_tbisr = v;
        break;
    case TANAR:
        s->reg_tanar = v;
        break;
    case TANLPAR:
        s->reg_tanlpar = v;
        break;
    case TANER:
        s->reg_taner = v;
        break;
    case TESR:
        s->reg_tesr = v;
        break;
    default:
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    pci_device_reset(pdev);

    s->reg_cr = 0;
    s->reg_cfg = 0;
    s->reg_mear = 0;
    s->reg_ptscr = 0;
    s->reg_isr = 0;
    s->reg_imr = 0;
    s->reg_ier = 0;
    s->reg_ihr = 0;
    s->reg_txdp = 0;
    s->reg_txdp_hi = 0;
    s->reg_txcfg = 0;
    s->reg_gpior = 0;
    s->reg_rxdp = 0;
    s->reg_rxdp_hi = 0;
    s->reg_rxcfg = 0;
    s->reg_pqcr = 0;
    s->reg_wcsr = 0;
    s->reg_pcr = 0;
    s->reg_rfcr = 0;
    s->reg_rfdr = 0;
    s->reg_srr = 0;
    s->reg_vrcr = 0;
    s->reg_vtcr = 0;
    s->reg_vdr = 0;
    s->reg_ccsr = 0;
    s->reg_tbicr = 0;
    s->reg_tbisr = 0;
    s->reg_tanar = 0;
    s->reg_tanlpar = 0;
    s->reg_taner = 0;
    s->reg_tesr = 0;
    s->linkstate = LINK_DOWN;

    if (s->mmio_backing && s->mmio_backing_size) {
        memset(s->mmio_backing, 0, s->mmio_backing_size);
    }
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
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    s->num_bars = 1;
    s->bar_info[0].index = 1;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x1000;
    s->bar_info[0].name = "ns83820-mmio";
    s->bar_info[0].sparse = false;

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) {
        return;
    }

    s->reg_cfg = 0;
    s->reg_vrcr = VRCR_INIT_VALUE;
    s->reg_vtcr = VTCR_INIT_VALUE;
    s->reg_srr = 0;
    s->linkstate = LINK_DOWN;
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

