/*
 * QEMU model for txgbevf VF device (based on txgbevf_main.c)
 * Phase 2: Functional behavior implementation
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

#define TYPE_PCIBASE_DEVICE "txgbevf_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Vendor ID placeholder (not in driver source) */
#define DEVICE_ID 0x1000
#define CLASS_ID PCI_CLASS_NETWORK_ETHERNET

/* Register offsets from txgbevf_main.c */
#define WX_VXMAILBOX         0x600
#define WX_VXMAILBOX_SIZE    15
#define WX_VXSTATUS          0x4
#define WX_VXCTRL            0x8
#define WX_VXCTRL_RST        BIT(0)
#define WX_VXIMS             0x108
#define WX_VXICR             0x100
#define WX_VXIMC             0x10C
#define WX_VXIVAR(i)         (0x240 + (4 * (i)))
#define WX_VXIVAR_MISC       0x260
#define WX_VXITR(i)          (0x200 + (4 * (i)))
#define WX_VXITR_MASK            GENMASK(8, 0)
#define WX_VXITR_CNT_WDIS        BIT(31)
#define WX_VXMRQC            0x78
#define WX_VXMRQC_RSS_EN         BIT(8)
#define WX_VXMRQC_RSS_ALG_IPV4   BIT(1)
#define WX_VXMRQC_RSS_ALG_IPV6   BIT(4)
#define WX_VXMRQC_RSS_ALG_IPV4_TCP BIT(0)
#define WX_VXMRQC_RSS_ALG_IPV6_TCP BIT(5)
#define WX_VXMRQC_RSS_MASK       GENMASK(31, 16)
#define WX_VXMRQC_RSS(f)         FIELD_PREP(GENMASK(31, 16), f)
#define WX_VXMRQC_PSR_MASK       GENMASK(5, 1)
#define WX_VXMRQC_PSR(f)         FIELD_PREP(GENMASK(5, 1), f)
#define WX_VXMRQC_PSR_L2HDR      BIT(2)
#define WX_VXMRQC_PSR_L3HDR      BIT(1)
#define WX_VXMRQC_PSR_L4HDR      BIT(0)
#define WX_VXMRQC_PSR_TUNMAC     BIT(4)
#define WX_VXMRQC_PSR_TUNHDR     BIT(3)
#define WX_VXRDBAL(r)        (0x1000 + (0x40 * (r)))
#define WX_VXRDBAH(r)        (0x1004 + (0x40 * (r)))
#define WX_VXRDT(r)          (0x1008 + (0x40 * (r)))
#define WX_VXRDH(r)          (0x100C + (0x40 * (r)))
#define WX_VXRXDCTL(r)       (0x1010 + (0x40 * (r)))
#define WX_VXRXDCTL_ENABLE   BIT(0)
#define WX_VXRXDCTL_DROP     BIT(30)
#define WX_VXRXDCTL_VLAN     BIT(31)
#define WX_VXRXDCTL_RSCEN    BIT(29)
#define WX_VXRXDCTL_DESC_MERGE BIT(19)
#define WX_VXRXDCTL_BUFSZ_MASK   GENMASK(11, 8)
#define WX_VXRXDCTL_BUFSZ(f)     FIELD_PREP(GENMASK(11, 8), f)
#define WX_VXRXDCTL_HDRSZ_MASK   GENMASK(15, 12)
#define WX_VXRXDCTL_HDRSZ(f)     FIELD_PREP(GENMASK(15, 12), f)
#define WX_VXRXDCTL_BUFLEN_MASK  GENMASK(6, 1)
#define WX_VXRXDCTL_BUFLEN(f)    FIELD_PREP(GENMASK(6, 1), f)
#define WX_VXRXDCTL_RSCMAX_MASK  GENMASK(24, 23)
#define WX_VXRXDCTL_RSCMAX(f)    FIELD_PREP(GENMASK(24, 23), f)
#define WX_VXTDBAL(r)        (0x3000 + (0x40 * (r)))
#define WX_VXTDBAH(r)        (0x3004 + (0x40 * (r)))
#define WX_VXTDT(r)          (0x3008 + (0x40 * (r)))
#define WX_VXTDH(r)          (0x300C + (0x40 * (r)))
#define WX_VXTXDCTL(r)       (0x3010 + (0x40 * (r)))
#define WX_VXTXDCTL_ENABLE   BIT(0)
#define WX_VXTXDCTL_FLUSH    BIT(26)
#define WX_VXTXDCTL_HEAD_WB  BIT(27)
#define WX_VXTXDCTL_BUFLEN(f)    FIELD_PREP(GENMASK(6, 1), f)
#define WX_VXTXD_HEAD_ADDRL(r)   (0x3028 + (0x40 * (r)))
#define WX_VXTXD_HEAD_ADDRH(r)   (0x302C + (0x40 * (r)))
#define WX_VF_RESET          0x01
#define WX_VF_BME_ENABLE     BIT(0)
#define WX_VF_IRQ_CLEAR_MASK 7
#define WX_VF_GET_QUEUES     0x09
#define WX_VF_SET_MAC_ADDR   0x02
#define WX_VF_SET_MULTICAST  0x03
#define WX_VF_SET_LPE        0x05
#define WX_VF_SET_MACVLAN    0x06
#define WX_VF_UPDATE_XCAST_MODE 0x0c
#define WX_VF_API_NEGOTIATE  0x08
#define WX_VF_GET_FW_VERSION 0x11
#define WX_VF_MAX_RX_QUEUES  4
#define WX_VF_MAX_TX_QUEUES  4
#define WX_VF_MAX_RING_NUMS  8
#define WX_VF_DEF_QUEUE      4
#define WX_VF_TRANS_VLAN     3
#define WX_VF_RX_QUEUES      2
#define WX_VF_TX_QUEUES      1
#define WX_VT_MSGTYPE_ACK    BIT(31)
#define WX_VT_MSGTYPE_CTS    BIT(29)
#define WX_VT_MSGTYPE_NACK   BIT(30)
#define WX_VT_MSGINFO_SHIFT   16
#define WX_VXMAILBOX_REQ     BIT(0)
#define WX_VXMAILBOX_ACK     BIT(1)
#define WX_VXMAILBOX_RSTI    BIT(6)
#define WX_VXMAILBOX_RSTD    BIT(7)
#define WX_VXMAILBOX_PFSTS   BIT(4)
#define WX_VXMAILBOX_PFACK   BIT(5)
#define WX_VXMAILBOX_VFU     BIT(2)
#define WX_VXMAILBOX_R2C_BITS (WX_VXMAILBOX_RSTD | \
	    WX_VXMAILBOX_PFSTS | WX_VXMAILBOX_PFACK)
#define WX_SPI_CMD           0x10104
#define WX_SPI_CMD_CMD(_v)   FIELD_PREP(GENMASK(30, 28), _v)
#define WX_SPI_CMD_CLK(_v)   FIELD_PREP(GENMASK(27, 25), _v)
#define WX_SPI_CMD_READ_DWORD 0x1
#define WX_SPI_CLK_DIV       0x3
#define WX_SPI_DATA          0x10108
#define WX_SPI_STATUS        0x1010C
#define WX_CFG_PORT_ST       0x14404
#define WX_CFG_PORT_ST_LANID     GENMASK(9, 8)
#define WX_VXRSSRK(i)        (0x80 + ((i) * 4))
#define WX_VXRETA(i)         (0xC0 + ((i) * 4))
#define WX_RSS_KEY_SIZE     40
#define WX_RSS_INDIR_TBL_MAX 64
#define WX_MAX_RETA_ENTRIES  128
#define WX_RSS_FIELD_IPV4          BIT(1)
#define WX_RSS_FIELD_IPV6          BIT(5)
#define WX_RSS_FIELD_IPV4_TCP      BIT(0)
#define WX_RSS_FIELD_IPV6_TCP      BIT(4)
#define WX_VXMBMEM           0x00C00
#define WX_VX_PF_BME         0x4B8
#define WX_PX_RR_CFG(_i)             (0x01010 + ((_i) * 0x40))
#define WX_PX_RR_CFG_RR_EN           BIT(0)
#define WX_PX_IVAR_ALLOC_VAL         0x80

/* MSI-X definitions from driver */
#define TXGBEVF_MAX_MSIX_VECTORS       2

/* BAR sizes */
#define MMIO_BAR0_SIZE 0x20000  /* 128 KB for main registers */
#define MMIO_BAR4_SIZE 0x1000   /* 4 KB for BAR4 (b4_addr) */

#define MAILBOX_MEM_DWORDS 16

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
    uint32_t intr_status;      /* WX_VXICR */
    uint32_t intr_mask;        /* WX_VXIMS */

    /* Shadow registers for BAR0 (MMIO_BAR0_SIZE/4 dwords) */
    uint32_t regs[MMIO_BAR0_SIZE / 4];

    /* BAR4 shadow registers */
    uint32_t bar4_regs[MMIO_BAR4_SIZE / 4];

    /* Mailbox state */
    uint32_t mailbox_mem[MAILBOX_MEM_DWORDS]; /* WX_VXMBMEM (0xC00) */
    bool mailbox_ack_pending;
    bool reset_in_progress; /* Indicates RSTI/RSTD assertion */

    /* MAC address storage */
    uint8_t mac_addr[6];
};

/* Internal helper for status-triggered signaling */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t pending = s->intr_status & ~s->intr_mask;

    if (pending) {
        /* For simplicity, fire MSI-X vector 0. The driver will read ICR to
         * determine the cause. Proper IVAR mapping is not implemented. */
        if (msix_enabled(pdev)) {
            msix_notify(pdev, 0);
        } else if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!msix_enabled(pdev) && !msi_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

/* Mailbox command handling */
static void mailbox_process_cmd(PCIBaseState *s)
{
    uint32_t cmd_id = s->mailbox_mem[0] & 0xFF; /* Command ID in lower bits */

    switch (cmd_id) {
    case WX_VF_RESET:
        s->mailbox_mem[0] = WX_VF_RESET | WX_VT_MSGTYPE_ACK;
        s->mailbox_mem[1] = 0x00000002; /* dummy perm MAC (02:00:00:00:00:00) */
        s->mailbox_mem[2] = 0x00000000;
        s->mailbox_mem[3] = 0;          /* mc_filter_type */
        s->mailbox_ack_pending = true;
        break;
    case WX_VF_GET_QUEUES:
        s->mailbox_mem[0] = WX_VF_GET_QUEUES | WX_VT_MSGTYPE_ACK;
        s->mailbox_mem[WX_VF_TX_QUEUES] = WX_VF_MAX_TX_QUEUES;
        s->mailbox_mem[WX_VF_RX_QUEUES] = WX_VF_MAX_RX_QUEUES;
        s->mailbox_mem[WX_VF_TRANS_VLAN] = 1;
        s->mailbox_mem[WX_VF_DEF_QUEUE] = 4;
        s->mailbox_ack_pending = true;
        break;
    case WX_VF_SET_MAC_ADDR:
        memcpy(s->mac_addr, &s->mailbox_mem[1], 6);
        s->mailbox_mem[0] = WX_VF_SET_MAC_ADDR | WX_VT_MSGTYPE_ACK;
        s->mailbox_ack_pending = true;
        break;
    case WX_VF_API_NEGOTIATE:
        /* Requested version is in mailbox_mem[1]; we always ACK with the same version */
        s->mailbox_mem[0] = WX_VF_API_NEGOTIATE | WX_VT_MSGTYPE_ACK;
        /* Leave mailbox_mem[1] unchanged */
        s->mailbox_ack_pending = true;
        break;
    case WX_VF_GET_FW_VERSION:
        s->mailbox_mem[0] = WX_VF_GET_FW_VERSION | WX_VT_MSGTYPE_ACK;
        s->mailbox_mem[1] = 0x00010000; /* firmware version */
        s->mailbox_ack_pending = true;
        break;
    default:
        /* Unsupported command: NACK */
        s->mailbox_mem[0] = cmd_id | WX_VT_MSGTYPE_NACK;
        s->mailbox_ack_pending = false;
        break;
    }

    if (s->mailbox_ack_pending) {
        /* Set interrupt status bit for mailbox (bit 1) */
        s->intr_status |= BIT(1);
        pcibase_update_irq(s);
    }
}

/* BAR0 MMIO read */
static uint64_t pcibase_bar0_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= MMIO_BAR0_SIZE) {
        return 0;
    }

    switch (addr) {
    case WX_VXSTATUS:
        /* Status register: bit 0 indicates link up? */
        val = BIT(0); /* link up */
        break;
    case WX_VXCTRL:
        val = s->regs[WX_VXCTRL / 4];
        break;
    case WX_VXICR:
        val = s->intr_status;
        break;
    case WX_VXIMS:
        val = s->intr_mask;
        break;
    case WX_VXMAILBOX:
        /* Mailbox status register */
        if (s->mailbox_ack_pending) {
            val = WX_VXMAILBOX_ACK | WX_VXMAILBOX_PFACK;
        } else {
            val = 0;
        }
        if (s->reset_in_progress) {
            val |= WX_VXMAILBOX_RSTI | WX_VXMAILBOX_RSTD;
            s->reset_in_progress = false; /* Complete reset visibility */
        }
        break;
    case WX_VXMRQC:
        val = s->regs[WX_VXMRQC / 4];
        break;
    default:
        if (addr >= WX_VXRSSRK(0) && addr <= WX_VXRSSRK(WX_RSS_KEY_SIZE - 1)) {
            int i = (addr - WX_VXRSSRK(0)) / 4;
            val = s->regs[(WX_VXRSSRK(0) / 4) + i];
        } else if (addr >= WX_VXRETA(0) && addr <= WX_VXRETA(WX_MAX_RETA_ENTRIES - 1)) {
            int i = (addr - WX_VXRETA(0)) / 4;
            val = s->regs[(WX_VXRETA(0) / 4) + i];
        } else if (addr >= WX_VXIVAR(0) && addr <= WX_VXIVAR_MISC) {
            int i = (addr - WX_VXIVAR(0)) / 4;
            if (i <= (WX_VXIVAR_MISC - WX_VXIVAR(0)) / 4) {
                val = s->regs[(WX_VXIVAR(0) / 4) + i];
            }
        } else if (addr >= WX_VXITR(0) && addr <= WX_VXITR(3)) {
            int i = (addr - WX_VXITR(0)) / 4;
            val = s->regs[(WX_VXITR(0) / 4) + i];
        } else if (addr >= WX_VXRDBAL(0) && addr <= (WX_VXRXDCTL(3) + 4)) {
            /* RX queue registers: map to regs array */
            val = s->regs[addr / 4];
        } else if (addr >= WX_VXTDBAL(0) && addr <= (WX_VXTXD_HEAD_ADDRH(3) + 4)) {
            val = s->regs[addr / 4];
        } else if (addr >= WX_VXMBMEM && addr < (WX_VXMBMEM + sizeof(s->mailbox_mem))) {
            int i = (addr - WX_VXMBMEM) / 4;
            val = s->mailbox_mem[i];
        } else if (addr >= WX_PX_RR_CFG(0) && addr <= (WX_PX_RR_CFG(7))) {
            val = s->regs[addr / 4];
        } else if (addr == WX_SPI_CMD || addr == WX_SPI_DATA || addr == WX_SPI_STATUS) {
            val = s->regs[addr / 4];
        } else if (addr == WX_CFG_PORT_ST) {
            val = s->regs[addr / 4];
        } else {
            /* Unknown register: return 0 */
            val = 0;
        }
        break;
    }

    return val;
}

/* BAR0 MMIO write */
static void pcibase_bar0_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= MMIO_BAR0_SIZE) {
        return;
    }

    switch (addr) {
    case WX_VXSTATUS:
        /* Read-only? */
        break;
    case WX_VXCTRL:
        s->regs[WX_VXCTRL / 4] = val;
        if (val & WX_VXCTRL_RST) {
            /* Software reset triggered */
            s->reset_in_progress = true;
            /* Reset internal state (registers will be cleared) */
            memset(s->regs, 0, sizeof(s->regs));
            s->intr_status = 0;
            s->intr_mask = 0xFFFFFFFF; /* All masked */
            s->mailbox_ack_pending = false;
            memset(s->mailbox_mem, 0, sizeof(s->mailbox_mem));
            memset(s->mac_addr, 0, 6);
        }
        break;
    case WX_VXICR:
        /* Write-1-to-clear bits in ICR */
        s->intr_status &= ~(val & WX_VF_IRQ_CLEAR_MASK);
        pcibase_update_irq(s);
        break;
    case WX_VXIMS:
        s->intr_mask = val;
        pcibase_update_irq(s);
        break;
    case WX_VXIMC:
        /* Write 1 to mask (clear) interrupts: set mask bits */
        s->intr_mask |= val;
        pcibase_update_irq(s);
        break;
    case WX_VXMAILBOX:
        if (val & WX_VXMAILBOX_REQ) {
            mailbox_process_cmd(s);
        }
        if (val & WX_VXMAILBOX_ACK) {
            /* Clear ACK from VF */
            s->mailbox_ack_pending = false;
            /* Also clear interrupt? */
            s->intr_status &= ~BIT(1);
            pcibase_update_irq(s);
        }
        break;
    case WX_VXMRQC:
        s->regs[WX_VXMRQC / 4] = val;
        break;
    default:
        /* Write to mapped register ranges */
        if (addr >= WX_VXRSSRK(0) && addr <= WX_VXRSSRK(WX_RSS_KEY_SIZE - 1)) {
            int i = (addr - WX_VXRSSRK(0)) / 4;
            s->regs[(WX_VXRSSRK(0) / 4) + i] = val;
        } else if (addr >= WX_VXRETA(0) && addr <= WX_VXRETA(WX_MAX_RETA_ENTRIES - 1)) {
            int i = (addr - WX_VXRETA(0)) / 4;
            s->regs[(WX_VXRETA(0) / 4) + i] = val;
        } else if (addr >= WX_VXIVAR(0) && addr <= WX_VXIVAR_MISC) {
            int i = (addr - WX_VXIVAR(0)) / 4;
            if (i <= (WX_VXIVAR_MISC - WX_VXIVAR(0)) / 4) {
                s->regs[(WX_VXIVAR(0) / 4) + i] = val;
            }
        } else if (addr >= WX_VXITR(0) && addr <= WX_VXITR(3)) {
            int i = (addr - WX_VXITR(0)) / 4;
            s->regs[(WX_VXITR(0) / 4) + i] = val;
        } else if (addr >= WX_VXRDBAL(0) && addr <= (WX_VXRXDCTL(3) + 4)) {
            s->regs[addr / 4] = val;
        } else if (addr >= WX_VXTDBAL(0) && addr <= (WX_VXTXD_HEAD_ADDRH(3) + 4)) {
            s->regs[addr / 4] = val;
        } else if (addr >= WX_VXMBMEM && addr < (WX_VXMBMEM + sizeof(s->mailbox_mem))) {
            int i = (addr - WX_VXMBMEM) / 4;
            s->mailbox_mem[i] = val;
        } else if (addr >= WX_PX_RR_CFG(0) && addr <= (WX_PX_RR_CFG(7))) {
            s->regs[addr / 4] = val;
        } else if (addr == WX_SPI_CMD || addr == WX_SPI_DATA || addr == WX_SPI_STATUS) {
            s->regs[addr / 4] = val;
        } else if (addr == WX_CFG_PORT_ST) {
            s->regs[addr / 4] = val;
        }
        break;
    }
}

/* BAR4 MMIO read (simple passthrough to shadow array) */
static uint64_t pcibase_bar4_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr >= MMIO_BAR4_SIZE) {
        return 0;
    }
    return s->bar4_regs[addr / 4];
}

/* BAR4 MMIO write */
static void pcibase_bar4_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr >= MMIO_BAR4_SIZE) {
        return;
    }
    s->bar4_regs[addr / 4] = val;
}

static const MemoryRegionOps pcibase_bar0_mmio_ops = {
    .read = pcibase_bar0_mmio_read,
    .write = pcibase_bar0_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 8 },
    .impl  = { .min_access_size = 4, .max_access_size = 8 },
};

static const MemoryRegionOps pcibase_bar4_mmio_ops = {
    .read = pcibase_bar4_mmio_read,
    .write = pcibase_bar4_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 8 },
    .impl  = { .min_access_size = 4, .max_access_size = 8 },
};

/* PIO handlers (unused but required for template completeness) */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* No PIO emulation needed */
}

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

    /* Reset registers to power-on defaults */
    memset(s->regs, 0, sizeof(s->regs));
    memset(s->bar4_regs, 0, sizeof(s->bar4_regs));
    s->intr_status = 0;
    s->intr_mask = 0xFFFFFFFF; /* All masked */
    s->mailbox_ack_pending = false;
    s->reset_in_progress = false;
    memset(s->mailbox_mem, 0, sizeof(s->mailbox_mem));
    memset(s->mac_addr, 0, 6);
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }
    
    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        if (bi->index == 0) {
            memory_region_init_io(mr, OBJECT(s), &pcibase_bar0_mmio_ops, s, bi->name, aligned_size);
        } else if (bi->index == 4) {
            memory_region_init_io(mr, OBJECT(s), &pcibase_bar4_mmio_ops, s, bi->name, aligned_size);
        } else {
            memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s, bi->name, aligned_size);
        }
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

    pci_set_word(pci_conf + PCI_VENDOR_ID, 0x1c5f);
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

    /* BAR0: main MMIO registers */
    s->num_bars = 2;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = MMIO_BAR0_SIZE;
    s->bar_info[0].name = "txgbevf-bar0";

    /* BAR4: secondary MMIO (b4_addr) */
    s->bar_info[1].index = 4;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = MMIO_BAR4_SIZE;
    s->bar_info[1].name = "txgbevf-bar4";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI-X initialization (BAR0 contains MSI-X table, offset 0) */
    if (msix_init(pdev, TXGBEVF_MAX_MSIX_VECTORS,
                  &s->bar_regions[0], 0, 0,
                  &s->bar_regions[0], 0, 0,
                  0,
                  errp)) {
        return;
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, &s->bar_regions[0], &s->bar_regions[0]);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

static const VMStateDescription vmstate_pcibase = {
    .name = "txgbevf_pci",
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
