/*
 * This template provides a robust skeleton for hardware emulation.
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

/* Additional include files retrieved from driver context */


#define TYPE_PCIBASE_DEVICE "gem_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_SUN		0x108e
#define PCI_DEVICE_ID_SUN_GEM		0x2bad

#define RXDMA_DONE	0x4104UL
#define RXDCTRL_BUFSZ	0x000000007fff0000ULL
#define RX_RING_SIZE 128
#define RXDCTRL_OWN	0x0000000080000000ULL
#define RXDCTRL_TCPCSUM	0x000000000000ffffULL
#define TX_RING_SIZE 128
#define MAC_LERR	0x611CUL
#define MAC_ECOLL	0x6108UL
#define MAC_LCOLL	0x610CUL
#define MAC_AERR	0x6120UL
#define MAC_FCSERR	0x6124UL
#define MIF_FRAME	0x620CUL
#define GREG_IMASK	0x0010UL
#define PCS_MIISTAT	0x9004UL
#define PCS_ISTAT	0x9018UL
#define MAC_TXSTAT	0x6010UL
#define GREG_BIFCFG	0x1008UL
#define RXDMA_PTHRESH	0x4020UL
#define RXDMA_KICK	0x4100UL
#define RXDMA_DBHI	0x4008UL
#define GREG_SWRST	0x1010UL
#define RXDMA_DBLOW	0x4004UL
#define MAC_RXCFG	0x6034UL
#define MAC_RXRST	0x6004UL
#define RXDMA_CFG	0x4000UL
#define RXDMA_BLANK	0x4108UL
#define MAC_RXSTAT	0x6014UL
#define MAC_SMACHINE	0x6134UL
#define MAC_CSTAT	0x6018UL
#define MIF_STATUS	0x6218UL
#define GREG_PCIESTAT	0x1000UL
#define GREG_STAT	0x000CUL
#define MAC_TXCFG	0x6030UL
#define TXDMA_CFG	0x2004UL
#define TXDMA_KICK	0x2000UL
#define PCS_MIICTRL	0x9000UL
#define PCS_SCTRL	0x9054UL
#define PCS_CFG		0x9010UL
#define PCS_MIIADV	0x9008UL
#define PCS_MIILP	0x900CUL
#define MAC_STIME	0x604CUL
#define MAC_MCCFG	0x6038UL
#define MAC_XIFCFG	0x603CUL
#define PCS_DMODE	0x9050UL
#define MIF_CFG		0x6210UL
#define TXDMA_DBLOW	0x2008UL
#define TXDMA_DBHI	0x200CUL
#define MAC_HASH0	0x60C0UL
#define MAC_AFILT2	0x60ACUL
#define MAC_ADDR6	0x6098UL
#define MAC_MCTYPE	0x6064UL
#define MAC_NCOLL	0x6100UL
#define MAC_ADDR7	0x609CUL
#define MAC_DTIMER	0x6110UL
#define MAC_SNDPAUSE	0x6008UL
#define MAC_AFILT0	0x60A4UL
#define MAC_RXCVERR	0x6128UL
#define MAC_TXMASK	0x6020UL
#define MAC_FASUCC	0x6104UL
#define MAC_AF21MSK	0x60B0UL
#define MAC_MAXFSZ	0x6054UL
#define MAC_MINFSZ	0x6050UL
#define MAC_IPG1	0x6044UL
#define MAC_ADDR2	0x6088UL
#define MAC_ADDR5	0x6094UL
#define MAC_ADDR4	0x6090UL
#define MAC_AF0MSK	0x60B4UL
#define MAC_PATMPS	0x6114UL
#define MAC_JAMSIZE	0x605CUL
#define MAC_RFCTR	0x6118UL
#define MAC_MCMASK	0x6028UL
#define WOL_WAKECSR	0x3010UL
#define MAC_ADDR0	0x6080UL
#define MAC_IPG0	0x6040UL
#define MAC_ADDR8	0x60A0UL
#define MAC_RANDSEED	0x6130UL
#define MAC_ATTLIM	0x6060UL
#define MAC_ADDR1	0x6084UL
#define MAC_PASIZE	0x6058UL
#define MAC_ADDR3	0x608CUL
#define MAC_AFILT1	0x60A8UL
#define MAC_IPG2	0x6048UL
#define GREG_CFG	0x0004UL
#define RXDMA_FSZ	0x4120UL
#define TXDMA_FSZ	0x2118UL
#define MIF_BBOENAB	0x6208UL
#define WOL_MATCH0	0x3000UL
#define WOL_MCOUNT	0x300CUL
#define MIF_BBDATA	0x6204UL
#define MIF_BBCLK	0x6200UL
#define WOL_MATCH2	0x3008UL
#define WOL_MATCH1	0x3004UL
#define MAC_TXRST	0x6000UL

#define TXDCTRL_CENAB	0x0000000020000000ULL
#define TXDCTRL_SOF	0x0000000080000000ULL
#define TXDCTRL_EOF	0x0000000040000000ULL
#define TXDCTRL_INTME	0x0000000100000000ULL
#define RXDCTRL_BAD	0x4000000000000000ULL
#define GREG_SWRST_TXRST	0x00000001
#define GREG_SWRST_RXRST	0x00000002

#define GREG_STAT_TXDONE	0x00000004
#define GREG_STAT_TXNR		0xfff80000
#define GREG_STAT_TXNR_SHIFT	19
#define GREG_STAT_ABNORMAL	(GREG_STAT_RXNOBUF | GREG_STAT_RXTAGERR | \
				 GREG_STAT_PCS | GREG_STAT_TXMAC | GREG_STAT_RXMAC | \
				 GREG_STAT_MAC | GREG_STAT_MIF | GREG_STAT_PCIERR)
#define GREG_STAT_RXNOBUF	0x00000020
#define GREG_STAT_RXTAGERR	0x00000040
#define GREG_STAT_PCS		0x00002000
#define GREG_STAT_TXMAC		0x00004000
#define GREG_STAT_RXMAC		0x00008000
#define GREG_STAT_MAC		0x00010000
#define GREG_STAT_MIF		0x00020000
#define GREG_STAT_PCIERR	0x00040000
#define TXDCTRL_BUFSZ	0x0000000000007fffULL
#define RX_OFFSET          2
#define RX_COPY_THRESHOLD  256
#define MIF_FRAME_PHYAD	0x0f800000
#define MIF_FRAME_REGAD	0x007c0000
#define MIF_FRAME_TAMSB	0x00020000
#define MIF_FRAME_TALSB	0x00010000
#define MIF_FRAME_DATA	0x0000ffff
#define MAC_RXRST_CMD	0x00000001
#define MAC_TXRST_CMD	0x00000001
#define RXDMA_CFG_ENABLE	0x00000001
#define TXDMA_CFG_ENABLE	0x00000001
#define MAC_RXCFG_ENAB	0x00000001
#define MAC_TXCFG_ENAB	0x00000001
#define PCS_MIICTRL_RST	0x00008000
#define PCS_MIICTRL_RAN	0x00000200
#define PCS_MIICTRL_ANE	0x00001000
#define PCS_MIICTRL_WB	0x00004000
#define PCS_CFG_ENABLE	0x00000001
#define PCS_CFG_TO	0x00000020
#define PCS_MIIADV_FD	0x00000020
#define PCS_MIIADV_HD	0x00000040
#define PCS_MIIADV_SP	0x00000080
#define PCS_MIIADV_AP	0x00000100
#define PCS_SCTRL_LOOP	0x00000001
#define PCS_MIISTAT_LS	0x00000004
#define PCS_MIISTAT_ANC	0x00000020
#define PCS_MIISTAT_RF	0x00000010
#define PCS_ISTAT_LSC	0x00000004
#define MAC_TXSTAT_DTE	0x00000080
#define MAC_TXSTAT_URUN	0x00000002
#define MAC_TXSTAT_MPE	0x00000004
#define MAC_TXSTAT_NCE	0x00000008
#define MAC_TXSTAT_ECE	0x00000010
#define MAC_TXSTAT_LCE	0x00000020
#define MAC_RXSTAT_OFLW	0x00000002
#define MAC_RXSTAT_ACE	0x00000008
#define MAC_RXSTAT_CCE	0x00000010
#define MAC_RXSTAT_LCE	0x00000020
#define MAC_CSTAT_PS	0x00000002
#define MAC_CSTAT_PRCV	0x00000001
#define MIF_STATUS_DATA	0xffff0000
#define MIF_STATUS_STAT	0x0000ffff
#define GREG_PCIESTAT_BADACK	0x00000001
#define GREG_PCIESTAT_DTRTO	0x00000002
#define GREG_PCIESTAT_OTHER	0x00000004
#define RXDMA_CFG_FTHRESH_128	0x01000000
#define RXDMA_BLANK_IPKTS	0x000001ff
#define RXDMA_BLANK_ITIME	0x000ff000
#define RXDMA_PTHRESH_OFF	0x000001ff
#define RXDMA_PTHRESH_ON	0x001ff000
#define MAC_RXMASK	0x6024UL
#define MAC_TXSTAT_XMIT	0x00000001
#define MAC_RXSTAT_RCV	0x00000001
#define TXDMA_CFG_PMODE		0x00200000
#define MAC_RXCFG_HFE	0x00000020
#define MAC_RXCFG_PROM	0x00000008
#define MAC_RXCFG_SFCS	0x00000004
#define MAC_TXCFG_EIPG0	0x00000008
#define MAC_TXCFG_NGU	0x00000010
#define MAC_TXCFG_ICS	0x00000002
#define MAC_TXCFG_ICOLL	0x00000004
#define MAC_TXCFG_TCE	0x00000200
#define MAC_RXCFG_RCE	0x00000100
#define MAC_XIFCFG_OE	0x00000001
#define MAC_XIFCFG_LLED	0x00000020
#define MAC_XIFCFG_DISE	0x00000004
#define MAC_XIFCFG_FLED	0x00000040
#define MAC_XIFCFG_GMII	0x00000008
#define MAC_XIFCFG_LBCK	0x00000002
#define MAC_MCCFG_SPE	0x00000001
#define MAC_MCCFG_RPE	0x00000002
#define GREG_CFG_RONPAULBIT	0x00000800
#define GREG_CFG_ENBUG2FIX	0x00001000
#define GREG_CFG_IBURST		0x00000001
#define GREG_CFG_TXDMALIM	0x0000003e
#define GREG_CFG_RXDMALIM	0x000007c0
#define MIF_CFG_PSELECT	0x00000001
#define MIF_CFG_POLL	0x00000002
#define MIF_CFG_BBMODE	0x00000004
#define MIF_CFG_MDI1	0x00000200
#define MIF_CFG_MDI0	0x00000100
#define PCS_DMODE_MGM	0x00000004
#define PCS_DMODE_SM	0x00000001
#define PCS_DMODE_GMOE	0x00000008
#define PCS_DMODE_ESM	0x00000002
#define WOL_MCOUNT_N		0x00000010
#define WOL_MCOUNT_M		0x00000000
#define WOL_WAKECSR_ENABLE	0x00000001
#define WOL_WAKECSR_MII		0x00000002

#define GREG_STAT_TXALL		0x00000002
#define GREG_STAT_TXINTME	0x00000001
#define GREG_STAT_RXDONE	0x00000010
#define RXDMA_CFG_RINGSZ_128	0x00000004
#define TXDMA_CFG_RINGSZ_128	0x00000004

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
    uint32_t greg_imask;
    uint32_t greg_stat;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t mac_txcfg;
    uint32_t mac_rxcfg;
    uint32_t mac_xifcfg;
    uint32_t mac_mccfg;
    uint32_t mif_cfg;
    uint32_t greg_cfg;
    uint32_t greg_bifcfg;
    uint32_t mif_frame;

    /* DMA Context */
    uint32_t rxdma_cfg;
    uint32_t rxdma_dblow;
    uint32_t rxdma_dbhi;
    uint32_t rxdma_kick;
    uint32_t rxdma_blank;
    uint32_t rxdma_pthresh;
    uint32_t rxdma_fsz;

    uint32_t txdma_cfg;
    uint32_t txdma_dblow;
    uint32_t txdma_dbhi;
    uint32_t txdma_kick;
    uint32_t txdma_fsz;

    uint32_t mac_txstat;
    uint32_t mac_rxstat;
    uint32_t mac_cstat;
    uint32_t mif_status;
    uint32_t pcs_miistat;
    uint32_t pcs_istat;
    uint32_t greg_swrst;
    uint32_t mac_txrst;
    uint32_t mac_rxrst;
    uint32_t wol_wakecsr;
    uint32_t wol_match0;
    uint32_t wol_match1;
    uint32_t wol_match2;
    uint32_t wol_mcount;
    uint32_t mac_addr0;
    uint32_t mac_addr1;
    uint32_t mac_addr2;
    uint32_t mac_addr3;
    uint32_t mac_addr4;
    uint32_t mac_addr5;
    uint32_t mac_addr6;
    uint32_t mac_addr7;
    uint32_t mac_addr8;
};

struct gem_rxd {
    uint64_t status_word;
    uint64_t buffer;
};

struct gem_txd {
    uint64_t control_word;
    uint64_t buffer;
};

#define INIT_BLOCK_TX_RING_SIZE 128
#define INIT_BLOCK_RX_RING_SIZE 128

struct gem_init_block {
    struct gem_txd txd[INIT_BLOCK_TX_RING_SIZE];
    struct gem_rxd rxd[INIT_BLOCK_RX_RING_SIZE];
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool level = (s->greg_stat & ~s->greg_imask) != 0;
    pci_set_irq(pdev, level);
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    if (is_write) {
        /* TX DMA Kick */
        s->greg_stat |= GREG_STAT_TXDONE;
        s->greg_stat |= GREG_STAT_TXALL;
        s->greg_stat |= GREG_STAT_TXINTME;
        pcibase_update_irq(s);
    } else {
        /* RX DMA Kick */
        /* Typically we don't do anything on RX kick unless we have pending packets */
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case GREG_IMASK: val = s->greg_imask; break;
    case GREG_STAT: val = s->greg_stat; break;
    case MAC_TXCFG: val = s->mac_txcfg; break;
    case MAC_RXCFG: val = s->mac_rxcfg; break;
    case MAC_XIFCFG: val = s->mac_xifcfg; break;
    case MAC_MCCFG: val = s->mac_mccfg; break;
    case MIF_CFG: val = s->mif_cfg; break;
    case GREG_CFG: val = s->greg_cfg; break;
    case GREG_BIFCFG: val = s->greg_bifcfg; break;
    case RXDMA_CFG: val = s->rxdma_cfg; break;
    case RXDMA_DBLOW: val = s->rxdma_dblow; break;
    case RXDMA_DBHI: val = s->rxdma_dbhi; break;
    case RXDMA_KICK: val = s->rxdma_kick; break;
    case RXDMA_BLANK: val = s->rxdma_blank; break;
    case RXDMA_PTHRESH: val = s->rxdma_pthresh; break;
    case RXDMA_FSZ: val = s->rxdma_fsz; break;
    case TXDMA_CFG: val = s->txdma_cfg; break;
    case TXDMA_DBLOW: val = s->txdma_dblow; break;
    case TXDMA_DBHI: val = s->txdma_dbhi; break;
    case TXDMA_KICK: val = s->txdma_kick; break;
    case TXDMA_FSZ: val = s->txdma_fsz; break;
    case MAC_TXSTAT: val = s->mac_txstat; break;
    case MAC_RXSTAT: val = s->mac_rxstat; break;
    case MAC_CSTAT: val = s->mac_cstat; break;
    case MIF_STATUS: val = s->mif_status; break;
    case PCS_MIISTAT: val = s->pcs_miistat; break;
    case PCS_ISTAT: val = s->pcs_istat; break;
    case GREG_SWRST: val = s->greg_swrst; break;
    case MAC_TXRST: val = s->mac_txrst; break;
    case MAC_RXRST: val = s->mac_rxrst; break;
    case WOL_WAKECSR: val = s->wol_wakecsr; break;
    case WOL_MATCH0: val = s->wol_match0; break;
    case WOL_MATCH1: val = s->wol_match1; break;
    case WOL_MATCH2: val = s->wol_match2; break;
    case WOL_MCOUNT: val = s->wol_mcount; break;
    case MAC_ADDR0: val = s->mac_addr0; break;
    case MAC_ADDR1: val = s->mac_addr1; break;
    case MAC_ADDR2: val = s->mac_addr2; break;
    case MAC_ADDR3: val = s->mac_addr3; break;
    case MAC_ADDR4: val = s->mac_addr4; break;
    case MAC_ADDR5: val = s->mac_addr5; break;
    case MAC_ADDR6: val = s->mac_addr6; break;
    case MAC_ADDR7: val = s->mac_addr7; break;
    case MAC_ADDR8: val = s->mac_addr8; break;
    case RXDMA_DONE: val = s->rxdma_kick; break;
    case MIF_FRAME: val = s->mif_frame; break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case GREG_IMASK: s->greg_imask = val; pcibase_update_irq(s); break;
    case GREG_STAT: s->greg_stat &= ~val; pcibase_update_irq(s); break;
    case MAC_TXCFG: s->mac_txcfg = val; break;
    case MAC_RXCFG: s->mac_rxcfg = val; break;
    case MAC_XIFCFG: s->mac_xifcfg = val; break;
    case MAC_MCCFG: s->mac_mccfg = val; break;
    case MIF_CFG: s->mif_cfg = val; break;
    case GREG_CFG: s->greg_cfg = val; break;
    case GREG_BIFCFG: s->greg_bifcfg = val; break;
    case RXDMA_CFG: s->rxdma_cfg = val; break;
    case RXDMA_DBLOW: s->rxdma_dblow = val; break;
    case RXDMA_DBHI: s->rxdma_dbhi = val; break;
    case RXDMA_KICK: s->rxdma_kick = val; pcibase_do_dma(s, false); break;
    case RXDMA_BLANK: s->rxdma_blank = val; break;
    case RXDMA_PTHRESH: s->rxdma_pthresh = val; break;
    case RXDMA_FSZ: s->rxdma_fsz = val; break;
    case TXDMA_CFG: s->txdma_cfg = val; break;
    case TXDMA_DBLOW: s->txdma_dblow = val; break;
    case TXDMA_DBHI: s->txdma_dbhi = val; break;
    case TXDMA_KICK: s->txdma_kick = val; pcibase_do_dma(s, true); break;
    case TXDMA_FSZ: s->txdma_fsz = val; break;
    case MAC_TXSTAT: s->mac_txstat = val; break;
    case MAC_RXSTAT: s->mac_rxstat = val; break;
    case MAC_CSTAT: s->mac_cstat = val; break;
    case MIF_STATUS: s->mif_status = val; break;
    case PCS_MIISTAT: s->pcs_miistat = val; break;
    case PCS_ISTAT: s->pcs_istat = val; break;
    case GREG_SWRST: 
        s->greg_swrst = val & ~(GREG_SWRST_TXRST | GREG_SWRST_RXRST); 
        break;
    case MAC_TXRST: 
        if (val & MAC_TXRST_CMD) {
            s->mac_txrst = 0;
        } else {
            s->mac_txrst = val;
        }
        break;
    case MAC_RXRST: 
        if (val & MAC_RXRST_CMD) {
            s->mac_rxrst = 0;
        } else {
            s->mac_rxrst = val;
        }
        break;
    case WOL_WAKECSR: s->wol_wakecsr = val; break;
    case WOL_MATCH0: s->wol_match0 = val; break;
    case WOL_MATCH1: s->wol_match1 = val; break;
    case WOL_MATCH2: s->wol_match2 = val; break;
    case WOL_MCOUNT: s->wol_mcount = val; break;
    case MAC_ADDR0: s->mac_addr0 = val; break;
    case MAC_ADDR1: s->mac_addr1 = val; break;
    case MAC_ADDR2: s->mac_addr2 = val; break;
    case MAC_ADDR3: s->mac_addr3 = val; break;
    case MAC_ADDR4: s->mac_addr4 = val; break;
    case MAC_ADDR5: s->mac_addr5 = val; break;
    case MAC_ADDR6: s->mac_addr6 = val; break;
    case MAC_ADDR7: s->mac_addr7 = val; break;
    case MAC_ADDR8: s->mac_addr8 = val; break;
    case MIF_FRAME: s->mif_frame = val; break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    
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

    s->greg_imask = 0xFFFFFFFF;
    s->greg_stat = 0;
    s->mac_txcfg = 0;
    s->mac_rxcfg = 0;
    s->mac_xifcfg = 0;
    s->mac_mccfg = 0;
    s->mif_cfg = 0;
    s->greg_cfg = 0;
    s->greg_bifcfg = 0;
    s->mif_frame = 0;
    s->rxdma_cfg = 0;
    s->rxdma_dblow = 0;
    s->rxdma_dbhi = 0;
    s->rxdma_kick = 0;
    s->rxdma_blank = 0;
    s->rxdma_pthresh = 0;
    s->rxdma_fsz = 320;
    s->txdma_cfg = 0;
    s->txdma_dblow = 0;
    s->txdma_dbhi = 0;
    s->txdma_kick = 0;
    s->txdma_fsz = 144;
    s->mac_txstat = 0;
    s->mac_rxstat = 0;
    s->mac_cstat = 0;
    s->mif_status = 0;
    s->pcs_miistat = 0;
    s->pcs_istat = 0;
    s->greg_swrst = 0;
    s->mac_txrst = 0;
    s->mac_rxrst = 0;
    s->wol_wakecsr = 0;
    s->wol_match0 = 0;
    s->wol_match1 = 0;
    s->wol_match2 = 0;
    s->wol_mcount = 0;
    s->mac_addr0 = 0;
    s->mac_addr1 = 0;
    s->mac_addr2 = 0;
    s->mac_addr3 = 0;
    s->mac_addr4 = 0;
    s->mac_addr5 = 0;
    s->mac_addr6 = 0;
    s->mac_addr7 = 0;
    s->mac_addr8 = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_SUN );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_SUN_GEM );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_ETHERNET );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x10000;
    s->bar_info[0].name = "gem-regs";
      
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

       /* Set DMA masks or ring buffer limits */
     /* Initialize internal hardware timers if used */
       /* Final state initialization before the device is 'live' */
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }

     /* Free buffers, stop timers, etc. */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "gem_pci",
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
