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
#define PCI_VENDOR_ID_SILAN 0x1904
#define SC92031_USE_PIO 0

#define MII_BMSR		0x01
#define BMSR_LSTATUS		0x0004
#define MII_OutputStatus    24

#define MII_JAB             16

#define PHY_16_JAB_ENB      0x1000

#define PHY_16_PORT_ENB     0x1

#define MII_BMCR		0x00
#define BMCR_ANENABLE		0x1000
#define BMCR_ANRESTART		0x0200
#define NUM_TX_DESC	   4

#define RX_BUF_LEN_IDX  3
#define MAX_ETH_FRAME_SIZE	  1536

#define TYPE_PCIBASE_DEVICE "sc92031_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
enum IntrStatusBits {
   LinkFail       = 0x80000000,
   LinkOK         = 0x40000000,
   TimeOut        = 0x20000000,
   RxOverflow     = 0x0040,
   RxOK           = 0x0020,
   TxOK           = 0x0001,
   IntrBits = LinkFail|LinkOK|TimeOut|RxOverflow|RxOK|TxOK,
};

enum TxStatusBits {
   TxCarrierLost = 0x20000000,
   TxAborted     = 0x10000000,
   TxOutOfWindow = 0x08000000,
   TxNccShift    = 22,
   EarlyTxThresShift = 16,
   TxStatOK      = 0x8000,
   TxUnderrun    = 0x4000,
   TxOwn         = 0x2000,
};

enum RxStatusBits {
   RxStatesOK   = 0x80000,
   RxBadAlign   = 0x40000,
   RxHugeFrame  = 0x20000,
   RxSmallFrame = 0x10000,
   RxCRCOK      = 0x8000,
   RxCrlFrame   = 0x4000,
   Rx_Broadcast = 0x2000,
   Rx_Multicast = 0x1000,
   RxAddrMatch  = 0x0800,
   MiiErr       = 0x0400,
};

enum Config1Bits {
   Cfg1_EarlyRx = 1 << 31,
   Cfg1_EarlyTx = 1 << 30,
   Cfg1_Rcv8K   = 0x0,
   Cfg1_Rcv16K  = 0x1,
   Cfg1_Rcv32K  = 0x3,
   Cfg1_Rcv64K  = 0x7,
   Cfg1_Rcv128K = 0xf,
};

enum RxConfigBits {
   RxFullDx    = 0x80000000,
   RxEnb       = 0x40000000,
   RxSmall     = 0x20000000,
   RxHuge      = 0x10000000,
   RxErr       = 0x08000000,
   RxAllphys   = 0x04000000,
   RxMulticast = 0x02000000,
   RxBroadcast = 0x01000000,
   RxLoopBack  = (1 << 23) | (1 << 22),
   LowThresholdShift  = 12,
   HighThresholdShift = 2,
};

enum silan_registers {
   Config0    = 0x00,
   Config1    = 0x04,
   RxBufWPtr  = 0x08,
   IntrStatus = 0x0C,
   IntrMask   = 0x10,
   RxbufAddr  = 0x14,
   RxBufRPtr  = 0x18,
   Txstatusall = 0x1C,
   TxStatus0  = 0x20,
   TxAddr0    = 0x30,
   RxConfig   = 0x40,
   MAC0       = 0x44,
   MAR0       = 0x4C,
   RxStatus0  = 0x54,
   TxConfig   = 0x5C,
   PhyCtrl    = 0x60,
   FlowCtrlConfig = 0x64,
   Miicmd0    = 0x68,
   Miicmd1    = 0x6C,
   Miistatus  = 0x70,
   Timercnt   = 0x74,
   TimerIntr  = 0x78,
   PMConfig   = 0x7C,
   CRC0       = 0x80,
   Wakeup0    = 0x88,
   LSBCRC0    = 0xC8,
   TestD0     = 0xD0,
   TestD4     = 0xD4,
   TestD8     = 0xD8,
};

enum TxConfigBits {
   TxFullDx       = 0x80000000,
   TxEnb          = 0x40000000,
   TxEnbPad       = 0x20000000,
   TxEnbHuge      = 0x10000000,
   TxEnbFCS       = 0x08000000,
   TxNoBackOff    = 0x04000000,
   TxEnbPrem      = 0x02000000,
   TxCareLostCrs  = 0x1000000,
   TxExdCollNum   = 0xf00000,
   TxDataRate     = 0x80000,
};

enum PhyCtrlconfigbits {
   PhyCtrlAne         = 0x80000000,
   PhyCtrlSpd100      = 0x40000000,
   PhyCtrlSpd10       = 0x20000000,
   PhyCtrlPhyBaseAddr = 0x1f000000,
   PhyCtrlDux         = 0x800000,
   PhyCtrlReset       = 0x400000,
};

enum FlowCtrlConfigBits {
   FlowCtrlFullDX = 0x80000000,
   FlowCtrlEnb    = 0x40000000,
};

enum Config0Bits {
   Cfg0_Reset  = 0x80000000,
   Cfg0_Anaoff = 0x40000000,
   Cfg0_LDPS   = 0x20000000,
};

enum MiiCmd0Bits {
   Mii_Divider = 0x20000000,
   Mii_WRITE   = 0x400000,
   Mii_READ    = 0x200000,
   Mii_SCAN    = 0x100000,
   Mii_Tamod   = 0x80000,
   Mii_Drvmod  = 0x40000,
   Mii_mdc     = 0x20000,
   Mii_mdoen   = 0x10000,
   Mii_mdo     = 0x8000,
   Mii_mdi     = 0x4000,
};

enum MiiStatusBits {
    Mii_StatusBusy = 0x80000000,
};

enum PMConfigBits {
   PM_Enable  = 1 << 31,
   PM_LongWF  = 1 << 30,
   PM_Magic   = 1 << 29,
   PM_LANWake = 1 << 28,
   PM_LWPTN   = (1 << 27 | 1<< 26),
   PM_LinkUp  = 1 << 25,
   PM_WakeUp  = 1 << 24,
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
    uint32_t intr_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t config0;
    uint32_t config1;
    uint32_t rx_buf_wptr;
    uint32_t rxbuf_addr;
    uint32_t rx_buf_rptr;
    uint32_t txstatusall;
    uint32_t tx_status0[NUM_TX_DESC];
    uint32_t tx_addr0[NUM_TX_DESC];
    uint32_t rx_config;
    uint32_t mac0[2];
    uint32_t mar0[2];
    uint32_t rx_status0;
    uint32_t tx_config;
    uint32_t phy_ctrl;
    uint32_t flow_ctrl_config;
    uint32_t miicmd0;
    uint32_t miicmd1;
    uint32_t miistatus;
    uint32_t timercnt;
    uint32_t timerintr;
    uint32_t pmconfig;
    uint32_t crc0[2];
    uint32_t wakeup0[8];
    uint32_t lsbcrc0[2];
    uint32_t testd0;
    uint32_t testd4;
    uint32_t testd8;

    /* DMA Context */
    dma_addr_t rx_ring_dma_addr;
    dma_addr_t tx_bufs_dma_addr;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool level = (s->intr_status & s->intr_mask) != 0;
    pci_set_irq(pdev, level);
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (is_write) {
        for (int i = 0; i < NUM_TX_DESC; i++) {
            uint32_t status = s->tx_status0[i];
            if (status != 0 && !(status & TxStatOK)) {
                uint32_t len = status & 0xFFFF;
                if (len > 0 && len <= 2048) {
                    uint8_t buf[2048];
                    pci_dma_read(pdev, s->tx_addr0[i], buf, len);
                    s->tx_status0[i] = status | TxStatOK;
                    s->intr_status |= TxOK;
                }
            }
        }
        pcibase_update_irq(s);
    }
}

static void pcibase_reset(DeviceState *dev);

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case Config0: val = s->config0; break;
    case Config1: val = s->config1; break;
    case RxBufWPtr: val = s->rx_buf_wptr; break;
    case IntrStatus:
        val = s->intr_status;
        s->intr_status = 0;
        pcibase_update_irq(s);
        break;
    case IntrMask: val = s->intr_mask; break;
    case RxbufAddr: val = s->rxbuf_addr; break;
    case RxBufRPtr: val = s->rx_buf_rptr; break;
    case Txstatusall: val = s->txstatusall; break;
    case TxStatus0: case TxStatus0 + 4: case TxStatus0 + 8: case TxStatus0 + 12:
        val = s->tx_status0[(addr - TxStatus0) / 4];
        break;
    case TxAddr0: case TxAddr0 + 4: case TxAddr0 + 8: case TxAddr0 + 12:
        val = s->tx_addr0[(addr - TxAddr0) / 4];
        break;
    case RxConfig: val = s->rx_config; break;
    case MAC0: val = s->mac0[0]; break;
    case MAC0 + 4: val = s->mac0[1]; break;
    case MAR0: val = s->mar0[0]; break;
    case MAR0 + 4: val = s->mar0[1]; break;
    case RxStatus0: val = s->rx_status0; break;
    case TxConfig: val = s->tx_config; break;
    case PhyCtrl: val = s->phy_ctrl; break;
    case FlowCtrlConfig: val = s->flow_ctrl_config; break;
    case Miicmd0: val = s->miicmd0; break;
    case Miicmd1: val = s->miicmd1; break;
    case Miistatus: val = s->miistatus; break;
    case Timercnt: val = s->timercnt; break;
    case TimerIntr: val = s->timerintr; break;
    case PMConfig: val = s->pmconfig; break;
    case CRC0: val = s->crc0[0]; break;
    case CRC0 + 4: val = s->crc0[1]; break;
    case Wakeup0: case Wakeup0 + 4: case Wakeup0 + 8: case Wakeup0 + 12:
    case Wakeup0 + 16: case Wakeup0 + 20: case Wakeup0 + 24: case Wakeup0 + 28:
        val = s->wakeup0[(addr - Wakeup0) / 4];
        break;
    case LSBCRC0: val = s->lsbcrc0[0]; break;
    case LSBCRC0 + 4: val = s->lsbcrc0[1]; break;
    case TestD0: val = s->testd0; break;
    case TestD4: val = s->testd4; break;
    case TestD8: val = s->testd8; break;
    default: val = 0; break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case Config0:
        s->config0 = val;
        if (val & Cfg0_Reset) {
            pcibase_reset(DEVICE(s));
        }
        break;
    case Config1: s->config1 = val; break;
    case RxBufWPtr: s->rx_buf_wptr = val; break;
    case IntrStatus: s->intr_status &= ~val; pcibase_update_irq(s); break;
    case IntrMask:
        s->intr_mask = val;
        pcibase_update_irq(s);
        break;
    case RxbufAddr: s->rxbuf_addr = val; break;
    case RxBufRPtr: s->rx_buf_rptr = val; break;
    case Txstatusall: s->txstatusall = val; break;
    case TxStatus0: case TxStatus0 + 4: case TxStatus0 + 8: case TxStatus0 + 12:
        s->tx_status0[(addr - TxStatus0) / 4] = val;
        if (val != 0) {
            pcibase_do_dma(s, true);
        }
        break;
    case TxAddr0: case TxAddr0 + 4: case TxAddr0 + 8: case TxAddr0 + 12:
        s->tx_addr0[(addr - TxAddr0) / 4] = val;
        break;
    case RxConfig: s->rx_config = val; break;
    case MAC0: s->mac0[0] = val; break;
    case MAC0 + 4: s->mac0[1] = val; break;
    case MAR0: s->mar0[0] = val; break;
    case MAR0 + 4: s->mar0[1] = val; break;
    case RxStatus0: s->rx_status0 = val; break;
    case TxConfig: s->tx_config = val; break;
    case PhyCtrl: s->phy_ctrl = val; break;
    case FlowCtrlConfig: s->flow_ctrl_config = val; break;
    case Miicmd0:
        s->miicmd0 = val;
        if (val & Mii_READ) {
            uint32_t reg = (s->miicmd1 >> 6) & 0x1F;
            uint16_t mii_val = 0;
            if (reg == MII_BMSR) {
                mii_val = BMSR_LSTATUS;
            } else if (reg == MII_OutputStatus) {
                mii_val = 0x0006;
            } else if (reg == MII_JAB) {
                mii_val = PHY_16_JAB_ENB | PHY_16_PORT_ENB;
            }
            s->miistatus = (mii_val << 13);
        } else if (val & Mii_WRITE) {
            s->miistatus = 0;
        } else if (val & Mii_SCAN) {
            s->miistatus = 0;
        }
        s->miistatus &= ~Mii_StatusBusy;
        break;
    case Miicmd1: s->miicmd1 = val; break;
    case Miistatus: s->miistatus = val; break;
    case Timercnt: s->timercnt = val; break;
    case TimerIntr: s->timerintr = val; break;
    case PMConfig: s->pmconfig = val; break;
    case CRC0: s->crc0[0] = val; break;
    case CRC0 + 4: s->crc0[1] = val; break;
    case Wakeup0: case Wakeup0 + 4: case Wakeup0 + 8: case Wakeup0 + 12:
    case Wakeup0 + 16: case Wakeup0 + 20: case Wakeup0 + 24: case Wakeup0 + 28:
        s->wakeup0[(addr - Wakeup0) / 4] = val;
        break;
    case LSBCRC0: s->lsbcrc0[0] = val; break;
    case LSBCRC0 + 4: s->lsbcrc0[1] = val; break;
    case TestD0: s->testd0 = val; break;
    case TestD4: s->testd4 = val; break;
    case TestD8: s->testd8 = val; break;
    default: break;
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
    s->intr_mask = 0;
    s->config0 = 0;
    s->config1 = 0;
    s->rx_buf_wptr = 0;
    s->rxbuf_addr = 0;
    s->rx_buf_rptr = 0;
    s->txstatusall = 0;
    memset(s->tx_status0, 0, sizeof(s->tx_status0));
    memset(s->tx_addr0, 0, sizeof(s->tx_addr0));
    s->rx_config = 0;
    s->mac0[0] = 0x12345678;
    s->mac0[1] = 0x00009ABC;
    s->mar0[0] = 0;
    s->mar0[1] = 0;
    s->rx_status0 = 0;
    s->tx_config = 0;
    s->phy_ctrl = 0;
    s->flow_ctrl_config = 0;
    s->miicmd0 = 0;
    s->miicmd1 = 0;
    s->miistatus = 0;
    s->timercnt = 0;
    s->timerintr = 0;
    s->pmconfig = 0;
    memset(s->crc0, 0, sizeof(s->crc0));
    memset(s->wakeup0, 0, sizeof(s->wakeup0));
    memset(s->lsbcrc0, 0, sizeof(s->lsbcrc0));
    s->testd0 = 0;
    s->testd4 = 0;
    s->testd8 = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_SILAN );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x2031 );
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
    s->bar_info[0].size = 0x1000;
    s->bar_info[0].name = "sc92031-mmio";

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
