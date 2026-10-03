/*
 * QEMU device model for Ralink RT2800 PCI (RT2860) wireless adapter.
 * Generated based on Linux driver rt2800pci.c for QEMU 8.2.10.
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

#define TYPE_PCIBASE_DEVICE "rt2800pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Vendor/Device IDs from driver */
#define VENDOR_ID 0x1814
#define DEVICE_ID 0x0601
#define CLASS_ID  0x0280

/* BAR configuration */
#define BAR0_SIZE 0x8000

/* Firmware image base (from driver) */
#define FIRMWARE_IMAGE_BASE 0x2000
#define FIRMWARE_SIZE      0x4000

/* EEPROM size (in bytes, from driver define EEPROM_SIZE 0x0200) */
#define EEPROM_SIZE 0x0200

/* RF size (in bytes, from driver define RF_SIZE 0x0010) */
#define RF_SIZE 0x0010

/* Number of TX queues (6 from driver registers TX_BASE_PTR0..5) */
#define NUM_TX_QUEUES 6

/* Register offsets (from rt2800pci.c) */
#define MAC_CSR0                        0x1000
#define MAC_SYS_CTRL                    0x1004
#define MAC_STATUS_CFG                  0x1200
#define GPIO_CTRL                       0x0228
#define GPIO_CTRL_DIR2                  0x00000400
#define GPIO_CTRL_DIR3                  0x00000800
#define GPIO_CTRL_DIR4                  0x00001000
#define GPIO_CTRL_DIR6                  0x00004000
#define GPIO_CTRL_DIR7                  0x00008000
#define GPIO_CTRL_DIR8                  0x01000000
#define GPIO_CTRL_VAL2                  0x00000004
#define GPIO_CTRL_VAL3                  0x00000008
#define GPIO_CTRL_VAL4                  0x00000010
#define GPIO_CTRL_VAL6                  0x00000040
#define GPIO_CTRL_VAL7                  0x00000080
#define GPIO_CTRL_VAL8                  0x00010000
#define INT_SOURCE_CSR                  0x0200
#define INT_MASK_CSR                    0x0204
#define WPDMA_GLO_CFG                   0x0208
#define WPDMA_RST_IDX                   0x020c
#define PBF_SYS_CTRL                    0x0400
#define HOST_CMD_CSR                    0x0404
#define PBF_CFG                         0x0408
#define PBF_MAX_PCNT                    0x040c
#define RF_CSR_CFG                      0x0500
#define RF_CONTROL0                     0x0518
#define RF_BYPASS0                      0x051c
#define RF_CONTROL1                     0x0520
#define RF_BYPASS1                      0x0524
#define RF_CONTROL2                     0x0528
#define RF_BYPASS2                      0x052c
#define RF_CONTROL3                     0x0530
#define RF_BYPASS3                      0x0534
#define EFUSE_CTRL                      0x0580
#define EFUSE_DATA0                     0x0590
#define EFUSE_DATA1                     0x0594
#define EFUSE_DATA2                     0x0598
#define EFUSE_DATA3                     0x059c
#define EFUSE_CTRL_3290                 0x0024
#define EFUSE_DATA0_3290                0x0034
#define EFUSE_DATA1_3290                0x0030
#define EFUSE_DATA2_3290                0x002c
#define EFUSE_DATA3_3290                0x0028
#define MAC_WCID_BASE                   0x1800
#define MAC_WCID_ATTRIBUTE_BASE         0x6800
#define MAC_IVEIV_TABLE_BASE            0x6000
#define PAIRWISE_KEY_TABLE_BASE         0x4000
#define SHARED_KEY_TABLE_BASE           0x6c00
#define SHARED_KEY_MODE_BASE            0x7000
#define HW_BEACON_BASE0                 0x7800
#define HW_BEACON_BASE4                 0x7200
#define HW_BEACON_BASE6                 0x5dc0
#define TX_BASE_PTR0                    0x0230
#define TX_MAX_CNT0                     0x0234
#define TX_CTX_IDX0                     0x0238
#define TX_DTX_IDX0                     0x023c
#define TX_BASE_PTR1                    0x0240
#define TX_MAX_CNT1                     0x0244
#define TX_CTX_IDX1                     0x0248
#define TX_DTX_IDX1                     0x024c
#define TX_BASE_PTR2                    0x0250
#define TX_MAX_CNT2                     0x0244
#define TX_CTX_IDX2                     0x0258
#define TX_DTX_IDX2                     0x025c
#define TX_BASE_PTR3                    0x0260
#define TX_MAX_CNT3                     0x0264
#define TX_CTX_IDX3                     0x0268
#define TX_DTX_IDX3                     0x026c
#define TX_BASE_PTR4                    0x0270
#define TX_MAX_CNT4                     0x0274
#define TX_CTX_IDX4                     0x0278
#define TX_DTX_IDX4                     0x027c
#define TX_BASE_PTR5                    0x0280
#define TX_MAX_CNT5                     0x0284
#define TX_CTX_IDX5                     0x0288
#define TX_DTX_IDX5                     0x028c
#define RX_BASE_PTR                     0x0290
#define RX_MAX_CNT                      0x0294
#define RX_CRX_IDX                      0x0298
#define RX_DRX_IDX                      0x029c
#define US_CYC_CNT                      0x02a4
#define DELAY_INT_CFG                   0x0210
#define WMM_AIFSN_CFG                   0x0214
#define WMM_CWMIN_CFG                   0x0218
#define WMM_CWMAX_CFG                   0x021c
#define WMM_TXOP0_CFG                   0x0220
#define TX_PIN_CFG                      0x1328
#define TX_BAND_CFG                     0x132c
#define TX_SW_CFG0                      0x1330
#define TX_SW_CFG1                      0x1334
#define TX_SW_CFG2                      0x1338
#define TX_RTS_CFG                      0x1344
#define TX_RTY_CFG                      0x134c
#define TX_LINK_CFG                     0x1350
#define HT_FBK_CFG0                     0x1354
#define HT_FBK_CFG1                     0x1358
#define LG_FBK_CFG0                     0x135c
#define LG_FBK_CFG1                     0x1360
#define OFDM_PROT_CFG                   0x1368
#define CCK_PROT_CFG                    0x1364
#define MM20_PROT_CFG                   0x136c
#define MM40_PROT_CFG                   0x1370
#define GF20_PROT_CFG                   0x1374
#define GF40_PROT_CFG                   0x1378
#define TXOP_CTRL_CFG                   0x1340
#define TX_TIMEOUT_CFG                  0x1348
#define TX_PWR_CFG_0                    0x1314
#define TX_PWR_CFG_1                    0x1318
#define TX_PWR_CFG_2                    0x131c
#define TX_PWR_CFG_3                    0x1320
#define TX_PWR_CFG_4                    0x1324
#define TX_PWR_CFG_5                    0x1384
#define TX_PWR_CFG_6                    0x1388
#define TX_PWR_CFG_7                    0x13d4
#define TX_PWR_CFG_8                    0x13d8
#define TX_PWR_CFG_9                    0x13dc
#define TX0_BB_GAIN_ATTEN               0x13c0
#define TX1_BB_GAIN_ATTEN               0x13c4
#define TX0_RF_GAIN_CORRECT             0x13a0
#define TX1_RF_GAIN_CORRECT             0x13a4
#define TX0_RF_GAIN_ATTEN               0x13a8
#define TX1_RF_GAIN_ATTEN               0x13ac
#define TX_ALC_CFG_0                    0x13b0
#define TX_ALC_CFG_1                    0x13b4
#define TX_ALC_VGA3                     0x13c8
#define TX_FBK_CFG_3S_0                 0x13c4
#define TX_FBK_CFG_3S_1                 0x13c8
#define TX_TXBF_CFG_0                   0x138c
#define TX_TXBF_CFG_3                   0x13ac
#define AMPDU_MAX_LEN_20M1S             0x1030
#define AMPDU_MAX_LEN_20M2S             0x1034
#define AMPDU_MAX_LEN_40M1S             0x1038
#define AMPDU_MAX_LEN_40M2S             0x103C
#define AMPDU_BA_WINSIZE                0x1040
#define TX_STA_CNT0                     0x170c
#define TX_STA_CNT1                     0x1710
#define TX_STA_CNT2                     0x1714
#define TX_STA_FIFO                     0x1718
#define RX_STA_CNT0                     0x1700
#define RX_STA_CNT1                     0x1704
#define RX_STA_CNT2                     0x1708
#define RX_FILTER_CFG                   0x1400
#define AUTO_RSP_CFG                    0x1404
#define LEGACY_BASIC_RATE               0x1408
#define HT_BASIC_RATE                   0x140c
#define BCN_TIME_CFG                    0x1114
#define TBTT_SYNC_CFG                   0x1118
#define TSF_TIMER_DW0                   0x111c
#define TSF_TIMER_DW1                   0x1120
#define INT_TIMER_EN                    0x112c
#define INT_TIMER_CFG                   0x1128
#define CH_TIME_CFG                     0x110c
#define CH_IDLE_STA                     0x1130
#define CH_BUSY_STA                     0x1134
#define CH_BUSY_STA_SEC                 0x1138
#define BCN_OFFSET0                     0x042c
#define BCN_OFFSET1                     0x0430
#define AUX_CTRL                        0x10c
#define PWR_PIN_CFG                     0x1204
#define AUTOWAKEUP_CFG                  0x1208
#define LED_CFG                         0x102c
#define MAC_CSR0_3290                   0x0000
#define MAC_DEBUG_INDEX                 0x05e8
#define GPIO_SWITCH                     0x05dc
#define LDO_CFG0                        0x05d4
#define E2PROM_CSR                      0x0004
#define BBP_CSR_CFG                     0x101c
#define RF_CSR_CFG0                     0x1020
#define CMB_CTRL                        0x0020
#define COEX_CFG0                       0x0040
#define COEX_CFG2                       0x0048
#define OSC_CTRL                        0x0038
#define PLL_CTRL                        0x0050
#define WLAN_FUN_CTRL                   0x0080
#define OPT_14_CSR                      0x0114
#define H2M_MAILBOX_CSR                 0x7010
#define H2M_MAILBOX_CID                 0x7014
#define H2M_MAILBOX_STATUS              0x701c
#define H2M_BBP_AGENT                   0x7028
#define H2M_INT_SRC                     0x7024
#define MAC_BSSID_DW0                   0x1010
#define MAC_BSSID_DW1                   0x1014
#define MAC_ADDR_DW0                    0x1008
#define MAX_LEN_CFG                     0x1018
#define BKOFF_SLOT_CFG                  0x1104
#define XIFS_TIME_CFG                   0x1100
#define TXOP_HLDR_ET                    0x1608
#define EXP_ACK_TIME                    0x1380
#define HT_FBK_TO_LEGACY                0x1384
#define TX_PWR_CFG_EXT_0                0x1390
#define TX_PWR_CFG_EXT_1                0x1394
#define TX_PWR_CFG_EXT_2                0x1398
#define TX_PWR_CFG_EXT_3                0x139c
#define TX_PWR_CFG_EXT_4                0x13a0

/* Field definitions (from driver macros) */
struct rt2x00_field32 {
    unsigned int bit_offset;
    uint32_t bit_mask;
};

#define FIELD32(__mask)                         ({ \
    struct rt2x00_field32 __f = {               \
        ctz32(__mask), (__mask)                 \
    };                                          \
    __f; })

#define GET_FIELD(__reg, __field)               \
    (((__reg) & (__field).bit_mask) >> (__field).bit_offset)

#define SET_FIELD(__reg, __field, __value)      \
    do {                                        \
        *(__reg) &= ~((__field).bit_mask);      \
        *(__reg) |= ((__value) << ((__field).bit_offset)) & ((__field).bit_mask); \
    } while (0)

/* Interrupt fields */
#define INT_SOURCE_CSR_RX_DONE          FIELD32(0x00000004)
#define INT_SOURCE_CSR_TBTT             FIELD32(0x00000800)
#define INT_SOURCE_CSR_PRE_TBTT         FIELD32(0x00001000)
#define INT_SOURCE_CSR_TX_FIFO_STATUS   FIELD32(0x00002000)
#define INT_SOURCE_CSR_AUTO_WAKEUP      FIELD32(0x00004000)
#define INT_SOURCE_CSR_RX_COHERENT      FIELD32(0x00010000)
#define INT_SOURCE_CSR_TX_COHERENT      FIELD32(0x00020000)
#define INT_MASK_CSR_RX_DONE            FIELD32(0x00000004)
#define INT_MASK_CSR_TBTT               FIELD32(0x00000800)
#define INT_MASK_CSR_PRE_TBTT           FIELD32(0x00001000)
#define INT_MASK_CSR_TX_FIFO_STATUS     FIELD32(0x00002000)
#define INT_MASK_CSR_AUTO_WAKEUP        FIELD32(0x00004000)

/* MAC_CSR0 and MAC_SYS_CTRL fields */
#define MAC_SYS_CTRL_RESET_CSR          FIELD32(0x00000001)
#define MAC_SYS_CTRL_RESET_BBP          FIELD32(0x00000002)
#define MAC_SYS_CTRL_ENABLE_TX          FIELD32(0x00000004)
#define MAC_SYS_CTRL_ENABLE_RX          FIELD32(0x00000008)

/* WPDMA_GLO_CFG fields */
#define WPDMA_GLO_CFG_ENABLE_TX_DMA     FIELD32(0x00000001)
#define WPDMA_GLO_CFG_TX_DMA_BUSY       FIELD32(0x00000002)
#define WPDMA_GLO_CFG_ENABLE_RX_DMA     FIELD32(0x00000004)
#define WPDMA_GLO_CFG_RX_DMA_BUSY       FIELD32(0x00000008)
#define WPDMA_GLO_CFG_TX_WRITEBACK_DONE FIELD32(0x00000040)
#define WPDMA_GLO_CFG_BIG_ENDIAN        FIELD32(0x00000080)
#define WPDMA_GLO_CFG_WP_DMA_BURST_SIZE FIELD32(0x00000030)
#define WPDMA_GLO_CFG_RX_HDR_SCATTER    FIELD32(0x0000ff00)
#define WPDMA_GLO_CFG_HDR_SEG_LEN       FIELD32(0xffff0000)

/* PBF_SYS_CTRL fields */
#define PBF_SYS_CTRL_HOST_RAM_WRITE     FIELD32(0x00010000)
#define PBF_SYS_CTRL_READY              FIELD32(0x00000080)

/* H2M_MAILBOX fields */
#define H2M_MAILBOX_CSR_OWNER           FIELD32(0xff000000)
#define H2M_MAILBOX_CSR_CMD_TOKEN       FIELD32(0x00ff0000)
#define H2M_MAILBOX_CSR_ARG1            FIELD32(0x0000ff00)
#define H2M_MAILBOX_CSR_ARG0            FIELD32(0x000000ff)
#define H2M_MAILBOX_CID_CMD0             FIELD32(0x000000ff)
#define H2M_MAILBOX_CID_CMD1             FIELD32(0x0000ff00)
#define H2M_MAILBOX_CID_CMD2             FIELD32(0x00ff0000)
#define H2M_MAILBOX_CID_CMD3             FIELD32(0xff000000)

/* RF_CSR_CFG fields */
#define RF_CSR_CFG_REGNUM               FIELD32(0x00003f00)
#define RF_CSR_CFG_WRITE                FIELD32(0x00010000)
#define RF_CSR_CFG_BUSY                 FIELD32(0x00020000)
#define RF_CSR_CFG_DATA                 FIELD32(0x000000ff)

/* BBP_CSR_CFG fields */
#define BBP_CSR_CFG_REGNUM              FIELD32(0x0000ff00)
#define BBP_CSR_CFG_VALUE               FIELD32(0x000000ff)
#define BBP_CSR_CFG_BUSY                FIELD32(0x00020000)
#define BBP_CSR_CFG_READ_CONTROL        FIELD32(0x00010000)
#define BBP_CSR_CFG_BBP_RW_MODE         FIELD32(0x00080000)

/* EFUSE_CTRL fields */
#define EFUSE_CTRL_PRESENT              FIELD32(0x80000000)
#define EFUSE_CTRL_KICK                 FIELD32(0x40000000)
#define EFUSE_CTRL_MODE                 FIELD32(0x000000c0)
#define EFUSE_CTRL_ADDRESS_IN           FIELD32(0x03fe0000)

/* E2PROM_CSR fields (not used if efuse is present) */
#define E2PROM_CSR_CHIP_SELECT          FIELD32(0x00000002)
#define E2PROM_CSR_DATA_CLOCK           FIELD32(0x00000001)
#define E2PROM_CSR_DATA_IN              FIELD32(0x00000004)
#define E2PROM_CSR_DATA_OUT             FIELD32(0x00000008)
#define E2PROM_CSR_TYPE                 FIELD32(0x00000030)

/* Power state enum */
enum dev_state {
    STATE_DEEP_SLEEP = 0,
    STATE_SLEEP = 1,
    STATE_STANDBY = 2,
    STATE_AWAKE = 3,
    STATE_RADIO_ON,
    STATE_RADIO_OFF,
    STATE_RADIO_IRQ_ON,
    STATE_RADIO_IRQ_OFF,
};

struct PCIBaseState {
    PCIDevice parent_obj;
    MemoryRegion bar0;
    uint32_t regs[BAR0_SIZE / 4];          /* General register shadows */
    uint8_t fw_ram[FIRMWARE_SIZE];         /* Firmware RAM */
    uint16_t eeprom[EEPROM_SIZE / 2];      /* EEPROM content (used by efuse) */

    /* Special registers that need behavioral handling (kept separately) */
    uint32_t int_source;                   /* INT_SOURCE_CSR */
    uint32_t int_mask;                     /* INT_MASK_CSR */
    uint32_t efuse_ctrl;                   /* EFUSE_CTRL */
    uint32_t efuse_data[4];                /* EFUSE_DATA0..3 */
    uint32_t h2m_mailbox_csr;
    uint32_t h2m_mailbox_cid;
    uint32_t h2m_mailbox_status;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->int_source & s->int_mask) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* Helper: apply special write logic for EFUSE_CTRL */
static void efuse_ctrl_write(PCIBaseState *s, uint32_t val)
{
    uint32_t old = s->efuse_ctrl;
    s->efuse_ctrl = val;

    /* Check if KICK bit was set (trigger efuse read) */
    if (GET_FIELD(val, EFUSE_CTRL_KICK)) {
        uint32_t mode = GET_FIELD(val, EFUSE_CTRL_MODE);
        uint32_t addr = GET_FIELD(val, EFUSE_CTRL_ADDRESS_IN);
        uint32_t i;
        uint16_t word1, word2;

        /* Simulate read: copy words from eeprom to efuse_data registers.
         * MODE 0 = read, others ignored for now.
         * Each EFUSE_DATA register provides a 32-bit value containing two
         * consecutive 16-bit eeprom words, packed as little-endian.
         * Data is read in reverse register order: DATA3 corresponds to lowest address words.
         */
        if (mode == 0) {
            /* efuse_data[3] -> eeprom[addr] and eeprom[addr+1] */
            if (addr < (EEPROM_SIZE / 2)) {
                word1 = s->eeprom[addr];
            } else {
                word1 = 0xffff;
            }
            if ((addr + 1) < (EEPROM_SIZE / 2)) {
                word2 = s->eeprom[addr + 1];
            } else {
                word2 = 0xffff;
            }
            s->efuse_data[3] = (word2 << 16) | word1;

            /* efuse_data[2] -> eeprom[addr+2] and eeprom[addr+3] */
            if ((addr + 2) < (EEPROM_SIZE / 2)) {
                word1 = s->eeprom[addr + 2];
            } else {
                word1 = 0xffff;
            }
            if ((addr + 3) < (EEPROM_SIZE / 2)) {
                word2 = s->eeprom[addr + 3];
            } else {
                word2 = 0xffff;
            }
            s->efuse_data[2] = (word2 << 16) | word1;

            /* efuse_data[1] -> eeprom[addr+4] and eeprom[addr+5] */
            if ((addr + 4) < (EEPROM_SIZE / 2)) {
                word1 = s->eeprom[addr + 4];
            } else {
                word1 = 0xffff;
            }
            if ((addr + 5) < (EEPROM_SIZE / 2)) {
                word2 = s->eeprom[addr + 5];
            } else {
                word2 = 0xffff;
            }
            s->efuse_data[1] = (word2 << 16) | word1;

            /* efuse_data[0] -> eeprom[addr+6] and eeprom[addr+7] */
            if ((addr + 6) < (EEPROM_SIZE / 2)) {
                word1 = s->eeprom[addr + 6];
            } else {
                word1 = 0xffff;
            }
            if ((addr + 7) < (EEPROM_SIZE / 2)) {
                word2 = s->eeprom[addr + 7];
            } else {
                word2 = 0xffff;
            }
            s->efuse_data[0] = (word2 << 16) | word1;
        } else {
            /* Unknown mode: set all efuse data to 0xffffffff */
            for (i = 0; i < 4; i++) {
                s->efuse_data[i] = 0xffffffff;
            }
        }

        /* Clear KICK bit (auto-clear) */
        s->efuse_ctrl &= ~EFUSE_CTRL_KICK.bit_mask;
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = PCIBASE_DEVICE(opaque);
    uint32_t val = 0;

    /* Firmware RAM region */
    if (addr >= FIRMWARE_IMAGE_BASE && addr < FIRMWARE_IMAGE_BASE + FIRMWARE_SIZE) {
        hwaddr offset = addr - FIRMWARE_IMAGE_BASE;
        if (offset + size <= FIRMWARE_SIZE) {
            memcpy(&val, &s->fw_ram[offset], MIN(size, sizeof(val)));
            return val;
        }
    }

    /* Special registers with behavioral handling */
    switch (addr) {
    case INT_SOURCE_CSR:
        return s->int_source;
    case INT_MASK_CSR:
        return s->int_mask;
    case EFUSE_CTRL:
        return s->efuse_ctrl;
    case EFUSE_DATA0:
        return s->efuse_data[0];
    case EFUSE_DATA1:
        return s->efuse_data[1];
    case EFUSE_DATA2:
        return s->efuse_data[2];
    case EFUSE_DATA3:
        return s->efuse_data[3];
    case H2M_MAILBOX_CSR:
        return s->h2m_mailbox_csr;
    case H2M_MAILBOX_CID:
        return s->h2m_mailbox_cid;
    case H2M_MAILBOX_STATUS:
        return s->h2m_mailbox_status;
    case BBP_CSR_CFG:
        val = s->regs[addr / 4];
        /* Ensure BUSY bit is cleared on read */
        val &= ~BBP_CSR_CFG_BUSY.bit_mask;
        return val;
    case RF_CSR_CFG:
        val = s->regs[addr / 4];
        val &= ~RF_CSR_CFG_BUSY.bit_mask;
        return val;
    case WPDMA_GLO_CFG:
        val = s->regs[addr / 4];
        /* Mask out BUSY status bits on read (they are write-clear) */
        val &= ~(WPDMA_GLO_CFG_TX_DMA_BUSY.bit_mask | WPDMA_GLO_CFG_RX_DMA_BUSY.bit_mask);
        return val;
    default:
        /* All other registers: simple shadow */
        if (addr % 4 == 0 && addr < BAR0_SIZE) {
            return s->regs[addr / 4];
        }
        qemu_log_mask(LOG_UNIMP, "rt2800pci: unimplemented read at 0x%" HWADDR_PRIx " size %u\n", addr, size);
        return 0xffffffff;
    }
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = PCIBASE_DEVICE(opaque);
    uint32_t val32 = (uint32_t)val;

    /* Firmware RAM write */
    if (addr >= FIRMWARE_IMAGE_BASE && addr < FIRMWARE_IMAGE_BASE + FIRMWARE_SIZE) {
        hwaddr offset = addr - FIRMWARE_IMAGE_BASE;
        if (offset + size <= FIRMWARE_SIZE) {
            memcpy(&s->fw_ram[offset], &val32, MIN(size, sizeof(val32)));
        }
        return;
    }

    /* Special registers with behavioral handling */
    switch (addr) {
    case INT_SOURCE_CSR:
        /* Write-1-to-clear */
        s->int_source &= ~val32;
        pcibase_update_irq(s);
        break;
    case INT_MASK_CSR:
        s->int_mask = val32;
        pcibase_update_irq(s);
        break;
    case EFUSE_CTRL:
        efuse_ctrl_write(s, val32);
        break;
    case H2M_MAILBOX_CSR:
        s->h2m_mailbox_csr = val32;
        /* Simulate MCU command completion: set CID with token */
        {
            uint32_t token = GET_FIELD(val32, H2M_MAILBOX_CSR_CMD_TOKEN);
            s->h2m_mailbox_cid = token | (token << 8) | (token << 16) | (token << 24);
        }
        break;
    case H2M_MAILBOX_CID:
        s->h2m_mailbox_cid = val32;
        break;
    case H2M_MAILBOX_STATUS:
        s->h2m_mailbox_status = val32;
        break;
    case BBP_CSR_CFG:
        /* Clear BUSY bit to emulate instant completion */
        val32 &= ~BBP_CSR_CFG_BUSY.bit_mask;
        s->regs[addr / 4] = val32;
        break;
    case RF_CSR_CFG:
        val32 &= ~RF_CSR_CFG_BUSY.bit_mask;
        s->regs[addr / 4] = val32;
        break;
    case WPDMA_GLO_CFG:
        /* Mask out BUSY status bits on write */
        val32 &= ~(WPDMA_GLO_CFG_TX_DMA_BUSY.bit_mask | WPDMA_GLO_CFG_RX_DMA_BUSY.bit_mask);
        s->regs[addr / 4] = val32;
        break;
    default:
        if (addr % 4 == 0 && addr < BAR0_SIZE) {
            s->regs[addr / 4] = val32;
        } else {
            qemu_log_mask(LOG_UNIMP, "rt2800pci: unimplemented write at 0x%" HWADDR_PRIx " size %u val 0x%" PRIx64 "\n",
                          addr, size, val);
        }
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

/* Basic reset: clear registers and set initial values */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    memset(s->regs, 0, sizeof(s->regs));
    memset(s->fw_ram, 0, sizeof(s->fw_ram));
    s->int_source = 0;
    s->int_mask = 0;
    s->efuse_ctrl = EFUSE_CTRL_PRESENT.bit_mask;  /* efuse present */
    s->efuse_data[0] = 0;
    s->efuse_data[1] = 0;
    s->efuse_data[2] = 0;
    s->efuse_data[3] = 0;
    s->h2m_mailbox_csr = 0;
    s->h2m_mailbox_cid = 0;
    s->h2m_mailbox_status = 0;

    /* Set MAC_CSR0: chipset 0x2860 in upper 16 bits, revision 0x0100 in lower 16 bits */
    s->regs[MAC_CSR0 / 4] = 0x28600100;

    /* Initialize EEPROM with default content (MAC and calibration) */
    {
        /* Minimal valid EEPROM for RT2860: magic 0x2860, MAC addr, CRC placeholder */
        memset(s->eeprom, 0xff, sizeof(s->eeprom));
        s->eeprom[0] = 0x2860;   /* Magic */
        s->eeprom[1] = 0x0001;   /* Version */
        s->eeprom[2] = 0x0C00;   /* MAC byte 0-1: 00:0C */
        s->eeprom[3] = 0x0043;   /* MAC byte 2-3: 43:00 */
        s->eeprom[4] = 0x0000;   /* MAC byte 4-5: 00:00 */
        /* Remaining words zero; CRC to be filled manually for validation.
         * Real driver will validate EEPROM; adjust as needed.
         */
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Express capability */
    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);

    /* Power management capability (required by driver) */
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR 0: MMIO register space */
    memory_region_init_io(&s->bar0, OBJECT(s), &pcibase_mmio_ops, s, "rt2800pci-mmio", BAR0_SIZE);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar0);

    /* Reset device state */
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
}

static const VMStateDescription vmstate_pcibase = {
    .name = "rt2800pci",
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
