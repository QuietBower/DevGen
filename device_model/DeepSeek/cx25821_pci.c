/*
 * QEMU PCI device model for Conexant CX25821
 * Derived from Linux driver: /home/eely/linux-7.1/drivers/media/pci/cx25821/cx25821-core.c
 * This device emulates the MMIO register interface for probing and initialization.
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

#define TYPE_PCIBASE_DEVICE "cx25821_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_CX25821      0x14f1
#define PCI_DEVICE_ID_CX25821      0x8210
#define PCI_CLASS_CX25821          0x040000

/* Register offsets (all from driver source, byte addresses) */
#define RISC_WRITEC         0x50000000
#define RISC_WRITE          0x10000000
#define RISC_SKIP           0x20000000
#define RISC_SYNC           0x80000000
#define RISC_WRITECR        0xD0000000
#define RISC_JUMP           0x70000000
#define RISC_WRITERM        0xB0000000
#define RISC_WRITECM        0xC0000000
#define RISC_READC          0xA0000000
#define RISC_READ           0x90000000
#define RISC_CNT_INC        0x00010000
#define RISC_IRQ1           0x01000000
#define RISC_EOL            0x04000000
#define NO_SYNC_LINE        (-1U)
#define RISC_RESYNC         0x80008000
#define RISC_SOL            0x08000000

#define PLL_B_INT_FRAC            0x110090
#define VID_CH_MODE_SEL           0x110078
#define PLL_C_INT_FRAC            0x110098
#define PLL_C_POST_STAT_BIST      0x11009C
#define PLL_B_POST_STAT_BIST      0x110094
#define FLD_USE_ALT_PLL_REF       0x00004000
#define PCI_INT_MSK               0x040010
#define FLD_VID_I_CLK_NOE         0x00001000
#define PLL_A_POST_STAT_BIST      0x11008C
#define PLL_D_INT_FRAC            0x1100A0
#define FLD_CFG_RCB_CK_EN         0x00000010
#define FLD_VID_J_CLK_NOE         0x00002000
#define VID_CH_CLK_SEL            0x11007C
#define CLK_RST                   0x11002C
#define RDR_TLCTL0                0x050318
#define PLL_D_POST_STAT_BIST      0x1100A4
#define PLL_A_INT_FRAC            0x110088
#define DEV_CNTRL2                0x040000
#define AFE_AB_DIAG_CTRL          0x0164
#define AUD_A_CDT                 0x10E80
#define AUD_A_INT_MSK             0x0400C0
#define AUD_INT_DMA_CTL           0x140500
#define UART_CTL                  0x1B0000
#define AUD_C_INT_STAT            0x0400E4
#define PCI_INT_STAT              0x040014
#define AUD_B_INT_STAT            0x0400D4
#define AUD_A_INT_STAT            0x0400C4
#define PAD_CTRL                  0x110068
#define AUD_E_INT_STAT            0x040104
#define CLK_DELAY                 0x110048
#define RDR_CFG2                  0x050008
#define I2C1_RDATA                0x18000C
#define I2C1_CTRL                 0x180008
#define I2C1_ADDR                 0x180000
#define I2C1_STAT                 0x180010
#define I2C1_WDATA                0x180004
#define DMA7_CNT1                 0x100118
#define DMA8_CNT1                 0x10011C
#define DMA4_PTR2                 0x10008C
#define VID_I_IQ                  0x11200
#define VID_C_INT_MSTAT           0x040048
#define VID_SRC_J_ACTIVE_CTL2     0x130F18
#define DMA6_PTR2                 0x100094
#define VID_I_UP_CLUSTER_1        0x19B00
#define VID_DST_B_DMA_CTL         0x130140
#define VID_DST_E_GPCNT_CTL       0x130430
#define VID_DST_D_DMA_CTL         0x130340
#define VID_A_INT_MSTAT           0x040028
#define VID_I_CDT                 0x10E00
#define VID_H_DOWN_CLUSTER_1      0x09DC0
#define AUD_B_INT_MSK             0x0400D0
#define VID_DST_F_DMA_CTL         0x130540
#define VID_H_IQ                  0x111C0
#define DMA6_PTR1                 0x100014
#define VID_A_DOWN_CLUSTER_1      0x00040
#define VID_G_INT_STAT            0x040084
#define AUD_B_UP_CLUSTER_1        0x1C980
#define VID_DST_D_GPCNT_CTL       0x130330
#define VID_J_CDT                 0x10E40
#define VID_CLUSTER_SIZE          1440
#define VID_SRC_J_DMA_CTL         0x130F0C
#define FLD_AUD_SRC_B_FIFO_EN     0x00000020
#define VID_B_DOWN_CLUSTER_1      0x016C0
#define VID_I_UP_CMDS             0x10460
#define VID_B_INT_STAT            0x040034
#define VID_E_DOWN_CMDS           0x10140
#define DMA1_CNT1                 0x100100
#define DMA5_CNT1                 0x100110
#define VID_SRC_J_ACTIVE_CTL1     0x130F14
#define VID_DST_H_DMA_CTL         0x130740
#define VID_DST_A_GPCNT_CTL       0x130030
#define VID_DST_B_GPCNT           0x130120
#define VID_DST_D_PIX_FRMT        0x130384
#define VID_SRC_J_GPCNT           0x130F08
#define VID_G_CDT                 0x10D80
#define DMA3_CNT1                 0x100108
#define VID_F_CDT                 0x10D40
#define VID_SRC_I_CDT_SZ          0x130E1C
#define VID_H_CDT                 0x10DC0
#define VID_J_IQ                  0x11240
#define VID_DST_B_PIX_FRMT        0x130184
#define VID_A_INT_MSK             0x040020
#define DMA22_PTR2                0x1000D4
#define DMA17_PTR2                0x1000C0
#define VID_SRC_I_ACTIVE_CTL2     0x130E18
#define DMA2_PTR1                 0x100004
#define AUD_B_UP_CMDS             0x10690
#define DMA17_PTR1                0x100040
#define VID_SRC_I_ACTIVE_CTL1     0x130E14
#define VID_A_INT_STAT            0x040024
#define DMA6_CNT1                 0x100114
#define VID_J_UP_CMDS             0x104B0
#define DMA15_PTR1                0x100038
#define VID_DST_C_GPCNT           0x130220
#define FLD_AUD_SRC_B_RISC_EN     0x00002000
#define VID_DST_G_PIX_FRMT        0x130684
#define DMA8_CNT2                 0x10019C
#define VID_SRC_I_FMT_CTL         0x130E10
#define VID_DST_H_GPCNT           0x130720
#define AUD_A_DOWN_CMDS           0x10500
#define DMA15_PTR2                0x1000B8
#define DMA1_PTR2                 0x100080
#define VID_DST_C_VIP_CTL         0x130280
#define VID_D_IQ                  0x110C0
#define VID_DST_C_PIX_FRMT        0x130284
#define VID_E_INT_MSK             0x040060
#define DMA4_CNT1                 0x10010C
#define AUD_B_GPCNT_CTL           0x140114
#define VID_DST_F_GPCNT_CTL       0x130530
#define VID_H_INT_MSK             0x040090
#define VID_E_CDT                 0x10D00
#define VID_DST_C_GPCNT_CTL       0x130230
#define DMA16_PTR2                0x1000BC
#define DMA2_CNT1                 0x100104
#define DMA7_PTR2                 0x100098
#define VID_I_INT_MSTAT           0x0400A8
#define DMA15_CNT2                0x1001B8
#define VID_G_INT_MSK             0x040080
#define VID_D_INT_STAT            0x040054
#define VID_E_INT_STAT            0x040064
#define DMA4_CNT2                 0x10018C
#define VID_B_IQ                  0x11040
#define VID_DST_E_VIP_CTL         0x130480
#define VID_DST_G_VIP_CTL         0x130680
#define VID_C_DOWN_CLUSTER_1      0x02D40
#define VID_SRC_I_GPCNT_CTL       0x130E04
#define VID_DST_F_GPCNT           0x130520
#define DMA17_CNT1                0x100140
#define DMA5_PTR1                 0x100010
#define VID_DST_B_VIP_CTL         0x130180
#define AUD_B_INT_MSTAT           0x0400D8
#define VID_DST_G_GPCNT           0x130620
#define VID_D_INT_MSK             0x040050
#define VID_F_INT_STAT            0x040074
#define VID_D_DOWN_CLUSTER_1      0x043C0
#define DMA16_CNT1                0x10013C
#define VID_SRC_J_FMT_CTL         0x130F10
#define VID_DST_F_VIP_CTL         0x130580
#define VID_B_DOWN_CMDS           0x10050
#define VID_C_DOWN_CMDS           0x100A0
#define VID_A_IQ                  0x11000
#define VID_G_DOWN_CLUSTER_1      0x08740
#define VID_J_INT_MSK             0x0400B0
#define VID_G_IQ                  0x11180
#define DMA22_CNT1                0x100154
#define VID_DST_H_GPCNT_CTL       0x130730
#define VID_F_DOWN_CMDS           0x10190
#define VID_DST_H_PIX_FRMT        0x130784
#define DMA8_PTR1                 0x10001C
#define DMA2_CNT2                 0x100184
#define VID_DST_G_DMA_CTL         0x130640
#define VID_DST_H_VIP_CTL         0x130780
#define VID_DST_E_GPCNT           0x130420
#define DMA3_PTR2                 0x100088
#define VID_A_CDT                 0x10C00
#define VID_DST_G_GPCNT_CTL       0x130630
#define AUD_B_CFG                 0x14011C
#define VID_DST_A_VIP_CTL         0x130080
#define VID_C_INT_MSK             0x040040
#define VID_H_INT_STAT            0x040094
#define VID_J_INT_MSTAT           0x0400B8
#define VID_DST_B_GPCNT_CTL       0x130130
#define AUD_B_CDT                 0x10EB0
#define DMA8_PTR2                 0x10009C
#define DMA17_CNT2                0x1001C0
#define DMA1_PTR1                 0x100000
#define DMA16_PTR1                0x10003C
#define AUD_A_DOWN_CLUSTER_1      0x0B500
#define VID_C_IQ                  0x11080
#define VID_H_DOWN_CMDS           0x10230
#define DMA6_CNT2                 0x100194
#define DMA15_CNT1                0x100138
#define VID_F_IQ                  0x11140
#define VID_DST_A_DMA_CTL         0x130040
#define VID_DST_A_PIX_FRMT        0x130084
#define VID_B_INT_MSK             0x040030
#define VID_SRC_I_GPCNT           0x130E08
#define VID_SRC_J_CDT_SZ          0x130F1C
#define DMA22_PTR1                0x100054
#define VID_F_INT_MSTAT           0x040078
#define AUD_A_IQ                  0x11280
#define VID_DST_D_GPCNT           0x130320
#define AUDIO_CLUSTER_SIZE        128
#define VID_D_DOWN_CMDS           0x100F0
#define VID_SRC_J_GPCNT_CTL       0x130F04
#define VID_F_DOWN_CLUSTER_1      0x070C0
#define DMA5_PTR2                 0x100090
#define VID_DST_C_DMA_CTL         0x130240
#define VID_B_CDT                 0x10C40
#define VID_E_IQ                  0x11100
#define VID_DST_D_VIP_CTL         0x130380
#define VID_DST_E_PIX_FRMT        0x130484
#define VID_G_DOWN_CMDS           0x101E0
#define VID_A_DOWN_CMDS           0x10000
#define DMA3_CNT2                 0x100188
#define VID_DST_F_PIX_FRMT        0x130584
#define VID_I_INT_MSK             0x0400A0
#define AUD_B_LNGTH               0x140118
#define VID_C_INT_STAT            0x040044
#define VID_DST_A_GPCNT           0x130020
#define DMA2_PTR2                 0x100084
#define DMA4_PTR1                 0x10000C
#define AUD_B_IQ                  0x112C0
#define VID_D_INT_MSTAT           0x040058
#define VID_E_DOWN_CLUSTER_1      0x05a40
#define VID_H_INT_MSTAT           0x040098
#define DMA3_PTR1                 0x100008
#define VID_I_INT_STAT            0x0400A4
#define VID_C_CDT                 0x10C80
#define VID_D_CDT                 0x10CC0
#define DMA7_CNT2                 0x100198
#define VID_J_UP_CLUSTER_1        0x1B180
#define VID_F_INT_MSK             0x040070
#define VID_J_INT_STAT            0x0400B4
#define DMA22_CNT2                0x1001D4
#define VID_DST_E_DMA_CTL         0x130440
#define VID_E_INT_MSTAT           0x040068
#define VID_G_INT_MSTAT           0x040088
#define VID_B_INT_MSTAT           0x040038
#define DMA7_PTR1                 0x100018
#define VID_SRC_I_DMA_CTL         0x130E0C
#define CX25821_BOARD_CONEXANT_ATHENA10 1
#define PIXEL_FRMT_411    5
#define DENC_AB_CTRL              0x0114
#define PIN_OE_CTRL               0x013C
#define BYP_AB_CTRL               0x0118
#define MON_A_CTRL                0x011C
#define FLD_VID_DST_RISC1         0x00000001
#define GPIO_LO                   0x110010
#define GPIO_HI                   0x110014
#define DISP_GH_CNT               0x0134
#define DISP_CD_CNT               0x012C
#define DISP_AB_CNT               0x0128
#define DISP_EF_CNT               0x0130
#define DENC_B_REG_4              0x030C
#define DENC_A_REG_4              0x020C
#define GPIO_LO_OE                0x110018
#define GPIO_HI_OE                0x11001C
#define FORMAT_FLAGS_PACKED       0x01
#define LINE_SIZE_D1    1440
#define VERT_TIM_CTRL             0x1024
#define MISC_TIM_CTRL             0x1028
#define DFE_CTRL1                 0x1040
#define DENC_A_REG_7              0x0218
#define DENC_A_REG_3              0x0208
#define OUT_CTRL_NS               0x1008
#define HORIZ_TIM_CTRL            0x1020
#define DENC_A_REG_2              0x0204
#define DENC_A_REG_1              0x0200
#define SC_STEP_SIZE              0x105C
#define MODE_CTRL                 0x1000
#define HSCALE_CTRL               0x1030
#define OUT_CTRL1                 0x1004
#define VSCALE_CTRL               0x1034
#define DENC_A_REG_6              0x0214
#define DENC_A_REG_5              0x0210
#define VDEC_G_OUT_CTRL_NS        0x1C08
#define VDEC_B_OUT_CTRL_NS        0x1208
#define VDEC_E_OUT_CTRL1          0x1804
#define VDEC_H_OUT_CTRL1          0x1E04
#define VDEC_E_OUT_CTRL_NS        0x1808
#define VDEC_C_OUT_CTRL_NS        0x1408
#define VDEC_D_OUT_CTRL1          0x1604
#define VDEC_D_OUT_CTRL_NS        0x1608
#define VDEC_F_OUT_CTRL_NS        0x1A08
#define VDEC_B_OUT_CTRL1          0x1204
#define VDEC_H_OUT_CTRL_NS        0x1E08
#define VDEC_G_OUT_CTRL1          0x1C04
#define VDEC_C_OUT_CTRL1          0x1404
#define VDEC_F_OUT_CTRL1          0x1A04
#define COMB_2D_LF_CFG            0x1070
#define COMB_2D_HFS_CFG           0x1068
#define COMB_2D_HFD_CFG           0x106C
#define COMB_FLAT_THRESH_CTRL     0x107C
#define COMB_MISC_CTRL            0x1078
#define COMB_2D_BLEND             0x1074

#define VID_CHANNEL_NUM 8
#define MAX_VID_CHANNEL_NUM     12
#define MAX_DECODERS            8
#define MAX_VID_CAP_CHANNEL_NUM     10
#define MAX_ENCODERS            2
#define CX25821_MAXBOARDS 2
#define UNSET (-1U)

#define GPIO_SET_BIT(b) (1 << (b))
#define GPIO_CLEAR_BIT(b) (~(1 << (b)))

#define SRAM_CH00  0
#define SRAM_CH01  1
#define SRAM_CH02  2
#define SRAM_CH03  3
#define SRAM_CH04  4
#define SRAM_CH05  5
#define SRAM_CH06  6
#define SRAM_CH07  7
#define SRAM_CH08  8
#define SRAM_CH09  9
#define SRAM_CH10  10
#define SRAM_CH11  11

#define PIXEL_ENGINE_VIP1 0
#define PIXEL_FRMT_422    4

#define VID_UPSTREAM_SRAM_CHANNEL_J     SRAM_CH10
#define VID_UPSTREAM_SRAM_CHANNEL_I     SRAM_CH09

/* Missing register defines moved before use */
#define DMA1_CNT2  0x100180
#define DMA5_CNT2  0x100190
#define DMA16_CNT2 0x1001BC
#define AUD_B_GPCNT 0x140110

/* Struct definitions from driver */
struct cx25821_riscmem;
struct cx25821_buffer;
struct cx25821_channel;
struct cx25821_i2c;
struct cx25821_video_out_data;
struct cx25821_fmt;
struct cx25821_dmaqueue;

enum port {
    CX25821_UNDEFINED = 0,
    CX25821_RAW,
    CX25821_264
};

struct sram_channel {
    const char *name;
    uint32_t i;
    uint32_t cmds_start;
    uint32_t ctrl_start;
    uint32_t cdt;
    uint32_t fifo_start;
    uint32_t fifo_size;
    uint32_t ptr1_reg;
    uint32_t ptr2_reg;
    uint32_t cnt1_reg;
    uint32_t cnt2_reg;
    uint32_t int_msk;
    uint32_t int_stat;
    uint32_t int_mstat;
    uint32_t dma_ctl;
    uint32_t gpcnt_ctl;
    uint32_t gpcnt;
    uint32_t aud_length;
    uint32_t aud_cfg;
    uint32_t fld_aud_fifo_en;
    uint32_t fld_aud_risc_en;

    /* For Upstream Video */
    uint32_t vid_fmt_ctl;
    uint32_t vid_active_ctl1;
    uint32_t vid_active_ctl2;
    uint32_t vid_cdt_size;

    uint32_t vip_ctl;
    uint32_t pix_frmt;
    uint32_t jumponly;
    uint32_t irq_bit;
};

struct cx25821_board {
    const char *name;
    enum port porta;
    enum port portb;
    enum port portc;
    uint32_t clk_freq;
};

/* Static SRAM channel table from driver */
static const struct sram_channel cx25821_sram_channels[] = {
    [SRAM_CH00] = {
        .i = SRAM_CH00,
        .name = "VID A",
        .cmds_start = VID_A_DOWN_CMDS,
        .ctrl_start = VID_A_IQ,
        .cdt = VID_A_CDT,
        .fifo_start = VID_A_DOWN_CLUSTER_1,
        .fifo_size = (VID_CLUSTER_SIZE << 2),
        .ptr1_reg = DMA1_PTR1,
        .ptr2_reg = DMA1_PTR2,
        .cnt1_reg = DMA1_CNT1,
        .cnt2_reg = DMA1_CNT2,
        .int_msk = VID_A_INT_MSK,
        .int_stat = VID_A_INT_STAT,
        .int_mstat = VID_A_INT_MSTAT,
        .dma_ctl = VID_DST_A_DMA_CTL,
        .gpcnt_ctl = VID_DST_A_GPCNT_CTL,
        .gpcnt = VID_DST_A_GPCNT,
        .vip_ctl = VID_DST_A_VIP_CTL,
        .pix_frmt = VID_DST_A_PIX_FRMT,
    },

    [SRAM_CH01] = {
        .i = SRAM_CH01,
        .name = "VID B",
        .cmds_start = VID_B_DOWN_CMDS,
        .ctrl_start = VID_B_IQ,
        .cdt = VID_B_CDT,
        .fifo_start = VID_B_DOWN_CLUSTER_1,
        .fifo_size = (VID_CLUSTER_SIZE << 2),
        .ptr1_reg = DMA2_PTR1,
        .ptr2_reg = DMA2_PTR2,
        .cnt1_reg = DMA2_CNT1,
        .cnt2_reg = DMA2_CNT2,
        .int_msk = VID_B_INT_MSK,
        .int_stat = VID_B_INT_STAT,
        .int_mstat = VID_B_INT_MSTAT,
        .dma_ctl = VID_DST_B_DMA_CTL,
        .gpcnt_ctl = VID_DST_B_GPCNT_CTL,
        .gpcnt = VID_DST_B_GPCNT,
        .vip_ctl = VID_DST_B_VIP_CTL,
        .pix_frmt = VID_DST_B_PIX_FRMT,
    },

    [SRAM_CH02] = {
        .i = SRAM_CH02,
        .name = "VID C",
        .cmds_start = VID_C_DOWN_CMDS,
        .ctrl_start = VID_C_IQ,
        .cdt = VID_C_CDT,
        .fifo_start = VID_C_DOWN_CLUSTER_1,
        .fifo_size = (VID_CLUSTER_SIZE << 2),
        .ptr1_reg = DMA3_PTR1,
        .ptr2_reg = DMA3_PTR2,
        .cnt1_reg = DMA3_CNT1,
        .cnt2_reg = DMA3_CNT2,
        .int_msk = VID_C_INT_MSK,
        .int_stat = VID_C_INT_STAT,
        .int_mstat = VID_C_INT_MSTAT,
        .dma_ctl = VID_DST_C_DMA_CTL,
        .gpcnt_ctl = VID_DST_C_GPCNT_CTL,
        .gpcnt = VID_DST_C_GPCNT,
        .vip_ctl = VID_DST_C_VIP_CTL,
        .pix_frmt = VID_DST_C_PIX_FRMT,
    },

    [SRAM_CH03] = {
        .i = SRAM_CH03,
        .name = "VID D",
        .cmds_start = VID_D_DOWN_CMDS,
        .ctrl_start = VID_D_IQ,
        .cdt = VID_D_CDT,
        .fifo_start = VID_D_DOWN_CLUSTER_1,
        .fifo_size = (VID_CLUSTER_SIZE << 2),
        .ptr1_reg = DMA4_PTR1,
        .ptr2_reg = DMA4_PTR2,
        .cnt1_reg = DMA4_CNT1,
        .cnt2_reg = DMA4_CNT2,
        .int_msk = VID_D_INT_MSK,
        .int_stat = VID_D_INT_STAT,
        .int_mstat = VID_D_INT_MSTAT,
        .dma_ctl = VID_DST_D_DMA_CTL,
        .gpcnt_ctl = VID_DST_D_GPCNT_CTL,
        .gpcnt = VID_DST_D_GPCNT,
        .vip_ctl = VID_DST_D_VIP_CTL,
        .pix_frmt = VID_DST_D_PIX_FRMT,
    },

    [SRAM_CH04] = {
        .i = SRAM_CH04,
        .name = "VID E",
        .cmds_start = VID_E_DOWN_CMDS,
        .ctrl_start = VID_E_IQ,
        .cdt = VID_E_CDT,
        .fifo_start = VID_E_DOWN_CLUSTER_1,
        .fifo_size = (VID_CLUSTER_SIZE << 2),
        .ptr1_reg = DMA5_PTR1,
        .ptr2_reg = DMA5_PTR2,
        .cnt1_reg = DMA5_CNT1,
        .cnt2_reg = DMA5_CNT2,
        .int_msk = VID_E_INT_MSK,
        .int_stat = VID_E_INT_STAT,
        .int_mstat = VID_E_INT_MSTAT,
        .dma_ctl = VID_DST_E_DMA_CTL,
        .gpcnt_ctl = VID_DST_E_GPCNT_CTL,
        .gpcnt = VID_DST_E_GPCNT,
        .vip_ctl = VID_DST_E_VIP_CTL,
        .pix_frmt = VID_DST_E_PIX_FRMT,
    },

    [SRAM_CH05] = {
        .i = SRAM_CH05,
        .name = "VID F",
        .cmds_start = VID_F_DOWN_CMDS,
        .ctrl_start = VID_F_IQ,
        .cdt = VID_F_CDT,
        .fifo_start = VID_F_DOWN_CLUSTER_1,
        .fifo_size = (VID_CLUSTER_SIZE << 2),
        .ptr1_reg = DMA6_PTR1,
        .ptr2_reg = DMA6_PTR2,
        .cnt1_reg = DMA6_CNT1,
        .cnt2_reg = DMA6_CNT2,
        .int_msk = VID_F_INT_MSK,
        .int_stat = VID_F_INT_STAT,
        .int_mstat = VID_F_INT_MSTAT,
        .dma_ctl = VID_DST_F_DMA_CTL,
        .gpcnt_ctl = VID_DST_F_GPCNT_CTL,
        .gpcnt = VID_DST_F_GPCNT,
        .vip_ctl = VID_DST_F_VIP_CTL,
        .pix_frmt = VID_DST_F_PIX_FRMT,
    },

    [SRAM_CH06] = {
        .i = SRAM_CH06,
        .name = "VID G",
        .cmds_start = VID_G_DOWN_CMDS,
        .ctrl_start = VID_G_IQ,
        .cdt = VID_G_CDT,
        .fifo_start = VID_G_DOWN_CLUSTER_1,
        .fifo_size = (VID_CLUSTER_SIZE << 2),
        .ptr1_reg = DMA7_PTR1,
        .ptr2_reg = DMA7_PTR2,
        .cnt1_reg = DMA7_CNT1,
        .cnt2_reg = DMA7_CNT2,
        .int_msk = VID_G_INT_MSK,
        .int_stat = VID_G_INT_STAT,
        .int_mstat = VID_G_INT_MSTAT,
        .dma_ctl = VID_DST_G_DMA_CTL,
        .gpcnt_ctl = VID_DST_G_GPCNT_CTL,
        .gpcnt = VID_DST_G_GPCNT,
        .vip_ctl = VID_DST_G_VIP_CTL,
        .pix_frmt = VID_DST_G_PIX_FRMT,
    },

    [SRAM_CH07] = {
        .i = SRAM_CH07,
        .name = "VID H",
        .cmds_start = VID_H_DOWN_CMDS,
        .ctrl_start = VID_H_IQ,
        .cdt = VID_H_CDT,
        .fifo_start = VID_H_DOWN_CLUSTER_1,
        .fifo_size = (VID_CLUSTER_SIZE << 2),
        .ptr1_reg = DMA8_PTR1,
        .ptr2_reg = DMA8_PTR2,
        .cnt1_reg = DMA8_CNT1,
        .cnt2_reg = DMA8_CNT2,
        .int_msk = VID_H_INT_MSK,
        .int_stat = VID_H_INT_STAT,
        .int_mstat = VID_H_INT_MSTAT,
        .dma_ctl = VID_DST_H_DMA_CTL,
        .gpcnt_ctl = VID_DST_H_GPCNT_CTL,
        .gpcnt = VID_DST_H_GPCNT,
        .vip_ctl = VID_DST_H_VIP_CTL,
        .pix_frmt = VID_DST_H_PIX_FRMT,
    },

    [SRAM_CH08] = {
        .name = "audio from",
        .cmds_start = AUD_A_DOWN_CMDS,
        .ctrl_start = AUD_A_IQ,
        .cdt = AUD_A_CDT,
        .fifo_start = AUD_A_DOWN_CLUSTER_1,
        .fifo_size = AUDIO_CLUSTER_SIZE * 3,
        .ptr1_reg = DMA17_PTR1,
        .ptr2_reg = DMA17_PTR2,
        .cnt1_reg = DMA17_CNT1,
        .cnt2_reg = DMA17_CNT2,
    },

    [SRAM_CH09] = {
        .i = SRAM_CH09,
        .name = "VID Upstream I",
        .cmds_start = VID_I_UP_CMDS,
        .ctrl_start = VID_I_IQ,
        .cdt = VID_I_CDT,
        .fifo_start = VID_I_UP_CLUSTER_1,
        .fifo_size = (VID_CLUSTER_SIZE << 2),
        .ptr1_reg = DMA15_PTR1,
        .ptr2_reg = DMA15_PTR2,
        .cnt1_reg = DMA15_CNT1,
        .cnt2_reg = DMA15_CNT2,
        .int_msk = VID_I_INT_MSK,
        .int_stat = VID_I_INT_STAT,
        .int_mstat = VID_I_INT_MSTAT,
        .dma_ctl = VID_SRC_I_DMA_CTL,
        .gpcnt_ctl = VID_SRC_I_GPCNT_CTL,
        .gpcnt = VID_SRC_I_GPCNT,

        .vid_fmt_ctl = VID_SRC_I_FMT_CTL,
        .vid_active_ctl1 = VID_SRC_I_ACTIVE_CTL1,
        .vid_active_ctl2 = VID_SRC_I_ACTIVE_CTL2,
        .vid_cdt_size = VID_SRC_I_CDT_SZ,
        .irq_bit = 8,
    },

    [SRAM_CH10] = {
        .i = SRAM_CH10,
        .name = "VID Upstream J",
        .cmds_start = VID_J_UP_CMDS,
        .ctrl_start = VID_J_IQ,
        .cdt = VID_J_CDT,
        .fifo_start = VID_J_UP_CLUSTER_1,
        .fifo_size = (VID_CLUSTER_SIZE << 2),
        .ptr1_reg = DMA16_PTR1,
        .ptr2_reg = DMA16_PTR2,
        .cnt1_reg = DMA16_CNT1,
        .cnt2_reg = DMA16_CNT2,
        .int_msk = VID_J_INT_MSK,
        .int_stat = VID_J_INT_STAT,
        .int_mstat = VID_J_INT_MSTAT,
        .dma_ctl = VID_SRC_J_DMA_CTL,
        .gpcnt_ctl = VID_SRC_J_GPCNT_CTL,
        .gpcnt = VID_SRC_J_GPCNT,

        .vid_fmt_ctl = VID_SRC_J_FMT_CTL,
        .vid_active_ctl1 = VID_SRC_J_ACTIVE_CTL1,
        .vid_active_ctl2 = VID_SRC_J_ACTIVE_CTL2,
        .vid_cdt_size = VID_SRC_J_CDT_SZ,
        .irq_bit = 9,
    },

    [SRAM_CH11] = {
        .i = SRAM_CH11,
        .name = "Audio Upstream Channel B",
        .cmds_start = AUD_B_UP_CMDS,
        .ctrl_start = AUD_B_IQ,
        .cdt = AUD_B_CDT,
        .fifo_start = AUD_B_UP_CLUSTER_1,
        .fifo_size = (AUDIO_CLUSTER_SIZE * 3),
        .ptr1_reg = DMA22_PTR1,
        .ptr2_reg = DMA22_PTR2,
        .cnt1_reg = DMA22_CNT1,
        .cnt2_reg = DMA22_CNT2,
        .int_msk = AUD_B_INT_MSK,
        .int_stat = AUD_B_INT_STAT,
        .int_mstat = AUD_B_INT_MSTAT,
        .dma_ctl = AUD_INT_DMA_CTL,
        .gpcnt_ctl = AUD_B_GPCNT_CTL,
        .gpcnt = AUD_B_GPCNT,
        .aud_length = AUD_B_LNGTH,
        .aud_cfg = AUD_B_CFG,
        .fld_aud_fifo_en = FLD_AUD_SRC_B_FIFO_EN,
        .fld_aud_risc_en = FLD_AUD_SRC_B_RISC_EN,
        .irq_bit = 11,
    },
};

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

#define MMIO_SIZE 0x200000

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

    /* DMA Context (unused) */
    struct {
        uint32_t control;
        dma_addr_t addr;
        uint32_t count;
    } dma;

    /* MMIO register file */
    uint32_t *mmio;
};

/* List of Write-1-to-Clear interrupt status registers */
static bool is_w1c_reg(hwaddr addr)
{
    switch (addr) {
    case PCI_INT_STAT:
    case VID_A_INT_STAT:
    case VID_B_INT_STAT:
    case VID_C_INT_STAT:
    case VID_D_INT_STAT:
    case VID_E_INT_STAT:
    case VID_F_INT_STAT:
    case VID_G_INT_STAT:
    case VID_H_INT_STAT:
    case VID_I_INT_STAT:
    case VID_J_INT_STAT:
    case AUD_A_INT_STAT:
    case AUD_B_INT_STAT:
    case AUD_C_INT_STAT:
    case AUD_E_INT_STAT:
        return true;
    default:
        return false;
    }
}

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t stat = s->mmio[PCI_INT_STAT >> 2];
    uint32_t msk = s->mmio[PCI_INT_MSK >> 2];

    if (stat & msk) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* MMIO read handler */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Ensure address is within BAR range and size is 4 bytes */
    if (addr + size > MMIO_SIZE || size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad read addr=0x%" HWADDR_PRIx " size=%d\n",
                      __func__, addr, size);
        return ~0ULL;
    }

    /* Handle special registers that return fixed values */
    if (addr == RDR_CFG2) {
        /* Return a fixed revision (e.g., 0x10) to satisfy the driver */
        return 0x10;
    }

    /* Default: return stored register value */
    val = s->mmio[addr >> 2];
    return val;
}

/* MMIO write handler */
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr + size > MMIO_SIZE || size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad write addr=0x%" HWADDR_PRIx " size=%d val=0x%" PRIx64 "\n",
                      __func__, addr, size, val);
        return;
    }

    /* Handle Write-1-to-Clear registers */
    if (is_w1c_reg(addr)) {
        s->mmio[addr >> 2] &= ~val;
    } else {
        s->mmio[addr >> 2] = val;
    }

    /* After writing PCI_INT_STAT or PCI_INT_MSK, update IRQ line */
    if (addr == PCI_INT_STAT || addr == PCI_INT_MSK) {
        pcibase_update_irq(s);
    }
}

/* Unused PIO handlers (driver doesn't use PIO) */
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

    /* Clear all MMIO registers */
    memset(s->mmio, 0, MMIO_SIZE);

    /* Set power-on default for RDR_CFG2 (hardware revision) */
    s->mmio[RDR_CFG2 >> 2] = 0x10;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, 0x14f1);
    pci_set_word(pci_conf + PCI_DEVICE_ID, 0x8210);
    pci_config_set_class(pci_conf, PCI_CLASS_MULTIMEDIA_VIDEO);
    pci_set_byte(pci_conf + PCI_CLASS_PROG, 0x00);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = MMIO_SIZE;
    s->bar_info[0].name = "cx25821-mmio";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Allocate flat MMIO register file */
    s->mmio = g_malloc0(MMIO_SIZE);
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
    g_free(s->mmio);
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "cx25821_pci",
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
