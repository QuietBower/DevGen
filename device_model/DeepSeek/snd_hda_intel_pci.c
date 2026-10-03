/*
 * QEMU 8.2.10 PCI device model for Intel HDA controller (Phase 4 fix).
 * Generated from driver: /home/eely/linux-7.1/sound/hda/controllers/intel.c
 * Fixed Device ID to ICH6 (0x2668) to avoid GPU binding requirement.
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

#define TYPE_PCIBASE_DEVICE "snd_hda_intel_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI vendor/device IDs (using first entry from driver's pci_device_id table) */
#define PCI_VENDOR_ID_INTEL          0x8086
#define DEVICE_ID_INTEL_HDA          0x2668   /* Intel ICH6 (no I915 dependency) */
#define CLASS_ID_HDA                 0x0403   /* PCI_CLASS_MULTIMEDIA_HD_AUDIO */

/* PCI config register offsets (vendor specific) */
#define ATI_SB450_HDAUDIO_MISC_CNTR2_ADDR   0x42
#define ATI_SB450_HDAUDIO_ENABLE_SNOOP      0x02
#define NVIDIA_HDA_TRANSREG_ADDR      0x4e
#define NVIDIA_HDA_ENABLE_COHBITS     0x0f
#define NVIDIA_HDA_ISTRM_COH          0x4d
#define NVIDIA_HDA_OSTRM_COH          0x4c
#define NVIDIA_HDA_ENABLE_COHBIT      0x01
#define INTEL_HDA_CGCTL  0x48
#define INTEL_HDA_CGCTL_MISCBDCGE   (0x1 << 6)
#define INTEL_SCH_HDA_DEVC   0x78
#define INTEL_SCH_HDA_DEVC_NOSNOOP   (0x1 << 11)
#define AZX_PCIREG_TCSEL    0x44   /* TCSEL offset from driver comment */

/* PCIBaseState - device shadow state */
struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];
    MemoryRegion hda_mmio;

    bool has_msi;
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Shadows for driver-accessible PCI config registers */
    uint32_t cgctl;       /* INTEL_HDA_CGCTL */
    uint32_t devc;        /* INTEL_SCH_HDA_DEVC */
    uint8_t  tcsell;      /* AZX_PCIREG_TCSEL */
    uint8_t  misc_cntr2;  /* ATI_SB450 HDAUDIO_MISC_CNTR2 */
    uint8_t  transreg;    /* NVIDIA HDA TRANSREG */
    uint8_t  istrm_coh;   /* NVIDIA ISTRM_COH */
    uint8_t  ostrm_coh;   /* NVIDIA OSTRM_COH */

    uint32_t status;
    int reset_flag;
    uint32_t power_state;
};

/* IRQ update logic placeholder (no specific driver mask/status registers yet) */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->intr_status & s->intr_mask) {
        if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* PCI config read override */
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t address, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    switch (address) {
    case INTEL_HDA_CGCTL:         return s->cgctl;
    case INTEL_SCH_HDA_DEVC:      return s->devc;
    case AZX_PCIREG_TCSEL:        return s->tcsell;
    case ATI_SB450_HDAUDIO_MISC_CNTR2_ADDR:
        return s->misc_cntr2;
    case NVIDIA_HDA_TRANSREG_ADDR: return s->transreg;
    case NVIDIA_HDA_ISTRM_COH:    return s->istrm_coh;
    case NVIDIA_HDA_OSTRM_COH:    return s->ostrm_coh;
    default:
        return pci_default_read_config(pdev, address, len);
    }
}

/* PCI config write override */
static void pcibase_config_write(PCIDevice *pdev, uint32_t address,
                                 uint32_t val, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    switch (address) {
    case INTEL_HDA_CGCTL:
        s->cgctl = val;
        break;
    case INTEL_SCH_HDA_DEVC:
        s->devc = val;
        break;
    case AZX_PCIREG_TCSEL:
        s->tcsell = (uint8_t)val;
        break;
    case ATI_SB450_HDAUDIO_MISC_CNTR2_ADDR:
        s->misc_cntr2 = (uint8_t)val;
        break;
    case NVIDIA_HDA_TRANSREG_ADDR:
        s->transreg = (uint8_t)val;
        break;
    case NVIDIA_HDA_ISTRM_COH:
        s->istrm_coh = (uint8_t)val;
        break;
    case NVIDIA_HDA_OSTRM_COH:
        s->ostrm_coh = (uint8_t)val;
        break;
    default:
        break;
    }
    pci_default_write_config(pdev, address, val, len);
}

/* MMIO read stub: returns 0 until HDA registers are implemented */
static uint64_t pcibase_hda_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    return 0;
}

/* MMIO write stub: ignores writes until HDA registers are implemented */
static void pcibase_hda_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                                   unsigned size)
{
    PCIBaseState *s = opaque;
}

static const MemoryRegionOps pcibase_hda_mmio_ops = {
    .read = pcibase_hda_mmio_read,
    .write = pcibase_hda_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    s->cgctl = 0;
    s->devc = 0;
    s->tcsell = 0;
    s->misc_cntr2 = 0;
    s->transreg = 0;
    s->istrm_coh = 0;
    s->ostrm_coh = 0;
    s->intr_status = 0;
    s->intr_mask = 0;
    s->status = 0;
    s->power_state = 0;
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pdev->config_read = pcibase_config_read;
    pdev->config_write = pcibase_config_write;

    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_INTEL);
    pci_set_word(pci_conf + PCI_DEVICE_ID, DEVICE_ID_INTEL_HDA);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID_HDA);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR0: HDA MMIO region (stub) */
    memory_region_init_io(&s->hda_mmio, OBJECT(s), &pcibase_hda_mmio_ops, s,
                          "hda-mmio", 0x4000);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->hda_mmio);

    if (msi_init(pdev, 0, 1, true, true, errp)) {
        error_report("MSI init failed");
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

static const VMStateDescription vmstate_pcibase = {
    .name = "snd_hda_intel_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(cgctl, PCIBaseState),
        VMSTATE_UINT32(devc, PCIBaseState),
        VMSTATE_UINT8(tcsell, PCIBaseState),
        VMSTATE_UINT8(misc_cntr2, PCIBaseState),
        VMSTATE_UINT8(transreg, PCIBaseState),
        VMSTATE_UINT8(istrm_coh, PCIBaseState),
        VMSTATE_UINT8(ostrm_coh, PCIBaseState),
        VMSTATE_UINT32(intr_status, PCIBaseState),
        VMSTATE_UINT32(intr_mask, PCIBaseState),
        VMSTATE_UINT32(status, PCIBaseState),
        VMSTATE_UINT32(power_state, PCIBaseState),
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
