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

#define TYPE_PCIBASE_DEVICE "sof_audio_pci_intel_lnl_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI Vendor and Device IDs */
#define VENDOR_ID 0x8086
#define DEVICE_ID 0xa828
#define CLASS_ID 0x040100 /* PCI_CLASS_MULTIMEDIA_AUDIO */

/* BAR Sizes - TODO: get actual values from driver defines */
#define HDA_DSP_HDA_BAR_SIZE  0x0000 /* request: define:HDA_DSP_HDA_BAR_SIZE */
#define HDA_DSP_PP_BAR_SIZE   0x0000 /* request: define:HDA_DSP_PP_BAR_SIZE */
#define HDA_DSP_SPIB_BAR_SIZE 0x0000 /* request: define:HDA_DSP_SPIB_BAR_SIZE */
#define HDA_DSP_BAR4_SIZE     0x0000 /* request: define:HDA_DSP_BAR4_SIZE */

/* Register Offsets and Bit Definitions from driver source */
#define SND_SOF_BARS 8
#define SOF_MAX_DSP_NUM_CORES 8
#define LNL_DSP_REG_HFDSC 0x160200
#define MTL_SSP_COUNT 3
#define SOF_DBG_IGNORE_D3_PERSISTENT BIT(7)
#define FSR_STATE_FW_ENTERED 0x5
#define HDA_DSP_REG_POLL_INTERVAL_US 500
#define HDA_DSP_ROM_IPC_CONTROL 0x01000000
#define HDA_FW_BOOT_ATTEMPTS 3
#define SOF_DBG_DUMP_OPTIONAL BIT(4)
#define FSR_STATE_INIT_DONE 0x1
#define FSR_TO_STATE_CODE(x) ((x) & FSR_STATE_MASK)
#define HDA_DSP_ROM_IPC_PURGE_FW 0x00004000
#define SOF_DBG_DUMP_MBOX BIT(1)
#define SOF_DBG_DUMP_PCI BIT(3)
#define HDA_DSP_BAR 4
#define SOF_DSP_PRIMARY_CORE 0
#define HDA_DSP_INIT_TIMEOUT_US 500000
#define HDA_DSP_RESET_TIMEOUT_US 50000
#define HDA_VS_INTEL_EM2_L1SEN BIT(13)
#define HDA_VS_INTEL_EM2 0x1030
#define HDA_DSP_HDA_BAR 0
#define SOF_HDA_INT_CTRL_EN BIT(30)
#define SOF_HDA_INT_GLOBAL_EN BIT(31)
#define SOF_HDA_INTCTL 0x20
#define HDA_CL_STREAM_FORMAT 0x40
#define SOF_HDA_SD_FIFOSIZE_FIFOS_MASK GENMASK(15, 0)
#define SOF_STREAM_SD_OFFSET(s) (SOF_HDA_ADSP_SD_ENTRY_SIZE * ((s)->index) + SOF_HDA_ADSP_LOADER_BASE)
#define SOF_HDA_ADSP_REG_SD_FIFOSIZE 0x10
#define HDA_DSP_BASEFW_TIMEOUT_US 3000000
#define HDA_DSP_PP_BAR 1
#define sof_ops(sdev) ((sdev)->pdata->desc->ops)
#define PCI_TCSEL 0x44
#define HDA_DSP_MBOX_UPLINK_OFFSET 0x81000
#define HDA_DSP_PD_TIMEOUT 50
#define SOF_DBG_PRINT_ALL_DUMPS BIT(6)
#define FSR_STATE_MASK GENMASK(23, 0)
#define SOF_DBG_DSPLESS_MODE BIT(15)
#define SND_SOF_SUSPEND_DELAY_MS 2000
#define sof_ipc_get_ops(sdev, ops_name) (((sdev)->ipc && (sdev)->ipc->ops) ? (sdev)->ipc->ops->ops_name : NULL)
#define SOF_DBG_FORCE_NOCODEC BIT(10)
#define SOF_HDA_PPCTL_PIE BIT(31)
#define SOF_HDA_REG_PP_PPCTL 0x04
#define SOF_HDA_PPCTL_GPROCEN BIT(30)
#define SOF_HDA_SD_CTL_DMA_START 0x02
#define SOF_HDA_CL_DMA_SD_INT_MASK (SOF_HDA_CL_DMA_SD_INT_DESC_ERR | SOF_HDA_CL_DMA_SD_INT_FIFO_ERR | SOF_HDA_CL_DMA_SD_INT_COMPLETE)
#define SOF_HDA_ADSP_LOADER_BASE 0x80
#define SOF_HDA_ADSP_SD_ENTRY_SIZE 0x20
#define FSR_HALTED BIT(31)
#define FSR_TO_WAIT_STATE_CODE(x) (((x) & FSR_WAIT_STATE_MASK) >> 24)
#define FSR_MOD_ROM_EXT 0x5
#define FSR_TO_MODULE_CODE(x) (((x) & FSR_MODULE_MASK) >> 28)
#define FSR_MOD_BRNGUP 0x4
#define SOF_HDA_D0I3_WORK_DELAY_MS 5000
#define HDA_IDISP_CODEC(x) ((x) & BIT(HDA_IDISP_ADDR))
#define SOF_HDA_GCAP 0x0
#define SOF_HDA_PPHC_BASE 0x10
#define SOF_HDA_PPLC_MULTI 0x10
#define SOF_HDA_SPIB_MAXFIFO 0x04
#define SOF_HDA_PPHC_INTERVAL 0x10
#define SOF_HDA_SPIB_INTERVAL 0x08
#define SOF_HDA_PPLC_BASE 0x10
#define SOF_HDA_PPLC_INTERVAL 0x10
#define SOF_HDA_DPIB_ENTRY_SIZE 0x8
#define SOF_HDA_PLAYBACK_STREAMS 16
#define HDA_DSP_BDL_SIZE 4096
#define SOF_HDA_SPIB_BASE 0x08
#define HDA_DSP_SPIB_BAR 2
#define SOF_HDA_SPIB_SPIB 0x00
#define SOF_HDA_CAPTURE_STREAMS 16
#define SOF_DBG_ENABLE_TRACE BIT(0)
#define SOF_HDA_GCTL 0x8
#define SOF_HDA_WAKESTS 0x0E
#define SOF_HDA_ADSP_DPLBASE 0x70
#define SOF_HDA_ADSP_DPUBASE 0x74
#define SOF_HDA_ADSP_REG_SD_STS 0x03
#define SOF_HDA_INT_ALL_STREAM 0xff
#define SOF_HDA_INTSTS 0x24
#define SOF_HDA_GCTL_RESET BIT(0)
#define SOF_HDA_WAKESTS_INT_MASK ((1 << 8) - 1)
#define SOF_HDA_CL_DMA_SD_INT_DESC_ERR 0x10
#define SOF_HDA_CL_DMA_SD_INT_FIFO_ERR 0x08
#define SOF_HDA_CL_DMA_SD_INT_COMPLETE 0x04
#define HDA_DSP_STREAM_RUN_TIMEOUT 300
#define HDA_DSP_SPIB_DISABLE 0
#define SOF_HDA_ADSP_REG_SD_BDLPL 0x18
#define SOF_HDA_ADSP_REG_SD_BDLPU 0x1C
#define HDA_DSP_SPIB_ENABLE 1
#define SOF_HDA_VS_D0I3C_I3 BIT(2)
#define FSR_WAIT_STATE_MASK GENMASK(27, 24)
#define FSR_MODULE_MASK GENMASK(30, 28)
#define HDA_DSP_ROM_UNEXPECTED_RESET 0xDECAF000
#define HDA_DSP_ROM_USER_EXCEPTION 0xBEEF0000
#define HDA_DSP_ROM_LOAD_OFFSET_TO_SMALL 47
#define HDA_DSP_ROM_BASE_FW_NOT_FOUND 43
#define HDA_DSP_ROM_IMR_TO_SMALL 42
#define HDA_DSP_ROM_BASEFW_INCOMPAT 51
#define HDA_DSP_ROM_KERNEL_EXCEPTION 0xCAFE0000
#define HDA_DSP_ROM_L2_CACHE_ERROR 46
#define HDA_DSP_ROM_CSE_WRONG_RESPONSE 41
#define HDA_DSP_ROM_IPC_FATAL_ERROR 45
#define HDA_DSP_ROM_NULL_FW_ENTRY 0x4c4c4e55
#define HDA_DSP_ROM_UNHANDLED_INTERRUPT 0xBEE00000
#define HDA_DSP_ROM_CSE_ERROR 40
#define HDA_DSP_ROM_MEMORY_HOLE_ECC 0xECC00000
#define HDA_DSP_ROM_API_PTR_INVALID 50
#define HDA_DSP_ROM_CSE_VALIDATION_FAILED 44
#define HDA_IDISP_ADDR 2
#define PCI_CGCTL 0x48
#define PCI_CGCTL_MISCBDCGE_MASK BIT(6)
#define HDA_DSP_CTRL_RESET_TIMEOUT 100
#define SOF_HDA_ADSP_REG_CL_SPBFIFO_SPBFCCTL 0x4
#define HDA_VS_INTEL_LTRP 0x1048
#define SOF_HDA_ADSP_REG_SD_LVI 0x0C
#define HDA_VS_INTEL_LTRP_GB_MASK 0x3F
#define SOF_HDA_ADSP_REG_SD_CBL 0x08
#define SOF_INTEL_PROCEN_FMT_QUIRK BIT(0)
#define SOF_HDA_ADSP_DPLBASE_ENABLE 0x01
#define SOF_HDA_CL_SD_CTL_STREAM_TAG_SHIFT 20
#define SOF_HDA_ADSP_REG_SD_FORMAT 0x12
#define SOF_HDA_CL_SD_CTL_STREAM_TAG_MASK GENMASK(23, 20)

/* BDL Entry (Buffer Descriptor List) */
struct sof_intel_dsp_bdl {
    uint32_t addr_l;
    uint32_t addr_h;
    uint32_t size;
    uint32_t ioc;
} __attribute__((packed));

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

    /* DMA Context */
    struct {
        hwaddr bdl_addr;
        uint32_t bdl_size;
        bool dma_active;
    } dma;

    uint32_t fsr;              /* Firmware Status Register */
    bool dsp_running;          /* Operational status flag */
    bool in_reset;             /* Probe/Reset state */
    int reset_attempts;
    uint32_t pm_state;         /* Power management state (D0-D3) */
};

/* Internal helper for status-triggered signaling */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
}

/* Device-initiated DMA logic */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
}

/* MMIO/PIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 4;
    s->bar_info[0].index = 0; s->bar_info[0].type = BAR_TYPE_MMIO; s->bar_info[0].size = HDA_DSP_HDA_BAR_SIZE; s->bar_info[0].name = "bar0";
    s->bar_info[1].index = 1; s->bar_info[1].type = BAR_TYPE_MMIO; s->bar_info[1].size = HDA_DSP_PP_BAR_SIZE; s->bar_info[1].name = "bar1";
    s->bar_info[2].index = 2; s->bar_info[2].type = BAR_TYPE_MMIO; s->bar_info[2].size = HDA_DSP_SPIB_BAR_SIZE; s->bar_info[2].name = "bar2";
    s->bar_info[3].index = 4; s->bar_info[3].type = BAR_TYPE_MMIO; s->bar_info[3].size = HDA_DSP_BAR4_SIZE; s->bar_info[3].name = "bar4";

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
    .name = "sof_audio_pci_intel_lnl_pci",
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
