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

#define TYPE_PCIBASE_DEVICE "sof_audio_pci_intel_cnl_pci"

typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* ------------------------------------------------------------------ */
/* Device-specific placeholders / macros                              */
/* ------------------------------------------------------------------ */

/* Some helper bit macros normally from linux/bitops.h */
#ifndef BIT
#define BIT(nr) (1U << (nr))
#endif

#ifndef GENMASK
#define GENMASK(h, l) (((~0U) - (1U << (l)) + 1) & (~0U >> (31 - (h))))
#endif

/* PCI configuration and BAR info extracted from driver */
#define SND_SOF_BARS 8

/* HDA DSP BAR indices used by SOF driver */
#define HDA_DSP_HDA_BAR 0
#define HDA_DSP_PP_BAR 1
#define HDA_DSP_SPIB_BAR 2
#define HDA_DSP_BAR 4

/* Generic HDA DSP register base within BAR */
#define HDA_DSP_GEN_BASE 0x0

/* IPC base inside BAR */
#define CNL_DSP_IPC_BASE 0xc0

/* SRAM / mailbox window layout */
#define SRAM_WINDOW_OFFSET(x) (0x80000 + (x) * 0x20000)
#define HDA_DSP_MBOX_OFFSET SRAM_WINDOW_OFFSET(0)

/* Selected HDA/DSP registers and bitfields */
#define HDA_DSP_REG_ADSPCS (HDA_DSP_GEN_BASE + 0x04)
#define HDA_DSP_REG_ADSPIS (HDA_DSP_GEN_BASE + 0x0C)
#define HDA_DSP_REG_ADSPIC2 (HDA_DSP_GEN_BASE + 0x10)
#define HDA_DSP_REG_ADSPIS2 (HDA_DSP_GEN_BASE + 0x14)

#define HDA_DSP_ADSPIC_IPC BIT(0)
#define HDA_DSP_ADSPIS_IPC BIT(0)
#define HDA_DSP_ADSPIS_CL_DMA BIT(1)
#define HDA_DSP_REG_ADSPIC2_SNDW BIT(5)
#define HDA_DSP_REG_ADSPIS2_SNDW BIT(5)

#define HDA_DSP_ADSPCS_CRST_SHIFT 0
#define HDA_DSP_ADSPCS_CSTALL_SHIFT 8
#define HDA_DSP_ADSPCS_SPA_SHIFT 16
#define HDA_DSP_ADSPCS_CPA_SHIFT 24

#define HDA_DSP_ADSPCS_CRST_MASK(cm) ((cm) << HDA_DSP_ADSPCS_CRST_SHIFT)
#define HDA_DSP_ADSPCS_CSTALL_MASK(cm) ((cm) << HDA_DSP_ADSPCS_CSTALL_SHIFT)
#define HDA_DSP_ADSPCS_SPA_MASK(cm) ((cm) << HDA_DSP_ADSPCS_SPA_SHIFT)
#define HDA_DSP_ADSPCS_CPA_MASK(cm) ((cm) << HDA_DSP_ADSPCS_CPA_SHIFT)

/* IPC registers and bitfields */
#define CNL_DSP_REG_HIPCTDR (CNL_DSP_IPC_BASE + 0x00)
#define CNL_DSP_REG_HIPCTDA (CNL_DSP_IPC_BASE + 0x04)
#define CNL_DSP_REG_HIPCTDD (CNL_DSP_IPC_BASE + 0x08)
#define CNL_DSP_REG_HIPCIDR (CNL_DSP_IPC_BASE + 0x10)
#define CNL_DSP_REG_HIPCIDA (CNL_DSP_IPC_BASE + 0x14)
#define CNL_DSP_REG_HIPCIDD (CNL_DSP_IPC_BASE + 0x18)
#define CNL_DSP_REG_HIPCCTL (CNL_DSP_IPC_BASE + 0x28)

#define CNL_DSP_REG_HIPCTDR_BUSY BIT(31)
#define CNL_DSP_REG_HIPCIDR_BUSY BIT(31)
#define CNL_DSP_REG_HIPCTDR_MSG_MASK 0x7FFFFFFF
#define CNL_DSP_REG_HIPCTDD_MSG_MASK 0x7FFFFFFF
#define CNL_DSP_REG_HIPCIDA_MSG_MASK 0x7FFFFFFF
#define CNL_DSP_REG_HIPCIDR_MSG_MASK 0x7FFFFFFF

#define CNL_DSP_REG_HIPCTDA_DONE BIT(31)
#define CNL_DSP_REG_HIPCIDA_DONE BIT(31)
#define CNL_DSP_REG_HIPCCTL_BUSY BIT(0)
#define CNL_DSP_REG_HIPCCTL_DONE BIT(1)

/* ROM / FW status */
#define HDA_DSP_SRAM_REG_ROM_STATUS (HDA_DSP_MBOX_OFFSET + 0x0)
#define HDA_DSP_ROM_IPC_CONTROL 0x01000000
#define HDA_DSP_ROM_IPC_PURGE_FW 0x00004000

#define FSR_STATE_MASK GENMASK(23, 0)
#define FSR_TO_STATE_CODE(x) ((x) & FSR_STATE_MASK)
#define FSR_STATE_INIT_DONE 0x1
#define FSR_STATE_FW_ENTERED 0x5

/* Timeouts and polling (not actively used in the model) */
#define HDA_DSP_INIT_TIMEOUT_US 500000
#define HDA_DSP_BASEFW_TIMEOUT_US 3000000
#define HDA_DSP_RESET_TIMEOUT_US 50000
#define HDA_DSP_STREAM_RUN_TIMEOUT 300
#define HDA_DSP_STREAM_RESET_TIMEOUT 300
#define HDA_DSP_PD_TIMEOUT 50
#define HDA_DSP_REG_POLL_INTERVAL_US 500
#define HDA_DSP_REG_POLL_RETRY_COUNT 50
#define HDA_FW_BOOT_ATTEMPTS 3

/* Panic offset helper */
#define HDA_DSP_PANIC_OFFSET(x) (((x) & 0xFFFFFF) + HDA_DSP_MBOX_OFFSET)

/* Stream / SD registers and bitfields */
#define SOF_HDA_ADSP_LOADER_BASE 0x80
#define SOF_HDA_ADSP_SD_ENTRY_SIZE 0x20
#define SOF_STREAM_SD_OFFSET_CRST 0x1

#define SOF_HDA_ADSP_REG_SD_STS 0x03
#define SOF_HDA_ADSP_REG_SD_LVI 0x0C
#define SOF_HDA_ADSP_REG_SD_CBL 0x08
#define SOF_HDA_ADSP_REG_SD_FORMAT 0x12
#define SOF_HDA_ADSP_REG_SD_BDLPL 0x18
#define SOF_HDA_ADSP_REG_SD_BDLPU 0x1C

#define SOF_HDA_SD_FIFOSIZE_FIFOS_MASK GENMASK(15, 0)
#define SOF_HDA_ADSP_REG_SD_FIFOSIZE 0x10
#define HDA_CL_STREAM_FORMAT 0x40

#define SOF_HDA_CL_SD_CTL_STREAM_TAG_SHIFT 20
#define SOF_HDA_CL_SD_CTL_STREAM_TAG_MASK \
    GENMASK(SOF_HDA_CL_SD_CTL_STREAM_TAG_SHIFT + 3, \
            SOF_HDA_CL_SD_CTL_STREAM_TAG_SHIFT)

#define SOF_HDA_SD_CTL_DMA_START 0x02
#define SOF_HDA_CL_DMA_SD_INT_DESC_ERR 0x10
#define SOF_HDA_CL_DMA_SD_INT_FIFO_ERR 0x08
#define SOF_HDA_CL_DMA_SD_INT_COMPLETE 0x04
#define SOF_HDA_CL_DMA_SD_INT_MASK \
    (SOF_HDA_CL_DMA_SD_INT_DESC_ERR | \
     SOF_HDA_CL_DMA_SD_INT_FIFO_ERR | \
     SOF_HDA_CL_DMA_SD_INT_COMPLETE)

/* SPIB and position buffers */
#define HDA_DSP_SPIB_ENABLE 1
#define HDA_DSP_SPIB_DISABLE 0
#define SOF_HDA_ADSP_DPLBASE 0x70
#define SOF_HDA_ADSP_DPUBASE 0x74
#define SOF_HDA_ADSP_DPLBASE_ENABLE 0x01
#define SOF_HDA_ADSP_REG_CL_SPBFIFO_SPBFCCTL 0x4

/* PCI power / clock gating registers */
#define PCI_TCSEL 0x44
#define PCI_PGCTL PCI_TCSEL
#define PCI_PGCTL_ADSPPGD BIT(2)
#define PCI_CGCTL 0x48
#define PCI_CGCTL_ADSPDCGE BIT(1)

/* Vendor-specific HDA registers */
#define HDA_VS_INTEL_EM2 0x1030
#define HDA_VS_INTEL_EM2_L1SEN BIT(13)
#define HDA_VS_INTEL_LTRP 0x1048
#define HDA_VS_INTEL_LTRP_GB_MASK 0x3F
#define SOF_HDA_VS_D0I3C 0x104A
#define SOF_HDA_VS_D0I3C_CIP BIT(0)
#define SOF_HDA_VS_D0I3C_I3 BIT(2)

/* HDA interrupt control/status */
#define SOF_HDA_INTCTL 0x20
#define SOF_HDA_INTSTS 0x24

#define SOF_HDA_PPCTL_GPROCEN BIT(30)
#define SOF_HDA_REG_PP_PPCTL 0x04
#define SOF_HDA_REG_PP_PPSTS 0x08

/* SSP configuration */
#define CNL_SSP_BASE_OFFSET 0x10000
#define CNL_SSP_COUNT 3
#define SSP_DEV_MEM_SIZE 0x1000
#define SSP_SSC1_OFFSET 0x4
#define SSP_SET_SFRM_CONSUMER BIT(24)
#define SSP_SET_SCLK_CONSUMER BIT(25)
#define SSP_SET_CBP_CFP (SSP_SET_SCLK_CONSUMER | SSP_SET_SFRM_CONSUMER)

/* Misc SOF / HDA constants */
#define SOF_MAX_DSP_NUM_CORES 8
#define SOF_DSP_PRIMARY_CORE 0

#define SOF_HDA_D0I3_WORK_DELAY_MS 5000
#define SND_SOF_SUSPEND_DELAY_MS 2000

#define DMA_CHAN_INVALID 0xFFFFFFFF
#define DMA_BUF_SIZE_FOR_TRACE (4096 * 16)
#define HDA_DSP_BDL_SIZE 4096

#define SOF_HDA_STREAM_DMI_L1_COMPATIBLE 1

/* Debug flags (unused in model) */
#define SOF_DBG_DUMP_REGS BIT(0)
#define SOF_DBG_DUMP_MBOX BIT(1)
#define SOF_DBG_DUMP_TEXT BIT(2)
#define SOF_DBG_DUMP_PCI BIT(3)
#define SOF_DBG_DUMP_OPTIONAL BIT(4)
#define SOF_DBG_PRINT_ALL_DUMPS BIT(6)
#define SOF_DBG_IGNORE_D3_PERSISTENT BIT(7)
#define SOF_DBG_PRINT_DMA_POSITION_UPDATE_LOGS BIT(8)
#define SOF_DBG_PRINT_IPC_SUCCESS_LOGS BIT(9)
#define SOF_DBG_FORCE_NOCODEC BIT(10)
#define SOF_DBG_DUMP_IPC_MESSAGE_PAYLOAD BIT(11)
#define SOF_DBG_ENABLE_TRACE BIT(0)
#define SOF_DBG_RETAIN_CTX BIT(1)
#define SOF_DBG_DSPLESS_MODE BIT(15)

/* PCI IDs */
#define PCI_VENDOR_ID_INTEL 0x8086

/* ------------------------------------------------------------------ */
/* BAR metadata and device state definition                           */
/* ------------------------------------------------------------------ */

typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
} BARType;

typedef struct {
    int index;
    BARType type;
    hwaddr size;
    const char *name;
    bool sparse;
} BARInfo;

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Unified BAR memory regions for MMIO/PIO */
    MemoryRegion bar_regions[6];

    /* Optional linear backing for simple register arrays */
    uint8_t *mmio_backing;
    size_t mmio_backing_size;

    /* BAR table */
    BARInfo bar_info[6];
    int num_bars;

    /* Feature flags */
    bool has_msi;
    bool has_msix;

    /* Register shadows needed for minimal behavior */
    uint32_t adspcs;
    uint32_t adspic;
    uint32_t adspis;
    uint32_t adspic2;
    uint32_t adspis2;

    uint32_t hipctdr;
    uint32_t hipctda;
    uint32_t hipctdd;
    uint32_t hipcidr;
    uint32_t hipcida;
    uint32_t hipcidd;
    uint32_t hipcctl;

    uint32_t intctl;
    uint32_t intsts;

    uint32_t pp_ppctl;
    uint32_t pp_ppsts;

    /* ROM/mailbox status */
    uint32_t rom_status;

    /* Interrupt state */
    uint32_t pending_irqs;
};

/* ------------------------------------------------------------------ */
/* Forward declarations                                               */
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
/* Helper: IRQ handling                                               */
/* ------------------------------------------------------------------ */

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool level = s->pending_irqs != 0;

    if (msi_enabled(pdev)) {
        if (level) {
            msi_notify(pdev, 0);
        }
    } else {
        pci_set_irq(pdev, level);
    }
}

static void pcibase_raise_irq(PCIBaseState *s, uint32_t mask)
{
    s->pending_irqs |= mask;
    pcibase_update_irq(s);
}

static void pcibase_lower_irq(PCIBaseState *s, uint32_t mask)
{
    s->pending_irqs &= ~mask;
    pcibase_update_irq(s);
}

/* ------------------------------------------------------------------ */
/* MemoryRegionOps                                                   */
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
    .impl  = { .min_access_size = 1, .max_access_size = 1 },
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
    }
    (void)errp;
}

/* ------------------------------------------------------------------ */
/* MMIO/PIO handlers                                                  */
/* ------------------------------------------------------------------ */

static uint32_t pcibase_mmio_readl(PCIBaseState *s, hwaddr addr)
{
    /* Map only the registers we model; others read as 0 */
    switch (addr) {
    case SOF_HDA_REG_PP_PPCTL:
        return s->pp_ppctl;
    case SOF_HDA_REG_PP_PPSTS:
        return s->pp_ppsts;
    case HDA_DSP_REG_ADSPIS:
        return s->adspis;
    case HDA_DSP_REG_ADSPIC2:
        return s->adspic2;
    case HDA_DSP_REG_ADSPIS2:
        return s->adspis2;

    case CNL_DSP_REG_HIPCTDR:
        return s->hipctdr;
    case CNL_DSP_REG_HIPCTDA:
        return s->hipctda;
    case CNL_DSP_REG_HIPCTDD:
        return s->hipctdd;
    case CNL_DSP_REG_HIPCIDR:
        return s->hipcidr;
    case CNL_DSP_REG_HIPCIDA:
        return s->hipcida;
    case CNL_DSP_REG_HIPCIDD:
        return s->hipcidd;
    case CNL_DSP_REG_HIPCCTL:
        return s->hipcctl;

    case HDA_DSP_SRAM_REG_ROM_STATUS:
        return s->rom_status;

    case SOF_HDA_INTCTL:
        return s->intctl;
    case SOF_HDA_INTSTS:
        return s->intsts;

    default:
        return 0;
    }
}

static void pcibase_mmio_writel(PCIBaseState *s, hwaddr addr, uint32_t val)
{
    switch (addr) {
    case SOF_HDA_REG_PP_PPCTL:
        s->pp_ppctl = val;
        break;
    case SOF_HDA_REG_PP_PPSTS:
        s->pp_ppsts = val;
        break;
    case HDA_DSP_REG_ADSPIS:
        /* Writing status typically clears bits; model as clear-on-write mask */
        s->adspis &= ~val;
        if (!(s->adspis & (HDA_DSP_ADSPIS_IPC | HDA_DSP_ADSPIS_CL_DMA))) {
            pcibase_lower_irq(s, HDA_DSP_ADSPIS_IPC | HDA_DSP_ADSPIS_CL_DMA);
        }
        break;
    case HDA_DSP_REG_ADSPIC2:
        s->adspic2 = val;
        break;
    case HDA_DSP_REG_ADSPIS2:
        s->adspis2 &= ~val;
        if (!(s->adspis2 & HDA_DSP_REG_ADSPIS2_SNDW)) {
            pcibase_lower_irq(s, HDA_DSP_REG_ADSPIS2_SNDW);
        }
        break;

    case CNL_DSP_REG_HIPCTDR:
        /* Host to DSP doorbell/data register. Driver sets BUSY when sending. */
        s->hipctdr = val;
        /* Indicate busy and raise IPC interrupt towards DSP (not modeled further). */
        s->hipctdr |= CNL_DSP_REG_HIPCTDR_BUSY;
        break;
    case CNL_DSP_REG_HIPCTDA:
        s->hipctda = val;
        break;
    case CNL_DSP_REG_HIPCTDD:
        s->hipctdd = val;
        break;

    case CNL_DSP_REG_HIPCIDR:
        /* DSP to host doorbell register; in hardware the DSP writes this.
         * The host driver will read BUSY and handle IPC RX.
         * For modeling host view, writes from host simply update shadow.
         */
        s->hipcidr = val;
        break;
    case CNL_DSP_REG_HIPCIDA:
        s->hipcida = val;
        break;
    case CNL_DSP_REG_HIPCIDD:
        s->hipcidd = val;
        break;
    case CNL_DSP_REG_HIPCCTL:
        /* Enable/disable IPC BUSY/DONE interrupt sources. */
        s->hipcctl = val;
        break;

    case HDA_DSP_SRAM_REG_ROM_STATUS:
        /* ROM status is read-only from host; ignore writes. */
        break;

    case SOF_HDA_INTCTL:
        s->intctl = val;
        break;
    case SOF_HDA_INTSTS:
        /* Clear-on-write-1 semantics for interrupt status */
        s->intsts &= ~val;
        if (!s->intsts) {
            pcibase_lower_irq(s, 0xFFFFFFFF);
        }
        break;

    default:
        break;
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* We only model 32-bit accesses for our registers */
    if (size == 4) {
        val = pcibase_mmio_readl(s, addr);
    } else if (size == 1 || size == 2 || size == 8) {
        /* For simplicity, perform 32-bit read and mask */
        uint32_t tmp = pcibase_mmio_readl(s, addr & ~0x3ULL);
        unsigned shift = (addr & 0x3ULL) * 8;
        if (size == 1) {
            val = (tmp >> shift) & 0xFF;
        } else if (size == 2) {
            val = (tmp >> shift) & 0xFFFF;
        } else {
            /* 8-byte: read two consecutive dwords */
            uint32_t tmp2 = pcibase_mmio_readl(s, (addr & ~0x3ULL) + 4);
            val = ((uint64_t)tmp2 << 32) | tmp;
        }
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size == 4) {
        pcibase_mmio_writel(s, addr, (uint32_t)val);
    } else if (size == 1 || size == 2 || size == 8) {
        /* Implement read-modify-write on 32-bit granularity */
        hwaddr base = addr & ~0x3ULL;
        uint32_t cur = pcibase_mmio_readl(s, base);
        uint32_t newv = cur;
        unsigned shift = (addr & 0x3ULL) * 8;
        uint32_t mask = (size == 1) ? 0xFF : (size == 2 ? 0xFFFF : 0xFFFFFFFF);
        if (size == 8) {
            /* write low dword then high dword */
            pcibase_mmio_writel(s, base, (uint32_t)val);
            pcibase_mmio_writel(s, base + 4, (uint32_t)(val >> 32));
            return;
        } else {
            newv &= ~(mask << shift);
            newv |= ((uint32_t)val & mask) << shift;
            pcibase_mmio_writel(s, base, newv);
        }
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* No port I/O registers are modeled for this device. */
    (void)opaque;
    (void)addr;
    (void)size;
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* No port I/O behavior modeled. */
    (void)opaque;
    (void)addr;
    (void)val;
    (void)size;
}

/* ------------------------------------------------------------------ */
/* Reset (clear state, IRQs, DMA)                                     */
/* ------------------------------------------------------------------ */

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    /* core QEMU resets */
    pci_device_reset(pdev);
    msi_reset(pdev);
    msix_reset(pdev);

    if (s->mmio_backing && s->mmio_backing_size) {
        memset(s->mmio_backing, 0, s->mmio_backing_size);
    }

    /* Initialize register shadows to a state that lets the driver probe. */
    s->adspcs = 0;
    s->adspic = 0;
    s->adspis = 0;
    s->adspic2 = 0;
    s->adspis2 = 0;

    s->hipctdr = 0;
    s->hipctda = 0;
    s->hipctdd = 0;
    s->hipcidr = 0;
    s->hipcida = 0;
    s->hipcidd = 0;
    s->hipcctl = 0;

    s->intctl = 0;
    s->intsts = 0;

    s->pp_ppctl = 0;
    s->pp_ppsts = 0;

    /* ROM status: report INIT_DONE so the driver sees a ready ROM. */
    s->rom_status = FSR_STATE_INIT_DONE;

    s->pending_irqs = 0;
    pcibase_update_irq(s);
}

/* ------------------------------------------------------------------ */
/* DMA initialization (called from realize)                           */
/* ------------------------------------------------------------------ */

static void pcibase_dma_device_realize(PCIDevice *pdev, Error **errp)
{
    /* The provided driver snippets do not expose explicit DMA descriptor
     * formats or engine programming for this PCI device; therefore we do not
     * model streaming DMA here.
     */
    (void)pdev;
    (void)errp;
}

/* ------------------------------------------------------------------ */
/* PCI config space access                                            */
/* ------------------------------------------------------------------ */

static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    uint32_t val = pci_default_read_config(pdev, addr, len);
    return val;
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    pci_default_write_config(pdev, addr, val, len);
}

/* ------------------------------------------------------------------ */
/* Core realize / initialize                                          */
/* ------------------------------------------------------------------ */

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;
    Error *local_err = NULL;

    /* Set Intel vendor and a generic device id for Cannonlake HDA controller. */
    pci_config_set_vendor_id(pci_conf, PCI_VENDOR_ID_INTEL);
    /* Use the Cannonlake HDA controller device id. */
    pci_config_set_device_id(pci_conf, 0x9dc8);
    pci_config_set_revision(pci_conf, 0x01);
    pci_config_set_class(pci_conf, PCI_CLASS_MULTIMEDIA_AUDIO);

    /* Interrupt pin A */
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Configure BAR layout: minimal scheme compatible with HDA/SOF: 
     * BAR0: HDA / DSP core and IPC registers (1 MB)
     * BAR1: PP capabilities (64 KB)
     * BAR2: SPIB / DMA position buffers (64 KB)
     */
    s->num_bars = 3;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x100000;
    s->bar_info[0].name = "sof-cnl-hda";
    s->bar_info[0].sparse = false;

    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = 0x10000;
    s->bar_info[1].name = "sof-cnl-pp";
    s->bar_info[1].sparse = false;

    s->bar_info[2].index = 2;
    s->bar_info[2].type = BAR_TYPE_MMIO;
    s->bar_info[2].size = 0x10000;
    s->bar_info[2].name = "sof-cnl-spib";
    s->bar_info[2].sparse = false;

    /* Allocate optional backing if needed later (not strictly used now). */
    s->mmio_backing_size = s->bar_info[0].size;
    s->mmio_backing = g_malloc0(s->mmio_backing_size);

    /* Register BARs */
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            return;
        }
    }

    /* DMA init (optional) */
    pcibase_dma_device_realize(pdev, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    /* Interrupt configuration: enable MSI with single vector, fallback to INTx. */
    if (msi_init(pdev, 0, 1, true, false, &local_err) == 0) {
        s->has_msi = true;
    } else {
        s->has_msi = false;
        if (local_err) {
            error_free(local_err);
            local_err = NULL;
        }
    }

    /* No MSI-X configured for this device */
    s->has_msix = false;

    /* Reset internal state to known defaults */
    pcibase_reset(DEVICE(pdev));

}

/* ------------------------------------------------------------------ */
/* Uninit / cleanup                                                   */
/* ------------------------------------------------------------------ */

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (s->has_msix) {
        msix_uninit(pdev, NULL, 0);
    }
    if (s->has_msi) {
        msi_uninit(pdev);
    }

    if (s->mmio_backing) {
        g_free(s->mmio_backing);
        s->mmio_backing = NULL;
        s->mmio_backing_size = 0;
    }
}

/* ------------------------------------------------------------------ */
/* Class init / type registration                                     */
/* ------------------------------------------------------------------ */

static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    /* Hook config read/write if device needs special handling */
    k->config_read  = pcibase_config_read;
    k->config_write = pcibase_config_write;

    k->realize = pcibase_realize;
    k->exit    = pcibase_uninit;
    dc->reset  = pcibase_reset;

    /* Identification */
    k->vendor_id = PCI_VENDOR_ID_INTEL;
    k->device_id = 0x9dc8;
    k->revision = 0x01;
    k->class_id = PCI_CLASS_MULTIMEDIA_AUDIO;

    set_bit(DEVICE_CATEGORY_SOUND, dc->categories);
    (void)data;
}

static void pcibase_register_types(void)
{
    static InterfaceInfo interfaces[] = {
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
