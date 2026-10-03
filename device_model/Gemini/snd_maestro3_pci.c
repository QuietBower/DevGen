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


#define TYPE_PCIBASE_DEVICE "snd_maestro3_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_ESS               0x125d
#define PCI_DEVICE_ID_ESS_ALLEGRO_1     0x1988
#define PCI_CLASS_MULTIMEDIA_AUDIO      0x0401
#define DRIVER_NAME "Maestro3"
#define CARD_NAME "ESS Maestro3/Allegro/Canyon3D-2"
#define GPIO_DATA               0x60
#define GPIO_MASK               0x64
#define GPIO_DATA_MASK          0x0FFF
#define MPU401_IRQ_ENABLE       0x00000010
#define MPU401_IO_ENABLE        0x00000008
#define DMA_DDMA                0x00000000
#define DMA_TDMA                0x00000100
#define DMA_PCPCI               0x00000200
#define ASSP_CONTROL_A          0xA2
#define ASSP_CONTROL_B          0xA4
#define ASSP_CONTROL_C          0xA6
#define MAX_PLAYBACKS	2
#define MAX_CAPTURES	1
#define NR_DSPS		(MAX_PLAYBACKS + MAX_CAPTURES)
#define PCI_LEGACY_AUDIO_CTRL   0x40
#define SOUND_BLASTER_ENABLE    0x00000001
#define FM_SYNTHESIS_ENABLE     0x00000002
#define GAME_PORT_ENABLE        0x00000004
#define ALIAS_10BIT_IO          0x00000020
#define SB_DMA_MASK             0x000000C0
#define SB_DMA_0                0x00000040
#define SB_DMA_1                0x00000040
#define SB_DMA_R                0x00000080
#define SB_DMA_3                0x000000C0
#define SB_IRQ_MASK             0x00000700
#define SB_IRQ_5                0x00000000
#define SB_IRQ_7                0x00000100
#define SB_IRQ_9                0x00000200
#define SB_IRQ_10               0x00000300
#define MIDI_IRQ_MASK           0x00003800
#define SERIAL_IRQ_ENABLE       0x00004000
#define DISABLE_LEGACY          0x00008000
#define PCI_ALLEGRO_CONFIG      0x50
#define SB_ADDR_240             0x00000004
#define MPU_ADDR_MASK           0x00000018
#define MPU_ADDR_330            0x00000000
#define MPU_ADDR_300            0x00000008
#define MPU_ADDR_320            0x00000010
#define MPU_ADDR_340            0x00000018
#define USE_PCI_TIMING          0x00000040
#define POSTED_WRITE_ENABLE     0x00000080
#define DMA_POLICY_MASK         0x00000700
#define DMA_WBDMA16             0x00000400
#define DMA_WBDMA4              0x00000500
#define DMA_WBDMA2              0x00000600
#define DMA_WBDMA1              0x00000700
#define DMA_SAFE_GUARD          0x00000800
#define HI_PERF_GP_ENABLE       0x00001000
#define PIC_SNOOP_MODE_0        0x00002000
#define PIC_SNOOP_MODE_1        0x00004000
#define SOUNDBLASTER_IRQ_MASK   0x00008000
#define RING_IN_ENABLE          0x00010000
#define SPDIF_TEST_MODE         0x00020000
#define CLK_MULT_MODE_SELECT_2  0x00040000
#define EEPROM_WRITE_ENABLE     0x00080000
#define CODEC_DIR_IN            0x00100000
#define HV_BUTTON_FROM_GD       0x00200000
#define REDUCED_DEBOUNCE        0x00400000
#define HV_CTRL_ENABLE          0x00800000
#define SPDIF_ENABLE            0x01000000
#define CLK_DIV_SELECT          0x06000000
#define CLK_DIV_BY_48           0x00000000
#define CLK_DIV_BY_49           0x02000000
#define CLK_DIV_BY_50           0x04000000
#define CLK_DIV_RESERVED        0x06000000
#define PM_CTRL_ENABLE          0x08000000
#define CLK_MULT_MODE_SELECT    0x30000000
#define CLK_MULT_MODE_SHIFT     28
#define CLK_MULT_MODE_0         0x00000000
#define CLK_MULT_MODE_1         0x10000000
#define CLK_MULT_MODE_2         0x20000000
#define CLK_MULT_MODE_3         0x30000000
#define INT_CLK_SELECT          0x40000000
#define INT_CLK_MULT_RESET      0x80000000
#define INT_CLK_SRC_NOT_PCI     0x00100000
#define INT_CLK_MULT_ENABLE     0x80000000
#define PCI_ACPI_CONTROL        0x54
#define PCI_ACPI_D0             0x00000000
#define PCI_ACPI_D1             0xB4F70000
#define PCI_ACPI_D2             0xB4F7B4F7
#define PCI_USER_CONFIG         0x58
#define EXT_PCI_MASTER_ENABLE   0x00000001
#define SPDIF_OUT_SELECT        0x00000002
#define TEST_PIN_DIR_CTRL       0x00000004
#define AC97_CODEC_TEST         0x00000020
#define TRI_STATE_BUFFER        0x00000080
#define IN_CLK_12MHZ_SELECT     0x00000100
#define MULTI_FUNC_DISABLE      0x00000200
#define EXT_MASTER_PAIR_SEL     0x00000400
#define PCI_MASTER_SUPPORT      0x00000800
#define STOP_CLOCK_ENABLE       0x00001000
#define EAPD_DRIVE_ENABLE       0x00002000
#define REQ_TRI_STATE_ENABLE    0x00004000
#define REQ_LOW_ENABLE          0x00008000
#define MIDI_1_ENABLE           0x00010000
#define MIDI_2_ENABLE           0x00020000
#define SB_AUDIO_SYNC           0x00040000
#define HV_CTRL_TEST            0x00100000
#define SOUNDBLASTER_TEST       0x00400000
#define PCI_USER_CONFIG_C       0x5C
#define PCI_DDMA_CTRL           0x60
#define DDMA_ENABLE             0x00000001
#define HOST_INT_CTRL           0x18
#define SB_INT_ENABLE           0x0001
#define MPU401_INT_ENABLE       0x0002
#define ASSP_INT_ENABLE         0x0010
#define RING_INT_ENABLE         0x0020
#define HV_INT_ENABLE           0x0040
#define CLKRUN_GEN_ENABLE       0x0100
#define HV_CTRL_TO_PME          0x0400
#define SOFTWARE_RESET_ENABLE   0x8000
#define REGB_ENABLE_RESET               0x01
#define REGB_STOP_CLOCK                 0x10
#define HOST_INT_STATUS         0x1A
#define SB_INT_PENDING          0x01
#define MPU401_INT_PENDING      0x02
#define ASSP_INT_PENDING        0x10
#define RING_INT_PENDING        0x20
#define HV_INT_PENDING          0x40
#define HARDWARE_VOL_CTRL       0x1B
#define SHADOW_MIX_REG_VOICE    0x1C
#define HW_VOL_COUNTER_VOICE    0x1D
#define SHADOW_MIX_REG_MASTER   0x1E
#define HW_VOL_COUNTER_MASTER   0x1F
#define CODEC_COMMAND           0x30
#define CODEC_READ_B            0x80
#define CODEC_STATUS            0x30
#define CODEC_BUSY_B            0x01
#define CODEC_DATA              0x32
#define RING_BUS_CTRL_A         0x36
#define RAC_PME_ENABLE          0x0100
#define RAC_SDFS_ENABLE         0x0200
#define LAC_PME_ENABLE          0x0400
#define LAC_SDFS_ENABLE         0x0800
#define SERIAL_AC_LINK_ENABLE   0x1000
#define IO_SRAM_ENABLE          0x2000
#define IIS_INPUT_ENABLE        0x8000
#define RING_BUS_CTRL_B         0x38
#define SECOND_CODEC_ID_MASK    0x0003
#define SPDIF_FUNC_ENABLE       0x0010
#define SECOND_AC_ENABLE        0x0020
#define SB_MODULE_INTF_ENABLE   0x0040
#define SSPE_ENABLE             0x0040
#define M3I_DOCK_ENABLE         0x0080
#define SDO_OUT_DEST_CTRL       0x3A
#define COMMAND_ADDR_OUT        0x0003
#define PCM_LR_OUT_LOCAL        0x0000
#define PCM_LR_OUT_REMOTE       0x0004
#define PCM_LR_OUT_MUTE         0x0008
#define PCM_LR_OUT_BOTH         0x000C
#define LINE1_DAC_OUT_LOCAL     0x0000
#define LINE1_DAC_OUT_REMOTE    0x0010
#define LINE1_DAC_OUT_MUTE      0x0020
#define LINE1_DAC_OUT_BOTH      0x0030
#define PCM_CLS_OUT_LOCAL       0x0000
#define PCM_CLS_OUT_REMOTE      0x0040
#define PCM_CLS_OUT_MUTE        0x0080
#define PCM_CLS_OUT_BOTH        0x00C0
#define PCM_RLF_OUT_LOCAL       0x0000
#define PCM_RLF_OUT_REMOTE      0x0100
#define PCM_RLF_OUT_MUTE        0x0200
#define PCM_RLF_OUT_BOTH        0x0300
#define LINE2_DAC_OUT_LOCAL     0x0000
#define LINE2_DAC_OUT_REMOTE    0x0400
#define LINE2_DAC_OUT_MUTE      0x0800
#define LINE2_DAC_OUT_BOTH      0x0C00
#define HANDSET_OUT_LOCAL       0x0000
#define HANDSET_OUT_REMOTE      0x1000
#define HANDSET_OUT_MUTE        0x2000
#define HANDSET_OUT_BOTH        0x3000
#define IO_CTRL_OUT_LOCAL       0x0000
#define IO_CTRL_OUT_REMOTE      0x4000
#define IO_CTRL_OUT_MUTE        0x8000
#define IO_CTRL_OUT_BOTH        0xC000
#define SDO_IN_DEST_CTRL        0x3C
#define STATUS_ADDR_IN          0x0003
#define PCM_LR_IN_LOCAL         0x0000
#define PCM_LR_IN_REMOTE        0x0004
#define PCM_LR_RESERVED         0x0008
#define PCM_LR_IN_BOTH          0x000C
#define LINE1_ADC_IN_LOCAL      0x0000
#define LINE1_ADC_IN_REMOTE     0x0010
#define LINE1_ADC_IN_MUTE       0x0020
#define MIC_ADC_IN_LOCAL        0x0000
#define MIC_ADC_IN_REMOTE       0x0040
#define MIC_ADC_IN_MUTE         0x0080
#define LINE2_DAC_IN_LOCAL      0x0000
#define LINE2_DAC_IN_REMOTE     0x0400
#define LINE2_DAC_IN_MUTE       0x0800
#define HANDSET_IN_LOCAL        0x0000
#define HANDSET_IN_REMOTE       0x1000
#define HANDSET_IN_MUTE         0x2000
#define IO_STATUS_IN_LOCAL      0x0000
#define IO_STATUS_IN_REMOTE     0x4000
#define SPDIF_IN_CTRL           0x3E
#define SPDIF_IN_ENABLE         0x0001
#define GPIO_HV_STATUS          0x3000
#define GPIO_PME_STATUS         0x4000
#define GPIO_DIRECTION          0x68
#define GPO_PRIMARY_AC97        0x0001
#define GPI_LINEOUT_SENSE       0x0004
#define GPO_SECONDARY_AC97      0x0008
#define GPI_VOL_DOWN            0x0010
#define GPI_VOL_UP              0x0020
#define GPI_IIS_CLK             0x0040
#define GPI_IIS_LRCLK           0x0080
#define GPI_IIS_DATA            0x0100
#define GPI_DOCKING_STATUS      0x0100
#define GPI_HEADPHONE_SENSE     0x0200
#define GPO_EXT_AMP_SHUTDOWN    0x1000
#define GPO_EXT_AMP_M3		1
#define GPO_EXT_AMP_ALLEGRO	8
#define GPO_M3_EXT_AMP_SHUTDN   0x0002
#define ASSP_INDEX_PORT         0x80
#define ASSP_MEMORY_PORT        0x82
#define ASSP_DATA_PORT          0x84
#define MPU401_DATA_PORT        0x98
#define MPU401_STATUS_PORT      0x99
#define CLK_MULT_DATA_PORT      0x9C
#define ASSP_0_WS_ENABLE        0x01
#define ASSP_CTRL_A_RESERVED1   0x02
#define ASSP_CTRL_A_RESERVED2   0x04
#define ASSP_CLK_49MHZ_SELECT   0x08
#define FAST_PLU_ENABLE         0x10
#define ASSP_CTRL_A_RESERVED3   0x20
#define DSP_CLK_36MHZ_SELECT    0x40
#define RESET_ASSP              0x00
#define RUN_ASSP                0x01
#define ENABLE_ASSP_CLOCK       0x00
#define STOP_ASSP_CLOCK         0x10
#define RESET_TOGGLE            0x40
#define ASSP_HOST_INT_ENABLE    0x01
#define FM_ADDR_REMAP_DISABLE   0x02
#define HOST_WRITE_PORT_ENABLE  0x08
#define ASSP_HOST_INT_STATUS    0xAC
#define DSP2HOST_REQ_PIORECORD  0x01
#define DSP2HOST_REQ_I2SRATE    0x02
#define DSP2HOST_REQ_TIMER      0x04
#define DSP_PORT_TIMER_COUNT    0x06
#define DSP_PORT_MEMORY_INDEX   0x80
#define DSP_PORT_MEMORY_TYPE    0x82
#define MEMTYPE_INTERNAL_CODE   0x0002
#define MEMTYPE_INTERNAL_DATA   0x0003
#define MEMTYPE_MASK            0x0003
#define DSP_PORT_MEMORY_DATA    0x84
#define DSP_PORT_CONTROL_REG_A  0xA2
#define DSP_PORT_CONTROL_REG_B  0xA4
#define DSP_PORT_CONTROL_REG_C  0xA6
#define REV_A_CODE_MEMORY_BEGIN         0x0000
#define REV_A_CODE_MEMORY_END           0x0FFF
#define REV_A_CODE_MEMORY_UNIT_LENGTH   0x0040
#define REV_A_CODE_MEMORY_LENGTH        (REV_A_CODE_MEMORY_END - REV_A_CODE_MEMORY_BEGIN + 1)
#define REV_B_CODE_MEMORY_BEGIN         0x0000
#define REV_B_CODE_MEMORY_END           0x0BFF
#define REV_B_CODE_MEMORY_UNIT_LENGTH   0x0040
#define REV_B_CODE_MEMORY_LENGTH        (REV_B_CODE_MEMORY_END - REV_B_CODE_MEMORY_BEGIN + 1)
#define REV_A_DATA_MEMORY_BEGIN         0x1000
#define REV_A_DATA_MEMORY_END           0x2FFF
#define REV_A_DATA_MEMORY_UNIT_LENGTH   0x0080
#define REV_A_DATA_MEMORY_LENGTH        (REV_A_DATA_MEMORY_END - REV_A_DATA_MEMORY_BEGIN + 1)
#define REV_B_DATA_MEMORY_BEGIN         0x1000
#define REV_B_DATA_MEMORY_END           0x2BFF
#define REV_B_DATA_MEMORY_UNIT_LENGTH   0x0080
#define REV_B_DATA_MEMORY_LENGTH        (REV_B_DATA_MEMORY_END - REV_B_DATA_MEMORY_BEGIN + 1)
#define NUM_UNITS_KERNEL_CODE          16
#define NUM_UNITS_KERNEL_DATA           2
#define NUM_UNITS_KERNEL_CODE_WITH_HSP 16
#define NUM_UNITS_KERNEL_DATA_WITH_HSP  5
#define DP_SHIFT_COUNT                  7
#define KDATA_BASE_ADDR                 0x1000
#define KDATA_BASE_ADDR2                0x1080
#define KDATA_TASK0                     (KDATA_BASE_ADDR + 0x0000)
#define KDATA_TASK1                     (KDATA_BASE_ADDR + 0x0001)
#define KDATA_TASK2                     (KDATA_BASE_ADDR + 0x0002)
#define KDATA_TASK3                     (KDATA_BASE_ADDR + 0x0003)
#define KDATA_TASK4                     (KDATA_BASE_ADDR + 0x0004)
#define KDATA_TASK5                     (KDATA_BASE_ADDR + 0x0005)
#define KDATA_TASK6                     (KDATA_BASE_ADDR + 0x0006)
#define KDATA_TASK7                     (KDATA_BASE_ADDR + 0x0007)
#define KDATA_TASK_ENDMARK              (KDATA_BASE_ADDR + 0x0008)
#define KDATA_CURRENT_TASK              (KDATA_BASE_ADDR + 0x0009)
#define KDATA_TASK_SWITCH               (KDATA_BASE_ADDR + 0x000A)
#define KDATA_INSTANCE0_POS3D           (KDATA_BASE_ADDR + 0x000B)
#define KDATA_INSTANCE1_POS3D           (KDATA_BASE_ADDR + 0x000C)
#define KDATA_INSTANCE2_POS3D           (KDATA_BASE_ADDR + 0x000D)
#define KDATA_INSTANCE3_POS3D           (KDATA_BASE_ADDR + 0x000E)
#define KDATA_INSTANCE4_POS3D           (KDATA_BASE_ADDR + 0x000F)
#define KDATA_INSTANCE5_POS3D           (KDATA_BASE_ADDR + 0x0010)
#define KDATA_INSTANCE6_POS3D           (KDATA_BASE_ADDR + 0x0011)
#define KDATA_INSTANCE7_POS3D           (KDATA_BASE_ADDR + 0x0012)
#define KDATA_INSTANCE8_POS3D           (KDATA_BASE_ADDR + 0x0013)
#define KDATA_INSTANCE_POS3D_ENDMARK    (KDATA_BASE_ADDR + 0x0014)
#define KDATA_INSTANCE0_SPKVIRT         (KDATA_BASE_ADDR + 0x0015)
#define KDATA_INSTANCE_SPKVIRT_ENDMARK  (KDATA_BASE_ADDR + 0x0016)
#define KDATA_INSTANCE0_SPDIF           (KDATA_BASE_ADDR + 0x0017)
#define KDATA_INSTANCE_SPDIF_ENDMARK    (KDATA_BASE_ADDR + 0x0018)
#define KDATA_INSTANCE0_MODEM           (KDATA_BASE_ADDR + 0x0019)
#define KDATA_INSTANCE_MODEM_ENDMARK    (KDATA_BASE_ADDR + 0x001A)
#define KDATA_INSTANCE0_SRC             (KDATA_BASE_ADDR + 0x001B)
#define KDATA_INSTANCE1_SRC             (KDATA_BASE_ADDR + 0x001C)
#define KDATA_INSTANCE_SRC_ENDMARK      (KDATA_BASE_ADDR + 0x001D)
#define KDATA_INSTANCE0_MINISRC         (KDATA_BASE_ADDR + 0x001E)
#define KDATA_INSTANCE1_MINISRC         (KDATA_BASE_ADDR + 0x001F)
#define KDATA_INSTANCE2_MINISRC         (KDATA_BASE_ADDR + 0x0020)
#define KDATA_INSTANCE3_MINISRC         (KDATA_BASE_ADDR + 0x0021)
#define KDATA_INSTANCE_MINISRC_ENDMARK  (KDATA_BASE_ADDR + 0x0022)
#define KDATA_INSTANCE0_CPYTHRU         (KDATA_BASE_ADDR + 0x0023)
#define KDATA_INSTANCE1_CPYTHRU         (KDATA_BASE_ADDR + 0x0024)
#define KDATA_INSTANCE_CPYTHRU_ENDMARK  (KDATA_BASE_ADDR + 0x0025)
#define KDATA_CURRENT_DMA               (KDATA_BASE_ADDR + 0x0026)
#define KDATA_DMA_SWITCH                (KDATA_BASE_ADDR + 0x0027)
#define KDATA_DMA_ACTIVE                (KDATA_BASE_ADDR + 0x0028)
#define KDATA_DMA_XFER0                 (KDATA_BASE_ADDR + 0x0029)
#define KDATA_DMA_XFER1                 (KDATA_BASE_ADDR + 0x002A)
#define KDATA_DMA_XFER2                 (KDATA_BASE_ADDR + 0x002B)
#define KDATA_DMA_XFER3                 (KDATA_BASE_ADDR + 0x002C)
#define KDATA_DMA_XFER4                 (KDATA_BASE_ADDR + 0x002D)
#define KDATA_DMA_XFER5                 (KDATA_BASE_ADDR + 0x002E)
#define KDATA_DMA_XFER6                 (KDATA_BASE_ADDR + 0x002F)
#define KDATA_DMA_XFER7                 (KDATA_BASE_ADDR + 0x0030)
#define KDATA_DMA_XFER8                 (KDATA_BASE_ADDR + 0x0031)
#define KDATA_DMA_XFER_ENDMARK          (KDATA_BASE_ADDR + 0x0032)
#define KDATA_I2S_SAMPLE_COUNT          (KDATA_BASE_ADDR + 0x0033)
#define KDATA_I2S_INT_METER             (KDATA_BASE_ADDR + 0x0034)
#define KDATA_I2S_ACTIVE                (KDATA_BASE_ADDR + 0x0035)
#define KDATA_TIMER_COUNT_RELOAD        (KDATA_BASE_ADDR + 0x0036)
#define KDATA_TIMER_COUNT_CURRENT       (KDATA_BASE_ADDR + 0x0037)
#define KDATA_HALT_SYNCH_CLIENT         (KDATA_BASE_ADDR + 0x0038)
#define KDATA_HALT_SYNCH_DMA            (KDATA_BASE_ADDR + 0x0039)
#define KDATA_HALT_ACKNOWLEDGE          (KDATA_BASE_ADDR + 0x003A)
#define KDATA_ADC1_XFER0                (KDATA_BASE_ADDR + 0x003B)
#define KDATA_ADC1_XFER_ENDMARK         (KDATA_BASE_ADDR + 0x003C)
#define KDATA_ADC1_LEFT_VOLUME			(KDATA_BASE_ADDR + 0x003D)
#define KDATA_ADC1_RIGHT_VOLUME  		(KDATA_BASE_ADDR + 0x003E)
#define KDATA_ADC1_LEFT_SUR_VOL			(KDATA_BASE_ADDR + 0x003F)
#define KDATA_ADC1_RIGHT_SUR_VOL		(KDATA_BASE_ADDR + 0x0040)
#define KDATA_ADC2_XFER0                (KDATA_BASE_ADDR + 0x0041)
#define KDATA_ADC2_XFER_ENDMARK         (KDATA_BASE_ADDR + 0x0042)
#define KDATA_ADC2_LEFT_VOLUME			(KDATA_BASE_ADDR + 0x0043)
#define KDATA_ADC2_RIGHT_VOLUME			(KDATA_BASE_ADDR + 0x0044)
#define KDATA_ADC2_LEFT_SUR_VOL			(KDATA_BASE_ADDR + 0x0045)
#define KDATA_ADC2_RIGHT_SUR_VOL		(KDATA_BASE_ADDR + 0x0046)
#define KDATA_CD_XFER0					(KDATA_BASE_ADDR + 0x0047)
#define KDATA_CD_XFER_ENDMARK			(KDATA_BASE_ADDR + 0x0048)
#define KDATA_CD_LEFT_VOLUME			(KDATA_BASE_ADDR + 0x0049)
#define KDATA_CD_RIGHT_VOLUME			(KDATA_BASE_ADDR + 0x004A)
#define KDATA_CD_LEFT_SUR_VOL			(KDATA_BASE_ADDR + 0x004B)
#define KDATA_CD_RIGHT_SUR_VOL			(KDATA_BASE_ADDR + 0x004C)
#define KDATA_MIC_XFER0					(KDATA_BASE_ADDR + 0x004D)
#define KDATA_MIC_XFER_ENDMARK			(KDATA_BASE_ADDR + 0x004E)
#define KDATA_MIC_VOLUME				(KDATA_BASE_ADDR + 0x004F)
#define KDATA_MIC_SUR_VOL				(KDATA_BASE_ADDR + 0x0050)
#define KDATA_I2S_XFER0                 (KDATA_BASE_ADDR + 0x0051)
#define KDATA_I2S_XFER_ENDMARK          (KDATA_BASE_ADDR + 0x0052)
#define KDATA_CHI_XFER0                 (KDATA_BASE_ADDR + 0x0053)
#define KDATA_CHI_XFER_ENDMARK          (KDATA_BASE_ADDR + 0x0054)
#define KDATA_SPDIF_XFER                (KDATA_BASE_ADDR + 0x0055)
#define KDATA_SPDIF_CURRENT_FRAME       (KDATA_BASE_ADDR + 0x0056)
#define KDATA_SPDIF_FRAME0              (KDATA_BASE_ADDR + 0x0057)
#define KDATA_SPDIF_FRAME1              (KDATA_BASE_ADDR + 0x0058)
#define KDATA_SPDIF_FRAME2              (KDATA_BASE_ADDR + 0x0059)
#define KDATA_SPDIF_REQUEST             (KDATA_BASE_ADDR + 0x005A)
#define KDATA_SPDIF_TEMP                (KDATA_BASE_ADDR + 0x005B)
#define KDATA_SPDIFIN_XFER0             (KDATA_BASE_ADDR + 0x005C)
#define KDATA_SPDIFIN_XFER_ENDMARK      (KDATA_BASE_ADDR + 0x005D)
#define KDATA_SPDIFIN_INT_METER         (KDATA_BASE_ADDR + 0x005E)
#define KDATA_DSP_RESET_COUNT           (KDATA_BASE_ADDR + 0x005F)
#define KDATA_DEBUG_OUTPUT              (KDATA_BASE_ADDR + 0x0060)
#define KDATA_KERNEL_ISR_LIST           (KDATA_BASE_ADDR + 0x0061)
#define KDATA_KERNEL_ISR_CBSR1          (KDATA_BASE_ADDR + 0x0062)
#define KDATA_KERNEL_ISR_CBER1          (KDATA_BASE_ADDR + 0x0063)
#define KDATA_KERNEL_ISR_CBCR           (KDATA_BASE_ADDR + 0x0064)
#define KDATA_KERNEL_ISR_AR0            (KDATA_BASE_ADDR + 0x0065)
#define KDATA_KERNEL_ISR_AR1            (KDATA_BASE_ADDR + 0x0066)
#define KDATA_KERNEL_ISR_AR2            (KDATA_BASE_ADDR + 0x0067)
#define KDATA_KERNEL_ISR_AR3            (KDATA_BASE_ADDR + 0x0068)
#define KDATA_KERNEL_ISR_AR4            (KDATA_BASE_ADDR + 0x0069)
#define KDATA_KERNEL_ISR_AR5            (KDATA_BASE_ADDR + 0x006A)
#define KDATA_KERNEL_ISR_BRCR           (KDATA_BASE_ADDR + 0x006B)
#define KDATA_KERNEL_ISR_PASR           (KDATA_BASE_ADDR + 0x006C)
#define KDATA_KERNEL_ISR_PAER           (KDATA_BASE_ADDR + 0x006D)
#define KDATA_CLIENT_SCRATCH0           (KDATA_BASE_ADDR + 0x006E)
#define KDATA_CLIENT_SCRATCH1           (KDATA_BASE_ADDR + 0x006F)
#define KDATA_KERNEL_SCRATCH            (KDATA_BASE_ADDR + 0x0070)
#define KDATA_KERNEL_ISR_SCRATCH        (KDATA_BASE_ADDR + 0x0071)
#define KDATA_OUEUE_LEFT                (KDATA_BASE_ADDR + 0x0072)
#define KDATA_QUEUE_RIGHT               (KDATA_BASE_ADDR + 0x0073)
#define KDATA_ADC1_REQUEST              (KDATA_BASE_ADDR + 0x0074)
#define KDATA_ADC2_REQUEST              (KDATA_BASE_ADDR + 0x0075)
#define KDATA_CD_REQUEST				(KDATA_BASE_ADDR + 0x0076)
#define KDATA_MIC_REQUEST				(KDATA_BASE_ADDR + 0x0077)
#define KDATA_ADC1_MIXER_REQUEST        (KDATA_BASE_ADDR + 0x0078)
#define KDATA_ADC2_MIXER_REQUEST        (KDATA_BASE_ADDR + 0x0079)
#define KDATA_CD_MIXER_REQUEST			(KDATA_BASE_ADDR + 0x007A)
#define KDATA_MIC_MIXER_REQUEST			(KDATA_BASE_ADDR + 0x007B)
#define KDATA_MIC_SYNC_COUNTER			(KDATA_BASE_ADDR + 0x007C)
#define KDATA_MIXER_WORD0               (KDATA_BASE_ADDR2 + 0x0000)
#define KDATA_MIXER_WORD1               (KDATA_BASE_ADDR2 + 0x0001)
#define KDATA_MIXER_WORD2               (KDATA_BASE_ADDR2 + 0x0002)
#define KDATA_MIXER_WORD3               (KDATA_BASE_ADDR2 + 0x0003)
#define KDATA_MIXER_WORD4               (KDATA_BASE_ADDR2 + 0x0004)
#define KDATA_MIXER_WORD5               (KDATA_BASE_ADDR2 + 0x0005)
#define KDATA_MIXER_WORD6               (KDATA_BASE_ADDR2 + 0x0006)
#define KDATA_MIXER_WORD7               (KDATA_BASE_ADDR2 + 0x0007)
#define KDATA_MIXER_WORD8               (KDATA_BASE_ADDR2 + 0x0008)
#define KDATA_MIXER_WORD9               (KDATA_BASE_ADDR2 + 0x0009)
#define KDATA_MIXER_WORDA               (KDATA_BASE_ADDR2 + 0x000A)
#define KDATA_MIXER_WORDB               (KDATA_BASE_ADDR2 + 0x000B)
#define KDATA_MIXER_WORDC               (KDATA_BASE_ADDR2 + 0x000C)
#define KDATA_MIXER_WORDD               (KDATA_BASE_ADDR2 + 0x000D)
#define KDATA_MIXER_WORDE               (KDATA_BASE_ADDR2 + 0x000E)
#define KDATA_MIXER_WORDF               (KDATA_BASE_ADDR2 + 0x000F)
#define KDATA_MIXER_XFER0               (KDATA_BASE_ADDR2 + 0x0010)
#define KDATA_MIXER_XFER1               (KDATA_BASE_ADDR2 + 0x0011)
#define KDATA_MIXER_XFER2               (KDATA_BASE_ADDR2 + 0x0012)
#define KDATA_MIXER_XFER3               (KDATA_BASE_ADDR2 + 0x0013)
#define KDATA_MIXER_XFER4               (KDATA_BASE_ADDR2 + 0x0014)
#define KDATA_MIXER_XFER5               (KDATA_BASE_ADDR2 + 0x0015)
#define KDATA_MIXER_XFER6               (KDATA_BASE_ADDR2 + 0x0016)
#define KDATA_MIXER_XFER7               (KDATA_BASE_ADDR2 + 0x0017)
#define KDATA_MIXER_XFER8               (KDATA_BASE_ADDR2 + 0x0018)
#define KDATA_MIXER_XFER9               (KDATA_BASE_ADDR2 + 0x0019)
#define KDATA_MIXER_XFER_ENDMARK        (KDATA_BASE_ADDR2 + 0x001A)
#define KDATA_MIXER_TASK_NUMBER         (KDATA_BASE_ADDR2 + 0x001B)
#define KDATA_CURRENT_MIXER             (KDATA_BASE_ADDR2 + 0x001C)
#define KDATA_MIXER_ACTIVE              (KDATA_BASE_ADDR2 + 0x001D)
#define KDATA_MIXER_BANK_STATUS         (KDATA_BASE_ADDR2 + 0x001E)
#define KDATA_DAC_LEFT_VOLUME	        (KDATA_BASE_ADDR2 + 0x001F)
#define KDATA_DAC_RIGHT_VOLUME          (KDATA_BASE_ADDR2 + 0x0020)
#define MAX_INSTANCE_MINISRC            (KDATA_INSTANCE_MINISRC_ENDMARK - KDATA_INSTANCE0_MINISRC)
#define MAX_VIRTUAL_DMA_CHANNELS        (KDATA_DMA_XFER_ENDMARK - KDATA_DMA_XFER0)
#define MAX_VIRTUAL_MIXER_CHANNELS      (KDATA_MIXER_XFER_ENDMARK - KDATA_MIXER_XFER0)
#define MAX_VIRTUAL_ADC1_CHANNELS       (KDATA_ADC1_XFER_ENDMARK - KDATA_ADC1_XFER0)
#define CDATA_INSTANCE_READY            0x00
#define CDATA_HOST_SRC_ADDRL            0x01
#define CDATA_HOST_SRC_ADDRH            0x02
#define CDATA_HOST_SRC_END_PLUS_1L      0x03
#define CDATA_HOST_SRC_END_PLUS_1H      0x04
#define CDATA_HOST_SRC_CURRENTL         0x05
#define CDATA_HOST_SRC_CURRENTH         0x06
#define CDATA_IN_BUF_CONNECT            0x07
#define CDATA_OUT_BUF_CONNECT           0x08
#define CDATA_IN_BUF_BEGIN              0x09
#define CDATA_IN_BUF_END_PLUS_1         0x0A
#define CDATA_IN_BUF_HEAD               0x0B
#define CDATA_IN_BUF_TAIL               0x0C
#define CDATA_OUT_BUF_BEGIN             0x0D
#define CDATA_OUT_BUF_END_PLUS_1        0x0E
#define CDATA_OUT_BUF_HEAD              0x0F
#define CDATA_OUT_BUF_TAIL              0x10
#define CDATA_DMA_CONTROL               0x11
#define CDATA_RESERVED                  0x12
#define CDATA_FREQUENCY                 0x13
#define CDATA_LEFT_VOLUME               0x14
#define CDATA_RIGHT_VOLUME              0x15
#define CDATA_LEFT_SUR_VOL              0x16
#define CDATA_RIGHT_SUR_VOL             0x17
#define CDATA_HEADER_LEN                0x18
#define SRC3_DIRECTION_OFFSET           CDATA_HEADER_LEN
#define SRC3_MODE_OFFSET                (CDATA_HEADER_LEN + 1)
#define SRC3_WORD_LENGTH_OFFSET         (CDATA_HEADER_LEN + 2)
#define SRC3_PARAMETER_OFFSET           (CDATA_HEADER_LEN + 3)
#define SRC3_COEFF_ADDR_OFFSET          (CDATA_HEADER_LEN + 8)
#define SRC3_FILTAP_ADDR_OFFSET         (CDATA_HEADER_LEN + 10)
#define SRC3_TEMP_INBUF_ADDR_OFFSET     (CDATA_HEADER_LEN + 16)
#define SRC3_TEMP_OUTBUF_ADDR_OFFSET    (CDATA_HEADER_LEN + 17)
#define MINISRC_IN_BUFFER_SIZE   ( 0x50 * 2 )
#define MINISRC_OUT_BUFFER_SIZE  ( 0x50 * 2 * 2)
#define MINISRC_TMP_BUFFER_SIZE  ( 112 + ( MINISRC_BIQUAD_STAGE * 3 + 4 ) * 2 * 2 )
#define MINISRC_BIQUAD_STAGE    2
#define MINISRC_COEF_LOC          0x175
#define DMACONTROL_BLOCK_MASK           0x000F
#define DMAC_BLOCK0_SELECTOR           0x0000
#define DMAC_BLOCK1_SELECTOR           0x0001
#define DMAC_BLOCK2_SELECTOR           0x0002
#define DMAC_BLOCK3_SELECTOR           0x0003
#define DMAC_BLOCK4_SELECTOR           0x0004
#define DMAC_BLOCK5_SELECTOR           0x0005
#define DMAC_BLOCK6_SELECTOR           0x0006
#define DMAC_BLOCK7_SELECTOR           0x0007
#define DMAC_BLOCK8_SELECTOR           0x0008
#define DMAC_BLOCK9_SELECTOR           0x0009
#define DMAC_BLOCKA_SELECTOR           0x000A
#define DMAC_BLOCKB_SELECTOR           0x000B
#define DMAC_BLOCKC_SELECTOR           0x000C
#define DMAC_BLOCKD_SELECTOR           0x000D
#define DMAC_BLOCKE_SELECTOR           0x000E
#define DMAC_BLOCKF_SELECTOR           0x000F
#define DMACONTROL_PAGE_MASK            0x00F0
#define DMAC_PAGE0_SELECTOR            0x0030
#define DMAC_PAGE1_SELECTOR            0x0020
#define DMAC_PAGE2_SELECTOR            0x0010
#define DMAC_PAGE3_SELECTOR            0x0000
#define DMACONTROL_AUTOREPEAT           0x1000
#define DMACONTROL_STOPPED              0x2000
#define DMACONTROL_DIRECTION            0x0100
#define ARB_VOLUME ( 0x6800 )
#define LO(x) ((x) & 0xffff)
#define HI(x) LO((x) >> 16)
#define MINISRC_LPF_LEN 10

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
    

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint8_t host_int_status;
    uint16_t host_int_ctrl;
    uint8_t assp_control_a;
    uint8_t assp_control_b;
    uint8_t assp_control_c;
    uint8_t assp_host_int_status;
    uint8_t hardware_vol_ctrl;
    uint8_t shadow_mix_reg_voice;
    uint8_t hw_vol_counter_voice;
    uint8_t shadow_mix_reg_master;
    uint8_t hw_vol_counter_master;

    /* DMA Context */
    uint8_t codec_command;
    uint16_t codec_data;
    uint16_t ring_bus_ctrl_a;
    uint16_t ring_bus_ctrl_b;
    uint16_t sdo_out_dest_ctrl;
    uint16_t sdo_in_dest_ctrl;
    uint16_t gpio_direction;
    uint16_t gpio_mask;
    uint16_t gpio_data;
    uint16_t dsp_port_memory_type;
    uint16_t dsp_port_memory_index;
    uint16_t dsp_port_memory_data;
    uint16_t ac97_regs[128];
    uint16_t assp_internal_data[0x3000];
    uint16_t assp_internal_code[0x3000];
};

struct play_vals {
	uint16_t addr, val;
};
struct rec_vals {
	uint16_t addr, val;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    bool irq_active = false;
    if (s->host_int_status & s->host_int_ctrl) {
        irq_active = true;
    }
    
    if (irq_active) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case DSP_PORT_MEMORY_TYPE: val = s->dsp_port_memory_type; break;
    case DSP_PORT_MEMORY_INDEX: val = s->dsp_port_memory_index; break;
    case DSP_PORT_MEMORY_DATA:
        if ((s->dsp_port_memory_type & MEMTYPE_MASK) == MEMTYPE_INTERNAL_DATA) {
            if (s->dsp_port_memory_index < 0x3000)
                val = s->assp_internal_data[s->dsp_port_memory_index];
        } else if ((s->dsp_port_memory_type & MEMTYPE_MASK) == MEMTYPE_INTERNAL_CODE) {
            if (s->dsp_port_memory_index < 0x3000)
                val = s->assp_internal_code[s->dsp_port_memory_index];
        }
        break;
    case CODEC_COMMAND: val = s->codec_command & ~0x01; break;
    case CODEC_DATA: val = s->codec_data; break;
    case HOST_INT_STATUS: val = s->host_int_status; break;
    case HOST_INT_CTRL: val = s->host_int_ctrl; break;
    case ASSP_HOST_INT_STATUS: val = s->assp_host_int_status; break;
    case ASSP_CONTROL_A: val = s->assp_control_a; break;
    case ASSP_CONTROL_B: val = s->assp_control_b; break;
    case ASSP_CONTROL_C: val = s->assp_control_c; break;
    case HARDWARE_VOL_CTRL: val = s->hardware_vol_ctrl; break;
    case SHADOW_MIX_REG_VOICE: val = s->shadow_mix_reg_voice; break;
    case HW_VOL_COUNTER_VOICE: val = s->hw_vol_counter_voice; break;
    case SHADOW_MIX_REG_MASTER: val = s->shadow_mix_reg_master; break;
    case HW_VOL_COUNTER_MASTER: val = s->hw_vol_counter_master; break;
    case RING_BUS_CTRL_A: val = s->ring_bus_ctrl_a; break;
    case RING_BUS_CTRL_B: val = s->ring_bus_ctrl_b; break;
    case SDO_OUT_DEST_CTRL: val = s->sdo_out_dest_ctrl; break;
    case SDO_IN_DEST_CTRL: val = s->sdo_in_dest_ctrl; break;
    case GPIO_DIRECTION: val = s->gpio_direction; break;
    case GPIO_MASK: val = s->gpio_mask; break;
    case GPIO_DATA: val = s->gpio_data; break;
    }
    
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    switch (addr) {
    case DSP_PORT_MEMORY_TYPE:
        s->dsp_port_memory_type = val;
        break;
    case DSP_PORT_MEMORY_INDEX:
        s->dsp_port_memory_index = val;
        break;
    case DSP_PORT_MEMORY_DATA:
        s->dsp_port_memory_data = val;
        if ((s->dsp_port_memory_type & MEMTYPE_MASK) == MEMTYPE_INTERNAL_DATA) {
            if (s->dsp_port_memory_index < 0x3000)
                s->assp_internal_data[s->dsp_port_memory_index] = val;
        } else if ((s->dsp_port_memory_type & MEMTYPE_MASK) == MEMTYPE_INTERNAL_CODE) {
            if (s->dsp_port_memory_index < 0x3000)
                s->assp_internal_code[s->dsp_port_memory_index] = val;
        }
        break;
    case CODEC_COMMAND:
        s->codec_command = val;
        if (val & 0x80) {
            s->codec_data = s->ac97_regs[val & 0x7f];
        } else {
            s->ac97_regs[val & 0x7f] = s->codec_data;
        }
        break;
    case CODEC_DATA:
        s->codec_data = val;
        break;
    case HOST_INT_STATUS:
        s->host_int_status &= ~val;
        pcibase_update_irq(s);
        break;
    case HOST_INT_CTRL:
        s->host_int_ctrl = val;
        pcibase_update_irq(s);
        break;
    case ASSP_HOST_INT_STATUS:
        s->assp_host_int_status &= ~val;
        pcibase_update_irq(s);
        break;
    case ASSP_CONTROL_A: s->assp_control_a = val; break;
    case ASSP_CONTROL_B: s->assp_control_b = val; break;
    case ASSP_CONTROL_C: s->assp_control_c = val; break;
    case HARDWARE_VOL_CTRL: s->hardware_vol_ctrl = val; break;
    case SHADOW_MIX_REG_VOICE: s->shadow_mix_reg_voice = val; break;
    case HW_VOL_COUNTER_VOICE: s->hw_vol_counter_voice = val; break;
    case SHADOW_MIX_REG_MASTER: s->shadow_mix_reg_master = val; break;
    case HW_VOL_COUNTER_MASTER: s->hw_vol_counter_master = val; break;
    case RING_BUS_CTRL_A: s->ring_bus_ctrl_a = val; break;
    case RING_BUS_CTRL_B: s->ring_bus_ctrl_b = val; break;
    case SDO_OUT_DEST_CTRL: s->sdo_out_dest_ctrl = val; break;
    case SDO_IN_DEST_CTRL: s->sdo_in_dest_ctrl = val; break;
    case GPIO_DIRECTION: s->gpio_direction = val; break;
    case GPIO_MASK: s->gpio_mask = val; break;
    case GPIO_DATA: s->gpio_data = val; break;
    }
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

    s->host_int_status = 0;
    s->host_int_ctrl = 0;
    s->assp_control_a = 0;
    s->assp_control_b = 0;
    s->assp_control_c = 0;
    s->assp_host_int_status = 0;
    s->hardware_vol_ctrl = 0;
    s->shadow_mix_reg_voice = 0;
    s->hw_vol_counter_voice = 0;
    s->shadow_mix_reg_master = 0;
    s->hw_vol_counter_master = 0;
    
    s->codec_command = 0;
    s->codec_data = 0;
    s->ring_bus_ctrl_a = 0;
    s->ring_bus_ctrl_b = 0;
    s->sdo_out_dest_ctrl = 0;
    s->sdo_in_dest_ctrl = 0;
    s->gpio_direction = 0;
    s->gpio_mask = 0;
    s->gpio_data = 0;
    s->dsp_port_memory_type = 0;
    s->dsp_port_memory_index = 0;
    s->dsp_port_memory_data = 0;

    memset(s->ac97_regs, 0, sizeof(s->ac97_regs));
    s->ac97_regs[0x7c] = 0x8384;
    s->ac97_regs[0x7e] = 0x7609;

    memset(s->assp_internal_data, 0, sizeof(s->assp_internal_data));
    memset(s->assp_internal_code, 0, sizeof(s->assp_internal_code));
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_ESS );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_ESS_ALLEGRO_1 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_MULTIMEDIA_AUDIO );
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
    s->bar_info[0] = (BARInfo){0, BAR_TYPE_PIO, 0x100, "maestro3-pio"};
      
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    pci_set_word(pdev->wmask + PCI_LEGACY_AUDIO_CTRL, 0xffff);
    pci_set_long(pdev->wmask + PCI_ALLEGRO_CONFIG, 0xffffffff);
    pci_set_long(pdev->wmask + PCI_USER_CONFIG, 0xffffffff);
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
    .name = "snd_maestro3_pci",
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
