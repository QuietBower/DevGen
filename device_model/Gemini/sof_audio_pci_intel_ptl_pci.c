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
#include "hw/pci/pci_device.h"

#define TYPE_PCIBASE_DEVICE "sof_audio_pci_intel_ptl_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_INTEL 0x8086
#define PCI_DEVICE_ID_INTEL_HDA_PTL 0xe428

#define SND_SOF_BARS 8
#define HDA_DSP_HDA_BAR 0
#define HDA_DSP_PP_BAR 1
#define HDA_DSP_SPIB_BAR 2
#define HDA_DSP_BAR 4

#define SOF_HDA_INTCTL 0x20
#define SOF_HDA_INTSTS 0x24
#define SOF_HDA_WAKESTS 0x0E
#define SOF_HDA_GCTL 0x8
#define SOF_HDA_ADSP_DPLBASE 0x70
#define SOF_HDA_ADSP_DPUBASE 0x74
#define SOF_HDA_REG_PP_PPCTL 0x04
#define SOF_HDA_SPIB_SPIB 0x00
#define SOF_HDA_SPIB_MAXFIFO 0x04
#define PCI_TCSEL 0x44
#define PCI_CGCTL 0x48

#define SOF_HDA_ADSP_REG_SD_FIFOSIZE 0x10
#define SOF_HDA_ADSP_REG_SD_STS 0x03
#define SOF_HDA_ADSP_REG_SD_BDLPL 0x18
#define SOF_HDA_ADSP_REG_SD_BDLPU 0x1C
#define SOF_HDA_ADSP_REG_SD_LVI 0x0C
#define SOF_HDA_ADSP_REG_SD_CBL 0x08
#define SOF_HDA_ADSP_REG_SD_FORMAT 0x12

#define MTL_DSP_REG_HFIPCXIDR       0x73210
#define MTL_DSP_REG_HFIPCXIDR_BUSY  (1U << 31)
#define MTL_DSP_REG_HFIPCXIDA       0x73214
#define MTL_DSP_REG_HFIPCXIDA_DONE  (1U << 31)
#define MTL_DSP_REG_HFIPCXCTL       0x73228
#define LNL_DSP_REG_HFDSC           0x160200
#define MTL_HDA_VS_D0I3C            0x1D4A

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
    uint32_t wakests;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t gctl;
    uint32_t ppctl;
    uint32_t tcsel;
    uint32_t cgctl;
    uint32_t adsp_dplbase;
    uint32_t adsp_dpubase;

    /* DMA Context */
    uint32_t sd_bdlpl;
    uint32_t sd_bdlpu;
    uint32_t sd_cbl;
    uint32_t sd_lvi;
    uint32_t sd_fifosize;
    uint32_t sd_format;
    uint32_t sd_sts;

    /* IPC / DSP Context */
    uint32_t hfipcxidr;
    uint32_t hfipcxida;
    uint32_t hfipcxctl;
    uint32_t hfdsc;
    uint32_t vs_d0i3c;

    uint32_t fw_state;
    uint32_t dsp_power_state;
};

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
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
    case SOF_HDA_WAKESTS:
        val = s->wakests;
        break;
    case SOF_HDA_GCTL: /* 0x08, also SOF_HDA_ADSP_REG_SD_CBL */
        val = s->gctl | s->sd_cbl;
        break;
    case SOF_HDA_ADSP_DPLBASE:
        val = s->adsp_dplbase;
        break;
    case SOF_HDA_ADSP_DPUBASE:
        val = s->adsp_dpubase;
        break;
    case SOF_HDA_REG_PP_PPCTL:
        val = s->ppctl;
        break;
    case PCI_TCSEL:
        val = s->tcsel;
        break;
    case PCI_CGCTL:
        val = s->cgctl;
        break;
    case SOF_HDA_ADSP_REG_SD_FIFOSIZE:
        val = s->sd_fifosize;
        break;
    case SOF_HDA_ADSP_REG_SD_STS:
        val = s->sd_sts;
        break;
    case SOF_HDA_ADSP_REG_SD_BDLPL:
        val = s->sd_bdlpl;
        break;
    case SOF_HDA_ADSP_REG_SD_BDLPU:
        val = s->sd_bdlpu;
        break;
    case SOF_HDA_ADSP_REG_SD_LVI:
        val = s->sd_lvi;
        break;
    case SOF_HDA_ADSP_REG_SD_FORMAT:
        val = s->sd_format;
        break;
    case MTL_DSP_REG_HFIPCXIDR:
        val = s->hfipcxidr;
        break;
    case MTL_DSP_REG_HFIPCXIDA:
        val = s->hfipcxida;
        break;
    case MTL_DSP_REG_HFIPCXCTL:
        val = s->hfipcxctl;
        break;
    case LNL_DSP_REG_HFDSC:
        val = s->hfdsc;
        break;
    case MTL_HDA_VS_D0I3C:
        val = s->vs_d0i3c;
        break;
    default:
        val = 0;
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case SOF_HDA_INTCTL:
        s->intctl = val;
        break;
    case SOF_HDA_INTSTS:
        s->intsts = val;
        break;
    case SOF_HDA_WAKESTS:
        s->wakests = val;
        break;
    case SOF_HDA_GCTL: /* 0x08, also SOF_HDA_ADSP_REG_SD_CBL */
        s->gctl = val;
        s->sd_cbl = val;
        break;
    case SOF_HDA_ADSP_DPLBASE:
        s->adsp_dplbase = val;
        break;
    case SOF_HDA_ADSP_DPUBASE:
        s->adsp_dpubase = val;
        break;
    case SOF_HDA_REG_PP_PPCTL:
        s->ppctl = val;
        break;
    case PCI_TCSEL:
        s->tcsel = val;
        break;
    case PCI_CGCTL:
        s->cgctl = val;
        break;
    case SOF_HDA_ADSP_REG_SD_FIFOSIZE:
        s->sd_fifosize = val;
        break;
    case SOF_HDA_ADSP_REG_SD_STS:
        s->sd_sts = val;
        break;
    case SOF_HDA_ADSP_REG_SD_BDLPL:
        s->sd_bdlpl = val;
        break;
    case SOF_HDA_ADSP_REG_SD_BDLPU:
        s->sd_bdlpu = val;
        break;
    case SOF_HDA_ADSP_REG_SD_LVI:
        s->sd_lvi = val;
        break;
    case SOF_HDA_ADSP_REG_SD_FORMAT:
        s->sd_format = val;
        break;
    case MTL_DSP_REG_HFIPCXIDR:
        s->hfipcxidr = val;
        break;
    case MTL_DSP_REG_HFIPCXIDA:
        s->hfipcxida = val;
        break;
    case MTL_DSP_REG_HFIPCXCTL:
        s->hfipcxctl = val;
        break;
    case LNL_DSP_REG_HFDSC:
        s->hfdsc = val;
        break;
    case MTL_HDA_VS_D0I3C:
        s->vs_d0i3c = val;
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

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    s->intctl = 0;
    s->intsts = 0;
    s->wakests = 0;
    s->gctl = 0;
    s->ppctl = 0;
    s->tcsel = 0;
    s->cgctl = 0;
    s->adsp_dplbase = 0;
    s->adsp_dpubase = 0;
    s->sd_bdlpl = 0;
    s->sd_bdlpu = 0;
    s->sd_cbl = 0;
    s->sd_lvi = 0;
    s->sd_fifosize = 0;
    s->sd_format = 0;
    s->sd_sts = 0;
    s->hfipcxidr = 0;
    s->hfipcxida = 0;
    s->hfipcxctl = 0;
    s->hfdsc = 0;
    s->vs_d0i3c = 0;
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
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_INTEL_HDA_PTL );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0403 );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 0; /* Sizes missing, requested in needed_sources */  
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    if (msi_init(pdev, 0, 1, true, false, errp) == 0) {
        s->has_msi = true;
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
    .name = "sof_audio_pci_intel_ptl_pci",
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
