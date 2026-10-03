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


#define TYPE_PCIBASE_DEVICE "sof_audio_pci_intel_ptl_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_INTEL 0x8086
#define PCI_DEVICE_ID_INTEL_HDA_PTL 0xe428
#define PCI_CLASS_MULTIMEDIA_AUDIO 0x040100

/* BAR Indices */
#define HDA_DSP_HDA_BAR  0
#define HDA_DSP_PP_BAR   1
#define HDA_DSP_SPIB_BAR 2
#define HDA_DSP_BAR      4

/* Basic bit manipulation macros */
#define BIT(n) (1UL << (n))
#define GENMASK(h, l) (((1UL << ((h) - (l) + 1)) - 1) << (l))

/* HDA / DSP register offsets and fields extracted from driver source */
#define SOF_HDA_GCAP			0x0
#define SOF_HDA_GCTL			0x8
#define SOF_HDA_WAKESTS			0x0E
#define SOF_HDA_INTCTL			0x20
#define SOF_HDA_INTSTS			0x24
#define SOF_HDA_INT_ALL_STREAM		0xff
#define SOF_HDA_GCTL_RESET		BIT(0)
#define SOF_HDA_INT_CTRL_EN		BIT(30)
#define SOF_HDA_INT_GLOBAL_EN		BIT(31)

#define SOF_HDA_PPCTL_PIE		BIT(31)
#define SOF_HDA_PPCTL_GPROCEN		BIT(30)
#define SOF_HDA_REG_PP_PPCTL		0x04

#define SOF_HDA_ADSP_REG_SD_STS		0x03
#define SOF_HDA_ADSP_REG_SD_FIFOSIZE	0x10
#define SOF_HDA_ADSP_REG_SD_LVI		0x0C
#define SOF_HDA_ADSP_REG_SD_CBL		0x08
#define SOF_HDA_ADSP_REG_SD_BDLPL	0x18
#define SOF_HDA_ADSP_REG_SD_BDLPU	0x1C
#define SOF_HDA_ADSP_REG_SD_FORMAT	0x12
#define SOF_HDA_ADSP_DPLBASE		0x70
#define SOF_HDA_ADSP_DPUBASE		0x74
#define SOF_HDA_ADSP_LOADER_BASE	0x80
#define SOF_HDA_ADSP_SD_ENTRY_SIZE	0x20
#define SOF_HDA_ADSP_REG_CL_SPBFIFO_SPBFCCTL 0x4

#define SOF_HDA_CL_STREAM_FORMAT		0x40
#define SOF_HDA_SD_FIFOSIZE_FIFOS_MASK		GENMASK(15, 0)
#define SOF_HDA_CL_DMA_SD_INT_DESC_ERR		0x10
#define SOF_HDA_CL_DMA_SD_INT_FIFO_ERR		0x08
#define SOF_HDA_CL_DMA_SD_INT_COMPLETE		0x04
#define SOF_HDA_CL_DMA_SD_INT_MASK \
	(SOF_HDA_CL_DMA_SD_INT_DESC_ERR | SOF_HDA_CL_DMA_SD_INT_FIFO_ERR | SOF_HDA_CL_DMA_SD_INT_COMPLETE)
#define SOF_HDA_CL_SD_CTL_STREAM_TAG_SHIFT	20
#define SOF_HDA_CL_SD_CTL_STREAM_TAG_MASK \
	GENMASK(SOF_HDA_CL_SD_CTL_STREAM_TAG_SHIFT + 3, SOF_HDA_CL_SD_CTL_STREAM_TAG_SHIFT)
#define SOF_HDA_SD_CTL_DMA_START		0x02
#define SOF_HDA_STREAM_SD_OFFSET_CRST		0x1

#define HDA_DSP_MBOX_UPLINK_OFFSET		0x81000
#define HDA_DSP_ROM_IPC_CONTROL			0x01000000
#define HDA_DSP_ROM_IPC_PURGE_FW		0x00004000

#define FSR_STATE_INIT_DONE			0x1
#define FSR_STATE_FW_ENTERED			0x5
#define FSR_HALTED				BIT(31)
#define FSR_STATE_MASK				GENMASK(23, 0)
#define FSR_WAIT_STATE_MASK			GENMASK(27, 24)
#define FSR_MODULE_MASK				GENMASK(30, 28)

#define SOF_HDA_VS_INTEL_EM2			0x1030
#define SOF_HDA_VS_INTEL_EM2_L1SEN		BIT(13)
#define HDA_VS_INTEL_LTRP			0x1048
#define HDA_VS_INTEL_LTRP_GB_MASK		0x3F
#define SOF_HDA_VS_D0I3C_I3			BIT(2)
#define SOF_HDA_VS_D0I3C_CIP			BIT(0)

#define PCI_TCSEL				0x44
#define PCI_CGCTL				0x48
#define PCI_CGCTL_MISCBDCGE_MASK		BIT(6)

#define SOF_HDA_DPIB_ENTRY_SIZE			0x8
#define SOF_HDA_SPIB_BASE			0x08
#define SOF_HDA_SPIB_SPIB			0x00
#define SOF_HDA_SPIB_MAXFIFO			0x04
#define SOF_HDA_SPIB_INTERVAL			0x08
#define SOF_HDA_PPHC_BASE			0x10
#define SOF_HDA_PPHC_INTERVAL			0x10
#define SOF_HDA_PPLC_BASE			0x10
#define SOF_HDA_PPLC_MULTI			0x10
#define SOF_HDA_PPLC_INTERVAL			0x10

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
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t reg_version; /* placeholder for version register */

    /* DMA Context */
    uint64_t dma_base;
    uint64_t dma_count;
    uint32_t dma_cmd;

    uint32_t status;
    bool in_reset;
    uint8_t power_state;

    /* Per-BAR data buffers */
    uint8_t *bar0_data;
    uint8_t *bar1_data;
    uint8_t *bar2_data;
    uint8_t *bar4_data;
};

/* ========================================================================== */
/* Per-BAR MMIO read/write handlers */
/* ========================================================================== */

static uint64_t pcibase_bar0_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= 0x2000) {
        return 0;
    }

    switch (addr) {
    case SOF_HDA_GCAP:
        val = ldl_le_p(s->bar0_data + addr);
        break;
    case SOF_HDA_GCTL:
        val = ldl_le_p(s->bar0_data + addr);
        break;
    case SOF_HDA_WAKESTS:
        val = lduw_le_p(s->bar0_data + addr);
        break;
    case SOF_HDA_INTCTL:
        val = ldl_le_p(s->bar0_data + addr);
        break;
    case SOF_HDA_INTSTS:
        val = ldl_le_p(s->bar0_data + addr);
        break;
    case SOF_HDA_VS_INTEL_EM2:
        val = ldl_le_p(s->bar0_data + addr);
        break;
    case HDA_VS_INTEL_LTRP:
        val = ldl_le_p(s->bar0_data + addr);
        break;
    default:
        if (addr + size <= 0x2000) {
            val = ldl_le_p(s->bar0_data + addr);
        }
        break;
    }
    return val;
}

static void pcibase_bar0_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= 0x2000) {
        return;
    }

    switch (addr) {
    case SOF_HDA_GCAP:
        stl_le_p(s->bar0_data + addr, (uint32_t)val);
        break;
    case SOF_HDA_GCTL:
        stl_le_p(s->bar0_data + addr, (uint32_t)val);
        break;
    case SOF_HDA_WAKESTS:
        stw_le_p(s->bar0_data + addr, (uint16_t)val);
        break;
    case SOF_HDA_INTCTL:
        stl_le_p(s->bar0_data + addr, (uint32_t)val);
        break;
    case SOF_HDA_INTSTS:
        /* Write-1-to-clear for INTSTS */
        {
            uint32_t old = ldl_le_p(s->bar0_data + addr);
            stl_le_p(s->bar0_data + addr, old & ~(uint32_t)val);
        }
        break;
    case SOF_HDA_VS_INTEL_EM2:
        stl_le_p(s->bar0_data + addr, (uint32_t)val);
        break;
    case HDA_VS_INTEL_LTRP:
        stl_le_p(s->bar0_data + addr, (uint32_t)val);
        break;
    default:
        if (addr + size <= 0x2000) {
            stl_le_p(s->bar0_data + addr, (uint32_t)val);
        }
        break;
    }
}

static uint64_t pcibase_bar1_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= 0x100) {
        return 0;
    }
    switch (addr) {
    case SOF_HDA_REG_PP_PPCTL:
        val = ldl_le_p(s->bar1_data + addr);
        break;
    default:
        val = ldl_le_p(s->bar1_data + addr);
        break;
    }
    return val;
}

static void pcibase_bar1_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= 0x100) {
        return;
    }
    switch (addr) {
    case SOF_HDA_REG_PP_PPCTL:
        stl_le_p(s->bar1_data + addr, (uint32_t)val);
        break;
    default:
        stl_le_p(s->bar1_data + addr, (uint32_t)val);
        break;
    }
}

static uint64_t pcibase_bar2_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr >= 0x100) {
        return 0;
    }
    return ldl_le_p(s->bar2_data + addr);
}

static void pcibase_bar2_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr >= 0x100) {
        return;
    }
    stl_le_p(s->bar2_data + addr, (uint32_t)val);
}

static uint64_t pcibase_bar4_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr >= 0x100000) {
        return 0;
    }
    return ldl_le_p(s->bar4_data + addr);
}

static void pcibase_bar4_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr >= 0x100000) {
        return;
    }
    stl_le_p(s->bar4_data + addr, (uint32_t)val);
    /* If writing to loader base, we could trigger DSP boot, but we assume
     * firmware is already loaded and FSR is already INIT_DONE. */
}

/* Per-BAR MemoryRegionOps */
static const MemoryRegionOps pcibase_bar0_ops = {
    .read = pcibase_bar0_read,
    .write = pcibase_bar0_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static const MemoryRegionOps pcibase_bar1_ops = {
    .read = pcibase_bar1_read,
    .write = pcibase_bar1_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static const MemoryRegionOps pcibase_bar2_ops = {
    .read = pcibase_bar2_read,
    .write = pcibase_bar2_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static const MemoryRegionOps pcibase_bar4_ops = {
    .read = pcibase_bar4_read,
    .write = pcibase_bar4_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

/* ========================================================================== */

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Reset BAR buffers to default values */
    if (s->bar0_data) {
        memset(s->bar0_data, 0, 0x2000);
        /* Set GCAP to indicate basic HDA capability: 4 output, 4 input streams */
        stl_le_p(s->bar0_data + SOF_HDA_GCAP, 0x4401);
    }
    if (s->bar1_data) {
        memset(s->bar1_data, 0, 0x100);
    }
    if (s->bar2_data) {
        memset(s->bar2_data, 0, 0x100);
    }
    if (s->bar4_data) {
        memset(s->bar4_data, 0, 0x100000);
        /* Set FSR to indicate firmware has initialized: INIT_DONE */
        stl_le_p(s->bar4_data + 0x0, FSR_STATE_INIT_DONE);
    }

    s->intr_status = 0;
    s->intr_mask = 0;
    s->in_reset = false;
    s->power_state = 0;
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }
    
    /* CRITICAL: PCI requires BAR sizes to be a power of 2. Prevent QEMU assert crash. */
    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    /* Select the appropriate MemoryRegionOps for each BAR */
    const MemoryRegionOps *ops = NULL;
    switch (bi->index) {
    case HDA_DSP_HDA_BAR:
        ops = &pcibase_bar0_ops;
        break;
    case HDA_DSP_PP_BAR:
        ops = &pcibase_bar1_ops;
        break;
    case HDA_DSP_SPIB_BAR:
        ops = &pcibase_bar2_ops;
        break;
    case HDA_DSP_BAR:
        ops = &pcibase_bar4_ops;
        break;
    default:
        error_setg(errp, "Unknown BAR index %d", bi->index);
        return;
    }

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), ops, s, bi->name, aligned_size);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_INTEL);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_INTEL_HDA_PTL);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_MULTIMEDIA_AUDIO);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* Allocate per-BAR data buffers */
    s->bar0_data = g_new0(uint8_t, 0x2000);
    s->bar1_data = g_new0(uint8_t, 0x100);
    s->bar2_data = g_new0(uint8_t, 0x100);
    s->bar4_data = g_new0(uint8_t, 0x100000);

    /* BAR Initialization */
    s->num_bars = 4;
    s->bar_info[0] = (BARInfo){ .index = HDA_DSP_HDA_BAR,  .type = BAR_TYPE_MMIO, .size = 0x2000, .name = "bar0-hda" };
    s->bar_info[1] = (BARInfo){ .index = HDA_DSP_PP_BAR,   .type = BAR_TYPE_MMIO, .size = 0x100,  .name = "bar1-pp" };
    s->bar_info[2] = (BARInfo){ .index = HDA_DSP_SPIB_BAR, .type = BAR_TYPE_MMIO, .size = 0x100,  .name = "bar2-spib" };
    s->bar_info[3] = (BARInfo){ .index = HDA_DSP_BAR,      .type = BAR_TYPE_MMIO, .size = 0x100000, .name = "bar4-dsp" };

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    if (msi_init(pdev, 0, 1, true, false, errp)) {
        return;
    }

    s->has_msi = true;

    pcibase_reset(DEVICE(s));
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

    g_free(s->bar0_data);
    g_free(s->bar1_data);
    g_free(s->bar2_data);
    g_free(s->bar4_data);
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
