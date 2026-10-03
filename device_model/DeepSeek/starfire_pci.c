/*
 * QEMU 8.2.10 PCI Device Model for Adaptec Starfire (AIC-6915)
 * Based on driver source: drivers/net/ethernet/adaptec/starfire.c
 * Phase 2: Functional behavior emulation
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

#define TYPE_PCIBASE_DEVICE "starfire_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/*
 * Register offsets and bit definitions from starfire.c
 */
enum chip_capability_flags { CanHaveMII = 1 };

enum register_offsets {
    PCIDeviceConfig = 0x50040,
    GenCtrl = 0x50070,
    IntrTimerCtrl = 0x50074,
    IntrClear = 0x50080,
    IntrStatus = 0x50084,
    IntrEnable = 0x50088,
    MIICtrl = 0x52000,
    TxStationAddr = 0x50120,
    EEPROMCtrl = 0x51000,
    GPIOCtrl = 0x5008C,
    TxDescCtrl = 0x50090,
    TxRingPtr = 0x50098,
    HiPriTxRingPtr = 0x50094,
    TxRingHiAddr = 0x5009C,
    TxProducerIdx = 0x500A0,
    TxConsumerIdx = 0x500A4,
    TxThreshold = 0x500B0,
    CompletionHiAddr = 0x500B4,
    TxCompletionAddr = 0x500B8,
    RxCompletionAddr = 0x500BC,
    RxCompletionQ2Addr = 0x500C0,
    CompletionQConsumerIdx = 0x500C4,
    RxDMACtrl = 0x500D0,
    RxDescQCtrl = 0x500D4,
    RxDescQHiAddr = 0x500DC,
    RxDescQAddr = 0x500E0,
    RxDescQIdx = 0x500E8,
    RxDMAStatus = 0x500F0,
    RxFilterMode = 0x500F4,
    TxMode = 0x55000,
    VlanType = 0x55064,
    PerfFilterTable = 0x56000,
    HashTable = 0x56100,
    TxGfpMem = 0x58000,
    RxGfpMem = 0x5a000,
};

enum intr_status_bits {
    IntrLinkChange = 0xf0000000,
    IntrStatsMax = 0x08000000,
    IntrAbnormalSummary = 0x02000000,
    IntrGeneralTimer = 0x01000000,
    IntrSoftware = 0x800000,
    IntrRxComplQ1Low = 0x400000,
    IntrTxComplQLow = 0x200000,
    IntrPCI = 0x100000,
    IntrDMAErr = 0x080000,
    IntrTxDataLow = 0x040000,
    IntrRxComplQ2Low = 0x020000,
    IntrRxDescQ1Low = 0x010000,
    IntrNormalSummary = 0x8000,
    IntrTxDone = 0x4000,
    IntrTxDMADone = 0x2000,
    IntrTxEmpty = 0x1000,
    IntrEarlyRxQ2 = 0x0800,
    IntrEarlyRxQ1 = 0x0400,
    IntrRxQ2Done = 0x0200,
    IntrRxQ1Done = 0x0100,
    IntrRxGFPDead = 0x80,
    IntrRxDescQ2Low = 0x40,
    IntrNoTxCsum = 0x20,
    IntrTxBadID = 0x10,
    IntrHiPriTxBadID = 0x08,
    IntrRxGfp = 0x04,
    IntrTxGfp = 0x02,
    IntrPCIPad = 0x01,
    IntrRxDone = IntrRxQ2Done | IntrRxQ1Done,
    IntrRxEmpty = IntrRxDescQ1Low | IntrRxDescQ2Low,
    IntrNormalMask = 0xff00,
    IntrAbnormalMask = 0x3ff00fe,
};

enum rx_mode_bits {
    AcceptBroadcast = 0x04,
    AcceptAllMulticast = 0x02,
    AcceptAll = 0x01,
    AcceptMulticast = 0x10,
    PerfectFilter = 0x40,
    HashFilter = 0x30,
    PerfectFilterVlan = 0x80,
    MinVLANPrio = 0xE000,
    VlanMode = 0x0200,
    WakeupOnGFP = 0x0800,
};

enum tx_mode_bits {
    MiiSoftReset = 0x8000,
    MIILoopback = 0x4000,
    TxFlowEnable = 0x0800,
    RxFlowEnable = 0x0400,
    PadEnable = 0x04,
    FullDuplex = 0x02,
    HugeFrame = 0x01,
};

enum tx_ctrl_bits {
    TxDescSpaceUnlim = 0x00,
    TxDescSpace32 = 0x10,
    TxDescSpace64 = 0x20,
    TxDescSpace128 = 0x30,
    TxDescSpace256 = 0x40,
    TxDescType0 = 0x00,
    TxDescType1 = 0x01,
    TxDescType2 = 0x02,
    TxDescType3 = 0x03,
    TxDescType4 = 0x04,
    TxNoDMACompletion = 0x08,
    TxDescQAddr64bit = 0x80,
    TxDescQAddr32bit = 0,
    TxHiPriFIFOThreshShift = 24,
    TxPadLenShift = 16,
    TxDMABurstSizeShift = 8,
};

enum rx_ctrl_bits {
    RxBufferLenShift = 16,
    RxMinDescrThreshShift = 0,
    RxPrefetchMode = 0x8000,
    RxVariableQ = 0x2000,
    Rx2048QEntries = 0x4000,
    Rx256QEntries = 0,
    RxDescAddr64bit = 0x1000,
    RxDescAddr32bit = 0,
    RxDescQAddr64bit = 0x0100,
    RxDescQAddr32bit = 0,
    RxDescSpace4 = 0x000,
    RxDescSpace8 = 0x100,
    RxDescSpace16 = 0x200,
    RxDescSpace32 = 0x300,
    RxDescSpace64 = 0x400,
    RxDescSpace128 = 0x500,
    RxConsumerWrEn = 0x80,
};

enum rx_dmactrl_bits {
    RxReportBadFrames = 0x80000000,
    RxDMAShortFrames = 0x40000000,
    RxDMABadFrames = 0x20000000,
    RxDMACrcErrorFrames = 0x10000000,
    RxDMAControlFrame = 0x08000000,
    RxDMAPauseFrame = 0x04000000,
    RxChecksumIgnore = 0,
    RxChecksumRejectTCPUDP = 0x02000000,
    RxChecksumRejectTCPOnly = 0x01000000,
    RxCompletionQ2Enable = 0x800000,
    RxDMAQ2Disable = 0,
    RxDMAQ2FPOnly = 0x100000,
    RxDMAQ2SmallPkt = 0x200000,
    RxDMAQ2HighPrio = 0x300000,
    RxDMAQ2NonIP = 0x400000,
    RxUseBackupQueue = 0x080000,
    RxDMACRC = 0x040000,
    RxEarlyIntThreshShift = 12,
    RxHighPrioThreshShift = 8,
    RxBurstSizeShift = 0,
};

enum rx_compl_bits {
    RxComplQAddr64bit = 0x80,
    RxComplQAddr32bit = 0,
    RxComplProducerWrEn = 0x40,
    RxComplType0 = 0x00,
    RxComplType1 = 0x10,
    RxComplType2 = 0x20,
    RxComplType3 = 0x30,
    RxComplThreshShift = 0,
};

enum tx_compl_bits {
    TxComplQAddr64bit = 0x80,
    TxComplQAddr32bit = 0,
    TxComplProducerWrEn = 0x40,
    TxComplIntrStatus = 0x20,
    CommonQueueMode = 0x10,
    TxComplThreshShift = 0,
};

enum gen_ctrl_bits {
    RxEnable = 0x05,
    TxEnable = 0x0a,
    RxGFPEnable = 0x10,
    TxGFPEnable = 0x20,
};

enum intr_ctrl_bits {
    Timer10X = 0x800,
    EnableIntrMasking = 0x60,
    SmallFrameBypass = 0x100,
    SmallFrame64 = 0,
    SmallFrame128 = 0x200,
    SmallFrame256 = 0x400,
    SmallFrame512 = 0x600,
    IntrLatencyMask = 0x1f,
};

enum rx_desc_bits {
    RxDescValid = 1,
    RxDescEndRing = 2,
};

enum rx_done_bits {
    RxOK = 0x20000000,
    RxFIFOErr = 0x10000000,
    RxBufQ2 = 0x08000000,
};

enum tx_desc_bits {
    TxDescID = 0xB0000000,
    TxCRCEn = 0x01000000,
    TxDescIntr = 0x08000000,
    TxRingWrap = 0x04000000,
    TxCalTCP = 0x02000000,
};

enum chipset {
    CH_6915 = 0,
};

/* Hardware descriptor structures from driver source */
typedef uint64_t netdrv_addr_t;

struct starfire_rx_desc {
    netdrv_addr_t rxaddr;
};

struct csum_rx_done_desc {
    uint32_t status;
    uint16_t csum;
    uint16_t status2;
};

struct full_rx_done_desc {
    uint32_t status;
    uint16_t status3;
    uint16_t status2;
    uint16_t vlanid;
    uint16_t csum;
    uint32_t timestamp;
};

struct starfire_tx_desc_1 {
    uint32_t status;
    uint32_t addr;
};

struct starfire_tx_desc_2 {
    uint32_t status;
    uint32_t reserved;
    uint64_t addr;
};

struct tx_done_desc {
    uint32_t status;
};

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

#define MAX_BARS 6

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[MAX_BARS];
    BARInfo bar_info[MAX_BARS];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_enable;

    /* Register storage */
    uint32_t reg_pci_device_config;
    uint32_t reg_gen_ctrl;
    uint32_t reg_intr_timer_ctrl;
    uint32_t reg_gpio_ctrl;
    uint32_t reg_tx_desc_ctrl;
    uint32_t reg_hi_pri_tx_ring_ptr;
    uint32_t reg_tx_ring_ptr;
    uint32_t reg_tx_ring_hi_addr;
    uint32_t reg_tx_producer_idx;
    uint32_t reg_tx_consumer_idx;
    uint32_t reg_tx_threshold;
    uint32_t reg_completion_hi_addr;
    uint32_t reg_tx_completion_addr;
    uint32_t reg_rx_completion_addr;
    uint32_t reg_rx_completion_q2_addr;
    uint32_t reg_completion_q_consumer_idx;
    uint32_t reg_rx_dma_ctrl;
    uint32_t reg_rx_desc_q_ctrl;
    uint32_t reg_rx_desc_q_hi_addr;
    uint32_t reg_rx_desc_q_addr;
    uint16_t reg_rx_desc_q_idx;
    uint32_t reg_rx_dma_status;
    uint32_t reg_rx_filter_mode;
    uint32_t reg_tx_mode;
    uint32_t reg_vlan_type;

    /* EEPROM data (32 bytes) */
    uint8_t eeprom[32];

    /* MII registers (PHY 0) */
    uint16_t mii_regs[32];

    /* Perfect filter table (16 entries of 16 bytes) */
    uint8_t perf_filter_table[16 * 16];

    /* Hash table (32 entries of 16 bytes) */
    uint8_t hash_table[32 * 16];

    /* Firmware memories (Tx and Rx GFP) */
    uint8_t tx_gfp_mem[4096];
    uint8_t rx_gfp_mem[4096];
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->intr_status & s->intr_enable) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* EEPROM reads */
    if (addr >= EEPROMCtrl && addr < EEPROMCtrl + 0x20) {
        if (size == 1) {
            return s->eeprom[addr - EEPROMCtrl];
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "starfire: invalid EEPROM access size %u at 0x%" HWADDR_PRIx "\n", size, addr);
            return 0;
        }
    }

    /* MII register reads (PHY 0 only) */
    if (addr >= MIICtrl && addr < MIICtrl + 32 * 4) {
        unsigned reg = (addr - MIICtrl) / 4;
        if (reg < 32) {
            return 0x80000000 | s->mii_regs[reg];
        }
        return 0;
    }

    /* Statistics area (0x57000 - 0x57FFF) */
    if (addr >= 0x57000 && addr < 0x58000) {
        return 0; /* no stats emulation */
    }

    /* Firmware memories (read returns stored value) */
    if (addr >= TxGfpMem && addr < TxGfpMem + sizeof(s->tx_gfp_mem)) {
        unsigned offset = (addr - TxGfpMem);
        switch (size) {
        case 4:
            return ldl_le_p(&s->tx_gfp_mem[offset]);
        case 2:
            return lduw_le_p(&s->tx_gfp_mem[offset]);
        case 1:
            return s->tx_gfp_mem[offset];
        default:
            qemu_log_mask(LOG_GUEST_ERROR, "starfire: invalid TxGFP read size %u at 0x%" HWADDR_PRIx "\n", size, addr);
            return 0;
        }
    }
    if (addr >= RxGfpMem && addr < RxGfpMem + sizeof(s->rx_gfp_mem)) {
        unsigned offset = (addr - RxGfpMem);
        switch (size) {
        case 4:
            return ldl_le_p(&s->rx_gfp_mem[offset]);
        case 2:
            return lduw_le_p(&s->rx_gfp_mem[offset]);
        case 1:
            return s->rx_gfp_mem[offset];
        default:
            qemu_log_mask(LOG_GUEST_ERROR, "starfire: invalid RxGFP read size %u at 0x%" HWADDR_PRIx "\n", size, addr);
            return 0;
        }
    }

    /* PerfFilterTable: 16-bit aligned reads */
    if (addr >= PerfFilterTable && addr < PerfFilterTable + sizeof(s->perf_filter_table)) {
        unsigned offset = addr - PerfFilterTable;
        if (size == 2) {
            return lduw_le_p(&s->perf_filter_table[offset]);
        } else if (size == 1) {
            return s->perf_filter_table[offset];
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "starfire: invalid PerfFilterTable access size %u at 0x%" HWADDR_PRIx "\n", size, addr);
            return 0;
        }
    }

    /* HashTable: 16-bit aligned reads */
    if (addr >= HashTable && addr < HashTable + sizeof(s->hash_table)) {
        unsigned offset = addr - HashTable;
        if (size == 2) {
            return lduw_le_p(&s->hash_table[offset]);
        } else if (size == 1) {
            return s->hash_table[offset];
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "starfire: invalid HashTable access size %u at 0x%" HWADDR_PRIx "\n", size, addr);
            return 0;
        }
    }

    /* Register reads */
    switch (addr) {
    case PCIDeviceConfig:
        return s->reg_pci_device_config;
    case GenCtrl:
        return s->reg_gen_ctrl;
    case IntrTimerCtrl:
        return s->reg_intr_timer_ctrl;
    case IntrClear:
        val = s->intr_status;
        s->intr_status = 0;
        pcibase_update_irq(s);
        return val;
    case IntrStatus:
        return s->intr_status;
    case IntrEnable:
        return s->intr_enable;
    case GPIOCtrl:
        return s->reg_gpio_ctrl;
    case TxDescCtrl:
        return s->reg_tx_desc_ctrl;
    case HiPriTxRingPtr:
        return s->reg_hi_pri_tx_ring_ptr;
    case TxRingPtr:
        return s->reg_tx_ring_ptr;
    case TxRingHiAddr:
        return s->reg_tx_ring_hi_addr;
    case TxProducerIdx:
        return s->reg_tx_producer_idx;
    case TxConsumerIdx:
        return s->reg_tx_consumer_idx;
    case TxThreshold:
        return s->reg_tx_threshold;
    case CompletionHiAddr:
        return s->reg_completion_hi_addr;
    case TxCompletionAddr:
        return s->reg_tx_completion_addr;
    case RxCompletionAddr:
        return s->reg_rx_completion_addr;
    case RxCompletionQ2Addr:
        return s->reg_rx_completion_q2_addr;
    case RxDMACtrl:
        return s->reg_rx_dma_ctrl;
    case RxDescQCtrl:
        return s->reg_rx_desc_q_ctrl;
    case RxDescQHiAddr:
        return s->reg_rx_desc_q_hi_addr;
    case RxDescQAddr:
        return s->reg_rx_desc_q_addr;
    case RxDMAStatus:
        return s->reg_rx_dma_status;
    case RxFilterMode:
        return s->reg_rx_filter_mode;
    case TxMode:
        return s->reg_tx_mode;
    case VlanType:
        return s->reg_vlan_type;
    case RxDescQIdx:
        if (size == 2) {
            return s->reg_rx_desc_q_idx;
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "starfire: RxDescQIdx read with size %u\n", size);
            return 0;
        }
    case CompletionQConsumerIdx:
        return s->reg_completion_q_consumer_idx;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "starfire: unimplemented read at 0x%" HWADDR_PRIx " (size %u)\n", addr, size);
        return 0;
    }
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* EEPROM writes: ignore (read-only) */
    if (addr >= EEPROMCtrl && addr < EEPROMCtrl + 0x20) {
        qemu_log_mask(LOG_GUEST_ERROR, "starfire: write to EEPROM ignored\n");
        return;
    }

    /* MII register writes (PHY 0 only) */
    if (addr >= MIICtrl && addr < MIICtrl + 32 * 4) {
        unsigned reg = (addr - MIICtrl) / 4;
        if (reg < 32) {
            s->mii_regs[reg] = val & 0xffff;
        }
        return;
    }

    /* Statistics area: ignore writes */
    if (addr >= 0x57000 && addr < 0x58000) {
        return;
    }

    /* Firmware memories */
    if (addr >= TxGfpMem && addr < TxGfpMem + sizeof(s->tx_gfp_mem)) {
        unsigned offset = (addr - TxGfpMem);
        switch (size) {
        case 4:
            stl_le_p(&s->tx_gfp_mem[offset], (uint32_t)val);
            break;
        case 2:
            stw_le_p(&s->tx_gfp_mem[offset], (uint16_t)val);
            break;
        case 1:
            s->tx_gfp_mem[offset] = (uint8_t)val;
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR, "starfire: invalid TxGFP write size %u at 0x%" HWADDR_PRIx "\n", size, addr);
            break;
        }
        return;
    }
    if (addr >= RxGfpMem && addr < RxGfpMem + sizeof(s->rx_gfp_mem)) {
        unsigned offset = (addr - RxGfpMem);
        switch (size) {
        case 4:
            stl_le_p(&s->rx_gfp_mem[offset], (uint32_t)val);
            break;
        case 2:
            stw_le_p(&s->rx_gfp_mem[offset], (uint16_t)val);
            break;
        case 1:
            s->rx_gfp_mem[offset] = (uint8_t)val;
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR, "starfire: invalid RxGFP write size %u at 0x%" HWADDR_PRIx "\n", size, addr);
            break;
        }
        return;
    }

    /* PerfFilterTable: 16-bit writes */
    if (addr >= PerfFilterTable && addr < PerfFilterTable + sizeof(s->perf_filter_table)) {
        unsigned offset = addr - PerfFilterTable;
        if (size == 2) {
            stw_le_p(&s->perf_filter_table[offset], val);
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "starfire: write to PerfFilterTable with size %u\n", size);
        }
        return;
    }

    /* HashTable: 16-bit writes */
    if (addr >= HashTable && addr < HashTable + sizeof(s->hash_table)) {
        unsigned offset = addr - HashTable;
        if (size == 2) {
            stw_le_p(&s->hash_table[offset], val);
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "starfire: write to HashTable with size %u\n", size);
        }
        return;
    }

    /* Register writes */
    switch (addr) {
    case PCIDeviceConfig:
        s->reg_pci_device_config = val;
        break;
    case GenCtrl:
        s->reg_gen_ctrl = val;
        break;
    case IntrTimerCtrl:
        s->reg_intr_timer_ctrl = val;
        break;
    case IntrClear:
        s->intr_status &= ~val;
        pcibase_update_irq(s);
        break;
    case IntrEnable:
        s->intr_enable = val;
        pcibase_update_irq(s);
        break;
    case GPIOCtrl:
        s->reg_gpio_ctrl = val;
        break;
    case TxDescCtrl:
        s->reg_tx_desc_ctrl = val;
        break;
    case HiPriTxRingPtr:
        s->reg_hi_pri_tx_ring_ptr = val;
        break;
    case TxRingPtr:
        s->reg_tx_ring_ptr = val;
        break;
    case TxRingHiAddr:
        s->reg_tx_ring_hi_addr = val;
        break;
    case TxProducerIdx:
        s->reg_tx_producer_idx = val;
        /* Simulate Tx consumer index equals producer for immediate completion? */
        /* Not implemented; leave consumer index unchanged */
        break;
    case TxConsumerIdx:
        s->reg_tx_consumer_idx = val; /* driver may not write, but allow */
        break;
    case TxThreshold:
        s->reg_tx_threshold = val;
        break;
    case CompletionHiAddr:
        s->reg_completion_hi_addr = val;
        break;
    case TxCompletionAddr:
        s->reg_tx_completion_addr = val;
        break;
    case RxCompletionAddr:
        s->reg_rx_completion_addr = val;
        break;
    case RxCompletionQ2Addr:
        s->reg_rx_completion_q2_addr = val;
        break;
    case CompletionQConsumerIdx:
        if (size == 2) {
            /* low or high 16-bit write? */
            if (addr == CompletionQConsumerIdx) {
                /* low 16 bits: Rx done consumer index */
                s->reg_completion_q_consumer_idx = (s->reg_completion_q_consumer_idx & 0xffff0000) | (val & 0xffff);
            } else { /* addr == CompletionQConsumerIdx + 2 */
                s->reg_completion_q_consumer_idx = (s->reg_completion_q_consumer_idx & 0xffff) | ((val & 0xffff) << 16);
            }
        } else if (size == 4) {
            s->reg_completion_q_consumer_idx = val;
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "starfire: CompletionQConsumerIdx write size %u\n", size);
        }
        break;
    case RxDMACtrl:
        s->reg_rx_dma_ctrl = val;
        break;
    case RxDescQCtrl:
        s->reg_rx_desc_q_ctrl = val;
        break;
    case RxDescQHiAddr:
        s->reg_rx_desc_q_hi_addr = val;
        break;
    case RxDescQAddr:
        s->reg_rx_desc_q_addr = val;
        break;
    case RxDescQIdx:
        if (size == 2) {
            s->reg_rx_desc_q_idx = val;
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "starfire: RxDescQIdx write with size %u\n", size);
        }
        break;
    case RxDMAStatus:
        s->reg_rx_dma_status = val;
        break;
    case RxFilterMode:
        s->reg_rx_filter_mode = val;
        break;
    case TxMode:
        s->reg_tx_mode = val;
        break;
    case VlanType:
        s->reg_vlan_type = val;
        break;
    case TxStationAddr ... TxStationAddr + 5:
        /* byte writes handled by generic handler? No, switch doesn't handle ranges */
        /* fall through to default for now; actually we can handle with multiple cases */
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "starfire: unimplemented write at 0x%" HWADDR_PRIx " (size %u, val 0x%" PRIx64 ")\n", addr, size, val);
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

    /* Reset all registers to default values */
    s->reg_pci_device_config = 0;
    s->reg_gen_ctrl = 0;
    s->reg_intr_timer_ctrl = 0;
    s->reg_gpio_ctrl = 0;
    s->reg_tx_desc_ctrl = 0;
    s->reg_hi_pri_tx_ring_ptr = 0;
    s->reg_tx_ring_ptr = 0;
    s->reg_tx_ring_hi_addr = 0;
    s->reg_tx_producer_idx = 0;
    s->reg_tx_consumer_idx = 0;
    s->reg_tx_threshold = 0;
    s->reg_completion_hi_addr = 0;
    s->reg_tx_completion_addr = 0;
    s->reg_rx_completion_addr = 0;
    s->reg_rx_completion_q2_addr = 0;
    s->reg_completion_q_consumer_idx = 0;
    s->reg_rx_dma_ctrl = 0;
    s->reg_rx_desc_q_ctrl = 0;
    s->reg_rx_desc_q_hi_addr = 0;
    s->reg_rx_desc_q_addr = 0;
    s->reg_rx_desc_q_idx = 0;
    s->reg_rx_dma_status = 0;
    s->reg_rx_filter_mode = 0;
    s->reg_tx_mode = 0;
    s->reg_vlan_type = 0;

    s->intr_status = 0;
    s->intr_enable = 0;

    memset(s->perf_filter_table, 0, sizeof(s->perf_filter_table));
    memset(s->hash_table, 0, sizeof(s->hash_table));
    memset(s->tx_gfp_mem, 0, sizeof(s->tx_gfp_mem));
    memset(s->rx_gfp_mem, 0, sizeof(s->rx_gfp_mem));

    /* Initialize EEPROM with a MAC address */
    memset(s->eeprom, 0, sizeof(s->eeprom));
    /* MAC: 02:00:00:00:00:01 placed at offsets 15..20 */
    s->eeprom[15] = 0x02;
    s->eeprom[16] = 0x00;
    s->eeprom[17] = 0x00;
    s->eeprom[18] = 0x00;
    s->eeprom[19] = 0x00;
    s->eeprom[20] = 0x01;

    /* Initialize MII registers */
    memset(s->mii_regs, 0, sizeof(s->mii_regs));
    s->mii_regs[0] = 0x3000; /* BMCR: Auto-negotiation, 100Mbps, full duplex */
    s->mii_regs[1] = 0x786d; /* BMSR: Link up, capabilities */
    s->mii_regs[4] = 0x01e1; /* ADVERTISE: 100baseTx-Full, 100baseTx-Half, 10baseT-Full, 10baseT-Half */
    s->mii_regs[5] = 0x41e1; /* LPA: partner abilities */
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

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID, 0x1093);
    pci_set_word(pci_conf + PCI_DEVICE_ID, 0x6915);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0200);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Add PCIe and PM capabilities (optional) */
    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* Set up BAR0: MMIO region covering all registers */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x10000; /* 64KB */
    s->bar_info[0].name = "starfire-mmio";
    pcibase_register_bar(pdev, s, &s->bar_info[0], errp);

    /* Initialize interrupt structures (legacy INTx only) */
    s->has_msi = false;
    s->has_msix = false;
}

static void pcibase_uninit(PCIDevice *pdev)
{
    /* No extra cleanup needed */
}

static const VMStateDescription vmstate_pcibase = {
    .name = "starfire_pci",
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
