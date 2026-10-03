/*
 * QEMU device model for Agere ET131x Ethernet controller
 * Based on Linux driver et131x.c
 * QEMU 8.2.10 compatible
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

#define TYPE_PCIBASE_DEVICE "et131x_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Vendor and Device IDs: first entry from pci_device_id table */
#define VENDOR_ID        0x0000  /* TODO: need ATT vendor ID */
#define DEVICE_ID        0xED00
#define CLASS_ID         0x020000  /* Ethernet controller */
#define ET131X_BAR0_SIZE 0x8000   /* BAR0 size 32KB */

/* PCI configuration space custom registers */
#define PCI_CONF_MAC_ADDR         0xA4
#define PCI_CONF_LBCIF_ADDRESS     0xAC
#define PCI_CONF_LBCIF_DATA        0xB0
#define PCI_CONF_LBCIF_CONTROL     0xB1
#define PCI_CONF_EEPROM_STATUS     0xB2
#define PCI_CONF_ACK_NACK          0xC0
#define PCI_CONF_REPLAY            0xC2
#define PCI_CONF_L0L1LATENCY       0xCF

/* Register block base offsets (from BAR0) */
#define GLOBAL_REGS_BASE  0x0000
#define TXDMA_REGS_BASE   0x1000
#define RXDMA_REGS_BASE   0x2000
#define TXMAC_REGS_BASE   0x3000
#define RXMAC_REGS_BASE   0x4000
#define MAC_REGS_BASE     0x5000
#define MACSTAT_REGS_BASE 0x6000
#define MMC_REGS_BASE     0x7000

/* Global register offsets */
#define REG_GLOBAL_TXQ_START_ADDR   0x0000
#define REG_GLOBAL_TXQ_END_ADDR     0x0004
#define REG_GLOBAL_RXQ_START_ADDR   0x0008
#define REG_GLOBAL_RXQ_END_ADDR     0x000C
#define REG_GLOBAL_PM_CSR           0x0010
#define REG_GLOBAL_INT_STATUS       0x0018
#define REG_GLOBAL_INT_MASK         0x001C
#define REG_GLOBAL_INT_ALIAS_CLR_EN 0x0020
#define REG_GLOBAL_INT_STATUS_ALIAS 0x0024
#define REG_GLOBAL_SW_RESET         0x0028
#define REG_GLOBAL_SLV_TIMER        0x002C
#define REG_GLOBAL_MSI_CONFIG       0x0030
#define REG_GLOBAL_LOOPBACK         0x0034
#define REG_GLOBAL_WATCHDOG_TIMER   0x0038

/* TXDMA register offsets (relative to TXDMA_REGS_BASE) */
#define REG_TXDMA_CSR                    0x1000
#define REG_TXDMA_PR_BASE_HI             0x1004
#define REG_TXDMA_PR_BASE_LO             0x1008
#define REG_TXDMA_PR_NUM_DES             0x100C
#define REG_TXDMA_TXQ_WR_ADDR            0x1010
#define REG_TXDMA_TXQ_WR_ADDR_EXT        0x1014
#define REG_TXDMA_TXQ_RD_ADDR            0x1018
#define REG_TXDMA_DMA_WB_BASE_HI         0x101C
#define REG_TXDMA_DMA_WB_BASE_LO         0x1020
#define REG_TXDMA_SERVICE_REQUEST        0x1024
#define REG_TXDMA_SERVICE_COMPLETE       0x1028
#define REG_TXDMA_CACHE_RD_INDEX         0x102C
#define REG_TXDMA_CACHE_WR_INDEX         0x1030
#define REG_TXDMA_TX_DMA_ERROR           0x1034
#define REG_TXDMA_DESC_ABORT_CNT         0x1038
#define REG_TXDMA_PAYLOAD_ABORT_CNT      0x103C
#define REG_TXDMA_WRITEBACK_ABORT_CNT    0x1040
#define REG_TXDMA_DESC_TIMEOUT_CNT       0x1044
#define REG_TXDMA_PAYLOAD_TIMEOUT_CNT    0x1048
#define REG_TXDMA_WRITEBACK_TIMEOUT_CNT  0x104C
#define REG_TXDMA_DESC_ERROR_CNT         0x1050
#define REG_TXDMA_PAYLOAD_ERROR_CNT      0x1054
#define REG_TXDMA_WRITEBACK_ERROR_CNT    0x1058
#define REG_TXDMA_DROPPED_TLP_CNT        0x105C
#define REG_TXDMA_NEW_SERVICE_COMPLETE   0x1060
#define REG_TXDMA_ETHERNET_PACKET_CNT    0x1064

/* RXDMA register offsets (relative to RXDMA_REGS_BASE) */
#define REG_RXDMA_CSR                   0x2000
#define REG_RXDMA_DMA_WB_BASE_HI        0x2004
#define REG_RXDMA_DMA_WB_BASE_LO        0x2008
#define REG_RXDMA_NUM_PKT_DONE          0x200C
#define REG_RXDMA_MAX_PKT_TIME          0x2010
#define REG_RXDMA_RXQ_RD_ADDR           0x2014
#define REG_RXDMA_RXQ_RD_ADDR_EXT       0x2018
#define REG_RXDMA_RXQ_WR_ADDR           0x201C
#define REG_RXDMA_PSR_BASE_HI           0x2020
#define REG_RXDMA_PSR_BASE_LO           0x2024
#define REG_RXDMA_PSR_NUM_DES           0x2028
#define REG_RXDMA_PSR_AVAIL_OFFSET      0x202C
#define REG_RXDMA_PSR_FULL_OFFSET       0x2030
#define REG_RXDMA_PSR_ACCESS_INDEX      0x2034
#define REG_RXDMA_PSR_MIN_DES           0x2038
#define REG_RXDMA_FBR0_BASE_LO          0x203C
#define REG_RXDMA_FBR0_BASE_HI          0x2040
#define REG_RXDMA_FBR0_NUM_DES          0x2044
#define REG_RXDMA_FBR0_AVAIL_OFFSET     0x2048
#define REG_RXDMA_FBR0_FULL_OFFSET      0x204C
#define REG_RXDMA_FBR0_RD_INDEX         0x2050
#define REG_RXDMA_FBR0_MIN_DES          0x2054
#define REG_RXDMA_FBR1_BASE_LO          0x2058
#define REG_RXDMA_FBR1_BASE_HI          0x205C
#define REG_RXDMA_FBR1_NUM_DES          0x2060
#define REG_RXDMA_FBR1_AVAIL_OFFSET     0x2064
#define REG_RXDMA_FBR1_FULL_OFFSET      0x2068
#define REG_RXDMA_FBR1_RD_INDEX         0x206C
#define REG_RXDMA_FBR1_MIN_DES          0x2070

/* TXMAC register offsets */
#define REG_TXMAC_CTL          0x3000
#define REG_TXMAC_CF_PARAM     0x3004
#define REG_TXMAC_BP_CTRL      0x3008
#define REG_TXMAC_ERR          0x300C
#define REG_TXMAC_TX_TEST      0x3010

/* RXMAC register offsets */
#define REG_RXMAC_CTRL                        0x4000
#define REG_RXMAC_CRC0                          0x4004
#define REG_RXMAC_CRC12                         0x4008
#define REG_RXMAC_CRC34                         0x400C
#define REG_RXMAC_MASK0_WORD0                   0x4010
#define REG_RXMAC_MASK4_WORD3                   0x405C /* last of 20 mask regs */
#define REG_RXMAC_SA_LO                         0x4060
#define REG_RXMAC_SA_HI                         0x4064
#define REG_RXMAC_PF_CTRL                       0x4068
#define REG_RXMAC_UNI_PF_ADDR1                  0x406C
#define REG_RXMAC_UNI_PF_ADDR2                  0x4070
#define REG_RXMAC_UNI_PF_ADDR3                  0x4074
#define REG_RXMAC_MULTI_HASH1                   0x4078
#define REG_RXMAC_MULTI_HASH2                   0x407C
#define REG_RXMAC_MULTI_HASH3                   0x4080
#define REG_RXMAC_MULTI_HASH4                   0x4084
#define REG_RXMAC_MCIF_CTRL_MAX_SEG             0x4088
#define REG_RXMAC_MCIF_WATER_MARK               0x408C
#define REG_RXMAC_MIF_CTRL                      0x4090
#define REG_RXMAC_SPACE_AVAIL                   0x4094
#define REG_RXMAC_ERR_REG                       0x4098
#define REG_RXMAC_RXQ_DIAG                      0x409C

/* MAC register offsets */
#define REG_MAC_CFG1               0x5000
#define REG_MAC_IPG                0x5004
#define REG_MAC_HFDP               0x5008
#define REG_MAC_IF_CTRL            0x500C
#define REG_MAC_MII_MGMT_CFG       0x5010
#define REG_MAC_STATION_ADDR_1     0x5014
#define REG_MAC_STATION_ADDR_2     0x5018
#define REG_MAC_MAX_FM_LEN         0x501C
#define REG_MAC_MII_MGMT_ADDR      0x5020
#define REG_MAC_MII_MGMT_CMD       0x5024
#define REG_MAC_MII_MGMT_CTRL      0x5028
#define REG_MAC_MII_MGMT_STAT      0x502C
#define REG_MAC_MII_MGMT_INDICATOR 0x5030

/* MACSTAT register offsets (selected) */
#define REG_MACSTAT_TX_TOTAL_COLLISIONS    0x6000
#define REG_MACSTAT_TX_SINGLE_COLLISIONS   0x6004
#define REG_MACSTAT_TX_DEFERRED            0x6008
#define REG_MACSTAT_TX_MULTIPLE_COLLISIONS 0x600C
#define REG_MACSTAT_TX_LATE_COLLISIONS     0x6010
#define REG_MACSTAT_TX_UNDERSIZE_FRAMES    0x6014
#define REG_MACSTAT_TX_OVERSIZE_FRAMES     0x6018
#define REG_MACSTAT_RX_ALIGN_ERRS          0x601C
#define REG_MACSTAT_RX_CODE_ERRS           0x6020
#define REG_MACSTAT_RX_DROPS               0x6024
#define REG_MACSTAT_RX_OVERSIZE_PACKETS    0x6028
#define REG_MACSTAT_RX_FCS_ERRS            0x602C
#define REG_MACSTAT_RX_FRAME_LEN_ERRS      0x6030
#define REG_MACSTAT_RX_FRAGMENT_PACKETS    0x6034
#define REG_MACSTAT_CARRY_REG1            0x6038
#define REG_MACSTAT_CARRY_REG2            0x603C
#define REG_MACSTAT_CARRY_REG1_MASK        0x6040
#define REG_MACSTAT_CARRY_REG2_MASK        0x6044

/* MMC register offset */
#define REG_MMC_CTRL          0x7000

/* Interrupt bit definitions */
#define ET_INTR_WATCHDOG       0x00004000
#define ET_INTR_TXDMA_ISR      0x00000008
#define ET_INTR_RXDMA_XFR_DONE 0x00000020
#define ET_INTR_TXDMA_ERR      0x00000010
#define ET_INTR_RXDMA_ERR      0x00000200
#define ET_INTR_RXDMA_FB_R0_LOW 0x00000040
#define ET_INTR_RXDMA_FB_R1_LOW 0x00000080
#define ET_INTR_RXDMA_STAT_LOW 0x00000100
#define ET_INTR_TXMAC           0x00020000
#define ET_INTR_RXMAC           0x00040000
#define ET_INTR_MAC_STAT        0x00080000
#define ET_INTR_WOL             0x00008000
#define ET_INTR_SLV_TIMEOUT     0x00100000

/* MII management constants */
#define ET_MAC_MGMT_WAIT  0x00000005
#define ET_MAC_MGMT_BUSY  0x00000001
#define ET_MAC_MII_ADDR(phy, reg)  ((phy) << 8 | (reg))

/* PHY registers */
#define MII_BMCR        0x00
#define MII_BMSR        0x01
#define MII_PHYSID1     0x02
#define MII_PHYSID2     0x03
#define MII_ADVERTISE   0x04
#define MII_LPA         0x05
#define MII_EXPANSION   0x06
#define MII_CTRL1000    0x09
#define MII_STAT1000    0x0A
#define MII_ESTATUS     0x0F
#define PHY_INDEX_REG   0x10
#define PHY_DATA_REG    0x11
#define PHY_MPHY_CONTROL_REG 0x12
#define PHY_LOOPBACK_CONTROL 0x13
#define PHY_REGISTER_MGMT_CONTROL 0x15
#define PHY_CONFIG      0x16
#define PHY_PHY_CONTROL 0x17
#define PHY_INTERRUPT_MASK 0x18
#define PHY_INTERRUPT_STATUS 0x19
#define PHY_PHY_STATUS  0x1A
#define PHY_LED_1       0x1B
#define PHY_LED_2       0x1C

/* Default PHY identifier */
#define PHY_ID1 0x0141
#define PHY_ID2 0x0C20

/* EEPROM size */
#define EEPROM_SIZE 256

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];
    int num_bars;

    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;

    uint32_t regs[ET131X_BAR0_SIZE / sizeof(uint32_t)]; /* register space */

    /* PHY emulation */
    uint16_t phy_regs[32];
    bool mii_busy;
    uint8_t mii_transaction; /* 0=none, 1=read */
    uint8_t mii_phy_addr;
    uint8_t mii_reg_addr;

    /* EEPROM */
    uint8_t eeprom[EEPROM_SIZE];
    uint32_t lbcif_address;
    bool lbcif_enable;
    bool lbcif_i2c_write;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t pending = s->intr_status & ~s->intr_mask;
    if (pending) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

static void phy_complete_read(PCIBaseState *s)
{
    if (s->mii_transaction != 1) return;
    if (s->mii_reg_addr < 32) {
        uint16_t val = s->phy_regs[s->mii_reg_addr];
        /* store in MII MGMT STAT register */
        uint32_t idx = REG_MAC_MII_MGMT_STAT >> 2;
        s->regs[idx] = val;
    }
    s->mii_transaction = 0;
}

static void phy_init(PCIBaseState *s)
{
    s->phy_regs[MII_BMCR] = 0x1140;   /* Auto-neg enabled, duplex */
    s->phy_regs[MII_BMSR] = 0x796d;   /* Link up, 1000T capable, etc. */
    s->phy_regs[MII_PHYSID1] = PHY_ID1;
    s->phy_regs[MII_PHYSID2] = PHY_ID2;
    s->phy_regs[MII_ADVERTISE] = 0x01e1;
    s->phy_regs[MII_LPA] = 0x45e1;
    s->phy_regs[MII_EXPANSION] = 0x0004;
    s->phy_regs[MII_CTRL1000] = 0x0300;
    s->phy_regs[MII_STAT1000] = 0x3800;
    s->phy_regs[MII_ESTATUS] = 0x0000;
    /* Provide some defaults for other registers */
    s->phy_regs[PHY_LOOPBACK_CONTROL] = 0x0000;
    s->phy_regs[PHY_LOOPBACK_CONTROL+1] = 0x0000;
}

static void eeprom_init(PCIBaseState *s)
{
    memset(s->eeprom, 0xff, EEPROM_SIZE);
    s->eeprom[0x70] = 0xCD;
    s->eeprom[0x71] = 0x00;
}

static void handle_lbcif_write(PCIBaseState *s, uint32_t addr, uint32_t val, unsigned size)
{
    /* addr is config space offset */
    switch (addr) {
    case PCI_CONF_LBCIF_ADDRESS:
        s->lbcif_address = val;
        break;
    case PCI_CONF_LBCIF_DATA:
        if (s->lbcif_enable && s->lbcif_i2c_write) {
            /* Write data to EEPROM */
            if (s->lbcif_address < EEPROM_SIZE)
                s->eeprom[s->lbcif_address] = val & 0xFF;
        }
        break;
    case PCI_CONF_LBCIF_CONTROL:
        s->lbcif_enable = (val & 0x80) != 0;
        s->lbcif_i2c_write = (val & 0x40) != 0;
        break;
    }
}

static uint32_t handle_lbcif_read(PCIBaseState *s, uint32_t addr)
{
    uint32_t ret = 0;
    switch (addr) {
    case PCI_CONF_LBCIF_DATA: {
        /* Provide data and status: bit 16 = ready, bit 18 = not error */
        uint32_t status = 0x00010000; /* ready */
        if (!(s->lbcif_address & 0x80000000)) { /* invent condition */
            status |= 0x00000000; /* no error */
        }
        if (s->lbcif_enable && !s->lbcif_i2c_write) {
            /* read from EEPROM */
            if (s->lbcif_address < EEPROM_SIZE)
                ret = s->eeprom[s->lbcif_address];
        }
        ret |= status;
        break;
    }
    case PCI_CONF_LBCIF_ADDRESS:
        ret = s->lbcif_address;
        break;
    case PCI_CONF_LBCIF_CONTROL:
        ret = (s->lbcif_enable ? 0x80 : 0) | (s->lbcif_i2c_write ? 0x40 : 0);
        break;
    }
    return ret;
}

static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint32_t val;

    switch (addr) {
    case PCI_CONF_MAC_ADDR ... PCI_CONF_MAC_ADDR+5:
    case PCI_CONF_EEPROM_STATUS:
    case PCI_CONF_ACK_NACK:
    case PCI_CONF_REPLAY:
    case PCI_CONF_L0L1LATENCY:
    case PCI_CONF_LBCIF_DATA:
    case PCI_CONF_LBCIF_ADDRESS:
    case PCI_CONF_LBCIF_CONTROL:
        /* handled below */
        break;
    default:
        return pci_default_read_config(pdev, addr, len);
    }

    val = 0;
    if (addr >= PCI_CONF_MAC_ADDR && addr <= PCI_CONF_MAC_ADDR+5) {
        int byte = addr - PCI_CONF_MAC_ADDR;
        val = pdev->config[addr] & 0xFF;
    } else if (addr == PCI_CONF_EEPROM_STATUS) {
        val = 0;  /* no error */
    } else if (addr == PCI_CONF_ACK_NACK) {
        val = 0x76;  /* default */
    } else if (addr == PCI_CONF_REPLAY) {
        val = 0x1E0;  /* default */
    } else if (addr == PCI_CONF_L0L1LATENCY) {
        val = 0x11;
    } else if (addr == PCI_CONF_LBCIF_DATA || addr == PCI_CONF_LBCIF_ADDRESS ||
               addr == PCI_CONF_LBCIF_CONTROL) {
        val = handle_lbcif_read(s, addr);
    }
    return val;
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    switch (addr) {
    case PCI_CONF_MAC_ADDR ... PCI_CONF_MAC_ADDR+5:
        pci_default_write_config(pdev, addr, val, len);
        break;
    case PCI_CONF_ACK_NACK:
    case PCI_CONF_REPLAY:
    case PCI_CONF_L0L1LATENCY:
        pci_set_word(pdev->config + addr, val);
        break;
    case PCI_CONF_LBCIF_ADDRESS:
    case PCI_CONF_LBCIF_DATA:
    case PCI_CONF_LBCIF_CONTROL:
        handle_lbcif_write(s, addr, val, len);
        /* also let default handler store it */
        pci_default_write_config(pdev, addr, val, len);
        break;
    default:
        pci_default_write_config(pdev, addr, val, len);
        break;
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    uint32_t idx = addr >> 2;

    if (idx >= ARRAY_SIZE(s->regs)) {
        return 0;
    }

    /* Handle special registers */
    switch (addr) {
    case REG_MAC_MII_MGMT_INDICATOR:
        return s->mii_busy ? 0x1 : 0;
    case REG_MAC_MII_MGMT_STAT:
        return s->regs[idx];  /* PHY completed already */
    default:
        val = s->regs[idx];
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t idx = addr >> 2;

    if (idx >= ARRAY_SIZE(s->regs)) {
        return;
    }

    /* Pre-action for special registers */
    switch (addr) {
    case REG_GLOBAL_INT_MASK:
        s->intr_mask = val;
        pcibase_update_irq(s);
        break;
    case REG_MAC_MII_MGMT_CMD:
        if (val == 1) {
            /* Start PHY read */
            s->mii_transaction = 1;
            /* Decode address from mii_mgmt_addr register */
            uint32_t mii_addr = s->regs[REG_MAC_MII_MGMT_ADDR >> 2];
            s->mii_phy_addr = (mii_addr >> 8) & 0x1F;
            s->mii_reg_addr = mii_addr & 0x1F;
            s->mii_busy = true;
            /* Immediate completion */
            phy_complete_read(s);
            s->mii_busy = false;
        } else if (val & 0x2) {
            /* PHY write: value in mii_mgmt_ctrl */
            uint32_t mii_addr = s->regs[REG_MAC_MII_MGMT_ADDR >> 2];
            uint8_t phy = (mii_addr >> 8) & 0x1F;
            uint8_t reg = mii_addr & 0x1F;
            uint16_t data = s->regs[REG_MAC_MII_MGMT_CTRL >> 2] & 0xFFFF;
            if (reg < 32) s->phy_regs[reg] = data;
        }
        break;
    case REG_MAC_MII_MGMT_ADDR:
    case REG_MAC_MII_MGMT_CTRL:
        /* just store */
        break;
    default:
        break;
    }

    /* Store the value */
    s->regs[idx] = val;

    /* Post-action */
    if (addr == REG_GLOBAL_INT_STATUS) {
        /* W1C: clear bits written 1 */
        s->intr_status &= ~val;
        pcibase_update_irq(s);
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    memset(s->regs, 0, sizeof(s->regs));
    s->intr_status = 0;
    s->intr_mask = 0xFFFFFFFF; /* all disabled initially */
    s->mii_busy = false;
    s->mii_transaction = 0;
    phy_init(s);
    eeprom_init(s);
    s->lbcif_address = 0;
    s->lbcif_enable = false;
    s->lbcif_i2c_write = false;
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

    /* Set MAC address in config space */
    static const uint8_t mac_addr[6] = { 0x00, 0x05, 0x3d, 0x00, 0x02, 0x01 };
    for (int i = 0; i < 6; i++) {
        pci_conf[PCI_CONF_MAC_ADDR + i] = mac_addr[i];
    }

    s->num_bars = 1;
    memory_region_init_io(&s->bar_regions[0], OBJECT(s), &pcibase_mmio_ops, s, "et131x-mmio", ET131X_BAR0_SIZE);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_regions[0]);

    /* Initialize PHY and EEPROM */
    phy_init(s);
    eeprom_init(s);
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
    .name = "et131x_pci",
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
    k->config_read  = pcibase_config_read;
    k->config_write = pcibase_config_write;
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
