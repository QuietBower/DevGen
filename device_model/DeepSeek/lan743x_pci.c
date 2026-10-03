/*
 * QEMU 8.2.10 virtual PCI device model for LAN743x Ethernet controller.
 * Generated from Linux driver lan743x_main.c. Only implements hardware
 * behavior visible in the driver's probe path.
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

#define TYPE_PCIBASE_DEVICE "lan743x_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Additional include files retrieved from driver context */
/* none */

/* Supplementary definition from driver */
#define PCI_VENDOR_ID_EFAR        0x1055

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_SMSC       PCI_VENDOR_ID_EFAR
#define PCI_DEVICE_ID_SMSC_LAN7430  0x7430
#define PCI_CLASS_ID             PCI_CLASS_NETWORK_ETHERNET

/* BAR0 size – must cover all MMIO registers used during probe (up to 0x4000 region) */
#define BAR0_SIZE                0x8000

/* Register offset definitions (from lan743x_main.c) */
#define ID_REV                  0x00
#define FPGA_REV                0x04
#define STRAP_READ              0x0C
#define PMT_CTL                 0x014
#define DP_SEL                  0x024
#define DP_CMD                  0x028
#define DP_ADDR                 0x02C
#define DP_DATA_0               0x030
#define HW_CFG                  0x010
#define E2P_CMD                 0x040
#define E2P_DATA                0x044
#define GPIO_CFG0               0x050
#define GPIO_CFG1               0x054
#define GPIO_CFG2               0x058
#define GPIO_CFG3               0x05C
#define MAC_CR                  0x100
#define MAC_RX                  0x104
#define MAC_TX                  0x108
#define MAC_FLOW                0x10C
#define MAC_RX_ADDRH            0x118
#define MAC_RX_ADDRL            0x11C
#define MAC_MII_ACC             0x120
#define MAC_MII_DATA            0x124
#define MAC_MII_READ            1
#define MAC_MII_WRITE           0
#define MAC_EEE_TX_LPI_REQ_DLY_CNT  0x130
#define MAC_WUCSR               0x140
#define MAC_WK_SRC              0x144
#define MAC_MP_SO_HI            0x148
#define MAC_MP_SO_LO            0x14C
#define MAC_WUF_CFG0            0x150
#define MAC_WUF_MASK0_0         0x200
#define MAC_WUF_MASK0_1         0x204
#define MAC_WUF_MASK0_2         0x208
#define MAC_WUF_MASK0_3         0x20C
#define MAC_WUCSR2              0x600
#define RFE_CTL                 0x508
#define RFE_RSS_CFG             0x554
#define RFE_HASH_KEY(x)         (0x558 + (x << 2))
#define RFE_INDX(x)             (0x580 + (x << 2))
#define RFE_ADDR_FILT_HI(x)     (0x400 + (8 * (x)))
#define RFE_ADDR_FILT_LO(x)     (0x404 + (8 * (x)))
#define FCT_TX_CTL              0xC4
#define FCT_RX_CTL              0xAC
#define FCT_FLOW(rx_channel)    (0xE0 + ((rx_channel) << 2))
#define SGMII_ACC               0x720
#define SGMII_DATA              0x724
#define SGMII_CTL               0x728
#define INT_STS                 0x780
#define INT_SET                 0x784
#define INT_EN_SET              0x788
#define INT_EN_CLR              0x78C
#define INT_STS_R2C             0x790
#define INT_VEC_EN_SET          0x794
#define INT_VEC_EN_CLR          0x798
#define INT_VEC_EN_AUTO_CLR     0x79C
#define INT_VEC_MAP0            0x7A0
#define INT_VEC_MAP1            0x7A4
#define INT_VEC_MAP2            0x7A8
#define INT_MOD_MAP0            0x7B0
#define INT_MOD_MAP1            0x7B4
#define INT_MOD_MAP2            0x7B8
#define INT_MOD_CFG0            0x7C0
#define INT_MOD_CFG1            0x7C4
#define INT_MOD_CFG2            0x7C8
#define INT_MOD_CFG3            0x7CC
#define INT_MOD_CFG4            0x7D0
#define INT_MOD_CFG5            0x7D4
#define INT_MOD_CFG6            0x7D8
#define INT_MOD_CFG7            0x7DC
#define INT_MOD_CFG8            0x7E0
#define INT_MOD_CFG9            0x7E4
#define DMAC_CFG                0xC00
#define DMAC_COAL_CFG           0xC04
#define DMAC_OBFF_CFG           0xC08
#define DMAC_CMD                0xC0C
#define DMAC_INT_STS            0xC10
#define DMAC_INT_EN_SET         0xC14
#define DMAC_INT_EN_CLR         0xC18
#define RX_BASE_ADDRL(channel)  (0xC4C + ((channel) << 6))
#define RX_BASE_ADDRH(channel)  (0xC48 + ((channel) << 6))
#define RX_CFG_A(channel)       (0xC40 + ((channel) << 6))
#define RX_CFG_B(channel)       (0xC44 + ((channel) << 6))
#define RX_CFG_C(channel)       (0xC64 + ((channel) << 6))
#define RX_HEAD(channel)        (0xC58 + ((channel) << 6))
#define RX_TAIL(channel)        (0xC5C + ((channel) << 6))
#define RX_HEAD_WRITEBACK_ADDRL(channel)    (0xC54 + ((channel) << 6))
#define RX_HEAD_WRITEBACK_ADDRH(channel)    (0xC50 + ((channel) << 6))
#define TX_BASE_ADDRL(channel)  (0xD4C + ((channel) << 6))
#define TX_BASE_ADDRH(channel)  (0xD48 + ((channel) << 6))
#define TX_CFG_A(channel)       (0xD40 + ((channel) << 6))
#define TX_CFG_B(channel)       (0xD44 + ((channel) << 6))
#define TX_CFG_C(channel)       (0xD64 + ((channel) << 6))
#define TX_HEAD(channel)        (0xD58 + ((channel) << 6))
#define TX_TAIL(channel)        (0xD5C + ((channel) << 6))
#define TX_HEAD_WRITEBACK_ADDRL(channel)    (0xD54 + ((channel) << 6))
#define TX_HEAD_WRITEBACK_ADDRH(channel)    (0xD50 + ((channel) << 6))
#define PTP_CMD_CTL             0x0A00
#define PTP_GENERAL_CONFIG      0x0A04
#define PTP_INT_STS             0x0A08
#define PTP_INT_EN_SET          0x0A0C
#define PTP_INT_EN_CLR          0x0A10
#define PTP_CLOCK_SEC           0x0A14
#define PTP_CLOCK_NS            0x0A18
#define PTP_CLOCK_SUBNS         0x0A1C
#define PTP_CLOCK_RATE_ADJ      0x0A20
#define PTP_CLOCK_STEP_ADJ      0x0A2C
#define PTP_CLOCK_TARGET_SEC_X(x) (0x0A30 + ((x) << 4))
#define PTP_CLOCK_TARGET_NS_X(x)  (0x0A34 + ((x) << 4))
#define PTP_CLOCK_TARGET_RELOAD_SEC_X(x) (0x0A38 + ((x) << 4))
#define PTP_CLOCK_TARGET_RELOAD_NS_X(x)  (0x0A3C + ((x) << 4))
#define PTP_IO_SEL              0x0A58
#define PTP_LATENCY             0x0A5C
#define PTP_CAP_INFO            0x0A60
#define PTP_RX_TS_CFG           0x0A68
#define PTP_TX_MOD              0x0AA4
#define PTP_TX_MOD2             0x0AA8
#define PTP_TX_EGRESS_SEC       0x0AAC
#define PTP_TX_EGRESS_NS        0x0AB0
#define PTP_TX_MSG_HEADER       0x0AB4
#define PTP_IO_CAP_CONFIG       0x0AC4
#define PTP_IO_EVENT_OUTPUT_CFG 0x0AD8
#define PTP_IO_PIN_CFG          0x0ADC
#define PTP_IO_RE_LTC_SEC_CAP_X 0x0AC8
#define PTP_IO_FE_LTC_SEC_CAP_X 0x0AD0
#define PTP_IO_RE_LTC_NS_CAP_X  0x0ACC
#define PTP_IO_FE_LTC_NS_CAP_X  0x0AD4
#define MISC_CTL_0              0x920
#define STAT_RX_JABBER_ERRORS           0x120C
#define STAT_TX_FCS_ERRORS              0x1280
#define STAT_TX_UNICAST_BYTE_COUNT      0x12A0
#define STAT_TX_BROADCAST_BYTE_COUNT    0x12A4
#define STAT_RX_MULTICAST_BYTE_COUNT    0x1224
#define STAT_RX_FCS_ERRORS              0x1200
#define STAT_TX_TOTAL_FRAMES            0x12D8
#define STAT_TX_LATE_COLLISIONS         0x129C
#define STAT_RX_MULTICAST_FRAMES        0x1230
#define STAT_RX_DROPPED_FRAMES          0x1218
#define STAT_TX_MULTIPLE_COLLISIONS     0x1294
#define STAT_TX_EXCESSIVE_COLLISION     0x1298
#define STAT_TX_SINGLE_COLLISIONS       0x1290
#define STAT_TX_MULTICAST_FRAMES        0x12B4
#define STAT_RX_ALIGNMENT_ERRORS        0x1204
#define STAT_RX_TOTAL_FRAMES            0x1254
#define STAT_TX_CARRIER_ERRORS          0x1288
#define STAT_TX_MULTICAST_BYTE_COUNT    0x12A8
#define STAT_RX_OVERSIZE_FRAME_ERRORS   0x1214
#define STAT_RX_UNDERSIZE_FRAME_ERRORS  0x1210
#define STAT_RX_BROADCAST_BYTE_COUNT    0x1220
#define STAT_TX_EXCESS_DEFERRAL_ERRORS  0x1284
#define STAT_RX_UNICAST_BYTE_COUNT      0x121C
#define STAT_RX_65_127_BYTE_FRAMES      0x123C
#define STAT_RX_FRAGMENT_ERRORS         0x1208
#define STAT_RX_1024_1518_BYTE_FRAMES   0x124C
#define STAT_RX_UNICAST_FRAMES          0x1228
#define STAT_RX_64_BYTE_FRAMES          0x1238
#define STAT_RX_256_511_BYTES_FRAMES    0x1244
#define STAT_RX_PAUSE_FRAMES            0x1234
#define STAT_RX_512_1023_BYTE_FRAMES    0x1248
#define STAT_RX_GREATER_1518_BYTE_FRAMES 0x1250
#define STAT_RX_BROADCAST_FRAMES        0x122C
#define STAT_RX_128_255_BYTE_FRAMES     0x1240
#define STAT_TX_512_1023_BYTE_FRAMES    0x12CC
#define STAT_TX_UNICAST_FRAMES          0x12AC
#define STAT_TX_BAD_BYTE_COUNT          0x128C
#define STAT_TX_65_127_BYTE_FRAMES      0x12C0
#define STAT_TX_256_511_BYTES_FRAMES    0x12C8
#define STAT_TX_1024_1518_BYTE_FRAMES   0x12D0
#define STAT_TX_PAUSE_FRAMES            0x12B8
#define STAT_EEE_TX_LPI_TIME            0x12E0
#define STAT_EEE_RX_LPI_TIME            0x125C
#define STAT_EEE_TX_LPI_TRANSITIONS     0x12DC
#define STAT_TX_BROADCAST_FRAMES        0x12B0
#define STAT_EEE_RX_LPI_TRANSITIONS     0x1258
#define STAT_RX_COUNTER_ROLLOVER_STATUS 0x127C
#define STAT_TX_128_255_BYTE_FRAMES     0x12C4
#define STAT_TX_COUNTER_ROLLOVER_STATUS 0x12FC
#define STAT_TX_GREATER_1518_BYTE_FRAMES 0x12D4
#define STAT_TX_64_BYTE_FRAMES          0x12BC
#define ETH_SYS_REG_ADDR_BASE           0x4000
#define CONFIG_REG_ADDR_BASE            0x0000
#define GEN_SYS_CONFIG_LOAD_STARTED_REG 0x0078
#define ETH_SYS_CONFIG_LOAD_STARTED_REG (ETH_SYS_REG_ADDR_BASE + CONFIG_REG_ADDR_BASE + GEN_SYS_CONFIG_LOAD_STARTED_REG)
#define SYS_LOCK_REG                    0x00A0
#define ETH_SYSTEM_SYS_LOCK_REG         (ETH_SYS_REG_ADDR_BASE + CONFIG_REG_ADDR_BASE + SYS_LOCK_REG)
#define HS_OTP_BLOCK_BASE               (ETH_SYS_REG_ADDR_BASE + 0x1000)
#define OTP_PWR_DN                      0x1000
#define OTP_ADDR_HIGH                   0x1004
#define OTP_ADDR_LOW                    0x1008
#define OTP_PRGM_DATA                   0x1010
#define OTP_PRGM_MODE                   0x1014
#define OTP_READ_DATA                   0x1018
#define OTP_FUNC_CMD                    0x1020
#define OTP_TST_CMD                     0x1024
#define OTP_CMD_GO                      0x1028
#define OTP_STATUS                      0x1030
#define HS_EEPROM_REG_ADDR_BASE         (ETH_SYS_REG_ADDR_BASE + 0x0E00)
#define HS_E2P_CMD                      (HS_EEPROM_REG_ADDR_BASE + 0x0000)
#define HS_E2P_DATA                     (HS_EEPROM_REG_ADDR_BASE + 0x0004)

/* Bitfield definitions */
#define ID_REV_CHIP_REV_MASK_           0x0000FFFF
#define ID_REV_CHIP_REV_A0_             0x00000000
#define ID_REV_CHIP_REV_B0_             0x00000010
#define ID_REV_ID_MASK_                 0xFFFF0000
#define ID_REV_ID_LAN7430_              0x74300000
#define ID_REV_ID_LAN7431_              0x74310000
#define ID_REV_ID_A011_                 0xA0110000
#define ID_REV_ID_A041_                 0xA0410000
#define PMT_CTL_READY_                  BIT(7)
#define PMT_CTL_ETH_PHY_RST_            BIT(4)
#define HW_CFG_LRST_                    BIT(1)
#define INT_BIT_MAS_                    BIT(0)
#define INT_BIT_SW_GP_                  BIT(9)
#define INT_BIT_1588_                   BIT(7)
#define INT_BIT_DMA_TX_(channel)        BIT(16 + (channel))
#define INT_BIT_DMA_RX_(channel)        BIT(24 + (channel))
#define INT_BIT_ALL_OTHER_              (INT_BIT_SW_GP_ | INT_BIT_1588_)
#define INT_BIT_ALL_RX_                 0x0F000000
#define INT_BIT_ALL_TX_                 0x000F0000
#define LAN743X_USED_RX_CHANNELS        4
#define LAN743X_USED_TX_CHANNELS        1
#define LAN743X_MAX_TX_CHANNELS         1
#define LAN743X_MAX_VECTOR_COUNT        8
#define PCI11X1X_MAX_VECTOR_COUNT       16
#define SGMII_CTL_SGMII_ENABLE_         BIT(31)
#define SGMII_CTL_SGMII_POWER_DN_       BIT(1)
#define MAC_CR_EEE_EN_                  BIT(17)
#define MAC_CR_DPX_                     BIT(3)
#define MAC_CR_CFG_H_                   BIT(2)
#define MAC_CR_CFG_L_                   BIT(1)
#define MAC_RX_RXEN_                    BIT(0)
#define MAC_RX_RXD_                     BIT(1)
#define MAC_TX_TXEN_                    BIT(0)
#define MAC_TX_TXD_                     BIT(1)

/* Additional constants */
#define LAN743X_RX_RING_SIZE            128
#define LAN743X_TX_RING_SIZE            128
#define RX_DESC_DATA0_OWN_              0x00008000
#define RX_DESC_DATA0_FS_               0x80000000
#define RX_DESC_DATA0_LS_               0x40000000
#define TX_DESC_DATA0_ICE_              0x00400000
#define TX_DESC_DATA0_FCS_              0x00020000
#define TX_DESC_DATA0_FS_               0x20000000
#define TX_DESC_DATA0_LS_               0x10000000
#define TX_DESC_DATA0_IOC_              0x04000000

/* Structure definitions for PCI device state */
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

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;
    uint32_t intr_status;    /* shadow of INT_STS */
    uint32_t intr_enable_set; /* shadow of INT_EN_SET */
    uint32_t intr_vec_en_set; /* shadow of INT_VEC_EN_SET */
    uint32_t intr_vec_map[3]; /* INT_VEC_MAP0..2 */
    uint32_t intr_mod_map[3]; /* INT_MOD_MAP0..2 */
    uint32_t intr_mod_cfg[10]; /* INT_MOD_CFG0..9 */

    /* Hardware Register Shadows (BAR0 MMIO space) */
    uint32_t regs[BAR0_SIZE / 4];

    /* DMA Context */
    struct {
        dma_addr_t tx_ring_dma[LAN743X_MAX_TX_CHANNELS];
        dma_addr_t rx_ring_dma[LAN743X_USED_RX_CHANNELS];
        uint32_t dmac_cfg;
        uint32_t dmac_cmd;
    } dma;

    /* Operational status flags */
    uint32_t csr_flags;

    /* Reset state */
    uint32_t reset_state;

    /* Power management state */
    uint32_t pmt_ctl;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    /* Minimal IRQ update: raise if any enabled status bit is set */
    if (s->intr_status & s->intr_enable_set) {
        if (msix_enabled(pdev)) {
            /* For now, just trigger vector 0 */
            msix_notify(pdev, 0);
        } else if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* DMA transfer logic not needed for probe; driver only configures DMA later during open. Deleting placeholder block entirely. */

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = 0;
    uint32_t offset = addr;

    if (offset >= BAR0_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR, "lan743x: read out of bounds at 0x%" HWADDR_PRIx "\n", addr);
        return 0;
    }

    switch (offset) {
    case ID_REV:
        val = s->regs[ID_REV / 4]; /* read-only */
        break;
    case FPGA_REV:
        val = s->regs[FPGA_REV / 4]; /* read-only */
        break;
    case STRAP_READ:
        val = s->regs[STRAP_READ / 4];
        break;
    case PMT_CTL:
        /* PMT_CTL: ensure READY is set, RST is cleared */
        val = s->pmt_ctl;  /* shadow field */
        val |= PMT_CTL_READY_;
        val &= ~PMT_CTL_ETH_PHY_RST_;
        break;
    case HW_CFG:
        /* HW_CFG: LRST is self-clearing, always read 0 */
        val = s->regs[HW_CFG / 4] & ~HW_CFG_LRST_;
        break;
    case DMAC_CMD:
        /* DMAC_CMD: some bits self-clearing */
        val = s->dma.dmac_cmd;
        break;
    case INT_STS:
        val = s->intr_status;
        break;
    case INT_STS_R2C:
        val = s->intr_status; /* R2C reads same status */
        break;
    /* For all other registers, just return from regs array */
    default:
        val = s->regs[offset / 4];
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t offset = addr;

    if (offset >= BAR0_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR, "lan743x: write out of bounds at 0x%" HWADDR_PRIx " val 0x%" PRIx64 "\n", addr, val);
        return;
    }

    switch (offset) {
    case PMT_CTL:
        /* When writing ETH_PHY_RST, clear it immediately and ensure READY */
        s->pmt_ctl = val;
        break;
    case HW_CFG:
        /* When writing LRST, the bit is self-clearing; ignore it */
        s->regs[HW_CFG / 4] = val & ~HW_CFG_LRST_;
        break;
    case DMAC_CMD:
        /* Handle SWR and start/stop bits; clear SWR immediately */
        if (val & BIT(0)) { /* DMAC_CMD_SWR_ assumed BIT(0) – from driver wait */
            s->dma.dmac_cmd &= ~BIT(0);
        } else {
            s->dma.dmac_cmd = val;
        }
        break;
    case INT_SET:
        /* Writing bits to INT_SET sets corresponding interrupt status */
        s->intr_status |= val;
        pcibase_update_irq(s);
        break;
    case INT_EN_SET:
        s->intr_enable_set |= val;
        pcibase_update_irq(s);
        break;
    case INT_EN_CLR:
        s->intr_enable_set &= ~val;
        pcibase_update_irq(s);
        break;
    case INT_VEC_EN_SET:
        s->intr_vec_en_set |= val;
        pcibase_update_irq(s);
        break;
    case INT_VEC_EN_CLR:
        s->intr_vec_en_set &= ~val;
        pcibase_update_irq(s);
        break;
    case INT_STS:
        /* W1C: clear bits written as 1 */
        s->intr_status &= ~val;
        pcibase_update_irq(s);
        break;
    case INT_STS_R2C:
        /* R2C: clear bits written as 1 */
        s->intr_status &= ~val;
        pcibase_update_irq(s);
        break;
    case DMAC_INT_STS:
        /* W1C for DMA interrupts (no actual DMA status yet, but driver may write to clear) */
        /* We'll just ignore as we don't track DMA interrupts */
        break;
    default:
        s->regs[offset / 4] = val;
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    (void)opaque;
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    (void)opaque;
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
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

    /* Reset all registers to power-on defaults */
    memset(s->regs, 0, BAR0_SIZE);
    s->intr_status = 0;
    s->intr_enable_set = 0;
    s->intr_vec_en_set = 0;
    memset(s->intr_vec_map, 0, sizeof(s->intr_vec_map));
    memset(s->intr_mod_map, 0, sizeof(s->intr_mod_map));
    memset(s->intr_mod_cfg, 0, sizeof(s->intr_mod_cfg));
    s->csr_flags = 0;
    s->pmt_ctl = 0;

    /* Set chip identification to LAN7430 B0 (valid probe path) */
    s->regs[ID_REV / 4] = ID_REV_ID_LAN7430_ | ID_REV_CHIP_REV_B0_;
    s->regs[FPGA_REV / 4] = 0;        /* no FPGA image */
    s->regs[STRAP_READ / 4] = 0;      /* strap data – default */
    s->pmt_ctl = PMT_CTL_READY_;      /* power management: ready */
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_SMSC );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_SMSC_LAN7430 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_ID );
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
    s->bar_info[0] = (BARInfo){
        .index = 0,
        .type = BAR_TYPE_MMIO,
        .size = BAR0_SIZE,
        .name = "lan743x-mmio",
    };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI/MSI-X Init */
    uint8_t msix_cap_offset = pci_add_capability(pdev, PCI_CAP_ID_MSIX, 0, 0, errp);
    if (msix_init(pdev, LAN743X_MAX_VECTOR_COUNT,
                  &s->bar_regions[0], 0, 0,
                  &s->bar_regions[0], 0, 0x1000,
                  msix_cap_offset, errp)) {
        error_propagate(errp, *errp);
        return;
    }
    s->has_msix = true;

    /* DMA Config – not needed during probe */
    /* Timer Config – not needed during probe */
    /* Field Init – nothing additional required */
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
    /* No other cleanup required */
}

static const VMStateDescription vmstate_pcibase = {
    .name = "lan743x_pci",
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
