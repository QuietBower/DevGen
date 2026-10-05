/*
 * QEMU CX88 Audio PCI Device Emulation
 *
 * Copyright (c) 2025 QEMU contributors
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
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

/* Related register/macros/constants from driver (Stage1) */
#define VENDOR_ID 0x14f1
#define DEVICE_ID 0x8801
#define CLASS_ID   PCI_CLASS_MULTIMEDIA_AUDIO
#define MO_PCI_INTMSK       0x200040
#define MO_AUDD_GPCNTRL     0x32C030
#define MO_AUD_INTSTAT      0x200064
#define AUD_INT_DN_RISCI1	(1 <<  0)
#define SRAM_CH25 4
#define MO_DEV_CNTRL2       0x200034
#define AUD_INT_DN_RISCI2	(1 <<  4)
#define AUD_INT_DN_SYNC		(1 << 12)
#define GP_COUNT_CONTROL_RESET		 0x3
#define MO_AUDD_LNGTH       0x32C048
#define MO_AUD_INTMSK       0x200060
#define AUD_INT_OPC_ERR		(1 << 16)
#define MO_AUD_DMACNTRL     0x32C040
#define PCI_INT_AUDINT		(1 <<  1)
#define MO_AUDD_GPCNT       0x32C020
#define MO_PCI_INTSTAT      0x200044
#define RISC_CNT_INC		 0x00010000
#define RISC_JUMP		 0x70000000
#define RISC_IRQ1		 0x01000000
#define AUD_BAL_CTL              0x320598
#define AUD_VOL_CTL              0x320594
#define SHADOW_AUD_VOL_CTL           1
#define SHADOW_MAX                   3
#define PCI_INT_IR_SMPINT	(1 << 18)
#define RISC_READ		 0x90000000
#define RISC_READC		 0xA0000000
#define RISC_WRITECM		 0xC0000000
#define RISC_WRITEC		 0x50000000
#define RISC_WRITERM		 0xB0000000
#define RISC_WRITE		 0x10000000
#define RISC_SKIP		 0x20000000
#define RISC_SYNC		 0x80000000
#define RISC_WRITECR		 0xD0000000
#define MO_SAMPLE_IO        0x35C058
#define RISC_SOL		 0x08000000
#define RISC_RESYNC		 0x80008000
#define RISC_EOL		 0x04000000
#define PCI_INT_RISC_WR_BERRINT	(1 << 11)
#define PCI_INT_IPB_DMA_BERRINT	(1 << 15)
#define PCI_INT_DST_DMA_BERRINT	(1 << 14)
#define PCI_INT_BRDG_BERRINT	(1 << 12)
#define PCI_INT_RISC_RD_BERRINT	(1 << 10)
#define PCI_INT_SRC_DMA_BERRINT	(1 << 13)
#define MO_VID_INTSTAT      0x200054
#define SRAM_CH22 1
#define MO_COLOR_CTRL       0x310184
#define SRAM_CH21 0
#define MO_AGC_BACK_VBI     0x310200
#define MO_PDMA_DTHRSH      0x200010
#define SRAM_CH24 3
#define MO_INT1_STAT        0x35C064
#define SRAM_CH23 2
#define SRAM_CH27 7
#define SRAM_CH26 5
#define MO_INPUT_FORMAT     0x310104
#define MO_SRST_IO          0x35C05C
#define MO_PDMA_STHRSH      0x200000
#define SRAM_CH28 6
#define MO_AGC_SYNC_TIP1    0x310208
#define CX88X_EN_VSFX 0x04
#define CX88X_DEVCTRL 0x40
#define CX88X_EN_TBFX 0x02

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
    
    /* DMA info placeholder */
    

    /* Register shadows */
    uint32_t shadow[SHADOW_MAX];

    /* Status fields */
    uint32_t aud_intstat;
    uint32_t pci_intstat;
    uint32_t aud_intmsk;
    uint32_t pci_intmsk;
    uint32_t aud_dmacntrl;
    uint32_t dev_cntrl2;
    uint32_t audd_gpcntrl;
    uint32_t audd_gpcnt;
    uint32_t audd_lngth;
    uint32_t aud_vol_ctl;
    uint32_t aud_bal_ctl;

    /* reset/probe state */
    

    /* power mgmt */
    

    /* other fields */
    
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
    uint64_t val = 0;

    switch (addr) {
    case MO_AUD_INTSTAT:
        val = s->aud_intstat;
        break;
    case MO_PCI_INTSTAT:
        val = s->pci_intstat;
        break;
    case MO_AUD_INTMSK:
        val = s->aud_intmsk;
        break;
    case MO_PCI_INTMSK:
        val = s->pci_intmsk;
        break;
    case MO_AUD_DMACNTRL:
        val = s->aud_dmacntrl;
        break;
    case MO_DEV_CNTRL2:
        val = s->dev_cntrl2;
        break;
    case MO_AUDD_GPCNTRL:
        val = s->audd_gpcntrl;
        break;
    case MO_AUDD_GPCNT:
        val = s->audd_gpcnt;
        break;
    case MO_AUDD_LNGTH:
        val = s->audd_lngth;
        break;
    case AUD_VOL_CTL:
        val = s->aud_vol_ctl;
        break;
    case AUD_BAL_CTL:
        val = s->aud_bal_ctl;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "[%s] unhandled mmio_read addr=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case MO_AUD_INTSTAT:
        s->aud_intstat &= ~val;
        break;
    case MO_PCI_INTSTAT:
        s->pci_intstat &= ~val;
        break;
    case MO_AUD_INTMSK:
        s->aud_intmsk = val;
        break;
    case MO_PCI_INTMSK:
        s->pci_intmsk = val;
        if (val & PCI_INT_AUDINT) {
            pci_set_irq(PCI_DEVICE(s), 1);
        } else {
            pci_set_irq(PCI_DEVICE(s), 0);
        }
        break;
    case MO_AUD_DMACNTRL:
        s->aud_dmacntrl = val;
        break;
    case MO_DEV_CNTRL2:
        s->dev_cntrl2 = val;
        break;
    case MO_AUDD_GPCNTRL:
        if (val == GP_COUNT_CONTROL_RESET) {
            s->audd_gpcnt = 0;
        }
        s->audd_gpcntrl = val;
        break;
    case MO_AUDD_LNGTH:
        s->audd_lngth = val;
        break;
    case AUD_VOL_CTL:
        s->aud_vol_ctl = val;
        break;
    case AUD_BAL_CTL:
        s->aud_bal_ctl = val;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "[%s] unhandled mmio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, (uint64_t)val, size);
        break;
    }
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
/* Reset       */
/* ------------------------------------------------------------------ */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    /* core resets */
    pci_device_reset(pdev);

    s->aud_intstat = 0;
    s->pci_intstat = 0;
    s->aud_intmsk = 0;
    s->pci_intmsk = 0;
    s->aud_dmacntrl = 0;
    s->dev_cntrl2 = 0;
    s->audd_gpcntrl = 0;
    s->audd_gpcnt = 0;
    s->audd_lngth = 0;
    s->aud_vol_ctl = 0;
    s->aud_bal_ctl = 0;

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

    /* Define BAR0 as MMIO with size 0x400000 */
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x400000;
    s->bar_info[0].name = "cx88-audio-mmio";
    s->num_bars = 1;

    /* register BARs */
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Interrupt config */
    s->has_msi = false;
    s->has_msix = false;

    /* DMA init */
    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) return;

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
