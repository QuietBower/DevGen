/*
 * Generic QEMU PCI device template (QEMU 8.2.x)
 *
 * Phase 1: fill register macros, BAR list, BAR sizes, structs, enums.
 * Phase 2: implement MMIO/PIO read/write, DMA init, IRQ init, reset, config hooks
 * Phase 3 (Repair Phase)：correct syntax, types, includes, missing symbols, remove dead code
 * Phase 4 (Debug & Update Phase)：Based on actual kernel logs from QEMU boot, repair the virtual hardware behavior
 *
 * Replace #PLACEHOLDER# blocks with device-specific code/data.
 * NOTE:
 * - Only essential fixes added (TYPE macro, PIO BAR handling, MMIO impl, DMA init, IRQ).
 * - No optional or advanced features added.
 * - Do not directly fill this file with the Linux kernel source code
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

/* ------------------------------------------------------------------ */
/* Additional include files (Stage1 optional)                          */
/* ------------------------------------------------------------------ */


#define TYPE_PCIBASE_DEVICE "bttv"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Related register/macros/constants from driver (Stage1) */
/* Extracted from bttv-driver.c */
#define PCI_VENDOR_ID_BROOKTREE 0x109e
#define PCI_DEVICE_ID_BT848     0x350
#define PCI_DEVICE_ID_BT849     0x351
#define PCI_DEVICE_ID_BT878     0x36e
#define PCI_DEVICE_ID_BT879     0x36f
#define PCI_DEVICE_ID_FUSION879 0x36c

/* Register Offsets */
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

/* Bit definitions */
#define BT848_INT_FMTCHG       (1<<0)
#define BT848_INT_VSYNC        (1<<1)
#define BT848_INT_HLOCK        (1<<4)
#define BT848_INT_I2CDONE      (1<<8)
#define BT848_INT_GPINT        (1<<9)
#define BT848_INT_RISCI        (1<<11)
#define BT848_INT_FDSR         (1<<14)
#define BT848_INT_OCERR        (1<<18)
#define BT848_INT_SCERR        (1<<19)
#define BT848_INT_ETBF         (1<<23)
#define BT848_INT_RACK         (1<<25)
#define BT848_INT_RISCS_VIDEO  (BT848_RISC_VIDEO << 28)
#define BT848_INT_RISCS_TOP    (BT848_RISC_TOP << 28)
#define BT848_INT_RISCS_VBI    (BT848_RISC_VBI << 28)

#define BT848_DSTATUS_PLOCK    (1<<2)
#define BT848_DSTATUS_NUML     (1<<4)
#define BT848_DSTATUS_HLOC     (1<<6)
#define BT848_DSTATUS_PRES     (1<<7)

#define BT848_CONTROL_LDEC     (1<<5)
#define BT848_CONTROL_COMP     (1<<6)

#define BT848_CAP_CTL_CAPTURE_EVEN     (1<<0)
#define BT848_CAP_CTL_CAPTURE_ODD      (1<<1)
#define BT848_CAP_CTL_CAPTURE_VBI_EVEN (1<<2)
#define BT848_CAP_CTL_CAPTURE_VBI_ODD  (1<<3)

#define BT848_RISC_VIDEO       1
#define BT848_RISC_TOP         2
#define BT848_RISC_VBI         4

/* RISC instructions */
#define BT848_RISC_WRITE       (0x01U<<28)
#define BT848_RISC_WRITEC      (0x05U<<28)
#define BT848_RISC_JUMP        (0x07U<<28)
#define BT848_RISC_SYNC        (0x08U<<28)
#define BT848_RISC_WRITE123    (0x09U<<28)
#define BT848_RISC_WRITE1S23   (0x0bU<<28)
#define BT848_RISC_SKIP        (0x02U<<28)
#define BT848_RISC_SKIP123     (0x0aU<<28)
#define BT848_RISC_IRQ         (1U<<24)
#define BT848_RISC_SOL         (1U<<27)
#define BT848_RISC_EOL         (1U<<26)


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
    
    /* Registers */
    uint32_t dstatus;
    uint32_t int_stat;
    uint32_t int_mask;
    uint32_t gpio_dma_ctl;
    uint32_t gpio_out_en;
    uint32_t gpio_reg_inp;
    uint32_t gpio_data;
    uint32_t risc_strt_add;
    uint32_t risc_count;
    uint32_t cap_ctl;
    uint32_t color_ctl;
    uint32_t iform;
    uint32_t bright;
    uint32_t contrast_lo;
    uint32_t hue;
    uint32_t sat_u_lo;
    uint32_t sat_v_lo;
    uint32_t e_vscale_hi;
    uint32_t o_vscale_hi;

    /* Timer for VSYNC/RISC simulation */
    QEMUTimer *timer;
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

static void bttv_update_irq(PCIBaseState *s) {
    int level = (s->int_stat & s->int_mask) ? 1 : 0;
    pci_set_irq(PCI_DEVICE(s), level);
}

static void bttv_timer_cb(void *opaque) {
    PCIBaseState *s = opaque;
    
    /* Simulate VSYNC */
    s->int_stat |= BT848_INT_VSYNC;
    
    /* Simulate RISC activity if started */
    if (s->risc_strt_add) {
        s->int_stat |= BT848_INT_RISCI;
        /* Alternate fields or just set video present */
        s->int_stat |= BT848_INT_RISCS_VIDEO;
        s->risc_count += 0x40; /* Advance counter */
    }
    
    bttv_update_irq(s);
    
    /* Reschedule for ~60Hz (16ms) */
    timer_mod(s->timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 16);
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
        case BT848_DSTATUS:
            val = s->dstatus;
            break;
        case BT848_INT_STAT:
            val = s->int_stat;
            break;
        case BT848_INT_MASK:
            val = s->int_mask;
            break;
        case BT848_GPIO_DMA_CTL:
            val = s->gpio_dma_ctl;
            break;
        case BT848_GPIO_OUT_EN:
            val = s->gpio_out_en;
            break;
        case BT848_GPIO_REG_INP:
            val = s->gpio_reg_inp;
            break;
        case BT848_GPIO_DATA:
            val = s->gpio_data;
            break;
        case BT848_RISC_STRT_ADD:
            val = s->risc_strt_add;
            break;
        case BT848_RISC_COUNT:
            val = s->risc_count;
            break;
        case BT848_IFORM:
            val = s->iform;
            break;
        case BT848_CAP_CTL:
            val = s->cap_ctl;
            break;
        default:
            break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
        case BT848_INT_STAT:
            /* W1C */
            s->int_stat &= ~val;
            bttv_update_irq(s);
            break;
        case BT848_INT_MASK:
            s->int_mask = val;
            bttv_update_irq(s);
            break;
        case BT848_GPIO_DMA_CTL:
            s->gpio_dma_ctl = val;
            break;
        case BT848_GPIO_OUT_EN:
            s->gpio_out_en = val;
            break;
        case BT848_GPIO_REG_INP:
            s->gpio_reg_inp = val;
            break;
        case BT848_GPIO_DATA:
            s->gpio_data = val;
            break;
        case BT848_RISC_STRT_ADD:
            s->risc_strt_add = val;
            s->risc_count = val; /* Reset count on new start */
            break;
        case BT848_IFORM:
            s->iform = val;
            break;
        case BT848_CAP_CTL:
            s->cap_ctl = val;
            break;
        case BT848_COLOR_CTL:
            s->color_ctl = val;
            break;
        case BT848_BRIGHT:
            s->bright = val;
            break;
        case BT848_CONTRAST_LO:
            s->contrast_lo = val;
            break;
        case BT848_HUE:
            s->hue = val;
            break;
        case BT848_SAT_U_LO:
            s->sat_u_lo = val;
            break;
        case BT848_SAT_V_LO:
            s->sat_v_lo = val;
            break;
        case BT848_E_VSCALE_HI:
            s->e_vscale_hi = val;
            break;
        case BT848_O_VSCALE_HI:
            s->o_vscale_hi = val;
            break;
        case BT848_I2C:
            /* Trigger I2C done interrupt immediately */
            s->int_stat |= BT848_INT_I2CDONE;
            bttv_update_irq(s);
            break;
        default:
            break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    (void)opaque;

    qemu_log_mask(LOG_UNIMP, "[%s] pio_read addr=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    (void)opaque;

    qemu_log_mask(LOG_UNIMP, "[%s] pio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, (uint64_t)val, size);
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

    s->int_stat = 0;
    s->int_mask = 0;
    /* Signal present, Locked, 625 lines, PLL locked */
    s->dstatus = BT848_DSTATUS_PRES | BT848_DSTATUS_HLOC | BT848_DSTATUS_NUML | BT848_DSTATUS_PLOCK;
    s->risc_strt_add = 0;
    s->risc_count = 0;
    s->gpio_out_en = 0;
    s->gpio_data = 0;
    
    bttv_update_irq(s);
}

/* ------------------------------------------------------------------ */
/* DMA initialize (called from realize)                                */
/* ------------------------------------------------------------------ */
static void pcibase_dma_device_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    
    (void)s;
    (void)errp;
}

/* ------------------------------------------------------------------ */
/* PCI config space access (class-level hooks)                         */
/* ------------------------------------------------------------------ */
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    uint32_t val = pci_default_read_config(pdev, addr, len);
    /* optional: override certain config offsets if needed */
    

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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_BROOKTREE );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_BT848 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0400 ); /* Multimedia Video */
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_set_byte(pci_conf + PCI_LATENCY_TIMER, 32);
    pci_config_set_interrupt_pin(pci_conf, 1);
    
    /* Init timer */
    s->timer = timer_new_ms(QEMU_CLOCK_VIRTUAL, bttv_timer_cb, s);
    timer_mod(s->timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 100);

    /* Setup BAR 0 */
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x1000;
    s->bar_info[0].name = "bttv-mmio";
    s->num_bars = 1;

    /* register BARs */
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Interrupt config (placeholders) */
    

    /* DMA init (called here so device can set DMA masks/etc) */
    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) return;

    

    /* Example MSI:
     *   if (msi_init(pdev, 0, 1, true, false, errp) == 0) { s->has_msi = true; }
     * Example MSIX:
     *   if (msix_init(pdev, nvec, table_mem, table_bar_nr) == 0) { s->has_msix = true; }
     */

    /* optional: map legacy INTx behavior if driver expects it */
    

    

    /* Power management / other init */
    
    /* Example:
    *   pdev->pm_cap = true;
    */
    qemu_log_mask(LOG_UNIMP, "[%s] device realized\n", TYPE_PCIBASE_DEVICE);
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

    if (s->timer) {
        timer_del(s->timer);
        timer_free(s->timer);
        s->timer = NULL;
    }

    if (s->mmio_backing) {
        g_free(s->mmio_backing);
        s->mmio_backing = NULL;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] device uninit\n", TYPE_PCIBASE_DEVICE);
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

    /* optional */
    
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

    /* Alias for command line compatibility */
    static const TypeInfo bttv_pci_info = {
        .name = "bttv_pci",
        .parent = TYPE_PCIBASE_DEVICE,
        .instance_size = sizeof(PCIBaseState),
    };

    type_register_static(&pcibase_info);
    type_register_static(&bttv_pci_info);
}

type_init(pcibase_register_types);
