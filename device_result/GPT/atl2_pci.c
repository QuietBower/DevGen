/*
 * QEMU PCI device model for Atheros ATL2 (atl2.c)
 *
 * This device only implements the minimal behavior required for the
 * Linux atl2 driver to probe, configure, and perform basic I/O.
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
#define PCIBASE_VENDOR_ID   0x1969
#define PCIBASE_DEVICE_ID   0x2048
#define PCIBASE_CLASS_ID    0x0200

#define REG_MAC_CTRL                  0x1480
#define REG_RX_HASH_TABLE             0x1490
#define REG_PAUSE_OFF_TH              0x15AA
#define REG_RXD_BASE_ADDR_LO          0x1554
#define REG_IRQ_MODU_TIMER_INIT       0x1408
#define REG_TXS_BASE_ADDR_LO          0x154C
#define REG_TXS_MEM_SIZE              0x1550
#define REG_TXD_BASE_ADDR_LO          0x1544
#define REG_MAC_IPG_IFG               0x1484
#define REG_PAUSE_ON_TH               0x15A8
#define REG_MAC_HALF_DUPLX_CTRL       0x1498
#define REG_TX_CUT_THRESH             0x1590
#define REG_DMAW                      0x15A0
#define REG_MB_RXD_RD_IDX             0x15F4
#define REG_MB_TXD_WR_IDX             0x15F0
#define REG_DESC_BASE_ADDR_HI         0x1540
#define REG_ISR                       0x1600
#define REG_MAC_STA_ADDR              0x1488
#define REG_RXD_BUF_NUM               0x1558
#define REG_MTU                       0x149C
#define REG_CMBDISDMA_TIMER           0x140E
#define REG_DMAR                      0x1580
#define REG_MASTER_CTRL               0x1400
#define REG_STS_RXD_OV                0x1704
#define REG_STS_RXS_OV                0x1708
#define REG_PM_CTRLSTAT               0x44
#define REG_PCIE_DLL_TX_CTRL1         0x1104
#define REG_PCIE_PHYMISC              0x1000
#define REG_SRAM_TXRAM_END            0x1500
#define REG_MANUAL_TIMER_INIT         0x1404
#define REG_IDLE_STATUS               0x1410
#define REG_MDIO_CTRL                 0x1414
#define REG_SERDES_LOCK               0x1424
#define REG_VPD_CAP                   0x6C
#define REG_PCIE_DEV_MISC_CTRL        0x21C
#define REG_SPI_FLASH_CTRL            0x200
#define REG_SPI_FLASH_CONFIG          0x20C
#define REG_PHY_ENABLE                0x140C
#define REG_TWSI_CTRL                 0x218
#define REG_SPI_DATA                  0x208
#define REG_SPI_ADDR                  0x204
#define REG_PCIE_CAP_LIST             0x58
#define REG_VPD_DATA                  0x70
#define REG_WOL_CTRL                  0x14A0
#define REG_PCIE_DLL_TX_CTRL1_DEF     0x568
#define REG_LTSSM_TEST_MODE           0x12FC
#define REG_SPI_FLASH_OP_READ         0x217
#define REG_SPI_FLASH_OP_WRSR         0x216
#define REG_SPI_FLASH_OP_PROGRAM      0x210
#define REG_SPI_FLASH_OP_CHIP_ERASE   0x212
#define REG_SPI_FLASH_OP_WREN         0x214
#define REG_SPI_FLASH_OP_RDID         0x213
#define REG_SPI_FLASH_OP_RDSR         0x215
#define REG_SPI_FLASH_OP_SC_ERASE     0x211
#define REG_IMR                       0x1604

#define ATL2_REGS_LEN                 42

#define ISR_PHY_LINKDOWN              0x10000000
#define ISR_MANUAL                    0x00000002
#define ISR_DMAR_TO_RST               0x00000200
#define ISR_DMAW_TO_RST               0x00000400
#define ISR_PHY                       0x00000800
#define ISR_TS_UPDATE                 0x00010000
#define ISR_RS_UPDATE                 0x00020000
#define ISR_TXS_OV                    0x00000010
#define ISR_TXF_UR                    0x00000008
#define ISR_TX_EARLY                  0x00040000
#define ISR_HOST_TXD_UR               0x00000080
#define ISR_HOST_RXD_OV               0x00000100
#define ISR_RXS_OV                    0x00000020
#define ISR_RXF_OV                    0x00000004
#define ISR_DIS_INT                   0x80000000

#define IMR_NORMAL_MASK ( \
    ISR_MANUAL        | \
    ISR_DMAR_TO_RST   | \
    ISR_DMAW_TO_RST   | \
    ISR_PHY           | \
    ISR_PHY_LINKDOWN  | \
    ISR_TS_UPDATE     | \
    ISR_RS_UPDATE)

#define ISR_TX_EVENT (ISR_TXF_UR | ISR_TXS_OV | ISR_HOST_TXD_UR | \
                      ISR_TS_UPDATE | ISR_TX_EARLY)
#define ISR_RX_EVENT (ISR_RXF_OV | ISR_RXS_OV | ISR_HOST_RXD_OV | \
                      ISR_RS_UPDATE)

/* New driver macros: we define them here so we can mirror simple behaviors
 * in our shadow registers and reset defaults. No hardware semantics beyond
 * these explicit bit definitions are inferred. */

#define MASTER_CTRL_SOFT_RST              0x1
#define MASTER_CTRL_MANUAL_INT           0x8

#define MAC_CTRL_PROMIS_EN               0x8000
#define MAC_CTRL_MC_ALL_EN               0x2000000
#define MAC_CTRL_RMV_VLAN                0x4000

#define MAC_IPG_IFG_IPGT_MASK            0x7F
#define MAC_IPG_IFG_IPGT_SHIFT           0
#define MAC_IPG_IFG_MIFG_MASK            0xFF
#define MAC_IPG_IFG_MIFG_SHIFT           8
#define MAC_IPG_IFG_IPGR1_MASK           0x7F
#define MAC_IPG_IFG_IPGR1_SHIFT          16
#define MAC_IPG_IFG_IPGR2_MASK           0x7F
#define MAC_IPG_IFG_IPGR2_SHIFT          24

#define MAC_HALF_DUPLX_CTRL_LCOL_MASK        0x3FF
#define MAC_HALF_DUPLX_CTRL_RETRY_MASK       0xF
#define MAC_HALF_DUPLX_CTRL_RETRY_SHIFT      12
#define MAC_HALF_DUPLX_CTRL_EXC_DEF_EN       0x10000
#define MAC_HALF_DUPLX_CTRL_ABEBT_SHIFT      20
#define MAC_HALF_DUPLX_CTRL_JAMIPG_MASK      0xF
#define MAC_HALF_DUPLX_CTRL_JAMIPG_SHIFT     24

#define MASTER_CTRL_ITIMER_EN            0x4

#define DMAR_EN                          0x1
#define DMAW_EN                          0x1

#define WOL_MAGIC_EN                     0x4
#define WOL_MAGIC_PME_EN                 0x8
#define WOL_LINK_CHG_EN                  0x10
#define WOL_LINK_CHG_PME_EN              0x20

#define PCIE_PHYMISC_FORCE_RCV_DET       0x4

#define PCIE_DLL_TX_CTRL1_SEL_NOR_CLK    0x400

#define LTSSM_TEST_MODE_DEF              0x6500

#define SPI_FLASH_CTRL_WAIT_READY        0x10000000

#define CUSTOM_SPI_CS_SETUP              2
#define SPI_FLASH_CTRL_CS_SETUP_MASK     0x3
#define SPI_FLASH_CTRL_CS_SETUP_SHIFT    24

#define CUSTOM_SPI_CLK_HI                2
#define SPI_FLASH_CTRL_CLK_HI_MASK       0x3
#define SPI_FLASH_CTRL_CLK_HI_SHIFT      22

#define CUSTOM_SPI_CLK_LO                2
#define SPI_FLASH_CTRL_CLK_LO_MASK       0x3
#define SPI_FLASH_CTRL_CLK_LO_SHIFT      20

#define CUSTOM_SPI_CS_HOLD               2
#define SPI_FLASH_CTRL_CS_HOLD_MASK      0x3
#define SPI_FLASH_CTRL_CS_HOLD_SHIFT     18

#define CUSTOM_SPI_CS_HI                 3
#define SPI_FLASH_CTRL_CS_HI_MASK        0x3
#define SPI_FLASH_CTRL_CS_HI_SHIFT       16

#define SPI_FLASH_CTRL_INS_MASK          0x7
#define SPI_FLASH_CTRL_INS_SHIFT         8
#define SPI_FLASH_CTRL_START             0x800
#define SPI_FLASH_CTRL_EN_VPD            0x2000

#define VPD_CAP_VPD_ADDR_MASK            0x7FFF
#define VPD_CAP_VPD_ADDR_SHIFT           16
#define VPD_CAP_VPD_FLAG                 0x80000000


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
    uint32_t regs[ATL2_REGS_LEN];

    /* DMA Context */


    uint32_t status_flags;

    uint32_t reset_state;

    uint32_t power_state;


};

static inline int reg_index_from_offset(hwaddr addr)
{
    switch (addr) {
    case REG_VPD_CAP:            return 0;
    case REG_SPI_FLASH_CTRL:     return 1;
    case REG_SPI_FLASH_CONFIG:   return 2;
    case REG_TWSI_CTRL:          return 3;
    case REG_PCIE_DEV_MISC_CTRL: return 4;
    case REG_MASTER_CTRL:        return 5;
    case REG_MANUAL_TIMER_INIT:  return 6;
    case REG_IRQ_MODU_TIMER_INIT:return 7;
    case REG_PHY_ENABLE:         return 8;
    case REG_CMBDISDMA_TIMER:    return 9;
    case REG_IDLE_STATUS:        return 10;
    case REG_MDIO_CTRL:          return 11;
    case REG_SERDES_LOCK:        return 12;
    case REG_MAC_CTRL:           return 13;
    case REG_MAC_IPG_IFG:        return 14;
    case REG_MAC_STA_ADDR:       return 15;
    case REG_MAC_STA_ADDR + 4:   return 16;
    case REG_RX_HASH_TABLE:      return 17;
    case REG_RX_HASH_TABLE + 4:  return 18;
    case REG_MAC_HALF_DUPLX_CTRL:return 19;
    case REG_MTU:                return 20;
    case REG_WOL_CTRL:           return 21;
    case REG_SRAM_TXRAM_END:     return 22;
    case REG_DESC_BASE_ADDR_HI:  return 23;
    case REG_TXD_BASE_ADDR_LO:   return 24;
    /* index 25 (REG_TXD_MEM_SIZE) is used in driver but not defined here */
    /* To avoid touching undefined macro, we don't map 25 by offset */
    case REG_TXS_BASE_ADDR_LO:   return 26;
    case REG_TXS_MEM_SIZE:       return 27;
    case REG_RXD_BASE_ADDR_LO:   return 28;
    case REG_RXD_BUF_NUM:        return 29;
    case REG_DMAR:               return 30;
    case REG_TX_CUT_THRESH:      return 31;
    case REG_DMAW:               return 32;
    case REG_PAUSE_ON_TH:        return 33;
    case REG_PAUSE_OFF_TH:       return 34;
    case REG_MB_TXD_WR_IDX:      return 35;
    case REG_MB_RXD_RD_IDX:      return 36;
    case REG_STS_RXD_OV:         return 37;
    case REG_ISR:                return 38;
    case REG_IMR:                return 39;
    default:
        return -1;
    }
}

/* Internal helper for status-triggered signaling. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    if (msi_enabled(pdev)) {
        if (s->intr_status & s->intr_mask) {
            msi_notify(pdev, 0);
        }
    } else {
        if (s->intr_status & s->intr_mask) {
            pci_set_irq(pdev, 1);
        } else {
            pci_set_irq(pdev, 0);
        }
    }
}

/* Device-initiated DMA logic based on driver access patterns.
 * The actual descriptor formats are not modeled; we only provide
 * a hook if needed later. Currently unused. */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    (void)s;
    (void)is_write;
}

/* MMIO/PIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Handle DWORD-aligned, little-endian accesses */
    if (size != 4) {
        /* The driver uses 32-bit accesses via ATL2_READ_REG/WRITE_REG */
        return 0;
    }

    /* Interrupt Status and Mask handled specially */
    if (addr == REG_ISR) {
        val = s->intr_status;
        return val;
    }
    if (addr == REG_IMR) {
        val = s->intr_mask;
        return val;
    }

    /* IDLE_STATUS is polled after reset; return shadow (0 means idle) */
    if (addr == REG_IDLE_STATUS) {
        int idx_idle = reg_index_from_offset(REG_IDLE_STATUS);
        if (idx_idle >= 0) {
            return s->regs[idx_idle];
        }
        return 0;
    }

    /* PCIE / PHY related registers return their shadows */
    if (addr == REG_PCIE_PHYMISC) {
        int idx = reg_index_from_offset(REG_PCIE_PHYMISC);
        if (idx >= 0) {
            return s->regs[idx];
        }
        return 0;
    }
    if (addr == REG_PCIE_DLL_TX_CTRL1) {
        int idx = reg_index_from_offset(REG_PCIE_DLL_TX_CTRL1);
        if (idx >= 0) {
            return s->regs[idx];
        }
        return 0;
    }
    if (addr == REG_LTSSM_TEST_MODE) {
        int idx = reg_index_from_offset(REG_LTSSM_TEST_MODE);
        if (idx >= 0) {
            return s->regs[idx];
        }
        return 0;
    }

    /* VPD capability / data are stored as simple shadows */
    if (addr == REG_VPD_CAP || addr == REG_VPD_DATA) {
        int idx = reg_index_from_offset(addr);
        if (idx >= 0) {
            return s->regs[idx];
        }
        return 0;
    }

    /* Generic register shadow array */
    int idx = reg_index_from_offset(addr);
    if (idx >= 0 && idx < ATL2_REGS_LEN) {
        val = s->regs[idx];
    } else {
        /* Unimplemented registers read as zero */
        val = 0;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        return;
    }

    /* Interrupt mask register (IMR) */
    if (addr == REG_IMR) {
        s->intr_mask = (uint32_t)val;
        /* The driver uses IMR_NORMAL_MASK and also writes 0 to disable */
        pcibase_update_irq(s);
        return;
    }

    /* Interrupt status register (ISR) with W1C semantics and DIS_INT bit. */
    if (addr == REG_ISR) {
        uint32_t w = (uint32_t)val;
        /* DIS_INT bit disables further interrupt assertion */
        if (w & ISR_DIS_INT) {
            s->intr_mask = 0;
        }
        /* Clear the bits that are written as 1 (W1C behavior) */
        s->intr_status &= ~w;
        pcibase_update_irq(s);
        return;
    }

    /* MASTER_CTRL: driver sets MASTER_CTRL_SOFT_RST and MASTER_CTRL_MANUAL_INT. */
    if (addr == REG_MASTER_CTRL) {
        int idx_mc = reg_index_from_offset(REG_MASTER_CTRL);
        uint32_t v = (uint32_t)val;

        if (idx_mc >= 0) {
            s->regs[idx_mc] = v;
        }

        /* Handle software reset explicitly when SOFT_RST bit is set. */
        if (v & MASTER_CTRL_SOFT_RST) {
            /* Perform a lightweight internal reset similar to pcibase_reset,
             * but without resetting PCI config space. */
            memset(s->regs, 0, sizeof(s->regs));
            s->intr_status = 0;
            s->intr_mask = 0;

            /* Re-establish key defaults that the driver re-checks after reset. */
            {
                int idx;
                idx = reg_index_from_offset(REG_IDLE_STATUS);
                if (idx >= 0) {
                    s->regs[idx] = 0; /* modules idle */
                }
                idx = reg_index_from_offset(REG_PCIE_PHYMISC);
                if (idx >= 0) {
                    s->regs[idx] = 0;
                }
                idx = reg_index_from_offset(REG_PCIE_DLL_TX_CTRL1);
                if (idx >= 0) {
                    s->regs[idx] = REG_PCIE_DLL_TX_CTRL1_DEF;
                }
                idx = reg_index_from_offset(REG_LTSSM_TEST_MODE);
                if (idx >= 0) {
                    s->regs[idx] = LTSSM_TEST_MODE_DEF;
                }
                idx = reg_index_from_offset(REG_PHY_ENABLE);
                if (idx >= 0) {
                    s->regs[idx] = 0;
                }
            }
            pcibase_update_irq(s);
        }

        /* If MANUAL_INT bit is set and corresponding mask is enabled,
         * raise a manual interrupt event. */
        if ((v & MASTER_CTRL_MANUAL_INT) && (s->intr_mask & ISR_MANUAL)) {
            s->intr_status |= ISR_MANUAL;
            pcibase_update_irq(s);
        }
        return;
    }

    /* DMA enable registers: store shadows only, driver uses DMAR_EN/DMAW_EN bits. */
    if (addr == REG_DMAR || addr == REG_DMAW) {
        int idx = reg_index_from_offset(addr);
        if (idx >= 0) {
            s->regs[idx] = (uint32_t)val;
        }
        return;
    }

    /* MDIO_CTRL: just store; PHY logic is not modeled. */
    if (addr == REG_MDIO_CTRL) {
        int idx = reg_index_from_offset(REG_MDIO_CTRL);
        if (idx >= 0) {
            s->regs[idx] = (uint32_t)val;
        }
        return;
    }

    /* MAC control, IPG/IFG, half-duplex, WOL, PCIe PHY/MISC etc. are stored
     * as simple shadows. The driver uses defined masks/shifts, but we do not
     * emulate side effects. */
    if (addr == REG_MAC_CTRL ||
        addr == REG_MAC_IPG_IFG ||
        addr == REG_MAC_HALF_DUPLX_CTRL ||
        addr == REG_WOL_CTRL ||
        addr == REG_PCIE_PHYMISC ||
        addr == REG_PCIE_DLL_TX_CTRL1 ||
        addr == REG_LTSSM_TEST_MODE ||
        addr == REG_SRAM_TXRAM_END ||
        addr == REG_MTU ||
        addr == REG_PHY_ENABLE ||
        addr == REG_PM_CTRLSTAT ||
        addr == REG_PCIE_DEV_MISC_CTRL ||
        addr == REG_IRQ_MODU_TIMER_INIT ||
        addr == REG_MANUAL_TIMER_INIT ||
        addr == REG_CMBDISDMA_TIMER) {
        int idx = reg_index_from_offset(addr);
        if (idx >= 0 && idx < ATL2_REGS_LEN) {
            s->regs[idx] = (uint32_t)val;
        }
        return;
    }

    /* SPI flash control/config and VPD control are stored as shadows; no flash
     * emulation is provided. */
    if (addr == REG_SPI_FLASH_CTRL ||
        addr == REG_SPI_FLASH_CONFIG ||
        addr == REG_SPI_ADDR ||
        addr == REG_SPI_DATA ||
        addr == REG_VPD_CAP ||
        addr == REG_VPD_DATA ||
        addr == REG_TWSI_CTRL) {
        int idx = reg_index_from_offset(addr);
        if (idx >= 0 && idx < ATL2_REGS_LEN) {
            s->regs[idx] = (uint32_t)val;
        }
        return;
    }

    /* PHY_ENABLE, WOL, timers etc. are stored in shadows. */
    {
        int idx = reg_index_from_offset(addr);
        if (idx >= 0 && idx < ATL2_REGS_LEN) {
            s->regs[idx] = (uint32_t)val;
        }
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

    /* Clear all internal state and initialize register defaults that
     * the driver expects immediately after reset. */
    memset(s->regs, 0, sizeof(s->regs));

    s->intr_status = 0;
    s->intr_mask = 0;

    /* IDLE_STATUS = 0 indicates all modules idle (atl2_reset_hw waits for 0) */
    {
        int idx = reg_index_from_offset(REG_IDLE_STATUS);
        if (idx >= 0) {
            s->regs[idx] = 0;
        }
    }

    /* PCIE / PHY defaults used during init */
    {
        int idx;
        idx = reg_index_from_offset(REG_PCIE_PHYMISC);
        if (idx >= 0) {
            s->regs[idx] = 0; /* no special flags */
        }
        idx = reg_index_from_offset(REG_PCIE_DLL_TX_CTRL1);
        if (idx >= 0) {
            s->regs[idx] = REG_PCIE_DLL_TX_CTRL1_DEF;
        }
        idx = reg_index_from_offset(REG_LTSSM_TEST_MODE);
        if (idx >= 0) {
            s->regs[idx] = LTSSM_TEST_MODE_DEF;
        }
    }

    /* PHY_ENABLE default 0, later set to 1 by driver */
    {
        int idx = reg_index_from_offset(REG_PHY_ENABLE);
        if (idx >= 0) {
            s->regs[idx] = 0;
        }
    }

    s->status_flags = 0;
    s->reset_state = 0;
    s->power_state = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCIBASE_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCIBASE_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCIBASE_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* Enable legacy INTx by default */
    pci_set_word(pci_conf + PCI_COMMAND,
                 PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);

    /* Optional MSI support (the driver tries to enable MSI) */
    s->has_msi = (msi_init(pdev, 0, 1, true, false, errp) == 0);
    s->has_msix = false;

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x4000; /* covers all used MMIO offsets */
    s->bar_info[0].name = "atl2-mmio";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = NULL;
    }
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize internal state similar to reset */
    memset(s->regs, 0, sizeof(s->regs));
    s->intr_status = 0;
    s->intr_mask = 0;

    int idx;
    idx = reg_index_from_offset(REG_IDLE_STATUS);
    if (idx >= 0) {
        s->regs[idx] = 0; /* all modules idle */
    }
    idx = reg_index_from_offset(REG_PCIE_PHYMISC);
    if (idx >= 0) {
        s->regs[idx] = 0;
    }
    idx = reg_index_from_offset(REG_PCIE_DLL_TX_CTRL1);
    if (idx >= 0) {
        s->regs[idx] = REG_PCIE_DLL_TX_CTRL1_DEF;
    }
    idx = reg_index_from_offset(REG_LTSSM_TEST_MODE);
    if (idx >= 0) {
        s->regs[idx] = LTSSM_TEST_MODE_DEF;
    }
    idx = reg_index_from_offset(REG_PHY_ENABLE);
    if (idx >= 0) {
        s->regs[idx] = 0;
    }

    s->status_flags = 0;
    s->reset_state = 0;
    s->power_state = 0;
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
