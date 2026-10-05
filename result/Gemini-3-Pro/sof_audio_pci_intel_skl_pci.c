/*
 * QEMU PCI device model for Intel Skylake Audio DSP (SOF)
 * Emulates behavior required by linux/sound/soc/sof/intel/pci-skl.c
 *
 * Target: QEMU 8.2.10
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

#define TYPE_PCIBASE_DEVICE "sof_audio_pci_intel_skl_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI_VENDOR_ID_INTEL 0x8086
#ifndef HDA_SKL_LP
#define HDA_SKL_LP 0x9d70
#endif

/* Define PCI Class if missing from environment headers */
#ifndef PCI_CLASS_MULTIMEDIA_HD_AUDIO
#define PCI_CLASS_MULTIMEDIA_HD_AUDIO 0x0403
#endif

#define HDA_DSP_BAR             4
#define HDA_DSP_PP_BAR          1
#define HDA_DSP_HDA_BAR         0
#define SND_SOF_BARS            8

#define HDA_DSP_IPC_BASE        0x40
#define HDA_DSP_REG_HIPCT       (HDA_DSP_IPC_BASE + 0x00) /* 0x40 */
#define HDA_DSP_REG_HIPCTE      (HDA_DSP_IPC_BASE + 0x04) /* 0x44 */
#define HDA_DSP_REG_HIPCI       (HDA_DSP_IPC_BASE + 0x08) /* 0x48 */
#define HDA_DSP_REG_HIPCIE      (HDA_DSP_IPC_BASE + 0x0C) /* 0x4C */
#define HDA_DSP_REG_HIPCCTL     (HDA_DSP_IPC_BASE + 0x10) /* 0x50 */

#define HDA_DSP_REG_HIPCT_BUSY      BIT(31)
#define HDA_DSP_REG_HIPCT_MSG_MASK  0x7FFFFFFF
#define HDA_DSP_REG_HIPCTE_MSG_MASK 0x3FFFFFFF
#define HDA_DSP_REG_HIPCI_BUSY      BIT(31)
#define HDA_DSP_REG_HIPCIE_DONE     BIT(30)
#define HDA_DSP_REG_HIPCCTL_BUSY    BIT(0)
#define HDA_DSP_REG_HIPCCTL_DONE    BIT(1)

#define HDA_DSP_SRAM_REG_ROM_STATUS_SKL 0x8000
#define HDA_DSP_MBOX_OFFSET             0x80000
#define HDA_DSP_SRAM_REG_ROM_ERROR      (HDA_DSP_MBOX_OFFSET + 0x4)

#define SOF_HDA_INTCTL          0x20
#define SOF_HDA_INTSTS          0x24
#define SOF_HDA_REG_PP_PPSTS    0x08

#define PCI_CGCTL               0x48
#define PCI_TCSEL               0x44
#define PCI_PGCTL               PCI_TCSEL

#define SOF_HDA_ADSP_LOADER_BASE        0x80
#define SOF_HDA_ADSP_REG_SD_CTL         0x00
#define SOF_HDA_ADSP_REG_SD_CBL         0x08
#define SOF_HDA_ADSP_REG_SD_LVI         0x0C
#define SOF_HDA_ADSP_REG_SD_BDLPL       0x18
#define SOF_HDA_ADSP_REG_SD_BDLPU       0x1C
#define SOF_HDA_ADSP_REG_SD_STS         0x03

#define SOF_DSP_REG_CL_SPBFIFO          (SOF_HDA_ADSP_LOADER_BASE + 0x20)

/* Simulated DSP ROM Status (FW_ENTERED = 5) */
#define SOF_ROM_INIT_DONE       0x5

/* Interrupt Status Bit for DSP (Arbitrary choice within INTSTS) */
#define HDA_DSP_INTSTS_BIT      BIT(30)

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

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[SND_SOF_BARS];
    BARInfo bar_info[SND_SOF_BARS];
    int num_bars;

    bool has_msi;
    bool has_msix;

    /* Registers */
    uint32_t hipct;
    uint32_t hipcte;
    uint32_t hipci;
    uint32_t hipcie;
    uint32_t hipcctl;
    
    uint32_t intctl;
    uint32_t intsts;

    /* Mailbox / SRAM backing */
    uint8_t mbox[0x1000];
};

static void pcibase_update_irq(PCIBaseState *s)
{
    /* Simple IRQ logic: If INTSTS has bits set and INTCTL enables global IRQ */
    /* Note: Standard HDA INTCTL bit 31 is Global Interrupt Enable (GIE) */
    bool gie = (s->intctl & BIT(31)) != 0;
    bool pending = (s->intsts & ~BIT(31)) != 0; /* Ignore CIS bit for now */

    if (gie && pending) {
        if (s->has_msi) {
            msi_notify(PCI_DEVICE(s), 0);
        } else {
            pci_set_irq(PCI_DEVICE(s), 1);
        }
    } else {
        pci_set_irq(PCI_DEVICE(s), 0);
    }
}

/* ------------------------------------------------------------------ */
/* BAR 0: HDA Registers (INTCTL, INTSTS)                              */
/* ------------------------------------------------------------------ */
static uint64_t hda_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case SOF_HDA_INTCTL:
        val = s->intctl;
        break;
    case SOF_HDA_INTSTS:
        val = s->intsts;
        break;
    default:
        /* Log unimplemented HDA reads as 0 */
        break;
    }
    return val;
}

static void hda_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case SOF_HDA_INTCTL:
        s->intctl = val;
        pcibase_update_irq(s);
        break;
    case SOF_HDA_INTSTS:
        /* W1C (Write 1 to Clear) behavior for status bits */
        s->intsts &= ~val;
        pcibase_update_irq(s);
        break;
    default:
        break;
    }
}

static const MemoryRegionOps hda_mmio_ops = {
    .read = hda_mmio_read,
    .write = hda_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

/* ------------------------------------------------------------------ */
/* BAR 4: DSP Registers (IPC, SRAM, ROM Status)                       */
/* ------------------------------------------------------------------ */
static uint64_t dsp_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Handle Mailbox/SRAM read */
    if (addr >= HDA_DSP_MBOX_OFFSET && addr < (HDA_DSP_MBOX_OFFSET + sizeof(s->mbox))) {
        if (size <= 8) {
            memcpy(&val, s->mbox + (addr - HDA_DSP_MBOX_OFFSET), size);
            return val;
        }
    }

    switch (addr) {
    case HDA_DSP_REG_HIPCT:
        val = s->hipct;
        break;
    case HDA_DSP_REG_HIPCTE:
        val = s->hipcte;
        break;
    case HDA_DSP_REG_HIPCI:
        val = s->hipci;
        break;
    case HDA_DSP_REG_HIPCIE:
        val = s->hipcie;
        break;
    case HDA_DSP_REG_HIPCCTL:
        val = s->hipcctl;
        break;
    case HDA_DSP_SRAM_REG_ROM_STATUS_SKL:
        /* Driver checks this to verify DSP boot */
        val = SOF_ROM_INIT_DONE;
        break;
    default:
        break;
    }
    return val;
}

static void dsp_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* Handle Mailbox/SRAM write */
    if (addr >= HDA_DSP_MBOX_OFFSET && addr < (HDA_DSP_MBOX_OFFSET + sizeof(s->mbox))) {
        if (size <= 8) {
            memcpy(s->mbox + (addr - HDA_DSP_MBOX_OFFSET), &val, size);
            return;
        }
    }

    switch (addr) {
    case HDA_DSP_REG_HIPCT:
        s->hipct = val;
        break;
    case HDA_DSP_REG_HIPCTE:
        s->hipcte = val;
        break;
    case HDA_DSP_REG_HIPCI:
        /* Host Initiator: Host writes here to send msg to DSP */
        s->hipci = val;
        if (val & HDA_DSP_REG_HIPCI_BUSY) {
            /* Auto-acknowledge: Clear BUSY, set DONE in HIPCIE */
            s->hipci &= ~HDA_DSP_REG_HIPCI_BUSY;
            s->hipcie |= HDA_DSP_REG_HIPCIE_DONE;
            
            /* Raise Interrupt in HDA BAR */
            s->intsts |= HDA_DSP_INTSTS_BIT;
            pcibase_update_irq(s);
        }
        break;
    case HDA_DSP_REG_HIPCIE:
        /* Host writes here to clear DONE bit */
        if (val & HDA_DSP_REG_HIPCIE_DONE) {
            s->hipcie &= ~HDA_DSP_REG_HIPCIE_DONE;
        }
        /* Preserve other bits if any (masking logic simplified) */
        break;
    case HDA_DSP_REG_HIPCCTL:
        s->hipcctl = val;
        break;
    default:
        break;
    }
}

static const MemoryRegionOps dsp_mmio_ops = {
    .read = dsp_mmio_read,
    .write = dsp_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

/* ------------------------------------------------------------------ */
/* BAR 1: PP Registers (Stub)                                         */
/* ------------------------------------------------------------------ */
static uint64_t pp_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pp_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Stub */
}

static const MemoryRegionOps pp_mmio_ops = {
    .read = pp_mmio_read,
    .write = pp_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

/* ------------------------------------------------------------------ */
/* Reset                                                              */
/* ------------------------------------------------------------------ */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    pci_device_reset(pdev);

    s->hipct = 0;
    s->hipcte = 0;
    s->hipci = 0;
    s->hipcie = 0;
    s->hipcctl = 0;
    s->intctl = 0;
    s->intsts = 0;
    memset(s->mbox, 0, sizeof(s->mbox));
}

/* ------------------------------------------------------------------ */
/* Realize                                                            */
/* ------------------------------------------------------------------ */
static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_INTEL);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  HDA_SKL_LP);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_MULTIMEDIA_HD_AUDIO);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    if (msi_init(pdev, 0, 1, true, false, errp) == 0) {
        s->has_msi = true;
    }

    /* BAR 0: HDA */
    memory_region_init_io(&s->bar_regions[HDA_DSP_HDA_BAR], OBJECT(s), 
                          &hda_mmio_ops, s, "sof-hda", 0x4000);
    pci_register_bar(pdev, HDA_DSP_HDA_BAR, PCI_BASE_ADDRESS_SPACE_MEMORY, 
                     &s->bar_regions[HDA_DSP_HDA_BAR]);

    /* BAR 1: PP */
    memory_region_init_io(&s->bar_regions[HDA_DSP_PP_BAR], OBJECT(s), 
                          &pp_mmio_ops, s, "sof-pp", 0x4000);
    pci_register_bar(pdev, HDA_DSP_PP_BAR, PCI_BASE_ADDRESS_SPACE_MEMORY, 
                     &s->bar_regions[HDA_DSP_PP_BAR]);

    /* BAR 4: DSP */
    memory_region_init_io(&s->bar_regions[HDA_DSP_BAR], OBJECT(s), 
                          &dsp_mmio_ops, s, "sof-dsp", 0x100000);
    pci_register_bar(pdev, HDA_DSP_BAR, PCI_BASE_ADDRESS_SPACE_MEMORY, 
                     &s->bar_regions[HDA_DSP_BAR]);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (s->has_msi) {
        msi_uninit(pdev);
        s->has_msi = false;
    }
}

static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

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
