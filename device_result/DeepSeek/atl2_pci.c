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

#define TYPE_PCIBASE_DEVICE "atl2_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x1969 /* PCI_VENDOR_ID_ATTANSIC */
#define DEVICE_ID 0x2048 /* PCI_DEVICE_ID_ATTANSIC_L2 */
#define CLASS_ID  0x0200 /* PCI_CLASS_NETWORK_ETHERNET */

/* Register offsets and bit definitions from atl2 driver */
#define REG_MAC_CTRL                 0x1480
#define MAC_CTRL_PROMIS_EN           0x8000
#define REG_RX_HASH_TABLE            0x1490
#define MAC_CTRL_MC_ALL_EN           0x2000000
#define MAC_HALF_DUPLX_CTRL_LCOL_MASK 0x3FF
#define REG_MASTER_CTRL              0x1400
#define MAC_IPG_IFG_IPGT_SHIFT       0
#define REG_TXD_BASE_ADDR_LO         0x1544
#define REG_DMAW                     0x15A0
#define ISR_PHY_LINKDOWN             0x10000000
#define REG_RXD_BASE_ADDR_LO         0x1554
#define REG_PAUSE_OFF_TH             0x15AA
#define DMAR_EN                      0x1
#define MAC_IPG_IFG_MIFG_SHIFT       8
#define DMAW_EN                      0x1
#define REG_MAC_HALF_DUPLX_CTRL      0x1498
#define MAC_HALF_DUPLX_CTRL_ABEBT_SHIFT 20
#define REG_ISR                      0x1600
#define MAC_HALF_DUPLX_CTRL_EXC_DEF_EN  0x10000
#define REG_MB_RXD_RD_IDX            0x15F4
#define REG_MB_TXD_WR_IDX            0x15f0
#define MAC_HALF_DUPLX_CTRL_JAMIPG_SHIFT 24
#define REG_DESC_BASE_ADDR_HI        0x1540
#define MAC_HALF_DUPLX_CTRL_RETRY_SHIFT 12
#define MAC_HALF_DUPLX_CTRL_JAMIPG_MASK 0xF
#define REG_RXD_BUF_NUM              0x1558
#define MAC_IPG_IFG_IPGT_MASK        0x7F
#define REG_CMBDISDMA_TIMER          0x140E
#define MAC_IPG_IFG_IPGR2_MASK       0x7F
#define MAC_IPG_IFG_IPGR1_SHIFT      16
#define REG_DMAR                     0x1580
#define REG_MAC_STA_ADDR             0x1488
#define REG_MAC_IPG_IFG              0x1484
#define REG_TX_CUT_THRESH            0x1590
#define REG_TXS_BASE_ADDR_LO         0x154C
#define MASTER_CTRL_ITIMER_EN        0x4
#define MAC_IPG_IFG_MIFG_MASK        0xFF
#define MAC_HALF_DUPLX_CTRL_RETRY_MASK 0xF
#define MAC_IPG_IFG_IPGR2_SHIFT      24
#define REG_TXD_MEM_SIZE             0x1548
#define REG_TXS_MEM_SIZE             0x1550
#define REG_IRQ_MODU_TIMER_INIT      0x1408
#define REG_PAUSE_ON_TH              0x15A8
#define MAC_IPG_IFG_IPGR1_MASK       0x7F
#define REG_MTU                      0x149C
#define IMR_NORMAL_MASK              (ISR_MANUAL | ISR_DMAR_TO_RST | ISR_DMAW_TO_RST | ISR_PHY | ISR_PHY_LINKDOWN | ISR_TS_UPDATE | ISR_RS_UPDATE)
#define REG_IMR                      0x1604
#define MAC_CTRL_RMV_VLAN            0x4000
#define ISR_PHY                      0x800
#define ISR_DMAW_TO_RST              0x400
#define ISR_TX_EVENT                 (ISR_TXF_UR | ISR_TXS_OV | ISR_HOST_TXD_UR | ISR_TS_UPDATE | ISR_TX_EARLY)
#define ISR_RX_EVENT                 (ISR_RXF_OV | ISR_RXS_OV | ISR_HOST_RXD_OV | ISR_RS_UPDATE)
#define ISR_DIS_INT                  0x80000000
#define ISR_MANUAL                   2
#define ISR_DMAR_TO_RST              0x200
#define MASTER_CTRL_MANUAL_INT       0x8
#define REG_STS_RXD_OV               0x1704
#define REG_STS_RXS_OV               0x1708
#define MII_CR_RESET                 0x8000
#define MII_CR_RESTART_AUTO_NEG      0x0200
#define MII_CR_AUTO_NEG_EN           0x1000
#define MAC_CTRL_RX_FLOW             8
#define MAC_CTRL_RX_EN               2
#define MAC_CTRL_TX_EN               1
#define MAC_CTRL_MACLP_CLK_PHY       0x8000000
#define MAC_CTRL_PRMLEN_SHIFT        10
#define MAC_CTRL_PAD                 0x80
#define MAC_CTRL_TX_FLOW             4
#define MAC_CTRL_BC_EN               0x4000000
#define MAC_CTRL_ADD_CRC             0x40
#define MAC_CTRL_DUPLX               0x20
#define MAC_CTRL_HALF_LEFT_BUF_MASK  0xF
#define MAC_CTRL_HALF_LEFT_BUF_SHIFT 28
#define MAC_CTRL_PRMLEN_MASK         0xF
#define HALF_DUPLEX                  1
#define MEDIA_TYPE_10M_HALF          4
#define MEDIA_TYPE_10M_FULL          3
#define SPEED_100                    100
#define MEDIA_TYPE_100M_FULL         1
#define MEDIA_TYPE_100M_HALF         2
#define SPEED_10                     10
#define REG_PM_CTRLSTAT              0x44
#define PCIE_DLL_TX_CTRL1_SEL_NOR_CLK 0x400
#define WOL_MAGIC_PME_EN             0x8
#define WOL_MAGIC_EN                 0x4
#define ATLX_WUFC_LNKC               0x00000001
#define PCIE_PHYMISC_FORCE_RCV_DET   0x4
#define WOL_LINK_CHG_PME_EN          0x20
#define WOL_LINK_CHG_EN              0x10
#define REG_PCIE_PHYMISC             0x1000
#define REG_WOL_CTRL                 0x14A0
#define REG_PCIE_DLL_TX_CTRL1        0x1104
#define ATLX_WUFC_MAG                0x00000002
#define MEDIA_TYPE_AUTO_SENSOR       0
#define REG_PCIE_DEV_MISC_CTRL       0x21C
#define REG_SPI_FLASH_CONFIG         0x20C
#define REG_MDIO_CTRL                0x1414
#define REG_SERDES_LOCK              0x1424
#define REG_MANUAL_TIMER_INIT        0x1404
#define REG_VPD_CAP                  0x6C
#define REG_SPI_FLASH_CTRL           0x200
#define REG_TWSI_CTRL                0x218
#define REG_PHY_ENABLE               0x140C
#define REG_IDLE_STATUS              0x1410
#define REG_SRAM_TXRAM_END           0x1500
#define ATLX_WUFC_BC                 0x00000010
#define ATLX_WUFC_EX                 0x00000004
#define ATLX_WUFC_MC                 0x00000008
#define CMD_BUS_MASTER               0x0004
#define PCI_REG_COMMAND              0x04
#define MASTER_CTRL_SOFT_RST         0x1
#define CMD_MEMORY_SPACE             0x0002
#define CMD_IO_SPACE                 0x0001
#define SPI_FLASH_CTRL_CLK_HI_SHIFT  22
#define SPI_FLASH_CTRL_CS_HOLD_MASK  0x3
#define SPI_FLASH_CTRL_CS_HOLD_SHIFT 18
#define SPI_FLASH_CTRL_CS_HI_MASK    0x3
#define SPI_FLASH_CTRL_CLK_HI_MASK   0x3
#define SPI_FLASH_CTRL_START         0x800
#define SPI_FLASH_CTRL_CLK_LO_MASK   0x3
#define SPI_FLASH_CTRL_CS_HI_SHIFT   16
#define SPI_FLASH_CTRL_CS_SETUP_SHIFT 24
#define SPI_FLASH_CTRL_CS_SETUP_MASK 0x3
#define REG_SPI_DATA                 0x208
#define SPI_FLASH_CTRL_WAIT_READY    0x10000000
#define SPI_FLASH_CTRL_INS_SHIFT     8
#define REG_SPI_ADDR                 0x204
#define SPI_FLASH_CTRL_CLK_LO_SHIFT  20
#define SPI_FLASH_CTRL_INS_MASK      0x7
#define REG_LTSSM_TEST_MODE          0x12FC
#define LTSSM_TEST_MODE_DEF          0x6500
#define PCIE_DLL_TX_CTRL1_DEF        0x568
#define REG_SPI_FLASH_OP_READ        0x217
#define REG_SPI_FLASH_OP_PROGRAM     0x210
#define REG_SPI_FLASH_OP_CHIP_ERASE  0x212
#define REG_SPI_FLASH_OP_WRSR        0x216
#define REG_SPI_FLASH_OP_WREN        0x214
#define REG_SPI_FLASH_OP_RDID        0x213
#define REG_SPI_FLASH_OP_SC_ERASE    0x211
#define REG_SPI_FLASH_OP_RDSR        0x215
#define MII_ATLX_PSSR_100MBS         0x4000
#define MII_ATLX_PSSR_DPLX           0x2000
#define MII_ATLX_PSSR_SPD_DPLX_RESOLVED 0x0800
#define MII_ATLX_PSSR_10MBS          0x0000
#define MII_ATLX_PSSR                0x11
#define ATLX_ERR_PHY_RES             8
#define ATLX_ERR_PHY_SPEED           7
#define MII_ATLX_PSSR_SPEED          0xC000
#define MDIO_WAIT_TIMES              10
#define MDIO_REG_ADDR_MASK           0x1F
#define MDIO_START                   0x800000
#define MDIO_SUP_PREAMBLE            0x400000
#define MDIO_CLK_25_4                0
#define ATLX_ERR_PHY                 2
#define MDIO_BUSY                    0x8000000
#define MDIO_REG_ADDR_SHIFT          16
#define MDIO_RW                      0x200000
#define MDIO_CLK_SEL_SHIFT           24
#define MDIO_DATA_MASK               0xFFFF
#define MDIO_DATA_SHIFT              0
#define MII_AR_DEFAULT_CAP_MASK      0x0DE0
#define MII_AR_PAUSE                 0x0400
#define MII_AR_ASM_DIR               0x0800
#define MII_AR_10T_HD_CAPS           0x0020
#define MII_AR_10T_FD_CAPS           0x0040
#define MII_AR_SPEED_MASK            0x01E0
#define MII_AR_100TX_FD_CAPS         0x0100
#define MII_AR_100TX_HD_CAPS         0x0080
#define MII_DBG_DATA                 0x1E
#define MII_DBG_ADDR                 0x1D
#define REG_PCIE_CAP_LIST            0x58
#define SPI_FLASH_CTRL_EN_VPD        0x2000
#define VPD_CAP_VPD_ADDR_MASK        0x7FFF
#define VPD_CAP_VPD_ADDR_SHIFT       16
#define VPD_CAP_VPD_FLAG             0x80000000
#define REG_VPD_DATA                 0x70
#define ISR_TS_UPDATE                0x10000
#define ISR_RS_UPDATE                0x20000
#define ISR_TXS_OV                   0x10
#define ISR_TX_EARLY                 0x40000
#define ISR_HOST_TXD_UR              0x80
#define ISR_TXF_UR                   8
#define ISR_RXS_OV                   0x20
#define ISR_HOST_RXD_OV              0x100
#define ISR_RXF_OV                   4

/* Additional MII register numbers */
#define MII_BMCR         0x00
#define MII_BMSR         0x01
#define MII_ADVERTISE    0x04
#define MII_LPA          0x05

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
    uint32_t isr;
    uint32_t imr;

    /* DMA Context */
    dma_addr_t ring_dma;
    dma_addr_t txd_dma;
    dma_addr_t txs_dma;
    dma_addr_t rxd_dma;
    uint32_t txd_ring_size;
    uint32_t txs_ring_size;
    uint32_t rxd_ring_size;

    /* MMIO region shadow */
    MemoryRegion mmio;
    uint32_t mmio_regs[0x2000/4]; /* 0x2000 bytes MMIO space */

    /* Power management */
    uint32_t pm_ctrlstat;

    /* MAC address */
    uint8_t mac_addr[6];

    /* PHY registers */
    uint16_t phy_regs[32];
};

/* SPI flash device table and descriptor structures from driver */
struct atl2_spi_flash_dev {
	const char *manu_name;	/* manufacturer id */
	/* op-code */
	uint8_t cmdWRSR;
	uint8_t cmdREAD;
	uint8_t cmdPROGRAM;
	uint8_t cmdWREN;
	uint8_t cmdWRDI;
	uint8_t cmdRDSR;
	uint8_t cmdRDID;
	uint8_t cmdSECTOR_ERASE;
	uint8_t cmdCHIP_ERASE;
};
static struct atl2_spi_flash_dev flash_table[] =
{
/* MFR    WRSR  READ  PROGRAM WREN  WRDI  RDSR  RDID  SECTOR_ERASE CHIP_ERASE */
{"Atmel", 0x0,  0x03, 0x02,   0x06, 0x04, 0x05, 0x15, 0x52,        0x62 },
{"SST",   0x01, 0x03, 0x02,   0x06, 0x04, 0x05, 0x90, 0x20,        0x60 },
{"ST",    0x01, 0x03, 0x02,   0x06, 0x04, 0x05, 0xAB, 0xD8,        0xC7 },
};

/* Forward declarations */
static void pcibase_update_irq(PCIBaseState *s);
static void pcibase_reset(DeviceState *dev);

/* Update interrupt status based on ISR and IMR */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t isr = s->mmio_regs[REG_ISR/4];
    uint32_t imr = s->mmio_regs[REG_IMR/4];
    uint32_t pending = isr & imr;

    if (pending) {
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

/* MMIO Read Handler */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    uint32_t offset = addr;

    if (offset + size > 0x2000) {
        return 0;
    }

    switch (size) {
    case 4:
        val = s->mmio_regs[offset/4];
        break;
    case 2:
        val = *(uint16_t *)((uint8_t *)s->mmio_regs + offset);
        break;
    case 1:
        val = *(uint8_t *)((uint8_t *)s->mmio_regs + offset);
        break;
    default:
        return 0;
    }
    return val;
}

/* MMIO Write Handler */
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t offset = addr;

    if (offset + size > 0x2000) {
        return;
    }

    /* Special handling for registers with side effects */
    if (offset == REG_ISR) {
        /* W1C: bits written with 1 clear those bits in ISR */
        uint32_t isr_val = s->mmio_regs[REG_ISR/4];
        isr_val &= ~val;
        s->mmio_regs[REG_ISR/4] = isr_val;
        pcibase_update_irq(s);
        return;
    } else if (offset == REG_IMR) {
        s->mmio_regs[REG_IMR/4] = val;
        pcibase_update_irq(s);
        return;
    } else if (offset == REG_MDIO_CTRL) {
        uint32_t mdio_val = val;
        if (mdio_val & MDIO_START) {
            uint32_t reg_addr = (mdio_val >> MDIO_REG_ADDR_SHIFT) & MDIO_REG_ADDR_MASK;
            if (mdio_val & MDIO_RW) { /* Read operation */
                uint16_t data = s->phy_regs[reg_addr];
                mdio_val = (mdio_val & ~(MDIO_START | MDIO_BUSY | 0xFFFF)) | data;
            } else { /* Write operation */
                uint16_t data = (mdio_val >> MDIO_DATA_SHIFT) & MDIO_DATA_MASK;
                s->phy_regs[reg_addr] = data;
                mdio_val &= ~(MDIO_START | MDIO_BUSY);
            }
        }
        s->mmio_regs[REG_MDIO_CTRL/4] = mdio_val;
        return;
    } else if (offset == REG_MASTER_CTRL) {
        /* Check for soft reset */
        if (val & MASTER_CTRL_SOFT_RST) {
            /* Simulate immediate reset completion; IDLE_STATUS will be 0 */
            /* No extra action needed, just store the value */
        }
        s->mmio_regs[REG_MASTER_CTRL/4] = val;
        return;
    }

    /* Generic write */
    switch (size) {
    case 4:
        s->mmio_regs[offset/4] = val;
        break;
    case 2:
        *(uint16_t *)((uint8_t *)s->mmio_regs + offset) = val;
        break;
    case 1:
        *(uint8_t *)((uint8_t *)s->mmio_regs + offset) = val;
        break;
    }
}

/* PIO handlers (unused) */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
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

    /* Clear MMIO space */
    memset(s->mmio_regs, 0, sizeof(s->mmio_regs));

    /* Set default MAC address: 00:13:74:00:5c:38 */
    uint8_t mac[6] = {0x00, 0x13, 0x74, 0x00, 0x5c, 0x38};
    uint32_t low = (mac[2] << 24) | (mac[3] << 16) | (mac[4] << 8) | mac[5];
    uint32_t high = (mac[0] << 8) | mac[1];
    s->mmio_regs[REG_MAC_STA_ADDR/4] = low;
    s->mmio_regs[(REG_MAC_STA_ADDR+4)/4] = high;

    /* Initialize PHY registers with default link-up values */
    s->phy_regs[MII_BMCR] = 0x1000;      /* Auto-negotiation enabled */
    s->phy_regs[MII_BMSR] = 0x7829;      /* Link up, auto-negotiate complete */
    s->phy_regs[MII_ADVERTISE] = 0x01E1;  /* Advertise 10/100 full/half */
    s->phy_regs[MII_LPA] = 0x41E1;       /* Link partner advertisement */
    s->phy_regs[MII_ATLX_PSSR] = 0x5800; /* 100M full duplex, speed/duplex resolved */
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

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID, VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = 0x2000, .name = "atl2-mmio" };
    s->num_bars = 1;
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    if (msi_init(pdev, 0, 1, true, false, errp) < 0) {
        error_setg(errp, "MSI initialization failed");
        return;
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

static const VMStateDescription vmstate_pcibase = {
    .name = "atl2_pci",
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
