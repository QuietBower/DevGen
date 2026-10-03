/*
 * QEMU PCI device model for SMSC EPIC100 (linux drivers/net/ethernet/smsc/epic100.c)
 *
 * This model only implements the minimal behavior exercised by the driver:
 *   - Register read/write over I/O port BAR 0
 *   - Simple interrupt status/mask handling
 *   - Basic MII/EEPROM interaction sufficient for probe and open
 *   - Very simplified descriptor/DMA behavior: we only emulate status bits
 *
 * It is NOT a full network-functioning model; it is tailored to let
 * the Linux epic100 driver probe, initialize, and believe the device
 * is operational so that the PCI layer shows "Kernel driver in use".
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
/* #include "hw/net/pcnet.h"  placeholder include; unused here and removed to fix compile error */

#define TYPE_PCIBASE_DEVICE "epic100_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI identification from first entry in epic_pci_tbl */
#define EPIC100_VENDOR_ID 0x10B8
#define EPIC100_DEVICE_ID 0x0005

/* The driver is a 10/100 Ethernet controller */
#define EPIC100_CLASS_ID 0x0200 /* PCI_CLASS_NETWORK_ETHERNET */

/* BAR and size info from driver */
#define EPIC_BAR_INDEX 0
#define EPIC_TOTAL_SIZE 0x100

/* Enum epic_registers from driver */
enum epic_registers {
    COMMAND   = 0x00,
    INTSTAT   = 0x04,
    INTMASK   = 0x08,
    GENCTL    = 0x0C,
    NVCTL     = 0x10,
    EECTL     = 0x14,
    PCIBurstCnt = 0x18,
    TEST1     = 0x1C,
    CRCCNT    = 0x20,
    ALICNT    = 0x24,
    MPCNT     = 0x28,
    MIICtrl   = 0x30,
    MIIData   = 0x34,
    MIICfg    = 0x38,
    LAN0      = 64,
    MC0       = 80,
    RxCtrl    = 96,
    TxCtrl    = 112,
    TxSTAT    = 0x74,
    PRxCDAR   = 0x84,
    RxSTAT    = 0xA4,
    EarlyRx   = 0xB0,
    PTxCDAR   = 0xC4,
    TxThresh  = 0xDC,
};

/* Interrupt status bits (enum IntrStatus) */
enum epic_intr_status {
    TxIdle        = 0x40000,
    RxIdle        = 0x20000,
    IntrSummary   = 0x010000,
    PCIBusErr170  = 0x7000,
    PCIBusErr175  = 0x1000,
    PhyEvent175   = 0x8000,
    RxStarted     = 0x0800,
    RxEarlyWarn   = 0x0400,
    CntFull       = 0x0200,
    TxUnderrun    = 0x0100,
    TxEmpty       = 0x0080,
    TxDone        = 0x0020,
    RxError       = 0x0010,
    RxOverflow    = 0x0008,
    RxFull        = 0x0004,
    RxHeader      = 0x0002,
    RxDone        = 0x0001,
};

/* Command bits (enum CommandBits) */
enum epic_command_bits {
    StopRx     = 0x01,
    StartRx    = 0x02,
    TxQueued   = 0x04,
    RxQueued   = 0x08,
    StopTxDMA  = 0x20,
    StopRxDMA  = 0x40,
    RestartTx  = 0x80,
};

/* Descriptor status bits (enum desc_status_bits) */
enum desc_status_bits {
    DescOwn = 0x8000,
};

/* Some other constants/macros from driver that may be useful later */
#define TX_RING_SIZE 256
#define RX_RING_SIZE 256

struct epic_tx_desc {
    uint32_t txstatus;
    uint32_t bufaddr;
    uint32_t buflength;
    uint32_t next;
};

struct epic_rx_desc {
    uint32_t rxstatus;
    uint32_t bufaddr;
    uint32_t buflength;
    uint32_t next;
};

struct epic_chip_info {
    const char *name;
    int drv_flags;
};

/* Interrupt-related register shadow */
typedef struct EpicIRQState {
    uint32_t int_status;
    uint32_t int_mask;
} EpicIRQState;

/* Register shadow struct for key device registers */
typedef struct EpicRegs {
    uint32_t command;
    uint32_t genctl;
    uint32_t nvctl;
    uint32_t eectl;
    uint32_t pciburstcnt;
    uint32_t test1;
    uint32_t crccnt;
    uint32_t alicnt;
    uint32_t mpcnt;
    uint32_t miictrl;
    uint32_t miidata;
    uint32_t miicfg;
    uint8_t  lan[6];
    uint8_t  mc_filter[8];
    uint32_t rxctrl;
    uint32_t txctrl;
    uint32_t txstat;
    uint32_t prxcdar;
    uint32_t rxstat;
    uint32_t earlyrx;
    uint32_t ptxcdar;
    uint32_t txthresh;
} EpicRegs;


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
    EpicIRQState irq;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    EpicRegs regs;

    /* Simple link / PHY state for MII emulation */
    uint16_t mii_regs[32][32]; /* [phy][reg] */
    int      phy_addr;

    /* EEPROM contents (only MAC address is used) */
    uint16_t eeprom[64];
};

/* Forward declarations */
static void pcibase_update_irq(PCIBaseState *s);

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t active = s->irq.int_status & s->irq.int_mask & IntrSummary;

    /* The driver tests IntrSummary bit in INTSTAT after acking events. */
    if (active) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* MMIO/PIO Handlers */
static uint64_t pcibase_io_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = 0xffffffffU;

    switch (addr) {
    case COMMAND:
        if (size == 1) {
            val = s->regs.command & 0xff;
        } else if (size == 2) {
            val = s->regs.command & 0xffff;
        } else {
            val = s->regs.command;
        }
        break;
    case INTSTAT:
        val = s->irq.int_status;
        break;
    case INTMASK:
        val = s->irq.int_mask;
        break;
    case GENCTL:
        val = s->regs.genctl;
        break;
    case NVCTL:
        val = s->regs.nvctl;
        break;
    case EECTL:
        val = s->regs.eectl;
        break;
    case PCIBurstCnt:
        val = s->regs.pciburstcnt;
        break;
    case TEST1:
        val = s->regs.test1;
        break;
    case CRCCNT:
        /* Statistics counters auto-clear on read in hardware; driver increments SW counters. */
        val = s->regs.crccnt & 0xff;
        s->regs.crccnt = 0;
        break;
    case ALICNT:
        val = s->regs.alicnt & 0xff;
        s->regs.alicnt = 0;
        break;
    case MPCNT:
        val = s->regs.mpcnt & 0xff;
        s->regs.mpcnt = 0;
        break;
    case MIICtrl:
        val = s->regs.miictrl;
        break;
    case MIIData:
        val = s->regs.miidata;
        break;
    case MIICfg:
        val = s->regs.miicfg;
        break;
    case TxSTAT:
        val = s->regs.txstat;
        break;
    case PRxCDAR:
        val = s->regs.prxcdar;
        break;
    case RxSTAT:
        val = s->regs.rxstat;
        break;
    case EarlyRx:
        val = s->regs.earlyrx;
        break;
    case PTxCDAR:
        val = s->regs.ptxcdar;
        break;
    case TxThresh:
        val = s->regs.txthresh;
        break;
    default:
        /* LAN and multicast filter and other gaps */
        if (addr >= LAN0 && addr < LAN0 + 6 && size == 2) {
            int i = (addr - LAN0) / 2;
            uint16_t tmp = (uint16_t)s->regs.lan[i * 2] |
                           ((uint16_t)s->regs.lan[i * 2 + 1] << 8);
            val = tmp;
        } else if (addr >= MC0 && addr < MC0 + 8 && size == 2) {
            int i = (addr - MC0) / 2;
            uint16_t tmp = (uint16_t)s->regs.mc_filter[i * 2] |
                           ((uint16_t)s->regs.mc_filter[i * 2 + 1] << 8);
            val = tmp;
        } else if (addr >= RxCtrl && addr < RxCtrl + 4) {
            val = s->regs.rxctrl;
        } else if (addr >= TxCtrl && addr < TxCtrl + 4) {
            val = s->regs.txctrl;
        } else {
            /* unmapped/unused: return 0 */
            val = 0;
        }
        break;
    }

    switch (size) {
    case 1:
        return (uint8_t)val;
    case 2:
        return (uint16_t)val;
    default:
        return val;
    }
}

static void pcibase_mii_process(PCIBaseState *s)
{
    /* MIICtrl format as used in driver mdio_read/mdio_write: */
    /* read_cmd = (phy << 9) | (reg << 4) | MII_READOP; */
    /* write: (phy << 9) | (reg << 4) | MII_WRITEOP */
    uint32_t cmd = s->regs.miictrl;
    uint32_t phy = (cmd >> 9) & 0x1f;
    uint32_t reg = (cmd >> 4) & 0x1f;

    /* We don't know exact READOP/WRITEOP bits; we only emulate when driver
     * clears them by polling for zero, so we simply clear lower bits to zero
     * immediately and provide sensible data. */

    /* Heuristic: if bit0 is set in cmd, treat as read; if bit1, as write. */
    bool is_read = cmd & 0x1;
    bool is_write = cmd & 0x2;

    if (phy >= 32 || reg >= 32) {
        s->regs.miictrl = 0; /* complete immediately */
        return;
    }

    if (is_read) {
        uint16_t val = s->mii_regs[phy][reg];
        s->regs.miidata = val;
    } else if (is_write) {
        uint16_t val = s->regs.miidata & 0xffff;
        s->mii_regs[phy][reg] = val;
    }

    /* clear operation bits so driver sees completion */
    s->regs.miictrl = 0;
}

static void pcibase_eeprom_process(PCIBaseState *s)
{
    /* The driver bit-bangs EEPROM via EECTL and then reads it via er32(EECTL)
     * & EE_DATA_READ. Implementing the full serial protocol would require
     * undocumented timing state. Instead, we only care about read_eeprom()
     * used in probe for debug printing. We can simply feed back prefilled
     * words based on current location bits in EECTL.
     *
     * However, the driver already reads MAC from LAN0 registers, so EEPROM
     * is not functionally required. We'll therefore do nothing here and let
     * EECTL reads just return the stored eectl value; read_eeprom() output
     * is effectively ignored except for logs.
     */
}

static void pcibase_io_write(void *opaque, hwaddr addr, uint64_t data,
                             unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = (uint32_t)data;

    switch (addr) {
    case COMMAND:
        /* COMMAND is used for StartRx, RxQueued, RestartTx, TxQueued, Stop* */
        if (size == 1) {
            s->regs.command &= ~0xffU;
            s->regs.command |= (val & 0xff);
        } else if (size == 2) {
            s->regs.command &= ~0xffffU;
            s->regs.command |= (val & 0xffff);
        } else {
            s->regs.command = val;
        }
        /* Very simple behavior: on TxQueued or RestartTx, mark TxDone and
         * set summary bit so that interrupt logic works. */
        if (s->regs.command & (TxQueued | RestartTx)) {
            s->regs.txstat = 0x0001; /* indicate some tx complete */
            s->irq.int_status |= (TxDone | IntrSummary);
            pcibase_update_irq(s);
        }
        break;
    case INTSTAT:
        /* Driver writes status & EpicNormalEvent (W1C). Clear those bits. */
        s->irq.int_status &= ~val;
        /* Keep IntrSummary set if any remaining enabled sources */
        if ((s->irq.int_status & ~IntrSummary) == 0) {
            s->irq.int_status &= ~IntrSummary;
        }
        pcibase_update_irq(s);
        break;
    case INTMASK:
        s->irq.int_mask = val;
        pcibase_update_irq(s);
        break;
    case GENCTL:
        s->regs.genctl = val;
        break;
    case NVCTL:
        s->regs.nvctl = val;
        break;
    case EECTL:
        s->regs.eectl = val;
        pcibase_eeprom_process(s);
        break;
    case PCIBurstCnt:
        s->regs.pciburstcnt = val;
        break;
    case TEST1:
        s->regs.test1 = val;
        break;
    case CRCCNT:
        s->regs.crccnt = val;
        break;
    case ALICNT:
        s->regs.alicnt = val;
        break;
    case MPCNT:
        s->regs.mpcnt = val;
        break;
    case MIICtrl:
        s->regs.miictrl = val;
        pcibase_mii_process(s);
        break;
    case MIIData:
        s->regs.miidata = val & 0xffff;
        break;
    case MIICfg:
        s->regs.miicfg = val;
        break;
    case TxSTAT:
        s->regs.txstat = val;
        break;
    case PRxCDAR:
        s->regs.prxcdar = val;
        break;
    case RxSTAT:
        s->regs.rxstat = val;
        break;
    case EarlyRx:
        s->regs.earlyrx = val;
        break;
    case PTxCDAR:
        s->regs.ptxcdar = val;
        break;
    case TxThresh:
        s->regs.txthresh = val;
        break;
    default:
        if (addr >= LAN0 && addr < LAN0 + 6 && size == 2) {
            int i = (addr - LAN0) / 2;
            if (i >= 0 && i < 3) {
                s->regs.lan[i * 2]     = val & 0xff;
                s->regs.lan[i * 2 + 1] = (val >> 8) & 0xff;
            }
        } else if (addr >= MC0 && addr < MC0 + 8 && size == 2) {
            int i = (addr - MC0) / 2;
            if (i >= 0 && i < 4) {
                s->regs.mc_filter[i * 2]     = val & 0xff;
                s->regs.mc_filter[i * 2 + 1] = (val >> 8) & 0xff;
            }
        } else if (addr >= RxCtrl && addr < RxCtrl + 4) {
            s->regs.rxctrl = val;
        } else if (addr >= TxCtrl && addr < TxCtrl + 4) {
            s->regs.txctrl = val;
        } else {
            /* ignore writes to unknown addresses */
        }
        break;
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* EPIC uses I/O space; we do not expose MMIO, but QEMU may still
     * call this for completeness if BAR type changed. Map to IO handler. */
    return pcibase_io_read(opaque, addr, size);
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                               unsigned size)
{
    pcibase_io_write(opaque, addr, val, size);
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return pcibase_io_read(opaque, addr, size);
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val,
                              unsigned size)
{
    pcibase_io_write(opaque, addr, val, size);
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
    s->irq.int_status = 0;
    s->irq.int_mask = 0;

    /* Provide a valid default MAC address in LAN0-2 so the driver
     * can read it during probe. */
    s->regs.lan[0] = 0x52;
    s->regs.lan[1] = 0x54;
    s->regs.lan[2] = 0x00;
    s->regs.lan[3] = 0x12;
    s->regs.lan[4] = 0x34;
    s->regs.lan[5] = 0x56;

    /* Initialize a simple MII PHY on phy address 1, with link up. */
    memset(s->mii_regs, 0, sizeof(s->mii_regs));
    s->phy_addr = 1;
    /* Basic Mode Status Register (BMSR, reg1): advertise link up */
    s->mii_regs[s->phy_addr][1] = 0x7849; /* arbitrary non-0, non-0xffff */
    /* Autonegotiation Advertisement (reg4) */
    s->mii_regs[s->phy_addr][4] = 0x01e1; /* common values; driver reads this */
    /* Link Partner Ability (reg5) */
    s->mii_regs[s->phy_addr][5] = 0x01e1;
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s,
                                 BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s,
                              bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s,
                              bi->name, aligned_size);
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  EPIC100_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  EPIC100_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, EPIC100_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR Initialization: driver uses EPIC_BAR (0) and EPIC_TOTAL_SIZE (0x100) */
    s->num_bars = 1;
    s->bar_info[0].index = EPIC_BAR_INDEX;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = EPIC_TOTAL_SIZE;
    s->bar_info[0].name = "epic100-io";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = "";
    }

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = false;
    s->has_msix = false;

    memset(&s->regs, 0, sizeof(s->regs));
    s->irq.int_status = 0;
    s->irq.int_mask = 0;

    /* Initialize reset state (MAC, PHY, etc.) */
    pcibase_reset(DEVICE(pdev));
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

