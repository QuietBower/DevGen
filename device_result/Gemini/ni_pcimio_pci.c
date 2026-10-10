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


#define TYPE_PCIBASE_DEVICE "ni_pcimio_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_NI 0x1093
#define PCIDMA
#define M_SERIES_EEPROM_SIZE		1024
#define MITE_IODWCR_1		0xf4
#define MITE_IODWBSR_1		0xc4
#define MITE_IODWBSR		0xc0
#define NI6143_CALIB_CHAN_RELAY_OFF	(1<<14)
#define NI6143_MAGIC_REG		0x19
#define NISTC_INT_CTRL_REG		59
#define NI6143_EOC_SET_REG		0x1d
#define NI6143_CALIB_CHAN_REG		0x42
#define NI6143_PIPELINE_DELAY_REG	0x1f
#define NI6143_AI_FIFO_FLAG_REG		0x84
#define NUM_PFI_OUTPUT_SELECT_REGS	6
#define MAX_N_CALDACS			34
#define MAX_N_AO_CHAN			8
#define NUM_GPCT			2
#define MAX_MITE_DMA_CHANNELS 8
#define NI_E_STC_WINDOW_DATA_REG	0x02
#define NI_E_STC_WINDOW_ADDR_REG	0x00
#define NISTC_CLK_FOUT_REG		56
#define NI_M_CDO_CMD_RESET		(1<<4)
#define NISTC_CLK_FOUT_SLOW_DIV2	(1<<12)
#define NISTC_INT_CTRL_INTA_ENA		(1<<11)
#define NI_M_DIO_DIR_REG		0x028
#define NISTC_DIO_CTRL_REG		11
#define NI_M_PFI_OUT_SEL_REG(x)		(0x1d0 + ((x) * 2))
#define NISTC_INT_CTRL_INTA_SEL(x)	(((x) & 0x7) << 8)
#define NI_M_CAL_PWM_REG		0x040
#define NI_M_CDI_CMD_RESET		(1<<5)
#define IS_PCIMIO 0
#define NI_E_DMA_G0_G1_SEL_REG		0x0b
#define NISTC_DIO_CTRL_DIR(x)		((x) & 0xff)
#define NI_M_PFI_DO_REG			0x1de
#define NISTC_INT_CTRL_INTB_ENA		(1<<15)
#define NI_M_CDIO_CMD_REG		0x224
#define NISTC_INT_CTRL_INTB_SEL(x)	(((x) & 0x7) << 12)
#define NISTC_CLK_FOUT_SLOW_TIMEBASE	(1<<11)
#define NISTC_INT_CTRL_3PIN_INT		(1<<1)
#define NI_M_AO_CALIB_REG		0x0a3
#define NISTC_CLK_FOUT_AO_OUT_DIV2	(1<<5)
#define NI_E_8255_BASE			0x19
#define NI_E_DMA_AI_AO_SEL_REG		0x09
#define NISTC_CLK_FOUT_AI_OUT_DIV2	(1<<7)
#define NISTC_CLK_FOUT_TO_BOARD_DIV2	(1<<9)
#define NISTC_CLK_FOUT_TO_BOARD		(1<<8)
#define NI_M_AO_REF_ATTENUATION_REG(x)	(0x264 + (x))
#define NISTC_INT_CTRL_INT_POL		(1<<0)
#define NI_M_AO_WAVEFORM_ORDER_REG(x)	(0x0c2 + ((x) * 4))
#define NI611X_MAGIC_REG		0x19
#define NISTC_IO_BIDIR_PIN_REG		57
#define WENAB			(1<<7)
#define NISTC_DIO_IN_REG		7
#define NISTC_DIO_SDIN			(1<<4)
#define NISTC_DIO_SDOUT			(1<<0)
#define NISTC_DIO_OUT_PARALLEL_MASK	NISTC_DIO_OUT_PARALLEL(0xff)
#define NISTC_DIO_OUT_PARALLEL(x)	((x) & 0xff)
#define NISTC_DIO_OUT_REG		10
#define NI_M_CDO_CMD_SW_UPDATE		(1<<19)
#define NI_M_CDO_MODE_HALT_ON_ERROR	(1<<9)
#define NI_M_CDO_MODE_REG		0x22c
#define NI_M_CDO_FIFO_DATA_REG		0x220
#define NI_M_CDO_MODE_FIFO_MODE		(1<<11)
#define NI_M_CDO_MODE_POLARITY		(1<<10)
#define NI_M_CDO_MASK_ENA_REG		0x234
#define NI_M_CDO_MODE_SAMPLE_SRC(x)	(((x) & 0x3f) << 0)
#define NISTC_CLK_FOUT_TO_DIVIDER(x)	(((x) >> 0) & 0xf)
#define CS5529_CFG_WORD_RATE_2180	CS5529_CFG_WORD_RATE(0)
#define CS5529_CFG_CALIB_BOTH_SELF	CS5529_CFG_CALIB(3)
#define CS5529_CFG_PORT_FLAG		(1<<5)
#define CS5529_CFG_CALIB_OFFSET_SELF	CS5529_CFG_CALIB(1)
#define CS5529_CFG_REG			CS5529_CMD_REG(2)
#define CS5529_GAIN_REG			CS5529_CMD_REG(1)
#define NI67XX_AO_SP_UPDATES_REG	0x14
#define NI_E_AO_DACSEL(x)		((x) << 8)
#define NI67XX_AO_CFG2_REG		0x18
#define NISTC_CLK_FOUT_DIVIDER(x)	(((x) & 0xf) << 0)
#define NISTC_CLK_FOUT_ENA		(1<<15)
#define NISTC_CLK_FOUT_DIVIDER_MASK	NISTC_CLK_FOUT_DIVIDER(0xf)
#define NI_M_CDO_CMD_F_REQ_INT_ENA_CLR	(1<<11)
#define NI_M_CDO_CMD_ERR_INT_ENA_CLR	(1<<7)
#define NI_M_CDO_CMD_F_E_INT_ENA_CLR	(1<<17)
#define NI_M_CDO_CMD_DISARM		(1<<0)
#define NI6143_AI_FIFO_CTRL_REG		0x88
#define NI_M_AI_FIFO_DATA_REG		0x01c
#define NI6143_AI_FIFO_STATUS_REG	0x88
#define NISTC_AI_STATUS1_REG		2
#define NI_TIMEOUT 1000
#define NISTC_AI_STATUS1_FIFO_E		(1<<12)
#define NI_E_STATUS_REG			0x01
#define NI6143_AI_FIFO_DATA_REG		0x8c
#define NISTC_AI_CMD1_CONVERT_PULSE	(1<<0)
#define NI611X_AI_FIFO_DATA_REG		0x1c
#define NI_E_AI_FIFO_DATA_REG		0x1c
#define NISTC_AI_CMD1_REG		8
#define NISTC_AI_OUT_CTRL_CONVERT_HIGH		NISTC_AI_OUT_CTRL_CONVERT_SEL(3)
#define NISTC_AI_OUT_CTRL_REG		60
#define NISTC_AI_OUT_CTRL_SC_TC_SEL(x)		(((x) & 0x3) << 2)
#define NI_E_MISC_CMD_EXT_ATRIG		NI_E_MISC_CMD_INTEXT_ATRIG(0)
#define NISTC_AI_MODE3_REG		87
#define NISTC_RESET_AI_CFG_END		(1<<8)
#define NISTC_AI_PERSONAL_SOC_POLARITY		(1<<13)
#define NISTC_AI_MODE1_RSVD		(1<<2)
#define NISTC_AI_PERSONAL_LOCALMUX_CLK_PW	(1<<5)
#define NISTC_AI_OUT_CTRL_CONVERT_LOW		NISTC_AI_OUT_CTRL_CONVERT_SEL(2)
#define NISTC_AI_MODE3_FIFO_MODE_NE	NISTC_AI_MODE3_FIFO_MODE(0)
#define NISTC_AI_OUT_CTRL_LOCALMUX_CLK_SEL(x)	(((x) & 0x3) << 4)
#define NISTC_RESET_AI			(1<<0)
#define NISTC_AI_PERSONAL_REG		77
#define NISTC_AI_MODE2_REG		13
#define NISTC_AI_PERSONAL_SHIFTIN_PW		(1<<15)
#define NISTC_AI_OUT_CTRL_EXTMUX_CLK_SEL(x)	(((x) & 0x3) << 6)
#define NISTC_AI_MODE1_REG		12
#define NISTC_INTA_ACK_REG		2
#define NISTC_RESET_AI_CFG_START	(1<<4)
#define NISTC_AI_PERSONAL_CONVERT_PW		(1<<10)
#define NISTC_AI_CMD1_DISARM		(1<<13)
#define NI_E_MISC_CMD_REG		0x0f
#define NISTC_AI_MODE1_START_STOP	(1<<3)
#define NISTC_AI_OUT_CTRL_SCAN_IN_PROG_SEL(x)	(((x) & 0x3) << 8)
#define NISTC_INTA_ENA_REG		73
#define NISTC_RESET_REG			72
#define NISTC_DIO_CTRL_DIR_MASK		NISTC_DIO_CTRL_DIR(0xff)
#define NI_M_PFI_DI_REG			0x1dc
#define NI_M_DIO_REG			0x024
#define NI67XX_AO_CAL_CHAN_SEL_REG	0x17
#define NI611X_CAL_GAIN_SEL_REG		0x05
#define NISTC_AO_MODE2_REG		39
#define NISTC_AO_OUT_CTRL_REG		86
#define NISTC_AO_CMD2_REG		5
#define NISTC_AO_MODE1_REG		38
#define NISTC_AO_PERSONAL_REG		78
#define NISTC_RESET_AO_CFG_END		(1<<9)
#define NISTC_AO_PERSONAL_BC_SRC_SEL		(1<<4)
#define NISTC_INTB_ACK_REG		3
#define NI611X_AO_MISC_CLEAR_WG		(1<<0)
#define NISTC_AO_START_SEL_REG		66
#define NISTC_AO_MODE3_REG		70
#define NISTC_RESET_AO_CFG_START	(1<<5)
#define NI611X_AO_MISC_REG		0x16
#define NISTC_RESET_AO			(1<<1)
#define NISTC_AO_CMD1_REG		9
#define NISTC_AO_CMD1_DISARM		(1<<13)
#define NISTC_INTB_ENA_REG		75
#define NISTC_AO_MODE3_LAST_GATE_DISABLE	(1<<0)
#define NI671X_AO_IMMEDIATE_REG		0x11
#define NISTC_AO_TRIG_SEL_REG		67
#define NISTC_CLK_FOUT_TIMEBASE_SEL	(1<<14)
#define NI_M_CAL_PWM_HIGH_TIME(x)	(((x) & 0xffff) << 16)
#define NI_M_CAL_PWM_LOW_TIME(x)	(((x) & 0xffff) << 0)
#define NI6143_CALIB_HI_TIME_REG	0x22
#define NI6143_CALIB_LO_TIME_REG	0x20
#define NISTC_AI_TRIG_START1_SEL(x)	(((x) & 0x1f) << 0)
#define NISTC_AI_MODE2_SC_RELOAD_MODE	(1<<1)
#define NISTC_AI_START_SYNC		(1<<6)
#define NISTC_AI_START_EDGE		(1<<5)
#define NISTC_INTA_ENA_AI_SC_TC		(1<<0)
#define NISTC_AI_MODE2_SI2_RELOAD_MODE	(1<<8)
#define NISTC_AI_CMD1_SI_LOAD		(1<<9)
#define NISTC_AI_SI2_LOADB_REG		25
#define NISTC_ATRIG_ETC_ENA		(1<<3)
#define NISTC_AI_TRIG_START2_SEL(x)	(((x) & 0x1f) << 7)
#define NISTC_AI_MODE2_SI_RELOAD_MODE(x) (((x) & 0x7) << 4)
#define NISTC_AI_MODE3_FIFO_MODE_HF	NISTC_AI_MODE3_FIFO_MODE(1)
#define NISTC_AI_TRIG_START1_EDGE	(1<<5)
#define NISTC_AI_MODE2_SI2_INIT_LOAD_SRC (1<<9)
#define NISTC_AI_CMD2_START1_PULSE	(1<<0)
#define NISTC_AI_CMD1_SI_ARM		(1<<10)
#define NISTC_AI_STOP_SYNC		(1<<13)
#define NISTC_AI_CMD1_SC_ARM		(1<<6)
#define NISTC_AI_STOP_SEL(x)		(((x) & 0x1f) << 7)
#define NISTC_AI_MODE1_CONTINUOUS	(1<<1)
#define NISTC_AI_SI_LOADA_REG		14
#define NISTC_AI_SC_LOADA_REG		18
#define NISTC_AI_TRIG_SEL_REG		63
#define NISTC_AI_START_POLARITY		(1<<15)
#define NISTC_AI_MODE2_SC_GATE_ENA	(1<<15)
#define NISTC_INTA_ENA_AI_ERR		(1<<5)
#define NISTC_AI_STOP_POLARITY		(1<<14)
#define NISTC_AI_TRIG_START1_POLARITY	(1<<15)
#define NISTC_AI_MODE1_TRIGGER_ONCE	(1<<0)
#define NISTC_AI_STOP_EDGE		(1<<12)
#define NISTC_AI_MODE2_START_STOP_GATE_ENA (1<<14)
#define NISTC_AI_MODE2_SI_INIT_LOAD_SRC	(1<<7)
#define NISTC_AI_CMD1_SC_LOAD		(1<<5)
#define NISTC_AI_CMD2_REG		4
#define NISTC_AI_MODE2_SC_INIT_LOAD_SRC	(1<<2)
#define NISTC_AI_SI2_LOADA_REG		23
#define NISTC_INTA_ENA_AI_STOP		(1<<4)
#define NISTC_AI_START_SEL(x)		(((x) & 0x1f) << 0)
#define NISTC_AI_MODE2_PRE_TRIGGER	(1<<13)
#define NISTC_AI_START_STOP_REG		62
#define NISTC_AI_MODE1_CONVERT_POLARITY	(1<<5)
#define NISTC_AI_CMD2_END_ON_EOS	(1<<14)
#define NISTC_AI_TRIG_START1_SYNC	(1<<6)
#define NISTC_AI_CMD1_DIV_ARM		(1<<8)
#define NISTC_AI_CMD1_SI2_LOAD		(1<<11)
#define NISTC_AI_MODE1_CONVERT_SRC(x)	(((x) & 0x1f) << 11)
#define NISTC_INTA_ENA_AI_FIFO		(1<<7)
#define NISTC_ATRIG_ETC_REG		61
#define NISTC_AI_CMD1_SI2_ARM		(1<<12)
#define NISTC_AI_MODE3_FIFO_MODE_HF_E	NISTC_AI_MODE3_FIFO_MODE(3)
#define NISTC_DIO_CTRL_HW_SER_ENA	(1<<9)
#define SERIAL_1_2US		1200
#define NISTC_DIO_CTRL_HW_SER_TIMEBASE	(1<<10)
#define SERIAL_600NS		600
#define SERIAL_DISABLED		0
#define NISTC_DIO_SDCLK			(1<<11)
#define SERIAL_10US			10000
#define NISTC_CLK_FOUT_DIO_SER_OUT_DIV2	(1<<13)
#define NI_M_DAC_DIRECT_DATA_REG(x)	(0x0c0 + ((x) * 4))
#define NI671X_DAC_DIRECT_DATA_REG(x)	(0x00 + (x))
#define NI_E_DAC_DIRECT_DATA_REG(x)	(0x18 + ((x) * 2))
#define NI_M_CLK_FOUT2_RTSI_10MHZ	(1<<7)
#define NISTC_ATRIG_ETC_GPFO_1_ENA	(1<<15)
#define NISTC_ATRIG_ETC_GPFO_1_SEL	(1<<7)
#define NISTC_ATRIG_ETC_GPFO_0_ENA	(1<<14)
#define NISTC_ATRIG_ETC_GPFO_0_SEL(x)	(((x) & 0x7) << 11)
#define NISTC_G1_LOADA_REG		32
#define NISTC_AO_UI_LOADA_REG		40
#define NISTC_G1_INPUT_SEL_REG		37
#define NISTC_AO_UC_LOADA_REG		48
#define NISTC_RTSI_TRIG_DIR_REG		58
#define NISTC_G1_LOADB_REG		34
#define NISTC_G0_CMD_REG		6
#define NISTC_G0_LOADB_REG		30
#define NISTC_ADC_FIFO_CLR_REG		83
#define NISTC_G0_AUTOINC_REG		68
#define NISTC_RTSI_TRIGB_OUT_REG	80
#define NISTC_INTA2_ENA_REG		74
#define NISTC_AI_SC_LOADB_REG		20
#define NISTC_G0_LOADA_REG		28
#define NISTC_G0_INPUT_SEL_REG		36
#define NISTC_AO_UC_LOADB_REG		50
#define NISTC_AI_DIV_LOADA_REG		64
#define NISTC_CFG_MEM_CLR_REG		82
#define NISTC_G1_MODE_REG		27
#define NISTC_RTSI_BOARD_REG		81
#define NISTC_G0_MODE_REG		26
#define NISTC_DAC_FIFO_CLR_REG		84
#define NISTC_AO_UI_LOADB_REG		42
#define NISTC_G1_CMD_REG		7
#define NISTC_RTSI_TRIGA_OUT_REG	79
#define NISTC_G1_AUTOINC_REG		69
#define NISTC_AO_BC_LOADA_REG		44
#define NISTC_AI_SI_LOADB_REG		16
#define NISTC_AO_BC_LOADB_REG		46
#define NISTC_INTB2_ENA_REG		76
#define NI_M_PFI_FILTER_SEL_MASK(_c)	NI_M_PFI_FILTER_SEL((_c), 0x3)
#define NI_M_PFI_FILTER_SEL(_c, _f)	(((_f) & 0x3) << ((_c) * 2))
#define NI_M_PFI_FILTER_REG		0x0b0
#define NI_M_CDIO_DMA_SEL_CDO(x)	(((x) & 0xf) << 4)
#define NI_STC_DMA_CHAN_SEL(x)	(((x) < 4) ? (1<<(x)) :	\
				 ((x) == 4) ? 0x3 :	\
				 ((x) == 5) ? 0x5 : 0x0)
#define NI_M_CDIO_DMA_SEL_CDO_MASK	NI_M_CDIO_DMA_SEL_CDO(0xf)
#define NI_M_CDIO_DMA_SEL_REG		0x007
#define NI_M_CDO_CMD_ARM		(1<<1)
#define NI_M_CDIO_STATUS_CDO_FIFO_FULL	(1<<1)
#define NI_M_CDO_CMD_ERR_INT_ENA_SET	(1<<6)
#define NI_M_CDO_CMD_F_E_INT_ENA_SET	(1<<16)
#define NI_M_CDIO_STATUS_REG		0x224
#define NI_E_DMA_G0_G1_SEL_MASK(_g)	NI_E_DMA_G0_G1_SEL((_g), 0xf)
#define NI_E_DMA_G0_G1_SEL(_g, _c)	(((_c) & 0xf) << ((_g) * 4))
#define NISTC_INTA_ENA_G0_GATE		(1<<8)
#define NISTC_INTB_ENA_G1_GATE		(1<<10)
#define CS5529_CFG_WORD_RATE(x)		(((x) & 0x7) << 13)
#define CS5529_CFG_CALIB(x)		(((x) & 0x7) << 0)
#define NI67XX_CAL_DATA_REG		0x1b
#define NI67XX_CAL_STATUS_OVERRANGE	(1<<2)
#define CS5529_CMD_SINGLE_CONV		(1<<6)
#define NI67XX_CAL_STATUS_OSC_DETECT	(1<<1)
#define CS5529_CMD_CB			(1<<7)
#define NI67XX_CAL_STATUS_REG		0x1a
#define NI67XX_CAL_CFG_LO_REG		0x1d
#define NI67XX_CAL_CFG_HI_REG		0x1c
#define CS5529_CMD_REG_MASK		CS5529_CMD_REG(7)
#define NI67XX_CAL_STATUS_BUSY		(1<<0)
#define CS5529_CMD_REG(x)		(((x) & 0x7) << 1)
#define NI611X_AO_WINDOW_DATA_REG	0x1e
#define NI611X_AO_WINDOW_ADDR_REG	0x18
#define NI_E_SERIAL_CMD_SCLK		(1<<0)
#define NI_E_SERIAL_CMD_SDATA		(1<<1)
#define NI_E_SERIAL_CMD_EEPROM_CS	(1<<2)
#define NI_E_STATUS_PROMOUT		(1<<0)
#define NI_E_SERIAL_CMD_REG		0x0d
#define NI_E_AI_CFG_LO_LAST_CHAN	(1<<15)
#define NI6143_CALIB_CHAN_RELAY_ON	(1<<15)
#define NI_E_AI_CFG_HI_CHAN(x)		(((x) & 0x3f) << 0)
#define NI_E_AI_CFG_LO_DITHER		(1<<9)
#define NI_E_AI_CFG_LO_GAIN(x)		((x) << 0)
#define NI_E_AI_CFG_HI_TYPE_GROUND	NI_E_AI_CFG_HI_TYPE(3)
#define NI_E_AI_CFG_HI_TYPE_DIFF	NI_E_AI_CFG_HI_TYPE(1)
#define NI_E_AI_CFG_HI_TYPE_COMMON	NI_E_AI_CFG_HI_TYPE(2)
#define NI611X_CALIB_CHAN_SEL_REG	0x1a
#define NI_E_AI_CFG_HI_REG		0x12
#define NI_E_AI_CFG_LO_REG		0x10
#define NI_M_STATIC_AI_CTRL_REG(x)	((x) ? (0x260 + (x)) : 0x064)
#define NI_E_SERIAL_CMD_DAC_LD(x)	(1<<(3 + (x)))
#define NISTC_INTA_ENA_AI_START		(1<<3)
#define NISTC_INTA_ENA_AI_START2	(1<<2)
#define NISTC_INTA_ENA_AI_START1	(1<<1)
#define NISTC_AI_OUT_CTRL_CONVERT_SEL(x)	(((x) & 0x3) << 0)
#define NI_E_MISC_CMD_INTEXT_ATRIG(x)	(((x) & 0x1) << 7)
#define NI_E_DMA_AI_SEL_MASK		NI_E_DMA_AI_SEL(0xf)
#define NISTC_AI_MODE3_FIFO_MODE(x)	(((x) & 0x3) << 6)
#define NISTC_INTA_ACK_AI_START		(1<<11)
#define NISTC_INTA_ACK_AI_SC_TC_ERR	(1<<7)
#define NISTC_INTA_ACK_AI_SC_TC		(1<<8)
#define NISTC_INTA_ACK_AI_START2	(1<<10)
#define NISTC_INTA_ACK_AI_ERR		(1<<13)
#define NISTC_INTA_ACK_AI_START1	(1<<9)
#define NISTC_INTA_ACK_AI_STOP		(1<<12)
#define NISTC_AO_CMD2_BC_GATE_ENA	(1<<11)
#define NISTC_AO_MODE2_UI_INIT_LOAD_SRC	(1<<7)
#define NISTC_AO_CMD1_DAC0_UPDATE_MODE	(1<<2)
#define NISTC_AO_MODE1_UI_SRC_MASK	NISTC_AO_MODE1_UI_SRC(0x1f)
#define NISTC_AO_CMD1_DAC1_UPDATE_MODE	(1<<4)
#define NISTC_AO_MODE1_UPDATE_SRC_MASK	NISTC_AO_MODE1_UPDATE_SRC(0x1f)
#define NISTC_AO_MODE1_UPDATE_SRC_POLARITY (1<<4)
#define NISTC_AO_MODE1_UPDATE_SRC(x)	(((x) & 0x1f) << 11)
#define NISTC_AO_MODE2_UI_RELOAD_MODE(x) (((x) & 0x7) << 4)
#define NISTC_AO_MODE1_UI_SRC_POLARITY	(1<<3)
#define NISTC_AO_CMD1_UI_LOAD		(1<<9)
#define NISTC_AO_TRIG_START1_POLARITY	(1<<13)
#define NISTC_AO_MODE1_CONTINUOUS	(1<<1)
#define NISTC_AO_TRIG_START1_SEL(x)	(((x) & 0x1f) << 0)
#define NISTC_AO_TRIG_START1_EDGE	(1<<5)
#define NISTC_AO_TRIG_START1_SYNC	(1<<6)
#define NISTC_AO_MODE3_TRIG_LEN			(1<<11)
#define NISTC_AO_MODE1_TRIGGER_ONCE	(1<<0)
#define NISTC_AO_MODE3_STOP_ON_OVERRUN_ERR	(1<<5)
#define NISTC_AO_CMD1_BC_LOAD		(1<<5)
#define NISTC_AO_CMD1_UC_LOAD		(1<<7)
#define NISTC_AO_MODE2_BC_INIT_LOAD_SRC	(1<<2)
#define NISTC_AO_MODE2_UC_INIT_LOAD_SRC	(1<<11)
#define NI611X_AO_TIMED_REG		0x10
#define NISTC_AO_OUT_CTRL_UPDATE_SEL_HIGHZ	NISTC_AO_OUT_CTRL_UPDATE_SEL(0)
#define NISTC_AO_MODE1_MULTI_CHAN	(1<<5)
#define NI611X_AO_WAVEFORM_GEN_REG	0x15
#define NISTC_AO_OUT_CTRL_CHANS(x)		(((x) & 0xf) << 6)
#define NISTC_AO_PERSONAL_TMRDACWR_PW		(1<<12)
#define NISTC_AO_PERSONAL_DMA_PIO_CTRL		(1<<8)
#define NISTC_AO_PERSONAL_UPDATE_PW		(1<<5)
#define NISTC_AO_PERSONAL_FIFO_ENA		(1<<10)
#define NISTC_AO_PERSONAL_NUM_DAC		(1<<14)
#define NISTC_INTB_ENA_AO_BC_TC		(1<<0)
#define NISTC_AO_MODE2_FIFO_MODE_HF_F	NISTC_AO_MODE2_FIFO_MODE(3)
#define NISTC_AO_MODE2_FIFO_MODE_MASK	NISTC_AO_MODE2_FIFO_MODE(3)
#define NISTC_AO_MODE2_FIFO_MODE_HF	NISTC_AO_MODE2_FIFO_MODE(1)
#define NISTC_AO_MODE2_FIFO_REXMIT_ENA	(1<<13)
#define NISTC_AO_START_AOFREQ_ENA	(1<<12)
#define NI_M_CFG_BYPASS_AI_CAL_POS_MASK	NI_M_CFG_BYPASS_AI_CAL_POS(7)
#define NI_M_CFG_BYPASS_AO_CAL_MASK	NI_M_CFG_BYPASS_AO_CAL(0xf)
#define NI_M_CFG_BYPASS_AI_MODE_MUX_MASK NI_M_CFG_BYPASS_AI_MODE_MUX(3)
#define NI_M_CFG_BYPASS_AI_CAL_NEG_MASK	NI_M_CFG_BYPASS_AI_CAL_NEG(7)
#define NISTC_INTB_ACK_AO_START		(1<<11)
#define NISTC_INTB_ACK_AO_STOP		(1<<12)
#define NISTC_INTB_ACK_AO_ERR		(1<<13)
#define NISTC_INTB_ACK_AO_BC_TC_TRIG_ERR (1<<3)
#define NISTC_INTB_ACK_AO_UPDATE	(1<<10)
#define NISTC_INTB_ACK_AO_BC_TC		(1<<8)
#define NISTC_INTB_ACK_AO_START1	(1<<9)
#define NISTC_INTB_ACK_AO_UC_TC		(1<<7)
#define NISTC_INTB_ACK_AO_BC_TC_ERR	(1<<4)
#define NI_E_DMA_AO_SEL_MASK		NI_E_DMA_AO_SEL(0xf)
#define NISTC_RTSI_TRIG_NUM_CHAN(_m)	((_m) ? 8 : 7)
#define NISTC_RTSI_TRIG_DIR(_c, _m)	((_m) ? (1<<(8 + (_c))) : (1<<(7 + (_c))))
#define NISTC_RTSI_TRIG_OLD_CLK_CHAN	7
#define NISTC_RTSI_TRIG_DRV_CLK		(1<<0)
#define NISTC_RTSI_TRIG_MASK(_c)	NISTC_RTSI_TRIG((_c), 0xf)
#define NISTC_RTSI_TRIG(_c, _s)		(((_s) & 0xf) << (((_c) % 4) * 4))
#define NI_M_CLK_FOUT2_TIMEBASE1_PLL	(1<<5)
#define NI_M_CLK_FOUT2_TIMEBASE3_PLL	(1<<6)
#define NI_M_CLK_FOUT2_REG		0x1c4
#define NI_M_PLL_CTRL_REG		0x1c6
#define NISTC_RTSI_TRIG_USE_CLK		(1<<1)
#define NISTC_RTSI_TRIG_TO_SRC(_c, _b)	(((_b) >> (((_c) % 4) * 4)) & 0xf)
#define NISTC_DIO_OUT_SERIAL(x)	(((x) & 0xff) << 8)
#define NISTC_STATUS1_SERIO_IN_PROG	(1<<12)
#define NISTC_DIO_OUT_SERIAL_MASK	NISTC_DIO_OUT_SERIAL(0xff)
#define NISTC_DIO_SERIAL_IN_REG		28
#define NISTC_STATUS1_REG		27
#define NISTC_DIO_CTRL_HW_SER_START	(1<<8)
#define NISTC_AO_CMD1_UC_ARM		(1<<8)
#define NISTC_INTB_ENA_AO_ERR		(1<<5)
#define NISTC_AO_CMD1_UI_ARM		(1<<10)
#define NISTC_AO_CMD1_BC_ARM		(1<<6)
#define NISTC_STATUS2_REG		29
#define NISTC_INTB_ENA_AO_FIFO		(1<<8)
#define NI611X_AO_FIFO_OFFSET_LOAD_REG	0x13
#define NISTC_AO_MODE3_NOT_AN_UPDATE		(1<<2)
#define NISTC_STATUS2_AO_TMRDACWRS_IN_PROGRESS	(1<<5)
#define NISTC_RTSI_TRIG_DIR_SUB_SEL1_SHIFT	2
#define NISTC_RTSI_TRIGB_SUB_SEL1_SHIFT	15
#define NISTC_RTSI_TRIGB_SUB_SEL1	(1<<15)
#define NISTC_RTSI_TRIG_DIR_SUB_SEL1	(1<<2)
#define NI_M_MAX_RTSI_CHAN		7
#define NI_M_PFI_OUT_SEL_TO_SRC(_c, _b)	(((_b) >> NI_M_PFI_CHAN(_c)) & 0x1f)
#define NI_M_PFI_OUT_SEL(_c, _s)	(((_s) & 0x1f) << NI_M_PFI_CHAN(_c))
#define NI_M_PFI_OUT_SEL_MASK(_c)	(0x1f << NI_M_PFI_CHAN(_c))
#define NI67XX_CAL_CMD_REG		0x19
#define NI_M_CFG_BYPASS_FIFO_REG	0x218
#define NI_M_AI_CFG_CHAN_TYPE_DIFF	NI_M_AI_CFG_CHAN_TYPE(1)
#define NI_M_CFG_BYPASS_AI_GAIN(x)	(((x) & 0x7) << 18)
#define NI_M_AI_CFG_POLARITY		(1<<12)
#define NI_M_AI_CFG_BANK_SEL(x)		((((x) & 0x40) << 4) | ((x) & 0x30))
#define NI_M_CFG_BYPASS_AI_CHAN(x)	(((x) & 0x7) << 0)
#define NI_M_CFG_BYPASS_FIFO		(1<<31)
#define NI_M_AI_CFG_DITHER		(1<<13)
#define NI_M_AI_CFG_FIFO_DATA_REG	0x05e
#define NI_M_AI_CFG_CHAN_SEL(x)		(((x) & 0xf) << 0)
#define NI_M_AI_CFG_LAST_CHAN		(1<<14)
#define NI_M_AI_CFG_CHAN_TYPE_COMMON	NI_M_AI_CFG_CHAN_TYPE(2)
#define NI_M_CFG_BYPASS_AI_POLARITY	(1<<22)
#define NI_M_AI_CFG_CHAN_TYPE_GROUND	NI_M_AI_CFG_CHAN_TYPE(3)
#define NI_M_CFG_BYPASS_AI_DITHER	(1<<21)
#define NI_M_AI_CFG_GAIN(x)		(((x) & 0x7) << 9)
#define NI_E_AI_CFG_HI_TYPE(x)		(((x) & 0x7) << 12)
#define NI_E_DMA_AI_SEL(x)		(((x) & 0xf) << 0)
#define NISTC_AO_MODE1_UI_SRC(x)	(((x) & 0x1f) << 6)
#define NISTC_AO_OUT_CTRL_UPDATE_SEL(x)		(((x) & 0x3) << 0)
#define NISTC_AO_CMD2_START1_PULSE	(1<<0)
#define NISTC_AO_MODE2_FIFO_MODE(x)	(((x) & 0x3) << 14)
#define NI_M_CFG_BYPASS_AI_CAL_POS(x)	(((x) & 0x7) << 7)
#define NI_M_CFG_BYPASS_AO_CAL(x)	(((x) & 0xf) << 15)
#define NI_M_CFG_BYPASS_AI_MODE_MUX(x)	(((x) & 0x3) << 13)
#define NI_M_CFG_BYPASS_AI_CAL_NEG(x)	(((x) & 0x7) << 10)
#define NI_E_DMA_AO_SEL(x)		(((x) & 0xf) << 4)
#define NI_M_CLK_FOUT2_PLL_SRC_PXI10	NI_M_CLK_FOUT2_PLL_SRC(0x1d)
#define NI_M_PLL_CTRL_DIVISOR(x)	(((x) & 0xf) << 8)
#define NI_M_CLK_FOUT2_PLL_SRC_STAR	NI_M_CLK_FOUT2_PLL_SRC(0x14)
#define NI_M_PLL_STATUS_REG		0x1c8
#define NI_M_CLK_FOUT2_PLL_SRC_RTSI(x)	(((x) == NI_M_MAX_RTSI_CHAN)	\
					 ? NI_M_CLK_FOUT2_PLL_SRC(0x1b)	\
					 : NI_M_CLK_FOUT2_PLL_SRC(0xb + (x)))
#define NI_M_PLL_CTRL_MULTIPLIER(x)	(((x) & 0xff) << 0)
#define NI_M_PLL_STATUS_LOCKED		(1<<0)
#define NI_M_PLL_CTRL_ENA		(1<<12)
#define NI_M_CLK_FOUT2_PLL_SRC_MASK	NI_M_CLK_FOUT2_PLL_SRC(0x1f)
#define NI_M_PLL_CTRL_VCO_MODE_75_150MHZ  NI_M_PLL_CTRL_VCO_MODE(3)
#define NI_E_AO_GROUND_REF		(1<<3)
#define NI_E_AO_CFG_BIP			(1<<0)
#define NI_E_AO_EXT_REF			(1<<2)
#define NI_E_AO_CFG_REG			0x16
#define NI_E_AO_DEGLITCH		(1<<1)
#define NI_M_AO_CFG_BANK_UPDATE_TIMED	(1<<6)
#define NI_M_AO_CFG_BANK_OFFSET_0V	NI_M_AO_CFG_BANK_OFFSET(0)
#define NI_M_AO_CFG_BANK_REF_INT_5V	NI_M_AO_CFG_BANK_REF(1)
#define NI_M_AO_CFG_BANK_OFFSET_5V	NI_M_AO_CFG_BANK_OFFSET(1)
#define NI_M_AO_CFG_BANK_REF_INT_10V	NI_M_AO_CFG_BANK_REF(0)
#define NI_M_AO_REF_ATTENUATION_X5	(1<<0)
#define NI_M_AO_CFG_BANK_REG(x)		(0x0c3 + ((x) * 4))
#define NISTC_AO_STATUS1_FIFO_HF	(1<<13)
#define NISTC_AO_STATUS1_REG		3
#define NISTC_G1_SAVE_REG		14
#define NISTC_G01_STATUS_REG		4
#define NISTC_AO_STATUS2_REG		6
#define NISTC_AI_SI_SAVE_REG		64
#define NISTC_G1_HW_SAVE_REG		10
#define NISTC_G0_HW_SAVE_REG		8
#define NISTC_G0_SAVE_REG		12
#define NISTC_AO_UI_SAVE_REG		16
#define NISTC_AI_SC_SAVE_REG		66
#define NISTC_AO_UC_SAVE_REG		20
#define NISTC_AI_STATUS2_REG		5
#define NISTC_AO_BC_SAVE_REG		18
#define NI_M_PFI_CHAN(_c)		(((_c) % 3) * 5)
#define NI_M_AI_CFG_CHAN_TYPE(x)	(((x) & 0x7) << 6)
#define NISTC_ATRIG_ETC_GPFO_0_SEL_TO_SRC(x)	(((x) >> 11) & 0x7)
#define NISTC_ATRIG_ETC_GPFO_1_SEL_TO_SRC(x)	(((x) >> 7) & 0x1)
#define NI_M_CLK_FOUT2_PLL_SRC(x)	(((x) & 0x1f) << 0)
#define NI_M_PLL_MAX_DIVISOR		0x10
#define NI_M_PLL_MAX_MULTIPLIER		0x100
#define NI_M_PLL_CTRL_VCO_MODE(x)	(((x) & 0x3) << 13)
#define NI_M_AO_CFG_BANK_OFFSET(x)	(((x) & 0x7) << 0)
#define NI_M_AO_CFG_BANK_REF(x)		(((x) & 0x7) << 3)
#define NI_E_AO_FIFO_DATA_REG		0x1e
#define NI611X_AO_FIFO_DATA_REG		0x14

typedef enum {
	BOARD_PCIMIO_16XE_50,
	BOARD_PCIMIO_16XE_10,
	BOARD_PCI6014,
	BOARD_PXI6030E,
	BOARD_PCIMIO_16E_1,
	BOARD_PCIMIO_16E_4,
	BOARD_PXI6040E,
	BOARD_PCI6031E,
	BOARD_PCI6032E,
	BOARD_PCI6033E,
	BOARD_PCI6071E,
	BOARD_PCI6023E,
	BOARD_PCI6024E,
	BOARD_PCI6025E,
	BOARD_PXI6025E,
	BOARD_PCI6034E,
	BOARD_PCI6035E,
	BOARD_PCI6052E,
	BOARD_PCI6110,
	BOARD_PCI6111,
	BOARD_PCI6711,
	BOARD_PXI6711,
	BOARD_PCI6713,
	BOARD_PXI6713,
	BOARD_PCI6731,
	BOARD_PCI6733,
	BOARD_PXI6733,
	BOARD_PXI6071E,
	BOARD_PXI6070E,
	BOARD_PXI6052E,
	BOARD_PXI6031E,
	BOARD_PCI6036E,
	BOARD_PCI6220,
	BOARD_PXI6220,
	BOARD_PCI6221,
	BOARD_PCI6221_37PIN,
	BOARD_PXI6221,
	BOARD_PCI6224,
	BOARD_PXI6224,
	BOARD_PCI6225,
	BOARD_PXI6225,
	BOARD_PCI6229,
	BOARD_PXI6229,
	BOARD_PCI6250,
	BOARD_PXI6250,
	BOARD_PCI6251,
	BOARD_PXI6251,
	BOARD_PCIE6251,
	BOARD_PXIE6251,
	BOARD_PCI6254,
	BOARD_PXI6254,
	BOARD_PCI6259,
	BOARD_PXI6259,
	BOARD_PCIE6259,
	BOARD_PXIE6259,
	BOARD_PCI6280,
	BOARD_PXI6280,
	BOARD_PCI6281,
	BOARD_PXI6281,
	BOARD_PCI6284,
	BOARD_PXI6284,
	BOARD_PCI6289,
	BOARD_PXI6289,
	BOARD_PCI6143,
	BOARD_PXI6143,
} ni_pcimio_boardid;

typedef enum {
	caldac_none = 0,
	mb88341,
	dac8800,
	dac8043,
	ad8522,
	ad8804,
	ad8842,
	ad8804_debug
} caldac_enum;

typedef enum {
	ni_gpct_variant_e_series,
	ni_gpct_variant_m_series,
	ni_gpct_variant_660x
} ni_gpct_variant;

typedef enum {
	NITIO_G0_AUTO_INC,
	NITIO_G1_AUTO_INC,
	NITIO_G2_AUTO_INC,
	NITIO_G3_AUTO_INC,
	NITIO_G0_CMD,
	NITIO_G1_CMD,
	NITIO_G2_CMD,
	NITIO_G3_CMD,
	NITIO_G0_HW_SAVE,
	NITIO_G1_HW_SAVE,
	NITIO_G2_HW_SAVE,
	NITIO_G3_HW_SAVE,
	NITIO_G0_SW_SAVE,
	NITIO_G1_SW_SAVE,
	NITIO_G2_SW_SAVE,
	NITIO_G3_SW_SAVE,
	NITIO_G0_MODE,
	NITIO_G1_MODE,
	NITIO_G2_MODE,
	NITIO_G3_MODE,
	NITIO_G0_LOADA,
	NITIO_G1_LOADA,
	NITIO_G2_LOADA,
	NITIO_G3_LOADA,
	NITIO_G0_LOADB,
	NITIO_G1_LOADB,
	NITIO_G2_LOADB,
	NITIO_G3_LOADB,
	NITIO_G0_INPUT_SEL,
	NITIO_G1_INPUT_SEL,
	NITIO_G2_INPUT_SEL,
	NITIO_G3_INPUT_SEL,
	NITIO_G0_CNT_MODE,
	NITIO_G1_CNT_MODE,
	NITIO_G2_CNT_MODE,
	NITIO_G3_CNT_MODE,
	NITIO_G0_GATE2,
	NITIO_G1_GATE2,
	NITIO_G2_GATE2,
	NITIO_G3_GATE2,
	NITIO_G01_STATUS,
	NITIO_G23_STATUS,
	NITIO_G01_RESET,
	NITIO_G23_RESET,
	NITIO_G01_STATUS1,
	NITIO_G23_STATUS1,
	NITIO_G01_STATUS2,
	NITIO_G23_STATUS2,
	NITIO_G0_DMA_CFG,
	NITIO_G1_DMA_CFG,
	NITIO_G2_DMA_CFG,
	NITIO_G3_DMA_CFG,
	NITIO_G0_DMA_STATUS,
	NITIO_G1_DMA_STATUS,
	NITIO_G2_DMA_STATUS,
	NITIO_G3_DMA_STATUS,
	NITIO_G0_ABZ,
	NITIO_G1_ABZ,
	NITIO_G0_INT_ACK,
	NITIO_G1_INT_ACK,
	NITIO_G2_INT_ACK,
	NITIO_G3_INT_ACK,
	NITIO_G0_STATUS,
	NITIO_G1_STATUS,
	NITIO_G2_STATUS,
	NITIO_G3_STATUS,
	NITIO_G0_INT_ENA,
	NITIO_G1_INT_ENA,
	NITIO_G2_INT_ENA,
	NITIO_G3_INT_ENA,
	NITIO_NUM_REGS,
} ni_gpct_register;

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
    uint16_t int_a_enable_reg;
    uint16_t int_b_enable_reg;
    uint16_t io_bidirection_pin_reg;
    uint16_t rtsi_trig_direction_reg;
    uint16_t rtsi_trig_a_output_reg;
    uint16_t rtsi_trig_b_output_reg;
    uint16_t pfi_output_select_reg[NUM_PFI_OUTPUT_SELECT_REGS];
    uint16_t ai_ao_select_reg;
    uint16_t g0_g1_select_reg;
    uint16_t cdio_dma_select_reg;
    uint16_t rtsi_shared_mux_reg;
    uint8_t eeprom_buffer[M_SERIES_EEPROM_SIZE];

    /* DMA Context */
    uint32_t mite_iodwcr_1;
    uint32_t mite_iodwbsr_1;
    uint32_t mite_iodwbsr;

    uint32_t status;
    uint16_t stc_window_addr;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mite_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case MITE_IODWBSR:
        val = s->mite_iodwbsr;
        break;
    case MITE_IODWBSR_1:
        val = s->mite_iodwbsr_1;
        break;
    case MITE_IODWCR_1:
        val = s->mite_iodwcr_1;
        break;
    case NI_M_CDIO_STATUS_REG:
        val = NI_M_CDIO_STATUS_CDO_FIFO_FULL;
        break;
    }

    return val;
}

static void pcibase_mite_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case MITE_IODWBSR:
        s->mite_iodwbsr = val;
        break;
    case MITE_IODWBSR_1:
        s->mite_iodwbsr_1 = val;
        break;
    case MITE_IODWCR_1:
        s->mite_iodwcr_1 = val;
        break;
    case NI_M_CDIO_CMD_REG:
        /* Handle CDIO commands */
        break;
    }
}

static uint64_t pcibase_daq_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= 0x400 && addr < 0x400 + M_SERIES_EEPROM_SIZE) {
        return s->eeprom_buffer[addr - 0x400];
    }

    switch (addr) {
    case NI_E_STATUS_REG:
        val = 0x80 | NI_E_STATUS_PROMOUT;
        break;
    case NI6143_AI_FIFO_STATUS_REG:
        val = 0x10 | 0x04 | 0x01;
        break;
    case NI67XX_CAL_STATUS_REG:
        val = 0;
        break;
    case NI_M_PLL_STATUS_REG:
        val = NI_M_PLL_STATUS_LOCKED;
        break;
    case NI_M_CDIO_STATUS_REG:
        val = NI_M_CDIO_STATUS_CDO_FIFO_FULL;
        break;
    case NI_E_STC_WINDOW_DATA_REG:
        switch (s->stc_window_addr) {
        case NISTC_STATUS1_REG:
            val = 0;
            break;
        case NISTC_DIO_SERIAL_IN_REG:
            val = 0;
            break;
        case NISTC_AI_STATUS1_REG:
            val = 0;
            break;
        case NISTC_STATUS2_REG:
            val = 0;
            break;
        case NISTC_DIO_IN_REG:
            val = 0;
            break;
        case NISTC_AO_STATUS1_REG:
            val = NISTC_AO_STATUS1_FIFO_HF;
            break;
        case NISTC_AO_STATUS2_REG:
            val = 0;
            break;
        }
        break;
    case NI_M_PFI_DI_REG:
        val = 0;
        break;
    case NI_M_DIO_REG:
        val = 0;
        break;
    case NI_E_AI_FIFO_DATA_REG:
        val = 0;
        break;
    }

    return val;
}

static void pcibase_daq_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    switch (addr) {
    case NI_E_STC_WINDOW_ADDR_REG:
        s->stc_window_addr = val;
        break;
    case NI_E_STC_WINDOW_DATA_REG:
        switch (s->stc_window_addr) {
        case NISTC_INTA_ENA_REG:
            s->int_a_enable_reg = val;
            break;
        case NISTC_INTB_ENA_REG:
            s->int_b_enable_reg = val;
            break;
        case NISTC_IO_BIDIR_PIN_REG:
            s->io_bidirection_pin_reg = val;
            break;
        case NISTC_RTSI_TRIG_DIR_REG:
            s->rtsi_trig_direction_reg = val;
            break;
        case NISTC_RTSI_TRIGA_OUT_REG:
            s->rtsi_trig_a_output_reg = val;
            break;
        case NISTC_RTSI_TRIGB_OUT_REG:
            s->rtsi_trig_b_output_reg = val;
            break;
        case NISTC_RTSI_BOARD_REG:
            s->rtsi_shared_mux_reg = val;
            break;
        case NISTC_INTA_ACK_REG:
            /* Acknowledge A interrupt */
            break;
        case NISTC_INTB_ACK_REG:
            /* Acknowledge B interrupt */
            break;
        }
        break;
    case NI_E_DMA_AI_AO_SEL_REG:
        s->ai_ao_select_reg = val;
        break;
    case NI_E_DMA_G0_G1_SEL_REG:
        s->g0_g1_select_reg = val;
        break;
    case NI_M_CDIO_DMA_SEL_REG:
        s->cdio_dma_select_reg = val;
        break;
    case NI_M_PFI_OUT_SEL_REG(0):
    case NI_M_PFI_OUT_SEL_REG(1):
    case NI_M_PFI_OUT_SEL_REG(2):
    case NI_M_PFI_OUT_SEL_REG(3):
    case NI_M_PFI_OUT_SEL_REG(4):
    case NI_M_PFI_OUT_SEL_REG(5):
        s->pfi_output_select_reg[(addr - 0x1d0) / 2] = val;
        break;
    case NI_M_CDIO_CMD_REG:
        /* Handle CDIO commands */
        break;
    }
}

static const MemoryRegionOps pcibase_mite_ops = {
    .read = pcibase_mite_read,
    .write = pcibase_mite_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static const MemoryRegionOps pcibase_daq_ops = {
    .read = pcibase_daq_read,
    .write = pcibase_daq_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    s->int_a_enable_reg = 0;
    s->int_b_enable_reg = 0;
    s->io_bidirection_pin_reg = 0;
    s->rtsi_trig_direction_reg = 0;
    s->rtsi_trig_a_output_reg = 0;
    s->rtsi_trig_b_output_reg = 0;
    s->ai_ao_select_reg = 0;
    s->g0_g1_select_reg = 0;
    s->cdio_dma_select_reg = 0;
    s->rtsi_shared_mux_reg = 0;
    s->stc_window_addr = 0;
    s->mite_iodwbsr = 0;
    s->mite_iodwbsr_1 = 0;
    s->mite_iodwcr_1 = 0;
    memset(s->pfi_output_select_reg, 0, sizeof(s->pfi_output_select_reg));
    memset(s->eeprom_buffer, 0, sizeof(s->eeprom_buffer));
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }
    
    /* CRITICAL: PCI requires BAR sizes to be a power of 2. Prevent QEMU assert crash. */
    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->index == 0) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mite_ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->index == 1) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_daq_ops, s, bi->name, aligned_size);
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

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_NI );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x0162 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_OTHERS );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 2;
    s->bar_info[0] = (BARInfo){0, BAR_TYPE_MMIO, 0x1000, "ni_pcimio_mite"};
    s->bar_info[1] = (BARInfo){1, BAR_TYPE_MMIO, 0x1000, "ni_pcimio_daq"};
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

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
    .name = "ni_pcimio_pci",
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
