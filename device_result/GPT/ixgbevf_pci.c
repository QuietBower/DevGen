/*
 * QEMU PCI device model for Intel ixgbevf virtual function
 * Functional emulation sufficient for Linux ixgbevf driver probe/bind.
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

#define TYPE_PCIBASE_DEVICE "ixgbevf_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define IXGBEVF_VENDOR_ID      PCI_VENDOR_ID_INTEL
#define IXGBEVF_DEVICE_ID      0x10ED /* IXGBE_DEV_ID_82599_VF */
#define IXGBEVF_CLASS_ID       PCI_CLASS_NETWORK_ETHERNET

#define IXGBE_VFCTRL           0x00000
#define IXGBE_VFSTATUS         0x00008
#define IXGBE_VFLINKS          0x00010
#define IXGBE_VFFRTIMER        0x00048
#define IXGBE_VTEICR           0x00100
#define IXGBE_VTEIMS           0x00108
#define IXGBE_VTEIMC           0x0010C
#define IXGBE_VTEIAC           0x00110
#define IXGBE_VTEIAM           0x00114
#define IXGBE_VTIVAR(x)        (0x00120 + (4 * (x)))
#define IXGBE_VTIVAR_MISC      0x00140
#define IXGBE_VFPSRTYPE        0x00300
#define IXGBE_VFRDBAL(x)       (0x01000 + (0x40 * (x)))
#define IXGBE_VFRDBAH(x)       (0x01004 + (0x40 * (x)))
#define IXGBE_VFRDLEN(x)       (0x01008 + (0x40 * (x)))
#define IXGBE_VFRDH(x)         (0x01010 + (0x40 * (x)))
#define IXGBE_VFRDT(x)         (0x01018 + (0x40 * (x)))
#define IXGBE_VFGPRC           0x0101C
#define IXGBE_VFGORC_LSB       0x01020
#define IXGBE_VFGORC_MSB       0x01024
#define IXGBE_VFRXDCTL(x)      (0x01028 + (0x40 * (x)))
#define IXGBE_VFMPRC           0x01034
#define IXGBE_VFRXMEMWRAP      0x03190
#define IXGBE_VFMRQC           0x3000
#define IXGBE_VFRSSRK(x)       (0x3100 + ((x) * 4))
#define IXGBE_VFRETA(x)        (0x3200 + ((x) * 4))
#define IXGBE_VFTDBAL(x)       (0x02000 + (0x40 * (x)))
#define IXGBE_VFTDBAH(x)       (0x02004 + (0x40 * (x)))
#define IXGBE_VFTDLEN(x)       (0x02008 + (0x40 * (x)))
#define IXGBE_VFDCA_TXCTRL(x)  (0x0200c + (0x40 * (x)))
#define IXGBE_VFGPTC           0x0201C
#define IXGBE_VFGOTC_LSB       0x02020
#define IXGBE_VFGOTC_MSB       0x02024
#define IXGBE_VFTXDCTL(x)      (0x02028 + (0x40 * (x)))
#define IXGBE_VFTDH(x)         (0x02010 + (0x40 * (x)))
#define IXGBE_VFTDT(x)         (0x02018 + (0x40 * (x)))
#define IXGBE_VFTDWBAL(x)      (0x02038 + (0x40 * (x)))
#define IXGBE_VFTDWBAH(x)      (0x0203C + (0x40 * (x)))
#define IXGBE_VTEITR(x)        (0x00820 + (4 * (x)))

#define IXGBE_VTEIMS_ENABLE_MASK_ALL  0xFFFFFFFFU

#define MAX_MSIX_Q_VECTORS 2

/* New macros from supplementary source that are actual registers/bitmasks */
#define IXGBE_VTEICS            0x00104
#define IXGBE_VFSRRCTL(x)       (0x01014 + (0x40 * (x)))
#define IXGBE_VFDCA_RXCTRL(x)   (0x0100C + (0x40 * (x)))

#define IXGBE_IVAR_ALLOC_VAL    0x80
#define IXGBE_MAX_EITR          0x00000FF8
#define IXGBE_EITR_CNT_WDIS     0x80000000U
#define IXGBE_SRRCTL_DROP_EN    0x10000000U
#define IXGBE_SRRCTL_BSIZEHDRSIZE_SHIFT 2
#define IXGBE_SRRCTL_BSIZEPKT_SHIFT 10
#define IXGBE_SRRCTL_DESCTYPE_ADV_ONEBUF 0x02000000U

#define IXGBE_DCA_TXCTRL_DESC_RRO_EN  (1U << 9)
#define IXGBE_DCA_TXCTRL_DATA_RRO_EN  (1U << 13)
#define IXGBE_DCA_RXCTRL_DESC_RRO_EN  (1U << 9)
#define IXGBE_DCA_RXCTRL_DATA_WRO_EN  (1U << 13)

#define IXGBE_RXDCTL_ENABLE     0x02000000U
#define IXGBE_RXDCTL_VME        0x40000000U
#define IXGBE_RXDCTL_RLPMLMASK  0x00003FFFU
#define IXGBE_RXDCTL_RLPML_EN   0x00008000U

#define IXGBE_TXDCTL_ENABLE     0x02000000U

#define IXGBE_LINK_SPEED_10GB_FULL   0x0080
#define IXGBE_LINK_SPEED_1GB_FULL    0x0020
#define IXGBE_LINK_SPEED_100_FULL    0x0008

#define IXGBE_FAILED_READ_REG   0xffffffffU

#define IXGBE_VFMRQC_RSS_FIELD_IPV4      0x00020000U
#define IXGBE_VFMRQC_RSS_FIELD_IPV4_TCP  0x00010000U
#define IXGBE_VFMRQC_RSS_FIELD_IPV6      0x00100000U
#define IXGBE_VFMRQC_RSS_FIELD_IPV6_TCP  0x00200000U
#define IXGBE_VFMRQC_RSSEN               0x00000001U

#define IXGBEVF_RSS_HASH_KEY_SIZE 40
#define IXGBEVF_VFRSSRK_REGS      10
#define IXGBEVF_X550_VFRETA_SIZE  64

#define IXGBEVF_MAX_RSS_QUEUES    2


/* Mailbox-related register and bit definitions inferred from usage */
#define IXGBE_VFMAILBOX          0x00200
#define IXGBE_VFMBMEM            0x00210

#define IXGBE_VFMAILBOX_VFU      0x00000001U
#define IXGBE_VFMAILBOX_ACK      0x00000002U
#define IXGBE_VFMAILBOX_REQ      0x00000004U
#define IXGBE_VFMAILBOX_PFSTS    0x00000008U
#define IXGBE_VFMAILBOX_PFACK    0x00000010U
#define IXGBE_VFMAILBOX_RSTD     0x00000020U
#define IXGBE_VFMAILBOX_RSTI     0x00000040U

/* R2C bits: PF to VF notification/ack/reset bits
 * (exact mask definition is not provided here, so we only declare
 * that it exists; the driver uses IXGBE_VFMAILBOX_R2C_BITS but does
 * not define it in the provided snippet).
 */

#define IXGBE_VF_MBX_INIT_TIMEOUT 0
#define IXGBE_VF_MBX_INIT_DELAY   0
#define IXGBE_VFMAILBOX_SIZE      16

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
    uint32_t intr_mask;

    struct {
        uint32_t vfctrl;
        uint32_t vfstatus;
        uint32_t vflinks;
        uint32_t vffrtimer;
        uint32_t vteicr;
        uint32_t vteims;
        uint32_t vteimc;
        uint32_t vteiac;
        uint32_t vteiam;
        uint32_t vfmrqc;

        /* Mailbox registers and memory */
        uint32_t vfmailbox;
        uint32_t vfmbmem[IXGBE_VFMAILBOX_SIZE];
    } regs;

    uint32_t status_flags;

    struct {
        bool pending_reset;
    } reset_state;

    struct {
        uint8_t pm_state;
    } pm_state;
};

static inline void pcibase_set_irq(PCIBaseState *s, bool level)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    if (s->has_msix && msix_enabled(pdev)) {
        if (level) {
            msix_notify(pdev, 0);
        }
    } else if (s->has_msi && msi_enabled(pdev)) {
        if (level) {
            msi_notify(pdev, 0);
        }
    } else {
        pci_set_irq(pdev, level);
    }
}

static void pcibase_update_irq(PCIBaseState *s)
{
    if (s->intr_status & s->intr_mask) {
        pcibase_set_irq(s, true);
    } else {
        pcibase_set_irq(s, false);
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = 0;

    if (size != 4) {
        return 0;
    }

    switch (addr) {
    case IXGBE_VFCTRL:
        val = s->regs.vfctrl;
        break;
    case IXGBE_VFSTATUS:
        val = s->regs.vfstatus;
        break;
    case IXGBE_VFLINKS:
        /* Report link up at 10G to satisfy driver link checks */
        val = s->regs.vflinks;
        if (val == 0) {
            val = IXGBE_LINK_SPEED_10GB_FULL;
        }
        break;
    case IXGBE_VFFRTIMER:
        val = s->regs.vffrtimer;
        break;
    case IXGBE_VTEICR:
        /* Reading EICR returns pending causes and clears them */
        val = s->regs.vteicr;
        s->regs.vteicr = 0;
        s->intr_status = 0;
        pcibase_update_irq(s);
        break;
    case IXGBE_VTEIMS:
        val = s->regs.vteims;
        break;
    case IXGBE_VTEIMC:
        val = s->regs.vteimc;
        break;
    case IXGBE_VTEIAC:
        val = s->regs.vteiac;
        break;
    case IXGBE_VTEIAM:
        val = s->regs.vteiam;
        break;
    case IXGBE_VFPSRTYPE:
        val = 0;
        break;
    case IXGBE_VTEICS:
        /* Software typically writes-only EICS; reads can return 0 */
        val = 0;
        break;
    case IXGBE_VFMRQC:
        val = s->regs.vfmrqc;
        break;
    case IXGBE_VFGPRC:
    case IXGBE_VFGORC_LSB:
    case IXGBE_VFGORC_MSB:
    case IXGBE_VFGPTC:
    case IXGBE_VFGOTC_LSB:
    case IXGBE_VFGOTC_MSB:
    case IXGBE_VFMPRC:
        val = 0;
        break;
    case IXGBE_VFMAILBOX:
        /*
         * The driver uses ixgbevf_read_mailbox_vf(), which does:
         *   vf_mailbox = IXGBE_READ_REG(hw, IXGBE_VFMAILBOX);
         *   vf_mailbox |= hw->mbx.vf_mailbox;
         *   hw->mbx.vf_mailbox |= vf_mailbox & IXGBE_VFMAILBOX_R2C_BITS;
         * We only emulate the VF-visible register contents here.
         */
        val = s->regs.vfmailbox;
        break;
    default:
        if (addr >= IXGBE_VFMBMEM &&
            addr < IXGBE_VFMBMEM + IXGBE_VFMAILBOX_SIZE * 4) {
            unsigned idx = (addr - IXGBE_VFMBMEM) >> 2;
            val = s->regs.vfmbmem[idx];
        } else if (addr >= IXGBE_VTIVAR(0) && addr <= IXGBE_VTIVAR_MISC) {
            /* IVAR / IVAR_MISC: default 0, driver programs mapping */
            val = 0;
        } else if (addr >= IXGBE_VFRDBAL(0) && addr < IXGBE_VFRXMEMWRAP) {
            /* RX ring registers: don't emulate rings, just return 0 */
            val = 0;
        } else if (addr >= IXGBE_VFTDBAL(0) && addr < IXGBE_VTEITR(0)) {
            /* TX ring registers */
            val = 0;
        } else if (addr >= IXGBE_VTEITR(0) &&
                   addr < IXGBE_VTEITR(0) + 4 * MAX_MSIX_Q_VECTORS) {
            /* EITR: return a clamped interval if programmed */
            int idx = (addr - IXGBE_VTEITR(0)) / 4;
            (void)idx;
            val = IXGBE_MAX_EITR;
        } else if (addr >= IXGBE_VFRSSRK(0) &&
                   addr < IXGBE_VFRETA(0) + 0x100) {
            /* RSS key/indirection table */
            val = 0;
        } else if (addr >= IXGBE_VFSRRCTL(0) &&
                   addr < IXGBE_VFSRRCTL(0) + (0x40 * IXGBEVF_MAX_RSS_QUEUES)) {
            /* SRRCTL per-queue: return single buffer descriptor type */
            val = IXGBE_SRRCTL_DESCTYPE_ADV_ONEBUF;
        } else if (addr >= IXGBE_VFDCA_RXCTRL(0) &&
                   addr < IXGBE_VFDCA_RXCTRL(0) + (0x40 * IXGBEVF_MAX_RSS_QUEUES)) {
            /* RX DCA control: default 0 */
            val = 0;
        } else {
            val = 0;
        }
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        return;
    }

    switch (addr) {
    case IXGBE_VFCTRL:
        s->regs.vfctrl = (uint32_t)val;
        break;
    case IXGBE_VFSTATUS:
        s->regs.vfstatus = (uint32_t)val;
        break;
    case IXGBE_VFLINKS:
        s->regs.vflinks = (uint32_t)val;
        break;
    case IXGBE_VFFRTIMER:
        s->regs.vffrtimer = (uint32_t)val;
        break;
    case IXGBE_VTEICR:
        /* W1C clear of interrupt causes */
        s->regs.vteicr &= ~(uint32_t)val;
        s->intr_status &= ~(uint32_t)val;
        pcibase_update_irq(s);
        break;
    case IXGBE_VTEIMS:
        /* Interrupt mask set */
        s->regs.vteims |= (uint32_t)val;
        s->intr_mask = s->regs.vteims;
        pcibase_update_irq(s);
        break;
    case IXGBE_VTEIMC:
        /* Interrupt mask clear */
        s->regs.vteimc = (uint32_t)val;
        s->regs.vteims &= ~((uint32_t)val);
        s->intr_mask = s->regs.vteims;
        pcibase_update_irq(s);
        break;
    case IXGBE_VTEIAC:
        s->regs.vteiac = (uint32_t)val;
        break;
    case IXGBE_VTEIAM:
        s->regs.vteiam = (uint32_t)val;
        s->intr_mask = s->regs.vteiam;
        pcibase_update_irq(s);
        break;
    case IXGBE_VFPSRTYPE:
        /* protocol split not modeled */
        break;
    case IXGBE_VTEICS:
        /* Writing EICS sets bits in EICR and triggers IRQ if unmasked */
        s->regs.vteicr |= (uint32_t)val;
        s->intr_status |= (uint32_t)val;
        pcibase_update_irq(s);
        break;
    case IXGBE_VFMRQC:
        /* Store MRQC; driver uses RSSEN and field masks */
        s->regs.vfmrqc = (uint32_t)val & (IXGBE_VFMRQC_RSSEN |
                                          IXGBE_VFMRQC_RSS_FIELD_IPV4 |
                                          IXGBE_VFMRQC_RSS_FIELD_IPV4_TCP |
                                          IXGBE_VFMRQC_RSS_FIELD_IPV6 |
                                          IXGBE_VFMRQC_RSS_FIELD_IPV6_TCP);
        break;
    case IXGBE_VFMAILBOX:
        /* The VF writes mailbox control bits here when obtaining
         * and releasing the mailbox lock, and when sending
         * requests to the PF. We simply store the value so that
         * subsequent reads see the same bits.
         */
        s->regs.vfmailbox = (uint32_t)val;
        break;
    default:
        if (addr >= IXGBE_VFMBMEM &&
            addr < IXGBE_VFMBMEM + IXGBE_VFMAILBOX_SIZE * 4) {
            unsigned idx = (addr - IXGBE_VFMBMEM) >> 2;
            if (idx < IXGBE_VFMAILBOX_SIZE) {
                s->regs.vfmbmem[idx] = (uint32_t)val;
            }
        } else if (addr >= IXGBE_VTIVAR(0) && addr <= IXGBE_VTIVAR_MISC) {
            /* IVAR/IVAR_MISC: allow any value, driver programs mapping */
            (void)val;
        } else if (addr >= IXGBE_VFRDBAL(0) && addr < IXGBE_VFRXMEMWRAP) {
            /* RX ring base/len/head/tail, etc. Accept but ignore for now */
            (void)val;
        } else if (addr >= IXGBE_VFTDBAL(0) && addr < IXGBE_VTEITR(0)) {
            /* TX ring base/len/head/tail, etc. */
            (void)val;
        } else if (addr >= IXGBE_VTEITR(0) &&
                   addr < IXGBE_VTEITR(0) + 4 * MAX_MSIX_Q_VECTORS) {
            /* EITR: driver writes throttle rate; clamp to allowed range */
            uint32_t rate = (uint32_t)val;
            rate &= (IXGBE_MAX_EITR | IXGBE_EITR_CNT_WDIS);
            (void)rate;
        } else if (addr >= IXGBE_VFRSSRK(0) &&
                   addr < IXGBE_VFRETA(0) + 0x100) {
            /* RSS key/indirection table; accept */
            (void)val;
        } else if (addr >= IXGBE_VFSRRCTL(0) &&
                   addr < IXGBE_VFSRRCTL(0) + (0x40 * IXGBEVF_MAX_RSS_QUEUES)) {
            /* SRRCTL per queue: allow driver to program packet/header sizes */
            (void)val;
        } else if (addr >= IXGBE_VFDCA_RXCTRL(0) &&
                   addr < IXGBE_VFDCA_RXCTRL(0) + (0x40 * IXGBEVF_MAX_RSS_QUEUES)) {
            /* RX DCA control: accept TX/RX RRO/WRO bits */
            (void)val;
        } else {
            /* Unhandled register writes are ignored */
            (void)val;
        }
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

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
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
    pci_device_reset(PCI_DEVICE(dev));

    s->regs.vfctrl    = 0;
    s->regs.vfstatus  = 0;
    s->regs.vflinks   = 0;
    s->regs.vffrtimer = 0;
    s->regs.vteicr    = 0;
    s->regs.vteims    = 0;
    s->regs.vteimc    = 0;
    s->regs.vteiac    = 0;
    s->regs.vteiam    = 0;
    s->regs.vfmrqc    = 0;
    s->regs.vfmailbox = 0;
    for (int i = 0; i < IXGBE_VFMAILBOX_SIZE; i++) {
        s->regs.vfmbmem[i] = 0;
    }
    s->intr_status    = 0;
    s->intr_mask      = 0;
    s->status_flags   = 0;
    s->reset_state.pending_reset = false;
    s->pm_state.pm_state = 0;
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  IXGBEVF_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  IXGBEVF_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, IXGBEVF_CLASS_ID);
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
    s->bar_info[0].size  = 128 * 1024;
    s->bar_info[0].name  = "ixgbevf-bar0";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type  = BAR_TYPE_NONE;
        s->bar_info[i].size  = 0;
        s->bar_info[i].name  = NULL;
    }

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msix = true;
    s->has_msi  = true;

    /* Adjusted to QEMU 8.2.10 msix_init_exclusive_bar signature (4 args) */
    if (msix_init_exclusive_bar(pdev, MAX_MSIX_Q_VECTORS + 1, 0, errp) < 0) {
        s->has_msix = false;
    }

    if (!s->has_msix) {
        if (msi_init(pdev, 0, 1, true, false, errp) == 0) {
            s->has_msi = true;
        } else {
            s->has_msi = false;
        }
    }

    pcibase_reset(DEVICE(pdev));

    /* On power up, report link up to simplify driver bring-up */
    s->regs.vfstatus = 0;
    s->regs.vflinks = IXGBE_LINK_SPEED_10GB_FULL;
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
    .name = "ixgbevf_pci",
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
