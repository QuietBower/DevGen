/*
 * Realtek 8139C+ (8139cp) QEMU PCI Device Model
 * 
 * Based on Linux driver: drivers/net/ethernet/realtek/8139cp.c
 * Target QEMU: 8.2.10
 */

#include "qemu/osdep.h"
#include <inttypes.h>
#include <string.h>
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "sysemu/dma.h"
#include "sysemu/reset.h"
#include "hw/irq.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/pci/pcie.h"
#include "qom/object.h"
#include "qapi/visitor.h"
#include "hw/qdev-properties.h"
#include "net/net.h"
#include "net/eth.h"

#define TYPE_PCIBASE_DEVICE "8139cp_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI IDs */
#ifndef PCI_VENDOR_ID_REALTEK
#define PCI_VENDOR_ID_REALTEK 0x10ec
#endif
#ifndef PCI_DEVICE_ID_REALTEK_8139
#define PCI_DEVICE_ID_REALTEK_8139 0x8139
#endif

/* Constants from driver */
#define CP_REGS_SIZE        0x100
#define CP_RX_RING_SIZE     64
#define CP_TX_RING_SIZE     64
#define CP_STATS_SIZE       64

/* Register Offsets */
#define REG_MAC0        0x00
#define REG_MAR0        0x08
#define REG_TXSTATUS0   0x10
#define REG_TXADDR0     0x20
#define REG_RXBUF       0x30
#define REG_CMD         0x37
#define REG_RXBUFPTR    0x38
#define REG_RXBUFADDR   0x3A
#define REG_INTRMASK    0x3C
#define REG_INTRSTATUS  0x3E
#define REG_TXCONFIG    0x40
#define REG_RXCONFIG    0x44
#define REG_TIMER       0x48
#define REG_RXMISSED    0x4C
#define REG_CFG9346     0x50
#define REG_CONFIG1     0x52
#define REG_CONFIG3     0x59
#define REG_CONFIG5     0xD8
#define REG_CPCMD       0xE0
#define REG_INTRMITIGATE 0xE2
#define REG_RXRINGADDR  0xE4
#define REG_TXRINGADDR  0xEC
#define REG_TXPOLL      0xD9

/* EEPROM Bits */
#define EE_SHIFT_CLK    0x04
#define EE_CS           0x08
#define EE_DATA_WRITE   0x02
#define EE_DATA_READ    0x01
#define EE_ENB          (0x80 | EE_CS)

/* Command Register Bits */
#define CmdReset        0x10
#define CmdRxOn         0x08
#define CmdTxOn         0x04

/* C+ Command Register Bits */
#define CpRxOn          0x0002
#define CpTxOn          0x0001

/* Descriptor Bits */
#define DescOwn         (1 << 31)
#define RingEnd         (1 << 30)
#define FirstFrag       (1 << 29)
#define LastFrag        (1 << 28)
#define TxError         (1 << 23)
#define TxOK            (1 << 15)
#define RxOK            (1 << 15)
#define RxError         (1 << 20)

/* Interrupt Status Bits */
enum IntrStatus {
    TxIdle = 0x40000,
    RxIdle = 0x20000,
    IntrSummary = 0x010000,
    PCIBusErr170 = 0x7000,
    PCIBusErr175 = 0x1000,
    PhyEvent175 = 0x8000,
    RxStarted = 0x0800,
    RxEarlyWarn = 0x0400,
    CntFull = 0x0200,
    TxUnderrun = 0x0100,
    TxEmpty = 0x0080,
    TxDone = 0x0020,
    IntrRxError = 0x0010, /* Renamed to avoid conflict with RxError macro */
    RxOverflow = 0x0008,
    RxFull = 0x0004,
    RxHeader = 0x0002,
    RxDone = 0x0001,
};

/* BAR metadata definition */
typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM 
} BARType;

typedef struct {
    int index;
    BARType type;
    hwaddr size;
    const char *name;
    bool sparse;
} BARInfo;

/* Descriptor structure (in guest memory) */
struct cp_desc {
    uint32_t opts1;
    uint32_t opts2;
    uint64_t addr;
};

/* Device State */
struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    NICState *nic;
    NICConf conf;

    /* Registers */
    uint8_t mac_reg[6];
    uint8_t cmd;
    uint16_t cp_cmd;
    uint16_t intr_mask;
    uint16_t intr_status;
    uint32_t rx_config;
    uint32_t tx_config;
    uint8_t cfg9346;
    uint64_t rx_ring_addr;
    uint64_t tx_ring_addr;
    uint8_t tx_poll;

    /* EEPROM State */
    uint16_t eeprom_data[128];
    int eeprom_op;
    int eeprom_mode;
    int eeprom_phase;
    int eeprom_addr;
    int eeprom_bit;
    int eeprom_val;

    /* DMA State */
    uint32_t rx_head;
    uint32_t tx_tail;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    int level = (s->intr_status & s->intr_mask) ? 1 : 0;
    pci_set_irq(PCI_DEVICE(s), level);
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    
    s->cmd = 0;
    s->cp_cmd = 0;
    s->intr_mask = 0;
    s->intr_status = 0;
    s->rx_config = 0;
    s->tx_config = 0;
    s->cfg9346 = 0;
    s->rx_ring_addr = 0;
    s->tx_ring_addr = 0;
    s->tx_poll = 0;
    s->rx_head = 0;
    s->tx_tail = 0;

    /* EEPROM Reset */
    s->eeprom_phase = 0;
    s->eeprom_op = 0;
    s->eeprom_addr = 0;
    s->eeprom_bit = 0;

    /* Load MAC from conf to registers */
    memcpy(s->mac_reg, s->conf.macaddr.a, 6);

    pcibase_update_irq(s);
}

static void pcibase_eeprom_reset(PCIBaseState *s)
{
    s->eeprom_phase = 0;
    s->eeprom_op = 0;
    s->eeprom_addr = 0;
    s->eeprom_bit = 0;
}

static void pcibase_eeprom_write(PCIBaseState *s, uint8_t val)
{
    /* Minimal 93C46 emulation for MAC read */
    bool cs = !!(val & EE_CS);
    bool clk = !!(val & EE_SHIFT_CLK);
    bool di = !!(val & EE_DATA_WRITE);
    
    if (!cs) {
        pcibase_eeprom_reset(s);
        return;
    }

    /* Rising edge of CLK shifts data in */
    if (clk && !(s->cfg9346 & EE_SHIFT_CLK)) {
        if (s->eeprom_phase == 0) { /* Waiting for start bit */
            if (di) s->eeprom_phase = 1;
        } else if (s->eeprom_phase == 1) { /* Opcode + Address */
            s->eeprom_op = (s->eeprom_op << 1) | di;
            s->eeprom_bit++;
            /* 2 bits opcode + 6 bits address (for 93C46 in 16-bit mode) */
            if (s->eeprom_bit == 8) {
                int opcode = (s->eeprom_op >> 6) & 0x3;
                int addr = s->eeprom_op & 0x3F;
                if (opcode == 2) { /* READ */
                    s->eeprom_val = s->eeprom_data[addr];
                    s->eeprom_phase = 2;
                    s->eeprom_bit = 0;
                }
            }
        } else if (s->eeprom_phase == 2) { /* Reading data out */
            /* Clocking out happens on edges, handled in read */
        }
    }
    s->cfg9346 = val;
}

static uint8_t pcibase_eeprom_read(PCIBaseState *s)
{
    uint8_t ret = s->cfg9346 & ~EE_DATA_READ;
    
    if (s->eeprom_phase == 2) {
        /* MSB first */
        if (s->eeprom_val & (1 << (15 - s->eeprom_bit))) {
            ret |= EE_DATA_READ;
        }
    }
    return ret;
}

static void pcibase_process_tx(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    struct cp_desc desc;
    uint8_t buf[4096];
    int max_loop = CP_TX_RING_SIZE;

    while (max_loop--) {
        dma_addr_t desc_addr = s->tx_ring_addr + (s->tx_tail * sizeof(struct cp_desc));
        if (dma_memory_read(&pdev->bus_master_as, desc_addr, &desc, sizeof(desc), MEMTXATTRS_UNSPECIFIED)) {
            break;
        }

        uint32_t opts1 = le32_to_cpu(desc.opts1);
        if (!(opts1 & DescOwn)) {
            /* Owned by driver, stop */
            break;
        }

        uint32_t len = opts1 & 0x1FFF;
        uint64_t buf_addr = le64_to_cpu(desc.addr);

        if (len > sizeof(buf)) len = sizeof(buf);
        dma_memory_read(&pdev->bus_master_as, buf_addr, buf, len, MEMTXATTRS_UNSPECIFIED);

        /* Send packet */
        qemu_send_packet(qemu_get_queue(s->nic), buf, len);

        /* Update descriptor */
        opts1 &= ~DescOwn;
        opts1 |= TxOK | TxDone;
        desc.opts1 = cpu_to_le32(opts1);
        dma_memory_write(&pdev->bus_master_as, desc_addr, &desc, sizeof(desc), MEMTXATTRS_UNSPECIFIED);

        s->intr_status |= TxDone | TxOK;
        
        if (opts1 & RingEnd) {
            s->tx_tail = 0;
        } else {
            s->tx_tail++;
        }
    }
    pcibase_update_irq(s);
}

static ssize_t pcibase_receive(NetClientState *nc, const uint8_t *buf, size_t size)
{
    PCIBaseState *s = qemu_get_nic_opaque(nc);
    PCIDevice *pdev = PCI_DEVICE(s);
    struct cp_desc desc;
    int max_loop = CP_RX_RING_SIZE;

    if (!(s->cmd & CmdRxOn) || !(s->cp_cmd & CpRxOn)) {
        return -1;
    }

    while (max_loop--) {
        dma_addr_t desc_addr = s->rx_ring_addr + (s->rx_head * sizeof(struct cp_desc));
        if (dma_memory_read(&pdev->bus_master_as, desc_addr, &desc, sizeof(desc), MEMTXATTRS_UNSPECIFIED)) {
            return -1;
        }

        uint32_t opts1 = le32_to_cpu(desc.opts1);
        if (!(opts1 & DescOwn)) {
            /* Owned by driver (not ready for RX), drop packet */
            return -1;
        }

        uint64_t buf_addr = le64_to_cpu(desc.addr);
        uint32_t buf_len = opts1 & 0x1FFF;

        if (size > buf_len) {
            /* Packet too large, drop */
            return size;
        }

        dma_memory_write(&pdev->bus_master_as, buf_addr, buf, size, MEMTXATTRS_UNSPECIFIED);

        /* Update descriptor */
        opts1 &= ~DescOwn;
        opts1 |= RxOK | RxDone | FirstFrag | LastFrag;
        opts1 &= ~0x1FFF; /* Clear length */
        opts1 |= (size & 0x1FFF);
        
        desc.opts1 = cpu_to_le32(opts1);
        dma_memory_write(&pdev->bus_master_as, desc_addr, &desc, sizeof(desc), MEMTXATTRS_UNSPECIFIED);

        s->intr_status |= RxOK | RxDone;
        
        if (opts1 & RingEnd) {
            s->rx_head = 0;
        } else {
            s->rx_head++;
        }
        
        pcibase_update_irq(s);
        return size;
    }
    return 0;
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case REG_MAC0 ... REG_MAC0 + 5:
        val = s->mac_reg[addr - REG_MAC0];
        break;
    case REG_CMD:
        val = s->cmd;
        break;
    case REG_INTRMASK:
        val = s->intr_mask;
        break;
    case REG_INTRSTATUS:
        val = s->intr_status;
        break;
    case REG_CPCMD:
        val = s->cp_cmd;
        break;
    case REG_CFG9346:
        val = pcibase_eeprom_read(s);
        break;
    case REG_CONFIG1:
        val = 0xCF; /* PMEnable | DriverLoaded etc */
        break;
    case REG_CONFIG3:
        val = 0; 
        break;
    case REG_CONFIG5:
        val = 0;
        break;
    case REG_TXCONFIG:
        val = s->tx_config;
        break;
    case REG_RXCONFIG:
        val = s->rx_config;
        break;
    case REG_RXRINGADDR:
        val = s->rx_ring_addr & 0xFFFFFFFF;
        break;
    case REG_RXRINGADDR + 4:
        val = (s->rx_ring_addr >> 32) & 0xFFFFFFFF;
        break;
    case REG_TXRINGADDR:
        val = s->tx_ring_addr & 0xFFFFFFFF;
        break;
    case REG_TXRINGADDR + 4:
        val = (s->tx_ring_addr >> 32) & 0xFFFFFFFF;
        break;
    default:
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case REG_MAC0 ... REG_MAC0 + 5:
        s->mac_reg[addr - REG_MAC0] = val;
        break;
    case REG_CMD:
        s->cmd = val;
        if (val & CmdReset) {
            pcibase_reset(DEVICE(s));
            s->cmd &= ~CmdReset; /* Auto clear */
        }
        break;
    case REG_INTRMASK:
        s->intr_mask = val;
        pcibase_update_irq(s);
        break;
    case REG_INTRSTATUS:
        s->intr_status &= ~val; /* Write 1 to clear */
        pcibase_update_irq(s);
        break;
    case REG_CPCMD:
        s->cp_cmd = val;
        break;
    case REG_CFG9346:
        if (s->cfg9346 & EE_SHIFT_CLK && !(val & EE_SHIFT_CLK)) {
             /* Falling edge, advance bit counter if reading */
             if (s->eeprom_phase == 2) s->eeprom_bit++;
        }
        pcibase_eeprom_write(s, val);
        break;
    case REG_TXPOLL:
        s->tx_poll = val;
        pcibase_process_tx(s);
        break;
    case REG_RXRINGADDR:
        s->rx_ring_addr = (s->rx_ring_addr & 0xFFFFFFFF00000000ULL) | (val & 0xFFFFFFFF);
        break;
    case REG_RXRINGADDR + 4:
        s->rx_ring_addr = (s->rx_ring_addr & 0x00000000FFFFFFFFULL) | ((uint64_t)val << 32);
        break;
    case REG_TXRINGADDR:
        s->tx_ring_addr = (s->tx_ring_addr & 0xFFFFFFFF00000000ULL) | (val & 0xFFFFFFFF);
        break;
    case REG_TXRINGADDR + 4:
        s->tx_ring_addr = (s->tx_ring_addr & 0x00000000FFFFFFFFULL) | ((uint64_t)val << 32);
        break;
    case REG_TXCONFIG:
        s->tx_config = val;
        break;
    case REG_RXCONFIG:
        s->rx_config = val;
        break;
    default:
        break;
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE)
        return;

    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

static NetClientInfo net_pcibase_info = {
    .type = NET_CLIENT_DRIVER_NIC,
    .size = sizeof(NICState),
    .receive = pcibase_receive,
};

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_REALTEK);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_REALTEK_8139);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_ETHERNET);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x20); /* C+ mode requires >= 0x20 */
    pci_config_set_interrupt_pin(pci_conf, 1);
    
    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = CP_REGS_SIZE;
    s->bar_info[1].name = "8139cp-mmio";
    s->num_bars = 6;

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->nic = qemu_new_nic(&net_pcibase_info, &s->conf, 
                          object_get_typename(OBJECT(s)), pdev->qdev.id,
                          &pdev->qdev.mem_reentrancy_guard, s);
    qemu_format_nic_info_str(qemu_get_queue(s->nic), s->conf.macaddr.a);

    /* Initialize EEPROM with MAC address */
    /* 93C46 16-bit mode: Address 0 = 0x8129 (Signature) */
    s->eeprom_data[0] = 0x8129;
    /* MAC at 7,8,9 */
    uint16_t *mac = (uint16_t *)s->conf.macaddr.a;
    s->eeprom_data[7] = mac[0];
    s->eeprom_data[8] = mac[1];
    s->eeprom_data[9] = mac[2];
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    qemu_del_nic(s->nic);
}

static Property pcibase_properties[] = {
    DEFINE_NIC_PROPERTIES(PCIBaseState, conf),
    DEFINE_PROP_END_OF_LIST(),
};

static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pcibase_realize;
    k->exit    = pcibase_uninit;
    dc->reset  = pcibase_reset;
    device_class_set_props(dc, pcibase_properties);
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
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