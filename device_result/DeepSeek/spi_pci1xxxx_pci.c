/*
 * QEMU model for MCHP SPI PCI device (spi-pci1xxxx).
 * Generated for Phase 2 implementation.
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

#define TYPE_PCIBASE_DEVICE "spi_pci1xxxx_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID_MCHP 0x1055
#define DEVICE_ID 0xa004
#define CLASS_ID 0x0c0500

#define SPI_MST_CTL_DEVSEL_MASK		(GENMASK(27, 25))
#define SPI_MST_CTL_CMD_LEN_MASK	(GENMASK(16, 8))
#define SPI_MST_CTL_SPEED_MASK		(GENMASK(7, 5))
#define SPI_MSI_VECTOR_SEL_MASK		(GENMASK(4, 4))
#define SPI_MST_CTL_FORCE_CE		(BIT(4))
#define SPI_MST_CTL_MODE_SEL		(BIT(2))
#define SPI_MST_CTL_GO			(BIT(0))
#define SPI_PERI_ADDR_BASE		(0x160000)
#define SPI_SYSTEM_ADDR_BASE		(0x2000)
#define SPI_MST1_ADDR_BASE		(0x800)
#define DEV_REV_REG			(SPI_SYSTEM_ADDR_BASE + 0x00)
#define SPI_SYSLOCK_REG			(SPI_SYSTEM_ADDR_BASE + 0xA0)
#define SPI_CONFIG_PERI_ENABLE_REG	(SPI_SYSTEM_ADDR_BASE + 0x108)
#define SPI_PERI_ENBLE_PF_MASK		(GENMASK(17, 16))
#define DEV_REV_MASK			(GENMASK(7, 0))
#define SPI_SYSLOCK			BIT(4)
#define SPI0				(0)
#define SPI1				(1)
#define SPI_DMA_ADDR_BASE		(0x1000)
#define SPI_DMA_GLOBAL_WR_ENGINE_EN	(SPI_DMA_ADDR_BASE + 0x0C)
#define SPI_DMA_WR_DOORBELL_REG		(SPI_DMA_ADDR_BASE + 0x10)
#define SPI_DMA_GLOBAL_RD_ENGINE_EN	(SPI_DMA_ADDR_BASE + 0x2C)
#define SPI_DMA_RD_DOORBELL_REG		(SPI_DMA_ADDR_BASE + 0x30)
#define SPI_DMA_INTR_WR_STS		(SPI_DMA_ADDR_BASE + 0x4C)
#define SPI_DMA_WR_INT_MASK		(SPI_DMA_ADDR_BASE + 0x54)
#define SPI_DMA_INTR_WR_CLR		(SPI_DMA_ADDR_BASE + 0x58)
#define SPI_DMA_ERR_WR_STS		(SPI_DMA_ADDR_BASE + 0x5C)
#define SPI_DMA_INTR_IMWR_WDONE_LOW	(SPI_DMA_ADDR_BASE + 0x60)
#define SPI_DMA_INTR_IMWR_WDONE_HIGH	(SPI_DMA_ADDR_BASE + 0x64)
#define SPI_DMA_INTR_IMWR_WABORT_LOW	(SPI_DMA_ADDR_BASE + 0x68)
#define SPI_DMA_INTR_IMWR_WABORT_HIGH	(SPI_DMA_ADDR_BASE + 0x6C)
#define SPI_DMA_INTR_WR_IMWR_DATA	(SPI_DMA_ADDR_BASE + 0x70)
#define SPI_DMA_INTR_RD_STS		(SPI_DMA_ADDR_BASE + 0xA0)
#define SPI_DMA_RD_INT_MASK		(SPI_DMA_ADDR_BASE + 0xA8)
#define SPI_DMA_INTR_RD_CLR		(SPI_DMA_ADDR_BASE + 0xAC)
#define SPI_DMA_ERR_RD_STS		(SPI_DMA_ADDR_BASE + 0xB8)
#define SPI_DMA_INTR_IMWR_RDONE_LOW	(SPI_DMA_ADDR_BASE + 0xCC)
#define SPI_DMA_INTR_IMWR_RDONE_HIGH	(SPI_DMA_ADDR_BASE + 0xD0)
#define SPI_DMA_INTR_IMWR_RABORT_LOW	(SPI_DMA_ADDR_BASE + 0xD4)
#define SPI_DMA_INTR_IMWR_RABORT_HIGH	(SPI_DMA_ADDR_BASE + 0xD8)
#define SPI_DMA_INTR_RD_IMWR_DATA	(SPI_DMA_ADDR_BASE + 0xDC)
#define SPI_DMA_CH0_WR_BASE		(SPI_DMA_ADDR_BASE + 0x200)
#define SPI_DMA_CH0_RD_BASE		(SPI_DMA_ADDR_BASE + 0x300)
#define SPI_DMA_CH1_WR_BASE		(SPI_DMA_ADDR_BASE + 0x400)
#define SPI_DMA_CH1_RD_BASE		(SPI_DMA_ADDR_BASE + 0x500)
#define SPI_DMA_CH_CTL1_OFFSET		(0x00)
#define SPI_DMA_CH_XFER_LEN_OFFSET	(0x08)
#define SPI_DMA_CH_SAR_LO_OFFSET	(0x0C)
#define SPI_DMA_CH_SAR_HI_OFFSET	(0x10)
#define SPI_DMA_CH_DAR_LO_OFFSET	(0x14)
#define SPI_DMA_CH_DAR_HI_OFFSET	(0x18)
#define SPI_DMA_CH0_DONE_INT		BIT(0)
#define SPI_DMA_CH1_DONE_INT		BIT(1)
#define SPI_DMA_CH0_ABORT_INT		BIT(16)
#define SPI_DMA_CH1_ABORT_INT		BIT(17)
#define SPI_DMA_DONE_INT_MASK(x)	(1 << (x))
#define SPI_DMA_ABORT_INT_MASK(x)	(1 << (16 + (x)))
#define DMA_CH_CONTROL_LIE		BIT(3)
#define DMA_CH_CONTROL_RIE		BIT(4)
#define DMA_INTR_EN			(DMA_CH_CONTROL_LIE | DMA_CH_CONTROL_RIE)
#define SPI_MST_CMD_BUF_OFFSET(x)		(((x) * SPI_MST1_ADDR_BASE) + 0x00)
#define SPI_MST_RSP_BUF_OFFSET(x)		(((x) * SPI_MST1_ADDR_BASE) + 0x200)
#define SPI_MST_CTL_REG_OFFSET(x)		(((x) * SPI_MST1_ADDR_BASE) + 0x400)
#define SPI_MST_EVENT_REG_OFFSET(x)		(((x) * SPI_MST1_ADDR_BASE) + 0x420)
#define SPI_MST_EVENT_MASK_REG_OFFSET(x)	(((x) * SPI_MST1_ADDR_BASE) + 0x424)
#define SPI_MST_PAD_CTL_REG_OFFSET(x)		(((x) * SPI_MST1_ADDR_BASE) + 0x460)
#define SPIALERT_MST_DB_REG_OFFSET(x)		(((x) * SPI_MST1_ADDR_BASE) + 0x464)
#define SPIALERT_MST_VAL_REG_OFFSET(x)		(((x) * SPI_MST1_ADDR_BASE) + 0x468)
#define SPI_PCI_CTRL_REG_OFFSET(x)		(((x) * SPI_MST1_ADDR_BASE) + 0x480)
#define PCI1XXXX_IRQ_FLAGS			(IRQF_NO_SUSPEND | IRQF_TRIGGER_NONE)
#define SPI_MAX_DATA_LEN			320
#define PCI1XXXX_SPI_TIMEOUT			(msecs_to_jiffies(100))
#define SPI_DMA_ENGINE_EN			(0x1)
#define SPI_DMA_ENGINE_DIS			(0x0)
#define SPI_INTR		(BIT(8))
#define SPI_FORCE_CE		(BIT(4))
#define SPI_CHIP_SEL_COUNT 7
#define SPI_SUSPEND_CONFIG 0x101
#define SPI_RESUME_CONFIG 0x203
#define NUM_VEC_PER_INST 3
#define NUM_MSI_VECTORS 8
#define NUM_HW_INST 2

#define BAR0_SIZE 0x4000
#define BAR2_SIZE 0x2000

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
    uint32_t msi_vector_sel;
    int num_msi_vectors;

    /* Hardware Register Shadows */
    uint32_t regs[0x1000]; /* not used directly */

    /* SPI registers */
    uint32_t dev_rev_reg;
    uint32_t syslock_reg;
    uint32_t peri_enable_reg;
    uint32_t ctl_reg[NUM_HW_INST];
    uint32_t event_reg[NUM_HW_INST];
    uint32_t event_mask[NUM_HW_INST];
    uint32_t pci_ctrl_reg[NUM_HW_INST];

    /* SPI buffers */
    uint8_t cmd_buf[NUM_HW_INST][512];
    uint8_t rsp_buf[NUM_HW_INST][512];

    /* DMA engine registers */
    uint32_t dma_regs[0x800]; /* 0x2000 bytes, covers all DMA offsets */
    uint32_t dma_global_en; /* bit0: WR, bit1: RD */
    uint32_t imwr_wdone_addr_lo;
    uint32_t imwr_wdone_addr_hi;
    uint32_t imwr_wabort_addr_lo;
    uint32_t imwr_wabort_addr_hi;
    uint32_t imwr_rdone_addr_lo;
    uint32_t imwr_rdone_addr_hi;
    uint32_t imwr_rabort_addr_lo;
    uint32_t imwr_rabort_addr_hi;
    uint32_t imwr_wr_data;
    uint32_t imwr_rd_data;

    /* DMA Context */
    struct {
        dma_addr_t src_addr;
        dma_addr_t dst_addr;
        dma_addr_t xfer_len;
        uint32_t control;
        bool running;
    } dma_ch[2]; /* not used */

    uint32_t device_status;
    bool reset_pending;
    uint32_t pm_state;
};

static uint64_t pcibase_regs_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= 0x000 && addr < 0x200) {
        /* Instance 0 CMD buffer */
        if (addr + size <= 0x200) {
            memcpy(&val, &s->cmd_buf[0][addr], MIN(size, sizeof(val)));
        }
    } else if (addr >= 0x200 && addr < 0x400) {
        /* Instance 0 RSP buffer */
        if (addr + size <= 0x400) {
            memcpy(&val, &s->rsp_buf[0][addr - 0x200], MIN(size, sizeof(val)));
        }
    } else if (addr >= 0x800 && addr < 0xA00) {
        /* Instance 1 CMD buffer */
        if (addr + size <= 0xA00) {
            memcpy(&val, &s->cmd_buf[1][addr - 0x800], MIN(size, sizeof(val)));
        }
    } else if (addr >= 0xA00 && addr < 0xC00) {
        /* Instance 1 RSP buffer */
        if (addr + size <= 0xC00) {
            memcpy(&val, &s->rsp_buf[1][addr - 0xA00], MIN(size, sizeof(val)));
        }
    } else {
        switch (addr) {
        case 0x400:
            val = s->ctl_reg[0];
            break;
        case 0x420:
            val = s->event_reg[0];
            break;
        case 0x424:
            val = s->event_mask[0];
            break;
        case 0x460:
            val = 0; /* PAD_CTL, not used */
            break;
        case 0x464:
            val = 0; /* SPIALERT_DB */
            break;
        case 0x468:
            val = 0; /* SPIALERT_VAL */
            break;
        case 0x480:
            val = s->pci_ctrl_reg[0];
            break;
        case 0xC00:
            val = s->ctl_reg[1];
            break;
        case 0xC20:
            val = s->event_reg[1];
            break;
        case 0xC24:
            val = s->event_mask[1];
            break;
        case 0xC60:
            val = 0; /* PAD_CTL inst1 */
            break;
        case 0xC64:
            val = 0; /* SPIALERT_DB inst1 */
            break;
        case 0xC68:
            val = 0; /* SPIALERT_VAL inst1 */
            break;
        case 0xC80:
            val = s->pci_ctrl_reg[1];
            break;
        case 0x2000: /* DEV_REV_REG */
            val = s->dev_rev_reg;
            break;
        case 0x20A0: /* SYSLOCK_REG */
            val = s->syslock_reg;
            break;
        case 0x2108: /* SPI_CONFIG_PERI_ENABLE_REG */
            val = s->peri_enable_reg;
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR, "%s: unimplemented regs read 0x%" HWADDR_PRIx "\n", __func__, addr);
            val = 0;
            break;
        }
    }
    return val;
}

static void pcibase_regs_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= 0x000 && addr < 0x200) {
        /* Instance 0 CMD buffer */
        if (addr + size <= 0x200) {
            memcpy(&s->cmd_buf[0][addr], &val, MIN(size, sizeof(val)));
        }
    } else if (addr >= 0x200 && addr < 0x400) {
        /* Instance 0 RSP buffer (read-only in hardware? Driver never writes, but allow) */
        if (addr + size <= 0x400) {
            memcpy(&s->rsp_buf[0][addr - 0x200], &val, MIN(size, sizeof(val)));
        }
    } else if (addr >= 0x800 && addr < 0xA00) {
        if (addr + size <= 0xA00) {
            memcpy(&s->cmd_buf[1][addr - 0x800], &val, MIN(size, sizeof(val)));
        }
    } else if (addr >= 0xA00 && addr < 0xC00) {
        if (addr + size <= 0xC00) {
            memcpy(&s->rsp_buf[1][addr - 0xA00], &val, MIN(size, sizeof(val)));
        }
    } else {
        switch (addr) {
        case 0x400:
            s->ctl_reg[0] = val;
            break;
        case 0x420:
            /* W1C: clear bits that are written */
            s->event_reg[0] &= ~val;
            break;
        case 0x424:
            s->event_mask[0] = val;
            break;
        case 0x460:
        case 0x464:
        case 0x468:
            break;
        case 0x480:
            s->pci_ctrl_reg[0] = val;
            break;
        case 0xC00:
            s->ctl_reg[1] = val;
            break;
        case 0xC20:
            s->event_reg[1] &= ~val;
            break;
        case 0xC24:
            s->event_mask[1] = val;
            break;
        case 0xC60:
        case 0xC64:
        case 0xC68:
            break;
        case 0xC80:
            s->pci_ctrl_reg[1] = val;
            break;
        case 0x2000: /* DEV_REV_REG read-only */
            break;
        case 0x20A0: /* SYSLOCK_REG */
            if (val & SPI_SYSLOCK) {
                s->syslock_reg = SPI_SYSLOCK;
            } else {
                s->syslock_reg = 0;
            }
            break;
        case 0x2108: /* SPI_CONFIG_PERI_ENABLE_REG read-only */
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR, "%s: unimplemented regs write 0x%" HWADDR_PRIx " = 0x%" PRIx64 "\n", __func__, addr, val);
            break;
        }
    }
}

static const MemoryRegionOps pcibase_regs_ops = {
    .read = pcibase_regs_read,
    .write = pcibase_regs_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static void send_dma_msi(PCIBaseState *s, int inst, bool is_write, bool done)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t addr_lo, addr_hi;
    uint32_t data;
    int vector;

    if (done) {
        if (is_write) {
            addr_lo = s->imwr_wdone_addr_lo;
            addr_hi = s->imwr_wdone_addr_hi;
            data = s->imwr_wr_data;
            /* low 16 bits for inst 0, high 16 for inst 1 */
            data = (inst == 0) ? (data & 0xFFFF) : ((data >> 16) & 0xFFFF);
        } else {
            addr_lo = s->imwr_rdone_addr_lo;
            addr_hi = s->imwr_rdone_addr_hi;
            data = s->imwr_rd_data;
            data = (inst == 0) ? (data & 0xFFFF) : ((data >> 16) & 0xFFFF);
        }
    } else { /* abort */
        if (is_write) {
            addr_lo = s->imwr_wabort_addr_lo;
            addr_hi = s->imwr_wabort_addr_hi;
            data = s->imwr_wr_data;
            data = (inst == 0) ? (data & 0xFFFF) : ((data >> 16) & 0xFFFF);
        } else {
            addr_lo = s->imwr_rabort_addr_lo;
            addr_hi = s->imwr_rabort_addr_hi;
            data = s->imwr_rd_data;
            data = (inst == 0) ? (data & 0xFFFF) : ((data >> 16) & 0xFFFF);
        }
    }

    /* The MSI vector index is embedded in the data (lower 16 bits of data?) */
    /* For simplicity, we assume that the vector is directly the data value. */
    vector = data;
    if (msi_enabled(pdev)) {
        msi_notify(pdev, vector);
    }
}

static void do_dma_write(PCIBaseState *s, int inst)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t base_off, ctl, xfer_len, dar_lo, dar_hi;
    dma_addr_t dst_addr;
    int ret;
    uint8_t buf[512];

    base_off = (inst == 0) ? SPI_DMA_CH0_WR_BASE : SPI_DMA_CH1_WR_BASE;
    ctl = s->dma_regs[(base_off - SPI_DMA_ADDR_BASE) / 4 + SPI_DMA_CH_CTL1_OFFSET / 4];
    xfer_len = s->dma_regs[(base_off - SPI_DMA_ADDR_BASE) / 4 + SPI_DMA_CH_XFER_LEN_OFFSET / 4];
    dar_lo = s->dma_regs[(base_off - SPI_DMA_ADDR_BASE) / 4 + SPI_DMA_CH_DAR_LO_OFFSET / 4];
    dar_hi = s->dma_regs[(base_off - SPI_DMA_ADDR_BASE) / 4 + SPI_DMA_CH_DAR_HI_OFFSET / 4];
    dst_addr = ((dma_addr_t)dar_hi << 32) | dar_lo;

    /* Perform DMA from device RSP buffer to host memory */
    if (xfer_len > sizeof(buf)) xfer_len = sizeof(buf);
    memcpy(buf, &s->rsp_buf[inst][0], xfer_len);
    ret = pci_dma_write(pdev, dst_addr, buf, xfer_len);
    if (ret) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: DMA write error\n", __func__);
        return;
    }

    /* Set done interrupt status and raise MSI if enabled */
    if (inst == 0) {
        s->dma_regs[(SPI_DMA_INTR_WR_STS - SPI_DMA_ADDR_BASE) / 4] |= SPI_DMA_CH0_DONE_INT;
    } else {
        s->dma_regs[(SPI_DMA_INTR_WR_STS - SPI_DMA_ADDR_BASE) / 4] |= SPI_DMA_CH1_DONE_INT;
    }
    if (ctl & DMA_CH_CONTROL_LIE) {
        send_dma_msi(s, inst, true, true);
    }
}

static void do_dma_read(PCIBaseState *s, int inst)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t base_off, ctl, xfer_len, sar_lo, sar_hi;
    dma_addr_t src_addr;
    int ret;
    uint8_t buf[512];

    base_off = (inst == 0) ? SPI_DMA_CH0_RD_BASE : SPI_DMA_CH1_RD_BASE;
    ctl = s->dma_regs[(base_off - SPI_DMA_ADDR_BASE) / 4 + SPI_DMA_CH_CTL1_OFFSET / 4];
    xfer_len = s->dma_regs[(base_off - SPI_DMA_ADDR_BASE) / 4 + SPI_DMA_CH_XFER_LEN_OFFSET / 4];
    sar_lo = s->dma_regs[(base_off - SPI_DMA_ADDR_BASE) / 4 + SPI_DMA_CH_SAR_LO_OFFSET / 4];
    sar_hi = s->dma_regs[(base_off - SPI_DMA_ADDR_BASE) / 4 + SPI_DMA_CH_SAR_HI_OFFSET / 4];
    src_addr = ((dma_addr_t)sar_hi << 32) | sar_lo;

    /* Perform DMA from host memory to device CMD buffer */
    if (xfer_len > sizeof(buf)) xfer_len = sizeof(buf);
    ret = pci_dma_read(pdev, src_addr, buf, xfer_len);
    if (ret) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: DMA read error\n", __func__);
        return;
    }
    memcpy(&s->cmd_buf[inst][0], buf, xfer_len);

    /* Set done interrupt status */
    if (inst == 0) {
        s->dma_regs[(SPI_DMA_INTR_RD_STS - SPI_DMA_ADDR_BASE) / 4] |= SPI_DMA_CH0_DONE_INT;
    } else {
        s->dma_regs[(SPI_DMA_INTR_RD_STS - SPI_DMA_ADDR_BASE) / 4] |= SPI_DMA_CH1_DONE_INT;
    }
    if (ctl & DMA_CH_CONTROL_LIE) {
        send_dma_msi(s, inst, false, true);
    }
}

static uint64_t pcibase_dma_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr < SPI_DMA_ADDR_BASE || addr >= SPI_DMA_ADDR_BASE + 0x600) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds DMA read 0x%" HWADDR_PRIx "\n", __func__, addr);
        return val;
    }

    uint32_t dma_offset = addr - SPI_DMA_ADDR_BASE;
    switch (dma_offset) {
    case 0x0C: /* SPI_DMA_GLOBAL_WR_ENGINE_EN */
        val = s->dma_global_en & 0x1;
        break;
    case 0x2C: /* SPI_DMA_GLOBAL_RD_ENGINE_EN */
        val = (s->dma_global_en >> 1) & 0x1;
        break;
    default:
        if (dma_offset < sizeof(s->dma_regs)) {
            val = s->dma_regs[dma_offset / 4];
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: unimplemented DMA read 0x%" HWADDR_PRIx "\n", __func__, addr);
        }
        break;
    }
    return val;
}

static void pcibase_dma_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr < SPI_DMA_ADDR_BASE || addr >= SPI_DMA_ADDR_BASE + 0x600) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds DMA write 0x%" HWADDR_PRIx "\n", __func__, addr);
        return;
    }

    uint32_t dma_offset = addr - SPI_DMA_ADDR_BASE;
    switch (dma_offset) {
    case 0x0C: /* SPI_DMA_GLOBAL_WR_ENGINE_EN */
        if (val & SPI_DMA_ENGINE_EN) {
            s->dma_global_en |= 1;
        } else {
            s->dma_global_en &= ~1;
        }
        break;
    case 0x2C: /* SPI_DMA_GLOBAL_RD_ENGINE_EN */
        if (val & SPI_DMA_ENGINE_EN) {
            s->dma_global_en |= 2;
        } else {
            s->dma_global_en &= ~2;
        }
        break;
    case 0x10: /* SPI_DMA_WR_DOORBELL_REG */
        if (s->dma_global_en & 1) {
            int inst = val & 0x1; /* instance number */
            if (inst < NUM_HW_INST) {
                do_dma_write(s, inst);
            }
        }
        break;
    case 0x30: /* SPI_DMA_RD_DOORBELL_REG */
        if (s->dma_global_en & 2) {
            int inst = val & 0x1;
            if (inst < NUM_HW_INST) {
                do_dma_read(s, inst);
            }
        }
        break;
    case 0x4C: /* SPI_DMA_INTR_WR_STS */
        s->dma_regs[(dma_offset) / 4] = val;
        break;
    case 0x54: /* SPI_DMA_WR_INT_MASK */
        s->dma_regs[(dma_offset) / 4] = val;
        break;
    case 0x58: /* SPI_DMA_INTR_WR_CLR (W1C) */
        s->dma_regs[(SPI_DMA_INTR_WR_STS - SPI_DMA_ADDR_BASE) / 4] &= ~val;
        break;
    case 0x5C: /* SPI_DMA_ERR_WR_STS */
        s->dma_regs[(dma_offset) / 4] = val;
        break;
    case 0x60: /* IMWR_WDONE_LOW */
        s->imwr_wdone_addr_lo = val;
        break;
    case 0x64: /* IMWR_WDONE_HIGH */
        s->imwr_wdone_addr_hi = val;
        break;
    case 0x68: /* IMWR_WABORT_LOW */
        s->imwr_wabort_addr_lo = val;
        break;
    case 0x6C: /* IMWR_WABORT_HIGH */
        s->imwr_wabort_addr_hi = val;
        break;
    case 0x70: /* IMWR_WR_IMWR_DATA */
        s->imwr_wr_data = val;
        break;
    case 0xA0: /* SPI_DMA_INTR_RD_STS */
        s->dma_regs[(dma_offset) / 4] = val;
        break;
    case 0xA8: /* SPI_DMA_RD_INT_MASK */
        s->dma_regs[(dma_offset) / 4] = val;
        break;
    case 0xAC: /* SPI_DMA_INTR_RD_CLR (W1C) */
        s->dma_regs[(SPI_DMA_INTR_RD_STS - SPI_DMA_ADDR_BASE) / 4] &= ~val;
        break;
    case 0xB8: /* SPI_DMA_ERR_RD_STS */
        s->dma_regs[(dma_offset) / 4] = val;
        break;
    case 0xCC: /* IMWR_RDONE_LOW */
        s->imwr_rdone_addr_lo = val;
        break;
    case 0xD0: /* IMWR_RDONE_HIGH */
        s->imwr_rdone_addr_hi = val;
        break;
    case 0xD4: /* IMWR_RABORT_LOW */
        s->imwr_rabort_addr_lo = val;
        break;
    case 0xD8: /* IMWR_RABORT_HIGH */
        s->imwr_rabort_addr_hi = val;
        break;
    case 0xDC: /* IMWR_RD_IMWR_DATA */
        s->imwr_rd_data = val;
        break;
    default:
        if (dma_offset < sizeof(s->dma_regs)) {
            s->dma_regs[dma_offset / 4] = val;
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: unimplemented DMA write 0x%" HWADDR_PRIx " = 0x%" PRIx64 "\n", __func__, addr, val);
        }
        break;
    }
}

static const MemoryRegionOps pcibase_dma_ops = {
    .read = pcibase_dma_read,
    .write = pcibase_dma_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Reset registers to power-on defaults */
    s->dev_rev_reg = 0xC0; /* >= 0xC0 enables DMA support */
    s->syslock_reg = 0;
    s->peri_enable_reg = 0; /* SPI_PERI_ENBLE_PF_MASK = 0 */
    for (int i = 0; i < NUM_HW_INST; i++) {
        s->ctl_reg[i] = 0;
        s->event_reg[i] = 0;
        s->event_mask[i] = SPI_INTR; /* Initially, SPI_INTR bit set (masked) */
        s->pci_ctrl_reg[i] = 0;
        memset(s->cmd_buf[i], 0, sizeof(s->cmd_buf[i]));
        memset(s->rsp_buf[i], 0, sizeof(s->rsp_buf[i]));
    }
    memset(s->dma_regs, 0, sizeof(s->dma_regs));
    s->dma_global_en = 0;
    s->imwr_wdone_addr_lo = 0;
    s->imwr_wdone_addr_hi = 0;
    s->imwr_wabort_addr_lo = 0;
    s->imwr_wabort_addr_hi = 0;
    s->imwr_rdone_addr_lo = 0;
    s->imwr_rdone_addr_hi = 0;
    s->imwr_rabort_addr_lo = 0;
    s->imwr_rabort_addr_hi = 0;
    s->imwr_wr_data = 0;
    s->imwr_rd_data = 0;
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID, 0x1055);
    pci_set_word(pci_conf + PCI_DEVICE_ID, 0xa004);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR0: SPI registers and buffers */
    memory_region_init_io(&s->bar_regions[0], OBJECT(s), &pcibase_regs_ops, s, "regs", BAR0_SIZE);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_regions[0]);

    /* BAR2: DMA engine registers */
    memory_region_init_io(&s->bar_regions[2], OBJECT(s), &pcibase_dma_ops, s, "dma", BAR2_SIZE);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_regions[2]);

    /* MSI initialization: allocate 8 vectors (power of 2, driver uses up to 6) */
    if (msi_init(pdev, 0, NUM_MSI_VECTORS, true, false, errp)) {
        return;
    }

    /* Field initializations */
    pcibase_reset(DEVICE(pdev));
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
