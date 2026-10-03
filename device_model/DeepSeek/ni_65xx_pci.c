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
#define BIT(x) (1U << (x))

#define TYPE_PCIBASE_DEVICE "ni_65xx_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_NI                0x1093
#define PCI_DEVICE_ID_NI_65XX_PXI6509   0x1710
#define PCI_CLASS_OTHERS                0xff00

/* Register offsets from ni_65xx driver */
#define MITE_IODWBSR                    0xc0
#define WENAB                           BIT(7)
#define NI_65XX_ID_REG                  0x00
#define NI_65XX_CLR_REG                 0x01
#define NI_65XX_CLR_WDOG_INT            BIT(6)
#define NI_65XX_CLR_WDOG_PING           BIT(5)
#define NI_65XX_CLR_WDOG_EXP            BIT(4)
#define NI_65XX_CLR_EDGE_INT            BIT(3)
#define NI_65XX_CLR_OVERFLOW_INT        BIT(2)
#define NI_65XX_STATUS_REG              0x02
#define NI_65XX_STATUS_WDOG_INT         BIT(5)
#define NI_65XX_STATUS_FALL_EDGE        BIT(4)
#define NI_65XX_STATUS_RISE_EDGE        BIT(3)
#define NI_65XX_STATUS_INT              BIT(2)
#define NI_65XX_STATUS_OVERFLOW_INT     BIT(1)
#define NI_65XX_STATUS_EDGE_INT         BIT(0)
#define NI_65XX_CTRL_REG                0x03
#define NI_65XX_CTRL_WDOG_ENA           BIT(5)
#define NI_65XX_CTRL_FALL_EDGE_ENA      BIT(4)
#define NI_65XX_CTRL_RISE_EDGE_ENA      BIT(3)
#define NI_65XX_CTRL_INT_ENA            BIT(2)
#define NI_65XX_CTRL_OVERFLOW_ENA       BIT(1)
#define NI_65XX_CTRL_EDGE_ENA           BIT(0)
#define NI_65XX_REV_REG                 0x04
#define NI_65XX_FILTER_REG              0x08
#define NI_65XX_RTSI_ROUTE_REG          0x0c
#define NI_65XX_RTSI_EDGE_REG           0x0e
#define NI_65XX_RTSI_WDOG_REG           0x10
#define NI_65XX_RTSI_TRIG_REG           0x12
#define NI_65XX_AUTO_CLK_SEL_REG        0x14
#define NI_65XX_AUTO_CLK_SEL_STATUS     BIT(1)
#define NI_65XX_AUTO_CLK_SEL_DISABLE    BIT(0)
#define NI_65XX_WDOG_CTRL_REG           0x15
#define NI_65XX_WDOG_CTRL_ENA           BIT(0)
#define NI_65XX_RTSI_CFG_REG            0x16
#define NI_65XX_RTSI_CFG_RISE_SENSE     BIT(2)
#define NI_65XX_RTSI_CFG_FALL_SENSE     BIT(1)
#define NI_65XX_RTSI_CFG_SYNC_DETECT    BIT(0)
#define NI_65XX_WDOG_STATUS_REG         0x17
#define NI_65XX_WDOG_STATUS_EXP         BIT(0)
#define NI_65XX_WDOG_INTERVAL_REG       0x18
#define NI_65XX_PORT(x)                 ((x) * 0x10)
#define NI_65XX_IO_DATA_REG(x)          (0x40 + NI_65XX_PORT(x))
#define NI_65XX_IO_SEL_REG(x)           (0x41 + NI_65XX_PORT(x))
#define NI_65XX_IO_SEL_OUTPUT           0
#define NI_65XX_IO_SEL_INPUT            BIT(0)
#define NI_65XX_RISE_EDGE_ENA_REG(x)    (0x42 + NI_65XX_PORT(x))
#define NI_65XX_FALL_EDGE_ENA_REG(x)    (0x43 + NI_65XX_PORT(x))
#define NI_65XX_FILTER_ENA(x)           (0x44 + NI_65XX_PORT(x))
#define NI_65XX_WDOG_HIZ_REG(x)         (0x46 + NI_65XX_PORT(x))
#define NI_65XX_WDOG_ENA(x)             (0x47 + NI_65XX_PORT(x))
#define NI_65XX_WDOG_HI_LO_REG(x)       (0x48 + NI_65XX_PORT(x))
#define NI_65XX_RTSI_ENA(x)             (0x49 + NI_65XX_PORT(x))
#define NI_65XX_PORT_TO_CHAN(x)         ((x) * 8)
#define NI_65XX_CHAN_TO_PORT(x)         ((x) / 8)
#define NI_65XX_CHAN_TO_MASK(x)         (1 << ((x) % 8))

/* Placeholder BAR sizes – exact values unknown, derived from reasonable estimation */
#define MITE_BAR_SIZE          0x1000   /* expected MITE MMIO region size */
#define NI_65XX_MMIO_BAR_SIZE  0x2000   /* expected main MMIO region size */

/* Board enumeration and static table */
enum ni_65xx_boardid {
    BOARD_PCI6509,
    BOARD_PXI6509,
    BOARD_PCI6510,
    BOARD_PCI6511,
    BOARD_PXI6511,
    BOARD_PCI6512,
    BOARD_PXI6512,
    BOARD_PCI6513,
    BOARD_PXI6513,
    BOARD_PCI6514,
    BOARD_PXI6514,
    BOARD_PCI6515,
    BOARD_PXI6515,
    BOARD_PCI6516,
    BOARD_PCI6517,
    BOARD_PCI6518,
    BOARD_PCI6519,
    BOARD_PCI6520,
    BOARD_PCI6521,
    BOARD_PXI6521,
    BOARD_PCI6528,
    BOARD_PXI6528,
};

struct ni_65xx_board {
    const char *name;
    unsigned int num_dio_ports;
    unsigned int num_di_ports;
    unsigned int num_do_ports;
    unsigned int legacy_invert:1;
};

static const struct ni_65xx_board ni_65xx_boards[] = {
    [BOARD_PCI6509] = {
        .name           = "pci-6509",
        .num_dio_ports  = 12,
    },
    [BOARD_PXI6509] = {
        .name           = "pxi-6509",
        .num_dio_ports  = 12,
    },
    [BOARD_PCI6510] = {
        .name           = "pci-6510",
        .num_di_ports   = 4,
    },
    [BOARD_PCI6511] = {
        .name           = "pci-6511",
        .num_di_ports   = 8,
    },
    [BOARD_PXI6511] = {
        .name           = "pxi-6511",
        .num_di_ports   = 8,
    },
    [BOARD_PCI6512] = {
        .name           = "pci-6512",
        .num_do_ports   = 8,
    },
    [BOARD_PXI6512] = {
        .name           = "pxi-6512",
        .num_do_ports   = 8,
    },
    [BOARD_PCI6513] = {
        .name           = "pci-6513",
        .num_do_ports   = 8,
        .legacy_invert  = 1,
    },
    [BOARD_PXI6513] = {
        .name           = "pxi-6513",
        .num_do_ports   = 8,
        .legacy_invert  = 1,
    },
    [BOARD_PCI6514] = {
        .name           = "pci-6514",
        .num_di_ports   = 4,
        .num_do_ports   = 4,
        .legacy_invert  = 1,
    },
    [BOARD_PXI6514] = {
        .name           = "pxi-6514",
        .num_di_ports   = 4,
        .num_do_ports   = 4,
        .legacy_invert  = 1,
    },
    [BOARD_PCI6515] = {
        .name           = "pci-6515",
        .num_di_ports   = 4,
        .num_do_ports   = 4,
        .legacy_invert  = 1,
    },
    [BOARD_PXI6515] = {
        .name           = "pxi-6515",
        .num_di_ports   = 4,
        .num_do_ports   = 4,
        .legacy_invert  = 1,
    },
    [BOARD_PCI6516] = {
        .name           = "pci-6516",
        .num_do_ports   = 4,
        .legacy_invert  = 1,
    },
    [BOARD_PCI6517] = {
        .name           = "pci-6517",
        .num_do_ports   = 4,
        .legacy_invert  = 1,
    },
    [BOARD_PCI6518] = {
        .name           = "pci-6518",
        .num_di_ports   = 2,
        .num_do_ports   = 2,
        .legacy_invert  = 1,
    },
    [BOARD_PCI6519] = {
        .name           = "pci-6519",
        .num_di_ports   = 2,
        .num_do_ports   = 2,
        .legacy_invert  = 1,
    },
    [BOARD_PCI6520] = {
        .name           = "pci-6520",
        .num_di_ports   = 1,
        .num_do_ports   = 1,
    },
    [BOARD_PCI6521] = {
        .name           = "pci-6521",
        .num_di_ports   = 1,
        .num_do_ports   = 1,
    },
    [BOARD_PXI6521] = {
        .name           = "pxi-6521",
        .num_di_ports   = 1,
        .num_do_ports   = 1,
    },
    [BOARD_PCI6528] = {
        .name           = "pci-6528",
        .num_di_ports   = 3,
        .num_do_ports   = 3,
    },
    [BOARD_PXI6528] = {
        .name           = "pxi-6528",
        .num_di_ports   = 3,
        .num_do_ports   = 3,
    },
};

/* Constants for the emulated board (PXI-6509) */
#define MAX_DIO_PORTS   12

/* MITE register subset – WENAB control at IODWBSR */
typedef struct {
    uint32_t iowbsr;        /* offset 0xc0 */
} MiteRegs;

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

/* Hardware register model for one DIO port */
typedef struct {
    uint32_t io_data;        /* 0x40 + port*0x10 */
    uint32_t io_sel;         /* 0x41 */
    uint32_t rise_edge_ena;  /* 0x42 */
    uint32_t fall_edge_ena;  /* 0x43 */
    uint32_t filter_ena;     /* 0x44 */
    uint32_t wdog_hiz;       /* 0x46 */
    uint32_t wdog_ena;       /* 0x47 */
    uint32_t wdog_hi_lo;     /* 0x48 */
    uint32_t rtsi_ena;       /* 0x49 */
} NI65xxPort;

/* Global device registers */
typedef struct {
    uint32_t id_reg;            /* 0x00 */
    uint32_t clr_reg;           /* 0x01 */
    uint32_t status_reg;        /* 0x02 */
    uint32_t ctrl_reg;          /* 0x03 */
    uint32_t rev_reg;           /* 0x04 */
    uint32_t filter_reg;        /* 0x08 */
    uint32_t rtsi_route_reg;    /* 0x0c */
    uint32_t rtsi_edge_reg;     /* 0x0e */
    uint32_t rtsi_wdog_reg;     /* 0x10 */
    uint32_t rtsi_trig_reg;     /* 0x12 */
    uint32_t auto_clk_sel_reg;  /* 0x14 */
    uint32_t wdog_ctrl_reg;     /* 0x15 */
    uint32_t rtsi_cfg_reg;      /* 0x16 */
    uint32_t wdog_status_reg;   /* 0x17 */
    uint32_t wdog_interval_reg; /* 0x18 */
    NI65xxPort ports[MAX_DIO_PORTS];
} NI65xxRegs;

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* MITE bridge registers (BAR0) */
    MiteRegs mite;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;
    uint32_t irq_status;
    uint32_t irq_enable;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    NI65xxRegs regs;
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint8_t ctrl = s->regs.ctrl_reg;
    uint8_t status = s->regs.status_reg;

    /* Check if global interrupt is enabled and active */
    bool int_active = (status & NI_65XX_STATUS_INT) &&
                      (ctrl & NI_65XX_CTRL_INT_ENA);
    if (int_active) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* MITE BAR0 handlers */
static uint64_t mite_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}
static void mite_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* The driver writes MITE_IODWBSR (0xc0) to set window; ignore */
}
static const MemoryRegionOps mite_mmio_ops = {
    .read = mite_mmio_read,
    .write = mite_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

/* NI65xx main registers BAR1 handlers */
static uint64_t ni65xx_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    NI65xxRegs *regs = &s->regs;
    uint64_t val = 0;

    switch (addr) {
    case 0x00: /* ID_REG */
        if (size == 1) val = regs->id_reg;
        break;
    case 0x02: /* STATUS_REG */
        if (size == 1) val = regs->status_reg;
        break;
    case 0x03: /* CTRL_REG */
        if (size == 1) val = regs->ctrl_reg;
        break;
    case 0x04: /* REV_REG */
        if (size == 1) val = regs->rev_reg;
        break;
    case 0x08: /* FILTER_REG */
        if (size == 4) val = regs->filter_reg;
        break;
    case 0x0c: /* RTSI_ROUTE_REG */
        if (size <= 2) val = regs->rtsi_route_reg;
        break;
    case 0x0e: /* RTSI_EDGE_REG */
        if (size <= 2) val = regs->rtsi_edge_reg;
        break;
    case 0x10: /* RTSI_WDOG_REG */
        if (size <= 2) val = regs->rtsi_wdog_reg;
        break;
    case 0x12: /* RTSI_TRIG_REG */
        if (size <= 2) val = regs->rtsi_trig_reg;
        break;
    case 0x14: /* AUTO_CLK_SEL_REG */
        if (size == 1) val = regs->auto_clk_sel_reg;
        break;
    case 0x15: /* WDOG_CTRL_REG */
        if (size == 1) val = regs->wdog_ctrl_reg;
        break;
    case 0x16: /* RTSI_CFG_REG */
        if (size == 1) val = regs->rtsi_cfg_reg;
        break;
    case 0x17: /* WDOG_STATUS_REG */
        if (size == 1) val = regs->wdog_status_reg;
        break;
    case 0x18: /* WDOG_INTERVAL_REG */
        if (size == 4) val = regs->wdog_interval_reg;
        break;
    default:
        if (addr >= 0x40 && addr < 0x40 + MAX_DIO_PORTS * 0x10) {
            int port = (addr - 0x40) / 0x10;
            int off = (addr - 0x40) % 0x10;
            if (port < MAX_DIO_PORTS) {
                switch (off) {
                case 0: /* IO_DATA_REG */
                    if (size == 1) val = regs->ports[port].io_data;
                    break;
                case 1: /* IO_SEL_REG */
                    if (size == 1) val = regs->ports[port].io_sel;
                    break;
                case 2: /* RISE_EDGE_ENA_REG */
                    if (size == 1) val = regs->ports[port].rise_edge_ena;
                    break;
                case 3: /* FALL_EDGE_ENA_REG */
                    if (size == 1) val = regs->ports[port].fall_edge_ena;
                    break;
                case 4: /* FILTER_ENA */
                    if (size == 1) val = regs->ports[port].filter_ena;
                    break;
                case 6: /* WDOG_HIZ_REG */
                    if (size == 1) val = regs->ports[port].wdog_hiz;
                    break;
                case 7: /* WDOG_ENA */
                    if (size == 1) val = regs->ports[port].wdog_ena;
                    break;
                case 8: /* WDOG_HI_LO_REG */
                    if (size == 1) val = regs->ports[port].wdog_hi_lo;
                    break;
                case 9: /* RTSI_ENA */
                    if (size == 1) val = regs->ports[port].rtsi_ena;
                    break;
                default:
                    break;
                }
            }
        }
        break;
    }
    return val;
}

static void ni65xx_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    NI65xxRegs *regs = &s->regs;

    switch (addr) {
    case 0x01: /* CLR_REG */
        if (size == 1) {
            if (val & NI_65XX_CLR_WDOG_INT)   regs->status_reg &= ~NI_65XX_STATUS_WDOG_INT;
            if (val & NI_65XX_CLR_WDOG_PING)  ; /* no status bit */
            if (val & NI_65XX_CLR_WDOG_EXP)   regs->status_reg &= ~NI_65XX_STATUS_WDOG_INT; /* assume */
            if (val & NI_65XX_CLR_EDGE_INT)   regs->status_reg &= ~(NI_65XX_STATUS_EDGE_INT | NI_65XX_STATUS_INT);
            if (val & NI_65XX_CLR_OVERFLOW_INT) regs->status_reg &= ~(NI_65XX_STATUS_OVERFLOW_INT | NI_65XX_STATUS_INT);
            pcibase_update_irq(s);
        }
        break;
    case 0x03: /* CTRL_REG */
        if (size == 1) {
            regs->ctrl_reg = val;
            pcibase_update_irq(s);
        }
        break;
    case 0x08: /* FILTER_REG */
        if (size == 4) regs->filter_reg = val;
        break;
    case 0x0c: /* RTSI_ROUTE_REG */
        if (size <= 2) regs->rtsi_route_reg = val;
        break;
    case 0x0e: /* RTSI_EDGE_REG */
        if (size <= 2) regs->rtsi_edge_reg = val;
        break;
    case 0x10: /* RTSI_WDOG_REG */
        if (size <= 2) regs->rtsi_wdog_reg = val;
        break;
    case 0x12: /* RTSI_TRIG_REG */
        if (size <= 2) regs->rtsi_trig_reg = val;
        break;
    case 0x14: /* AUTO_CLK_SEL_REG */
        if (size == 1) regs->auto_clk_sel_reg = val;
        break;
    case 0x15: /* WDOG_CTRL_REG */
        if (size == 1) regs->wdog_ctrl_reg = val;
        break;
    case 0x16: /* RTSI_CFG_REG */
        if (size == 1) regs->rtsi_cfg_reg = val;
        break;
    case 0x17: /* WDOG_STATUS_REG */
        if (size == 1) regs->wdog_status_reg = val;
        break;
    case 0x18: /* WDOG_INTERVAL_REG */
        if (size == 4) regs->wdog_interval_reg = val;
        break;
    default:
        if (addr >= 0x40 && addr < 0x40 + MAX_DIO_PORTS * 0x10) {
            int port = (addr - 0x40) / 0x10;
            int off = (addr - 0x40) % 0x10;
            if (port < MAX_DIO_PORTS) {
                switch (off) {
                case 0: /* IO_DATA_REG */
                    if (size == 1) regs->ports[port].io_data = val;
                    break;
                case 1: /* IO_SEL_REG */
                    if (size == 1) regs->ports[port].io_sel = val;
                    break;
                case 2: /* RISE_EDGE_ENA_REG */
                    if (size == 1) regs->ports[port].rise_edge_ena = val;
                    break;
                case 3: /* FALL_EDGE_ENA_REG */
                    if (size == 1) regs->ports[port].fall_edge_ena = val;
                    break;
                case 4: /* FILTER_ENA */
                    if (size == 1) regs->ports[port].filter_ena = val;
                    break;
                case 6: /* WDOG_HIZ_REG */
                    if (size == 1) regs->ports[port].wdog_hiz = val;
                    break;
                case 7: /* WDOG_ENA */
                    if (size == 1) regs->ports[port].wdog_ena = val;
                    break;
                case 8: /* WDOG_HI_LO_REG */
                    if (size == 1) regs->ports[port].wdog_hi_lo = val;
                    break;
                case 9: /* RTSI_ENA */
                    if (size == 1) regs->ports[port].rtsi_ena = val;
                    break;
                default:
                    break;
                }
            }
        }
        break;
    }
}

static const MemoryRegionOps ni65xx_mmio_ops = {
    .read = ni65xx_mmio_read,
    .write = ni65xx_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

/* PIO Handlers (not used by driver, left as stubs) */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* No PIO used */
}

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
    s->regs.id_reg = 0x01;
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }
    
    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        const MemoryRegionOps *ops;
        if (bi->index == 0) {
            ops = &mite_mmio_ops;
        } else {
            ops = &ni65xx_mmio_ops;
        }
        memory_region_init_io(mr, OBJECT(s), ops, s, bi->name, aligned_size);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x1093 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x1710 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0xff00 );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = MITE_BAR_SIZE, .name = "mite" };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_MMIO, .size = NI_65XX_MMIO_BAR_SIZE, .name = "ni_65xx_regs" };
    s->num_bars = 2;
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize registers to power-on defaults */
    memset(&s->regs, 0, sizeof(s->regs));
    s->regs.id_reg = 0x01;
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
    /* No additional resources to free */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "ni_65xx_pci",
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
