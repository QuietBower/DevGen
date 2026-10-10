/*
 * QEMU PCI device model for cb_pcidas64, behavioral subset for driver probing
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
#include "hw/pci/pci_ids.h"

#ifndef PCI_VENDOR_ID_CB
#define PCI_VENDOR_ID_CB 0x1307
#endif

#define TYPE_PCIBASE_DEVICE "cb_pcidas64_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCIBASE_VENDOR_ID      PCI_VENDOR_ID_CB
#define PCIBASE_DEVICE_ID      0x001d
#define PCIBASE_CLASS_ID       PCI_CLASS_OTHERS

#define DMA_BUFFER_SIZE        0x1000
#define TIMER_BASE             25
#define PRESCALED_TIMER_BASE   10000
#define DAC_FIFO_SIZE          0x2000
#define MAX_AI_DMA_RING_COUNT  (0x80000 / DMA_BUFFER_SIZE)
#define MIN_AI_DMA_RING_COUNT  (0x10000 / DMA_BUFFER_SIZE)
#define AO_DMA_RING_COUNT      (0x10000 / DMA_BUFFER_SIZE)

#define PLX_CNTRL_EESK         (1U << 24)
#define PLX_CNTRL_USERO        (1U << 16)
#define PLX_CNTRL_EEWB         (1U << 26)
#define PLX_CNTRL_EERB         (1U << 27)
#define PLX_CNTRL_EECS         (1U << 25)
#define PLX_CNTRL_EEPRESENT    (1U << 28)

#define PLX_REG_CNTRL          0x006c
#define PLX_REG_INTCSR         0x0068
#define PLX_REG_BIGEND         0x000c
#define PLX_REG_DMAMODE0       0x0080
#define PLX_REG_DMAPADR0       0x0084
#define PLX_REG_DMALADR0       0x0088
#define PLX_REG_DMASIZ0        0x008c
#define PLX_REG_DMAMODE1       0x0094
#define PLX_REG_DMAPADR1       0x0098
#define PLX_REG_DMALADR1       0x009c
#define PLX_REG_DMASIZ1        0x00a0
#define PLX_REG_DMADPR0        0x0090
#define PLX_REG_DMADPR1        0x00a4
#define PLX_REG_DMACSR0        0x00a8
#define PLX_REG_DMACSR1        0x00a9
#define PLX_REG_L2PDBELL       0x0064
#define PLX_REG_LAS0RR         0x0000
#define PLX_REG_LAS0BA         0x0004
#define PLX_REG_LAS1RR         0x00f0
#define PLX_REG_LAS1BA         0x00f4

#define PLX_INTCSR_PLIEN       (1U << 11)
#define PLX_INTCSR_PABORTIEN   (1U << 10)
#define PLX_INTCSR_PIEN        (1U << 8)
#define PLX_INTCSR_LSEABORTEN  (1U << 0)
#define PLX_INTCSR_LSEPARITYEN (1U << 1)
#define PLX_INTCSR_LIOEN       (1U << 16)
#define PLX_INTCSR_DMA0IEN     (1U << 18)
#define PLX_INTCSR_DMA1IEN     (1U << 19)
#define PLX_INTCSR_LDBIA       (1U << 20)
#define PLX_INTCSR_DMA0IA      (1U << 21)
#define PLX_INTCSR_DMA1IA      (1U << 22)

#define PLX_DMAMODE_BURSTEN    (1U << 8)
#define PLX_DMAMODE_LACONST    (1U << 11)
#define PLX_DMAMODE_DONEIEN    (1U << 10)
#define PLX_DMAMODE_READYIEN   (1U << 6)
#define PLX_DMAMODE_CHAINEN    (1U << 9)
#define PLX_DMAMODE_DEMAND     (1U << 12)
#define PLX_DMAMODE_INTRPCI    (1U << 17)
#define PLX_DMAMODE_BTERMIEN   (1U << 7)
#define PLX_DMAMODE_WIDTH_16   ((1U << 0) * 1)
#define PLX_DMAMODE_WIDTH_32   ((1U << 0) * 2)

#define PLX_BIGEND_DMA0        (1U << 7)
#define PLX_BIGEND_DMA1        (1U << 6)

#define PLX_DMADPR_TCINTR      (1U << 2)
#define PLX_DMADPR_XFERL2P     (1U << 3)
#define PLX_DMADPR_DESCPCI     (1U << 0)
#define PLX_DMADPR_CHAINEND    (1U << 1)

#define PLX_DMACSR_CLEARINTR   (1U << 3)
#define PLX_DMACSR_ENABLE      (1U << 0)
#define PLX_DMACSR_START       (1U << 1)
#define PLX_DMACSR_DONE        (1U << 4)
#define PLX_DMACSR_ABORT       (1U << 2)

/* main_iobase register offsets (16-bit unless noted) */
enum write_only_registers {
    INTR_ENABLE_REG = 0x0,
    HW_CONFIG_REG = 0x2,
    DAQ_SYNC_REG = 0x0c,
    DAQ_ATRIG_LOW_4020_REG = 0x0c,
    ADC_CONTROL0_REG = 0x10,
    ADC_CONTROL1_REG = 0x12,
    CALIBRATION_REG = 0x14,
    ADC_SAMPLE_INTERVAL_LOWER_REG = 0x16,
    ADC_SAMPLE_INTERVAL_UPPER_REG = 0x18,
    ADC_DELAY_INTERVAL_LOWER_REG = 0x1a,
    ADC_DELAY_INTERVAL_UPPER_REG = 0x1c,
    ADC_COUNT_LOWER_REG = 0x1e,
    ADC_COUNT_UPPER_REG = 0x20,
    ADC_START_REG = 0x22,
    ADC_CONVERT_REG = 0x24,
    ADC_QUEUE_CLEAR_REG = 0x26,
    ADC_QUEUE_LOAD_REG = 0x28,
    ADC_BUFFER_CLEAR_REG = 0x2a,
    ADC_QUEUE_HIGH_REG = 0x2c,
    DAC_CONTROL0_REG = 0x50,
    DAC_CONTROL1_REG = 0x52,
    DAC_SAMPLE_INTERVAL_LOWER_REG = 0x54,
    DAC_SAMPLE_INTERVAL_UPPER_REG = 0x56,
    DAC_SELECT_REG = 0x60,
    DAC_START_REG = 0x64,
    DAC_BUFFER_CLEAR_REG = 0x66,
};

enum read_only_registers {
    HW_STATUS_REG = 0x0,
    PIPE1_READ_REG = 0x4,
    ADC_READ_PNTR_REG = 0x8,
    LOWER_XFER_REG = 0x10,
    ADC_WRITE_PNTR_REG = 0x0c,
    PREPOST_REG = 0x14,
};

enum read_write_registers {
    I8255_4020_REG = 0x48,
    ADC_QUEUE_FIFO_REG = 0x100,
    ADC_FIFO_REG = 0x200,
    DAC_FIFO_REG = 0x300,
};

enum dio_counter_registers {
    DIO_8255_OFFSET = 0x0,
    DO_REG = 0x20,
    DI_REG = 0x28,
    DIO_DIRECTION_60XX_REG = 0x40,
    DIO_DATA_60XX_REG = 0x48,
};

enum intr_enable_contents {
    ADC_INTR_SRC_MASK = 0x3,
    ADC_INTR_QFULL_BITS = 0x0,
    ADC_INTR_EOC_BITS = 0x1,
    ADC_INTR_EOSCAN_BITS = 0x2,
    ADC_INTR_EOSEQ_BITS = 0x3,
    EN_ADC_INTR_SRC_BIT = 0x4,
    EN_ADC_DONE_INTR_BIT = 0x8,
    DAC_INTR_SRC_MASK = 0x30,
    DAC_INTR_QEMPTY_BITS = 0x0,
    DAC_INTR_HIGH_CHAN_BITS = 0x10,
    EN_DAC_INTR_SRC_BIT = 0x40,
    EN_DAC_DONE_INTR_BIT = 0x80,
    EN_ADC_ACTIVE_INTR_BIT = 0x200,
    EN_ADC_STOP_INTR_BIT = 0x400,
    EN_DAC_ACTIVE_INTR_BIT = 0x800,
    EN_DAC_UNDERRUN_BIT = 0x4000,
    EN_ADC_OVERRUN_BIT = 0x8000,
};

enum hw_config_contents {
    MASTER_CLOCK_4020_MASK = 0x3,
    INTERNAL_CLOCK_4020_BITS = 0x1,
    BNC_CLOCK_4020_BITS = 0x2,
    EXT_CLOCK_4020_BITS = 0x3,
    EXT_QUEUE_BIT = 0x200,
    SLOW_DAC_BIT = 0x400,
    HW_CONFIG_DUMMY_BITS = 0x2000,
    DMA_CH_SELECT_BIT = 0x8000,
    FIFO_SIZE_REG = 0x4,
    DAC_FIFO_SIZE_MASK = 0xff00,
    DAC_FIFO_BITS = 0xf800,
};

enum daq_atrig_low_4020_contents {
    EXT_AGATE_BNC_BIT = 0x8000,
    EXT_STOP_TRIG_BNC_BIT = 0x4000,
    EXT_START_TRIG_BNC_BIT = 0x2000,
};

enum adc_control0_contents {
    ADC_GATE_SRC_MASK = 0x3,
    ADC_SOFT_GATE_BITS = 0x1,
    ADC_EXT_GATE_BITS = 0x2,
    ADC_ANALOG_GATE_BITS = 0x3,
    ADC_GATE_LEVEL_BIT = 0x4,
    ADC_GATE_POLARITY_BIT = 0x8,
    ADC_START_TRIG_SOFT_BITS = 0x10,
    ADC_START_TRIG_EXT_BITS = 0x20,
    ADC_START_TRIG_ANALOG_BITS = 0x30,
    ADC_START_TRIG_MASK = 0x30,
    ADC_START_TRIG_FALLING_BIT = 0x40,
    ADC_EXT_CONV_FALLING_BIT = 0x800,
    ADC_SAMPLE_COUNTER_EN_BIT = 0x1000,
    ADC_DMA_DISABLE_BIT = 0x4000,
    ADC_ENABLE_BIT = 0x8000,
};

enum adc_control1_contents {
    ADC_QUEUE_CONFIG_BIT = 0x1,
    CONVERT_POLARITY_BIT = 0x10,
    EOC_POLARITY_BIT = 0x20,
    ADC_SW_GATE_BIT = 0x40,
    ADC_DITHER_BIT = 0x200,
    RETRIGGER_BIT = 0x800,
    ADC_LO_CHANNEL_4020_MASK = 0x300,
    ADC_HI_CHANNEL_4020_MASK = 0xc00,
    TWO_CHANNEL_4020_BITS = 0x1000,
    FOUR_CHANNEL_4020_BITS = 0x2000,
    CHANNEL_MODE_4020_MASK = 0x3000,
    ADC_MODE_MASK = 0xf000,
};

enum calibration_contents {
    SELECT_8800_BIT = 0x1,
    SELECT_8402_64XX_BIT = 0x2,
    SELECT_1590_60XX_BIT = 0x2,
    CAL_EN_64XX_BIT = 0x40,
    SERIAL_DATA_IN_BIT = 0x80,
    SERIAL_CLOCK_BIT = 0x100,
    CAL_EN_60XX_BIT = 0x200,
    CAL_GAIN_BIT = 0x800,
};

enum adc_queue_load_contents {
    UNIP_BIT = 0x800,
    ADC_SE_DIFF_BIT = 0x1000,
    ADC_COMMON_BIT = 0x2000,
    QUEUE_EOSEQ_BIT = 0x4000,
    QUEUE_EOSCAN_BIT = 0x8000,
};

enum dac_control0_contents {
    DAC_ENABLE_BIT = 0x8000,
    DAC_CYCLIC_STOP_BIT = 0x4000,
    DAC_WAVEFORM_MODE_BIT = 0x100,
    DAC_EXT_UPDATE_FALLING_BIT = 0x80,
    DAC_EXT_UPDATE_ENABLE_BIT = 0x40,
    WAVEFORM_TRIG_MASK = 0x30,
    WAVEFORM_TRIG_DISABLED_BITS = 0x0,
    WAVEFORM_TRIG_SOFT_BITS = 0x10,
    WAVEFORM_TRIG_EXT_BITS = 0x20,
    WAVEFORM_TRIG_ADC1_BITS = 0x30,
    WAVEFORM_TRIG_FALLING_BIT = 0x8,
    WAVEFORM_GATE_LEVEL_BIT = 0x4,
    WAVEFORM_GATE_ENABLE_BIT = 0x2,
    WAVEFORM_GATE_SELECT_BIT = 0x1,
};

enum dac_control1_contents {
    DAC_WRITE_POLARITY_BIT = 0x800,
    DAC1_EXT_REF_BIT = 0x200,
    DAC0_EXT_REF_BIT = 0x100,
    DAC_OUTPUT_ENABLE_BIT = 0x80,
    DAC_UPDATE_POLARITY_BIT = 0x40,
    DAC_SW_GATE_BIT = 0x20,
    DAC1_UNIPOLAR_BIT = 0x8,
    DAC0_UNIPOLAR_BIT = 0x2,
};

enum hw_status_contents {
    DAC_UNDERRUN_BIT = 0x1,
    ADC_OVERRUN_BIT = 0x2,
    DAC_ACTIVE_BIT = 0x4,
    ADC_ACTIVE_BIT = 0x8,
    DAC_INTR_PENDING_BIT = 0x10,
    ADC_INTR_PENDING_BIT = 0x20,
    DAC_DONE_BIT = 0x40,
    ADC_DONE_BIT = 0x80,
    EXT_INTR_PENDING_BIT = 0x100,
    ADC_STOP_BIT = 0x200,
};

enum i2c_addresses {
    RANGE_CAL_I2C_ADDR = 0x20,
    CALDAC0_I2C_ADDR = 0x0c,
    CALDAC1_I2C_ADDR = 0x0d,
};

enum range_cal_i2c_contents {
    ADC_SRC_4020_MASK = 0x70,
    BNC_TRIG_THRESHOLD_0V_BIT = 0x80,
};

enum register_layout {
    LAYOUT_60XX,
    LAYOUT_64XX,
    LAYOUT_4020,
};

enum pcidas64_boardid {
    BOARD_PCIDAS6402_16,
    BOARD_PCIDAS6402_12,
    BOARD_PCIDAS64_M1_16,
    BOARD_PCIDAS64_M2_16,
    BOARD_PCIDAS64_M3_16,
    BOARD_PCIDAS6013,
    BOARD_PCIDAS6014,
    BOARD_PCIDAS6023,
    BOARD_PCIDAS6025,
    BOARD_PCIDAS6030,
    BOARD_PCIDAS6031,
    BOARD_PCIDAS6032,
    BOARD_PCIDAS6033,
    BOARD_PCIDAS6034,
    BOARD_PCIDAS6035,
    BOARD_PCIDAS6036,
    BOARD_PCIDAS6040,
    BOARD_PCIDAS6052,
    BOARD_PCIDAS6070,
    BOARD_PCIDAS6071,
    BOARD_PCIDAS4020_12,
    BOARD_PCIDAS6402_16_JR,
    BOARD_PCIDAS64_M1_16_JR,
    BOARD_PCIDAS64_M2_16_JR,
    BOARD_PCIDAS64_M3_16_JR,
    BOARD_PCIDAS64_M1_14,
    BOARD_PCIDAS64_M2_14,
    BOARD_PCIDAS64_M3_14,
};

struct hw_fifo_info {
    unsigned int num_segments;
    unsigned int max_segment_length;
    unsigned int sample_packing_ratio;
    uint16_t fifo_size_reg_mask;
};

struct comedi_krange {
    int min;
    int max;
    unsigned int flags;
};

struct comedi_lrange {
    int length;
    struct comedi_krange range[];
};

struct pcidas64_board {
    const char *name;
    int ai_se_chans;
    int ai_bits;
    int ai_speed;
    const struct comedi_lrange *ai_range_table;
    const uint8_t *ai_range_code;
    int ao_nchan;
    int ao_bits;
    int ao_scan_speed;
    const struct comedi_lrange *ao_range_table;
    const int *ao_range_code;
    const struct hw_fifo_info *ai_fifo;
    enum register_layout layout;
    unsigned has_8255:1;
};

struct ext_clock_info {
    unsigned int divisor;
    unsigned int chanspec;
};

struct plx_dma_desc {
    uint32_t pci_start_addr;
    uint32_t local_start_addr;
    uint32_t transfer_size;
    uint32_t next;
};

/* bytes_in_sample was unused; remove to avoid compiler warning without affecting behavior */

extern const struct comedi_lrange ai_ranges_64xx;
extern const struct comedi_lrange ai_ranges_64_mx;
extern const struct comedi_lrange ai_ranges_60xx;
extern const struct comedi_lrange ai_ranges_6030;
extern const struct comedi_lrange ai_ranges_6052;
extern const struct comedi_lrange ai_ranges_4020;
extern const struct comedi_lrange ao_ranges_64xx;
extern const struct comedi_lrange ao_ranges_6030;
extern const struct comedi_lrange ao_ranges_4020;
extern const struct hw_fifo_info ai_fifo_4020;
extern const struct hw_fifo_info ai_fifo_64xx;
extern const struct hw_fifo_info ai_fifo_60xx;

extern const struct pcidas64_board pcidas64_boards[];


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

    /* PLX 9080 register shadows (BAR0, 32-bit) */
    uint32_t plx_cntrl;
    uint32_t plx_intcsr;
    uint32_t plx_bigend;
    uint32_t plx_dmamode0;
    uint32_t plx_dmamode1;
    uint32_t plx_dmapadr0;
    uint32_t plx_dmapadr1;
    uint32_t plx_dmaladr0;
    uint32_t plx_dmaladr1;
    uint32_t plx_dmadpr0;
    uint32_t plx_dmadpr1;
    uint32_t plx_dmasiz0;
    uint32_t plx_dmasiz1;
    uint8_t  plx_dmacsr0;
    uint8_t  plx_dmacsr1;
    uint32_t plx_l2pdbell;
    uint32_t plx_las0rr;
    uint32_t plx_las0ba;
    uint32_t plx_las1rr;
    uint32_t plx_las1ba;

    /* main_iobase register shadows (BAR2, 16-bit unless noted) */
    uint16_t intr_enable;
    uint16_t hw_config;
    uint16_t daq_sync;
    uint16_t adc_control0;
    uint16_t adc_control1;
    uint16_t calibration;
    uint16_t adc_sample_interval_lower;
    uint16_t adc_sample_interval_upper;
    uint16_t adc_delay_interval_lower;
    uint16_t adc_delay_interval_upper;
    uint16_t adc_count_lower;
    uint16_t adc_count_upper;
    uint16_t adc_queue_load;
    uint16_t adc_queue_high;
    uint16_t dac_control0;
    uint16_t dac_control1;
    uint16_t dac_sample_interval_lower;
    uint16_t dac_sample_interval_upper;
    uint16_t dac_select;

    /* status-like read-only shadows */
    uint16_t hw_status;
    uint16_t adc_read_pntr;
    uint16_t adc_write_pntr;
    uint16_t prepost;

    /* simple FIFOs (16-bit samples) */
    uint16_t adc_fifo[1024];
    uint32_t adc_fifo_wpos;
    uint32_t adc_fifo_rpos;

    uint16_t dac_fifo[1024];
    uint32_t dac_fifo_wpos;
    uint32_t dac_fifo_rpos;

    /* BAR3 simple registers (8-bit DIO area) */
    uint8_t dio_do_reg;
    uint8_t dio_di_reg;
    uint8_t dio_direction_60xx;
    uint8_t dio_data_60xx;

    /* interrupt state */
    bool irq_asserted;
};


/* Internal helper for status-triggered signaling. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool enable_plx = (s->plx_intcsr & PLX_INTCSR_PLIEN) &&
                      (s->plx_intcsr & PLX_INTCSR_PIEN);
    bool any_status_irq = false;

    /* Map simple interrupt conditions from hw_status + intr_enable */
    if (s->intr_enable & (EN_ADC_DONE_INTR_BIT | EN_ADC_OVERRUN_BIT |
                          EN_DAC_DONE_INTR_BIT | EN_DAC_UNDERRUN_BIT |
                          EN_ADC_ACTIVE_INTR_BIT | EN_ADC_STOP_INTR_BIT |
                          EN_DAC_ACTIVE_INTR_BIT)) {
        if (s->hw_status & (ADC_DONE_BIT | ADC_OVERRUN_BIT |
                            DAC_DONE_BIT | DAC_UNDERRUN_BIT |
                            ADC_ACTIVE_BIT | ADC_STOP_BIT |
                            DAC_ACTIVE_BIT)) {
            any_status_irq = true;
        }
    }

    bool want_irq = enable_plx && any_status_irq;

    if (want_irq != s->irq_asserted) {
        s->irq_asserted = want_irq;
        pci_set_irq(pdev, want_irq ? 1 : 0);
    }
}

/* Device-initiated DMA logic: minimal stub, DMA not actually used by driver
 * before initial command execution; keep as no-op. */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    (void)s;
    (void)is_write;
}

/* Helpers for BAR dispatch - unused, so drop static inline to avoid warning */

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    hwaddr off = addr;

    /* BAR0: PLX */
    if (off < 0x1000) {
        switch (off) {
        case PLX_REG_CNTRL:
            val = s->plx_cntrl;
            break;
        case PLX_REG_INTCSR:
            val = s->plx_intcsr;
            break;
        case PLX_REG_BIGEND:
            val = s->plx_bigend;
            break;
        case PLX_REG_DMAMODE0:
            val = s->plx_dmamode0;
            break;
        case PLX_REG_DMAMODE1:
            val = s->plx_dmamode1;
            break;
        case PLX_REG_DMAPADR0:
            val = s->plx_dmapadr0;
            break;
        case PLX_REG_DMAPADR1:
            val = s->plx_dmapadr1;
            break;
        case PLX_REG_DMALADR0:
            val = s->plx_dmaladr0;
            break;
        case PLX_REG_DMALADR1:
            val = s->plx_dmaladr1;
            break;
        case PLX_REG_DMADPR0:
            val = s->plx_dmadpr0;
            break;
        case PLX_REG_DMADPR1:
            val = s->plx_dmadpr1;
            break;
        case PLX_REG_DMASIZ0:
            val = s->plx_dmasiz0;
            break;
        case PLX_REG_DMASIZ1:
            val = s->plx_dmasiz1;
            break;
        case PLX_REG_DMACSR0:
            val = s->plx_dmacsr0;
            break;
        case PLX_REG_DMACSR1:
            val = s->plx_dmacsr1;
            break;
        case PLX_REG_L2PDBELL:
            val = s->plx_l2pdbell;
            break;
        case PLX_REG_LAS0RR:
            val = s->plx_las0rr;
            break;
        case PLX_REG_LAS0BA:
            val = s->plx_las0ba;
            break;
        case PLX_REG_LAS1RR:
            val = s->plx_las1rr;
            break;
        case PLX_REG_LAS1BA:
            val = s->plx_las1ba;
            break;
        default:
            break;
        }
        return val;
    }

    /* BAR2: main_iobase registers */
    off -= 0x1000;
    if (off < 0x1000) {
        switch (off) {
        case HW_STATUS_REG:
            val = s->hw_status;
            break;
        case PIPE1_READ_REG:
            /* 16-bit read from ADC FIFO if available */
            if (s->adc_fifo_rpos != s->adc_fifo_wpos) {
                val = s->adc_fifo[s->adc_fifo_rpos & 0x3ff];
                s->adc_fifo_rpos++;
                s->adc_read_pntr = (uint16_t)(s->adc_fifo_rpos & 0x7fff);
            } else {
                val = 0;
            }
            break;
        case ADC_READ_PNTR_REG:
            val = s->adc_read_pntr;
            break;
        case ADC_WRITE_PNTR_REG:
            val = s->adc_write_pntr;
            break;
        case PREPOST_REG:
            val = s->prepost;
            break;
        case ADC_FIFO_REG: {
            /* some code reads via 32-bit l, some via 16-bit w */
            if (size == 4) {
                uint32_t low = 0, high = 0;
                if (s->adc_fifo_rpos != s->adc_fifo_wpos) {
                    low = s->adc_fifo[s->adc_fifo_rpos & 0x3ff];
                    s->adc_fifo_rpos++;
                    s->adc_read_pntr = (uint16_t)(s->adc_fifo_rpos & 0x7fff);
                }
                if (s->adc_fifo_rpos != s->adc_fifo_wpos) {
                    high = s->adc_fifo[s->adc_fifo_rpos & 0x3ff];
                    s->adc_fifo_rpos++;
                    s->adc_read_pntr = (uint16_t)(s->adc_fifo_rpos & 0x7fff);
                }
                val = (uint32_t)low | ((uint32_t)high << 16);
            } else {
                if (s->adc_fifo_rpos != s->adc_fifo_wpos) {
                    val = s->adc_fifo[s->adc_fifo_rpos & 0x3ff];
                    s->adc_fifo_rpos++;
                    s->adc_read_pntr = (uint16_t)(s->adc_fifo_rpos & 0x7fff);
                } else {
                    val = 0;
                }
            }
            break;
        }
        case DAC_FIFO_REG: {
            /* readback not used, just return 0 */
            val = 0;
            break;
        }
        default:
            /* 16-bit register generic read */
            if (size == 2) {
                uint16_t *p16 = NULL;
                switch (off) {
                case INTR_ENABLE_REG: p16 = &s->intr_enable; break;
                case HW_CONFIG_REG: p16 = &s->hw_config; break;
                case DAQ_SYNC_REG: p16 = &s->daq_sync; break;
                case ADC_CONTROL0_REG: p16 = &s->adc_control0; break;
                case ADC_CONTROL1_REG: p16 = &s->adc_control1; break;
                case CALIBRATION_REG: p16 = &s->calibration; break;
                case ADC_SAMPLE_INTERVAL_LOWER_REG: p16 = &s->adc_sample_interval_lower; break;
                case ADC_SAMPLE_INTERVAL_UPPER_REG: p16 = &s->adc_sample_interval_upper; break;
                case ADC_DELAY_INTERVAL_LOWER_REG: p16 = &s->adc_delay_interval_lower; break;
                case ADC_DELAY_INTERVAL_UPPER_REG: p16 = &s->adc_delay_interval_upper; break;
                case ADC_COUNT_LOWER_REG: p16 = &s->adc_count_lower; break;
                case ADC_COUNT_UPPER_REG: p16 = &s->adc_count_upper; break;
                case ADC_QUEUE_LOAD_REG: p16 = &s->adc_queue_load; break;
                case ADC_QUEUE_HIGH_REG: p16 = &s->adc_queue_high; break;
                case DAC_CONTROL0_REG: p16 = &s->dac_control0; break;
                case DAC_CONTROL1_REG: p16 = &s->dac_control1; break;
                case DAC_SAMPLE_INTERVAL_LOWER_REG: p16 = &s->dac_sample_interval_lower; break;
                case DAC_SAMPLE_INTERVAL_UPPER_REG: p16 = &s->dac_sample_interval_upper; break;
                case DAC_SELECT_REG: p16 = &s->dac_select; break;
                default:
                    break;
                }
                if (p16) {
                    val = *p16;
                }
            }
            break;
        }
        return val;
    }

    /* BAR3: dio_counter / DIO area */
    off -= 0x1000;
    if (off < 0x1000) {
        switch (off) {
        case DI_REG:
            /* Inputs are whatever has been written to dio_di_reg (e.g. for loopback) */
            val = s->dio_di_reg & 0x0f;
            break;
        case DO_REG:
            val = s->dio_do_reg & 0x0f;
            break;
        case DIO_DIRECTION_60XX_REG:
            val = s->dio_direction_60xx;
            break;
        case DIO_DATA_60XX_REG:
            val = s->dio_data_60xx;
            break;
        default:
            break;
        }
        return val;
    }

    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    hwaddr off = addr;

    /* BAR0: PLX */
    if (off < 0x1000) {
        switch (off) {
        case PLX_REG_CNTRL:
            s->plx_cntrl = (uint32_t)val;
            break;
        case PLX_REG_INTCSR:
            s->plx_intcsr = (uint32_t)val;
            pcibase_update_irq(s);
            break;
        case PLX_REG_BIGEND:
            s->plx_bigend = (uint32_t)val;
            break;
        case PLX_REG_DMAMODE0:
            s->plx_dmamode0 = (uint32_t)val;
            break;
        case PLX_REG_DMAMODE1:
            s->plx_dmamode1 = (uint32_t)val;
            break;
        case PLX_REG_DMAPADR0:
            s->plx_dmapadr0 = (uint32_t)val;
            break;
        case PLX_REG_DMAPADR1:
            s->plx_dmapadr1 = (uint32_t)val;
            break;
        case PLX_REG_DMALADR0:
            s->plx_dmaladr0 = (uint32_t)val;
            break;
        case PLX_REG_DMALADR1:
            s->plx_dmaladr1 = (uint32_t)val;
            break;
        case PLX_REG_DMADPR0:
            s->plx_dmadpr0 = (uint32_t)val;
            break;
        case PLX_REG_DMADPR1:
            s->plx_dmadpr1 = (uint32_t)val;
            break;
        case PLX_REG_DMASIZ0:
            s->plx_dmasiz0 = (uint32_t)val;
            break;
        case PLX_REG_DMASIZ1:
            s->plx_dmasiz1 = (uint32_t)val;
            break;
        case PLX_REG_DMACSR0: {
            uint8_t b = (uint8_t)val;
            if (b & PLX_DMACSR_CLEARINTR) {
                s->plx_dmacsr0 &= ~PLX_DMACSR_DONE;
            }
            if (b & PLX_DMACSR_ENABLE) {
                s->plx_dmacsr0 |= PLX_DMACSR_ENABLE;
            } else {
                s->plx_dmacsr0 &= ~PLX_DMACSR_ENABLE;
            }
            if (b & PLX_DMACSR_ABORT) {
                s->plx_dmacsr0 &= ~(PLX_DMACSR_ENABLE | PLX_DMACSR_DONE);
            }
            if (b & PLX_DMACSR_START) {
                /* emulate instantaneous DMA completion */
                s->plx_dmacsr0 |= PLX_DMACSR_DONE;
                /* raise DAC related done status if enabled */
                if (s->intr_enable & EN_DAC_DONE_INTR_BIT) {
                    s->hw_status |= DAC_DONE_BIT;
                }
                pcibase_update_irq(s);
            }
            break;
        }
        case PLX_REG_DMACSR1: {
            uint8_t b = (uint8_t)val;
            if (b & PLX_DMACSR_CLEARINTR) {
                s->plx_dmacsr1 &= ~PLX_DMACSR_DONE;
            }
            if (b & PLX_DMACSR_ENABLE) {
                s->plx_dmacsr1 |= PLX_DMACSR_ENABLE;
            } else {
                s->plx_dmacsr1 &= ~PLX_DMACSR_ENABLE;
            }
            if (b & PLX_DMACSR_ABORT) {
                s->plx_dmacsr1 &= ~(PLX_DMACSR_ENABLE | PLX_DMACSR_DONE);
            }
            if (b & PLX_DMACSR_START) {
                /* emulate instantaneous DMA completion */
                s->plx_dmacsr1 |= PLX_DMACSR_DONE;
                if (s->intr_enable & EN_ADC_DONE_INTR_BIT) {
                    s->hw_status |= ADC_DONE_BIT;
                }
                pcibase_update_irq(s);
            }
            break;
        }
        case PLX_REG_L2PDBELL:
            s->plx_l2pdbell = (uint32_t)val;
            break;
        case PLX_REG_LAS0RR:
            s->plx_las0rr = (uint32_t)val;
            break;
        case PLX_REG_LAS0BA:
            s->plx_las0ba = (uint32_t)val;
            break;
        case PLX_REG_LAS1RR:
            s->plx_las1rr = (uint32_t)val;
            break;
        case PLX_REG_LAS1BA:
            s->plx_las1ba = (uint32_t)val;
            break;
        default:
            break;
        }
        return;
    }

    /* BAR2: main_iobase */
    off -= 0x1000;
    if (off < 0x1000) {
        if (size == 2) {
            uint16_t v16 = (uint16_t)val;
            switch (off) {
            case INTR_ENABLE_REG:
                s->intr_enable = v16;
                pcibase_update_irq(s);
                break;
            case HW_CONFIG_REG:
                s->hw_config = v16;
                break;
            case DAQ_SYNC_REG:
                s->daq_sync = v16;
                break;
            case ADC_CONTROL0_REG:
                s->adc_control0 = v16;
                /* start/enable bits may change status */
                if (v16 & ADC_ENABLE_BIT) {
                    s->hw_status |= ADC_ACTIVE_BIT;
                } else {
                    s->hw_status &= ~ADC_ACTIVE_BIT;
                }
                pcibase_update_irq(s);
                break;
            case ADC_CONTROL1_REG:
                s->adc_control1 = v16;
                break;
            case CALIBRATION_REG:
                s->calibration = v16;
                break;
            case ADC_SAMPLE_INTERVAL_LOWER_REG:
                s->adc_sample_interval_lower = v16;
                break;
            case ADC_SAMPLE_INTERVAL_UPPER_REG:
                s->adc_sample_interval_upper = v16;
                break;
            case ADC_DELAY_INTERVAL_LOWER_REG:
                s->adc_delay_interval_lower = v16;
                break;
            case ADC_DELAY_INTERVAL_UPPER_REG:
                s->adc_delay_interval_upper = v16;
                break;
            case ADC_COUNT_LOWER_REG:
                s->adc_count_lower = v16;
                break;
            case ADC_COUNT_UPPER_REG:
                s->adc_count_upper = v16;
                break;
            case ADC_QUEUE_CLEAR_REG:
                s->adc_queue_load = 0;
                s->adc_queue_high = 0;
                break;
            case ADC_QUEUE_LOAD_REG:
                s->adc_queue_load = v16;
                break;
            case ADC_BUFFER_CLEAR_REG:
                s->adc_fifo_rpos = s->adc_fifo_wpos = 0;
                s->adc_read_pntr = 0;
                s->adc_write_pntr = 0;
                break;
            case ADC_QUEUE_HIGH_REG:
                s->adc_queue_high = v16;
                break;
            case ADC_START_REG:
                /* start acquisition: produce one dummy sample and set DONE */
                if (s->adc_fifo_wpos < 1024) {
                    s->adc_fifo[s->adc_fifo_wpos & 0x3ff] = 0x8000;
                    s->adc_fifo_wpos++;
                    s->adc_write_pntr = (uint16_t)(s->adc_fifo_wpos & 0x7fff);
                }
                s->hw_status |= ADC_DONE_BIT;
                /* Mark ADC interrupt pending when done */
                s->hw_status |= ADC_INTR_PENDING_BIT;
                pcibase_update_irq(s);
                break;
            case ADC_CONVERT_REG:
                /* single conversion used by ai_rinsn: push one sample */
                if (s->adc_fifo_wpos < 1024) {
                    s->adc_fifo[s->adc_fifo_wpos & 0x3ff] = 0x1234;
                    s->adc_fifo_wpos++;
                    s->adc_write_pntr = (uint16_t)(s->adc_fifo_wpos & 0x7fff);
                }
                s->hw_status |= ADC_DONE_BIT;
                s->hw_status |= ADC_INTR_PENDING_BIT;
                pcibase_update_irq(s);
                break;
            case DAC_CONTROL0_REG:
                s->dac_control0 = v16;
                if (!(v16 & DAC_ENABLE_BIT)) {
                    s->hw_status &= ~DAC_ACTIVE_BIT;
                }
                pcibase_update_irq(s);
                break;
            case DAC_CONTROL1_REG:
                s->dac_control1 = v16;
                break;
            case DAC_SAMPLE_INTERVAL_LOWER_REG:
                s->dac_sample_interval_lower = v16;
                break;
            case DAC_SAMPLE_INTERVAL_UPPER_REG:
                s->dac_sample_interval_upper = v16;
                break;
            case DAC_SELECT_REG:
                s->dac_select = v16;
                break;
            case DAC_START_REG:
                /* starting AO: mark active and done */
                s->hw_status |= DAC_ACTIVE_BIT;
                s->hw_status |= DAC_DONE_BIT;
                s->hw_status |= DAC_INTR_PENDING_BIT;
                pcibase_update_irq(s);
                break;
            case DAC_BUFFER_CLEAR_REG:
                s->dac_fifo_rpos = s->dac_fifo_wpos = 0;
                break;
            default:
                break;
            }
        } else if (off == ADC_FIFO_REG && size == 2) {
            /* 16-bit write into ADC FIFO not used; ignore */
        } else if (off == DAC_FIFO_REG && size == 2) {
            /* write AO sample to DAC FIFO */
            if (s->dac_fifo_wpos < 1024) {
                s->dac_fifo[s->dac_fifo_wpos & 0x3ff] = (uint16_t)val;
                s->dac_fifo_wpos++;
            } else {
                /* underrun/overflow condition - set status bit */
                s->hw_status |= DAC_UNDERRUN_BIT;
                pcibase_update_irq(s);
            }
        }
        return;
    }

    /* BAR3: DIO area */
    off -= 0x1000;
    if (off < 0x1000) {
        uint8_t v8 = (uint8_t)val;
        switch (off) {
        case DO_REG:
            /* update DO register and mirror it into DI/data for simple loopback */
            s->dio_do_reg = v8 & 0x0f;
            s->dio_data_60xx = (s->dio_data_60xx & ~0x0f) | (s->dio_do_reg & 0x0f);
            s->dio_di_reg = s->dio_do_reg;
            break;
        case DIO_DIRECTION_60XX_REG:
            s->dio_direction_60xx = v8;
            break;
        case DIO_DATA_60XX_REG:
            /* generic data register; writes also update DO/DI lower bits */
            s->dio_data_60xx = v8;
            s->dio_do_reg = v8 & 0x0f;
            s->dio_di_reg = s->dio_do_reg;
            break;
        default:
            break;
        }
        return;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;
    (void)addr;
    (void)size;
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;
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

    s->plx_cntrl = 0;
    s->plx_intcsr = 0;
    s->plx_bigend = 0;
    s->plx_dmamode0 = 0;
    s->plx_dmamode1 = 0;
    s->plx_dmapadr0 = 0;
    s->plx_dmapadr1 = 0;
    s->plx_dmaladr0 = 0;
    s->plx_dmaladr1 = 0;
    s->plx_dmadpr0 = 0;
    s->plx_dmadpr1 = 0;
    s->plx_dmasiz0 = 0;
    s->plx_dmasiz1 = 0;
    s->plx_dmacsr0 = 0;
    s->plx_dmacsr1 = 0;
    s->plx_l2pdbell = 0;
    s->plx_las0rr = 0;
    s->plx_las0ba = 0;
    s->plx_las1rr = 0;
    s->plx_las1ba = 0;

    s->intr_enable = 0;
    s->hw_config = 0;
    s->daq_sync = 0;
    s->adc_control0 = 0;
    s->adc_control1 = 0;
    s->calibration = 0;
    s->adc_sample_interval_lower = 0;
    s->adc_sample_interval_upper = 0;
    s->adc_delay_interval_lower = 0;
    s->adc_delay_interval_upper = 0;
    s->adc_count_lower = 0;
    s->adc_count_upper = 0;
    s->adc_queue_load = 0;
    s->adc_queue_high = 0;
    s->dac_control0 = 0;
    s->dac_control1 = 0;
    s->dac_sample_interval_lower = 0;
    s->dac_sample_interval_upper = 0;
    s->dac_select = 0;

    s->hw_status = 0;
    s->adc_read_pntr = 0;
    s->adc_write_pntr = 0;
    s->prepost = 0;

    s->adc_fifo_wpos = s->adc_fifo_rpos = 0;
    s->dac_fifo_wpos = s->dac_fifo_rpos = 0;

    s->dio_do_reg = 0;
    s->dio_di_reg = 0;
    s->dio_direction_60xx = 0;
    s->dio_data_60xx = 0;

    s->irq_asserted = false;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCIBASE_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCIBASE_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCIBASE_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization: match linux driver usage: BAR0 PLX, BAR2 main, BAR3 dio */
    s->num_bars = 3;

    s->bar_info[0].index = 0;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    s->bar_info[0].size  = 0x1000;
    s->bar_info[0].name  = "cb_pcidas64-plx";

    s->bar_info[1].index = 1;
    s->bar_info[1].type  = BAR_TYPE_NONE;
    s->bar_info[1].size  = 0;
    s->bar_info[1].name  = "unused";

    s->bar_info[2].index = 2;
    s->bar_info[2].type  = BAR_TYPE_MMIO;
    s->bar_info[2].size  = 0x1000;
    s->bar_info[2].name  = "cb_pcidas64-main";

    s->bar_info[3].index = 3;
    s->bar_info[3].type  = BAR_TYPE_MMIO;
    s->bar_info[3].size  = 0x1000;
    s->bar_info[3].name  = "cb_pcidas64-dio";

    s->bar_info[4].index = 4;
    s->bar_info[4].type  = BAR_TYPE_NONE;
    s->bar_info[4].size  = 0;
    s->bar_info[4].name  = "unused";

    s->bar_info[5].index = 5;
    s->bar_info[5].type  = BAR_TYPE_NONE;
    s->bar_info[5].size  = 0;
    s->bar_info[5].name  = "unused";

    for (int i = 0; i < 6; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = false;
    s->has_msix = false;

    /* Initialize register defaults approximating reset */
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
    (void)s;
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "cb_pcidas64_pci",
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
    k->vendor_id = PCIBASE_VENDOR_ID;
    k->device_id = PCIBASE_DEVICE_ID;
    k->revision = 0x01;
    k->class_id = PCIBASE_CLASS_ID;
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
