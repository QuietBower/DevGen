/*
 * QEMU PCI device model for Marvell Yukon (skge) NIC - minimal functional model
 *
 * This model is tailored to satisfy the Linux skge driver probe and basic
 * initialization. It exposes a single MMIO BAR with a 0x4000-byte register
 * window and implements a small subset of registers needed for reset,
 * basic configuration, and interrupt handling.
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

#include "hw/pci/pci_ids.h"

#ifndef PCI_VENDOR_ID_3COM
#define PCI_VENDOR_ID_3COM 0x10b7
#endif

#define TYPE_PCIBASE_DEVICE "skge_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI IDs as used by skge (first table entry in driver is 3Com PCI_VENDOR_ID_3COM, 0x1700) */
#define SKGE_VENDOR_ID   PCI_VENDOR_ID_3COM
#define SKGE_DEVICE_ID   0x1700
#define SKGE_CLASS_ID    PCI_CLASS_NETWORK_ETHERNET

/* PCI config registers used in driver */
#define SKGE_PCI_DEV_REG1       0x40
#define SKGE_PCI_DEV_REG2       0x44
#define SKGE_PCI_PHY_COMA       0x08000000u
#define SKGE_PCI_REV_DESC       (1u << 2)

#define SKGE_EEPROM_MAGIC       0x9933aabbu

/* Helper macros from driver (address arithmetic) */
#define SKGE_SK_REG(port, reg)  (((port) << 7) + (uint16_t)(reg))
#define SKGE_WOL_REGS(port, x)  ((x) + (port) * 0x80)

/* We only model a minimal subset of internal register space to satisfy
 * skge_reset() and related functions. Most driver accesses are simple
 * read/write without depending on specific values, except for a few
 * identification and status bits.
 */

/* Top-level register blocks (offsets from datasheets/driver names).
 * Only the ones explicitly read back by the driver are given behavior.
 */

/* B0 (Base) registers -- keep these in sync with Linux skge.h. */
#define B0_CTST            0x0004  /* Control/Status (8/16-bit access in driver) */
#define B0_LED             0x0006  /* LED control (16-bit writes) */
#define B0_POWER_CTRL      0x0007  /* Power control (8-bit writes) */
#define B0_ISRC            0x0008  /* Interrupt source (32-bit read) */
#define B0_IMSK            0x000c  /* Interrupt mask (32-bit read/write) */
#define B0_HWE_ISRC        0x0010  /* HW error interrupt source (32-bit read) */
#define B0_HWE_IMSK        0x0014  /* HW error interrupt mask (32-bit write) */
#define B0_SP_ISRC         0x0018  /* Special IRQ source (32-bit read) */
#define B0_R1_CSR          0x0060
#define B0_R2_CSR          0x0064

/* B2 block */
#define B2_MAC_1           0x0100  /* station address for port 1 */
#define B2_MAC_2           0x0108  /* station address for port 2 */
#define B2_PMD_TYP         0x0119  /* physical media type */
#define B2_MAC_CFG         0x011a  /* MAC config/chip revision */
#define B2_CHIP_ID         0x011b  /* 8-bit chip id */
#define B2_E_0             0x011c  /* EEPROM byte 0 (RAM size) */
#define B2_E_1             0x011d  /* EEPROM byte 1 (PHY type) */
#define B2_FAR             0x0120  /* flash address register */
#define B2_TI_CTRL         0x0138  /* 8-bit timer control */
#define B2_IRQM_INI        0x0140  /* 32-bit IRQ moderation init */
#define B2_IRQM_CTRL       0x0148  /* 8-bit IRQ moderation ctrl */
#define B2_IRQM_MSK        0x014c  /* 32-bit IRQ moderation mask */
#define B2_TST_CTRL1       0x0158  /* 8-bit test ctrl1 */
#define B2_TST_CTRL2       0x0159  /* 8-bit test ctrl2 */
#define B2_GP_IO           0x015c  /* 32-bit general purpose IO */

/* B3 block */
#define B3_RI_WTO_R1       0x0190  /* timers, modeled as 8-bit regs */
#define B3_RI_WTO_XA1      0x0191
#define B3_RI_WTO_XS1      0x0192
#define B3_RI_RTO_R1       0x0193
#define B3_RI_RTO_XA1      0x0194
#define B3_RI_RTO_XS1      0x0195
#define B3_RI_WTO_R2       0x0196
#define B3_RI_WTO_XA2      0x0197
#define B3_RI_WTO_XS2      0x0198
#define B3_RI_RTO_R2       0x0199
#define B3_RI_RTO_XA2      0x019a
#define B3_RI_RTO_XS2      0x019b
#define B3_RI_CTRL         0x01a0  /* 16-bit RAM interface control */
#define B3_MA_TOINI_RX1    0x01b0  /* 8-bit timeout init */
#define B3_MA_TOINI_RX2    0x01b1
#define B3_MA_TOINI_TX1    0x01b2
#define B3_MA_TOINI_TX2    0x01b3
#define B3_MA_TO_CTRL      0x01b8  /* 16-bit MAC arbiter ctrl */
#define B3_MA_RCINI_RX1    0x01c0
#define B3_MA_RCINI_RX2    0x01c1
#define B3_MA_RCINI_TX1    0x01c2
#define B3_MA_RCINI_TX2    0x01c3
#define B3_PA_TOINI_RX1    0x01d0  /* 32-bit */
#define B3_PA_TOINI_RX2    0x01d4
#define B3_PA_TOINI_TX1    0x01d8
#define B3_PA_TOINI_TX2    0x01dc
#define B3_PA_CTRL         0x01f0  /* 16-bit packet arbiter ctrl */

/* B0/B2/B3 status/control bits used by driver (only those tested) */
#define CS_RST_SET         0x01
#define CS_RST_CLR         0x02
#define CS_MRST_CLR        (1 << 3)
#define CS_CLK_RUN_HOT     (1 << 13)
#define CS_CLK_RUN_RST     (1 << 12)
#define CS_CLK_RUN_ENA     (1 << 11)

#define LED_STAT_ON        (1 << 1)
#define LED_STAT_OFF       (1 << 0)

/* New driver snippet: TIM_STOP/TIM_START redefine actual bit positions */
#define TIM_STOP           (1 << 1)
#define TIM_START          (1 << 2)
#define TIM_CLR_IRQ        (1 << 0)

#define RI_RST_CLR         (1 << 1)
#define RI_CLR_RD_PERR     (1 << 9)
#define RI_CLR_WR_PERR     (1 << 8)

/* New driver snippet: update interrupt bit positions */
#define IS_EXT_REG         (1u << 23)
#define IS_XA1_F           (1u << 7)
#define IS_R1_F            (1u << 16)
#define IS_XA2_F           (1u << 1)
#define IS_R2_F            (1u << 13)
#define IS_PA_TO_RX1       (1u << 29)
#define IS_PA_TO_TX1       (1u << 27)
#define IS_PA_TO_RX2       (1u << 28)
#define IS_PA_TO_TX2       (1u << 26)
#define IS_MAC1            (1u << 21)
#define IS_MAC2            (1u << 19)
#define IS_HW_ERR          (1u << 31)

#define IS_ERR_MSK         0xffffffffu

/* Simple queue / RAM buffer register helper macros
 * The driver uses Q_ADDR(q, reg) and RB_ADDR(q, reg); we only need a few
 * specific values used in skge_up()/down()/rx_stop(). Instead of full
 * macros, we collapse them to concrete offsets.
 */

/* For our minimalist model, treat queue/RB control as simple CSRs: */
#define Q_R1_CSR_OFFSET     0x0100
#define Q_R2_CSR_OFFSET     0x0104
#define Q_XA1_CSR_OFFSET    0x0108
#define Q_XA2_CSR_OFFSET    0x010c

#define RB_R1_CTRL_OFFSET   0x0180
#define RB_R2_CTRL_OFFSET   0x0184
#define RB_XA1_CTRL_OFFSET  0x0188
#define RB_XA2_CTRL_OFFSET  0x018c

/* New driver snippet: CSR_* bits get concrete definitions consistent with snippet */
#define CSR_START           (1L << 4)
#define CSR_STOP            0x02
#define CSR_IRQ_CL_F        (1L << 1)
#define CSR_IRQ_CL_P        (1L << 3)
#define CSR_SET_RESET       (0) /* composite in HW; we treat as opaque */
#define CSR_CLR_RESET       (0)

#define CSR_SET_RESET_MASK  0xffffffffu
#define CSR_CLR_RESET_MASK  0xffffffffu

#define RB_RST_SET          0x01
#define RB_RST_CLR          0x02
#define RB_DIS_OP_MD        0x04
#define RB_ENA_OP_MD        0x08
#define RB_ENA_STFWD        0x10

/* GMAC / PHY related registers used for simple identification/LED */
#define GMAC_BASE(port)        (0x0200 + (port) * 0x100)
#define GMAC_IRQ_SRC(port)     (GMAC_BASE(port) + 0x00) /* 8-bit */
#define GMAC_IRQ_MSK(port)     (GMAC_BASE(port) + 0x01) /* 8-bit */
#define GMAC_CTRL(port)        (GMAC_BASE(port) + 0x02) /* 8-bit */

/* New driver snippet: GMAC_DEF_MSK from GM_IS_* bits */
#define GM_IS_RX_FF_OR         0x01
#define GM_IS_TX_FF_UR         0x02
#define GMAC_DEF_MSK           (GM_IS_RX_FF_OR | GM_IS_TX_FF_UR)

#define GMC_PAUSE_ON           0x01
#define GMC_PAUSE_OFF          0x00
#define GMF_CLI_RX_FO          0x01
#define GMF_CLI_TX_FU          0x02
#define GMF_CLI_TX_PE          0x04
#define GMF_CLI_TX_FC          0x08
#define GMF_RST_SET            0x80
#define GMF_RST_CLR            0x00

/* Link LED registers via SK_REG(port, LNK_LED_REG) etc.
 * We approximate by assigning some offsets within per-port block.
 */
#define PORT_BLOCK(port)       (0x0300 + (port) * 0x80)
#define LNK_LED_REG(port)      (PORT_BLOCK(port) + 0x00) /* 8-bit */
#define RX_GMF_CTRL_T(port)    (PORT_BLOCK(port) + 0x10) /* 8-bit */
#define TX_GMF_CTRL_T(port)    (PORT_BLOCK(port) + 0x11) /* 8-bit */
#define GMAC_LINK_CTRL(port)   (PORT_BLOCK(port) + 0x12) /* 16-bit */
#define GPHY_CTRL(port)        (PORT_BLOCK(port) + 0x20) /* 32-bit */
#define TXA_CTRL(port)         (PORT_BLOCK(port) + 0x30) /* 8-bit */
#define TX_MFF_CTRL1(port)     (PORT_BLOCK(port) + 0x40) /* 16-bit */
#define TX_MFF_CTRL2(port)     (PORT_BLOCK(port) + 0x42) /* 8-bit */
#define RX_MFF_CTRL1(port)     (PORT_BLOCK(port) + 0x44) /* 16-bit */
#define RX_MFF_CTRL2(port)     (PORT_BLOCK(port) + 0x46) /* 8-bit */

#define LED_REG_ON             0x01
#define LED_REG_OFF            0x00

#define GMLC_RST_SET           0x0001
#define GMLC_RST_CLR           0x0002

#define TXA_ENA_ARB            0x01
#define TXA_DIS_FSYNC          0x02
#define TXA_DIS_ALLOC          0x04
#define TXA_STOP_RC            0x08

#define MFF_SET_MAC_RST        0x0001
#define MFF_CLR_MAC_RST        0x0002
#define MFF_CLR_INSTAT         0x0004
#define MFF_CLR_PERR           0x0008
#define MFF_RST_SET            0x01
#define MFF_RST_CLR            0x00
#define MFF_ENA_TIM_PAT        0x0010
#define MFF_ENA_OP_MD          0x02
#define MFF_TX_CTRL_DEF        0x0020
#define MFF_ENA_FLUSH          0x0040
#define MFF_DIS_PAUSE          0x0080
#define MFF_ENA_PAUSE          0x0100

/* B0/B3 PA_CTRL bits that the driver writes/reads; we just store them. */
#define PA_RST_CLR             0x0001
#define PA_ENA_TO_TX1          0x0002
#define PA_ENA_TO_TX2          0x0004
#define PA_CLR_TO_TX1          0x0008
#define PA_CLR_TO_TX2          0x0010
#define PA_CLR_TO_RX1          0x0020
#define PA_CLR_TO_RX2          0x0040

/* B0_HWE_ISRC error bits (we don't emulate actual errors) */
#define IS_NO_STAT_M1          0x00000001u
#define IS_NO_TIST_M1          0x00000002u
#define IS_NO_STAT_M2          0x00000004u
#define IS_NO_TIST_M2          0x00000008u
#define IS_RAM_RD_PAR          0x00000010u
#define IS_RAM_WR_PAR          0x00000020u
#define IS_M1_PAR_ERR          0x00000040u
#define IS_M2_PAR_ERR          0x00000080u
#define IS_R1_PAR_ERR          0x00000100u
#define IS_R2_PAR_ERR          0x00000200u
#define IS_IRQ_TIST_OV         0x00000400u
#define IS_IRQ_MST_ERR         (1L << 23)
#define IS_IRQ_STAT            (1L << 24)
#define IS_IRQ_SENSOR          0x00002000u

/* RAM size and layout defaults consistent with driver expectations */
#define DEF_CHIP_ID_YUKON      0xb0  /* CHIP_ID_YUKON */
#define DEF_PHY_TYPE_MARV      0x04  /* SK_PHY_MARV_COPPER */

/* BAR info */
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

    /* MSI/MSI-X flags (unused: driver uses legacy INTx) */
    bool has_msi;
    bool has_msix;

    /* Hardware identification/state visible to driver via MMIO */
    uint8_t chip_id;
    uint8_t chip_rev;
    uint8_t ports;

    /* MMIO register storage (4KB window as per skge_get_regs_len) */
    uint8_t regs[0x4000];

    /* Interrupt state */
    uint32_t sp_isrc;   /* B0_SP_ISRC shadow */
    uint32_t imsk;      /* B0_IMSK shadow */
    uint32_t isrc;      /* B0_ISRC shadow */
    uint32_t hwe_isrc;  /* B0_HWE_ISRC shadow */
    uint32_t hwe_imsk;  /* B0_HWE_IMSK shadow */

    /* IRQ moderation (B2_IRQM_*) */
    uint32_t irqm_msk;
    uint32_t irqm_ini;
    uint32_t irqm_ctrl;
};

static inline PCIBaseState *pcibase_from_device(PCIDevice *pdev)
{
    return PCIBASE_DEVICE(pdev);
}

/* Interrupt helper */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* In this minimal model, we always assert INTx when there is any
     * unmasked bit in SP_ISRC. The driver will clear conditions by
     * manipulating masks; we do not model per-bit auto-clear.
     */
    if (s->sp_isrc & s->imsk) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

static uint8_t pcibase_mmio_read8(PCIBaseState *s, hwaddr addr)
{
    if (addr < sizeof(s->regs)) {
        return s->regs[addr];
    }
    return 0xff;
}

static uint16_t pcibase_mmio_read16(PCIBaseState *s, hwaddr addr)
{
    uint16_t v = 0xffff;
    if (addr + 1 < sizeof(s->regs)) {
        v = s->regs[addr] | ((uint16_t)s->regs[addr + 1] << 8);
    }
    return v;
}

static uint32_t pcibase_mmio_read32(PCIBaseState *s, hwaddr addr)
{
    uint32_t v = 0xffffffffu;
    if (addr + 3 < sizeof(s->regs)) {
        v = s->regs[addr]
          | ((uint32_t)s->regs[addr + 1] << 8)
          | ((uint32_t)s->regs[addr + 2] << 16)
          | ((uint32_t)s->regs[addr + 3] << 24);
    }
    return v;
}

static void pcibase_mmio_write8(PCIBaseState *s, hwaddr addr, uint8_t val)
{
    if (addr < sizeof(s->regs)) {
        s->regs[addr] = val;
    }
}

static void pcibase_mmio_write16(PCIBaseState *s, hwaddr addr, uint16_t val)
{
    if (addr + 1 < sizeof(s->regs)) {
        s->regs[addr] = val & 0xff;
        s->regs[addr + 1] = (val >> 8) & 0xff;
    }
}

static void pcibase_mmio_write32(PCIBaseState *s, hwaddr addr, uint32_t val)
{
    if (addr + 3 < sizeof(s->regs)) {
        s->regs[addr] = val & 0xff;
        s->regs[addr + 1] = (val >> 8) & 0xff;
        s->regs[addr + 2] = (val >> 16) & 0xff;
        s->regs[addr + 3] = (val >> 24) & 0xff;
    }
}

static void pcibase_init_identity(PCIBaseState *s)
{
    static const uint8_t mac1[6] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
    static const uint8_t mac2[6] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x57 };

    s->regs[B2_CHIP_ID] = s->chip_id;
    s->regs[B2_MAC_CFG] = (s->chip_rev << 4) |
                          (s->ports == 1 ? 0x01 : 0x00);
    /* EEPROM RAM-size value 0 means the Yukon default of 128 KiB. */
    s->regs[B2_E_0] = 0;
    s->regs[B2_E_1] = DEF_PHY_TYPE_MARV;
    s->regs[B2_PMD_TYP] = 'T';
    memcpy(&s->regs[B2_MAC_1], mac1, sizeof(mac1));
    memcpy(&s->regs[B2_MAC_2], mac2, sizeof(mac2));
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    /* Handle special registers with behavior, otherwise fall back to
     * simple storage in s->regs.
     */
    switch (size) {
    case 1:
        if (addr == B0_CTST) {
            return pcibase_mmio_read8(s, addr);
        }
        if (addr == B0_POWER_CTRL || addr == B2_TST_CTRL1 ||
            addr == B2_TST_CTRL2 || addr == B2_TI_CTRL) {
            return pcibase_mmio_read8(s, addr);
        }
        return pcibase_mmio_read8(s, addr);
    case 2:
        if (addr == B0_CTST) {
            return pcibase_mmio_read16(s, addr);
        }
        if (addr == B0_LED || addr == B3_RI_CTRL || addr == B3_MA_TO_CTRL || addr == B3_PA_CTRL) {
            return pcibase_mmio_read16(s, addr);
        }
        return pcibase_mmio_read16(s, addr);
    case 4:
        if (addr == B0_IMSK) {
            return s->imsk;
        }
        if (addr == B0_ISRC) {
            /* driver reads B0_ISRC only in debug; return 0 means no pending */
            return s->isrc;
        }
        if (addr == B0_HWE_ISRC) {
            return s->hwe_isrc;
        }
        if (addr == B0_SP_ISRC) {
            /* Read of SP_ISRC masks IRQ in driver logic. We simply
             * return current SP_ISRC; driver then ANDs with intr_mask.
             */
            return s->sp_isrc;
        }
        if (addr == B0_R1_CSR) {
            return pcibase_mmio_read32(s, B0_R1_CSR);
        }
        if (addr == B0_R2_CSR) {
            return pcibase_mmio_read32(s, B0_R2_CSR);
        }
        if (addr == B2_FAR || addr == B2_IRQM_MSK || addr == B2_IRQM_INI || addr == B2_IRQM_CTRL || addr == B2_GP_IO) {
            return pcibase_mmio_read32(s, addr);
        }
        if (addr == B3_RI_WTO_R1 || addr == B3_RI_WTO_XA1 || addr == B3_RI_RTO_R1 || addr == B3_RI_RTO_XA1 || addr == B3_RI_WTO_R2 || addr == B3_RI_WTO_XA2 || addr == B3_RI_RTO_R2 || addr == B3_RI_RTO_XA2) {
            return pcibase_mmio_read32(s, addr);
        }
        return pcibase_mmio_read32(s, addr);
    default:
        return 0;
    }
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (size) {
    case 1: {
        uint8_t v8 = val & 0xff;
        if (addr == B0_CTST) {
            /* SW reset sequence: driver writes CS_RST_SET then CS_RST_CLR
             * then CS_MRST_CLR. We simply store bits without elaborate
             * side effects, but we clear some internal state on CS_RST_SET.
             */
            if (v8 & CS_RST_SET) {
                /* Basic reset of internal register space except identity */
                memset(s->regs, 0, sizeof(s->regs));
                /* These fields are ROM/EEPROM-backed and survive SW reset. */
                pcibase_init_identity(s);
            }
            pcibase_mmio_write8(s, addr, v8);
            return;
        }
        if (addr == B0_POWER_CTRL || addr == B2_TST_CTRL1 ||
            addr == B2_TST_CTRL2 || addr == B2_TI_CTRL) {
            pcibase_mmio_write8(s, addr, v8);
            return;
        }
        if (addr == GMAC_IRQ_MSK(0) || addr == GMAC_IRQ_MSK(1) || addr == GMAC_CTRL(0) || addr == GMAC_CTRL(1) || addr == RX_GMF_CTRL_T(0) || addr == RX_GMF_CTRL_T(1) || addr == TX_GMF_CTRL_T(0) || addr == TX_GMF_CTRL_T(1) || addr == LNK_LED_REG(0) || addr == LNK_LED_REG(1) || addr == TXA_CTRL(0) || addr == TXA_CTRL(1)) {
            pcibase_mmio_write8(s, addr, v8);
            return;
        }
        pcibase_mmio_write8(s, addr, v8);
        return;
    }
    case 2: {
        uint16_t v16 = val & 0xffff;
        if (addr == B0_LED) {
            pcibase_mmio_write16(s, addr, v16);
            return;
        }
        if (addr == B3_RI_CTRL || addr == B3_MA_TO_CTRL || addr == B3_PA_CTRL) {
            pcibase_mmio_write16(s, addr, v16);
            return;
        }
        if (addr == TX_MFF_CTRL1(0) || addr == TX_MFF_CTRL1(1) || addr == RX_MFF_CTRL1(0) || addr == RX_MFF_CTRL1(1)) {
            pcibase_mmio_write16(s, addr, v16);
            return;
        }
        pcibase_mmio_write16(s, addr, v16);
        return;
    }
    case 4: {
        uint32_t v32 = val & 0xffffffffu;
        if (addr == B0_IMSK) {
            s->imsk = v32;
            pcibase_mmio_write32(s, addr, v32);
            pcibase_update_irq(s);
            return;
        }
        if (addr == B0_ISRC) {
            /* Not directly written by driver in provided snippet */
            pcibase_mmio_write32(s, addr, v32);
            return;
        }
        if (addr == B0_HWE_IMSK) {
            s->hwe_imsk = v32;
            pcibase_mmio_write32(s, addr, v32);
            return;
        }
        if (addr == B0_R1_CSR || addr == B0_R2_CSR) {
            pcibase_mmio_write32(s, addr, v32);
            return;
        }
        if (addr == B2_FAR || addr == B2_GP_IO) {
            pcibase_mmio_write32(s, addr, v32);
            return;
        }
        if (addr == B2_IRQM_MSK) {
            s->irqm_msk = v32;
            pcibase_mmio_write32(s, addr, v32);
            return;
        }
        if (addr == B2_IRQM_INI) {
            s->irqm_ini = v32;
            pcibase_mmio_write32(s, addr, v32);
            return;
        }
        if (addr == B2_IRQM_CTRL) {
            s->irqm_ctrl = v32;
            pcibase_mmio_write32(s, addr, v32);
            return;
        }
        if (addr == B3_RI_WTO_R1 || addr == B3_RI_WTO_XA1 || addr == B3_RI_RTO_R1 || addr == B3_RI_RTO_XA1 || addr == B3_RI_WTO_R2 || addr == B3_RI_WTO_XA2 || addr == B3_RI_RTO_R2 || addr == B3_RI_RTO_XA2) {
            pcibase_mmio_write32(s, addr, v32);
            return;
        }
        pcibase_mmio_write32(s, addr, v32);
        return;
    }
    default:
        return;
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
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
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
    PCIDevice *pdev = PCI_DEVICE(dev);

    pci_device_reset(pdev);

    /* Initialize identification fields for skge_reset() */
    s->chip_id = DEF_CHIP_ID_YUKON;
    s->chip_rev = 1;
    s->ports = 2;

    memset(s->regs, 0, sizeof(s->regs));

    pcibase_init_identity(s);

    /* Basic defaults */
    s->imsk = 0;
    s->isrc = 0;
    s->sp_isrc = 0;
    s->hwe_isrc = 0;
    s->hwe_imsk = IS_ERR_MSK;
    s->irqm_msk = 0;
    s->irqm_ini = 0;
    s->irqm_ctrl = 0;

    /* LED off */
    pcibase_mmio_write16(s, B0_LED, LED_STAT_OFF);
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    hwaddr aligned_size = bi->size;
    if (aligned_size & (aligned_size - 1)) {
        aligned_size = pow2ceil(bi->size);
    }

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

    /* PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  SKGE_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  SKGE_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, SKGE_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);

    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR0: 0x4000 bytes for registers (driver uses skge_get_regs_len=0x4000) */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    s->bar_info[0].size  = 0x4000;
    s->bar_info[0].name  = "skge-mmio";

    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type  = BAR_TYPE_NONE;
        s->bar_info[i].size  = 0;
        s->bar_info[i].name  = "unused";
    }

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = false;
    s->has_msix = false;

    /* Initialize reset state */
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

static const VMStateDescription vmstate_pcibase = {
    .name = "skge_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT8(chip_id, PCIBaseState),
        VMSTATE_UINT8(chip_rev, PCIBaseState),
        VMSTATE_UINT8(ports, PCIBaseState),
        VMSTATE_UINT32(sp_isrc, PCIBaseState),
        VMSTATE_UINT32(imsk, PCIBaseState),
        VMSTATE_UINT32(isrc, PCIBaseState),
        VMSTATE_UINT32(hwe_isrc, PCIBaseState),
        VMSTATE_UINT32(hwe_imsk, PCIBaseState),
        VMSTATE_UINT32(irqm_msk, PCIBaseState),
        VMSTATE_UINT32(irqm_ini, PCIBaseState),
        VMSTATE_UINT32(irqm_ctrl, PCIBaseState),
        VMSTATE_BUFFER(regs, PCIBaseState),
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
