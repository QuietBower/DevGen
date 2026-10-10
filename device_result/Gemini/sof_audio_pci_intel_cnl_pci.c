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
#include "hw/pci/pci_ids.h"

#define TYPE_PCIBASE_DEVICE "sof_audio_pci_intel_cnl_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define CNL_DSP_IPC_BASE		0xc0
#define CNL_DSP_REG_HIPCTDR		(CNL_DSP_IPC_BASE + 0x00)
#define CNL_DSP_REG_HIPCTDA		(CNL_DSP_IPC_BASE + 0x04)
#define CNL_DSP_REG_HIPCTDD		(CNL_DSP_IPC_BASE + 0x08)
#define CNL_DSP_REG_HIPCIDR		(CNL_DSP_IPC_BASE + 0x10)
#define CNL_DSP_REG_HIPCIDA		(CNL_DSP_IPC_BASE + 0x14)
#define CNL_DSP_REG_HIPCIDD		(CNL_DSP_IPC_BASE + 0x18)
#define CNL_DSP_REG_HIPCCTL		(CNL_DSP_IPC_BASE + 0x28)

#define HDA_DSP_GEN_BASE		0x0
#define HDA_DSP_REG_ADSPCS		(HDA_DSP_GEN_BASE + 0x04)
#define HDA_DSP_REG_ADSPIC		(HDA_DSP_GEN_BASE + 0x08)
#define HDA_DSP_REG_ADSPIS		(HDA_DSP_GEN_BASE + 0x0C)
#define HDA_DSP_REG_ADSPIC2		(HDA_DSP_GEN_BASE + 0x10)
#define HDA_DSP_REG_ADSPIS2		(HDA_DSP_GEN_BASE + 0x14)

#define SOF_HDA_INTCTL			0x20
#define SOF_HDA_INTSTS			0x24

#define HDA_DSP_HDA_BAR			0
#define HDA_DSP_PP_BAR			1
#define HDA_DSP_SPIB_BAR		2
#define HDA_DSP_BAR			4

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
    uint32_t intctl;
    uint32_t intsts;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t hipctdr;
    uint32_t hipctda;
    uint32_t hipctdd;
    uint32_t hipcidr;
    uint32_t hipcida;
    uint32_t hipcidd;
    uint32_t hipcctl;
    uint32_t adspcs;
    uint32_t adspic;
    uint32_t adspis;
    uint32_t adspic2;
    uint32_t adspis2;

    /* DMA Context */
    uint32_t sd_cbl;
    uint32_t sd_lvi;
    uint32_t sd_fifosize;
    uint32_t sd_format;
    uint32_t sd_bdlpl;
    uint32_t sd_bdlpu;
    uint32_t sd_sts;

    uint32_t ppctl;
    uint32_t ppsts;
    uint32_t d0i3c;
};

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case CNL_DSP_REG_HIPCTDR: val = s->hipctdr; break;
    case CNL_DSP_REG_HIPCTDA: val = s->hipctda; break;
    case CNL_DSP_REG_HIPCTDD: val = s->hipctdd; break;
    case CNL_DSP_REG_HIPCIDR: val = s->hipcidr; break;
    case CNL_DSP_REG_HIPCIDA: val = s->hipcida; break;
    case CNL_DSP_REG_HIPCIDD: val = s->hipcidd; break;
    case CNL_DSP_REG_HIPCCTL: val = s->hipcctl; break;
    case HDA_DSP_REG_ADSPCS: val = s->adspcs; break;
    case HDA_DSP_REG_ADSPIC: val = s->adspic; break;
    case HDA_DSP_REG_ADSPIS: val = s->adspis; break;
    case HDA_DSP_REG_ADSPIC2: val = s->adspic2; break;
    case HDA_DSP_REG_ADSPIS2: val = s->adspis2; break;
    case SOF_HDA_INTCTL: val = s->intctl; break;
    case SOF_HDA_INTSTS: val = s->intsts; break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad read at offset 0x%" HWADDR_PRIx "\n", __func__, addr);
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case CNL_DSP_REG_HIPCTDR: s->hipctdr = val; break;
    case CNL_DSP_REG_HIPCTDA: s->hipctda = val; break;
    case CNL_DSP_REG_HIPCTDD: s->hipctdd = val; break;
    case CNL_DSP_REG_HIPCIDR: s->hipcidr = val; break;
    case CNL_DSP_REG_HIPCIDA: s->hipcida = val; break;
    case CNL_DSP_REG_HIPCIDD: s->hipcidd = val; break;
    case CNL_DSP_REG_HIPCCTL: s->hipcctl = val; break;
    case HDA_DSP_REG_ADSPCS: s->adspcs = val; break;
    case HDA_DSP_REG_ADSPIC: s->adspic = val; break;
    case HDA_DSP_REG_ADSPIS: s->adspis = val; break;
    case HDA_DSP_REG_ADSPIC2: s->adspic2 = val; break;
    case HDA_DSP_REG_ADSPIS2: s->adspis2 = val; break;
    case SOF_HDA_INTCTL: s->intctl = val; break;
    case SOF_HDA_INTSTS: s->intsts = val; break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad write at offset 0x%" HWADDR_PRIx "\n", __func__, addr);
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
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

    s->hipctdr = 0;
    s->hipctda = 0;
    s->hipctdd = 0;
    s->hipcidr = 0;
    s->hipcida = 0;
    s->hipcidd = 0;
    s->hipcctl = 0;
    s->adspcs = 0;
    s->adspic = 0;
    s->adspis = 0;
    s->adspic2 = 0;
    s->adspis2 = 0;
    s->intctl = 0;
    s->intsts = 0;
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
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x9dc8 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0401 );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_VENDOR_ID, PCI_VENDOR_ID_INTEL);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_ID, 0x9dc8);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 4;
    s->bar_info[0] = (BARInfo){ .index = HDA_DSP_HDA_BAR, .type = BAR_TYPE_MMIO, .size = 0x1000, .name = "hda_bar" };
    s->bar_info[1] = (BARInfo){ .index = HDA_DSP_PP_BAR, .type = BAR_TYPE_MMIO, .size = 0x1000, .name = "pp_bar" };
    s->bar_info[2] = (BARInfo){ .index = HDA_DSP_SPIB_BAR, .type = BAR_TYPE_MMIO, .size = 0x1000, .name = "spib_bar" };
    s->bar_info[3] = (BARInfo){ .index = HDA_DSP_BAR, .type = BAR_TYPE_MMIO, .size = 0x1000, .name = "dsp_bar" };  
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    if (msi_init(pdev, 0, 1, true, false, errp)) {
        return;
    }
    s->has_msi = true;  
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

}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "sof_audio_pci_intel_cnl_pci",
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
