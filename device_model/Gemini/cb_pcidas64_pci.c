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
#include "qemu/bitops.h"

#ifndef GENMASK
#define GENMASK(h, l) (((1ULL << ((h) - (l) + 1)) - 1) << (l))
#endif

#define TYPE_PCIBASE_DEVICE "cb_pcidas64_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x1307
#define DEVICE_ID 0x001d
#define CLASS_ID PCI_CLASS_OTHERS

#define DMA_BUFFER_SIZE     0x1000
#define TIMER_BASE 25
#define PRESCALED_TIMER_BASE    10000
#define DAC_FIFO_SIZE       0x2000
#define MAX_AI_DMA_RING_COUNT (0x80000 / DMA_BUFFER_SIZE)
#define MIN_AI_DMA_RING_COUNT (0x10000 / DMA_BUFFER_SIZE)
#define AO_DMA_RING_COUNT (0x10000 / DMA_BUFFER_SIZE)
#define PLX_CNTRL_EECS      BIT(25)
#define PLX_CNTRL_EEWB      BIT(26)
#define PLX_REG_CNTRL       0x006c
#define PLX_CNTRL_USERO     BIT(16)
#define PLX_CNTRL_EESK      BIT(24)
#define PLX_CNTRL_EERB      BIT(27)
#define PLX_REG_INTCSR      0x0068
#define PLX_DMAMODE_LACONST BIT(11)
#define PLX_INTCSR_LSEPARITYEN  BIT(1)
#define PLX_DMAMODE_BTERMIEN    BIT(7)
#define PLX_DMAMODE_WIDTH_32    (BIT(0) * 2)
#define PLX_REG_BIGEND      0x000c
#define PLX_INTCSR_PABORTIEN    BIT(10)
#define PLX_INTCSR_DMA1IEN  BIT(19)
#define PLX_DMAMODE_WIDTH_16    (BIT(0) * 1)
#define PLX_DMAMODE_DEMAND  BIT(12)
#define PLX_INTCSR_LIOEN    BIT(16)
#define PLX_INTCSR_LSEABORTEN   BIT(0)
#define PLX_DMAMODE_BURSTEN BIT(8)
#define PLX_INTCSR_PLIEN    BIT(11)
#define PLX_DMAMODE_CHAINEN BIT(9)
#define PLX_REG_DMAMODE1    0x0094
#define PLX_BIGEND_DMA1     BIT(6)
#define PLX_DMAMODE_READYIEN    BIT(6)
#define PLX_INTCSR_PIEN     BIT(8)
#define PLX_DMAMODE_INTRPCI BIT(17)
#define PLX_INTCSR_DMA0IEN  BIT(18)
#define PLX_BIGEND_DMA0     BIT(7)
#define PLX_DMAMODE_DONEIEN BIT(10)
#define PLX_REG_DMAMODE0    0x0080
#define PLX_DMADPR_DESCPCI  BIT(0)
#define PLX_DMADPR_XFERL2P  BIT(3)
#define PLX_DMADPR_TCINTR   BIT(2)
#define PLX_DMACSR_CLEARINTR    BIT(3)
#define PLX_DMACSR_ENABLE   BIT(0)
#define PLX_DMACSR_START    BIT(1)
#define PLX_REG_DMACSR(n)   ((n) ? PLX_REG_DMACSR1 : PLX_REG_DMACSR0)
#define PLX_REG_DMASIZ1     0x00a0
#define PLX_REG_DMADPR0     0x0090
#define PLX_REG_DMAPADR0    0x0084
#define PLX_REG_DMALADR0    0x0088
#define PLX_REG_DMADPR1     0x00a4
#define PLX_REG_DMASIZ0     0x008c
#define PLX_REG_DMAPADR1    0x0098
#define PLX_REG_DMALADR1    0x009c
#define PLX_REG_DMAPADR(n)  ((n) ? PLX_REG_DMAPADR1 : PLX_REG_DMAPADR0)
#define PLX_INTCSR_DMA1IA   BIT(22)
#define PLX_REG_DMACSR1     0x00a9
#define PLX_DMACSR_DONE     BIT(4)
#define PLX_REG_DMACSR0     0x00a8
#define PLX_DMADPR_CHAINEND BIT(1)
#define PLX_INTCSR_DMA0IA   BIT(21)
#define PLX_INTCSR_LDBIA    BIT(20)
#define PLX_REG_L2PDBELL    0x0064
#define PLX_CNTRL_EEPRESENT BIT(28)
#define PLX_REG_LAS1BA      0x00f4
#define PLX_LASRR_MEM_MASK  GENMASK(31, 4)
#define PLX_REG_LAS0BA      0x0004
#define PLX_REG_LAS0RR      0x0000
#define PLX_LASBA_MEM_MASK  GENMASK(31, 4)
#define PLX_REG_LAS1RR      0x00f0
#define PLX_DMACSR_ABORT    BIT(2)

enum write_only_registers {
    INTR_ENABLE_REG = 0x0,
    HW_CONFIG_REG = 0x2,
    DAQ_SYNC_REG = 0xc,
    DAQ_ATRIG_LOW_4020_REG = 0xc,
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
    ADC_WRITE_PNTR_REG = 0xc,
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

struct plx_dma_desc {
    uint32_t pci_start_addr;
    uint32_t local_start_addr;
    uint32_t transfer_size;
    uint32_t next;
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

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;
    uint32_t intr_enable_bits;
    uint32_t plx_intcsr_bits;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t plx_regs[0x100 / 4];
    uint16_t main_regs[0x400 / 2];
    uint16_t dio_regs[0x100 / 2];
    uint16_t adc_control1_bits;
    uint16_t fifo_size_bits;
    uint16_t hw_config_bits;
    uint16_t dac_control1_bits;
    uint32_t plx_control_bits;

    /* DMA Context */
    uint32_t local0_iobase;
    uint32_t local1_iobase;
    struct plx_dma_desc ai_dma_desc;
    struct plx_dma_desc ao_dma_desc;
    unsigned int ai_dma_index;
    unsigned int ao_dma_index;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool irq_active = false;

    uint32_t intcsr = s->plx_regs[PLX_REG_INTCSR / 4];
    uint16_t hw_status = s->main_regs[HW_STATUS_REG / 2];
    uint16_t intr_enable = s->intr_enable_bits;

    if ((intcsr & PLX_INTCSR_DMA0IEN) && (intcsr & PLX_INTCSR_DMA0IA)) {
        irq_active = true;
    }
    if ((intcsr & PLX_INTCSR_DMA1IEN) && (intcsr & PLX_INTCSR_DMA1IA)) {
        irq_active = true;
    }
    if ((intcsr & PLX_INTCSR_LIOEN) && (intcsr & PLX_INTCSR_LDBIA)) {
        irq_active = true;
    }

    if ((intr_enable & EN_ADC_DONE_INTR_BIT) && (hw_status & ADC_DONE_BIT)) {
        irq_active = true;
    }
    if ((intr_enable & EN_DAC_DONE_INTR_BIT) && (hw_status & DAC_DONE_BIT)) {
        irq_active = true;
    }

    pci_set_irq(pdev, irq_active ? 1 : 0);
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static uint64_t pcibase_plx_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t reg = addr / 4;
    if (addr < 0x100) {
        return s->plx_regs[reg];
    }
    return 0;
}

static void pcibase_plx_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t reg = addr / 4;
    uint32_t shift = (addr % 4) * 8;
    uint32_t mask = (size == 1) ? 0xff : (size == 2) ? 0xffff : 0xffffffff;

    if (addr < 0x100) {
        if (reg == PLX_REG_L2PDBELL / 4) {
            s->plx_regs[reg] &= ~(val << shift);
            if (s->plx_regs[reg] == 0) {
                s->plx_regs[PLX_REG_INTCSR / 4] &= ~PLX_INTCSR_LDBIA;
                pcibase_update_irq(s);
            }
        } else {
            s->plx_regs[reg] = (s->plx_regs[reg] & ~(mask << shift)) | ((val & mask) << shift);

            if (reg == PLX_REG_INTCSR / 4) {
                pcibase_update_irq(s);
            } else if (reg == 0xa8 / 4) {
                uint8_t csr0 = s->plx_regs[reg] & 0xff;
                uint8_t csr1 = (s->plx_regs[reg] >> 8) & 0xff;

                if (csr0 & PLX_DMACSR_CLEARINTR) {
                    csr0 &= ~PLX_DMACSR_CLEARINTR;
                    s->plx_regs[PLX_REG_INTCSR / 4] &= ~PLX_INTCSR_DMA0IA;
                    pcibase_update_irq(s);
                }
                if (csr0 & PLX_DMACSR_ABORT) {
                    csr0 &= ~PLX_DMACSR_ABORT;
                    csr0 |= PLX_DMACSR_DONE;
                }
                if (csr0 & PLX_DMACSR_START) {
                    csr0 &= ~PLX_DMACSR_START;
                    csr0 |= PLX_DMACSR_DONE;
                    s->plx_regs[PLX_REG_INTCSR / 4] |= PLX_INTCSR_DMA0IA;
                    pcibase_update_irq(s);
                }

                if (csr1 & PLX_DMACSR_CLEARINTR) {
                    csr1 &= ~PLX_DMACSR_CLEARINTR;
                    s->plx_regs[PLX_REG_INTCSR / 4] &= ~PLX_INTCSR_DMA1IA;
                    pcibase_update_irq(s);
                }
                if (csr1 & PLX_DMACSR_ABORT) {
                    csr1 &= ~PLX_DMACSR_ABORT;
                    csr1 |= PLX_DMACSR_DONE;
                }
                if (csr1 & PLX_DMACSR_START) {
                    csr1 &= ~PLX_DMACSR_START;
                    csr1 |= PLX_DMACSR_DONE;
                    s->plx_regs[PLX_REG_INTCSR / 4] |= PLX_INTCSR_DMA1IA;
                    pcibase_update_irq(s);
                }

                s->plx_regs[reg] = (s->plx_regs[reg] & 0xffff0000) | (csr1 << 8) | csr0;
            }
        }
    }
}

static uint64_t pcibase_main_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr < 0x400) {
        if (size == 4) {
            uint32_t val = s->main_regs[addr / 2] | (s->main_regs[(addr / 2) + 1] << 16);
            if (addr == ADC_FIFO_REG) {
                return 0;
            }
            return val;
        } else {
            if (addr == HW_STATUS_REG) {
                return s->main_regs[addr / 2] | ADC_DONE_BIT | DAC_DONE_BIT | (3 << 10);
            }
            if (addr == ADC_WRITE_PNTR_REG) {
                return 1;
            }
            return s->main_regs[addr / 2];
        }
    }
    return 0;
}

static void pcibase_main_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr < 0x400) {
        if (size == 4) {
            s->main_regs[addr / 2] = val & 0xffff;
            s->main_regs[(addr / 2) + 1] = (val >> 16) & 0xffff;
        } else {
            s->main_regs[addr / 2] = val;
        }

        if (addr == INTR_ENABLE_REG) {
            s->intr_enable_bits = val;
            pcibase_update_irq(s);
        } else if (addr == ADC_START_REG || addr == ADC_CONVERT_REG) {
            s->main_regs[HW_STATUS_REG / 2] |= ADC_DONE_BIT | ADC_INTR_PENDING_BIT;
            pcibase_update_irq(s);
        } else if (addr == DAC_START_REG) {
            s->main_regs[HW_STATUS_REG / 2] |= DAC_DONE_BIT | DAC_INTR_PENDING_BIT;
            pcibase_update_irq(s);
        } else if (addr == ADC_BUFFER_CLEAR_REG) {
            s->main_regs[HW_STATUS_REG / 2] &= ~(ADC_DONE_BIT | ADC_INTR_PENDING_BIT);
            pcibase_update_irq(s);
        } else if (addr == DAC_BUFFER_CLEAR_REG) {
            s->main_regs[HW_STATUS_REG / 2] &= ~(DAC_DONE_BIT | DAC_INTR_PENDING_BIT);
            pcibase_update_irq(s);
        }
    }
}

static uint64_t pcibase_dio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr < 0x100) {
        uint8_t *regs = (uint8_t *)s->dio_regs;
        return regs[addr];
    }
    return 0;
}

static void pcibase_dio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr < 0x100) {
        uint8_t *regs = (uint8_t *)s->dio_regs;
        regs[addr] = val;
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

static const MemoryRegionOps pcibase_plx_ops = {
    .read = pcibase_plx_read,
    .write = pcibase_plx_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static const MemoryRegionOps pcibase_main_ops = {
    .read = pcibase_main_read,
    .write = pcibase_main_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static const MemoryRegionOps pcibase_dio_ops = {
    .read = pcibase_dio_read,
    .write = pcibase_dio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    memset(s->plx_regs, 0, sizeof(s->plx_regs));
    memset(s->main_regs, 0, sizeof(s->main_regs));
    memset(s->dio_regs, 0, sizeof(s->dio_regs));

    s->plx_regs[PLX_REG_LAS0RR / 4] = 0xfffff000;
    s->plx_regs[PLX_REG_LAS0BA / 4] = 0x00000001;
    s->plx_regs[PLX_REG_LAS1RR / 4] = 0xfffff000;
    s->plx_regs[PLX_REG_LAS1BA / 4] = 0x00000001;

    s->plx_regs[PLX_REG_CNTRL / 4] = PLX_CNTRL_EEPRESENT;

    s->main_regs[HW_STATUS_REG / 2] = ADC_DONE_BIT | DAC_DONE_BIT;
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
        const MemoryRegionOps *ops = &pcibase_mmio_ops;
        if (bi->index == 0) ops = &pcibase_plx_ops;
        else if (bi->index == 2) ops = &pcibase_main_ops;
        else if (bi->index == 3) ops = &pcibase_dio_ops;
        memory_region_init_io(mr, OBJECT(s), ops, s, bi->name, aligned_size);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 4;
    s->bar_info[0] = (BARInfo){0, BAR_TYPE_MMIO, 0x100, "plx9080"};
    s->bar_info[1] = (BARInfo){1, BAR_TYPE_NONE, 0, ""};
    s->bar_info[2] = (BARInfo){2, BAR_TYPE_MMIO, 0x400, "main"};
    s->bar_info[3] = (BARInfo){3, BAR_TYPE_MMIO, 0x100, "dio"};
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
