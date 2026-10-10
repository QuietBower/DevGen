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

#define TYPE_PCIBASE_DEVICE "sof_audio_pci_intel_skl_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define HDA_DSP_GEN_BASE		0x0
#define HDA_DSP_REG_ADSPCS		(HDA_DSP_GEN_BASE + 0x04)
#define HDA_DSP_REG_ADSPIC		(HDA_DSP_GEN_BASE + 0x08)
#define HDA_DSP_REG_ADSPIS		(HDA_DSP_GEN_BASE + 0x0C)

#define HDA_DSP_IPC_BASE		0x40
#define HDA_DSP_REG_HIPCT		(HDA_DSP_IPC_BASE + 0x00)
#define HDA_DSP_REG_HIPCTE		(HDA_DSP_IPC_BASE + 0x04)
#define HDA_DSP_REG_HIPCI		(HDA_DSP_IPC_BASE + 0x08)
#define HDA_DSP_REG_HIPCIE		(HDA_DSP_IPC_BASE + 0x0C)
#define HDA_DSP_REG_HIPCCTL		(HDA_DSP_IPC_BASE + 0x10)

#define SOF_HDA_INTCTL			0x20
#define SOF_HDA_INTSTS			0x24
#define SOF_HDA_REG_PP_PPSTS		0x08

#define SOF_HDA_ADSP_LOADER_BASE	0x80
#define SOF_HDA_ADSP_REG_SD_CTL		0x00
#define SOF_HDA_ADSP_REG_SD_STS		0x03
#define SOF_HDA_ADSP_REG_SD_CBL		0x08
#define SOF_HDA_ADSP_REG_SD_LVI		0x0C
#define SOF_HDA_ADSP_REG_SD_BDLPL	0x18
#define SOF_HDA_ADSP_REG_SD_BDLPU	0x1C
#define SOF_DSP_REG_CL_SPBFIFO		(SOF_HDA_ADSP_LOADER_BASE + 0x20)

#define PCI_TCSEL			0x44
#define PCI_CGCTL			0x48
#define PCI_PGCTL			PCI_TCSEL
#define HDA_VS_INTEL_EM2		0x1030

#define HDA_DSP_HDA_BAR			0
#define HDA_DSP_PP_BAR			1
#define HDA_DSP_BAR			4

/* Fallback sizes if not explicitly defined */
#define HDA_DSP_HDA_BAR_SIZE 0x4000
#define HDA_DSP_PP_BAR_SIZE 0x1000
#define HDA_DSP_BAR_SIZE 0x100000

/* Fallback Device ID if not explicitly defined */
#ifndef HDA_SKL_LP
#define HDA_SKL_LP 0x9d70
#endif

#define HDA_DSP_REG_HIPCI_BUSY		(1U << 31)
#define HDA_DSP_REG_HIPCIE_DONE		(1U << 30)
#define HDA_DSP_SRAM_REG_ROM_STATUS_SKL	0x8000

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
    uint32_t adspcs;
    uint32_t adspic;
    uint32_t adspis;
    uint32_t hipct;
    uint32_t hipcte;
    uint32_t hipci;
    uint32_t hipcie;
    uint32_t hipcctl;
    uint32_t pp_ppsts;

    /* DMA Context */
    uint32_t sd_ctl;
    uint32_t sd_sts;
    uint32_t sd_cbl;
    uint32_t sd_lvi;
    uint32_t sd_bdlpl;
    uint32_t sd_bdlpu;
    uint32_t cl_spbfifo;

    uint32_t status;
    uint32_t reset_state;
    uint32_t tcsel;
    uint32_t cgctl;
    uint32_t vs_intel_em2;
    
    uint32_t rom_status;
    uint32_t rom_error;
};

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case HDA_DSP_REG_ADSPIS:
        val = s->adspis;
        break;
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
        val = s->rom_status;
        break;
    /* HDA_DSP_SRAM_REG_ROM_ERROR will be added when macro is resolved */
    default:
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case HDA_DSP_REG_HIPCIE:
        s->hipcie = val;
        break;
    case HDA_DSP_REG_HIPCI:
        s->hipci = val;
        if (val & HDA_DSP_REG_HIPCI_BUSY) {
            /* DSP processing message... */
        }
        break;
    case HDA_DSP_REG_HIPCCTL:
        s->hipcctl = val;
        break;
    default:
        break;
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    s->hipci = 0;
    s->hipcie = 0;
    s->hipct = 0;
    s->hipcte = 0;
    s->hipcctl = 0;
    s->adspis = 0;
    s->rom_status = 0;
    s->rom_error = 0;
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
    pci_set_word(pci_conf + PCI_DEVICE_ID,  HDA_SKL_LP );
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
    s->num_bars = 5;
    s->bar_info[HDA_DSP_HDA_BAR] = (BARInfo){HDA_DSP_HDA_BAR, BAR_TYPE_MMIO, HDA_DSP_HDA_BAR_SIZE, "hda_bar"};
    s->bar_info[HDA_DSP_PP_BAR] = (BARInfo){HDA_DSP_PP_BAR, BAR_TYPE_MMIO, HDA_DSP_PP_BAR_SIZE, "pp_bar"};
    s->bar_info[HDA_DSP_BAR] = (BARInfo){HDA_DSP_BAR, BAR_TYPE_MMIO, HDA_DSP_BAR_SIZE, "dsp_bar"};
  
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    msi_init(pdev, 0, 1, true, false, errp);
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
    .name = "sof_audio_pci_intel_skl_pci",
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
