/*
 * QEMU PCI device model for snd_hda_intel_pci - behavioral implementation
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

#include "hw/pci/pci_ids.h"

#define TYPE_PCIBASE_DEVICE "snd_hda_intel_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCIBASE_VENDOR_ID      PCI_VENDOR_ID_INTEL
#define PCIBASE_DEVICE_ID      0x1c20
#define PCIBASE_CLASS_ID       (PCI_CLASS_MULTIMEDIA_AUDIO)

#define ATI_SB450_HDAUDIO_MISC_CNTR2_ADDR   0x42
#define ATI_SB450_HDAUDIO_ENABLE_SNOOP      0x02
#define NVIDIA_HDA_TRANSREG_ADDR            0x4e
#define NVIDIA_HDA_ENABLE_COHBITS           0x0f
#define NVIDIA_HDA_ISTRM_COH                0x4d
#define NVIDIA_HDA_OSTRM_COH                0x4c
#define NVIDIA_HDA_ENABLE_COHBIT            0x01
#define INTEL_HDA_CGCTL                     0x48
#define INTEL_HDA_CGCTL_MISCBDCGE           (0x1 << 6)
#define INTEL_SCH_HDA_DEVC                  0x78
#define INTEL_SCH_HDA_DEVC_NOSNOOP          (0x1 << 11)

#define PCIBASE_BAR_INDEX       0
#define PCIBASE_BAR_SIZE        (1 * MiB)

#define ICH6_NUM_CAPTURE        4
#define ICH6_NUM_PLAYBACK       4
#define ULI_NUM_CAPTURE         5
#define ULI_NUM_PLAYBACK        6
#define ATIHDMI_NUM_CAPTURE     0
#define ATIHDMI_NUM_PLAYBACK    8
#define AMD_FIFO_SIZE           32
#define AZX_FORCE_CODEC_MASK    0x100

#define AZX_DCAPS_SNOOP_MASK           (3 << 10)
#define AZX_DCAPS_OLD_SSYNC            (1 << 20)
#define AZX_DCAPS_NO_ALIGN_BUFSIZE     (1 << 21)
#define AZX_DCAPS_COUNT_LPIB_DELAY     (1 << 25)
#define AZX_DCAPS_I915_COMPONENT       (1 << 13)
#define AZX_DCAPS_PM_RUNTIME           (1 << 26)
#define AZX_DCAPS_SEPARATE_STREAM_TAG  (1 << 30)
#define AZX_DCAPS_PIO_COMMANDS         (1 << 31)
#define AZX_DCAPS_NO_TCSEL             (1 << 8)
#define AZX_DCAPS_POSFIX_LPIB          (1 << 16)
#define AZX_DCAPS_NO_MSI64             (1 << 29)
#define AZX_DCAPS_SNOOP_OFF            (1 << 12)
#define AZX_DCAPS_AMD_WORKAROUND       (1 << 17)
#define AZX_DCAPS_RETRY_PROBE          (1 << 27)
#define AZX_DCAPS_NO_MSI               (1 << 9)
#define AZX_DCAPS_CORBRP_SELF_CLEAR    (1 << 28)
#define AZX_DCAPS_NO_64BIT             (1 << 18)
#define AZX_DCAPS_4K_BDLE_BOUNDARY     (1 << 23)

#define AZX_DCAPS_SNOOP_TYPE(type)    ((AZX_SNOOP_TYPE_ ## type) << 10)
#define AZX_DCAPS_INTEL_ICH \
    (AZX_DCAPS_OLD_SSYNC | AZX_DCAPS_NO_ALIGN_BUFSIZE)
#define AZX_DCAPS_INTEL_PCH_BASE \
    (AZX_DCAPS_NO_ALIGN_BUFSIZE | AZX_DCAPS_COUNT_LPIB_DELAY | \
     AZX_DCAPS_SNOOP_TYPE(SCH))
#define AZX_DCAPS_INTEL_PCH_NOPM \
    (AZX_DCAPS_INTEL_PCH_BASE | AZX_DCAPS_I915_COMPONENT)
#define AZX_DCAPS_INTEL_PCH \
    (AZX_DCAPS_INTEL_PCH_BASE | AZX_DCAPS_PM_RUNTIME)
#define AZX_DCAPS_INTEL_HASWELL \
    (AZX_DCAPS_COUNT_LPIB_DELAY | \
     AZX_DCAPS_PM_RUNTIME | AZX_DCAPS_I915_COMPONENT | \
     AZX_DCAPS_SNOOP_TYPE(SCH))
#define AZX_DCAPS_INTEL_BROADWELL \
    (AZX_DCAPS_POSFIX_LPIB | \
     AZX_DCAPS_PM_RUNTIME | AZX_DCAPS_I915_COMPONENT | \
     AZX_DCAPS_SNOOP_TYPE(SCH))
#define AZX_DCAPS_INTEL_BAYTRAIL \
    (AZX_DCAPS_INTEL_PCH_BASE | AZX_DCAPS_I915_COMPONENT)
#define AZX_DCAPS_INTEL_BRASWELL \
    (AZX_DCAPS_INTEL_PCH_BASE | AZX_DCAPS_PM_RUNTIME | \
     AZX_DCAPS_I915_COMPONENT)
#define AZX_DCAPS_INTEL_SKYLAKE \
    (AZX_DCAPS_INTEL_PCH_BASE | AZX_DCAPS_PM_RUNTIME | \
     AZX_DCAPS_SEPARATE_STREAM_TAG | AZX_DCAPS_I915_COMPONENT)
#define AZX_DCAPS_INTEL_BROXTON      AZX_DCAPS_INTEL_SKYLAKE
#define AZX_DCAPS_INTEL_LNL \
    (AZX_DCAPS_INTEL_SKYLAKE | AZX_DCAPS_PIO_COMMANDS)
#define AZX_DCAPS_INTEL_NVL \
    (AZX_DCAPS_INTEL_LNL & ~AZX_DCAPS_NO_ALIGN_BUFSIZE)
#define AZX_DCAPS_PRESET_ATI_SB \
    (AZX_DCAPS_NO_TCSEL | AZX_DCAPS_POSFIX_LPIB | \
     AZX_DCAPS_SNOOP_TYPE(ATI))
#define AZX_DCAPS_PRESET_ATI_HDMI \
    (AZX_DCAPS_NO_TCSEL | AZX_DCAPS_POSFIX_LPIB | \
     AZX_DCAPS_NO_MSI64)
#define AZX_DCAPS_PRESET_ATI_HDMI_NS \
    (AZX_DCAPS_PRESET_ATI_HDMI | AZX_DCAPS_SNOOP_OFF)
#define AZX_DCAPS_PRESET_AMD_SB \
    (AZX_DCAPS_NO_TCSEL | AZX_DCAPS_AMD_WORKAROUND | \
     AZX_DCAPS_SNOOP_TYPE(ATI) | AZX_DCAPS_PM_RUNTIME | \
     AZX_DCAPS_RETRY_PROBE)
#define AZX_DCAPS_PRESET_NVIDIA \
    (AZX_DCAPS_NO_MSI | AZX_DCAPS_CORBRP_SELF_CLEAR | \
     AZX_DCAPS_SNOOP_TYPE(NVIDIA))
#define AZX_DCAPS_PRESET_CTHDA \
    (AZX_DCAPS_NO_MSI | AZX_DCAPS_POSFIX_LPIB | \
     AZX_DCAPS_NO_64BIT | \
     AZX_DCAPS_4K_BDLE_BOUNDARY | AZX_DCAPS_SNOOP_OFF)

/* From new driver snippet */
#define AZX_GCAP_64OK        (1 << 0)
#define AZX_PCIREG_TCSEL     0x44

/* Basic HDA core registers that intel.c interacts with indirectly:
 * Only STATESTS and WAKEEN are referenced here (via azx_readw/writew).
 * GCAP is read in azx_first_init(); we provide a simple constant.
 */
#define HDA_REG_GCAP      0x0000 /* Global Capabilities (16-bit) */
#define HDA_REG_STATESTS  0x0004 /* State Change Status (16-bit) */
#define HDA_REG_WAKEEN    0x0006 /* Wake Enable (16-bit) */

/* New driver constant provided in this iteration */
#define HDA_MAX_CODECS        8

/* The driver uses STATESTS_INT_MASK in WAKEEN writes; that mask itself is
 * defined in other headers, but we don't need the value here since only the
 * driver manipulates bits. We just expose the registers.
 */

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

    /* PCI config-byte shadows touched by intel.c (snoop / TCSEL / CGCTL / DEVC) */
    uint8_t tcsel;
    uint8_t ati_misc_cntl2;
    uint8_t nvidia_transreg;
    uint8_t nvidia_istrm_coh;
    uint8_t nvidia_ostrm_coh;
    uint16_t sch_devc;
    uint32_t intel_cgctl;

    /* Minimal HDA register shadow subset required by intel.c */
    uint16_t gcap;
    uint16_t statests;
    uint16_t wakeen;

    /* Interrupt state */
    uint32_t irq_status;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    if (s->irq_status) {
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

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case HDA_REG_GCAP:
        if (size == 2) {
            return s->gcap;
        }
        break;
    case HDA_REG_STATESTS:
        if (size == 2) {
            return s->statests;
        }
        break;
    case HDA_REG_WAKEEN:
        if (size == 2) {
            return s->wakeen;
        }
        break;
    default:
        break;
    }

    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case HDA_REG_STATESTS:
        if (size == 2) {
            /* Typical HDA semantics are W1C, but intel.c only reads this
             * register via azx_readw(). It doesn't write it directly here,
             * so we just store the value.
             */
            s->statests = (uint16_t)val;
        }
        break;
    case HDA_REG_WAKEEN:
        if (size == 2) {
            s->wakeen = (uint16_t)val;
        }
        break;
    case HDA_REG_GCAP:
        if (size == 2) {
            s->gcap = (uint16_t)val;
        }
        break;
    default:
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    (void)opaque;
    (void)addr;
    (void)size;
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    (void)opaque;
    (void)addr;
    (void)val;
    (void)size;
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
    PCIDevice *pdev = PCI_DEVICE(dev);
    uint8_t *pci_conf = pdev->config;

    pci_device_reset(pdev);

    /* Reinitialize our HDA core shadows to simple sane defaults that
     * satisfy azx_first_init(): gcap must expose at least one playback
     * and capture stream, and 64-bit DMA capability is allowed.
     * We choose: playback_streams=4 (bits 15:12), capture_streams=4 (bits 11:8),
     * and set the 64OK bit (AZX_GCAP_64OK) to indicate 64-bit capable.
     */
    s->gcap = 0;
    s->gcap |= (4 & 0x0f) << 12; /* playback */
    s->gcap |= (4 & 0x0f) << 8;  /* capture  */
    s->gcap |= AZX_GCAP_64OK;    /* 64-bit capable */

    s->statests = 0;
    s->wakeen = 0;

    /* PCI config shadows */
    s->tcsel = 0;
    s->ati_misc_cntl2 = 0;
    s->nvidia_transreg = 0;
    s->nvidia_istrm_coh = 0;
    s->nvidia_ostrm_coh = 0;
    s->sch_devc = 0;
    s->intel_cgctl = 0;

    /* Keep PCI config space and our shadows consistent */
    pci_conf[AZX_PCIREG_TCSEL] = s->tcsel;
    pci_conf[ATI_SB450_HDAUDIO_MISC_CNTR2_ADDR] = s->ati_misc_cntl2;
    pci_conf[NVIDIA_HDA_TRANSREG_ADDR] = s->nvidia_transreg;
    pci_conf[NVIDIA_HDA_ISTRM_COH] = s->nvidia_istrm_coh;
    pci_conf[NVIDIA_HDA_OSTRM_COH] = s->nvidia_ostrm_coh;
    pci_set_word(pci_conf + INTEL_SCH_HDA_DEVC, s->sch_devc);
    pci_set_long(pci_conf + INTEL_HDA_CGCTL, s->intel_cgctl);

    s->irq_status = 0;
    pcibase_update_irq(s);
}

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

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCIBASE_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCIBASE_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCIBASE_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    s->num_bars = 1;
    s->bar_info[0].index = PCIBASE_BAR_INDEX;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    s->bar_info[0].size  = PCIBASE_BAR_SIZE;
    s->bar_info[0].name  = "hda-intel-mmio";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type  = BAR_TYPE_NONE;
        s->bar_info[i].size  = 0;
        s->bar_info[i].name  = NULL;
    }

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Enable MSI if the guest driver chooses to; msix not used here. */
    s->has_msi = true;
    s->has_msix = false;

    if (s->has_msi) {
        Error *local_err = NULL;
        if (msi_init(pdev, 0, 1, true, false, &local_err)) {
            if (local_err) {
                error_propagate(errp, local_err);
            }
        }
    }

    /* Initialize runtime state via reset helper */
    pcibase_reset(DEVICE(pdev));
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    (void)s;

    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
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
        VMSTATE_UINT16(gcap, PCIBaseState),
        VMSTATE_UINT16(statests, PCIBaseState),
        VMSTATE_UINT16(wakeen, PCIBaseState),
        VMSTATE_UINT32(irq_status, PCIBaseState),
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

type_init(pcibase_register_types)
