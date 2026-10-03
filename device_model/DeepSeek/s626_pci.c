/*
 * QEMU PCI model for Sensoray 626 (SAA7146-based)
 * Generated from Linux driver s626.c - Phase 2 Implementation
 * Phase 4: Fixed DEBI timeout with timer-based completion and interrupt generation
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

#define TYPE_PCIBASE_DEVICE "s626_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define S626_VENDOR_ID          0x1131
#define S626_DEVICE_ID          0x7146
#define S626_CLASS_ID           PCI_CLASS_MULTIMEDIA_OTHER
#define S626_SUBVENDOR_ID       0x6000
#define S626_SUBDEVICE_ID       0x0272

#define S626_BAR0_SIZE          0x2000

#define S626_INDXMASK(C)        (1 << (((C) > 2) ? ((C) * 2 - 1) : ((C) * 2 +  4)))
#define S626_OVERMASK(C)        (1 << (((C) > 2) ? ((C) * 2 + 5) : ((C) * 2 + 10)))
#define S626_BUGFIX_STREG(REGADRS)   ((REGADRS) - 4)
#define S626_VECTPORT(VECTNUM)  (S626_P_TSL2 + ((VECTNUM) << 2))
#define S626_MAX_SPEED          200000
#define S626_MIN_SPEED          2000000000
#define S626_P_TSL2             0x01C0
#define S626_PSR_DEBI_S         0x00080000
#define S626_P_MC2              0x0100
#define S626_MC2_UPLD_DEBI      0x0002
#define S626_P_PSR              0x0110
#define S626_P_DEBIAD           0x0088
#define S626_DEBI_CMD_RDWORD    (S626_DEBI_CMD_READ | S626_DEBI_CMD_SIZE16)
#define S626_P_DEBICMD          0x0080
#define S626_DEBI_CMD_WRWORD    (S626_DEBI_CMD_WRITE | S626_DEBI_CMD_SIZE16)
#define S626_MC2_UPLD_IIC       0x0001
#define S626_I2C_ERR            0x0002
#define S626_I2C_BUSY           0x0001
#define S626_P_I2CCTRL          0x008C
#define S626_I2C_ATTRSTART      0x3
#define S626_I2C_ATTRNOP        0x0
#define S626_I2C_ATTRSTOP       0x1
#define S626_I2C_B2(ATTR, VAL)  (((ATTR) << 6) | ((VAL) << 24))
#define S626_I2C_B1(ATTR, VAL)  (((ATTR) << 4) | ((VAL) << 16))
#define S626_I2C_B0(ATTR, VAL)  (((ATTR) << 2) | ((VAL) <<  8))
#define S626_P_MC1              0x00FC
#define S626_P_SSR              0x0114
#define S626_MC1_A2OUT          0x0008
#define S626_P_FB_BUFFER2       0x0148
#define S626_SSR_AF2_OUT        0x00000200
#define S626_EOS                0x00000001
#define S626_XSD2               0x00000008
#define S626_SIB_A2             0x00000200
#define S626_ISR_AFOU           0x00000800
#define S626_P_ISR              0x010C
#define S626_RSD2               0x00001000
#define S626_LP_DACPOL          0x0082
#define S626_XFIFO_2            0x00000020
#define S626_XFIFO_3            0x00000030
#define S626_WS1                0x40000000
#define S626_XFIFO_0            0x00000000
#define S626_WS2                0x20000000
#define S626_XFIFO_1            0x00000010
#define S626_WS3                0x10000000
#define S626_CRBMSK_INTCTRL     (S626_CRBMSK_INTRESETCMD | \
                                 S626_CRBMSK_INTRESET_A | \
                                 S626_CRBMSK_INTRESET_B)
#define S626_CRBMSK_LATCHSRC    S626_SET_CRB_LATCHSRC(~0)
#define S626_SET_CRB_LATCHSRC(x) \
        S626_MAKE((x), S626_CRBWID_LATCHSRC, S626_CRBBIT_LATCHSRC)
#define S626_LP_CRB(x)          (0x0002 + (((x) % 3) * 0x4))
#define S626_LP_CNTR(x)         (0x000c  + (((x) < 3) ? 0x0 : 0x4) + \
                                           (((x) % 3) * 0x8))
#define S626_SET_CRB_INTRESETCMD(x) \
        S626_MAKE((x), S626_CRBWID_INTRESETCMD, S626_CRBBIT_INTRESETCMD)
#define S626_SET_CRB_INTRESET_A(x) \
        S626_MAKE((x), S626_CRBWID_INTRESET_A, S626_CRBBIT_INTRESET_A)
#define S626_SET_CRB_INTRESET_B(x) \
        S626_MAKE((x), S626_CRBWID_INTRESET_B, S626_CRBBIT_INTRESET_B)
#define S626_GET_STD_INDXPOL(v) \
        S626_UNMAKE((v), S626_STDWID_INDXPOL, S626_STDBIT_INDXPOL)
#define S626_SET_CRB_CLKENAB_A(x) \
        S626_MAKE((x), S626_CRBWID_CLKENAB_A, S626_CRBBIT_CLKENAB_A)
#define S626_SET_CRA_INDXPOL_A(x) \
        S626_MAKE((x), S626_CRAWID_INDXPOL_A, S626_CRABIT_INDXPOL_A)
#define S626_GET_STD_INTSRC(v) \
        S626_UNMAKE((v), S626_STDWID_INTSRC, S626_STDBIT_INTSRC)
#define S626_CRBMSK_CLKENAB_A   S626_SET_CRB_CLKENAB_A(~0)
#define S626_ENCMODE_EXTENDER   3
#define S626_GET_STD_CLKENAB(v) \
        S626_UNMAKE((v), S626_STDWID_CLKENAB, S626_STDBIT_CLKENAB)
#define S626_GET_STD_INDXSRC(v) \
        S626_UNMAKE((v), S626_STDWID_INDXSRC, S626_STDBIT_INDXSRC)
#define S626_GET_STD_CLKMULT(v) \
        S626_UNMAKE((v), S626_STDWID_CLKMULT, S626_STDBIT_CLKMULT)
#define S626_LP_CRA(x)          (0x0000 + (((x) % 3) * 0x4))
#define S626_CRAMSK_CNTSRC_B    S626_SET_CRA_CNTSRC_B(~0)
#define S626_SET_CRA_LOADSRC_A(x) \
        S626_MAKE((x), S626_CRAWID_LOADSRC_A, S626_CRABIT_LOADSRC_A)
#define S626_CNTSRC_ENCODER     0
#define S626_INDXSRC_SOFT       2
#define S626_SET_CRA_CNTSRC_A(x) \
        S626_MAKE((x), S626_CRAWID_CNTSRC_A, S626_CRABIT_CNTSRC_A)
#define S626_SET_CRA_CLKPOL_A(x) \
        S626_MAKE((x), S626_CRAWID_CLKPOL_A, S626_CRABIT_CLKPOL_A)
#define S626_SET_CRA_INDXSRC_A(x) \
        S626_MAKE((x), S626_CRAWID_INDXSRC_A, S626_CRABIT_INDXSRC_A)
#define S626_CLKMULT_1X         2
#define S626_SET_CRA_CLKMULT_A(x) \
        S626_MAKE((x), S626_CRAWID_CLKMULT_A, S626_CRABIT_CLKMULT_A)
#define S626_CLKMULT_SPECIAL    3
#define S626_ENCMODE_TIMER      2
#define S626_GET_STD_LOADSRC(v) \
        S626_UNMAKE((v), S626_STDWID_LOADSRC, S626_STDBIT_LOADSRC)
#define S626_CRAMSK_INDXSRC_B   S626_SET_CRA_INDXSRC_B(~0)
#define S626_GET_STD_ENCMODE(v) \
        S626_UNMAKE((v), S626_STDWID_ENCMODE, S626_STDBIT_ENCMODE)
#define S626_CNTSRC_SYSCLK      2
#define S626_SET_CRA_INTSRC_A(x) \
        S626_MAKE((x), S626_CRAWID_INTSRC_A, S626_CRABIT_INTSRC_A)
#define S626_GET_STD_CLKPOL(v) \
        S626_UNMAKE((v), S626_STDWID_CLKPOL, S626_STDBIT_CLKPOL)
#define S626_SET_CRB_CLKMULT_B(x) \
        S626_MAKE((x), S626_CRBWID_CLKMULT_B, S626_CRBBIT_CLKMULT_B)
#define S626_SET_CRB_CLKENAB_B(x) \
        S626_MAKE((x), S626_CRBWID_CLKENAB_B, S626_CRBBIT_CLKENAB_B)
#define S626_SET_CRB_INDXPOL_B(x) \
        S626_MAKE((x), S626_CRBWID_INDXPOL_B, S626_CRBBIT_INDXPOL_B)
#define S626_SET_CRB_LOADSRC_B(x) \
        S626_MAKE((x), S626_CRBWID_LOADSRC_B, S626_CRBBIT_LOADSRC_B)
#define S626_SET_CRB_CLKPOL_B(x) \
        S626_MAKE((x), S626_CRBWID_CLKPOL_B, S626_CRBBIT_CLKPOL_B)
#define S626_SET_CRA_INDXSRC_B(x) \
        S626_MAKE((x), S626_CRAWID_INDXSRC_B, S626_CRABIT_INDXSRC_B)
#define S626_SET_CRA_CNTSRC_B(x) \
        S626_MAKE((x), S626_CRAWID_CNTSRC_B, S626_CRABIT_CNTSRC_B)
#define S626_SET_CRB_INTSRC_B(x) \
        S626_MAKE((x), S626_CRBWID_INTSRC_B, S626_CRBBIT_INTSRC_B)
#define S626_CRBMSK_CLKENAB_B   S626_SET_CRB_CLKENAB_B(~0)
#define S626_CRAMSK_LOADSRC_A   S626_SET_CRA_LOADSRC_A(~0)
#define S626_CRBMSK_LOADSRC_B   S626_SET_CRB_LOADSRC_B(~0)
#define S626_CRAMSK_INTSRC_A    S626_SET_CRA_INTSRC_A(~0)
#define S626_CRBMSK_INTSRC_B    S626_SET_CRB_INTSRC_B(~0)
#define S626_CRAMSK_INDXPOL_A   S626_SET_CRA_INDXPOL_A(~0)
#define S626_CRBMSK_INDXPOL_B   S626_SET_CRB_INDXPOL_B(~0)
#define S626_LP_WREDGSEL(x)     (0x0044 + (x) * 0x10)
#define S626_LP_RDINTSEL(x)     (0x004a + (x) * 0x10)
#define S626_LP_MISC1           0x0088
#define S626_LP_RDEDGSEL(x)     (0x004c + (x) * 0x10)
#define S626_LP_RDCAPSEL(x)     (0x004e + (x) * 0x10)
#define S626_LP_WRINTSEL(x)     (0x0042 + (x) * 0x10)
#define S626_LP_WRCAPSEL(x)     (0x0046 + (x) * 0x10)
#define S626_MISC1_EDCAP        0x1000
#define S626_MISC1_NOEDCAP      0x0000
#define S626_DIO_BANKS          3
#define S626_MC2_ADC_RPS        S626_MC2_RPSSIG0
#define S626_MC1_ERPS1          0x2000
#define S626_CLKENAB_ALWAYS     0
#define S626_LP_RDCAPFLG(x)     (0x0048 + (x) * 0x10)
#define S626_IRQ_COINT1B        0x0800
#define S626_LP_RDMISC2         0x0082
#define S626_IRQ_COINT1A        0x0400
#define S626_IRQ_COINT2A        0x1000
#define S626_CLKENAB_INDEX      1
#define S626_IRQ_COINT2B        0x2000
#define S626_IRQ_COINT3A        0x4000
#define S626_IRQ_COINT3B        0x8000
#define S626_P_IER              0x00DC
#define S626_IRQ_GPIO3          0x00000040
#define S626_IRQ_RPS1           0x10000000
#define S626_RPS_GPIO2          0x00080000
#define S626_GPIO1_LO           0x00000000
#define S626_GSEL_BIPOLAR5V     0x00F0
#define S626_EOPL               0x80
#define S626_RPS_LDREG          0x90000100
#define S626_RPS_IRQ            0x60000000
#define S626_P_FB_BUFFER1       0x0144
#define S626_LP_ISEL            0x0086
#define S626_RPS_DEBI           0x00000002
#define S626_RPS_UPLOAD         0x40000000
#define S626_GPIO_BASE          0x10004000
#define S626_RPS_SIGADC         S626_RPS_SIG0
#define S626_P_GPIO             0x00E0
#define S626_RPS_JUMP           0x80000000
#define S626_RPS_PAUSE          0x20000000
#define S626_RPS_STREG          0xA0000100
#define S626_LP_GSEL            0x0084
#define S626_RPS_NOP            0x00000000
#define S626_RPS_CLRSIGNAL      0x00000000
#define S626_P_RPSADDR1         0x0108
#define S626_RPSCLK_PER_US      (33 / S626_RPSCLK_SCALAR)
#define S626_GSEL_BIPOLAR10V    0x00A0
#define S626_GPIO1_HI           0x00001000
#define S626_PSR_GPIO2          0x00000020
#define S626_RANGE_5V           0x10
#define S626_RANGE_10V          0x00
#define S626_SET_STD_CLKMULT(x) \
        S626_MAKE((x), S626_STDWID_CLKMULT, S626_STDBIT_CLKMULT)
#define S626_SET_STD_INDXSRC(x) \
        S626_MAKE((x), S626_STDWID_INDXSRC, S626_STDBIT_INDXSRC)
#define S626_SET_STD_ENCMODE(x) \
        S626_MAKE((x), S626_STDWID_ENCMODE, S626_STDBIT_ENCMODE)
#define S626_SET_STD_CLKPOL(x)  \
        S626_MAKE((x), S626_STDWID_CLKPOL, S626_STDBIT_CLKPOL)
#define S626_SET_STD_LOADSRC(x) \
        S626_MAKE((x), S626_STDWID_LOADSRC, S626_STDBIT_LOADSRC)
#define S626_SET_STD_CLKENAB(x) \
        S626_MAKE((x), S626_STDWID_CLKENAB, S626_STDBIT_CLKENAB)
#define S626_LOADSRC_INDX       0
#define S626_CNTDIR_DOWN        1
#define S626_LATCHSRC_A_INDXA   1
#define S626_INTSRC_OVER        1
#define S626_LP_WRDOUT(x)       (0x0048 + (x) * 0x10)
#define S626_LP_RDDIN(x)        (0x0040 + (x) * 0x10)
#define S626_LATCHSRC_AB_READ   0
#define S626_ENCMODE_COUNTER    0
#define S626_CLKPOL_POS         0
#define S626_MISC1_WDISABLE     0x0000
#define S626_LP_WRMISC2         0x0090
#define S626_MISC1_WENABLE      0x8000
#define S626_ENCODER_CHANNELS   6
#define S626_DMABUF_SIZE        4096
#define S626_ACON1_ADCSTART     S626_ACON1_BASE
#define S626_MC1_I2C            0x0100
#define S626_SIB_A1             0x00400000
#define S626_DEBI_CFG_INTEL     0x00020000
#define S626_DEBI_CFG_SLAVE16   0x00080000
#define S626_LF_A2              0x00000080
#define S626_P_RPSPAGE1         0x00C8
#define S626_P_PAGEA2_OUT       0x00C0
#define S626_MC1_DEBI           0x0800
#define S626_P_DEBIPAGE         0x0084
#define S626_ACON1_DACSTART     (S626_ACON1_BASE | S626_A2_RUN)
#define S626_DAC_CHANNELS       4
#define S626_DEBI_CFG_TOUT_BIT  22
#define S626_P_ACON1            0x00F4
#define S626_DEBI_TOUT          7
#define S626_I2C_CLKSEL         0x0400
#define S626_DEBI_PAGE_DISABLE  0x00000000
#define S626_I2C_ABORT          0x0080
#define S626_RSD1               0x01000000
#define S626_P_RPS1_TOUT        0x00D8
#define S626_MC1_AUDIO          0x0200
#define S626_ACON2_INIT         (S626_ACON2_XORMASK ^ \
                                 (S626_A1_CLKSRC_BCLK1 | S626_A2_CLKSRC_X2 | \
                                  S626_INVERT_BCLK2 | S626_BCLK2_OE))
#define S626_P_I2CSTAT          0x0090
#define S626_P_ACON2            0x00F8
#define S626_P_TSL1             0x0180
#define S626_P_DEBICFG          0x007C
#define S626_P_PCI_BT_A         0x004C
#define S626_MISC2_BATT_ENABLE  0x0008
#define S626_P_PROTA2_OUT       0x00BC
#define S626_P_BASEA2_OUT       0x00B8
#define S626_DEBI_SWAP          S626_DEBI_CFG_SWAP_NONE
#define S626_DAC_WDMABUF_OS     S626_ADC_DMABUF_DWORDS
#define S626_MC1_SOFT_RESET     0x80000000
#define S626_ADC_CHANNELS       16
#define S626_MC1_SHUTDOWN       0x3FFF0000
#define S626_ACON1_BASE         (S626_WS_MODES | S626_A1_RUN)
#define S626_DEBI_CMD_SIZE16    (2 << 17)
#define S626_DEBI_CMD_READ      0x00010000
#define S626_DEBI_CMD_WRITE     0x00000000
#define S626_CRBMSK_INTRESETCMD S626_SET_CRB_INTRESETCMD(~0)
#define S626_CRBMSK_INTRESET_B  S626_SET_CRB_INTRESET_B(~0)
#define S626_CRBMSK_INTRESET_A  S626_SET_CRB_INTRESET_A(~0)
#define S626_MAKE(x, w, p)     (((x) & ((1 << (w)) - 1)) << (p))
#define S626_CRBBIT_LATCHSRC    8
#define S626_CRBWID_LATCHSRC    2
#define S626_CRBBIT_INTRESETCMD 15
#define S626_CRBWID_INTRESETCMD 1
#define S626_CRBWID_INTRESET_A  1
#define S626_CRBBIT_INTRESET_A  13
#define S626_CRBBIT_INTRESET_B  14
#define S626_CRBWID_INTRESET_B  1
#define S626_STDBIT_INDXPOL     6
#define S626_UNMAKE(v, w, p)   (((v) >> (p)) & ((1 << (w)) - 1))
#define S626_STDWID_INDXPOL     1
#define S626_CRBBIT_CLKENAB_A   12
#define S626_CRBWID_CLKENAB_A   1
#define S626_CRABIT_INDXPOL_A   11
#define S626_CRAWID_INDXPOL_A   1
#define S626_STDWID_INTSRC      2
#define S626_STDBIT_INTSRC      13
#define S626_STDWID_CLKENAB     1
#define S626_STDBIT_CLKENAB     0
#define S626_STDWID_INDXSRC     2
#define S626_STDBIT_INDXSRC     7
#define S626_STDBIT_CLKMULT     1
#define S626_STDWID_CLKMULT     2
#define S626_CRAWID_LOADSRC_A   2
#define S626_CRABIT_LOADSRC_A   9
#define S626_CRAWID_CNTSRC_A    2
#define S626_CRABIT_CNTSRC_A    0
#define S626_CRABIT_CLKPOL_A    4
#define S626_CRAWID_CLKPOL_A    1
#define S626_CRABIT_INDXSRC_A   2
#define S626_CRAWID_INDXSRC_A   2
#define S626_CRABIT_CLKMULT_A   7
#define S626_CRAWID_CLKMULT_A   2
#define S626_STDBIT_LOADSRC     9
#define S626_STDWID_LOADSRC     2
#define S626_STDWID_ENCMODE     2
#define S626_STDBIT_ENCMODE     4
#define S626_CRAWID_INTSRC_A    2
#define S626_CRABIT_INTSRC_A    5
#define S626_STDBIT_CLKPOL      3
#define S626_STDWID_CLKPOL      1
#define S626_CRBWID_CLKMULT_B   2
#define S626_CRBBIT_CLKMULT_B   3
#define S626_CRBBIT_CLKENAB_B   2
#define S626_CRBWID_CLKENAB_B   1
#define S626_CRBBIT_INDXPOL_B   1
#define S626_CRBWID_INDXPOL_B   1
#define S626_CRBBIT_LOADSRC_B   6
#define S626_CRBWID_LOADSRC_B   2
#define S626_CRBWID_CLKPOL_B    1
#define S626_CRBBIT_CLKPOL_B    0
#define S626_CRABIT_INDXSRC_B   14
#define S626_CRAWID_INDXSRC_B   2
#define S626_CRAWID_CNTSRC_B    2
#define S626_CRABIT_CNTSRC_B    12
#define S626_CRBBIT_INTSRC_B    10
#define S626_CRBWID_INTSRC_B    2
#define S626_MC2_RPSSIG0        0x0800
#define S626_RPS_SIG0           0x00200000
#define S626_RPSCLK_SCALAR      8
#define S626_A2_RUN             0x40000000
#define S626_ACON2_XORMASK      0x000C0000
#define S626_A1_CLKSRC_BCLK1    0x00000000
#define S626_BCLK2_OE           0x00040000
#define S626_A2_CLKSRC_X2       0x00C00000
#define S626_INVERT_BCLK2       0x00100000
#define S626_DEBI_CFG_SWAP_NONE 0x00000000
#define S626_ADC_DMABUF_DWORDS  40
#define S626_WS_MODES           0x00019999
#define S626_A1_RUN             0x20000000

/* QEMU-specific definitions */
#define S626_BAR0_MMIO_SIZE     S626_BAR0_SIZE

/* Internal LP RAM size */
#define S626_LP_RAM_SIZE        0x400

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
    uint32_t ier;
    uint32_t isr;

    /* Shadow registers: full 8KB MMIO space */
    uint32_t mmio[0x800];

    /* LP RAM (DEBI-accessible gate array registers) */
    uint16_t lp_ram[S626_LP_RAM_SIZE];

    /* Timer for DEBI transfer completion */
    QEMUTimer debi_timer;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t active = s->ier & s->isr;

    if (active) {
        if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else if (msix_enabled(pdev)) {
            msix_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!msi_enabled(pdev) && !msix_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

/* DEBI timer callback: called when transfer completes */
static void debi_timer_cb(void *opaque)
{
    PCIBaseState *s = opaque;

    /* Clear DEBI busy status */
    s->mmio[S626_P_PSR >> 2] &= ~S626_PSR_DEBI_S;
    /* Clear the UPLD_DEBI bit in MC2 to indicate completion */
    s->mmio[S626_P_MC2 >> 2] &= ~S626_MC2_UPLD_DEBI;
    /* Set RPS1 interrupt */
    s->isr |= S626_IRQ_RPS1;
    /* Update IRQ line */
    pcibase_update_irq(s);
}

/* MMIO read handler */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t reg_index = addr >> 2;
    uint64_t val = 0;

    if (addr >= S626_BAR0_SIZE) {
        return ~0ULL;
    }

    switch (addr) {
    case S626_P_MC1:
    case S626_P_MC2:
    case S626_P_PSR:
    case S626_P_ISR:
    case S626_P_IER:
    case S626_P_DEBICMD:
    case S626_P_DEBIAD:
    case S626_P_DEBICFG:
    case S626_P_DEBIPAGE:
    case S626_P_GPIO:
    case S626_P_I2CCTRL:
    case S626_P_I2CSTAT:
    case S626_P_ACON1:
    case S626_P_ACON2:
    case S626_P_SSR:
    case S626_P_FB_BUFFER1:
    case S626_P_FB_BUFFER2:
    /* TSL1 registers */
    case S626_P_TSL1:
    case S626_P_TSL1 + 4:
    /* TSL2 vector port registers */
    case S626_VECTPORT(0):
    case S626_VECTPORT(1):
    case S626_VECTPORT(2):
    case S626_VECTPORT(3):
    case S626_VECTPORT(4):
    case S626_VECTPORT(5):
    case S626_P_RPSADDR1:
    case S626_P_RPSPAGE1:
    case S626_P_RPS1_TOUT:
    case S626_P_PCI_BT_A:
    case S626_P_BASEA2_OUT:
    case S626_P_PROTA2_OUT:
    case S626_P_PAGEA2_OUT:
        val = s->mmio[reg_index];
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "s626: read from unimplemented register 0x%"HWADDR_PRIx"\n", addr);
        val = 0;
        break;
    }
    return val;
}

/* MMIO write handler */
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t reg_index = addr >> 2;

    if (addr >= S626_BAR0_SIZE) {
        return;
    }

    switch (addr) {
    case S626_P_MC1:
        s->mmio[reg_index] = val;
        /* Soft reset? Not fully resetting device here; driver sets it and later writes other values. */
        break;
    case S626_P_MC2:
    {
        uint32_t old = s->mmio[reg_index];
        uint32_t set_bits = val & 0xFFFF;
        uint32_t clear_bits = (val >> 16) & 0xFFFF;
        /* Set has priority over clear for same bit */
        uint32_t new_val = (old & ~clear_bits) | set_bits;
        s->mmio[reg_index] = new_val;

        /* Trigger DEBI upload if MC2_UPLD_DEBI set */
        if (set_bits & S626_MC2_UPLD_DEBI) {
            /* Set busy status in PSR */
            s->mmio[S626_P_PSR >> 2] |= S626_PSR_DEBI_S;
            /* Perform DEBI transfer synchronously */
            uint32_t debicmd = s->mmio[S626_P_DEBICMD >> 2];
            uint16_t debi_addr = debicmd & 0xFFFF;
            bool is_read = (debicmd & S626_DEBI_CMD_READ) != 0;

            if (debi_addr < S626_LP_RAM_SIZE) {
                if (is_read) {
                    s->mmio[S626_P_DEBIAD >> 2] = s->lp_ram[debi_addr];
                } else {
                    s->lp_ram[debi_addr] = s->mmio[S626_P_DEBIAD >> 2] & 0xFFFF;
                }
            }
            /* Start a timer to complete the DEBI transfer after a short delay
             * This ensures the driver's polling loop sees PSR_DEBI_S set and then clear,
             * and also raises an interrupt on completion. */
            timer_mod(&s->debi_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 1);
        }
        /* Trigger I2C if MC2_UPLD_IIC set */
        if (set_bits & S626_MC2_UPLD_IIC) {
            /* Set busy bit in I2CSTAT to simulate a very fast I2C transaction */
            s->mmio[S626_P_I2CSTAT >> 2] |= S626_I2C_BUSY;
            /* Optionally set the data byte in I2CCTRL high byte to 0 (simulating successful read) */
            s->mmio[S626_P_I2CCTRL >> 2] = (s->mmio[S626_P_I2CCTRL >> 2] & 0xFF00FFFF) | (0x00 << 16);
            /* Clear busy bit (instant for now, but could also use a timer) */
            s->mmio[S626_P_I2CSTAT >> 2] &= ~S626_I2C_BUSY;
            /* Clear UPLD_IIC bit in MC2 */
            s->mmio[reg_index] &= ~S626_MC2_UPLD_IIC;
        }
        break;
    }
    case S626_P_PSR:
        /* Some bits may be read-only, but we only emulated DEBI_S; ignore writes */
        break;
    case S626_P_ISR:
        /* Write-1-to-clear? Driver writes to clear: writel(irqtype, dev->mmio + S626_P_ISR); */
        s->mmio[reg_index] &= ~val;
        break;
    case S626_P_IER:
        s->mmio[reg_index] = val;
        pcibase_update_irq(s);
        break;
    case S626_P_DEBICMD:
        s->mmio[reg_index] = val;
        break;
    case S626_P_DEBIAD:
        s->mmio[reg_index] = val;
        break;
    case S626_P_DEBICFG:
        s->mmio[reg_index] = val;
        break;
    case S626_P_DEBIPAGE:
        s->mmio[reg_index] = val;
        break;
    case S626_P_GPIO:
        s->mmio[reg_index] = val;
        break;
    case S626_P_I2CCTRL:
        s->mmio[reg_index] = val;
        break;
    case S626_P_I2CSTAT:
        s->mmio[reg_index] = val;
        break;
    case S626_P_ACON1:
        s->mmio[reg_index] = val;
        break;
    case S626_P_ACON2:
        s->mmio[reg_index] = val;
        break;
    case S626_P_SSR:
        /* Some bits read-only? Driver writes to clear? */
        s->mmio[reg_index] = val;
        break;
    case S626_P_FB_BUFFER1:
        s->mmio[reg_index] = val;
        break;
    case S626_P_FB_BUFFER2:
        s->mmio[reg_index] = val;
        break;
    /* TSL1 */
    case S626_P_TSL1:
    case S626_P_TSL1 + 4:
        s->mmio[reg_index] = val;
        break;
    /* TSL2 vector ports */
    case S626_VECTPORT(0):
    case S626_VECTPORT(1):
    case S626_VECTPORT(2):
    case S626_VECTPORT(3):
    case S626_VECTPORT(4):
    case S626_VECTPORT(5):
        s->mmio[reg_index] = val;
        break;
    case S626_P_RPSADDR1:
        s->mmio[reg_index] = val;
        break;
    case S626_P_RPSPAGE1:
        s->mmio[reg_index] = val;
        break;
    case S626_P_RPS1_TOUT:
        s->mmio[reg_index] = val;
        break;
    case S626_P_PCI_BT_A:
        s->mmio[reg_index] = val;
        break;
    case S626_P_BASEA2_OUT:
        s->mmio[reg_index] = val;
        break;
    case S626_P_PROTA2_OUT:
        s->mmio[reg_index] = val;
        break;
    case S626_P_PAGEA2_OUT:
        s->mmio[reg_index] = val;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "s626: write to unimplemented register 0x%"HWADDR_PRIx" = 0x%"PRIx64"\n", addr, val);
        break;
    }
}

/* PIO handlers - keep as stubs since only MMIO is used */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
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

    /* Clear all MMIO registers */
    memset(s->mmio, 0, sizeof(s->mmio));
    /* Reset LP RAM to zero */
    memset(s->lp_ram, 0, sizeof(s->lp_ram));

    /* Set default hardware states */
    s->lp_ram[S626_LP_RDMISC2] = S626_MISC2_BATT_ENABLE; /* Battery enable bit */
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  S626_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  S626_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, S626_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = S626_BAR0_SIZE;
    s->bar_info[0].name = "s626-bar0";

    pci_set_word(pci_conf + PCI_SUBSYSTEM_VENDOR_ID, S626_SUBVENDOR_ID);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_ID, S626_SUBDEVICE_ID);

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize DEBI completion timer */
    timer_init_ms(&s->debi_timer, QEMU_CLOCK_VIRTUAL, debi_timer_cb, s);

    /* Reset state to defaults */
    pcibase_reset(DEVICE(pdev));
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    timer_del(&s->debi_timer);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

static const VMStateDescription vmstate_pcibase = {
    .name = "s626_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32_ARRAY(mmio, PCIBaseState, 0x800),
        VMSTATE_UINT16_ARRAY(lp_ram, PCIBaseState, S626_LP_RAM_SIZE),
        VMSTATE_TIMER(debi_timer, PCIBaseState),
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