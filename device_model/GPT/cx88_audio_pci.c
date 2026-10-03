/*
 * QEMU PCI device model for cx88-alsa (cx88_audio)
 *
 * Phase 2: minimal behavior strictly derived from cx88-alsa.c and
 * shared core helpers used by cx88-alsa.
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

#define TYPE_PCIBASE_DEVICE "cx88_audio_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define CX88_AUDIO_VENDOR_ID 0x14f1
#define CX88_AUDIO_DEVICE_ID 0x8801
#define CX88_AUDIO_CLASS_ID  PCI_CLASS_MULTIMEDIA_AUDIO

#define MAX_IRQ_LOOP 50
#define DEFAULT_FIFO_SIZE 4096
#define MO_PCI_INTMSK       0x200040
#define MO_AUDD_GPCNTRL     0x32C030
#define MO_AUD_INTSTAT      0x200064
#define AUD_INT_DN_RISCI1   (1 << 0)
#define SRAM_CH25 4
#define MO_DEV_CNTRL2       0x200034
#define AUD_INT_DN_RISCI2   (1 << 4)
#define AUD_INT_DN_SYNC     (1 << 12)
#define GP_COUNT_CONTROL_RESET 0x3
#define MO_AUDD_LNGTH       0x32C048
#define MO_AUD_INTMSK       0x200060
#define AUD_INT_OPC_ERR     (1 << 16)
#define MO_AUD_DMACNTRL     0x32C040
#define PCI_INT_AUDINT      (1 << 1)
#define MO_AUDD_GPCNT       0x32C020
#define MO_PCI_INTSTAT      0x200044
#define RISC_CNT_INC        0x00010000
#define RISC_JUMP           0x70000000
#define RISC_IRQ1           0x01000000
#define AUD_BAL_CTL         0x320598
#define AUD_VOL_CTL         0x320594
#define SHADOW_AUD_VOL_CTL  1
#define SHADOW_MAX          3
#define PCI_INT_IR_SMPINT   (1 << 18)
#define CX88_MAXBOARDS      8
#define UNSET               (-1U)
#define RISC_READ           0x90000000
#define RISC_READC          0xA0000000
#define RISC_WRITECM        0xC0000000
#define RISC_WRITEC         0x50000000
#define RISC_WRITERM        0xB0000000
#define RISC_WRITE          0x10000000
#define RISC_SKIP           0x20000000
#define RISC_SYNC           0x80000000
#define RISC_WRITECR        0xD0000000
#define MAX_CX88_INPUT      8
#define MO_SAMPLE_IO        0x35C058
#define RISC_SOL            0x08000000
#define RISC_RESYNC         0x80008000
#define RISC_EOL            0x04000000
#define PCI_INT_RISC_WR_BERRINT (1 << 11)
#define PCI_INT_IPB_DMA_BERRINT (1 << 15)
#define PCI_INT_DST_DMA_BERRINT (1 << 14)
#define CX88_BOARD_UNKNOWN  0
#define PCI_INT_BRDG_BERRINT (1 << 12)
#define PCI_INT_RISC_RD_BERRINT (1 << 10)
#define PCI_INT_SRC_DMA_BERRINT (1 << 13)
#define MO_VID_INTSTAT      0x200054
#define SRAM_CH22           1
#define MO_COLOR_CTRL       0x310184
#define SRAM_CH21           0
#define MO_AGC_BACK_VBI     0x310200
#define MO_PDMA_DTHRSH      0x200010
#define SRAM_CH24           3
#define MO_INT1_STAT        0x35C064
#define SRAM_CH23           2
#define SRAM_CH27           7
#define SRAM_CH26           5
#define MO_INPUT_FORMAT     0x310104
#define MO_SRST_IO          0x35C05C
#define MO_PDMA_STHRSH      0x200000
#define SRAM_CH28           6
#define MO_AGC_SYNC_TIP1    0x310208
#define CX88X_EN_VSFX       0x04
#define CX88X_DEVCTRL       0x40
#define CX88X_EN_TBFX       0x02
#define MO_GP0_IO           0x350010
#define MO_GP1_IO           0x350014
#define MO_GP2_IO           0x350018
#define MO_TS_INTMSK        0x200070
#define MO_GPHST_DMACNTRL   0x38C040
#define MO_TS_DMACNTRL      0x33C040
#define MO_GPHST_INTMSK     0x200090
#define MO_VID_DMACNTRL     0x31C040
#define MO_VID_INTMSK       0x200050
#define MO_VIP_INTMSK       0x200080
#define MO_VIP_DMACNTRL     0x34C040
#define VID_CAPTURE_CONTROL 0x310180
#define MO_I2C              0x368000

#define SNDRV_PCM_INFO_MMAP        0x00000001
#define SNDRV_PCM_INFO_INTERLEAVED 0x00000100
#define SNDRV_PCM_INFO_BLOCK_TRANSFER   0x00010000
#define SNDRV_PCM_INFO_MMAP_VALID  0x00000002
#define SNDRV_PCM_RATE_48000       (1U<<7)
#define PCI_ANY_ID (~0)
#define PCI_CLASS_MULTIMEDIA_AUDIO  0x0401

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

    MemoryRegion bar_regions[6];

    uint8_t *mmio_backing;
    size_t mmio_backing_size;

    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;

    /* simple register model */
    uint32_t reg_pci_intmsk;
    uint32_t reg_pci_intstat;
    uint32_t reg_aud_intmsk;
    uint32_t reg_aud_intstat;
    uint32_t reg_audd_gpcntrl;
    uint32_t reg_audd_gpcnt;
    uint32_t reg_audd_lngth;
    uint32_t reg_aud_dmacntrl;
    uint32_t reg_dev_cntrl2;
    uint32_t reg_aud_vol_ctl;
    uint32_t reg_aud_bal_ctl;
    uint32_t shadow[SHADOW_MAX];

    /* audio engine state */
    bool dma_running;
    uint32_t period_size_bytes;
    uint32_t num_periods;
    uint32_t period_count;
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
/* MemoryRegionOps                                                     */
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
/* Helper: register a BAR                                              */
/* ------------------------------------------------------------------ */
static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, bi->size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

/* ------------------------------------------------------------------ */
/* IRQ helpers                                                         */
/* ------------------------------------------------------------------ */
static void cx88_update_irq(PCIBaseState *s)
{
    uint32_t pending = s->reg_pci_intstat & (s->reg_pci_intmsk | PCI_INT_AUDINT);
    if (pending) {
        pci_set_irq(PCI_DEVICE(s), 1);
    } else {
        pci_set_irq(PCI_DEVICE(s), 0);
    }
}

/* ------------------------------------------------------------------ */
/* MMIO / PIO handlers                                                 */
/* ------------------------------------------------------------------ */
static uint32_t cx88_readl(PCIBaseState *s, hwaddr addr)
{
    switch (addr) {
    case MO_PCI_INTMSK:
        return s->reg_pci_intmsk;
    case MO_PCI_INTSTAT:
        return s->reg_pci_intstat;
    case MO_AUD_INTMSK:
        return s->reg_aud_intmsk;
    case MO_AUD_INTSTAT:
        return s->reg_aud_intstat;
    case MO_AUDD_GPCNTRL:
        return s->reg_audd_gpcntrl;
    case MO_AUDD_GPCNT:
        return s->reg_audd_gpcnt;
    case MO_AUDD_LNGTH:
        return s->reg_audd_lngth;
    case MO_AUD_DMACNTRL:
        return s->reg_aud_dmacntrl;
    case MO_DEV_CNTRL2:
        return s->reg_dev_cntrl2;
    case AUD_VOL_CTL:
        return s->reg_aud_vol_ctl;
    case AUD_BAL_CTL:
        return s->reg_aud_bal_ctl;
    case MO_SAMPLE_IO:
        /* IR sampling register - return 0 to indicate idle */
        return 0;
    default:
        return 0;
    }
}

static void cx88_writel(PCIBaseState *s, hwaddr addr, uint32_t val)
{
    switch (addr) {
    case MO_PCI_INTMSK:
        s->reg_pci_intmsk = val;
        cx88_update_irq(s);
        break;
    case MO_PCI_INTSTAT:
        /* write-1-to-clear */
        s->reg_pci_intstat &= ~val;
        cx88_update_irq(s);
        break;
    case MO_AUD_INTMSK:
        s->reg_aud_intmsk = val;
        cx88_update_irq(s);
        break;
    case MO_AUD_INTSTAT:
        /* write-1-to-clear */
        s->reg_aud_intstat &= ~val;
        if (!(s->reg_aud_intstat & s->reg_aud_intmsk)) {
            s->reg_pci_intstat &= ~PCI_INT_AUDINT;
        }
        cx88_update_irq(s);
        break;
    case MO_AUDD_GPCNTRL:
        s->reg_audd_gpcntrl = val;
        if (val & GP_COUNT_CONTROL_RESET) {
            s->reg_audd_gpcnt = 0;
        }
        break;
    case MO_AUDD_LNGTH:
        s->reg_audd_lngth = val;
        s->period_size_bytes = val;
        break;
    case MO_AUD_DMACNTRL:
        /* bit0 FIFO enable, bit4 RISC enable (0x11 controls) */
        if (val & 0x11) {
            s->reg_aud_dmacntrl |= 0x11;
            s->dma_running = true;
            if (s->period_size_bytes) {
                s->num_periods = 1024;
            }
        } else {
            s->reg_aud_dmacntrl &= ~0x11;
            s->dma_running = false;
        }
        break;
    case MO_DEV_CNTRL2:
        s->reg_dev_cntrl2 = val;
        break;
    case AUD_VOL_CTL:
        s->reg_aud_vol_ctl = val;
        break;
    case AUD_BAL_CTL:
        s->reg_aud_bal_ctl = val;
        break;
    default:
        break;
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size == 4) {
        return cx88_readl(s, addr);
    }

    qemu_log_mask(LOG_UNIMP, "[%s] mmio_read addr=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size == 4) {
        cx88_writel(s, addr, (uint32_t)val);
        return;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] mmio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, (uint64_t)val, size);
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "[%s] pio_read addr=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "[%s] pio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, (uint64_t)val, size);
}

/* ------------------------------------------------------------------ */
/* Reset                                                              */
/* ------------------------------------------------------------------ */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    pci_device_reset(pdev);

    s->reg_pci_intmsk = 0;
    s->reg_pci_intstat = 0;
    s->reg_aud_intmsk = 0;
    s->reg_aud_intstat = 0;
    s->reg_audd_gpcntrl = 0;
    s->reg_audd_gpcnt = 0;
    s->reg_audd_lngth = 0;
    s->reg_aud_dmacntrl = 0;
    s->reg_dev_cntrl2 = 0;
    s->reg_aud_vol_ctl = 0;
    s->reg_aud_bal_ctl = 0;
    memset(s->shadow, 0, sizeof(s->shadow));

    s->dma_running = false;
    s->period_size_bytes = 0;
    s->num_periods = 0;
    s->period_count = 0;

    if (s->mmio_backing && s->mmio_backing_size) {
        memset(s->mmio_backing, 0, s->mmio_backing_size);
    }
}

/* ------------------------------------------------------------------ */
/* DMA initialize (no real DMA modeled here)                          */
/* ------------------------------------------------------------------ */
static void pcibase_dma_device_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    (void)s;
    (void)errp;
}

/* ------------------------------------------------------------------ */
/* PCI config space access                                            */
/* ------------------------------------------------------------------ */
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    uint32_t val = pci_default_read_config(pdev, addr, len);

    switch (len) {
    case 1:
        val &= 0xFF;
        break;
    case 2:
        val &= 0xFFFF;
        break;
    case 4:
    default:
        break;
    }
    return val;
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    if (addr >= PCI_BASE_ADDRESS_0 && addr <= PCI_BASE_ADDRESS_5) {
        pci_default_write_config(pdev, addr, val, len);
        return;
    }

    pci_default_write_config(pdev, addr, val, len);
}

/* ------------------------------------------------------------------ */
/* Realize (device init)                                              */
/* ------------------------------------------------------------------ */
static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    memset(s->bar_info, 0, sizeof(s->bar_info));
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x400000;
    s->bar_info[0].name = "cx88-mmio";
    s->bar_info[0].sparse = false;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  CX88_AUDIO_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  CX88_AUDIO_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CX88_AUDIO_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) {
        return;
    }

    s->has_msi = false;
    s->has_msix = false;

    qemu_log_mask(LOG_UNIMP, "[%s] device realized\n", TYPE_PCIBASE_DEVICE);
}

/* ------------------------------------------------------------------ */
/* Uninit/cleanup                                                     */
/* ------------------------------------------------------------------ */
static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (s->has_msix) {
        msix_uninit(pdev, NULL, 0);
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

    qemu_log_mask(LOG_UNIMP, "[%s] device uninit\n", TYPE_PCIBASE_DEVICE);
}

/* ------------------------------------------------------------------ */
/* Class init / type registration                                     */
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

