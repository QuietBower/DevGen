/*
 * QEMU 8.2.10 virtual PCI device for Geode LX framebuffer (lxfb)
 * Based on Linux driver drivers/video/fbdev/geode/lxfb_core.c
 * Generated from Stage-1 structural template, implemented behavior placeholders
 * according to visible driver logic.
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
/* None required at this stage */

#define TYPE_PCIBASE_DEVICE "lxfb_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define OUTPUT_PANEL 0x02
#define OUTPUT_CRT   0x01
#define MSR_LX_SPARE_MSR_DIS_VIFO_WM	(1 << 6)
#define MSR_LX_SPARE_MSR_WM_LPEN_OVRD	(1 << 9)
#define DC_DV_CTL_DV_LINE_SIZE		((1 << 10) | (1 << 11))
#define MSR_LX_SPARE_MSR_DIS_INIT_V_PRI	(1 << 7)
#define DC_DISPLAY_CFG_DCEN		(1 << 24)
#define MSR_LX_GLD_MSR_CONFIG_FMT	((1 << 3) | (1 << 4) | (1 << 5))
#define DC_DV_CTL_DV_LINE_SIZE_1K	(0)
#define DC_DISPLAY_CFG_TRUP		(1 << 6)
#define MSR_LX_GLD_MSR_CONFIG_FMT_CRT	(0)
#define DC_GENLK_CTL_ALPHA_FLICK_EN	(1 << 25)
#define MSR_LX_SPARE_MSR_DIS_CFIFO_HGO	(1 << 11)
#define DC_DV_TOP_DV_TOP_EN		(1 << 0)
#define DC_GENERAL_CFG_FDTY		(1 << 17)
#define DC_DISPLAY_CFG_VISL		(1 << 27)
#define DC_GENLK_CTL_FLICK_SEL_MASK	(0x0F << 28)
#define MSR_LX_SPARE_MSR_VFIFO_ARB_SEL	(1 << 10)
#define DC_DV_CTL_DV_LINE_SIZE_8K	((1 << 10) | (1 << 11))
#define MSR_LX_GLD_MSR_CONFIG_FMT_FP	(1 << 3)
#define MSR_LX_GLD_MSR_CONFIG_FPC	(1 << 15)
#define DC_DV_CTL_DV_LINE_SIZE_4K	(1 << 11)
#define DC_GENLK_CTL_FLICK_EN		(1 << 24)
#define DC_DV_CTL_DV_LINE_SIZE_2K	(1 << 10)
#define MSR_LX_SPARE_MSR_LOAD_WM_LPEN_M	(1 << 8)
#define DC_VFILT_COUNT	0x100
#define DC_HFILT_COUNT	0x100
#define VP_COEFF_SIZE	0x1000
#define VP_PAL_COUNT	0x100
#define DC_CLR_KEY_CLR_KEY_EN		(1 << 24)
#define GP_BLT_STATUS_PB		(1 << 0)
#define DC_GENLK_CTL_GENLK_EN		(1 << 18)
#define DC_IRQ_STATUS			(1 << 20)
#define DC_IRQ_MASK			(1 << 0)
#define DC_IRQ_VIP_VSYNC_IRQ_STATUS	(1 << 21)
#define DC_IRQ_VIP_VSYNC_LOSS_IRQ_MASK	(1 << 1)
#define GP_BLT_STATUS_CE		(1 << 4)
#define DC_GENERAL_CFG_VGAE		(1 << 7)
#define VP_DCFG_PWR_SEQ_DELAY		((1 << 17) | (1 << 18) | (1 << 19))
#define FP_PT2_SCRC			(1 << 27)
#define VP_DCFG_PWR_SEQ_DELAY_DEFAULT	(1 << 19)
#define MSR_LX_MSR_PADSEL_TFT_SEL_LOW	0xDFFFFFFF
#define FP_DFC_BC			((1 << 4) | (1 << 5) | (1 << 6))
#define MSR_LX_MSR_PADSEL_TFT_SEL_HIGH	0x0000003F
#define DC_IRQ_FILT_CTL_H_FILT_SEL	(1 << 10)
#define MSR_GLCP_DOTPL_HALFPIX		(1 << 24)
#define DC_DV_CTL_CLEAR_DV_RAM		(1 << 0)

/* Placeholder identifiers for missing static definitions */
#define VENDOR_ID 0x1022
#define DEVICE_ID 0x2081
#define CLASS_ID 0x030000
#define GP_REG_COUNT  (0x50 / 4)   /* 20 */
#define DC_REG_COUNT  (0x90 / 4)   /* 36 */
#define VP_REG_COUNT  (0x138 / 8)   /* 39 */
#define FP_REG_COUNT  (0x68 / 8)   /* 13 */
#define DC_PAL_COUNT   0x104         /* 260 */
#define VP_PAL_COUNT   0x100         /* 256 */
#define HCOEFF_COUNT 0
#define VCOEFF_COUNT 0
#define VP_COEFF_COUNT 0

/* DC_UNLOCK register offset and values (from new driver source) */
#define DC_UNLOCK       0x00
#define DC_UNLOCK_UNLOCK 0x00004758
#define DC_UNLOCK_LOCK   0x00000000

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
    const MemoryRegionOps *ops; /* ops to use (NULL for default) */
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

    /* Hardware Register Shadows */
    uint32_t *gp;
    uint32_t *dc;
    uint64_t *vp;
    uint64_t *fp;
    uint32_t *dc_pal;
    uint32_t *vp_pal;
    uint32_t *hcoeff;
    uint32_t *vcoeff;
    uint32_t *vp_coeff;

    /* Operational Status */
    int powered_down;
    bool dc_unlocked;   /* DC register unlock state */

    /* Power Management state */
    struct {
        uint64_t padsel, dotpll, dfglcfg, dcspare;
    } msr;
};

/* Shared handler implementations (stub until register offsets are defined) */
static uint64_t gp_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = 0;
    if (addr < GP_REG_COUNT * 4) {
        /* return shadowed GP register value */
        val = s->gp[addr / 4];
        qemu_log_mask(LOG_UNIMP, "lxfb: GP read offset 0x%" HWADDR_PRIx " value 0x%x\n", addr, val);
    }
    return val;
}

static void gp_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr < GP_REG_COUNT * 4) {
        uint32_t *reg = &s->gp[addr / 4];
        /* only full 32-bit writes, ignore size for now */
        *reg = (uint32_t)val;
        qemu_log_mask(LOG_UNIMP, "lxfb: GP write offset 0x%" HWADDR_PRIx " value 0x%" PRIx64 "\n", addr, val);
        /* Any side effects? For now, none */
    }
}

static const MemoryRegionOps gp_mmio_ops = {
    .read = gp_mmio_read,
    .write = gp_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl = { .min_access_size = 4, .max_access_size = 4 },
};

static uint64_t dc_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = 0;
    if (addr < DC_REG_COUNT * 4) {
        val = s->dc[addr / 4];
        qemu_log_mask(LOG_UNIMP, "lxfb: DC read offset 0x%" HWADDR_PRIx " value 0x%x\n", addr, val);
    }
    return val;
}

static void dc_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t reg_offset = addr / 4;

    if (addr >= DC_REG_COUNT * 4) {
        return;
    }

    /* Check DC lock state: writes to registers other than UNLOCK require unlocked state */
    if (reg_offset != DC_UNLOCK / 4) {
        if (!s->dc_unlocked) {
            /* Ignore writes when locked */
            qemu_log_mask(LOG_GUEST_ERROR, "lxfb: DC write to offset 0x%" HWADDR_PRIx " while locked\n", addr);
            return;
        }
    }

    /* Handle DC_UNLOCK register */
    if (reg_offset == DC_UNLOCK / 4) {
        if ((uint32_t)val == DC_UNLOCK_UNLOCK) {
            s->dc_unlocked = true;
            qemu_log_mask(LOG_UNIMP, "lxfb: DC unlocked\n");
        } else if ((uint32_t)val == DC_UNLOCK_LOCK) {
            s->dc_unlocked = false;
            qemu_log_mask(LOG_UNIMP, "lxfb: DC locked\n");
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "lxfb: DC unlock write with invalid value 0x%" PRIx64 "\n", val);
        }
        return;
    }

    /* Write to register */
    s->dc[reg_offset] = (uint32_t)val;
    qemu_log_mask(LOG_UNIMP, "lxfb: DC write offset 0x%" HWADDR_PRIx " value 0x%" PRIx64 "\n", addr, val);
}

static const MemoryRegionOps dc_mmio_ops = {
    .read = dc_mmio_read,
    .write = dc_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl = { .min_access_size = 4, .max_access_size = 4 },
};

static uint64_t vp_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    if (addr < VP_REG_COUNT * 8) {
        val = s->vp[addr / 8];
        qemu_log_mask(LOG_UNIMP, "lxfb: VP read offset 0x%" HWADDR_PRIx " value 0x%" PRIx64 "\n", addr, val);
    }
    return val;
}

static void vp_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr < VP_REG_COUNT * 8) {
        uint64_t *reg = &s->vp[addr / 8];
        *reg = val;
        qemu_log_mask(LOG_UNIMP, "lxfb: VP write offset 0x%" HWADDR_PRIx " value 0x%" PRIx64 "\n", addr, val);
    }
}

static const MemoryRegionOps vp_mmio_ops = {
    .read = vp_mmio_read,
    .write = vp_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 8, .max_access_size = 8 },  /* VP registers are 64-bit */
    .impl = { .min_access_size = 8, .max_access_size = 8 },
};

static uint64_t fp_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    if (addr < FP_REG_COUNT * 8) {
        val = s->fp[addr / 8];
        qemu_log_mask(LOG_UNIMP, "lxfb: FP read offset 0x%" HWADDR_PRIx " value 0x%" PRIx64 "\n", addr, val);
    }
    return val;
}

static void fp_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr < FP_REG_COUNT * 8) {
        uint64_t *reg = &s->fp[addr / 8];
        *reg = val;
        qemu_log_mask(LOG_UNIMP, "lxfb: FP write offset 0x%" HWADDR_PRIx " value 0x%" PRIx64 "\n", addr, val);
    }
}

static const MemoryRegionOps fp_mmio_ops = {
    .read = fp_mmio_read,
    .write = fp_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 8, .max_access_size = 8 },  /* FP registers are 64-bit */
    .impl = { .min_access_size = 8, .max_access_size = 8 },
};

/* Default fallback MMIO ops (not used) */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* Should never be called; overridden per BAR */
    abort();
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    abort();
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    /* No PIO BARs in this device; driver uses MMIO */
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    /* No PIO BARs */
}

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* Internal helper for status-triggered signaling - not used, driver does not enable IRQs */
static void pcibase_update_irq(PCIBaseState *s)
{
    /* Placeholder deleted - no IRQ logic visible in driver */
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Reset all register shadows to default state */
    memset(s->gp, 0, GP_REG_COUNT * sizeof(uint32_t));
    memset(s->dc, 0, DC_REG_COUNT * sizeof(uint32_t));
    memset(s->vp, 0, VP_REG_COUNT * sizeof(uint64_t));
    memset(s->fp, 0, FP_REG_COUNT * sizeof(uint64_t));
    memset(s->dc_pal, 0, DC_PAL_COUNT * sizeof(uint32_t));
    memset(s->vp_pal, 0, VP_PAL_COUNT * sizeof(uint32_t));
    /* coeff arrays not allocated if size 0 */
    if (HCOEFF_COUNT) memset(s->hcoeff, 0, HCOEFF_COUNT * sizeof(uint32_t));
    if (VCOEFF_COUNT) memset(s->vcoeff, 0, VCOEFF_COUNT * sizeof(uint32_t));
    if (VP_COEFF_COUNT) memset(s->vp_coeff, 0, VP_COEFF_COUNT * sizeof(uint32_t));

    /* Reset operational state */
    s->powered_down = 0;
    s->dc_unlocked = false;
    memset(&s->msr, 0, sizeof(s->msr));
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }
    
    /* Ensure BAR size is power of 2 */
    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];
    const MemoryRegionOps *ops = bi->ops;

    if (bi->type == BAR_TYPE_MMIO) {
        if (ops == NULL) ops = &pcibase_mmio_ops;
        memory_region_init_io(mr, OBJECT(s), ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        if (ops == NULL) ops = &pcibase_pio_ops;
        memory_region_init_io(mr, OBJECT(s), ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY |
                         PCI_BASE_ADDRESS_MEM_TYPE_64, mr);
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
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

    /* BAR Initialization */
    s->num_bars = 5;  /* Added FP BAR */
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_RAM, .size = 0x1000000, .name = "lxfb-framebuffer", .ops = NULL };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_MMIO, .size = 0x100, .name = "lxfb-gp", .ops = &gp_mmio_ops };
    s->bar_info[2] = (BARInfo){ .index = 2, .type = BAR_TYPE_MMIO, .size = 0x100, .name = "lxfb-dc", .ops = &dc_mmio_ops };
    s->bar_info[3] = (BARInfo){ .index = 3, .type = BAR_TYPE_MMIO, .size = 0x200, .name = "lxfb-vp", .ops = &vp_mmio_ops };
    s->bar_info[4] = (BARInfo){ .index = 4, .type = BAR_TYPE_MMIO, .size = 0x80, .name = "lxfb-fp", .ops = &fp_mmio_ops };  /* 0x68 rounded up to 0x80 */

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Allocate register shadow arrays */
    s->gp = g_new0(uint32_t, GP_REG_COUNT);
    s->dc = g_new0(uint32_t, DC_REG_COUNT);
    s->vp = g_new0(uint64_t, VP_REG_COUNT);
    s->fp = g_new0(uint64_t, FP_REG_COUNT);
    s->dc_pal = g_new0(uint32_t, DC_PAL_COUNT);
    s->vp_pal = g_new0(uint32_t, VP_PAL_COUNT);
    if (HCOEFF_COUNT) s->hcoeff = g_new0(uint32_t, HCOEFF_COUNT);
    if (VCOEFF_COUNT) s->vcoeff = g_new0(uint32_t, VCOEFF_COUNT);
    if (VP_COEFF_COUNT) s->vp_coeff = g_new0(uint32_t, VP_COEFF_COUNT);

    /* Final state initialization before the device is 'live' */
    s->powered_down = 0;
    s->dc_unlocked = false;
    memset(&s->msr, 0, sizeof(s->msr));

    /* No MSI/MSI-X */
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

    /* Free allocated register arrays */
    g_free(s->gp);
    g_free(s->dc);
    g_free(s->vp);
    g_free(s->fp);
    g_free(s->dc_pal);
    g_free(s->vp_pal);
    g_free(s->hcoeff);
    g_free(s->vcoeff);
    g_free(s->vp_coeff);
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "lxfb_pci",
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
