/*
 * QEMU PCI Device model for Inverse Phase ATM (iphase) adapter
 * Behavioral implementation tailored to linux drivers/atm/iphase.c
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

#define TYPE_PCIBASE_DEVICE "ia_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define IPHASE_PCI_VENDOR_ID   0x107e
#define IPHASE_PCI_DEVICE_ID   0x0008

/* The driver uses a bus control register at REG_BASE + IPHASE5575_BUS_CONTROL_REG */
#define IPHASE5575_BUS_CONTROL_REG_BASE 0x1000
#define IPHASE5575_BUS_CONTROL_REG      0x00

/* Other base regions used by the driver */
#define IPHASE5575_DMA_CONTROL_REG_BASE  0x4000
#define IPHASE5575_FRONT_END_REG_BASE   IPHASE5575_DMA_CONTROL_REG_BASE
#define IPHASE5575_FRAG_CONTROL_REG_BASE 0x2000
#define IPHASE5575_REASS_CONTROL_REG_BASE 0x3000

/* Fragment control RAM base explicitly provided by driver */
#define IPHASE5575_FRAG_CONTROL_RAM_BASE 0x10000

/* Additional base macros from driver */
#define REG_BASE   IPHASE5575_BUS_CONTROL_REG_BASE
#define PHY_BASE   IPHASE5575_FRONT_END_REG_BASE
#define SEG_BASE   IPHASE5575_FRAG_CONTROL_REG_BASE
#define REASS_BASE IPHASE5575_REASS_CONTROL_REG_BASE
#define RAM_BASE   IPHASE5575_FRAG_CONTROL_RAM_BASE

/* Register offsets within REG_BASE from driver */
#define IPHASE5575_BUS_STATUS_REG  0x01
#define IPHASE5575_TX_COUNTER      0x200
#define IPHASE5575_RX_COUNTER      0x280
#define IPHASE5575_TX_LIST_ADDR    0x300
#define IPHASE5575_RX_LIST_ADDR    0x380

/* The driver also refers to EXT_RESET, MAC1, MAC2 in REG space */
#define IPHASE5575_EXT_RESET       0x3c
#define IPHASE5575_MAC1            0x40
#define IPHASE5575_MAC2            0x44

/* Example control bits for the BUS_CONTROL_REG (status/mask bits) */
#define CTRL_LED        0x40000000
#define CTRL_B32        0x00000040
#define CTRL_SEGMASK    0x00020000
#define CTRL_FE_RST     0x80000000
#define CTRL_DLERMASK   0x00080000
#define CTRL_REASSMASK  0x00010000
#define CTRL_B64        0x00000100
#define CTRL_B48        0x00000080
#define CTRL_DLETMASK   0x00100000
#define CTRL_B128       0x00000200
#define CTRL_B16        0x00000020
#define CTRL_FEMASK     0x00040000
#define CTRL_CSPREEMPT  0x00002000
#define CTRL_B8         0x00000010
#define CTRL_ERRMASK    0x00400000

/* Interrupt status bits used by the driver (BUS_STATUS_REG) */
#define STAT_REASSINT   0x00000001
#define STAT_SEGINT     0x00000002
#define STAT_FEINT      0x00000004
#define STAT_DLERINT    0x00000008
#define STAT_DLETINT    0x00000010
#define STAT_MARKINT    0x00000020
#define STAT_ERRINT     0x00000040

/* Class: the card is an ATM adapter */
#define IPHASE_PCI_CLASS_ID PCI_CLASS_NETWORK_ATM

/* Segmentation control register offsets used in driver (SEG_BASE) */
#define SEG_MASK_REG            0x00  /* used with readw/writew */
#define SEG_COMMAND_REG         0x02
#define MODE_REG_0              0x04
#define MODE_REG_1              0x06
#define SEG_INTR_STATUS_REG     0x08
#define SEG_QUEUE_BASE          0x0a
#define TCQ_ST_ADR              0x0c
#define TCQ_RD_PTR              0x0e
#define TCQ_WR_PTR              0x10
#define TCQ_ED_ADR              0x12
#define PRQ_ST_ADR              0x14
#define PRQ_RD_PTR              0x16
#define PRQ_WR_PTR              0x18
#define PRQ_ED_ADR              0x1a
#define CBR_PTR_BASE            0x1c
#define CBR_TAB_BEG             0x1e
#define CBR_TAB_END             0x20
#define MAXRATE                 0x22
#define STPARMS                 0x24
#define IDLEHEADHI              0x26
#define IDLEHEADLO              0x28
#define ABRUBR_ARB              0x2a
#define RM_TYPE                 0x2c

/* Reassembly control register offsets (REASS_BASE) */
#define REASS_INTR_STATUS_REG   0x00
#define REASS_MASK_REG          0x02
#define MODE_REG                0x04
#define REASS_COMMAND_REG       0x06
#define REASS_DESC_BASE         0x08
#define BUF_SIZE                0x0a
#define REASS_QUEUE_BASE        0x0c
#define FREEQ_ST_ADR            0x0e
#define FREEQ_ED_ADR            0x10
#define FREEQ_RD_PTR            0x12
#define FREEQ_WR_PTR            0x14
#define PCQ_ST_ADR              0x16
#define PCQ_ED_ADR              0x18
#define PCQ_RD_PTR              0x1a
#define PCQ_WR_PTR              0x1c
#define EXCP_Q_ST_ADR           0x1e
#define EXCP_Q_ED_ADR           0x20
#define EXCP_Q_RD_PTR           0x22
#define EXCP_Q_WR_PTR           0x24
#define REASS_TABLE_BASE        0x26
#define VC_LKUP_BASE            0x28
#define ABR_LKUP_BASE           0x2a
#define VP_FILTER               0x2c
#define XTRA_RM_OFFSET          0x2e
#define PROTOCOL_ID             0x30
#define STATE_REG               0x32
#define CELL_CTR0               0x34
#define CELL_CTR1               0x36
#define DRP_PKT_CNTR            0x38
#define ERR_CNTR                0x3a

/* Event bits for segment/reassembly interrupt status (simplified) */
#define TRANSMIT_DONE           0x0001
#define TCQ_NOT_EMPTY           0x0002
#define RX_PKT_RCVD             0x0001
#define RX_FREEQ_EMPT           0x0002
#define RX_EXCP_RCVD            0x0004
#define RX_RAW_RCVD             0x0008

/* Mode bits used in driver */
#define T_ONLINE                0x0001
#define R_ONLINE                0x0001
#define RESET_SEG               0x0001
#define RESET_REASS             0x0001

/* Memory-based table offsets (in RAM, iadev->memSize scaling is in driver,
 * but for MMIO we only need consistency for queue pointers etc.). We choose
 * concrete offsets matching driver names; values themselves are opaque to
 * QEMU as RAM lives in guest memory space. These are only used by driver
 * to compute addresses inside seg_ram/reass_ram, which in hardware are on
 * card. Here we just expose flat RAM window and don't need to interpret them.
 * No behavior in MMIO depends on the exact numeric values, so these defines
 * are present for completeness but unused in device model.
 */
#define TX_DESC_BASE            0x0000
#define TX_PACKET_RAM           0x1000
#define TX_COMP_Q               0x1400
#define PKT_RDY_Q               0x1400
#define MAIN_VC_TABLE           0x8000
#define EXT_VC_TABLE            0x6000
#define UBR_SCHED_TABLE         0x3000
#define UBR_WAIT_Q              0x4000
#define ABR_SCHED_TABLE         0x5000
#define ABR_WAIT_Q              0x5800
#define RX_DESC_BASE            0x0000
#define FREE_BUF_DESC_Q         0x6000
#define PKT_COMP_Q              0x6800
#define EXCEPTION_Q             0x5e00
#define REASS_TABLE             0x7000
#define RX_VC_TABLE             0x7800
#define ABR_VC_TABLE            0x8000

/* Class for BAR types */
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
    uint32_t intr_status; /* mirrors BUS_STATUS_REG */
    uint32_t intr_mask;   /* not used by driver for BUS_STATUS_REG */

    /* Hardware Register Shadows (The 'Identity' of the device) */
    struct {
        uint32_t bus_control;   /* IPHASE5575_BUS_CONTROL_REG */
        uint32_t bus_status;    /* IPHASE5575_BUS_STATUS_REG (low 8 bits used) */
        uint32_t ext_reset;     /* IPHASE5575_EXT_RESET */
        uint32_t mac1;          /* MAC1 register */
        uint32_t mac2;          /* MAC2 register */

        /* DMA-related registers */
        uint32_t tx_list_addr;  /* IPHASE5575_TX_LIST_ADDR */
        uint32_t rx_list_addr;  /* IPHASE5575_RX_LIST_ADDR */
        uint32_t tx_counter;    /* IPHASE5575_TX_COUNTER */
        uint32_t rx_counter;    /* IPHASE5575_RX_COUNTER */

        /* Segmentation engine registers (16-bit in hardware) */
        uint16_t seg_mask_reg;
        uint16_t seg_command_reg;
        uint16_t seg_mode_reg0;
        uint16_t seg_mode_reg1;
        uint16_t seg_intr_status_reg;
        uint16_t seg_queue_base;
        uint16_t tcq_st_adr;
        uint16_t tcq_rd_ptr;
        uint16_t tcq_wr_ptr;
        uint16_t tcq_ed_adr;
        uint16_t prq_st_adr;
        uint16_t prq_rd_ptr;
        uint16_t prq_wr_ptr;
        uint16_t prq_ed_adr;
        uint16_t cbr_ptr_base;
        uint16_t cbr_tab_beg;
        uint16_t cbr_tab_end;
        uint16_t maxrate;
        uint16_t stparms;
        uint16_t idleheadhi;
        uint16_t idleheadlo;
        uint16_t abrubr_arb;
        uint16_t rm_type;

        /* Reassembly engine registers (16-bit in hardware) */
        uint16_t reass_intr_status_reg;
        uint16_t reass_mask_reg;
        uint16_t reass_mode_reg;
        uint16_t reass_command_reg;
        uint16_t reass_desc_base;
        uint16_t buf_size;
        uint16_t reass_queue_base;
        uint16_t freeq_st_adr;
        uint16_t freeq_ed_adr;
        uint16_t freeq_rd_ptr;
        uint16_t freeq_wr_ptr;
        uint16_t pcq_st_adr;
        uint16_t pcq_ed_adr;
        uint16_t pcq_rd_ptr;
        uint16_t pcq_wr_ptr;
        uint16_t excp_q_st_adr;
        uint16_t excp_q_ed_adr;
        uint16_t excp_q_rd_ptr;
        uint16_t excp_q_wr_ptr;
        uint16_t reass_table_base;
        uint16_t vc_lkup_base;
        uint16_t abr_lkup_base;
        uint16_t vp_filter;
        uint16_t xtra_rm_offset;
        uint16_t protocol_id;
        uint16_t state_reg;
        uint16_t cell_ctr0;
        uint16_t cell_ctr1;
        uint16_t drp_pkt_cntr;
        uint16_t err_cntr;
    } regs;

};


/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* The driver uses a single shared INTx line. BUS_STATUS_REG encodes
     * pending interrupt sources in bits 0..6. When non-zero we assert the
     * line; when all bits clear we deassert it.
     */
    if (s->regs.bus_status & 0x7f) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}


/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    /* All MMIO accesses in the driver use 16-bit readw or 32-bit readl. */
    if (size != 1 && size != 2 && size != 4) {
        return 0xffffffffULL;
    }

    /* BAR0 is a flat region; driver subregions: */
    if (addr >= REG_BASE && addr < REG_BASE + 0x4000) {
        /* Bus interface control region */
        hwaddr off = addr - REG_BASE;
        switch (off) {
        case IPHASE5575_BUS_CONTROL_REG:
            if (size == 4) {
                return s->regs.bus_control;
            }
            break;
        case IPHASE5575_BUS_STATUS_REG:
            /* BUS_STATUS_REG is read as 32-bit in driver but only low 8 bits
             * are used. Also: it is *read-to-clear* for any bits that the
             * driver handles by calling per-source handlers.
             */
            if (size == 4) {
                uint32_t val = s->regs.bus_status & 0x7f;
                /* Clear all bits on read, matching polling loop semantics.
                 * Individual handlers also clear their bits explicitly, but
                 * read in ia_int() is the primary event source.
                 */
                s->regs.bus_status = 0;
                pcibase_update_irq(s);
                return val;
            }
            break;
        case IPHASE5575_EXT_RESET:
            if (size == 4) {
                return s->regs.ext_reset;
            }
            break;
        case IPHASE5575_MAC1:
            if (size == 4) {
                return s->regs.mac1;
            }
            break;
        case IPHASE5575_MAC2:
            if (size == 4) {
                return s->regs.mac2;
            }
            break;
        case IPHASE5575_TX_COUNTER:
            if (size == 4) {
                return s->regs.tx_counter;
            }
            break;
        case IPHASE5575_RX_COUNTER:
            if (size == 4) {
                return s->regs.rx_counter;
            }
            break;
        case IPHASE5575_TX_LIST_ADDR:
            if (size == 4) {
                return s->regs.tx_list_addr;
            }
            break;
        case IPHASE5575_RX_LIST_ADDR:
            if (size == 4) {
                return s->regs.rx_list_addr;
            }
            break;
        default:
            break;
        }
    } else if (addr >= SEG_BASE && addr < SEG_BASE + 0x1000) {
        /* Segmentation control registers (16-bit accesses) */
        hwaddr off = addr - SEG_BASE;
        switch (off) {
        case SEG_MASK_REG:
            if (size == 2) return s->regs.seg_mask_reg;
            break;
        case SEG_COMMAND_REG:
            if (size == 2) return s->regs.seg_command_reg;
            break;
        case MODE_REG_0:
            if (size == 2) return s->regs.seg_mode_reg0;
            break;
        case MODE_REG_1:
            if (size == 2) return s->regs.seg_mode_reg1;
            break;
        case SEG_INTR_STATUS_REG:
            if (size == 2) {
                uint16_t v = s->regs.seg_intr_status_reg;
                /* Read to clear semantics used in driver */
                s->regs.seg_intr_status_reg = 0;
                return v;
            }
            break;
        case SEG_QUEUE_BASE:
            if (size == 2) return s->regs.seg_queue_base;
            break;
        case TCQ_ST_ADR:
            if (size == 2) return s->regs.tcq_st_adr;
            break;
        case TCQ_RD_PTR:
            if (size == 2) return s->regs.tcq_rd_ptr;
            break;
        case TCQ_WR_PTR:
            if (size == 2) return s->regs.tcq_wr_ptr;
            break;
        case TCQ_ED_ADR:
            if (size == 2) return s->regs.tcq_ed_adr;
            break;
        case PRQ_ST_ADR:
            if (size == 2) return s->regs.prq_st_adr;
            break;
        case PRQ_RD_PTR:
            if (size == 2) return s->regs.prq_rd_ptr;
            break;
        case PRQ_WR_PTR:
            if (size == 2) return s->regs.prq_wr_ptr;
            break;
        case PRQ_ED_ADR:
            if (size == 2) return s->regs.prq_ed_adr;
            break;
        case CBR_PTR_BASE:
            if (size == 2) return s->regs.cbr_ptr_base;
            break;
        case CBR_TAB_BEG:
            if (size == 2) return s->regs.cbr_tab_beg;
            break;
        case CBR_TAB_END:
            if (size == 2) return s->regs.cbr_tab_end;
            break;
        case CBR_TAB_END + 1:
            /* driver reads CBR_PTR at CBR_TAB_END+1, we mirror TAB_END */
            if (size == 2) return s->regs.cbr_tab_end;
            break;
        case MAXRATE:
            if (size == 2) return s->regs.maxrate;
            break;
        case STPARMS:
            if (size == 2) return s->regs.stparms;
            break;
        case IDLEHEADHI:
            if (size == 2) return s->regs.idleheadhi;
            break;
        case IDLEHEADLO:
            if (size == 2) return s->regs.idleheadlo;
            break;
        case ABRUBR_ARB:
            if (size == 2) return s->regs.abrubr_arb;
            break;
        case RM_TYPE:
            if (size == 2) return s->regs.rm_type;
            break;
        default:
            break;
        }
    } else if (addr >= REASS_BASE && addr < REASS_BASE + 0x1000) {
        hwaddr off = addr - REASS_BASE;
        switch (off) {
        case REASS_INTR_STATUS_REG:
            if (size == 2) {
                uint16_t v = s->regs.reass_intr_status_reg;
                /* read to clear semantics */
                s->regs.reass_intr_status_reg = 0;
                return v;
            }
            break;
        case REASS_MASK_REG:
            if (size == 2) return s->regs.reass_mask_reg;
            break;
        case MODE_REG:
            if (size == 2) return s->regs.reass_mode_reg;
            break;
        case REASS_COMMAND_REG:
            if (size == 2) return s->regs.reass_command_reg;
            break;
        case REASS_DESC_BASE:
            if (size == 2) return s->regs.reass_desc_base;
            break;
        case BUF_SIZE:
            if (size == 2) return s->regs.buf_size;
            break;
        case REASS_QUEUE_BASE:
            if (size == 2) return s->regs.reass_queue_base;
            break;
        case FREEQ_ST_ADR:
            if (size == 2) return s->regs.freeq_st_adr;
            break;
        case FREEQ_ED_ADR:
            if (size == 2) return s->regs.freeq_ed_adr;
            break;
        case FREEQ_RD_PTR:
            if (size == 2) return s->regs.freeq_rd_ptr;
            break;
        case FREEQ_WR_PTR:
            if (size == 2) return s->regs.freeq_wr_ptr;
            break;
        case PCQ_ST_ADR:
            if (size == 2) return s->regs.pcq_st_adr;
            break;
        case PCQ_ED_ADR:
            if (size == 2) return s->regs.pcq_ed_adr;
            break;
        case PCQ_RD_PTR:
            if (size == 2) return s->regs.pcq_rd_ptr;
            break;
        case PCQ_WR_PTR:
            if (size == 2) return s->regs.pcq_wr_ptr;
            break;
        case EXCP_Q_ST_ADR:
            if (size == 2) return s->regs.excp_q_st_adr;
            break;
        case EXCP_Q_ED_ADR:
            if (size == 2) return s->regs.excp_q_ed_adr;
            break;
        case EXCP_Q_RD_PTR:
            if (size == 2) return s->regs.excp_q_rd_ptr;
            break;
        case EXCP_Q_WR_PTR:
            if (size == 2) return s->regs.excp_q_wr_ptr;
            break;
        case REASS_TABLE_BASE:
            if (size == 2) return s->regs.reass_table_base;
            break;
        case VC_LKUP_BASE:
            if (size == 2) return s->regs.vc_lkup_base;
            break;
        case ABR_LKUP_BASE:
            if (size == 2) return s->regs.abr_lkup_base;
            break;
        case VP_FILTER:
            if (size == 2) return s->regs.vp_filter;
            break;
        case XTRA_RM_OFFSET:
            if (size == 2) return s->regs.xtra_rm_offset;
            break;
        case PROTOCOL_ID:
            if (size == 2) return s->regs.protocol_id;
            break;
        case STATE_REG:
            if (size == 2) return s->regs.state_reg;
            break;
        case CELL_CTR0:
            if (size == 2) return s->regs.cell_ctr0;
            break;
        case CELL_CTR1:
            if (size == 2) return s->regs.cell_ctr1;
            break;
        case DRP_PKT_CNTR:
            if (size == 2) return s->regs.drp_pkt_cntr;
            break;
        case ERR_CNTR:
            if (size == 2) return s->regs.err_cntr;
            break;
        default:
            break;
        }
    } else if (addr >= PHY_BASE && addr < PHY_BASE + 0x1000) {
        /* Front-end/PHY registers: driver treats as 32-bit words at
         * ia->phy + (reg >> 2). Our model only needs to be readable and
         * writable; we maintain a simple 4KB array of 32-bit words.
         */
        /* we implement them via a simple dummy area; return 0. */
        return 0;
    }

    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 1 && size != 2 && size != 4) {
        return;
    }

    if (addr >= REG_BASE && addr < REG_BASE + 0x4000) {
        hwaddr off = addr - REG_BASE;
        switch (off) {
        case IPHASE5575_BUS_CONTROL_REG:
            if (size == 4) {
                /* Pure shadow; LED bit toggling is observed by timer */
                s->regs.bus_control = (uint32_t)val;
            }
            break;
        case IPHASE5575_BUS_STATUS_REG:
            if (size == 4) {
                uint32_t w = (uint32_t)val;
                /* The driver writes STAT_DLERINT/STAT_DLETINT bits to clear
                 * DLE-related interrupt sources (W1C behavior). Only two
                 * bits are explicitly cleared this way.
                 */
                if (w & STAT_DLERINT) {
                    s->regs.bus_status &= ~STAT_DLERINT;
                }
                if (w & STAT_DLETINT) {
                    s->regs.bus_status &= ~STAT_DLETINT;
                }
                pcibase_update_irq(s);
            }
            break;
        case IPHASE5575_EXT_RESET:
            if (size == 4) {
                s->regs.ext_reset = (uint32_t)val;
                /* The driver writes 0 then restores PCI config space via
                 * reset_sar(). We do not emulate side effects beyond
                 * storing the value.
                 */
            }
            break;
        case IPHASE5575_MAC1:
            if (size == 4) {
                s->regs.mac1 = (uint32_t)val;
            }
            break;
        case IPHASE5575_MAC2:
            if (size == 4) {
                s->regs.mac2 = (uint32_t)val;
            }
            break;
        case IPHASE5575_TX_LIST_ADDR:
            if (size == 4) {
                s->regs.tx_list_addr = (uint32_t)val;
            }
            break;
        case IPHASE5575_RX_LIST_ADDR:
            if (size == 4) {
                s->regs.rx_list_addr = (uint32_t)val;
            }
            break;
        case IPHASE5575_TX_COUNTER:
            if (size == 4) {
                /* Driver writes 2 to kick TX DLE processing; we just
                 * record the value and (optionally) raise a DLE TX
                 * interrupt to satisfy tx_dle_intr() expectations.
                 */
                s->regs.tx_counter += (uint32_t)val;
                /* Signal DLET interrupt to host */
                s->regs.bus_status |= STAT_DLETINT;
                pcibase_update_irq(s);
            }
            break;
        case IPHASE5575_RX_COUNTER:
            if (size == 4) {
                s->regs.rx_counter += (uint32_t)val;
                /* Signal DLER interrupt for RX DLE completion */
                s->regs.bus_status |= STAT_DLERINT;
                pcibase_update_irq(s);
            }
            break;
        default:
            break;
        }
    } else if (addr >= SEG_BASE && addr < SEG_BASE + 0x1000) {
        hwaddr off = addr - SEG_BASE;
        switch (off) {
        case SEG_MASK_REG:
            if (size == 2) {
                s->regs.seg_mask_reg = (uint16_t)val;
            }
            break;
        case SEG_COMMAND_REG:
            if (size == 2) {
                s->regs.seg_command_reg = (uint16_t)val;
                /* RESET_SEG is written here; could clear internal state,
                 * but we leave that abstract.
                 */
            }
            break;
        case MODE_REG_0:
            if (size == 2) {
                s->regs.seg_mode_reg0 = (uint16_t)val;
            }
            break;
        case MODE_REG_1:
            if (size == 2) {
                s->regs.seg_mode_reg1 = (uint16_t)val;
            }
            break;
        case SEG_INTR_STATUS_REG:
            if (size == 2) {
                uint16_t w = (uint16_t)val;
                /* The driver writes TRANSMIT_DONE to acknowledge the
                 * transmit-done interrupt. SEG_INTR_STATUS_REG is likely
                 * W1C; emulate that.
                 */
                s->regs.seg_intr_status_reg &= ~w;
                /* Also clear the corresponding SEGINT bit in BUS_STATUS */
                if (w & TRANSMIT_DONE) {
                    s->regs.bus_status &= ~STAT_SEGINT;
                }
                pcibase_update_irq(s);
            }
            break;
        case SEG_QUEUE_BASE:
            if (size == 2) s->regs.seg_queue_base = (uint16_t)val;
            break;
        case TCQ_ST_ADR:
            if (size == 2) s->regs.tcq_st_adr = (uint16_t)val;
            break;
        case TCQ_RD_PTR:
            if (size == 2) s->regs.tcq_rd_ptr = (uint16_t)val;
            break;
        case TCQ_WR_PTR:
            if (size == 2) s->regs.tcq_wr_ptr = (uint16_t)val;
            break;
        case TCQ_ED_ADR:
            if (size == 2) s->regs.tcq_ed_adr = (uint16_t)val;
            break;
        case PRQ_ST_ADR:
            if (size == 2) s->regs.prq_st_adr = (uint16_t)val;
            break;
        case PRQ_RD_PTR:
            if (size == 2) s->regs.prq_rd_ptr = (uint16_t)val;
            break;
        case PRQ_WR_PTR:
            if (size == 2) s->regs.prq_wr_ptr = (uint16_t)val;
            break;
        case PRQ_ED_ADR:
            if (size == 2) s->regs.prq_ed_adr = (uint16_t)val;
            break;
        case CBR_PTR_BASE:
            if (size == 2) s->regs.cbr_ptr_base = (uint16_t)val;
            break;
        case CBR_TAB_BEG:
            if (size == 2) s->regs.cbr_tab_beg = (uint16_t)val;
            break;
        case CBR_TAB_END:
            if (size == 2) s->regs.cbr_tab_end = (uint16_t)val;
            break;
        case CBR_TAB_END + 1:
            /* driver writes CBR_PTR at CBR_TAB_END+1; we simply accept */
            if (size == 2) {
                /* no separate shadow; ignore */
            }
            break;
        case MAXRATE:
            if (size == 2) s->regs.maxrate = (uint16_t)val;
            break;
        case STPARMS:
            if (size == 2) s->regs.stparms = (uint16_t)val;
            break;
        case IDLEHEADHI:
            if (size == 2) s->regs.idleheadhi = (uint16_t)val;
            break;
        case IDLEHEADLO:
            if (size == 2) s->regs.idleheadlo = (uint16_t)val;
            break;
        case ABRUBR_ARB:
            if (size == 2) s->regs.abrubr_arb = (uint16_t)val;
            break;
        case RM_TYPE:
            if (size == 2) s->regs.rm_type = (uint16_t)val;
            break;
        default:
            break;
        }
    } else if (addr >= REASS_BASE && addr < REASS_BASE + 0x1000) {
        hwaddr off = addr - REASS_BASE;
        switch (off) {
        case REASS_INTR_STATUS_REG:
            if (size == 2) {
                uint16_t w = (uint16_t)val;
                /* W1C behavior for reassembly interrupt status */
                s->regs.reass_intr_status_reg &= ~w;
                if (w & (RX_PKT_RCVD | RX_FREEQ_EMPT | RX_EXCP_RCVD | RX_RAW_RCVD)) {
                    s->regs.bus_status &= ~STAT_REASSINT;
                }
                pcibase_update_irq(s);
            }
            break;
        case REASS_MASK_REG:
            if (size == 2) s->regs.reass_mask_reg = (uint16_t)val;
            break;
        case MODE_REG:
            if (size == 2) s->regs.reass_mode_reg = (uint16_t)val;
            break;
        case REASS_COMMAND_REG:
            if (size == 2) s->regs.reass_command_reg = (uint16_t)val;
            break;
        case REASS_DESC_BASE:
            if (size == 2) s->regs.reass_desc_base = (uint16_t)val;
            break;
        case BUF_SIZE:
            if (size == 2) s->regs.buf_size = (uint16_t)val;
            break;
        case REASS_QUEUE_BASE:
            if (size == 2) s->regs.reass_queue_base = (uint16_t)val;
            break;
        case FREEQ_ST_ADR:
            if (size == 2) s->regs.freeq_st_adr = (uint16_t)val;
            break;
        case FREEQ_ED_ADR:
            if (size == 2) s->regs.freeq_ed_adr = (uint16_t)val;
            break;
        case FREEQ_RD_PTR:
            if (size == 2) s->regs.freeq_rd_ptr = (uint16_t)val;
            break;
        case FREEQ_WR_PTR:
            if (size == 2) s->regs.freeq_wr_ptr = (uint16_t)val;
            break;
        case PCQ_ST_ADR:
            if (size == 2) s->regs.pcq_st_adr = (uint16_t)val;
            break;
        case PCQ_ED_ADR:
            if (size == 2) s->regs.pcq_ed_adr = (uint16_t)val;
            break;
        case PCQ_RD_PTR:
            if (size == 2) s->regs.pcq_rd_ptr = (uint16_t)val;
            break;
        case PCQ_WR_PTR:
            if (size == 2) s->regs.pcq_wr_ptr = (uint16_t)val;
            break;
        case EXCP_Q_ST_ADR:
            if (size == 2) s->regs.excp_q_st_adr = (uint16_t)val;
            break;
        case EXCP_Q_ED_ADR:
            if (size == 2) s->regs.excp_q_ed_adr = (uint16_t)val;
            break;
        case EXCP_Q_RD_PTR:
            if (size == 2) s->regs.excp_q_rd_ptr = (uint16_t)val;
            break;
        case EXCP_Q_WR_PTR:
            if (size == 2) s->regs.excp_q_wr_ptr = (uint16_t)val;
            break;
        case REASS_TABLE_BASE:
            if (size == 2) s->regs.reass_table_base = (uint16_t)val;
            break;
        case VC_LKUP_BASE:
            if (size == 2) s->regs.vc_lkup_base = (uint16_t)val;
            break;
        case ABR_LKUP_BASE:
            if (size == 2) s->regs.abr_lkup_base = (uint16_t)val;
            break;
        case VP_FILTER:
            if (size == 2) s->regs.vp_filter = (uint16_t)val;
            break;
        case XTRA_RM_OFFSET:
            if (size == 2) s->regs.xtra_rm_offset = (uint16_t)val;
            break;
        case PROTOCOL_ID:
            if (size == 2) s->regs.protocol_id = (uint16_t)val;
            break;
        case STATE_REG:
            if (size == 2) s->regs.state_reg = (uint16_t)val;
            break;
        case CELL_CTR0:
            if (size == 2) s->regs.cell_ctr0 = (uint16_t)val;
            break;
        case CELL_CTR1:
            if (size == 2) s->regs.cell_ctr1 = (uint16_t)val;
            break;
        case DRP_PKT_CNTR:
            if (size == 2) s->regs.drp_pkt_cntr = (uint16_t)val;
            break;
        case ERR_CNTR:
            if (size == 2) s->regs.err_cntr = (uint16_t)val;
            break;
        default:
            break;
        }
    } else if (addr >= PHY_BASE && addr < PHY_BASE + 0x1000) {
        /* PHY/Front-end space; handled by ia_phy_get/put in driver.
         * Our generic implementation does not model individual bits,
         * but we must allow writes to succeed.
         */
        (void)val;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    (void)opaque;
    (void)addr;
    (void)size;
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
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

    memset(&s->regs, 0, sizeof(s->regs));
    s->intr_status = 0;
    s->intr_mask = 0;

    /* Provide a deterministic MAC address so that get_esi() sees
     * a non-zero value and the driver can derive dev->esi[]
     * from IPHASE5575_MAC1/MAC2.
     */
    s->regs.mac1 = 0x00112233;
    s->regs.mac2 = 0x4455;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  IPHASE_PCI_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  IPHASE_PCI_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, IPHASE_PCI_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR Initialization
     * The driver uses a single memory-mapped region (BAR0) with
     * size either 0x100000 or 0x40000, detected via pci_resource_len.
     * To satisfy both, we expose 1 MiB; the guest can request a smaller
     * mapping via PCI sizing.
     */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    s->bar_info[0].size  = 0x100000; /* large enough to cover all regions */
    s->bar_info[0].name  = "iphase-mmio";
    for (int i = 1; i < 6; ++i) {
        s->bar_info[i].index = i;
        s->bar_info[i].type  = BAR_TYPE_NONE;
        s->bar_info[i].size  = 0;
        s->bar_info[i].name  = NULL;
    }

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = false;
    s->has_msix = false;

    memset(&s->regs, 0, sizeof(s->regs));
    /* Initialize MAC registers to fixed values; driver will read them
     * and treat them as little-endian then convert to big-endian.
     */
    s->regs.mac1 = 0x00112233;
    s->regs.mac2 = 0x4455;
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, 0);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
    (void)s;
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "ia_pci",
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

type_init(pcibase_register_types)
;

