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
#include "qemu/bitops.h"
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

#define TYPE_PCIBASE_DEVICE "spi_pci1xxxx_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#ifndef GENMASK
#define GENMASK(h, l) (((1ULL << ((h) - (l) + 1)) - 1) << (l))
#endif

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID_MCHP 0x1055
#define DEVICE_ID_MCHP_SPI 0xa004
#define SPI_SYSTEM_ADDR_BASE 0x2000
#define SPI_MST1_ADDR_BASE 0x800
#define SPI_DMA_ADDR_BASE 0x1000
#define SPI_PERI_ADDR_BASE 0x160000

#define NUM_VEC_PER_INST 3
#define SPI_MST_EVENT_MASK_REG_OFFSET(x)    (((x) * SPI_MST1_ADDR_BASE) + 0x424)
#define SPI_INTR        BIT(8)
#define SPI_PCI_CTRL_REG_OFFSET(x)      (((x) * SPI_MST1_ADDR_BASE) + 0x480)
#define SPI_DMA_GLOBAL_WR_ENGINE_EN (SPI_DMA_ADDR_BASE + 0x0C)
#define SPI_DMA_GLOBAL_RD_ENGINE_EN (SPI_DMA_ADDR_BASE + 0x2C)
#define SPI_DMA_ENGINE_EN           (0x1)
#define SPI_MST_EVENT_REG_OFFSET(x)     (((x) * SPI_MST1_ADDR_BASE) + 0x420)
#define SPI_DMA_WR_DOORBELL_REG     (SPI_DMA_ADDR_BASE + 0x10)

#define SPI_SYSLOCK_REG			(SPI_SYSTEM_ADDR_BASE + 0xA0)
#define SPI_SYSLOCK			BIT(4)
#define DEV_REV_REG			(SPI_SYSTEM_ADDR_BASE + 0x00)
#define DEV_REV_MASK			(GENMASK(7, 0))
#define SPI_CONFIG_PERI_ENABLE_REG	(SPI_SYSTEM_ADDR_BASE + 0x108)
#define SPI_PERI_ENBLE_PF_MASK		(GENMASK(17, 16))
#define SPI_DMA_INTR_IMWR_WDONE_HIGH	(SPI_DMA_ADDR_BASE + 0x64)
#define SPI_DMA_INTR_IMWR_WABORT_HIGH	(SPI_DMA_ADDR_BASE + 0x6C)
#define SPI_DMA_INTR_IMWR_RDONE_HIGH	(SPI_DMA_ADDR_BASE + 0xD0)
#define SPI_DMA_INTR_IMWR_RABORT_HIGH	(SPI_DMA_ADDR_BASE + 0xD8)
#define SPI_DMA_INTR_IMWR_WDONE_LOW	(SPI_DMA_ADDR_BASE + 0x60)
#define SPI_DMA_INTR_IMWR_WABORT_LOW	(SPI_DMA_ADDR_BASE + 0x68)
#define SPI_DMA_INTR_IMWR_RDONE_LOW	(SPI_DMA_ADDR_BASE + 0xCC)
#define SPI_DMA_INTR_IMWR_RABORT_LOW	(SPI_DMA_ADDR_BASE + 0xD4)
#define SPI_DMA_INTR_WR_IMWR_DATA	(SPI_DMA_ADDR_BASE + 0x70)
#define SPI_DMA_INTR_RD_IMWR_DATA	(SPI_DMA_ADDR_BASE + 0xDC)
#define SPI_MST_CTL_REG_OFFSET(x)		(((x) * SPI_MST1_ADDR_BASE) + 0x400)
#define SPI_FORCE_CE		BIT(4)
#define SPI_MST_CTL_DEVSEL_MASK		(GENMASK(27, 25))
#define SPI_DMA_CH0_RD_BASE		(SPI_DMA_ADDR_BASE + 0x300)
#define SPI_DMA_CH1_RD_BASE		(SPI_DMA_ADDR_BASE + 0x500)
#define SPI_DMA_CH_CTL1_OFFSET		(0x00)
#define DMA_CH_CONTROL_RIE		BIT(4)
#define DMA_CH_CONTROL_LIE		BIT(3)
#define SPI_DMA_CH_XFER_LEN_OFFSET	(0x08)
#define SPI_DMA_CH_SAR_LO_OFFSET	(0x0C)
#define SPI_DMA_CH_SAR_HI_OFFSET	(0x10)
#define SPI_DMA_CH_DAR_LO_OFFSET	(0x14)
#define SPI_DMA_CH_DAR_HI_OFFSET	(0x18)
#define SPI_MST_CMD_BUF_OFFSET(x)		(((x) * SPI_MST1_ADDR_BASE) + 0x00)
#define SPI_DMA_CH0_WR_BASE		(SPI_DMA_ADDR_BASE + 0x200)
#define SPI_DMA_CH1_WR_BASE		(SPI_DMA_ADDR_BASE + 0x400)
#define SPI_MST_RSP_BUF_OFFSET(x)		(((x) * SPI_MST1_ADDR_BASE) + 0x200)
#define SPI_MST_CTL_MODE_SEL		(BIT(2))
#define SPI_MST_CTL_CMD_LEN_MASK	(GENMASK(16, 8))
#define SPI_MST_CTL_SPEED_MASK		(GENMASK(7, 5))
#define SPI_MST_CTL_GO			(BIT(0))
#define SPI_DMA_RD_DOORBELL_REG		(SPI_DMA_ADDR_BASE + 0x30)
#define SPI_DMA_ENGINE_DIS			(0x0)
#define SPI_DMA_INTR_RD_STS		(SPI_DMA_ADDR_BASE + 0xA0)
#define SPI_DMA_DONE_INT_MASK(x)	(1 << (x))
#define SPI_DMA_ABORT_INT_MASK(x)	(1 << (16 + (x)))
#define SPI_DMA_INTR_RD_CLR		(SPI_DMA_ADDR_BASE + 0xAC)
#define SPI_DMA_INTR_WR_STS		(SPI_DMA_ADDR_BASE + 0x4C)
#define SPI_DMA_INTR_WR_CLR		(SPI_DMA_ADDR_BASE + 0x58)
#define SPI_MSI_VECTOR_SEL_MASK		(GENMASK(4, 4))

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
    uint32_t dev_rev;
    uint32_t syslock;
    uint32_t config_peri_enable;

    /* DMA Context */
    uint32_t dma_global_wr_engine_en;
    uint32_t dma_global_rd_engine_en;
    uint32_t dma_intr_wr_sts;
    uint32_t dma_wr_int_mask;
    uint32_t dma_intr_rd_sts;
    uint32_t dma_rd_int_mask;

    /* New Registers */
    uint32_t mst_ctl[2];
    uint32_t dma_ch_rd_ctl1[2];
    uint32_t dma_ch_rd_xfer_len[2];
    uint32_t dma_ch_rd_sar_lo[2];
    uint32_t dma_ch_rd_sar_hi[2];
    uint32_t dma_ch_rd_dar_lo[2];
    uint32_t dma_ch_rd_dar_hi[2];

    uint32_t dma_ch_wr_ctl1[2];
    uint32_t dma_ch_wr_xfer_len[2];
    uint32_t dma_ch_wr_sar_lo[2];
    uint32_t dma_ch_wr_sar_hi[2];
    uint32_t dma_ch_wr_dar_lo[2];
    uint32_t dma_ch_wr_dar_hi[2];

    uint32_t dma_intr_imwr_wdone_high;
    uint32_t dma_intr_imwr_wdone_low;
    uint32_t dma_intr_imwr_wabort_high;
    uint32_t dma_intr_imwr_wabort_low;
    uint32_t dma_intr_wr_imwr_data;

    uint32_t dma_intr_imwr_rdone_high;
    uint32_t dma_intr_imwr_rdone_low;
    uint32_t dma_intr_imwr_rabort_high;
    uint32_t dma_intr_imwr_rabort_low;
    uint32_t dma_intr_rd_imwr_data;
};

G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->intr_status & ~s->intr_mask) {
        if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!msi_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /* DMA logic will be implemented when DMA registers are available */
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case DEV_REV_REG:
        val = s->dev_rev;
        break;
    case SPI_SYSLOCK_REG:
        val = s->syslock;
        break;
    case SPI_CONFIG_PERI_ENABLE_REG:
        val = s->config_peri_enable;
        break;
    case SPI_MST_EVENT_REG_OFFSET(0):
    case SPI_MST_EVENT_REG_OFFSET(1):
        val = s->intr_status;
        break;
    case SPI_MST_EVENT_MASK_REG_OFFSET(0):
    case SPI_MST_EVENT_MASK_REG_OFFSET(1):
        val = s->intr_mask;
        break;
    case SPI_PCI_CTRL_REG_OFFSET(0):
    case SPI_PCI_CTRL_REG_OFFSET(1):
        val = s->config_peri_enable;
        break;
    case SPI_MST_CTL_REG_OFFSET(0):
        val = s->mst_ctl[0];
        break;
    case SPI_MST_CTL_REG_OFFSET(1):
        val = s->mst_ctl[1];
        break;
    case SPI_DMA_GLOBAL_WR_ENGINE_EN:
        val = s->dma_global_wr_engine_en;
        break;
    case SPI_DMA_GLOBAL_RD_ENGINE_EN:
        val = s->dma_global_rd_engine_en;
        break;
    case SPI_DMA_WR_DOORBELL_REG:
        val = 0;
        break;
    case SPI_DMA_RD_DOORBELL_REG:
        val = 0;
        break;
    case SPI_DMA_INTR_RD_STS:
        val = s->dma_intr_rd_sts;
        break;
    case SPI_DMA_INTR_WR_STS:
        val = s->dma_intr_wr_sts;
        break;
    case SPI_DMA_INTR_IMWR_WDONE_HIGH: val = s->dma_intr_imwr_wdone_high; break;
    case SPI_DMA_INTR_IMWR_WDONE_LOW: val = s->dma_intr_imwr_wdone_low; break;
    case SPI_DMA_INTR_IMWR_WABORT_HIGH: val = s->dma_intr_imwr_wabort_high; break;
    case SPI_DMA_INTR_IMWR_WABORT_LOW: val = s->dma_intr_imwr_wabort_low; break;
    case SPI_DMA_INTR_WR_IMWR_DATA: val = s->dma_intr_wr_imwr_data; break;
    case SPI_DMA_INTR_IMWR_RDONE_HIGH: val = s->dma_intr_imwr_rdone_high; break;
    case SPI_DMA_INTR_IMWR_RDONE_LOW: val = s->dma_intr_imwr_rdone_low; break;
    case SPI_DMA_INTR_IMWR_RABORT_HIGH: val = s->dma_intr_imwr_rabort_high; break;
    case SPI_DMA_INTR_IMWR_RABORT_LOW: val = s->dma_intr_imwr_rabort_low; break;
    case SPI_DMA_INTR_RD_IMWR_DATA: val = s->dma_intr_rd_imwr_data; break;

    /* DMA CH0 RD */
    case SPI_DMA_CH0_RD_BASE + SPI_DMA_CH_CTL1_OFFSET: val = s->dma_ch_rd_ctl1[0]; break;
    case SPI_DMA_CH0_RD_BASE + SPI_DMA_CH_XFER_LEN_OFFSET: val = s->dma_ch_rd_xfer_len[0]; break;
    case SPI_DMA_CH0_RD_BASE + SPI_DMA_CH_SAR_LO_OFFSET: val = s->dma_ch_rd_sar_lo[0]; break;
    case SPI_DMA_CH0_RD_BASE + SPI_DMA_CH_SAR_HI_OFFSET: val = s->dma_ch_rd_sar_hi[0]; break;
    case SPI_DMA_CH0_RD_BASE + SPI_DMA_CH_DAR_LO_OFFSET: val = s->dma_ch_rd_dar_lo[0]; break;
    case SPI_DMA_CH0_RD_BASE + SPI_DMA_CH_DAR_HI_OFFSET: val = s->dma_ch_rd_dar_hi[0]; break;

    /* DMA CH1 RD */
    case SPI_DMA_CH1_RD_BASE + SPI_DMA_CH_CTL1_OFFSET: val = s->dma_ch_rd_ctl1[1]; break;
    case SPI_DMA_CH1_RD_BASE + SPI_DMA_CH_XFER_LEN_OFFSET: val = s->dma_ch_rd_xfer_len[1]; break;
    case SPI_DMA_CH1_RD_BASE + SPI_DMA_CH_SAR_LO_OFFSET: val = s->dma_ch_rd_sar_lo[1]; break;
    case SPI_DMA_CH1_RD_BASE + SPI_DMA_CH_SAR_HI_OFFSET: val = s->dma_ch_rd_sar_hi[1]; break;
    case SPI_DMA_CH1_RD_BASE + SPI_DMA_CH_DAR_LO_OFFSET: val = s->dma_ch_rd_dar_lo[1]; break;
    case SPI_DMA_CH1_RD_BASE + SPI_DMA_CH_DAR_HI_OFFSET: val = s->dma_ch_rd_dar_hi[1]; break;

    /* DMA CH0 WR */
    case SPI_DMA_CH0_WR_BASE + SPI_DMA_CH_CTL1_OFFSET: val = s->dma_ch_wr_ctl1[0]; break;
    case SPI_DMA_CH0_WR_BASE + SPI_DMA_CH_XFER_LEN_OFFSET: val = s->dma_ch_wr_xfer_len[0]; break;
    case SPI_DMA_CH0_WR_BASE + SPI_DMA_CH_SAR_LO_OFFSET: val = s->dma_ch_wr_sar_lo[0]; break;
    case SPI_DMA_CH0_WR_BASE + SPI_DMA_CH_SAR_HI_OFFSET: val = s->dma_ch_wr_sar_hi[0]; break;
    case SPI_DMA_CH0_WR_BASE + SPI_DMA_CH_DAR_LO_OFFSET: val = s->dma_ch_wr_dar_lo[0]; break;
    case SPI_DMA_CH0_WR_BASE + SPI_DMA_CH_DAR_HI_OFFSET: val = s->dma_ch_wr_dar_hi[0]; break;

    /* DMA CH1 WR */
    case SPI_DMA_CH1_WR_BASE + SPI_DMA_CH_CTL1_OFFSET: val = s->dma_ch_wr_ctl1[1]; break;
    case SPI_DMA_CH1_WR_BASE + SPI_DMA_CH_XFER_LEN_OFFSET: val = s->dma_ch_wr_xfer_len[1]; break;
    case SPI_DMA_CH1_WR_BASE + SPI_DMA_CH_SAR_LO_OFFSET: val = s->dma_ch_wr_sar_lo[1]; break;
    case SPI_DMA_CH1_WR_BASE + SPI_DMA_CH_SAR_HI_OFFSET: val = s->dma_ch_wr_sar_hi[1]; break;
    case SPI_DMA_CH1_WR_BASE + SPI_DMA_CH_DAR_LO_OFFSET: val = s->dma_ch_wr_dar_lo[1]; break;
    case SPI_DMA_CH1_WR_BASE + SPI_DMA_CH_DAR_HI_OFFSET: val = s->dma_ch_wr_dar_hi[1]; break;

    default:
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case SPI_SYSLOCK_REG:
        s->syslock = val;
        break;
    case SPI_CONFIG_PERI_ENABLE_REG:
        s->config_peri_enable = val;
        break;
    case SPI_MST_EVENT_REG_OFFSET(0):
    case SPI_MST_EVENT_REG_OFFSET(1):
        s->intr_status &= ~val;
        pcibase_update_irq(s);
        break;
    case SPI_MST_EVENT_MASK_REG_OFFSET(0):
    case SPI_MST_EVENT_MASK_REG_OFFSET(1):
        s->intr_mask = val;
        pcibase_update_irq(s);
        break;
    case SPI_PCI_CTRL_REG_OFFSET(0):
    case SPI_PCI_CTRL_REG_OFFSET(1):
        s->config_peri_enable = val;
        break;
    case SPI_MST_CTL_REG_OFFSET(0):
        s->mst_ctl[0] = val;
        break;
    case SPI_MST_CTL_REG_OFFSET(1):
        s->mst_ctl[1] = val;
        break;
    case SPI_DMA_GLOBAL_WR_ENGINE_EN:
        s->dma_global_wr_engine_en = val;
        break;
    case SPI_DMA_GLOBAL_RD_ENGINE_EN:
        s->dma_global_rd_engine_en = val;
        break;
    case SPI_DMA_WR_DOORBELL_REG:
        /* Trigger DMA write */
        break;
    case SPI_DMA_RD_DOORBELL_REG:
        /* Trigger DMA read */
        break;
    case SPI_DMA_INTR_RD_CLR:
        s->dma_intr_rd_sts &= ~val;
        pcibase_update_irq(s);
        break;
    case SPI_DMA_INTR_WR_CLR:
        s->dma_intr_wr_sts &= ~val;
        pcibase_update_irq(s);
        break;
    case SPI_DMA_INTR_IMWR_WDONE_HIGH: s->dma_intr_imwr_wdone_high = val; break;
    case SPI_DMA_INTR_IMWR_WDONE_LOW: s->dma_intr_imwr_wdone_low = val; break;
    case SPI_DMA_INTR_IMWR_WABORT_HIGH: s->dma_intr_imwr_wabort_high = val; break;
    case SPI_DMA_INTR_IMWR_WABORT_LOW: s->dma_intr_imwr_wabort_low = val; break;
    case SPI_DMA_INTR_WR_IMWR_DATA: s->dma_intr_wr_imwr_data = val; break;
    case SPI_DMA_INTR_IMWR_RDONE_HIGH: s->dma_intr_imwr_rdone_high = val; break;
    case SPI_DMA_INTR_IMWR_RDONE_LOW: s->dma_intr_imwr_rdone_low = val; break;
    case SPI_DMA_INTR_IMWR_RABORT_HIGH: s->dma_intr_imwr_rabort_high = val; break;
    case SPI_DMA_INTR_IMWR_RABORT_LOW: s->dma_intr_imwr_rabort_low = val; break;
    case SPI_DMA_INTR_RD_IMWR_DATA: s->dma_intr_rd_imwr_data = val; break;

    /* DMA CH0 RD */
    case SPI_DMA_CH0_RD_BASE + SPI_DMA_CH_CTL1_OFFSET: s->dma_ch_rd_ctl1[0] = val; break;
    case SPI_DMA_CH0_RD_BASE + SPI_DMA_CH_XFER_LEN_OFFSET: s->dma_ch_rd_xfer_len[0] = val; break;
    case SPI_DMA_CH0_RD_BASE + SPI_DMA_CH_SAR_LO_OFFSET: s->dma_ch_rd_sar_lo[0] = val; break;
    case SPI_DMA_CH0_RD_BASE + SPI_DMA_CH_SAR_HI_OFFSET: s->dma_ch_rd_sar_hi[0] = val; break;
    case SPI_DMA_CH0_RD_BASE + SPI_DMA_CH_DAR_LO_OFFSET: s->dma_ch_rd_dar_lo[0] = val; break;
    case SPI_DMA_CH0_RD_BASE + SPI_DMA_CH_DAR_HI_OFFSET: s->dma_ch_rd_dar_hi[0] = val; break;

    /* DMA CH1 RD */
    case SPI_DMA_CH1_RD_BASE + SPI_DMA_CH_CTL1_OFFSET: s->dma_ch_rd_ctl1[1] = val; break;
    case SPI_DMA_CH1_RD_BASE + SPI_DMA_CH_XFER_LEN_OFFSET: s->dma_ch_rd_xfer_len[1] = val; break;
    case SPI_DMA_CH1_RD_BASE + SPI_DMA_CH_SAR_LO_OFFSET: s->dma_ch_rd_sar_lo[1] = val; break;
    case SPI_DMA_CH1_RD_BASE + SPI_DMA_CH_SAR_HI_OFFSET: s->dma_ch_rd_sar_hi[1] = val; break;
    case SPI_DMA_CH1_RD_BASE + SPI_DMA_CH_DAR_LO_OFFSET: s->dma_ch_rd_dar_lo[1] = val; break;
    case SPI_DMA_CH1_RD_BASE + SPI_DMA_CH_DAR_HI_OFFSET: s->dma_ch_rd_dar_hi[1] = val; break;

    /* DMA CH0 WR */
    case SPI_DMA_CH0_WR_BASE + SPI_DMA_CH_CTL1_OFFSET: s->dma_ch_wr_ctl1[0] = val; break;
    case SPI_DMA_CH0_WR_BASE + SPI_DMA_CH_XFER_LEN_OFFSET: s->dma_ch_wr_xfer_len[0] = val; break;
    case SPI_DMA_CH0_WR_BASE + SPI_DMA_CH_SAR_LO_OFFSET: s->dma_ch_wr_sar_lo[0] = val; break;
    case SPI_DMA_CH0_WR_BASE + SPI_DMA_CH_SAR_HI_OFFSET: s->dma_ch_wr_sar_hi[0] = val; break;
    case SPI_DMA_CH0_WR_BASE + SPI_DMA_CH_DAR_LO_OFFSET: s->dma_ch_wr_dar_lo[0] = val; break;
    case SPI_DMA_CH0_WR_BASE + SPI_DMA_CH_DAR_HI_OFFSET: s->dma_ch_wr_dar_hi[0] = val; break;

    /* DMA CH1 WR */
    case SPI_DMA_CH1_WR_BASE + SPI_DMA_CH_CTL1_OFFSET: s->dma_ch_wr_ctl1[1] = val; break;
    case SPI_DMA_CH1_WR_BASE + SPI_DMA_CH_XFER_LEN_OFFSET: s->dma_ch_wr_xfer_len[1] = val; break;
    case SPI_DMA_CH1_WR_BASE + SPI_DMA_CH_SAR_LO_OFFSET: s->dma_ch_wr_sar_lo[1] = val; break;
    case SPI_DMA_CH1_WR_BASE + SPI_DMA_CH_SAR_HI_OFFSET: s->dma_ch_wr_sar_hi[1] = val; break;
    case SPI_DMA_CH1_WR_BASE + SPI_DMA_CH_DAR_LO_OFFSET: s->dma_ch_wr_dar_lo[1] = val; break;
    case SPI_DMA_CH1_WR_BASE + SPI_DMA_CH_DAR_HI_OFFSET: s->dma_ch_wr_dar_hi[1] = val; break;

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
    
    s->intr_status = 0;
    s->intr_mask = 0;
    s->dev_rev = 0xC0;
    s->syslock = 0;
    s->config_peri_enable = 0;
    s->dma_global_wr_engine_en = 0;
    s->dma_global_rd_engine_en = 0;

    s->mst_ctl[0] = 0;
    s->mst_ctl[1] = 0;
    
    for (int i = 0; i < 2; i++) {
        s->dma_ch_rd_ctl1[i] = 0;
        s->dma_ch_rd_xfer_len[i] = 0;
        s->dma_ch_rd_sar_lo[i] = 0;
        s->dma_ch_rd_sar_hi[i] = 0;
        s->dma_ch_rd_dar_lo[i] = 0;
        s->dma_ch_rd_dar_hi[i] = 0;

        s->dma_ch_wr_ctl1[i] = 0;
        s->dma_ch_wr_xfer_len[i] = 0;
        s->dma_ch_wr_sar_lo[i] = 0;
        s->dma_ch_wr_sar_hi[i] = 0;
        s->dma_ch_wr_dar_lo[i] = 0;
        s->dma_ch_wr_dar_hi[i] = 0;
    }

    s->dma_intr_imwr_wdone_high = 0;
    s->dma_intr_imwr_wdone_low = 0;
    s->dma_intr_imwr_wabort_high = 0;
    s->dma_intr_imwr_wabort_low = 0;
    s->dma_intr_wr_imwr_data = 0;

    s->dma_intr_imwr_rdone_high = 0;
    s->dma_intr_imwr_rdone_low = 0;
    s->dma_intr_imwr_rabort_high = 0;
    s->dma_intr_imwr_rabort_low = 0;
    s->dma_intr_rd_imwr_data = 0;

    s->dma_intr_rd_sts = 0;
    s->dma_intr_wr_sts = 0;
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }
    
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x1055 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0xa004 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0c80 );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    s->num_bars = 2;  
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x200000; 
    s->bar_info[0].name = "spi_pci1xxxx_bar0";

    s->bar_info[1].index = 2;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = 0x2000; 
    s->bar_info[1].name = "spi_pci1xxxx_bar2";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = true;
    if (msi_init(pdev, 0, 32, true, false, errp)) {
        /* MSI init failed */
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

static const VMStateDescription vmstate_pcibase = {
    .name = "spi_pci1xxxx_pci",
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
