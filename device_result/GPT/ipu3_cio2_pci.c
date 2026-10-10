/*
 * QEMU PCI Device Model for Intel IPU3 CIO2 - Behavioral Implementation
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
#include "hw/pci/pci_ids.h"

#define TYPE_PCIBASE_DEVICE "ipu3_cio2_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */

/* PCI and hardware related constants derived from ipu3-cio2 driver */
#define CIO2_PCI_BAR                     0
#define CIO2_PCI_ID                      0x9d32
#define CIO2_REG_CSIRX_BASE              0x000
#define CIO2_REG_IRQCTRL_BASE            0x300
#define CIO2_REG_MIPIBE_BASE             0x100
#define CIO2_REG_GPREG_BASE              0x1000
#define CIO2_REG_CGC                     0x1400
#define CIO2_REG_PBM_ARB_CTRL            0x1460
#define CIO2_REG_PBM_WMCTRL1             0x1464
#define CIO2_REG_PBM_WMCTRL2             0x1468
#define CIO2_REG_LTRCTRL                 0x1480
#define CIO2_REG_LTRVAL23                0x1484
#define CIO2_REG_LTRVAL01                0x1488
#define CIO2_REG_INT_STS                 0x1414
#define CIO2_REG_INT_EN                  0x1420
#define CIO2_REG_INT_EN_EXT_OE           0x1424
#define CIO2_REG_INT_STS_EXT_IE          0x17e4
#define CIO2_REG_INT_EN_EXT_IE           0x17e8
#define CIO2_REG_IRQCTRL_EDGE            (CIO2_REG_IRQCTRL_BASE + 0x00)
#define CIO2_REG_IRQCTRL_MASK            (CIO2_REG_IRQCTRL_BASE + 0x04)
#define CIO2_REG_IRQCTRL_STATUS          (CIO2_REG_IRQCTRL_BASE + 0x08)
#define CIO2_REG_IRQCTRL_CLEAR           (CIO2_REG_IRQCTRL_BASE + 0x0c)
#define CIO2_REG_IRQCTRL_ENABLE          (CIO2_REG_IRQCTRL_BASE + 0x10)
#define CIO2_REG_IRQCTRL_LEVEL_NOT_PULSE (CIO2_REG_IRQCTRL_BASE + 0x14)
#define CIO2_REG_CSIRX_ENABLE            (CIO2_REG_CSIRX_BASE + 0x0)
#define CIO2_REG_CSIRX_NOF_ENABLED_LANES (CIO2_REG_CSIRX_BASE + 0x4)
#define CIO2_REG_CSIRX_STATUS_DLANE_HS   (CIO2_REG_CSIRX_BASE + 0x1c)
#define CIO2_REG_CSIRX_STATUS_DLANE_LP   (CIO2_REG_CSIRX_BASE + 0x20)
#define CIO2_REG_CSIRX_DLY_CNT_TERMEN(lane) \
                (CIO2_REG_CSIRX_BASE + 0x2c + 8 * (lane))
#define CIO2_REG_CSIRX_DLY_CNT_SETTLE(lane) \
                (CIO2_REG_CSIRX_BASE + 0x30 + 8 * (lane))
#define CIO2_REG_CDMABA(n)               (0x1500 + 0x10 * (n))
#define CIO2_REG_CDMAC0(n)               (0x1508 + 0x10 * (n))
#define CIO2_REG_CDMAC1(n)               (0x150c + 0x10 * (n))
#define CIO2_REG_CDMARI(n)               (0x1504 + 0x10 * (n))
#define CIO2_REG_PXM_PXF_FMT_CFG0(n)     (0x1700 + 0x30 * (n))
#define CIO2_REG_PXM_FRF_CFG(n)          (0x1720 + 0x30 * (n))
#define CIO2_REG_PXM_SID2BID0(n)         (0x1724 + 0x30 * (n))
#define CIO2_REG_PIPE_BASE(n)            ((n) * 0x0400)
#define CIO2_REG_MIPIBE_ENABLE           (CIO2_REG_MIPIBE_BASE + 0x0)
#define CIO2_REG_MIPIBE_COMP_FORMAT(vc)  (CIO2_REG_MIPIBE_BASE + 0x8 + 0x4 * (vc))
#define CIO2_REG_MIPIBE_FORCE_RAW8       (CIO2_REG_MIPIBE_BASE + 0x20)
#define CIO2_REG_MIPIBE_GLOBAL_LUT_DISREGARD (CIO2_REG_MIPIBE_BASE + 0x68)
#define CIO2_REG_MIPIBE_SP_LUT_ENTRY(vc) (CIO2_REG_MIPIBE_BASE + 0x74 + 4 * (vc))
#define CIO2_REG_MIPIBE_LP_LUT_ENTRY(m)  (CIO2_REG_MIPIBE_BASE + 0x84 + 4 * (m))
#define CIO2_REG_FB_HPLL_FREQ            (CIO2_REG_GPREG_BASE + 0x08)
#define CIO2_REG_ISCLK_RATIO             (CIO2_REG_GPREG_BASE + 0xc)
#define CIO2_REG_D0I3C                   0x1408

#define CIO2_IRQCTRL_MASK                0x3ffff

#define CIO2_INT_IOIRQ                   BIT(24)
#define CIO2_INT_IOIE                    BIT(22)
#define CIO2_INT_IOOE                    BIT(23)

#define CIO2_INT_IOS_IOLN_SHIFT          12
#define CIO2_INT_IOC_SHIFT               0

#define CIO2_INT_IOC(dma) (1U << ((dma) < 4U ? (dma) : ((dma) >> 1U) + 2U))
#define CIO2_INT_IOC_MASK   (0x7ff << CIO2_INT_IOC_SHIFT)
#define CIO2_INT_IOS_IOLN(dma) (1U << (((dma) >> 1U) + 12U))
#define CIO2_INT_IOS_IOLN_MASK (0x3ff << CIO2_INT_IOS_IOLN_SHIFT)

#define CIO2_INT_EXT_OE_DMAOE_SHIFT      0U
#define CIO2_INT_EXT_OE_OES_SHIFT        24U
#define CIO2_INT_EXT_OE_DMAOE_MASK       0x7ffff
#define CIO2_INT_EXT_OE_OES_MASK         (0xf << CIO2_INT_EXT_OE_OES_SHIFT)

#define CIO2_INT_EN_EXT_IE_MASK          0xffffffff
#define CIO2_INT_EN_EXT_OE_MASK          0x8f0fffff

#define CIO2_INT_EXT_IE_IRQ(n)           (0x80 << (8U * (n)))

#define CIO2_REG_INT_EN_IRQ              (1 << 24)
#define CIO2_INT_IOC(dma)                (1U << ((dma) < 4U ? (dma) : ((dma) >> 1U) + 2U))
#define CIO2_REG_INT_EN_IOS(dma)         (1U << (((dma) >> 1U) + 12U))
#define CIO2_INT_EXT_OE_DMAOE_MASK       0x7ffff

#define CIO2_NUM_PORTS                   4U
#define CIO2_QUEUES                      CIO2_NUM_PORTS
#define CIO2_NUM_DMA_CHAN                20U
#define CIO2_MAX_LOPS                    8
#define CIO2_MAX_BUFFERS                 (PAGE_SIZE / 16 / CIO2_MAX_LOPS)

#define CIO2_IMAGE_MAX_WIDTH             4224U
#define CIO2_IMAGE_MAX_HEIGHT            3136U

#define CIO2_PADS                        2U
#define CIO2_PAD_SINK                    0U
#define CIO2_PAD_SOURCE                  1U

#define CIO2_DEVICE_NAME                 "Intel IPU3 CIO2"
#define CIO2_NAME                        "ipu3-cio2"

#define PCIBASE_VENDOR_ID                PCI_VENDOR_ID_INTEL
#define PCIBASE_DEVICE_ID                CIO2_PCI_ID
#define PCIBASE_CLASS_ID                 PCI_CLASS_MULTIMEDIA_VIDEO

/* In absence of explicit BAR size in the driver source, use 1 MiB as a safe power-of-two placeholder. */
#define CIO2_BAR0_SIZE                   (1 * MiB)


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
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    struct {
        uint32_t cgc;
        uint32_t d0i3c;
        uint32_t int_en;
        uint32_t int_sts;
        uint32_t int_en_ext_oe;
        uint32_t int_sts_ext_ie;
        uint32_t int_en_ext_ie;
        uint32_t irqctrl_edge;
        uint32_t irqctrl_mask;
        uint32_t irqctrl_status;
        uint32_t irqctrl_clear;
        uint32_t irqctrl_enable;
        uint32_t irqctrl_level_not_pulse;
    } regs;

    /* Minimal DMA-related context extracted from driver-visible registers */
    uint32_t cdmaba[CIO2_NUM_DMA_CHAN];
    uint32_t cdmari[CIO2_NUM_DMA_CHAN];
    uint32_t cdmaco[CIO2_NUM_DMA_CHAN];
    uint32_t cdmaco1[CIO2_NUM_DMA_CHAN];
};


/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool level = false;

    /* Basic interrupt enable/disable logic:
     * - Interrupts are enabled when INT_EN has IRQ bit set (bit24).
     * - Raise interrupt if there is any pending status in INT_STS masked
     *   by INT_EN and global mask.
     */
    if (s->regs.int_en & CIO2_REG_INT_EN_IRQ) {
        uint32_t pending = s->regs.int_sts & s->regs.int_en;
        if (pending) {
            level = true;
        }
    }

    if (level) {
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

/* Device-initiated DMA logic based on driver access patterns
 * The driver programs CDMABA/CDMAC0/1 but the actual DMA descriptors and
 * memory layout are not specified in the provided source. Therefore, we
 * only maintain register shadows without performing real DMA transfers.
 */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    (void)s;
    (void)is_write;
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val32 = 0;

    /* Only 32-bit accesses are expected from the driver (readl/writel). */
    if (size != 4) {
        return 0;
    }

    switch (addr) {
    case CIO2_REG_CGC:
        val32 = s->regs.cgc;
        break;
    case CIO2_REG_D0I3C:
        val32 = s->regs.d0i3c;
        break;
    case CIO2_REG_INT_EN:
        val32 = s->regs.int_en;
        break;
    case CIO2_REG_INT_STS:
        val32 = s->regs.int_sts;
        break;
    case CIO2_REG_INT_EN_EXT_OE:
        val32 = s->regs.int_en_ext_oe;
        break;
    case CIO2_REG_INT_STS_EXT_IE:
        val32 = s->regs.int_sts_ext_ie;
        break;
    case CIO2_REG_INT_EN_EXT_IE:
        val32 = s->regs.int_en_ext_ie;
        break;
    case CIO2_REG_IRQCTRL_EDGE:
        val32 = s->regs.irqctrl_edge;
        break;
    case CIO2_REG_IRQCTRL_MASK:
        val32 = s->regs.irqctrl_mask;
        break;
    case CIO2_REG_IRQCTRL_STATUS:
        val32 = s->regs.irqctrl_status;
        break;
    case CIO2_REG_IRQCTRL_CLEAR:
        /* Reading CLEAR register returns 0 in hardware; keep shadow. */
        val32 = 0;
        break;
    case CIO2_REG_IRQCTRL_ENABLE:
        val32 = s->regs.irqctrl_enable;
        break;
    case CIO2_REG_IRQCTRL_LEVEL_NOT_PULSE:
        val32 = s->regs.irqctrl_level_not_pulse;
        break;
    default:
        /* Handle CDMA and per-port IRQ status registers used by the driver. */
        if (addr >= CIO2_REG_CDMABA(0) &&
            addr < CIO2_REG_CDMABA(CIO2_NUM_DMA_CHAN)) {
            int n = (addr - CIO2_REG_CDMABA(0)) / 0x10;
            if (n >= 0 && n < (int)CIO2_NUM_DMA_CHAN &&
                ((addr - CIO2_REG_CDMABA(0)) % 0x10) == 0) {
                val32 = s->cdmaba[n];
                break;
            }
        }
        if (addr >= CIO2_REG_CDMARI(0) &&
            addr < CIO2_REG_CDMARI(CIO2_NUM_DMA_CHAN)) {
            int n = (addr - CIO2_REG_CDMARI(0)) / 0x10;
            if (n >= 0 && n < (int)CIO2_NUM_DMA_CHAN &&
                ((addr - CIO2_REG_CDMARI(0)) % 0x10) == 0) {
                val32 = s->cdmari[n];
                break;
            }
        }
        if (addr >= CIO2_REG_CDMAC0(0) &&
            addr < CIO2_REG_CDMAC0(CIO2_NUM_DMA_CHAN)) {
            int n = (addr - CIO2_REG_CDMAC0(0)) / 0x10;
            if (n >= 0 && n < (int)CIO2_NUM_DMA_CHAN &&
                ((addr - CIO2_REG_CDMAC0(0)) % 0x10) == 0) {
                val32 = s->cdmaco[n];
                break;
            }
        }
        if (addr >= CIO2_REG_CDMAC1(0) &&
            addr < CIO2_REG_CDMAC1(CIO2_NUM_DMA_CHAN)) {
            int n = (addr - CIO2_REG_CDMAC1(0)) / 0x10;
            if (n >= 0 && n < (int)CIO2_NUM_DMA_CHAN &&
                ((addr - CIO2_REG_CDMAC1(0)) % 0x10) == 0) {
                val32 = s->cdmaco1[n];
                break;
            }
        }

        /* Per-pipe IRQCTRL_STATUS is read in cio2_irq_handle_once() */
        if (addr >= CIO2_REG_PIPE_BASE(0) + CIO2_REG_IRQCTRL_STATUS &&
            addr < CIO2_REG_PIPE_BASE(CIO2_NUM_PORTS) + CIO2_REG_IRQCTRL_STATUS) {
            /* Return zero: no CSI-2 errors pending. */
            val32 = 0;
            break;
        }

        /* Default: unimplemented register reads as 0 */
        val32 = 0;
        break;
    }

    return val32;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t v = (uint32_t)val;

    if (size != 4) {
        return;
    }

    switch (addr) {
    case CIO2_REG_CGC:
        s->regs.cgc = v;
        break;
    case CIO2_REG_D0I3C:
        s->regs.d0i3c = v;
        break;
    case CIO2_REG_INT_EN:
        s->regs.int_en = v;
        pcibase_update_irq(s);
        break;
    case CIO2_REG_INT_STS:
        /* W1C: writing '1' clears corresponding bits. */
        s->regs.int_sts &= ~v;
        pcibase_update_irq(s);
        break;
    case CIO2_REG_INT_EN_EXT_OE:
        s->regs.int_en_ext_oe = v;
        break;
    case CIO2_REG_INT_STS_EXT_IE:
        /* W1C for external IE status. */
        s->regs.int_sts_ext_ie &= ~v;
        break;
    case CIO2_REG_INT_EN_EXT_IE:
        s->regs.int_en_ext_ie = v;
        break;
    case CIO2_REG_IRQCTRL_EDGE:
        s->regs.irqctrl_edge = v;
        break;
    case CIO2_REG_IRQCTRL_MASK:
        s->regs.irqctrl_mask = v;
        break;
    case CIO2_REG_IRQCTRL_STATUS:
        /* Status is updated by internal events; writes ignored. */
        break;
    case CIO2_REG_IRQCTRL_CLEAR:
        /* W1C for per-port IRQCTRL status; we simply clear shadow. */
        s->regs.irqctrl_status &= ~v;
        break;
    case CIO2_REG_IRQCTRL_ENABLE:
        s->regs.irqctrl_enable = v;
        break;
    case CIO2_REG_IRQCTRL_LEVEL_NOT_PULSE:
        s->regs.irqctrl_level_not_pulse = v;
        break;
    default:
        /* Handle CDMA and other registers explicitly used by driver */
        if (addr >= CIO2_REG_CDMABA(0) &&
            addr < CIO2_REG_CDMABA(CIO2_NUM_DMA_CHAN)) {
            int n = (addr - CIO2_REG_CDMABA(0)) / 0x10;
            if (n >= 0 && n < (int)CIO2_NUM_DMA_CHAN &&
                ((addr - CIO2_REG_CDMABA(0)) % 0x10) == 0) {
                s->cdmaba[n] = v;
                break;
            }
        }
        if (addr >= CIO2_REG_CDMARI(0) &&
            addr < CIO2_REG_CDMARI(CIO2_NUM_DMA_CHAN)) {
            int n = (addr - CIO2_REG_CDMARI(0)) / 0x10;
            if (n >= 0 && n < (int)CIO2_NUM_DMA_CHAN &&
                ((addr - CIO2_REG_CDMARI(0)) % 0x10) == 0) {
                s->cdmari[n] = v;
                break;
            }
        }
        if (addr >= CIO2_REG_CDMAC0(0) &&
            addr < CIO2_REG_CDMAC0(CIO2_NUM_DMA_CHAN)) {
            int n = (addr - CIO2_REG_CDMAC0(0)) / 0x10;
            if (n >= 0 && n < (int)CIO2_NUM_DMA_CHAN &&
                ((addr - CIO2_REG_CDMAC0(0)) % 0x10) == 0) {
                s->cdmaco[n] = v;
                break;
            }
        }
        if (addr >= CIO2_REG_CDMAC1(0) &&
            addr < CIO2_REG_CDMAC1(CIO2_NUM_DMA_CHAN)) {
            int n = (addr - CIO2_REG_CDMAC1(0)) / 0x10;
            if (n >= 0 && n < (int)CIO2_NUM_DMA_CHAN &&
                ((addr - CIO2_REG_CDMAC1(0)) % 0x10) == 0) {
                s->cdmaco1[n] = v;
                break;
            }
        }

        /* CSIRX and MIPIBE configuration writes are stored nowhere, as
         * their values are not read back in the provided driver code.
         * For completeness, we simply ignore them.
         */
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    (void)s;
    (void)addr;
    (void)size;

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;
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

    /* Initialize register shadows to power-on defaults inferred from driver. */
    s->regs.cgc = 0;
    s->regs.d0i3c = 0;
    s->regs.int_en = 0;
    s->regs.int_sts = 0;
    s->regs.int_en_ext_oe = 0;
    s->regs.int_sts_ext_ie = 0;
    s->regs.int_en_ext_ie = 0;
    s->regs.irqctrl_edge = 0;
    s->regs.irqctrl_mask = 0;
    s->regs.irqctrl_status = 0;
    s->regs.irqctrl_clear = 0;
    s->regs.irqctrl_enable = 0;
    s->regs.irqctrl_level_not_pulse = 0;

    memset(s->cdmaba, 0, sizeof(s->cdmaba));
    memset(s->cdmari, 0, sizeof(s->cdmari));
    memset(s->cdmaco, 0, sizeof(s->cdmaco));
    memset(s->cdmaco1, 0, sizeof(s->cdmaco1));

    /* Clear interrupt outputs. */
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

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0].index = CIO2_PCI_BAR;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = CIO2_BAR0_SIZE;
    s->bar_info[0].name = "ipu3-cio2-mmio";

    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = NULL;
    }

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize registers and internal state */
    pcibase_reset(DEVICE(pdev));

    /* Enable MSI to match pci_enable_msi() in the driver. */
    if (!msi_init(pdev, 0, 1, true, false, errp)) {
        s->has_msi = true;
    } else {
        s->has_msi = false;
    }
    s->has_msix = false;
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

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "ipu3_cio2_pci",
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
