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

#define TYPE_PCIBASE_DEVICE "sof_audio_pci_intel_skl_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define BIT(x) (1UL << (x))

/* PCI IDs from supplementary driver source */
#define PCI_VENDOR_ID_INTEL               0x8086
#define PCI_DEVICE_ID_INTEL_HDA_SKL_LP    0x9d70
#define PCI_CLASS_MULTIMEDIA_HD_AUDIO     0x0403

/* BAR sizes - exact values needed from driver definitions */
#define BAR0_SIZE 0x1000   /* define:BAR0_SIZE */
#define BAR1_SIZE 0x1000   /* define:BAR1_SIZE */
#define BAR4_SIZE 0x100000 /* define:BAR4_SIZE */

/* Hardware register offsets from driver source */
#define HDA_DSP_IPC_BASE                0x40
#define HDA_DSP_GEN_BASE                0x0
#define SRAM_WINDOW_OFFSET(x)           (0x80000 + (x) * 0x20000)
#define HDA_DSP_REG_HIPCI_BUSY          BIT(31)
#define HDA_DSP_REG_HIPCI               (HDA_DSP_IPC_BASE + 0x08)
#define HDA_DSP_REG_HIPCCTL             (HDA_DSP_IPC_BASE + 0x10)
#define HDA_DSP_SRAM_REG_ROM_STATUS_SKL 0x8000
#define HDA_DSP_REG_HIPCIE_DONE         BIT(30)
#define HDA_DSP_REG_HIPCIE              (HDA_DSP_IPC_BASE + 0x0C)
#define HDA_DSP_REG_ADSPIS              (HDA_DSP_GEN_BASE + 0x0C)
#define HDA_DSP_ADSPIS_IPC              BIT(0)
#define HDA_DSP_ADSPIS_CL_DMA           BIT(1)
#define HDA_DSP_REG_HIPCT_BUSY          BIT(31)
#define HDA_DSP_REG_HIPCTE_MSG_MASK     0x3FFFFFFF
#define HDA_DSP_REG_HIPCT               (HDA_DSP_IPC_BASE + 0x00)
#define HDA_DSP_REG_HIPCTE              (HDA_DSP_IPC_BASE + 0x04)
#define HDA_DSP_REG_HIPCT_MSG_MASK      0x7FFFFFFF
#define HDA_DSP_REG_HIPCCTL_BUSY        BIT(0)
#define HDA_DSP_REG_HIPCCTL_DONE        BIT(1)
#define HDA_DSP_REG_POLL_INTERVAL_US    500
#define HDA_DSP_SRAM_REG_ROM_ERROR      (HDA_DSP_MBOX_OFFSET + 0x4)
#define HDA_DSP_BASEFW_TIMEOUT_US       3000000
#define HDA_DSP_ADSPIC_IPC              BIT(0)
#define HDA_DSP_REG_ADSPIC              (HDA_DSP_GEN_BASE + 0x08)
#define HDA_DSP_MBOX_OFFSET             SRAM_WINDOW_OFFSET(0)
#define SOF_HDA_INTCTL                  0x20
#define SOF_HDA_INTSTS                  0x24
#define SOF_HDA_REG_PP_PPSTS            0x08
#define HDA_DSP_ADSPCS_CPA_SHIFT        24
#define HDA_DSP_ADSPCS_SPA_SHIFT        16
#define HDA_DSP_ADSPCS_CSTALL_SHIFT     8
#define HDA_DSP_ADSPCS_CRST_SHIFT       0
#define HDA_DSP_ADSPCS_CPA_MASK(cm)     ((cm) << HDA_DSP_ADSPCS_CPA_SHIFT)
#define HDA_DSP_ADSPCS_SPA_MASK(cm)     ((cm) << HDA_DSP_ADSPCS_SPA_SHIFT)
#define HDA_DSP_ADSPCS_CSTALL_MASK(cm)  ((cm) << HDA_DSP_ADSPCS_CSTALL_SHIFT)
#define HDA_DSP_ADSPCS_CRST_MASK(cm)    ((cm) << HDA_DSP_ADSPCS_CRST_SHIFT)
#define HDA_DSP_REG_ADSPCS              (HDA_DSP_GEN_BASE + 0x04)
#define HDA_DSP_ADSPIC_CL_DMA           BIT(1)
#define SOF_DSP_REG_CL_SPBFIFO          (SOF_HDA_ADSP_LOADER_BASE + 0x20)
#define SOF_HDA_ADSP_LOADER_BASE        0x80
#define SOF_HDA_ADSP_REG_CL_SPBFIFO_SPIB        0x8
#define SOF_HDA_ADSP_REG_CL_SPBFIFO_SPBFCCTL    0x4
#define SOF_HDA_ADSP_REG_SD_LVI                 0x0C
#define SOF_HDA_ADSP_REG_SD_BDLPU               0x1C
#define SOF_HDA_ADSP_REG_SD_CBL                 0x08
#define SOF_HDA_ADSP_REG_SD_BDLPL               0x18
#define PCI_TCSEL                              0x44
#define HDA_DSP_RESET_TIMEOUT_US        50000
#define SOF_HDA_ADSP_REG_SD_STS                 0x03
#define SOF_HDA_ADSP_REG_SD_CTL                 0x00

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
};

/* MMIO/PIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* Not used: all BARs are RAM-based. Return 0. */
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Not used: all BARs are RAM-based. */
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* Not used. */
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Not used. */
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_INTEL);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_INTEL_HDA_SKL_LP);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_MULTIMEDIA_HD_AUDIO);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 3;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_RAM, .size = BAR0_SIZE, .name = "bar0" };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_RAM, .size = BAR1_SIZE, .name = "bar1" };
    s->bar_info[2] = (BARInfo){ .index = 4, .type = BAR_TYPE_RAM, .size = BAR4_SIZE, .name = "bar4" };

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    msi_init(pdev, 0, 1, true, false, errp);
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
