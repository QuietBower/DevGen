/*
 * QEMU PCI device model for Adaptec Starfire (starfire.c driver)
 * Functional emulation based strictly on provided driver-visible register use.
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

#include <stdint.h>
#include <string.h>

#define TYPE_PCIBASE_DEVICE "starfire_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define STARFIRE_VENDOR_ID 0x9004
#define STARFIRE_DEVICE_ID 0x6915
#define STARFIRE_CLASS_ID 0x0200

enum starfire_register_offsets {
    STARFIRE_PCIDeviceConfig   = 0x50040,
    STARFIRE_GenCtrl           = 0x50070,
    STARFIRE_IntrTimerCtrl     = 0x50074,
    STARFIRE_IntrClear         = 0x50080,
    STARFIRE_IntrStatus        = 0x50084,
    STARFIRE_IntrEnable        = 0x50088,
    STARFIRE_MIICtrl           = 0x52000,
    STARFIRE_TxStationAddr     = 0x50120,
    STARFIRE_EEPROMCtrl        = 0x51000,
    STARFIRE_GPIOCtrl          = 0x5008C,
    STARFIRE_TxDescCtrl        = 0x50090,
    STARFIRE_TxRingPtr         = 0x50098,
    STARFIRE_HiPriTxRingPtr    = 0x50094,
    STARFIRE_TxRingHiAddr      = 0x5009C,
    STARFIRE_TxProducerIdx     = 0x500A0,
    STARFIRE_TxConsumerIdx     = 0x500A4,
    STARFIRE_TxThreshold       = 0x500B0,
    STARFIRE_CompletionHiAddr  = 0x500B4,
    STARFIRE_TxCompletionAddr  = 0x500B8,
    STARFIRE_RxCompletionAddr  = 0x500BC,
    STARFIRE_RxCompletionQ2Addr= 0x500C0,
    STARFIRE_CompletionQConsumerIdx = 0x500C4,
    STARFIRE_RxDMACtrl         = 0x500D0,
    STARFIRE_RxDescQCtrl       = 0x500D4,
    STARFIRE_RxDescQHiAddr     = 0x500DC,
    STARFIRE_RxDescQAddr       = 0x500E0,
    STARFIRE_RxDescQIdx        = 0x500E8,
    STARFIRE_RxDMAStatus       = 0x500F0,
    STARFIRE_RxFilterMode      = 0x500F4,
    STARFIRE_TxMode            = 0x55000,
    STARFIRE_VlanType          = 0x55064,
    STARFIRE_PerfFilterTable   = 0x56000,
    STARFIRE_HashTable         = 0x56100,
    STARFIRE_TxGfpMem          = 0x58000,
    STARFIRE_RxGfpMem          = 0x5a000,
};

enum starfire_intr_status_bits {
    STARFIRE_IntrLinkChange       = 0xf0000000,
    STARFIRE_IntrStatsMax         = 0x08000000,
    STARFIRE_IntrAbnormalSummary  = 0x02000000,
    STARFIRE_IntrGeneralTimer     = 0x01000000,
    STARFIRE_IntrSoftware         = 0x00800000,
    STARFIRE_IntrRxComplQ1Low     = 0x00400000,
    STARFIRE_IntrTxComplQLow      = 0x00200000,
    STARFIRE_IntrPCI              = 0x00100000,
    STARFIRE_IntrDMAErr           = 0x00080000,
    STARFIRE_IntrTxDataLow        = 0x00040000,
    STARFIRE_IntrRxComplQ2Low     = 0x00020000,
    STARFIRE_IntrRxDescQ1Low      = 0x00010000,
    STARFIRE_IntrNormalSummary    = 0x00008000,
    STARFIRE_IntrTxDone           = 0x00004000,
    STARFIRE_IntrTxDMADone        = 0x00002000,
    STARFIRE_IntrTxEmpty          = 0x00001000,
    STARFIRE_IntrEarlyRxQ2        = 0x00000800,
    STARFIRE_IntrEarlyRxQ1        = 0x00000400,
    STARFIRE_IntrRxQ2Done         = 0x00000200,
    STARFIRE_IntrRxQ1Done         = 0x00000100,
    STARFIRE_IntrRxGFPDead        = 0x00000080,
    STARFIRE_IntrRxDescQ2Low      = 0x00000040,
    STARFIRE_IntrNoTxCsum         = 0x00000020,
    STARFIRE_IntrTxBadID          = 0x00000010,
    STARFIRE_IntrHiPriTxBadID     = 0x00000008,
    STARFIRE_IntrRxGfp            = 0x00000004,
    STARFIRE_IntrTxGfp            = 0x00000002,
    STARFIRE_IntrPCIPad           = 0x00000001,
    STARFIRE_IntrRxDone           = 0x00000300,
    STARFIRE_IntrRxEmpty          = 0x00010040,
    STARFIRE_IntrNormalMask       = 0x0000ff00,
    STARFIRE_IntrAbnormalMask     = 0x03ff00fe,
};

enum starfire_chip_capability_flags { STARFIRE_CanHaveMII = 1 };

enum starfire_rx_mode_bits {
    STARFIRE_AcceptBroadcast   = 0x04,
    STARFIRE_AcceptAllMulticast= 0x02,
    STARFIRE_AcceptAll        = 0x01,
    STARFIRE_AcceptMulticast  = 0x10,
    STARFIRE_PerfectFilter    = 0x40,
    STARFIRE_HashFilter       = 0x30,
    STARFIRE_PerfectFilterVlan= 0x80,
    STARFIRE_MinVLANPrio      = 0xE000,
    STARFIRE_VlanMode         = 0x0200,
    STARFIRE_WakeupOnGFP      = 0x0800,
};

enum starfire_tx_mode_bits {
    STARFIRE_MiiSoftReset  = 0x8000,
    STARFIRE_MIILoopback   = 0x4000,
    STARFIRE_TxFlowEnable  = 0x0800,
    STARFIRE_RxFlowEnable  = 0x0400,
    STARFIRE_PadEnable     = 0x0004,
    STARFIRE_FullDuplex    = 0x0002,
    STARFIRE_HugeFrame     = 0x0001,
};

enum starfire_tx_ctrl_bits {
    STARFIRE_TxDescSpaceUnlim  = 0x00,
    STARFIRE_TxDescSpace32     = 0x10,
    STARFIRE_TxDescSpace64     = 0x20,
    STARFIRE_TxDescSpace128    = 0x30,
    STARFIRE_TxDescSpace256    = 0x40,
    STARFIRE_TxDescType0       = 0x00,
    STARFIRE_TxDescType1       = 0x01,
    STARFIRE_TxDescType2       = 0x02,
    STARFIRE_TxDescType3       = 0x03,
    STARFIRE_TxDescType4       = 0x04,
    STARFIRE_TxNoDMACompletion = 0x08,
    STARFIRE_TxDescQAddr64bit  = 0x80,
    STARFIRE_TxDescQAddr32bit  = 0x00,
    STARFIRE_TxHiPriFIFOThreshShift = 24,
    STARFIRE_TxPadLenShift     = 16,
    STARFIRE_TxDMABurstSizeShift = 8,
};

enum starfire_rx_ctrl_bits {
    STARFIRE_RxBufferLenShift      = 16,
    STARFIRE_RxMinDescrThreshShift = 0,
    STARFIRE_RxPrefetchMode        = 0x8000,
    STARFIRE_RxVariableQ           = 0x2000,
    STARFIRE_Rx2048QEntries        = 0x4000,
    STARFIRE_Rx256QEntries         = 0x0000,
    STARFIRE_RxDescAddr64bit       = 0x1000,
    STARFIRE_RxDescAddr32bit       = 0x0000,
    STARFIRE_RxDescQAddr64bit      = 0x0100,
    STARFIRE_RxDescQAddr32bit      = 0x0000,
    STARFIRE_RxDescSpace4          = 0x0000,
    STARFIRE_RxDescSpace8          = 0x0100,
    STARFIRE_RxDescSpace16         = 0x0200,
    STARFIRE_RxDescSpace32         = 0x0300,
    STARFIRE_RxDescSpace64         = 0x0400,
    STARFIRE_RxDescSpace128        = 0x0500,
    STARFIRE_RxConsumerWrEn        = 0x0080,
};

enum starfire_rx_dmactrl_bits {
    STARFIRE_RxReportBadFrames      = 0x80000000,
    STARFIRE_RxDMAShortFrames       = 0x40000000,
    STARFIRE_RxDMABadFrames         = 0x20000000,
    STARFIRE_RxDMACrcErrorFrames    = 0x10000000,
    STARFIRE_RxDMAControlFrame      = 0x08000000,
    STARFIRE_RxDMAPauseFrame        = 0x04000000,
    STARFIRE_RxChecksumIgnore       = 0x00000000,
    STARFIRE_RxChecksumRejectTCPUDP = 0x02000000,
    STARFIRE_RxChecksumRejectTCPOnly= 0x01000000,
    STARFIRE_RxCompletionQ2Enable   = 0x00800000,
    STARFIRE_RxDMAQ2Disable         = 0x00000000,
    STARFIRE_RxDMAQ2FPOnly          = 0x00100000,
    STARFIRE_RxDMAQ2SmallPkt        = 0x00200000,
    STARFIRE_RxDMAQ2HighPrio        = 0x00300000,
    STARFIRE_RxDMAQ2NonIP           = 0x00400000,
    STARFIRE_RxUseBackupQueue       = 0x00080000,
    STARFIRE_RxDMACRC               = 0x00040000,
    STARFIRE_RxEarlyIntThreshShift  = 12,
    STARFIRE_RxHighPrioThreshShift  = 8,
    STARFIRE_RxBurstSizeShift       = 0,
};

enum starfire_rx_compl_bits {
    STARFIRE_RxComplQAddr64bit   = 0x80,
    STARFIRE_RxComplQAddr32bit   = 0x00,
    STARFIRE_RxComplProducerWrEn = 0x40,
    STARFIRE_RxComplType0        = 0x00,
    STARFIRE_RxComplType1        = 0x10,
    STARFIRE_RxComplType2        = 0x20,
    STARFIRE_RxComplType3        = 0x30,
    STARFIRE_RxComplThreshShift  = 0,
};

enum starfire_tx_compl_bits {
    STARFIRE_TxComplQAddr64bit   = 0x80,
    STARFIRE_TxComplQAddr32bit   = 0x00,
    STARFIRE_TxComplProducerWrEn = 0x40,
    STARFIRE_TxComplIntrStatus   = 0x20,
    STARFIRE_CommonQueueMode     = 0x10,
    STARFIRE_TxComplThreshShift  = 0,
};

enum starfire_gen_ctrl_bits {
    STARFIRE_RxEnable    = 0x05,
    STARFIRE_TxEnable    = 0x0a,
    STARFIRE_RxGFPEnable = 0x10,
    STARFIRE_TxGFPEnable = 0x20,
};

enum starfire_intr_ctrl_bits {
    STARFIRE_Timer10X          = 0x0800,
    STARFIRE_EnableIntrMasking = 0x0060,
    STARFIRE_SmallFrameBypass  = 0x0100,
    STARFIRE_SmallFrame64      = 0x0000,
    STARFIRE_SmallFrame128     = 0x0200,
    STARFIRE_SmallFrame256     = 0x0400,
    STARFIRE_SmallFrame512     = 0x0600,
    STARFIRE_IntrLatencyMask   = 0x001f,
};

enum starfire_rx_desc_bits { STARFIRE_RxDescValid = 0x01, STARFIRE_RxDescEndRing = 0x02 };

enum starfire_rx_done_bits { STARFIRE_RxOK = 0x20000000, STARFIRE_RxFIFOErr = 0x10000000, STARFIRE_RxBufQ2 = 0x08000000 };

enum starfire_tx_desc_bits { STARFIRE_TxDescID = 0xB0000000, STARFIRE_TxCRCEn = 0x01000000, STARFIRE_TxDescIntr = 0x08000000, STARFIRE_TxRingWrap = 0x04000000, STARFIRE_TxCalTCP = 0x02000000 };

enum starfire_chipset { STARFIRE_CH_6915 = 0 };

struct starfire_rx_desc_hw { uint64_t rxaddr; };
struct starfire_csum_rx_done_desc_hw { uint32_t status; uint16_t csum; uint16_t status2; };
struct starfire_full_rx_done_desc_hw { uint32_t status; uint16_t status3; uint16_t status2; uint16_t vlanid; uint16_t csum; uint32_t timestamp; };
struct starfire_tx_desc_1_hw { uint32_t status; uint32_t addr; };
struct starfire_tx_desc_2_hw { uint32_t status; uint32_t reserved; uint64_t addr; };
struct starfire_tx_done_desc_hw { uint32_t status; };

typedef enum { BAR_TYPE_NONE = 0, BAR_TYPE_MMIO, BAR_TYPE_PIO, BAR_TYPE_RAM } BARType;
typedef struct { int index; BARType type; hwaddr size; const char *name; } BARInfo;

struct PCIBaseState {
    PCIDevice parent_obj;
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;
    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;
    uint32_t gen_ctrl;
    uint32_t intr_timer_ctrl;
    uint32_t intr_enable;
    uint32_t mii_ctrl;
    uint32_t tx_mode;
    uint32_t rx_filter_mode;
    uint64_t tx_ring_ptr;
    uint64_t tx_completion_addr;
    uint64_t rx_desc_q_addr;
    uint64_t rx_completion_addr;
    uint32_t status_flags;
    uint32_t reset_state;
    uint8_t power_state;
    uint8_t eeprom[0x20];
    uint8_t mac[6];
    uint16_t phy_regs[32][32];
};

static void pcibase_update_irq(PCIBaseState *s)
{
    if (s->intr_status & s->intr_enable) {
        pci_set_irq(PCI_DEVICE(s), 1);
    } else {
        pci_set_irq(PCI_DEVICE(s), 0);
    }
}

static uint16_t pcibase_mdio_read(PCIBaseState *s, uint8_t phy, uint8_t reg)
{
    phy &= 0x1f;
    reg &= 0x1f;
    return s->phy_regs[phy][reg];
}

static void pcibase_mdio_write(PCIBaseState *s, uint8_t phy, uint8_t reg, uint16_t val)
{
    phy &= 0x1f;
    reg &= 0x1f;
    s->phy_regs[phy][reg] = val;
    if (reg == 0 && (val & 0x8000)) {
        s->phy_regs[phy][0] &= ~0x8000;
        s->phy_regs[phy][1] |= 0x0004;
        s->phy_regs[phy][1] |= 0x0001;
    }
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    s->intr_status = 0;
    s->intr_mask = 0;
    s->gen_ctrl = 0;
    s->intr_timer_ctrl = 0;
    s->intr_enable = 0;
    s->mii_ctrl = 0;
    s->tx_mode = 0;
    s->rx_filter_mode = 0;
    s->tx_ring_ptr = 0;
    s->tx_completion_addr = 0;
    s->rx_desc_q_addr = 0;
    s->rx_completion_addr = 0;
    s->status_flags = 0;
    s->reset_state = 0;
    s->power_state = 0;
    memset(s->phy_regs, 0, sizeof(s->phy_regs));
    for (int phy = 0; phy < 32; phy++) {
        s->phy_regs[phy][1] = 0x7809;
        s->phy_regs[phy][2] = 0x0007;
        s->phy_regs[phy][3] = 0xc0f1;
        s->phy_regs[phy][4] = 0x01e1;
        s->phy_regs[phy][5] = 0x0000;
        s->phy_regs[phy][6] = 0x0000;
    }
    s->phy_regs[1][1] = 0x7849;
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    switch (addr) {
    case STARFIRE_PCIDeviceConfig:
        val = pci_get_long(PCI_DEVICE(s)->config + PCI_COMMAND);
        break;
    case STARFIRE_GenCtrl:
        val = s->gen_ctrl;
        break;
    case STARFIRE_IntrTimerCtrl:
        val = s->intr_timer_ctrl;
        break;
    case STARFIRE_IntrClear:
    case STARFIRE_IntrStatus:
        val = s->intr_status;
        break;
    case STARFIRE_IntrEnable:
        val = s->intr_enable;
        break;
    case STARFIRE_TxMode:
        val = s->tx_mode;
        break;
    case STARFIRE_RxFilterMode:
        val = s->rx_filter_mode;
        break;
    case STARFIRE_TxRingPtr:
        val = s->tx_ring_ptr;
        break;
    case STARFIRE_TxCompletionAddr:
        val = s->tx_completion_addr;
        break;
    case STARFIRE_RxDescQAddr:
        val = s->rx_desc_q_addr;
        break;
    case STARFIRE_RxCompletionAddr:
        val = s->rx_completion_addr;
        break;
    case STARFIRE_RxDMAStatus:
        val = 0;
        break;
    default:
        if (addr >= STARFIRE_EEPROMCtrl && addr < STARFIRE_EEPROMCtrl + sizeof(s->eeprom)) {
            val = s->eeprom[addr - STARFIRE_EEPROMCtrl];
        } else if (addr >= STARFIRE_MIICtrl && addr < STARFIRE_MIICtrl + (32 * 128)) {
            uint32_t rel = addr - STARFIRE_MIICtrl;
            uint8_t phy = (rel >> 7) & 0x1f;
            uint8_t reg = (rel >> 2) & 0x1f;
            val = pcibase_mdio_read(s, phy, reg);
            val |= 0x80000000;
        }
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    switch (addr) {
    case STARFIRE_PCIDeviceConfig:
        pci_set_long(PCI_DEVICE(s)->config + PCI_COMMAND, (uint32_t)val);
        if (val & 1) {
            s->reset_state = 1;
            s->intr_status = 0;
            pcibase_update_irq(s);
            s->reset_state = 0;
        }
        break;
    case STARFIRE_GenCtrl:
        s->gen_ctrl = val;
        break;
    case STARFIRE_IntrTimerCtrl:
        s->intr_timer_ctrl = val;
        break;
    case STARFIRE_IntrClear:
        s->intr_status &= ~((uint32_t)val);
        pcibase_update_irq(s);
        break;
    case STARFIRE_IntrEnable:
        s->intr_enable = val;
        pcibase_update_irq(s);
        break;
    case STARFIRE_TxMode:
        s->tx_mode = val;
        break;
    case STARFIRE_RxFilterMode:
        s->rx_filter_mode = val;
        break;
    case STARFIRE_TxRingPtr:
        s->tx_ring_ptr = (s->tx_ring_ptr & 0xffffffff00000000ULL) | (uint32_t)val;
        break;
    case STARFIRE_HiPriTxRingPtr:
    case STARFIRE_TxRingHiAddr:
        s->tx_ring_ptr = ((uint64_t)(uint32_t)val << 32) | (s->tx_ring_ptr & 0xffffffffULL);
        break;
    case STARFIRE_TxProducerIdx:
        s->status_flags = (uint32_t)val;
        break;
    case STARFIRE_TxConsumerIdx:
        break;
    case STARFIRE_TxThreshold:
        break;
    case STARFIRE_CompletionHiAddr:
        break;
    case STARFIRE_TxCompletionAddr:
        s->tx_completion_addr = (s->tx_completion_addr & 0xffffffff00000000ULL) | (uint32_t)val;
        break;
    case STARFIRE_RxCompletionAddr:
        s->rx_completion_addr = (s->rx_completion_addr & 0xffffffff00000000ULL) | (uint32_t)val;
        break;
    case STARFIRE_RxCompletionQ2Addr:
        break;
    case STARFIRE_CompletionQConsumerIdx:
        break;
    case STARFIRE_RxDMACtrl:
        break;
    case STARFIRE_RxDescQCtrl:
        break;
    case STARFIRE_RxDescQHiAddr:
        s->rx_desc_q_addr = ((uint64_t)(uint32_t)val << 32) | (s->rx_desc_q_addr & 0xffffffffULL);
        break;
    case STARFIRE_RxDescQAddr:
        s->rx_desc_q_addr = (s->rx_desc_q_addr & 0xffffffff00000000ULL) | (uint32_t)val;
        break;
    case STARFIRE_RxDescQIdx:
        break;
    case STARFIRE_RxDMAStatus:
        break;
    case STARFIRE_TxStationAddr:
    case STARFIRE_TxStationAddr + 1:
    case STARFIRE_TxStationAddr + 2:
    case STARFIRE_TxStationAddr + 3:
    case STARFIRE_TxStationAddr + 4:
    case STARFIRE_TxStationAddr + 5:
        s->mac[5 - (addr - STARFIRE_TxStationAddr)] = (uint8_t)val;
        break;
    default:
        if (addr >= STARFIRE_EEPROMCtrl && addr < STARFIRE_EEPROMCtrl + sizeof(s->eeprom)) {
            s->eeprom[addr - STARFIRE_EEPROMCtrl] = (uint8_t)val;
        } else if (addr >= STARFIRE_MIICtrl && addr < STARFIRE_MIICtrl + (32 * 128)) {
            uint32_t rel = addr - STARFIRE_MIICtrl;
            uint8_t phy = (rel >> 7) & 0x1f;
            uint8_t reg = (rel >> 2) & 0x1f;
            pcibase_mdio_write(s, phy, reg, (uint16_t)val);
        }
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
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

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }
    hwaddr aligned_size = pow2ceil(bi->size ? bi->size : 1);
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

    pci_set_word(pci_conf + PCI_VENDOR_ID, STARFIRE_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, STARFIRE_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, STARFIRE_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    s->num_bars = 1;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = 1 * MiB, .name = "starfire-mmio" };
    for (int i = 1; i < 6; i++) {
        s->bar_info[i] = (BARInfo){ .index = i, .type = BAR_TYPE_NONE, .size = 0, .name = "" };
    }
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = false;
    s->has_msix = false;

    memset(s->eeprom, 0, sizeof(s->eeprom));
    s->eeprom[0x14] = 0x00;
    s->eeprom[0x15] = 0x11;
    s->eeprom[0x16] = 0x22;
    s->eeprom[0x17] = 0x33;
    s->eeprom[0x18] = 0x44;
    s->eeprom[0x19] = 0x55;

    s->mac[0] = 0x00;
    s->mac[1] = 0x11;
    s->mac[2] = 0x22;
    s->mac[3] = 0x33;
    s->mac[4] = 0x44;
    s->mac[5] = 0x55;

    pcibase_reset(DEVICE(pdev));
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, 0);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
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
    k->exit = pcibase_uninit;
    dc->reset = pcibase_reset;
    dc->vmsd = &vmstate_pcibase;
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

type_init(pcibase_register_types)
