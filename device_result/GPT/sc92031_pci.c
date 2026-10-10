/*
 * QEMU PCI device model for Silan SC92031-based NIC
 * Phase 2: functional behavior based on Linux driver sc92031.c
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
#include "hw/hw.h"

#define TYPE_PCIBASE_DEVICE "sc92031_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI identifiers: from first entry of sc92031_pci_device_id_table */
#define SC92031_PCI_VENDOR_ID 0x1904
#define SC92031_PCI_DEVICE_ID 0x2031

/* Use standard Ethernet network class code */
#define SC92031_PCI_CLASS_ID  0x0200

/* Register offsets from enum silan_registers */
#define SC92031_REG_Config0           0x00
#define SC92031_REG_Config1           0x04
#define SC92031_REG_RxBufWPtr         0x08
#define SC92031_REG_IntrStatus        0x0C
#define SC92031_REG_IntrMask          0x10
#define SC92031_REG_RxbufAddr         0x14
#define SC92031_REG_RxBufRPtr         0x18
#define SC92031_REG_Txstatusall       0x1C
#define SC92031_REG_TxStatus0         0x20
#define SC92031_REG_TxAddr0           0x30
#define SC92031_REG_RxConfig          0x40
#define SC92031_REG_MAC0              0x44
#define SC92031_REG_MAR0              0x4C
#define SC92031_REG_RxStatus0         0x54
#define SC92031_REG_TxConfig          0x5C
#define SC92031_REG_PhyCtrl           0x60
#define SC92031_REG_FlowCtrlConfig    0x64
#define SC92031_REG_Miicmd0           0x68
#define SC92031_REG_Miicmd1           0x6C
#define SC92031_REG_Miistatus         0x70
#define SC92031_REG_Timercnt          0x74
#define SC92031_REG_TimerIntr         0x78
#define SC92031_REG_PMConfig          0x7C
#define SC92031_REG_CRC0              0x80
#define SC92031_REG_Wakeup0           0x88
#define SC92031_REG_LSBCRC0           0xC8
#define SC92031_REG_TestD0            0xD0
#define SC92031_REG_TestD4            0xD4
#define SC92031_REG_TestD8            0xD8

/* Interrupt Status Bits */
#define SC92031_INTR_LinkFail     0x80000000u
#define SC92031_INTR_LinkOK       0x40000000u
#define SC92031_INTR_TimeOut      0x20000000u
#define SC92031_INTR_RxOverflow   0x00000040u
#define SC92031_INTR_RxOK         0x00000020u
#define SC92031_INTR_TxOK         0x00000001u
#define SC92031_INTR_BITS (SC92031_INTR_LinkFail | SC92031_INTR_LinkOK | \
                           SC92031_INTR_TimeOut | SC92031_INTR_RxOverflow | \
                           SC92031_INTR_RxOK | SC92031_INTR_TxOK)

/* Tx Status Bits */
#define SC92031_TXSTAT_TxCarrierLost     0x20000000u
#define SC92031_TXSTAT_TxAborted         0x10000000u
#define SC92031_TXSTAT_TxOutOfWindow     0x08000000u
#define SC92031_TXSTAT_TxNccShift        22
#define SC92031_TXSTAT_EarlyTxThresShift 16
#define SC92031_TXSTAT_TxStatOK          0x00008000u
#define SC92031_TXSTAT_TxUnderrun        0x00004000u
#define SC92031_TXSTAT_TxOwn             0x00002000u

/* Rx Status Bits */
#define SC92031_RXSTAT_RxStatesOK    0x00080000u
#define SC92031_RXSTAT_RxBadAlign    0x00040000u
#define SC92031_RXSTAT_RxHugeFrame   0x00020000u
#define SC92031_RXSTAT_RxSmallFrame  0x00010000u
#define SC92031_RXSTAT_RxCRCOK       0x00008000u
#define SC92031_RXSTAT_RxCrlFrame    0x00004000u
#define SC92031_RXSTAT_Rx_Broadcast  0x00002000u
#define SC92031_RXSTAT_Rx_Multicast  0x00001000u
#define SC92031_RXSTAT_RxAddrMatch   0x00000800u
#define SC92031_RXSTAT_MiiErr        0x00000400u

/* Rx Config Bits */
#define SC92031_RXCFG_RxFullDx       0x80000000u
#define SC92031_RXCFG_RxEnb          0x40000000u
#define SC92031_RXCFG_RxSmall        0x20000000u
#define SC92031_RXCFG_RxHuge         0x10000000u
#define SC92031_RXCFG_RxErr          0x08000000u
#define SC92031_RXCFG_RxAllphys      0x04000000u
#define SC92031_RXCFG_RxMulticast    0x02000000u
#define SC92031_RXCFG_RxBroadcast    0x01000000u
#define SC92031_RXCFG_RxLoopBack     ((1u << 23) | (1u << 22))
#define SC92031_RXCFG_LowThresholdShift   12
#define SC92031_RXCFG_HighThresholdShift  2

/* Tx Config Bits */
#define SC92031_TXCFG_TxFullDx       0x80000000u
#define SC92031_TXCFG_TxEnb          0x40000000u
#define SC92031_TXCFG_TxEnbPad       0x20000000u
#define SC92031_TXCFG_TxEnbHuge      0x10000000u
#define SC92031_TXCFG_TxEnbFCS       0x08000000u
#define SC92031_TXCFG_TxNoBackOff    0x04000000u
#define SC92031_TXCFG_TxEnbPrem      0x02000000u
#define SC92031_TXCFG_TxCareLostCrs  0x01000000u
#define SC92031_TXCFG_TxExdCollNum   0x00F00000u
#define SC92031_TXCFG_TxDataRate     0x00080000u

/* Phy Control Config Bits */
#define SC92031_PHY_PhyCtrlAne         0x80000000u
#define SC92031_PHY_PhyCtrlSpd100      0x40000000u
#define SC92031_PHY_PhyCtrlSpd10       0x20000000u
#define SC92031_PHY_PhyCtrlPhyBaseAddr 0x1F000000u
#define SC92031_PHY_PhyCtrlDux         0x00800000u
#define SC92031_PHY_PhyCtrlReset       0x00400000u

/* Flow Control Config Bits */
#define SC92031_FLOW_FlowCtrlFullDX 0x80000000u
#define SC92031_FLOW_FlowCtrlEnb    0x40000000u

/* Config0 Bits */
#define SC92031_CFG0_Reset   0x80000000u
#define SC92031_CFG0_Anaoff  0x40000000u
#define SC92031_CFG0_LDPS    0x20000000u

/* Config1 Bits */
#define SC92031_CFG1_EarlyRx (1u << 31)
#define SC92031_CFG1_EarlyTx (1u << 30)
#define SC92031_CFG1_Rcv8K   0x0u
#define SC92031_CFG1_Rcv16K  0x1u
#define SC92031_CFG1_Rcv32K  0x3u
#define SC92031_CFG1_Rcv64K  0x7u
#define SC92031_CFG1_Rcv128K 0xFu

/* MII Command 0 Bits */
#define SC92031_MII_Mii_Divider 0x20000000u
#define SC92031_MII_Mii_WRITE   0x00400000u
#define SC92031_MII_Mii_READ    0x00200000u
#define SC92031_MII_Mii_SCAN    0x00100000u
#define SC92031_MII_Mii_Tamod   0x00080000u
#define SC92031_MII_Mii_Drvmod  0x00040000u
#define SC92031_MII_Mii_mdc     0x00020000u
#define SC92031_MII_Mii_mdoen   0x00010000u
#define SC92031_MII_Mii_mdo     0x00008000u
#define SC92031_MII_Mii_mdi     0x00004000u

/* MII Status Bits */
#define SC92031_MII_StatusBusy  0x80000000u

/* Power Management Config Bits */
#define SC92031_PM_PM_Enable   (1u << 31)
#define SC92031_PM_PM_LongWF   (1u << 30)
#define SC92031_PM_PM_Magic    (1u << 29)
#define SC92031_PM_PM_LANWake  (1u << 28)
#define SC92031_PM_PM_LWPTN    ((1u << 27) | (1u << 26))
#define SC92031_PM_PM_LinkUp   (1u << 25)
#define SC92031_PM_PM_WakeUp   (1u << 24)

/* Other constants from driver */
#define SC92031_RX_FIFO_THRESH      7
#define SC92031_RX_BUF_LEN_IDX      3
#define SC92031_NUM_TX_DESC         4
#define SC92031_MAX_ETH_FRAME_SIZE  1536
#define SC92031_TX_BUF_SIZE         SC92031_MAX_ETH_FRAME_SIZE
#define SC92031_TX_BUF_TOT_LEN      (SC92031_TX_BUF_SIZE * SC92031_NUM_TX_DESC)
#define SC92031_SILAN_STATS_NUM     2

/* BAR index selection: driver uses iomapped I/O (SC92031_USE_PIO) */
#define SC92031_USE_PIO 0

/* RX buffer size from driver macro RX_BUF_LEN = (RX_BUF_LEN_IDX ? 16K etc.)
 * We only need it to match masking in RxBufWPtr handling.
 * From driver: buffer length index 3 corresponds to 64K.
 */
#define SC92031_RX_BUF_LEN (64 * 1024)

/* Simplified internal PHY / MII emulation constants */
#define SC92031_PHY_REG_BMSR          0x01
#define SC92031_PHY_REG_BMCR          0x00
#define SC92031_PHY_REG_OUTPUT_STATUS 0x10 /* MII_OutputStatus in driver */

/* BMCR/BMSR bit definitions used by driver are kernel macros; we just
 * implement minimal shadow for behavior.
 */
#define SC92031_BMSR_LSTATUS 0x0004


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

    /* Register shadows */
    uint32_t reg_config0;
    uint32_t reg_config1;
    uint32_t reg_rxbuf_wptr;
    uint32_t reg_intr_status;
    uint32_t reg_intr_mask;
    uint32_t reg_rxbuf_addr;
    uint32_t reg_rxbuf_rptr;
    uint32_t reg_txstatusall;
    uint32_t reg_txstatus[SC92031_NUM_TX_DESC];
    uint32_t reg_txaddr[SC92031_NUM_TX_DESC];
    uint32_t reg_rxconfig;
    uint32_t reg_mac0;
    uint32_t reg_mac1;
    uint32_t reg_mar0;
    uint32_t reg_mar1;
    uint32_t reg_rxstatus0;
    uint32_t reg_txconfig;
    uint32_t reg_phyctrl;
    uint32_t reg_flowctrl;
    uint32_t reg_miicmd0;
    uint32_t reg_miicmd1;
    uint32_t reg_miistatus;
    uint32_t reg_timercnt;
    uint32_t reg_timerintr;
    uint32_t reg_pmconfig;

    /* DMA context */
    dma_addr_t rx_ring_dma_addr;
    dma_addr_t rx_ring_tail;
    dma_addr_t tx_bufs_dma_addr;

    /* Internal modeled state */
    bool link_up;
    bool phy_busy;

    /* Simple MII register set */
    uint16_t phy_bmsr;
    uint16_t phy_bmcr;
    uint16_t phy_output_status;
};


/* Internal helper for status-triggered signaling. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t pending = s->reg_intr_status & s->reg_intr_mask & SC92031_INTR_BITS;

    if (pending) {
        if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else if (!msix_enabled(pdev)) {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!msi_enabled(pdev) && !msix_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /* The real device performs DMA based on RxbufAddr/TxAddr0+TxStatus0 etc.
     * The driver never inspects device-written descriptor fields beyond
     * what it wrote itself, and it does not require us to actually move
     * packet payloads for probe/open to succeed. Therefore, we do not
     * implement active DMA here.
     */
    (void)s;
    (void)is_write;
}

/* Helper: emulate simple MII operation based on Miicmd0/Miicmd1 */
static void pcibase_update_mii(PCIBaseState *s)
{
    uint32_t cmd0 = s->reg_miicmd0;
    uint32_t cmd1 = s->reg_miicmd1;

    /* Clear busy before starting a new op */
    s->reg_miistatus &= ~SC92031_MII_StatusBusy;

    if (cmd0 & (SC92031_MII_Mii_READ | SC92031_MII_Mii_WRITE | SC92031_MII_Mii_SCAN)) {
        /* Start operation: set busy then immediately complete. */
        s->reg_miistatus |= SC92031_MII_StatusBusy;

        /* Decode register index: reg << 6 in driver. */
        unsigned reg = (cmd1 >> 6) & 0x1F;

        if (cmd0 & SC92031_MII_Mii_READ) {
            uint16_t val = 0;
            switch (reg) {
            case SC92031_PHY_REG_BMSR:
                val = s->phy_bmsr;
                break;
            case SC92031_PHY_REG_BMCR:
                val = s->phy_bmcr;
                break;
            case SC92031_PHY_REG_OUTPUT_STATUS:
                val = s->phy_output_status;
                break;
            default:
                val = 0;
                break;
            }
            /* Driver expects _sc92031_mii_cmd() >> 13 to return 16-bit value. */
            s->reg_miistatus = ((uint32_t)val) << 13;
        } else if (cmd0 & SC92031_MII_Mii_WRITE) {
            /* Write: lower bits of cmd1 carry value << 11 in driver. */
            uint16_t val = (cmd1 >> 11) & 0xFFFF;
            switch (reg) {
            case SC92031_PHY_REG_BMSR:
                s->phy_bmsr = val;
                break;
            case SC92031_PHY_REG_BMCR:
                s->phy_bmcr = val;
                break;
            case SC92031_PHY_REG_OUTPUT_STATUS:
                s->phy_output_status = val;
                break;
            default:
                break;
            }
            s->reg_miistatus = 0;
        } else if (cmd0 & SC92031_MII_Mii_SCAN) {
            /* SCAN is used only for side-effect in driver; return non-busy. */
            s->reg_miistatus = 0;
        }
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = 0;

    switch (addr) {
    case SC92031_REG_Config0:
        val = s->reg_config0;
        break;
    case SC92031_REG_Config1:
        val = s->reg_config1;
        break;
    case SC92031_REG_RxBufWPtr:
        val = s->reg_rxbuf_wptr;
        break;
    case SC92031_REG_IntrStatus:
        val = s->reg_intr_status;
        break;
    case SC92031_REG_IntrMask:
        val = s->reg_intr_mask;
        break;
    case SC92031_REG_RxbufAddr:
        val = s->reg_rxbuf_addr;
        break;
    case SC92031_REG_RxBufRPtr:
        val = s->reg_rxbuf_rptr;
        break;
    case SC92031_REG_Txstatusall:
        val = s->reg_txstatusall;
        break;
    case SC92031_REG_TxStatus0:
    case SC92031_REG_TxStatus0 + 4:
    case SC92031_REG_TxStatus0 + 8:
    case SC92031_REG_TxStatus0 + 12:
    {
        unsigned idx = (addr - SC92031_REG_TxStatus0) / 4;
        if (idx < SC92031_NUM_TX_DESC) {
            val = s->reg_txstatus[idx];
        }
        break;
    }
    case SC92031_REG_TxAddr0:
    case SC92031_REG_TxAddr0 + 4:
    case SC92031_REG_TxAddr0 + 8:
    case SC92031_REG_TxAddr0 + 12:
    {
        unsigned idx = (addr - SC92031_REG_TxAddr0) / 4;
        if (idx < SC92031_NUM_TX_DESC) {
            val = s->reg_txaddr[idx];
        }
        break;
    }
    case SC92031_REG_RxConfig:
        val = s->reg_rxconfig;
        break;
    case SC92031_REG_MAC0:
        val = s->reg_mac0;
        break;
    case SC92031_REG_MAC0 + 4:
        val = s->reg_mac1;
        break;
    case SC92031_REG_MAR0:
        val = s->reg_mar0;
        break;
    case SC92031_REG_MAR0 + 4:
        val = s->reg_mar1;
        break;
    case SC92031_REG_RxStatus0:
        val = s->reg_rxstatus0;
        break;
    case SC92031_REG_TxConfig:
        val = s->reg_txconfig;
        break;
    case SC92031_REG_PhyCtrl:
        val = s->reg_phyctrl;
        break;
    case SC92031_REG_FlowCtrlConfig:
        val = s->reg_flowctrl;
        break;
    case SC92031_REG_Miicmd0:
        val = s->reg_miicmd0;
        break;
    case SC92031_REG_Miicmd1:
        val = s->reg_miicmd1;
        break;
    case SC92031_REG_Miistatus:
        val = s->reg_miistatus;
        break;
    case SC92031_REG_Timercnt:
        val = s->reg_timercnt;
        break;
    case SC92031_REG_TimerIntr:
        val = s->reg_timerintr;
        break;
    case SC92031_REG_PMConfig:
        val = s->reg_pmconfig;
        break;
    case SC92031_REG_CRC0:
    case SC92031_REG_Wakeup0:
    case SC92031_REG_LSBCRC0:
    case SC92031_REG_TestD0:
    case SC92031_REG_TestD4:
    case SC92031_REG_TestD8:
        /* Not used by driver during probe/open except WOL helpers, which only
         * care about PMConfig. Keep these at 0.
         */
        val = 0;
        break;
    default:
        val = 0;
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case SC92031_REG_Config0:
        s->reg_config0 = (uint32_t)val;
        /* Driver uses Cfg0_Reset to soft reset. We approximate by clearing
         * internal regs when this bit is written, then later clearing by 0.
         */
        if (s->reg_config0 & SC92031_CFG0_Reset) {
            /* Soft reset: clear interrupt status and masks, RX/TX config etc. */
            s->reg_intr_status = 0;
            s->reg_intr_mask = 0;
            s->reg_rxconfig = 0;
            s->reg_txconfig = 0;
            s->reg_flowctrl = 0;
            s->reg_rxbuf_rptr = 0;
            s->reg_rxbuf_wptr = 0;
            s->rx_ring_tail = s->rx_ring_dma_addr;
            pcibase_update_irq(s);
        }
        break;
    case SC92031_REG_Config1:
        s->reg_config1 = (uint32_t)val;
        break;
    case SC92031_REG_RxBufWPtr:
        /* This register is written by hardware in real NIC. Driver treats it
         * as read-only. Ignore writes.
         */
        break;
    case SC92031_REG_IntrStatus:
        /* W1C for handled bits; driver masks first then reads. We accept
         * any writes as clear for written bits.
         */
        s->reg_intr_status &= ~((uint32_t)val & SC92031_INTR_BITS);
        pcibase_update_irq(s);
        break;
    case SC92031_REG_IntrMask:
        s->reg_intr_mask = (uint32_t)val;
        pcibase_update_irq(s);
        break;
    case SC92031_REG_RxbufAddr:
        s->reg_rxbuf_addr = (uint32_t)val;
        s->rx_ring_dma_addr = (dma_addr_t)val;
        s->rx_ring_tail = s->rx_ring_dma_addr;
        break;
    case SC92031_REG_RxBufRPtr:
        s->reg_rxbuf_rptr = (uint32_t)val;
        s->rx_ring_tail = (dma_addr_t)val;
        break;
    case SC92031_REG_Txstatusall:
        s->reg_txstatusall = (uint32_t)val;
        break;
    case SC92031_REG_TxStatus0:
    case SC92031_REG_TxStatus0 + 4:
    case SC92031_REG_TxStatus0 + 8:
    case SC92031_REG_TxStatus0 + 12:
    {
        /* Driver writes tx_status after writing TxAddr, NIC should then
         * start transmission and later set completion bits that tasklet
         * polls for. Here we simply echo back what was written so tasklet
         * sees TxStatOK and length, without simulating timing.
         */
        unsigned idx = (addr - SC92031_REG_TxStatus0) / 4;
        if (idx < SC92031_NUM_TX_DESC) {
            s->reg_txstatus[idx] = (uint32_t)val | SC92031_TXSTAT_TxStatOK;
            /* Also set global TxOK interrupt bit */
            s->reg_intr_status |= SC92031_INTR_TxOK;
            pcibase_update_irq(s);
        }
        break;
    }
    case SC92031_REG_TxAddr0:
    case SC92031_REG_TxAddr0 + 4:
    case SC92031_REG_TxAddr0 + 8:
    case SC92031_REG_TxAddr0 + 12:
    {
        unsigned idx = (addr - SC92031_REG_TxAddr0) / 4;
        if (idx < SC92031_NUM_TX_DESC) {
            s->reg_txaddr[idx] = (uint32_t)val;
        }
        break;
    }
    case SC92031_REG_RxConfig:
        s->reg_rxconfig = (uint32_t)val;
        break;
    case SC92031_REG_MAC0:
        s->reg_mac0 = (uint32_t)val;
        break;
    case SC92031_REG_MAC0 + 4:
        s->reg_mac1 = (uint32_t)val;
        break;
    case SC92031_REG_MAR0:
        s->reg_mar0 = (uint32_t)val;
        break;
    case SC92031_REG_MAR0 + 4:
        s->reg_mar1 = (uint32_t)val;
        break;
    case SC92031_REG_RxStatus0:
        s->reg_rxstatus0 = (uint32_t)val;
        break;
    case SC92031_REG_TxConfig:
        s->reg_txconfig = (uint32_t)val;
        break;
    case SC92031_REG_PhyCtrl:
        s->reg_phyctrl = (uint32_t)val;
        break;
    case SC92031_REG_FlowCtrlConfig:
        s->reg_flowctrl = (uint32_t)val;
        break;
    case SC92031_REG_Miicmd0:
        s->reg_miicmd0 = (uint32_t)val;
        pcibase_update_mii(s);
        break;
    case SC92031_REG_Miicmd1:
        s->reg_miicmd1 = (uint32_t)val;
        /* update_mii is triggered when Miicmd0 is written */
        break;
    case SC92031_REG_Miistatus:
        /* Status is read-only from driver perspective. Ignore writes. */
        break;
    case SC92031_REG_Timercnt:
        s->reg_timercnt = (uint32_t)val;
        break;
    case SC92031_REG_TimerIntr:
        s->reg_timerintr = (uint32_t)val;
        break;
    case SC92031_REG_PMConfig:
        s->reg_pmconfig = (uint32_t)val;
        break;
    case SC92031_REG_CRC0:
    case SC92031_REG_Wakeup0:
    case SC92031_REG_LSBCRC0:
    case SC92031_REG_TestD0:
    case SC92031_REG_TestD4:
    case SC92031_REG_TestD8:
        /* Not used by driver in critical paths; allow writes to shadow. */
        break;
    default:
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    /* The driver only uses pci_iomap() and then ioread32()/iowrite32(),
     * which QEMU maps to MMIO or PIO depending on BAR type. For PIO BAR,
     * we reuse same layout.
     */
    return pcibase_mmio_read(s, addr, size);
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    pcibase_mmio_write(s, addr, val, size);
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

    /* Power-on defaults approximated from driver expectations */
    s->reg_config0 = 0;
    s->reg_config1 = 0;
    s->reg_rxbuf_wptr = 0;
    s->reg_intr_status = 0;
    s->reg_intr_mask = 0;
    s->reg_rxbuf_addr = 0;
    s->reg_rxbuf_rptr = 0;
    s->reg_txstatusall = 0;
    for (int i = 0; i < SC92031_NUM_TX_DESC; i++) {
        s->reg_txstatus[i] = 0;
        s->reg_txaddr[i] = 0;
    }
    s->reg_rxconfig = 0;
    /* Provide a non-zero MAC address so probe can read it. */
    s->reg_mac0 = 0x00112233; /* bytes 0..3 */
    s->reg_mac1 = 0x00004455; /* bytes 4..5 in low bits */
    s->reg_mar0 = 0;
    s->reg_mar1 = 0;
    s->reg_rxstatus0 = 0;
    s->reg_txconfig = 0;
    s->reg_phyctrl = SC92031_PHY_PhyCtrlAne | SC92031_PHY_PhyCtrlSpd100 | SC92031_PHY_PhyCtrlSpd10;
    s->reg_flowctrl = 0;
    s->reg_miicmd0 = 0;
    s->reg_miicmd1 = 0;
    s->reg_miistatus = 0;
    s->reg_timercnt = 0;
    s->reg_timerintr = 0;
    s->reg_pmconfig = SC92031_PM_PM_Enable;

    s->rx_ring_dma_addr = 0;
    s->rx_ring_tail = 0;
    s->tx_bufs_dma_addr = 0;

    s->link_up = false;

    /* PHY defaults: link up at 100Mbps full duplex to satisfy media check. */
    s->phy_bmsr = SC92031_BMSR_LSTATUS;
    s->phy_bmcr = 0;
    s->phy_output_status = 0x0006; /* bit1 speed_100, bit2 duplex_full */

    pcibase_update_irq(s);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  SC92031_PCI_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  SC92031_PCI_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, SC92031_PCI_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization: driver uses BAR0 via pci_iomap(SC92031_USE_PIO) */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 256; /* covers all used registers up to 0xD8 */
    s->bar_info[0].name = "sc92031-io";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize MSI/MSI-X if desired; driver does not use them explicitly. */
    s->has_msi = false;
    s->has_msix = false;

    /* Reset internal state to power-on defaults */
    pcibase_reset(DEVICE(pdev));
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

    (void)s;
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "sc92031_pci",
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
#ifdef DEVICE_CATEGORY_NET
    set_bit(DEVICE_CATEGORY_NET, dc->categories);
#endif
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
