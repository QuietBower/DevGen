/*
 * QEMU PCI device model for MOXA Intellio family (moxa.c driver)
 *
 * This model only emulates the minimal register/memory interface
 * required for the Linux driver to probe, load firmware, and
 * initialize the board. It does NOT emulate actual serial I/O.
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
/* linux/pci_ids.h is not available in QEMU build environment; replicate needed IDs */

#define PCI_VENDOR_ID_MOXA       0x1393
#define PCI_DEVICE_ID_MOXA_C218  0x1020

#define TYPE_PCIBASE_DEVICE "moxa_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
/* Vendor/Device/Class IDs and hardware-related register offsets */
#define PCIBASE_VENDOR_ID         PCI_VENDOR_ID_MOXA
#define PCIBASE_DEVICE_ID         PCI_DEVICE_ID_MOXA_C218
#define PCIBASE_CLASS_ID          PCI_CLASS_OTHERS

#define MOXA_MAGIC_CODE           0x404

#define MOXA_C218_CONF_BASE       0x800
#define MOXA_C218_STATUS          (MOXA_C218_CONF_BASE + 0x0)
#define MOXA_C218_DIAG            (MOXA_C218_CONF_BASE + 0x2)
#define MOXA_C218_KEY             (MOXA_C218_CONF_BASE + 0x4)
#define MOXA_C218_DLOAD_LEN       (MOXA_C218_CONF_BASE + 0x6)
#define MOXA_C218_CHECK_SUM       (MOXA_C218_CONF_BASE + 0x8)
#define MOXA_C218_CHKSUM_OK       (MOXA_C218_CONF_BASE + 0x0a)
#define MOXA_C218_TEST_RX         (MOXA_C218_CONF_BASE + 0x10)
#define MOXA_C218_TEST_TX         (MOXA_C218_CONF_BASE + 0x18)
#define MOXA_C218_RXERR           (MOXA_C218_CONF_BASE + 0x20)
#define MOXA_C218_ERRFLAG         (MOXA_C218_CONF_BASE + 0x28)
#define MOXA_C218_LOADBUF         0x0f00
#define MOXA_C218_KEYCODE         0x218

#define MOXA_CP204J_KEYCODE       0x204

#define MOXA_C320_CONF_BASE       0x800
#define MOXA_C320_LOADBUF         0x0f00
#define MOXA_STS_INIT             0x05

#define MOXA_C320_STATUS          (MOXA_C320_CONF_BASE + 0x0)
#define MOXA_C320_DIAG            (MOXA_C320_CONF_BASE + 0x2)
#define MOXA_C320_KEY             (MOXA_C320_CONF_BASE + 0x4)
#define MOXA_C320_DLOAD_LEN       (MOXA_C320_CONF_BASE + 0x6)
#define MOXA_C320_CHECK_SUM       (MOXA_C320_CONF_BASE + 0x8)
#define MOXA_C320_CHKSUM_OK       (MOXA_C320_CONF_BASE + 0x0a)
#define MOXA_C320_BAPI_LEN        (MOXA_C320_CONF_BASE + 0x0c)
#define MOXA_C320_UART_NO         (MOXA_C320_CONF_BASE + 0x0e)
#define MOXA_C320_KEYCODE         0x320

#define MOXA_FIXPAGE_ADDR         0x0000
#define MOXA_DYNPAGE_ADDR         0x2000
#define MOXA_C218_START           0x3000

#define MOXA_CONTROL_REG          0x1ff0
#define MOXA_HW_RESET             0x80

#define MOXA_FC_CARDRESET         0x80
#define MOXA_FC_CHANNELRESET      1
#define MOXA_FC_ENABLECH          2
#define MOXA_FC_DISABLECH         3
#define MOXA_FC_SETPARAM          4
#define MOXA_FC_SETMODE           5
#define MOXA_FC_SETRATE           6
#define MOXA_FC_LINECONTROL       7
#define MOXA_FC_LINESTATUS        8
#define MOXA_FC_XMITCONTROL       9
#define MOXA_FC_FLUSHQUEUE        10
#define MOXA_FC_SENDBREAK         11
#define MOXA_FC_STOPBREAK         12
#define MOXA_FC_LOOPBACKON        13
#define MOXA_FC_LOOPBACKOFF       14
#define MOXA_FC_CLRIRQTABLE       15
#define MOXA_FC_SENDXON           16
#define MOXA_FC_SETTERMIRQ        17
#define MOXA_FC_SETCNTIRQ         18
#define MOXA_FC_SETBREAKIRQ       19
#define MOXA_FC_SETLINEIRQ        20
#define MOXA_FC_SETFLOWCTL        21
#define MOXA_FC_GENIRQ            22
#define MOXA_FC_INCD180           23
#define MOXA_FC_OUTCD180          24
#define MOXA_FC_INUARTREG         23
#define MOXA_FC_OUTUARTREG        24
#define MOXA_FC_SETXONXOFF        25
#define MOXA_FC_OUTCD180CCR       26
#define MOXA_FC_EXTIQUEUE         27
#define MOXA_FC_EXTOQUEUE         28
#define MOXA_FC_CLRLINEIRQ        29
#define MOXA_FC_HWFLOWCTL         30
#define MOXA_FC_GETCLOCKRATE      35
#define MOXA_FC_SETBAUD           36
#define MOXA_FC_SETDATAMODE       41
#define MOXA_FC_GETCCSR           43
#define MOXA_FC_GETDATAERROR      45
#define MOXA_FC_RXCONTROL         50
#define MOXA_FC_IMMSEND           51
#define MOXA_FC_SETXONSTATE       52
#define MOXA_FC_SETXOFFSTATE      53
#define MOXA_FC_SETRXFIFOTRIG     54
#define MOXA_FC_SETTXFIFOCNT      55
#define MOXA_FC_UNIXRATE          56
#define MOXA_FC_UNIXRESETTIMER    57

#define MOXA_DRAM_GLOBAL          0
#define MOXA_INT_DATA             (MOXA_DRAM_GLOBAL + 0)
#define MOXA_CONFIG_BASE          (MOXA_DRAM_GLOBAL + 0x108)

#define MOXA_IRQINDEX             (MOXA_INT_DATA + 0)
#define MOXA_IRQPENDING           (MOXA_INT_DATA + 4)
#define MOXA_IRQTABLE             (MOXA_INT_DATA + 8)

#define MOXA_INTR_RX              0x01
#define MOXA_INTR_TX              0x02
#define MOXA_INTR_FUNC            0x04
#define MOXA_INTR_BREAK           0x08
#define MOXA_INTR_LINE            0x10
#define MOXA_INTR_INTR            0x20
#define MOXA_INTR_QUIT            0x40
#define MOXA_INTR_EOF             0x80
#define MOXA_INTR_RXTRIGGER       0x100
#define MOXA_INTR_TXTRIGGER       0x200

#define MOXA_MAGIC_NO             (MOXA_CONFIG_BASE + 0)
#define MOXA_CARD_MODEL_NO        (MOXA_CONFIG_BASE + 2)
#define MOXA_TOTAL_PORTS          (MOXA_CONFIG_BASE + 4)
#define MOXA_MODULE_CNT           (MOXA_CONFIG_BASE + 8)
#define MOXA_MODULE_NO            (MOXA_CONFIG_BASE + 10)
#define MOXA_TIMER_10MS           (MOXA_CONFIG_BASE + 14)
#define MOXA_DISABLE_IRQ          (MOXA_CONFIG_BASE + 20)
#define MOXA_TMS320_PORT1         (MOXA_CONFIG_BASE + 22)
#define MOXA_TMS320_PORT2         (MOXA_CONFIG_BASE + 24)
#define MOXA_TMS320_CLOCK         (MOXA_CONFIG_BASE + 26)

#define MOXA_EXTERN_TABLE         0x400
#define MOXA_EXTERN_SIZE          0x60

#define MOXA_EX_RXRPTR            0x00
#define MOXA_EX_RXWPTR            0x02
#define MOXA_EX_TXRPTR            0x04
#define MOXA_EX_TXWPTR            0x06
#define MOXA_EX_HOSTSTAT          0x08
#define MOXA_EX_FLAGSTAT          0x0a
#define MOXA_EX_FLOWCONTROL       0x0c
#define MOXA_EX_BREAK_CNT         0x0e
#define MOXA_EX_CD180TXIRQ        0x10
#define MOXA_EX_RX_MASK           0x12
#define MOXA_EX_TX_MASK           0x14
#define MOXA_EX_OFS_RXB           0x16
#define MOXA_EX_OFS_TXB           0x18
#define MOXA_EX_PAGE_RXB          0x1a
#define MOXA_EX_PAGE_TXB          0x1c
#define MOXA_EX_ENDPAGE_RXB       0x1e
#define MOXA_EX_ENDPAGE_TXB       0x20
#define MOXA_EX_DATA_ERROR        0x22
#define MOXA_EX_RXTRIGGER         0x28
#define MOXA_EX_TXTRIGGER         0x2a
#define MOXA_EX_RRXWPTR           0x34
#define MOXA_EX_LOW_WATER         0x36
#define MOXA_EX_FUNCCODE          0x40
#define MOXA_EX_FUNCARG           0x42
#define MOXA_EX_FUNCARG1          0x44

#define MOXA_C218RX_SIZE          0x2000
#define MOXA_C218TX_SIZE          0x8000
#define MOXA_C218RX_MASK          (MOXA_C218RX_SIZE - 1)
#define MOXA_C218TX_MASK          (MOXA_C218TX_SIZE - 1)

#define MOXA_C320P8RX_SIZE        0x2000
#define MOXA_C320P8TX_SIZE        0x8000
#define MOXA_C320P8RX_MASK        (MOXA_C320P8RX_SIZE - 1)
#define MOXA_C320P8TX_MASK        (MOXA_C320P8TX_SIZE - 1)

#define MOXA_C320P16RX_SIZE       0x2000
#define MOXA_C320P16TX_SIZE       0x4000
#define MOXA_C320P16RX_MASK       (MOXA_C320P16RX_SIZE - 1)
#define MOXA_C320P16TX_MASK       (MOXA_C320P16TX_SIZE - 1)

#define MOXA_C320P24RX_SIZE       0x2000
#define MOXA_C320P24TX_SIZE       0x2000
#define MOXA_C320P24RX_MASK       (MOXA_C320P24RX_SIZE - 1)
#define MOXA_C320P24TX_MASK       (MOXA_C320P24TX_SIZE - 1)

#define MOXA_C320P32RX_SIZE       0x1000
#define MOXA_C320P32TX_SIZE       0x1000
#define MOXA_C320P32RX_MASK       (MOXA_C320P32RX_SIZE - 1)
#define MOXA_C320P32TX_MASK       (MOXA_C320P32TX_SIZE - 1)

#define MOXA_PAGE_SIZE            0x2000U
#define MOXA_PAGE_MASK            (MOXA_PAGE_SIZE - 1)

#define MOXA_C218RX_SPAGE         3
#define MOXA_C218TX_SPAGE         4
#define MOXA_C218RX_PAGENO        1
#define MOXA_C218TX_PAGENO        4
#define MOXA_C218BUF_PAGENO       5

#define MOXA_C320P8RX_SPAGE       3
#define MOXA_C320P8TX_SPAGE       4
#define MOXA_C320P8RX_PGNO        1
#define MOXA_C320P8TX_PGNO        4
#define MOXA_C320P8BUF_PGNO       5

#define MOXA_C320P16RX_SPAGE      3
#define MOXA_C320P16TX_SPAGE      4
#define MOXA_C320P16RX_PGNO       1
#define MOXA_C320P16TX_PGNO       2
#define MOXA_C320P16BUF_PGNO      3

#define MOXA_C320P24RX_SPAGE      3
#define MOXA_C320P24TX_SPAGE      4
#define MOXA_C320P24RX_PGNO       1
#define MOXA_C320P24TX_PGNO       1
#define MOXA_C320P24BUF_PGNO      2

#define MOXA_C320P32RX_SPAGE      3
#define MOXA_C320P32TX_OFS        MOXA_C320P32TX_SIZE
#define MOXA_C320P32TX_SPAGE      3
#define MOXA_C320P32BUF_PGNO      1

#define MOXA_WAKEUPRX             0x01
#define MOXA_WAKEUPTX             0x02
#define MOXA_WAKEUPBREAK          0x08
#define MOXA_WAKEUPLINE           0x10
#define MOXA_WAKEUPINTR           0x20
#define MOXA_WAKEUPQUIT           0x40
#define MOXA_WAKEUPEOF            0x80
#define MOXA_WAKEUPRXTRIGGER      0x100
#define MOXA_WAKEUPTXTRIGGER      0x200

#define MOXA_RX_OVER              0x01
#define MOXA_XOFF_STATE           0x02
#define MOXA_TX_FLOWOFF           0x04
#define MOXA_TX_ENABLE            0x08
#define MOXA_CTS_STATE            0x10
#define MOXA_DSR_STATE            0x20
#define MOXA_DCD_STATE            0x80

#define MOXA_CTS_FLOWCTL          1
#define MOXA_RTS_FLOWCTL          2
#define MOXA_TX_FLOWCTL           4
#define MOXA_RX_FLOWCTL           8

#define MOXA_IXM_IXANY            0x10
#define MOXA_LOWWATER             128

#define MOXA_DTR_ON               1
#define MOXA_RTS_ON               2
#define MOXA_CTS_ON               1
#define MOXA_DSR_ON               2
#define MOXA_DCD_ON               8

#define MOXA_MX_CS8               0x03
#define MOXA_MX_CS7               0x02
#define MOXA_MX_CS6               0x01
#define MOXA_MX_CS5               0x00

#define MOXA_MX_STOP1             0x00
#define MOXA_MX_STOP15            0x04
#define MOXA_MX_STOP2             0x08

#define MOXA_MX_PARNONE           0x00
#define MOXA_MX_PAREVEN           0x40
#define MOXA_MX_PARODD            0xc0
#define MOXA_MX_PARMARK           0xa0
#define MOXA_MX_PARSPACE          0x20

#define MOXA_FW_HDRLEN            32

#define MOXA_MAX_BOARDS           4
#define MOXA_MAX_PORTS_PER_BOARD  32

#define MOXA_TXSTOPPED            1
#define MOXA_LOWWAIT              2
#define MOXA_EMPTYWAIT            3

#define MOXA_CONSTANT             0x400

/* offsets for Extern table per port relative to MOXA_EXTERN_TABLE */
#define MOXA_FLAGSTAT_DCD_BIT     MOXA_DCD_STATE

/* Extern table function-related offsets in driver naming */
#define FUNC_CODE_OFFSET          MOXA_EX_FUNCCODE
#define FUNC_ARG_OFFSET           MOXA_EX_FUNCARG
#define FUNC_ARG1_OFFSET          MOXA_EX_FUNCARG1

#define FLAGSTAT_OFFSET           MOXA_EX_FLAGSTAT
#define HOSTSTAT_OFFSET           MOXA_EX_HOSTSTAT
#define RX_MASK_OFFSET            MOXA_EX_RX_MASK
#define TX_MASK_OFFSET            MOXA_EX_TX_MASK
#define PAGE_RXB_OFFSET           MOXA_EX_PAGE_RXB
#define END_PAGE_RXB_OFFSET       MOXA_EX_ENDPAGE_RXB
#define PAGE_TXB_OFFSET           MOXA_EX_PAGE_TXB
#define END_PAGE_TXB_OFFSET       MOXA_EX_ENDPAGE_TXB
#define RX_RPTR_OFFSET            MOXA_EX_RXRPTR
#define RX_WPTR_OFFSET            MOXA_EX_RXWPTR
#define TX_RPTR_OFFSET            MOXA_EX_TXRPTR
#define TX_WPTR_OFFSET            MOXA_EX_TXWPTR
#define OFS_RXB_OFFSET            MOXA_EX_OFS_RXB
#define OFS_TXB_OFFSET            MOXA_EX_OFS_TXB
#define LOW_WATER_OFFSET          MOXA_EX_LOW_WATER
#define CD180TXIRQ_OFFSET         MOXA_EX_CD180TXIRQ

#define INT_INDEX_OFFSET          MOXA_IRQINDEX
#define INT_PENDING_OFFSET        MOXA_IRQPENDING
#define INT_TABLE_OFFSET          MOXA_IRQTABLE

#define MAGIC_NO_OFFSET           MOXA_MAGIC_NO
#define MODULE_CNT_OFFSET         MOXA_MODULE_CNT
#define MODULE_NO_OFFSET          MOXA_MODULE_NO
#define DISABLE_IRQ_OFFSET        MOXA_DISABLE_IRQ
#define TMS320_PORT1_OFFSET       MOXA_TMS320_PORT1
#define TMS320_PORT2_OFFSET       MOXA_TMS320_PORT2
#define TMS320_CLOCK_OFFSET       MOXA_TMS320_CLOCK

#define CONTROL_REG_OFFSET        MOXA_CONTROL_REG

/* bits in HostStat, wakeups */
#define WAKEUP_TX_BIT             MOXA_WAKEUPTX
#define WAKEUP_BREAK_BIT          MOXA_WAKEUPBREAK

/* bits in FlagStat */
#define XOFF_STATE_BIT            MOXA_XOFF_STATE
#define DCD_STATE_BIT             MOXA_DCD_STATE

/* simple helpers */
#define MOXA_DYN_PAGE_ADDR        MOXA_DYNPAGE_ADDR

#define MOXA_MAX_PORTS_DEVICE     32

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

    /* Hardware Register Shadows (The 'Identity' of the device) */
    /* Minimal register shadowing for key configuration/status registers */
    uint32_t magic_code;
    uint16_t c218_status;
    uint16_t c218_diag;
    uint16_t c218_key;
    uint16_t c218_dload_len;
    uint16_t c218_check_sum;
    uint16_t c218_chksum_ok;
    uint16_t c320_status;
    uint16_t c320_diag;
    uint16_t c320_key;
    uint16_t c320_dload_len;
    uint16_t c320_check_sum;
    uint16_t c320_chksum_ok;
    uint16_t control_reg;

    /* Simple DRAM emulation backing the 0x4000-byte region driver maps. */
    uint8_t *dram;
    size_t dram_size;

    /* For firmware loading protocol: track current board type/keycode. */
    uint16_t expected_keycode;
    bool is_320_board;
    uint16_t num_ports;

    /* Track functional command completion: per-port func code/args. */
    /* Nothing persistent beyond what is stored in dram, but we may
     * need helper state later; keep placeholder fields. */
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* The real hardware uses shared memory IRQ tables and a PCI INT.
     * The driver polls via timer and also uses the IRQ table if
     * MOXA_INTR_* bits indicate work. For now, we do not assert the
     * legacy PCI INT line and rely on polling. */
    (void)pdev;
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* The driver accesses board memory via ioremap and memcpy_toio/
     * memcpy_fromio, not via DMA APIs, so no bus-master DMA is used. */
    (void)pdev;
    (void)is_write;
}

static inline uint8_t pcibase_readb(PCIBaseState *s, hwaddr addr)
{
    if (addr >= s->dram_size) {
        return 0xff;
    }
    return s->dram[addr];
}

static inline void pcibase_writeb(PCIBaseState *s, hwaddr addr, uint8_t val)
{
    if (addr >= s->dram_size) {
        return;
    }
    s->dram[addr] = val;
}

static inline uint16_t pcibase_readw(PCIBaseState *s, hwaddr addr)
{
    uint16_t v = 0xffff;
    if (addr + 1 >= s->dram_size) {
        return v;
    }
    v = s->dram[addr] | ((uint16_t)s->dram[addr + 1] << 8);
    return v;
}

static inline void pcibase_writew(PCIBaseState *s, hwaddr addr, uint16_t val)
{
    if (addr + 1 >= s->dram_size) {
        return;
    }
    s->dram[addr] = val & 0xff;
    s->dram[addr + 1] = (val >> 8) & 0xff;
}

G_GNUC_UNUSED static inline void pcibase_memcpy_from_guest(PCIBaseState *s, hwaddr addr,
                                             const uint8_t *src, size_t len)
{
    if (addr >= s->dram_size) {
        return;
    }
    if (addr + len > s->dram_size) {
        len = s->dram_size - addr;
    }
    memcpy(s->dram + addr, src, len);
}

G_GNUC_UNUSED static inline void pcibase_memcpy_to_guest(PCIBaseState *s, uint8_t *dst,
                                           hwaddr addr, size_t len)
{
    if (addr >= s->dram_size) {
        memset(dst, 0xff, len);
        return;
    }
    if (addr + len > s->dram_size) {
        size_t avail = s->dram_size - addr;
        memcpy(dst, s->dram + addr, avail);
        if (len > avail) {
            memset(dst + avail, 0xff, len - avail);
        }
        return;
    }
    memcpy(dst, s->dram + addr, len);
}

/* Emulate the synchronous firmware function call engine used via moxafunc().
 * The driver writes FuncArg/FuncArg1 and FuncCode in the extern table for a
 * port. Hardware clears FuncCode when the function completes and may update
 * FuncArg (for moxafuncret) and other per-port registers.
 */
static void pcibase_handle_function(PCIBaseState *s, hwaddr func_base)
{
    /* func_base is MOXA_EXTERN_TABLE + MOXA_EXTERN_SIZE*port + FUNCCODE */
    uint16_t cmd = pcibase_readw(s, func_base);
    uint16_t arg = pcibase_readw(s, func_base + (FUNC_ARG_OFFSET - FUNC_CODE_OFFSET));
    uint16_t arg1 = pcibase_readw(s, func_base + (FUNC_ARG1_OFFSET - FUNC_CODE_OFFSET));

    /* Compute base of this port's extern struct from func_base. */
    hwaddr port_base = func_base - FUNC_CODE_OFFSET;

    switch (cmd) {
    case MOXA_FC_FLUSHQUEUE:
        /* arg: 0 = RX, 1 = TX, 2 = both. We clear RX/TX ring pointers. */
        if (arg == 0 || arg == 2) {
            pcibase_writew(s, port_base + RX_RPTR_OFFSET, 0);
            pcibase_writew(s, port_base + RX_WPTR_OFFSET, 0);
        }
        if (arg == 1 || arg == 2) {
            pcibase_writew(s, port_base + TX_RPTR_OFFSET, 0);
            pcibase_writew(s, port_base + TX_WPTR_OFFSET, 0);
        }
        break;
    case MOXA_FC_SETBAUD:
        /* arg is divider. For simplicity, accept anything and do not
         * change behaviour. No return value needed. */
        (void)arg1;
        break;
    case MOXA_FC_SETDATAMODE:
        /* arg is mode (data bits, parity, stop). Accept silently. */
        (void)arg1;
        break;
    case MOXA_FC_SETXONXOFF:
        /* FuncArg (low byte) = VSTART, FuncArg1 (low byte) = VSTOP. */
        /* We don't emulate XON/XOFF further. */
        (void)arg;
        (void)arg1;
        break;
    case MOXA_FC_SETFLOWCTL: {
        /* arg: bitmask of RTS/CTS/Tx/Rx flow control and IXANY. */
        uint16_t flow = arg;
        pcibase_writew(s, port_base + MOXA_EX_FLOWCONTROL, flow);
        break;
    }
    case MOXA_FC_LINECONTROL: {
        /* arg: DTR/RTS bits. Save in a simple per-port shadow location. */
        uint8_t line = arg & (MOXA_DTR_ON | MOXA_RTS_ON);
        /* store in low byte of FLOWCONTROL shadow (no dedicated offset) */
        uint16_t old = pcibase_readw(s, port_base + MOXA_EX_FLOWCONTROL);
        old = (old & 0xff00) | line;
        pcibase_writew(s, port_base + MOXA_EX_FLOWCONTROL, old);
        break;
    }
    case MOXA_FC_SETBREAKIRQ:
        /* arg ignored; driver uses this only on 320 boards. */
        (void)arg;
        (void)arg1;
        break;
    case MOXA_FC_SETLINEIRQ:
        /* arg holds Magic_code. Enable DCD-change notifications. We
         * do not generate real interrupts; polling sees FlagStat. */
        (void)arg;
        (void)arg1;
        break;
    case MOXA_FC_ENABLECH:
    case MOXA_FC_DISABLECH:
        /* Mark channel enabled/disabled. We can use HostStat bit TX. */
        {
            uint16_t hs = pcibase_readw(s, port_base + HOSTSTAT_OFFSET);
            if (cmd == MOXA_FC_ENABLECH) {
                hs |= WAKEUP_TX_BIT;
            } else {
                hs &= (uint16_t)~WAKEUP_TX_BIT;
            }
            pcibase_writew(s, port_base + HOSTSTAT_OFFSET, hs);
        }
        break;
    case MOXA_FC_SETXONSTATE:
        /* clear XOFF bit */
        {
            uint16_t fs = pcibase_readw(s, port_base + FLAGSTAT_OFFSET);
            fs &= (uint16_t)~XOFF_STATE_BIT;
            pcibase_writew(s, port_base + FLAGSTAT_OFFSET, fs);
        }
        break;
    case MOXA_FC_SETXOFFSTATE:
        {
            uint16_t fs2 = pcibase_readw(s, port_base + FLAGSTAT_OFFSET);
            fs2 |= XOFF_STATE_BIT;
            pcibase_writew(s, port_base + FLAGSTAT_OFFSET, fs2);
        }
        break;
    case MOXA_FC_GETCLOCKRATE:
        /* Return some nominal clock. Driver uses only for ioctl paths,
         * which are not part of boot-time probe; leave unimplemented
         * to avoid inventing behaviour. */
        (void)arg;
        (void)arg1;
        break;
    case MOXA_FC_LINESTATUS:
        /* moxafuncret(FC_LineStatus, 0) expects modem bits in FuncArg.
         * We derive from FlagStat shifting as in driver:
         * val = FlagStat >> 4; val &= 0x0B; if (val & 8) val |= 4. */
        {
            uint16_t fs3 = pcibase_readw(s, port_base + FLAGSTAT_OFFSET);
            int val = (fs3 >> 4) & 0x0b;
            if (val & 8) {
                val |= 4;
            }
            pcibase_writew(s, func_base + (FUNC_ARG_OFFSET - FUNC_CODE_OFFSET),
                           (uint16_t)val);
        }
        break;
    default:
        /* Many command codes exist; we only implement ones used during
         * probe/init as visible above. Others are no-ops. */
        break;
    }

    /* Clear FuncCode to signal completion */
    pcibase_writew(s, func_base, 0);
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* The driver only uses 1- and 2-byte accesses via readb/readw.
     * Guard size accordingly. */
    if (size == 1) {
        val = pcibase_readb(s, addr);
    } else if (size == 2) {
        val = pcibase_readw(s, addr);
    } else {
        /* For unsupported access sizes, return 0. */
        val = 0;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size == 1) {
        pcibase_writeb(s, addr, (uint8_t)val);
    } else if (size == 2) {
        pcibase_writew(s, addr, (uint16_t)val);
    } else {
        /* Ignore unsupported sizes. */
        return;
    }

    /* Special handling for key firmware-visible registers. */

    /* Control_reg: board reset and page selection. */
    if (addr == CONTROL_REG_OFFSET) {
        uint8_t ctrl = pcibase_readb(s, CONTROL_REG_OFFSET);
        s->control_reg = ctrl;
        if (ctrl & MOXA_HW_RESET) {
            /* Hardware reset: clear DRAM contents and internal shadows. */
            memset(s->dram, 0, s->dram_size);
            s->magic_code = MOXA_MAGIC_CODE;
            s->c218_status = 0;
            s->c218_diag = 0;
            s->c218_key = 0;
            s->c218_dload_len = 0;
            s->c218_check_sum = 0;
            s->c218_chksum_ok = 0;
            s->c320_status = 0;
            s->c320_diag = 0;
            s->c320_key = 0;
            s->c320_dload_len = 0;
            s->c320_check_sum = 0;
            s->c320_chksum_ok = 0;
            /* After reset, driver writes BIOS to low 4KB and then writes
             * 0 to Control_reg to restart. We do not interpret BIOS. */
        }
        return;
    }

    /* Firmware loading protocol (moxa_real_load_code) uses sequences on
     * C218/C320 key and lengths/checksum registers. The driver treats
     * MOXA_BOARD_C218_PCI and CP204J identically here; both use
     * C218_xxx offsets and expect C218/CP204J keycodes. For simplicity
     * we model a C218 PCI card with keycode 0x218.
     */

    if (addr == MOXA_C218_DLOAD_LEN || addr == MOXA_C320_DLOAD_LEN) {
        /* Driver writes block length here before using key handshake. */
        uint16_t len = pcibase_readw(s, addr);
        if (addr == MOXA_C218_DLOAD_LEN) {
            s->c218_dload_len = len;
        } else {
            s->c320_dload_len = len;
        }
        return;
    }

    if (addr == MOXA_C218_KEY || addr == MOXA_C320_KEY) {
        uint16_t w = pcibase_readw(s, addr);
        /* In firmware download, driver writes 0, then polls until
         * readw(key) == keycode. We emulate that by, upon write 0,
         * storing the expected keycode into the shadow register so
         * subsequent reads see it immediately.
         */
        if (w == 0) {
            if (addr == MOXA_C218_KEY) {
                s->c218_key = MOXA_C218_KEYCODE;
                pcibase_writew(s, MOXA_C218_KEY, MOXA_C218_KEYCODE);
            } else {
                s->c320_key = MOXA_C320_KEYCODE;
                pcibase_writew(s, MOXA_C320_KEY, MOXA_C320_KEYCODE);
            }
        }
        return;
    }

    if (addr == MOXA_C218_CHECK_SUM || addr == MOXA_C320_CHECK_SUM) {
        /* Driver writes cumulative checksum then writes 0 to key and
         * polls checksum_ok byte for 1. We accept any checksum and
         * set checksum_ok to 1 and the key register to keycode. */
        if (addr == MOXA_C218_CHECK_SUM) {
            s->c218_check_sum = pcibase_readw(s, addr);
            pcibase_writeb(s, MOXA_C218_CHKSUM_OK, 1);
            s->c218_chksum_ok = 1;
            s->c218_key = MOXA_C218_KEYCODE;
            pcibase_writew(s, MOXA_C218_KEY, MOXA_C218_KEYCODE);
        } else {
            s->c320_check_sum = pcibase_readw(s, addr);
            pcibase_writeb(s, MOXA_C320_CHKSUM_OK, 1);
            s->c320_chksum_ok = 1;
            s->c320_key = MOXA_C320_KEYCODE;
            pcibase_writew(s, MOXA_C320_KEY, MOXA_C320_KEYCODE);
        }
        return;
    }

    if (addr == MAGIC_NO_OFFSET) {
        /* Driver writes 0 then polls until MAGIC_CODE appears, twice. */
        uint16_t w2 = pcibase_readw(s, MAGIC_NO_OFFSET);
        if (w2 == 0) {
            pcibase_writew(s, MAGIC_NO_OFFSET, MOXA_MAGIC_CODE);
            s->magic_code = MOXA_MAGIC_CODE;
        }
        return;
    }

    if (addr == MODULE_NO_OFFSET) {
        /* Driver writes j (module count) for 320 boards and then waits
         * for Magic_no to again become Magic_code. We model Magic_no
         * as already stable, so no extra work needed. */
        return;
    }

    if (addr == DISABLE_IRQ_OFFSET) {
        /* Writing 1 disables board IRQ generation. We do not generate
         * hardware IRQs, so this is a no-op beyond storing the value. */
        return;
    }

    if (addr == TMS320_PORT1_OFFSET || addr == TMS320_PORT2_OFFSET ||
        addr == TMS320_CLOCK_OFFSET) {
        /* Stored but unused by our model. */
        return;
    }

    /* Handling of extern-table function engine: detect writes to FuncCode. */
    if ((addr >= MOXA_EXTERN_TABLE + FUNC_CODE_OFFSET) &&
        (addr <  MOXA_EXTERN_TABLE + MOXA_EXTERN_SIZE * MOXA_MAX_PORTS_DEVICE)) {
        /* Check if this is exactly a write to a FUNCCODE offset within a
         * per-port extern table entry. Extern entries repeat every 0x60. */
        hwaddr rel = addr - MOXA_EXTERN_TABLE;
        unsigned port = rel / MOXA_EXTERN_SIZE;
        hwaddr off_in_port = rel % MOXA_EXTERN_SIZE;
        if (off_in_port == FUNC_CODE_OFFSET && port < MOXA_MAX_PORTS_DEVICE) {
            hwaddr func_base = MOXA_EXTERN_TABLE + port * MOXA_EXTERN_SIZE + FUNC_CODE_OFFSET;
            /* After storing command/args, execute it synchronously. */
            pcibase_handle_function(s, func_base);
        }
        return;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    (void)s;
    (void)addr;
    (void)size;

    /* The driver does not use IO-port BARs; return 0. */

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    (void)s;
    (void)addr;
    (void)val;
    (void)size;

    /* No IO-port BARs are used by the driver. */
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

    /* Revert registers to power-on defaults */
    if (s->dram && s->dram_size) {
        memset(s->dram, 0, s->dram_size);
    }

    s->magic_code = MOXA_MAGIC_CODE;
    s->c218_status = 0;
    s->c218_diag = 0;
    s->c218_key = 0;
    s->c218_dload_len = 0;
    s->c218_check_sum = 0;
    s->c218_chksum_ok = 0;
    s->c320_status = 0;
    s->c320_diag = 0;
    s->c320_key = 0;
    s->c320_dload_len = 0;
    s->c320_check_sum = 0;
    s->c320_chksum_ok = 0;
    s->control_reg = 0;

    /* Initialize configuration region expected by driver. */
    pcibase_writew(s, MAGIC_NO_OFFSET, 0);
    pcibase_writew(s, MODULE_CNT_OFFSET, 0);
    pcibase_writew(s, MODULE_NO_OFFSET, 0);
    pcibase_writew(s, DISABLE_IRQ_OFFSET, 1);
    /* Extern tables and IRQ data are zeroed. */
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

    /* Allocate DRAM backing store for BAR2 region driver ioremaps. */
    s->dram_size = 0x4000; /* driver uses ioremap(..., 0x4000) */
    s->dram = g_malloc0(s->dram_size);

    /* BAR Initialization
     * The driver requests PCI BAR 2 as memory. For our virtual device we
     * map BAR0 as the memory region it will ioremap; Linux will still see
     * this as BAR2 logically based on the pci_device_id table, but QEMU
     * does not enforce index matching for the guest. However, to more
     * closely match the driver, we can expose BAR2 as MMIO and mark BAR0/1
     * unused.
     */
    for (int i = 0; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = NULL;
    }

    s->num_bars = 1;
    s->bar_info[0].index = 2; /* BAR2 as used by driver */
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = s->dram_size; /* 0x4000 */
    s->bar_info[0].name = "moxa-mmio";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No MSI/MSI-X or timers used by driver for this board. */

    /* Field initialization before device is live */
    s->magic_code = MOXA_MAGIC_CODE;
    s->c218_status = 0;
    s->c218_diag = 0;
    s->c218_key = 0;
    s->c218_dload_len = 0;
    s->c218_check_sum = 0;
    s->c218_chksum_ok = 0;
    s->c320_status = 0;
    s->c320_diag = 0;
    s->c320_key = 0;
    s->c320_dload_len = 0;
    s->c320_check_sum = 0;
    s->c320_chksum_ok = 0;
    s->control_reg = 0;

    s->has_msi = false;
    s->has_msix = false;

    /* Initialize configuration region expected to be read by firmware
     * loader later; magic_no will be polled after BIOS/code loads. */
    pcibase_writew(s, MAGIC_NO_OFFSET, 0);
    pcibase_writew(s, MODULE_CNT_OFFSET, 1); /* one module => 8 ports */
    pcibase_writew(s, MODULE_NO_OFFSET, 0);
    pcibase_writew(s, DISABLE_IRQ_OFFSET, 1);
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

    if (s->dram) {
        g_free(s->dram);
        s->dram = NULL;
        s->dram_size = 0;
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "moxa_pci",
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

