/*
 * QEMU PCI device model for 3c59x-like NIC sufficient for Linux 3c59x driver probe
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

#define TYPE_PCIBASE_DEVICE "3c59x_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCIBASE_VENDOR_ID 0x10B7
#define PCIBASE_DEVICE_ID 0x5900
#define PCIBASE_CLASS_ID  0x0200 /* PCI_CLASS_NETWORK_ETHERNET */

/* Basic windowed register offsets emulated in IO space */
#define EL3_CMD_REG      0x0E
#define EL3_STATUS_REG   0x0E

#define INTR_STATUS_REG  0x0E

/* Some IO-mapped registers used by driver */
#define REG_TX_FIFO        0x10
#define REG_RX_FIFO        0x10
#define REG_RX_ERRORS      0x14
#define REG_RX_STATUS      0x18
#define REG_TIMER          0x1A
#define REG_TX_STATUS      0x1B
#define REG_TX_FREE        0x1C

#define REG_PKT_STATUS     0x20
#define REG_DOWN_LIST_PTR  0x24
#define REG_FRAG_ADDR      0x28
#define REG_FRAG_LEN       0x2C
#define REG_TX_FREE_THRESH 0x2F
#define REG_UP_PKT_STATUS  0x30
#define REG_UP_LIST_PTR    0x38

/* vortex_cmd values (command encodings) */
enum vortex_cmd {
    VORTEX_TOTAL_RESET   = 0 << 11,
    VORTEX_SELECT_WINDOW = 1 << 11,
    VORTEX_START_COAX    = 2 << 11,
    VORTEX_RX_DISABLE    = 3 << 11,
    VORTEX_RX_ENABLE     = 4 << 11,
    VORTEX_RX_RESET      = 5 << 11,
    VORTEX_UP_STALL      = 6 << 11,
    VORTEX_UP_UNSTALL    = (6 << 11) + 1,
    VORTEX_DOWN_STALL    = (6 << 11) + 2,
    VORTEX_DOWN_UNSTALL  = (6 << 11) + 3,
    VORTEX_RX_DISCARD    = 8 << 11,
    VORTEX_TX_ENABLE     = 9 << 11,
    VORTEX_TX_DISABLE    = 10 << 11,
    VORTEX_TX_RESET      = 11 << 11,
    VORTEX_FAKE_INTR     = 12 << 11,
    VORTEX_ACK_INTR      = 13 << 11,
    VORTEX_SET_INTR_ENB  = 14 << 11,
    VORTEX_SET_STATUS_ENB= 15 << 11,
    VORTEX_SET_RX_FILTER = 16 << 11,
    VORTEX_SET_RX_THRESH = 17 << 11,
    VORTEX_SET_TX_THRESH = 18 << 11,
    VORTEX_SET_TX_START  = 19 << 11,
    VORTEX_START_DMA_UP  = 20 << 11,
    VORTEX_START_DMA_DOWN= (20 << 11) + 1,
    VORTEX_STATS_ENABLE  = 21 << 11,
    VORTEX_STATS_DISABLE = 22 << 11,
    VORTEX_STOP_COAX     = 23 << 11,
    VORTEX_SET_FILTER_BIT= 25 << 11,
};

/* vortex_status interrupt/status bits (EL3_STATUS) */
enum vortex_status_bits {
    VORTEX_STATUS_INT_LATCH     = 0x0001,
    VORTEX_STATUS_HOST_ERROR    = 0x0002,
    VORTEX_STATUS_TX_COMPLETE   = 0x0004,
    VORTEX_STATUS_TX_AVAILABLE  = 0x0008,
    VORTEX_STATUS_RX_COMPLETE   = 0x0010,
    VORTEX_STATUS_RX_EARLY      = 0x0020,
    VORTEX_STATUS_INT_REQ       = 0x0040,
    VORTEX_STATUS_STATS_FULL    = 0x0080,
    VORTEX_STATUS_DMA_DONE      = 1 << 8,
    VORTEX_STATUS_DOWN_COMPLETE = 1 << 9,
    VORTEX_STATUS_UP_COMPLETE   = 1 << 10,
    VORTEX_STATUS_DMA_IN_PROGRESS = 1 << 11,
    VORTEX_STATUS_CMD_IN_PROGRESS = 1 << 12,
};

/* Basic Ack/Enable command low bits as used in driver */
#define CMD_INTLATCH      0x0001
#define CMD_HOST_ERROR    0x0002
#define CMD_TX_COMPLETE   0x0004
#define CMD_TX_AVAILABLE  0x0008
#define CMD_RX_COMPLETE   0x0010
#define CMD_RX_EARLY      0x0020
#define CMD_INTREQ        0x0040
#define CMD_STATS_FULL    0x0080
#define CMD_DMA_DONE      0x0100
#define CMD_DOWN_COMPLETE 0x0200
#define CMD_UP_COMPLETE   0x0400

#define CMD_SET_STATUS_ENB (VORTEX_SET_STATUS_ENB)
#define CMD_SET_INTR_ENB   (VORTEX_SET_INTR_ENB)
#define CMD_ACK_INTR       (VORTEX_ACK_INTR)
#define CMD_FAKE_INTR      (VORTEX_FAKE_INTR)

/* Bit for Command-In-Progress reported in status */
#define CMD_IN_PROGRESS_BIT VORTEX_STATUS_CMD_IN_PROGRESS

/* Simple RX/TX enable bits from commands */
#define CMD_RX_ENABLE   VORTEX_RX_ENABLE
#define CMD_RX_DISABLE  VORTEX_RX_DISABLE
#define CMD_TX_ENABLE   VORTEX_TX_ENABLE
#define CMD_TX_DISABLE  VORTEX_TX_DISABLE

/* Minimal media/link bits (used only for timer / debug) */
#define MEDIA_LNK_BEAT  0x0800

/* Timer register artificial wrap */
#define TIMER_MAX 0xFF


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

    /* Windowing state */
    uint8_t current_window; /* 0-7 */

    /* Core status/control shadow registers */
    uint16_t el3_cmd;       /* last written command value */
    uint16_t el3_status;    /* status/interrupt register */

    uint16_t status_enable_mask; /* from SetStatusEnb commands */
    uint16_t intr_enable_mask;   /* from SetIntrEnb commands */

    bool rx_enabled;
    bool tx_enabled;

    /* TX related simple shadows */
    uint8_t tx_status;
    uint16_t tx_free;
    uint8_t tx_free_threshold;

    /* Minimal statistics counters (only what driver reads early) */
    uint8_t stats_win6[16]; /* bytes as accessed via window 6 */
    uint16_t stats_rx_bytes_low;
    uint16_t stats_tx_bytes_low;

    /* Window 2 station MAC address (6 bytes) */
    uint8_t mac_addr[6];

    /* Window 3 config/maxpkt/mac_ctrl/options */
    uint32_t win3_config;
    uint16_t win3_max_pkt_size;
    uint16_t win3_mac_ctrl;
    uint16_t win3_options;

    /* Window 4 diag/media/physmgmt */
    uint16_t win4_fifo_diag;
    uint16_t win4_net_diag;
    uint32_t win4_physical_mgmt;
    uint16_t win4_media;

    /* Window 7 master/vlan registers used by driver */
    uint32_t win7_master_addr;
    uint16_t win7_vlan_etype;
    uint16_t win7_master_len;
    uint16_t win7_master_status;

    /* Bus master rings / pkt status shadows */
    uint32_t pkt_status;
    uint32_t down_list_ptr;
    uint32_t up_list_ptr;

    /* Simple emulated timer */
    uint8_t timer_val;

    /* IRQ line state */
    bool irq_asserted;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* Determine if an interrupt should be asserted based on status + masks. */
    uint16_t active = s->el3_status & s->intr_enable_mask;

    bool want_irq = (active & VORTEX_STATUS_INT_REQ) != 0;

    if (want_irq && !s->irq_asserted) {
        s->irq_asserted = true;
        pci_set_irq(pdev, 1);
    } else if (!want_irq && s->irq_asserted) {
        s->irq_asserted = false;
        pci_set_irq(pdev, 0);
    }
}

/* Helper: raise specific bits in status and latch interrupt */
static void pcibase_raise_status(PCIBaseState *s, uint16_t bits)
{
    s->el3_status |= bits;
    if (bits & ~VORTEX_STATUS_INT_LATCH) {
        s->el3_status |= VORTEX_STATUS_INT_LATCH | VORTEX_STATUS_INT_REQ;
    }
    pcibase_update_irq(s);
}

/* Helper: clear bits from status (w1c masked by command) */
static void pcibase_clear_status(PCIBaseState *s, uint16_t bits)
{
    s->el3_status &= ~bits;
    pcibase_update_irq(s);
}

/* Device-initiated DMA logic based on driver access patterns
 * For Phase 2, we only minimally acknowledge DMA commands and set DMADone.
 */
static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    (void)is_write;

    /* The driver uses DMA for tx and rx if bus_master/full_bus_master_*.
     * We do not emulate real DMA transfers yet; we just signal completion.
     */

    /* Mark DMA done and clear in-progress. */
    s->el3_status &= ~VORTEX_STATUS_DMA_IN_PROGRESS;
    pcibase_raise_status(s, VORTEX_STATUS_DMA_DONE);
}

static uint16_t pcibase_windowed_read8(PCIBaseState *s, int win, uint8_t off)
{
    uint8_t val8 = 0;

    switch (win) {
    case 2:
        if (off < 6) {
            val8 = s->mac_addr[off];
        }
        break;
    case 3:
        if (off == 0) {
            val8 = s->win3_config & 0xff;
        }
        break;
    case 4:
        if (off == 12) {
            /* BadSSD counter, we just keep 0 */
            val8 = 0;
        } else if (off == 13) {
            /* upper bits used to extend rx/tx bytes; keep 0 */
            val8 = 0;
        }
        break;
    case 6:
        if (off < sizeof(s->stats_win6)) {
            val8 = s->stats_win6[off];
        }
        break;
    default:
        break;
    }

    return val8;
}

static uint16_t pcibase_windowed_read16(PCIBaseState *s, int win, uint8_t off)
{
    uint16_t val = 0;

    switch (win) {
    case 0:
        if (off == 0x0A) {
            /* EEPROM command status; bit15 clear means ready */
            val = 0x0000;
        } else if (off == 0x0C) {
            /* EEPROM data; driver reads 0x40 words for MAC/config. We provide
             * a deterministic but simple contents: valid checksum + fake MAC.
             */
            /* We synthesize enough for the probe eeprom usage: indexes 0-0x17. */
            /* Layout approximated: eeprom[i] values */
            static const uint16_t eeprom_stub[0x40] = {
                /* 0-2 used in product info */
                0x0000, 0x0000, 0x0000,
                0x0000, /*4*/ 0x0000,
                0x0000, /*6*/ 0x0000,
            };
            (void)eeprom_stub; /* silence warning; we compute directly instead */
            /* For simplicity, always return 0 except MAC and checksum. */
            /* We'll return MAC at i+10 indices when called; but we lack index here.
             * The driver sets command base+i and then polls this register, so when
             * it reaches i 10..12, it expects MAC words. We just always return
             * 00:11:22:33:44:55 split as BE16.
             */
            /* Fake deterministic data depending on last command value low bits */
            uint16_t cmd = s->el3_cmd;
            uint8_t idx = cmd & 0x3f;
            if (idx >= 10 && idx <= 12) {
                static const uint8_t mac[6] = {0x00,0x11,0x22,0x33,0x44,0x55};
                int m = idx - 10;
                val = (mac[2*m] << 8) | mac[2*m+1];
            } else if (idx == 0x13) {
                /* eeprom[0x13] participates in checksum path; choose 0 */
                val = 0x0000;
            } else if (idx == 0x15) {
                val = 0x0000;
            } else if (idx == 0x10) {
                val = 0x0000;
            } else {
                val = 0x0000;
            }
        }
        break;
    case 2:
        if (off < 12 && (off & 1) == 0) {
            int idx = off;
            if (idx < 6) {
                val = (s->mac_addr[idx] & 0xff) | ((uint16_t)s->mac_addr[idx+1] << 8);
            } else {
                val = 0x0000;
            }
        }
        break;
    case 3:
        if (off == 0) {
            val = s->win3_config & 0xffff;
        } else if (off == 4) {
            val = (s->win3_config >> 16) & 0xffff;
        } else if (off == 6) {
            val = s->win3_mac_ctrl;
        } else if (off == 8) {
            val = s->win3_options;
        } else if (off == 4 /*Wn3_MaxPktSize*/ ) {
            /* not used here */
            ;
        }
        break;
    case 4:
        if (off == 4) {
            val = s->win4_fifo_diag;
        } else if (off == 6) {
            val = s->win4_net_diag;
        } else if (off == 8 || off == 0x0C) {
            /* physical mgmt / WOL bits; keep 0 */
            val = 0x0000;
        } else if (off == 10) {
            val = s->win4_media;
        } else if (off == 12) {
            /* BadSSD counter */
            val = 0;
        }
        break;
    case 6:
        if (off == 10) {
            val = s->stats_rx_bytes_low;
        } else if (off == 12) {
            val = s->stats_tx_bytes_low;
        } else if (off < sizeof(s->stats_win6)) {
            /* combine 2 bytes */
            uint8_t lo = s->stats_win6[off];
            uint8_t hi = 0;
            if (off+1 < sizeof(s->stats_win6)) {
                hi = s->stats_win6[off+1];
            }
            val = lo | ((uint16_t)hi << 8);
        }
        break;
    case 7:
        if (off == 0) {
            val = (uint16_t)(s->win7_master_addr & 0xffff);
        } else if (off == 2) {
            val = (uint16_t)((s->win7_master_addr >> 16) & 0xffff);
        } else if (off == 4) {
            val = s->win7_vlan_etype;
        } else if (off == 6) {
            val = s->win7_master_len;
        } else if (off == 12) {
            val = s->win7_master_status;
        }
        break;
    default:
        break;
    }

    return val;
}

static uint32_t pcibase_windowed_read32(PCIBaseState *s, int win, uint8_t off)
{
    uint32_t val = 0;

    switch (win) {
    case 3:
        if (off == 0) {
            val = s->win3_config;
        }
        break;
    case 4:
        if (off == 8) {
            val = s->win4_physical_mgmt;
        }
        break;
    case 7:
        if (off == 0) {
            val = s->win7_master_addr;
        }
        break;
    default:
        break;
    }

    return val;
}

static void pcibase_windowed_write8(PCIBaseState *s, int win, uint8_t off, uint8_t val)
{
    switch (win) {
    case 2:
        if (off < 6) {
            s->mac_addr[off] = val;
        }
        break;
    case 4:
        if (off == 12) {
            /* BadSSD counter write ignored */
        }
        break;
    case 6:
        if (off < sizeof(s->stats_win6)) {
            s->stats_win6[off] = val;
        }
        break;
    default:
        break;
    }
}

static void pcibase_windowed_write16(PCIBaseState *s, int win, uint8_t off, uint16_t val)
{
    switch (win) {
    case 0:
        if (off == 0x0A) {
            /* EEPROM command register; lower bits are address commands. */
            s->el3_cmd = val;
        } else if (off == 0x0C) {
            /* EEPROM data; ignored */
        }
        break;
    case 2:
        if (off < 12 && (off & 1) == 0) {
            int idx = off;
            if (idx < 6) {
                s->mac_addr[idx] = val & 0xff;
                s->mac_addr[idx+1] = (val >> 8) & 0xff;
            }
        }
        break;
    case 3:
        if (off == 0) {
            s->win3_config = (s->win3_config & 0xffff0000u) | val;
        } else if (off == 4) {
            s->win3_config = (s->win3_config & 0x0000ffffu) | ((uint32_t)val << 16);
        } else if (off == 6) {
            s->win3_mac_ctrl = val;
        } else if (off == 8) {
            s->win3_options = val;
        } else if (off == 4 /*MaxPktSize alias*/ ) {
            s->win3_max_pkt_size = val;
        }
        break;
    case 4:
        if (off == 4) {
            s->win4_fifo_diag = val;
        } else if (off == 6) {
            s->win4_net_diag = val;
        } else if (off == 8) {
            s->win4_physical_mgmt = (s->win4_physical_mgmt & 0xffff0000u) | val;
        } else if (off == 10) {
            s->win4_media = val;
        } else if (off == 0x0C) {
            /* WOL control or similar; ignore */
        }
        break;
    case 7:
        if (off == 4) {
            s->win7_vlan_etype = val;
        } else if (off == 6) {
            s->win7_master_len = val;
        } else if (off == 12) {
            /* Wn7_MasterStatus w1c when driver acks DMADone */
            s->win7_master_status &= ~val;
        }
        break;
    default:
        break;
    }
}

static void pcibase_windowed_write32(PCIBaseState *s, int win, uint8_t off, uint32_t val)
{
    switch (win) {
    case 3:
        if (off == 0) {
            s->win3_config = val;
        }
        break;
    case 4:
        if (off == 8) {
            s->win4_physical_mgmt = val;
        }
        break;
    case 7:
        if (off == 0) {
            s->win7_master_addr = val;
        }
        break;
    default:
        break;
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* This device only implements PIO BAR in this model; MMIO unused. */
    (void)s;
    (void)addr;
    (void)size;
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    (void)s;
    (void)addr;
    (void)val;
    (void)size;
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Map IO space operations according to addresses used in driver. */
    switch (size) {
    case 1:
        if (addr == REG_TX_STATUS) {
            val = s->tx_status;
        } else if (addr == REG_TIMER) {
            val = s->timer_val;
        } else if (addr == REG_TX_FREE) {
            val = s->tx_free & 0xff;
        } else {
            /* Some windowed 8-bit stats / diag via window_read8() (offset in win) */
            /* The driver uses window_read8(vp, win, off) which goes through
             * a helper using ioread8(ioaddr+off) after selecting window.
             */
            uint8_t off = addr & 0x1F;
            val = pcibase_windowed_read8(s, s->current_window, off);
        }
        break;
    case 2:
        if (addr == EL3_STATUS_REG) {
            val = s->el3_status;
        } else if (addr == REG_RX_STATUS) {
            /* RxStatus: for simplicity, report 0 -> no packets. */
            val = 0;
        } else if (addr == REG_RX_ERRORS) {
            val = 0;
        } else {
            uint8_t off = addr & 0x1E;
            val = pcibase_windowed_read16(s, s->current_window, off);
        }
        break;
    case 4:
        if (addr == REG_PKT_STATUS) {
            val = s->pkt_status;
        } else if (addr == REG_DOWN_LIST_PTR) {
            val = s->down_list_ptr;
        } else if (addr == REG_FRAG_ADDR) {
            /* Not used directly in driver; return 0 */
            val = 0;
        } else if (addr == REG_FRAG_LEN) {
            val = 0;
        } else if (addr == REG_UP_PKT_STATUS) {
            val = 0;
        } else if (addr == REG_UP_LIST_PTR) {
            val = s->up_list_ptr;
        } else {
            uint8_t off = addr & 0x1C;
            val = pcibase_windowed_read32(s, s->current_window, off);
        }
        break;
    default:
        break;
    }

    return val;
}

static void pcibase_handle_command(PCIBaseState *s, uint16_t cmd)
{
    uint16_t op = cmd & 0xF800; /* upper 5 bits */
    uint16_t arg = cmd & 0x07FF; /* low bits */

    s->el3_cmd = cmd;

    switch (op) {
    case VORTEX_TOTAL_RESET:
        /* TotalReset: clear most state but keep basic config */
        s->rx_enabled = false;
        s->tx_enabled = false;
        s->status_enable_mask = 0;
        s->intr_enable_mask = 0;
        s->el3_status = 0;
        s->tx_status = 0;
        s->tx_free = 0x3FFF;
        s->tx_free_threshold = 0;
        s->pkt_status = 0;
        s->down_list_ptr = 0;
        s->up_list_ptr = 0;
        s->win7_master_status = 0;
        s->win7_master_len = 0;
        s->win7_master_addr = 0;
        s->timer_val = 0;
        pcibase_update_irq(s);
        break;
    case VORTEX_SELECT_WINDOW:
        s->current_window = arg & 0x07;
        break;
    case VORTEX_RX_DISABLE:
        s->rx_enabled = false;
        break;
    case VORTEX_RX_ENABLE:
        s->rx_enabled = true;
        break;
    case VORTEX_RX_RESET:
        /* Clear RX-related error bits */
        s->el3_status &= ~VORTEX_STATUS_RX_COMPLETE;
        break;
    case VORTEX_TX_DISABLE:
        s->tx_enabled = false;
        break;
    case VORTEX_TX_ENABLE:
        s->tx_enabled = true;
        break;
    case VORTEX_TX_RESET:
        s->tx_status = 0;
        s->el3_status &= ~(VORTEX_STATUS_TX_COMPLETE | VORTEX_STATUS_TX_AVAILABLE);
        break;
    case VORTEX_FAKE_INTR:
        /* Driver uses this to schedule deferred work; we just raise IntReq */
        pcibase_raise_status(s, VORTEX_STATUS_INT_REQ);
        break;
    case VORTEX_ACK_INTR:
    {
        /* AckIntr | bits: low 11 bits of cmd determine which status bits to clear. */
        uint16_t mask = arg & 0x07FF;
        uint16_t bits = 0;
        if (mask & CMD_INTLATCH)      bits |= VORTEX_STATUS_INT_LATCH;
        if (mask & CMD_HOST_ERROR)    bits |= VORTEX_STATUS_HOST_ERROR;
        if (mask & CMD_TX_COMPLETE)   bits |= VORTEX_STATUS_TX_COMPLETE;
        if (mask & CMD_TX_AVAILABLE)  bits |= VORTEX_STATUS_TX_AVAILABLE;
        if (mask & CMD_RX_COMPLETE)   bits |= VORTEX_STATUS_RX_COMPLETE;
        if (mask & CMD_RX_EARLY)      bits |= VORTEX_STATUS_RX_EARLY;
        if (mask & CMD_INTREQ)        bits |= VORTEX_STATUS_INT_REQ;
        if (mask & CMD_STATS_FULL)    bits |= VORTEX_STATUS_STATS_FULL;
        if (mask & CMD_DMA_DONE)      bits |= VORTEX_STATUS_DMA_DONE;
        if (mask & CMD_DOWN_COMPLETE) bits |= VORTEX_STATUS_DOWN_COMPLETE;
        if (mask & CMD_UP_COMPLETE)   bits |= VORTEX_STATUS_UP_COMPLETE;
        pcibase_clear_status(s, bits);
        break;
    }
    case VORTEX_SET_INTR_ENB:
    {
        /* SetIntrEnb | mask: driver writes full value including opcode */
        uint16_t mask = arg & 0x07FF;
        s->intr_enable_mask = mask;
        pcibase_update_irq(s);
        break;
    }
    case VORTEX_SET_STATUS_ENB:
    {
        uint16_t mask = arg & 0x07FF;
        s->status_enable_mask = mask;
        break;
    }
    case VORTEX_SET_RX_THRESH:
        /* Not modeled; ignore */
        break;
    case VORTEX_SET_TX_THRESH:
        /* lower bits define threshold; driver writes via SetTxThreshold */
        s->tx_free_threshold = arg & 0xff;
        break;
    case VORTEX_START_DMA_UP:
        /* StartDMAUp; mark DMA in progress then immediately complete */
        s->el3_status |= VORTEX_STATUS_DMA_IN_PROGRESS;
        pcibase_do_dma(s, false);
        break;
    case VORTEX_START_DMA_DOWN:
        s->el3_status |= VORTEX_STATUS_DMA_IN_PROGRESS;
        pcibase_do_dma(s, true);
        break;
    case VORTEX_STATS_ENABLE:
        /* stats counting already implicit; ignore */
        break;
    case VORTEX_STATS_DISABLE:
        break;
    default:
        break;
    }
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (size) {
    case 1:
        if (addr == REG_TX_STATUS) {
            /* TxStatus is a stack; driver writes 0 to pop/clear. */
            s->tx_status = (uint8_t)val;
        } else if (addr == REG_TX_FREE_THRESH) {
            s->tx_free_threshold = (uint8_t)val;
        } else {
            uint8_t off = addr & 0x1F;
            pcibase_windowed_write8(s, s->current_window, off, (uint8_t)val);
        }
        break;
    case 2:
        if (addr == EL3_CMD_REG) {
            pcibase_handle_command(s, (uint16_t)val);
        } else if (addr == REG_RX_STATUS) {
            /* RxStatus w1c; ignore */
        } else if (addr == REG_RX_ERRORS) {
            /* ignore */
        } else if (addr == REG_TIMER) {
            s->timer_val = (uint8_t)val;
        } else {
            uint8_t off = addr & 0x1E;
            pcibase_windowed_write16(s, s->current_window, off, (uint16_t)val);
        }
        break;
    case 4:
        if (addr == REG_TX_FIFO) {
            /* driver writes packet length first; ignore */
        } else if (addr == REG_PKT_STATUS) {
            s->pkt_status = (uint32_t)val;
        } else if (addr == REG_DOWN_LIST_PTR) {
            s->down_list_ptr = (uint32_t)val;
            if (val != 0) {
                /* When driver programs DownListPtr then enables DMA, it expects
                 * DownComplete interrupts upon tx completion. We skip real DMA.
                 */
            }
        } else if (addr == REG_UP_LIST_PTR) {
            s->up_list_ptr = (uint32_t)val;
        } else if (addr == REG_FRAG_ADDR) {
            /* not modeled */
        } else if (addr == REG_FRAG_LEN) {
            /* not modeled */
        } else {
            uint8_t off = addr & 0x1C;
            pcibase_windowed_write32(s, s->current_window, off, (uint32_t)val);
        }
        break;
    default:
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

    /* Initialize device state to power-on defaults sufficient for probe. */
    s->current_window = 0;
    s->el3_cmd = 0;
    s->el3_status = 0;
    s->status_enable_mask = 0;
    s->intr_enable_mask = 0;
    s->rx_enabled = false;
    s->tx_enabled = false;
    s->tx_status = 0;
    s->tx_free = 0x3FFF; /* lots of room */
    s->tx_free_threshold = 0;

    memset(s->stats_win6, 0, sizeof(s->stats_win6));
    s->stats_rx_bytes_low = 0;
    s->stats_tx_bytes_low = 0;

    /* Default MAC 00:11:22:33:44:55 so eeprom + window2 writes consistent */
    s->mac_addr[0] = 0x00;
    s->mac_addr[1] = 0x11;
    s->mac_addr[2] = 0x22;
    s->mac_addr[3] = 0x33;
    s->mac_addr[4] = 0x44;
    s->mac_addr[5] = 0x55;

    s->win3_config = 0;
    s->win3_max_pkt_size = 1514;
    s->win3_mac_ctrl = 0;
    s->win3_options = 0x0040; /* ensure available_media has bit 0x40 as in driver comment */

    s->win4_fifo_diag = 0;
    s->win4_net_diag = 0;
    s->win4_physical_mgmt = 0;
    s->win4_media = MEDIA_LNK_BEAT; /* report link beat present */

    s->win7_master_addr = 0;
    s->win7_vlan_etype = 0x8100; /* VLAN_ETHER_TYPE */
    s->win7_master_len = 0;
    s->win7_master_status = 0;

    s->pkt_status = 0;
    s->down_list_ptr = 0;
    s->up_list_ptr = 0;

    s->timer_val = 0;

    s->irq_asserted = false;
    pcibase_update_irq(s);
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

    /* BAR Initialization: IO BAR 0, 0x20 bytes as in driver VORTEX_TOTAL_SIZE. */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type  = BAR_TYPE_PIO;
    s->bar_info[0].size  = 0x20;
    s->bar_info[0].name  = "3c59x-io";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type  = BAR_TYPE_NONE;
        s->bar_info[i].size  = 0;
        s->bar_info[i].name  = NULL;
    }

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = false;
    s->has_msix = false;

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
    .name = "3c59x_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT8(current_window, PCIBaseState),
        VMSTATE_UINT16(el3_cmd, PCIBaseState),
        VMSTATE_UINT16(el3_status, PCIBaseState),
        VMSTATE_UINT16(status_enable_mask, PCIBaseState),
        VMSTATE_UINT16(intr_enable_mask, PCIBaseState),
        VMSTATE_BOOL(rx_enabled, PCIBaseState),
        VMSTATE_BOOL(tx_enabled, PCIBaseState),
        VMSTATE_UINT8(tx_status, PCIBaseState),
        VMSTATE_UINT16(tx_free, PCIBaseState),
        VMSTATE_UINT8(tx_free_threshold, PCIBaseState),
        VMSTATE_UINT8_ARRAY(stats_win6, PCIBaseState, 16),
        VMSTATE_UINT16(stats_rx_bytes_low, PCIBaseState),
        VMSTATE_UINT16(stats_tx_bytes_low, PCIBaseState),
        VMSTATE_UINT8_ARRAY(mac_addr, PCIBaseState, 6),
        VMSTATE_UINT32(win3_config, PCIBaseState),
        VMSTATE_UINT16(win3_max_pkt_size, PCIBaseState),
        VMSTATE_UINT16(win3_mac_ctrl, PCIBaseState),
        VMSTATE_UINT16(win3_options, PCIBaseState),
        VMSTATE_UINT16(win4_fifo_diag, PCIBaseState),
        VMSTATE_UINT16(win4_net_diag, PCIBaseState),
        VMSTATE_UINT32(win4_physical_mgmt, PCIBaseState),
        VMSTATE_UINT16(win4_media, PCIBaseState),
        VMSTATE_UINT32(win7_master_addr, PCIBaseState),
        VMSTATE_UINT16(win7_vlan_etype, PCIBaseState),
        VMSTATE_UINT16(win7_master_len, PCIBaseState),
        VMSTATE_UINT16(win7_master_status, PCIBaseState),
        VMSTATE_UINT32(pkt_status, PCIBaseState),
        VMSTATE_UINT32(down_list_ptr, PCIBaseState),
        VMSTATE_UINT32(up_list_ptr, PCIBaseState),
        VMSTATE_UINT8(timer_val, PCIBaseState),
        VMSTATE_BOOL(irq_asserted, PCIBaseState),
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
