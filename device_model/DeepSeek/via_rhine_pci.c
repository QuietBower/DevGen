/*
 * QEMU model of VIA Rhine Ethernet controller for QEMU 8.2.10.
 * Generated from driver source via-rhine.c behavior.
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
#include "qemu/bswap.h"

#define TYPE_PCIBASE_DEVICE "via_rhine_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x1106
#define DEVICE_ID 0x3043
#define CLASS_ID  0x020000

/* Register offsets from driver enum register_offsets */
#define StationAddr   0x00
#define RxConfig      0x06
#define TxConfig      0x07
#define ChipCmd       0x08
#define ChipCmd1      0x09
#define TQWake        0x0A
#define IntrStatus    0x0C
#define IntrEnable    0x0E
#define MulticastFilter0 0x10
#define MulticastFilter1 0x14
#define RxRingPtr     0x18
#define TxRingPtr     0x1C
#define GFIFOTest     0x54
#define MIIPhyAddr    0x6C
#define MIIStatus     0x6D
#define PCIBusConfig  0x6E
#define PCIBusConfig1 0x6F
#define MIICmd        0x70
#define MIIRegAddr    0x71
#define MIIData       0x72
#define MACRegEEcsr   0x74
#define ConfigA       0x78
#define ConfigB       0x79
#define ConfigC       0x7A
#define ConfigD       0x7B
#define RxMissed      0x7C
#define RxCRCErrs     0x7E
#define MiscCmd       0x81
#define StickyHW      0x83
#define IntrStatus2   0x84
#define CamMask       0x88
#define CamCon        0x92
#define CamAddr       0x93
#define WOLcrSet      0xA0
#define PwcfgSet      0xA1
#define WOLcgSet      0xA3
#define WOLcrClr      0xA4
#define WOLcrClr1     0xA6
#define WOLcgClr      0xA7
#define PwrcsrSet     0xA8
#define PwrcsrSet1    0xA9
#define PwrcsrClr     0xAC
#define PwrcsrClr1    0xAD

/* Enum definitions from driver source */
enum intr_status_bits {
	IntrRxDone	= 0x0001,
	IntrTxDone	= 0x0002,
	IntrRxErr	= 0x0004,
	IntrTxError	= 0x0008,
	IntrRxEmpty	= 0x0020,
	IntrPCIErr	= 0x0040,
	IntrStatsMax	= 0x0080,
	IntrRxEarly	= 0x0100,
	IntrTxUnderrun	= 0x0210,
	IntrRxOverflow	= 0x0400,
	IntrRxDropped	= 0x0800,
	IntrRxNoBuf	= 0x1000,
	IntrTxAborted	= 0x2000,
	IntrLinkChange	= 0x4000,
	IntrRxWakeUp	= 0x8000,
	IntrTxDescRace	= 0x080000,
	IntrNormalSummary = IntrRxDone | IntrTxDone,
	IntrTxErrSummary  = IntrTxDescRace | IntrTxAborted | IntrTxError | IntrTxUnderrun,
};

enum desc_status_bits {
	DescOwn=0x80000000
};

enum chip_cmd_bits {
	CmdInit=0x01, CmdStart=0x02, CmdStop=0x04, CmdRxOn=0x08,
	CmdTxOn=0x10, Cmd1TxDemand=0x20, CmdRxDemand=0x40,
	Cmd1EarlyRx=0x01, Cmd1EarlyTx=0x02, Cmd1FDuplex=0x04,
	Cmd1NoTxPoll=0x08, Cmd1Reset=0x80,
};

/* RHINE_EVENT from driver: all normal Rx/Tx interrupts + error summary bits */
#define RHINE_EVENT (IntrNormalSummary | IntrRxErr | IntrTxError | IntrTxUnderrun | \
                     IntrRxEmpty | IntrRxOverflow | IntrRxDropped | IntrRxNoBuf | \
                     IntrTxAborted | IntrTxDescRace | IntrRxEarly | IntrRxWakeUp)

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
    uint8_t regs[256];  /* Covers all register offsets */

    /* DMA Context */
    int _dma_placeholder; /* DMA state handled via registers */

    uint32_t status;      /* Operational status flags */

    uint8_t pwr_state;    /* Power management state */
    uint8_t wolopts;

    /* Internal MII PHY registers (0-31) */
    uint16_t phy_regs[32];
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint16_t status = lduw_le_p(s->regs + IntrStatus);
    uint16_t enable = lduw_le_p(s->regs + IntrEnable);
    if (status & enable) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (size) {
    case 1:
        val = s->regs[addr];
        break;
    case 2:
        val = lduw_le_p(s->regs + addr);
        break;
    case 4:
        val = ldl_le_p(s->regs + addr);
        break;
    case 8:
        val = ldq_le_p(s->regs + addr);
        break;
    default:
        val = 0;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* Handle Write-1-to-clear on IntrStatus and IntrStatus2 BEFORE storing */
    if (addr == IntrStatus && size == 2) {
        uint16_t current = lduw_le_p(s->regs + IntrStatus);
        uint16_t clear = val;
        stw_le_p(s->regs + IntrStatus, current & ~clear);
    } else if (addr == IntrStatus2 && size == 1) {
        uint8_t current = s->regs[IntrStatus2];
        uint8_t clear = val;
        s->regs[IntrStatus2] = current & ~clear;
    } else {
        /* Normal store */
        switch (size) {
        case 1:
            s->regs[addr] = val;
            break;
        case 2:
            stw_le_p(s->regs + addr, val);
            break;
        case 4:
            stl_le_p(s->regs + addr, val);
            break;
        case 8:
            stq_le_p(s->regs + addr, val);
            break;
        default:
            return;
        }

        /* Post-write side effects for specific registers */
        if (addr == ChipCmd1 && (val & Cmd1Reset)) {
            s->regs[ChipCmd1] &= ~Cmd1Reset;
        } else if (addr == MIICmd) {
            uint8_t cmd = s->regs[MIICmd];
            if (cmd & 0x40) {
                /* MDIO read command */
                uint8_t phy_id = s->regs[MIIPhyAddr];
                uint8_t reg_idx = s->regs[MIIRegAddr];
                if (reg_idx < 32) {
                    uint16_t data = s->phy_regs[reg_idx];
                    stw_le_p(s->regs + MIIData, data);
                }
                s->regs[MIICmd] &= ~0x40;
            } else if (cmd & 0x20) {
                /* MDIO write command */
                uint8_t reg_idx = s->regs[MIIRegAddr];
                if (reg_idx < 32) {
                    s->phy_regs[reg_idx] = lduw_le_p(s->regs + MIIData);
                }
                s->regs[MIICmd] &= ~0x20;
            }
        } else if (addr == MACRegEEcsr && (val & 0x20)) {
            s->regs[MACRegEEcsr] &= ~0x20;
        }
    }
    pcibase_update_irq(s);
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* PIO and MMIO register space are identical */
    switch (size) {
    case 1:
        val = s->regs[addr];
        break;
    case 2:
        val = lduw_le_p(s->regs + addr);
        break;
    case 4:
        val = ldl_le_p(s->regs + addr);
        break;
    case 8:
        val = ldq_le_p(s->regs + addr);
        break;
    default:
        val = 0;
    }
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* Same logic as MMIO write */
    if (addr == IntrStatus && size == 2) {
        uint16_t current = lduw_le_p(s->regs + IntrStatus);
        uint16_t clear = val;
        stw_le_p(s->regs + IntrStatus, current & ~clear);
    } else if (addr == IntrStatus2 && size == 1) {
        uint8_t current = s->regs[IntrStatus2];
        uint8_t clear = val;
        s->regs[IntrStatus2] = current & ~clear;
    } else {
        switch (size) {
        case 1:
            s->regs[addr] = val;
            break;
        case 2:
            stw_le_p(s->regs + addr, val);
            break;
        case 4:
            stl_le_p(s->regs + addr, val);
            break;
        case 8:
            stq_le_p(s->regs + addr, val);
            break;
        default:
            return;
        }
        if (addr == ChipCmd1 && (val & Cmd1Reset)) {
            s->regs[ChipCmd1] &= ~Cmd1Reset;
        } else if (addr == MIICmd) {
            uint8_t cmd = s->regs[MIICmd];
            if (cmd & 0x40) {
                uint8_t phy_id = s->regs[MIIPhyAddr];
                uint8_t reg_idx = s->regs[MIIRegAddr];
                if (reg_idx < 32) {
                    uint16_t data = s->phy_regs[reg_idx];
                    stw_le_p(s->regs + MIIData, data);
                }
                s->regs[MIICmd] &= ~0x40;
            } else if (cmd & 0x20) {
                uint8_t reg_idx = s->regs[MIIRegAddr];
                if (reg_idx < 32) {
                    s->phy_regs[reg_idx] = lduw_le_p(s->regs + MIIData);
                }
                s->regs[MIICmd] &= ~0x20;
            }
        } else if (addr == MACRegEEcsr && (val & 0x20)) {
            s->regs[MACRegEEcsr] &= ~0x20;
        }
    }
    pcibase_update_irq(s);
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

    /* Zero all registers */
    memset(s->regs, 0, sizeof(s->regs));

    /* Set default MAC address (QEMU default) */
    s->regs[StationAddr] = 0x52;
    s->regs[StationAddr+1] = 0x54;
    s->regs[StationAddr+2] = 0x00;
    s->regs[StationAddr+3] = 0x12;
    s->regs[StationAddr+4] = 0x34;
    s->regs[StationAddr+5] = 0x56;

    /* Initialize MII PHY registers to a valid link-up state */
    s->phy_regs[0] = 0x3100;  /* BMCR: autoneg, 100Mbps, full-duplex */
    s->phy_regs[1] = 0x7809;  /* BMSR: link up, 100Base-TX full duplex, autoneg complete */
    s->phy_regs[4] = 0x01e1;  /* Advertisement: 100Base-TX full/half, 10BaseT full/half, 802.3 */
    s->phy_regs[5] = 0xc1e1;  /* Link partner ability: mirror advertisement */

    /* Ensure interrupts are off and status clear */
    stw_le_p(s->regs + IntrStatus, 0);
    stw_le_p(s->regs + IntrEnable, 0);
    s->regs[IntrStatus2] = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x00);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 2;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_PIO, .size = 256, .name = "via-rhine-pio" };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_MMIO, .size = 256, .name = "via-rhine-mmio" };
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
    .name = "via_rhine_pci",
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
