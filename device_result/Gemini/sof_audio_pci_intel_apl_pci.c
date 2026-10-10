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

#define TYPE_PCIBASE_DEVICE "sof_audio_pci_intel_apl_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define HDA_DSP_IPC_BASE		0x40
#define HDA_DSP_REG_HIPCT		(HDA_DSP_IPC_BASE + 0x00)
#define HDA_DSP_REG_HIPCTE		(HDA_DSP_IPC_BASE + 0x04)
#define HDA_DSP_REG_HIPCI		(HDA_DSP_IPC_BASE + 0x08)
#define HDA_DSP_REG_HIPCIE		(HDA_DSP_IPC_BASE + 0x0C)
#define HDA_DSP_REG_HIPCCTL		(HDA_DSP_IPC_BASE + 0x10)

#define HDA_DSP_GEN_BASE		0x0
#define HDA_DSP_REG_ADSPCS		(HDA_DSP_GEN_BASE + 0x04)
#define HDA_DSP_REG_ADSPIC		(HDA_DSP_GEN_BASE + 0x08)
#define HDA_DSP_REG_ADSPIS		(HDA_DSP_GEN_BASE + 0x0C)

#define SOF_HDA_INTCTL			0x20
#define SOF_HDA_INTSTS			0x24
#define SOF_HDA_REG_PP_PPCTL		0x04
#define SOF_HDA_REG_PP_PPSTS		0x08

#define HDA_DSP_HDA_BAR			0
#define HDA_DSP_PP_BAR			1
#define HDA_DSP_SPIB_BAR		2
#define HDA_DSP_BAR			4
#define SND_SOF_BARS			8

/* Newly discovered macros */
#define HDA_DSP_REG_HIPCI_BUSY		BIT(31)
#define HDA_DSP_REG_HIPCIE_DONE		BIT(30)
#define APL_SSP_COUNT			6
#define APL_SSP_BASE_OFFSET		0x2000
#define SOF_HDA_VS_D0I3C		0x104A
#define SOF_INTEL_PROCEN_FMT_QUIRK	BIT(0)

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
    uint32_t hipct;
    uint32_t hipcte;
    uint32_t hipci;
    uint32_t hipcie;
    uint32_t hipcctl;
    uint32_t adspcs;
    uint32_t adspic;
    uint32_t adspis;

    /* DMA Context */
    uint32_t dplbase;
    uint32_t dpubbase;
    uint32_t sd_bdlpl;
    uint32_t sd_bdlpu;
    uint32_t sd_cbl;
    uint32_t sd_lvi;

    uint32_t rom_status;
    uint32_t fw_state;
    uint32_t pm_state;
    uint32_t ppctl;
    uint32_t ppsts;
};

struct sof_intel_dsp_bdl {
    uint32_t addr_l;
    uint32_t addr_h;
    uint32_t size;
    uint32_t ioc;
};

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = PCIBASE_DEVICE(opaque);
    uint64_t val = 0;

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
    case HDA_DSP_REG_ADSPCS:
        val = s->adspcs;
        break;
    case HDA_DSP_REG_ADSPIC:
        val = s->adspic;
        break;
    case HDA_DSP_REG_ADSPIS:
        val = s->adspis;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented read from offset 0x%" HWADDR_PRIx "\n", __func__, addr);
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = PCIBASE_DEVICE(opaque);

    switch (addr) {
    case HDA_DSP_REG_HIPCT:
        s->hipct = val;
        break;
    case HDA_DSP_REG_HIPCTE:
        s->hipcte = val;
        break;
    case HDA_DSP_REG_HIPCI:
        s->hipci = val;
        break;
    case HDA_DSP_REG_HIPCIE:
        s->hipcie = val;
        break;
    case HDA_DSP_REG_HIPCCTL:
        s->hipcctl = val;
        break;
    case HDA_DSP_REG_ADSPCS:
        s->adspcs = val;
        break;
    case HDA_DSP_REG_ADSPIC:
        s->adspic = val;
        break;
    case HDA_DSP_REG_ADSPIS:
        s->adspis = val;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented write to offset 0x%" HWADDR_PRIx "\n", __func__, addr);
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
    pci_device_reset(PCI_DEVICE(dev));
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
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x5a98 );
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
    s->num_bars = 0; /* Sizes unknown, requested in needed_sources */  
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    msi_init(pdev, 0, 1, true, false, errp);  /* msi_init or msix_init calls */
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
    .name = "sof_audio_pci_intel_apl_pci",
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
