/*
 * QEMU AD1889 Audio Controller Device Model
 * Based on Linux driver sound/pci/ad1889.c
 * QEMU 8.2.10 compatible
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

/* Register definitions from driver */
#define AD_DS_WSMC	0x00
#define AD_DS_RAMC	0x02
#define AD_DS_WADA	0x04
#define AD_DS_WADA_LWAM 0x8000
#define AD_DS_WADA_RWAM 0x0080
#define AD_DS_WAS	0x08
#define AD_DS_RES	0x0a
#define AD_DS_CCS	0x0c
#define AD_DS_CCS_CLKEN 0x8000
#define AD_DMA_ADCBA	0x50
#define AD_DMA_ADCCA	0x54
#define AD_DMA_ADCBC	0x58
#define AD_DMA_ADCCC	0x5c
#define AD_DMA_ADCIB	0x8c
#define AD_DMA_ADCIC	0x88
#define AD_DMA_WAVBA	0x70
#define AD_DMA_WAVCA	0x74
#define AD_DMA_WAVBC	0x78
#define AD_DMA_WAVCC	0x7c
#define AD_DMA_WAVIC	0x98
#define AD_DMA_WAVIB	0x9c
#define AD_DMA_CHSS	0xc4
#define AD_DMA_CHSS_WAVS 0x000008
#define AD_DMA_CHSS_ADCS 0x000002
#define AD_DMA_DISR	0xc0
#define AD_DMA_DISR_RESI 0x000001
#define AD_DMA_DISR_ADCI 0x000002
#define AD_DMA_DISR_SYNI 0x000004
#define AD_DMA_DISR_WAVI 0x000008
#define AD_DMA_DISR_PMAI 0x004000
#define AD_DMA_DISR_PTAI 0x008000
#define AD_DMA_DISR_PMAE 0x020000
#define AD_DMA_DISR_PTAE 0x010000
#define AD_INTR_MASK     (AD_DMA_DISR_RESI|AD_DMA_DISR_ADCI| \
                           AD_DMA_DISR_WAVI|AD_DMA_DISR_SYNI| \
                           AD_DMA_DISR_PMAI|AD_DMA_DISR_PTAI)
#define AD_DMA_IM	0x000c
#define AD_DMA_IM_DIS	(~AD_DMA_IM)
#define AD_DMA_IM_CNT	0x0004
#define AD_DMA_LOOP	0x0002
#define AD_DMA_ADC	0xa8
#define AD_CHAN_ADC	0x0002
#define AD_DMA_WAV	0xb8
#define AD_CHAN_WAV	0x0001
#define AD_DS_WSMC_WAEN 0x0400
#define AD_DS_WSMC_WAST 0x0200
#define AD_DS_WSMC_WA16 0x0100
#define AD_DS_WSMC_SYEN 0x0004
#define AD_DS_WSMC_SYRQ 0x0030
#define AD_DS_WSMC_WARQ 0x3000
#define AD_DS_RAMC_ADEN 0x0004
#define AD_DS_RAMC_ADST 0x0002
#define AD_DS_RAMC_AD16 0x0001
#define AD_DS_RAMC_REEN 0x0400
#define AD_DS_RAMC_RERQ 0x3000
#define AD_DS_RAMC_ACRQ 0x0030
#define AD_DS_WADA_LWAA 0x3e00
#define AD_DS_WADA_RWAA 0x001f
#define AD_AC97_BASE	0x100
#define AD_AC97_ACIC	0x180
#define AD_AC97_ACIC_ACRDY 0x8000
#define AD_AC97_ACIC_ACRD  0x0002
#define AD_AC97_ACIC_ASOE  0x0004
#define AD_AC97_ACIC_VSRM  0x0008
#define AD_AC97_ACIC_ACIE  0x0001

#define TYPE_PCIBASE_DEVICE "snd_ad1889_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

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

    /* MMIO registers */
    uint16_t ds_wsmc;
    uint16_t ds_ramc;
    uint16_t ds_wada;
    uint16_t ds_was;
    uint16_t ds_res;
    uint16_t ds_ccs;

    /* DMA wave (playback) registers */
    uint32_t dma_wavba;
    uint32_t dma_wavca;
    uint32_t dma_wavbc;
    uint32_t dma_wavcc;
    uint32_t dma_wavib;
    uint32_t dma_wavic;
    uint16_t dma_wav;

    /* DMA ADC (capture) registers */
    uint32_t dma_adcba;
    uint32_t dma_adcca;
    uint32_t dma_adcbc;
    uint32_t dma_adccc;
    uint32_t dma_adcib;
    uint32_t dma_adcic;
    uint16_t dma_adc;

    uint32_t dma_chss;
    uint32_t dma_disr;

    /* AC97 interface */
    uint16_t ac97_acic;
    uint16_t ac97_regs[64];

    /* DMA emulation timers */
    QEMUTimer wave_timer;
    QEMUTimer adc_timer;
    bool wave_enabled;
    bool adc_enabled;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->dma_disr & AD_INTR_MASK) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

static void ad1889_wave_timer(void *opaque)
{
    PCIBaseState *s = opaque;
    uint32_t period = s->dma_wavic;
    if (unlikely(!s->wave_enabled || !s->dma_wavba || !s->dma_wavbc || !period)) {
        return;
    }
    uint32_t current = s->dma_wavca;
    uint32_t base = s->dma_wavba;
    uint32_t size = s->dma_wavbc;
    uint32_t offset = current - base;
    offset = (offset + period) % size;
    s->dma_wavca = base + offset;
    if (s->dma_wav & AD_DMA_IM_CNT) {
        s->dma_disr |= AD_DMA_DISR_WAVI;
        pcibase_update_irq(s);
    }
    timer_mod(&s->wave_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 1);
}

static void ad1889_adc_timer(void *opaque)
{
    PCIBaseState *s = opaque;
    uint32_t period = s->dma_adcic;
    if (unlikely(!s->adc_enabled || !s->dma_adcba || !s->dma_adcbc || !period)) {
        return;
    }
    uint32_t current = s->dma_adcca;
    uint32_t base = s->dma_adcba;
    uint32_t size = s->dma_adcbc;
    uint32_t offset = current - base;
    offset = (offset + period) % size;
    s->dma_adcca = base + offset;
    if (s->dma_adc & AD_DMA_IM_CNT) {
        s->dma_disr |= AD_DMA_DISR_ADCI;
        pcibase_update_irq(s);
    }
    timer_mod(&s->adc_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 1);
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= 0x100 && addr < 0x180) {
        if (size == 2 && (addr % 2) == 0) {
            int index = (addr - 0x100) >> 1;
            if (index < 64) {
                val = s->ac97_regs[index];
            }
        }
    } else if (addr >= 0x180 && addr < 0x182) {
        if (size == 2 && addr == 0x180) {
            val = s->ac97_acic;
            if (s->ac97_acic & AD_AC97_ACIC_ACIE) {
                val |= AD_AC97_ACIC_ACRDY;
            }
        }
    } else {
        switch (addr) {
        case AD_DS_WSMC:
            if (size == 2) val = s->ds_wsmc;
            break;
        case AD_DS_RAMC:
            if (size == 2) val = s->ds_ramc;
            break;
        case AD_DS_WADA:
            if (size == 2) val = s->ds_wada;
            break;
        case AD_DS_WAS:
            if (size == 2) val = s->ds_was;
            break;
        case AD_DS_RES:
            if (size == 2) val = s->ds_res;
            break;
        case AD_DS_CCS:
            if (size == 2) val = s->ds_ccs;
            break;
        case AD_DMA_WAVBA:
            if (size == 4) val = s->dma_wavba;
            break;
        case AD_DMA_WAVCA:
            if (size == 4) val = s->dma_wavca;
            break;
        case AD_DMA_WAVBC:
            if (size == 4) val = s->dma_wavbc;
            break;
        case AD_DMA_WAVCC:
            if (size == 4) val = s->dma_wavcc;
            break;
        case AD_DMA_WAVIC:
            if (size == 4) val = s->dma_wavic;
            break;
        case AD_DMA_WAVIB:
            if (size == 4) val = s->dma_wavib;
            break;
        case AD_DMA_ADCBA:
            if (size == 4) val = s->dma_adcba;
            break;
        case AD_DMA_ADCCA:
            if (size == 4) val = s->dma_adcca;
            break;
        case AD_DMA_ADCBC:
            if (size == 4) val = s->dma_adcbc;
            break;
        case AD_DMA_ADCCC:
            if (size == 4) val = s->dma_adccc;
            break;
        case AD_DMA_ADCIC:
            if (size == 4) val = s->dma_adcic;
            break;
        case AD_DMA_ADCIB:
            if (size == 4) val = s->dma_adcib;
            break;
        case AD_DMA_CHSS:
            if (size == 4) val = s->dma_chss;
            break;
        case AD_DMA_DISR:
            if (size == 4) val = s->dma_disr;
            break;
        case AD_DMA_WAV:
            if (size == 2) val = s->dma_wav;
            break;
        case AD_DMA_ADC:
            if (size == 2) val = s->dma_adc;
            break;
        default:
            break;
        }
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= 0x100 && addr < 0x180) {
        if (size == 2 && (addr % 2) == 0) {
            int index = (addr - 0x100) >> 1;
            if (index < 64) {
                s->ac97_regs[index] = (uint16_t)val;
            }
        }
        return;
    }

    if (addr >= 0x180 && addr < 0x182) {
        if (size == 2 && addr == 0x180) {
            s->ac97_acic = (uint16_t)val;
        }
        return;
    }

    switch (addr) {
    case AD_DS_WSMC:
        if (size == 2) {
            uint16_t old = s->ds_wsmc;
            s->ds_wsmc = (uint16_t)val;
            bool old_en = old & AD_DS_WSMC_WAEN;
            bool new_en = val & AD_DS_WSMC_WAEN;
            if (!old_en && new_en) {
                if (s->dma_wav & AD_DMA_LOOP && s->dma_wavba && s->dma_wavbc && s->dma_wavic) {
                    s->wave_enabled = true;
                    timer_mod(&s->wave_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 1);
                }
            } else if (old_en && !new_en) {
                s->wave_enabled = false;
                timer_del(&s->wave_timer);
            }
        }
        break;
    case AD_DS_RAMC:
        if (size == 2) {
            uint16_t old = s->ds_ramc;
            s->ds_ramc = (uint16_t)val;
            bool old_en = old & AD_DS_RAMC_ADEN;
            bool new_en = val & AD_DS_RAMC_ADEN;
            if (!old_en && new_en) {
                if (s->dma_adc & AD_DMA_LOOP && s->dma_adcba && s->dma_adcbc && s->dma_adcic) {
                    s->adc_enabled = true;
                    timer_mod(&s->adc_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 1);
                }
            } else if (old_en && !new_en) {
                s->adc_enabled = false;
                timer_del(&s->adc_timer);
            }
        }
        break;
    case AD_DS_WADA:
        if (size == 2) s->ds_wada = (uint16_t)val;
        break;
    case AD_DS_WAS:
        if (size == 2) s->ds_was = (uint16_t)val;
        break;
    case AD_DS_RES:
        if (size == 2) s->ds_res = (uint16_t)val;
        break;
    case AD_DS_CCS:
        if (size == 2) s->ds_ccs = (uint16_t)val;
        break;
    case AD_DMA_WAVBA:
        if (size == 4) s->dma_wavba = (uint32_t)val;
        break;
    case AD_DMA_WAVCA:
        if (size == 4) s->dma_wavca = (uint32_t)val;
        break;
    case AD_DMA_WAVBC:
        if (size == 4) s->dma_wavbc = (uint32_t)val;
        break;
    case AD_DMA_WAVCC:
        if (size == 4) s->dma_wavcc = (uint32_t)val;
        break;
    case AD_DMA_WAVIC:
        if (size == 4) s->dma_wavic = (uint32_t)val;
        break;
    case AD_DMA_WAVIB:
        if (size == 4) s->dma_wavib = (uint32_t)val;
        break;
    case AD_DMA_ADCBA:
        if (size == 4) s->dma_adcba = (uint32_t)val;
        break;
    case AD_DMA_ADCCA:
        if (size == 4) s->dma_adcca = (uint32_t)val;
        break;
    case AD_DMA_ADCBC:
        if (size == 4) s->dma_adcbc = (uint32_t)val;
        break;
    case AD_DMA_ADCCC:
        if (size == 4) s->dma_adccc = (uint32_t)val;
        break;
    case AD_DMA_ADCIC:
        if (size == 4) s->dma_adcic = (uint32_t)val;
        break;
    case AD_DMA_ADCIB:
        if (size == 4) s->dma_adcib = (uint32_t)val;
        break;
    case AD_DMA_CHSS:
        if (size == 4) s->dma_chss = (uint32_t)val;
        break;
    case AD_DMA_DISR:
        if (size == 4) {
            uint32_t status_mask = AD_INTR_MASK;
            uint32_t enable_mask = AD_DMA_DISR_PMAE | AD_DMA_DISR_PTAE;
            s->dma_disr &= ~(status_mask & val);
            s->dma_disr |= (val & enable_mask);
            pcibase_update_irq(s);
        }
        break;
    case AD_DMA_WAV:
        if (size == 2) {
            s->dma_wav = (uint16_t)val;
            if (s->ds_wsmc & AD_DS_WSMC_WAEN) {
                if (s->dma_wav & AD_DMA_LOOP && s->dma_wavba && s->dma_wavbc && s->dma_wavic) {
                    if (!s->wave_enabled) {
                        s->wave_enabled = true;
                        timer_mod(&s->wave_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 1);
                    }
                } else {
                    if (s->wave_enabled) {
                        s->wave_enabled = false;
                        timer_del(&s->wave_timer);
                    }
                }
            }
        }
        break;
    case AD_DMA_ADC:
        if (size == 2) {
            s->dma_adc = (uint16_t)val;
            if (s->ds_ramc & AD_DS_RAMC_ADEN) {
                if (s->dma_adc & AD_DMA_LOOP && s->dma_adcba && s->dma_adcbc && s->dma_adcic) {
                    if (!s->adc_enabled) {
                        s->adc_enabled = true;
                        timer_mod(&s->adc_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 1);
                    }
                } else {
                    if (s->adc_enabled) {
                        s->adc_enabled = false;
                        timer_del(&s->adc_timer);
                    }
                }
            }
        }
        break;
    default:
        break;
    }
}

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

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    s->ds_wsmc = 0;
    s->ds_ramc = 0;
    s->ds_wada = 0;
    s->ds_was = 0;
    s->ds_res = 0;
    s->ds_ccs = 0;
    s->dma_wavba = s->dma_wavca = s->dma_wavbc = s->dma_wavcc = s->dma_wavib = s->dma_wavic = 0;
    s->dma_wav = 0;
    s->dma_adcba = s->dma_adcca = s->dma_adcbc = s->dma_adccc = s->dma_adcib = s->dma_adcic = 0;
    s->dma_adc = 0;
    s->dma_chss = 0;
    s->dma_disr = 0;
    s->ac97_acic = 0;
    memset(s->ac97_regs, 0, sizeof(s->ac97_regs));
    s->ac97_regs[0x7c >> 1] = 0x4144; /* VENDOR_ID1 for AD1881A */
    s->ac97_regs[0x7e >> 1] = 0x5348; /* VENDOR_ID2 */
    timer_del(&s->wave_timer);
    timer_del(&s->adc_timer);
    s->wave_enabled = false;
    s->adc_enabled = false;
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, 0x11d4);
    pci_set_word(pci_conf + PCI_DEVICE_ID, 0x1889);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_MULTIMEDIA_AUDIO);
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
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x200;
    s->bar_info[0].name = "ad1889-bar0";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    timer_init_ms(&s->wave_timer, QEMU_CLOCK_VIRTUAL, ad1889_wave_timer, s);
    timer_init_ms(&s->adc_timer, QEMU_CLOCK_VIRTUAL, ad1889_adc_timer, s);
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
    timer_del(&s->wave_timer);
    timer_del(&s->adc_timer);
}

static const VMStateDescription vmstate_pcibase = {
    .name = "snd_ad1889_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT16(ds_wsmc, PCIBaseState),
        VMSTATE_UINT16(ds_ramc, PCIBaseState),
        VMSTATE_UINT16(ds_wada, PCIBaseState),
        VMSTATE_UINT16(ds_was, PCIBaseState),
        VMSTATE_UINT16(ds_res, PCIBaseState),
        VMSTATE_UINT16(ds_ccs, PCIBaseState),
        VMSTATE_UINT32(dma_wavba, PCIBaseState),
        VMSTATE_UINT32(dma_wavca, PCIBaseState),
        VMSTATE_UINT32(dma_wavbc, PCIBaseState),
        VMSTATE_UINT32(dma_wavcc, PCIBaseState),
        VMSTATE_UINT32(dma_wavib, PCIBaseState),
        VMSTATE_UINT32(dma_wavic, PCIBaseState),
        VMSTATE_UINT16(dma_wav, PCIBaseState),
        VMSTATE_UINT32(dma_adcba, PCIBaseState),
        VMSTATE_UINT32(dma_adcca, PCIBaseState),
        VMSTATE_UINT32(dma_adcbc, PCIBaseState),
        VMSTATE_UINT32(dma_adccc, PCIBaseState),
        VMSTATE_UINT32(dma_adcib, PCIBaseState),
        VMSTATE_UINT32(dma_adcic, PCIBaseState),
        VMSTATE_UINT16(dma_adc, PCIBaseState),
        VMSTATE_UINT32(dma_chss, PCIBaseState),
        VMSTATE_UINT32(dma_disr, PCIBaseState),
        VMSTATE_UINT16(ac97_acic, PCIBaseState),
        VMSTATE_UINT16_ARRAY(ac97_regs, PCIBaseState, 64),
        VMSTATE_BOOL(wave_enabled, PCIBaseState),
        VMSTATE_BOOL(adc_enabled, PCIBaseState),
        VMSTATE_TIMER(wave_timer, PCIBaseState),
        VMSTATE_TIMER(adc_timer, PCIBaseState),
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
