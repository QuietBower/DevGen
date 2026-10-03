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


#define TYPE_PCIBASE_DEVICE "epic100_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define EPIC_VENDOR_ID 0x10B8
#define EPIC_DEVICE_ID 0x0005
#define EPIC_CLASS_ID  0x0200

enum epic_registers {
    COMMAND=0, INTSTAT=4, INTMASK=8, GENCTL=0x0C, NVCTL=0x10, EECTL=0x14,
    PCIBurstCnt=0x18,
    TEST1=0x1C, CRCCNT=0x20, ALICNT=0x24, MPCNT=0x28,
    MIICtrl=0x30, MIIData=0x34, MIICfg=0x38,
    LAN0=64,
    MC0=80,
    RxCtrl=96, TxCtrl=112, TxSTAT=0x74,
    PRxCDAR=0x84, RxSTAT=0xA4, EarlyRx=0xB0, PTxCDAR=0xC4, TxThresh=0xDC,
};

enum IntrStatus {
    TxIdle=0x40000, RxIdle=0x20000, IntrSummary=0x010000,
    PCIBusErr170=0x7000, PCIBusErr175=0x1000, PhyEvent175=0x8000,
    RxStarted=0x0800, RxEarlyWarn=0x0400, CntFull=0x0200, TxUnderrun=0x0100,
    TxEmpty=0x0080, TxDone=0x0020, RxError=0x0010,
    RxOverflow=0x0008, RxFull=0x0004, RxHeader=0x0002, RxDone=0x0001,
};

enum CommandBits {
    StopRx=1, StartRx=2, TxQueued=4, RxQueued=8,
    StopTxDMA=0x20, StopRxDMA=0x40, RestartTx=0x80,
};

struct epic_rx_desc {
    uint32_t rxstatus;
    uint32_t bufaddr;
    uint32_t buflength;
    uint32_t next;
};

struct epic_tx_desc {
    uint32_t txstatus;
    uint32_t bufaddr;
    uint32_t buflength;
    uint32_t next;
};

#define EpicNapiEvent   (TxEmpty | TxDone | \
                         RxDone | RxStarted | RxEarlyWarn | RxOverflow | RxFull)
#define EpicNormalEvent (0x0000ffff & ~EpicNapiEvent)
#define EpicRemoved     0xffffffff

#define EE_ENB          (0x0001 | EE_CS)
#define EE_CS           0x02
#define EE_SHIFT_CLK    0x04
#define EE_DATA_READ    0x10
#define EE_WRITE_1      0x09
#define EE_WRITE_0      0x01
#define EE_READ64_CMD   (6 << 6)
#define EE_READ256_CMD  (6 << 8)

#define MII_READOP      1
#define MII_WRITEOP     2

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
    uint32_t intstat;
    uint32_t intmask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t command;
    uint32_t genctl;
    uint32_t nvctl;
    uint32_t eectl;
    uint32_t pciburstcnt;
    uint32_t test1;
    uint32_t miictrl;
    uint32_t miidata;
    uint32_t miicfg;
    uint8_t lan0[6];
    uint32_t mc0[4];
    uint32_t rxctrl;
    uint32_t txctrl;
    uint32_t txstat;
    uint32_t rxstat;
    uint32_t earlyrx;
    uint32_t txthresh;

    /* DMA Context */
    uint32_t prxcdar;
    uint32_t ptxcdar;

    uint32_t crccnt;
    uint32_t alicnt;
    uint32_t mpcnt;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool level = (s->intstat & s->intmask) != 0;
    pci_set_irq(pdev, level);
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (is_write) {
        struct epic_tx_desc desc;
        pci_dma_read(pdev, s->ptxcdar, &desc, sizeof(desc));
        /* Further DMA processing deferred until ownership bits are known */
    } else {
        struct epic_rx_desc desc;
        pci_dma_read(pdev, s->prxcdar, &desc, sizeof(desc));
        /* Further DMA processing deferred until ownership bits are known */
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case COMMAND: val = s->command; break;
    case INTSTAT: val = s->intstat; break;
    case INTMASK: val = s->intmask; break;
    case GENCTL: val = s->genctl; break;
    case NVCTL: val = s->nvctl; break;
    case EECTL: val = s->eectl; break;
    case PCIBurstCnt: val = s->pciburstcnt; break;
    case TEST1: val = s->test1; break;
    case CRCCNT: val = s->crccnt; break;
    case ALICNT: val = s->alicnt; break;
    case MPCNT: val = s->mpcnt; break;
    case MIICtrl: val = s->miictrl; break;
    case MIIData: val = s->miidata; break;
    case MIICfg: val = s->miicfg; break;
    case MC0: case MC0+4: case MC0+8: case MC0+12:
        val = s->mc0[(addr - MC0) / 4]; break;
    case RxCtrl: val = s->rxctrl; break;
    case TxCtrl: val = s->txctrl; break;
    case TxSTAT: val = s->txstat; break;
    case PRxCDAR: val = s->prxcdar; break;
    case RxSTAT: val = s->rxstat; break;
    case EarlyRx: val = s->earlyrx; break;
    case PTxCDAR: val = s->ptxcdar; break;
    case TxThresh: val = s->txthresh; break;
    default: break;
    }
    
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case COMMAND:
        s->command = val;
        if (val & TxQueued) {
            pcibase_do_dma(s, true);
        }
        if (val & RxQueued) {
            pcibase_do_dma(s, false);
        }
        break;
    case INTSTAT:
        s->intstat &= ~val; /* W1C */
        pcibase_update_irq(s);
        break;
    case INTMASK:
        s->intmask = val;
        pcibase_update_irq(s);
        break;
    case GENCTL:
        s->genctl = val;
        if (val == 0x4001) { /* Soft reset */
            s->intstat = 0;
            s->intmask = 0;
            pcibase_update_irq(s);
        }
        break;
    case NVCTL: s->nvctl = val; break;
    case EECTL: s->eectl = val; break;
    case PCIBurstCnt: s->pciburstcnt = val; break;
    case TEST1: s->test1 = val; break;
    case MIICtrl:
        s->miictrl = val & ~(MII_READOP | MII_WRITEOP);
        break;
    case MIIData: s->miidata = val; break;
    case MIICfg: s->miicfg = val; break;
    case MC0: case MC0+4: case MC0+8: case MC0+12:
        s->mc0[(addr - MC0) / 4] = val; break;
    case RxCtrl: s->rxctrl = val; break;
    case TxCtrl: s->txctrl = val; break;
    case PRxCDAR: s->prxcdar = val; break;
    case PTxCDAR: s->ptxcdar = val; break;
    case TxThresh: s->txthresh = val; break;
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

    s->command = 0;
    s->intstat = 0;
    s->intmask = 0;
    s->genctl = 0;
    s->nvctl = 0;
    s->eectl = 0;
    s->pciburstcnt = 0;
    s->test1 = 0;
    s->miictrl = 0;
    s->miidata = 0;
    s->miicfg = 0;
    s->rxctrl = 0;
    s->txctrl = 0;
    s->txstat = 0;
    s->rxstat = 0;
    s->earlyrx = 0;
    s->txthresh = 0;
    s->prxcdar = 0;
    s->ptxcdar = 0;
    s->crccnt = 0;
    s->alicnt = 0;
    s->mpcnt = 0;
    memset(s->mc0, 0, sizeof(s->mc0));
    memset(s->lan0, 0, sizeof(s->lan0));
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x10B8 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x0005 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0200 );
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
    s->bar_info[0].size = 0x100;
    s->bar_info[0].name = "epic-bar0";  
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "epic100_pci",
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
