/*
 * QEMU PCI device model for cx23885-based device
 * Phase 2: Behavioral implementation based on Linux driver expectations.
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
#include "hw/hw.h"

#define TYPE_PCIBASE_DEVICE "cx23885_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define CX23885_VENDOR_ID 0x14f1
#define CX23885_DEVICE_ID 0x8852
#define CX23885_PCI_CLASS 0x0400

#define PCI_INT_MSK              0x00040010
#define PCI_INT_STAT             0x00040014
#define VID_A_INT_MSK            0x00040020
#define VID_A_INT_STAT           0x00040024
#define VID_B_INT_MSK            0x00040030
#define VID_B_INT_STAT           0x00040034
#define VID_B_INT_MSTAT          0x00040038
#define VID_B_INT_SSTAT          0x0004003C
#define VID_C_INT_MSK            0x00040040
#define VID_C_INT_STAT           0x00040044
#define VID_C_INT_MSTAT          0x00040048
#define VID_C_INT_SSTAT          0x0004004C
#define AUDIO_INT_INT_MSK        0x00040050
#define AUDIO_INT_INT_STAT       0x00040054
#define AUDIO_EXT_INT_MSK        0x00040060
#define AUDIO_EXT_INT_STAT       0x00040064

#define DEV_CNTRL2               0x00040000
#define TC_REQ                   0x00040090
#define TC_REQ_SET               0x00040094

#define VID_A_DMA_CTL            0x00130040
#define VID_A_GPCNT              0x00130020
#define VID_A_VBI_CTRL           0x00130088
#define VBI_A_GPCNT              0x00130024
#define VBI_A_GPCNT_CTL          0x00130034

#define VID_B_DMA                0x00130100
#define VID_B_DMA_CTL            0x00130140
#define VID_B_LNGTH              0x00130150
#define VID_B_HW_SOP_CTL         0x00130154
#define VID_B_GEN_CTL            0x00130158
#define VID_B_BD_PKT_STATUS      0x0013015C
#define VID_B_SOP_STATUS         0x00130160
#define VID_B_FIFO_OVFL_STAT     0x00130164
#define VID_B_VLD_MISC           0x00130168
#define VID_B_TS_CLK_EN          0x0013016C
#define VID_B_GPCNT              0x00130120
#define VID_B_GPCNT_CTL          0x00130134
#define VID_B_SRC_SEL            0x00130144

#define VID_C_DMA                0x00130200
#define VID_C_DMA_CTL            0x00130240
#define VID_C_LNGTH              0x00130250
#define VID_C_HW_SOP_CTL         0x00130254
#define VID_C_GEN_CTL            0x00130258
#define VID_C_BD_PKT_STATUS      0x0013025C
#define VID_C_SOP_STATUS         0x00130260
#define VID_C_FIFO_OVFL_STAT     0x00130264
#define VID_C_VLD_MISC           0x00130268
#define VID_C_TS_CLK_EN          0x0013026C
#define VID_C_GPCNT              0x00130220
#define VID_C_GPCNT_CTL          0x00130230

#define AUD_INT_DMA_CTL          0x00140040
#define AUD_INT_A_GPCNT          0x00140020
#define AUD_INT_A_GPCNT_CTL      0x00140030

#define AUD_EXT_DMA_CTL          0x00140140

#define IR_CNTRL_REG             0x00000200

#define UART_CTL                 0x001B0000

#define PAD_CTRL                 0x0011004C
#define CLK_DELAY                0x00110048
#define GP0_IO                   0x00110010
#define GPIO_ISM                 0x00110014
#define MC417_RWD                0x00110020
#define MC417_OEN                0x00110024
#define MC417_CTL                0x00110028
#define ALT_PIN_OUT_SEL          0x0011002C

#define GPIO0                    0x00000001
#define GPIO1                    0x00000002
#define GPIO2                    0x00000004
#define GPIO3                    0x00000008
#define GPIO5                    0x00000020
#define GPIO6                    0x00000040
#define GPIO8                    0x00000100
#define GPIO9                    0x00000200
#define GPIO11                   0x00000800
#define GPIO12                   0x00001000
#define GPIO13                   0x00002000
#define GPIO14                   0x00004000
#define GPIO15                   0x00008000

#define I2C1_ADDR                0x00180000
#define I2C1_WDATA               0x00180004
#define I2C1_CTRL                0x00180008
#define I2C1_RDATA               0x0018000C
#define I2C1_STAT                0x00180010

#define I2C2_ADDR                0x00190000
#define I2C2_WDATA               0x00190004
#define I2C2_CTRL                0x00190008
#define I2C2_RDATA               0x0019000C
#define I2C2_STAT                0x00190010

#define I2C3_ADDR                0x001A0000
#define I2C3_WDATA               0x001A0004
#define I2C3_CTRL                0x001A0008
#define I2C3_RDATA               0x001A000C
#define I2C3_STAT                0x001A0010

#define DMA1_PTR1                0x00100000
#define DMA1_PTR2                0x00100040
#define DMA1_CNT1                0x00100080
#define DMA1_CNT2                0x001000C0

#define DMA2_PTR1                0x00100004
#define DMA2_PTR2                0x00100044
#define DMA2_CNT1                0x00100084
#define DMA2_CNT2                0x001000C4

#define DMA3_PTR1                0x00100008
#define DMA3_PTR2                0x00100048
#define DMA3_CNT1                0x00100088
#define DMA3_CNT2                0x001000C8

#define DMA4_PTR1                0x0010000C
#define DMA4_PTR2                0x0010004C
#define DMA4_CNT1                0x0010008C
#define DMA4_CNT2                0x001000CC

#define DMA5_PTR1                0x00100010
#define DMA5_PTR2                0x00100050
#define DMA5_CNT1                0x00100090
#define DMA5_CNT2                0x001000D0

#define DMA6_PTR1                0x00100014
#define DMA6_PTR2                0x00100054
#define DMA6_CNT1                0x00100094
#define DMA6_CNT2                0x001000D4

#define DMA7_PTR1                0x00100018
#define DMA7_PTR2                0x00100058
#define DMA7_CNT1                0x00100098
#define DMA7_CNT2                0x001000D8

#define DMA8_PTR1                0x0010001C
#define DMA8_PTR2                0x0010005C
#define DMA8_CNT1                0x0010009C
#define DMA8_CNT2                0x001000DC

#define CX23888_IR_CNTRL_REG     0x170000
#define CX23888_IR_TXCLK_REG     0x170004
#define CX23888_IR_RXCLK_REG     0x170008
#define CX23888_IR_IRQEN_REG     0x170014
#define CX23888_IR_FILTR_REG     0x170018

#define PCI_MSK_VID_A            (1 << 0)
#define PCI_MSK_VID_B            (1 << 1)
#define PCI_MSK_VID_C            (1 << 2)
#define PCI_MSK_AUD_INT          (1 << 3)
#define PCI_MSK_AUD_EXT          (1 << 4)
#define PCI_MSK_RISC_RD          (1 << 8)
#define PCI_MSK_RISC_WR          (1 << 9)
#define PCI_MSK_AL_RD            (1 << 10)
#define PCI_MSK_AL_WR            (1 << 11)
#define PCI_MSK_APB_DMA          (1 << 12)
#define PCI_MSK_GPIO0            (1 << 23)
#define PCI_MSK_GPIO1            (1 << 24)
#define PCI_MSK_AV_CORE          (1 << 27)
#define PCI_MSK_IR               (1 << 28)

#define VID_B_MSK_RISCI1         0x00000001
#define VID_B_MSK_VBI_RISCI1     (1 << 1)
#define VID_B_MSK_OF             (1 << 8)
#define VID_B_MSK_VBI_OF         (1 << 9)
#define VID_B_MSK_SYNC           (1 << 12)
#define VID_B_MSK_VBI_SYNC       (1 << 13)
#define VID_B_MSK_OPC_ERR        (1 << 16)
#define VID_B_MSK_VBI_OPC_ERR    (1 << 17)
#define VID_B_MSK_BAD_PKT        (1 << 20)

#define VID_BC_MSK_RISCI1        0x00000001
#define VID_BC_MSK_OF            (1 << 8)
#define VID_BC_MSK_SYNC          (1 << 12)
#define VID_BC_MSK_OPC_ERR       (1 << 16)
#define VID_BC_MSK_BAD_PKT       (1 << 20)

#define AUD_INT_DN_RISCI1        (1 << 0)
#define AUD_INT_DN_SYNC          (1 << 12)
#define AUD_INT_OPC_ERR          (1 << 16)

#define GP_COUNT_CONTROL_RESET   0x3

#define CX23885_HW_888_IR        (1 << 0)
#define CX23885_HW_AV_CORE       (1 << 1)

#define CX23885_NORMS            0x00000000

#define CX23885_VERSION          "0.0.4"
#define CX23885_MAXBOARDS        8

#define CX23888_IR_RX_HW_FIFO_OVERRUN   2
#define CX23888_IR_RX_FIFO_SERVICE_REQ  0
#define CX23888_IR_RX_SW_FIFO_OVERRUN   3
#define CX23888_IR_RX_END_OF_RX_DETECTED 1
#define CX23888_IR_TX_FIFO_SERVICE_REQ  0

#define CX23888_VIDCLK_FREQ      108000000
#define CX23888_IR_REFCLK_FREQ   (CX23888_VIDCLK_FREQ / 2)

#define RXCLK_RCD                 0x0000FFFF
#define TXCLK_TCD                 0x0000FFFF

#define IRQEN_RTE                 0x00000001
#define IRQEN_ROE                 0x00000002
#define IRQEN_RSE                 0x00000010
#define IRQEN_TSE                 0x00000020

#define CNTRL_EDG_NONE            0x00000000
#define CNTRL_EDG_BOTH            0x0000000C
#define CNTRL_DMD                 0x00000010
#define CNTRL_MOD                 0x00000020
#define CNTRL_RFE                 0x00000040
#define CNTRL_TFE                 0x00000080
#define CNTRL_RXE                 0x00000100
#define CNTRL_TXE                 0x00000200

#define FILTR_LPF                 0x0000FFFF

#define FLD_CH_SEL                (1 << 3)

#define CH_PWR_CTRL1              0x0000000E
#define CH_PWR_CTRL2              0x0000000F

#define FORMAT_FLAGS_PACKED       0x01

#define MAX_CX23885_INPUT         8

#define RISC_READC                0xA0000000
#define RISC_WRITE                0x10000000
#define RISC_SYNC                 0x80000000
#define RISC_WRITECR              0xD0000000
#define RISC_READ                 0x90000000
#define RISC_WRITECM              0xC0000000
#define RISC_WRITEC               0x50000000
#define RISC_JUMP                 0x70000000
#define RISC_WRITERM              0xB0000000
#define RISC_SKIP                 0x20000000
#define RISC_SOL                  0x08000000
#define RISC_RESYNC               0x80008000
#define RISC_EOL                  0x04000000
#define RISC_IRQ1                 0x01000000
#define RISC_CNT_RESET            0x00030000
#define RISC_CNT_INC              0x00010000

#define VBI_B_DMA                 0x00130108
#define VBI_C_DMA                 0x00130208

#define VBI_NTSC_LINE_COUNT       12
#define VBI_PAL_LINE_COUNT        18
#define VBI_LINE_LENGTH           1440

#define CX23888_IR_IRQEN_REG      0x170014

#define MCI_REGISTER_DATA_BYTE0   0x800
#define MCI_REGISTER_DATA_BYTE1   0x900
#define MCI_REGISTER_DATA_BYTE2   0xA00
#define MCI_REGISTER_DATA_BYTE3   0xB00
#define MCI_REGISTER_ADDRESS_BYTE0 0xC00
#define MCI_REGISTER_ADDRESS_BYTE1 0xD00
#define MCI_REGISTER_MODE         0xE00
#define MCI_MODE_REGISTER_READ    0
#define MCI_MODE_REGISTER_WRITE   1

#define MCI_MEMORY_DATA_BYTE0     0x000
#define MCI_MEMORY_DATA_BYTE1     0x100
#define MCI_MEMORY_DATA_BYTE2     0x200
#define MCI_MEMORY_DATA_BYTE3     0x300
#define MCI_MEMORY_ADDRESS_BYTE0  0x600
#define MCI_MEMORY_ADDRESS_BYTE1  0x500
#define MCI_MEMORY_ADDRESS_BYTE2  0x400
#define MCI_MODE_MEMORY_READ      0
#define MCI_MODE_MEMORY_WRITE     0x40

#define MC417_MIDATA              0x00FF
#define MC417_MICS                0x2000
#define MC417_MIRD                0x4000
#define MC417_MIWR                0x8000
#define MC417_MIRDY               0x1000
#define MC417_SPD_CTL(x)          (((x) << 4) & 0x00000030)
#define MC417_SPD_CTL_FAST        0x3
#define MC417_GPIO_SEL(x)         (((x) << 1) & 0x00000006)
#define MC417_GPIO_SEL_GPIO3      0x3
#define MC417_UART_GPIO_EN        0x00000001

#define SP2_RD                    0x00004000
#define SP2_WR                    0x00008000
#define SP2_DATA                  0x000000ff
#define SP2_ADHI                  0x00000800
#define SP2_ADLO                  0x00000400
#define SP2_CS0                   0x00000100
#define SP2_CS1                   0x00000200
#define SP2_EN_ALL                0x00001000
#define SP2_ACK                   0x00001000
#define SP2_CTRL_OFF              (SP2_CS1 | SP2_CS0 | SP2_WR | SP2_RD)

#define NETUP_RD                  0x00004000
#define NETUP_WR                  0x00008000
#define NETUP_DATA                0x000000ff
#define NETUP_ADHI                0x00000800
#define NETUP_ADLO                0x00000400
#define NETUP_CS0                 0x00000100
#define NETUP_CS1                 0x00000200
#define NETUP_EN_ALL              0x00001000
#define NETUP_ACK                 0x00001000
#define NETUP_CTRL_OFF            (NETUP_CS1 | NETUP_CS0 | NETUP_WR | NETUP_RD)

#define ALT_AD_RG                 0x00000200
#define ALT_RDY                   0x00001000
#define ALT_CS                    0x00000100
#define ALT_WR                    0x00000400
#define ALT_RD                    0x00000800
#define ALT_DATA                  0x000000ff

#define GP0_IO_GPIO_0             GPIO0

#define CX23885_FIRM_IMAGE_NAME   "v4l-cx23885-enc.fw"
#define CX23885_FIRM_IMAGE_SIZE   376836

#define MAX_XFER_SIZE             64

#define EEPROM_I2C_ADDR           0x50

#define MODULE_NAME               "cx23885"

#define CX23885_BOARD_UNKNOWN                  0
#define CX23885_BOARD_HAUPPAUGE_HVR1800        2
#define CX23885_BOARD_HAUPPAUGE_HVR1500        6
#define CX23885_BOARD_HAUPPAUGE_HVR1500Q       5
#define CX23885_BOARD_HAUPPAUGE_HVR1700        8
#define CX23885_BOARD_HAUPPAUGE_HVR1400        9
#define CX23885_BOARD_HAUPPAUGE_HVR1200        7
#define CX23885_BOARD_DVICO_FUSIONHDTV_5_EXP   4
#define CX23885_BOARD_DVICO_FUSIONHDTV_7_DUAL_EXP 10
#define CX23885_BOARD_DVICO_FUSIONHDTV_DVB_T_DUAL_EXP 11
#define CX23885_BOARD_DVICO_FUSIONHDTV_DVB_T_DUAL_EXP2 44
#define CX23885_BOARD_TBS_6920                 14
#define CX23885_BOARD_TEVII_S470               15
#define CX23885_BOARD_DVBWORLD_2005            16
#define CX23885_BOARD_NETUP_DUAL_DVBS2_CI      17
#define CX23885_BOARD_HAUPPAUGE_HVR1270        18
#define CX23885_BOARD_HAUPPAUGE_HVR1275        19
#define CX23885_BOARD_HAUPPAUGE_HVR1255        20
#define CX23885_BOARD_HAUPPAUGE_HVR1210        21
#define CX23885_BOARD_MYGICA_X8506             22
#define CX23885_BOARD_MAGICPRO_PROHDTVE2       23
#define CX23885_BOARD_HAUPPAUGE_HVR1850        24
#define CX23885_BOARD_COMPRO_VIDEOMATE_E800    25
#define CX23885_BOARD_HAUPPAUGE_HVR1290        26
#define CX23885_BOARD_MYGICA_X8558PRO          27
#define CX23885_BOARD_LEADTEK_WINFAST_PXTV1200 28
#define CX23885_BOARD_GOTVIEW_X5_3D_HYBRID     29
#define CX23885_BOARD_NETUP_DUAL_DVB_T_C_CI_RF 30
#define CX23885_BOARD_LEADTEK_WINFAST_PXDVR3200_H_XC4000 31
#define CX23885_BOARD_MPX885                   32
#define CX23885_BOARD_MYGICA_X8507             33
#define CX23885_BOARD_TERRATEC_CINERGY_T_PCIE_DUAL 34
#define CX23885_BOARD_TEVII_S471               35
#define CX23885_BOARD_HAUPPAUGE_HVR1255_22111  36
#define CX23885_BOARD_PROF_8000                37
#define CX23885_BOARD_HAUPPAUGE_HVR4400        38
#define CX23885_BOARD_AVERMEDIA_HC81R          39
#define CX23885_BOARD_TBS_6981                 40
#define CX23885_BOARD_TBS_6980                 41
#define CX23885_BOARD_LEADTEK_WINFAST_PXPVR2200 42
#define CX23885_BOARD_HAUPPAUGE_IMPACTVCBE     43
#define CX23885_BOARD_DVBSKY_S950              49
#define CX23885_BOARD_DVBSKY_S952              50
#define CX23885_BOARD_DVBSKY_T982              51
#define CX23885_BOARD_HAUPPAUGE_HVR5525        52
#define CX23885_BOARD_HAUPPAUGE_STARBURST      53
#define CX23885_BOARD_VIEWCAST_260E            54
#define CX23885_BOARD_VIEWCAST_460E            55
#define CX23885_BOARD_HAUPPAUGE_QUADHD_DVB     56
#define CX23885_BOARD_HAUPPAUGE_QUADHD_ATSC    57
#define CX23885_BOARD_HAUPPAUGE_HVR1265_K4     58
#define CX23885_BOARD_HAUPPAUGE_STARBURST2     59
#define CX23885_BOARD_HAUPPAUGE_QUADHD_DVB_885 60
#define CX23885_BOARD_HAUPPAUGE_QUADHD_ATSC_885 61
#define CX23885_BOARD_AVERMEDIA_CE310B         62
#define CX23885_BOARD_AVERMEDIA_H789C          63

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

    /* Hardware Register Shadows */
    struct {
        uint32_t pci_int_msk;
        uint32_t pci_int_stat;
        uint32_t dev_cntrl2;
        uint32_t vid_a_int_msk;
        uint32_t vid_a_int_stat;
        uint32_t vid_b_int_msk;
        uint32_t vid_b_int_stat;
        uint32_t vid_c_int_msk;
        uint32_t vid_c_int_stat;
        uint32_t audio_int_msk;
        uint32_t audio_int_stat;
        uint32_t audio_ext_msk;
        uint32_t audio_ext_stat;
        uint32_t ir_cntrl;
        uint32_t uart_ctl;
        uint32_t pad_ctrl;
        uint32_t clk_delay;
        uint32_t gp0_io;
        uint32_t gpio_ism;
        uint32_t mc417_rwd;
        uint32_t mc417_oen;
        uint32_t mc417_ctl;
        uint32_t alt_pin_out_sel;
        uint32_t i2c1_addr;
        uint32_t i2c1_wdata;
        uint32_t i2c1_ctrl;
        uint32_t i2c1_rdata;
        uint32_t i2c1_stat;
        uint32_t i2c2_addr;
        uint32_t i2c2_wdata;
        uint32_t i2c2_ctrl;
        uint32_t i2c2_rdata;
        uint32_t i2c2_stat;
        uint32_t i2c3_addr;
        uint32_t i2c3_wdata;
        uint32_t i2c3_ctrl;
        uint32_t i2c3_rdata;
        uint32_t i2c3_stat;
    } regs;
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* If any enabled PCI interrupts are pending, raise INTx */
    if (s->regs.pci_int_stat & s->regs.pci_int_msk) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* MMIO/PIO Handlers */
static uint32_t pcibase_mmio_read32(PCIBaseState *s, hwaddr addr)
{
    switch (addr) {
    case DEV_CNTRL2:
        return s->regs.dev_cntrl2;
    case PCI_INT_MSK:
        return s->regs.pci_int_msk;
    case PCI_INT_STAT:
        return s->regs.pci_int_stat;
    case VID_A_INT_MSK:
        return s->regs.vid_a_int_msk;
    case VID_A_INT_STAT:
        return s->regs.vid_a_int_stat;
    case VID_B_INT_MSK:
        return s->regs.vid_b_int_msk;
    case VID_B_INT_STAT:
        return s->regs.vid_b_int_stat;
    case VID_B_INT_MSTAT:
        /* mirror of VID_B_INT_STAT */
        return s->regs.vid_b_int_stat;
    case VID_B_INT_SSTAT:
        /* secondary status mirror */
        return s->regs.vid_b_int_stat;
    case VID_C_INT_MSK:
        return s->regs.vid_c_int_msk;
    case VID_C_INT_STAT:
        return s->regs.vid_c_int_stat;
    case VID_C_INT_MSTAT:
        return s->regs.vid_c_int_stat;
    case VID_C_INT_SSTAT:
        return s->regs.vid_c_int_stat;
    case AUDIO_INT_INT_MSK:
        return s->regs.audio_int_msk;
    case AUDIO_INT_INT_STAT:
        return s->regs.audio_int_stat;
    case AUDIO_EXT_INT_MSK:
        return s->regs.audio_ext_msk;
    case AUDIO_EXT_INT_STAT:
        return s->regs.audio_ext_stat;

    case VID_A_GPCNT:
        /* simple free-running counter placeholder */
        return 0;
    case VBI_A_GPCNT:
        return 0;
    case VBI_A_GPCNT_CTL:
        return 0;

    case VID_A_DMA_CTL:
        return 0;
    case VID_B_DMA:
        return 0;
    case VID_B_DMA_CTL:
        return 0;
    case VID_B_LNGTH:
        return 0;
    case VID_B_HW_SOP_CTL:
        return 0;
    case VID_B_GEN_CTL:
        return 0;
    case VID_B_BD_PKT_STATUS:
        return 0;
    case VID_B_SOP_STATUS:
        return 0;
    case VID_B_FIFO_OVFL_STAT:
        return 0;
    case VID_B_VLD_MISC:
        return 0;
    case VID_B_TS_CLK_EN:
        return 0;
    case VID_B_GPCNT:
        return 0;
    case VID_B_GPCNT_CTL:
        return 0;
    case VID_B_SRC_SEL:
        return 0;

    case VID_C_DMA:
        return 0;
    case VID_C_DMA_CTL:
        return 0;
    case VID_C_LNGTH:
        return 0;
    case VID_C_HW_SOP_CTL:
        return 0;
    case VID_C_GEN_CTL:
        return 0;
    case VID_C_BD_PKT_STATUS:
        return 0;
    case VID_C_SOP_STATUS:
        return 0;
    case VID_C_FIFO_OVFL_STAT:
        return 0;
    case VID_C_VLD_MISC:
        return 0;
    case VID_C_TS_CLK_EN:
        return 0;
    case VID_C_GPCNT:
        return 0;
    case VID_C_GPCNT_CTL:
        return 0;

    case AUD_INT_DMA_CTL:
        return 0;
    case AUD_INT_A_GPCNT:
        return 0;
    case AUD_INT_A_GPCNT_CTL:
        return 0;

    case AUD_EXT_DMA_CTL:
        return 0;

    case IR_CNTRL_REG:
    case CX23888_IR_CNTRL_REG:
        return s->regs.ir_cntrl;

    case CX23888_IR_TXCLK_REG:
        return 0;
    case CX23888_IR_RXCLK_REG:
        return 0;
    case CX23888_IR_IRQEN_REG:
        /* mirror audio_ext_msk as generic IRQEN for IR */
        return s->regs.audio_ext_msk;
    case CX23888_IR_FILTR_REG:
        return 0;

    case UART_CTL:
        return s->regs.uart_ctl;

    case PAD_CTRL:
        return s->regs.pad_ctrl;
    case CLK_DELAY:
        return s->regs.clk_delay;
    case GP0_IO:
        return s->regs.gp0_io;
    case GPIO_ISM:
        return s->regs.gpio_ism;
    case MC417_RWD:
        return s->regs.mc417_rwd;
    case MC417_OEN:
        return s->regs.mc417_oen;
    case MC417_CTL:
        return s->regs.mc417_ctl;
    case ALT_PIN_OUT_SEL:
        return s->regs.alt_pin_out_sel;

    case I2C1_ADDR:
        return s->regs.i2c1_addr;
    case I2C1_WDATA:
        return s->regs.i2c1_wdata;
    case I2C1_CTRL:
        return s->regs.i2c1_ctrl;
    case I2C1_RDATA:
        return s->regs.i2c1_rdata;
    case I2C1_STAT:
        return s->regs.i2c1_stat;

    case I2C2_ADDR:
        return s->regs.i2c2_addr;
    case I2C2_WDATA:
        return s->regs.i2c2_wdata;
    case I2C2_CTRL:
        return s->regs.i2c2_ctrl;
    case I2C2_RDATA:
        return s->regs.i2c2_rdata;
    case I2C2_STAT:
        return s->regs.i2c2_stat;

    case I2C3_ADDR:
        return s->regs.i2c3_addr;
    case I2C3_WDATA:
        return s->regs.i2c3_wdata;
    case I2C3_CTRL:
        return s->regs.i2c3_ctrl;
    case I2C3_RDATA:
        return s->regs.i2c3_rdata;
    case I2C3_STAT:
        return s->regs.i2c3_stat;

    case TC_REQ:
        /* emulate no DMA in progress */
        return 0;
    case TC_REQ_SET:
        return 0;

    default:
        break;
    }

    /* Unknown register: return 0, log for debug */
    qemu_log_mask(LOG_GUEST_ERROR, "cx23885: read32 unknown addr 0x%08" HWADDR_PRIx "\n", addr);
    return 0;
}

static void pcibase_mmio_write32(PCIBaseState *s, hwaddr addr, uint32_t val)
{
    switch (addr) {
    case DEV_CNTRL2:
        s->regs.dev_cntrl2 = val;
        break;

    case PCI_INT_MSK:
        s->regs.pci_int_msk |= val;
        pcibase_update_irq(s);
        break;
    case PCI_INT_STAT:
        /* write-1-to-clear */
        s->regs.pci_int_stat &= ~val;
        pcibase_update_irq(s);
        break;

    case VID_A_INT_MSK:
        s->regs.vid_a_int_msk = val;
        break;
    case VID_A_INT_STAT:
        s->regs.vid_a_int_stat &= ~val;
        break;

    case VID_B_INT_MSK:
        s->regs.vid_b_int_msk = val;
        break;
    case VID_B_INT_STAT:
        s->regs.vid_b_int_stat &= ~val;
        break;

    case VID_C_INT_MSK:
        s->regs.vid_c_int_msk = val;
        break;
    case VID_C_INT_STAT:
        s->regs.vid_c_int_stat &= ~val;
        break;

    case AUDIO_INT_INT_MSK:
        s->regs.audio_int_msk = val;
        break;
    case AUDIO_INT_INT_STAT:
        s->regs.audio_int_stat &= ~val;
        break;

    case AUDIO_EXT_INT_MSK:
        s->regs.audio_ext_msk = val;
        break;
    case AUDIO_EXT_INT_STAT:
        s->regs.audio_ext_stat &= ~val;
        break;

    case VID_A_DMA_CTL:
    case VID_A_GPCNT:
    case VID_A_VBI_CTRL:
    case VBI_A_GPCNT:
    case VBI_A_GPCNT_CTL:
    case VID_B_DMA:
    case VID_B_DMA_CTL:
    case VID_B_LNGTH:
    case VID_B_HW_SOP_CTL:
    case VID_B_GEN_CTL:
    case VID_B_BD_PKT_STATUS:
    case VID_B_SOP_STATUS:
    case VID_B_FIFO_OVFL_STAT:
    case VID_B_VLD_MISC:
    case VID_B_TS_CLK_EN:
    case VID_B_GPCNT:
    case VID_B_GPCNT_CTL:
    case VID_B_SRC_SEL:
    case VID_C_DMA:
    case VID_C_DMA_CTL:
    case VID_C_LNGTH:
    case VID_C_HW_SOP_CTL:
    case VID_C_GEN_CTL:
    case VID_C_BD_PKT_STATUS:
    case VID_C_SOP_STATUS:
    case VID_C_FIFO_OVFL_STAT:
    case VID_C_VLD_MISC:
    case VID_C_TS_CLK_EN:
    case VID_C_GPCNT:
    case VID_C_GPCNT_CTL:
    case AUD_INT_DMA_CTL:
    case AUD_INT_A_GPCNT:
    case AUD_INT_A_GPCNT_CTL:
    case AUD_EXT_DMA_CTL:
        /* For now, ignore writes, no DMA implemented */
        break;

    case IR_CNTRL_REG:
    case CX23888_IR_CNTRL_REG:
        s->regs.ir_cntrl = val;
        break;
    case CX23888_IR_TXCLK_REG:
        /* ignore */
        break;
    case CX23888_IR_RXCLK_REG:
        /* ignore */
        break;
    case CX23888_IR_IRQEN_REG:
        s->regs.audio_ext_msk = val;
        break;
    case CX23888_IR_FILTR_REG:
        /* ignore */
        break;

    case UART_CTL:
        s->regs.uart_ctl = val;
        break;

    case PAD_CTRL:
        s->regs.pad_ctrl = val;
        break;
    case CLK_DELAY:
        s->regs.clk_delay = val;
        break;
    case GP0_IO:
        s->regs.gp0_io = val;
        break;
    case GPIO_ISM:
        s->regs.gpio_ism = val;
        break;
    case MC417_RWD:
        s->regs.mc417_rwd = val;
        break;
    case MC417_OEN:
        s->regs.mc417_oen = val;
        break;
    case MC417_CTL:
        s->regs.mc417_ctl = val;
        break;
    case ALT_PIN_OUT_SEL:
        s->regs.alt_pin_out_sel = val;
        break;

    case I2C1_ADDR:
        s->regs.i2c1_addr = val;
        break;
    case I2C1_WDATA:
        s->regs.i2c1_wdata = val;
        break;
    case I2C1_CTRL:
        s->regs.i2c1_ctrl = val;
        /* no real I2C side effects */
        break;
    case I2C1_RDATA:
        s->regs.i2c1_rdata = val;
        break;
    case I2C1_STAT:
        s->regs.i2c1_stat = val;
        break;

    case I2C2_ADDR:
        s->regs.i2c2_addr = val;
        break;
    case I2C2_WDATA:
        s->regs.i2c2_wdata = val;
        break;
    case I2C2_CTRL:
        s->regs.i2c2_ctrl = val;
        break;
    case I2C2_RDATA:
        s->regs.i2c2_rdata = val;
        break;
    case I2C2_STAT:
        s->regs.i2c2_stat = val;
        break;

    case I2C3_ADDR:
        s->regs.i2c3_addr = val;
        break;
    case I2C3_WDATA:
        s->regs.i2c3_wdata = val;
        break;
    case I2C3_CTRL:
        s->regs.i2c3_ctrl = val;
        break;
    case I2C3_RDATA:
        s->regs.i2c3_rdata = val;
        break;
    case I2C3_STAT:
        s->regs.i2c3_stat = val;
        break;

    case TC_REQ:
        /* driver may write back reg1_val, just store */
        break;
    case TC_REQ_SET:
        /* store */
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "cx23885: write32 unknown addr 0x%08" HWADDR_PRIx " val=0x%08x\n", addr, val);
        break;
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size == 4) {
        val = pcibase_mmio_read32(s, addr);
    } else if (size == 1 || size == 2 || size == 8) {
        /* Use 32-bit accesses and mask/shift appropriately */
        hwaddr aligned = addr & ~0x3ULL;
        uint32_t v32 = pcibase_mmio_read32(s, aligned);
        unsigned shift = (addr & 0x3) * 8;
        if (size == 1) {
            val = (v32 >> shift) & 0xff;
        } else if (size == 2) {
            if ((addr & 0x1) != 0) {
                /* unaligned halfword, return 0 */
                val = 0;
            } else {
                val = (v32 >> shift) & 0xffff;
            }
        } else { /* size == 8 */
            uint32_t lo = v32;
            uint32_t hi = pcibase_mmio_read32(s, aligned + 4);
            val = ((uint64_t)hi << 32) | lo;
        }
    } else {
        qemu_log_mask(LOG_GUEST_ERROR, "cx23885: invalid MMIO read size %u at 0x%08" HWADDR_PRIx "\n", size, addr);
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size == 4) {
        pcibase_mmio_write32(s, addr, (uint32_t)val);
    } else if (size == 1 || size == 2 || size == 8) {
        hwaddr aligned = addr & ~0x3ULL;
        uint32_t old = pcibase_mmio_read32(s, aligned);
        uint32_t v32 = old;
        unsigned shift = (addr & 0x3) * 8;
        uint32_t mask;

        if (size == 1) {
            mask = 0xffu << shift;
            v32 = (old & ~mask) | (((uint32_t)val & 0xffu) << shift);
        } else if (size == 2) {
            if ((addr & 0x1) != 0) {
                /* unaligned halfword, ignore */
                return;
            }
            mask = 0xffffu << shift;
            v32 = (old & ~mask) | (((uint32_t)val & 0xffffu) << shift);
        } else { /* size == 8 */
            uint32_t lo = (uint32_t)(val & 0xffffffffu);
            uint32_t hi = (uint32_t)(val >> 32);
            pcibase_mmio_write32(s, aligned, lo);
            pcibase_mmio_write32(s, aligned + 4, hi);
            return;
        }
        pcibase_mmio_write32(s, aligned, v32);
    } else {
        qemu_log_mask(LOG_GUEST_ERROR, "cx23885: invalid MMIO write size %u at 0x%08" HWADDR_PRIx "\n", size, addr);
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* No PIO space is used by the driver; return 0. */
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* No PIO space; ignore writes. */
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

    /* Initialize register shadows to reasonable defaults used by driver */
    s->regs.pci_int_msk = 0;
    s->regs.pci_int_stat = 0;
    s->regs.dev_cntrl2 = 0;
    s->regs.vid_a_int_msk = 0;
    s->regs.vid_a_int_stat = 0;
    s->regs.vid_b_int_msk = 0;
    s->regs.vid_b_int_stat = 0;
    s->regs.vid_c_int_msk = 0;
    s->regs.vid_c_int_stat = 0;
    s->regs.audio_int_msk = 0;
    s->regs.audio_int_stat = 0;
    s->regs.audio_ext_msk = 0;
    s->regs.audio_ext_stat = 0;
    s->regs.ir_cntrl = 0;
    s->regs.uart_ctl = 0;
    /* CLK_DELAY upper bit preserved per reset path */
    s->regs.clk_delay &= 0x80000000u;
    s->regs.pad_ctrl = 0x00500300u;
    pcibase_update_irq(s);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  CX23885_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  CX23885_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CX23885_PCI_CLASS );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR Initialization: one MMIO BAR covering all used registers */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x00200000; /* 2 MB window, as in phase 1 */
    s->bar_info[0].name = "cx23885-mmio";
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

    /* Initialize registers to power-on defaults */
    s->regs.pci_int_msk = 0;
    s->regs.pci_int_stat = 0;
    s->regs.dev_cntrl2 = 0;
    s->regs.vid_a_int_msk = 0;
    s->regs.vid_a_int_stat = 0;
    s->regs.vid_b_int_msk = 0;
    s->regs.vid_b_int_stat = 0;
    s->regs.vid_c_int_msk = 0;
    s->regs.vid_c_int_stat = 0;
    s->regs.audio_int_msk = 0;
    s->regs.audio_int_stat = 0;
    s->regs.audio_ext_msk = 0;
    s->regs.audio_ext_stat = 0;
    s->regs.ir_cntrl = 0;
    s->regs.uart_ctl = 0;
    s->regs.pad_ctrl = 0;
    s->regs.clk_delay = 0;
    s->regs.gp0_io = 0;
    s->regs.gpio_ism = 0;
    s->regs.mc417_rwd = 0;
    s->regs.mc417_oen = 0;
    s->regs.mc417_ctl = 0;
    s->regs.alt_pin_out_sel = 0;
    s->regs.i2c1_addr = 0;
    s->regs.i2c1_wdata = 0;
    s->regs.i2c1_ctrl = 0;
    s->regs.i2c1_rdata = 0;
    s->regs.i2c1_stat = 0;
    s->regs.i2c2_addr = 0;
    s->regs.i2c2_wdata = 0;
    s->regs.i2c2_ctrl = 0;
    s->regs.i2c2_rdata = 0;
    s->regs.i2c2_stat = 0;
    s->regs.i2c3_addr = 0;
    s->regs.i2c3_wdata = 0;
    s->regs.i2c3_ctrl = 0;
    s->regs.i2c3_rdata = 0;
    s->regs.i2c3_stat = 0;
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
    .name = "cx23885_pci",
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

