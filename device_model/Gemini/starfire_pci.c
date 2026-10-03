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


#define TYPE_PCIBASE_DEVICE "starfire_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x9004 /* PCI_VENDOR_ID_ADAPTEC */
#define DEVICE_ID 0x6915
#define CLASS_ID PCI_CLASS_NETWORK_ETHERNET

enum register_offsets {
    PCIDeviceConfig=0x50040, GenCtrl=0x50070, IntrTimerCtrl=0x50074,
    IntrClear=0x50080, IntrStatus=0x50084, IntrEnable=0x50088,
    MIICtrl=0x52000, TxStationAddr=0x50120, EEPROMCtrl=0x51000,
    GPIOCtrl=0x5008C, TxDescCtrl=0x50090,
    TxRingPtr=0x50098, HiPriTxRingPtr=0x50094,
    TxRingHiAddr=0x5009C,
    TxProducerIdx=0x500A0, TxConsumerIdx=0x500A4,
    TxThreshold=0x500B0,
    CompletionHiAddr=0x500B4, TxCompletionAddr=0x500B8,
    RxCompletionAddr=0x500BC, RxCompletionQ2Addr=0x500C0,
    CompletionQConsumerIdx=0x500C4, RxDMACtrl=0x500D0,
    RxDescQCtrl=0x500D4, RxDescQHiAddr=0x500DC, RxDescQAddr=0x500E0,
    RxDescQIdx=0x500E8, RxDMAStatus=0x500F0, RxFilterMode=0x500F4,
    TxMode=0x55000, VlanType=0x55064,
    PerfFilterTable=0x56000, HashTable=0x56100,
    TxGfpMem=0x58000, RxGfpMem=0x5a000,
};

enum intr_status_bits {
    IntrLinkChange=0xf0000000, IntrStatsMax=0x08000000,
    IntrAbnormalSummary=0x02000000, IntrGeneralTimer=0x01000000,
    IntrSoftware=0x800000, IntrRxComplQ1Low=0x400000,
    IntrTxComplQLow=0x200000, IntrPCI=0x100000,
    IntrDMAErr=0x080000, IntrTxDataLow=0x040000,
    IntrRxComplQ2Low=0x020000, IntrRxDescQ1Low=0x010000,
    IntrNormalSummary=0x8000, IntrTxDone=0x4000,
    IntrTxDMADone=0x2000, IntrTxEmpty=0x1000,
    IntrEarlyRxQ2=0x0800, IntrEarlyRxQ1=0x0400,
    IntrRxQ2Done=0x0200, IntrRxQ1Done=0x0100,
    IntrRxGFPDead=0x80, IntrRxDescQ2Low=0x40,
    IntrNoTxCsum=0x20, IntrTxBadID=0x10,
    IntrHiPriTxBadID=0x08, IntrRxGfp=0x04,
    IntrTxGfp=0x02, IntrPCIPad=0x01,
    IntrRxDone=IntrRxQ2Done | IntrRxQ1Done,
    IntrRxEmpty=IntrRxDescQ1Low | IntrRxDescQ2Low,
    IntrNormalMask=0xff00, IntrAbnormalMask=0x3ff00fe,
};

enum rx_mode_bits {
    AcceptBroadcast=0x04, AcceptAllMulticast=0x02, AcceptAll=0x01,
    AcceptMulticast=0x10, PerfectFilter=0x40, HashFilter=0x30,
    PerfectFilterVlan=0x80, MinVLANPrio=0xE000, VlanMode=0x0200,
    WakeupOnGFP=0x0800,
};

enum tx_mode_bits {
    MiiSoftReset=0x8000, MIILoopback=0x4000,
    TxFlowEnable=0x0800, RxFlowEnable=0x0400,
    PadEnable=0x04, FullDuplex=0x02, HugeFrame=0x01,
};

enum tx_ctrl_bits {
    TxDescSpaceUnlim=0x00, TxDescSpace32=0x10, TxDescSpace64=0x20,
    TxDescSpace128=0x30, TxDescSpace256=0x40,
    TxDescType0=0x00, TxDescType1=0x01, TxDescType2=0x02,
    TxDescType3=0x03, TxDescType4=0x04,
    TxNoDMACompletion=0x08,
    TxDescQAddr64bit=0x80, TxDescQAddr32bit=0,
    TxHiPriFIFOThreshShift=24, TxPadLenShift=16,
    TxDMABurstSizeShift=8,
};

enum rx_ctrl_bits {
    RxBufferLenShift=16, RxMinDescrThreshShift=0,
    RxPrefetchMode=0x8000, RxVariableQ=0x2000,
    Rx2048QEntries=0x4000, Rx256QEntries=0,
    RxDescAddr64bit=0x1000, RxDescAddr32bit=0,
    RxDescQAddr64bit=0x0100, RxDescQAddr32bit=0,
    RxDescSpace4=0x000, RxDescSpace8=0x100,
    RxDescSpace16=0x200, RxDescSpace32=0x300,
    RxDescSpace64=0x400, RxDescSpace128=0x500,
    RxConsumerWrEn=0x80,
};

enum rx_dmactrl_bits {
    RxReportBadFrames=0x80000000, RxDMAShortFrames=0x40000000,
    RxDMABadFrames=0x20000000, RxDMACrcErrorFrames=0x10000000,
    RxDMAControlFrame=0x08000000, RxDMAPauseFrame=0x04000000,
    RxChecksumIgnore=0, RxChecksumRejectTCPUDP=0x02000000,
    RxChecksumRejectTCPOnly=0x01000000,
    RxCompletionQ2Enable=0x800000,
    RxDMAQ2Disable=0, RxDMAQ2FPOnly=0x100000,
    RxDMAQ2SmallPkt=0x200000, RxDMAQ2HighPrio=0x300000,
    RxDMAQ2NonIP=0x400000,
    RxUseBackupQueue=0x080000, RxDMACRC=0x040000,
    RxEarlyIntThreshShift=12, RxHighPrioThreshShift=8,
    RxBurstSizeShift=0,
};

enum rx_compl_bits {
    RxComplQAddr64bit=0x80, RxComplQAddr32bit=0,
    RxComplProducerWrEn=0x40,
    RxComplType0=0x00, RxComplType1=0x10,
    RxComplType2=0x20, RxComplType3=0x30,
    RxComplThreshShift=0,
};

enum tx_compl_bits {
    TxComplQAddr64bit=0x80, TxComplQAddr32bit=0,
    TxComplProducerWrEn=0x40,
    TxComplIntrStatus=0x20,
    CommonQueueMode=0x10,
    TxComplThreshShift=0,
};

enum gen_ctrl_bits {
    RxEnable=0x05, TxEnable=0x0a,
    RxGFPEnable=0x10, TxGFPEnable=0x20,
};

enum intr_ctrl_bits {
    Timer10X=0x800, EnableIntrMasking=0x60, SmallFrameBypass=0x100,
    SmallFrame64=0, SmallFrame128=0x200, SmallFrame256=0x400, SmallFrame512=0x600,
    IntrLatencyMask=0x1f,
};

enum rx_desc_bits {
    RxDescValid=1, RxDescEndRing=2,
};

enum rx_done_bits {
    RxOK=0x20000000, RxFIFOErr=0x10000000, RxBufQ2=0x08000000,
};

enum tx_desc_bits {
    TxDescID=0xB0000000,
    TxCRCEn=0x01000000, TxDescIntr=0x08000000,
    TxRingWrap=0x04000000, TxCalTCP=0x02000000,
};

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
    uint32_t intr_enable;
    uint32_t intr_timer_ctrl;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t gen_ctrl;
    uint32_t mii_ctrl;
    uint32_t tx_station_addr;
    uint32_t eeprom_ctrl;
    uint32_t gpio_ctrl;
    uint32_t tx_desc_ctrl;
    uint32_t tx_ring_ptr;
    uint32_t hi_pri_tx_ring_ptr;
    uint32_t tx_ring_hi_addr;
    uint32_t tx_producer_idx;
    uint32_t tx_consumer_idx;
    uint32_t tx_threshold;
    uint32_t completion_hi_addr;
    uint32_t tx_completion_addr;
    uint32_t rx_completion_addr;
    uint32_t rx_completion_q2_addr;
    uint32_t completion_q_consumer_idx;
    uint32_t rx_dma_ctrl;
    uint32_t rx_desc_q_ctrl;
    uint32_t rx_desc_q_hi_addr;
    uint32_t rx_desc_q_addr;
    uint32_t rx_desc_q_idx;
    uint32_t rx_dma_status;
    uint32_t rx_filter_mode;
    uint32_t tx_mode;
    uint32_t vlan_type;

    /* DMA Context */
    dma_addr_t rx_ring_dma;
    dma_addr_t tx_ring_dma;
    dma_addr_t rx_done_q_dma;
    dma_addr_t tx_done_q_dma;

    int speed100;
    
    
    uint8_t phys[2];
};

#define netdrv_addr_t uint64_t
#define __le32 uint32_t
#define __le16 uint16_t
#define __le64 uint64_t

struct starfire_rx_desc {
    netdrv_addr_t rxaddr;
};

struct csum_rx_done_desc {
    __le32 status;
    __le16 csum;
    __le16 status2;
};

struct full_rx_done_desc {
    __le32 status;
    __le16 status3;
    __le16 status2;
    __le16 vlanid;
    __le16 csum;
    __le32 timestamp;
};

struct starfire_tx_desc_1 {
    __le32 status;
    __le32 addr;
};

struct starfire_tx_desc_2 {
    __le32 status;
    __le32 reserved;
    __le64 addr;
};

struct tx_done_desc {
    __le32 status;
};

typedef struct starfire_tx_desc_2 starfire_tx_desc;
typedef struct full_rx_done_desc rx_done_desc;

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    bool level = (s->intr_status & s->intr_enable) != 0;
    pci_set_irq(pdev, level);
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    (void)pdev;

    if (is_write) {
        s->tx_consumer_idx = s->tx_producer_idx;
        s->intr_status |= IntrTxDMADone | IntrTxDone;
        pcibase_update_irq(s);
    } else {
        s->intr_status |= IntrRxDone;
        pcibase_update_irq(s);
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= EEPROMCtrl && addr < EEPROMCtrl + 0x20) {
        int offset = addr - EEPROMCtrl;
        uint8_t mac[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
        if (offset >= 15 && offset <= 20) {
            return mac[20 - offset];
        }
        return 0;
    }
    if (addr >= MIICtrl && addr < MIICtrl + 0x1000) {
        int location = ((addr - MIICtrl) >> 2) & 0x1F;
        if (location == 0x01) return 0x782D; /* BMSR: link up, autoneg complete */
        if (location == 0x02) return 0x0200; /* PHYSID1 */
        if (location == 0x03) return 0x0200; /* PHYSID2 */
        if (location == 0x04) return 0x01E1; /* ADVERTISE */
        return 0;
    }

    switch (addr) {
        case PCIDeviceConfig: val = 0; break; /* Always ready */
        case GenCtrl: val = s->gen_ctrl; break;
        case IntrTimerCtrl: val = s->intr_timer_ctrl; break;
        case IntrClear:
            val = s->intr_status;
            s->intr_status = 0;
            pcibase_update_irq(s);
            break;
        case IntrStatus: val = s->intr_status; break;
        case IntrEnable: val = s->intr_enable; break;
        case TxStationAddr: val = s->tx_station_addr; break;
        case GPIOCtrl: val = s->gpio_ctrl; break;
        case TxDescCtrl: val = s->tx_desc_ctrl; break;
        case TxRingPtr: val = s->tx_ring_ptr; break;
        case HiPriTxRingPtr: val = s->hi_pri_tx_ring_ptr; break;
        case TxRingHiAddr: val = s->tx_ring_hi_addr; break;
        case TxProducerIdx: val = s->tx_producer_idx; break;
        case TxConsumerIdx: val = s->tx_consumer_idx; break;
        case TxThreshold: val = s->tx_threshold; break;
        case CompletionHiAddr: val = s->completion_hi_addr; break;
        case TxCompletionAddr: val = s->tx_completion_addr; break;
        case RxCompletionAddr: val = s->rx_completion_addr; break;
        case RxCompletionQ2Addr: val = s->rx_completion_q2_addr; break;
        case CompletionQConsumerIdx: val = s->completion_q_consumer_idx; break;
        case RxDMACtrl: val = s->rx_dma_ctrl; break;
        case RxDescQCtrl: val = s->rx_desc_q_ctrl; break;
        case RxDescQHiAddr: val = s->rx_desc_q_hi_addr; break;
        case RxDescQAddr: val = s->rx_desc_q_addr; break;
        case RxDescQIdx: val = s->rx_desc_q_idx; break;
        case RxDMAStatus: val = s->rx_dma_status; break;
        case RxFilterMode: val = s->rx_filter_mode; break;
        case TxMode: val = s->tx_mode; break;
        case VlanType: val = s->vlan_type; break;
        default:
            val = 0;
            break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= MIICtrl && addr < MIICtrl + 0x1000) {
        return;
    }

    switch (addr) {
        case PCIDeviceConfig: break;
        case GenCtrl: s->gen_ctrl = val; break;
        case IntrTimerCtrl: s->intr_timer_ctrl = val; break;
        case IntrEnable:
            s->intr_enable = val;
            pcibase_update_irq(s);
            break;
        case TxStationAddr: s->tx_station_addr = val; break;
        case GPIOCtrl: s->gpio_ctrl = val; break;
        case TxDescCtrl: s->tx_desc_ctrl = val; break;
        case TxRingPtr: s->tx_ring_ptr = val; break;
        case HiPriTxRingPtr: s->hi_pri_tx_ring_ptr = val; break;
        case TxRingHiAddr: s->tx_ring_hi_addr = val; break;
        case TxProducerIdx:
            s->tx_producer_idx = val;
            pcibase_do_dma(s, true);
            break;
        case TxConsumerIdx: s->tx_consumer_idx = val; break;
        case TxThreshold: s->tx_threshold = val; break;
        case CompletionHiAddr: s->completion_hi_addr = val; break;
        case TxCompletionAddr: s->tx_completion_addr = val; break;
        case RxCompletionAddr: s->rx_completion_addr = val; break;
        case RxCompletionQ2Addr: s->rx_completion_q2_addr = val; break;
        case CompletionQConsumerIdx: s->completion_q_consumer_idx = val; break;
        case RxDMACtrl: s->rx_dma_ctrl = val; break;
        case RxDescQCtrl: s->rx_desc_q_ctrl = val; break;
        case RxDescQHiAddr: s->rx_desc_q_hi_addr = val; break;
        case RxDescQAddr: s->rx_desc_q_addr = val; break;
        case RxDescQIdx:
            s->rx_desc_q_idx = val;
            pcibase_do_dma(s, false);
            break;
        case RxFilterMode: s->rx_filter_mode = val; break;
        case TxMode: s->tx_mode = val; break;
        case VlanType: s->vlan_type = val; break;
        default:
            break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;
    uint64_t val = 0;

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;
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

    s->intr_status = 0;
    s->intr_enable = 0;
    s->gen_ctrl = 0;
    s->intr_timer_ctrl = 0;
    s->tx_station_addr = 0;
    s->gpio_ctrl = 0;
    s->tx_desc_ctrl = 0;
    s->tx_ring_ptr = 0;
    s->hi_pri_tx_ring_ptr = 0;
    s->tx_ring_hi_addr = 0;
    s->tx_producer_idx = 0;
    s->tx_consumer_idx = 0;
    s->tx_threshold = 0;
    s->completion_hi_addr = 0;
    s->tx_completion_addr = 0;
    s->rx_completion_addr = 0;
    s->rx_completion_q2_addr = 0;
    s->completion_q_consumer_idx = 0;
    s->rx_dma_ctrl = 0;
    s->rx_desc_q_ctrl = 0;
    s->rx_desc_q_hi_addr = 0;
    s->rx_desc_q_addr = 0;
    s->rx_desc_q_idx = 0;
    s->rx_dma_status = 0;
    s->rx_filter_mode = 0;
    s->tx_mode = 0;
    s->vlan_type = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x9004 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x6915 );
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
    s->bar_info[0].size = 0x80000;
    s->bar_info[0].name = "starfire-mmio";
      
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = false;
    s->has_msix = false;
    
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

/* Minimal VMState to satisfy QEMU migration subsystems */
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
