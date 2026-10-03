/*
 * QEMU model of Brooktree BT848 Multimedia Video Device
 * PCI template with functional implementation for bttv-driver.c
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
/* None needed beyond standard QEMU headers */

#define TYPE_PCIBASE_DEVICE "bttv_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#include "hw/pci/pci_ids.h"

#define PCI_VENDOR_ID_BROOKTREE 0x109e
#define PCI_DEVICE_ID_BT848 0x0350
#define PCI_CLASS_MULTIMEDIA_VIDEO 0x0400

/* Register offsets (from bttv-driver.c) */
#define BT848_DSTATUS          0x000
#define BT848_IFORM            0x004
#define BT848_E_VDELAY_LO      0x010
#define BT848_E_VACTIVE_LO     0x014
#define BT848_E_HDELAY_LO      0x018
#define BT848_E_HACTIVE_LO     0x01C
#define BT848_E_HSCALE_HI      0x020
#define BT848_E_HSCALE_LO      0x024
#define BT848_BRIGHT           0x028
#define BT848_E_CONTROL        0x02C
#define BT848_CONTRAST_LO      0x030
#define BT848_SAT_U_LO         0x034
#define BT848_SAT_V_LO         0x038
#define BT848_HUE              0x03C
#define BT848_E_SCLOOP         0x040
#define BT848_WC_UP            0x044
#define BT848_OFORM            0x048
#define BT848_E_VSCALE_HI      0x04C
#define BT848_E_VSCALE_LO      0x050
#define BT848_ADC              0x068
#define BT848_E_VTC            0x06C
#define BT848_ADELAY           0x060
#define BT848_BDELAY           0x064
#define BT848_WC_DOWN          0x078
#define BT848_TGLB             0x080
#define BT848_TGCTRL           0x084
#define BT848_O_CROP           0x08C
#define BT848_O_VDELAY_LO      0x090
#define BT848_O_CONTROL        0x0AC
#define BT848_O_VSCALE_HI      0x0CC
#define BT848_O_SCLOOP         0x0C0
#define BT848_COLOR_CTL        0x0D8
#define BT848_COLOR_FMT        0x0D4
#define BT848_CAP_CTL          0x0DC
#define BT848_PLL_F_LO         0x0F0
#define BT848_PLL_F_HI         0x0F4
#define BT848_PLL_XCI          0x0F8
#define BT848_DVSIF            0x0FC
#define BT848_INT_STAT         0x100
#define BT848_INT_MASK         0x104
#define BT848_GPIO_DMA_CTL     0x10C
#define BT848_I2C              0x110
#define BT848_RISC_STRT_ADD    0x114
#define BT848_GPIO_OUT_EN      0x118
#define BT848_GPIO_REG_INP     0x11C
#define BT848_RISC_COUNT       0x120
#define BT848_GPIO_DATA        0x200
#define BT848_VTOTAL_LO        0xB0
#define BT848_VTOTAL_HI        0xB4
#define BT848_VBI_PACK_SIZE    0x0E0
#define BT848_VBI_PACK_DEL     0x0E4

/* Bitfield macros */
#define BT848_DSTATUS_PRES     (1<<7)
#define BT848_DSTATUS_HLOC     (1<<6)
#define BT848_DSTATUS_NUML     (1<<4)
#define BT848_DSTATUS_PLOCK    (1<<2)
#define BT848_CONTROL_COMP     (1<<6)
#define BT848_CONTROL_LDEC     (1<<5)
#define BT848_IFORM_NORM       7
#define BT848_IFORM_XTAUTO     (3<<3)
#define BT848_IFORM_XTBOTH     (3<<3)
#define BT848_IFORM_XT1        (2<<3)
#define BT848_IFORM_XT0        (1<<3)
#define BT848_IFORM_AUTO       0
#define BT848_IFORM_NTSC       1
#define BT848_IFORM_NTSC_J     2
#define BT848_IFORM_PAL_BDGHI  3
#define BT848_IFORM_PAL_M      4
#define BT848_IFORM_PAL_N      5
#define BT848_IFORM_SECAM      6
#define BT848_IFORM_PAL_NC     7
#define BT848_OFORM_RANGE      (1<<7)
#define BT848_OFORM_CORE32     (3<<5)
#define BT848_ADC_RESERVED     (2<<6)
#define BT848_ADC_CRUSH        (1<<0)
#define BT848_ADC_AGC_EN       (1<<4)
#define BT848_SCLOOP_CAGC      (1<<6)
#define BT848_SCLOOP_CKILL     (1<<5)
#define BT848_VSCALE_INT       (1<<5)
#define BT848_VSCALE_COMB      (1<<6)
#define BT848_CAP_CTL_CAPTURE_EVEN     (1<<0)
#define BT848_CAP_CTL_CAPTURE_ODD      (1<<1)
#define BT848_CAP_CTL_CAPTURE_VBI_EVEN (1<<2)
#define BT848_CAP_CTL_CAPTURE_VBI_ODD  (1<<3)
#define BT848_INT_VSYNC   (1<<1)
#define BT848_INT_I2CDONE (1<<8)
#define BT848_INT_GPINT   (1<<9)
#define BT848_INT_RISCI   (1<<11)
#define BT848_INT_FDSR    (1<<14)
#define BT848_INT_OCERR   (1<<18)
#define BT848_INT_SCERR   (1<<19)
#define BT848_INT_ETBF    (1<<23)
#define BT848_INT_RACK    (1<<25)
#define BT848_INT_HLOCK   (1<<4)
#define BT848_INT_VPRES   (1<<5)
#define BT848_INT_FMTCHG  (1<<0)
#define BT848_INT_RISCS_VBI   (BT848_RISC_VBI << 28)
#define BT848_INT_RISCS_TOP   (BT848_RISC_TOP << 28)
#define BT848_INT_RISCS_VIDEO (BT848_RISC_VIDEO << 28)
#define BT848_RISC_IRQ         (1U<<24)
#define BT848_RISC_EOL         (1U<<26)
#define BT848_RISC_SOL         (1U<<27)
#define BT848_RISC_SYNC        (0x08U<<28)
#define BT848_RISC_WRITE       (0x01U<<28)
#define BT848_RISC_WRITEC      (0x05U<<28)
#define BT848_RISC_WRITE123    (0x09U<<28)
#define BT848_RISC_WRITE1S23   (0x0bU<<28)
#define BT848_RISC_SKIP        (0x02U<<28)
#define BT848_RISC_SKIP123     (0x0aU<<28)
#define BT848_RISC_JUMP        (0x07U<<28)
#define BT848_RISC_RESYNC      (1<<15)
#define BT848_RISC_TOP 2
#define BT848_RISC_VIDEO 1
#define BT848_RISC_VBI 4
#define BT848_GPIO_DMA_CTL_FIFO_ENABLE (1<<0)
#define BT848_GPIO_DMA_CTL_RISC_ENABLE (1<<1)
#define BT848_GPIO_DMA_CTL_GPINTC      (1<<15)
#define BT848_GPIO_DMA_CTL_GPINTI      (1<<14)
#define BT848_GPIO_DMA_CTL_GPCLKMODE   (1<<10)
#define BT848_GPIO_DMA_CTL_PLTP23_16   (2<<6)
#define BT848_GPIO_DMA_CTL_PLTP1_16    (2<<4)
#define BT848_GPIO_DMA_CTL_PKTP_32     (3<<2)
#define BT848_PLL_X            (1<<7)
#define BT848_FIFO_STATUS_VRE  0x04
#define BT848_FIFO_STATUS_VRO  0x0c
#define BT848_FIFO_STATUS_FM1  0x06
#define BT848_FIFO_STATUS_FM3  0x0e
#define BT848_COLOR_CTL_GAMMA  0x40
#define BT848_COLOR_FMT_RGB32       0x00
#define BT848_COLOR_FMT_RGB24       0x11
#define BT848_COLOR_FMT_RGB16       0x22
#define BT848_COLOR_FMT_RGB15       0x33
#define BT848_COLOR_FMT_YUY2        0x44
#define BT848_COLOR_FMT_Y8          0x66
#define BT848_COLOR_FMT_RGB8        0x77
#define BT848_COLOR_FMT_YCrCb422    0x88
#define BT848_COLOR_FMT_YCrCb411    0x99
#define BT848_COLOR_FMT_RAW         0xee
#define BT878_EN_VSFX 0x04
#define BT878_EN_TBFX 0x02
#define BT878_DEVCTRL 0x40
#define BT878_I2C_MODE         (1<<7)
#define BT878_I2C_NOSTOP       (1<<5)
#define BT878_I2C_NOSTART      (1<<4)
#define BT848_I2C_W3B	       (1<<2)
#define BT848_I2C_SYNC         (1<<3)
#define BT848_I2C_SDA          (1<<0)
#define BT848_I2C_SCL          (1<<1)

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

    /* Hardware Register Shadows (full MMIO space) */
    uint32_t regs[256]; /* 256 * 4 = 1024 bytes, covers 0x000..0x3FF */
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t stat = s->regs[BT848_INT_STAT >> 2];
    uint32_t mask = s->regs[BT848_INT_MASK >> 2];

    if (stat & mask) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* MMIO Read Handler */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: unsupported read size %u at 0x%"HWADDR_PRIx"\n",
                      __func__, size, addr);
        return 0;
    }

    if (addr < 0x1000) {
        uint32_t offset = addr >> 2;
        if (offset < ARRAY_SIZE(s->regs)) {
            val = s->regs[offset];
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: read out of bounds at 0x%"HWADDR_PRIx"\n",
                          __func__, addr);
            return 0xFFFFFFFF;
        }
    } else {
        return 0xFFFFFFFF;
    }

    /* Override DSTATUS to report signal present and horizontal lock */
    if (addr == BT848_DSTATUS) {
        val = BT848_DSTATUS_PRES | BT848_DSTATUS_HLOC | BT848_DSTATUS_NUML;
    }

    return val;
}

/* MMIO Write Handler */
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: unsupported write size %u at 0x%"HWADDR_PRIx"\n",
                      __func__, size, addr);
        return;
    }

    if (addr >= 0x1000) {
        return;
    }

    uint32_t offset = addr >> 2;
    uint32_t *reg = &s->regs[offset];

    switch (addr) {
    case BT848_I2C:
        *reg = (uint32_t)val;
        /* Trigger I2C completion immediately */
        s->regs[BT848_INT_STAT >> 2] |= BT848_INT_I2CDONE;
        pcibase_update_irq(s);
        break;
    case BT848_INT_STAT:
        /* Write-1-to-clear for interrupt status bits */
        *reg &= ~((uint32_t)val);
        pcibase_update_irq(s);
        break;
    case BT848_INT_MASK:
        *reg = (uint32_t)val;
        pcibase_update_irq(s);
        break;
    default:
        *reg = (uint32_t)val;
        break;
    }
}

/* PIO handlers (not used) */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
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

    /* Initialize register defaults */
    memset(s->regs, 0, sizeof(s->regs));

    /* Set DSTATUS to indicate signal presence and horizontal lock */
    s->regs[BT848_DSTATUS >> 2] = BT848_DSTATUS_PRES | BT848_DSTATUS_HLOC | BT848_DSTATUS_NUML;

    /* Clear interrupts */
    s->regs[BT848_INT_STAT >> 2] = 0;
    s->regs[BT848_INT_MASK >> 2] = 0;

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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_BROOKTREE );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_BT848 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_MULTIMEDIA_VIDEO );
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
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x1000;  /* 4K, covering all BT848 registers */
    s->bar_info[0].name = "bttv-mmio";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI/MSI-X not used by driver; skip init */
    s->has_msi = false;
    s->has_msix = false;

    /* Initialize register defaults */
    memset(s->regs, 0, sizeof(s->regs));
    s->regs[BT848_DSTATUS >> 2] = BT848_DSTATUS_PRES | BT848_DSTATUS_HLOC | BT848_DSTATUS_NUML;
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

    /* No extra resources to free */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "bttv_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32_ARRAY(regs, PCIBaseState, 256),
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
