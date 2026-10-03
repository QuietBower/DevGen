/*
 * QEMU NGBEVF VF Network Device Emulation
 * Based on ngbevf driver at linux-7.1/drivers/net/ethernet/wangxun/ngbevf/ngbevf_main.c
 * Updated with mailbox, reset, and SPI flash handling.
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

#define TYPE_PCIBASE_DEVICE "ngbevf_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x1057
#define DEVICE_ID 0x0110
#define CLASS_ID 0x0200 /* Ethernet controller */

/* BAR sizes */
#define BAR0_SIZE (128 * KiB)
#define BAR4_SIZE (8 * KiB)   /* increased for MSI-X table + PBA */

/* Register offsets from driver */
#define WX_VXMAILBOX         0x600
#define WX_VXSTATUS          0x4
#define WX_VXCTRL            0x8
#define WX_VXCTRL_RST        BIT(0)
#define WX_VXIMS             0x108
#define WX_VXICR             0x100
#define WX_VXIMC             0x10C
#define WX_VXIVAR(i)         (0x240 + (4 * (i)))
#define WX_VXIVAR_MISC       0x260
#define WX_VXMRQC            0x78
#define WX_VXRETA(i)         (0xC0 + ((i) * 4))
#define WX_VXRSSRK(i)        (0x80 + ((i) * 4))
#define WX_VXRDBAL(r)        (0x1000 + (0x40 * (r)))
#define WX_VXRDBAH(r)        (0x1004 + (0x40 * (r)))
#define WX_VXRDT(r)          (0x1008 + (0x40 * (r)))
#define WX_VXRDH(r)          (0x100C + (0x40 * (r)))
#define WX_VXRXDCTL(r)       (0x1010 + (0x40 * (r)))
#define WX_VXTXDCTL(r)       (0x3010 + (0x40 * (r)))
#define WX_VXTDBAL(r)        (0x3000 + (0x40 * (r)))
#define WX_VXTDBAH(r)        (0x3004 + (0x40 * (r)))
#define WX_VXTDT(r)          (0x3008 + (0x40 * (r)))
#define WX_VXTDH(r)          (0x300C + (0x40 * (r)))
#define WX_VXTXD_HEAD_ADDRL(r)   (0x3028 + (0x40 * (r)))
#define WX_VXTXD_HEAD_ADDRH(r)   (0x302C + (0x40 * (r)))
#define WX_VXITR(i)          (0x200 + (4 * (i)))
#define WX_VXMBMEM           0x00C00
#define WX_PX_RR_CFG(_i)     (0x01010 + ((_i) * 0x40))
#define WX_CFG_PORT_ST       0x14404
#define WX_VF_BME            0x4B8
#define WX_SPI_CMD           0x10104
#define WX_SPI_DATA          0x10108
#define WX_SPI_STATUS        0x1010C

/* Mailbox bits */
#define WX_VXMAILBOX_REQ     BIT(0)
#define WX_VXMAILBOX_ACK     BIT(1)
#define WX_VXMAILBOX_VFU     BIT(2)
#define WX_VXMAILBOX_PFSTS   BIT(4)
#define WX_VXMAILBOX_PFACK   BIT(5)
#define WX_VXMAILBOX_RSTI    BIT(6)
#define WX_VXMAILBOX_RSTD    BIT(7)
#define WX_VXMAILBOX_R2C_BITS (WX_VXMAILBOX_RSTD | WX_VXMAILBOX_PFSTS | WX_VXMAILBOX_PFACK)
#define WX_VT_MSGTYPE_ACK     BIT(31)
#define WX_VT_MSGTYPE_NACK    BIT(30)
#define WX_VT_MSGTYPE_CTS     BIT(29)
#define WX_VT_MSGINFO_SHIFT   16

/* Interrupt bits */
#define WX_VF_IRQ_CLEAR_MASK 7

/* RX/TX descriptor bits */
#define WX_VXRXDCTL_ENABLE       BIT(0)
#define WX_VXRXDCTL_BUFSZ_MASK   GENMASK(11, 8)
#define WX_VXRXDCTL_HDRSZ_MASK   GENMASK(15, 12)
#define WX_VXRXDCTL_BUFSZ(f)     FIELD_PREP(GENMASK(11, 8), f)
#define WX_VXRXDCTL_HDRSZ(f)     FIELD_PREP(GENMASK(15, 12), f)
#define WX_VXRXDCTL_BUFLEN_MASK  GENMASK(6, 1)
#define WX_VXRXDCTL_BUFLEN(f)    FIELD_PREP(GENMASK(6, 1), f)
#define WX_VXRXDCTL_RSCEN        BIT(29)
#define WX_VXRXDCTL_RSCMAX_MASK  GENMASK(24, 23)
#define WX_VXRXDCTL_RSCMAX(f)    FIELD_PREP(GENMASK(24, 23), f)
#define WX_VXRXDCTL_DROP         BIT(30)
#define WX_VXRXDCTL_VLAN         BIT(31)
#define WX_VXRXDCTL_DESC_MERGE   BIT(19)
#define WX_VXTXDCTL_ENABLE       BIT(0)
#define WX_VXTXDCTL_FLUSH        BIT(26)
#define WX_VXTXDCTL_HEAD_WB      BIT(27)
#define WX_VXTXDCTL_BUFLEN(f)    FIELD_PREP(GENMASK(6, 1), f)

/* RSS bits */
#define WX_VXMRQC_RSS_EN             BIT(8)
#define WX_VXMRQC_RSS_ALG_IPV4_TCP   BIT(0)
#define WX_VXMRQC_RSS_ALG_IPV4       BIT(1)
#define WX_VXMRQC_RSS_ALG_IPV6       BIT(4)
#define WX_VXMRQC_RSS_ALG_IPV6_TCP   BIT(5)
#define WX_VXMRQC_RSS_HASH(f)    FIELD_PREP(GENMASK(15, 13), f)
#define WX_VXMRQC_RSS(f)         FIELD_PREP(GENMASK(31, 16), f)
#define WX_VXMRQC_RSS_MASK       GENMASK(31, 16)
#define WX_VXMRQC_PSR_L3HDR      BIT(1)
#define WX_VXMRQC_PSR_L4HDR      BIT(0)
#define WX_VXMRQC_PSR_L2HDR      BIT(2)
#define WX_VXMRQC_PSR_TUNMAC     BIT(4)
#define WX_VXMRQC_PSR_TUNHDR     BIT(3)
#define WX_VXMRQC_PSR_MASK       GENMASK(5, 1)
#define WX_VXMRQC_PSR(f)         FIELD_PREP(GENMASK(5, 1), f)

/* VF API commands */
#define WX_VF_RESET                  0x01
#define WX_VF_SET_MAC_ADDR           0x02
#define WX_VF_SET_MULTICAST          0x03
#define WX_VF_UPDATE_XCAST_MODE      0x0c
#define WX_VF_SET_LPE                0x05
#define WX_VF_SET_MACVLAN            0x06
#define WX_VF_API_NEGOTIATE          0x08
#define WX_VF_GET_FW_VERSION         0x11

/* I2C / SPI */
#define WX_SPI_CMD_READ_DWORD        0x1
#define WX_SPI_CMD_CMD(_v)           FIELD_PREP(GENMASK(30, 28), _v)
#define WX_SPI_CMD_CLK(_v)           FIELD_PREP(GENMASK(27, 25), _v)
#define WX_SPI_CLK_DIV               0x3
#define WX_VF_BME_ENABLE             0x1

/* Generic macros (redefined locally to avoid kernel dependencies) */
#define GENMASK(h, l) (((~0UL) << (l)) & (~0UL >> (63 - (h) + (l))))
#define FIELD_PREP(mask, val) (((typeof(mask))(val) << (__ffs(mask))) & (mask))
#define FIELD_GET(mask, val) (((typeof(mask))(val) & (mask)) >> __ffs(mask))
#define BIT(nr) (1UL << (nr))

/* Local definition of __ffs (find first set bit, 0-indexed) */
static inline int __ffs(unsigned long word) {
    return __builtin_ctzl(word);
}

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
    MemoryRegion bar_mmios[6];  /* IO regions for each BAR */
    BARInfo bar_info[6];
    int num_bars;

    /* MMIO shadow buffers */
    uint8_t *bar0_mem;
    uint8_t *bar4_mem;

    /* Interrupt state */
    bool has_msi;
    bool has_msix;
    uint32_t intr_mask;     /* WX_VXIMS */
    uint32_t intr_status;   /* WX_VXICR cause bits */

    /* Mailbox state */
    uint32_t mailbox_reg;
    bool mbx_req_pending;
    bool mbx_pfack_set;
    uint8_t perm_mac[6];

    /* SPI flash */
    uint8_t *spi_flash;
    uint32_t spi_addr_reg;
    bool spi_done;

    /* VXCTRL and VXSTATUS */
    uint32_t vxctrl;
    uint32_t vxstatus;
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t pending = s->intr_status & ~s->intr_mask;
    if (pending) {
        if (s->has_msix && msix_enabled(pdev)) {
            msix_notify(pdev, 0);
        } else if (s->has_msi && msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!s->has_msix && !s->has_msi) {
            pci_set_irq(pdev, 0);
        }
    }
}

/* SPI flash helper */
static uint32_t spi_read_flash(PCIBaseState *s, hwaddr offset)
{
    if (!s->spi_flash || offset + 3 >= (256 * KiB)) {
        return 0xFFFFFFFF;
    }
    return ldl_le_p(s->spi_flash + offset);
}

/* Mailbox command processing */
static void process_mailbox_cmd(PCIBaseState *s)
{
    uint32_t command = ldl_le_p(s->bar0_mem + WX_VXMBMEM);
    uint32_t response[4] = {0};
    bool ack = false;

    qemu_log_mask(LOG_GUEST_ERROR, "ngbevf: mailbox command 0x%x\n", command);

    switch (command) {
    case WX_VF_RESET:
        response[0] = WX_VF_RESET | WX_VT_MSGTYPE_ACK;
        memcpy(&response[1], s->perm_mac, 6);
        response[3] = 0;  /* mc_filter_type */
        ack = true;
        break;
    case WX_VF_API_NEGOTIATE: {
        uint32_t version = ldl_le_p(s->bar0_mem + WX_VXMBMEM + 4);
        response[0] = WX_VF_API_NEGOTIATE | WX_VT_MSGTYPE_ACK;
        response[1] = version;
        ack = true;
        break;
    }
    case WX_VF_GET_FW_VERSION:
        response[0] = WX_VF_GET_FW_VERSION | WX_VT_MSGTYPE_ACK;
        response[1] = 0x00010000;
        ack = true;
        break;
    case WX_VF_SET_MAC_ADDR:
        response[0] = WX_VF_SET_MAC_ADDR | WX_VT_MSGTYPE_ACK;
        ack = true;
        break;
    default:
        response[0] = command | WX_VT_MSGTYPE_NACK;
        break;
    }

    /* Write response to mailbox data */
    for (int i = 0; i < 4; i++) {
        stl_le_p(s->bar0_mem + WX_VXMBMEM + i * 4, response[i]);
    }

    if (ack) {
        s->mailbox_reg |= WX_VXMAILBOX_PFACK;
        s->mbx_pfack_set = true;
    }
    s->mbx_req_pending = false;
}

/* MMIO/PIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Determine which BAR this access targets */
    if (addr < BAR0_SIZE) {
        /* BAR0 region */
        if (addr + size > BAR0_SIZE) return 0;

        switch (addr) {
        case WX_VXSTATUS:
            val = s->vxstatus;
            break;
        case WX_VXCTRL:
            val = s->vxctrl;
            break;
        case WX_VXICR:
            val = s->intr_status;
            break;
        case WX_VXMAILBOX:
            val = s->mailbox_reg;
            break;
        case WX_SPI_STATUS:
            val = s->spi_done ? 0x1 : 0x0;  /* bit 0: done */
            break;
        case WX_SPI_DATA:
            /* Read last SPI data */
            val = ldl_le_p(s->bar0_mem + addr);
            break;
        default:
            memcpy(&val, s->bar0_mem + addr, size);
            break;
        }
    } else {
        /* BAR4 region (offset from base of BAR4) */
        hwaddr bar4_off = addr - BAR0_SIZE;
        if (bar4_off + size > BAR4_SIZE) return 0;
        memcpy(&val, s->bar4_mem + bar4_off, size);
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < BAR0_SIZE) {
        /* BAR0 writes */
        if (addr + size > BAR0_SIZE) return;

        switch (addr) {
        case WX_VXSTATUS:
            /* Read-only, ignore */
            break;
        case WX_VXCTRL:
            s->vxctrl = val;
            if (val & WX_VXCTRL_RST) {
                /* Initiate reset */
                s->mailbox_reg |= WX_VXMAILBOX_RSTD;
                s->mailbox_reg &= ~WX_VXMAILBOX_RSTI;
                s->vxctrl &= ~WX_VXCTRL_RST;
                /* Clear some state */
                s->intr_mask = 0;
                s->intr_status = 0;
                pcibase_update_irq(s);
            }
            break;
        case WX_VXICR:
            /* Write 1 to clear bits */
            s->intr_status &= ~val;
            pcibase_update_irq(s);
            break;
        case WX_VXIMS:
            s->intr_mask |= val;
            pcibase_update_irq(s);
            break;
        case WX_VXIMC:
            s->intr_mask &= ~val;
            pcibase_update_irq(s);
            break;
        case WX_VXMAILBOX:
            /* Mailbox handshake */
            if (val & WX_VXMAILBOX_REQ) {
                s->mbx_req_pending = true;
                s->mailbox_reg |= WX_VXMAILBOX_REQ;
                if (!s->mbx_pfack_set) {
                    process_mailbox_cmd(s);
                }
            } else if (val & WX_VXMAILBOX_ACK) {
                /* VF acknowledges; clear PFACK and REQ */
                s->mailbox_reg &= ~(WX_VXMAILBOX_PFACK | WX_VXMAILBOX_REQ);
                s->mailbox_reg |= WX_VXMAILBOX_ACK;
                s->mbx_pfack_set = false;
            } else {
                /* Direct write, update bits except REQ/ACK if not set */
                s->mailbox_reg = (val & ~(WX_VXMAILBOX_REQ | WX_VXMAILBOX_ACK)) |
                                 (s->mailbox_reg & (WX_VXMAILBOX_REQ | WX_VXMAILBOX_ACK));
            }
            break;
        case WX_SPI_CMD:
        {
            uint32_t cmd_type = FIELD_GET(GENMASK(30, 28), val);
            if (cmd_type == WX_SPI_CMD_READ_DWORD) {
                /* Simple: read from flash at pre-stored address */
                uint32_t flash_addr = s->spi_addr_reg;
                uint32_t flash_data = spi_read_flash(s, flash_addr);
                stl_le_p(s->bar0_mem + WX_SPI_DATA, flash_data);
                s->spi_done = true;
            }
            stl_le_p(s->bar0_mem + addr, val);
            break;
        }
        case WX_SPI_DATA:
            /* Store written value for address or data */
            s->spi_addr_reg = val;  /* assume it's the flash address */
            stl_le_p(s->bar0_mem + addr, val);
            break;
        case WX_SPI_STATUS:
            /* Ignore writes */
            break;
        default:
            /* Default: write to shadow memory */
            memcpy(s->bar0_mem + addr, &val, size);
            break;
        }
    } else {
        /* BAR4 writes */
        hwaddr bar4_off = addr - BAR0_SIZE;
        if (bar4_off + size > BAR4_SIZE) return;
        memcpy(s->bar4_mem + bar4_off, &val, size);
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;
    uint64_t val = 0;
    /* No PIO access for this device */
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;
    /* No PIO access for this device */
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

    /* Clear BAR MMIO buffers */
    if (s->bar0_mem) {
        memset(s->bar0_mem, 0, BAR0_SIZE);
    }
    if (s->bar4_mem) {
        memset(s->bar4_mem, 0, BAR4_SIZE);
    }

    /* Reset interrupt state */
    s->intr_mask = 0;
    s->intr_status = 0;

    /* Reset mailbox */
    s->mailbox_reg = 0;
    s->mbx_req_pending = false;
    s->mbx_pfack_set = false;

    /* Reset VXCTRL and VXSTATUS */
    s->vxctrl = 0;
    s->vxstatus = 0;

    /* SPI flash state */
    s->spi_done = false;
    s->spi_addr_reg = 0;

    pcibase_update_irq(s);
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }
    
    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];
    MemoryRegion *mmio = &s->bar_mmios[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init(mr, OBJECT(s), bi->name, aligned_size);
        memory_region_init_io(mmio, OBJECT(s), &pcibase_mmio_ops, s, bi->name, aligned_size);
        memory_region_add_subregion(mr, 0, mmio);
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
    int pm_pos;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID, VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, DEVICE_ID);
    pci_config_set_class(pci_conf, CLASS_ID);
    pci_set_byte(pci_conf + PCI_CLASS_PROG, 0x00); /* Programming interface */
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Subsystem IDs to match Wangxun vendor so flash read is not needed, but we still emulate */
    pci_set_word(pci_conf + PCI_SUBSYSTEM_VENDOR_ID, VENDOR_ID);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_ID, 0x0110);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* Allocate MMIO shadow buffers */
    s->bar0_mem = g_malloc0(BAR0_SIZE);
    s->bar4_mem = g_malloc0(BAR4_SIZE);

    /* Initialize permanent MAC address */
    s->perm_mac[0] = 0x02;
    s->perm_mac[1] = 0x00;
    s->perm_mac[2] = 0x00;
    s->perm_mac[3] = 0x00;
    s->perm_mac[4] = 0x00;
    s->perm_mac[5] = 0x01;

    /* Allocate SPI flash (256 KiB) */
    s->spi_flash = g_malloc0(256 * KiB);
    /* Pre-fill a valid subsystem device ID at offset 0xfffdc */
    stl_le_p(s->spi_flash + 0xfffdc, 0x1234);

    /* BAR Initialization */
    s->num_bars = 2;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = BAR0_SIZE, .name = "bar0" };
    s->bar_info[1] = (BARInfo){ .index = 4, .type = BAR_TYPE_MMIO, .size = BAR4_SIZE, .name = "bar4" };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI-X initialization on BAR4 */
    s->has_msix = true;
    s->has_msi = false;
    uint8_t msix_cap = 0; /* auto-assign capability offset */
    if (msix_init(pdev, 1, &s->bar_regions[4], 4, 0, &s->bar_regions[4], 4, 0x1000, msix_cap, errp)) {
        g_free(s->bar0_mem);
        g_free(s->bar4_mem);
        g_free(s->spi_flash);
        return;
    }
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
    g_free(s->bar0_mem);
    g_free(s->bar4_mem);
    g_free(s->spi_flash);
}

static const VMStateDescription vmstate_pcibase = {
    .name = "ngbevf_pci",
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
