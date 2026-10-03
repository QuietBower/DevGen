/*
 * QEMU 8.2.10 device model for Adaptec AIC-79xx PCI SCSI controller.
 * This file was generated based on the Linux driver source
 * (aic79xx_osm_pci.c) and register definitions.
 *
 * The device emulates enough register-level behavior to allow the
 * driver to probe, initialize, and bind successfully, reaching
 * "Kernel driver in use".
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

#define TYPE_PCIBASE_DEVICE "aic79xx_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x9005
#define DEVICE_ID 0x8000
#define CLASS_ID  0x0100

#define CLRPCIINT       0x10
#define STA             0x08
#define PCIINT          0x10
#define TARGPCISTAT     0xa7
#define SEQCTL0         0xd6
#define KERNEL_QFREEZE_COUNT 0x134
#define SCSISEQ0        0x3a
#define SELECTOUT_QFROZEN 0x04
#define MODE_PTR        0x00
#define SCSISEQ1        0x3b
#define SEQINTCTL       0xd9
#define SPLTINT         0x01
#define DF0PCISTAT      0xa0
#define FLX_FSTAT_BUSY  0x1
#define FLXADDR_ROMSTAT_CURSENSECTL 0x1
#define FLX_CSTAT_SHIFT 2
#define FLXADDR_CURRENT_STAT 0x4
#define FLX_CSTAT_UNDER 0x2
#define CURSENSE_ENB    0x1
#define FLX_CSTAT_OKAY  0x0
#define FLX_CSTAT_MASK  0x03
#define FLX_CSTAT_INVALID 0x3
#define FLXADDR_FLEXSTAT 0x2
#define FLX_CSTAT_OVER  0x1
#define CMDS_PENDING    0x154
#define SCBID_IS_NULL(scbid) (((scbid) & 0xFF00 ) == SCB_LIST_NULL)
#define COMPLETE_DMA_SCB_HEAD 0x12c
#define GSFIFO          0x58
#define SCB_NEXT_COMPLETE 0x18c
#define LQISTAT2        0x52
#define COMPLETE_SCB_HEAD 0x128
#define COMPLETE_DMA_SCB_TAIL 0x12e
#define SG_STATUS_VALID 0x04
#define LQIGSAVAIL      0x01
#define COMPLETE_ON_QFREEZE_HEAD 0x130
#define HWERRINT        0x80
#define SEQINTCODE      0x02
#define NO_SEQINT       0x00
#define SIMODE3         0x53
#define LQOMODE0        0x54
#define LQOMODE1        0x55
#define LQIMODE1        0x51
#define LQIMODE0        0x50
#define CURADDR         0xf4
#define ABRTBITPTR      0x2c
#define ENCFG4DATA      0x10
#define WRTBIASCTL_HP_DEFAULT 0x00
#define ENLQIOVERI_NLQ  0x01
#define MULTARGID       0x40
#define ENCFG4ISTAT     0x08
#define MK_MESSAGE_SCSIID 0x162
#define WAITING_TID_HEAD 0x120
#define CMDLENPTR       0x25
#define LQONOCHKOVER    0x01
#define NUMDSPS         0x14
#define LUNLEN          0x30
#define WIDERESEN       0x10
#define CLRSEQINTSRC    0x5b
#define ANNEXDAT        0x66
#define INT_COALESCING_CMDCOUNT 0x156
#define MK_MESSAGE_SCB  0x160
#define QOUTFIFO_NEXT_ADDR 0x144
#define NTRAMPERR       0x02
#define LUNLEN_SINGLE_LEVEL_LUN 0x0f
#define NEGCONOPTS      0x64
#define ENLIQABORT      0x20
#define ENSLOWCRC       0x08
#define MK_MESSAGE_BIT_OFFSET 0x04
#define ENOSRAMPERR     0x01
#define WAITING_TID_TAIL 0x122
#define ENLQOATNLQ      0x04
#define WAITING_SCB_TAILS 0x100
#define SCB_QSIZE_512   0x07
#define SCSCHKN         0x66
#define INTVEC1_ADDR    0xf4
#define ABRTBYTEPTR     0x2b
#define COMPLETE_SCB_DMAINPROG_HEAD 0x12a
#define ATTRPTR         0x26
#define QNEXTPTR        0x29
#define CURRFIFODEF     0x20
#define WRTBIASCTL      0xc5
#define SG_STATE        0xa6
#define ENSAVEPTRS      0x20
#define TOWNID          0x69
#define LQCTL1          0x38
#define LUNPTR          0x22
#define ANNEXCOL        0x65
#define FLAGPTR         0x27
#define SCB_CDB_LEN_PTR 0x80
#define SPLTSTADIS      0x08
#define ENCFG4ICMD      0x02
#define NEGOADDR        0x60
#define STIMESEL_BUG_ADJ 0x08
#define DSPSELECT       0xc4
#define MAXCMD          0x32
#define IOWNID          0x67
#define ENLQIBADLQI     0x04
#define ENLQIOVERI_LQ   0x02
#define NEXT_QUEUED_SCB_ADDR 0x124
#define LONGJMP_ADDR    0xf8
#define SHVALIDSTDIS    0x02
#define ENLQOATNPKT     0x02
#define SCBAUTOPTR      0xab
#define ENLQIPHASE_NLQ  0x40
#define QFREEZE_COUNT   0x132
#define ENLQICRCI_NLQ   0x08
#define QOUTFIFO_ENTRY_VALID_TAG 0x13d
#define AUSCBPTR_EN     0x80
#define INVALID_ADDR    0x80
#define ENLQICRCI_LQ    0x10
#define LQOSCSCTL       0x5a
#define QOUTFIFO_ENTRY_VALID 0x80
#define ENLQOBUSFREE    0x02
#define CLRSINT3        0x53
#define STIMESEL_MIN    0x18
#define PCIXCTL         0x93
#define OSRAMPERR       0x01
#define ENOVERRUN       0x04
#define INTVEC2_ADDR    0xf6
#define SEQIMODE        0x5c
#define CDBLIMIT        0x31
#define ENLQIPHASE_LQ   0x80
#define ENCFG4TSTAT     0x04
#define ENCFG4TCMD      0x01
#define ABORTPENDING    0x01
#define ENLQOTCRC       0x01
#define CMDPTR          0x28
#define ENNTRAMPERR     0x02
#define SRC_MODE_SHIFT  0x00
#define DST_MODE_SHIFT  0x04
#define CURRFIFO_1      0x01
#define DFFSTAT         0x3f
#define CURRFIFO        0x03
#define SEESTART        0x01
#define SEEOP_READ      0x60
#define SEEADR          0xba
#define SEEDAT          0xbc
#define FLX_ROMSTAT_SEE_NONE 0xF0
#define FLX_ROMSTAT_SEECFG 0xF0
#define VPDBOOTHOST     0x0002
#define VPDMASTERBIOS   0x0001
#define FLX_TERMCTL_ENSECHIGH 0x8
#define FLX_TERMCTL_ENSECLOW 0x4
#define FLX_TERMCTL_ENPRIHIGH 0x2
#define FLX_TERMCTL_ENPRILOW 0x1
#define FLXADDR_TERMCTL 0x0
#define CLRSPLTINT      0x01
#define DCHSPLTSTAT0    0x96
#define PCIXR_STATUS    0x9A
#define SGSPLTSTAT0     0x9e
#define SGSPLTSTAT1     0x9f
#define DCHSPLTSTAT1    0x97
#define SELOID          0x6b
#define REG0            0xa0
#define LQOSTAT0        0x54
#define LQOSTAT1        0x55
#define SCB_FIFO_USE_COUNT 0x190
#define MDFFSTAT        0x5d
#define SCB_NEXT2       0x1ae
#define LQOSTATE        0x4f
#define LQISTAT0        0x50
#define LQISTATE        0x4e
#define PERRDIAG        0x4e
#define NEXTSCB         0x5a
#define LQOSTAT2        0x56
#define LQIN            0x20
#define LQISTAT1        0x51
#define FIFO0FREE       0x10
#define SEQINTSTAT      0x0c
#define CURRSCB         0x5c
#define LASTSCB         0x5e
#define SEQINTSRC       0x5b
#define SOFFCNT         0x4f
#define OS_SPACE_CNT    0x56
#define MAXCMDCNT       0x33
#define SAVED_MODE      0x136
#define SHCNT           0x68
#define LOCAL_HS_MAILBOX 0x157
#define INTCTL          0x18
#define DFFSXFRCTL      0x5a
#define SCSIBUS         0x46
#define BRDEN           0x04
#define BRDDAT          0xb8
#define SAVEPTRS        0x20
#define CFG4DATA        0x10
#define CLRCFG4DATA     0x10
#define SCSIENWRDIS     0x40
#define FETCH_INPROG    0x04
#define LOADING_NEEDED  0x02
#define CLRSAVEPTRS     0x20
#define LQOBUSFREE      0x02
#define BUSFREE_DFF0    0x80
#define CLRLQIINT1      0x51
#define BUSFREE_DFF1    0xc0
#define BUSFREE_LQO     0x40
#define CLRLQICRCI_NLQ  0x08
#define LQIPHASE_NLQ    0x40
#define BUSFREETIME     0xc0
#define CLRLQOINT1      0x55
#define CLRLQOINT0      0x54
#define LQICRCI_NLQ     0x08
#define LQIPHASE_LQ     0x80
#define TRACEPOINT0     0x15
#define INVALID_SEQINT  0x0e
#define SCB_TASK_MANAGEMENT 0x197
#define CFG4ISTAT       0x08
#define DUMP_CARD_STATE 0x0c
#define CLRLQOPHACHGINPKT 0x01
#define ENTERING_NONPACK 0x12
#define TRACEPOINT3     0x18
#define SIU_TASKMGMT_TARGET_RESET 0x20
#define TASKMGMT_CMD_CMPLT_OKAY 0x14
#define CFG4OVERRUN     0x11
#define ILLEGAL_PHASE   0x0d
#define SIU_TASKMGMT_CLEAR_TASK_SET 0x04
#define TRACEPOINT1     0x16
#define SIU_TASKMGMT_ABORT_TASK_SET 0x02
#define SAW_HWERR       0x19
#define SIU_TASKMGMT_LUN_RESET 0x08
#define TASKMGMT_FUNC_COMPLETE 0x13
#define SIU_TASKMGMT_ABORT_TASK 0x01
#define LQIPHASE_OUTPKT 0x40
#define TRACEPOINT2     0x17
#define CFG4ISTAT_INTR  0x0f
#define STATUS_OVERRUN  0x10
#define INT_COALESCING_MAXCMDS 0x152
#define INT_COALESCING_MINCMDS 0x153
#define INT_COALESCING_TIMER 0x150
#define PKT_OVERRUN_BUFOFFSET 0x05
#define CACHELINE_MASK  0x07
#define SCB_TRANSFER_SIZE 0x06
#define SCB_TRANSFER_SIZE_1BYTE_LUN 0x30
#define PRGMCNT         0xde
#define SG_PREFETCH_CNT_LIMIT 0x01
#define SESCB_QOFF      0x12
#define ENINT_COALESCE  0x40
#define NEGPERIOD       0x61
#define MSG_EXT_PPR_PCOMP_EN 0x80
#define NEGOFFSET       0x62
#define PPROPT_PACE     0x08
#define NEGPPROPTS      0x63
#define HESCB_QOFF      0x08
#define RCVROFFSTDIS    0x04
#define BYPASSENAB      0x80
#define XMITOFFSTDIS    0x02
#define DSPDATACTL      0xc1
#define SRC_MODE        0x07
#define DST_MODE        0x70
#define CLRSHCNT        0x04
#define RSTCHN          0x01
#define ENARBO          0x20
#define CLRLQIATNQAS    0x20
#define CLRLQOTCRC      0x01
#define CLRLQIATNCMD    0x01
#define CLRLQICRCT2     0x08
#define CLRLQIATNLQ     0x02
#define CLRLQOTARGSCBPERR 0x10
#define CLRLQIBADLQI    0x04
#define CLRLQIINT0      0x50
#define CLRLQIBADLQT    0x04
#define CLRLQOBUSFREE   0x02
#define CLROVERRUN      0x04
#define CLRLQIOVERI_LQ  0x02
#define CLRLQOSTOPI2    0x08
#define CLRLIQABORT     0x20
#define CLRLQIPHASE_NLQ 0x40
#define CLRLQOATNLQ     0x04
#define CLRLQICRCT1     0x10
#define CLRLQICRCI_LQ   0x10
#define CLRLQIOVERI_NLQ 0x01
#define CLRLQOINITSCBPERR 0x10
#define CLRLQOSTOPT2    0x08
#define CLRNONPACKREQ   0x20
#define CLROSRAMPERR    0x01
#define CLRNTRAMPERR    0x02
#define CLRLQOBADQAS    0x04
#define CLRLQOATNPKT    0x02
#define CLRLQIPHASE_LQ  0x80
#define SEEBUSY         0x02
#define SEEARBACK       0x04
#define SEESTAT         0xbe
#define PENDING_MK_MESSAGE 0x01
#define FLXARBACK       0x80
#define DSCTMOUT        0x02
#define PARITYERR       0x10
#define LQOTOIDLE       0x02
#define LQCTL2          0x39
#define LQIRETRY        0x80
#define DLZERO          0x04
#define PREVPHASE       0x20
#define LQIOVERI_NLQ    0x01
#define LQIOVERI_LQ     0x02
#define LQIBADLQI       0x04
#define LQICRCI_LQ      0x10
#define SCSIDAT         0x44
#define CONT_MSG_LOOP_READ 0x03
#define CONT_MSG_LOOP_TARG 0x02
#define CONT_MSG_LOOP_WRITE 0x04
#define FIFOFREE        0x01
#define CURRFIFO_0      0x00
#define MSG_IDENTIFYFLAG 0x80
#define MSG_IDENTIFY_DISCFLAG 0x40
#define SCB_TASK_ATTRIBUTE 0x195
#define SG_OVERRUN_RESID 0x02
#define STATUS_PKT_SENSE 0xff
#define PACKETIZED      0x80
#define SCB_DISCONNECTED_LISTS 0x1b8

/* Additional register to provide chip ID and revision */
#define SSTAT0          0x00

#define AHD_CHIPID_MASK  0x0F
#define AHD_REVISION_MASK 0xF0

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
    uint8_t regs[0x200];

    /* DMA Context */
    dma_addr_t dma_addr;
    uint32_t dma_len;

    uint8_t power_state;
};

/* Hardware structures used by the driver */
struct ahd_dma_seg {
    uint32_t    addr;
    uint32_t    len;
};

struct ahd_dma64_seg {
    uint64_t    addr;
    uint32_t    len;
    uint32_t    pad;
};

struct ahd_completion {
    uint16_t    tag;
    uint8_t     sg_status;
    uint8_t     valid_tag;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t pending = s->intr_status & s->intr_mask;

    if (pending) {
        if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!msi_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
        /* MSI cannot be lowered, but will be re-raised if needed later */
    }
}

/* MMIO/PIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr < sizeof(s->regs)) {
        switch (size) {
        case 1:
            val = s->regs[addr];
            break;
        case 2:
            val = lduw_le_p(&s->regs[addr]);
            break;
        case 4:
            val = ldl_le_p(&s->regs[addr]);
            break;
        case 8:
            val = ldq_le_p(&s->regs[addr]);
            break;
        default:
            qemu_log_mask(LOG_UNIMP, "aic79xx: unsupported MMIO read size %u\n", size);
            break;
        }
    } else {
        qemu_log_mask(LOG_UNIMP, "aic79xx: MMIO read out of bounds at 0x%" HWADDR_PRIx "\n", addr);
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < sizeof(s->regs)) {
        switch (size) {
        case 1:
            s->regs[addr] = val & 0xff;
            break;
        case 2:
            stw_le_p(&s->regs[addr], val);
            break;
        case 4:
            stl_le_p(&s->regs[addr], val);
            break;
        case 8:
            stq_le_p(&s->regs[addr], val);
            break;
        default:
            qemu_log_mask(LOG_UNIMP, "aic79xx: unsupported MMIO write size %u\n", size);
            return;
        }

        /* Handle special interrupt registers */
        if (addr == SEQINTSTAT) {
            /* W1C: clear bits written as 1 */
            s->intr_status &= ~(uint8_t)val;
            pcibase_update_irq(s);
        } else if (addr == SEQINTSRC) {
            /* W1C */
            s->intr_status &= ~((uint8_t)val & 0x7f);
            pcibase_update_irq(s);
        } else if (addr == CLRSEQINTSRC) {
            s->intr_status = 0;
            pcibase_update_irq(s);
        } else if (addr == INTCTL) {
            /* INTCTL write updates interrupt mask */
            s->intr_mask = (val & 0x80) ? 0xff : 0x00;
            pcibase_update_irq(s);
        }
    } else {
        qemu_log_mask(LOG_UNIMP, "aic79xx: MMIO write out of bounds at 0x%" HWADDR_PRIx "\n", addr);
    }
}

static MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

/* PIO handlers for BAR0 (channel A registers 0x00-0xFF) */
static uint64_t pcibase_pio_bar0_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr < 0x100) {
        switch (size) {
        case 1:
            val = s->regs[addr];
            break;
        case 2:
            val = lduw_le_p(&s->regs[addr]);
            break;
        case 4:
            val = ldl_le_p(&s->regs[addr]);
            break;
        default:
            qemu_log_mask(LOG_UNIMP, "aic79xx: unsupported PIO read size %u\n", size);
            return 0;
        }
    } else {
        qemu_log_mask(LOG_UNIMP, "aic79xx: PIO BAR0 read out of bounds at 0x%" HWADDR_PRIx "\n", addr);
    }
    return val;
}

static void pcibase_pio_bar0_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < 0x100) {
        switch (size) {
        case 1:
            s->regs[addr] = val & 0xff;
            break;
        case 2:
            stw_le_p(&s->regs[addr], val);
            break;
        case 4:
            stl_le_p(&s->regs[addr], val);
            break;
        default:
            qemu_log_mask(LOG_UNIMP, "aic79xx: unsupported PIO write size %u\n", size);
            return;
        }

        /* Handle special interrupt registers (same logic as MMIO) */
        if (addr == SEQINTSTAT) {
            s->intr_status &= ~(uint8_t)val;
            pcibase_update_irq(s);
        } else if (addr == SEQINTSRC) {
            s->intr_status &= ~((uint8_t)val & 0x7f);
            pcibase_update_irq(s);
        } else if (addr == CLRSEQINTSRC) {
            s->intr_status = 0;
            pcibase_update_irq(s);
        } else if (addr == INTCTL) {
            s->intr_mask = (val & 0x80) ? 0xff : 0x00;
            pcibase_update_irq(s);
        }
    } else {
        qemu_log_mask(LOG_UNIMP, "aic79xx: PIO BAR0 write out of bounds at 0x%" HWADDR_PRIx "\n", addr);
    }
}

static const MemoryRegionOps pcibase_pio_bar0_ops = {
    .read = pcibase_pio_bar0_read,
    .write = pcibase_pio_bar0_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* PIO handlers for BAR2 (channel B registers 0x100-0x1FF) */
static uint64_t pcibase_pio_bar2_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    uint32_t offset = addr + 0x100;

    if (offset < sizeof(s->regs)) {
        switch (size) {
        case 1:
            val = s->regs[offset];
            break;
        case 2:
            val = lduw_le_p(&s->regs[offset]);
            break;
        case 4:
            val = ldl_le_p(&s->regs[offset]);
            break;
        default:
            qemu_log_mask(LOG_UNIMP, "aic79xx: unsupported PIO BAR2 read size %u\n", size);
            return 0;
        }
    } else {
        qemu_log_mask(LOG_UNIMP, "aic79xx: PIO BAR2 read out of bounds at 0x%" HWADDR_PRIx "\n", addr);
    }
    return val;
}

static void pcibase_pio_bar2_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t offset = addr + 0x100;

    if (offset < sizeof(s->regs)) {
        switch (size) {
        case 1:
            s->regs[offset] = val & 0xff;
            break;
        case 2:
            stw_le_p(&s->regs[offset], val);
            break;
        case 4:
            stl_le_p(&s->regs[offset], val);
            break;
        default:
            qemu_log_mask(LOG_UNIMP, "aic79xx: unsupported PIO BAR2 write size %u\n", size);
            return;
        }

        /* No interrupt registers in channel B typically */
    } else {
        qemu_log_mask(LOG_UNIMP, "aic79xx: PIO BAR2 write out of bounds at 0x%" HWADDR_PRIx "\n", addr);
    }
}

static const MemoryRegionOps pcibase_pio_bar2_ops = {
    .read = pcibase_pio_bar2_read,
    .write = pcibase_pio_bar2_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Clear all register shadows and interrupt state */
    memset(s->regs, 0, sizeof(s->regs));
    s->intr_status = 0;
    s->intr_mask = 0;
    s->power_state = 0;

    /* Set hardware-default values for registers that the driver expects on reset.
     * These provide a supported chip identity (AIC-7901 revision B0). */
    s->regs[SSTAT0] = 0x21;      /* SSTAT0: chip ID = 1 (AIC-7901), revision = 2 (B0) */
    s->regs[PCIXCTL] = 0x21;     /* PCIXCTL: consistent with SSTAT0 */
    s->regs[SEQCTL0] = 0x00;
    s->regs[SCSISEQ0] = 0x00;
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;
    MemoryRegion *mr;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x10);  /* Set PCI revision to A4 */
    pci_set_word(pci_conf + PCI_SUBSYSTEM_VENDOR_ID, 0x9005);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_ID, 0x8000);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* MSI support */
    if (msi_init(pdev, 0, 1, true, false, errp)) {
        error_report("aic79xx: msi_init failed");
        return;
    }

    /* BAR0: PIO channel A registers (256 bytes) */
    mr = &s->bar_regions[0];
    memory_region_init_io(mr, OBJECT(s), &pcibase_pio_bar0_ops, s,
                          "aic79xx-bar0", 256);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_IO, mr);

    /* BAR1: MMIO registers (4KB) */
    mr = &s->bar_regions[1];
    memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s,
                          "aic79xx-bar1", 0x1000);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);

    /* BAR2: PIO channel B registers (256 bytes) */
    mr = &s->bar_regions[2];
    memory_region_init_io(mr, OBJECT(s), &pcibase_pio_bar2_ops, s,
                          "aic79xx-bar2", 256);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_IO, mr);

    s->power_state = 0;
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
    .name = "aic79xx_pci",
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
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
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