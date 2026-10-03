/*
 * Virtual Device Model for Intel Meteor Lake (MTL) Audio DSP
 * Based on Linux driver sound/soc/sof/intel/pci-mtl.c
 * This device provides BAR memory regions to allow the driver to probe successfully.
 * Behavior is minimal; full emulation requires additional driver source.
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

#define TYPE_PCIBASE_DEVICE "sof_audio_pci_intel_mtl_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x8086
#define DEVICE_ID 0x7e28
#define CLASS_ID  0x000a

/* Register offsets and bit definitions from driver (not all used in the model) */
#define CNL_SSP_BASE_OFFSET         0x10000
#define MTL_HDA_VS_D0I3C            0x1D4A
#define MTL_DSP_REG_HFFLGPXQWY      0x163200
#define MTL_DSP_REG_HFIPCXIDR       0x73210
#define MTL_DSP_REG_HFIPCXIDA       0x73214
#define MTL_DSP_REG_HFIPCXIDA_DONE  BIT(31)
#define MTL_DSP_REG_HFIPCXIDR_BUSY  BIT(31)
#define MTL_DSP_REG_HFIPCXCTL       0x73228
#define MTL_DSP_REG_HFIPCXCTL_BUSY  BIT(0)
#define MTL_DSP_REG_HFIPCXCTL_DONE  BIT(1)
#define MTL_DSP_REG_HFIPCXTDR       0x73200
#define MTL_DSP_REG_HFIPCXTDR_BUSY  BIT(31)
#define MTL_DSP_REG_HFIPCXTDR_MSG_MASK GENMASK(30, 0)
#define MTL_DSP_REG_HFIPCXTDDY      0x73300
#define MTL_DSP_REG_HFIPCXIDDY      0x73380
#define MTL_DSP_REG_HFIPCXTDA       0x73204
#define MTL_DSP_REG_HFIPCXTDA_BUSY  BIT(31)
#define MTL_DSP_IRQSTS              0x20
#define MTL_DSP_IRQSTS_IPC          BIT(0)
#define MTL_DSP_IRQSTS_SDW          BIT(6)
#define MTL_HFINTIPPTR              0x1108
#define MTL_HFINTIPPTR_PTR_MASK     GENMASK(20, 0)
#define MTL_HFPWRCTL                0x1D18
#define MTL_HFPWRCTL_WPDSPHPXPG     BIT(0)
#define MTL_HFPWRCTL_WPIOXPG(x)     BIT((x) + 8)
#define MTL_HFPWRSTS                0x1D1C
#define MTL_HFPWRSTS_DSPHPXPGS_MASK BIT(0)
#define PTL_HFPWRCTL2               0x1D20
#define PTL_HFPWRSTS2               0x1D24
#define MTL_HFDSSCS                 0x1000
#define MTL_HFDSSCS_SPA_MASK        BIT(16)
#define MTL_HFDSSCS_CPA_MASK        BIT(24)
#define MTL_DSP2CXCTL_PRIMARY_CORE           0x178D04
#define MTL_DSP2CXCTL_PRIMARY_CORE_SPA_MASK  BIT(0)
#define MTL_DSP2CXCTL_PRIMARY_CORE_CPA_MASK  BIT(8)
#define MTL_DSP2CXCTL_PRIMARY_CORE_OSEL      GENMASK(25, 24)
#define MTL_DSP2CXCTL_PRIMARY_CORE_OSEL_SHIFT 24
#define MTL_IRQ_INTEN_L_HOST_IPC_MASK    BIT(0)
#define MTL_IRQ_INTEN_L_SOUNDWIRE_MASK   BIT(6)
#define MTL_SRAM_WINDOW_OFFSET(x)       (0x180000 + 0x8000 * (x))
#define MTL_DSP_ROM_STS                 MTL_SRAM_WINDOW_OFFSET(0)
#define MTL_DSP_ROM_ERROR               (MTL_SRAM_WINDOW_OFFSET(0) + 0x4)
#define MTL_DSP_MBOX_UPLINK_OFFSET      (MTL_SRAM_WINDOW_OFFSET(0) + 0x1000)
#define HDA_DSP_HDA_BAR                 0
#define HDA_DSP_PP_BAR                  1
#define HDA_DSP_SPIB_BAR                2
#define HDA_DSP_BAR                     4
#define SOF_HDA_ADSP_LOADER_BASE        0x80
#define SOF_HDA_ADSP_SD_ENTRY_SIZE      0x20
#define SOF_HDA_ADSP_REG_SD_FIFOSIZE    0x10
#define SOF_HDA_ADSP_REG_SD_STS         0x03
#define SOF_HDA_ADSP_REG_SD_BDLPL       0x18
#define SOF_HDA_ADSP_REG_SD_BDLPU       0x1C
#define SOF_HDA_ADSP_REG_SD_LVI         0x0C
#define SOF_HDA_ADSP_REG_SD_CBL         0x08
#define SOF_HDA_ADSP_REG_SD_FORMAT      0x12
#define SOF_HDA_ADSP_REG_CL_SPBFIFO_SPBFCCTL 0x4
#define SOF_HDA_ADSP_DPLBASE            0x70
#define SOF_HDA_ADSP_DPUBASE            0x74
#define SOF_HDA_REG_PP_PPCTL            0x04
#define HDA_DSP_SPIB_DISABLE            0
#define HDA_DSP_SPIB_ENABLE             1
#define SOF_HDA_VS_D0I3C_I3             BIT(2)
#define SOF_HDA_VS_D0I3C_CIP            BIT(0)
#define HDA_VS_INTEL_LTRP               0x1048
#define HDA_VS_INTEL_LTRP_GB_MASK       0x3F
#define SOF_HDA_CL_SD_CTL_STREAM_TAG_SHIFT   20
#define SOF_HDA_CL_SD_CTL_STREAM_TAG_MASK    GENMASK(SOF_HDA_CL_SD_CTL_STREAM_TAG_SHIFT + 3, SOF_HDA_CL_SD_CTL_STREAM_TAG_SHIFT)
#define SOF_HDA_CL_DMA_SD_INT_MASK  (SOF_HDA_CL_DMA_SD_INT_DESC_ERR | SOF_HDA_CL_DMA_SD_INT_FIFO_ERR | SOF_HDA_CL_DMA_SD_INT_COMPLETE)
#define SOF_HDA_CL_DMA_SD_INT_DESC_ERR    0x10
#define SOF_HDA_CL_DMA_SD_INT_FIFO_ERR    0x08
#define SOF_HDA_CL_DMA_SD_INT_COMPLETE    0x04
#define SOF_HDA_SD_CTL_DMA_START          0x02
#define HDA_CL_STREAM_FORMAT              0x40

/* Helper macros */
#define BIT(nr)                     (1UL << (nr))
#define GENMASK(msb, lsb)           (((1UL << ((msb) - (lsb) + 1)) - 1) << (lsb))

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
    uint32_t intr_status;
    uint32_t intr_mask;

    /* No additional fields required for minimal RAM-backed model */
};

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* Initialize BAR sizes and types based on typical SOF platform requirements.
     * These are inferred from register offset ranges defined in the driver.
     */
    s->num_bars = 4;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_RAM, .size = 0x4000, .name = "HDA" };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_RAM, .size = 0x1000, .name = "PP" };
    s->bar_info[2] = (BARInfo){ .index = 2, .type = BAR_TYPE_RAM, .size = 0x1000, .name = "SPIB" };
    s->bar_info[4] = (BARInfo){ .index = 4, .type = BAR_TYPE_RAM, .size = 0x200000, .name = "DSP" };

    /* Mark unused BARs */
    for (int i = 0; i < 6; i++) {
        if (i == 0 || i == 1 || i == 2 || i == 4) {
            continue;
        }
        s->bar_info[i].type = BAR_TYPE_NONE;
    }

    for (int i = 0; i < 6; i++) {
        if (s->bar_info[i].type != BAR_TYPE_NONE) {
            pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
        }
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    /* No dynamic cleanup needed for RAM regions */
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    /* No special reset logic required; RAM regions are cleared by QEMU if necessary. */
}

static const VMStateDescription vmstate_pcibase = {
    .name = "sof_audio_pci_intel_mtl_pci",
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
