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

/* Additional include files retrieved from driver context */
/* No additional includes */

#define TYPE_PCIBASE_DEVICE "rtl818x_pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x10ec
#define DEVICE_ID 0x8199
#define CLASS_ID PCI_CLASS_NETWORK_OTHER

/* CSR register offsets from struct rtl818x_csr - corrected to match driver layout */
#define CSR_MAC               0x00
#define CSR_MAR               0x08
#define CSR_TBKDA             0x10
#define CSR_TBEDA             0x14
#define CSR_TSFT              0x18
#define CSR_TLPDA             0x20
#define CSR_TNPDA             0x24
#define CSR_THPDA             0x28
#define CSR_BRSR              0x2c
#define CSR_BSSID             0x2e
#define CSR_RESP_RATE_EIFS    0x34
#define CSR_CMD               0x37
#define CSR_INT_MASK_STATUS   0x3c
#define CSR_TX_CONF           0x40
#define CSR_RX_CONF           0x44
#define CSR_INT_TIMEOUT       0x48
#define CSR_TBDA              0x4c
#define CSR_EEPROM_CMD        0x50
#define CSR_CONFIG0           0x51
#define CSR_CONFIG1           0x52
#define CSR_CONFIG2           0x53
#define CSR_ANAPARAM          0x54
#define CSR_MSR               0x58
#define CSR_CONFIG3           0x59
#define CSR_CONFIG4           0x5a
#define CSR_TESTR             0x5b
#define CSR_PGSELECT          0x5e
#define CSR_SECURITY          0x5f
#define CSR_ANAPARAM2         0x60
#define CSR_IMR               0x6c
#define CSR_BEACON_INTERVAL   0x70
#define CSR_ATIM_WND          0x72
#define CSR_BEACON_INTERVAL_TIME 0x74
#define CSR_ATIMTR_INTERVAL   0x76
#define CSR_PHY_DELAY         0x78
#define CSR_CARRIER_SENSE_COUNTER 0x79
#define CSR_PHY               0x7c
#define CSR_RFPinsOutput      0x80
#define CSR_RFPinsEnable      0x82
#define CSR_RFPinsSelect      0x84
#define CSR_RFPinsInput       0x86
#define CSR_RF_PARA           0x88
#define CSR_RF_TIMING         0x8c
#define CSR_GP_ENABLE         0x90
#define CSR_GPIO0             0x91
#define CSR_GPIO1             0x92
#define CSR_TPPOLL_STOP       0x93
#define CSR_HSSI_PARA         0x94
#define CSR_TX_AGC_CTL        0x9c
#define CSR_TX_GAIN_CCK       0x9d
#define CSR_TX_GAIN_OFDM      0x9e
#define CSR_TX_ANTENNA        0x9f
#define CSR_WPA_CONF          0xb0
#define CSR_SIFS              0xb4
#define CSR_DIFS              0xb5
#define CSR_SLOT              0xb6
#define CSR_CW_CONF           0xbc
#define CSR_CW_VAL            0xbd
#define CSR_RATE_FALLBACK     0xbe
#define CSR_ACM_CONTROL       0xbf
#define CSR_CONFIG5           0xd8
#define CSR_TX_DMA_POLLING    0xd9
#define CSR_PHY_PR            0xda
#define CSR_CWR               0xdc
#define CSR_RETRY_CTR         0xde
#define CSR_INT_MIG           0xe2
#define CSR_RDSAR             0xe4
#define CSR_TID_AC_MAP        0xe8
#define CSR_ANAPARAM3         0xee
#define CSR_AC_VO_PARAM       0xf0
#define CSR_AC_VI_PARAM_FEMR  0xf4
#define CSR_AC_BE_PARAM_TALLY_CNT 0xf8
#define CSR_TALLY_SEL_AC_BK_PARAM 0xfc

/* Register bit definitions */
/* CMD bits */
#define RTL818X_CMD_TX_ENABLE       (1 << 2)
#define RTL818X_CMD_RX_ENABLE       (1 << 3)
#define RTL818X_CMD_RESET           (1 << 4)
/* Interrupt bits (common) */
#define RTL818X_INT_RX_OK           (1 <<  0)
#define RTL818X_INT_RX_ERR          (1 <<  1)
#define RTL818X_INT_TXL_OK          (1 <<  2)
#define RTL818X_INT_TXL_ERR         (1 <<  3)
#define RTL818X_INT_RX_DU           (1 <<  4)
#define RTL818X_INT_RX_FO           (1 <<  5)
#define RTL818X_INT_TXN_OK          (1 <<  6)
#define RTL818X_INT_TXN_ERR         (1 <<  7)
#define RTL818X_INT_TXH_OK          (1 <<  8)
#define RTL818X_INT_TXH_ERR         (1 <<  9)
#define RTL818X_INT_TXB_OK          (1 << 10)
#define RTL818X_INT_TXB_ERR         (1 << 11)
#define RTL818X_INT_ATIM            (1 << 12)
#define RTL818X_INT_BEACON          (1 << 13)
#define RTL818X_INT_TIME_OUT        (1 << 14)
#define RTL818X_INT_TX_FO           (1 << 15)
/* Interrupt bits for rtl8187se */
#define RTL818X_INT_SE_TIMER3       (1 <<  0)
#define RTL818X_INT_SE_TIMER2       (1 <<  1)
#define RTL818X_INT_SE_RQ0SOR       (1 <<  2)
#define RTL818X_INT_SE_TXBED_OK     (1 <<  3)
#define RTL818X_INT_SE_TXBED_ERR    (1 <<  4)
#define RTL818X_INT_SE_TXBE_OK      (1 <<  5)
#define RTL818X_INT_SE_TXBE_ERR     (1 <<  6)
#define RTL818X_INT_SE_RX_OK        (1 <<  7)
#define RTL818X_INT_SE_RX_ERR       (1 <<  8)
#define RTL818X_INT_SE_TXL_OK       (1 <<  9)
#define RTL818X_INT_SE_TXL_ERR      (1 << 10)
#define RTL818X_INT_SE_RX_DU        (1 << 11)
#define RTL818X_INT_SE_RX_FIFO      (1 << 12)
#define RTL818X_INT_SE_TXN_OK       (1 << 13)
#define RTL818X_INT_SE_TXN_ERR      (1 << 14)
#define RTL818X_INT_SE_TXH_OK       (1 << 15)
#define RTL818X_INT_SE_TXH_ERR      (1 << 16)
#define RTL818X_INT_SE_TXB_OK       (1 << 17)
#define RTL818X_INT_SE_TXB_ERR      (1 << 18)
#define RTL818X_INT_SE_ATIM_TO      (1 << 19)
#define RTL818X_INT_SE_BK_TO        (1 << 20)
#define RTL818X_INT_SE_TIMER1       (1 << 21)
#define RTL818X_INT_SE_TX_FIFO      (1 << 22)
#define RTL818X_INT_SE_WAKEUP       (1 << 23)
#define RTL818X_INT_SE_BK_DMA       (1 << 24)
#define RTL818X_INT_SE_TMGD_OK      (1 << 30)
/* TX_CONF bits */
#define RTL818X_TX_CONF_LOOPBACK_MAC    (1 << 17)
#define RTL818X_TX_CONF_LOOPBACK_CONT   (3 << 17)
#define RTL818X_TX_CONF_NO_ICV          (1 << 19)
#define RTL818X_TX_CONF_DISCW           (1 << 20)
#define RTL818X_TX_CONF_SAT_HWPLCP      (1 << 24)
#define RTL818X_TX_CONF_R8180_ABCD      (2 << 25)
#define RTL818X_TX_CONF_R8180_F         (3 << 25)
#define RTL818X_TX_CONF_R8185_ABC       (4 << 25)
#define RTL818X_TX_CONF_R8185_D         (5 << 25)
#define RTL818X_TX_CONF_R8187vD         (5 << 25)
#define RTL818X_TX_CONF_R8187vD_B       (6 << 25)
#define RTL818X_TX_CONF_RTL8187SE       (6 << 25)
#define RTL818X_TX_CONF_HWVER_MASK      (7 << 25)
#define RTL818X_TX_CONF_DISREQQSIZE     (1 << 28)
#define RTL818X_TX_CONF_PROBE_DTS       (1 << 29)
#define RTL818X_TX_CONF_HW_SEQNUM       (1 << 30)
#define RTL818X_TX_CONF_CW_MIN          (1 << 31)
/* RX_CONF bits */
#define RTL818X_RX_CONF_MONITOR         (1 <<  0)
#define RTL818X_RX_CONF_NICMAC          (1 <<  1)
#define RTL818X_RX_CONF_MULTICAST       (1 <<  2)
#define RTL818X_RX_CONF_BROADCAST       (1 <<  3)
#define RTL818X_RX_CONF_FCS             (1 <<  5)
#define RTL818X_RX_CONF_DATA            (1 << 18)
#define RTL818X_RX_CONF_CTRL            (1 << 19)
#define RTL818X_RX_CONF_MGMT            (1 << 20)
#define RTL818X_RX_CONF_ADDR3           (1 << 21)
#define RTL818X_RX_CONF_PM              (1 << 22)
#define RTL818X_RX_CONF_BSSID           (1 << 23)
#define RTL818X_RX_CONF_RX_AUTORESETPHY (1 << 28)
#define RTL818X_RX_CONF_CSDM1           (1 << 29)
#define RTL818X_RX_CONF_CSDM2           (1 << 30)
#define RTL818X_RX_CONF_ONLYERLPKT      (1 << 31)
/* EEPROM_CMD bits */
#define RTL818X_EEPROM_CMD_READ     (1 << 0)
#define RTL818X_EEPROM_CMD_WRITE    (1 << 1)
#define RTL818X_EEPROM_CMD_CK       (1 << 2)
#define RTL818X_EEPROM_CMD_CS       (1 << 3)
#define RTL818X_EEPROM_CMD_NORMAL   (0 << 6)
#define RTL818X_EEPROM_CMD_LOAD     (1 << 6)
#define RTL818X_EEPROM_CMD_PROGRAM  (2 << 6)
#define RTL818X_EEPROM_CMD_CONFIG   (3 << 6)
/* CONFIG2 bits */
#define RTL818X_CONFIG2_ANTENNA_DIV (1 << 6)
/* MSR bits */
#define RTL818X_MSR_NO_LINK     (0 << 2)
#define RTL818X_MSR_ADHOC       (1 << 2)
#define RTL818X_MSR_INFRA       (2 << 2)
#define RTL818X_MSR_MASTER      (3 << 2)
#define RTL818X_MSR_ENEDCA      (4 << 2)
/* CONFIG3 bits */
#define RTL818X_CONFIG3_ANAPARAM_WRITE  (1 << 6)
#define RTL818X_CONFIG3_GNT_SELECT      (1 << 7)
/* CONFIG4 bits */
#define RTL818X_CONFIG4_POWEROFF    (1 << 6)
#define RTL818X_CONFIG4_VCOOFF      (1 << 7)
/* IMR bits (8187se) */
#define IMR_TMGDOK      ((1 << 30))
#define IMR_DOT11HINT   ((1 << 25))
#define IMR_BCNDMAINT   ((1 << 24))
#define IMR_WAKEINT     ((1 << 23))
#define IMR_TXFOVW      ((1 << 22))
#define IMR_TIMEOUT1    ((1 << 21))
#define IMR_BCNINT      ((1 << 20))
#define IMR_ATIMINT     ((1 << 19))
#define IMR_TBDER       ((1 << 18))
#define IMR_TBDOK       ((1 << 17))
#define IMR_THPDER      ((1 << 16))
#define IMR_THPDOK      ((1 << 15))
#define IMR_TVODER      ((1 << 14))
#define IMR_TVODOK      ((1 << 13))
#define IMR_FOVW        ((1 << 12))
#define IMR_RDU         ((1 << 11))
#define IMR_TVIDER      ((1 << 10))
#define IMR_TVIDOK      ((1 << 9))
#define IMR_RER         ((1 << 8))
#define IMR_ROK         ((1 << 7))
#define IMR_TBEDER      ((1 << 6))
#define IMR_TBEDOK      ((1 << 5))
#define IMR_TBKDER      ((1 << 4))
#define IMR_TBKDOK      ((1 << 3))
#define IMR_RQOSOK      ((1 << 2))
#define IMR_TIMEOUT2    ((1 << 1))
#define IMR_TIMEOUT3    ((1 << 0))
/* TPPOLL_STOP bits */
#define RTL818x_TPPOLL_STOP_BQ  (1 << 7)
#define RTL818x_TPPOLL_STOP_VI  (1 << 4)
#define RTL818x_TPPOLL_STOP_VO  (1 << 5)
#define RTL818x_TPPOLL_STOP_BE  (1 << 3)
#define RTL818x_TPPOLL_STOP_BK  (1 << 2)
#define RTL818x_TPPOLL_STOP_MG  (1 << 1)
#define RTL818x_TPPOLL_STOP_HI  (1 << 6)
/* TX_AGC_CTL bits */
#define RTL818X_TX_AGC_CTL_PERPACKET_GAIN    (1 << 0)
#define RTL818X_TX_AGC_CTL_PERPACKET_ANTSEL  (1 << 1)
#define RTL818X_TX_AGC_CTL_FEEDBACK_ANT      (1 << 2)
/* CW_CONF bits */
#define RTL818X_CW_CONF_PERPACKET_CW     (1 << 0)
#define RTL818X_CW_CONF_PERPACKET_RETRY  (1 << 1)
/* RATE_FALLBACK bits */
#define RTL818X_RATE_FALLBACK_ENABLE     (1 << 7)
/* AC parameters */
#define AC_PARAM_TXOP_LIMIT_SHIFT    16
#define AC_PARAM_ECW_MAX_SHIFT      12
#define AC_PARAM_ECW_MIN_SHIFT      8
#define AC_PARAM_AIFS_SHIFT         0

/* Hardware revision constants */
#define RTL818X_R8187B_B    0
#define RTL818X_R8187B_D    1
#define RTL818X_R8187B_E    2

/* CSR structure exactly as in the driver */
typedef uint16_t __le16;
typedef uint32_t __le32;
struct rtl818x_csr {
    uint8_t     MAC[6];
    uint8_t     reserved_0[2];
    union {
        __le32  MAR[2];
        struct __attribute__((packed)) {
            uint8_t     rf_sw_config;
            uint8_t     reserved_01[3];
            __le32      TMGDA;
        };
    } __attribute__((packed));
    union {
        struct __attribute__((packed)) {
            uint8_t     RX_FIFO_COUNT;
            uint8_t     reserved_1;
            uint8_t     TX_FIFO_COUNT;
            uint8_t     BQREQ;
        };
        __le32      TBKDA;
    } __attribute__((packed));
    __le32      TBEDA;
    __le32      TSFT[2];
    union {
        __le32      TLPDA;
        __le32      TVIDA;
    } __attribute__((packed));
    union {
        __le32      TNPDA;
        __le32      TVODA;
    } __attribute__((packed));
    __le32      THPDA;
    union {
        struct __attribute__((packed)) {
            uint8_t     reserved_2a;
            uint8_t     EIFS_8187SE;
        };
        __le16      BRSR;
    } __attribute__((packed));
    uint8_t     BSSID[6];
    union {
        struct __attribute__((packed)) {
            uint8_t     RESP_RATE;
            uint8_t     EIFS;
        };
        __le16      BRSR_8187SE;
    } __attribute__((packed));
    uint8_t     reserved_3[1];
    uint8_t     CMD;
    uint8_t     reserved_4[4];
    union {
        struct __attribute__((packed)) {
            __le16      INT_MASK;
            __le16      INT_STATUS;
        };
        __le32      INT_STATUS_SE;
    } __attribute__((packed));
    __le32      TX_CONF;
    __le32      RX_CONF;
    __le32      INT_TIMEOUT;
    __le32      TBDA;
    uint8_t     EEPROM_CMD;
    uint8_t     CONFIG0;
    uint8_t     CONFIG1;
    uint8_t     CONFIG2;
    __le32      ANAPARAM;
    uint8_t     MSR;
    uint8_t     CONFIG3;
    uint8_t     CONFIG4;
    uint8_t     TESTR;
    uint8_t     reserved_9[2];
    uint8_t     PGSELECT;
    uint8_t     SECURITY;
    __le32      ANAPARAM2;
    uint8_t     reserved_10[8];
    __le32      IMR;
    __le16      BEACON_INTERVAL;
    __le16      ATIM_WND;
    __le16      BEACON_INTERVAL_TIME;
    __le16      ATIMTR_INTERVAL;
    uint8_t     PHY_DELAY;
    uint8_t     CARRIER_SENSE_COUNTER;
    uint8_t     reserved_11[2];
    uint8_t     PHY[4];
    __le16      RFPinsOutput;
    __le16      RFPinsEnable;
    __le16      RFPinsSelect;
    __le16      RFPinsInput;
    __le32      RF_PARA;
    __le32      RF_TIMING;
    uint8_t     GP_ENABLE;
    uint8_t     GPIO0;
    uint8_t     GPIO1;
    uint8_t     TPPOLL_STOP;
    __le32      HSSI_PARA;
    uint8_t     reserved_13[4];
    uint8_t     TX_AGC_CTL;
    uint8_t     TX_GAIN_CCK;
    uint8_t     TX_GAIN_OFDM;
    uint8_t     TX_ANTENNA;
    uint8_t     reserved_14[16];
    uint8_t     WPA_CONF;
    uint8_t     reserved_15[3];
    uint8_t     SIFS;
    uint8_t     DIFS;
    uint8_t     SLOT;
    uint8_t     reserved_16[5];
    uint8_t     CW_CONF;
    uint8_t     CW_VAL;
    uint8_t     RATE_FALLBACK;
    uint8_t     ACM_CONTROL;
    uint8_t     reserved_17[24];
    uint8_t     CONFIG5;
    uint8_t     TX_DMA_POLLING;
    uint8_t     PHY_PR;
    uint8_t     reserved_18;
    __le16      CWR;
    uint8_t     RETRY_CTR;
    uint8_t     reserved_19[3];
    __le16      INT_MIG;
    __le32      RDSAR;
    __le16      TID_AC_MAP;
    uint8_t     reserved_20[4];
    union {
        __le16      ANAPARAM3;
        uint8_t     ANAPARAM3A;
    } __attribute__((packed));
    __le32      AC_VO_PARAM;
    union {
        __le32      AC_VI_PARAM;
        __le16      FEMR;
    } __attribute__((packed));
    union {
        __le32      AC_BE_PARAM;
        struct __attribute__((packed)) {
            uint8_t     reserved_21[2];
            __le16      TALLY_CNT;
        };
    } __attribute__((packed));
    union {
        uint8_t     TALLY_SEL;
        __le32      AC_BK_PARAM;
    } __attribute__((packed));
} __attribute__((packed));

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

    struct rtl818x_csr regs;

    struct {
        dma_addr_t base;
        uint32_t count;
        uint32_t status;
    } dma;

    uint32_t status;
    uint8_t pm_state;
    uint32_t anaparam;
    uint16_t rfparam;

    uint16_t eeprom_data[64];
    bool eeprom_cs;
    bool eeprom_ck;
    bool eeprom_di;
    bool eeprom_do;
    enum { EEPROM_IDLE, EEPROM_CMD, EEPROM_ADDR, EEPROM_READ } eeprom_state;
    uint8_t eeprom_bit_count;
    uint32_t eeprom_shift;
    uint8_t eeprom_address_width;
    uint8_t eeprom_address;
    uint16_t eeprom_read_data;

    bool phy_write_pending;
    uint32_t phy_write_data;
};

/* Forward declarations */
static void eeprom_update_cs(PCIBaseState *s, bool cs);
static void eeprom_update_ck(PCIBaseState *s, bool ck);

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t status = le32_to_cpu(s->regs.INT_STATUS_SE);
    uint32_t mask = le32_to_cpu(s->regs.IMR);
    if (status & mask) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
        case CSR_MAC ... CSR_MAC+5:
            if (size == 1) val = s->regs.MAC[addr - CSR_MAC];
            else if (size == 2) val = lduw_le_p(&s->regs.MAC[addr - CSR_MAC]);
            else if (size == 4) val = ldl_le_p(&s->regs.MAC[addr - CSR_MAC]);
            break;
        case CSR_MAR:
            if (size == 4) val = le32_to_cpu(s->regs.MAR[0]);
            else val = ldl_le_p(&s->regs.MAR[0]);
            break;
        case CSR_MAR + 4:
            if (size == 4) val = le32_to_cpu(s->regs.MAR[1]);
            break;
        case CSR_TBKDA:
            if (size == 4) val = le32_to_cpu(s->regs.TBKDA);
            break;
        case CSR_TBEDA:
            if (size == 4) val = le32_to_cpu(s->regs.TBEDA);
            break;
        case CSR_TSFT:
            if (size == 4) val = le32_to_cpu(s->regs.TSFT[0]);
            break;
        case CSR_TSFT + 4:
            if (size == 4) val = le32_to_cpu(s->regs.TSFT[1]);
            break;
        case CSR_TLPDA:
            if (size == 4) val = le32_to_cpu(s->regs.TLPDA);
            break;
        case CSR_TNPDA:
            if (size == 4) val = le32_to_cpu(s->regs.TNPDA);
            break;
        case CSR_THPDA:
            if (size == 4) val = le32_to_cpu(s->regs.THPDA);
            break;
        case CSR_BRSR:
            if (size == 2) val = le16_to_cpu(s->regs.BRSR);
            break;
        case CSR_BSSID ... CSR_BSSID+5:
            if (size == 1) val = s->regs.BSSID[addr - CSR_BSSID];
            else if (size == 2) val = lduw_le_p(&s->regs.BSSID[addr - CSR_BSSID]);
            else if (size == 4) val = ldl_le_p(&s->regs.BSSID[addr - CSR_BSSID]);
            break;
        case CSR_RESP_RATE_EIFS:
            if (size == 2) val = le16_to_cpu(s->regs.BRSR_8187SE);
            break;
        case CSR_CMD:
            if (size == 1) val = s->regs.CMD;
            break;
        case CSR_INT_MASK_STATUS:
            val = le32_to_cpu(s->regs.INT_STATUS_SE);
            break;
        case CSR_INT_MASK_STATUS + 2:
            val = le16_to_cpu(s->regs.INT_MASK);
            break;
        case CSR_TX_CONF:
            if (size == 4) val = le32_to_cpu(s->regs.TX_CONF);
            break;
        case CSR_RX_CONF:
            if (size == 4) val = le32_to_cpu(s->regs.RX_CONF);
            break;
        case CSR_INT_TIMEOUT:
            if (size == 4) val = le32_to_cpu(s->regs.INT_TIMEOUT);
            break;
        case CSR_TBDA:
            if (size == 4) val = le32_to_cpu(s->regs.TBDA);
            break;
        case CSR_EEPROM_CMD:
            val = s->regs.EEPROM_CMD;
            if (s->eeprom_do) val |= RTL818X_EEPROM_CMD_READ;
            else val &= ~RTL818X_EEPROM_CMD_READ;
            break;
        case CSR_CONFIG0: val = s->regs.CONFIG0; break;
        case CSR_CONFIG1: val = s->regs.CONFIG1; break;
        case CSR_CONFIG2: val = s->regs.CONFIG2; break;
        case CSR_ANAPARAM:
            if (size == 4) val = le32_to_cpu(s->regs.ANAPARAM);
            break;
        case CSR_MSR: val = s->regs.MSR; break;
        case CSR_CONFIG3: val = s->regs.CONFIG3; break;
        case CSR_CONFIG4: val = s->regs.CONFIG4; break;
        case CSR_TESTR: val = s->regs.TESTR; break;
        case CSR_PGSELECT: val = s->regs.PGSELECT; break;
        case CSR_SECURITY: val = s->regs.SECURITY; break;
        case CSR_ANAPARAM2:
            if (size == 4) val = le32_to_cpu(s->regs.ANAPARAM2);
            break;
        case CSR_IMR:
            if (size == 4) val = le32_to_cpu(s->regs.IMR);
            break;
        case CSR_BEACON_INTERVAL:
            if (size == 2) val = le16_to_cpu(s->regs.BEACON_INTERVAL);
            break;
        case CSR_ATIM_WND:
            if (size == 2) val = le16_to_cpu(s->regs.ATIM_WND);
            break;
        case CSR_BEACON_INTERVAL_TIME:
            if (size == 2) val = le16_to_cpu(s->regs.BEACON_INTERVAL_TIME);
            break;
        case CSR_ATIMTR_INTERVAL:
            if (size == 2) val = le16_to_cpu(s->regs.ATIMTR_INTERVAL);
            break;
        case CSR_PHY_DELAY: val = s->regs.PHY_DELAY; break;
        case CSR_CARRIER_SENSE_COUNTER: val = s->regs.CARRIER_SENSE_COUNTER; break;
        case CSR_PHY:
            if (size == 4) val = ldl_le_p(s->regs.PHY);
            break;
        case CSR_PHY + 2:
            if (size == 4) {
                val = ldl_le_p(&s->regs.PHY[2]);
                if (s->phy_write_pending) {
                    val = (val & ~0xFF) | s->phy_write_data;
                }
            }
            break;
        case CSR_RFPinsOutput:
            if (size == 2) val = le16_to_cpu(s->regs.RFPinsOutput);
            break;
        case CSR_RFPinsEnable:
            if (size == 2) val = le16_to_cpu(s->regs.RFPinsEnable);
            break;
        case CSR_RFPinsSelect:
            if (size == 2) val = le16_to_cpu(s->regs.RFPinsSelect);
            break;
        case CSR_RFPinsInput:
            if (size == 2) val = le16_to_cpu(s->regs.RFPinsInput);
            break;
        case CSR_RF_PARA:
            if (size == 4) val = le32_to_cpu(s->regs.RF_PARA);
            break;
        case CSR_RF_TIMING:
            if (size == 4) val = le32_to_cpu(s->regs.RF_TIMING);
            break;
        case CSR_GP_ENABLE: val = s->regs.GP_ENABLE; break;
        case CSR_GPIO0: val = s->regs.GPIO0; break;
        case CSR_GPIO1: val = s->regs.GPIO1; break;
        case CSR_TPPOLL_STOP: val = s->regs.TPPOLL_STOP; break;
        case CSR_HSSI_PARA:
            if (size == 4) val = le32_to_cpu(s->regs.HSSI_PARA);
            break;
        case CSR_TX_AGC_CTL: val = s->regs.TX_AGC_CTL; break;
        case CSR_TX_GAIN_CCK: val = s->regs.TX_GAIN_CCK; break;
        case CSR_TX_GAIN_OFDM: val = s->regs.TX_GAIN_OFDM; break;
        case CSR_TX_ANTENNA: val = s->regs.TX_ANTENNA; break;
        case CSR_WPA_CONF: val = s->regs.WPA_CONF; break;
        case CSR_SIFS: val = s->regs.SIFS; break;
        case CSR_DIFS: val = s->regs.DIFS; break;
        case CSR_SLOT: val = s->regs.SLOT; break;
        case CSR_CW_CONF: val = s->regs.CW_CONF; break;
        case CSR_CW_VAL: val = s->regs.CW_VAL; break;
        case CSR_RATE_FALLBACK: val = s->regs.RATE_FALLBACK; break;
        case CSR_ACM_CONTROL: val = s->regs.ACM_CONTROL; break;
        case CSR_CONFIG5: val = s->regs.CONFIG5; break;
        case CSR_TX_DMA_POLLING: val = s->regs.TX_DMA_POLLING; break;
        case CSR_PHY_PR: val = s->regs.PHY_PR; break;
        case CSR_CWR:
            if (size == 2) val = le16_to_cpu(s->regs.CWR);
            break;
        case CSR_RETRY_CTR: val = s->regs.RETRY_CTR; break;
        case CSR_INT_MIG:
            if (size == 2) val = le16_to_cpu(s->regs.INT_MIG);
            break;
        case CSR_RDSAR:
            if (size == 4) val = le32_to_cpu(s->regs.RDSAR);
            break;
        case CSR_TID_AC_MAP:
            if (size == 2) val = le16_to_cpu(s->regs.TID_AC_MAP);
            break;
        case CSR_ANAPARAM3:
            if (size == 2) val = le16_to_cpu(s->regs.ANAPARAM3);
            break;
        case CSR_AC_VO_PARAM:
            if (size == 4) val = le32_to_cpu(s->regs.AC_VO_PARAM);
            break;
        case CSR_AC_VI_PARAM_FEMR:
            if (size == 4) val = le32_to_cpu(s->regs.AC_VI_PARAM);
            break;
        case CSR_AC_BE_PARAM_TALLY_CNT:
            if (size == 4) val = le32_to_cpu(s->regs.AC_BE_PARAM);
            break;
        case CSR_TALLY_SEL_AC_BK_PARAM:
            if (size == 4) val = le32_to_cpu(s->regs.AC_BK_PARAM);
            else if (size == 1) val = s->regs.TALLY_SEL;
            break;
        default:
            qemu_log_mask(LOG_UNIMP, "rtl818x: unsupported read addr 0x%" HWADDR_PRIx "\n", addr);
            val = ~0ULL;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
        case CSR_MAC ... CSR_MAC+5:
            if (size == 1) s->regs.MAC[addr - CSR_MAC] = val;
            else if (size == 2) stw_le_p(&s->regs.MAC[addr - CSR_MAC], val);
            else if (size == 4) stl_le_p(&s->regs.MAC[addr - CSR_MAC], val);
            break;
        case CSR_MAR:
            if (size == 4) s->regs.MAR[0] = cpu_to_le32(val);
            else stl_le_p(&s->regs.MAR[0], val);
            break;
        case CSR_MAR + 4:
            if (size == 4) s->regs.MAR[1] = cpu_to_le32(val);
            break;
        case CSR_TBKDA:
            if (size == 4) s->regs.TBKDA = cpu_to_le32(val);
            break;
        case CSR_TBEDA:
            if (size == 4) s->regs.TBEDA = cpu_to_le32(val);
            break;
        case CSR_TSFT:
            if (size == 4) s->regs.TSFT[0] = cpu_to_le32(val);
            break;
        case CSR_TSFT + 4:
            if (size == 4) s->regs.TSFT[1] = cpu_to_le32(val);
            break;
        case CSR_TLPDA:
            if (size == 4) s->regs.TLPDA = cpu_to_le32(val);
            break;
        case CSR_TNPDA:
            if (size == 4) s->regs.TNPDA = cpu_to_le32(val);
            break;
        case CSR_THPDA:
            if (size == 4) s->regs.THPDA = cpu_to_le32(val);
            break;
        case CSR_BRSR:
            if (size == 2) s->regs.BRSR = cpu_to_le16(val);
            break;
        case CSR_BSSID ... CSR_BSSID+5:
            if (size == 1) s->regs.BSSID[addr - CSR_BSSID] = val;
            else if (size == 2) stw_le_p(&s->regs.BSSID[addr - CSR_BSSID], val);
            else if (size == 4) stl_le_p(&s->regs.BSSID[addr - CSR_BSSID], val);
            break;
        case CSR_RESP_RATE_EIFS:
            if (size == 2) s->regs.BRSR_8187SE = cpu_to_le16(val);
            break;
        case CSR_CMD:
            if (size == 1) s->regs.CMD = val;
            break;
        case CSR_INT_MASK_STATUS:
            if (size == 4) {
                uint32_t cur = le32_to_cpu(s->regs.INT_STATUS_SE);
                cur &= ~(val & 0xFFFFFFFF);
                s->regs.INT_STATUS_SE = cpu_to_le32(cur);
                pcibase_update_irq(s);
            } else if (size == 2 && addr == CSR_INT_MASK_STATUS) {
                s->regs.INT_MASK = cpu_to_le16(val);
                pcibase_update_irq(s);
            }
            break;
        case CSR_INT_MASK_STATUS + 2:
            if (size == 2) s->regs.INT_STATUS = cpu_to_le16(val);
            break;
        case CSR_TX_CONF:
            if (size == 4) s->regs.TX_CONF = cpu_to_le32(val);
            break;
        case CSR_RX_CONF:
            if (size == 4) s->regs.RX_CONF = cpu_to_le32(val);
            break;
        case CSR_INT_TIMEOUT:
            if (size == 4) s->regs.INT_TIMEOUT = cpu_to_le32(val);
            break;
        case CSR_TBDA:
            if (size == 4) s->regs.TBDA = cpu_to_le32(val);
            break;
        case CSR_EEPROM_CMD: {
            uint8_t old_val = s->regs.EEPROM_CMD;
            s->regs.EEPROM_CMD = val;
            bool new_cs = (val >> 3) & 1;
            bool new_ck = (val >> 2) & 1;
            bool new_di = (val >> 1) & 1;
            if (new_cs != ((old_val >> 3) & 1))
                eeprom_update_cs(s, new_cs);
            if (new_ck != ((old_val >> 2) & 1))
                eeprom_update_ck(s, new_ck);
            s->eeprom_di = new_di;
            break;
        }
        case CSR_CONFIG0: s->regs.CONFIG0 = val; break;
        case CSR_CONFIG1: s->regs.CONFIG1 = val; break;
        case CSR_CONFIG2: s->regs.CONFIG2 = val; break;
        case CSR_ANAPARAM:
            if (size == 4) s->regs.ANAPARAM = cpu_to_le32(val);
            break;
        case CSR_MSR: s->regs.MSR = val; break;
        case CSR_CONFIG3: s->regs.CONFIG3 = val; break;
        case CSR_CONFIG4: s->regs.CONFIG4 = val; break;
        case CSR_TESTR: s->regs.TESTR = val; break;
        case CSR_PGSELECT: s->regs.PGSELECT = val; break;
        case CSR_SECURITY: s->regs.SECURITY = val; break;
        case CSR_ANAPARAM2:
            if (size == 4) s->regs.ANAPARAM2 = cpu_to_le32(val);
            break;
        case CSR_IMR:
            if (size == 4) {
                s->regs.IMR = cpu_to_le32(val);
                pcibase_update_irq(s);
            }
            break;
        case CSR_BEACON_INTERVAL:
            if (size == 2) s->regs.BEACON_INTERVAL = cpu_to_le16(val);
            break;
        case CSR_ATIM_WND:
            if (size == 2) s->regs.ATIM_WND = cpu_to_le16(val);
            break;
        case CSR_BEACON_INTERVAL_TIME:
            if (size == 2) s->regs.BEACON_INTERVAL_TIME = cpu_to_le16(val);
            break;
        case CSR_ATIMTR_INTERVAL:
            if (size == 2) s->regs.ATIMTR_INTERVAL = cpu_to_le16(val);
            break;
        case CSR_PHY_DELAY: s->regs.PHY_DELAY = val; break;
        case CSR_CARRIER_SENSE_COUNTER: s->regs.CARRIER_SENSE_COUNTER = val; break;
        case CSR_PHY:
            if (size == 4) {
                uint32_t wval = cpu_to_le32(val);
                if (val & 0x80) {
                    s->phy_write_data = val;
                    s->phy_write_pending = true;
                } else if (s->phy_write_pending && !(val & 0x80)) {
                    s->phy_write_pending = false;
                    s->phy_write_data = val & 0xFF;
                }
                stl_le_p(s->regs.PHY, wval);
            }
            break;
        case CSR_RFPinsOutput:
            if (size == 2) s->regs.RFPinsOutput = cpu_to_le16(val);
            break;
        case CSR_RFPinsEnable:
            if (size == 2) s->regs.RFPinsEnable = cpu_to_le16(val);
            break;
        case CSR_RFPinsSelect:
            if (size == 2) s->regs.RFPinsSelect = cpu_to_le16(val);
            break;
        case CSR_RFPinsInput:
            if (size == 2) s->regs.RFPinsInput = cpu_to_le16(val);
            break;
        case CSR_RF_PARA:
            if (size == 4) s->regs.RF_PARA = cpu_to_le32(val);
            break;
        case CSR_RF_TIMING:
            if (size == 4) s->regs.RF_TIMING = cpu_to_le32(val);
            break;
        case CSR_GP_ENABLE: s->regs.GP_ENABLE = val; break;
        case CSR_GPIO0: s->regs.GPIO0 = val; break;
        case CSR_GPIO1: s->regs.GPIO1 = val; break;
        case CSR_TPPOLL_STOP: s->regs.TPPOLL_STOP = val; break;
        case CSR_HSSI_PARA:
            if (size == 4) s->regs.HSSI_PARA = cpu_to_le32(val);
            break;
        case CSR_TX_AGC_CTL: s->regs.TX_AGC_CTL = val; break;
        case CSR_TX_GAIN_CCK: s->regs.TX_GAIN_CCK = val; break;
        case CSR_TX_GAIN_OFDM: s->regs.TX_GAIN_OFDM = val; break;
        case CSR_TX_ANTENNA: s->regs.TX_ANTENNA = val; break;
        case CSR_WPA_CONF: s->regs.WPA_CONF = val; break;
        case CSR_SIFS: s->regs.SIFS = val; break;
        case CSR_DIFS: s->regs.DIFS = val; break;
        case CSR_SLOT: s->regs.SLOT = val; break;
        case CSR_CW_CONF: s->regs.CW_CONF = val; break;
        case CSR_CW_VAL: s->regs.CW_VAL = val; break;
        case CSR_RATE_FALLBACK: s->regs.RATE_FALLBACK = val; break;
        case CSR_ACM_CONTROL: s->regs.ACM_CONTROL = val; break;
        case CSR_CONFIG5: s->regs.CONFIG5 = val; break;
        case CSR_TX_DMA_POLLING: s->regs.TX_DMA_POLLING = val; break;
        case CSR_PHY_PR: s->regs.PHY_PR = val; break;
        case CSR_CWR:
            if (size == 2) s->regs.CWR = cpu_to_le16(val);
            break;
        case CSR_RETRY_CTR: s->regs.RETRY_CTR = val; break;
        case CSR_INT_MIG:
            if (size == 2) s->regs.INT_MIG = cpu_to_le16(val);
            break;
        case CSR_RDSAR:
            if (size == 4) s->regs.RDSAR = cpu_to_le32(val);
            break;
        case CSR_TID_AC_MAP:
            if (size == 2) s->regs.TID_AC_MAP = cpu_to_le16(val);
            break;
        case CSR_ANAPARAM3:
            if (size == 2) s->regs.ANAPARAM3 = cpu_to_le16(val);
            break;
        case CSR_AC_VO_PARAM:
            if (size == 4) s->regs.AC_VO_PARAM = cpu_to_le32(val);
            break;
        case CSR_AC_VI_PARAM_FEMR:
            if (size == 4) s->regs.AC_VI_PARAM = cpu_to_le32(val);
            break;
        case CSR_AC_BE_PARAM_TALLY_CNT:
            if (size == 4) s->regs.AC_BE_PARAM = cpu_to_le32(val);
            break;
        case CSR_TALLY_SEL_AC_BK_PARAM:
            if (size == 4) s->regs.AC_BK_PARAM = cpu_to_le32(val);
            else if (size == 1) s->regs.TALLY_SEL = val;
            break;
        default:
            qemu_log_mask(LOG_UNIMP, "rtl818x: unsupported write addr 0x%" HWADDR_PRIx " val 0x%" PRIx64 "\n", addr, val);
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    return pcibase_mmio_read(s, addr, size);
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    pcibase_mmio_write(s, addr, val, size);
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

static void eeprom_update_cs(PCIBaseState *s, bool cs)
{
    if (cs && !s->eeprom_cs) {
        s->eeprom_state = EEPROM_CMD;
        s->eeprom_bit_count = 0;
        s->eeprom_shift = 0;
    } else if (!cs && s->eeprom_cs) {
        s->eeprom_state = EEPROM_IDLE;
        s->eeprom_do = false;
    }
    s->eeprom_cs = cs;
}

static void eeprom_update_ck(PCIBaseState *s, bool ck)
{
    if (!s->eeprom_cs) return;
    if (ck && !s->eeprom_ck) {
        s->eeprom_shift = (s->eeprom_shift << 1) | (s->eeprom_di ? 1 : 0);
        s->eeprom_bit_count++;

        switch (s->eeprom_state) {
        case EEPROM_CMD:
            if (s->eeprom_bit_count == 3) {
                uint8_t op = (s->eeprom_shift >> 1) & 3;
                if (op == 2) {
                    s->eeprom_state = EEPROM_ADDR;
                    s->eeprom_address_width = 8;
                } else if (op == 1) {
                    s->eeprom_state = EEPROM_ADDR;
                } else {
                    s->eeprom_state = EEPROM_IDLE;
                }
            }
            break;
        case EEPROM_ADDR:
            if (s->eeprom_bit_count == 3 + s->eeprom_address_width) {
                s->eeprom_address = s->eeprom_shift & ((1 << s->eeprom_address_width) - 1);
                if (s->eeprom_address < 64) {
                    s->eeprom_read_data = s->eeprom_data[s->eeprom_address];
                } else {
                    s->eeprom_read_data = 0;
                }
                s->eeprom_state = EEPROM_READ;
                s->eeprom_bit_count = 0;
                s->eeprom_do = (s->eeprom_read_data >> 15) & 1;
            }
            break;
        case EEPROM_READ:
            if (s->eeprom_bit_count < 16) {
                s->eeprom_do = (s->eeprom_read_data >> (15 - s->eeprom_bit_count)) & 1;
            } else {
                s->eeprom_state = EEPROM_IDLE;
            }
            break;
        default:
            break;
        }
    }
    s->eeprom_ck = ck;
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    memset(&s->regs, 0, sizeof(s->regs));
    s->regs.TX_CONF = cpu_to_le32(RTL818X_TX_CONF_RTL8187SE);
    s->regs.RX_CONF = cpu_to_le32(1 << 6);

    memset(s->eeprom_data, 0, sizeof(s->eeprom_data));
    s->eeprom_data[0x06] = 0x0009;
    s->eeprom_data[0x07] = 0x1100;
    s->eeprom_data[0x08] = 0x3322;
    s->eeprom_data[0x09] = 0x5544;
    s->eeprom_data[0x0D] = 0;
    s->eeprom_data[0x0E] = 0;
    s->eeprom_data[0x19] = 0;
    s->eeprom_data[0x3F] = 0x0100;
    s->eeprom_data[0x7C] = 0;

    s->eeprom_state = EEPROM_IDLE;
    s->eeprom_cs = false;
    s->eeprom_ck = false;
    s->eeprom_di = false;
    s->eeprom_do = false;
    s->eeprom_bit_count = 0;
    s->eeprom_shift = 0;
    s->eeprom_address_width = 8;
    s->phy_write_pending = false;
    s->phy_write_data = 0;
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR 0: PIO, BAR 1: MMIO - both covering whole register space */
    s->num_bars = 2;
    s->bar_info[0] = (BARInfo){
        .index = 0,
        .type = BAR_TYPE_PIO,
        .size = sizeof(struct rtl818x_csr),
        .name = "rtl818x-pio"
    };
    s->bar_info[1] = (BARInfo){
        .index = 1,
        .type = BAR_TYPE_MMIO,
        .size = sizeof(struct rtl818x_csr),
        .name = "rtl818x-mmio"
    };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
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
    .name = "rtl818x_pci_pci",
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
