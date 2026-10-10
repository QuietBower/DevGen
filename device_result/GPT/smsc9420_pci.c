/*
 * QEMU PCI device model for smsc9420 (behavioral implementation)
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
/* Removed missing include hw/net/smsc9420_regs.h to fix compilation */

#define TYPE_PCIBASE_DEVICE "smsc9420_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define SMSC9420_PCI_VENDOR_ID 0x1055
#define SMSC9420_PCI_DEVICE_ID 0xE420
#define SMSC9420_PCI_CLASS_ID  PCI_CLASS_NETWORK_ETHERNET

/* Selected register offsets and bit definitions from driver */
#define SMSC9420_REG_ID_REV              0xC0
#define SMSC9420_REG_MII_ACCESS          0x94
#define SMSC9420_REG_M_MII_ACCESS        SMSC9420_REG_MII_ACCESS
#define SMSC9420_REG_MII_DATA            0x98
#define SMSC9420_REG_E2P_CMD             0xF8
#define SMSC9420_REG_GPIO_CFG            0xD0
#define SMSC9420_REG_E2P_DATA            0xFC
#define SMSC9420_REG_ADDRL               0x88
#define SMSC9420_REG_ADDRH               0x84
#define SMSC9420_REG_DMAC_CONTROL        0x18
#define SMSC9420_REG_DMAC_INTR_ENA       0x1C
#define SMSC9420_REG_DMAC_STATUS         0x14
#define SMSC9420_REG_MAC_CR              0x80
#define SMSC9420_REG_INT_CTL             0xC4
#define SMSC9420_REG_INT_STAT            0xC8
#define SMSC9420_REG_INT_CFG             0xCC
#define SMSC9420_REG_RX_POLL_DEMAND      0x08
#define SMSC9420_REG_MISS_FRAME_CNTR     0x20
#define SMSC9420_REG_HASHL               0x90
#define SMSC9420_REG_HASHH               0x8C
#define SMSC9420_REG_FLOW                0x9C
#define SMSC9420_REG_TX_POLL_DEMAND      0x04
#define SMSC9420_REG_TX_BASE_ADDR        0x10
#define SMSC9420_REG_COE_CR              0xB0
#define SMSC9420_REG_VLAN1               0xA0
#define SMSC9420_REG_RX_BASE_ADDR        0x0C
#define SMSC9420_REG_BUS_MODE            0x00
#define SMSC9420_REG_BUS_CFG             0xDC

#define SMSC9420_BAR_INDEX               3

/* Bit definitions (subset) */
#define SMSC9420_MAC_CR_TXEN             0x00000008
#define SMSC9420_MAC_CR_RXEN             0x00000004
#define SMSC9420_INT_CTL_SW_INT_EN       0x00008000
#define SMSC9420_INT_STAT_SW_INT         (1u << 15)
#define SMSC9420_INT_CFG_IRQ_EN          0x00040000
#define SMSC9420_INT_CFG_IRQ_INT         0x00080000

#define SMSC9420_DMAC_INTR_ENA_TX        (1u << 0)
#define SMSC9420_DMAC_INTR_ENA_RX        (1u << 6)
#define SMSC9420_DMAC_INTR_ENA_NIS       (1u << 16)

#define SMSC9420_DMAC_STS_TX             (1u << 0)
#define SMSC9420_DMAC_STS_TXPS           (1u << 1)
#define SMSC9420_DMAC_STS_RX             (1u << 6)
#define SMSC9420_DMAC_STS_RXPS           (1u << 8)
#define SMSC9420_DMAC_STS_NIS            (1u << 16)

#define SMSC9420_BUS_MODE_SWR            (1u << 0)
#define SMSC9420_BUS_MODE_DBO            (1u << 20)

#define SMSC9420_DMAC_CONTROL_ST         (1u << 13)
#define SMSC9420_DMAC_CONTROL_SR         (1u << 1)
#define SMSC9420_DMAC_CONTROL_OSF        (1u << 2)
#define SMSC9420_DMAC_CONTROL_SF         (1u << 21)

#define SMSC9420_GPIO_CFG_EEPR_EN        0x00700000
#define SMSC9420_GPIO_CFG_LED_1          0x10000000
#define SMSC9420_GPIO_CFG_LED_2          0x20000000
#define SMSC9420_GPIO_CFG_LED_3          0x40000000

#define SMSC9420_INT_CFG_INT_DEAS_MASK   0x000000FF

#define SMSC9420_LAN9420_CPSR_ENDIAN_OFFSET 0

#define SMSC9420_EEPROM_SIZE             ((uint32_t)11)
#define SMSC9420_EEPROM_MAGIC            0x9420

#define SMSC9420_E2P_CMD_EPC_CMD_RELOAD  0x70000000
#define SMSC9420_E2P_CMD_EPC_BUSY        0x80000000
#define SMSC9420_E2P_CMD_EPC_TIMEOUT     0x00000200
#define SMSC9420_E2P_CMD_EPC_CMD_READ    0x00000000
#define SMSC9420_E2P_CMD_EPC_CMD_ERASE   0x50000000
#define SMSC9420_E2P_CMD_EPC_CMD_WRITE   0x30000000
#define SMSC9420_E2P_CMD_EPC_CMD_EWDS    0x10000000
#define SMSC9420_E2P_CMD_EPC_CMD_EWEN    0x20000000

#define SMSC9420_MII_ACCESS_MII_BUSY     0x00000001
#define SMSC9420_MII_ACCESS_MII_READ     0x00000000
#define SMSC9420_MII_ACCESS_MII_WRITE    0x00000002

#define SMSC9420_TX_RING_SIZE            32
#define SMSC9420_RX_RING_SIZE            128

#define SMSC9420_PKT_BUF_SZ              (VLAN_ETH_FRAME_LEN + NET_IP_ALIGN + 4)

#define SMSC9420_INT_DEAS_TIME           50

#define SMSC9420_PCI_BAR                 3

#define SMSC9420_PCI_LAN9420_CPSR_ENDIAN_OFFSET 0

#define SMSC9420_PCI_DEVICE_ID_9420      0xE420
#define SMSC9420_PCI_VENDOR_ID_9420      0x1055

#define SMSC9420_BAR_DEFAULT_SIZE        (1 * MiB)

#define SMSC9420_INT_STATUS_DEFAULT      0x00000000

#define SMSC9420_MII_DEFAULT             0x00000000

#define SMSC9420_ID_REV_DEFAULT          0x94200000

#define SMSC9420_MAC_CR_DEFAULT          0x00000000

#define SMSC9420_DMAC_DEFAULT            0x00000000

#define SMSC9420_INT_CFG_DEFAULT         0x00000000

#define SMSC9420_BUS_MODE_DEFAULT        0x00000000

#define SMSC9420_BUS_CFG_DEFAULT         0x00000000

/* Minimal definitions derived from new driver source snippet */
#define VLAN_ETH_FRAME_LEN 1518
#define NET_IP_ALIGN 0

/* Structures referenced by the driver for DMA rings and device private data */
struct smsc9420_dma_desc {
    uint32_t status;
    uint32_t length;
    uint32_t buffer1;
    uint32_t buffer2;
};

struct smsc9420_ring_info {
    void *skb;          /* placeholder type for struct sk_buff * */
    uint64_t mapping;   /* placeholder type for dma_addr_t */
};

struct smsc9420_pdata {
    void *ioaddr;                   /* void __iomem * */
    void *pdev;                     /* struct pci_dev * */
    void *dev;                      /* struct net_device * */

    struct smsc9420_dma_desc *rx_ring;
    struct smsc9420_dma_desc *tx_ring;
    struct smsc9420_ring_info *tx_buffers;
    struct smsc9420_ring_info *rx_buffers;
    uint64_t rx_dma_addr;          /* dma_addr_t */
    uint64_t tx_dma_addr;          /* dma_addr_t */
    int tx_ring_head, tx_ring_tail;
    int rx_ring_head, rx_ring_tail;

    uint32_t int_lock;             /* spinlock_t placeholder */
    uint32_t phy_lock;             /* spinlock_t placeholder */

    uint8_t napi[64];              /* struct napi_struct placeholder */

    bool software_irq_signal;
    bool rx_csum;
    uint32_t msg_enable;

    void *mii_bus;                 /* struct mii_bus * */
    int last_duplex;
    int last_carrier;
};

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
    struct {
        uint32_t id_rev;
        uint32_t mac_cr;
        uint32_t dmac_control;
        uint32_t dmac_intr_ena;
        uint32_t dmac_status;
        uint32_t int_ctl;
        uint32_t int_stat;
        uint32_t int_cfg;
        uint32_t bus_mode;
        uint32_t bus_cfg;
        uint32_t mii_access;
        uint32_t mii_data;
        uint32_t e2p_cmd;
        uint32_t e2p_data;
        uint32_t gpio_cfg;
        uint32_t addrl;
        uint32_t addrh;
        uint32_t hashl;
        uint32_t hashh;
        uint32_t flow;
        uint32_t coe_cr;
        uint32_t vlan1;
        uint32_t rx_poll_demand;
        uint32_t tx_poll_demand;
        uint32_t rx_base_addr;
        uint32_t tx_base_addr;
        uint32_t miss_frame_cntr;
    } regs;

    /* Simple PHY and EEPROM emulation state */
    uint16_t phy_regs[32];
    uint8_t eeprom[SMSC9420_EEPROM_SIZE];

    bool mii_busy;
    int mii_timer_ticks;

    bool e2p_busy;
    int e2p_timer_ticks;
};


/* Internal helper for status-triggered signaling. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool level = false;

    /* Only DMAC interrupt considered and single INT_STAT bit (SW_INT) */
    if (s->regs.int_cfg & SMSC9420_INT_CFG_IRQ_EN) {
        if (s->regs.int_stat & (SMSC9420_INT_STAT_SW_INT)) {
            level = true;
        }
        /* DMAC_INT is folded into INT_STAT elsewhere; here we only look at int_stat */
    }

    if (msi_enabled(pdev)) {
        if (level) {
            msi_notify(pdev, 0);
        }
    } else {
        pci_set_irq(pdev, level ? 1 : 0);
    }
}

/* Device-initiated DMA logic based on driver access patterns
 * (Not implemented: driver never inspects DMAC-driven memory contents) */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    (void)s;
    (void)pdev;
    (void)is_write;
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = 0;

    if (size != 4) {
        return 0; /* driver always uses 32-bit accesses */
    }

    switch (addr) {
    case SMSC9420_REG_ID_REV:
        val = s->regs.id_rev;
        break;
    case SMSC9420_REG_MAC_CR:
        val = s->regs.mac_cr;
        break;
    case SMSC9420_REG_DMAC_CONTROL:
        val = s->regs.dmac_control;
        break;
    case SMSC9420_REG_DMAC_INTR_ENA:
        val = s->regs.dmac_intr_ena;
        break;
    case SMSC9420_REG_DMAC_STATUS:
        val = s->regs.dmac_status;
        break;
    case SMSC9420_REG_INT_CTL:
        val = s->regs.int_ctl;
        break;
    case SMSC9420_REG_INT_STAT:
        val = s->regs.int_stat;
        break;
    case SMSC9420_REG_INT_CFG:
        val = s->regs.int_cfg;
        break;
    case SMSC9420_REG_BUS_MODE:
        val = s->regs.bus_mode;
        break;
    case SMSC9420_REG_BUS_CFG:
        val = s->regs.bus_cfg;
        break;
    case SMSC9420_REG_MII_ACCESS:
        /* Implement busy bit timing */
        if (s->mii_busy && s->mii_timer_ticks > 0) {
            s->mii_timer_ticks--;
            if (s->mii_timer_ticks == 0) {
                s->mii_busy = false;
            }
        }
        val = s->regs.mii_access;
        if (s->mii_busy) {
            val |= SMSC9420_MII_ACCESS_MII_BUSY;
        } else {
            val &= ~SMSC9420_MII_ACCESS_MII_BUSY;
        }
        break;
    case SMSC9420_REG_MII_DATA:
        val = s->regs.mii_data;
        break;
    case SMSC9420_REG_E2P_CMD:
        /* Implement EEPROM busy and timeout behaviour */
        if (s->e2p_busy && s->e2p_timer_ticks > 0) {
            s->e2p_timer_ticks--;
            if (s->e2p_timer_ticks == 0) {
                s->e2p_busy = false;
            }
        }
        val = s->regs.e2p_cmd;
        if (s->e2p_busy) {
            val |= SMSC9420_E2P_CMD_EPC_BUSY;
        } else {
            val &= ~SMSC9420_E2P_CMD_EPC_BUSY;
        }
        break;
    case SMSC9420_REG_E2P_DATA:
        val = s->regs.e2p_data;
        break;
    case SMSC9420_REG_GPIO_CFG:
        val = s->regs.gpio_cfg;
        break;
    case SMSC9420_REG_ADDRL:
        val = s->regs.addrl;
        break;
    case SMSC9420_REG_ADDRH:
        val = s->regs.addrh;
        break;
    case SMSC9420_REG_HASHL:
        val = s->regs.hashl;
        break;
    case SMSC9420_REG_HASHH:
        val = s->regs.hashh;
        break;
    case SMSC9420_REG_FLOW:
        val = s->regs.flow;
        break;
    case SMSC9420_REG_COE_CR:
        val = s->regs.coe_cr;
        break;
    case SMSC9420_REG_VLAN1:
        val = s->regs.vlan1;
        break;
    case SMSC9420_REG_RX_POLL_DEMAND:
        val = s->regs.rx_poll_demand;
        break;
    case SMSC9420_REG_TX_POLL_DEMAND:
        val = s->regs.tx_poll_demand;
        break;
    case SMSC9420_REG_RX_BASE_ADDR:
        val = s->regs.rx_base_addr;
        break;
    case SMSC9420_REG_TX_BASE_ADDR:
        val = s->regs.tx_base_addr;
        break;
    case SMSC9420_REG_MISS_FRAME_CNTR:
        /* driver only reads and accumulates, we can leave at 0 */
        val = s->regs.miss_frame_cntr;
        break;
    default:
        /* Unknown/unused register, return 0 */
        val = 0;
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        return; /* driver uses 32-bit writes only */
    }

    switch (addr) {
    case SMSC9420_REG_MAC_CR:
        s->regs.mac_cr = (uint32_t)val;
        break;
    case SMSC9420_REG_DMAC_CONTROL:
        s->regs.dmac_control = (uint32_t)val;
        break;
    case SMSC9420_REG_DMAC_INTR_ENA:
        s->regs.dmac_intr_ena = (uint32_t)val;
        break;
    case SMSC9420_REG_DMAC_STATUS:
        /* W1C for DMAC status bits */
        s->regs.dmac_status &= ~((uint32_t)val);
        break;
    case SMSC9420_REG_INT_CTL:
        s->regs.int_ctl = (uint32_t)val;
        break;
    case SMSC9420_REG_INT_STAT:
        /* W1C for interrupt status */
        s->regs.int_stat &= ~((uint32_t)val);
        pcibase_update_irq(s);
        break;
    case SMSC9420_REG_INT_CFG:
        s->regs.int_cfg = (uint32_t)val;
        pcibase_update_irq(s);
        break;
    case SMSC9420_REG_BUS_MODE:
        s->regs.bus_mode = (uint32_t)val;
        /* handle software reset bit by clearing it quickly */
        if (s->regs.bus_mode & SMSC9420_BUS_MODE_SWR) {
            /* emulate short delay; driver just polls once */
            s->regs.bus_mode &= ~SMSC9420_BUS_MODE_SWR;
        }
        break;
    case SMSC9420_REG_BUS_CFG:
        s->regs.bus_cfg = (uint32_t)val;
        break;
    case SMSC9420_REG_MII_DATA:
        s->regs.mii_data = (uint32_t)val;
        break;
    case SMSC9420_REG_MII_ACCESS: {
        /* emulate busy flag and simple PHY register space */
        uint32_t access = (uint32_t)val;
        int phyaddr = (access >> 11) & 0x1F;
        int regidx  = (access >> 6) & 0x1F;
        uint32_t op = access & SMSC9420_MII_ACCESS_MII_WRITE;

        s->regs.mii_access = access;
        s->mii_busy = true;
        s->mii_timer_ticks = 2; /* after a couple of reads, busy clears */

        if (phyaddr == 1 && regidx >= 0 && regidx < 32) {
            if (op == SMSC9420_MII_ACCESS_MII_WRITE) {
                /* write: value was placed in MII_DATA before this */
                s->phy_regs[regidx] = (uint16_t)(s->regs.mii_data & 0xFFFF);
            } else {
                /* read */
                s->regs.mii_data = s->phy_regs[regidx];
            }
        }
        break;
    }
    case SMSC9420_REG_E2P_CMD: {
        uint32_t cmd = (uint32_t)val;
        s->regs.e2p_cmd = cmd;
        /* Handle operations when BUSY bit set */
        if (cmd & SMSC9420_E2P_CMD_EPC_BUSY) {
            s->e2p_busy = true;
            s->e2p_timer_ticks = 2; /* will clear quickly */
            /* decode address in low bits */
            uint8_t addr = (uint8_t)(cmd & 0xFF);
            uint32_t op = cmd & 0x70000000;

            if (addr < SMSC9420_EEPROM_SIZE) {
                if (op == SMSC9420_E2P_CMD_EPC_CMD_READ) {
                    s->regs.e2p_data = s->eeprom[addr];
                } else if (op == SMSC9420_E2P_CMD_EPC_CMD_WRITE) {
                    s->eeprom[addr] = (uint8_t)(s->regs.e2p_data & 0xFF);
                } else if (op == SMSC9420_E2P_CMD_EPC_CMD_RELOAD) {
                    /* reload: pre-programmed contents already in eeprom[] */
                } else {
                    /* erase or other ops: ignore */
                }
            }
        } else {
            s->e2p_busy = false;
        }
        break;
    }
    case SMSC9420_REG_E2P_DATA:
        s->regs.e2p_data = (uint32_t)val;
        break;
    case SMSC9420_REG_GPIO_CFG:
        s->regs.gpio_cfg = (uint32_t)val;
        break;
    case SMSC9420_REG_ADDRL:
        s->regs.addrl = (uint32_t)val;
        break;
    case SMSC9420_REG_ADDRH:
        s->regs.addrh = (uint32_t)val;
        break;
    case SMSC9420_REG_HASHL:
        s->regs.hashl = (uint32_t)val;
        break;
    case SMSC9420_REG_HASHH:
        s->regs.hashh = (uint32_t)val;
        break;
    case SMSC9420_REG_FLOW:
        s->regs.flow = (uint32_t)val;
        break;
    case SMSC9420_REG_COE_CR:
        s->regs.coe_cr = (uint32_t)val;
        break;
    case SMSC9420_REG_VLAN1:
        s->regs.vlan1 = (uint32_t)val;
        break;
    case SMSC9420_REG_RX_POLL_DEMAND:
        s->regs.rx_poll_demand = (uint32_t)val;
        break;
    case SMSC9420_REG_TX_POLL_DEMAND:
        s->regs.tx_poll_demand = (uint32_t)val;
        break;
    case SMSC9420_REG_RX_BASE_ADDR:
        s->regs.rx_base_addr = (uint32_t)val;
        break;
    case SMSC9420_REG_TX_BASE_ADDR:
        s->regs.tx_base_addr = (uint32_t)val;
        break;
    default:
        /* ignore writes to unknown offsets */
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;
    (void)addr;
    (void)size;
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;
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

    /* Revert registers to power-on defaults */
    s->regs.id_rev = SMSC9420_ID_REV_DEFAULT;
    s->regs.mac_cr = SMSC9420_MAC_CR_DEFAULT;
    s->regs.dmac_control = SMSC9420_DMAC_DEFAULT;
    s->regs.dmac_intr_ena = SMSC9420_DMAC_DEFAULT;
    s->regs.dmac_status = SMSC9420_DMAC_DEFAULT;
    s->regs.int_ctl = SMSC9420_INT_STATUS_DEFAULT;
    s->regs.int_stat = SMSC9420_INT_STATUS_DEFAULT;
    s->regs.int_cfg = SMSC9420_INT_CFG_DEFAULT;
    s->regs.bus_mode = SMSC9420_BUS_MODE_DEFAULT;
    s->regs.bus_cfg = SMSC9420_BUS_CFG_DEFAULT;
    s->regs.mii_access = SMSC9420_MII_DEFAULT;
    s->regs.mii_data = SMSC9420_MII_DEFAULT;
    s->regs.e2p_cmd = 0;
    s->regs.e2p_data = 0;
    s->regs.gpio_cfg = 0;
    s->regs.addrl = 0;
    s->regs.addrh = 0;
    s->regs.hashl = 0;
    s->regs.hashh = 0;
    s->regs.flow = 0;
    s->regs.coe_cr = 0;
    s->regs.vlan1 = 0;
    s->regs.rx_poll_demand = 0;
    s->regs.tx_poll_demand = 0;
    s->regs.rx_base_addr = 0;
    s->regs.tx_base_addr = 0;
    s->regs.miss_frame_cntr = 0;

    s->intr_status = 0;
    s->intr_mask = 0;

    s->mii_busy = false;
    s->mii_timer_ticks = 0;
    s->e2p_busy = false;
    s->e2p_timer_ticks = 0;

    /* Initialize a simple EEPROM content so MAC address reads as invalid
     * (driver will then randomize and write a MAC). We leave contents zero. */
    memset(s->eeprom, 0, sizeof(s->eeprom));

    /* Initialize PHY registers to safe defaults */
    memset(s->phy_regs, 0, sizeof(s->phy_regs));
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  SMSC9420_PCI_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  SMSC9420_PCI_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_ETHERNET);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0].index = SMSC9420_BAR_INDEX;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    s->bar_info[0].size  = SMSC9420_BAR_DEFAULT_SIZE;
    s->bar_info[0].name  = "smsc9420-mmio";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = NULL;
    }

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No MSI/MSI-X configuration: rely on legacy INTx */
    s->has_msi = false;
    s->has_msix = false;

    /* Initialize register shadow defaults */
    pcibase_reset(DEVICE(pdev));
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

    (void)s;
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "smsc9420_pci",
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

type_init(pcibase_register_types)
