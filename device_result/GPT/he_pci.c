/*
 * QEMU PCI device model for FORE HE ATM adapter
 * Combined Phase 1 (structure) + Phase 2 (behavior)
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

#define TYPE_PCIBASE_DEVICE "he_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI identification extracted from he_pci_tbl and PCI_DEVICE_ID_FORE_HE */
#define HE_PCI_VENDOR_ID   0x1127
#define HE_PCI_DEVICE_ID   0x0400
#define HE_PCI_CLASS_ID    0x0280

/* Key register offsets and sizes from driver */
#define HE_REG_RESET_CNTL      0x80000
#define HE_REG_HOST_CNTL       0x80004
#define HE_REG_LB_SWAP         0x80008
#define HE_REG_INT_FIFO        0x8001c
#define HE_REG_ABORT_ADDR      0x80020
#define HE_REG_SDRAM_CTL       0x80018

#define HE_REG_IRQ0_BASE       0x80080
#define HE_REG_IRQ0_HEAD       0x80084
#define HE_REG_IRQ0_CNTL       0x80088
#define HE_REG_IRQ0_DATA       0x8008c
#define HE_REG_IRQ1_BASE       0x80090
#define HE_REG_IRQ1_HEAD       0x80094
#define HE_REG_IRQ1_CNTL       0x80098
#define HE_REG_IRQ1_DATA       0x8009c
#define HE_REG_IRQ2_BASE       0x800a0
#define HE_REG_IRQ2_HEAD       0x800a4
#define HE_REG_IRQ2_CNTL       0x800a8
#define HE_REG_IRQ2_DATA       0x800ac
#define HE_REG_IRQ3_BASE       0x800b0
#define HE_REG_IRQ3_HEAD       0x800b4
#define HE_REG_IRQ3_CNTL       0x800b8
#define HE_REG_IRQ3_DATA       0x800bc

#define HE_REG_GRP_10_MAP      0x800c0
#define HE_REG_GRP_32_MAP      0x800c4
#define HE_REG_GRP_54_MAP      0x800c8
#define HE_REG_GRP_76_MAP      0x800cc

#define HE_REG_GEN_CNTL_0      0x00040
#define HE_REG_PROD_ID         0x00008
#define HE_REG_MEDIA           0x0003e

#define HE_REG_RH_CONFIG       0x805c0
#define HE_REG_RXTHRSH         0x806f0
#define HE_REG_LITHRSH         0x806f4

#define HE_REG_LB_CONFIG       0x807f4
#define HE_REG_HSP_BA          0x807f0
#define HE_REG_CON_DAT         0x807f8
#define HE_REG_CON_CTL         0x807fc

#define HE_REG_TPD_BA          0x80750
#define HE_REG_TSRB_BA         0x80744
#define HE_REG_TSRC_BA         0x80748
#define HE_REG_TSRD_BA         0x80758
#define HE_REG_TMABR_BA        0x8074c
#define HE_REG_RCMRSRB_BA      0x80784
#define HE_REG_RCMABR_BA       0x8078c
#define HE_REG_RCMCONFIG       0x80780
#define HE_REG_SDRAMCON        0x80704
#define HE_REG_LBARB           0x80700
#define HE_REG_TCMCONFIG       0x80740
#define HE_REG_TX_CONFIG       0x80760
#define HE_REG_TXAAL5_PROTO    0x80764
#define HE_REG_TX_CONFIG_2     0x80760
#define HE_REG_RC_CONFIG       0x807c0
#define HE_REG_MCC             0x807c4
#define HE_REG_OEC             0x807c8
#define HE_REG_DCC             0x807cc
#define HE_REG_CEC             0x807d0
#define HE_REG_RCC_STAT        0x8070c

#define HE_HE_REGMAP_SIZE      0x100000

#define HE_BAR_INDEX_MMIO      0
#define HE_BAR_MMIO_SIZE       HE_HE_REGMAP_SIZE

/* Interrupt-related bit definitions (subset used during init/probe) */
#define HE_INT_PROC_ENBL       (1u << 25)
#define HE_PHY_INT_ENB         (1u << 10)
#define HE_PERR_INT_ENB        (1u << 2)
#define HE_INT_CLEAR_A         (1u << 8)

/* Reset and configuration bits */
#define HE_BOARD_RST_STATUS    (1u << 6)
#define HE_INIT_ENB            (1u << 2)
#define HE_TX_ENABLE           (1u << 28)
#define HE_RX_ENABLE           (1u << 8)
#define HE_ENBL_64             (1u << 0)
#define HE_BIG_ENDIAN_HOST     (1u << 14)
#define HE_INTSWAP             (1u << 17)
#define HE_DESC_RD_SWAP        (1u << 19)
#define HE_DESC_WR_SWAP        (1u << 16)
#define HE_DATA_RD_SWAP        (1u << 18)
#define HE_DATA_WR_SWAP        (1u << 20)
#define HE_OUTFF_ENB           (1u << 5)
#define HE_CMDFF_ENB           (1u << 4)
#define HE_MRM_ENB             (1u << 4)
#define HE_MRL_ENB             (1u << 5)
#define HE_LB_64_ENB           (1u << 3)

#define HE_CONFIG_RBRQ_SIZE    512
#define HE_CONFIG_TBRQ_SIZE    512
#define HE_CONFIG_RBPL_SIZE    512
#define HE_CONFIG_RBPL_BUFSIZE 4096
#define HE_CONFIG_TPDRQ_SIZE   512
#define HE_CONFIG_IRQ_SIZE     128

#define HE_IRQ_MASK            ((HE_CONFIG_IRQ_SIZE << 2) - 1)

#define HE_DEV_LABEL           "he"
#define HE_PROD_ID_LEN         30
#define HE_HE_NUM_GROUPS       8
#define HE_HE_NUM_CS_STPER     16
#define HE_HE_MAXCIDBITS       12

#define HE_CONFIG_TSRA         0x00000
#define HE_CONFIG_TSRB         0x08000
#define HE_CONFIG_TSRC         0x0c000
#define HE_CONFIG_TSRD         0x0e000
#define HE_CONFIG_RCMABR       0x0d800
#define HE_CONFIG_TMABR        0x0f000

#define HE_CON_CTL_RCM         (0u << 30)
#define HE_CON_CTL_TCM         (1u << 30)
#define HE_CON_CTL_MBOX        (2u << 30)
#define HE_CON_CTL_BUSY        (1u << 28)
#define HE_CON_CTL_WRITE       (1u << 29)
#define HE_CON_CTL_READ        (0u << 29)
#define HE_CON_BYTE_DISABLE_0  (1u << 19)
#define HE_CON_BYTE_DISABLE_1  (1u << 20)
#define HE_CON_BYTE_DISABLE_2  (1u << 21)

#define HE_CONFIG_RBRQ_THRESH  400
#define HE_CONFIG_TBRQ_THRESH  400
#define HE_CONFIG_RBPL_THRESH  64

/* PCI config / GEN_CNTL_0 related bits from driver */
#define HE_GEN_CNTL_0_MRM_ENB      (1u << 4)
#define HE_GEN_CNTL_0_MRL_ENB      (1u << 5)
#define HE_GEN_CNTL_0_IGNORE_TIMEOUT (1u << 1)
#define HE_GEN_CNTL_0_PCI_BUS_SIZE64 (1u << 27)


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

    struct {
        uint32_t reset_cntl;
        uint32_t host_cntl;
        uint32_t lb_swap;
        uint32_t sdram_ctl;
        uint32_t gen_cntl_0;
        uint32_t prod_id;
        uint32_t media;
        uint32_t rh_config;
        uint32_t rxthrsh;
        uint32_t lithrsh;
        uint32_t lb_config;
        uint32_t hsp_ba;
        uint32_t con_dat;
        uint32_t con_ctl;
        uint32_t tpd_ba;
        uint32_t tsrb_ba;
        uint32_t tsrd_ba;
        uint32_t tsrc_ba;
        uint32_t tmabr_ba;
        uint32_t rcmrsrb_ba;
        uint32_t rcmabr_ba;
        uint32_t rcmconfig;
        uint32_t sdramcon;
        uint32_t lbarb;
        uint32_t tcmconfig;
        uint32_t tx_config;
        uint32_t txaal5_proto;
        uint32_t rc_config;
        uint32_t mcc;
        uint32_t oec;
        uint32_t dcc;
        uint32_t cec;
        uint32_t rcc_stat;
        uint32_t irq0_base;
        uint32_t irq0_head;
        uint32_t irq0_cntl;
        uint32_t irq0_data;
        uint32_t irq1_base;
        uint32_t irq1_head;
        uint32_t irq1_cntl;
        uint32_t irq1_data;
        uint32_t irq2_base;
        uint32_t irq2_head;
        uint32_t irq2_cntl;
        uint32_t irq2_data;
        uint32_t irq3_base;
        uint32_t irq3_head;
        uint32_t irq3_cntl;
        uint32_t irq3_data;
        uint32_t grp_10_map;
        uint32_t grp_32_map;
        uint32_t grp_54_map;
        uint32_t grp_76_map;
        uint32_t int_fifo;
        uint32_t abort_addr;
    } regs;

    struct {
        uint64_t tpdrq_ba;
        uint64_t rbrq_ba;
        uint64_t rbpl_ba;
        uint64_t tbrq_ba;
        uint64_t hsp_ba;
    } dma;

    uint32_t status_flags;
    uint32_t reset_state;
    uint8_t pm_state;
};

/* Forward declaration of internal helpers */
static void pcibase_update_irq(PCIBaseState *s);

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    if (s->intr_status) {
        /* Legacy INTx only; driver uses request_irq on PCI line */
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val32 = 0;

    if (size != 4) {
        return 0;
    }

    switch (addr) {
    case HE_REG_RESET_CNTL:
        /* driver checks BOARD_RST_STATUS after reset */
        val32 = s->regs.reset_cntl;
        break;
    case HE_REG_HOST_CNTL:
        val32 = s->regs.host_cntl;
        break;
    case HE_REG_LB_SWAP:
        val32 = s->regs.lb_swap;
        break;
    case HE_REG_SDRAM_CTL:
        val32 = s->regs.sdram_ctl;
        break;
    case HE_REG_INT_FIFO:
        val32 = s->regs.int_fifo;
        break;
    case HE_REG_ABORT_ADDR:
        val32 = s->regs.abort_addr;
        break;

    case HE_REG_IRQ0_BASE:
        val32 = s->regs.irq0_base;
        break;
    case HE_REG_IRQ0_HEAD:
        val32 = s->regs.irq0_head;
        break;
    case HE_REG_IRQ0_CNTL:
        val32 = s->regs.irq0_cntl;
        break;
    case HE_REG_IRQ0_DATA:
        val32 = s->regs.irq0_data;
        break;
    case HE_REG_IRQ1_BASE:
        val32 = s->regs.irq1_base;
        break;
    case HE_REG_IRQ1_HEAD:
        val32 = s->regs.irq1_head;
        break;
    case HE_REG_IRQ1_CNTL:
        val32 = s->regs.irq1_cntl;
        break;
    case HE_REG_IRQ1_DATA:
        val32 = s->regs.irq1_data;
        break;
    case HE_REG_IRQ2_BASE:
        val32 = s->regs.irq2_base;
        break;
    case HE_REG_IRQ2_HEAD:
        val32 = s->regs.irq2_head;
        break;
    case HE_REG_IRQ2_CNTL:
        val32 = s->regs.irq2_cntl;
        break;
    case HE_REG_IRQ2_DATA:
        val32 = s->regs.irq2_data;
        break;
    case HE_REG_IRQ3_BASE:
        val32 = s->regs.irq3_base;
        break;
    case HE_REG_IRQ3_HEAD:
        val32 = s->regs.irq3_head;
        break;
    case HE_REG_IRQ3_CNTL:
        val32 = s->regs.irq3_cntl;
        break;
    case HE_REG_IRQ3_DATA:
        val32 = s->regs.irq3_data;
        break;

    case HE_REG_GRP_10_MAP:
        val32 = s->regs.grp_10_map;
        break;
    case HE_REG_GRP_32_MAP:
        val32 = s->regs.grp_32_map;
        break;
    case HE_REG_GRP_54_MAP:
        val32 = s->regs.grp_54_map;
        break;
    case HE_REG_GRP_76_MAP:
        val32 = s->regs.grp_76_map;
        break;

    case HE_REG_GEN_CNTL_0:
        val32 = s->regs.gen_cntl_0;
        break;
    case HE_REG_PROD_ID:
        val32 = s->regs.prod_id;
        break;
    case HE_REG_MEDIA:
        val32 = s->regs.media;
        break;

    case HE_REG_RH_CONFIG:
        val32 = s->regs.rh_config;
        break;
    case HE_REG_RXTHRSH:
        val32 = s->regs.rxthrsh;
        break;
    case HE_REG_LITHRSH:
        val32 = s->regs.lithrsh;
        break;

    case HE_REG_LB_CONFIG:
        val32 = s->regs.lb_config;
        break;
    case HE_REG_HSP_BA:
        val32 = s->regs.hsp_ba;
        break;
    case HE_REG_CON_DAT:
        val32 = s->regs.con_dat;
        break;
    case HE_REG_CON_CTL:
        val32 = s->regs.con_ctl;
        break;

    case HE_REG_TPD_BA:
        val32 = s->regs.tpd_ba;
        break;
    case HE_REG_TSRB_BA:
        val32 = s->regs.tsrb_ba;
        break;
    case HE_REG_TSRC_BA:
        val32 = s->regs.tsrc_ba;
        break;
    case HE_REG_TSRD_BA:
        val32 = s->regs.tsrd_ba;
        break;
    case HE_REG_TMABR_BA:
        val32 = s->regs.tmabr_ba;
        break;
    case HE_REG_RCMRSRB_BA:
        val32 = s->regs.rcmrsrb_ba;
        break;
    case HE_REG_RCMABR_BA:
        val32 = s->regs.rcmabr_ba;
        break;
    case HE_REG_RCMCONFIG:
        val32 = s->regs.rcmconfig;
        break;
    case HE_REG_SDRAMCON:
        val32 = s->regs.sdramcon;
        break;
    case HE_REG_LBARB:
        val32 = s->regs.lbarb;
        break;
    case HE_REG_TCMCONFIG:
        val32 = s->regs.tcmconfig;
        break;
    case HE_REG_TX_CONFIG:
        val32 = s->regs.tx_config;
        break;
    case HE_REG_TXAAL5_PROTO:
        val32 = s->regs.txaal5_proto;
        break;
    case HE_REG_RC_CONFIG:
        val32 = s->regs.rc_config;
        break;
    case HE_REG_MCC:
        val32 = s->regs.mcc;
        break;
    case HE_REG_OEC:
        val32 = s->regs.oec;
        break;
    case HE_REG_DCC:
        val32 = s->regs.dcc;
        break;
    case HE_REG_CEC:
        val32 = s->regs.cec;
        break;
    case HE_REG_RCC_STAT:
        val32 = s->regs.rcc_stat;
        break;

    default:
        val32 = 0;
        break;
    }

    return val32;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                               unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        return;
    }

    switch (addr) {
    case HE_REG_RESET_CNTL:
        /* reset sequence: write 0 then 0xff then poll BOARD_RST_STATUS */
        s->regs.reset_cntl = (uint32_t)val;
        if (val == 0xff) {
            s->regs.reset_cntl |= HE_BOARD_RST_STATUS;
        }
        break;

    case HE_REG_HOST_CNTL:
        s->regs.host_cntl = (uint32_t)val;
        break;

    case HE_REG_LB_SWAP:
        s->regs.lb_swap = (uint32_t)val;
        break;

    case HE_REG_SDRAM_CTL:
        s->regs.sdram_ctl = (uint32_t)val;
        break;

    case HE_REG_INT_FIFO:
        /* driver writes INT_CLEAR_A to clear interrupts */
        if (val & HE_INT_CLEAR_A) {
            s->intr_status = 0;
            s->regs.int_fifo = 0;
            pcibase_update_irq(s);
        }
        break;

    case HE_REG_ABORT_ADDR:
        s->regs.abort_addr = (uint32_t)val;
        break;

    case HE_REG_IRQ0_BASE:
        s->regs.irq0_base = (uint32_t)val;
        s->dma.rbrq_ba = (uint64_t)val;
        break;
    case HE_REG_IRQ0_HEAD:
        s->regs.irq0_head = (uint32_t)val;
        break;
    case HE_REG_IRQ0_CNTL:
        s->regs.irq0_cntl = (uint32_t)val;
        break;
    case HE_REG_IRQ0_DATA:
        s->regs.irq0_data = (uint32_t)val;
        break;

    case HE_REG_IRQ1_BASE:
        s->regs.irq1_base = (uint32_t)val;
        break;
    case HE_REG_IRQ1_HEAD:
        s->regs.irq1_head = (uint32_t)val;
        break;
    case HE_REG_IRQ1_CNTL:
        s->regs.irq1_cntl = (uint32_t)val;
        break;
    case HE_REG_IRQ1_DATA:
        s->regs.irq1_data = (uint32_t)val;
        break;

    case HE_REG_IRQ2_BASE:
        s->regs.irq2_base = (uint32_t)val;
        break;
    case HE_REG_IRQ2_HEAD:
        s->regs.irq2_head = (uint32_t)val;
        break;
    case HE_REG_IRQ2_CNTL:
        s->regs.irq2_cntl = (uint32_t)val;
        break;
    case HE_REG_IRQ2_DATA:
        s->regs.irq2_data = (uint32_t)val;
        break;

    case HE_REG_IRQ3_BASE:
        s->regs.irq3_base = (uint32_t)val;
        break;
    case HE_REG_IRQ3_HEAD:
        s->regs.irq3_head = (uint32_t)val;
        break;
    case HE_REG_IRQ3_CNTL:
        s->regs.irq3_cntl = (uint32_t)val;
        break;
    case HE_REG_IRQ3_DATA:
        s->regs.irq3_data = (uint32_t)val;
        break;

    case HE_REG_GRP_10_MAP:
        s->regs.grp_10_map = (uint32_t)val;
        break;
    case HE_REG_GRP_32_MAP:
        s->regs.grp_32_map = (uint32_t)val;
        break;
    case HE_REG_GRP_54_MAP:
        s->regs.grp_54_map = (uint32_t)val;
        break;
    case HE_REG_GRP_76_MAP:
        s->regs.grp_76_map = (uint32_t)val;
        break;

    case HE_REG_GEN_CNTL_0:
        /* GEN_CNTL_0 shadow: driver sets MRM_ENB, MRL_ENB, IGNORE_TIMEOUT,
         * PCI_BUS_SIZE64, etc. We simply store the written value; no
         * additional behavior is required for probe/open to succeed.
         */
        s->regs.gen_cntl_0 = (uint32_t)val;
        break;

    case HE_REG_RH_CONFIG:
        s->regs.rh_config = (uint32_t)val;
        break;
    case HE_REG_RXTHRSH:
        s->regs.rxthrsh = (uint32_t)val;
        break;
    case HE_REG_LITHRSH:
        s->regs.lithrsh = (uint32_t)val;
        break;

    case HE_REG_LB_CONFIG:
        s->regs.lb_config = (uint32_t)val;
        break;

    case HE_REG_HSP_BA:
        s->regs.hsp_ba = (uint32_t)val;
        s->dma.hsp_ba = (uint64_t)val;
        break;

    case HE_REG_CON_DAT:
        /* buffered for internal mailbox/connection RAM operations */
        s->regs.con_dat = (uint32_t)val;
        break;

    case HE_REG_CON_CTL:
        /* very simplified: emulate BUSY handshaking, but no actual RAM */
        s->regs.con_ctl = (uint32_t)val;
        /* Clear BUSY immediately so he_{readl,writel}_internal loops end */
        s->regs.con_ctl &= ~HE_CON_CTL_BUSY;
        break;

    case HE_REG_TPD_BA:
        s->regs.tpd_ba = (uint32_t)val;
        s->dma.tpdrq_ba = (uint64_t)val;
        break;
    case HE_REG_TSRB_BA:
        s->regs.tsrb_ba = (uint32_t)val;
        break;
    case HE_REG_TSRC_BA:
        s->regs.tsrc_ba = (uint32_t)val;
        break;
    case HE_REG_TSRD_BA:
        s->regs.tsrd_ba = (uint32_t)val;
        break;
    case HE_REG_TMABR_BA:
        s->regs.tmabr_ba = (uint32_t)val;
        break;
    case HE_REG_RCMRSRB_BA:
        s->regs.rcmrsrb_ba = (uint32_t)val;
        break;
    case HE_REG_RCMABR_BA:
        s->regs.rcmabr_ba = (uint32_t)val;
        break;
    case HE_REG_RCMCONFIG:
        s->regs.rcmconfig = (uint32_t)val;
        break;
    case HE_REG_SDRAMCON:
        s->regs.sdramcon = (uint32_t)val;
        break;
    case HE_REG_LBARB:
        s->regs.lbarb = (uint32_t)val;
        break;
    case HE_REG_TCMCONFIG:
        s->regs.tcmconfig = (uint32_t)val;
        break;
    case HE_REG_TX_CONFIG:
        s->regs.tx_config = (uint32_t)val;
        break;
    case HE_REG_TXAAL5_PROTO:
        s->regs.txaal5_proto = (uint32_t)val;
        break;
    case HE_REG_RC_CONFIG:
        s->regs.rc_config = (uint32_t)val;
        break;
    case HE_REG_MCC:
        s->regs.mcc = (uint32_t)val;
        break;
    case HE_REG_OEC:
        s->regs.oec = (uint32_t)val;
        break;
    case HE_REG_DCC:
        s->regs.dcc = (uint32_t)val;
        break;
    case HE_REG_CEC:
        s->regs.cec = (uint32_t)val;
        break;
    case HE_REG_RCC_STAT:
        s->regs.rcc_stat = (uint32_t)val;
        break;

    default:
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    (void)opaque;
    (void)addr;
    (void)size;
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val,
                              unsigned size)
{
    (void)opaque;
    (void)addr;
    (void)val;
    (void)size;
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
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

    memset(&s->regs, 0, sizeof(s->regs));
    s->intr_status = 0;
    s->intr_mask = 0;
    s->status_flags = 0;
    s->reset_state = 0;
    s->pm_state = 0;

    /* Provide a plausible default for PROD_ID/media so proc_read prints */
    s->regs.prod_id = 0;  /* contents not parsed by driver */
    s->regs.media = 0;    /* MM/SM bit checked, but affects only printk */
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s,
                                 BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s,
                              bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s,
                              bi->name, aligned_size);
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  HE_PCI_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  HE_PCI_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, HE_PCI_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Basic PCI config settings that the driver expects to manipulate:
     * GEN_CNTL_0 is memory-mapped at 0x40; the driver sets MRM_ENB, MRL_ENB,
     * IGNORE_TIMEOUT and PCI_BUS_SIZE64 bits there. PCI command bits like
     * INVALIDATE are handled through standard PCI config space and don't
     * require device-specific emulation here.
     */

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    s->num_bars = 1;
    s->bar_info[0].index = HE_BAR_INDEX_MMIO;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    s->bar_info[0].size  = HE_BAR_MMIO_SIZE;
    s->bar_info[0].name  = "he-mmio";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = NULL;
    }

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = false;
    s->has_msix = false;

    memset(&s->regs, 0, sizeof(s->regs));
    memset(&s->dma, 0, sizeof(s->dma));
    s->intr_status = 0;
    s->intr_mask = 0;
    s->status_flags = 0;
    s->reset_state = 0;
    s->pm_state = 0;
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
    .name = "he_pci",
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
