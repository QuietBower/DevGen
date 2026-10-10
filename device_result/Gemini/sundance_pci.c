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

#define TYPE_PCIBASE_DEVICE "sundance_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define DRV_NAME "sundance"
#define TX_RING_SIZE 32
#define RX_RING_SIZE 64
#define PKT_BUF_SZ 1536

#define DEFAULT_INTR (IntrRxDMADone | IntrPCIErr | \
			IntrDrvRqst | IntrTxDone | StatsMax | \
			LinkChange)

#define EEPROM_SA_OFFSET	0x10

#define MDIO_EnbIn  (0)
#define MII_BMSR		0x01
#define MII_ADVERTISE		0x04
#define MII_LPA			0x05
#define MII_BMCR		0x00
#define BMCR_SPEED100		0x2000
#define BMCR_FULLDPLX		0x0100
#define BMCR_RESET		0x8000
#define BMCR_ANENABLE		0x1000
#define BMCR_ANRESTART		0x0200
#define ADVERTISE_100FULL	0x0100
#define ADVERTISE_100HALF	0x0080
#define ADVERTISE_10FULL	0x0040
#define ADVERTISE_10HALF	0x0020
#define MAX_UNITS 8
#define MII_CNT		4
#define RX_BUDGET	32
#define TX_QUEUE_LEN	(TX_RING_SIZE - 1)
#define ASIC_HI_WORD(x)	((x) + 2)

struct desc_frag { uint32_t addr, length; };

struct netdev_desc {
    uint32_t next_desc;
    uint32_t status;
    struct desc_frag frag;
};

#define TX_TOTAL_SIZE	(TX_RING_SIZE*sizeof(struct netdev_desc))
#define RX_TOTAL_SIZE	(RX_RING_SIZE*sizeof(struct netdev_desc))

enum intr_status_bits {
    IntrSummary=0x0001, IntrPCIErr=0x0002, IntrMACCtrl=0x0008,
    IntrTxDone=0x0004, IntrRxDone=0x0010, IntrRxStart=0x0020,
    IntrDrvRqst=0x0040,
    StatsMax=0x0080, LinkChange=0x0100,
    IntrTxDMADone=0x0200, IntrRxDMADone=0x0400,
};

enum rx_mode_bits {
    AcceptAllIPMulti=0x20, AcceptMultiHash=0x10, AcceptAll=0x08,
    AcceptBroadcast=0x04, AcceptMulticast=0x02, AcceptMyPhys=0x01,
};

enum desc_status_bits {
    DescOwn=0x8000,
    DescEndPacket=0x4000,
    DescEndRing=0x2000,
    LastFrag=0x80000000,
    DescIntrOnTx=0x8000,
    DescIntrOnDMADone=0x80000000,
    DisableAlign = 0x00000001,
};

enum alta_offsets {
    DMACtrl = 0x00,
    TxListPtr = 0x04,
    TxDMABurstThresh = 0x08,
    TxDMAUrgentThresh = 0x09,
    TxDMAPollPeriod = 0x0a,
    RxDMAStatus = 0x0c,
    RxListPtr = 0x10,
    DebugCtrl0 = 0x1a,
    DebugCtrl1 = 0x1c,
    RxDMABurstThresh = 0x14,
    RxDMAUrgentThresh = 0x15,
    RxDMAPollPeriod = 0x16,
    LEDCtrl = 0x1a,
    ASICCtrl = 0x30,
    EEData = 0x34,
    EECtrl = 0x36,
    FlashAddr = 0x40,
    FlashData = 0x44,
    WakeEvent = 0x45,
    TxStatus = 0x46,
    TxFrameId = 0x47,
    DownCounter = 0x18,
    IntrClear = 0x4a,
    IntrEnable = 0x4c,
    IntrStatus = 0x4e,
    MACCtrl0 = 0x50,
    MACCtrl1 = 0x52,
    StationAddr = 0x54,
    MaxFrameSize = 0x5A,
    RxMode = 0x5c,
    MIICtrl = 0x5e,
    MulticastFilter0 = 0x60,
    MulticastFilter1 = 0x64,
    RxOctetsLow = 0x68,
    RxOctetsHigh = 0x6a,
    TxOctetsLow = 0x6c,
    TxOctetsHigh = 0x6e,
    TxFramesOK = 0x70,
    RxFramesOK = 0x72,
    StatsCarrierError = 0x74,
    StatsLateColl = 0x75,
    StatsMultiColl = 0x76,
    StatsOneColl = 0x77,
    StatsTxDefer = 0x78,
    RxMissed = 0x79,
    StatsTxXSDefer = 0x7a,
    StatsTxAbort = 0x7b,
    StatsBcastTx = 0x7c,
    StatsBcastRx = 0x7d,
    StatsMcastTx = 0x7e,
    StatsMcastRx = 0x7f,
    RxStatus = 0x0c,
};

enum ASICCtrl_HiWord_bit {
    GlobalReset = 0x0001,
    RxReset = 0x0002,
    TxReset = 0x0004,
    DMAReset = 0x0008,
    FIFOReset = 0x0010,
    NetworkReset = 0x0020,
    HostReset = 0x0040,
    ResetBusy = 0x0400,
};

enum mac_ctrl0_bits {
    EnbFullDuplex=0x20, EnbRcvLargeFrame=0x40,
    EnbFlowCtrl=0x100, EnbPassRxCRC=0x200,
};

enum mac_ctrl1_bits {
    StatsEnable=0x0020, StatsDisable=0x0040, StatsEnabled=0x0080,
    TxEnable=0x0100, TxDisable=0x0200, TxEnabled=0x0400,
    RxEnable=0x0800, RxDisable=0x1000, RxEnabled=0x2000,
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
    uint16_t intr_status;
    uint16_t intr_enable;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t registers[0x80 / 4];

    /* DMA Context */
    uint32_t tx_list_ptr;
    uint32_t rx_list_ptr;
};

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
    if (is_write) {
        uint32_t desc_addr = s->tx_list_ptr;
        while (desc_addr != 0) {
            struct netdev_desc desc;
            pci_dma_read(pdev, desc_addr, &desc, sizeof(desc));
            
            uint32_t status = le32_to_cpu(desc.status);
            
            desc.status = cpu_to_le32(status | 0x00010000);
            pci_dma_write(pdev, desc_addr + offsetof(struct netdev_desc, status), &desc.status, sizeof(desc.status));
            
            if (status & DescIntrOnTx) {
                s->intr_status |= IntrTxDone;
            }
            
            desc_addr = le32_to_cpu(desc.next_desc);
        }
        s->tx_list_ptr = 0;
        s->registers[TxListPtr/4] = 0;
        pcibase_update_irq(s);
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    
    if (addr < 0x80) {
        memcpy(&val, (uint8_t *)s->registers + addr, size);
    }

    switch (addr) {
        case EECtrl:
            val &= ~0x8000;
            break;
        case MIICtrl:
            val = (uint64_t)qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) & 0xFF;
            break;
        case IntrStatus:
            val = s->intr_status;
            break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < 0x80) {
        memcpy((uint8_t *)s->registers + addr, &val, size);
    }

    switch (addr) {
        case EECtrl:
            if (val & 0x0200) {
                int loc = val & 0xff;
                if (loc >= EEPROM_SA_OFFSET && loc < EEPROM_SA_OFFSET + 3) {
                    s->registers[EEData/4] = 0x1234;
                }
            }
            break;
        case TxListPtr:
            s->tx_list_ptr = val;
            if (val != 0) {
                pcibase_do_dma(s, true);
            }
            break;
        case RxListPtr:
            s->rx_list_ptr = val;
            break;
        case IntrEnable:
            s->intr_enable = val;
            pcibase_update_irq(s);
            break;
        case IntrStatus:
            s->intr_status &= ~val;
            pcibase_update_irq(s);
            break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return pcibase_mmio_read(opaque, addr, size);
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    pcibase_mmio_write(opaque, addr, val, size);
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
    memset(s->registers, 0, sizeof(s->registers));
    s->intr_status = 0;
    s->intr_enable = 0;
    s->tx_list_ptr = 0;
    s->rx_list_ptr = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x1186 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x1002 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_ETHERNET );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x14);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    s->num_bars = 2;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_PIO, .size = 128, .name = "sundance-pio" };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_MMIO, .size = 128, .name = "sundance-mmio" };

    /* BAR Initialization */
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

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "sundance_pci",
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
