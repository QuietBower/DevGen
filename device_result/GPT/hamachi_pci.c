/*
 * QEMU PCI device model for PacketEngines Hamachi NIC (for Linux hamachi driver)
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

#define TYPE_PCIBASE_DEVICE "hamachi_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI identifiers: from hamachi_pci_tbl first entry and chip_tbl */
#define HAMACHI_PCI_VENDOR_ID 0x1318
#define HAMACHI_PCI_DEVICE_ID 0x0911

/* Class: Ethernet controller */
#define HAMACHI_PCI_CLASS_ID PCI_CLASS_NETWORK_ETHERNET

/* Interrupt status bits (enum intr_status_bits) */
#define HAMACHI_INTR_RX_DONE         0x00000001
#define HAMACHI_INTR_RX_PCI_FAULT    0x00000002
#define HAMACHI_INTR_RX_PCI_ERR      0x00000004
#define HAMACHI_INTR_TX_DONE         0x00000100
#define HAMACHI_INTR_TX_PCI_FAULT    0x00000200
#define HAMACHI_INTR_TX_PCI_ERR      0x00000400
#define HAMACHI_INTR_LINK_CHANGE     0x00010000
#define HAMACHI_INTR_NEGOT_CHANGE    0x00020000
#define HAMACHI_INTR_STATS_MAX       0x00040000

/* Descriptor status bits (enum desc_status_bits) */
#define HAMACHI_DESC_OWN             0x80000000
#define HAMACHI_DESC_END_PACKET      0x40000000
#define HAMACHI_DESC_END_RING        0x20000000
#define HAMACHI_DESC_INTR            0x10000000

/* Capability flags (enum capability_flags) */
#define HAMACHI_CAN_HAVE_MII         0x00000001

/* Register offsets (enum hamachi_offsets) */
#define HAMACHI_REG_TX_DMA_CTRL              0x0000
#define HAMACHI_REG_TX_CMD                   0x0004
#define HAMACHI_REG_TX_STATUS                0x0006
#define HAMACHI_REG_TX_PTR                   0x0008
#define HAMACHI_REG_TX_CUR_PTR               0x0010
#define HAMACHI_REG_RX_DMA_CTRL              0x0020
#define HAMACHI_REG_RX_CMD                   0x0024
#define HAMACHI_REG_RX_STATUS                0x0026
#define HAMACHI_REG_RX_PTR                   0x0028
#define HAMACHI_REG_RX_CUR_PTR               0x0030
#define HAMACHI_REG_PCI_CLK_MEAS             0x0060
#define HAMACHI_REG_MISC_STATUS              0x0066
#define HAMACHI_REG_CHIP_REV                 0x0068
#define HAMACHI_REG_CHIP_RESET               0x006B
#define HAMACHI_REG_LED_CTRL                 0x006C
#define HAMACHI_REG_VIRTUAL_JUMPERS          0x006D
#define HAMACHI_REG_GPIO                     0x006E
#define HAMACHI_REG_TX_CHECKSUM              0x0074
#define HAMACHI_REG_RX_CHECKSUM              0x0076
#define HAMACHI_REG_TX_INTR_CTRL             0x0078
#define HAMACHI_REG_RX_INTR_CTRL             0x007C
#define HAMACHI_REG_INTERRUPT_ENABLE         0x0080
#define HAMACHI_REG_INTERRUPT_CLEAR          0x0084
#define HAMACHI_REG_INTR_STATUS              0x0088
#define HAMACHI_REG_EVENT_STATUS             0x008C
#define HAMACHI_REG_MAC_CNFG                 0x00A0
#define HAMACHI_REG_FRAME_GAP0               0x00A2
#define HAMACHI_REG_FRAME_GAP1               0x00A4
#define HAMACHI_REG_MAC_CNFG2                0x00B0
#define HAMACHI_REG_RX_DEPTH                 0x00B8
#define HAMACHI_REG_FLOW_CTRL                0x00BC
#define HAMACHI_REG_MAX_FRAME_SIZE           0x00CE
#define HAMACHI_REG_ADDR_MODE                0x00D0
#define HAMACHI_REG_STATION_ADDR             0x00D2
#define HAMACHI_REG_AN_CTRL                  0x00E0
#define HAMACHI_REG_AN_STATUS                0x00E2
#define HAMACHI_REG_AN_XCHNG_CTRL            0x00E4
#define HAMACHI_REG_AN_ADVERTISE             0x00E8
#define HAMACHI_REG_AN_LINK_PARTNER_ABILITY  0x00EA
#define HAMACHI_REG_EE_CMD_STATUS            0x00F0
#define HAMACHI_REG_EE_DATA                  0x00F1
#define HAMACHI_REG_EE_ADDR                  0x00F2
#define HAMACHI_REG_FIFO_CFG                 0x00F8

/* MII register offsets (enum MII_offsets) */
#define HAMACHI_REG_MII_CMD                  0x00A6
#define HAMACHI_REG_MII_ADDR                 0x00A8
#define HAMACHI_REG_MII_WR_DATA              0x00AA
#define HAMACHI_REG_MII_RD_DATA              0x00AC
#define HAMACHI_REG_MII_STATUS               0x00AE

/* Additional offsets actually used by the driver but not named above */
#define HAMACHI_CAM_BASE                     0x0100

/* Ring sizes and related constants */
#define HAMACHI_TX_RING_SIZE   64
#define HAMACHI_RX_RING_SIZE   512
#define HAMACHI_MAX_UNITS      8
#define HAMACHI_MAX_FRAME_SIZE 1518

#define HAMACHI_BAR0_INDEX 0
#define HAMACHI_BAR0_SIZE  0x1000


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

    /* Hardware Register Shadows */
    uint16_t tx_dma_ctrl;
    uint16_t tx_cmd;
    uint16_t tx_status;
    uint32_t tx_ptr;
    uint32_t tx_cur_ptr;
    uint16_t rx_dma_ctrl;
    uint16_t rx_cmd;
    uint16_t rx_status;
    uint32_t rx_ptr;
    uint32_t rx_cur_ptr;
    uint8_t  pci_clk_meas;
    uint16_t misc_status;
    uint32_t chip_rev;
    uint8_t  chip_reset;
    uint8_t  led_ctrl;
    uint8_t  virtual_jumpers;
    uint8_t  gpio;
    uint16_t tx_checksum;
    uint16_t rx_checksum;
    uint32_t tx_intr_ctrl;
    uint32_t rx_intr_ctrl;
    uint32_t interrupt_enable;
    uint32_t interrupt_clear;
    uint32_t event_status;
    uint16_t mac_cnfg;
    uint16_t frame_gap0;
    uint16_t frame_gap1;
    uint16_t mac_cnfg2;
    uint16_t rx_depth;
    uint32_t flow_ctrl;
    uint16_t max_frame_size;
    uint16_t addr_mode;
    uint8_t  station_addr[6];
    uint16_t an_ctrl;
    uint16_t an_status;
    uint16_t an_xchng_ctrl;
    uint16_t an_advertise;
    uint16_t an_link_partner_ability;
    uint8_t  ee_cmd_status;
    uint8_t  ee_data;
    uint16_t ee_addr;
    uint16_t fifo_cfg;
    uint16_t mii_cmd;
    uint16_t mii_addr;
    uint16_t mii_wr_data;
    uint16_t mii_rd_data;
    uint16_t mii_status;

    /* Simple EEPROM emulation storage: 256 bytes */
    uint8_t eeprom[256];

    /* DMA Context: descriptor ring base addresses */
    uint64_t tx_ring_dma;
    uint64_t rx_ring_dma;
    uint32_t tx_ring_size;
    uint32_t rx_ring_size;

    bool link_up;
    bool in_reset;
    uint8_t power_state;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t pending = s->intr_status & s->intr_mask;

    if (pending) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* MMIO/PIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case HAMACHI_REG_TX_DMA_CTRL:
        val = s->tx_dma_ctrl;
        break;
    case HAMACHI_REG_TX_CMD:
        val = s->tx_cmd;
        break;
    case HAMACHI_REG_TX_STATUS:
        val = s->tx_status;
        break;
    case HAMACHI_REG_TX_PTR:
        val = s->tx_ptr;
        break;
    case HAMACHI_REG_TX_CUR_PTR:
        val = s->tx_cur_ptr;
        break;
    case HAMACHI_REG_RX_DMA_CTRL:
        val = s->rx_dma_ctrl;
        break;
    case HAMACHI_REG_RX_CMD:
        val = s->rx_cmd;
        break;
    case HAMACHI_REG_RX_STATUS:
        val = s->rx_status;
        break;
    case HAMACHI_REG_RX_PTR:
        val = s->rx_ptr;
        break;
    case HAMACHI_REG_RX_CUR_PTR:
        val = s->rx_cur_ptr;
        break;
    case HAMACHI_REG_PCI_CLK_MEAS:
        val = s->pci_clk_meas;
        break;
    case HAMACHI_REG_MISC_STATUS:
        val = s->misc_status;
        break;
    case HAMACHI_REG_CHIP_REV:
        val = s->chip_rev;
        break;
    case HAMACHI_REG_CHIP_RESET:
        val = s->chip_reset;
        break;
    case HAMACHI_REG_LED_CTRL:
        val = s->led_ctrl;
        break;
    case HAMACHI_REG_VIRTUAL_JUMPERS:
        val = s->virtual_jumpers;
        break;
    case HAMACHI_REG_GPIO:
        val = s->gpio;
        break;
    case HAMACHI_REG_TX_CHECKSUM:
        val = s->tx_checksum;
        break;
    case HAMACHI_REG_RX_CHECKSUM:
        val = s->rx_checksum;
        break;
    case HAMACHI_REG_TX_INTR_CTRL:
        val = s->tx_intr_ctrl;
        break;
    case HAMACHI_REG_RX_INTR_CTRL:
        val = s->rx_intr_ctrl;
        break;
    case HAMACHI_REG_INTERRUPT_ENABLE:
        val = s->interrupt_enable;
        break;
    case HAMACHI_REG_INTERRUPT_CLEAR:
        /* driver reads InterruptClear to get intr_status */
        val = s->intr_status;
        break;
    case HAMACHI_REG_INTR_STATUS:
        val = s->intr_status;
        break;
    case HAMACHI_REG_EVENT_STATUS:
        val = s->event_status;
        break;
    case HAMACHI_REG_MAC_CNFG:
        val = s->mac_cnfg;
        break;
    case HAMACHI_REG_FRAME_GAP0:
        val = s->frame_gap0;
        break;
    case HAMACHI_REG_FRAME_GAP1:
        val = s->frame_gap1;
        break;
    case HAMACHI_REG_MAC_CNFG2:
        val = s->mac_cnfg2;
        break;
    case HAMACHI_REG_RX_DEPTH:
        val = s->rx_depth;
        break;
    case HAMACHI_REG_FLOW_CTRL:
        val = s->flow_ctrl;
        break;
    case HAMACHI_REG_MAX_FRAME_SIZE:
        val = s->max_frame_size;
        break;
    case HAMACHI_REG_ADDR_MODE:
        val = s->addr_mode;
        break;
    case HAMACHI_REG_STATION_ADDR + 0:
        val = s->station_addr[0];
        break;
    case HAMACHI_REG_STATION_ADDR + 1:
        val = s->station_addr[1];
        break;
    case HAMACHI_REG_STATION_ADDR + 2:
        val = s->station_addr[2];
        break;
    case HAMACHI_REG_STATION_ADDR + 3:
        val = s->station_addr[3];
        break;
    case HAMACHI_REG_STATION_ADDR + 4:
        val = s->station_addr[4];
        break;
    case HAMACHI_REG_STATION_ADDR + 5:
        val = s->station_addr[5];
        break;
    case HAMACHI_REG_AN_CTRL:
        val = s->an_ctrl;
        break;
    case HAMACHI_REG_AN_STATUS:
        val = s->an_status;
        break;
    case HAMACHI_REG_AN_XCHNG_CTRL:
        val = s->an_xchng_ctrl;
        break;
    case HAMACHI_REG_AN_ADVERTISE:
        val = s->an_advertise;
        break;
    case HAMACHI_REG_AN_LINK_PARTNER_ABILITY:
        val = s->an_link_partner_ability;
        break;
    case HAMACHI_REG_EE_CMD_STATUS:
        val = s->ee_cmd_status;
        break;
    case HAMACHI_REG_EE_DATA:
        val = s->ee_data;
        break;
    case HAMACHI_REG_EE_ADDR:
        val = s->ee_addr;
        break;
    case HAMACHI_REG_FIFO_CFG:
        val = s->fifo_cfg;
        break;
    case HAMACHI_REG_MII_CMD:
        val = s->mii_cmd;
        break;
    case HAMACHI_REG_MII_ADDR:
        val = s->mii_addr;
        break;
    case HAMACHI_REG_MII_WR_DATA:
        val = s->mii_wr_data;
        break;
    case HAMACHI_REG_MII_RD_DATA:
        val = s->mii_rd_data;
        break;
    case HAMACHI_REG_MII_STATUS:
        val = s->mii_status;
        break;
    default:
        /* CAM and statistics area: no behavior, return 0 */
        val = 0;
        break;
    }

    /* Mask to access size */
    if (size == 1) {
        val &= 0xff;
    } else if (size == 2) {
        val &= 0xffff;
    } else if (size == 4) {
        val &= 0xffffffffu;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* normalize val to size */
    if (size == 1) {
        val &= 0xff;
    } else if (size == 2) {
        val &= 0xffff;
    } else if (size == 4) {
        val &= 0xffffffffu;
    }

    switch (addr) {
    case HAMACHI_REG_TX_DMA_CTRL:
        s->tx_dma_ctrl = val;
        break;
    case HAMACHI_REG_TX_CMD:
        s->tx_cmd = val;
        /* driver writes 1 to start, 2 to stop; we do not simulate DMA */
        break;
    case HAMACHI_REG_TX_STATUS:
        s->tx_status = val;
        break;
    case HAMACHI_REG_TX_PTR:
        s->tx_ptr = val;
        s->tx_ring_dma = val;
        break;
    case HAMACHI_REG_TX_CUR_PTR:
        s->tx_cur_ptr = val;
        break;
    case HAMACHI_REG_RX_DMA_CTRL:
        s->rx_dma_ctrl = val;
        break;
    case HAMACHI_REG_RX_CMD:
        s->rx_cmd = val;
        break;
    case HAMACHI_REG_RX_STATUS:
        s->rx_status = val;
        break;
    case HAMACHI_REG_RX_PTR:
        s->rx_ptr = val;
        s->rx_ring_dma = val;
        break;
    case HAMACHI_REG_RX_CUR_PTR:
        s->rx_cur_ptr = val;
        break;
    case HAMACHI_REG_PCI_CLK_MEAS:
        /* read-only measurement in real hw, ignore writes */
        break;
    case HAMACHI_REG_MISC_STATUS:
        s->misc_status = val;
        break;
    case HAMACHI_REG_CHIP_REV:
        /* read-only */
        break;
    case HAMACHI_REG_CHIP_RESET:
        s->chip_reset = val;
        /* driver writes 0x01 to reset; we don't alter other state here */
        break;
    case HAMACHI_REG_LED_CTRL:
        s->led_ctrl = val;
        break;
    case HAMACHI_REG_VIRTUAL_JUMPERS:
        s->virtual_jumpers = val;
        break;
    case HAMACHI_REG_GPIO:
        s->gpio = val;
        break;
    case HAMACHI_REG_TX_CHECKSUM:
        s->tx_checksum = val;
        break;
    case HAMACHI_REG_RX_CHECKSUM:
        s->rx_checksum = val;
        break;
    case HAMACHI_REG_TX_INTR_CTRL:
        s->tx_intr_ctrl = val;
        break;
    case HAMACHI_REG_RX_INTR_CTRL:
        s->rx_intr_ctrl = val;
        break;
    case HAMACHI_REG_INTERRUPT_ENABLE:
        s->interrupt_enable = val;
        s->intr_mask = val;
        pcibase_update_irq(s);
        break;
    case HAMACHI_REG_INTERRUPT_CLEAR:
        /* driver does not explicitly write InterruptClear; if it did, we could W1C */
        s->interrupt_clear = val;
        break;
    case HAMACHI_REG_INTR_STATUS:
        /* treat as write-1-to-clear for intr_status */
        s->intr_status &= ~((uint32_t)val);
        pcibase_update_irq(s);
        break;
    case HAMACHI_REG_EVENT_STATUS:
        s->event_status = val;
        break;
    case HAMACHI_REG_MAC_CNFG:
        s->mac_cnfg = val;
        break;
    case HAMACHI_REG_FRAME_GAP0:
        s->frame_gap0 = val;
        break;
    case HAMACHI_REG_FRAME_GAP1:
        s->frame_gap1 = val;
        break;
    case HAMACHI_REG_MAC_CNFG2:
        s->mac_cnfg2 = val;
        break;
    case HAMACHI_REG_RX_DEPTH:
        s->rx_depth = val;
        break;
    case HAMACHI_REG_FLOW_CTRL:
        s->flow_ctrl = val;
        break;
    case HAMACHI_REG_MAX_FRAME_SIZE:
        s->max_frame_size = val;
        break;
    case HAMACHI_REG_ADDR_MODE:
        s->addr_mode = val;
        break;
    case HAMACHI_REG_STATION_ADDR + 0:
        s->station_addr[0] = val;
        break;
    case HAMACHI_REG_STATION_ADDR + 1:
        s->station_addr[1] = val;
        break;
    case HAMACHI_REG_STATION_ADDR + 2:
        s->station_addr[2] = val;
        break;
    case HAMACHI_REG_STATION_ADDR + 3:
        s->station_addr[3] = val;
        break;
    case HAMACHI_REG_STATION_ADDR + 4:
        s->station_addr[4] = val;
        break;
    case HAMACHI_REG_STATION_ADDR + 5:
        s->station_addr[5] = val;
        break;
    case HAMACHI_REG_AN_CTRL:
        s->an_ctrl = val;
        break;
    case HAMACHI_REG_AN_STATUS:
        s->an_status = val;
        break;
    case HAMACHI_REG_AN_XCHNG_CTRL:
        s->an_xchng_ctrl = val;
        break;
    case HAMACHI_REG_AN_ADVERTISE:
        s->an_advertise = val;
        break;
    case HAMACHI_REG_AN_LINK_PARTNER_ABILITY:
        s->an_link_partner_ability = val;
        break;
    case HAMACHI_REG_EE_CMD_STATUS:
        /* bit 6 is busy; driver writes 0x02 to start read */
        s->ee_cmd_status = val;
        if ((val & 0x02) && !(s->ee_cmd_status & 0x40)) {
            /* start EEPROM read: set busy, load data, then clear busy */
            s->ee_cmd_status |= 0x40;
            s->ee_data = s->eeprom[s->ee_addr & 0xff];
            s->ee_cmd_status &= ~0x40;
        }
        break;
    case HAMACHI_REG_EE_DATA:
        s->ee_data = val;
        break;
    case HAMACHI_REG_EE_ADDR:
        s->ee_addr = val;
        break;
    case HAMACHI_REG_FIFO_CFG:
        s->fifo_cfg = val;
        break;
    case HAMACHI_REG_MII_CMD:
        s->mii_cmd = val;
        break;
    case HAMACHI_REG_MII_ADDR:
        s->mii_addr = val;
        break;
    case HAMACHI_REG_MII_WR_DATA:
        s->mii_wr_data = val;
        break;
    case HAMACHI_REG_MII_RD_DATA:
        s->mii_rd_data = val;
        break;
    case HAMACHI_REG_MII_STATUS:
        s->mii_status = val;
        break;
    default:
        /* CAM programming and statistics: ignore */
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    (void)s;
    (void)addr;
    (void)size;
    return val;
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

    s->tx_dma_ctrl = 0;
    s->tx_cmd = 0;
    s->tx_status = 0;
    s->tx_ptr = 0;
    s->tx_cur_ptr = 0;
    s->rx_dma_ctrl = 0;
    s->rx_cmd = 0;
    s->rx_status = 0;
    s->rx_ptr = 0;
    s->rx_cur_ptr = 0;
    s->pci_clk_meas = 0x80; /* bit 7 set to indicate valid, arbitrary freq */
    s->misc_status = 0; /* bit0 0->32bit bus; driver only reads */
    s->chip_rev = 0x00000001;
    s->chip_reset = 0;
    s->led_ctrl = 0;
    s->virtual_jumpers = 0;
    s->gpio = 0;
    s->tx_checksum = 0;
    s->rx_checksum = 0;
    s->tx_intr_ctrl = 0;
    s->rx_intr_ctrl = 0;
    s->interrupt_enable = 0;
    s->interrupt_clear = 0;
    s->intr_status = 0;
    s->event_status = 0;
    s->mac_cnfg = 0;
    s->frame_gap0 = 0;
    s->frame_gap1 = 0;
    s->mac_cnfg2 = 0;
    s->rx_depth = 0;
    s->flow_ctrl = 0;
    s->max_frame_size = HAMACHI_MAX_FRAME_SIZE;
    s->addr_mode = 0;
    memset(s->station_addr, 0, sizeof(s->station_addr));
    s->an_ctrl = 0;
    s->an_status = 0;
    s->an_xchng_ctrl = 0;
    s->an_advertise = 0;
    s->an_link_partner_ability = 0;
    s->ee_cmd_status = 0;
    s->ee_data = 0;
    s->ee_addr = 0;
    s->fifo_cfg = 0;
    s->mii_cmd = 0;
    s->mii_addr = 0;
    s->mii_wr_data = 0;
    s->mii_rd_data = 0;
    s->mii_status = 0;
    s->tx_ring_dma = 0;
    s->rx_ring_dma = 0;
    s->tx_ring_size = HAMACHI_TX_RING_SIZE;
    s->rx_ring_size = HAMACHI_RX_RING_SIZE;
    s->link_up = false;
    s->in_reset = false;
    s->power_state = 0;
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  HAMACHI_PCI_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  HAMACHI_PCI_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, HAMACHI_PCI_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    s->num_bars = 1;
    s->bar_info[0].index = HAMACHI_BAR0_INDEX;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    s->bar_info[0].size  = HAMACHI_BAR0_SIZE;
    s->bar_info[0].name  = "hamachi-bar0";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = "";
    }
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = false;
    s->has_msix = false;

    s->intr_status = 0;
    s->intr_mask = 0;
    s->tx_ring_size = HAMACHI_TX_RING_SIZE;
    s->rx_ring_size = HAMACHI_RX_RING_SIZE;
    s->link_up = false;
    s->in_reset = false;
    s->power_state = 0;

    /* Initialize fake EEPROM contents: MAC address bytes at locations 4..9 */
    for (int i = 0; i < 256; i++) {
        s->eeprom[i] = 0;
    }
    s->eeprom[4] = 0x52;
    s->eeprom[5] = 0x54;
    s->eeprom[6] = 0x00;
    s->eeprom[7] = 0x12;
    s->eeprom[8] = 0x34;
    s->eeprom[9] = 0x56;

    /* Provide sane default autoneg values so that hamachi_timer prints useful data */
    s->an_status = 0x0020; /* link up bit that driver tests */
    s->an_link_partner_ability = 0x0001;
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

static const VMStateDescription vmstate_pcibase = {
    .name = "hamachi_pci",
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
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
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

