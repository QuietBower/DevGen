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

#define TYPE_PCIBASE_DEVICE "natsemi_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_NS 0x100b
#define VENDOR_ID PCI_VENDOR_ID_NS
#define DEVICE_ID 0x0020
#define CLASS_ID PCI_CLASS_NETWORK_ETHERNET
#define NATSEMI_BAR0_SIZE 512

/* EEPROM definitions */
#define EEPROM_SIZE 64
#define EE_Write0 0x0
#define EE_Write1 0x0100
#define EE_ChipSelect 0x0200
#define EE_ShiftClk 0x0400
#define EE_DataOut 0x0800

/* Register offsets and bitfield enums from natsemi.c */
enum register_offsets {
    ChipCmd          = 0x00,
    ChipConfig       = 0x04,
    EECtrl           = 0x08,
    PCIBusCfg        = 0x0C,
    IntrStatus       = 0x10,
    IntrMask         = 0x14,
    IntrEnable       = 0x18,
    IntrHoldoff      = 0x1C,
    TxRingPtr        = 0x20,
    TxConfig         = 0x24,
    RxRingPtr        = 0x30,
    RxConfig         = 0x34,
    ClkRun           = 0x3C,
    WOLCmd           = 0x40,
    PauseCmd         = 0x44,
    RxFilterAddr     = 0x48,
    RxFilterData     = 0x4C,
    BootRomAddr      = 0x50,
    BootRomData      = 0x54,
    SiliconRev       = 0x58,
    StatsCtrl        = 0x5C,
    StatsData        = 0x60,
    RxPktErrs        = 0x60,
    RxMissed         = 0x68,
    RxCRCErrs        = 0x64,
    BasicControl     = 0x80,
    BasicStatus      = 0x84,
    AnegAdv          = 0x90,
    AnegPeer         = 0x94,
    PhyStatus        = 0xC0,
    MIntrCtrl        = 0xC4,
    MIntrStatus      = 0xC8,
    PGSEL            = 0xCC,
    PMDCSR           = 0xE4,
    TSTDAT           = 0xFC,
    DSPCFG           = 0xF4,
    SDCFG            = 0xF8
};

enum desc_status_bits {
    DescOwn         = 0x80000000,
    DescMore        = 0x40000000,
    DescIntr        = 0x20000000,
    DescNoCRC       = 0x10000000,
    DescPktOK       = 0x08000000,
    DescSizeMask    = 0xfff,
    DescTxAbort     = 0x04000000,
    DescTxFIFO      = 0x02000000,
    DescTxCarrier   = 0x01000000,
    DescTxDefer     = 0x00800000,
    DescTxExcDefer  = 0x00400000,
    DescTxOOWCol    = 0x00200000,
    DescTxExcColl   = 0x00100000,
    DescTxCollCount = 0x000f0000,
    DescRxAbort     = 0x04000000,
    DescRxOver      = 0x02000000,
    DescRxDest      = 0x01800000,
    DescRxLong      = 0x00400000,
    DescRxRunt      = 0x00200000,
    DescRxInvalid   = 0x00100000,
    DescRxCRC       = 0x00080000,
    DescRxAlign     = 0x00040000,
    DescRxLoop      = 0x00020000,
    DesRxColl       = 0x00010000
};

enum ChipCmd_bits {
    ChipReset       = 0x100,
    RxReset         = 0x20,
    TxReset         = 0x10,
    RxOff           = 0x08,
    RxOn            = 0x04,
    TxOff           = 0x02,
    TxOn            = 0x01
};

enum IntrStatus_bits {
    IntrRxDone      = 0x0001,
    IntrRxIntr      = 0x0002,
    IntrRxErr       = 0x0004,
    IntrRxEarly     = 0x0008,
    IntrRxIdle      = 0x0010,
    IntrRxOverrun   = 0x0020,
    IntrTxDone      = 0x0040,
    IntrTxIntr      = 0x0080,
    IntrTxErr       = 0x0100,
    IntrTxIdle      = 0x0200,
    IntrTxUnderrun  = 0x0400,
    StatsMax        = 0x0800,
    SWInt           = 0x1000,
    WOLPkt          = 0x2000,
    LinkChange      = 0x4000,
    IntrHighBits    = 0x8000,
    RxStatusFIFOOver = 0x10000,
    IntrPCIErr      = 0xf00000,
    RxResetDone     = 0x1000000,
    TxResetDone     = 0x2000000,
    IntrAbnormalSummary = 0xCD20
};

#define CFG_RESET_SAVE  0xfde000
#define WCSR_RESET_SAVE 0x61f
#define RFCR_RESET_SAVE 0xf8500000

struct dp8381x_desc {
    uint32_t next_desc;
    uint32_t cmd_status;
    uint32_t addr;
    uint32_t software_use;
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

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;

    uint32_t intr_status;
    uint32_t intr_mask;
    uint32_t intr_enable;

    uint32_t regs[76];  /* shadow registers, count based on NATSEMI_NREGS */

    dma_addr_t tx_ring_ptr;
    dma_addr_t rx_ring_ptr;

    uint32_t saved_clkrun;

    /* Added fields for functional emulation */
    uint16_t phy_regs[32];
    uint16_t eeprom_data[EEPROM_SIZE];
    struct {
        uint32_t cmd_bits;
        int cmd_count;
        int phase; /* 0=IDLE, 1=CMD, 2=CMD_DONE, 3=DATA */
        int data_bit_idx;
        uint16_t data_word;
        uint32_t last_eec;
    } eeprom;
};

G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t status = s->regs[IntrStatus / 4];
    uint32_t mask = s->regs[IntrMask / 4];
    uint32_t enable = s->regs[IntrEnable / 4];
    if (enable && (status & mask)) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /* DMA not implemented; not used during probe */
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    
    /* Special handling for PHY registers (0x80-0xFF) 16-bit access */
    if (addr >= 0x80 && addr < 0x100) {
        int reg = (addr - 0x80) >> 2;
        if (reg < 32) {
            val = s->phy_regs[reg];
            return val;
        }
    }
    
    /* Generic read/write with byte enables */
    hwaddr base = addr & ~3;
    if (base >= sizeof(s->regs) * 4) {
        return ~0ULL;
    }
    uint32_t word = s->regs[base / 4];
    
    switch (base) {
    case ChipCmd:
        /* ChipReset bit is cleared automatically after reset, keep it cleared */
        break;
    case IntrStatus:
        /* Read clears */
        word = s->regs[IntrStatus / 4];
        /* The driver expects auto-clear, so we clear after read */
        /* But we must return the current status before clearing */
        break;
    case EECtrl:
        /* Return stored value (includes data out bit) */
        word = s->regs[EECtrl / 4];
        break;
    default:
        break;
    }
    
    /* Apply byte shift */
    int shift = (addr & 3) * 8;
    if (size == 4) {
        val = word;
    } else if (size == 2 && (addr & 1) == 0) {
        val = (word >> shift) & 0xFFFF;
    } else if (size == 1) {
        val = (word >> shift) & 0xFF;
    } else {
        /* Unsupported access size, fallback to byte */
        val = (word >> shift) & 0xFF;
    }
    
    /* Special action after read for IntrStatus: clear */
    if (base == IntrStatus) {
        s->regs[IntrStatus / 4] = 0;
        pcibase_update_irq(s);
    }
    
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    /* Special handling for PHY registers */
    if (addr >= 0x80 && addr < 0x100) {
        int reg = (addr - 0x80) >> 2;
        if (reg < 32) {
            if (size == 2 || size == 4) {
                s->phy_regs[reg] = (uint16_t)(val & 0xFFFF);
            } else if (size == 1) {
                int shift = (addr & 3) * 8;
                uint16_t old = s->phy_regs[reg];
                uint16_t mask = 0xFF << shift;
                s->phy_regs[reg] = (old & ~mask) | ((val & 0xFF) << shift);
            }
            return;
        }
    }
    
    /* Generic write */
    hwaddr base = addr & ~3;
    if (base >= sizeof(s->regs) * 4) {
        return;
    }
    uint32_t old_word = s->regs[base / 4];
    uint32_t new_word = old_word;
    
    if (base == EECtrl) {
        /* EEPROM access: special handling */
        uint32_t old_eec = s->eeprom.last_eec;
        uint32_t new_eec = (uint32_t)val;
        int data_in = (new_eec & 0x100) ? 1 : 0;
        int chip_sel = new_eec & 0x200;
        int shift_clk = new_eec & 0x400;
        int rising_clk = shift_clk && !(old_eec & 0x400);
        
        switch (s->eeprom.phase) {
        case 0: /* IDLE */
            if (rising_clk && !chip_sel) {
                s->eeprom.phase = 1;
                s->eeprom.cmd_count = 1;
                s->eeprom.cmd_bits = data_in;
            }
            break;
        case 1: /* CMD */
            if (rising_clk && !chip_sel) {
                s->eeprom.cmd_bits <<= 1;
                s->eeprom.cmd_bits |= data_in;
                s->eeprom.cmd_count++;
                if (s->eeprom.cmd_count == 11) {
                    uint32_t cmd = s->eeprom.cmd_bits & 0x7FF;
                    int address = cmd & 0x3F;
                    if (address < EEPROM_SIZE) {
                        s->eeprom.data_word = s->eeprom_data[address];
                    } else {
                        s->eeprom.data_word = 0xFFFF;
                    }
                    s->eeprom.phase = 2;
                }
            }
            break;
        case 2: /* CMD_DONE, waiting for chip select */
            if (chip_sel && !(old_eec & 0x200)) {
                s->eeprom.phase = 3;
                s->eeprom.data_bit_idx = 0;
            }
            break;
        case 3: /* DATA */
            if (chip_sel && rising_clk) {
                int bit = (s->eeprom.data_word >> s->eeprom.data_bit_idx) & 1;
                new_eec = (new_eec & ~0x800) | (bit ? 0x800 : 0);
                s->eeprom.data_bit_idx++;
            }
            /* After data phase, if chip select goes low, go back to IDLE */
            if (!chip_sel && (old_eec & 0x200)) {
                s->eeprom.phase = 0;
            }
            break;
        }
        s->eeprom.last_eec = new_eec;
        s->regs[base / 4] = new_eec;
        return;
    }
    
    /* Regular write with byte enables */
    int shift = (addr & 3) * 8;
    if (size == 4) {
        new_word = (uint32_t)val;
    } else if (size == 2 && (addr & 1) == 0) {
        uint32_t mask = 0xFFFF << shift;
        new_word = (old_word & ~mask) | (((uint32_t)val & 0xFFFF) << shift);
    } else if (size == 1) {
        uint32_t mask = 0xFF << shift;
        new_word = (old_word & ~mask) | (((uint32_t)val & 0xFF) << shift);
    } else {
        return;
    }
    
    /* Special behavior for some registers */
    switch (base) {
    case ChipCmd:
        if (new_word & ChipReset) {
            new_word &= ~ChipReset; /* Clear reset bit immediately */
        }
        break;
    case IntrStatus:
        /* Writing to IntrStatus has no effect (reads clear) */
        return;
    default:
        break;
    }
    
    s->regs[base / 4] = new_word;
    
    /* After writing, update IRQ if necessary */
    if (base == IntrMask || base == IntrEnable) {
        pcibase_update_irq(s);
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

    /* Reset all registers to power-on defaults */
    memset(s->regs, 0, sizeof(s->regs));
    s->regs[SiliconRev / 4] = 0x00010200; /* DP83815D */
    /* EEPROM preload MAC address 02:00:00:00:00:01 */
    memset(s->eeprom_data, 0, sizeof(s->eeprom_data));
    s->eeprom_data[6] = 0x0000;
    s->eeprom_data[7] = 0x0001;
    s->eeprom_data[8] = 0x0000;
    s->eeprom_data[9] = 0x0080;
    s->eeprom.last_eec = 0;
    s->eeprom.phase = 0;
    s->eeprom.cmd_count = 0;
    /* PHY registers defaults */
    s->phy_regs[0]  = 0x1000; /* BMCR: AutoNeg, 100Mbps, full duplex */
    s->phy_regs[1]  = 0x786D; /* BMSR: Link up, Autoneg complete, capabilities */
    s->phy_regs[2]  = 0x2000; /* PHYID1 */
    s->phy_regs[3]  = 0x5C80; /* PHYID2 */
    s->phy_regs[4]  = 0x01E1; /* ADVERTISE: All */
    s->phy_regs[5]  = 0x41E1; /* LPA: Partner all */
    /* Others zero */
    /* ChipConfig: internal phy */
    s->regs[ChipConfig / 4] = 0x00000000;
    /* ClkRun: no PME */
    s->regs[ClkRun / 4] = 0x00000000;
    /* WOLCmd: no wake */
    s->regs[WOLCmd / 4] = 0x00000000;
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_NS );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x0020 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_ETHERNET );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    s->num_bars = 1;
    s->bar_info[0] = (BARInfo){ .index = 1, .type = BAR_TYPE_MMIO, .size = NATSEMI_BAR0_SIZE, .name = "natsemi-mmio" };
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
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
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

type_init(pcibase_register_types);
