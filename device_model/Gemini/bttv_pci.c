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



#define TYPE_PCIBASE_DEVICE "bttv_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_BROOKTREE 0x109e
#define PCI_DEVICE_ID_BT848     0x350
#define PCI_CLASS_ID_BT848      0x0400

#define BT848_DSTATUS          0x000
#define BT848_IFORM            0x004
#define BT848_E_CROP           0x00C
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
#define BT848_ADELAY           0x060
#define BT848_BDELAY           0x064
#define BT848_ADC              0x068
#define BT848_E_VTC            0x06C
#define BT848_WC_DOWN          0x078
#define BT848_TGLB             0x080
#define BT848_TGCTRL           0x084
#define BT848_O_CROP           0x08C
#define BT848_O_VDELAY_LO      0x090
#define BT848_O_CONTROL        0x0AC
#define BT848_VTOTAL_LO        0xB0
#define BT848_VTOTAL_HI        0xB4
#define BT848_O_SCLOOP         0x0C0
#define BT848_O_VSCALE_HI      0x0CC
#define BT848_COLOR_FMT        0x0D4
#define BT848_COLOR_CTL        0x0D8
#define BT848_CAP_CTL          0x0DC
#define BT848_VBI_PACK_SIZE    0x0E0
#define BT848_VBI_PACK_DEL     0x0E4
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

#define BT848_PLL_X            (1<<7)
#define BT848_INT_VSYNC        (1<<1)
#define BT848_INT_GPINT        (1<<9)
#define BT848_INT_SCERR        (1<<19)
#define BT848_INT_FDSR         (1<<14)
#define BT848_INT_RISCI        (1<<11)
#define BT848_INT_OCERR        (1<<18)
#define BT848_INT_FMTCHG       (1<<0)
#define BT848_INT_HLOCK        (1<<4)
#define BT848_INT_I2CDONE      (1<<8)
#define BT848_INT_VPRES        (1<<5)
#define BT848_RISC_VIDEO       1
#define BT848_RISC_VBI         4
#define BT848_RISC_TOP         2
#define BT848_INT_RISCS_VBI    (BT848_RISC_VBI << 28)
#define BT848_INT_RISCS_TOP    (BT848_RISC_TOP << 28)
#define BT848_INT_RISCS_VIDEO  (BT848_RISC_VIDEO << 28)
#define BT848_DSTATUS_HLOC     (1<<6)
#define BT848_DSTATUS_PRES     (1<<7)
#define BT848_DSTATUS_NUML     (1<<4)
#define BT848_DSTATUS_PLOCK    (1<<2)
#define BT848_COLOR_CTL_GAMMA  (1<<4)
#define BT848_IFORM_XTAUTO     (3<<3)
#define BT848_IFORM_AUTO       0
#define BT848_IFORM_NORM       7
#define BT848_IFORM_XTBOTH     (3<<3)
#define BT848_GPIO_DMA_CTL_PKTP_32     (3<<2)
#define BT848_GPIO_DMA_CTL_PLTP1_16    (2<<4)
#define BT848_GPIO_DMA_CTL_PLTP23_16   (2<<6)
#define BT848_GPIO_DMA_CTL_GPINTC      (1<<15)
#define BT848_GPIO_DMA_CTL_GPINTI      (1<<14)
#define BT848_CONTROL_COMP     (1<<6)
#define BT848_CONTROL_LDEC     (1<<5)
#define BT848_SCLOOP_CKILL     (1<<5)
#define BT848_SCLOOP_CAGC      (1<<6)
#define BT848_ADC_RESERVED     (2<<6)
#define BT848_ADC_CRUSH        (1<<0)
#define BT848_OFORM_RANGE      (1<<7)
#define BT848_OFORM_CORE32     (3<<5)
#define BT848_CAP_CTL_CAPTURE_ODD      (1<<1)
#define BT848_CAP_CTL_CAPTURE_EVEN     (1<<0)
#define BT848_RISC_WRITE       (0x01U<<28)
#define BT848_RISC_SKIP        (0x02U<<28)
#define BT848_RISC_WRITEC      (0x05U<<28)
#define BT848_RISC_JUMP        (0x07U<<28)
#define BT848_RISC_SYNC        (0x08U<<28)
#define BT848_RISC_WRITE123    (0x09U<<28)
#define BT848_RISC_SKIP123     (0x0aU<<28)
#define BT848_RISC_WRITE1S23   (0x0bU<<28)
#define RISC_SLOT_O_VBI        4
#define RISC_SLOT_O_FIELD      6

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
    uint32_t int_stat;
    uint32_t int_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t dstatus;
    uint32_t iform;
    uint32_t e_crop;
    uint32_t e_vdelay_lo;
    uint32_t e_vactive_lo;
    uint32_t e_hdelay_lo;
    uint32_t e_hactive_lo;
    uint32_t e_hscale_hi;
    uint32_t e_hscale_lo;
    uint32_t bright;
    uint32_t e_control;
    uint32_t contrast_lo;
    uint32_t sat_u_lo;
    uint32_t sat_v_lo;
    uint32_t hue;
    uint32_t e_scloop;
    uint32_t wc_up;
    uint32_t oform;
    uint32_t e_vscale_hi;
    uint32_t e_vscale_lo;
    uint32_t adelay;
    uint32_t bdelay;
    uint32_t adc;
    uint32_t e_vtc;
    uint32_t wc_down;
    uint32_t tglb;
    uint32_t tgctrl;
    uint32_t o_crop;
    uint32_t o_vdelay_lo;
    uint32_t o_control;
    uint32_t vtotal_lo;
    uint32_t vtotal_hi;
    uint32_t o_scloop;
    uint32_t o_vscale_hi;
    uint32_t color_fmt;
    uint32_t color_ctl;
    uint32_t cap_ctl;
    uint32_t vbi_pack_size;
    uint32_t vbi_pack_del;
    uint32_t pll_f_lo;
    uint32_t pll_f_hi;
    uint32_t pll_xci;
    uint32_t dvsif;
    uint32_t gpio_dma_ctl;
    uint32_t i2c;
    uint32_t risc_strt_add;
    uint32_t gpio_out_en;
    uint32_t gpio_reg_inp;
    uint32_t risc_count;
    uint32_t gpio_data;

    /* DMA Context */
    uint32_t risc_strt_add_reg;
    uint32_t risc_count_reg;

    uint32_t pm_state;
    QEMUTimer *timeout_timer;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool level = (s->int_stat & s->int_mask) != 0;
    if (s->has_msix) {
        if (level) {
            msix_notify(pdev, 0);
        }
    } else if (s->has_msi) {
        if (level) {
            msi_notify(pdev, 0);
        }
    } else {
        pci_set_irq(pdev, level);
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case BT848_DSTATUS: val = s->dstatus; break;
    case BT848_IFORM: val = s->iform; break;
    case BT848_E_CROP: val = s->e_crop; break;
    case BT848_E_VDELAY_LO: val = s->e_vdelay_lo; break;
    case BT848_E_VACTIVE_LO: val = s->e_vactive_lo; break;
    case BT848_E_HDELAY_LO: val = s->e_hdelay_lo; break;
    case BT848_E_HACTIVE_LO: val = s->e_hactive_lo; break;
    case BT848_E_HSCALE_HI: val = s->e_hscale_hi; break;
    case BT848_E_HSCALE_LO: val = s->e_hscale_lo; break;
    case BT848_BRIGHT: val = s->bright; break;
    case BT848_E_CONTROL: val = s->e_control; break;
    case BT848_CONTRAST_LO: val = s->contrast_lo; break;
    case BT848_SAT_U_LO: val = s->sat_u_lo; break;
    case BT848_SAT_V_LO: val = s->sat_v_lo; break;
    case BT848_HUE: val = s->hue; break;
    case BT848_E_SCLOOP: val = s->e_scloop; break;
    case BT848_WC_UP: val = s->wc_up; break;
    case BT848_OFORM: val = s->oform; break;
    case BT848_E_VSCALE_HI: val = s->e_vscale_hi; break;
    case BT848_E_VSCALE_LO: val = s->e_vscale_lo; break;
    case BT848_ADELAY: val = s->adelay; break;
    case BT848_BDELAY: val = s->bdelay; break;
    case BT848_ADC: val = s->adc; break;
    case BT848_E_VTC: val = s->e_vtc; break;
    case BT848_WC_DOWN: val = s->wc_down; break;
    case BT848_TGLB: val = s->tglb; break;
    case BT848_TGCTRL: val = s->tgctrl; break;
    case BT848_O_CROP: val = s->o_crop; break;
    case BT848_O_VDELAY_LO: val = s->o_vdelay_lo; break;
    case BT848_O_CONTROL: val = s->o_control; break;
    case BT848_VTOTAL_LO: val = s->vtotal_lo; break;
    case BT848_VTOTAL_HI: val = s->vtotal_hi; break;
    case BT848_O_SCLOOP: val = s->o_scloop; break;
    case BT848_O_VSCALE_HI: val = s->o_vscale_hi; break;
    case BT848_COLOR_FMT: val = s->color_fmt; break;
    case BT848_COLOR_CTL: val = s->color_ctl; break;
    case BT848_CAP_CTL: val = s->cap_ctl; break;
    case BT848_VBI_PACK_SIZE: val = s->vbi_pack_size; break;
    case BT848_VBI_PACK_DEL: val = s->vbi_pack_del; break;
    case BT848_PLL_F_LO: val = s->pll_f_lo; break;
    case BT848_PLL_F_HI: val = s->pll_f_hi; break;
    case BT848_PLL_XCI: val = s->pll_xci; break;
    case BT848_DVSIF: val = s->dvsif; break;
    case BT848_INT_STAT: val = s->int_stat; break;
    case BT848_INT_MASK: val = s->int_mask; break;
    case BT848_GPIO_DMA_CTL: val = s->gpio_dma_ctl; break;
    case BT848_I2C: val = s->i2c; break;
    case BT848_RISC_STRT_ADD: val = s->risc_strt_add; break;
    case BT848_GPIO_OUT_EN: val = s->gpio_out_en; break;
    case BT848_GPIO_REG_INP: val = s->gpio_reg_inp; break;
    case BT848_RISC_COUNT: val = s->risc_count; break;
    case BT848_GPIO_DATA: val = s->gpio_data; break;
    default:
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case BT848_DSTATUS: s->dstatus = val; break;
    case BT848_IFORM: s->iform = val; break;
    case BT848_E_CROP: s->e_crop = val; break;
    case BT848_E_VDELAY_LO: s->e_vdelay_lo = val; break;
    case BT848_E_VACTIVE_LO: s->e_vactive_lo = val; break;
    case BT848_E_HDELAY_LO: s->e_hdelay_lo = val; break;
    case BT848_E_HACTIVE_LO: s->e_hactive_lo = val; break;
    case BT848_E_HSCALE_HI: s->e_hscale_hi = val; break;
    case BT848_E_HSCALE_LO: s->e_hscale_lo = val; break;
    case BT848_BRIGHT: s->bright = val; break;
    case BT848_E_CONTROL: s->e_control = val; break;
    case BT848_CONTRAST_LO: s->contrast_lo = val; break;
    case BT848_SAT_U_LO: s->sat_u_lo = val; break;
    case BT848_SAT_V_LO: s->sat_v_lo = val; break;
    case BT848_HUE: s->hue = val; break;
    case BT848_E_SCLOOP: s->e_scloop = val; break;
    case BT848_WC_UP: s->wc_up = val; break;
    case BT848_OFORM: s->oform = val; break;
    case BT848_E_VSCALE_HI: s->e_vscale_hi = val; break;
    case BT848_E_VSCALE_LO: s->e_vscale_lo = val; break;
    case BT848_ADELAY: s->adelay = val; break;
    case BT848_BDELAY: s->bdelay = val; break;
    case BT848_ADC: s->adc = val; break;
    case BT848_E_VTC: s->e_vtc = val; break;
    case BT848_WC_DOWN: s->wc_down = val; break;
    case BT848_TGLB: s->tglb = val; break;
    case BT848_TGCTRL: s->tgctrl = val; break;
    case BT848_O_CROP: s->o_crop = val; break;
    case BT848_O_VDELAY_LO: s->o_vdelay_lo = val; break;
    case BT848_O_CONTROL: s->o_control = val; break;
    case BT848_VTOTAL_LO: s->vtotal_lo = val; break;
    case BT848_VTOTAL_HI: s->vtotal_hi = val; break;
    case BT848_O_SCLOOP: s->o_scloop = val; break;
    case BT848_O_VSCALE_HI: s->o_vscale_hi = val; break;
    case BT848_COLOR_FMT: s->color_fmt = val; break;
    case BT848_COLOR_CTL: s->color_ctl = val; break;
    case BT848_CAP_CTL: s->cap_ctl = val; break;
    case BT848_VBI_PACK_SIZE: s->vbi_pack_size = val; break;
    case BT848_VBI_PACK_DEL: s->vbi_pack_del = val; break;
    case BT848_PLL_F_LO: s->pll_f_lo = val; break;
    case BT848_PLL_F_HI: s->pll_f_hi = val; break;
    case BT848_PLL_XCI: s->pll_xci = val; break;
    case BT848_DVSIF: s->dvsif = val; break;
    case BT848_INT_STAT: 
        s->int_stat &= ~val; /* W1C */
        pcibase_update_irq(s);
        break;
    case BT848_INT_MASK: 
        s->int_mask = val; 
        pcibase_update_irq(s);
        break;
    case BT848_GPIO_DMA_CTL: s->gpio_dma_ctl = val; break;
    case BT848_I2C: s->i2c = val; break;
    case BT848_RISC_STRT_ADD: s->risc_strt_add = val; break;
    case BT848_GPIO_OUT_EN: s->gpio_out_en = val; break;
    case BT848_GPIO_REG_INP: s->gpio_reg_inp = val; break;
    case BT848_RISC_COUNT: s->risc_count = val; break;
    case BT848_GPIO_DATA: s->gpio_data = val; break;
    default:
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
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

    s->dstatus = 0;
    s->iform = 0;
    s->e_crop = 0;
    s->e_vdelay_lo = 0;
    s->e_vactive_lo = 0;
    s->e_hdelay_lo = 0;
    s->e_hactive_lo = 0;
    s->e_hscale_hi = 0;
    s->e_hscale_lo = 0;
    s->bright = 0;
    s->e_control = 0;
    s->contrast_lo = 0;
    s->sat_u_lo = 0;
    s->sat_v_lo = 0;
    s->hue = 0;
    s->e_scloop = 0;
    s->wc_up = 0;
    s->oform = 0;
    s->e_vscale_hi = 0;
    s->e_vscale_lo = 0;
    s->adelay = 0;
    s->bdelay = 0;
    s->adc = 0;
    s->e_vtc = 0;
    s->wc_down = 0;
    s->tglb = 0;
    s->tgctrl = 0;
    s->o_crop = 0;
    s->o_vdelay_lo = 0;
    s->o_control = 0;
    s->vtotal_lo = 0;
    s->vtotal_hi = 0;
    s->o_scloop = 0;
    s->o_vscale_hi = 0;
    s->color_fmt = 0;
    s->color_ctl = 0;
    s->cap_ctl = 0;
    s->vbi_pack_size = 0;
    s->vbi_pack_del = 0;
    s->pll_f_lo = 0;
    s->pll_f_hi = 0;
    s->pll_xci = 0;
    s->dvsif = 0;
    s->int_stat = 0;
    s->int_mask = 0;
    s->gpio_dma_ctl = 0;
    s->i2c = 0;
    s->risc_strt_add = 0;
    s->gpio_out_en = 0;
    s->gpio_reg_inp = 0;
    s->risc_count = 0;
    s->gpio_data = 0;
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
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0400 );
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
    s->bar_info[0].size = 0x1000;
    s->bar_info[0].name = "bt848-mmio";  
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
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
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "bttv_pci",
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
