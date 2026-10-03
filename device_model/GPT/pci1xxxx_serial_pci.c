/*
 * QEMU PCI device model for Microchip/EFAR pci1xxxx UART (behavioral stub)
 *
 * This model is derived strictly from the provided Linux driver
 * drivers/tty/serial/8250/8250_pci1xxxx.c and only implements behaviors
 * that are explicitly visible there. It is intended to be sufficient for
 * the driver to probe, initialize, and register ports.
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

#define TYPE_PCIBASE_DEVICE "pci1xxxx_serial_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_EFAR            0x1055
#define PCI_DEVICE_ID_EFAR_PCI12000   0xa002
#define PCI_DEVICE_ID_EFAR_PCI11010   0xa012
#define PCI_DEVICE_ID_EFAR_PCI11101   0xa022
#define PCI_DEVICE_ID_EFAR_PCI11400   0xa032
#define PCI_DEVICE_ID_EFAR_PCI11414   0xa042

#define UART_SYSTEM_ADDR_BASE         0x1000
#define UART_DEV_REV_REG              (UART_SYSTEM_ADDR_BASE + 0x00)
#define UART_SYSLOCK_REG              (UART_SYSTEM_ADDR_BASE + 0xA0)
#define UART_RX_BYTE_FIFO             0x00
#define UART_TX_BYTE_FIFO             0x00
#define UART_FIFO_CTL                 0x02
#define UART_MODEM_CTL_REG            0x04
#define UART_LINE_STAT_REG            0x05
#define UART_ACTV_REG                 0x11
#define UART_PCI_CTRL_REG             0x80
#define ADCL_CFG_REG                  0x40
#define UART_WAKE_REG                 0x8C
#define UART_WAKE_MASK_REG            0x90
#define UART_BAUD_CLK_DIVISOR_REG     0x54
#define FRAC_DIV_CFG_REG              0x58
#define UART_RESET_REG                0x94
#define UART_BURST_STATUS_REG         0x9C
#define UART_TX_BURST_FIFO            0xA0
#define UART_RX_BURST_FIFO            0xA4

#define PORT_OFFSET                   0x100

#define PCIBASE_VENDOR_ID             PCI_VENDOR_ID_EFAR
#define PCIBASE_DEVICE_ID             PCI_DEVICE_ID_EFAR_PCI11010
#define PCIBASE_CLASS_ID              0x0700

#define MAX_PORTS                     4

/* Minimal subset of register bits used in the driver that we must shadow */
#define UART_SYSLOCK                  0x1

/* Updated UART_BURST_STATUS_REG bits from new driver snippet */
#define UART_BST_STAT_LSR_RX_ERR_MASK 0x9E000000U
#define UART_BST_STAT_LSR_OVERRUN_ERR 0x02000000U
#define UART_BST_STAT_LSR_FRAME_ERR   0x08000000U
#define UART_BST_STAT_LSR_PARITY_ERR  0x04000000U
#define UART_BST_STAT_IIR_INT_PEND    0x00100000U
#define UART_BST_STAT_LSR_RX_MASK     0x9F000000U
#define UART_BST_STAT_LSR_THRE        0x20000000U
#define UART_BST_STAT_RX_COUNT_MASK   0x000000FFU
#define UART_BST_STAT_TX_COUNT_MASK   0x0000FF00U

/* UART line status overrun clear value */
#define UART_LSR_OVERRUN_ERR_CLR      0x43

/* Wake-related bits and masks from driver snippet */
#define UART_WAKE_N_PIN               (1U << 2)
#define UART_WAKE_SRCS                (UART_WAKE_N_PIN)
#define UART_BLOCK_SET_ACTIVE         (1U << 0)

/* PCI control bits used in the driver */
#define UART_PCI_CTRL_D3_CLK_ENABLE       (1U << 0)
#define UART_PCI_CTRL_SET_MULTIPLE_MSI    (1U << 4)

/* Reset bits used in the driver */
#define UART_RESET_HOT_RESET_DISABLE  (1U << 17)
#define UART_RESET_D3_RESET_DISABLE   (1U << 16)

/* ADCL / RS485 related bits and masks used in the driver */
#define ADCL_CFG_EN                   (1U << 0)
#define ADCL_CFG_PIN_SEL              (1U << 1)
#define ADCL_CFG_POL_SEL              (1U << 2)
#define ADCL_CFG_RTS_DELAY_MASK       (((uint32_t)0xF) << 8)
#define FRAC_DIV_TX_END_POINT_MASK    (((uint32_t)0xF) << 20)

/* BAUD_CLOCK_DIV_INT_MSK is used with FRAC_DIV/UART_BAUD_CLK_DIVISOR */
#define BAUD_CLOCK_DIV_INT_MSK        0xFFFFFF00U

/* Minimal FIELD_PREP/FIELD_GET/FIELD_MAX helpers for masks */
#define FIELD_PREP(_mask, _val)   (((uint32_t)(_val) << __builtin_ctz(_mask)) & (_mask))
#define FIELD_GET(_mask, _reg)    (((uint32_t)(_reg) & (_mask)) >> __builtin_ctz(_mask))
#define FIELD_MAX(_mask)          FIELD_GET(_mask, _mask)

/* Burst sizes inferred from driver usage (byte count granularity) */
#define UART_BURST_SIZE               4U
#define RX_BUF_SIZE                   256U
#define UART_BYTE_SIZE                1U

/* Simple enum for BAR types */
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

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint8_t dev_rev;

    /* BAR0 MMIO register space shadow for system region + UARTs */
    uint32_t sys_reg_dev_rev;
    uint32_t sys_reg_syslock;
    uint32_t sys_reg_reset;
    uint32_t sys_reg_pci_ctrl;

    uint8_t sys_reg_wake;
    uint8_t sys_reg_wake_mask;

    /* Per-UART port register shadows (up to MAX_PORTS) */
    uint32_t port_adcl_cfg[MAX_PORTS];
    uint32_t port_frac_div_cfg[MAX_PORTS];
    uint32_t port_baud_clk_div[MAX_PORTS];
    uint8_t  port_modem_ctl[MAX_PORTS];
    uint8_t  port_line_status[MAX_PORTS];
    uint8_t  port_actv[MAX_PORTS];

    /* Burst FIFOs and status */
    uint32_t port_burst_status[MAX_PORTS];

    /* RX/TX FIFO contents for each port (very simple queues) */
    uint8_t  port_rx_fifo[MAX_PORTS][RX_BUF_SIZE];
    uint32_t port_rx_head[MAX_PORTS];
    uint32_t port_rx_tail[MAX_PORTS];

    uint8_t  port_tx_fifo[MAX_PORTS][RX_BUF_SIZE];
    uint32_t port_tx_head[MAX_PORTS];
    uint32_t port_tx_tail[MAX_PORTS];

    /* Interrupt state */
    uint32_t irq_status; /* bit per port, simplistic */
};

/* Internal helper for status-triggered signaling. */
static void pcibase_raise_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    if (msi_enabled(pdev)) {
        msi_notify(pdev, 0);
    } else if (msix_enabled(pdev)) {
        msix_notify(pdev, 0);
    } else {
        pci_set_irq(pdev, 1);
    }
}

static void pcibase_lower_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    if (!msi_enabled(pdev) && !msix_enabled(pdev)) {
        pci_set_irq(pdev, 0);
    }
}

G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    if (s->irq_status) {
        pcibase_raise_irq(s);
    } else {
        pcibase_lower_irq(s);
    }
}

/* Device-initiated DMA logic: the provided driver code does not configure
 * any device DMA descriptors or use pci_dma_* for this UART block.
 * Therefore we do not implement any DMA engine here.
 */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    (void)s;
    (void)is_write;
}

/* Simple helper to map a BAR0 offset to port index and per-port offset.
 * The 8250 core is configured with PORT_OFFSET * port_idx when setting up
 * each port, so we mimic that here by splitting address space into chunks.
 */
static bool pcibase_decode_port(hwaddr addr, unsigned *port, hwaddr *poff)
{
    if (addr < PORT_OFFSET) {
        *port = 0;
        *poff = addr;
        return true;
    }
    unsigned p = addr / PORT_OFFSET;
    if (p >= MAX_PORTS) {
        return false;
    }
    *port = p;
    *poff = addr - (hwaddr)p * PORT_OFFSET;
    return true;
}

static uint8_t pcibase_fifo_pop(uint8_t *buf, uint32_t *head, uint32_t *tail)
{
    if (*head == *tail) {
        return 0;
    }
    uint8_t v = buf[*tail];
    *tail = (*tail + 1) % RX_BUF_SIZE;
    return v;
}

static void pcibase_fifo_push(uint8_t *buf, uint32_t *head, uint32_t *tail, uint8_t v)
{
    uint32_t next = (*head + 1) % RX_BUF_SIZE;
    if (next == *tail) {
        return;
    }
    buf[*head] = v;
    *head = next;
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Handle system registers in BAR0 at UART_SYSTEM_ADDR_BASE */
    if (addr >= UART_SYSTEM_ADDR_BASE && addr < UART_SYSTEM_ADDR_BASE + 0x200) {
        hwaddr off = addr - UART_SYSTEM_ADDR_BASE;
        switch (off) {
        case 0x00: /* UART_DEV_REV_REG */
            val = s->sys_reg_dev_rev;
            break;
        case 0xA0: /* UART_SYSLOCK_REG */
            val = s->sys_reg_syslock;
            break;
        case 0x94: /* UART_RESET_REG */
            val = s->sys_reg_reset;
            break;
        case 0x80: /* UART_PCI_CTRL_REG */
            val = s->sys_reg_pci_ctrl;
            break;
        case 0x8C: /* UART_WAKE_REG */
            val = s->sys_reg_wake;
            break;
        case 0x90: /* UART_WAKE_MASK_REG */
            val = s->sys_reg_wake_mask;
            break;
        default:
            val = 0;
            break;
        }
        return val;
    }

    /* Per-port region decoding */
    unsigned port;
    hwaddr poff;
    if (!pcibase_decode_port(addr, &port, &poff)) {
        return 0;
    }

    if (port >= MAX_PORTS) {
        return 0;
    }

    switch (poff) {
    case UART_RX_BYTE_FIFO:
        if (size == 1) {
            val = pcibase_fifo_pop(s->port_rx_fifo[port], &s->port_rx_head[port], &s->port_rx_tail[port]);
            /* update RX count in burst status */
            {
                uint32_t count = FIELD_GET(UART_BST_STAT_RX_COUNT_MASK, s->port_burst_status[port]);
                if (count > 0) {
                    count--;
                    s->port_burst_status[port] &= ~UART_BST_STAT_RX_COUNT_MASK;
                    s->port_burst_status[port] |= FIELD_PREP(UART_BST_STAT_RX_COUNT_MASK, count);
                    if (count == 0) {
                        s->port_burst_status[port] &= ~UART_BST_STAT_LSR_RX_MASK;
                    }
                }
            }
        }
        break;
    case UART_FIFO_CTL:
        if (size == 1) {
            val = 0;
        }
        break;
    case UART_MODEM_CTL_REG:
        if (size == 1) {
            val = s->port_modem_ctl[port];
        }
        break;
    case UART_LINE_STAT_REG:
        if (size == 1) {
            val = s->port_line_status[port];
        }
        break;
    case UART_ACTV_REG:
        if (size == 1) {
            val = s->port_actv[port];
        }
        break;
    case UART_BURST_STATUS_REG:
        if (size == 4) {
            val = s->port_burst_status[port];
            /* Simplify: clear RX/THRE and pending bits on read of status */
            s->port_burst_status[port] &= ~(UART_BST_STAT_LSR_RX_MASK | UART_BST_STAT_LSR_THRE | UART_BST_STAT_IIR_INT_PEND);
            s->irq_status &= ~(1u << port);
            pcibase_update_irq(s);
        }
        break;
    case UART_RX_BURST_FIFO:
        if (size == 4) {
            uint32_t word = 0;
            uint32_t count = FIELD_GET(UART_BST_STAT_RX_COUNT_MASK, s->port_burst_status[port]);
            for (int i = 0; i < 4; i++) {
                uint8_t b = 0;
                if (count > 0) {
                    b = pcibase_fifo_pop(s->port_rx_fifo[port], &s->port_rx_head[port], &s->port_rx_tail[port]);
                    count--;
                }
                word |= ((uint32_t)b) << (8 * i);
            }
            s->port_burst_status[port] &= ~UART_BST_STAT_RX_COUNT_MASK;
            s->port_burst_status[port] |= FIELD_PREP(UART_BST_STAT_RX_COUNT_MASK, count);
            if (count == 0) {
                s->port_burst_status[port] &= ~UART_BST_STAT_LSR_RX_MASK;
            }
            val = word;
        }
        break;
    case ADCL_CFG_REG:
        if (size == 4) {
            val = s->port_adcl_cfg[port];
        }
        break;
    case FRAC_DIV_CFG_REG:
        if (size == 4) {
            val = s->port_frac_div_cfg[port];
        }
        break;
    case UART_BAUD_CLK_DIVISOR_REG:
        if (size == 4) {
            val = s->port_baud_clk_div[port];
        }
        break;
    case UART_WAKE_REG:
        if (size == 1) {
            val = s->sys_reg_wake;
        }
        break;
    case UART_WAKE_MASK_REG:
        if (size == 1) {
            val = s->sys_reg_wake_mask;
        }
        break;
    default:
        val = 0;
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* System registers */
    if (addr >= UART_SYSTEM_ADDR_BASE && addr < UART_SYSTEM_ADDR_BASE + 0x200) {
        hwaddr off = addr - UART_SYSTEM_ADDR_BASE;
        switch (off) {
        case 0x00: /* UART_DEV_REV_REG - read only in driver; ignore writes */
            break;
        case 0xA0: /* UART_SYSLOCK_REG */
            if (size == 4) {
                s->sys_reg_syslock = (uint32_t)val;
            }
            break;
        case 0x94: /* UART_RESET_REG */
            if (size == 4) {
                /* Just store; driver ORs and ANDs specific bits */
                s->sys_reg_reset = (uint32_t)val;
            }
            break;
        case 0x80: /* UART_PCI_CTRL_REG */
            if (size == 1 || size == 4) {
                s->sys_reg_pci_ctrl = (uint32_t)val;
            }
            break;
        case 0x8C: /* UART_WAKE_REG */
            if (size == 1 || size == 4) {
                s->sys_reg_wake = (uint8_t)val;
            }
            break;
        case 0x90: /* UART_WAKE_MASK_REG */
            if (size == 1 || size == 4) {
                s->sys_reg_wake_mask = (uint8_t)val;
            }
            break;
        default:
            break;
        }
        return;
    }

    unsigned port;
    hwaddr poff;
    if (!pcibase_decode_port(addr, &port, &poff)) {
        return;
    }

    if (port >= MAX_PORTS) {
        return;
    }

    switch (poff) {
    case UART_TX_BYTE_FIFO:
        if (size == 1) {
            uint8_t b = (uint8_t)val;
            pcibase_fifo_push(s->port_tx_fifo[port], &s->port_tx_head[port], &s->port_tx_tail[port], b);
            /* Mark TX data and THRE for simplicity */
            s->port_burst_status[port] |= UART_BST_STAT_LSR_THRE;
            {
                uint32_t count = FIELD_GET(UART_BST_STAT_TX_COUNT_MASK, s->port_burst_status[port]);
                if (count < FIELD_MAX(UART_BST_STAT_TX_COUNT_MASK)) {
                    count++;
                }
                s->port_burst_status[port] &= ~UART_BST_STAT_TX_COUNT_MASK;
                s->port_burst_status[port] |= FIELD_PREP(UART_BST_STAT_TX_COUNT_MASK, count);
            }
            s->port_burst_status[port] |= UART_BST_STAT_IIR_INT_PEND;
            s->irq_status |= (1u << port);
            pcibase_update_irq(s);
        }
        break;
    case UART_TX_BURST_FIFO:
        if (size == 4) {
            uint32_t word = (uint32_t)val;
            for (int i = 0; i < 4; i++) {
                uint8_t b = (word >> (8 * i)) & 0xFF;
                pcibase_fifo_push(s->port_tx_fifo[port], &s->port_tx_head[port], &s->port_tx_tail[port], b);
            }
            s->port_burst_status[port] |= UART_BST_STAT_LSR_THRE;
            {
                uint32_t count = FIELD_GET(UART_BST_STAT_TX_COUNT_MASK, s->port_burst_status[port]);
                uint32_t add = 4;
                if (count + add > FIELD_MAX(UART_BST_STAT_TX_COUNT_MASK)) {
                    count = FIELD_MAX(UART_BST_STAT_TX_COUNT_MASK);
                } else {
                    count += add;
                }
                s->port_burst_status[port] &= ~UART_BST_STAT_TX_COUNT_MASK;
                s->port_burst_status[port] |= FIELD_PREP(UART_BST_STAT_TX_COUNT_MASK, count);
            }
            s->port_burst_status[port] |= UART_BST_STAT_IIR_INT_PEND;
            s->irq_status |= (1u << port);
            pcibase_update_irq(s);
        }
        break;
    case UART_FIFO_CTL:
        if (size == 1) {
            /* Overrun clear uses UART_LSR_OVERRUN_ERR_CLR */
            if (((uint8_t)val & UART_LSR_OVERRUN_ERR_CLR) == UART_LSR_OVERRUN_ERR_CLR) {
                s->port_burst_status[port] &= ~UART_BST_STAT_LSR_OVERRUN_ERR;
            }
        }
        break;
    case UART_MODEM_CTL_REG:
        if (size == 1) {
            s->port_modem_ctl[port] = (uint8_t)val;
        }
        break;
    case UART_ACTV_REG:
        if (size == 1) {
            s->port_actv[port] = (uint8_t)val;
        }
        break;
    case UART_WAKE_REG:
        if (size == 1 || size == 4) {
            s->sys_reg_wake = (uint8_t)val;
        }
        break;
    case UART_WAKE_MASK_REG:
        if (size == 1 || size == 4) {
            s->sys_reg_wake_mask = (uint8_t)val;
        }
        break;
    case ADCL_CFG_REG:
        if (size == 4) {
            s->port_adcl_cfg[port] = (uint32_t)val;
        }
        break;
    case FRAC_DIV_CFG_REG:
        if (size == 4) {
            s->port_frac_div_cfg[port] = (uint32_t)val;
        }
        break;
    case UART_BAUD_CLK_DIVISOR_REG:
        if (size == 4) {
            s->port_baud_clk_div[port] = (uint32_t)val;
        }
        break;
    default:
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* The driver never uses PIO; all access is MMIO via pci_ioremap_bar.
     * We therefore return zero for all I/O port reads.
     */
    (void)opaque;
    (void)addr;
    (void)size;
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* PIO is unused by the driver; ignore writes. */
    (void)opaque;
    (void)addr;
    (void)val;
    (void)size;
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

    s->sys_reg_dev_rev = s->dev_rev;
    s->sys_reg_syslock = 0;
    s->sys_reg_reset = 0;
    s->sys_reg_pci_ctrl = 0;
    s->sys_reg_wake = UART_WAKE_SRCS;
    s->sys_reg_wake_mask = UART_WAKE_N_PIN;

    for (int i = 0; i < MAX_PORTS; i++) {
        s->port_adcl_cfg[i] = 0;
        s->port_frac_div_cfg[i] = 0;
        s->port_baud_clk_div[i] = 0;
        s->port_modem_ctl[i] = 0;
        s->port_line_status[i] = 0;
        s->port_actv[i] = 0;
        /* Initialize burst status with some TX/RX capacity and no errors */
        s->port_burst_status[i] = 0;
        s->port_burst_status[i] |= FIELD_PREP(UART_BST_STAT_RX_COUNT_MASK, 16);
        s->port_burst_status[i] |= FIELD_PREP(UART_BST_STAT_TX_COUNT_MASK, 16);
        s->port_rx_head[i] = s->port_rx_tail[i] = 0;
        s->port_tx_head[i] = s->port_tx_tail[i] = 0;
    }

    s->irq_status = 0;
    pcibase_update_irq(s);
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCIBASE_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCIBASE_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCIBASE_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0xC0); /* at least C0 to enable burst mode */
    s->dev_rev = 0xC0;
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* Setup BAR0 as MMIO space: include UART_SYSTEM_ADDR_BASE and per-port regions. */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = UART_SYSTEM_ADDR_BASE + 0x200 + (hwaddr)PORT_OFFSET * MAX_PORTS;
    s->bar_info[0].name = "pci1xxxx-mmio";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize interrupt capabilities: enable legacy + MSI if desired. */
    Error *local_err = NULL;
    if (msi_init(pdev, 0, 1, true, false, &local_err) == 0) {
        s->has_msi = true;
    } else {
        s->has_msi = false;
        error_free(local_err);
    }

    s->has_msix = false;

    pcibase_reset(DEVICE(pdev));
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    (void)s;
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

static const VMStateDescription vmstate_pcibase = {
    .name = "pci1xxxx_serial_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT8(dev_rev, PCIBaseState),
        VMSTATE_UINT32(sys_reg_dev_rev, PCIBaseState),
        VMSTATE_UINT32(sys_reg_syslock, PCIBaseState),
        VMSTATE_UINT32(sys_reg_reset, PCIBaseState),
        VMSTATE_UINT32(sys_reg_pci_ctrl, PCIBaseState),
        VMSTATE_UINT8(sys_reg_wake, PCIBaseState),
        VMSTATE_UINT8(sys_reg_wake_mask, PCIBaseState),
        VMSTATE_UINT32_ARRAY(port_adcl_cfg, PCIBaseState, MAX_PORTS),
        VMSTATE_UINT32_ARRAY(port_frac_div_cfg, PCIBaseState, MAX_PORTS),
        VMSTATE_UINT32_ARRAY(port_baud_clk_div, PCIBaseState, MAX_PORTS),
        VMSTATE_UINT8_ARRAY(port_modem_ctl, PCIBaseState, MAX_PORTS),
        VMSTATE_UINT8_ARRAY(port_line_status, PCIBaseState, MAX_PORTS),
        VMSTATE_UINT8_ARRAY(port_actv, PCIBaseState, MAX_PORTS),
        VMSTATE_UINT32_ARRAY(port_burst_status, PCIBaseState, MAX_PORTS),
        VMSTATE_UINT32(irq_status, PCIBaseState),
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
