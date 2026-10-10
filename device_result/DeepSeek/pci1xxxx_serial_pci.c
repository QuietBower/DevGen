/*
 * Virtual device model for PCI1xxxx Serial Port
 * Based on Linux driver: drivers/tty/serial/8250/8250_pci1xxxx.c
 * QEMU 8.2.10 compatible.
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

/* Vendor & Device IDs from driver */
#define PCI_VENDOR_ID_EFAR     0x1055
#define PCI_DEVICE_ID_EFAR_PCI11010  0xa012
#define DEVICE_ID_VAL          PCI_DEVICE_ID_EFAR_PCI11010
#define CLASS_ID               0x0700 /* Communication controller subclass: serial 16550 */
#define BAR0_SIZE              0x10000
#define MSI_VECTOR_COUNT       4

/* From driver: PCI_DEVICE_ID_EFAR_* */
#define PCI_DEVICE_ID_EFAR_PCI11101  0xa022
#define PCI_DEVICE_ID_EFAR_PCI11400  0xa032
#define PCI_DEVICE_ID_EFAR_PCI11414  0xa042

/* Subsystem device IDs */
#define PCI_SUBDEVICE_ID_EFAR_PCI1XXXX_4p   0x0001
#define PCI_SUBDEVICE_ID_EFAR_PCI1XXXX_3p012 0x0002
#define PCI_SUBDEVICE_ID_EFAR_PCI1XXXX_3p013 0x0003
#define PCI_SUBDEVICE_ID_EFAR_PCI1XXXX_3p023 0x0004
#define PCI_SUBDEVICE_ID_EFAR_PCI1XXXX_3p123 0x0005
#define PCI_SUBDEVICE_ID_EFAR_PCI1XXXX_2p01  0x0006
#define PCI_SUBDEVICE_ID_EFAR_PCI1XXXX_2p02  0x0007
#define PCI_SUBDEVICE_ID_EFAR_PCI1XXXX_2p03  0x0008
#define PCI_SUBDEVICE_ID_EFAR_PCI1XXXX_2p12  0x0009
#define PCI_SUBDEVICE_ID_EFAR_PCI1XXXX_2p13  0x000a
#define PCI_SUBDEVICE_ID_EFAR_PCI1XXXX_2p23  0x000b
#define PCI_SUBDEVICE_ID_EFAR_PCI1XXXX_1p0   0x000c
#define PCI_SUBDEVICE_ID_EFAR_PCI1XXXX_1p1   0x000d
#define PCI_SUBDEVICE_ID_EFAR_PCI1XXXX_1p2   0x000e
#define PCI_SUBDEVICE_ID_EFAR_PCI1XXXX_1p3   0x000f
#define PCI_SUBDEVICE_ID_EFAR_PCI11414       0x0010

/* Register layout as per driver */
#define UART_SYSTEM_ADDR_BASE                0x1000
#define UART_DEV_REV_REG                     (UART_SYSTEM_ADDR_BASE + 0x00)
#define UART_DEV_REV_MASK                    0xFF
#define UART_SYSLOCK_REG                     (UART_SYSTEM_ADDR_BASE + 0xA0)
#define UART_SYSLOCK                         BIT(2)
#define SYSLOCK_SLEEP_TIMEOUT                100
#define SYSLOCK_RETRY_CNT                    1000

#define UART_RX_BYTE_FIFO                    0x00
#define UART_TX_BYTE_FIFO                    0x00
#define UART_FIFO_CTL                         0x02
#define UART_MODEM_CTL_REG                   0x04
#define UART_MODEM_CTL_RTS_SET               BIT(1)
#define UART_LINE_STAT_REG                   0x05
#define UART_LINE_XMIT_CHECK_MASK            GENMASK(6, 5)
#define UART_ACTV_REG                         0x11
#define UART_BLOCK_SET_ACTIVE                BIT(0)
#define UART_PCI_CTRL_REG                     0x80
#define UART_PCI_CTRL_SET_MULTIPLE_MSI       BIT(4)
#define UART_PCI_CTRL_D3_CLK_ENABLE          BIT(0)
#define ADCL_CFG_REG                          0x40
#define ADCL_CFG_POL_SEL                     BIT(2)
#define ADCL_CFG_PIN_SEL                     BIT(1)
#define ADCL_CFG_EN                          BIT(0)
#define UART_BIT_SAMPLE_CNT_8               8
#define UART_BIT_SAMPLE_CNT_16              16
#define BAUD_CLOCK_DIV_INT_MSK               GENMASK(31, 8)
#define ADCL_CFG_RTS_DELAY_MASK              GENMASK(11, 8)
#define FRAC_DIV_TX_END_POINT_MASK           GENMASK(23, 20)
#define UART_WAKE_REG                         0x8C
#define UART_WAKE_MASK_REG                    0x90
#define UART_WAKE_N_PIN                      BIT(2)
#define UART_WAKE_NCTS                       BIT(1)
#define UART_WAKE_INT                        BIT(0)
#define UART_WAKE_SRCS                       (UART_WAKE_N_PIN | UART_WAKE_NCTS | UART_WAKE_INT)
#define UART_BAUD_CLK_DIVISOR_REG             0x54
#define FRAC_DIV_CFG_REG                      0x58
#define UART_RESET_REG                        0x94
#define UART_RESET_D3_RESET_DISABLE          BIT(16)
#define UART_RESET_HOT_RESET_DISABLE         BIT(17)
#define UART_BURST_STATUS_REG                0x9C
#define UART_TX_BURST_FIFO                    0xA0
#define UART_RX_BURST_FIFO                    0xA4
#define UART_BIT_DIVISOR_8                   0x26731000
#define UART_BIT_DIVISOR_16                  0x6ef71000
#define UART_BAUD_4MBPS                       4000000
#define PORT_OFFSET                           0x100
#define UART_BYTE_SIZE                        1
#define UART_BURST_SIZE                       4
#define UART_BST_STAT_RX_COUNT_MASK          0x00FF
#define UART_BST_STAT_TX_COUNT_MASK          0xFF00
#define UART_BST_STAT_IIR_INT_PEND           0x100000
#define UART_LSR_OVERRUN_ERR_CLR             0x43
#define UART_BST_STAT_LSR_RX_MASK            0x9F000000
#define UART_BST_STAT_LSR_RX_ERR_MASK        0x9E000000
#define UART_BST_STAT_LSR_OVERRUN_ERR        0x2000000
#define UART_BST_STAT_LSR_PARITY_ERR         0x4000000
#define UART_BST_STAT_LSR_FRAME_ERR          0x8000000
#define UART_BST_STAT_LSR_THRE               0x20000000

#define MAX_PORTS                            4

/* Standard 8250 register offsets */
#define UART_RBR 0
#define UART_THR 0
#define UART_IER 1
#define UART_FCR 2
#define UART_LCR 3
#define UART_MCR 4
#define UART_LSR 5
#define UART_MSR 6
#define UART_SCR 7
#define UART_DLL 0 /* when DLAB=1 */
#define UART_DLH 1 /* when DLAB=1 */

/* Default TX FIFO empty count (128 bytes) */
#define TX_FIFO_SIZE 128

/* Per-port UART register bank */
typedef struct {
    uint8_t regs[256];
    bool active; /* controlled via UART_ACTV_REG */
    uint32_t burst_status;
} PCI1xxxxPort;

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];
    int num_bars;
    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Per-port banks */
    PCI1xxxxPort port[MAX_PORTS];
    int num_ports;

    /* Global registers */
    uint32_t pci_ctrl;
    uint32_t reset_reg;
    uint32_t dev_rev;
    uint32_t syslock;
    uint16_t subsys_id; /* subsystem device ID */
};

/* Forward declaration */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

/* Read from per-port register file (little-endian) */
static uint64_t port_reg_read(PCI1xxxxPort *p, hwaddr offset, unsigned size)
{
    uint64_t val = 0;
    if (offset + size > 256) {
        return 0;
    }
    memcpy(&val, &p->regs[offset], size);
    return val;
}

/* Write to per-port register file (little-endian) */
static void port_reg_write(PCI1xxxxPort *p, hwaddr offset, uint64_t val, unsigned size)
{
    if (offset + size > 256) {
        return;
    }
    memcpy(&p->regs[offset], &val, size);
}

/* Get port index from address (0 .. 0x3FF) */
static int port_index_from_addr(hwaddr addr)
{
    return addr / PORT_OFFSET;
}

/* Read system register (base + 0x80, 0x94, system area) */
static uint64_t system_reg_read(PCIBaseState *s, hwaddr addr, unsigned size)
{
    if (addr == UART_PCI_CTRL_REG) {
        return s->pci_ctrl;
    } else if (addr == UART_RESET_REG) {
        return s->reset_reg;
    } else if (addr >= UART_SYSTEM_ADDR_BASE && addr < UART_SYSTEM_ADDR_BASE + 0x1000) {
        hwaddr sys_off = addr - UART_SYSTEM_ADDR_BASE;
        if (sys_off == (UART_DEV_REV_REG - UART_SYSTEM_ADDR_BASE)) {
            return s->dev_rev;
        } else if (sys_off == (UART_SYSLOCK_REG - UART_SYSTEM_ADDR_BASE)) {
            return s->syslock;
        }
    }
    return 0;
}

/* Write system register */
static void system_reg_write(PCIBaseState *s, hwaddr addr, uint64_t val, unsigned size)
{
    if (addr == UART_PCI_CTRL_REG) {
        s->pci_ctrl = val & 0xFF;
    } else if (addr == UART_RESET_REG) {
        s->reset_reg = val;
    } else if (addr >= UART_SYSTEM_ADDR_BASE && addr < UART_SYSTEM_ADDR_BASE + 0x1000) {
        hwaddr sys_off = addr - UART_SYSTEM_ADDR_BASE;
        if (sys_off == (UART_SYSLOCK_REG - UART_SYSTEM_ADDR_BASE)) {
            /* Write UART_SYSLOCK bit to set/clear lock */
            if (val & UART_SYSLOCK) {
                s->syslock = UART_SYSLOCK;
            } else {
                s->syslock = 0;
            }
        }
    }
}

/* Per-port MMIO read with special register handling */
static uint64_t port_mmio_read(PCIBaseState *s, int port_idx, hwaddr offset, unsigned size)
{
    PCI1xxxxPort *p = &s->port[port_idx];
    uint64_t val;

    switch (offset) {
    case UART_BURST_STATUS_REG:
        if (size == 4) {
            return p->burst_status;
        }
        break;
    case UART_LINE_STAT_REG:
        /* LSR: always report TX empty, no errors */
        val = port_reg_read(p, offset, 1); /* read byte */
        val |= 0x60; /* THRE and TEMT */
        return val;
    case UART_MSR:
        /* MSR: ensure no modem status changes */
        val = port_reg_read(p, offset, 1);
        return val;
    default:
        /* Read raw register bytes */
        return port_reg_read(p, offset, size);
    }
    return port_reg_read(p, offset, size);
}

/* Per-port MMIO write with special register handling */
static void port_mmio_write(PCIBaseState *s, int port_idx, hwaddr offset, uint64_t val, unsigned size)
{
    PCI1xxxxPort *p = &s->port[port_idx];

    switch (offset) {
    case UART_ACTV_REG:
        if (size == 4 && (val & UART_BLOCK_SET_ACTIVE)) {
            p->active = true;
        }
        port_reg_write(p, offset, val, size);
        break;
    case UART_FIFO_CTL:
        port_reg_write(p, offset, val, size);
        /* If clearing overrun, no other action */
        break;
    case UART_MODEM_CTL_REG:
        port_reg_write(p, offset, val, size);
        break;
    case UART_LINE_STAT_REG:
        /* LSR is read-only, ignore writes */
        break;
    default:
        port_reg_write(p, offset, val, size);
        break;
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    /* Global registers at base 0x80, 0x94 and system area 0x1000+ */
    if ((addr >= UART_PCI_CTRL_REG && addr < UART_SYSTEM_ADDR_BASE) || addr >= UART_SYSTEM_ADDR_BASE) {
        return system_reg_read(s, addr, size);
    }

    /* Per-port area: 0x00 - 0x3FF (max 4 ports) */
    if (addr < (PORT_OFFSET * MAX_PORTS)) {
        int port_idx = port_index_from_addr(addr);
        hwaddr port_offset = addr % PORT_OFFSET;
        return port_mmio_read(s, port_idx, port_offset, size);
    }
    return -1ull;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if ((addr >= UART_PCI_CTRL_REG && addr < UART_SYSTEM_ADDR_BASE) || addr >= UART_SYSTEM_ADDR_BASE) {
        system_reg_write(s, addr, val, size);
        return;
    }

    if (addr < (PORT_OFFSET * MAX_PORTS)) {
        int port_idx = port_index_from_addr(addr);
        hwaddr port_offset = addr % PORT_OFFSET;
        port_mmio_write(s, port_idx, port_offset, val, size);
        return;
    }
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Reset per-port registers and burst status */
    for (int i = 0; i < MAX_PORTS; i++) {
        memset(s->port[i].regs, 0, sizeof(s->port[i].regs));
        s->port[i].active = false;
        s->port[i].burst_status = UART_BST_STAT_IIR_INT_PEND |
                                  UART_BST_STAT_LSR_THRE |
                                  (TX_FIFO_SIZE << 8);
    }

    /* Reset global registers */
    s->pci_ctrl = 0;
    s->reset_reg = 0;
    s->dev_rev = 0xC0; /* Revision for burst mode */
    s->syslock = 0;
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;
    int num_ports;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_EFAR);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID_VAL);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0xC0);
    pci_config_set_interrupt_pin(pci_conf, 0x01);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_VENDOR_ID, PCI_VENDOR_ID_EFAR);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_ID, s->subsys_id);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    switch (s->subsys_id) {
    case PCI_SUBDEVICE_ID_EFAR_PCI1XXXX_4p:
    case PCI_SUBDEVICE_ID_EFAR_PCI11414:
        num_ports = 4;
        break;
    case PCI_SUBDEVICE_ID_EFAR_PCI1XXXX_3p012:
    case PCI_SUBDEVICE_ID_EFAR_PCI1XXXX_3p123:
    case PCI_SUBDEVICE_ID_EFAR_PCI1XXXX_3p013:
    case PCI_SUBDEVICE_ID_EFAR_PCI1XXXX_3p023:
        num_ports = 3;
        break;
    case PCI_SUBDEVICE_ID_EFAR_PCI1XXXX_2p01:
    case PCI_SUBDEVICE_ID_EFAR_PCI1XXXX_2p02:
    case PCI_SUBDEVICE_ID_EFAR_PCI1XXXX_2p03:
    case PCI_SUBDEVICE_ID_EFAR_PCI1XXXX_2p12:
    case PCI_SUBDEVICE_ID_EFAR_PCI1XXXX_2p13:
    case PCI_SUBDEVICE_ID_EFAR_PCI1XXXX_2p23:
        num_ports = 2;
        break;
    default:
        num_ports = 1;
        break;
    }
    s->num_ports = num_ports;

    /* Initialize BAR0 */
    MemoryRegion *bar0 = &s->bar_regions[0];
    memory_region_init_io(bar0, OBJECT(s), &pcibase_mmio_ops, s, "pci1xxxx-mmio", BAR0_SIZE);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, bar0);

    /* MSI support: allocate up to num_ports vectors */
    if (msi_init(pdev, 0, num_ports, true, false, errp)) {
        return;
    }
    s->has_msi = true;

    /* Default reset state */
    pcibase_reset(DEVICE(s));
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

static Property pcibase_properties[] = {
    DEFINE_PROP_UINT16("subsys_id", PCIBaseState, subsys_id, PCI_SUBDEVICE_ID_EFAR_PCI1XXXX_4p),
    DEFINE_PROP_END_OF_LIST(),
};

static const VMStateDescription vmstate_pcibase_port = {
    .name = "pci1xxxx_port",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8_ARRAY(regs, PCI1xxxxPort, 256),
        VMSTATE_BOOL(active, PCI1xxxxPort),
        VMSTATE_UINT32(burst_status, PCI1xxxxPort),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_pcibase = {
    .name = TYPE_PCIBASE_DEVICE,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_STRUCT_ARRAY(port, PCIBaseState, MAX_PORTS, 0, vmstate_pcibase_port, PCI1xxxxPort),
        VMSTATE_INT32(num_ports, PCIBaseState),
        VMSTATE_UINT32(pci_ctrl, PCIBaseState),
        VMSTATE_UINT32(reset_reg, PCIBaseState),
        VMSTATE_UINT32(dev_rev, PCIBaseState),
        VMSTATE_UINT32(syslock, PCIBaseState),
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
    device_class_set_props(dc, pcibase_properties);
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
