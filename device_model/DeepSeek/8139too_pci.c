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

#define TYPE_PCIBASE_DEVICE "8139too_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID   0x10ec
#define DEVICE_ID   0x8139
#define CLASS_ID    0x0200

/* Register offsets from enum RTL8139_registers */
enum RTL8139_registers {
    MAC0        = 0,
    MAR0        = 8,
    TxStatus0   = 0x10,
    TxAddr0     = 0x20,
    RxBuf       = 0x30,
    ChipCmd     = 0x37,
    RxBufPtr    = 0x38,
    RxBufAddr   = 0x3A,
    IntrMask    = 0x3C,
    IntrStatus  = 0x3E,
    TxConfig    = 0x40,
    RxConfig    = 0x44,
    Timer       = 0x48,
    RxMissed    = 0x4C,
    Cfg9346     = 0x50,
    Config0     = 0x51,
    Config1     = 0x52,
    TimerInt    = 0x54,
    MediaStatus = 0x58,
    Config3     = 0x59,
    Config4     = 0x5A,
    HltClk      = 0x5B,
    MultiIntr   = 0x5C,
    TxSummary   = 0x60,
    BasicModeCtrl   = 0x62,
    BasicModeStatus = 0x64,
    NWayAdvert  = 0x66,
    NWayLPAR    = 0x68,
    NWayExpansion = 0x6A,
    FIFOTMS     = 0x70,
    CSCR        = 0x74,
    PARA78      = 0x78,
    FlashReg    = 0xD4,
    PARA7c      = 0x7c,
    Config5     = 0xD8,
};

/* Bit definitions and enums from driver */
#define MDIO_DATA_OUT   0x04
#define EE_CS           0x08
#define MDIO_WRITE0     (MDIO_DIR)
#define MDIO_WRITE1     (MDIO_DIR | MDIO_DATA_OUT)
#define EE_SHIFT_CLK    0x04
#define EE_DATA_WRITE   0x02
#define EE_DATA_READ    0x01
#define EE_ENB          (0x80 | EE_CS)
#define MDIO_DIR        0x80
#define MDIO_DATA_IN    0x02
#define MDIO_CLK        0x01

enum rx_mode_bits {
    AcceptErr       = 0x20,
    AcceptRunt      = 0x10,
    AcceptBroadcast = 0x08,
    AcceptMulticast = 0x04,
    AcceptMyPhys    = 0x02,
    AcceptAllPhys   = 0x01,
};

enum ChipCmdBits {
    CmdReset    = 0x10,
    CmdRxEnb    = 0x08,
    CmdTxEnb    = 0x04,
    RxBufEmpty  = 0x01,
};

enum IntrStatusBits {
    PCIErr      = 0x8000,
    PCSTimeout  = 0x4000,
    RxFIFOOver  = 0x40,
    RxUnderrun  = 0x20,
    RxOverflow  = 0x10,
    TxErr       = 0x08,
    TxOK        = 0x04,
    RxErr       = 0x02,
    RxOK        = 0x01,
    RxAckBits   = RxFIFOOver | RxOverflow | RxOK,
};

enum TxStatusBits {
    TxHostOwns      = 0x2000,
    TxUnderrun      = 0x4000,
    TxStatOK        = 0x8000,
    TxOutOfWindow   = 0x20000000,
    TxAborted       = 0x40000000,
    TxCarrierLost   = 0x80000000,
};

enum RxStatusBits {
    RxMulticast = 0x8000,
    RxPhysical  = 0x4000,
    RxBroadcast = 0x2000,
    RxBadSymbol = 0x0020,
    RxRunt      = 0x0010,
    RxTooLong   = 0x0008,
    RxCRCErr    = 0x0004,
    RxBadAlign  = 0x0002,
    RxStatusOK  = 0x0001,
};

enum tx_config_bits {
    TxIFGShift  = 24,
    TxIFG84     = (0 << 24),
    TxIFG88     = (1 << 24),
    TxIFG92     = (2 << 24),
    TxIFG96     = (3 << 24),
    TxLoopBack  = (1 << 18) | (1 << 17),
    TxCRC       = (1 << 16),
    TxClearAbt  = (1 << 0),
    TxDMAShift  = 8,
    TxRetryShift = 4,
    TxVersionMask = 0x7C800000,
};

enum Config1Bits {
    Cfg1_PM_Enable   = 0x01,
    Cfg1_VPD_Enable  = 0x02,
    Cfg1_PIO         = 0x04,
    Cfg1_MMIO        = 0x08,
    LWAKE            = 0x10,
    Cfg1_Driver_Load = 0x20,
    Cfg1_LED0        = 0x40,
    Cfg1_LED1        = 0x80,
    SLEEP            = (1 << 1),
    PWRDN            = (1 << 0),
};

enum Config3Bits {
    Cfg3_FBtBEn     = (1 << 0),
    Cfg3_FuncRegEn  = (1 << 1),
    Cfg3_CLKRUN_En  = (1 << 2),
    Cfg3_CardB_En   = (1 << 3),
    Cfg3_LinkUp     = (1 << 4),
    Cfg3_Magic      = (1 << 5),
    Cfg3_PARM_En    = (1 << 6),
    Cfg3_GNTSel     = (1 << 7),
};

enum Config4Bits {
    LWPTN = (1 << 2),
};

enum Config5Bits {
    Cfg5_PME_STS    = (1 << 0),
    Cfg5_LANWake    = (1 << 1),
    Cfg5_LDPS       = (1 << 2),
    Cfg5_FIFOAddrPtr = (1 << 3),
    Cfg5_UWF        = (1 << 4),
    Cfg5_MWF        = (1 << 5),
    Cfg5_BWF        = (1 << 6),
};

enum RxConfigBits {
    RxCfgFIFOShift   = 13,
    RxCfgFIFONone    = (7 << 13),
    RxCfgDMAShift    = 8,
    RxCfgDMAUnlimited = (7 << 8),
    RxCfgRcv8K       = 0,
    RxCfgRcv16K      = (1 << 11),
    RxCfgRcv32K      = (1 << 12),
    RxCfgRcv64K      = (1 << 11) | (1 << 12),
    RxNoWrap         = (1 << 7),
};

enum CSCRBits {
    CSCR_LinkOKBit      = 0x0400,
    CSCR_LinkChangeBit  = 0x0800,
    CSCR_LinkStatusBits = 0x0f000,
    CSCR_LinkDownOffCmd = 0x003c0,
    CSCR_LinkDownCmd    = 0x0f3c0,
};

enum Cfg9346Bits {
    Cfg9346_Lock    = 0x00,
    Cfg9346_Unlock  = 0xC0,
};

enum chip_flags {
    HasHltClk = (1 << 0),
    HasLWake  = (1 << 1),
};

enum TwisterParamVals {
    PARA78_default = 0x78fa8388,
    PARA7c_default = 0xcb38de43,
    PARA7c_xxx     = 0xcb38de43,
};

typedef enum {
    RTL8139 = 0,
    RTL8129,
} board_t;

typedef enum {
    CH_8139 = 0,
    CH_8139_K,
    CH_8139A,
    CH_8139A_G,
    CH_8139B,
    CH_8130,
    CH_8139C,
    CH_8100,
    CH_8100B_8139D,
    CH_8101,
} chip_t;

#define NUM_TX_DESC 4

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
    uint16_t intr_status;
    uint16_t intr_mask;

    uint8_t regs[256];

    /* DMA context */
    struct {
        uint32_t tx_addr[NUM_TX_DESC];
        uint32_t rx_buf_addr;
        uint32_t cur_rx;
        uint32_t rx_buf_ptr;
        uint32_t dummy; /* for alignment */
    } dma;

    uint32_t status;
    bool reset_active;
    uint8_t pm_state;
    chip_t chipset;

    /* EEPROM state */
    uint16_t eeprom_data[256];
    enum {
        EE_IDLE,
        EE_COMMAND,
        EE_READ
    } ee_state;
    uint32_t ee_shift_reg;
    int ee_bit_count;
    uint8_t ee_addr_len;
    bool ee_after_command;
    uint8_t ee_data_out_bit;
    uint16_t ee_out_data;
    int ee_out_bit_index;
    uint8_t ee_prev_clk;
    uint8_t ee_prev_cs;
    uint8_t ee_prev_data_wr;

    QEMUTimer *reset_timer;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* Not required for probe; stub */
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* Not required for probe; stub */
}

static void eeprom_write(PCIBaseState *s, uint8_t val)
{
    uint8_t clk = (val & EE_SHIFT_CLK) ? 1 : 0;
    uint8_t cs = (val & EE_CS) ? 1 : 0;
    uint8_t data_wr = (val & EE_DATA_WRITE) ? 1 : 0;

    /* Chip select trailing edge detection */
    if (cs && !s->ee_prev_cs) {
        /* CS low -> high: start a new EEPROM access */
        s->ee_state = EE_COMMAND;
        s->ee_shift_reg = 0;
        s->ee_bit_count = 0;
        s->ee_addr_len = 0;
        s->ee_after_command = false;
    } else if (!cs && s->ee_prev_cs) {
        /* CS high -> low: terminate access */
        s->ee_state = EE_IDLE;
        s->ee_after_command = false;
    }

    /* Rising edge of clock during command phase: sample data */
    if (clk && !s->ee_prev_clk && s->ee_state == EE_COMMAND) {
        s->ee_shift_reg = (s->ee_shift_reg << 1) | data_wr;
        s->ee_bit_count++;
    }

    /* Detect end of command: a write with CLK low, DATA low after some bits */
    if (s->ee_state == EE_COMMAND && !s->ee_after_command &&
        clk == 0 && data_wr == 0 && cs == 1 && s->ee_bit_count >= 5) {
        s->ee_after_command = true;
        s->ee_addr_len = s->ee_bit_count - 5;
        if (s->ee_addr_len < 6) s->ee_addr_len = 6;
        if (s->ee_addr_len > 8) s->ee_addr_len = 8;
        uint8_t addr = s->ee_shift_reg & ((1 << s->ee_addr_len) - 1);
        if (addr < 256) {
            s->ee_out_data = s->eeprom_data[addr];
            s->ee_out_bit_index = 0;
        } else {
            s->ee_out_data = 0xffff;
        }
    }

    /* Transition to READ state on first rising clock after command */
    if (s->ee_after_command && clk && !s->ee_prev_clk) {
        s->ee_state = EE_READ;
        s->ee_after_command = false;
        /* Prepare first data bit (LSB first) */
        s->ee_data_out_bit = s->ee_out_data & 1;
        s->ee_out_data >>= 1;
        s->ee_out_bit_index++;
    } else if (s->ee_state == EE_READ && clk && !s->ee_prev_clk) {
        /* Subsequent read clocks: prepare next bit */
        if (s->ee_out_bit_index < 16) {
            s->ee_data_out_bit = s->ee_out_data & 1;
            s->ee_out_data >>= 1;
            s->ee_out_bit_index++;
        } else {
            s->ee_data_out_bit = 0;
        }
    }

    /* Save current state for edge detection */
    s->ee_prev_clk = clk;
    s->ee_prev_cs = cs;
    s->ee_prev_data_wr = data_wr;
}

static void rtl8139_chip_reset_timer_cb(void *opaque)
{
    PCIBaseState *s = opaque;
    s->regs[ChipCmd] &= ~CmdReset;
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Not used; stub */
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* Not used; stub */
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= 256) {
        return val;
    }

    /* Special register handling */
    if (addr == RxMissed && size == 4) {
        val = ldl_le_p(s->regs + addr);
        memset(s->regs + addr, 0, 4);
        return val;
    }

    if (addr == Cfg9346 && size == 1) {
        uint8_t base = s->regs[addr];
        base &= ~EE_DATA_READ;
        if (s->ee_state == EE_READ && s->ee_data_out_bit) {
            base |= EE_DATA_READ;
        }
        return base;
    }

    switch (size) {
    case 1: val = ldub_p(s->regs + addr); break;
    case 2: val = lduw_le_p(s->regs + addr); break;
    case 4: val = ldl_le_p(s->regs + addr); break;
    default: val = 0; break;
    }
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= 256) {
        return;
    }

    switch (addr) {
    case ChipCmd:
        if (size != 1) goto general_store;
        if (val & CmdReset) {
            s->regs[addr] = val;
            timer_mod(s->reset_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 1);
        } else {
            s->regs[addr] = val;
        }
        return;

    case IntrStatus:
        if (size == 2) {
            uint16_t old = lduw_le_p(s->regs + addr);
            old &= ~((uint16_t)val);
            stw_le_p(s->regs + addr, old);
        } else {
            goto general_store;
        }
        return;

    case Cfg9346:
        if (size == 1) {
            eeprom_write(s, (uint8_t)val);
            s->regs[addr] = (uint8_t)val;
        } else {
            goto general_store;
        }
        return;

    case TxStatus0:
    case TxStatus0 + 4:
    case TxStatus0 + 8:
    case TxStatus0 + 12:
        /* TX descriptor write: just store, no DMA for probe */
        goto general_store;

    default:
        break;
    }

general_store:
    switch (size) {
    case 1: stb_p(s->regs + addr, val); break;
    case 2: stw_le_p(s->regs + addr, val); break;
    case 4: stl_le_p(s->regs + addr, val); break;
    default: break;
    }
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

    memset(s->regs, 0, sizeof(s->regs));
    s->regs[TxConfig]     = 0x00;
    s->regs[TxConfig + 1] = 0x10;
    s->regs[TxConfig + 2] = 0x74;
    s->regs[TxConfig + 3] = 0x78; /* 0x78107400? Let's use 0x78100000 */
    /* Actually simpler: just ldl_le_p == 0x78100000 */
    stl_le_p(s->regs + TxConfig, 0x78100000);

    s->ee_state = EE_IDLE;
    s->ee_after_command = false;
    s->ee_data_out_bit = 0;
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x10ec);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x8139);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0200);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    s->num_bars = 1;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_PIO, .size = 256, .name = "8139too-pio" };

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialization */
    s->reset_timer = timer_new_ms(QEMU_CLOCK_VIRTUAL, rtl8139_chip_reset_timer_cb, s);
    memset(s->regs, 0, sizeof(s->regs));
    stl_le_p(s->regs + TxConfig, 0x78100000);

    /* EEPROM contents */
    memset(s->eeprom_data, 0, sizeof(s->eeprom_data));
    s->eeprom_data[0] = 0x8139; /* read_eeprom(location=0, addr_len=8) => 0x8129? but we want addr_len=6, so return !=0x8129 */
    s->eeprom_data[7] = 0x5452; /* MAC byte 0,1 */
    s->eeprom_data[8] = 0x1200; /* MAC byte 2,3 */
    s->eeprom_data[9] = 0x5634; /* MAC byte 4,5 */
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
    timer_del(s->reset_timer);
    timer_free(s->reset_timer);

    /* No additional cleanup */
}

static const VMStateDescription vmstate_pcibase = {
    .name = "8139too_pci",
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
