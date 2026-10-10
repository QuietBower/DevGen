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
/* (none) */

#define TYPE_PCIBASE_DEVICE "hsu_dma_pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_INTEL              0x8086
#define PCI_DEVICE_ID_INTEL_MFLD_HSU_DMA 0x081e

#define HSU_PCI_DMASR         0x00
#define HSU_PCI_DMAISR        0x04
#define HSU_PCI_CHAN_OFFSET   0x100

#define HSU_CH_SR             0x00
#define HSU_CH_SR_DESCE_ANY   0x000F0000
#define HSU_CH_SR_CDESC_ANY   0xC0000000
#define HSU_CH_SR_DESCTO_ANY  0x00000F00
#define HSU_CH_SR_CHE         BIT(15)

#define HSU_CH_CR             0x04
#define HSU_CH_CR_CHA         BIT(0)
#define HSU_CH_CR_CHD         BIT(1)

#define HSU_CH_DCR            0x08
#define HSU_CH_DCR_DESCA(x)   BIT(0 + (x))
#define HSU_CH_DCR_CHSOE      BIT(15)
#define HSU_CH_DCR_CHDI(x)    BIT(16 + (x))
#define HSU_CH_DCR_CHEI       BIT(23)
#define HSU_CH_DCR_CHTOI(x)   BIT(24 + (x))
#define HSU_CH_DCR_CHSOD(x)   BIT(8 + (x))

#define HSU_CH_BSR            0x10
#define HSU_CH_MTSR           0x14
#define HSU_CH_DxSAR(x)       (0x20 + 8 * (x))
#define HSU_CH_DxTSR(x)       (0x24 + 8 * (x))

#define HSU_DMA_CHAN_LENGTH   0x40
#define HSU_DMA_CHAN_NR_DESC  4

/* Values refined with concrete driver evidence */
#define HSU_PCI_BAR0_SIZE     0x200
#define HSU_DMA_NR_CHANNELS   4

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

typedef struct HSUChanReg {
    uint32_t sr;      /* 0x00 */
    uint32_t cr;      /* 0x04 */
    uint32_t dcr;     /* 0x08 */
    uint32_t _pad1;   /* 0x0C */
    uint32_t bsr;     /* 0x10 */
    uint32_t mtsr;    /* 0x14 */
    uint32_t _pad2[2];/* 0x18-0x1C */
    struct {
        uint32_t sar;
        uint32_t tsr;
    } desc[4];        /* 0x20-0x3C */
} HSUChanReg;

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;
    uint32_t dmasr;   /* DMA Status Register */
    uint32_t dmaisr;  /* DMA Interrupt Status Register */

    /* Hardware Register Shadows (The 'Identity' of the device) */
    HSUChanReg chan[HSU_DMA_NR_CHANNELS];

    /* DMA Context */
    /* placeholder: DMA state (descriptor processing, etc.) */

    /* Operational status flags */
    /* placeholder */

    /* Probe/reset state */
    /* placeholder */

    /* Power management state (D0-D3) */
    /* placeholder */

    /* placeholder */
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t mask = 0;
    int ch;

    for (ch = 0; ch < HSU_DMA_NR_CHANNELS; ch++) {
        uint32_t sr = s->chan[ch].sr;
        uint32_t dcr = s->chan[ch].dcr;
        bool int_pending = false;

        /* Check descriptor interrupts: CHDI(x) enables, SR DESCE_ANY bits per descriptor */
        for (int i = 0; i < 4; i++) {
            if (dcr & BIT(16 + i) && sr & BIT(16 + i)) {
                int_pending = true;
                break;
            }
        }
        if (!int_pending) {
            /* Check descriptor timeout interrupts: CHTOI(x) enables, SR DESCTO_ANY bits */
            for (int i = 0; i < 4; i++) {
                if (dcr & BIT(24 + i) && sr & BIT(8 + i)) {
                    int_pending = true;
                    break;
                }
            }
        }
        if (!int_pending) {
            /* Check channel halt error: CHEI enable, CHE in SR */
            if (dcr & BIT(23) && sr & BIT(15)) {
                int_pending = true;
            }
        }
        if (int_pending) {
            mask |= BIT(ch);
        }
    }
    s->dmaisr = mask;

    if (s->dmaisr) {
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

/* Device-initiated DMA logic based on driver access patterns */
/* (not used, removed) */

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr < 0x100) {
        /* Global registers */
        switch (addr) {
        case HSU_PCI_DMASR:
            val = s->dmasr;
            break;
        case HSU_PCI_DMAISR:
            val = s->dmaisr;
            break;
        default:
            val = 0;
            break;
        }
    } else if (addr >= HSU_PCI_CHAN_OFFSET && 
               addr < HSU_PCI_CHAN_OFFSET + HSU_DMA_NR_CHANNELS * HSU_DMA_CHAN_LENGTH) {
        /* Channel registers */
        uint32_t offset = addr - HSU_PCI_CHAN_OFFSET;
        int ch = offset / HSU_DMA_CHAN_LENGTH;
        uint32_t reg_off = offset % HSU_DMA_CHAN_LENGTH;
        HSUChanReg *chan = &s->chan[ch];

        if (size != 4) {
            return ~0ULL;
        }

        switch (reg_off) {
        case HSU_CH_SR:
            val = chan->sr;
            /* Errata: read clears interrupt status bits */
            chan->sr &= ~(HSU_CH_SR_DESCTO_ANY | HSU_CH_SR_CHE |
                          HSU_CH_SR_DESCE_ANY | HSU_CH_SR_CDESC_ANY);
            break;
        case HSU_CH_CR:
            val = chan->cr;
            break;
        case HSU_CH_DCR:
            val = chan->dcr;
            break;
        case HSU_CH_BSR:
            val = chan->bsr;
            break;
        case HSU_CH_MTSR:
            val = chan->mtsr;
            break;
        case 0x20 ... 0x3C: /* descriptor SAR/TSR */
            {
                int desc_idx = (reg_off - 0x20) / 8;
                int desc_reg = (reg_off - 0x20) % 8;
                if (desc_idx < 4) {
                    if (desc_reg == 0) {
                        val = chan->desc[desc_idx].sar;
                    } else if (desc_reg == 4) {
                        val = chan->desc[desc_idx].tsr;
                    }
                }
            }
            break;
        default:
            val = 0;
            break;
        }
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < 0x100) {
        /* Global registers */
        switch (addr) {
        case HSU_PCI_DMASR:
            s->dmasr = val;
            break;
        case HSU_PCI_DMAISR:
            s->dmaisr = val;
            break;
        default:
            break;
        }
        pcibase_update_irq(s);
    } else if (addr >= HSU_PCI_CHAN_OFFSET && 
               addr < HSU_PCI_CHAN_OFFSET + HSU_DMA_NR_CHANNELS * HSU_DMA_CHAN_LENGTH) {
        uint32_t offset = addr - HSU_PCI_CHAN_OFFSET;
        int ch = offset / HSU_DMA_CHAN_LENGTH;
        uint32_t reg_off = offset % HSU_DMA_CHAN_LENGTH;
        HSUChanReg *chan = &s->chan[ch];

        if (size != 4) {
            return;
        }

        switch (reg_off) {
        case HSU_CH_SR:
            chan->sr = val;
            break;
        case HSU_CH_CR:
            chan->cr = val;
            break;
        case HSU_CH_DCR:
            chan->dcr = val;
            break;
        case HSU_CH_BSR:
            chan->bsr = val;
            break;
        case HSU_CH_MTSR:
            chan->mtsr = val;
            break;
        case 0x20 ... 0x3C:
            {
                int desc_idx = (reg_off - 0x20) / 8;
                int desc_reg = (reg_off - 0x20) % 8;
                if (desc_idx < 4) {
                    if (desc_reg == 0) {
                        chan->desc[desc_idx].sar = val;
                    } else if (desc_reg == 4) {
                        chan->desc[desc_idx].tsr = val;
                    }
                }
            }
            break;
        default:
            break;
        }
        pcibase_update_irq(s);
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* PIO not used by driver */
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    /* PIO not used by driver */
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

    memset(s->chan, 0, sizeof(s->chan));
    s->dmasr = 0;
    s->dmaisr = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_INTEL );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_INTEL_MFLD_HSU_DMA );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0880 );
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
    s->bar_info[0].size = HSU_PCI_BAR0_SIZE;
    s->bar_info[0].name = "hsu-mmio";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI/MSI-X initialization: support 1 MSI vector */
    if (msi_init(pdev, 0, 1, true, false, errp) < 0) {
        /* MSI failed, will use INTx; not fatal */
    }

    /* Initialize channel registers to zero */
    memset(s->chan, 0, sizeof(s->chan));
    s->dmasr = 0;
    s->dmaisr = 0;
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

    /* Nothing else to clean up */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "hsu_dma_pci_pci",
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
