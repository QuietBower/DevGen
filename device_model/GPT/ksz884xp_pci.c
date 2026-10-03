/*
 * KSZ884x PCI device QEMU model - Functional Implementation
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

#define TYPE_PCIBASE_DEVICE "ksz884xp_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI identifiers from pcidev_table (first entry only) */
#define PCIBASE_VENDOR_ID        0x16C6
#define PCIBASE_DEVICE_ID        0x8841
#define PCIBASE_CLASS_ID         0x0200

/* DMA control and start registers */
#define KS_DMA_TX_CTRL                  0x0000
#define KS_DMA_RX_CTRL                  0x0004
#define KS_DMA_TX_START                 0x0008
#define KS_DMA_RX_START                 0x000C
#define KS_DMA_TX_ADDR                  0x0010
#define KS_DMA_RX_ADDR                  0x0014

#define DMA_TX_ENABLE                   0x00000001
#define DMA_TX_CRC_ENABLE               0x00000002
#define DMA_TX_PAD_ENABLE               0x00000004
#define DMA_TX_LOOPBACK                 0x00000100
#define DMA_TX_FLOW_ENABLE              0x00000200
#define DMA_TX_CSUM_IP                  0x00010000
#define DMA_TX_CSUM_TCP                 0x00020000
#define DMA_TX_CSUM_UDP                 0x00040000
#define DMA_TX_BURST_SIZE               0x3F000000

#define DMA_RX_ENABLE                   0x00000001
#define KS884X_DMA_RX_MULTICAST         0x00000002
#define DMA_RX_PROMISCUOUS              0x00000004
#define DMA_RX_ERROR                    0x00000008
#define DMA_RX_UNICAST                  0x00000010
#define DMA_RX_ALL_MULTICAST            0x00000020
#define DMA_RX_BROADCAST                0x00000040
#define DMA_RX_FLOW_ENABLE              0x00000200
#define DMA_RX_CSUM_IP                  0x00010000
#define DMA_RX_CSUM_TCP                 0x00020000
#define DMA_RX_CSUM_UDP                 0x00040000
#define DMA_RX_BURST_SIZE               0x3F000000

#define DMA_BURST_SHIFT                 24
#define DMA_BURST_DEFAULT               8

#define DMA_START                       0x00000001
#define DMA_ADDR_LIST_MASK              0xFFFFFFFC
#define DMA_ADDR_LIST_SHIFT             2

/* Interrupt registers and masks */
#define KS884X_MULTICAST_0_OFFSET       0x0020
#define KS884X_MULTICAST_1_OFFSET       0x0021
#define KS884X_MULTICAST_2_OFFSET       0x0022
#define KS884x_MULTICAST_3_OFFSET       0x0023
#define KS884X_MULTICAST_4_OFFSET       0x0024
#define KS884X_MULTICAST_5_OFFSET       0x0025
#define KS884X_MULTICAST_6_OFFSET       0x0026
#define KS884X_MULTICAST_7_OFFSET       0x0027

#define KS884X_INTERRUPTS_ENABLE        0x0028
#define KS884X_INTERRUPTS_STATUS        0x002C

#define KS884X_INT_RX_STOPPED           0x02000000
#define KS884X_INT_TX_STOPPED           0x04000000
#define KS884X_INT_RX_OVERRUN           0x08000000
#define KS884X_INT_TX_EMPTY             0x10000000
#define KS884X_INT_RX                   0x20000000
#define KS884X_INT_TX                   0x40000000
#define KS884X_INT_PHY                  0x80000000

#define KS884X_INT_RX_MASK              (KS884X_INT_RX | KS884X_INT_RX_OVERRUN)
#define KS884X_INT_TX_MASK              (KS884X_INT_TX | KS884X_INT_TX_EMPTY)
#define KS884X_INT_MASK                 (KS884X_INT_RX | KS884X_INT_TX | KS884X_INT_PHY)

/* MAC address control registers */
#define KS_ADD_ADDR_0_LO                0x0080
#define KS_ADD_ADDR_0_HI                0x0084
#define KS_ADD_ADDR_1_LO                0x0088
#define KS_ADD_ADDR_1_HI                0x008C
#define KS_ADD_ADDR_2_LO                0x0090
#define KS_ADD_ADDR_2_HI                0x0094
#define KS_ADD_ADDR_3_LO                0x0098
#define KS_ADD_ADDR_3_HI                0x009C
#define KS_ADD_ADDR_4_LO                0x00A0
#define KS_ADD_ADDR_4_HI                0x00A4
#define KS_ADD_ADDR_5_LO                0x00A8
#define KS_ADD_ADDR_5_HI                0x00AC
#define KS_ADD_ADDR_6_LO                0x00B0
#define KS_ADD_ADDR_6_HI                0x00B4
#define KS_ADD_ADDR_7_LO                0x00B8
#define KS_ADD_ADDR_7_HI                0x00BC
#define KS_ADD_ADDR_8_LO                0x00C0
#define KS_ADD_ADDR_8_HI                0x00C4
#define KS_ADD_ADDR_9_LO                0x00C8
#define KS_ADD_ADDR_9_HI                0x00CC
#define KS_ADD_ADDR_A_LO                0x00D0
#define KS_ADD_ADDR_A_HI                0x00D4
#define KS_ADD_ADDR_B_LO                0x00D8
#define KS_ADD_ADDR_B_HI                0x00DC
#define KS_ADD_ADDR_C_LO                0x00E0
#define KS_ADD_ADDR_C_HI                0x00E4
#define KS_ADD_ADDR_D_LO                0x00E8
#define KS_ADD_ADDR_D_HI                0x00EC
#define KS_ADD_ADDR_E_LO                0x00F0
#define KS_ADD_ADDR_E_HI                0x00F4
#define KS_ADD_ADDR_F_LO                0x00F8
#define KS_ADD_ADDR_F_HI                0x00FC

#define ADD_ADDR_HI_MASK                0x0000FFFF
#define ADD_ADDR_ENABLE                 0x80000000
#define ADD_ADDR_INCR                   8

/* Switch and PHY related registers (subset) */
#define KS884X_ADDR_0_OFFSET            0x0200
#define KS884X_ADDR_1_OFFSET            0x0201
#define KS884X_ADDR_2_OFFSET            0x0202
#define KS884X_ADDR_3_OFFSET            0x0203
#define KS884X_ADDR_4_OFFSET            0x0204
#define KS884X_ADDR_5_OFFSET            0x0205

#define KS884X_BUS_CTRL_OFFSET          0x0210
#define BUS_SPEED_125_MHZ               0x0000
#define BUS_SPEED_62_5_MHZ              0x0001
#define BUS_SPEED_41_66_MHZ             0x0002
#define BUS_SPEED_25_MHZ                0x0003

#define KS884X_EEPROM_CTRL_OFFSET       0x0212
#define EEPROM_CHIP_SELECT              0x0001
#define EEPROM_SERIAL_CLOCK             0x0002
#define EEPROM_DATA_OUT                 0x0004
#define EEPROM_DATA_IN                  0x0008
#define EEPROM_ACCESS_ENABLE            0x0010

#define KS884X_MEM_INFO_OFFSET          0x0214
#define RX_MEM_TEST_FAILED              0x0008
#define RX_MEM_TEST_FINISHED            0x0010
#define TX_MEM_TEST_FAILED              0x0800
#define TX_MEM_TEST_FINISHED            0x1000

#define KS884X_GLOBAL_CTRL_OFFSET       0x0216
#define GLOBAL_SOFTWARE_RESET           0x0001

#define KS8841_POWER_MANAGE_OFFSET      0x0218
#define KS8841_WOL_CTRL_OFFSET          0x021A
#define KS8841_WOL_MAGIC_ENABLE         0x0080
#define KS8841_WOL_FRAME3_ENABLE        0x0008
#define KS8841_WOL_FRAME2_ENABLE        0x0004
#define KS8841_WOL_FRAME1_ENABLE        0x0002
#define KS8841_WOL_FRAME0_ENABLE        0x0001

#define KS8841_WOL_FRAME_CRC_OFFSET     0x0220
#define KS8841_WOL_FRAME_BYTE0_OFFSET   0x0224
#define KS8841_WOL_FRAME_BYTE2_OFFSET   0x0228

#define KS884X_IACR_P                   0x04A0
#define KS884X_IACR_OFFSET              KS884X_IACR_P
#define KS884X_IADR1_P                  0x04A2
#define KS884X_IADR2_P                  0x04A4
#define KS884X_IADR3_P                  0x04A6
#define KS884X_IADR4_P                  0x04A8
#define KS884X_IADR5_P                  0x04AA

#define KS884X_ACC_CTRL_SEL_OFFSET      KS884X_IACR_P
#define KS884X_ACC_CTRL_INDEX_OFFSET    (KS884X_ACC_CTRL_SEL_OFFSET + 1)
#define KS884X_ACC_DATA_0_OFFSET        KS884X_IADR4_P
#define KS884X_ACC_DATA_1_OFFSET        (KS884X_ACC_DATA_0_OFFSET + 1)
#define KS884X_ACC_DATA_2_OFFSET        KS884X_IADR5_P
#define KS884X_ACC_DATA_3_OFFSET        (KS884X_ACC_DATA_2_OFFSET + 1)
#define KS884X_ACC_DATA_4_OFFSET        KS884X_IADR2_P
#define KS884X_ACC_DATA_5_OFFSET        (KS884X_ACC_DATA_4_OFFSET + 1)
#define KS884X_ACC_DATA_6_OFFSET        KS884X_IADR3_P
#define KS884X_ACC_DATA_7_OFFSET        (KS884X_ACC_DATA_6_OFFSET + 1)
#define KS884X_ACC_DATA_8_OFFSET        KS884X_IADR1_P

#define KS884X_P1MBCR_P                 0x04D0
#define KS884X_P1MBSR_P                 0x04D2
#define KS884X_PHY1ILR_P                0x04D4
#define KS884X_PHY1IHR_P                0x04D6
#define KS884X_P1ANAR_P                 0x04D8
#define KS884X_P1ANLPR_P                0x04DA

#define KS884X_P2MBCR_P                 0x04E0
#define KS884X_P2MBSR_P                 0x04E2
#define KS884X_PHY2ILR_P                0x04E4
#define KS884X_PHY2IHR_P                0x04E6
#define KS884X_P2ANAR_P                 0x04E8
#define KS884X_P2ANLPR_P                0x04EA

#define KS884X_PHY_1_CTRL_OFFSET        KS884X_P1MBCR_P
#define PHY_CTRL_INTERVAL               (KS884X_P2MBCR_P - KS884X_P1MBCR_P)
#define KS884X_PHY_CTRL_OFFSET          0x00
#define KS884X_PHY_STATUS_OFFSET        0x02
#define KS884X_PHY_ID_1_OFFSET          0x04
#define KS884X_PHY_ID_2_OFFSET          0x06
#define KS884X_PHY_AUTO_NEG_OFFSET      0x08
#define KS884X_PHY_REMOTE_CAP_OFFSET    0x0A

#define KS884X_P1VCT_P                  0x04F0
#define KS884X_P1PHYCTRL_P              0x04F2
#define KS884X_P2VCT_P                  0x04F4
#define KS884X_P2PHYCTRL_P              0x04F6

#define KS884X_PHY_SPECIAL_OFFSET       KS884X_P1VCT_P
#define PHY_SPECIAL_INTERVAL            (KS884X_P2VCT_P - KS884X_P1VCT_P)
#define KS884X_PHY_LINK_MD_OFFSET       0x00
#define KS884X_PHY_PHY_CTRL_OFFSET      0x02

#define KS884X_SIDER_P                  0x0400
#define KS884X_CHIP_ID_OFFSET           KS884X_SIDER_P
#define KS884X_FAMILY_ID_OFFSET         (KS884X_CHIP_ID_OFFSET + 1)

#define REG_FAMILY_ID                   0x88
#define REG_CHIP_ID_41                  0x8810
#define REG_CHIP_ID_42                  0x8800
#define KS884X_CHIP_ID_MASK_41          0xFF10
#define KS884X_CHIP_ID_MASK             0xFFF0
#define KS884X_CHIP_ID_SHIFT            4
#define KS884X_REVISION_MASK            0x000E
#define KS884X_REVISION_SHIFT           1

#define KS8842_START                    0x0001

#define KS8842_SGCR1_P                  0x0402
#define KS8842_SWITCH_CTRL_1_OFFSET     KS8842_SGCR1_P

#define KS8842_SGCR2_P                  0x0404
#define KS8842_SWITCH_CTRL_2_OFFSET     KS8842_SGCR2_P

#define KS8842_SGCR3_P                  0x0406
#define KS8842_SWITCH_CTRL_3_OFFSET     KS8842_SGCR3_P

#define KS8842_SGCR4_P                  0x0408
#define KS8842_SGCR5_P                  0x040A
#define KS8842_SWITCH_CTRL_5_OFFSET     KS8842_SGCR5_P

#define KS8842_SGCR6_P                  0x0410
#define KS8842_SWITCH_CTRL_6_OFFSET     KS8842_SGCR6_P

#define KS8842_SGCR7_P                  0x0412
#define KS8842_SWITCH_CTRL_7_OFFSET     KS8842_SGCR7_P

#define KS8842_MACAR1_P                 0x0470
#define KS8842_MACAR2_P                 0x0472
#define KS8842_MACAR3_P                 0x0474
#define KS8842_MAC_ADDR_1_OFFSET        KS8842_MACAR1_P
#define KS8842_MAC_ADDR_0_OFFSET        (KS8842_MAC_ADDR_1_OFFSET + 1)
#define KS8842_MAC_ADDR_3_OFFSET        KS8842_MACAR2_P
#define KS8842_MAC_ADDR_2_OFFSET        (KS8842_MAC_ADDR_3_OFFSET + 1)
#define KS8842_MAC_ADDR_5_OFFSET        KS8842_MACAR3_P
#define KS8842_MAC_ADDR_4_OFFSET        (KS8842_MAC_ADDR_5_OFFSET + 1)

#define KS8842_TOSR1_P                  0x0480
#define KS8842_TOSR2_P                  0x0482
#define KS8842_TOSR3_P                  0x0484
#define KS8842_TOSR4_P                  0x0486
#define KS8842_TOSR5_P                  0x0488
#define KS8842_TOSR6_P                  0x048A
#define KS8842_TOSR7_P                  0x0490
#define KS8842_TOSR8_P                  0x0492

#define KS8842_P1CR1_P                  0x0500
#define KS8842_P1CR2_P                  0x0502
#define KS8842_P1VIDR_P                 0x0504
#define KS8842_P1CR3_P                  0x0506
#define KS8842_P1IRCR_P                 0x0508
#define KS8842_P1ERCR_P                 0x050A
#define KS884X_P1SCSLMD_P               0x0510
#define KS884X_P1CR4_P                  0x0512
#define KS884X_P1SR_P                   0x0514

#define KS8842_P2CR1_P                  0x0520
#define KS8842_P2CR2_P                  0x0522
#define KS8842_P2VIDR_P                 0x0524
#define KS8842_P2CR3_P                  0x0526
#define KS8842_P2IRCR_P                 0x0528
#define KS8842_P2ERCR_P                 0x052A
#define KS884X_P2SCSLMD_P               0x0530
#define KS884X_P2CR4_P                  0x0532
#define KS884X_P2SR_P                   0x0534

#define KS8842_P3CR1_P                  0x0540
#define KS8842_P3CR2_P                  0x0542
#define KS8842_P3VIDR_P                 0x0544
#define KS8842_P3CR3_P                  0x0546
#define KS8842_P3IRCR_P                 0x0548
#define KS8842_P3ERCR_P                 0x054A

#define KS8842_PORT_1_CTRL_1            KS8842_P1CR1_P
#define KS8842_PORT_2_CTRL_1            KS8842_P2CR1_P
#define KS8842_PORT_3_CTRL_1            KS8842_P3CR1_P

#define KS8842_PORT_CTRL_1_OFFSET       0x00
#define KS8842_PORT_CTRL_2_OFFSET       0x02
#define KS8842_PORT_CTRL_VID_OFFSET     0x04
#define KS8842_PORT_CTRL_3_OFFSET       0x06
#define KS8842_PORT_IN_RATE_OFFSET      0x08
#define KS8842_PORT_OUT_RATE_OFFSET     0x0A

#define KS884X_PORT_LINK_MD             0x10
#define KS884X_PORT_CTRL_4_OFFSET       0x12
#define KS884X_PORT_STATUS_OFFSET       0x14

#define KS884X_DMA_MASK                 (~0x0UL)

#define DESC_ALIGNMENT                  16
#define BUFFER_ALIGNMENT                8
#define NUM_OF_RX_DESC                  64
#define NUM_OF_TX_DESC                  64

#define KS_DESC_RX_FRAME_LEN            0x000007FF
#define KS_DESC_RX_FRAME_TYPE           0x00008000
#define KS_DESC_RX_ERROR_CRC            0x00010000
#define KS_DESC_RX_ERROR_RUNT           0x00020000
#define KS_DESC_RX_ERROR_TOO_LONG       0x00040000
#define KS_DESC_RX_ERROR_PHY            0x00080000
#define KS884X_DESC_RX_PORT_MASK        0x00300000
#define KS_DESC_RX_MULTICAST            0x01000000
#define KS_DESC_RX_ERROR                0x02000000
#define KS_DESC_RX_ERROR_CSUM_UDP       0x04000000
#define KS_DESC_RX_ERROR_CSUM_TCP       0x08000000
#define KS_DESC_RX_ERROR_CSUM_IP        0x10000000
#define KS_DESC_RX_LAST                 0x20000000
#define KS_DESC_RX_FIRST                0x40000000
#define KS_DESC_RX_ERROR_COND           (KS_DESC_RX_ERROR_CRC | \
                                         KS_DESC_RX_ERROR_RUNT | \
                                         KS_DESC_RX_ERROR_PHY | \
                                         KS_DESC_RX_ERROR_TOO_LONG)
#define KS_DESC_HW_OWNED                0x80000000

#define KS_DESC_BUF_SIZE                0x000007FF

#define KS884X_DESC_TX_PORT_MASK        0x00300000
#define KS_DESC_END_OF_RING             0x02000000
#define KS_DESC_TX_CSUM_GEN_UDP         0x04000000
#define KS_DESC_TX_CSUM_GEN_TCP         0x08000000
#define KS_DESC_TX_CSUM_GEN_IP          0x10000000
#define KS_DESC_TX_LAST                 0x20000000
#define KS_DESC_TX_FIRST                0x40000000
#define KS_DESC_TX_INTERRUPT            0x80000000

#define KS_DESC_PORT_SHIFT              20

#define KS_DESC_RX_MASK                 (KS_DESC_BUF_SIZE)
#define KS_DESC_TX_MASK                 (KS_DESC_TX_INTERRUPT | \
                                         KS_DESC_TX_FIRST | \
                                         KS_DESC_TX_LAST | \
                                         KS_DESC_TX_CSUM_GEN_IP | \
                                         KS_DESC_TX_CSUM_GEN_TCP | \
                                         KS_DESC_TX_CSUM_GEN_UDP | \
                                         KS_DESC_BUF_SIZE)

static const uint8_t DEFAULT_MAC_ADDRESS[6] = { 0x00, 0x10, 0xA1, 0x88, 0x42, 0x01 };

#define SWITCH_PORT_NUM                 2
#define HOST_PORT                       SWITCH_PORT_NUM
#define TOTAL_PORT_NUM                  (SWITCH_PORT_NUM + 1)
#define PORT_COUNTER_NUM                0x20
#define TOTAL_PORT_COUNTER_NUM          (PORT_COUNTER_NUM + 2)

#define STATS_LEN                       (TOTAL_PORT_COUNTER_NUM)

/* Internal enums and helpers */
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
    uint32_t intr_status;
    uint32_t intr_enable;

    struct {
        uint16_t chip_id;
        uint8_t  family_id;
        uint32_t tx_cfg;
        uint32_t rx_cfg;
        uint32_t tx_start;
        uint32_t rx_start;
        uint32_t tx_addr;
        uint32_t rx_addr;
    } regs;

    struct {
        uint32_t tx_desc_base;
        uint32_t rx_desc_base;
    } dma_info;

    uint32_t status_flags;

    struct {
        bool in_reset;
    } reset_state;

    struct {
        uint8_t pm_state;
    } pm_state;

    /* Simple shadow memory for MMIO space used by driver */
    uint8_t *mmio;
    hwaddr mmio_size;
};

static inline void mmio_write32(PCIBaseState *s, hwaddr addr, uint32_t val)
{
    if (addr + 4 > s->mmio_size) {
        return;
    }
    stl_le_p(s->mmio + addr, val);
}

static inline uint32_t mmio_read32(PCIBaseState *s, hwaddr addr)
{
    if (addr + 4 > s->mmio_size) {
        return 0;
    }
    return ldl_le_p(s->mmio + addr);
}

static inline void mmio_write16(PCIBaseState *s, hwaddr addr, uint16_t val)
{
    if (addr + 2 > s->mmio_size) {
        return;
    }
    stw_le_p(s->mmio + addr, val);
}

static inline uint16_t mmio_read16(PCIBaseState *s, hwaddr addr)
{
    if (addr + 2 > s->mmio_size) {
        return 0;
    }
    return lduw_le_p(s->mmio + addr);
}

static inline void mmio_write8(PCIBaseState *s, hwaddr addr, uint8_t val)
{
    if (addr + 1 > s->mmio_size) {
        return;
    }
    s->mmio[addr] = val;
}

static inline uint8_t mmio_read8(PCIBaseState *s, hwaddr addr)
{
    if (addr + 1 > s->mmio_size) {
        return 0;
    }
    return s->mmio[addr];
}

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t pending = s->intr_status & s->intr_enable;

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

static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    (void)s;
    (void)is_write;
    /* The driver performs real DMA to host memory; modeling exact descriptor
     * behavior would require descriptor layout structs not fully provided
     * here. For probe/initialization success, we do not need to simulate
     * data movement, so this is intentionally left empty.
     */
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    /* Handle specific registers with shadow semantics; otherwise
     * fall back to generic shadow memory behavior.
     */

    if (size == 4) {
        switch (addr) {
        case KS_DMA_TX_CTRL:
            return s->regs.tx_cfg;
        case KS_DMA_RX_CTRL:
            return s->regs.rx_cfg;
        case KS_DMA_TX_START:
            return s->regs.tx_start;
        case KS_DMA_RX_START:
            return s->regs.rx_start;
        case KS_DMA_TX_ADDR:
            return s->regs.tx_addr;
        case KS_DMA_RX_ADDR:
            return s->regs.rx_addr;
        case KS884X_INTERRUPTS_ENABLE:
            return s->intr_enable;
        case KS884X_INTERRUPTS_STATUS:
            return s->intr_status;
        default:
            break;
        }
    }

    if (size == 2) {
        switch (addr) {
        case KS884X_BUS_CTRL_OFFSET:
            /* hw_init writes BUS_SPEED_125_MHZ and never reads it back
             * before writing, but provide readback anyway.
             */
            return mmio_read16(s, addr);
        case KS884X_GLOBAL_CTRL_OFFSET:
            return mmio_read16(s, addr);
        case KS8841_WOL_CTRL_OFFSET:
            return mmio_read16(s, addr);
        case KS884X_EEPROM_CTRL_OFFSET:
            return mmio_read16(s, addr);
        case KS884X_CHIP_ID_OFFSET:
            return s->regs.chip_id;
        case KS884X_FAMILY_ID_OFFSET:
            return s->regs.family_id;
        default:
            break;
        }
    }

    if (size == 1) {
        switch (addr) {
        case KS884X_ADDR_0_OFFSET:
        case KS884X_ADDR_1_OFFSET:
        case KS884X_ADDR_2_OFFSET:
        case KS884X_ADDR_3_OFFSET:
        case KS884X_ADDR_4_OFFSET:
        case KS884X_ADDR_5_OFFSET:
        {
            /* MAC address shadow stored in generic mmio area */
            return mmio_read8(s, addr);
        }
        case KS884X_MULTICAST_0_OFFSET:
        case KS884X_MULTICAST_1_OFFSET:
        case KS884X_MULTICAST_2_OFFSET:
        case KS884x_MULTICAST_3_OFFSET:
        case KS884X_MULTICAST_4_OFFSET:
        case KS884X_MULTICAST_5_OFFSET:
        case KS884X_MULTICAST_6_OFFSET:
        case KS884X_MULTICAST_7_OFFSET:
            return mmio_read8(s, addr);
        default:
            break;
        }
    }

    /* Generic backing store */
    switch (size) {
    case 1:
        return mmio_read8(s, addr);
    case 2:
        return mmio_read16(s, addr);
    case 4:
        return mmio_read32(s, addr);
    default:
        return 0;
    }
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                               unsigned size)
{
    PCIBaseState *s = opaque;

    if (size == 4) {
        switch (addr) {
        case KS_DMA_TX_CTRL:
            s->regs.tx_cfg = (uint32_t)val;
            mmio_write32(s, addr, (uint32_t)val);
            return;
        case KS_DMA_RX_CTRL:
            s->regs.rx_cfg = (uint32_t)val;
            mmio_write32(s, addr, (uint32_t)val);
            return;
        case KS_DMA_TX_START:
            s->regs.tx_start = (uint32_t)val;
            mmio_write32(s, addr, (uint32_t)val);
            if (val & DMA_START) {
                pcibase_do_dma(s, true);
                /* Signal TX interrupt to match hw_send_pkt behaviour */
                s->intr_status |= KS884X_INT_TX;
                pcibase_update_irq(s);
            }
            return;
        case KS_DMA_RX_START:
            s->regs.rx_start = (uint32_t)val;
            mmio_write32(s, addr, (uint32_t)val);
            if (val & DMA_START) {
                pcibase_do_dma(s, false);
            }
            return;
        case KS_DMA_TX_ADDR:
            s->regs.tx_addr = (uint32_t)val;
            s->dma_info.tx_desc_base = (uint32_t)val;
            mmio_write32(s, addr, (uint32_t)val);
            return;
        case KS_DMA_RX_ADDR:
            s->regs.rx_addr = (uint32_t)val;
            s->dma_info.rx_desc_base = (uint32_t)val;
            mmio_write32(s, addr, (uint32_t)val);
            return;
        case KS884X_INTERRUPTS_ENABLE:
            s->intr_enable = (uint32_t)val;
            mmio_write32(s, addr, (uint32_t)val);
            pcibase_update_irq(s);
            return;
        case KS884X_INTERRUPTS_STATUS:
            /* W1C behavior: hw_ack_intr writes interrupt mask to clear */
            s->intr_status &= ~((uint32_t)val);
            mmio_write32(s, addr, s->intr_status);
            pcibase_update_irq(s);
            return;
        default:
            break;
        }
    }

    if (size == 2) {
        switch (addr) {
        case KS884X_BUS_CTRL_OFFSET:
            /* driver writes BUS_SPEED_125_MHZ */
            mmio_write16(s, addr, (uint16_t)val);
            return;
        case KS884X_GLOBAL_CTRL_OFFSET:
            /* hw_reset writes GLOBAL_SOFTWARE_RESET then 0; just shadow */
            mmio_write16(s, addr, (uint16_t)val);
            return;
        case KS8841_WOL_CTRL_OFFSET:
            /* hw_cfg_wol manipulates bits; store them */
            mmio_write16(s, addr, (uint16_t)val);
            return;
        case KS884X_EEPROM_CTRL_OFFSET:
            /* EEPROM GPIO emulation: just store value for readback */
            mmio_write16(s, addr, (uint16_t)val);
            return;
        default:
            break;
        }
    }

    if (size == 1) {
        switch (addr) {
        case KS884X_ADDR_0_OFFSET:
        case KS884X_ADDR_1_OFFSET:
        case KS884X_ADDR_2_OFFSET:
        case KS884X_ADDR_3_OFFSET:
        case KS884X_ADDR_4_OFFSET:
        case KS884X_ADDR_5_OFFSET:
            /* hw_set_addr writes MAC bytes here */
            mmio_write8(s, addr, (uint8_t)val);
            return;
        case KS884X_MULTICAST_0_OFFSET:
        case KS884X_MULTICAST_1_OFFSET:
        case KS884X_MULTICAST_2_OFFSET:
        case KS884x_MULTICAST_3_OFFSET:
        case KS884X_MULTICAST_4_OFFSET:
        case KS884X_MULTICAST_5_OFFSET:
        case KS884X_MULTICAST_6_OFFSET:
        case KS884X_MULTICAST_7_OFFSET:
            /* hw_set_grp_addr writes these; just shadow */
            mmio_write8(s, addr, (uint8_t)val);
            return;
        default:
            break;
        }
    }

    /* Generic backing store */
    switch (size) {
    case 1:
        mmio_write8(s, addr, (uint8_t)val);
        break;
    case 2:
        mmio_write16(s, addr, (uint16_t)val);
        break;
    case 4:
        mmio_write32(s, addr, (uint32_t)val);
        break;
    default:
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    (void)opaque;
    (void)addr;
    (void)size;
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val,
                              unsigned size)
{
    (void)opaque;
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
    PCIDevice *pdev = PCI_DEVICE(dev);
    uint8_t *pci_conf = pdev->config;

    pci_device_reset(pdev);

    /* Reinitialize register shadows to power-on defaults expected by driver */
    s->intr_status = 0;
    s->intr_enable = 0;

    s->regs.chip_id = REG_CHIP_ID_41;
    s->regs.family_id = REG_FAMILY_ID;
    s->regs.tx_cfg = 0;
    s->regs.rx_cfg = 0;
    s->regs.tx_start = 0;
    s->regs.rx_start = 0;
    s->regs.tx_addr = 0;
    s->regs.rx_addr = 0;
    s->dma_info.tx_desc_base = 0;
    s->dma_info.rx_desc_base = 0;

    /* Program PM capability to allow WOL helper functions to operate */
    int pm_pos = pci_find_capability(pdev, PCI_CAP_ID_PM);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
        pci_set_word(pci_conf + pm_pos + PCI_PM_CTRL, 0x0000);
    }

    /* Default MAC into address registers so hw_read_addr sees it */
    for (int i = 0; i < 6; i++) {
        mmio_write8(s, KS884X_ADDR_0_OFFSET + i, DEFAULT_MAC_ADDRESS[i]);
    }
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s,
                                 BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s,
                              bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s,
                              bi->name, aligned_size);
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCIBASE_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCIBASE_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCIBASE_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    s->bar_info[0].size  = 0x1000;
    s->bar_info[0].name  = "ksz884x-mmio";

    s->mmio_size = s->bar_info[0].size;
    s->mmio = g_malloc0(s->mmio_size);

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = false;
    s->has_msix = false;
    s->intr_status = 0;
    s->intr_enable = 0;
    s->regs.chip_id = REG_CHIP_ID_41;
    s->regs.family_id = REG_FAMILY_ID;
    s->regs.tx_cfg = 0;
    s->regs.rx_cfg = 0;
    s->regs.tx_start = 0;
    s->regs.rx_start = 0;
    s->regs.tx_addr = 0;
    s->regs.rx_addr = 0;
    s->dma_info.tx_desc_base = 0;
    s->dma_info.rx_desc_base = 0;
    s->status_flags = 0;
    s->reset_state.in_reset = false;
    s->pm_state.pm_state = 0;

    /* Provide default MAC in chip address registers */
    for (int i = 0; i < 6; i++) {
        mmio_write8(s, KS884X_ADDR_0_OFFSET + i, DEFAULT_MAC_ADDRESS[i]);
    }

    /* Set BUS speed default */
    mmio_write16(s, KS884X_BUS_CTRL_OFFSET, BUS_SPEED_125_MHZ);
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

    if (s->mmio) {
        g_free(s->mmio);
        s->mmio = NULL;
    }
}

static const VMStateDescription vmstate_pcibase = {
    .name = "ksz884xp_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(intr_status, PCIBaseState),
        VMSTATE_UINT32(intr_enable, PCIBaseState),
        VMSTATE_UINT16(regs.chip_id, PCIBaseState),
        VMSTATE_UINT8(regs.family_id, PCIBaseState),
        VMSTATE_UINT32(regs.tx_cfg, PCIBaseState),
        VMSTATE_UINT32(regs.rx_cfg, PCIBaseState),
        VMSTATE_UINT32(regs.tx_start, PCIBaseState),
        VMSTATE_UINT32(regs.rx_start, PCIBaseState),
        VMSTATE_UINT32(regs.tx_addr, PCIBaseState),
        VMSTATE_UINT32(regs.rx_addr, PCIBaseState),
        VMSTATE_UINT32(dma_info.tx_desc_base, PCIBaseState),
        VMSTATE_UINT32(dma_info.rx_desc_base, PCIBaseState),
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

type_init(pcibase_register_types)
