/*
 * Integrated QEMU PCI device template (QEMU 8.2.10).
 * Designed for register-level modeling and PCIe driver probing.
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

/* Register Layout and Hardware Identifiers extracted from driver source */
#define ATI_SB450_HDAUDIO_MISC_CNTR2_ADDR   0x42
#define ATI_SB450_HDAUDIO_ENABLE_SNOOP      0x02
#define NVIDIA_HDA_TRANSREG_ADDR      0x4e
#define NVIDIA_HDA_ENABLE_COHBITS     0x0f
#define NVIDIA_HDA_ISTRM_COH          0x4d
#define NVIDIA_HDA_OSTRM_COH          0x4c
#define NVIDIA_HDA_ENABLE_COHBIT      0x01
#define INTEL_HDA_CGCTL	 0x48
#define INTEL_HDA_CGCTL_MISCBDCGE        (0x1 << 6)
#define INTEL_SCH_HDA_DEVC      0x78
#define INTEL_SCH_HDA_DEVC_NOSNOOP       (0x1<<11)
#define AZX_GCAP_64OK		(1 << 0)

#define AZX_ML_BASE			0x40
#define AZX_REG_ML_LCAP			0x00
#define AZX_REG_ML_LCTL			0x04
#define AZX_ML_LCTL_SPA			(1 << 16)
#define AZX_ML_LCTL_SPA_SHIFT		16
#define AZX_ML_LCTL_CPA			(1 << 23)
#define AZX_ML_LCTL_CPA_SHIFT		23
#define AZX_ML_LCTL_SCF			0x0F
#define AZX_PCIREG_TCSEL		0x44

#define RIRB_INT_MASK		0x05
#define RIRB_INT_RESPONSE	0x01
#define HDA_MAX_CODECS		8

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
    bool irq_pending;
    unsigned int irq_pending_warned:1;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint8_t cgctl;
    uint8_t devc;
    uint32_t gcap;
    uint32_t wallclk;
    uint16_t statests;
    uint16_t wakeen;
    uint32_t vs_em4l;
    uint32_t ml_lcap;
    uint32_t ml_lctl;

    unsigned int init_failed:1;
    unsigned int freed:1;
    unsigned int probe_continued:1;
    int probe_retry;
    unsigned int runtime_pm_disabled:1;
    bool need_i915_power:1;
    unsigned int use_vga_switcheroo:1;
    unsigned int vga_switcheroo_registered:1;
};

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

#ifdef GCAP
    if (addr == GCAP) return s->gcap;
#endif
#ifdef WALLCLK
    if (addr == WALLCLK) return s->wallclk;
#endif
#ifdef STATESTS
    if (addr == STATESTS) return s->statests;
#endif
#ifdef WAKEEN
    if (addr == WAKEEN) return s->wakeen;
#endif
#ifdef VS_EM4L
    if (addr == VS_EM4L) return s->vs_em4l;
#endif

    if (addr == (AZX_ML_BASE + AZX_REG_ML_LCAP)) return s->ml_lcap;
    if (addr == (AZX_ML_BASE + AZX_REG_ML_LCTL)) return s->ml_lctl;

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

#ifdef WAKEEN
    if (addr == WAKEEN) {
        s->wakeen = val;
        return;
    }
#endif
#ifdef VS_EM4L
    if (addr == VS_EM4L) {
        s->vs_em4l = val;
        return;
    }
#endif

    if (addr == (AZX_ML_BASE + AZX_REG_ML_LCTL)) {
        s->ml_lctl = val;
        /* Emulate hardware CPA matching SPA immediately to satisfy driver polling */
        uint32_t spa = (val & AZX_ML_LCTL_SPA) >> AZX_ML_LCTL_SPA_SHIFT;
        s->ml_lctl &= ~AZX_ML_LCTL_CPA;
        s->ml_lctl |= (spa << AZX_ML_LCTL_CPA_SHIFT);
        return;
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

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Initialize GCAP to support 64-bit DMA and indicate 1 playback/1 capture stream */
    s->gcap = AZX_GCAP_64OK | (1 << 8) | (1 << 12);
    s->wallclk = 0;
    s->statests = 0;
    s->wakeen = 0;
    s->vs_em4l = 0;
    s->ml_lcap = 0;
    s->ml_lctl = 0;
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
#ifdef HDA_CPT
    pci_set_word(pci_conf + PCI_DEVICE_ID,  HDA_CPT );
#else
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x1c20 ); /* Fallback */
#endif
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0403 );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* Initialize specific PCI config registers used by driver */
    pci_set_long(pci_conf + INTEL_HDA_CGCTL, 0);
    pci_set_word(pci_conf + INTEL_SCH_HDA_DEVC, 0);

    /* BAR Initialization */
    if (s->num_bars == 0) {
        s->num_bars = 1;
        s->bar_info[0].index = 0;
        s->bar_info[0].type = BAR_TYPE_MMIO;
        s->bar_info[0].size = 0x4000;
        s->bar_info[0].name = "hda-mmio";
    }
      
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = true;
    if (s->has_msi) {
        msi_init(pdev, 0, 1, true, false, errp);
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
    .name = "snd_hda_intel_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(gcap, PCIBaseState),
        VMSTATE_UINT32(wallclk, PCIBaseState),
        VMSTATE_UINT16(statests, PCIBaseState),
        VMSTATE_UINT16(wakeen, PCIBaseState),
        VMSTATE_UINT32(vs_em4l, PCIBaseState),
        VMSTATE_UINT32(ml_lcap, PCIBaseState),
        VMSTATE_UINT32(ml_lctl, PCIBaseState),
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
