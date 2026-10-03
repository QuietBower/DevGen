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

#define TYPE_PCIBASE_DEVICE "sis190_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_SI         0x1039
#define PCI_DEVICE_ID_SIS190     0x0190
#define SIS190_REGS_SIZE         0x80

enum sis190_registers {
	TxControl		= 0x00,
	TxDescStartAddr		= 0x04,
	rsv0			= 0x08,
	TxSts			= 0x0c,
	RxControl		= 0x10,
	RxDescStartAddr		= 0x14,
	rsv1			= 0x18,
	RxSts			= 0x1c,
	IntrStatus		= 0x20,
	IntrMask		= 0x24,
	IntrControl		= 0x28,
	IntrTimer		= 0x2c,
	PMControl		= 0x30,
	rsv2			= 0x34,
	ROMControl		= 0x38,
	ROMInterface		= 0x3c,
	StationControl		= 0x40,
	GMIIControl		= 0x44,
	GIoCR			= 0x48,
	GIoCtrl			= 0x4c,
	TxMacControl		= 0x50,
	TxLimit			= 0x54,
	RGDelay			= 0x58,
	rsv3			= 0x5c,
	RxMacControl		= 0x60,
	RxMacAddr		= 0x62,
	RxHashTable		= 0x68,
	RxWolCtrl		= 0x70,
	RxWolData		= 0x74,
	RxMPSControl		= 0x78,
	rsv4			= 0x7c,
};

enum sis190_register_content {
	/* IntrStatus */
	SoftInt			= 0x40000000,
	Timeup			= 0x20000000,
	PauseFrame		= 0x00080000,
	MagicPacket		= 0x00040000,
	WakeupFrame		= 0x00020000,
	LinkChange		= 0x00010000,
	RxQEmpty		= 0x00000080,
	RxQInt			= 0x00000040,
	TxQ1Empty		= 0x00000020,
	TxQ1Int			= 0x00000010,
	TxQ0Empty		= 0x00000008,
	TxQ0Int			= 0x00000004,
	RxHalt			= 0x00000002,
	TxHalt			= 0x00000001,

	/* {Rx/Tx}CmdBits */
	CmdReset		= 0x10,
	CmdRxEnb		= 0x08,
	CmdTxEnb		= 0x01,
	RxBufEmpty		= 0x01,

	/* Cfg9346Bits */
	Cfg9346_Lock		= 0x00,
	Cfg9346_Unlock		= 0xc0,

	/* RxMacControl */
	AcceptErr		= 0x20,
	AcceptRunt		= 0x10,
	AcceptBroadcast		= 0x0800,
	AcceptMulticast		= 0x0400,
	AcceptMyPhys		= 0x0200,
	AcceptAllPhys		= 0x0100,

	/* RxConfigBits */
	RxCfgFIFOShift		= 13,
	RxCfgDMAShift		= 8,

	/* TxConfigBits */
	TxInterFrameGapShift	= 24,
	TxDMAShift		= 8,

	LinkStatus		= 0x02,
	FullDup			= 0x01,

	/* TBICSRBit */
	TBILinkOK		= 0x02000000,
};

enum _DescStatusBit {
	OWNbit		= 0x80000000,
	INTbit		= 0x40000000,
	CRCbit		= 0x00020000,
	PADbit		= 0x00010000,
	RingEnd		= 0x80000000,
	LSEN		= 0x08000000,
	IPCS		= 0x04000000,
	TCPCS		= 0x02000000,
	UDPCS		= 0x01000000,
	BSTEN		= 0x00800000,
	EXTEN		= 0x00400000,
	DEFEN		= 0x00200000,
	BKFEN		= 0x00100000,
	CRSEN		= 0x00080000,
	COLEN		= 0x00040000,
	THOL3		= 0x30000000,
	THOL2		= 0x20000000,
	THOL1		= 0x10000000,
	THOL0		= 0x00000000,
	WND		= 0x00080000,
	TABRT		= 0x00040000,
	FIFO		= 0x00020000,
	LINK		= 0x00010000,
	ColCountMask	= 0x0000ffff,
	IPON		= 0x20000000,
	TCPON		= 0x10000000,
	UDPON		= 0x08000000,
	Wakup		= 0x00400000,
	Magic		= 0x00200000,
	Pause		= 0x00100000,
	DEFbit		= 0x00200000,
	BCAST		= 0x000c0000,
	MCAST		= 0x00080000,
	UCAST		= 0x00040000,
	TAGON		= 0x80000000,
	RxDescCountMask	= 0x7f000000,
	ABORT		= 0x00800000,
	SHORT		= 0x00400000,
	LIMIT		= 0x00200000,
	MIIER		= 0x00100000,
	OVRUN		= 0x00080000,
	NIBON		= 0x00040000,
	COLON		= 0x00020000,
	CRCOK		= 0x00010000,
	RxSizeMask	= 0x0000ffff,
};

enum sis190_eeprom_access_register_bits {
	EECS	= 0x00000001,
	EECLK	= 0x00000002,
	EEDO	= 0x00000008,
	EEDI	= 0x00000004,
	EEREQ	= 0x00000080,
	EEROP	= 0x00000200,
	EEWOP	= 0x00000100,
};

/* MDIO access bits (inferred from common SiS PHY management) */
#define EhnMIIreq         0x00000001
#define EhnMIIread        0x00000002
#define EhnMIIwrite       0x00000004
#define EhnMIIregShift    4
#define EhnMIIpmdShift    11
#define EhnMIIdataShift   16
#define EhnMIInotDone     0x80000000

/* Standard MII registers */
#define MII_BMCR          0x00
#define MII_BMSR          0x01
#define MII_PHYSID1       0x02
#define MII_PHYSID2       0x03

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
    uint8_t regs[SIS190_REGS_SIZE];

    /* EEPROM contents (128 words, little-endian) */
    uint16_t eeprom[128];

    /* PHY registers (32 PHYs, 32 registers each) */
    uint16_t phy_regs[32][32];

    /* MDIO transaction state machine */
    QEMUTimer mdio_timer;
    bool mdio_active;
    uint8_t mdio_phy_id;
    uint8_t mdio_reg;
    bool mdio_is_write;
    uint16_t mdio_data;
};

static void pcibase_do_mdio_complete(PCIBaseState *s)
{
    uint32_t reg_val;

    if (!s->mdio_active) {
        return;
    }

    /* Process MDIO transaction */
    if (s->mdio_is_write) {
        s->phy_regs[s->mdio_phy_id][s->mdio_reg] = s->mdio_data;
    } else {
        s->mdio_data = s->phy_regs[s->mdio_phy_id][s->mdio_reg];
    }

    /* Update GMIIControl register:
       - Clear EhnMIInotDone
       - For reads, set data in upper 16 bits
       - Preserve other bits that might have been set by the driver */
    reg_val = ldl_le_p(s->regs + GMIIControl);
    reg_val &= ~EhnMIInotDone;
    if (!s->mdio_is_write) {
        reg_val &= ~(0xFFFF << EhnMIIdataShift); /* clear old data field */
        reg_val |= (s->mdio_data << EhnMIIdataShift);
    }
    stl_le_p(s->regs + GMIIControl, reg_val);

    s->mdio_active = false;
}

static void pcibase_mdio_timer_cb(void *opaque)
{
    pcibase_do_mdio_complete(opaque);
}

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* Not needed for probe; interrupt logic is not implemented in this phase. */
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* Not needed for probe; DMA engine is not implemented in this phase. */
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= SIS190_REGS_SIZE) {
        return (uint64_t)-1;
    }

    switch (size) {
    case 1:
        val = s->regs[addr];
        break;
    case 2:
        if (addr + 1 < SIS190_REGS_SIZE) {
            val = lduw_le_p(s->regs + addr);
        }
        break;
    case 4:
        if (addr + 3 < SIS190_REGS_SIZE) {
            val = ldl_le_p(s->regs + addr);
        }
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: unsupported read size %d at 0x%" HWADDR_PRIx "\n",
                      __func__, size, addr);
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= SIS190_REGS_SIZE) {
        return;
    }

    /* Handle special registers first */
    if (addr == ROMInterface && size == 4) {
        uint32_t v = (uint32_t)val;
        if (v & EEREQ) {
            int word_addr = (v >> 10) & 0x7F;
            uint16_t data = s->eeprom[word_addr];
            /* Clear request bits and set data in upper half */
            v = (data << 16) | (v & ~(EEREQ | EEROP | EEWOP | (0xFFFF0000)));
            v &= ~EEREQ;  /* ensure EEREQ is cleared */
            stl_le_p(s->regs + ROMInterface, v);
            return;
        }
    } else if (addr == GMIIControl && size == 4) {
        uint32_t ctl = (uint32_t)val;
        if (ctl & EhnMIIreq) {
            /* Start a new MDIO transaction only if not already busy */
            if (!s->mdio_active) {
                s->mdio_phy_id = (ctl >> EhnMIIpmdShift) & 0x1F;
                s->mdio_reg = (ctl >> EhnMIIregShift) & 0x1F;
                s->mdio_is_write = (ctl & EhnMIIwrite) ? true : false;
                if (s->mdio_is_write) {
                    s->mdio_data = (ctl >> EhnMIIdataShift) & 0xFFFF;
                }

                /* Store the command bits into GMIIControl (with EhnMIInotDone set) */
                uint32_t cmd = ctl;
                cmd &= ~(EhnMIIreq | EhnMIIread | EhnMIIwrite); /* clear request type bits in stored value */
                cmd |= EhnMIInotDone; /* set not-done */
                cmd &= ~(0xFFFF << EhnMIIdataShift); /* clear data field for now */
                stl_le_p(s->regs + GMIIControl, cmd);

                s->mdio_active = true;
                /* Schedule completion after a short delay (1 microsecond) */
                timer_mod(&s->mdio_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 1000);
            }
            return;
        }
        /* If no request bit, fall through to default store (allows driver to write other bits) */
    }

    /* Default: store value to register */
    switch (size) {
    case 1:
        s->regs[addr] = (uint8_t)val;
        break;
    case 2:
        if (addr + 1 < SIS190_REGS_SIZE) {
            stw_le_p(s->regs + addr, (uint16_t)val);
        }
        break;
    case 4:
        if (addr + 3 < SIS190_REGS_SIZE) {
            stl_le_p(s->regs + addr, (uint32_t)val);
        }
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: unsupported write size %d at 0x%" HWADDR_PRIx "\n",
                      __func__, size, addr);
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* PIO not used */
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* PIO not used */
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

    /* Cancel any pending MDIO timer and reset state */
    timer_del(&s->mdio_timer);
    s->mdio_active = false;

    /* Reset registers to power-on defaults */
    memset(s->regs, 0, sizeof(s->regs));
    /* ROMControl: indicate EEPROM present (bit 1) */
    s->regs[ROMControl] |= 0x02;

    /* Initialize EEPROM content */
    memset(s->eeprom, 0, sizeof(s->eeprom));
    s->eeprom[0] = 0x1234;  /* signature */
    s->eeprom[1] = 0x0011;  /* MAC byte 0-1 */
    s->eeprom[2] = 0x2233;  /* MAC byte 2-3 */
    s->eeprom[3] = 0x4455;  /* MAC byte 4-5 */
    s->eeprom[4] = 0x0080;  /* RGMII enable */

    /* Initialize PHY registers */
    memset(s->phy_regs, 0, sizeof(s->phy_regs));
    /* PHY 0 registers */
    s->phy_regs[0][MII_BMSR] = 0x0024;   /* Link up, auto-negotiation complete */
    s->phy_regs[0][MII_PHYSID1] = 0x0141; /* arbitrary PHY ID */
    s->phy_regs[0][MII_PHYSID2] = 0x0C00;
    s->phy_regs[0][MII_BMCR] = 0x1000;   /* Auto-negotiation enabled */
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, 0x1039);
    pci_set_word(pci_conf + PCI_DEVICE_ID, 0x0190);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_ETHERNET);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* Initialize MDIO timer */
    timer_init_ns(&s->mdio_timer, QEMU_CLOCK_VIRTUAL, pcibase_mdio_timer_cb, s);
    s->mdio_active = false;

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = SIS190_REGS_SIZE, .name = "sis190-mmio" };
    
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

    timer_del(&s->mdio_timer);
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "sis190_pci",
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
