/*
 * This template provides a robust skeleton for hardware emulation.
 * Integrated QEMU PCI device template (QEMU 8.2.10).
 * Replace #PLACEHOLDER# blocks with driver-specific definitions.
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

/* Additional include files retrieved from driver context */
/* #HeadFile# - none needed for static definitions */

#define TYPE_PCIBASE_DEVICE "sof_audio_pci_intel_apl_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
/* #Related_Config_Info# */
/* BIT already defined in qemu/bitops.h; removed redefinition */

/* PCI Vendor and Device ID from supplementary source */
#define PCI_VENDOR_ID_INTEL             0x8086
#define PCI_DEVICE_ID_INTEL_HDA_APL     0x5a98

/* BAR indices */
#define HDA_DSP_HDA_BAR     0
#define HDA_DSP_PP_BAR      1
#define HDA_DSP_SPIB_BAR    2
#define HDA_DSP_BAR         4
#define SND_SOF_BARS        8

/* Base addresses */
#define HDA_DSP_IPC_BASE    0x40
#define HDA_DSP_GEN_BASE    0x00
#define SRAM_WINDOW_OFFSET(x)   (0x80000 + (x) * 0x20000)
#define HDA_DSP_MBOX_OFFSET     SRAM_WINDOW_OFFSET(0)

/* Register offsets (relative to base) */
#define HDA_DSP_REG_HIPCT        (HDA_DSP_IPC_BASE + 0x00)
#define HDA_DSP_REG_HIPCTE       (HDA_DSP_IPC_BASE + 0x04)
#define HDA_DSP_REG_HIPCI        (HDA_DSP_IPC_BASE + 0x08)
#define HDA_DSP_REG_HIPCIE       (HDA_DSP_IPC_BASE + 0x0C)
#define HDA_DSP_REG_HIPCCTL      (HDA_DSP_IPC_BASE + 0x10)
#define HDA_DSP_SRAM_REG_ROM_STATUS (HDA_DSP_MBOX_OFFSET + 0x0)
#define HDA_DSP_REG_ADSPCS       (HDA_DSP_GEN_BASE + 0x04)
#define HDA_DSP_REG_ADSPIC       (HDA_DSP_GEN_BASE + 0x08)
#define HDA_DSP_REG_ADSPIS       (HDA_DSP_GEN_BASE + 0x0C)
#define SOF_HDA_REG_PP_PPCTL     0x04
#define SOF_HDA_REG_PP_PPSTS     0x08
#define SOF_HDA_INTCTL           0x20
#define SOF_HDA_INTSTS           0x24
#define SOF_HDA_ADSP_LOADER_BASE 0x80
#define SOF_HDA_ADSP_SD_ENTRY_SIZE 0x20
#define SOF_HDA_ADSP_REG_SD_STS  0x03
#define SOF_HDA_ADSP_REG_SD_LVI  0x0C
#define SOF_HDA_ADSP_REG_SD_FIFOSIZE 0x10
#define SOF_HDA_ADSP_REG_SD_FORMAT 0x12
#define SOF_HDA_ADSP_REG_SD_BDLPL 0x18
#define SOF_HDA_ADSP_REG_SD_BDLPU 0x1C
#define SOF_HDA_ADSP_REG_SD_CBL  0x08
#define SOF_HDA_ADSP_DPLBASE      0x70
#define SOF_HDA_ADSP_DPUBASE      0x74
#define SOF_HDA_ADSP_REG_CL_SPBFIFO_SPBFCCTL 0x4
#define SOF_HDA_VS_D0I3C          0x104A
#define HDA_VS_INTEL_EM2          0x1030
#define HDA_VS_INTEL_LTRP         0x1048
#define PCI_PGCTL                 0x44  /* PCI_TCSEL */
#define PCI_CGCTL                 0x48
#define HDA_CL_STREAM_FORMAT      0x40

/* Bit definitions */
#define HDA_DSP_REG_HIPCT_BUSY        BIT(31)
#define HDA_DSP_REG_HIPCT_MSG_MASK    0x7FFFFFFF
#define HDA_DSP_REG_HIPCTE_MSG_MASK   0x3FFFFFFF
#define HDA_DSP_REG_HIPCI_BUSY        BIT(31)
#define HDA_DSP_REG_HIPCI_MSG_MASK    0x7FFFFFFF
#define HDA_DSP_REG_HIPCIE_DONE       BIT(30)
#define HDA_DSP_REG_HIPCIE_MSG_MASK   0x3FFFFFFF
#define HDA_DSP_REG_HIPCCTL_BUSY      BIT(0)
#define HDA_DSP_REG_HIPCCTL_DONE      BIT(1)
#define HDA_DSP_ADSPIC_IPC            BIT(0)
#define HDA_DSP_ADSPIS_IPC            BIT(0)
#define HDA_DSP_ADSPIS_CL_DMA         BIT(1)
#define HDA_DSP_ADSPCS_CRST_SHIFT     0
#define HDA_DSP_ADSPCS_CRST_MASK(cm)  ((cm) << HDA_DSP_ADSPCS_CRST_SHIFT)
#define HDA_DSP_ADSPCS_CSTALL_SHIFT    8
#define HDA_DSP_ADSPCS_CSTALL_MASK(cm) ((cm) << HDA_DSP_ADSPCS_CSTALL_SHIFT)
#define HDA_DSP_ADSPCS_SPA_SHIFT      16
#define HDA_DSP_ADSPCS_SPA_MASK(cm)   ((cm) << HDA_DSP_ADSPCS_SPA_SHIFT)
#define HDA_DSP_ADSPCS_CPA_SHIFT      24
#define HDA_DSP_ADSPCS_CPA_MASK(cm)   ((cm) << HDA_DSP_ADSPCS_CPA_SHIFT)
#define SOF_HDA_PPCTL_GPROCEN         BIT(30)
#define SOF_HDA_VS_D0I3C_I3           BIT(2)
#define SOF_HDA_VS_D0I3C_CIP          BIT(0)
#define HDA_VS_INTEL_EM2_L1SEN        BIT(13)
#define HDA_VS_INTEL_LTRP_GB_MASK     0x3F
#define PCI_PGCTL_ADSPPGD             BIT(2)
#define PCI_CGCTL_ADSPDCGE            BIT(1)
#define SOF_HDA_CL_DMA_SD_INT_DESC_ERR 0x10
#define SOF_HDA_CL_DMA_SD_INT_FIFO_ERR 0x08
#define SOF_HDA_CL_DMA_SD_INT_COMPLETE 0x04
#define SOF_HDA_CL_DMA_SD_INT_MASK \
    (SOF_HDA_CL_DMA_SD_INT_DESC_ERR | \
     SOF_HDA_CL_DMA_SD_INT_FIFO_ERR | \
     SOF_HDA_CL_DMA_SD_INT_COMPLETE)
#define SOF_HDA_SD_CTL_DMA_START       0x02
#define SOF_HDA_CL_SD_CTL_STREAM_TAG_SHIFT 20
#define SOF_HDA_CL_SD_CTL_STREAM_TAG_MASK \
    GENMASK(SOF_HDA_CL_SD_CTL_STREAM_TAG_SHIFT + 3, \
            SOF_HDA_CL_SD_CTL_STREAM_TAG_SHIFT)
#define SOF_HDA_SD_FIFOSIZE_FIFOS_MASK  GENMASK(15, 0)
#define SOF_HDA_ADSP_REG_SD_FORMAT      0x12
#define SOF_HDA_ADSP_DPLBASE_ENABLE     0x01
#define HDA_DSP_SPIB_ENABLE             1
#define HDA_DSP_SPIB_DISABLE            0
#define HDA_DSP_STREAM_RUN_TIMEOUT      300
#define HDA_DSP_STREAM_RESET_TIMEOUT    300
#define HDA_DSP_BDL_SIZE                4096
#define HDA_DSP_MAX_BDL_ENTRIES         (HDA_DSP_BDL_SIZE / sizeof(struct sof_intel_dsp_bdl))
#define SOF_HDA_ADSP_SD_ENTRY_SIZE      0x20
#define SOF_STREAM_SD_OFFSET(s) \
    (SOF_HDA_ADSP_SD_ENTRY_SIZE * ((s)->index) + SOF_HDA_ADSP_LOADER_BASE)

/* Firmware states */
#define FSR_STATE_MASK              GENMASK(23, 0)
#define FSR_STATE_INIT_DONE         0x1
#define FSR_STATE_FW_ENTERED        0x5
#define FSR_TO_STATE_CODE(x)        ((x) & FSR_STATE_MASK)

/* IPC commands */
#define HDA_DSP_ROM_IPC_CONTROL     0x01000000
#define HDA_DSP_ROM_IPC_PURGE_FW    0x00004000
#define SOF_GLB_TYPE(x)             ((x) << 24)
#define SOF_IPC_FW_READY            SOF_GLB_TYPE(0x7U)

/* Other constants */
#define HDA_FW_BOOT_ATTEMPTS        3
#define HDA_DSP_INIT_TIMEOUT_US     500000
#define HDA_DSP_BASEFW_TIMEOUT_US   3000000
#define HDA_DSP_REG_POLL_INTERVAL_US 500
#define HDA_DSP_REG_POLL_RETRY_COUNT 50
#define HDA_DSP_RESET_TIMEOUT_US    50000
#define HDA_DSP_PD_TIMEOUT          50
#define SOF_MAX_DSP_NUM_CORES       8
#define APL_SSP_COUNT               6
#define APL_SSP_BASE_OFFSET         0x2000
#define SSP_DEV_MEM_SIZE            0x1000
#define SOF_DBG_DUMP_REGS           BIT(0)
#define SOF_DBG_DUMP_MBOX           BIT(1)
#define SOF_DBG_DUMP_TEXT           BIT(2)
#define SOF_DBG_DUMP_PCI            BIT(3)
#define SOF_DBG_DUMP_OPTIONAL       BIT(4)
#define SOF_DBG_PRINT_ALL_DUMPS     BIT(6)
#define SOF_DBG_DSPLESS_MODE        BIT(15)
#define SOF_DBG_IGNORE_D3_PERSISTENT BIT(7)
#define SOF_DBG_ENABLE_TRACE        BIT(0)
#define SOF_DBG_FORCE_NOCODEC       BIT(10)
#define SOF_DBG_VERIFY_TPLG         BIT(2)
#define DMA_CHAN_INVALID            0xFFFFFFFF
#define SSP_SSC1_OFFSET             0x4
#define SOF_HDA_D0I3_WORK_DELAY_MS  5000
#define SND_SOF_SUSPEND_DELAY_MS    2000
#define SOF_AUDIO_PCM_DRV_NAME      "sof-audio-component"
#define SOF_BE_PCM_BASE             16
#define SOF_TLV_ITEMS               3

/* PCI class (audio) */
#define PCI_CLASS_AUDIO  0x040100

/* Helper macro */
#ifndef GENMASK
#define GENMASK(high, low) \
    (((~0UL) << (low)) & (~0UL >> (__BITS_PER_LONG - 1 - (high))))
#endif

/* Forward declaration */
struct sof_intel_dsp_bdl {
    uint32_t addr_l;
    uint32_t addr_h;
    uint32_t size;
    uint32_t ioc;
};

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

/* Wrapper opaque to pass BAR index with the state */
typedef struct BAROpaque {
    PCIBaseState *s;
    int bar_index;
} BAROpaque;

/* Timer stages for DMA and FW_READY simulation */
#define DMA_STAGE_DMA 1
#define DMA_STAGE_FW_READY 2

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[SND_SOF_BARS];
    BARInfo bar_info[SND_SOF_BARS];
    uint8_t *bar_mem[SND_SOF_BARS];
    BAROpaque *bar_opaques[SND_SOF_BARS];
    int num_bars;

    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;

    uint32_t dma_fake;
    uint32_t status;
    uint32_t pm_state;

    /* DMA engine state for code loader stream */
    uint32_t sd_ctl;
    uint32_t sd_lvi;
    uint32_t sd_bdlpl;
    uint32_t sd_bdlpu;
    uint32_t sd_cbl;
    uint32_t sd_fifosize;
    uint32_t sd_format;
    uint32_t sd_sts;
    bool sd_running;
    QEMUTimer dma_timer;
    int dma_timer_stage;

    /* IPC related */
    uint32_t hipci_ext;  /* HIPCIE written before HIPCI */
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t pending = s->intr_status & s->intr_mask;
    if (pending) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

static void pcibase_write_reg(PCIBaseState *s, int bar, hwaddr addr, uint32_t val)
{
    uint8_t *mem = s->bar_mem[bar];
    if (!mem) return;

    if (bar == HDA_DSP_HDA_BAR) {
        /* BAR0: HDA BAR with stream descriptors */
        switch (addr) {
            case 0x04: /* SDCTL */
                s->sd_ctl = val;
                memcpy(mem + addr, &val, 4);
                if (val & SOF_HDA_SD_CTL_DMA_START && !s->sd_running) {
                    s->sd_running = true;
                    s->dma_timer_stage = DMA_STAGE_DMA;
                    timer_mod(&s->dma_timer, qemu_clock_get_us(QEMU_CLOCK_VIRTUAL) + 100);
                }
                break;
            case 0x03: /* SDSTS: Write to clear status bits */
                s->sd_sts &= ~(val & 0x3F);
                memcpy(mem + addr, &s->sd_sts, 4);
                break;
            case 0x08: /* SDCBL */
                s->sd_cbl = val;
                memcpy(mem + addr, &val, 4);
                break;
            case 0x0C: /* SDLVI */
                s->sd_lvi = val;
                memcpy(mem + addr, &val, 4);
                break;
            case 0x10: /* SDFIFOS */
                s->sd_fifosize = val;
                memcpy(mem + addr, &val, 4);
                break;
            case 0x12: /* SDFMT */
                s->sd_format = val;
                memcpy(mem + addr, &val, 4);
                break;
            case 0x18: /* SD_BDLPL */
                s->sd_bdlpl = val;
                memcpy(mem + addr, &val, 4);
                break;
            case 0x1C: /* SD_BDLPU */
                s->sd_bdlpu = val;
                memcpy(mem + addr, &val, 4);
                break;
            default:
                memcpy(mem + addr, &val, 4);
                break;
        }
    } else if (bar == HDA_DSP_BAR) {
        /* BAR4: DSP BAR with IPC and other registers */
        switch (addr) {
            case HDA_DSP_REG_HIPCI: /* 0x40 */
                memcpy(mem + addr, &val, 4);
                if (val & HDA_DSP_REG_HIPCI_BUSY) {
                    /* Simulate DSP reply: set HIPCIE_DONE, clear HIPCI BUSY */
                    uint32_t reply_hipci = 0;
                    uint32_t reply_hipcie = HDA_DSP_REG_HIPCIE_DONE;
                    memcpy(mem + HDA_DSP_REG_HIPCI, &reply_hipci, 4);
                    memcpy(mem + HDA_DSP_REG_HIPCIE, &reply_hipcie, 4);
                    /* Set IPC interrupt status bit */
                    uint32_t adspis;
                    memcpy(&adspis, mem + HDA_DSP_REG_ADSPIS, 4);
                    adspis |= HDA_DSP_ADSPIS_IPC;
                    memcpy(mem + HDA_DSP_REG_ADSPIS, &adspis, 4);
                    s->intr_status |= HDA_DSP_ADSPIS_IPC;
                    pcibase_update_irq(s);
                    s->hipci_ext = 0;
                }
                break;
            case HDA_DSP_REG_HIPCIE: /* 0x44 */
                s->hipci_ext = val;
                memcpy(mem + addr, &val, 4);
                break;
            case HDA_DSP_REG_HIPCT: /* 0x48 */
            case HDA_DSP_REG_HIPCTE: /* 0x4C */
                memcpy(mem + addr, &val, 4);
                break;
            case HDA_DSP_REG_HIPCCTL: /* 0x50 */
                memcpy(mem + addr, &val, 4);
                break;
            case HDA_DSP_REG_ADSPIC: /* 0x08 */
                s->intr_mask = val;
                pcibase_update_irq(s);
                memcpy(mem + addr, &val, 4);
                break;
            case HDA_DSP_REG_ADSPIS: /* 0x0C */
                s->intr_status &= ~val;
                pcibase_update_irq(s);
                memcpy(mem + addr, &val, 4);
                break;
            default:
                memcpy(mem + addr, &val, 4);
                break;
        }
    } else {
        /* Other BARs: simple memory */
        memcpy(mem + addr, &val, 4);
    }
}

static uint64_t pcibase_read_reg(PCIBaseState *s, int bar, hwaddr addr, unsigned size)
{
    uint8_t *mem = s->bar_mem[bar];
    if (!mem) return 0;
    uint64_t val = 0;
    if (bar == HDA_DSP_HDA_BAR) {
        switch (addr) {
            case 0x03: /* SDSTS */
                return s->sd_sts;
            case 0x04: /* SDCTL */
                return s->sd_ctl;
            case 0x08: /* SDCBL */
                return s->sd_cbl;
            case 0x0C: /* SDLVI */
                return s->sd_lvi;
            case 0x10: /* SDFIFOS */
                return s->sd_fifosize;
            case 0x12: /* SDFMT */
                return s->sd_format;
            case 0x18: /* SD_BDLPL */
                return s->sd_bdlpl;
            case 0x1C: /* SD_BDLPU */
                return s->sd_bdlpu;
            default:
                memcpy(&val, mem + addr, size);
                return val;
        }
    } else {
        memcpy(&val, mem + addr, size);
        return val;
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    BAROpaque *bo = opaque;
    PCIBaseState *s = bo->s;
    int bar = bo->bar_index;
    return pcibase_read_reg(s, bar, addr, size);
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    BAROpaque *bo = opaque;
    PCIBaseState *s = bo->s;
    int bar = bo->bar_index;
    if (size != 4) return; /* driver uses readl/writel mostly, 32-bit access */
    pcibase_write_reg(s, bar, addr, (uint32_t)val);
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }
    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];
    if (bi->type == BAR_TYPE_MMIO) {
        BAROpaque *opaque = g_new0(BAROpaque, 1);
        opaque->s = s;
        opaque->bar_index = bi->index;
        s->bar_opaques[bi->index] = opaque;
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, opaque, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        /* PIO not used */
        error_setg(errp, "PIO BAR not supported");
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

static void dma_transfer_func(void *opaque)
{
    PCIBaseState *s = opaque;
    PCIDevice *pdev = PCI_DEVICE(s);
    uint8_t *mem4 = s->bar_mem[HDA_DSP_BAR];

    if (s->dma_timer_stage == DMA_STAGE_DMA) {
        if (!s->sd_running) return;
        uint64_t bdl_base = (uint64_t)s->sd_bdlpu << 32 | s->sd_bdlpl;
        if (bdl_base == 0) {
            s->sd_running = false;
            s->sd_sts |= SOF_HDA_CL_DMA_SD_INT_COMPLETE;
            return;
        }
        uint32_t num_entries = s->sd_lvi + 1;
        uint8_t *sram = mem4 + SRAM_WINDOW_OFFSET(0);
        for (uint32_t i = 0; i < num_entries; i++) {
            struct sof_intel_dsp_bdl bdl;
            pci_dma_read(pdev, bdl_base + i * sizeof(bdl), &bdl, sizeof(bdl));
            dma_addr_t src_addr = ((uint64_t)bdl.addr_h << 32) | bdl.addr_l;
            uint32_t size = bdl.size;
            if (size == 0) break;
            pci_dma_read(pdev, src_addr, sram, size);
            sram += size;
        }
        s->sd_sts |= SOF_HDA_CL_DMA_SD_INT_COMPLETE;
        s->sd_running = false;
        /* Update ROM_STATUS to FW_ENTERED */
        uint32_t fw_entered = FSR_STATE_FW_ENTERED;
        memcpy(mem4 + HDA_DSP_SRAM_REG_ROM_STATUS, &fw_entered, 4);
        /* Set CL DMA interrupt */
        uint32_t adspis;
        memcpy(&adspis, mem4 + HDA_DSP_REG_ADSPIS, 4);
        adspis |= HDA_DSP_ADSPIS_CL_DMA;
        memcpy(mem4 + HDA_DSP_REG_ADSPIS, &adspis, 4);
        s->intr_status |= HDA_DSP_ADSPIS_CL_DMA;
        pcibase_update_irq(s);
        /* Schedule FW_READY message */
        s->dma_timer_stage = DMA_STAGE_FW_READY;
        timer_mod(&s->dma_timer, qemu_clock_get_us(QEMU_CLOCK_VIRTUAL) + 1000);
    } else if (s->dma_timer_stage == DMA_STAGE_FW_READY) {
        /* Simulate DSP sending FW_READY IPC via HIPCT */
        uint32_t hipct = SOF_IPC_FW_READY | HDA_DSP_REG_HIPCT_BUSY;
        uint32_t hipcte = 0;
        memcpy(mem4 + HDA_DSP_REG_HIPCT, &hipct, 4);
        memcpy(mem4 + HDA_DSP_REG_HIPCTE, &hipcte, 4);
        uint32_t adspis;
        memcpy(&adspis, mem4 + HDA_DSP_REG_ADSPIS, 4);
        adspis |= HDA_DSP_ADSPIS_IPC;
        memcpy(mem4 + HDA_DSP_REG_ADSPIS, &adspis, 4);
        s->intr_status |= HDA_DSP_ADSPIS_IPC;
        pcibase_update_irq(s);
        s->dma_timer_stage = 0;
    }
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    /* Clear and initialize register state */
    for (int i = 0; i < SND_SOF_BARS; i++) {
        if (s->bar_mem[i]) {
            memset(s->bar_mem[i], 0, pow2ceil(s->bar_info[i].size));
        }
    }
    s->intr_status = 0;
    s->intr_mask = 0;
    /* Reset DMA engine state */
    s->sd_ctl = 0;
    s->sd_lvi = 0;
    s->sd_bdlpl = 0;
    s->sd_bdlpu = 0;
    s->sd_cbl = 0;
    s->sd_fifosize = 0;
    s->sd_format = 0;
    s->sd_sts = 0;
    s->sd_running = false;
    s->dma_timer_stage = 0;
    timer_del(&s->dma_timer);
    /* Set ROM status to indicate firmware initialisation done if present */
    if (s->bar_mem[HDA_DSP_BAR]) {
        uint32_t rom_status = FSR_STATE_INIT_DONE;
        memcpy(s->bar_mem[HDA_DSP_BAR] + HDA_DSP_SRAM_REG_ROM_STATUS, &rom_status, 4);
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_INTEL );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_INTEL_HDA_APL );
    pci_config_set_class(pci_conf, PCI_CLASS_AUDIO);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    memset(s->bar_info, 0, sizeof(s->bar_info));
    memset(s->bar_mem, 0, sizeof(s->bar_mem));
    memset(s->bar_opaques, 0, sizeof(s->bar_opaques));

    /* Define BARs used by the driver */
    s->bar_info[HDA_DSP_HDA_BAR] = (BARInfo){
        .index = HDA_DSP_HDA_BAR,
        .type = BAR_TYPE_MMIO,
        .size = 0x4000,
        .name = "hda-bar"
    };
    s->bar_info[HDA_DSP_PP_BAR] = (BARInfo){
        .index = HDA_DSP_PP_BAR,
        .type = BAR_TYPE_MMIO,
        .size = 0x1000,
        .name = "pp-bar"
    };
    s->bar_info[HDA_DSP_SPIB_BAR] = (BARInfo){
        .index = HDA_DSP_SPIB_BAR,
        .type = BAR_TYPE_MMIO,
        .size = 0x1000,
        .name = "spib-bar"
    };
    s->bar_info[HDA_DSP_BAR] = (BARInfo){
        .index = HDA_DSP_BAR,
        .type = BAR_TYPE_MMIO,
        .size = 0x100000,
        .name = "dsp-bar"
    };

    /* Allocate memory storage for each used BAR */
    for (int i = 0; i < SND_SOF_BARS; i++) {
        if (s->bar_info[i].type != BAR_TYPE_NONE) {
            hwaddr size = pow2ceil(s->bar_info[i].size);
            s->bar_mem[i] = g_malloc0(size);
        }
    }

    s->num_bars = SND_SOF_BARS;
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
        if (*errp) {
            return;
        }
    }

    /* Initialize DMA timer */
    timer_init_us(&s->dma_timer, QEMU_CLOCK_VIRTUAL, dma_transfer_func, s);

    /* State initialization: all zeroes by default */
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
    /* Free allocated memory and opaques */
    for (int i = 0; i < SND_SOF_BARS; i++) {
        g_free(s->bar_mem[i]);
        g_free(s->bar_opaques[i]);
    }
    timer_del(&s->dma_timer);
}

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