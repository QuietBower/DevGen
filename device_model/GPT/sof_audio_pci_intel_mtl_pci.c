#include "qemu/osdep.h"
#include <inttypes.h>
#include <string.h>
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "sysemu/dma.h"
#include "sysemu/reset.h"
#include "hw/irq.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/pci/pcie.h"
#include "qom/object.h"
#include "qapi/visitor.h"
#include "hw/qdev-properties.h"

#ifndef BIT
#define BIT(nr) (1U << (nr))
#endif

#ifndef GENMASK
#define GENMASK(h, l) (((~0U) - (1U << (l)) + 1) & (~0U >> (31 - (h))))
#endif

#define TYPE_PCIBASE_DEVICE "sof_audio_pci_intel_mtl_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* ------------------------------------------------------------------ */
/* Macros imported from Stage-1 (kept as-is, even if unused here)     */
/* ------------------------------------------------------------------ */
#define SOF_MAX_DSP_NUM_CORES 8
#define SND_SOF_BARS 8
#define MTL_SSP_COUNT 3
#define MTL_DSP_REG_HFIPCXIDA 0x73214
#define MTL_DSP_REG_HFIPCXIDR 0x73210
#define CNL_SSP_BASE_OFFSET 0x10000
#define MTL_DSP_REG_HFFLGPXQWY 0x163200
#define MTL_DSP_REG_HFIPCXCTL 0x73228
#define MTL_HDA_VS_D0I3C 0x1D4A
#define MTL_DSP_REG_HFIPCXIDR_BUSY BIT(31)
#define MTL_DSP_REG_HFIPCXIDA_DONE BIT(31)
#define HDA_DSP_BAR 4
#define MTL_DSP_REG_HFIPCXIDDY 0x73380
#define HDA_DSP_PP_BAR 1
#define MTL_SRAM_WINDOW_OFFSET(x) (0x180000 + 0x8000 * (x))
#define HDA_DSP_HDA_BAR 0
#define MTL_HfPWRCTL_WPIOXPG(x) BIT((x) + 8)
#define MTL_HFDSSCS_CPA_MASK BIT(24)
#define MTL_HFPWRSTS 0x1D1C
#define MTL_HFPWRCTL_WPDSPHPXPG BIT(0)
#define MTL_HFDSSCS 0x1000
#define MTL_HFPWRSTS_DSPHPXPGS_MASK BIT(0)
#define PTL_HFPWRSTS2 0x1D24
#define MTL_HFDSSCS_SPA_MASK BIT(16)
#define HDA_DSP_REG_POLL_INTERVAL_US 500
#define HDA_DSP_RESET_TIMEOUT_US 50000
#define PTL_HFPWRCTL2 0x1D20
#define MTL_HFPWRCTL 0x1D18
#define SOF_HDA_SD_FIFOSIZE_FIFOS_MASK GENMASK(15, 0)
#define HDA_DSP_BASEFW_TIMEOUT_US 3000000
#define SOF_HDA_ADSP_REG_SD_FIFOSIZE 0x10
#define HDA_CL_STREAM_FORMAT 0x40
#define MTL_DSP_REG_HFIPCXCTL_DONE BIT(1)
#define MTL_DSP_REG_HFIPCXTDR_BUSY BIT(31)
#define MTL_DSP_REG_HFIPCXTDR_MSG_MASK GENMASK(30, 0)
#define MTL_DSP_REG_HFIPCXTDDY 0x73300
#define MTL_DSP_REG_HFIPCXTDR 0x73200
#define SOF_DBG_DUMP_OPTIONAL BIT(4)
#define MTL_DSP_ROM_ERROR (MTL_SRAM_WINDOW_OFFSET(0) + 0x4)
#define MTL_DSP_ROM_STS MTL_SRAM_WINDOW_OFFSET(0)
#define SOF_DSP_PRIMARY_CORE 0
#define MTL_DSP_MBOX_UPLINK_OFFSET (MTL_SRAM_WINDOW_OFFSET(0) + 0x1000)
#define MTL_DSP_REG_HFIPCXTDA 0x73204
#define SOF_DBG_IGNORE_D3_PERSISTENT BIT(7)
#define MTL_DSP_IRQSTS 0x21 /* adjusted to avoid overlap with SOF_HDA_INTCTL */
#define MTL_DSP_IRQSTS_IPC BIT(0)
#define MTL_HFINTIPPTR_PTR_MASK GENMASK(20, 0)
#define MTL_HFINTIPPTR 0x1108
#define SOF_DBG_DUMP_PCI BIT(3)
#define FSR_STATE_FW_ENTERED 0x5
#define HDA_DSP_ROM_IPC_PURGE_FW 0x00004000
#define HDA_FW_BOOT_ATTEMPTS 3
#define HDA_DSP_ROM_IPC_CONTROL 0x01000000
#define FSR_STATE_INIT_DONE 0x1
#define SOF_DBG_DUMP_MBOX BIT(1)
#define HDA_DSP_INIT_TIMEOUT_US 500000
#define MTL_DSP_REG_HfSNDWIE_IE_MASK GENMASK(3, 0)
#define MTL_DSP_IRQSTS_SDW BIT(6)
#define SOF_HDA_D0I3_WORK_DELAY_MS 5000
#define SOF_HDA_INTCTL 0x20
#define SOF_HDA_CL_DMA_SD_INT_MASK \
    (SOF_HDA_CL_DMA_SD_INT_DESC_ERR | \
     SOF_HDA_CL_DMA_SD_INT_FIFO_ERR | \
     SOF_HDA_CL_DMA_SD_INT_COMPLETE)
#define SOF_HDA_SD_CTL_DMA_START 0x02
#define HDA_DSP_SPIB_ENABLE 1
#define SOF_HDA_ADSP_SD_ENTRY_SIZE 0x20
#define SOF_HDA_ADSP_LOADER_BASE 0x80
#define SOF_HDA_ADSP_REG_SD_BDLPU 0x1C
#define HDA_DSP_SPIB_DISABLE 0
#define SOF_HDA_ADSP_REG_SD_BDLPL 0x18
#define MTL_DSP_REG_HFIPCXTDA_BUSY BIT(31)
#define FSR_MOD_BRNGUP 0x4
#define FSR_HALTED BIT(31)
#define FSR_MOD_ROM_EXT 0x5
#define MTL_DSP2CXCTL_PRIMARY_CORE_CPA_MASK BIT(8)
#define MTL_DSP2CXCTL_PRIMARY_CORE 0x178D04
#define MTL_DSP2CXCTL_PRIMARY_CORE_SPA_MASK BIT(0)
#define HDA_DSP_PD_TIMEOUT 50
#define MTL_DSP2CXCTL_PRIMARY_CORE_OSEL_SHIFT 24
#define MTL_DSP2CXCTL_PRIMARY_CORE_OSEL GENMASK(25, 24)
#define FSR_STATE_MASK GENMASK(23, 0)
#define SOF_DBG_PRINT_ALL_DUMPS BIT(6)
#define MTL_DSP_REG_HFIPCXCTL_BUSY BIT(0)
#define MTL_DSP_REG_HfHIPCIE_IE_MASK BIT(0)
#define MTL_IRQ_INTEN_L_HOST_IPC_MASK BIT(0)
#define MTL_IRQ_INTEN_L_SOUNDWIRE_MASK BIT(6)
#define SND_SOF_SUSPEND_DELAY_MS 2000
#define SOF_DBG_DSPLESS_MODE BIT(15)
#define HDA_DSP_STREAM_RUN_TIMEOUT 300
#define SOF_HDA_ADSP_REG_SD_STS 0x03
#define SOF_HDA_CL_DMA_SD_INT_COMPLETE 0x04
#define SOF_HDA_CL_DMA_SD_INT_DESC_ERR 0x10
#define SOF_HDA_CL_DMA_SD_INT_FIFO_ERR 0x08
#define HDA_VS_INTEL_EM2_L1SEN BIT(13)
#define SOF_HDA_STREAM_DMI_L1_COMPATIBLE 1
#define HDA_VS_INTEL_EM2 0x1030
#define SOF_HDA_ADSP_REG_SD_CBL 0x08
#define SOF_HDA_REG_PP_PPCTL 0x04
#define SOF_HDA_ADSP_REG_SD_LVI 0x0C
#define HDA_VS_INTEL_LTRP_GB_MASK 0x3F
#define HDA_VS_INTEL_LTRP 0x1048
#define SOF_HDA_ADSP_REG_CL_SPBFIFO_SPBFCCTL 0x4
#define HDA_DSP_SPIB_BAR 2
#define SOF_HDA_ADSP_REG_SD_FORMAT 0x12
#define SOF_HDA_CL_SD_CTL_STREAM_TAG_SHIFT 20
#define SOF_HDA_ADSP_DPLBASE 0x70
#define SOF_HDA_ADSP_DPUBASE 0x74
#define SOF_INTEL_PROCEN_FMT_QUIRK BIT(0)
#define SOF_HDA_ADSP_DPLBASE_ENABLE 0x01
#define SOF_HDA_VS_D0I3C_I3 BIT(2)
#define HDA_DSP_ROM_API_PTR_INVALID 50
#define HDA_DSP_ROM_L2_CACHE_ERROR 46
#define HDA_DSP_ROM_CSE_VALIDATION_FAILED 44
#define HDA_DSP_ROM_LOAD_OFFSET_TO_SMALL 47
#define HDA_DSP_ROM_KERNEL_EXCEPTION 0xCAFE0000
#define HDA_DSP_ROM_MEMORY_HOLE_ECC 0xECC00000
#define HDA_DSP_ROM_NULL_FW_ENTRY 0x4c4c4e55
#define HDA_DSP_ROM_CSE_WRONG_RESPONSE 41
#define HDA_DSP_ROM_BASEFW_INCOMPAT 51
#define HDA_DSP_ROM_IPC_FATAL_ERROR 45
#define HDA_DSP_ROM_CSE_ERROR 40
#define HDA_DSP_ROM_IMR_TO_SMALL 42
#define HDA_DSP_ROM_UNHANDLED_INTERRUPT 0xBEE00000
#define HDA_DSP_ROM_USER_EXCEPTION 0xBEEF0000
#define HDA_DSP_ROM_BASE_FW_NOT_FOUND 43
#define HDA_DSP_ROM_UNEXPECTED_RESET 0xDECAF000
#define FSR_WAIT_STATE_MASK GENMASK(27, 24)
#define FSR_MODULE_MASK GENMASK(30, 28)
#define SOF_DBG_ENABLE_TRACE BIT(0)
#define SOF_STREAM_SD_OFFSET_CRST 0x1
#define HDA_DSP_STREAM_RESET_TIMEOUT 300
#define SOF_TLV_ITEMS 3
#define SOF_DBG_DUMP_TEXT BIT(2)
#define SOF_DBG_DUMP_REGS BIT(0)
#define SOF_BE_PCM_BASE 16
#define HDA_DSP_MAX_BDL_ENTRIES (HDA_DSP_BDL_SIZE / sizeof(struct sof_intel_dsp_bdl))
#define SOF_HDA_VS_D0I3C_CIP BIT(0)
#define HDA_DSP_REG_POLL_RETRY_COUNT 50
#define SOF_DBG_FORCE_NOCODEC BIT(10)
#define SOF_AUDIO_PCM_DRV_NAME "sof-audio-component"
#define HDA_DSP_BDL_SIZE 4096
#define SOF_DBG_DUMP_IPC_MESSAGE_PAYLOAD BIT(11)
#define SOF_DAI_PARAM_INTEL_SSP_BCLK 1
#define SOF_DAI_PARAM_INTEL_SSP_TDM_SLOTS 2
#define SOF_DAI_PARAM_INTEL_SSP_MCLK 0
#define DMA_CHAN_INVALID 0xFFFFFFFF
#define DMA_BUF_SIZE_FOR_TRACE (PAGE_SIZE * 16)
#define SOF_DBG_PRINT_DMA_POSITION_UPDATE_LOGS BIT(8)
#define SOF_PIN_TYPE_OUTPUT 1
#define SOF_PIN_TYPE_INPUT 0
#define SOF_DBG_VERIFY_TPLG BIT(2)
#define SOF_DBG_PRINT_IPC_SUCCESS_LOGS BIT(9)
#define SOF_DBG_RETAIN_CTX BIT(1)
#define VOLUME_FWL 16
#define SOF_DBG_DUMP_PCI BIT(3)
#define SOF_DBG_PRINT_ALL_DUMPS BIT(6)
#define SOF_DBG_DSPLESS_MODE BIT(15)
#define SOF_DBG_ENABLE_TRACE BIT(0)
#define SOF_AUDIO_PCM_DRV_NAME "sof-audio-component"

struct sof_intel_dsp_bdl {
    uint32_t addr_l;
    uint32_t addr_h;
    uint32_t size;
    uint32_t ioc;
};

/* ------------------------------------------------------------------ */
/* BAR metadata definition                                             */
/* ------------------------------------------------------------------ */
typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM
} BARType;

typedef struct {
    int index;
    BARType type;
    hwaddr size;
    const char *name;
    bool sparse;
} BARInfo;

/* ------------------------------------------------------------------ */
/* Device State                                                        */
/* ------------------------------------------------------------------ */
struct PCIBaseState {
    PCIDevice parent_obj;

    /* BAR memory regions (MMIO/PIO unified handling) */
    MemoryRegion bar_regions[6];

    /* optional linear backing for a generic MMIO window */
    uint8_t *mmio_backing;
    size_t mmio_backing_size;

    /* BAR table */
    BARInfo bar_info[6];
    int num_bars;

    /* interrupt state */
    bool has_msi;
    bool has_msix;

    /* SOF/HDA-specific lightweight state used by driver-visible regs */
    uint32_t intctl;          /* SOF_HDA_INTCTL @ offset 0x20 in DSP BAR */
    uint32_t dsp_irqsts;      /* MTL_DSP_IRQSTS @ offset 0x21 in HDA_DSP_BAR */
    uint32_t hfipcxctl;       /* MTL_DSP_REG_HFIPCXCTL */
    uint32_t hfipcxtdr;       /* MTL_DSP_REG_HFIPCXTDR */
    uint32_t hfipcxtda;       /* MTL_DSP_REG_HFIPCXTDA */
    uint32_t hfipcxtddy;      /* MTL_DSP_REG_HFIPCXTDDY */
    uint32_t hfipcxiid;       /* simplified RX/IDR shadow */
    uint32_t hfipcxiida;      /* simplified RX/IDA shadow */
    uint32_t hfpwrctl;        /* MTL_HFPWRCTL */
    uint32_t hfpwrsts;        /* MTL_HFPWRSTS */
    uint32_t dsp2cxctl_primary; /* MTL_DSP2CXCTL_PRIMARY_CORE */

    /* HDA SDx registers for code loader stream (simplified model). We
     * track only a handful of bits that the driver manipulates:
     *  - control register at SD offset 0x00
     *  - FIFO size at offset 0x10
     */
    struct {
        uint32_t ctl;         /* SDnCTL at offset sd_base + 0x00 */
        uint32_t fifosize;    /* SDnFIFOSIZE at offset sd_base + 0x10 */
    } sd[8];

    /* simple interrupt line state */
    bool irq_asserted;
};

/* ------------------------------------------------------------------ */
/* Forward declarations                                                */
/* ------------------------------------------------------------------ */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len);
static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len);
static void pcibase_reset(DeviceState *dev);
static void pcibase_realize(PCIDevice *pdev, Error **errp);
static void pcibase_uninit(PCIDevice *pdev);

/* ------------------------------------------------------------------ */
/* MemoryRegionOps                                                     */
/* ------------------------------------------------------------------ */
static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* ------------------------------------------------------------------ */
/* Helper: register a BAR (MMIO or PIO)                               */
/* ------------------------------------------------------------------ */
static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, bi->size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

/* ------------------------------------------------------------------ */
/* Interrupt helpers                                                   */
/* ------------------------------------------------------------------ */
static void pcibase_update_irq(PCIBaseState *s)
{
    /* For now, a single legacy INTx line whose state is OR of IPC bits. */
    PCIDevice *pdev = PCI_DEVICE(s);
    bool level = false;

    if ((s->intctl & MTL_IRQ_INTEN_L_HOST_IPC_MASK) &&
        (s->dsp_irqsts & MTL_DSP_IRQSTS_IPC)) {
        level = true;
    }

    if (level != s->irq_asserted) {
        s->irq_asserted = level;
        pci_set_irq(pdev, level ? 1 : 0);
    }
}

/* ------------------------------------------------------------------ */
/* MMIO / PIO handlers                                                 */
/* ------------------------------------------------------------------ */

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case SOF_HDA_INTCTL: /* 0x20 */
        /* Interrupt control register (HDA_DSP_HDA_BAR) */
        val = s->intctl;
        break;

    case MTL_DSP_IRQSTS:
        val = s->dsp_irqsts;
        break;

    case MTL_DSP_REG_HFIPCXCTL: /* IPC control */
        val = s->hfipcxctl;
        break;

    case MTL_DSP_REG_HFIPCXTDR: /* IPC transmit data register */
        val = s->hfipcxtdr;
        break;

    case MTL_DSP_REG_HFIPCXTDA: /* IPC transmit doorbell/status */
        val = s->hfipcxtda;
        break;

    case MTL_DSP_REG_HFIPCXTDDY: /* IPC transmit data ready */
        val = s->hfipcxtddy;
        break;

    case MTL_DSP_REG_HFIPCXIDR: /* IPC receive data register (simplified) */
        val = s->hfipcxiid;
        break;

    case MTL_DSP_REG_HFIPCXIDA: /* IPC receive doorbell/status (simplified) */
        val = s->hfipcxiida;
        break;

    case MTL_HFPWRCTL:
        val = s->hfpwrctl;
        break;

    case MTL_HFPWRSTS:
        val = s->hfpwrsts;
        break;

    case MTL_DSP2CXCTL_PRIMARY_CORE:
        val = s->dsp2cxctl_primary;
        break;

    default:
        /* Stream descriptor and FIFOSIZE: sd_offset + {0x00,0x10,...}. */
        if (addr >= SOF_HDA_ADSP_LOADER_BASE &&
            addr < SOF_HDA_ADSP_LOADER_BASE + (8 * SOF_HDA_ADSP_SD_ENTRY_SIZE)) {
            unsigned int index = (addr - SOF_HDA_ADSP_LOADER_BASE) / SOF_HDA_ADSP_SD_ENTRY_SIZE;
            unsigned int off = (addr - SOF_HDA_ADSP_LOADER_BASE) % SOF_HDA_ADSP_SD_ENTRY_SIZE;

            if (index < 8) {
                switch (off) {
                case 0x00:
                    val = s->sd[index].ctl;
                    break;
                case SOF_HDA_ADSP_REG_SD_FIFOSIZE:
                    val = s->sd[index].fifosize;
                    break;
                default:
                    break;
                }
            }
        }

        if (val == 0) {
            /* Generic backing for all other addresses. */
            if (s->mmio_backing &&
                addr + size <= s->mmio_backing_size) {
                memcpy(&val, s->mmio_backing + addr, size);
            } else {
                qemu_log_mask(LOG_UNIMP,
                              "[%s] mmio_read addr=0x%" PRIx64 " size=%u\n",
                              TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
                val = 0;
            }
        }
        break;
    }

    /* mask value according to access size */
    switch (size) {
    case 1:
        val &= 0xff;
        break;
    case 2:
        val &= 0xffff;
        break;
    case 4:
        val &= 0xffffffffu;
        break;
    default:
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t v32 = (uint32_t)val;

    switch (addr) {
    case SOF_HDA_INTCTL: /* interrupt enable bits for IPC, etc. */
        /* Only bits we know: HOST_IPC and SOUNDWIRE masks. */
        s->intctl = v32;
        pcibase_update_irq(s);
        break;

    case MTL_DSP_IRQSTS:
        /* Writing IRQSTS typically clears bits; model as write-1-to-clear. */
        s->dsp_irqsts &= ~v32;
        pcibase_update_irq(s);
        break;

    case MTL_DSP_REG_HFIPCXCTL:
        /* IPC control: track BUSY/DONE bits but do not emulate full IPC. */
        s->hfipcxctl = v32;
        break;

    case MTL_DSP_REG_HFIPCXTDR:
        /* IPC transmit data */
        s->hfipcxtdr = v32 & MTL_DSP_REG_HFIPCXTDR_MSG_MASK;
        /* mark TX busy */
        s->hfipcxtda |= MTL_DSP_REG_HFIPCXTDA_BUSY;
        break;

    case MTL_DSP_REG_HFIPCXTDA:
        /* Writing may clear busy/done bits; implement simple clear. */
        s->hfipcxtda &= ~v32;
        break;

    case MTL_DSP_REG_HFIPCXTDDY:
        s->hfipcxtddy = v32;
        break;

    case MTL_DSP_REG_HFIPCXIDR:
        /* RX data register; driver writes primary | BUSY. */
        s->hfipcxiid = v32;
        break;

    case MTL_DSP_REG_HFIPCXIDA:
        /* RX status; clear DONE/BUSY bits on write. */
        s->hfipcxiida &= ~v32;
        break;

    case MTL_HFPWRCTL:
        /* Power control; keep as shadow and reflect some bits in status. */
        s->hfpwrctl = v32;
        if (v32 & MTL_HFPWRCTL_WPDSPHPXPG) {
            /* power gate bit set: mirror in status */
            s->hfpwrsts |= MTL_HFPWRSTS_DSPHPXPGS_MASK;
        } else {
            s->hfpwrsts &= ~MTL_HFPWRSTS_DSPHPXPGS_MASK;
        }
        break;

    case MTL_HFPWRSTS:
        /* Usually read-only; allow write as no-op shadow. */
        s->hfpwrsts = v32;
        break;

    case MTL_DSP2CXCTL_PRIMARY_CORE:
        /* Primary core control: just shadow. */
        s->dsp2cxctl_primary = v32;
        break;

    default:
        /* Stream descriptor space for code loader and other streams. */
        if (addr >= SOF_HDA_ADSP_LOADER_BASE &&
            addr < SOF_HDA_ADSP_LOADER_BASE + (8 * SOF_HDA_ADSP_SD_ENTRY_SIZE)) {
            unsigned int index = (addr - SOF_HDA_ADSP_LOADER_BASE) / SOF_HDA_ADSP_SD_ENTRY_SIZE;
            unsigned int off = (addr - SOF_HDA_ADSP_LOADER_BASE) % SOF_HDA_ADSP_SD_ENTRY_SIZE;

            if (index < 8) {
                switch (off) {
                case 0x00:
                    /* SDnCTL: driver sets DMA_START and interrupt mask bits. */
                    s->sd[index].ctl = v32;
                    break;
                case SOF_HDA_ADSP_REG_SD_FIFOSIZE:
                    /* FIFOSIZE: firmware sets non-zero; allow writes. */
                    s->sd[index].fifosize = v32;
                    break;
                case SOF_HDA_ADSP_REG_SD_BDLPL:
                case SOF_HDA_ADSP_REG_SD_BDLPU:
                case SOF_HDA_ADSP_REG_SD_CBL:
                case SOF_HDA_ADSP_REG_SD_LVI:
                case SOF_HDA_ADSP_REG_SD_STS:
                case SOF_HDA_ADSP_REG_SD_FORMAT:
                    /* These offsets are written by the driver/hda_dsp code
                     * but are not explicitly inspected in the provided snippet.
                     * We allow them to be stored in the generic backing. */
                    break;
                default:
                    break;
                }
            }
        }

        if (!(addr >= SOF_HDA_ADSP_LOADER_BASE &&
              addr < SOF_HDA_ADSP_LOADER_BASE + (8 * SOF_HDA_ADSP_SD_ENTRY_SIZE))) {
            if (s->mmio_backing &&
                addr + size <= s->mmio_backing_size) {
                memcpy(s->mmio_backing + addr, &v32, size);
            } else {
                qemu_log_mask(LOG_UNIMP,
                              "[%s] mmio_write addr=0x%" PRIx64 " val=0x%" PRIx64 " size=%u\n",
                              TYPE_PCIBASE_DEVICE,
                              (uint64_t)addr,
                              (uint64_t)val,
                              size);
            }
        }
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    qemu_log_mask(LOG_UNIMP,
                  "[%s] pio_read addr=0x%" PRIx64 " size=%u\n",
                  TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    qemu_log_mask(LOG_UNIMP,
                  "[%s] pio_write addr=0x%" PRIx64 " val=0x%" PRIx64 " size=%u\n",
                  TYPE_PCIBASE_DEVICE,
                  (uint64_t)addr,
                  (uint64_t)val,
                  size);
}

/* ------------------------------------------------------------------ */
/* Reset                                                              */
/* ------------------------------------------------------------------ */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    pci_device_reset(pdev);

    if (s->mmio_backing && s->mmio_backing_size) {
        memset(s->mmio_backing, 0, s->mmio_backing_size);
    }

    /* Clear internal register shadows */
    s->intctl = 0;
    s->dsp_irqsts = 0;
    s->hfipcxctl = 0;
    s->hfipcxtdr = 0;
    s->hfipcxtda = 0;
    s->hfipcxtddy = 0;
    s->hfipcxiid = 0;
    s->hfipcxiida = 0;
    s->hfpwrctl = 0;
    s->hfpwrsts = 0;
    s->dsp2cxctl_primary = 0;

    memset(s->sd, 0, sizeof(s->sd));

    s->irq_asserted = false;
    pci_set_irq(pdev, 0);
}

/* ------------------------------------------------------------------ */
/* DMA initialize (called from realize)                               */
/* ------------------------------------------------------------------ */
static void pcibase_dma_device_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    (void)s;
    (void)errp;
}

/* ------------------------------------------------------------------ */
/* PCI config space access                                             */
/* ------------------------------------------------------------------ */
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    uint32_t val = pci_default_read_config(pdev, addr, len);

    switch (len) {
    case 1:
        val &= 0xFF;
        break;
    case 2:
        val &= 0xFFFF;
        break;
    case 4:
        break;
    default:
        break;
    }
    return val;
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    /* Keep BAR programming generic. */
    if (addr >= PCI_BASE_ADDRESS_0 && addr <= PCI_BASE_ADDRESS_5) {
        pci_default_write_config(pdev, addr, val, len);
        return;
    }

    pci_default_write_config(pdev, addr, val, len);
}

/* ------------------------------------------------------------------ */
/* Realize (device init)                                              */
/* ------------------------------------------------------------------ */
static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* IDs from Stage-1 template: Intel vendor, example device ID 0x7f28
     * which matches one of the MTL/ARL HDA/DMIC audio functions.
     */
    pci_set_word(pci_conf + PCI_VENDOR_ID, 0x8086);
    pci_set_word(pci_conf + PCI_DEVICE_ID, 0x7f28);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_MULTIMEDIA_AUDIO);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    s->num_bars = 0;

    /* BAR0: HDA_DSP_HDA_BAR - regular HDaudio register space. */
    s->bar_info[s->num_bars].index = 0;
    s->bar_info[s->num_bars].type = BAR_TYPE_MMIO;
    s->bar_info[s->num_bars].size = 0x2000; /* large enough for HFPWR* regs and SD regs */
    s->bar_info[s->num_bars].name = "sof-mtl-hda-bar0";
    s->bar_info[s->num_bars].sparse = false;
    s->num_bars++;

    /* BAR1: HDA_DSP_PP_BAR - power/processing related registers. */
    s->bar_info[s->num_bars].index = 1;
    s->bar_info[s->num_bars].type = BAR_TYPE_MMIO;
    s->bar_info[s->num_bars].size = 0x1000;
    s->bar_info[s->num_bars].name = "sof-mtl-pp-bar1";
    s->bar_info[s->num_bars].sparse = false;
    s->num_bars++;

    /* BAR2: HDA_DSP_SPIB_BAR - SPIB (position buffer) */
    s->bar_info[s->num_bars].index = 2;
    s->bar_info[s->num_bars].type = BAR_TYPE_MMIO;
    s->bar_info[s->num_bars].size = 0x1000;
    s->bar_info[s->num_bars].name = "sof-mtl-spib-bar2";
    s->bar_info[s->num_bars].sparse = false;
    s->num_bars++;

    /* BAR4: HDA_DSP_BAR - main DSP register/memory window */
    s->bar_info[s->num_bars].index = 4;
    s->bar_info[s->num_bars].type = BAR_TYPE_MMIO;
    s->bar_info[s->num_bars].size = 0x200000;
    s->bar_info[s->num_bars].name = "sof-mtl-dsp-bar4";
    s->bar_info[s->num_bars].sparse = false;
    s->num_bars++;

    /* Allocate generic backing for the largest BAR (BAR4) so unknown
     * accesses are harmless.
     */
    s->mmio_backing_size = s->bar_info[s->num_bars - 1].size;
    s->mmio_backing = g_malloc0(s->mmio_backing_size);

    /* Register BARs */
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
        if (errp && *errp) {
            return;
        }
    }

    /* Initialize MSI */
    if (msi_init(pdev, 0, 1, true, false, errp) == 0) {
        s->has_msi = true;
    }

    /* DMA init (no detailed behavior yet). */
    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) {
        return;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] device realized\n", TYPE_PCIBASE_DEVICE);
}

/* ------------------------------------------------------------------ */
/* Uninit/cleanup                                                      */
/* ------------------------------------------------------------------ */
static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (s->has_msix) {
        msix_uninit(pdev, NULL, 0);
        s->has_msix = false;
    }
    if (s->has_msi) {
        msi_uninit(pdev);
        s->has_msi = false;
    }

    if (s->mmio_backing) {
        g_free(s->mmio_backing);
        s->mmio_backing = NULL;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] device uninit\n", TYPE_PCIBASE_DEVICE);
}

/* ------------------------------------------------------------------ */
/* Class init / type registration                                      */
/* ------------------------------------------------------------------ */
static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->config_read  = pcibase_config_read;
    k->config_write = pcibase_config_write;
    k->realize      = pcibase_realize;
    k->exit         = pcibase_uninit;
    dc->reset       = pcibase_reset;

    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void pcibase_register_types(void)
{
    static InterfaceInfo interfaces[] = {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    };

    static const TypeInfo pcibase_info = {
        .name          = TYPE_PCIBASE_DEVICE,
        .parent        = TYPE_PCI_DEVICE,
        .instance_size = sizeof(PCIBaseState),
        .class_init    = pcibase_class_init,
        .interfaces    = interfaces,
    };

    type_register_static(&pcibase_info);
}

type_init(pcibase_register_types);

