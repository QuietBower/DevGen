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


#define TYPE_PCIBASE_DEVICE "qla1280_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_QLOGIC           0x1077
#define PCI_DEVICE_ID_QLOGIC_ISP12160  0x1216
#define PCI_CLASS_STORAGE_SCSI         0x0100

/* Register offsets (from struct device_reg) */
#define REG_ID_L              0x00
#define REG_ID_H              0x02
#define REG_CFG0              0x04
#define REG_CFG1              0x06
#define REG_ICTRL             0x08
#define REG_ISTATUS           0x0A
#define REG_SEMAPHORE         0x0C
#define REG_NVRAM             0x0E
#define REG_FLASH_DATA        0x10
#define REG_FLASH_ADDRESS     0x12
/* 0x14-0x1f unused_1 */
#define REG_CDMA_CFG          0x20
#define REG_CDMA_CTRL         0x22
#define REG_CDMA_STATUS       0x24
#define REG_CDMA_FIFO_STATUS  0x26
#define REG_CDMA_COUNT        0x28
#define REG_CDMA_RESERVED     0x2A
#define REG_CDMA_ADDRESS_COUNT_0 0x2C
#define REG_CDMA_ADDRESS_COUNT_1 0x2E
#define REG_CDMA_ADDRESS_COUNT_2 0x30
#define REG_CDMA_ADDRESS_COUNT_3 0x32
/* 0x34-0x3f unused_2 */
#define REG_DDMA_CFG          0x40
#define REG_DDMA_CTRL         0x42
#define REG_DDMA_STATUS       0x44
#define REG_DDMA_FIFO_STATUS  0x46
#define REG_DDMA_XFER_COUNT_LOW 0x48
#define REG_DDMA_XFER_COUNT_HIGH 0x4A
#define REG_DDMA_ADDR_COUNT_0 0x4C
#define REG_DDMA_ADDR_COUNT_1 0x4E
#define REG_DDMA_ADDR_COUNT_2 0x50
#define REG_DDMA_ADDR_COUNT_3 0x52
/* 0x54-0x6f unused_3 */
#define REG_MAILBOX0          0x70
#define REG_MAILBOX1          0x72
#define REG_MAILBOX2          0x74
#define REG_MAILBOX3          0x76
#define REG_MAILBOX4          0x78
#define REG_MAILBOX5          0x7A
#define REG_MAILBOX6          0x7C
#define REG_MAILBOX7          0x7E
/* 0x80-0xbf unused_4 */
#define REG_HOST_CMD          0xC0
/* 0xC2-0xcb unused_5 */
#define REG_GPIO_DATA         0xCC
#define REG_GPIO_ENABLE       0xCE
/* 0xD0-0xf1 unused_6 */
#define REG_SCSI_CONTROL_PINS 0xF2

/* ISP register bit definitions (extracted from driver macros) */
#define ISP_RESET        BIT_0
#define ISP_EN_INT       BIT_1
#define ISP_EN_RISC      BIT_2
#define RISC_INT         BIT_2
#define PCI_INT          BIT_1
#define HOST_INT         BIT_7
#define BIOS_ENABLE      BIT_0

#define ISP_CFG0_HWMSK   0x000f
#define ISP_CFG0_1020     1
#define ISP_CFG0_1020A    2
#define ISP_CFG0_1040     3
#define ISP_CFG0_1040A    4
#define ISP_CFG0_1040B    5
#define ISP_CFG0_1040C    6

#define ISP_CFG1_F128     BIT_6
#define ISP_CFG1_F64      BIT_4|BIT_5
#define ISP_CFG1_F32      BIT_5
#define ISP_CFG1_F16      BIT_4
#define ISP_CFG1_BENAB    BIT_2
#define ISP_CFG1_SXP      BIT_0

#define PCI_64BIT_SLOT    BIT_14
#define ISP_FLASH_ENABLE  BIT_8
#define ISP_FLASH_UPPER   BIT_9

#define NV_DESELECT      0
#define NV_CLOCK         BIT_0
#define NV_SELECT        BIT_1
#define NV_DATA_OUT      BIT_2
#define NV_DATA_IN       BIT_3

#define CDMA_CONF_SENAB   BIT_3
#define CDMA_CONF_RIRQ    BIT_2
#define CDMA_CONF_BENAB   BIT_1
#define CDMA_CONF_DIR     BIT_0

#define DDMA_CONF_SENAB   BIT_3
#define DDMA_CONF_RIRQ    BIT_2
#define DDMA_CONF_BENAB   BIT_1
#define DDMA_CONF_DIR     BIT_0

/* Host Command register bits (from driver macros) */
#define HC_CLR_RISC_INT   0x7000
#define HC_CLR_HOST_INT   0x6000
#define HC_DISABLE_BIOS   0x9000
#define HC_RESET_RISC     0x1000
#define HC_RELEASE_RISC   0x3000
#define HC_SET_HOST_INT   0x5000
#define HC_PAUSE_RISC     0x2000

/* Mailbox command codes (selected) */
#define MBC_LOAD_RAM_A64_ROM     9
#define MBC_DUMP_RAM_A64_ROM     0x0a
#define MBC_MAILBOX_REGISTER_TEST 6
#define MBC_WRITE_RAM_WORD       4
#define MBC_VERIFY_CHECKSUM      7
#define MBC_EXECUTE_FIRMWARE     2
#define MBC_INIT_RESPONSE_QUEUE_A64 0x53
#define MBC_INIT_REQUEST_QUEUE_A64  0x52
#define MBC_SET_INITIATOR_ID     0x30
#define MBC_SET_CLOCK_RATE       0x34
#define MBC_SET_TAG_AGE_LIMIT    0x33
#define MBC_SET_ACTIVE_NEGATION  0x35
#define MBC_SET_SYSTEM_PARAMETER 0x45
#define MBC_SET_RETRY_COUNT      0x32
#define MBC_SET_DATA_OVERRUN_RECOVERY 0x5A
#define MBC_SET_SELECTION_TIMEOUT 0x31
#define MBC_SET_ASYNC_DATA_SETUP 0x36
#define MBC_SET_FIRMWARE_FEATURES 0x4A
#define MBC_SET_PCI_CONTROL      0x37
#define MBC_BUS_RESET            0x18
#define MBC_ABORT_TARGET         0x17
#define MBC_ABORT_COMMAND        0x15
#define MBC_GET_TARGET_PARAMETERS 0x28
#define MBC_SET_DEVICE_QUEUE     0x39
#define MBC_SET_TARGET_PARAMETERS 0x38

#define PROD_ID_1                0x4953
#define PROD_ID_2                0x0000
#define PROD_ID_3                0x2020
#define PROD_ID_4                0x1

#define BIT_0  0x1
#define BIT_1  0x2
#define BIT_2  0x4
#define BIT_3  0x8
#define BIT_4  0x10
#define BIT_5  0x20
#define BIT_6  0x40
#define BIT_7  0x80
#define BIT_8  0x100
#define BIT_9  0x200
#define BIT_10 0x400
#define BIT_11 0x800
#define BIT_12 0x1000
#define BIT_13 0x2000
#define BIT_14 0x4000
#define BIT_15 0x8000
#define BIT_25 0x2000000
#define BIT_26 0x4000000
#define BIT_31 0x80000000

/* Additional required command codes (not in provided driver snippet) */
#define LOAD_CMD                   0x1b
#define MBS_BUSY                   0x0004
#define MBS_CMD_CMP                0x4000
#define CMD_ARGS                   (BIT_7 | BIT_6 | BIT_4 | BIT_3 | BIT_2 | BIT_1 | BIT_0) /* 0xDF */

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
    int     bar_flags; /* Additional BAR flags (e.g., 64-bit, prefetch) */
} BARInfo;

/* Hardware register definition extracted from driver's struct device_reg */
struct device_reg {
    uint16_t id_l;          /* ID low */
    uint16_t id_h;          /* ID high */
    uint16_t cfg_0;         /* Configuration 0 */
    uint16_t cfg_1;         /* Configuration 1 */
    uint16_t ictrl;         /* Interface control */
    uint16_t istatus;       /* Interface status */
    uint16_t semaphore;     /* Semaphore */
    uint16_t nvram;         /* NVRAM register. */
    uint16_t flash_data;    /* Flash BIOS data */
    uint16_t flash_address; /* Flash BIOS address */

    uint16_t unused_1[0x06];

    /* cdma_* and ddma_* are 1040 only */
    uint16_t cdma_cfg;
    uint16_t cdma_ctrl;
    uint16_t cdma_status;
    uint16_t cdma_fifo_status;
    uint16_t cdma_count;
    uint16_t cdma_reserved;
    uint16_t cdma_address_count_0;
    uint16_t cdma_address_count_1;
    uint16_t cdma_address_count_2;
    uint16_t cdma_address_count_3;

    uint16_t unused_2[0x06];

    uint16_t ddma_cfg;
    uint16_t ddma_ctrl;
    uint16_t ddma_status;
    uint16_t ddma_fifo_status;
    uint16_t ddma_xfer_count_low;
    uint16_t ddma_xfer_count_high;
    uint16_t ddma_addr_count_0;
    uint16_t ddma_addr_count_1;
    uint16_t ddma_addr_count_2;
    uint16_t ddma_addr_count_3;

    uint16_t unused_3[0x0e];

    uint16_t mailbox0;       /* Mailbox 0 */
    uint16_t mailbox1;       /* Mailbox 1 */
    uint16_t mailbox2;       /* Mailbox 2 */
    uint16_t mailbox3;       /* Mailbox 3 */
    uint16_t mailbox4;       /* Mailbox 4 */
    uint16_t mailbox5;       /* Mailbox 5 */
    uint16_t mailbox6;       /* Mailbox 6 */
    uint16_t mailbox7;       /* Mailbox 7 */

    uint16_t unused_4[0x20]; /* 0x80-0xbf Gap */

    uint16_t host_cmd;       /* Host command and control */

    uint16_t unused_5[0x5];  /* 0xc2-0xcb Gap */

    uint16_t gpio_data;
    uint16_t gpio_enable;

    uint16_t unused_6[0x11]; /* d0-f0 */
    uint16_t scsiControlPins; /* f2 */
};

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    struct device_reg reg;

    /* DMA Context */
    dma_addr_t request_dma;
    uint32_t request_ring_cnt;
    dma_addr_t response_dma;
    uint32_t response_ring_cnt;
    uint16_t risc_ram[0x8000]; /* 32K words RISC RAM */

    /* Operational status flags */
    bool firmware_loaded;
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool int_enabled = (s->reg.ictrl & ISP_EN_INT) != 0;
    bool risc_int = (s->reg.istatus & RISC_INT) != 0;
    if (int_enabled && risc_int) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

static void pcibase_process_mailbox(PCIBaseState *s)
{
    uint8_t cmd = s->reg.mailbox0 & 0xFF;
    uint64_t dma_addr;
    uint32_t word_count;

    switch (cmd) {
    case MBC_MAILBOX_REGISTER_TEST:
        /* Echo back; mailboxes retain the written values */
        s->reg.mailbox0 = MBS_CMD_CMP;
        break;

    case MBC_WRITE_RAM_WORD:
        /* Not used for DMA firmware loading, but handled for completeness */
        s->reg.mailbox0 = MBS_CMD_CMP;
        break;

    case LOAD_CMD:
        /* Reconstruct 64-bit DMA address: mb[6]<<48 | mb[7]<<32 | mb[2]<<16 | mb[3] */
        dma_addr = ((uint64_t)s->reg.mailbox6 << 48) |
                   ((uint64_t)s->reg.mailbox7 << 32) |
                   (s->reg.mailbox2 << 16) |
                   s->reg.mailbox3;
        word_count = s->reg.mailbox4;
        if (word_count > 0x8000) {
            /* Limit to RISC RAM size */
            word_count = 0x8000;
        }
        /* Read from host memory into local RISC RAM */
        pci_dma_read(PCI_DEVICE(s), dma_addr,
                     &s->risc_ram[s->reg.mailbox1], word_count * 2);
        s->reg.mailbox0 = MBS_CMD_CMP;
        break;

    case MBC_VERIFY_CHECKSUM:
        /* Pretend checksum is always OK */
        s->reg.mailbox1 = 0;
        s->reg.mailbox0 = MBS_CMD_CMP;
        break;

    case MBC_EXECUTE_FIRMWARE:
        s->firmware_loaded = true;
        s->reg.mailbox0 = MBS_CMD_CMP;
        break;

    case MBC_INIT_REQUEST_QUEUE_A64:
        s->request_dma = ((uint64_t)s->reg.mailbox6 << 48) |
                         ((uint64_t)s->reg.mailbox7 << 32) |
                         (s->reg.mailbox2 << 16) |
                         s->reg.mailbox3;
        s->request_ring_cnt = s->reg.mailbox1;
        s->reg.mailbox0 = MBS_CMD_CMP;
        break;

    case MBC_INIT_RESPONSE_QUEUE_A64:
        s->response_dma = ((uint64_t)s->reg.mailbox6 << 48) |
                          ((uint64_t)s->reg.mailbox7 << 32) |
                          (s->reg.mailbox2 << 16) |
                          s->reg.mailbox3;
        s->response_ring_cnt = s->reg.mailbox1;
        s->reg.mailbox0 = MBS_CMD_CMP;
        break;

    case MBC_BUS_RESET:
        /* Just acknowledge; delay is handled by driver via ssleep */
        s->reg.mailbox0 = MBS_CMD_CMP;
        break;

    case MBC_GET_TARGET_PARAMETERS:
        /* Return default parameters (async, no wide, no PPR) */
        s->reg.mailbox3 = 0; /* sync period/offset */
        s->reg.mailbox2 = 0; /* flags */
        s->reg.mailbox6 = 0; /* PPR flags */
        s->reg.mailbox0 = MBS_CMD_CMP;
        break;

    default:
        /* All other commands succeed silently */
        s->reg.mailbox0 = MBS_CMD_CMP;
        break;
    }

    /* Set completion status and raise interrupt */
    s->reg.semaphore |= BIT_0;
    s->reg.istatus |= RISC_INT;
    pcibase_update_irq(s);
}

/* PIO Handlers */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = (PCIBaseState *)opaque;
    uint64_t val = 0xFFFFFFFFFFFFFFFFULL;

    /* All registers are 16-bit; support only 2-byte aligned accesses. */
    if (size != 2 || (addr & 1)) {
        return val;
    }

    switch (addr) {
    case REG_ID_L:            val = s->reg.id_l; break;
    case REG_ID_H:            val = s->reg.id_h; break;
    case REG_CFG0:            val = s->reg.cfg_0; break;
    case REG_CFG1:            val = s->reg.cfg_1; break;
    case REG_ICTRL:           val = s->reg.ictrl; break;
    case REG_ISTATUS:         val = s->reg.istatus; break;
    case REG_SEMAPHORE:       val = s->reg.semaphore; break;
    case REG_NVRAM:           /* Return stored value but force NV_DATA_IN=0 */
                              val = (s->reg.nvram & ~NV_DATA_IN);
                              break;
    case REG_FLASH_DATA:      val = s->reg.flash_data; break;
    case REG_FLASH_ADDRESS:   val = s->reg.flash_address; break;
    /* CDMA registers */
    case REG_CDMA_CFG:               val = s->reg.cdma_cfg; break;
    case REG_CDMA_CTRL:              val = s->reg.cdma_ctrl; break;
    case REG_CDMA_STATUS:            val = s->reg.cdma_status; break;
    case REG_CDMA_FIFO_STATUS:       val = s->reg.cdma_fifo_status; break;
    case REG_CDMA_COUNT:             val = s->reg.cdma_count; break;
    case REG_CDMA_RESERVED:          val = s->reg.cdma_reserved; break;
    case REG_CDMA_ADDRESS_COUNT_0:   val = s->reg.cdma_address_count_0; break;
    case REG_CDMA_ADDRESS_COUNT_1:   val = s->reg.cdma_address_count_1; break;
    case REG_CDMA_ADDRESS_COUNT_2:   val = s->reg.cdma_address_count_2; break;
    case REG_CDMA_ADDRESS_COUNT_3:   val = s->reg.cdma_address_count_3; break;
    /* DDMA registers */
    case REG_DDMA_CFG:               val = s->reg.ddma_cfg; break;
    case REG_DDMA_CTRL:              val = s->reg.ddma_ctrl; break;
    case REG_DDMA_STATUS:            val = s->reg.ddma_status; break;
    case REG_DDMA_FIFO_STATUS:       val = s->reg.ddma_fifo_status; break;
    case REG_DDMA_XFER_COUNT_LOW:    val = s->reg.ddma_xfer_count_low; break;
    case REG_DDMA_XFER_COUNT_HIGH:   val = s->reg.ddma_xfer_count_high; break;
    case REG_DDMA_ADDR_COUNT_0:      val = s->reg.ddma_addr_count_0; break;
    case REG_DDMA_ADDR_COUNT_1:      val = s->reg.ddma_addr_count_1; break;
    case REG_DDMA_ADDR_COUNT_2:      val = s->reg.ddma_addr_count_2; break;
    case REG_DDMA_ADDR_COUNT_3:      val = s->reg.ddma_addr_count_3; break;
    /* Mailboxes */
    case REG_MAILBOX0:         val = s->reg.mailbox0; break;
    case REG_MAILBOX1:         val = s->reg.mailbox1; break;
    case REG_MAILBOX2:         val = s->reg.mailbox2; break;
    case REG_MAILBOX3:         val = s->reg.mailbox3; break;
    case REG_MAILBOX4:         val = s->reg.mailbox4; break;
    case REG_MAILBOX5:         val = s->reg.mailbox5; break;
    case REG_MAILBOX6:         val = s->reg.mailbox6; break;
    case REG_MAILBOX7:         val = s->reg.mailbox7; break;
    /* Host command */
    case REG_HOST_CMD:         val = s->reg.host_cmd; break;
    /* GPIO */
    case REG_GPIO_DATA:        val = s->reg.gpio_data; break;
    case REG_GPIO_ENABLE:      val = s->reg.gpio_enable; break;
    /* SCSI control pins */
    case REG_SCSI_CONTROL_PINS: val = s->reg.scsiControlPins; break;
    default:
        /* Unused ranges: return 0 */
        val = 0;
        break;
    }
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = (PCIBaseState *)opaque;

    if (size != 2 || (addr & 1)) {
        return;
    }

    switch (addr) {
    case REG_ID_L:            s->reg.id_l = val; break;
    case REG_ID_H:            s->reg.id_h = val; break;
    case REG_CFG0:            s->reg.cfg_0 = val; break;
    case REG_CFG1:            s->reg.cfg_1 = val; break;
    case REG_ICTRL:
        s->reg.ictrl = val;
        pcibase_update_irq(s);
        break;
    case REG_ISTATUS:
        s->reg.istatus = val;
        pcibase_update_irq(s);
        break;
    case REG_SEMAPHORE:
        s->reg.semaphore = val;
        break;
    case REG_NVRAM:
        /* Store written value, but clear read-only bits */
        s->reg.nvram = val & ~NV_DATA_IN;
        break;
    case REG_FLASH_DATA:      s->reg.flash_data = val; break;
    case REG_FLASH_ADDRESS:   s->reg.flash_address = val; break;
    /* CDMA registers */
    case REG_CDMA_CFG:               s->reg.cdma_cfg = val; break;
    case REG_CDMA_CTRL:              s->reg.cdma_ctrl = val; break;
    case REG_CDMA_STATUS:            s->reg.cdma_status = val; break;
    case REG_CDMA_FIFO_STATUS:       s->reg.cdma_fifo_status = val; break;
    case REG_CDMA_COUNT:             s->reg.cdma_count = val; break;
    case REG_CDMA_RESERVED:          s->reg.cdma_reserved = val; break;
    case REG_CDMA_ADDRESS_COUNT_0:   s->reg.cdma_address_count_0 = val; break;
    case REG_CDMA_ADDRESS_COUNT_1:   s->reg.cdma_address_count_1 = val; break;
    case REG_CDMA_ADDRESS_COUNT_2:   s->reg.cdma_address_count_2 = val; break;
    case REG_CDMA_ADDRESS_COUNT_3:   s->reg.cdma_address_count_3 = val; break;
    /* DDMA registers */
    case REG_DDMA_CFG:               s->reg.ddma_cfg = val; break;
    case REG_DDMA_CTRL:              s->reg.ddma_ctrl = val; break;
    case REG_DDMA_STATUS:            s->reg.ddma_status = val; break;
    case REG_DDMA_FIFO_STATUS:       s->reg.ddma_fifo_status = val; break;
    case REG_DDMA_XFER_COUNT_LOW:    s->reg.ddma_xfer_count_low = val; break;
    case REG_DDMA_XFER_COUNT_HIGH:   s->reg.ddma_xfer_count_high = val; break;
    case REG_DDMA_ADDR_COUNT_0:      s->reg.ddma_addr_count_0 = val; break;
    case REG_DDMA_ADDR_COUNT_1:      s->reg.ddma_addr_count_1 = val; break;
    case REG_DDMA_ADDR_COUNT_2:      s->reg.ddma_addr_count_2 = val; break;
    case REG_DDMA_ADDR_COUNT_3:      s->reg.ddma_addr_count_3 = val; break;
    /* Mailboxes */
    case REG_MAILBOX0:         s->reg.mailbox0 = val; break;
    case REG_MAILBOX1:         s->reg.mailbox1 = val; break;
    case REG_MAILBOX2:         s->reg.mailbox2 = val; break;
    case REG_MAILBOX3:         s->reg.mailbox3 = val; break;
    case REG_MAILBOX4:         s->reg.mailbox4 = val; break;
    case REG_MAILBOX5:         s->reg.mailbox5 = val; break;
    case REG_MAILBOX6:         s->reg.mailbox6 = val; break;
    case REG_MAILBOX7:         s->reg.mailbox7 = val; break;
    /* Host command */
    case REG_HOST_CMD:
        s->reg.host_cmd = val;
        if (val & HC_CLR_RISC_INT) {
            s->reg.istatus &= ~RISC_INT;
            pcibase_update_irq(s);
        }
        if (val & HC_CLR_HOST_INT) {
            s->reg.istatus &= ~HOST_INT;
            pcibase_update_irq(s);
        }
        if (val & HC_SET_HOST_INT) {
            /* Process the mailbox command */
            pcibase_process_mailbox(s);
        }
        if (val & HC_RESET_RISC) {
            /* Soft reset: set product IDs, clear mailbox0 */
            s->reg.mailbox0 = 0;
            s->reg.mailbox1 = PROD_ID_1;
            s->reg.mailbox2 = PROD_ID_2;
            s->reg.mailbox3 = PROD_ID_3;
            s->reg.mailbox4 = PROD_ID_4;
            s->reg.ictrl = 0;
            s->reg.istatus = 0;
            pcibase_update_irq(s);
        }
        break;
    /* GPIO */
    case REG_GPIO_DATA:        s->reg.gpio_data = val; break;
    case REG_GPIO_ENABLE:      s->reg.gpio_enable = val; break;
    /* SCSI control pins */
    case REG_SCSI_CONTROL_PINS: s->reg.scsiControlPins = val; break;
    default:
        /* Unused ranges: ignore writes */
        break;
    }
}

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Set power-on defaults */
    memset(&s->reg, 0, sizeof(s->reg));
    s->reg.mailbox0 = 0;
    s->reg.mailbox1 = PROD_ID_1;
    s->reg.mailbox2 = PROD_ID_2;
    s->reg.mailbox3 = PROD_ID_3;
    s->reg.mailbox4 = PROD_ID_4;
    s->reg.cfg_0 = ISP_CFG0_1020; /* Non-1040 revision */
    s->firmware_loaded = false;
    memset(s->risc_ram, 0, sizeof(s->risc_ram));
    s->request_dma = 0;
    s->response_dma = 0;
    s->request_ring_cnt = 0;
    s->response_ring_cnt = 0;
    pcibase_update_irq(s);
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
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY | bi->bar_flags, mr);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_QLOGIC);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_QLOGIC_ISP12160);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_STORAGE_SCSI);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* BAR Initialization */
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_PIO, .size = 0x100, .name = "qla1280-pio", .bar_flags = 0 };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_MMIO, .size = 0x1000, .name = "qla1280-mmio", .bar_flags = PCI_BASE_ADDRESS_MEM_TYPE_64 };
    s->num_bars = 2;

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* DMA mask is set by driver via pci_set_dma_mask; no device model action needed */
    /* Field Initialization */
    pcibase_reset(DEVICE(s));
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
    /* No additional cleanup */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "qla1280_pci",
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
