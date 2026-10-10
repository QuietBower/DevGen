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

/* Additional include files retrieved from driver context */

#ifndef PCI_VENDOR_ID_ANALOG_DEVICES
#define PCI_VENDOR_ID_ANALOG_DEVICES 0x11d4
#endif
#ifndef PCI_DEVICE_ID_AD1889JS
#define PCI_DEVICE_ID_AD1889JS 0x1889
#endif

#define TYPE_PCIBASE_DEVICE "snd_ad1889_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define AD_DS_WSMC	0x00
#define AD_DS_WSMC_WAEN 0x0400
#define AD_DS_WSMC_WAST 0x0200
#define AD_DS_WSMC_WA16 0x0100
#define AD_DS_WSMC_WARQ 0x3000
#define AD_DS_WSMC_SYEN 0x0004
#define AD_DS_WSMC_SYRQ 0x0030

#define AD_DS_RAMC	0x02
#define AD_DS_RAMC_ADEN 0x0004
#define AD_DS_RAMC_ADST 0x0002
#define AD_DS_RAMC_AD16 0x0001
#define AD_DS_RAMC_ACRQ 0x0030
#define AD_DS_RAMC_REEN 0x0400
#define AD_DS_RAMC_RERQ 0x3000

#define AD_DS_WADA	0x04
#define AD_DS_WADA_RWAM 0x0080
#define AD_DS_WADA_LWAM 0x8000
#define AD_DS_WADA_RWAA 0x001f
#define AD_DS_WADA_LWAA 0x3e00

#define AD_DS_WAS	0x08
#define AD_DS_RES	0x0a
#define AD_DS_CCS	0x0c
#define AD_DS_CCS_CLKEN 0x8000

#define AD_DMA_ADCBA	0x50
#define AD_DMA_ADCCA	0x54
#define AD_DMA_ADCBC	0x58
#define AD_DMA_ADCCC	0x5c
#define AD_DMA_WAVBA	0x70
#define AD_DMA_WAVCA	0x74
#define AD_DMA_WAVBC	0x78
#define AD_DMA_WAVCC	0x7c
#define AD_DMA_ADCIC	0x88
#define AD_DMA_ADCIB	0x8c
#define AD_DMA_WAVIC	0x98
#define AD_DMA_WAVIB	0x9c
#define AD_DMA_ADC	0xa8
#define AD_DMA_WAV	0xb8

#define AD_DMA_DISR	0xc0
#define AD_DMA_DISR_PMAE 0x020000
#define AD_DMA_DISR_PTAE 0x010000
#define AD_DMA_DISR_PMAI 0x004000
#define AD_DMA_DISR_PTAI 0x008000
#define AD_DMA_DISR_WAVI 0x000008
#define AD_DMA_DISR_SYNI 0x000004
#define AD_DMA_DISR_ADCI 0x000002
#define AD_DMA_DISR_RESI 0x000001

#define AD_DMA_CHSS	0xc4
#define AD_DMA_CHSS_WAVS 0x000008
#define AD_DMA_CHSS_ADCS 0x000002

#define AD_AC97_BASE	0x100
#define AD_AC97_ACIC	0x180
#define AD_AC97_ACIC_ACRDY 0x8000
#define AD_AC97_ACIC_ACRD  0x0002
#define AD_AC97_ACIC_ACIE  0x0001
#define AD_AC97_ACIC_ASOE  0x0004
#define AD_AC97_ACIC_VSRM  0x0008

#define AD_DMA_LOOP	0x0002
#define AD_DMA_IM_CNT	0x0004
#define AD_DMA_IM	0x000c

#define AD_CHAN_WAV	0x0001
#define AD_CHAN_ADC	0x0002

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
    uint32_t dma_disr;
    uint32_t intr_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint16_t ds_wsmc;
    uint16_t ds_ramc;
    uint16_t ds_wada;
    uint16_t ds_was;
    uint16_t ds_res;
    uint16_t ds_ccs;
    uint32_t ac97_acic;
    uint16_t ac97_regs[128];

    /* DMA Context */
    uint32_t dma_adcba;
    uint32_t dma_adcca;
    uint32_t dma_adcbc;
    uint32_t dma_adccc;
    uint32_t dma_adcib;
    uint32_t dma_adcic;
    uint32_t dma_wavba;
    uint32_t dma_wavca;
    uint32_t dma_wavbc;
    uint32_t dma_wavcc;
    uint32_t dma_wavib;
    uint32_t dma_wavic;
    uint32_t dma_adc;
    uint32_t dma_wav;
    uint32_t dma_chss;

};



/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* Basic IRQ logic: if any status bit is set, raise IRQ.
     * We exclude the enable bits (PMAE, PTAE) from the check.
     */
    uint32_t status_bits = s->dma_disr & ~(AD_DMA_DISR_PMAE | AD_DMA_DISR_PTAE);
    pci_set_irq(pdev, status_bits ? 1 : 0);
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= AD_AC97_BASE && addr < AD_AC97_BASE + 0x100) {
        if (addr == AD_AC97_ACIC) {
            val = s->ac97_acic | AD_AC97_ACIC_ACRDY; /* Always ready */
        } else {
            val = s->ac97_regs[(addr - AD_AC97_BASE) / 2];
        }
        return val;
    }

    switch (addr) {
    case AD_DS_WSMC: val = s->ds_wsmc; break;
    case AD_DS_RAMC: val = s->ds_ramc; break;
    case AD_DS_WADA: val = s->ds_wada; break;
    case AD_DS_WAS: val = s->ds_was; break;
    case AD_DS_RES: val = s->ds_res; break;
    case AD_DS_CCS: val = s->ds_ccs; break;
    case AD_DMA_ADCBA: val = s->dma_adcba; break;
    case AD_DMA_ADCCA: val = s->dma_adcca; break;
    case AD_DMA_ADCBC: val = s->dma_adcbc; break;
    case AD_DMA_ADCCC: val = s->dma_adccc; break;
    case AD_DMA_WAVBA: val = s->dma_wavba; break;
    case AD_DMA_WAVCA: val = s->dma_wavca; break;
    case AD_DMA_WAVBC: val = s->dma_wavbc; break;
    case AD_DMA_WAVCC: val = s->dma_wavcc; break;
    case AD_DMA_ADCIC: val = s->dma_adcic; break;
    case AD_DMA_ADCIB: val = s->dma_adcib; break;
    case AD_DMA_WAVIC: val = s->dma_wavic; break;
    case AD_DMA_WAVIB: val = s->dma_wavib; break;
    case AD_DMA_ADC: val = s->dma_adc; break;
    case AD_DMA_WAV: val = s->dma_wav; break;
    case AD_DMA_DISR: val = s->dma_disr; break;
    case AD_DMA_CHSS: val = s->dma_chss; break;
    }
    
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= AD_AC97_BASE && addr < AD_AC97_BASE + 0x100) {
        if (addr == AD_AC97_ACIC) {
            s->ac97_acic = val;
        } else if (addr == AD_AC97_BASE + 0x00) {
            /* Writing to AC97_RESET resets the codec */
            s->ac97_regs[0x00 / 2] = 0x0090;
            s->ac97_regs[0x26 / 2] = 0x000f;
            s->ac97_regs[0x7c / 2] = 0x4144;
            s->ac97_regs[0x7e / 2] = 0x5340;
        } else {
            s->ac97_regs[(addr - AD_AC97_BASE) / 2] = val;
        }
        return;
    }

    switch (addr) {
    case AD_DS_WSMC: s->ds_wsmc = val; break;
    case AD_DS_RAMC: s->ds_ramc = val; break;
    case AD_DS_WADA: s->ds_wada = val; break;
    case AD_DS_WAS: s->ds_was = val; break;
    case AD_DS_RES: s->ds_res = val; break;
    case AD_DS_CCS: s->ds_ccs = val; break;
    case AD_DMA_ADCBA: s->dma_adcba = val; break;
    case AD_DMA_ADCCA: s->dma_adcca = val; break;
    case AD_DMA_ADCBC: s->dma_adcbc = val; break;
    case AD_DMA_ADCCC: s->dma_adccc = val; break;
    case AD_DMA_WAVBA: s->dma_wavba = val; break;
    case AD_DMA_WAVCA: s->dma_wavca = val; break;
    case AD_DMA_WAVBC: s->dma_wavbc = val; break;
    case AD_DMA_WAVCC: s->dma_wavcc = val; break;
    case AD_DMA_ADCIC: s->dma_adcic = val; break;
    case AD_DMA_ADCIB: s->dma_adcib = val; break;
    case AD_DMA_WAVIC: s->dma_wavic = val; break;
    case AD_DMA_WAVIB: s->dma_wavib = val; break;
    case AD_DMA_ADC: s->dma_adc = val; break;
    case AD_DMA_WAV: s->dma_wav = val; break;
    case AD_DMA_DISR: 
        s->dma_disr &= ~val; /* W1C */
        pcibase_update_irq(s);
        break;
    case AD_DMA_CHSS: s->dma_chss = val; break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    uint64_t val = 0;

    
    return val;
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
    s->ac97_acic = 0;
    memset(s->ac97_regs, 0, sizeof(s->ac97_regs));
    s->ac97_regs[0x00 / 2] = 0x0090;
    s->ac97_regs[0x26 / 2] = 0x000f;
    s->ac97_regs[0x7c / 2] = 0x4144;
    s->ac97_regs[0x7e / 2] = 0x5340;

    s->dma_adcba = 0;
    s->dma_adcca = 0;
    s->dma_adcbc = 0;
    s->dma_adccc = 0;
    s->dma_adcib = 0;
    s->dma_adcic = 0;
    s->dma_wavba = 0;
    s->dma_wavca = 0;
    s->dma_wavbc = 0;
    s->dma_wavcc = 0;
    s->dma_wavib = 0;
    s->dma_wavic = 0;
    s->dma_adc = 0;
    s->dma_wav = 0;
    s->dma_chss = 0;
    s->dma_disr = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_ANALOG_DEVICES );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_AD1889JS );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_MULTIMEDIA_AUDIO );
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
    s->bar_info[0] = (BARInfo){
        .index = 0,
        .type = BAR_TYPE_MMIO,
        .size = 0x200,
        .name = "ad1889-mmio"
    };
      
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }

    
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "snd_ad1889_pci",
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
