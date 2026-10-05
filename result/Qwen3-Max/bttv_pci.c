/*
 * QEMU BT848/BT878 PCI Video Capture Device
 *
 * Copyright (c) 2025 QEMU Team
 *
 * Based on Linux kernel driver: /linux-6.18/drivers/media/pci/bt8xx/bttv-driver.c
 */

#include "qemu/osdep.h"
#include <inttypes.h>
#include <string.h>
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "sysemu/dma.h"
#include "sysemu/reset.h"
#include "hw/irq.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/pci/pcie.h"
#include "qom/object.h"
#include "qapi/visitor.h"
#include "hw/qdev-properties.h"

#define TYPE_PCIBASE_DEVICE "bttv_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Related register/macros/constants from driver (Stage1) */
#define VENDOR_ID 0x109e
#define DEVICE_ID 0x0350
#define CLASS_ID   PCI_CLASS_MULTIMEDIA_VIDEO

/* Register offsets */
#define BT848_DSTATUS          0x000
#define BT848_IFORM            0x004
#define BT848_E_VDELAY_LO      0x010
#define BT848_E_HDELAY_LO      0x018
#define BT848_E_HACTIVE_LO     0x01C
#define BT848_CONTRAST_LO      0x030
#define BT848_SAT_U_LO         0x034
#define BT848_SAT_V_LO         0x038
#define BT848_HUE              0x03C
#define BT848_BRIGHT           0x028
#define BT848_E_HSCALE_LO      0x024
#define BT848_E_HSCALE_HI      0x020
#define BT848_E_VACTIVE_LO     0x014
#define BT848_E_CROP           0x00C
#define BT848_E_SCLOOP         0x040
#define BT848_WC_UP            0x044
#define BT848_COLOR_FMT        0x0D4
#define BT848_COLOR_CTL        0x0D8
#define BT848_CAP_CTL          0x0DC
#define BT848_E_VSCALE_LO      0x050
#define BT848_E_VSCALE_HI      0x04C
#define BT848_ADELAY           0x060
#define BT848_BDELAY           0x064
#define BT848_ADC              0x068
#define BT848_WC_DOWN          0x078
#define BT848_TGLB             0x080
#define BT848_TGCTRL           0x084
#define BT848_O_CROP           0x08C
#define BT848_O_VDELAY_LO      0x090
#define BT848_O_SCLOOP         0x0C0
#define BT848_O_VSCALE_HI      0x0CC
#define BT848_OFORM            0x048
#define BT848_VTOTAL_LO        0xB0
#define BT848_VTOTAL_HI        0xB4
#define BT848_E_VTC            0x06C
#define BT848_INT_STAT         0x100
#define BT848_INT_MASK         0x104
#define BT848_GPIO_DMA_CTL     0x10C
#define BT848_GPIO_OUT_EN      0x118
#define BT848_GPIO_REG_INP     0x11C
#define BT848_I2C              0x110
#define BT848_RISC_STRT_ADD    0x114
#define BT848_RISC_COUNT       0x120
#define BT848_VBI_PACK_SIZE    0x0E0
#define BT848_VBI_PACK_DEL     0x0E4
#define BT848_PLL_XCI          0x0F8
#define BT848_PLL_F_HI         0x0F4
#define BT848_PLL_F_LO         0x0F0
#define BT848_DVSIF            0x0FC
#define BT848_GPIO_DATA        0x200

/* Interrupt bits */
#define BT848_INT_FMTCHG  (1<<0)
#define BT848_INT_VSYNC   (1<<1)
#define BT848_INT_HLOCK   (1<<4)
#define BT848_INT_VPRES   (1<<5)
#define BT848_INT_I2CDONE (1<<8)
#define BT848_INT_GPINT   (1<<9)
#define BT848_INT_RISCI   (1<<11)
#define BT848_INT_FDSR    (1<<14)
#define BT848_INT_OCERR   (1<<18)
#define BT848_INT_SCERR   (1<<19)
#define BT848_INT_ETBF    (1<<23)
#define BT848_INT_RACK    (1<<25)

/* Control bits */
#define BT848_CAP_CTL_CAPTURE_ODD      (1<<1)
#define BT848_CAP_CTL_CAPTURE_EVEN     (1<<0)
#define BT848_CAP_CTL_CAPTURE_VBI_ODD  (1<<3)
#define BT848_CAP_CTL_CAPTURE_VBI_EVEN (1<<2)
#define BT848_GPIO_DMA_CTL_RISC_ENABLE (1<<1)
#define BT848_GPIO_DMA_CTL_FIFO_ENABLE (1<<0)
#define BT848_GPIO_DMA_CTL_GPINTI      (1<<14)
#define BT848_GPIO_DMA_CTL_GPINTC      (1<<15)
#define BT848_GPIO_DMA_CTL_GPCLKMODE   (1<<10)
#define BT848_GPIO_DMA_CTL_PLTP1_16    (2<<4)
#define BT848_GPIO_DMA_CTL_PLTP23_16   (2<<6)
#define BT848_GPIO_DMA_CTL_PKTP_32     (3<<2)

/* Color formats */
#define BT848_COLOR_FMT_YCrCb422    0x88
#define BT848_COLOR_FMT_RGB24       0x11
#define BT848_COLOR_FMT_RGB8        0x77
#define BT848_COLOR_FMT_RGB16       0x22
#define BT848_COLOR_FMT_Y8          0x66
#define BT848_COLOR_FMT_YCrCb411    0x99
#define BT848_COLOR_FMT_YUY2        0x44
#define BT848_COLOR_FMT_RGB32       0x00
#define BT848_COLOR_FMT_RGB15       0x33
#define BT848_COLOR_FMT_RAW         0xee

/* ------------------------------------------------------------------ */
/* BAR metadata definition                                             */
/* ------------------------------------------------------------------ */
typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM 
} BARType;

typedef struct {
    int index;
    BARType type;
    hwaddr size;
    const char *name;
    bool sparse;
} BARInfo;

/* ------------------------------------------------------------------ */
/* Device State                                                        */
/* ------------------------------------------------------------------ */
struct PCIBaseState {
    PCIDevice parent_obj;

    /* BAR memory regions (MMIO/PIO unified handling fix) */
    MemoryRegion bar_regions[6];

    /* optional linear backing */
    uint8_t *mmio_backing;
    size_t mmio_backing_size;

    /* BAR table */
    BARInfo bar_info[6];
    int num_bars;

    /* interrupt state feature flags */
    bool has_msi;
    bool has_msix;
    
    /* Register shadows */
    uint32_t dstatus;
    uint32_t iform;
    uint32_t e_vdelay_lo;
    uint32_t e_hdelay_lo;
    uint32_t e_hactive_lo;
    uint32_t contrast_lo;
    uint32_t sat_u_lo;
    uint32_t sat_v_lo;
    uint32_t hue;
    uint32_t bright;
    uint32_t e_hscale_lo;
    uint32_t e_hscale_hi;
    uint32_t e_vactive_lo;
    uint32_t e_crop;
    uint32_t e_scloop;
    uint32_t wc_up;
    uint32_t color_fmt;
    uint32_t color_ctl;
    uint32_t cap_ctl;
    uint32_t e_vscale_lo;
    uint32_t e_vscale_hi;
    uint32_t adelay;
    uint32_t bdelay;
    uint32_t adc;
    uint32_t wc_down;
    uint32_t tglb;
    uint32_t tgctrl;
    uint32_t o_crop;
    uint32_t o_vdelay_lo;
    uint32_t o_scloop;
    uint32_t o_vscale_hi;
    uint32_t oform;
    uint32_t vtotal_lo;
    uint32_t vtotal_hi;
    uint32_t e_vtc;
    uint32_t int_stat;
    uint32_t int_mask;
    uint32_t gpio_dma_ctl;
    uint32_t gpio_out_en;
    uint32_t gpio_reg_inp;
    uint32_t i2c;
    uint32_t risc_strt_add;
    uint32_t risc_count;
    uint32_t vbi_pack_size;
    uint32_t vbi_pack_del;
    uint32_t pll_xci;
    uint32_t pll_f_hi;
    uint32_t pll_f_lo;
    uint32_t dvsif;
    uint32_t gpio_data;

    /* Status fields */
    uint32_t irq_status;

    /* reset/probe state */
    bool initialized;
};

/* ------------------------------------------------------------------ */
/* Forward declarations                                                */
/* ------------------------------------------------------------------ */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len);
static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len);
static void pcibase_reset(DeviceState *dev);
static void pcibase_realize(PCIDevice *pdev, Error **errp);
static void pcibase_uninit(PCIDevice *pdev);

/* ------------------------------------------------------------------ */
/* MemoryRegionOps                                                      */
/* ------------------------------------------------------------------ */
static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* ------------------------------------------------------------------ */
/* Helper: register a BAR (MMIO or PIO)                                 */
/* ------------------------------------------------------------------ */
static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE)
        return;

    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    }else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, bi->size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }

}

/* ------------------------------------------------------------------ */
/* MMIO / PIO handlers (device-specific code goes into placeholders)   */
/* ------------------------------------------------------------------ */

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case BT848_DSTATUS:
        return s->dstatus;
    case BT848_IFORM:
        return s->iform;
    case BT848_E_VDELAY_LO:
        return s->e_vdelay_lo;
    case BT848_E_HDELAY_LO:
        return s->e_hdelay_lo;
    case BT848_E_HACTIVE_LO:
        return s->e_hactive_lo;
    case BT848_CONTRAST_LO:
        return s->contrast_lo;
    case BT848_SAT_U_LO:
        return s->sat_u_lo;
    case BT848_SAT_V_LO:
        return s->sat_v_lo;
    case BT848_HUE:
        return s->hue;
    case BT848_BRIGHT:
        return s->bright;
    case BT848_E_HSCALE_LO:
        return s->e_hscale_lo;
    case BT848_E_HSCALE_HI:
        return s->e_hscale_hi;
    case BT848_E_VACTIVE_LO:
        return s->e_vactive_lo;
    case BT848_E_CROP:
        return s->e_crop;
    case BT848_E_SCLOOP:
        return s->e_scloop;
    case BT848_WC_UP:
        return s->wc_up;
    case BT848_COLOR_FMT:
        return s->color_fmt;
    case BT848_COLOR_CTL:
        return s->color_ctl;
    case BT848_CAP_CTL:
        return s->cap_ctl;
    case BT848_E_VSCALE_LO:
        return s->e_vscale_lo;
    case BT848_E_VSCALE_HI:
        return s->e_vscale_hi;
    case BT848_ADELAY:
        return s->adelay;
    case BT848_BDELAY:
        return s->bdelay;
    case BT848_ADC:
        return s->adc;
    case BT848_WC_DOWN:
        return s->wc_down;
    case BT848_TGLB:
        return s->tglb;
    case BT848_TGCTRL:
        return s->tgctrl;
    case BT848_O_CROP:
        return s->o_crop;
    case BT848_O_VDELAY_LO:
        return s->o_vdelay_lo;
    case BT848_O_SCLOOP:
        return s->o_scloop;
    case BT848_O_VSCALE_HI:
        return s->o_vscale_hi;
    case BT848_OFORM:
        return s->oform;
    case BT848_VTOTAL_LO:
        return s->vtotal_lo;
    case BT848_VTOTAL_HI:
        return s->vtotal_hi;
    case BT848_E_VTC:
        return s->e_vtc;
    case BT848_INT_STAT:
        return s->int_stat;
    case BT848_INT_MASK:
        return s->int_mask;
    case BT848_GPIO_DMA_CTL:
        return s->gpio_dma_ctl;
    case BT848_GPIO_OUT_EN:
        return s->gpio_out_en;
    case BT848_GPIO_REG_INP:
        return s->gpio_reg_inp;
    case BT848_I2C:
        return s->i2c;
    case BT848_RISC_STRT_ADD:
        return s->risc_strt_add;
    case BT848_RISC_COUNT:
        return s->risc_count;
    case BT848_VBI_PACK_SIZE:
        return s->vbi_pack_size;
    case BT848_VBI_PACK_DEL:
        return s->vbi_pack_del;
    case BT848_PLL_XCI:
        return s->pll_xci;
    case BT848_PLL_F_HI:
        return s->pll_f_hi;
    case BT848_PLL_F_LO:
        return s->pll_f_lo;
    case BT848_DVSIF:
        return s->dvsif;
    case BT848_GPIO_DATA:
        return s->gpio_data;
    default:
        break;
    }

    qemu_log_mask(LOG_UNIMP, "[bttv_pci] mmio_read addr=%" PRIx64 " size=%u\n", (uint64_t)addr, size);
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case BT848_IFORM:
        s->iform = val;
        break;
    case BT848_E_VDELAY_LO:
        s->e_vdelay_lo = val;
        break;
    case BT848_E_HDELAY_LO:
        s->e_hdelay_lo = val;
        break;
    case BT848_E_HACTIVE_LO:
        s->e_hactive_lo = val;
        break;
    case BT848_CONTRAST_LO:
        s->contrast_lo = val;
        break;
    case BT848_SAT_U_LO:
        s->sat_u_lo = val;
        break;
    case BT848_SAT_V_LO:
        s->sat_v_lo = val;
        break;
    case BT848_HUE:
        s->hue = val;
        break;
    case BT848_BRIGHT:
        s->bright = val;
        break;
    case BT848_E_HSCALE_LO:
        s->e_hscale_lo = val;
        break;
    case BT848_E_HSCALE_HI:
        s->e_hscale_hi = val;
        break;
    case BT848_E_VACTIVE_LO:
        s->e_vactive_lo = val;
        break;
    case BT848_E_CROP:
        s->e_crop = val;
        break;
    case BT848_E_SCLOOP:
        s->e_scloop = val;
        break;
    case BT848_WC_UP:
        s->wc_up = val;
        break;
    case BT848_COLOR_FMT:
        s->color_fmt = val;
        break;
    case BT848_COLOR_CTL:
        s->color_ctl = val;
        break;
    case BT848_CAP_CTL:
        s->cap_ctl = val;
        break;
    case BT848_E_VSCALE_LO:
        s->e_vscale_lo = val;
        break;
    case BT848_E_VSCALE_HI:
        s->e_vscale_hi = val;
        break;
    case BT848_ADELAY:
        s->adelay = val;
        break;
    case BT848_BDELAY:
        s->bdelay = val;
        break;
    case BT848_ADC:
        s->adc = val;
        break;
    case BT848_WC_DOWN:
        s->wc_down = val;
        break;
    case BT848_TGLB:
        s->tglb = val;
        break;
    case BT848_TGCTRL:
        s->tgctrl = val;
        break;
    case BT848_O_CROP:
        s->o_crop = val;
        break;
    case BT848_O_VDELAY_LO:
        s->o_vdelay_lo = val;
        break;
    case BT848_O_SCLOOP:
        s->o_scloop = val;
        break;
    case BT848_O_VSCALE_HI:
        s->o_vscale_hi = val;
        break;
    case BT848_OFORM:
        s->oform = val;
        break;
    case BT848_VTOTAL_LO:
        s->vtotal_lo = val;
        break;
    case BT848_VTOTAL_HI:
        s->vtotal_hi = val;
        break;
    case BT848_E_VTC:
        s->e_vtc = val;
        break;
    case BT848_INT_STAT:
        s->int_stat = val;
        break;
    case BT848_INT_MASK:
        s->int_mask = val;
        break;
    case BT848_GPIO_DMA_CTL:
        s->gpio_dma_ctl = val;
        break;
    case BT848_GPIO_OUT_EN:
        s->gpio_out_en = val;
        break;
    case BT848_I2C:
        s->i2c = val;
        break;
    case BT848_RISC_STRT_ADD:
        s->risc_strt_add = val;
        break;
    case BT848_RISC_COUNT:
        s->risc_count = val;
        break;
    case BT848_VBI_PACK_SIZE:
        s->vbi_pack_size = val;
        break;
    case BT848_VBI_PACK_DEL:
        s->vbi_pack_del = val;
        break;
    case BT848_PLL_XCI:
        s->pll_xci = val;
        break;
    case BT848_PLL_F_HI:
        s->pll_f_hi = val;
        break;
    case BT848_PLL_F_LO:
        s->pll_f_lo = val;
        break;
    case BT848_DVSIF:
        s->dvsif = val;
        break;
    case BT848_GPIO_DATA:
        s->gpio_data = val;
        break;
    default:
        break;
    }

    qemu_log_mask(LOG_UNIMP, "[bttv_pci] mmio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n", (uint64_t)addr, (uint64_t)val, size);
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "[bttv_pci] pio_read addr=%" PRIx64 " size=%u\n", (uint64_t)addr, size);
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "[bttv_pci] pio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n", (uint64_t)addr, (uint64_t)val, size);
}

/* ------------------------------------------------------------------ */
/* Reset       */
/* ------------------------------------------------------------------ */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    /* core resets */
    pci_device_reset(pdev);

    s->dstatus = 0;
    s->iform = 0;
    s->e_vdelay_lo = 0;
    s->e_hdelay_lo = 0;
    s->e_hactive_lo = 0;
    s->contrast_lo = 0;
    s->sat_u_lo = 0;
    s->sat_v_lo = 0;
    s->hue = 0;
    s->bright = 0;
    s->e_hscale_lo = 0;
    s->e_hscale_hi = 0;
    s->e_vactive_lo = 0;
    s->e_crop = 0;
    s->e_scloop = 0;
    s->wc_up = 0;
    s->color_fmt = 0;
    s->color_ctl = 0;
    s->cap_ctl = 0;
    s->e_vscale_lo = 0;
    s->e_vscale_hi = 0;
    s->adelay = 0;
    s->bdelay = 0;
    s->adc = 0;
    s->wc_down = 0;
    s->tglb = 0;
    s->tgctrl = 0;
    s->o_crop = 0;
    s->o_vdelay_lo = 0;
    s->o_scloop = 0;
    s->o_vscale_hi = 0;
    s->oform = 0;
    s->vtotal_lo = 0;
    s->vtotal_hi = 0;
    s->e_vtc = 0;
    s->int_stat = 0;
    s->int_mask = 0;
    s->gpio_dma_ctl = 0;
    s->gpio_out_en = 0;
    s->gpio_reg_inp = 0;
    s->i2c = 0;
    s->risc_strt_add = 0;
    s->risc_count = 0;
    s->vbi_pack_size = 0;
    s->vbi_pack_del = 0;
    s->pll_xci = 0;
    s->pll_f_hi = 0;
    s->pll_f_lo = 0;
    s->dvsif = 0;
    s->gpio_data = 0;
    s->irq_status = 0;
    s->initialized = false;

    if (s->mmio_backing && s->mmio_backing_size)
        memset(s->mmio_backing, 0, s->mmio_backing_size);
}

/* ------------------------------------------------------------------ */
/* DMA initialize (called from realize)                                */
/* ------------------------------------------------------------------ */
static void pcibase_dma_device_realize(PCIDevice *pdev, Error **errp)
{
    (void)pdev;
    (void)errp;
}

/* ------------------------------------------------------------------ */
/* PCI config space access (class-level hooks)                         */
/* ------------------------------------------------------------------ */
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    uint32_t val = pci_default_read_config(pdev, addr, len);

    switch (len) {
        case 1: val &= 0xFF; break;
        case 2: val &= 0xFFFF; break;
        case 4: break;
        default: break;
    }
    return val;
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    if (addr >= PCI_BASE_ADDRESS_0 && addr <= PCI_BASE_ADDRESS_5) {
        pci_default_write_config(pdev, addr, val, len);
        return;
    }
}

/* ------------------------------------------------------------------ */
/* Realize (device init)                                                */
/* ------------------------------------------------------------------ */
static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);
    
    /* Initialize BAR info */
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x400; /* Based on register map up to 0x200 and typical size */
    s->bar_info[0].name = "bttv-mmio";
    s->bar_info[0].sparse = false;
    s->num_bars = 1;

    /* register BARs */
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Interrupt config (placeholders) */
    /* Legacy INTx only - no MSI/MSIX in driver */

    /* DMA init (called here so device can set DMA masks/etc) */
    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) return;

    s->initialized = true;

    qemu_log_mask(LOG_UNIMP, "[bttv_pci] device realized\n");
}

/* ------------------------------------------------------------------ */
/* Uninit/cleanup                                                      */
/* ------------------------------------------------------------------ */
static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (s->has_msix) {
        msix_uninit(pdev,NULL,0);
        s->has_msix = false;
    }
    if (s->has_msi) {
        msi_uninit(pdev);
        s->has_msi = false;
    }

    if (s->mmio_backing) {
        g_free(s->mmio_backing);
        s->mmio_backing = NULL;
    }

    qemu_log_mask(LOG_UNIMP, "[bttv_pci] device uninit\n");
}

/* ------------------------------------------------------------------ */
/* Class init / type registration                                      */
/* ------------------------------------------------------------------ */
static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->config_read  = pcibase_config_read;
    k->config_write = pcibase_config_write;

    k->realize = pcibase_realize;
    k->exit    = pcibase_uninit;
    dc->reset  = pcibase_reset;

    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void pcibase_register_types(void)
{
    static InterfaceInfo interfaces[] = {
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
