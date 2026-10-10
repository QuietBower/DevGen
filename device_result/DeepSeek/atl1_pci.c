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

#define TYPE_PCIBASE_DEVICE "atl1_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID               0x1969
#define DEVICE_ID               0x1048
#define CLASS_ID                0x020000

#define REG_MASTER_CTRL          0x1400
#define MASTER_CTRL_SOFT_RST     0x1
#define MASTER_CTRL_ITIMER_EN    0x4

#define REG_PHY_ENABLE           0x140C
#define REG_IDLE_STATUS          0x1410

#define REG_PCIE_CAP_LIST        0x58

#define REG_SPI_FLASH_CTRL       0x200
#define SPI_FLASH_CTRL_EN_VPD    0x2000
#define SPI_FLASH_CTRL_START     0x800
#define SPI_FLASH_CTRL_WAIT_READY 0x10000000
#define SPI_FLASH_CTRL_CLK_HI_SHIFT   22
#define SPI_FLASH_CTRL_CS_HOLD_MASK    0x3
#define SPI_FLASH_CTRL_CS_HOLD_SHIFT   18
#define SPI_FLASH_CTRL_CS_HI_MASK      0x3
#define SPI_FLASH_CTRL_CLK_HI_MASK     0x3
#define SPI_FLASH_CTRL_CLK_LO_MASK     0x3
#define SPI_FLASH_CTRL_CS_HI_SHIFT     16
#define SPI_FLASH_CTRL_CS_SETUP_SHIFT  24
#define SPI_FLASH_CTRL_CS_SETUP_MASK   0x3
#define SPI_FLASH_CTRL_CLK_LO_SHIFT    20
#define SPI_FLASH_CTRL_INS_SHIFT       8
#define SPI_FLASH_CTRL_INS_MASK        0x7

#define REG_SPI_DATA             0x208
#define REG_SPI_ADDR             0x204

#define REG_VPD_CAP              0x6C
#define VPD_CAP_VPD_ADDR_MASK    0x7FFF
#define VPD_CAP_VPD_FLAG         0x80000000
#define VPD_CAP_VPD_ADDR_SHIFT   16
#define REG_VPD_DATA             0x70

#define REG_MDIO_CTRL            0x1414
#define MDIO_WAIT_TIMES          30
#define MDIO_CLK_25_4            0
#define MDIO_REG_ADDR_MASK       0x1F
#define MDIO_START               0x800000
#define MDIO_SUP_PREAMBLE        0x400000
#define MDIO_BUSY                0x8000000
#define MDIO_RW                  0x200000
#define MDIO_REG_ADDR_SHIFT      16
#define MDIO_CLK_SEL_SHIFT       24
#define MDIO_DATA_MASK           0xFFFF
#define MDIO_DATA_SHIFT          0

#define REG_MAC_STA_ADDR         0x1488
#define REG_RX_HASH_TABLE        0x1490

#define REG_MAC_CTRL             0x1480
#define MAC_CTRL_TX_EN           1
#define MAC_CTRL_RX_EN           2
#define MAC_CTRL_TX_FLOW         4
#define MAC_CTRL_RX_FLOW         8
#define MAC_CTRL_PROMIS_EN       0x8000
#define MAC_CTRL_MC_ALL_EN       0x2000000
#define MAC_CTRL_BC_EN           0x4000000
#define MAC_CTRL_SPEED_10_100    0x1
#define MAC_CTRL_SPEED_1000      0x2
#define MAC_CTRL_DUPLX           0x20
#define MAC_CTRL_PAD             0x80
#define MAC_CTRL_ADD_CRC         0x40
#define MAC_CTRL_PRMLEN_SHIFT    10
#define MAC_CTRL_PRMLEN_MASK     0xF
#define MAC_CTRL_SPEED_SHIFT     20

#define REG_MAC_IPG_IFG          0x1484
#define MAC_IPG_IFG_IPGT_SHIFT   0
#define MAC_IPG_IFG_IPGT_MASK    0x7F
#define MAC_IPG_IFG_MIFG_SHIFT   8
#define MAC_IPG_IFG_MIFG_MASK    0xFF
#define MAC_IPG_IFG_IPGR1_SHIFT  16
#define MAC_IPG_IFG_IPGR1_MASK   0x7F
#define MAC_IPG_IFG_IPGR2_SHIFT  24
#define MAC_IPG_IFG_IPGR2_MASK   0x7F

#define REG_MAC_HALF_DUPLX_CTRL  0x1498
#define MAC_HALF_DUPLX_CTRL_LCOL_MASK       0x3FF
#define MAC_HALF_DUPLX_CTRL_RETRY_MASK      0xF
#define MAC_HALF_DUPLX_CTRL_RETRY_SHIFT     12
#define MAC_HALF_DUPLX_CTRL_EXC_DEF_EN      0x10000
#define MAC_HALF_DUPLX_CTRL_JAMIPG_SHIFT    24
#define MAC_HALF_DUPLX_CTRL_JAMIPG_MASK     0xF
#define MAC_HALF_DUPLX_CTRL_ABEBT_SHIFT     20

#define REG_MTU                  0x149C

#define REG_WOL_CTRL             0x14A0
#define WOL_MAGIC_PME_EN         0x8
#define WOL_MAGIC_EN             0x4
#define WOL_LINK_CHG_PME_EN      0x20
#define WOL_LINK_CHG_EN          0x10

#define REG_PCIE_PHYMISC         0x1000
#define PCIE_PHYMISC_FORCE_RCV_DET 0x4

#define REG_IRQ_MODU_TIMER_INIT  0x1408

#define REG_DESC_BASE_ADDR_HI    0x1540
#define REG_DESC_RFD_ADDR_LO     0x1544
#define REG_DESC_RRD_ADDR_LO     0x1548
#define REG_DESC_TPD_ADDR_LO     0x154C
#define REG_DESC_CMB_ADDR_LO     0x1550
#define REG_DESC_SMB_ADDR_LO     0x1554
#define REG_DESC_RFD_RRD_RING_SIZE 0x1558
#define REG_DESC_TPD_RING_SIZE   0x155C

#define REG_SRAM_RFD_LEN         0x1524
#define REG_SRAM_RRD_LEN         0x150C

#define REG_DMA_CTRL             0x15C0
#define DMA_CTRL_DMAR_EN         0x400
#define DMA_CTRL_DMAW_EN         0x800
#define DMA_CTRL_DMAR_BURST_LEN_SHIFT 4
#define DMA_CTRL_DMAR_BURST_LEN_MASK  7
#define DMA_CTRL_DMAW_BURST_LEN_SHIFT 7
#define DMA_CTRL_DMAW_BURST_LEN_MASK  7
#define DMA_CTRL_RCB_VALUE       0x8

#define REG_CSMB_CTRL            0x15D0
#define CSMB_CTRL_CMB_EN         4
#define CSMB_CTRL_SMB_EN         8

#define REG_CMB_WRITE_TH         0x15D4
#define REG_CMB_WRITE_TIMER      0x15D8
#define REG_SMB_TIMER            0x15E4

#define REG_MAILBOX              0x15F0
#define MB_TPD_PROD_INDX_MASK    0x3FF
#define MB_TPD_PROD_INDX_SHIFT   22
#define MB_RFD_PROD_INDX_MASK    0x7FF
#define MB_RFD_PROD_INDX_SHIFT   0
#define MB_RRD_CONS_INDX_MASK    0x7FF
#define MB_RRD_CONS_INDX_SHIFT   11

#define REG_CMBDISDMA_TIMER      0x140E

#define REG_TXQ_CTRL             0x1580
#define TXQ_CTRL_EN              0x20
#define TXQ_CTRL_ENH_MODE        0x40
#define TXQ_CTRL_TPD_BURST_NUM_SHIFT 0
#define TXQ_CTRL_TPD_BURST_NUM_MASK  0x1F
#define TXQ_CTRL_TXF_BURST_NUM_SHIFT 16
#define TXQ_CTRL_TXF_BURST_NUM_MASK  0xFFFF
#define TXQ_CTRL_TPD_FETCH_TH_SHIFT  8
#define TXQ_CTRL_TPD_FETCH_TH_MASK   0x3F

#define REG_TX_JUMBO_TASK_TH_TPD_IPG 0x1584
#define TX_JUMBO_TASK_TH_SHIFT    0
#define TX_JUMBO_TASK_TH_MASK     0x7FF
#define TX_TPD_MIN_IPG_SHIFT     16
#define TX_TPD_MIN_IPG_MASK      0x1F

#define REG_RXQ_CTRL             0x15A0
#define RXQ_CTRL_EN              0x80000000
#define RXQ_CTRL_CUT_THRU_EN     0x40000000
#define RXQ_CTRL_RFD_BURST_NUM_SHIFT 0
#define RXQ_CTRL_RFD_BURST_NUM_MASK  0xFF
#define RXQ_CTRL_RRD_BURST_THRESH_SHIFT 8
#define RXQ_CTRL_RRD_BURST_THRESH_MASK  0xFF
#define RXQ_CTRL_RFD_PREF_MIN_IPG_SHIFT 16
#define RXQ_CTRL_RFD_PREF_MIN_IPG_MASK  0x1F

#define REG_RXQ_JMBOSZ_RRDTIM    0x15A4
#define RXQ_JMBOSZ_TH_SHIFT      0
#define RXQ_JMBOSZ_TH_MASK       0x7FF
#define RXQ_JMBO_LKAH_SHIFT      11
#define RXQ_JMBO_LKAH_MASK       0xF
#define RXQ_RRD_TIMER_SHIFT      16
#define RXQ_RRD_TIMER_MASK       0xFFFF

#define REG_RXQ_RXF_PAUSE_THRESH 0x15A8
#define RXQ_RXF_PAUSE_TH_HI_SHIFT 16
#define RXQ_RXF_PAUSE_TH_HI_MASK 0xFFF
#define RXQ_RXF_PAUSE_TH_LO_SHIFT 0
#define RXQ_RXF_PAUSE_TH_LO_MASK 0xFFF

#define REG_RXQ_RRD_PAUSE_THRESH 0x15AC
#define RXQ_RRD_PAUSE_TH_HI_SHIFT 0
#define RXQ_RRD_PAUSE_TH_HI_MASK 0xFFF
#define RXQ_RRD_PAUSE_TH_LO_SHIFT 16
#define RXQ_RRD_PAUSE_TH_LO_MASK 0xFFF

#define REG_LOAD_PTR             0x1534

#define REG_ISR                  0x1600
#define ISR_SMB                  0x1
#define ISR_RXF_OV               0x8
#define ISR_RFD_UNRUN            0x10
#define ISR_RRD_OV               0x20
#define ISR_HOST_RFD_UNRUN       0x100
#define ISR_HOST_RRD_OV          0x200
#define ISR_DMAR_TO_RST          0x400
#define ISR_DMAW_TO_RST          0x800
#define ISR_GPHY                 0x1000
#define ISR_CMB_RX               0x100000
#define ISR_CMB_TX               0x200000
#define ISR_PHY_LINKDOWN         0x10000000
#define ISR_DIS_SMB              0x20000000
#define ISR_DIS_DMA              0x40000000
#define ISR_DIS_INT              0x80000000

#define REG_IMR                  0x1604
#define IMR_NORMAL_MASK          (IMR_NORXTX_MASK | ISR_CMB_TX | ISR_CMB_RX)
#define IMR_NORXTX_MASK          (ISR_SMB | ISR_GPHY | ISR_PHY_LINKDOWN | ISR_DMAR_TO_RST | ISR_DMAW_TO_RST)

#define ATL1_REG_COUNT           1538

#define REG_SPI_FLASH_OP_READ    0x217
#define REG_SPI_FLASH_OP_PROGRAM 0x210
#define REG_SPI_FLASH_OP_CHIP_ERASE 0x212
#define REG_SPI_FLASH_OP_WRSR    0x216
#define REG_SPI_FLASH_OP_WREN    0x214
#define REG_SPI_FLASH_OP_RDID    0x213
#define REG_SPI_FLASH_OP_SC_ERASE 0x211
#define REG_SPI_FLASH_OP_RDSR    0x215

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
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t mmio_regs[0x800]; /* 0x2000 bytes of register space */

    /* DMA Context */
    uint32_t desc_rfd_addr_lo;
    uint32_t desc_rrd_addr_lo;
    uint32_t desc_tpd_addr_lo;
    uint32_t desc_cmb_addr_lo;
    uint32_t desc_smb_addr_lo;
    uint32_t desc_base_addr_hi;
    uint32_t rfd_rrd_ring_size;
    uint32_t tpd_ring_size;
    uint32_t dma_ctrl;
    uint32_t mailbox;

    /* Operational status flags */
    uint32_t status;

    /* State used to handle reset sequences */
    bool reset_active;

    /* Power management state (D0-D3) */
    uint8_t pm_state;

    /* PHY registers */
    uint16_t phy_regs[32];
};

/* descriptor structures used by hardware */
struct rx_free_desc {
    uint64_t buffer_addr;
    uint16_t buf_len;
    uint16_t coalese;
} __attribute__((packed));

struct rx_return_desc {
    uint8_t num_buf;
    uint8_t resved;
    uint16_t buf_indx;
    union {
        uint32_t valid;
        struct {
            uint16_t rx_chksum;
            uint16_t pkt_size;
        } xsum_sz;
    } xsz;
    uint16_t pkt_flg;
    uint16_t err_flg;
    uint16_t resved2;
    uint16_t vlan_tag;
} __attribute__((packed));

struct tx_packet_desc {
    uint64_t buffer_addr;
    uint32_t word2;
    uint32_t word3;
} __attribute__((packed));

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    if (s->intr_status & s->intr_mask) {
        if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!msi_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

static void pcibase_soft_reset(PCIBaseState *s)
{
    /* Reset all registers to power-on defaults */
    memset(s->mmio_regs, 0, sizeof(s->mmio_regs));
    
    /* Set revision: 0x9003 in high word of MASTER_CTRL */
    s->mmio_regs[REG_MASTER_CTRL / 4] = (0x9003 << 16); /* low word 0 */
    
    /* Default MAC address: 52:54:00:12:34:56 */
    /* low dword: addr[2]<<24 | addr[3]<<16 | addr[4]<<8 | addr[5] */
    s->mmio_regs[REG_MAC_STA_ADDR / 4] = (0x00 << 24) | (0x12 << 16) | (0x34 << 8) | 0x56;
    /* high dword: (addr[0]<<8) | addr[1] */
    s->mmio_regs[(REG_MAC_STA_ADDR + 4) / 4] = (0x52 << 8) | 0x54;
    
    /* SRAM sizes defaults */
    s->mmio_regs[REG_SRAM_RFD_LEN / 4] = 0x1000;
    s->mmio_regs[REG_SRAM_RRD_LEN / 4] = 0x200;
    
    /* Ensure IDLE_STATUS is 0 */
    s->mmio_regs[REG_IDLE_STATUS / 4] = 0;
    
    s->intr_status = 0;
    s->intr_mask = 0;
    pcibase_update_irq(s);
}

static void pcibase_init_regs(PCIBaseState *s)
{
    /* Initialize registers to power-on defaults */
    pcibase_soft_reset(s);
    /* PHY register defaults */
    memset(s->phy_regs, 0, sizeof(s->phy_regs));
    /* Optional: set a PHY ID if needed, otherwise all zeros. */
}

static void handle_mdio_write(PCIBaseState *s, uint32_t val)
{
    uint16_t reg_addr = (val >> MDIO_REG_ADDR_SHIFT) & MDIO_REG_ADDR_MASK;
    bool is_write = val & MDIO_RW;
    if (val & MDIO_START) {
        if (is_write) {
            uint16_t data = (val >> MDIO_DATA_SHIFT) & MDIO_DATA_MASK;
            s->phy_regs[reg_addr] = data;
        } else {
            uint16_t data = s->phy_regs[reg_addr];
            uint32_t new_val = val & ~(MDIO_START | MDIO_BUSY);
            new_val = (new_val & ~(MDIO_DATA_MASK << MDIO_DATA_SHIFT)) | 
                       ((data & MDIO_DATA_MASK) << MDIO_DATA_SHIFT);
            s->mmio_regs[REG_MDIO_CTRL / 4] = new_val;
            return;
        }
        /* For write, clear START and BUSY */
        s->mmio_regs[REG_MDIO_CTRL / 4] = val & ~(MDIO_START | MDIO_BUSY);
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= sizeof(s->mmio_regs)) {
        return 0;
    }

    /* Special handling for interrupt registers */
    if (addr == REG_ISR) {
        if (size == 4) {
            return s->intr_status;
        }
        /* else fallback to raw read */
    }
    if (addr == REG_IMR) {
        if (size == 4) {
            return s->intr_mask;
        }
    }

    /* Generic read from mmio_regs as byte array */
    if (size == 1) {
        val = *((uint8_t*)((uint8_t*)s->mmio_regs + addr));
    } else if (size == 2) {
        val = *((uint16_t*)((uint8_t*)s->mmio_regs + addr));
    } else if (size == 4) {
        val = *((uint32_t*)((uint8_t*)s->mmio_regs + addr));
    } else if (size == 8) {
        val = *((uint64_t*)((uint8_t*)s->mmio_regs + addr));
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= sizeof(s->mmio_regs)) {
        return;
    }

    /* Generic write */
    if (size == 1) {
        *((uint8_t*)((uint8_t*)s->mmio_regs + addr)) = (uint8_t)val;
    } else if (size == 2) {
        *((uint16_t*)((uint8_t*)s->mmio_regs + addr)) = (uint16_t)val;
    } else if (size == 4) {
        *((uint32_t*)((uint8_t*)s->mmio_regs + addr)) = (uint32_t)val;
    } else if (size == 8) {
        *((uint64_t*)((uint8_t*)s->mmio_regs + addr)) = val;
    }

    /* Handle side effects */
    if (addr == REG_MASTER_CTRL && (val & MASTER_CTRL_SOFT_RST)) {
        pcibase_soft_reset(s);
    }
    if (addr == REG_ISR) {
        s->intr_status &= ~((uint32_t)val);
        s->mmio_regs[REG_ISR / 4] = s->intr_status;
        pcibase_update_irq(s);
    }
    if (addr == REG_IMR) {
        s->intr_mask = (uint32_t)val;
        s->mmio_regs[REG_IMR / 4] = s->intr_mask;
        pcibase_update_irq(s);
    }
    if (addr == REG_MDIO_CTRL) {
        handle_mdio_write(s, (uint32_t)val);
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* No PIO implemented; return all ones */
    return ~0ULL;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* No PIO implemented */
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

    pcibase_soft_reset(s);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x1969 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x1048 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x020000 );
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
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = 0x2000, .name = "atl1-mmio" };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Final state initialization before the device is 'live' */
    pcibase_init_regs(s);
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

    /* No additional cleanup required */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "atl1_pci",
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
