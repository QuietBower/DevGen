/*
 * QEMU device model for Atheros L1E Ethernet NIC
 * Generated from driver: atl1e_main.c
 * QEMU 8.2.10 PCI device model
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

#define TYPE_PCIBASE_DEVICE "ATL1E_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Vendor and Device IDs */
#ifndef PCI_VENDOR_ID_ATTANSIC
#define PCI_VENDOR_ID_ATTANSIC 0x1969
#endif
#define PCI_DEVICE_ID_ATTANSIC_L1E      0x1026

/* Register offsets */
#define REG_ISR    0x1600
#define REG_IMR    0x1604
#define REG_MAC_CTRL                0x1480
#define REG_RX_HASH_TABLE           0x1490
#define REG_PM_CTRLSTAT             0x44
#define REG_PHY_STATUS              0x1418
#define REG_LOAD_PTR                0x1534
#define REG_DESC_BASE_ADDR_HI       0x1540
#define REG_TPD_RING_SIZE           0x155C
#define REG_HOST_TX_CMB_LO          0x1840
#define REG_TPD_BASE_ADDR_LO        0x154C
#define REG_HOST_RXFPAGE_SIZE       0x1558
#define REG_TX_EARLY_TH             0x1584
#define REG_TXQ_CTRL                0x1580
#define REG_DEVICE_CTRL             0x60
#define REG_BASE_CPU_NUMBER         0x157C
#define REG_RXQ_CTRL                0x15A0
#define REG_RXQ_JMBOSZ_RRDTIM       0x15A4
#define REG_RXQ_RXF_PAUSE_THRESH    0x15A8
#define REG_IDT_TABLE0              0x1560
#define REG_SRAM_RXF_LEN            0x1524
#define REG_DMA_CTRL                0x15C0
#define REG_MASTER_CTRL             0x1400
#define REG_SMB_STAT_TIMER          0x15C4
#define REG_IRQ_MODU_TIMER2_INIT    0x140A
#define REG_TRIG_RRD_THRESH         0x15CA
#define REG_TRIG_RXTIMER            0x15CE
#define REG_CMBDISDMA_TIMER         0x140E
#define REG_TRIG_TPD_THRESH         0x15C8
#define REG_TRIG_TXTIMER            0x15CC
#define REG_WOL_CTRL                0x14a0
#define REG_IRQ_MODU_TIMER_INIT     0x1408
#define REG_MTU                     0x149c
#define REG_MAC_RX_STATUS_END       0x175c
#define REG_MAC_RX_STATUS_BIN       0x1700
#define REG_MAC_TX_STATUS_BIN       0x1760
#define REG_MAC_TX_STATUS_END       0x17c0
#define REG_TPD_CONS_IDX            0x1804
#define REG_DEBUG_DATA0             0x1900
#define REG_MB_TPD_PROD_IDX         0x15F0
#define REG_HOST_RXF0_PAGE1_VLD     0x15F5
#define REG_HOST_RXF2_PAGE1_VLD     0x15F9
#define REG_HOST_RXF1_PAGE0_VLD     0x15F6
#define REG_HOST_RXF2_PAGE0_VLD     0x15F8
#define REG_HOST_RXF0_PAGE0_VLD     0x15F4
#define REG_HOST_RXF3_PAGE1_VLD     0x15FB
#define REG_HOST_RXF3_PAGE0_VLD     0x15FA
#define REG_HOST_RXF1_PAGE1_VLD     0x15F7
#define REG_RXF0_BASE_ADDR_HI       0x1540
#define REG_RXF2_BASE_ADDR_HI       0x1554
#define REG_RXF1_BASE_ADDR_HI       0x1550
#define REG_RXF3_BASE_ADDR_HI       0x153C
#define REG_HOST_RXF1_PAGE1_LO      0x15D4
#define REG_HOST_RXF1_PAGE0_LO      0x15D0
#define REG_HOST_RXF2_PAGE0_LO      0x15D8
#define REG_HOST_RXF0_PAGE1_LO      0x1548
#define REG_HOST_RXF3_PAGE1_LO      0x15E4
#define REG_HOST_RXF3_PAGE0_LO      0x15E0
#define REG_HOST_RXF2_PAGE1_LO      0x15DC
#define REG_HOST_RXF0_PAGE0_LO      0x1544
#define REG_HOST_RXF2_MB1_LO        0x1834
#define REG_HOST_RXF3_MB1_LO        0x183C
#define REG_HOST_RXF3_MB0_LO        0x1838
#define REG_HOST_RXF2_MB0_LO        0x1830
#define REG_HOST_RXF1_MB0_LO        0x1828
#define REG_HOST_RXF0_MB0_LO        0x1820
#define REG_HOST_RXF0_MB1_LO        0x1824
#define REG_HOST_RXF1_MB1_LO        0x182C
#define REG_PCIE_PHYMISC            0x1000
#define REG_MDIO_CTRL               0x1414
#define REG_MAC_STA_ADDR            0x1488
#define REG_IDLE_STATUS              0x1410
#define REG_GPHY_CTRL               0x140C
#define REG_TWSI_CTRL               0x218
#define REG_SPI_FLASH_CONFIG        0x20C
#define REG_MAC_HALF_DUPLX_CTRL     0x1498
#define REG_SERDES_LOCK             0x1424
#define REG_MANUAL_TIMER_INIT       0x1404
#define REG_SRAM_TCPH_ADDR          0x1530
#define REG_VPD_CAP                 0x6C
#define REG_SPI_FLASH_CTRL          0x200
#define REG_SRAM_TRD_LEN            0x151C
#define REG_SRAM_TXF_ADDR           0x1528
#define REG_SRAM_TRD_ADDR           0x1518
#define REG_SRAM_RXF_ADDR           0x1520
#define REG_SRAM_PKTH_ADDR          0x1532
#define REG_MAC_IPG_IFG             0x1484
#define REG_PCIE_CAP_LIST           0x58
#define REG_VPD_DATA                0x70

/* ISR/IMR bit definitions */
#define ISR_SMB             0x00000001
#define ISR_MANUAL          0x00000004
#define ISR_HW_RXF_OV       0x00000008
#define ISR_HOST_RXF0_OV    0x00000010
#define ISR_TXF_UN          0x00000100
#define ISR_DMAR_TO_RST     0x00000400
#define ISR_DMAW_TO_RST     0x00000800
#define ISR_GPHY            0x00001000
#define ISR_RX_PKT          0x00010000
#define ISR_TX_PKT          0x00020000
#define ISR_PHY_LINKDOWN    0x10000000
#define ISR_DIS_INT         0x80000000
#define ISR_GPHY_LPW        0x00004000
#define IMR_NORMAL_MASK     (ISR_SMB | ISR_TXF_UN | ISR_HW_RXF_OV | \
                             ISR_HOST_RXF0_OV | ISR_MANUAL | ISR_GPHY | \
                             ISR_GPHY_LPW | ISR_DMAR_TO_RST | ISR_DMAW_TO_RST | \
                             ISR_PHY_LINKDOWN | ISR_RX_PKT | ISR_TX_PKT)

/* MAC_CTRL bit definitions */
#define MAC_CTRL_TX_EN              0x00000001
#define MAC_CTRL_RX_EN              0x00000002
#define MAC_CTRL_TX_FLOW            0x00000004
#define MAC_CTRL_RX_FLOW            0x00000008
#define MAC_CTRL_DUPLX              0x00000020
#define MAC_CTRL_ADD_CRC            0x00000040
#define MAC_CTRL_PAD                0x00000080
#define MAC_CTRL_PRMLEN_SHIFT       10
#define MAC_CTRL_PRMLEN_MASK        0xf
#define MAC_CTRL_SPEED_SHIFT        20
#define MAC_CTRL_SPEED_10_100       0x00000000
#define MAC_CTRL_SPEED_1000         0x00000002
#define MAC_CTRL_PROMIS_EN          0x00008000
#define MAC_CTRL_BC_EN              0x04000000
#define MAC_CTRL_MC_ALL_EN          0x02000000
#define MAC_CTRL_DBG                0x08000000
#define MAC_CTRL_RMV_VLAN           0x00004000

/* MASTER_CTRL bits */
#define MASTER_CTRL_SOFT_RST        0x00000001
#define MASTER_CTRL_ITIMER_EN       0x00000004
#define MASTER_CTRL_MANUAL_INT      0x00000008
#define MASTER_CTRL_ITIMER2_EN      0x00000020
#define MASTER_CTRL_LED_MODE        0x00000200

/* DMA_CTRL bits */
#define DMA_CTRL_DMAR_BURST_LEN_SHIFT   4
#define DMA_CTRL_DMAR_BURST_LEN_MASK    7
#define DMA_CTRL_DMAW_BURST_LEN_SHIFT   7
#define DMA_CTRL_DMAW_BURST_LEN_MASK    7
#define DMA_CTRL_DMAR_REQ_PRI           0x00000400
#define DMA_CTRL_DMAR_OUT_ORDER         0x00000004
#define DMA_CTRL_DMAW_DLY_CNT_MASK      0xF
#define DMA_CTRL_DMAW_DLY_CNT_SHIFT     16
#define DMA_CTRL_DMAR_DLY_CNT_MASK      0x1F
#define DMA_CTRL_DMAR_DLY_CNT_SHIFT     11
#define DMA_CTRL_RXCMB_EN               0x00200000

/* RXQ_CTRL bits */
#define RXQ_CTRL_EN                     0x80000000
#define RXQ_CTRL_CUT_THRU_EN            0x40000000
#define RXQ_CTRL_HASH_ENABLE            0x20000000
#define RXQ_CTRL_HASH_TYPE_IPV4         0x00010000
#define RXQ_CTRL_HASH_TYPE_IPV4_TCP     0x00020000
#define RXQ_CTRL_HASH_TYPE_IPV6         0x00040000
#define RXQ_CTRL_HASH_TYPE_IPV6_TCP     0x00080000
#define RXQ_CTRL_PBA_ALIGN_32           0x00000000
#define RXQ_CTRL_IPV6_XSUM_VERIFY_EN    0x00000080
#define RXQ_CTRL_RSS_MODE_MQUESINT      0x08000000

/* TXQ_CTRL bits */
#define TXQ_CTRL_EN                     0x00000020
#define TXQ_CTRL_ENH_MODE               0x00000040
#define TXQ_CTRL_NUM_TPD_BURST_SHIFT    0
#define TXQ_CTRL_NUM_TPD_BURST_MASK     0xF

/* Misc hardware constants */
#define AT_MAX_RECEIVE_QUEUE    4
#define AT_PAGE_NUM_PER_QUEUE   2
#define AT_DMA_HI_ADDR_MASK     0xffffffff00000000ULL
#define AT_DMA_LO_ADDR_MASK     0x00000000ffffffffULL
#define MAX_JUMBO_FRAME_SIZE    0x2000
#define AT_EEPROM_LEN           512
#define AT_REGS_LEN             75
#define ATL1E_MMIO_SIZE         (8 * 1024)

/* PHY Status bits (inferred) */
#define PHY_STATUS_100M         0x00000001
#define PHY_STATUS_EMI_CA       0x00000002

/* Enums from driver */
enum atl1e_nic_type {
    athr_l1e = 0,
    athr_l2e_revA = 1,
    athr_l2e_revB = 2
};
enum atl1e_rrs_type {
    atl1e_rrs_disable = 0,
    atl1e_rrs_ipv4 = 1,
    atl1e_rrs_ipv4_tcp = 2,
    atl1e_rrs_ipv6 = 4,
    atl1e_rrs_ipv6_tcp = 8
};
enum atl1e_dma_req_block {
    atl1e_dma_req_128 = 0,
    atl1e_dma_req_256 = 1,
    atl1e_dma_req_512 = 2,
    atl1e_dma_req_1024 = 3,
    atl1e_dma_req_2048 = 4,
    atl1e_dma_req_4096 = 5
};

/* Static lookup tables */
static const uint16_t
atl1e_rx_page_vld_regs[AT_MAX_RECEIVE_QUEUE][AT_PAGE_NUM_PER_QUEUE] = {
    {REG_HOST_RXF0_PAGE0_VLD, REG_HOST_RXF0_PAGE1_VLD},
    {REG_HOST_RXF1_PAGE0_VLD, REG_HOST_RXF1_PAGE1_VLD},
    {REG_HOST_RXF2_PAGE0_VLD, REG_HOST_RXF2_PAGE1_VLD},
    {REG_HOST_RXF3_PAGE0_VLD, REG_HOST_RXF3_PAGE1_VLD}
};
static const uint16_t atl1e_rx_page_hi_addr_regs[AT_MAX_RECEIVE_QUEUE] = {
    REG_RXF0_BASE_ADDR_HI,
    REG_RXF1_BASE_ADDR_HI,
    REG_RXF2_BASE_ADDR_HI,
    REG_RXF3_BASE_ADDR_HI
};
static const uint16_t
atl1e_rx_page_lo_addr_regs[AT_MAX_RECEIVE_QUEUE][AT_PAGE_NUM_PER_QUEUE] = {
    {REG_HOST_RXF0_PAGE0_LO, REG_HOST_RXF0_PAGE1_LO},
    {REG_HOST_RXF1_PAGE0_LO, REG_HOST_RXF1_PAGE1_LO},
    {REG_HOST_RXF2_PAGE0_LO, REG_HOST_RXF2_PAGE1_LO},
    {REG_HOST_RXF3_PAGE0_LO, REG_HOST_RXF3_PAGE1_LO}
};
static const uint16_t
atl1e_rx_page_write_offset_regs[AT_MAX_RECEIVE_QUEUE][AT_PAGE_NUM_PER_QUEUE] = {
    {REG_HOST_RXF0_MB0_LO,  REG_HOST_RXF0_MB1_LO},
    {REG_HOST_RXF1_MB0_LO,  REG_HOST_RXF1_MB1_LO},
    {REG_HOST_RXF2_MB0_LO,  REG_HOST_RXF2_MB1_LO},
    {REG_HOST_RXF3_MB0_LO,  REG_HOST_RXF3_MB1_LO}
};
static const uint16_t atl1e_pay_load_size[] = {
    128, 256, 512, 1024, 2048, 4096,
};

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
    uint32_t isr;   /* REG_ISR */
    uint32_t imr;   /* REG_IMR */

    uint32_t regs[ATL1E_MMIO_SIZE / 4]; /* MMIO register space */

    struct {
        dma_addr_t tpd_base;
        dma_addr_t rxf_base[4];
        dma_addr_t rxf_page_base[4][2];
        uint32_t dma_ctrl;
        uint32_t tpd_ring_size;
        uint32_t rxf_page_size;
    } dma;

    uint16_t link_speed;
    uint16_t link_duplex;
    bool phy_configured;
    bool re_autoneg;
    bool emi_ca;

    bool in_reset;
    uint32_t master_ctrl;

    uint32_t wol;
    uint32_t pm_ctrlstat;
    uint8_t power_state;
};

/* Helper: update IRQ line based on ISR and IMR */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool raise = (s->isr & s->imr) != 0;
    pci_set_irq(pdev, raise ? 1 : 0);
}

/* Helper: read from regs array with proper byte-lane handling */
static uint64_t pcibase_reg_read(PCIBaseState *s, hwaddr addr, unsigned size)
{
    hwaddr reg = addr & ~3;
    hwaddr offset = addr & 3;
    uint64_t val = 0;

    if (reg >= sizeof(s->regs)) {
        return 0;
    }

    switch (size) {
    case 1:
        val = (s->regs[reg / 4] >> (offset * 8)) & 0xff;
        break;
    case 2:
        if (offset == 0 || offset == 2) {
            val = (s->regs[reg / 4] >> (offset * 8)) & 0xffff;
        } else {
            /* unaligned 2-byte across two words */
            uint32_t lo = s->regs[reg / 4];
            uint32_t hi = (reg / 4 + 1 < sizeof(s->regs) / 4) ? s->regs[reg / 4 + 1] : 0;
            val = ((hi & 0xff) << 8) | ((lo >> 24) & 0xff);
        }
        break;
    case 4:
        val = s->regs[reg / 4];
        break;
    case 8:
        val = s->regs[reg / 4];
        if (reg / 4 + 1 < sizeof(s->regs) / 4) {
            val |= (uint64_t)s->regs[reg / 4 + 1] << 32;
        }
        break;
    default:
        break;
    }
    return val;
}

/* Helper: write to regs array with proper byte-lane handling */
static void pcibase_reg_write(PCIBaseState *s, hwaddr addr, uint64_t val, unsigned size)
{
    hwaddr reg = addr & ~3;
    hwaddr offset = addr & 3;

    if (reg >= sizeof(s->regs)) {
        return;
    }

    switch (size) {
    case 1: {
        uint32_t mask = 0xff << (offset * 8);
        s->regs[reg / 4] = (s->regs[reg / 4] & ~mask) | ((val & 0xff) << (offset * 8));
        break;
    }
    case 2: {
        if (offset == 0 || offset == 2) {
            uint32_t mask = 0xffff << (offset * 8);
            s->regs[reg / 4] = (s->regs[reg / 4] & ~mask) | ((val & 0xffff) << (offset * 8));
        } else {
            /* unaligned: offset must be 1 or 3 */
            uint32_t lo_mask, hi_mask;
            if (offset == 1) {
                lo_mask = 0xffffff00;
                hi_mask = 0x000000ff;
                s->regs[reg / 4] = (s->regs[reg / 4] & lo_mask) | ((val & 0xff) << 24);
                if (reg / 4 + 1 < sizeof(s->regs) / 4) {
                    s->regs[reg / 4 + 1] = (s->regs[reg / 4 + 1] & hi_mask) | ((val >> 8) & 0xff);
                }
            } else if (offset == 3) {
                lo_mask = 0x000000ff;
                hi_mask = 0xffffff00;
                s->regs[reg / 4] = (s->regs[reg / 4] & lo_mask) | ((val & 0xff) << 24);
                if (reg / 4 + 1 < sizeof(s->regs) / 4) {
                    s->regs[reg / 4 + 1] = (s->regs[reg / 4 + 1] & hi_mask) | ((val >> 8) & 0xff);
                }
            }
        }
        break;
    }
    case 4:
        s->regs[reg / 4] = val;
        break;
    case 8:
        s->regs[reg / 4] = val & 0xffffffff;
        if (reg / 4 + 1 < sizeof(s->regs) / 4) {
            s->regs[reg / 4 + 1] = (val >> 32) & 0xffffffff;
        }
        break;
    default:
        break;
    }
}

static void atl1e_soft_reset(PCIBaseState *s)
{
    memset(s->regs, 0, sizeof(s->regs));
    s->isr = 0;
    s->imr = 0;
    s->link_speed = 0;
    s->link_duplex = 0;
    s->phy_configured = false;
    s->re_autoneg = false;
    s->emi_ca = false;
    s->master_ctrl = 0;
    s->wol = 0;
    s->pm_ctrlstat = 0;
    s->power_state = 0;
    memset(&s->dma, 0, sizeof(s->dma));

    /* Power-on defaults */
    s->regs[REG_PHY_STATUS / 4] = PHY_STATUS_100M;
    /* MAC address default */
    s->regs[REG_MAC_STA_ADDR / 4] = 0x22334455;
    s->regs[(REG_MAC_STA_ADDR + 4) / 4] = 0x00000011;
}

/* MMIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case REG_ISR:
        val = s->isr;
        break;
    case REG_IMR:
        val = s->imr;
        break;
    case REG_TWSI_CTRL:
        /* Simulate completion of EEPROM load */
        val = 0;
        break;
    default:
        val = pcibase_reg_read(s, addr, size);
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case REG_ISR:
        s->isr &= ~val;
        pcibase_update_irq(s);
        break;
    case REG_IMR:
        s->imr = val;
        pcibase_update_irq(s);
        break;
    case REG_TWSI_CTRL:
        /* Ignore writes: controller always appears idle */
        break;
    case REG_MASTER_CTRL:
        pcibase_reg_write(s, addr, val, size);
        if (val & MASTER_CTRL_SOFT_RST) {
            atl1e_soft_reset(s);
            /* Clear the SOFT_RST bit to indicate reset complete */
            s->regs[REG_MASTER_CTRL / 4] &= ~MASTER_CTRL_SOFT_RST;
        }
        if (val & MASTER_CTRL_MANUAL_INT) {
            s->isr |= ISR_MANUAL;
            pcibase_update_irq(s);
        }
        break;
    default:
        pcibase_reg_write(s, addr, val, size);
        break;
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
    atl1e_soft_reset(s);
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
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_ATTANSIC );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_ATTANSIC_L1E );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_ETHERNET );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    s->num_bars = 1;
    s->bar_info[0] = (BARInfo){
        .index = 0,
        .type = BAR_TYPE_MMIO,
        .size = ATL1E_MMIO_SIZE,
        .name = "atl1e-mmio"
    };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = false;
    s->has_msix = false;

    pcibase_reset(DEVICE(s));
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
    .name = "ATL1E_pci",
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