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

#define TYPE_PCIBASE_DEVICE "r8169_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x10ec
#define DEVICE_ID 0x2502
#define CLASS_ID PCI_CLASS_NETWORK_ETHERNET

#define R8169_REGS_SIZE 0x7000  /* increased to cover all registers used by driver */

/* Register offsets from rtl_registers */
#define MAC0             0x00
#define MAR0             0x08
#define CounterAddrLow   0x10
#define CounterAddrHigh  0x14
#define TxDescStartAddrLow  0x20
#define TxDescStartAddrHigh 0x24
#define TxHDescStartAddrLow 0x28
#define TxHDescStartAddrHigh 0x2c
#define FLASH            0x30
#define ERSR             0x36
#define ChipCmd          0x37
#define TxPoll           0x38
#define IntrStatus       0x3e
#define TxConfig         0x40
#define RxConfig         0x44
#define Cfg9346          0x50
#define Config0          0x51
#define Config1          0x52
#define Config2          0x53
#define Config3          0x54
#define Config4          0x55
#define Config5          0x56
#define PHYAR            0x60
#define PHYstatus        0x6c
#define RxMaxSize        0xda
#define CPlusCmd         0xe0
#define IntrMitigate     0xe2
#define RxDescAddrLow    0xe4
#define RxDescAddrHigh   0xe8
#define EarlyTxThres     0xec
#define FuncEvent        0xf0
#define FuncEventMask    0xf4
#define FuncPresetState  0xf8
#define IBCR0            0xf8
#define IBCR2            0xf9
#define IBIMR0           0xfa
#define IBISR0           0xfb
#define FuncForceEvent   0xfc

/* rtl8168_8101_registers */
#define CSIDR            0x64
#define CSIAR            0x68
#define PMCH             0x6f
#define EPHYAR           0x80
#define DLLPR            0xd0
#define DBG_REG          0xd1
#define TWSI             0xd2
#define MCU              0xd3
#define EFUSEAR          0xdc
#define MISC_1           0xf2

/* rtl8168_registers */
#define LED_CTRL         0x18
#define LED_FREQ         0x1a
#define EEE_LED          0x1b
#define ERIDR            0x70
#define ERIAR            0x74
#define EPHY_RXER_NUM    0x7c
#define OCPDR            0xb0
#define OCPAR            0xb4
#define GPHY_OCP         0xb8
#define RDSAR1           0xd0
#define MISC             0xf0
#define COMBO_LTR_EXTEND 0xb6

/* rtl8125_registers */
#define LEDSEL0          0x18
#define INT_CFG0_8125    0x34
#define IntrMask_8125    0x38
#define IntrStatus_8125  0x3c
#define INT_CFG1_8125    0x7a
#define LEDSEL2          0x84
#define LEDSEL1          0x86
#define TxPoll_8125      0x90
#define LEDSEL3          0x96
#define MAC0_BKP         0x19e0
#define RSS_CTRL_8125    0x4500
#define Q_NUM_CTRL_8125  0x4800
#define EEE_TXIDLE_TIMER_8125 0x6048

/* Additional registers */
#define TX_CONFIG_V2     0x60b0
#define ALDPS_LTR        0xe0a2
#define LTR_OBFF_LOCK    0xe032
#define LTR_SNOOP        0xe034

/* Bit definitions */
#define ChipCmd_StopReq  0x80
#define ChipCmd_CmdReset 0x10
#define ChipCmd_CmdRxEnb 0x08
#define ChipCmd_CmdTxEnb 0x04
#define ChipCmd_RxBufEmpty 0x01
#define TxBufEmpty       0x02  /* Added to satisfy rtl_rxtx_empty_cond */

/* IntrStatus bits */
#define SYSErr           0x8000
#define PCSTimeout       0x4000
#define SWInt            0x0100
#define TxDescUnavail    0x0080
#define RxFIFOOver       0x0040
#define LinkChg          0x0020
#define RxOverflow       0x0010
#define TxErr            0x0008
#define TxOK             0x0004
#define RxErr            0x0002
#define RxOK             0x0001

/* Config1 */
#define LEDS1            (1 << 7)
#define LEDS0            (1 << 6)
#define Speed_down       (1 << 4)
#define MEMMAP           (1 << 3)
#define IOMAP            (1 << 2)
#define VPD              (1 << 1)
#define PMEnable         (1 << 0)

/* Config2 */
#define ClkReqEn         (1 << 7)
#define MSIEnable        (1 << 5)
#define PCI_Clock_66MHz  0x01
#define PCI_Clock_33MHz  0x00

/* Config5 */
#define LanWake          (1 << 1)
#define PMEStatus        (1 << 0)
#define ASPM_en          (1 << 0)

/* CPlusCmd */
#define Normal_mode      (1 << 13)
#define RxVlan           (1 << 6)
#define RxChkSum         (1 << 5)

/* Descriptor bits */
#define DescOwn          (1 << 31)
#define RingEnd          (1 << 30)
#define FirstFrag        (1 << 29)
#define LastFrag         (1 << 28)

/* OCP constants from driver headers */
#define OCPAR_FLAG       (1U << 31)
#define OCP_STD_PHY_BASE 0xa400

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

    uint8_t regs[R8169_REGS_SIZE];
    uint16_t phy_regs[32]; /* PHY registers (0..31) */

    uint32_t gphy_ocp_cmd; /* remember last GPHY OCP command for busy emulation */
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool active = (s->intr_status & s->intr_mask) != 0;

    if (msi_enabled(pdev)) {
        if (active) {
            msi_notify(pdev, 0);
        }
    } else {
        pci_set_irq(pdev, active ? 1 : 0);
    }
}

static void r8169_reset_regs(PCIBaseState *s)
{
    /* Reset all MAC registers and PHY registers to power-on defaults */
    memset(s->regs, 0, sizeof(s->regs));
    memset(s->phy_regs, 0, sizeof(s->phy_regs));
    s->intr_status = 0;
    s->intr_mask = 0;
    s->gphy_ocp_cmd = 0;

    /* Set default values for crucial registers */
    /* TxConfig: set XID to 0x64100000 (RTL8125a) */
    stl_le_p(s->regs + TxConfig, 0x64100000);
    /* PHY ID1 and ID2 (Realtek pseudo) */
    s->phy_regs[2] = 0x001c;
    s->phy_regs[3] = 0xc912;
    /* BMSR: autoneg complete, link up, 1000/100/10 capable */
    s->phy_regs[1] = 0x7949;
    /* BMCR: reset completed, autoneg enable */
    s->phy_regs[0] = 0x1140;

    /* Indicate chip idle: RX and TX buffers empty, chip stopped */
    s->regs[ChipCmd] = ChipCmd_RxBufEmpty | TxBufEmpty | ChipCmd_StopReq;

    /* Advertise MSI capability */
    s->regs[Config2] = MSIEnable;

    /* Set MCU with LINK_LIST_RDY and RXTX_EMPTY bits to satisfy loop wait conditions */
    s->regs[MCU] = 0xc1; /* Bit0: LINK_LIST_RDY, Bits 6-7: RXTX_EMPTY (guessed) */
    /* Set IntrMitigate to 0x0103 to satisfy rtl_rxtx_empty_cond_2 */
    stw_le_p(s->regs + IntrMitigate, 0x0103);
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= R8169_REGS_SIZE) {
        return ~0ULL;
    }

    /* Special register reads */
    switch (addr) {
    case IntrStatus: /* 0x3e, 16-bit classic */
        if (size == 2) {
            return s->intr_status & 0xffff;
        }
        break;
    case IntrStatus_8125: /* 0x3c, 32-bit 8125 */
        if (size == 4) {
            return s->intr_status;
        }
        break;
    case IntrMask_8125: /* 0x38, 32-bit 8125 */
        if (size == 4) {
            return s->intr_mask;
        }
        break;
    case PHYAR: /* 0x60 */
        if (size == 4) {
            return *(uint32_t *)(s->regs + addr);
        }
        break;
    case ChipCmd: /* 0x37, always set RxBufEmpty and TxBufEmpty to satisfy idle state */
        if (size == 1) {
            val = s->regs[addr] | ChipCmd_RxBufEmpty | TxBufEmpty;
            return val;
        }
        break;
    case OCPAR: /* 0xb4, return 0 to indicate not busy */
        if (size == 4) {
            return 0;
        }
        break;
    case GPHY_OCP: /* 0xb8, emulated two-phase OCP read/write */
        if (size == 4) {
            uint32_t cur = ldl_le_p(s->regs + addr);
            if (cur & OCPAR_FLAG) {
                /* Busy flag set: complete the OCP command */
                uint32_t cmd = s->gphy_ocp_cmd;
                uint32_t ocp_addr = (cmd >> 15) & 0x1fffff; /* match address extraction */
                uint32_t data = 0;
                if (cmd & OCPAR_FLAG) {
                    /* Write command: data already written? Actually write command doesn't need data */
                    /* For write, just clear busy */
                    data = 0;
                } else {
                    /* Read command */
                    if (ocp_addr >= OCP_STD_PHY_BASE && ocp_addr <= OCP_STD_PHY_BASE + 62) {
                        int reg = (ocp_addr - OCP_STD_PHY_BASE) / 2;
                        data = s->phy_regs[reg];
                    }
                }
                stl_le_p(s->regs + addr, data);
                s->gphy_ocp_cmd = 0;
                return data;
            } else {
                /* Not busy, return current value */
                return cur;
            }
        }
        break;
    case MCU: /* 0xd3, force LINK_LIST_RDY (bit0) and RXTX_EMPTY (bits 6-7) set to satisfy loops */
        if (size == 1) {
            val = s->regs[MCU] | 0xc1;
            return val;
        }
        break;
    case IntrMitigate: /* 0xe2, force 0x0103 to satisfy rtl_rxtx_empty_cond_2 */
        if (size == 2) {
            return 0x0103;
        }
        break;
    }

    /* Generic byte-array read with size handling */
    if (addr + size <= R8169_REGS_SIZE) {
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
            qemu_log_mask(LOG_GUEST_ERROR, "%s: unsupported size %d at 0x%"PRIx64"\n", __func__, size, addr);
            break;
        }
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= R8169_REGS_SIZE) {
        return;
    }

    /* Special register writes */
    switch (addr) {
    case IntrStatus: /* 0x3e, 16-bit W1C */
        if (size == 2) {
            s->intr_status &= ~(val & 0xffff);
            pcibase_update_irq(s);
            return;
        }
        break;
    case IntrStatus_8125: /* 0x3c, 32-bit W1C */
        if (size == 4) {
            s->intr_status &= ~val;
            pcibase_update_irq(s);
            return;
        }
        break;
    case IntrMask_8125: /* 0x38, 32-bit */
        if (size == 4) {
            s->intr_mask = val;
            pcibase_update_irq(s);
            return;
        }
        break;
    case ChipCmd: /* 0x37, 8-bit */
        if (size == 1) {
            if (val & ChipCmd_CmdReset) {
                /* Perform full MAC reset */
                r8169_reset_regs(s);
            } else {
                /* Update only writable command bits, keep status bits */
                uint8_t new_val = s->regs[addr] & ~0x9C; /* clear command bits */
                new_val |= val & 0x9C; /* set new command bits */
                s->regs[addr] = new_val;
            }
            return;
        }
        break;
    case PHYAR: /* 0x60, 32-bit */
        if (size == 4) {
            uint8_t reg = (val >> 16) & 0x1f;
            if (val & 0x80000000) {
                /* MDIO write */
                s->phy_regs[reg] = val & 0xffff;
                /* Write complete: clear busy and data */
                stl_le_p(s->regs + addr, 0);
            } else {
                /* MDIO read */
                uint32_t resp = 0x80000000 | s->phy_regs[reg];
                stl_le_p(s->regs + addr, resp);
            }
            return;
        }
        break;
    case GPHY_OCP: /* 0xb8, start OCP command */
        if (size == 4) {
            s->gphy_ocp_cmd = (uint32_t)val;
            /* Set busy flag; actual completion happens in read handler */
            stl_le_p(s->regs + addr, OCPAR_FLAG);
            return;
        }
        break;
    case OCPDR: /* 0xb0 */
        if (size == 4) {
            /* For simplicity, just store the value */
            stl_le_p(s->regs + addr, val);
            return;
        }
        break;
    case OCPAR: /* 0xb4, just store the value; read will indicate idle */
        if (size == 4) {
            stl_le_p(s->regs + addr, val);
            return;
        }
        break;
    case MCU: /* 0xd3, ignore writes to maintain forced bits for loop conditions */
        /* Do nothing: keep original value with LINK_LIST_RDY and RXTX_EMPTY set */
        return;
    case IntrMitigate: /* 0xe2, ignore writes to keep fixed 0x0103 */
        /* Do nothing */
        return;
    }

    /* Generic byte-array write */
    if (addr + size <= R8169_REGS_SIZE) {
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
            qemu_log_mask(LOG_GUEST_ERROR, "%s: unsupported size %d at 0x%"PRIx64"\n", __func__, size, addr);
            break;
        }
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* PIO not used by driver; return 0xff */
    return ~0ULL;
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
    r8169_reset_regs(s);
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* Enable MSI */
    if (msi_init(pdev, 0, 1, true, false, errp) < 0) {
        return;
    }
    s->has_msi = true;

    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = R8169_REGS_SIZE;
    s->bar_info[0].name = "r8169-mmio";

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
    .name = "r8169_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(intr_status, PCIBaseState),
        VMSTATE_UINT32(intr_mask, PCIBaseState),
        VMSTATE_BUFFER(regs, PCIBaseState),
        VMSTATE_UINT16_ARRAY(phy_regs, PCIBaseState, 32),
        VMSTATE_UINT32(gphy_ocp_cmd, PCIBaseState),
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
