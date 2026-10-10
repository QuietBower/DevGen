/*
 * QEMU PCI device model for SysKonnect FDDI (skfp) adapter
 * Functional implementation derived strictly from Linux driver skfddi.c
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
#include <linux/types.h>

#define TYPE_PCIBASE_DEVICE "skfddi_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define SKFP_PCI_VENDOR_ID   0x1148
#define SKFP_PCI_DEVICE_ID   0x4000
#define SKFP_PCI_CLASS_ID    0x0200  /* PCI_CLASS_NETWORK_ETHERNET is 0x0200, used as generic net class here */

/* Important I/O register offsets used by the driver */
#define SKFP_FP_IO_LEN   256
#define SKFP_B0_CTRL     0x0004
#define SKFP_B0_LED      0x0006
#define SKFP_B0_ISRC     0x0008
#define SKFP_B0_IMSK     0x000c
#define SKFP_B0_ST1U     0x0010
#define SKFP_B0_ST1L     0x0014
#define SKFP_B0_ST2U     0x0018
#define SKFP_B0_ST2L     0x001c
#define SKFP_B0_ST3U     0x0034
#define SKFP_B0_ST3L     0x0038
#define SKFP_B0_DAS      0x0005
#define SKFP_B0_TST_CTRL 0x0007
#define SKFP_B0_XA_CSR   0x0078
#define SKFP_B0_XS_CSR   0x007c
#define SKFP_B0_R1_CSR   0x0070
#define SKFP_B0_R2_CSR   0x0074

#define SKFP_B2_MAC_0    0x0100
#define SKFP_B2_PMD_TYP  0x0109
#define SKFP_B2_CONN_TYP 0x0108
#define SKFP_B2_TI_INI   0x0120
#define SKFP_B2_TI_VAL   0x0124
#define SKFP_B2_TI_CRTL  0x0128
#define SKFP_B2_WDOG_INI 0x0130
#define SKFP_B2_WDOG_CRTL 0x0138
#define SKFP_B2_RTM_INI  0x0140
#define SKFP_B2_RTM_CRTL 0x0148
#define SKFP_B2_FAR      0x0110

#define SKFP_B3_CFG_SPC  0x0180

#define SKFP_B4_R1_DA    0x0210
#define SKFP_B4_R1_CSR   0x021c
#define SKFP_B4_R1_F     0x0220
#define SKFP_B4_R2_DA    0x0250

#define SKFP_B5_XA_DA    0x0290
#define SKFP_B5_XA_CSR   0x029c
#define SKFP_B5_XA_F     0x02a0
#define SKFP_B5_XS_DA    0x02d0
#define SKFP_B5_XS_CSR   0x02dc
#define SKFP_B5_XS_F     0x02e0

/* Interrupt source bits used by the driver */
#define SKFP_IS_MINTR1   (1UL<<16)
#define SKFP_IS_MINTR2   (1UL<<17)
#define SKFP_IS_MINTR3   (1UL<<18)
#define SKFP_IS_PLINT2   (1UL<<19)
#define SKFP_IS_PLINT1   (1UL<<20)
#define SKFP_IS_TOKEN    (1UL<<21)
#define SKFP_IS_TIMINT   (1UL<<22)
#define SKFP_IS_XS_C     (1UL<<0)
#define SKFP_IS_XS_F     (1UL<<1)
#define SKFP_IS_XA_C     (1UL<<4)
#define SKFP_IS_XA_F     (1UL<<5)
#define SKFP_IS_R1_C     (1UL<<12)
#define SKFP_IS_R1_F     (1UL<<13)
#define SKFP_IS_R1_P     (1UL<<15)

/* Control/status bits */
#define SKFP_CTRL_RST_SET   (1<<0)
#define SKFP_CTRL_RST_CLR   (1<<1)
#define SKFP_CTRL_MRST_CLR  (1<<3)
#define SKFP_CTRL_HPI_SET   (1<<4)
#define SKFP_CTRL_HPI_CLR   (1<<5)

#define SKFP_TIM_START      (1<<2)
#define SKFP_TIM_STOP       (1<<1)
#define SKFP_TIM_CL_IRQ     (1<<0)
#define SKFP_TIM_RES_TOK    (1<<3)

#define SKFP_DAS_BYP_INS    (1<<0)
#define SKFP_DAS_BYP_RMV    (1<<1)
#define SKFP_DAS_BYP_ST     (1<<2)
#define SKFP_DAS_AVAIL      (1<<3)

#define SKFP_LED_0_OFF      (1<<0)
#define SKFP_LED_0_ON       (1<<1)
#define SKFP_LED_1_OFF      (1<<2)
#define SKFP_LED_1_ON       (1<<3)
#define SKFP_LED_2_OFF      (1<<4)
#define SKFP_LED_2_ON       (1<<5)

#define SKFP_BMU_OWN        (1UL<<31)
#define SKFP_BMU_STF        (1UL<<30)
#define SKFP_BMU_EOF        (1UL<<29)
#define SKFP_BMU_EN_IRQ_EOF (1UL<<27)
#define SKFP_BMU_ST_BUF     (1UL<<25)
#define SKFP_BMU_SMT_TX     (1UL<<25)
#define SKFP_BMU_CHECK      0x00550000UL

#define SKFP_CSR_START      (1UL<<4)
#define SKFP_CSR_IRQ_CL_C   (1UL<<0)
#define SKFP_CSR_IRQ_CL_F   (1UL<<1)
#define SKFP_CSR_IRQ_CL_P   (1UL<<3)

#define SKFP_CSR_DESC_CLEAR (1UL<<21)
#define SKFP_CSR_FIFO_CLEAR (1UL<<19)
#define SKFP_CSR_HPI_RUN    (1UL<<17)
#define SKFP_CSR_SV_RUN     (1UL<<15)
#define SKFP_CSR_DREAD_RUN  (1UL<<13)
#define SKFP_CSR_TRANS_RUN  (1UL<<9)
#define SKFP_CSR_DWRITE_RUN (1UL<<11)

#define SKFP_CSR_FIFO_SET   (1UL<<18)
#define SKFP_CSR_DESC_SET   (1UL<<20)
#define SKFP_CSR_HPI_RST    (1UL<<16)
#define SKFP_CSR_SV_RST     (1UL<<14)
#define SKFP_CSR_DREAD_RST  (1UL<<12)
#define SKFP_CSR_TRANS_RST  (1UL<<8)
#define SKFP_CSR_DWRITE_RST (1UL<<10)

#define SKFP_CSR_CLR_RESET (SKFP_CSR_DESC_CLEAR|SKFP_CSR_FIFO_CLEAR|SKFP_CSR_HPI_RUN|SKFP_CSR_SV_RUN|SKFP_CSR_DREAD_RUN|SKFP_CSR_DWRITE_RUN|SKFP_CSR_TRANS_RUN)
#define SKFP_CSR_SET_RESET (SKFP_CSR_DESC_SET|SKFP_CSR_FIFO_SET|SKFP_CSR_HPI_RST|SKFP_CSR_SV_RST|SKFP_CSR_DREAD_RST|SKFP_CSR_DWRITE_RST|SKFP_CSR_TRANS_RST)

/* Additional driver macros related to timer and ISR access used in new code */
#define SKFP_TIM_START_BIT   SKFP_TIM_START
#define SKFP_TIM_STOP_BIT    SKFP_TIM_STOP
#define SKFP_TIM_CL_IRQ_BIT  SKFP_TIM_CL_IRQ
#define SKFP_TIM_RES_TOK_BIT SKFP_TIM_RES_TOK

/* Macros matching driver names where they reference B2 timer/RTM control */
#define TIM_START    SKFP_TIM_START
#define TIM_STOP     SKFP_TIM_STOP
#define TIM_CL_IRQ   SKFP_TIM_CL_IRQ
#define TIM_RES_TOK  SKFP_TIM_RES_TOK

/* GET_ISR() in driver is inpd(ISR_A) where ISR_A maps to B0_ISRC */


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
    uint32_t intr_status;   /* pending interrupt sources (B0_ISRC shadow) */
    uint32_t intr_mask;     /* interrupt mask (B0_IMSK shadow) */

    /* Hardware Register Shadows (The 'Identity' of the device) */
    struct {
        uint32_t b0_ctrl;
        uint32_t b0_led;
        uint32_t b0_isrc;
        uint32_t b0_imsk;
        uint32_t b0_st1u;
        uint32_t b0_st1l;
        uint32_t b0_st2u;
        uint32_t b0_st2l;
        uint32_t b0_st3u;
        uint32_t b0_st3l;
        uint32_t b0_das;
        uint32_t b0_tst_ctrl;

        uint32_t b2_mac_0;
        uint32_t b2_pmd_typ;
        uint32_t b2_conn_typ;
        uint32_t b2_ti_ini;
        uint32_t b2_ti_val;
        uint32_t b2_ti_crtl;
        uint32_t b2_wdog_ini;
        uint32_t b2_wdog_crtl;
        uint32_t b2_rtm_ini;
        uint32_t b2_rtm_crtl;
        uint32_t b2_far;

        uint32_t b3_cfg_spc;

        uint32_t b4_r1_da;
        uint32_t b4_r1_csr;
        uint32_t b4_r1_f;
        uint32_t b4_r2_da;

        uint32_t b5_xa_da;
        uint32_t b5_xa_csr;
        uint32_t b5_xa_f;
        uint32_t b5_xs_da;
        uint32_t b5_xs_csr;
        uint32_t b5_xs_f;
    } regs;

    /* DMA Context */
    /* No autonomous bus-master DMA modeled. */

    /* Status / flags */
    uint32_t hw_state;

    /* Probe / reset related state */
    uint32_t reset_state;

    /* Power management state (D0-D3) */
    uint8_t pm_state;

    /* Internal timer emulation for B2_TI_* and B2_RTM_* */
    QEMUTimer ti_timer;
    QEMUTimer rtm_timer;
    bool ti_running;
    bool rtm_running;
};

/* Helper to (re)assert or clear PCI INTx based on pending & masked bits. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* Interrupt is asserted if any enabled source is pending. */
    if (s->intr_status & s->intr_mask) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* Device-initiated DMA logic: not modeled because hardware side of DMA
 * engine is not visible in the provided driver sources (handled in
 * mac/hwm code). */
static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    (void)s;
    (void)is_write;
}

/* Timer callback for B2 general timer (TI) emulation.
 * The driver uses hwt_quick_read() which reads B2_TI_INI, stops timer via
 * B2_TI_CRTL (TIM_STOP), reads B2_TI_VAL, restarts with TIM_START, then
 * restores interval. Here we implement a simple down-counter model:
 * - TI_VAL counts down from TI_INI to 0.
 * - When it reaches 0 we raise the TIMINT interrupt bit in B0_ISRC.
 */
static void pcibase_ti_timer_cb(void *opaque)
{
    PCIBaseState *s = opaque;

    if (!s->ti_running) {
        return;
    }

    if (s->regs.b2_ti_val > 0) {
        s->regs.b2_ti_val--;
    }

    if (s->regs.b2_ti_val == 0) {
        /* Raise timer interrupt bit as used in IMASK_SLOW/ISR_MASK */
        s->regs.b0_isrc |= SKFP_IS_TIMINT;
        s->intr_status = s->regs.b0_isrc;
        pcibase_update_irq(s);

        /* For one-shot behaviour, stop timer until restarted by driver. */
        s->ti_running = false;
    } else {
        /* Re-arm timer for next tick (arbitrary 1ms granularity). */
        timer_mod(&s->ti_timer,
                  qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 1);
    }
}

/* Timer callback for B2_RTM_CRTL watchdog. The driver rtm_irq() does:
 *  outpw(B2_RTM_CRTL, TIM_CL_IRQ);
 *  if (inpw(B2_RTM_CRTL) & TIM_RES_TOK) { ... }
 *  outpw(B2_RTM_CRTL, TIM_START);
 * We implement only the interrupt generation: when the RTM timer expires,
 * set TIM_RES_TOK in B2_RTM_CRTL and also set the TIMINT interrupt bit
 * in B0_ISRC so the ISR can notice. */
static void pcibase_rtm_timer_cb(void *opaque)
{
    PCIBaseState *s = opaque;

    if (!s->rtm_running) {
        return;
    }

    /* Set result token and raise timer interrupt bit */
    s->regs.b2_rtm_crtl |= SKFP_TIM_RES_TOK;
    s->regs.b0_isrc |= SKFP_IS_TIMINT;
    s->intr_status = s->regs.b0_isrc;
    pcibase_update_irq(s);

    /* One-shot: stop until restarted. */
    s->rtm_running = false;
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = 0;

    /* All documented registers are 32-bit, but driver may access using
     * inb/outb/inw etc. We return zero for mis-sized accesses. */
    if (size != 4) {
        return 0;
    }

    switch (addr) {
    case SKFP_B0_CTRL:
        val = s->regs.b0_ctrl;
        break;
    case SKFP_B0_LED:
        val = s->regs.b0_led;
        break;
    case SKFP_B0_ISRC:
        /* Return current pending interrupt sources. */
        val = s->regs.b0_isrc;
        break;
    case SKFP_B0_IMSK:
        val = s->regs.b0_imsk;
        break;
    case SKFP_B0_ST1U:
        val = s->regs.b0_st1u;
        break;
    case SKFP_B0_ST1L:
        val = s->regs.b0_st1l;
        break;
    case SKFP_B0_ST2U:
        val = s->regs.b0_st2u;
        break;
    case SKFP_B0_ST2L:
        val = s->regs.b0_st2l;
        break;
    case SKFP_B0_ST3U:
        val = s->regs.b0_st3u;
        break;
    case SKFP_B0_ST3L:
        val = s->regs.b0_st3l;
        break;
    case SKFP_B0_DAS:
        val = s->regs.b0_das;
        break;
    case SKFP_B0_TST_CTRL:
        val = s->regs.b0_tst_ctrl;
        break;

    case SKFP_B2_MAC_0:
        val = s->regs.b2_mac_0;
        break;
    case SKFP_B2_PMD_TYP:
        val = s->regs.b2_pmd_typ;
        break;
    case SKFP_B2_CONN_TYP:
        val = s->regs.b2_conn_typ;
        break;
    case SKFP_B2_TI_INI:
        val = s->regs.b2_ti_ini;
        break;
    case SKFP_B2_TI_VAL:
        /* TI_VAL is maintained by timer callback; just return shadow. */
        val = s->regs.b2_ti_val;
        break;
    case SKFP_B2_TI_CRTL:
        val = s->regs.b2_ti_crtl;
        break;
    case SKFP_B2_WDOG_INI:
        val = s->regs.b2_wdog_ini;
        break;
    case SKFP_B2_WDOG_CRTL:
        val = s->regs.b2_wdog_crtl;
        break;
    case SKFP_B2_RTM_INI:
        val = s->regs.b2_rtm_ini;
        break;
    case SKFP_B2_RTM_CRTL:
        val = s->regs.b2_rtm_crtl;
        break;
    case SKFP_B2_FAR:
        val = s->regs.b2_far;
        break;

    case SKFP_B3_CFG_SPC:
        val = s->regs.b3_cfg_spc;
        break;

    case SKFP_B4_R1_DA:
        val = s->regs.b4_r1_da;
        break;
    case SKFP_B4_R1_CSR:
        val = s->regs.b4_r1_csr;
        break;
    case SKFP_B4_R1_F:
        val = s->regs.b4_r1_f;
        break;
    case SKFP_B4_R2_DA:
        val = s->regs.b4_r2_da;
        break;

    case SKFP_B5_XA_DA:
        val = s->regs.b5_xa_da;
        break;
    case SKFP_B5_XA_CSR:
        val = s->regs.b5_xa_csr;
        break;
    case SKFP_B5_XA_F:
        val = s->regs.b5_xa_f;
        break;
    case SKFP_B5_XS_DA:
        val = s->regs.b5_xs_da;
        break;
    case SKFP_B5_XS_CSR:
        val = s->regs.b5_xs_csr;
        break;
    case SKFP_B5_XS_F:
        val = s->regs.b5_xs_f;
        break;

    default:
        /* Unimplemented offsets return 0. */
        val = 0;
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        return;
    }

    switch (addr) {
    case SKFP_B0_CTRL:
        /* Store control word. Implement only bookkeeping; reset side
         * effects are not fully modeled due to lack of explicit driver
         * requirements. */
        s->regs.b0_ctrl = (uint32_t)val;
        break;
    case SKFP_B0_LED:
        s->regs.b0_led = (uint32_t)val;
        break;
    case SKFP_B0_ISRC:
        /* Write-1-to-clear semantics for interrupt source register. */
        s->regs.b0_isrc &= ~((uint32_t)val);
        s->intr_status = s->regs.b0_isrc;
        pcibase_update_irq(s);
        break;
    case SKFP_B0_IMSK:
        s->regs.b0_imsk = (uint32_t)val;
        s->intr_mask = s->regs.b0_imsk;
        pcibase_update_irq(s);
        break;
    case SKFP_B0_ST1U:
        s->regs.b0_st1u = (uint32_t)val;
        break;
    case SKFP_B0_ST1L:
        s->regs.b0_st1l = (uint32_t)val;
        break;
    case SKFP_B0_ST2U:
        s->regs.b0_st2u = (uint32_t)val;
        break;
    case SKFP_B0_ST2L:
        s->regs.b0_st2l = (uint32_t)val;
        break;
    case SKFP_B0_ST3U:
        s->regs.b0_st3u = (uint32_t)val;
        break;
    case SKFP_B0_ST3L:
        s->regs.b0_st3l = (uint32_t)val;
        break;
    case SKFP_B0_DAS:
        s->regs.b0_das = (uint32_t)val;
        break;
    case SKFP_B0_TST_CTRL:
        s->regs.b0_tst_ctrl = (uint32_t)val;
        break;

    case SKFP_B2_MAC_0:
        s->regs.b2_mac_0 = (uint32_t)val;
        break;
    case SKFP_B2_PMD_TYP:
        s->regs.b2_pmd_typ = (uint32_t)val;
        break;
    case SKFP_B2_CONN_TYP:
        s->regs.b2_conn_typ = (uint32_t)val;
        break;
    case SKFP_B2_TI_INI:
        /* Interval register; driver reads/writes in hwt_quick_read(). */
        s->regs.b2_ti_ini = (uint32_t)val;
        break;
    case SKFP_B2_TI_VAL:
        /* Directly set current timer value. */
        s->regs.b2_ti_val = (uint32_t)val;
        break;
    case SKFP_B2_TI_CRTL: {
        uint32_t old = s->regs.b2_ti_crtl;
        s->regs.b2_ti_crtl = (uint32_t)val;

        /* Emulate basic start/stop semantics the driver relies on. */
        if (val & TIM_STOP) {
            /* Stop TI timer */
            s->ti_running = false;
            timer_del(&s->ti_timer);
        }
        if (val & TIM_START) {
            /* Start TI timer with interval from TI_INI. */
            if (!s->ti_running) {
                s->ti_running = true;
                /* Initialize TI_VAL from TI_INI if it is zero */
                if (s->regs.b2_ti_val == 0) {
                    s->regs.b2_ti_val = s->regs.b2_ti_ini;
                }
                timer_mod(&s->ti_timer,
                          qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 1);
            }
        }
        (void)old;
        break;
    }
    case SKFP_B2_WDOG_INI:
        s->regs.b2_wdog_ini = (uint32_t)val;
        break;
    case SKFP_B2_WDOG_CRTL:
        /* Only TIM_STOP is used in smt_stop_watchdog() when wdog_used. */
        s->regs.b2_wdog_crtl = (uint32_t)val;
        if (val & TIM_STOP) {
            /* We do not model a watchdog timer; just remember stopped. */
        }
        break;
    case SKFP_B2_RTM_INI:
        s->regs.b2_rtm_ini = (uint32_t)val;
        break;
    case SKFP_B2_RTM_CRTL: {
        uint32_t newv = (uint32_t)val;
        /* Clearing IRQ simply clears TIM_RES_TOK bit. */
        if (newv & TIM_CL_IRQ) {
            s->regs.b2_rtm_crtl &= ~TIM_RES_TOK;
        }
        /* Preserve other bits, then OR with new control bits. */
        s->regs.b2_rtm_crtl |= (newv & (TIM_START | TIM_STOP | TIM_RES_TOK));

        if (newv & TIM_STOP) {
            s->rtm_running = false;
            timer_del(&s->rtm_timer);
        }
        if (newv & TIM_START) {
            /* Start/Restart RTM timer with interval from RTM_INI. */
            s->rtm_running = true;
            timer_mod(&s->rtm_timer,
                      qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 1);
        }
        break;
    }
    case SKFP_B2_FAR:
        s->regs.b2_far = (uint32_t)val;
        break;

    case SKFP_B3_CFG_SPC:
        s->regs.b3_cfg_spc = (uint32_t)val;
        break;

    case SKFP_B4_R1_DA:
        s->regs.b4_r1_da = (uint32_t)val;
        break;
    case SKFP_B4_R1_CSR:
        s->regs.b4_r1_csr = (uint32_t)val;
        /* The driver writes CSR_IRQ_CL_P/C/F bits here to clear
         * corresponding interrupt causes (IS_R1_P/IS_R1_C/IS_R1_F). */
        if (val & SKFP_CSR_IRQ_CL_P) {
            s->regs.b0_isrc &= ~SKFP_IS_R1_P;
        }
        if (val & SKFP_CSR_IRQ_CL_C) {
            s->regs.b0_isrc &= ~SKFP_IS_R1_C;
        }
        if (val & SKFP_CSR_IRQ_CL_F) {
            s->regs.b0_isrc &= ~SKFP_IS_R1_F;
        }
        s->intr_status = s->regs.b0_isrc;
        pcibase_update_irq(s);
        break;
    case SKFP_B4_R1_F:
        s->regs.b4_r1_f = (uint32_t)val;
        break;
    case SKFP_B4_R2_DA:
        s->regs.b4_r2_da = (uint32_t)val;
        break;

    case SKFP_B5_XA_DA:
        s->regs.b5_xa_da = (uint32_t)val;
        break;
    case SKFP_B5_XA_CSR:
        s->regs.b5_xa_csr = (uint32_t)val;
        /* Driver uses CSR_IRQ_CL_C/F on XA CSR to clear async TX
         * encoding/fast-complete interrupts (IS_XA_C/IS_XA_F). */
        if (val & SKFP_CSR_IRQ_CL_C) {
            s->regs.b0_isrc &= ~SKFP_IS_XA_C;
        }
        if (val & SKFP_CSR_IRQ_CL_F) {
            s->regs.b0_isrc &= ~SKFP_IS_XA_F;
        }
        s->intr_status = s->regs.b0_isrc;
        pcibase_update_irq(s);
        break;
    case SKFP_B5_XA_F:
        s->regs.b5_xa_f = (uint32_t)val;
        break;
    case SKFP_B5_XS_DA:
        s->regs.b5_xs_da = (uint32_t)val;
        break;
    case SKFP_B5_XS_CSR:
        s->regs.b5_xs_csr = (uint32_t)val;
        /* Driver uses CSR_IRQ_CL_C/F on XS CSR to clear sync TX
         * encoding/fast-complete interrupts (IS_XS_C/IS_XS_F). */
        if (val & SKFP_CSR_IRQ_CL_C) {
            s->regs.b0_isrc &= ~SKFP_IS_XS_C;
        }
        if (val & SKFP_CSR_IRQ_CL_F) {
            s->regs.b0_isrc &= ~SKFP_IS_XS_F;
        }
        s->intr_status = s->regs.b0_isrc;
        pcibase_update_irq(s);
        break;
    case SKFP_B5_XS_F:
        s->regs.b5_xs_f = (uint32_t)val;
        break;

    default:
        /* Ignore writes to unknown offsets. */
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* The driver uses ioport_map()/ioread32 on the BAR and then
     * macros like inpd() which perform 32-bit reads. Those map
     * directly onto MMIO semantics here; treat PIO BAR as same
     * backing register file. */
    return pcibase_mmio_read(opaque, addr, size);
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    pcibase_mmio_write(opaque, addr, val, size);
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

    /* Reset register shadows to power-on defaults */
    memset(&s->regs, 0, sizeof(s->regs));
    s->intr_status = 0;
    s->intr_mask = 0;
    s->hw_state = 0;
    s->reset_state = 0;
    s->pm_state = 0;

    s->ti_running = false;
    s->rtm_running = false;
    timer_del(&s->ti_timer);
    timer_del(&s->rtm_timer);

    /* Driver uses ISR_A (B0_ISRC) and B0_IMSK but does not rely on
     * specific reset values, so leave them at 0. */

    pcibase_update_irq(s);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  SKFP_PCI_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  SKFP_PCI_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, SKFP_PCI_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR layout: the driver with MEM_MAPPED_IO uses ioremap on the
     * adapter's I/O space via ADDR(Bx_*) macros, which are based on
     * smc->hw.iop. That corresponds to a memory-mapped BAR. Model
     * BAR0 as MMIO with size FP_IO_LEN (256 bytes). */

    /* Initialize all BARs to NONE first. */
    for (int i = 0; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = NULL;
    }

    /* Use BAR0 as MMIO region of 256 bytes. */
    s->num_bars = 1;
    s->bar_info[0].index = 0;           /* BAR #0 */
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = SKFP_FP_IO_LEN;
    s->bar_info[0].name = "skfddi-mmio";

    pcibase_register_bar(pdev, s, &s->bar_info[0], errp);

    /* The Linux driver does not request MSI/MSI-X explicitly */
    s->has_msi = false;
    s->has_msix = false;

    memset(&s->regs, 0, sizeof(s->regs));
    s->intr_status = 0;
    s->intr_mask = 0;
    s->hw_state = 0;
    s->reset_state = 0;
    s->pm_state = 0;

    /* Initialize software timers for TI and RTM. */
    timer_init_ms(&s->ti_timer, QEMU_CLOCK_VIRTUAL, pcibase_ti_timer_cb, s);
    timer_init_ms(&s->rtm_timer, QEMU_CLOCK_VIRTUAL, pcibase_rtm_timer_cb, s);
    s->ti_running = false;
    s->rtm_running = false;

    pcibase_update_irq(s);
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

    /* Stop and delete timers. */
    timer_del(&s->ti_timer);
    timer_del(&s->rtm_timer);

    (void)s;
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "skfddi_pci",
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
