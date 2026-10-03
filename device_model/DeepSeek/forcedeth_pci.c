/* Integrated QEMU PCI device template (QEMU 8.2.10) for forcedeth driver.
 * This device model implements the MMIO registers and MII PHY interface */

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

#define TYPE_PCIBASE_DEVICE "forcedeth_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_NVIDIA      0x10DE
#define PCI_DEVICE_ID_NVIDIA_NVENET_1 0x01C3
#define PCI_CLASS_NETWORK_ETHERNET    0x0200

/* Register offsets from forcedeth driver */
#define NvRegIrqStatus        0x000
#define NvRegIrqMask          0x004
#define NvRegUnknownSetupReg6 0x008
#define NvRegPollingInterval  0x00C
#define NvRegTransmitterControl 0x080
#define NvRegTransmitterStatus  0x084
#define NvRegReceiverControl    0x0C0
#define NvRegReceiverStatus     0x0C4
#define NvRegSlotTime           0x0C8
#define NvRegMacAddrA           0x0A8
#define NvRegMacAddrB           0x0AC
#define NvRegMulticastAddrA     0x0B0
#define NvRegMulticastAddrB     0x0B4
#define NvRegMulticastMaskA     0x0B8
#define NvRegMulticastMaskB     0x0BC
#define NvRegPacketFilterFlags  0x0D8
#define NvRegTxRxControl        0x0E4
#define NvRegLinkSpeed          0x158
#define NvRegWakeUpFlags        0x0F8
#define NvRegPowerState         0x1E4
#define NvRegPowerState2        0x1E8
#define NvRegMIIControl         0x090
#define NvRegMIIData            0x094
#define NvRegMIIStatus          0x098
#define NvRegMIISpeed           0x09C
#define NvRegMIIMask            0x0A0
#define NvRegTxRingPhysAddr     0x100
#define NvRegRxRingPhysAddr     0x104
#define NvRegTxRingPhysAddrHigh 0x148
#define NvRegRxRingPhysAddrHigh 0x14C
#define NvRegRingSizes          0x108
#define NvRegOffloadConfig      0x1C8
#define NvRegTxDeferral         0x140
#define NvRegRxDeferral         0x144
#define NvRegTxWatermark        0x13C
#define NvRegMisc1              0x1E0
#define NvRegPhyInterface       0x1D0
#define NvRegAdapterControl     0x1B0
#define NvRegVlanControl        0x1F0
#define NvRegBackOffControl     0x0D4
#define NvRegMSIIrqMask         0x1C0
#define NvRegMSIMap0            0x1C4
#define NvRegMSIMap1            0x1C8
#define NvRegMSIXIrqStatus      0x1D4
#define NvRegMSIXMap0           0x200
#define NvRegMSIXMap1           0x204
#define NvRegTxPauseFrame       0x190
#define NvRegTxPauseFrameLimit  0x194
#define NvRegUnknownSetupReg5   0x130
#define NvRegTransmitPoll       0x0D0

/* Driver definitions */
#define NVREG_IRQSTAT_MIIEVENT 0x040
#define NVREG_IRQSTAT_MASK  0x83ff
#define NVREG_IRQ_RX_ERROR  0x0001
#define NVREG_IRQ_RX   0x0002
#define NVREG_IRQ_RX_NOBUF  0x0004
#define NVREG_IRQ_TX_ERR  0x0008
#define NVREG_IRQ_TX_OK  0x0010
#define NVREG_IRQ_TIMER   0x0020
#define NVREG_IRQ_LINK   0x0040
#define NVREG_IRQ_RX_FORCED  0x0080
#define NVREG_IRQ_TX_FORCED  0x0100
#define NVREG_IRQ_RECOVER_ERROR  0x8200
#define NVREG_IRQMASK_THROUGHPUT 0x00df
#define NVREG_IRQMASK_CPU  0x0060
#define NVREG_IRQ_TX_ALL  (NVREG_IRQ_TX_ERR|NVREG_IRQ_TX_OK|NVREG_IRQ_TX_FORCED)
#define NVREG_IRQ_RX_ALL  (NVREG_IRQ_RX_ERROR|NVREG_IRQ_RX|NVREG_IRQ_RX_NOBUF|NVREG_IRQ_RX_FORCED)
#define NVREG_IRQ_OTHER   (NVREG_IRQ_TIMER|NVREG_IRQ_LINK|NVREG_IRQ_RECOVER_ERROR)
#define NVREG_UNKSETUP6_VAL  3
#define NVREG_POLL_DEFAULT_THROUGHPUT 65535
#define NVREG_POLL_DEFAULT_CPU 13
#define NVREG_MSI_VECTOR_0_ENABLED 0x01
#define NVREG_MISC1_PAUSE_TX 0x01
#define NVREG_MISC1_HD   0x02
#define NVREG_MISC1_FORCE 0x3b0f3c
#define NVREG_MAC_RESET_ASSERT 0x0F3
#define NVREG_XMITCTL_START 0x01
#define NVREG_XMITCTL_MGMT_ST 0x40000000
#define NVREG_XMITCTL_SYNC_MASK  0x000f0000
#define NVREG_XMITCTL_SYNC_NOT_READY 0x0
#define NVREG_XMITCTL_SYNC_PHY_INIT 0x00040000
#define NVREG_XMITCTL_MGMT_SEMA_MASK 0x00000f00
#define NVREG_XMITCTL_MGMT_SEMA_FREE 0x0
#define NVREG_XMITCTL_HOST_SEMA_MASK 0x0000f000
#define NVREG_XMITCTL_HOST_SEMA_ACQ 0x0000f000
#define NVREG_XMITCTL_HOST_LOADED 0x00004000
#define NVREG_XMITCTL_TX_PATH_EN 0x01000000
#define NVREG_XMITCTL_DATA_START 0x00100000
#define NVREG_XMITCTL_DATA_READY 0x00010000
#define NVREG_XMITCTL_DATA_ERROR 0x00020000
#define NVREG_XMITSTAT_BUSY 0x01
#define NVREG_PFF_PAUSE_RX 0x08
#define NVREG_PFF_ALWAYS 0x7F0000
#define NVREG_PFF_PROMISC 0x80
#define NVREG_PFF_MYADDR 0x20
#define NVREG_PFF_LOOPBACK 0x10
#define NVREG_OFFLOAD_HOMEPHY 0x601
#define NVREG_OFFLOAD_NORMAL RX_NIC_BUFSIZE
#define NVREG_RCVCTL_START 0x01
#define NVREG_RCVCTL_RX_PATH_EN 0x01000000
#define NVREG_RCVSTAT_BUSY 0x01
#define NVREG_SLOTTIME_LEGBF_ENABLED 0x80000000
#define NVREG_SLOTTIME_10_100_FULL 0x00007f00
#define NVREG_SLOTTIME_1000_FULL 0x0003ff00
#define NVREG_SLOTTIME_HALF  0x0000ff00
#define NVREG_SLOTTIME_DEFAULT  0x00007f00
#define NVREG_SLOTTIME_MASK  0x000000ff
#define NVREG_TX_DEFERRAL_DEFAULT  0x15050f
#define NVREG_TX_DEFERRAL_RGMII_10_100  0x16070f
#define NVREG_TX_DEFERRAL_RGMII_1000  0x14050f
#define NVREG_TX_DEFERRAL_RGMII_STRETCH_10 0x16190f
#define NVREG_TX_DEFERRAL_RGMII_STRETCH_100 0x16300f
#define NVREG_TX_DEFERRAL_MII_STRETCH  0x152000
#define NVREG_RX_DEFERRAL_DEFAULT 0x16
#define NVREG_MCASTADDRA_FORCE 0x01
#define NVREG_MCASTMASKA_NONE  0xffffffff
#define NVREG_MCASTMASKB_NONE  0xffff
#define PHY_RGMII  0x10000000
#define NVREG_BKOFFCTRL_DEFAULT  0x70000000
#define NVREG_BKOFFCTRL_SEED_MASK  0x000003ff
#define NVREG_BKOFFCTRL_SELECT   24
#define NVREG_BKOFFCTRL_GEAR   12
#define NVREG_RINGSZ_TXSHIFT 0
#define NVREG_RINGSZ_RXSHIFT 16
#define NVREG_TRANSMITPOLL_MAC_ADDR_REV 0x00008000
#define NVREG_LINKSPEED_FORCE 0x10000
#define NVREG_LINKSPEED_10 1000
#define NVREG_LINKSPEED_100 100
#define NVREG_LINKSPEED_1000 50
#define NVREG_LINKSPEED_MASK (0xFFF)
#define NVREG_UNKSETUP5_BIT31 (1<<31)
#define NVREG_TX_WM_DESC1_DEFAULT 0x0200010
#define NVREG_TX_WM_DESC2_3_DEFAULT 0x1e08000
#define NVREG_TX_WM_DESC2_3_1000 0xfe08000
#define NVREG_TXRXCTL_KICK 0x0001
#define NVREG_TXRXCTL_BIT1 0x0002
#define NVREG_TXRXCTL_BIT2 0x0004
#define NVREG_TXRXCTL_IDLE 0x0008
#define NVREG_TXRXCTL_RESET 0x0010
#define NVREG_TXRXCTL_RXCHECK 0x0400
#define NVREG_TXRXCTL_DESC_1 0
#define NVREG_TXRXCTL_DESC_2 0x002100
#define NVREG_TXRXCTL_DESC_3 0xc02200
#define NVREG_TXRXCTL_VLANSTRIP 0x00040
#define NVREG_TXRXCTL_VLANINS 0x00080
#define NVREG_TX_PAUSEFRAME_DISABLE 0x0fff0080
#define NVREG_TX_PAUSEFRAME_ENABLE_V1 0x01800010
#define NVREG_TX_PAUSEFRAME_ENABLE_V2 0x056003f0
#define NVREG_TX_PAUSEFRAME_ENABLE_V3 0x09f00880
#define NVREG_TX_PAUSEFRAMELIMIT_ENABLE 0x00010000
#define NVREG_MIISTAT_ERROR  0x0001
#define NVREG_MIISTAT_LINKCHANGE 0x0008
#define NVREG_MIISTAT_MASK_RW  0x0007
#define NVREG_MIISTAT_MASK_ALL  0x000f
#define NVREG_MII_LINKCHANGE  0x0008
#define NVREG_ADAPTCTL_START 0x02
#define NVREG_ADAPTCTL_LINKUP 0x04
#define NVREG_ADAPTCTL_PHYVALID 0x40000
#define NVREG_ADAPTCTL_RUNNING 0x100000
#define NVREG_ADAPTCTL_PHYSHIFT 24
#define NVREG_MIISPEED_BIT8 (1<<8)
#define NVREG_MIIDELAY 5
#define NVREG_MIICTL_INUSE 0x08000
#define NVREG_MIICTL_WRITE 0x00400
#define NVREG_MIICTL_ADDRSHIFT 5
#define NVREG_WAKEUPFLAGS_VAL  0x7770
#define NVREG_WAKEUPFLAGS_BUSYSHIFT 24
#define NVREG_WAKEUPFLAGS_ENABLESHIFT 16
#define NVREG_WAKEUPFLAGS_D3SHIFT 12
#define NVREG_WAKEUPFLAGS_D2SHIFT 8
#define NVREG_WAKEUPFLAGS_D1SHIFT 4
#define NVREG_WAKEUPFLAGS_D0SHIFT 0
#define NVREG_WAKEUPFLAGS_ACCEPT_MAGPAT  0x01
#define NVREG_WAKEUPFLAGS_ACCEPT_WAKEUPPAT 0x02
#define NVREG_WAKEUPFLAGS_ACCEPT_LINKCHANGE 0x04
#define NVREG_WAKEUPFLAGS_ENABLE 0x1111
#define NVREG_MGMTUNITGETVERSION 0x01
#define NVREG_MGMTUNITVERSION  0x08
#define NVREG_POWERCAP_D3SUPP (1<<30)
#define NVREG_POWERCAP_D2SUPP (1<<26)
#define NVREG_POWERCAP_D1SUPP (1<<25)
#define NVREG_POWERSTATE_POWEREDUP 0x8000
#define NVREG_POWERSTATE_VALID  0x0100
#define NVREG_POWERSTATE_MASK  0x0003
#define NVREG_POWERSTATE_D0  0x0000
#define NVREG_POWERSTATE_D1  0x0001
#define NVREG_POWERSTATE_D2  0x0002
#define NVREG_POWERSTATE_D3  0x0003
#define NVREG_MGMTUNITCONTROL_INUSE 0x20000
#define NVREG_VLANCONTROL_ENABLE 0x2000
#define NVREG_POWERSTATE2_POWERUP_MASK  0x0F15
#define NVREG_POWERSTATE2_POWERUP_REV_A3 0x0001
#define NVREG_POWERSTATE2_PHY_RESET  0x0004
#define NVREG_POWERSTATE2_GATE_CLOCKS  0x0F00
#define FLAG_MASK_V1 0xffff0000
#define FLAG_MASK_V2 0xffffc000
#define LEN_MASK_V1 (0xffffffff ^ FLAG_MASK_V1)
#define LEN_MASK_V2 (0xffffffff ^ FLAG_MASK_V2)
#define NV_TX_LASTPACKET (1<<16)
#define NV_TX_RETRYERROR (1<<19)
#define NV_TX_RETRYCOUNT_MASK (0xF<<20)
#define NV_TX_FORCED_INTERRUPT (1<<24)
#define NV_TX_DEFERRED  (1<<26)
#define NV_TX_CARRIERLOST (1<<27)
#define NV_TX_LATECOLLISION (1<<28)
#define NV_TX_UNDERFLOW  (1<<29)
#define NV_TX_ERROR  (1<<30)
#define NV_TX_VALID  (1<<31)
#define NV_TX2_LASTPACKET (1<<29)
#define NV_TX2_RETRYERROR (1<<18)
#define NV_TX2_RETRYCOUNT_MASK (0xF<<19)
#define NV_TX2_FORCED_INTERRUPT (1<<30)
#define NV_TX2_DEFERRED  (1<<25)
#define NV_TX2_CARRIERLOST (1<<26)
#define NV_TX2_LATECOLLISION (1<<27)
#define NV_TX2_UNDERFLOW (1<<28)
#define NV_TX2_ERROR  (1<<30)
#define NV_TX2_VALID  (1<<31)
#define NV_TX2_TSO  (1<<28)
#define NV_TX2_TSO_SHIFT 14
#define NV_TX2_TSO_MAX_SHIFT 14
#define NV_TX2_TSO_MAX_SIZE (1<<NV_TX2_TSO_MAX_SHIFT)
#define NV_TX2_CHECKSUM_L3 (1<<27)
#define NV_TX2_CHECKSUM_L4 (1<<26)
#define NV_TX3_VLAN_TAG_PRESENT (1<<18)
#define NV_RX_DESCRIPTORVALID (1<<16)
#define NV_RX_MISSEDFRAME (1<<17)
#define NV_RX_SUBTRACT1  (1<<18)
#define NV_RX_ERROR1  (1<<23)
#define NV_RX_ERROR2  (1<<24)
#define NV_RX_ERROR3  (1<<25)
#define NV_RX_ERROR4  (1<<26)
#define NV_RX_CRCERR  (1<<27)
#define NV_RX_OVERFLOW  (1<<28)
#define NV_RX_FRAMINGERR (1<<29)
#define NV_RX_ERROR  (1<<30)
#define NV_RX_AVAIL  (1<<31)
#define NV_RX_ERROR_MASK (NV_RX_ERROR1|NV_RX_ERROR2|NV_RX_ERROR3|NV_RX_ERROR4|NV_RX_CRCERR|NV_RX_OVERFLOW|NV_RX_FRAMINGERR)
#define NV_RX2_CHECKSUMMASK (0x1C000000)
#define NV_RX2_CHECKSUM_IP (0x10000000)
#define NV_RX2_CHECKSUM_IP_TCP (0x14000000)
#define NV_RX2_CHECKSUM_IP_UDP (0x18000000)
#define NV_RX2_DESCRIPTORVALID (1<<29)
#define NV_RX2_SUBTRACT1 (1<<25)
#define NV_RX2_ERROR1  (1<<18)
#define NV_RX2_ERROR2  (1<<19)
#define NV_RX2_ERROR3  (1<<20)
#define NV_RX2_ERROR4  (1<<21)
#define NV_RX2_CRCERR  (1<<22)
#define NV_RX2_OVERFLOW  (1<<23)
#define NV_RX2_FRAMINGERR (1<<24)
#define NV_RX2_ERROR  (1<<30)
#define NV_RX2_AVAIL  (1<<31)
#define NV_RX2_ERROR_MASK (NV_RX2_ERROR1|NV_RX2_ERROR2|NV_RX2_ERROR3|NV_RX2_ERROR4|NV_RX2_CRCERR|NV_RX2_OVERFLOW|NV_RX2_FRAMINGERR)
#define NV_RX3_VLAN_TAG_PRESENT (1<<16)
#define NV_RX3_VLAN_TAG_MASK (0x0000FFFF)
#define NV_PCI_REGSZ_VER1 0x270
#define NV_PCI_REGSZ_VER2 0x2d4
#define NV_PCI_REGSZ_VER3 0x604
#define NV_PCI_REGSZ_MAX 0x604

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
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;
    bool has_msi;
    bool has_msix;
    uint32_t irq_status;
    uint32_t irq_mask;
    uint32_t mmio_regs[0x604/4];
    dma_addr_t tx_ring_dma;
    dma_addr_t rx_ring_dma;
    uint32_t tx_ring_size;
    uint32_t rx_ring_size;
    uint32_t tx_status;
    uint32_t rx_status;
    uint32_t power_state;
    uint16_t phy_regs[32][32];
};

struct ring_desc {
    uint32_t buf;
    uint32_t flaglen;
};
struct ring_desc_ex {
    uint32_t bufhigh;
    uint32_t buflow;
    uint32_t txvlan;
    uint32_t flaglen;
};

G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
}

G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    uint32_t offset = addr & ~3u;

    if (offset >= sizeof(s->mmio_regs)) {
        return ~0ULL;
    }

    val = s->mmio_regs[offset / 4];
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t offset = addr & ~3u;

    if (offset >= sizeof(s->mmio_regs)) {
        return;
    }

    if (offset == NvRegMIIControl) {
        uint32_t new_ctrl = val;
        if (new_ctrl & NVREG_MIICTL_INUSE) {
            /* MII transaction triggered: decode and execute */
            int phy_addr = (new_ctrl >> NVREG_MIICTL_ADDRSHIFT) & 0x1F;
            int reg = new_ctrl & 0x1F;
            if (new_ctrl & NVREG_MIICTL_WRITE) {
                uint32_t data = s->mmio_regs[NvRegMIIData / 4];
                s->phy_regs[phy_addr][reg] = data & 0xFFFF;
            } else {
                uint16_t phy_val = s->phy_regs[phy_addr][reg];
                if (phy_addr == 1 && reg == 2) {
                    phy_val = 0x0022;
                } else if (phy_addr == 1 && reg == 3) {
                    phy_val = 0x1610;
                }
                s->mmio_regs[NvRegMIIData / 4] = phy_val;
            }
            /* Clear INUSE to signal completion */
            s->mmio_regs[NvRegMIIControl / 4] = new_ctrl & ~NVREG_MIICTL_INUSE;
        } else {
            /* Setup write, just store */
            s->mmio_regs[NvRegMIIControl / 4] = new_ctrl;
        }
        return;
    }

    if (offset == NvRegMIIStatus) {
        s->mmio_regs[NvRegMIIStatus / 4] &= ~((uint32_t)val);
        return;
    }

    s->mmio_regs[offset / 4] = (uint32_t)val;
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
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

    s->mmio_regs[NvRegMacAddrA / 4] = 0x33221100;
    s->mmio_regs[NvRegMacAddrB / 4] = 0x00005544;
    s->mmio_regs[NvRegTransmitPoll / 4] = NVREG_TRANSMITPOLL_MAC_ADDR_REV;
    s->mmio_regs[NvRegAdapterControl / 4] = NVREG_ADAPTCTL_PHYVALID | NVREG_ADAPTCTL_RUNNING;

    memset(s->phy_regs, 0, sizeof(s->phy_regs));
    s->phy_regs[1][0] = 0x3100;
    s->phy_regs[1][1] = 0x786d;
    s->phy_regs[1][2] = 0x0022;
    s->phy_regs[1][3] = 0x1610;
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_NVIDIA );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_NVIDIA_NVENET_1 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_ETHERNET );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    s->num_bars = 1;
    s->bar_info[0] = (BARInfo) {
        .index = 0,
        .type = BAR_TYPE_MMIO,
        .size = 0x270,
        .name = "forcedeth-mmio"
    };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
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
}

static const VMStateDescription vmstate_pcibase = {
    .name = "forcedeth_pci",
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
