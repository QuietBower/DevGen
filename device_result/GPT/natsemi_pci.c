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

#include <stdint.h>

#define TYPE_PCIBASE_DEVICE "natsemi_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* The real driver uses PCI_VENDOR_ID_NS / PCI_DEVICE_ID_NS_83815.
 * Here we use the first entry (0x100b:0x0020) as requested.
 */
#define NATSEMI_VENDOR_ID   0x100b
#define NATSEMI_DEVICE_ID   0x0020
#define NATSEMI_CLASS_ID    PCI_CLASS_NETWORK_ETHERNET

/* enum register_offsets from driver */
enum natsemi_register_offsets {
    NATSEMI_ChipCmd       = 0x00,
    NATSEMI_ChipConfig    = 0x04,
    NATSEMI_EECtrl        = 0x08,
    NATSEMI_PCIBusCfg     = 0x0C,
    NATSEMI_IntrStatus    = 0x10,
    NATSEMI_IntrMask      = 0x14,
    NATSEMI_IntrEnable    = 0x18,
    NATSEMI_IntrHoldoff   = 0x1C,
    NATSEMI_TxRingPtr     = 0x20,
    NATSEMI_TxConfig      = 0x24,
    NATSEMI_RxRingPtr     = 0x30,
    NATSEMI_RxConfig      = 0x34,
    NATSEMI_ClkRun        = 0x3C,
    NATSEMI_WOLCmd        = 0x40,
    NATSEMI_PauseCmd      = 0x44,
    NATSEMI_RxFilterAddr  = 0x48,
    NATSEMI_RxFilterData  = 0x4C,
    NATSEMI_BootRomAddr   = 0x50,
    NATSEMI_BootRomData   = 0x54,
    NATSEMI_SiliconRev    = 0x58,
    NATSEMI_StatsCtrl     = 0x5C,
    NATSEMI_StatsData     = 0x60,
    NATSEMI_RxPktErrs     = 0x60,
    NATSEMI_RxMissed      = 0x68,
    NATSEMI_RxCRCErrs     = 0x64,
    NATSEMI_BasicControl  = 0x80,
    NATSEMI_BasicStatus   = 0x84,
    NATSEMI_AnegAdv       = 0x90,
    NATSEMI_AnegPeer      = 0x94,
    NATSEMI_PhyStatus     = 0xC0,
    NATSEMI_MIntrCtrl     = 0xC4,
    NATSEMI_MIntrStatus   = 0xC8,
    NATSEMI_PhyCtrl       = 0xE4,
    NATSEMI_PGSEL         = 0xCC,
    NATSEMI_PMDCSR        = 0xE4,
    NATSEMI_TSTDAT        = 0xFC,
    NATSEMI_DSPCFG        = 0xF4,
    NATSEMI_SDCFG         = 0xF8
};

/* enum IntrStatus_bits from driver */
enum natsemi_intrstatus_bits {
    NATSEMI_IntrRxDone          = 0x0001,
    NATSEMI_IntrRxIntr          = 0x0002,
    NATSEMI_IntrRxErr           = 0x0004,
    NATSEMI_IntrRxEarly         = 0x0008,
    NATSEMI_IntrRxIdle          = 0x0010,
    NATSEMI_IntrRxOverrun       = 0x0020,
    NATSEMI_IntrTxDone          = 0x0040,
    NATSEMI_IntrTxIntr          = 0x0080,
    NATSEMI_IntrTxErr           = 0x0100,
    NATSEMI_IntrTxIdle          = 0x0200,
    NATSEMI_IntrTxUnderrun      = 0x0400,
    NATSEMI_StatsMax            = 0x0800,
    NATSEMI_SWInt               = 0x1000,
    NATSEMI_WOLPkt              = 0x2000,
    NATSEMI_LinkChange          = 0x4000,
    NATSEMI_IntrHighBits        = 0x8000,
    NATSEMI_RxStatusFIFOOver    = 0x10000,
    NATSEMI_IntrPCIErr          = 0x0f00000,
    NATSEMI_RxResetDone         = 0x1000000,
    NATSEMI_TxResetDone         = 0x2000000,
    NATSEMI_IntrAbnormalSummary = 0x00cd20,
};

/* enum ChipCmd_bits from driver */
enum natsemi_chipcmd_bits {
    NATSEMI_ChipReset   = 0x0100,
    NATSEMI_RxReset     = 0x0020,
    NATSEMI_TxReset     = 0x0010,
    NATSEMI_RxOff       = 0x0008,
    NATSEMI_RxOn        = 0x0004,
    NATSEMI_TxOff       = 0x0002,
    NATSEMI_TxOn        = 0x0001,
};

/* enum ChipConfig_bits from driver */
enum natsemi_chipconfig_bits {
    NATSEMI_CfgPhyDis       = 0x00000200,
    NATSEMI_CfgPhyRst       = 0x00000400,
    NATSEMI_CfgExtPhy       = 0x00001000,
    NATSEMI_CfgAnegEnable   = 0x00002000,
    NATSEMI_CfgAneg100      = 0x00004000,
    NATSEMI_CfgAnegFull     = 0x00008000,
    NATSEMI_CfgAnegDone     = 0x08000000,
    NATSEMI_CfgFullDuplex   = 0x20000000,
    NATSEMI_CfgSpeed100     = 0x40000000,
    NATSEMI_CfgLink         = 0x80000000,
};

/* enum EECtrl_bits from driver */
enum natsemi_eectrl_bits {
    NATSEMI_EE_ShiftClk   = 0x00000004,
    NATSEMI_EE_DataIn     = 0x00000001,
    NATSEMI_EE_ChipSelect = 0x00000008,
    NATSEMI_EE_DataOut    = 0x00000002,
    NATSEMI_MII_Data      = 0x00000010,
    NATSEMI_MII_Write     = 0x00000020,
    NATSEMI_MII_ShiftClk  = 0x00000040,
};

/* enum PCIBusCfg_bits from driver */
enum natsemi_pcibuscfg_bits {
    NATSEMI_EepromReload = 0x00000004,
};

/* enum TxConfig_bits from driver */
enum natsemi_txconfig_bits {
    NATSEMI_TxDrthMask    = 0x0000003f,
    NATSEMI_TxFlthMask    = 0x00003f00,
    NATSEMI_TxMxdmaMask   = 0x00700000,
    NATSEMI_TxMxdma_512   = 0x00000000,
    NATSEMI_TxMxdma_4     = 0x00100000,
    NATSEMI_TxMxdma_8     = 0x00200000,
    NATSEMI_TxMxdma_16    = 0x00300000,
    NATSEMI_TxMxdma_32    = 0x00400000,
    NATSEMI_TxMxdma_64    = 0x00500000,
    NATSEMI_TxMxdma_128   = 0x00600000,
    NATSEMI_TxMxdma_256   = 0x00700000,
    NATSEMI_TxCollRetry   = 0x00800000,
    NATSEMI_TxAutoPad     = 0x10000000,
    NATSEMI_TxMacLoop     = 0x20000000,
    NATSEMI_TxHeartIgn    = 0x40000000,
    NATSEMI_TxCarrierIgn  = 0x80000000,
};

/* enum RxConfig_bits from driver */
enum natsemi_rxconfig_bits {
    NATSEMI_RxDrthMask      = 0x0000003e,
    NATSEMI_RxMxdmaMask     = 0x00700000,
    NATSEMI_RxMxdma_512     = 0x00000000,
    NATSEMI_RxMdma_4_dummy  = 0x00100000, /* not directly used */
    NATSEMI_RxMxdma_4       = 0x00100000,
    NATSEMI_RxMxdma_8       = 0x00200000,
    NATSEMI_RxMxdma_16      = 0x00300000,
    NATSEMI_RxMxdma_32      = 0x00400000,
    NATSEMI_RxMxdma_64      = 0x00500000,
    NATSEMI_RxMxdma_128     = 0x00600000,
    NATSEMI_RxMxdma_256     = 0x00700000,
    NATSEMI_RxAcceptLong    = 0x08000000,
    NATSEMI_RxAcceptTx      = 0x10000000,
    NATSEMI_RxAcceptRunt    = 0x40000000,
    NATSEMI_RxAcceptErr     = 0x80000000,
};

/* enum ClkRun_bits from driver */
enum natsemi_clkrun_bits {
    NATSEMI_PMEEnable = 0x00000100,
    NATSEMI_PMEStatus = 0x00008000,
};

/* enum WolCmd_bits from driver */
enum natsemi_wolcmd_bits {
    NATSEMI_WakePhy         = 0x00000001,
    NATSEMI_WakeUnicast     = 0x00000002,
    NATSEMI_WakeMulticast   = 0x00000004,
    NATSEMI_WakeBroadcast   = 0x00000008,
    NATSEMI_WakeArp         = 0x00000010,
    NATSEMI_WakePMatch0     = 0x00000020,
    NATSEMI_WakePMatch1     = 0x00000040,
    NATSEMI_WakePMatch2     = 0x00000080,
    NATSEMI_WakePMatch3     = 0x00000100,
    NATSEMI_WakeMagic       = 0x00000200,
    NATSEMI_WakeMagicSecure = 0x00000400,
    NATSEMI_SecureHack      = 0x00100000,
    NATSEMI_WokePhy         = 0x00400000,
    NATSEMI_WokeUnicast     = 0x00800000,
    NATSEMI_WokeMulticast   = 0x01000000,
    NATSEMI_WokeBroadcast   = 0x02000000,
    NATSEMI_WokeArp         = 0x04000000,
    NATSEMI_WokePMatch0     = 0x08000000,
    NATSEMI_WokePMatch1     = 0x10000000,
    NATSEMI_WokePMatch2     = 0x20000000,
    NATSEMI_WokePMatch3     = 0x40000000,
    NATSEMI_WokeMagic       = 0x80000000,
    NATSEMI_WakeOptsSummary = 0x000007ff,
};

/* enum RxFilterAddr_bits from driver */
enum natsemi_rxfilteraddr_bits {
    NATSEMI_RFCRAddressMask     = 0x000003ff,
    NATSEMI_AcceptMulticast     = 0x00200000,
    NATSEMI_AcceptMyPhys        = 0x08000000,
    NATSEMI_AcceptAllPhys       = 0x10000000,
    NATSEMI_AcceptAllMulticast  = 0x20000000,
    NATSEMI_AcceptBroadcast     = 0x40000000,
    NATSEMI_RxFilterEnable      = 0x80000000,
};

/* enum StatsCtrl_bits from driver */
enum natsemi_statsctrl_bits {
    NATSEMI_StatsWarn   = 0x00000001,
    NATSEMI_StatsFreeze = 0x00000002,
    NATSEMI_StatsClear  = 0x00000004,
    NATSEMI_StatsStrobe = 0x00000008,
};

/* enum MIntrCtrl_bits from driver */
enum natsemi_mintrctrl_bits {
    NATSEMI_MICRIntEn = 0x00000002,
};

/* enum PhyCtrl_bits from driver */
enum natsemi_phyctrl_bits {
    NATSEMI_PhyAddrMask = 0x0000001f,
};

/* PCI config space offsets */
enum natsemi_pci_register_offsets {
    NATSEMI_PCIPM = 0x44,
};

#define NATSEMI_NREGS       0x40
#define NATSEMI_REGS_SIZE   (NATSEMI_NREGS * sizeof(uint32_t))


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

    /* Hardware Register Shadows */
    struct {
        uint32_t chipcmd;
        uint32_t chipconfig;
        uint32_t eectrl;
        uint32_t pcibuscfg;
        uint32_t intrstatus;
        uint32_t intrmask;
        uint32_t intrenable;
        uint32_t intrholdoff;
        uint32_t txringptr;
        uint32_t txconfig;
        uint32_t rxringptr;
        uint32_t rxconfig;
        uint32_t clkrun;
        uint32_t wolcmd;
        uint32_t pausecmd;
        uint32_t rxfilteraddr;
        uint32_t rxfilterdata;
        uint32_t bootromaddr;
        uint32_t bootromdata;
        uint32_t siliconrev;
        uint32_t statsctrl;
        uint32_t statsdata;
        uint32_t rxpkterrs;
        uint32_t rxmissed;
        uint32_t rxcrcerrs;
        uint32_t basiccontrol;
        uint32_t basicstatus;
        uint32_t anegadv;
        uint32_t anegpeer;
        uint32_t phystatus;
        uint32_t mintrctrl;
        uint32_t mintrstatus;
        uint32_t phyctrl;
        uint32_t pgsel;
        uint32_t pmdcsr;
        uint32_t tstdat;
        uint32_t dspcfg;
        uint32_t sdcfg;
    } regs;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    bool active = false;

    if (s->regs.intrenable) {
        uint32_t pending = s->regs.intrstatus & s->regs.intrmask;
        if (pending) {
            active = true;
        }
    }

    if (!s->has_msi && !s->has_msix) {
        pci_set_irq(pdev, active ? 1 : 0);
    } else if (active) {
        if (s->has_msix && msix_enabled(pdev)) {
            msix_notify(pdev, 0);
        } else if (s->has_msi && msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        }
    }
}

static uint32_t pcibase_reg_read32(PCIBaseState *s, hwaddr addr)
{
    switch (addr) {
    case NATSEMI_ChipCmd:
        return s->regs.chipcmd;
    case NATSEMI_ChipConfig:
        return s->regs.chipconfig;
    case NATSEMI_EECtrl:
        return s->regs.eectrl;
    case NATSEMI_PCIBusCfg:
        return s->regs.pcibuscfg;
    case NATSEMI_IntrStatus:
        /* Clear-on-read behaviour: driver comments say this. */
        {
            uint32_t val = s->regs.intrstatus;
            s->regs.intrstatus = 0;
            pcibase_update_irq(s);
            return val;
        }
    case NATSEMI_IntrMask:
        return s->regs.intrmask;
    case NATSEMI_IntrEnable:
        return s->regs.intrenable;
    case NATSEMI_IntrHoldoff:
        return s->regs.intrholdoff;
    case NATSEMI_TxRingPtr:
        return s->regs.txringptr;
    case NATSEMI_TxConfig:
        return s->regs.txconfig;
    case NATSEMI_RxRingPtr:
        return s->regs.rxringptr;
    case NATSEMI_RxConfig:
        return s->regs.rxconfig;
    case NATSEMI_ClkRun:
        return s->regs.clkrun;
    case NATSEMI_WOLCmd:
        return s->regs.wolcmd;
    case NATSEMI_PauseCmd:
        return s->regs.pausecmd;
    case NATSEMI_RxFilterAddr:
        return s->regs.rxfilteraddr;
    case NATSEMI_RxFilterData:
        return s->regs.rxfilterdata;
    case NATSEMI_BootRomAddr:
        return s->regs.bootromaddr;
    case NATSEMI_BootRomData:
        return s->regs.bootromdata;
    case NATSEMI_SiliconRev:
        return s->regs.siliconrev;
    case NATSEMI_StatsCtrl:
        return s->regs.statsctrl;
    case NATSEMI_StatsData:
        return s->regs.statsdata;
    case NATSEMI_RxCRCErrs:
        return s->regs.rxcrcerrs;
    case NATSEMI_RxMissed:
        return s->regs.rxmissed;
    case NATSEMI_BasicControl:
        return s->regs.basiccontrol;
    case NATSEMI_BasicStatus:
        /* Link up bit so that check_link() sees link. */
        return s->regs.basicstatus;
    case NATSEMI_AnegAdv:
        return s->regs.anegadv;
    case NATSEMI_AnegPeer:
        return s->regs.anegpeer;
    case NATSEMI_PhyStatus:
        return s->regs.phystatus;
    case NATSEMI_MIntrCtrl:
        return s->regs.mintrctrl;
    case NATSEMI_MIntrStatus:
        {
            uint32_t v = s->regs.mintrstatus;
            s->regs.mintrstatus = 0;
            return v;
        }
    case NATSEMI_PhyCtrl:
        return s->regs.phyctrl;
    case NATSEMI_PGSEL:
        return s->regs.pgsel;
    /* NATSEMI_PMDCSR shares offset with NATSEMI_PhyCtrl; keep only one case */
    case NATSEMI_TSTDAT:
        return s->regs.tstdat;
    case NATSEMI_DSPCFG:
        return s->regs.dspcfg;
    case NATSEMI_SDCFG:
        return s->regs.sdcfg;
    default:
        return 0;
    }
}

static void pcibase_reg_write32(PCIBaseState *s, hwaddr addr, uint32_t val)
{
    switch (addr) {
    case NATSEMI_ChipCmd:
        s->regs.chipcmd = val;
        /* Handle ChipReset bit: reset internal registers. */
        if (val & NATSEMI_ChipReset) {
            /* Preserve some fields as driver does explicitly. We
             * cannot reconstruct those here, so we just clear core
             * state and let driver reprogram. */
            s->regs.chipcmd = 0;
            s->regs.intrstatus = 0;
            s->regs.intrmask = 0;
            s->regs.intrenable = 0;
            s->regs.txringptr = 0;
            s->regs.rxringptr = 0;
            s->regs.txconfig = 0;
            s->regs.rxconfig = 0;
        }
        break;
    case NATSEMI_ChipConfig:
        s->regs.chipconfig = val;
        break;
    case NATSEMI_EECtrl:
        s->regs.eectrl = val;
        break;
    case NATSEMI_PCIBusCfg:
        s->regs.pcibuscfg = val;
        /* EepromReload bit: immediately clear after a short delay
         * from driver pov; we simply clear to signal completion. */
        if (val & NATSEMI_EepromReload) {
            s->regs.pcibuscfg &= ~NATSEMI_EepromReload;
        }
        break;
    case NATSEMI_IntrStatus:
        /* Some bits may be W1C in real HW, but driver never writes it
         * directly, only reads; keep a simple OR. */
        s->regs.intrstatus = val;
        pcibase_update_irq(s);
        break;
    case NATSEMI_IntrMask:
        s->regs.intrmask = val;
        pcibase_update_irq(s);
        break;
    case NATSEMI_IntrEnable:
        s->regs.intrenable = val;
        pcibase_update_irq(s);
        break;
    case NATSEMI_IntrHoldoff:
        s->regs.intrholdoff = val;
        break;
    case NATSEMI_TxRingPtr:
        s->regs.txringptr = val;
        break;
    case NATSEMI_TxConfig:
        s->regs.txconfig = val;
        break;
    case NATSEMI_RxRingPtr:
        s->regs.rxringptr = val;
        break;
    case NATSEMI_RxConfig:
        s->regs.rxconfig = val;
        break;
    case NATSEMI_ClkRun:
        s->regs.clkrun = val;
        break;
    case NATSEMI_WOLCmd:
        s->regs.wolcmd = val;
        break;
    case NATSEMI_PauseCmd:
        s->regs.pausecmd = val;
        break;
    case NATSEMI_RxFilterAddr:
        s->regs.rxfilteraddr = val;
        break;
    case NATSEMI_RxFilterData:
        s->regs.rxfilterdata = val;
        break;
    case NATSEMI_BootRomAddr:
        s->regs.bootromaddr = val;
        break;
    case NATSEMI_BootRomData:
        s->regs.bootromdata = val;
        break;
    case NATSEMI_SiliconRev:
        s->regs.siliconrev = val;
        break;
    case NATSEMI_StatsCtrl:
        s->regs.statsctrl = val;
        if (val & NATSEMI_StatsClear) {
            s->regs.rxcrcerrs = 0;
            s->regs.rxmissed = 0;
        }
        break;
    case NATSEMI_StatsData:
        s->regs.statsdata = val;
        break;
    case NATSEMI_RxCRCErrs:
        s->regs.rxcrcerrs = val;
        break;
    case NATSEMI_RxMissed:
        s->regs.rxmissed = val;
        break;
    case NATSEMI_BasicControl:
        s->regs.basiccontrol = val;
        break;
    case NATSEMI_BasicStatus:
        s->regs.basicstatus = val;
        break;
    case NATSEMI_AnegAdv:
        s->regs.anegadv = val;
        break;
    case NATSEMI_AnegPeer:
        s->regs.anegpeer = val;
        break;
    case NATSEMI_PhyStatus:
        s->regs.phystatus = val;
        break;
    case NATSEMI_MIntrCtrl:
        s->regs.mintrctrl = val;
        break;
    case NATSEMI_MIntrStatus:
        s->regs.mintrstatus = val;
        break;
    case NATSEMI_PhyCtrl:
        s->regs.phyctrl = val;
        break;
    case NATSEMI_PGSEL:
        s->regs.pgsel = val;
        break;
    /* NATSEMI_PMDCSR shares offset with NATSEMI_PhyCtrl; keep only one case */
    case NATSEMI_TSTDAT:
        s->regs.tstdat = val;
        break;
    case NATSEMI_DSPCFG:
        s->regs.dspcfg = val;
        break;
    case NATSEMI_SDCFG:
        s->regs.sdcfg = val;
        break;
    default:
        break;
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size == 4) {
        val = pcibase_reg_read32(s, addr);
    } else if (size == 2) {
        uint32_t v = pcibase_reg_read32(s, addr & ~0x3);
        if (addr & 0x2) {
            val = (v >> 16) & 0xffff;
        } else {
            val = v & 0xffff;
        }
    } else if (size == 1) {
        uint32_t v = pcibase_reg_read32(s, addr & ~0x3);
        unsigned shift = (addr & 0x3) * 8;
        val = (v >> shift) & 0xff;
    } else {
        val = 0;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size == 4) {
        pcibase_reg_write32(s, addr, (uint32_t)val);
    } else if (size == 2) {
        uint32_t old = pcibase_reg_read32(s, addr & ~0x3);
        if (addr & 0x2) {
            old &= 0x0000ffff;
            old |= ((uint32_t)(val & 0xffff)) << 16;
        } else {
            old &= 0xffff0000;
            old |= (uint32_t)(val & 0xffff);
        }
        pcibase_reg_write32(s, addr & ~0x3, old);
    } else if (size == 1) {
        uint32_t old = pcibase_reg_read32(s, addr & ~0x3);
        unsigned shift = (addr & 0x3) * 8;
        uint32_t mask = 0xffu << shift;
        old &= ~mask;
        old |= ((uint32_t)(val & 0xff)) << shift;
        pcibase_reg_write32(s, addr & ~0x3, old);
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* Driver uses MMIO via ioremap; no PIO access expected. */
    (void)opaque;
    (void)addr;
    (void)size;
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    (void)opaque;
    (void)addr;
    (void)val;
    (void)size;
}

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

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    memset(&s->regs, 0, sizeof(s->regs));
    s->intr_status = 0;
    s->intr_mask = 0;

    /* Provide sane defaults expected by driver: link up, etc. */
    s->regs.basicstatus = 0x0000;
    /* The mii helper will read BMSR; link is determined there. It
     * reads from MDIO paths, which are implemented via EECtrl and
     * mii_getbit/mii_send_bits. Here we leave basicstatus 0. */

    s->regs.siliconrev = 0x00000010; /* arbitrary but non-zero */
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    hwaddr aligned_size = pow2ceil(bi->size ? bi->size : 4);
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  NATSEMI_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  NATSEMI_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, NATSEMI_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    s->num_bars = 1;
    s->bar_info[0].index = 1; /* driver uses pcibar = 1 */
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = NATSEMI_REGS_SIZE;
    s->bar_info[0].name = "natsemi-mmio";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = NULL;
    }
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = false;
    s->has_msix = false;

    memset(&s->regs, 0, sizeof(s->regs));
    s->intr_status = 0;
    s->intr_mask = 0;

    /* Provide silicon revision so driver prints something sane. */
    s->regs.siliconrev = 0x00000020;
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

static const VMStateDescription vmstate_pcibase = {
    .name = "natsemi_pci",
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

type_init(pcibase_register_types)

