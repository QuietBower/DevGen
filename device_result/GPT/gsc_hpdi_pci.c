/*
 * QEMU PCI device model for gsc_hpdi (Functional Implementation)
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

#define TYPE_PCIBASE_DEVICE "gsc_hpdi_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define GSC_HPDI_VENDOR_ID 0x10b5
#define GSC_HPDI_DEVICE_ID 0x9080
#define GSC_HPDI_CLASS_ID  PCI_CLASS_OTHERS

#define DMA_BUFFER_SIZE                             0x10000
#define FIRMWARE_REV_REG                            0x00
#define FEATURES_REG_PRESENT_BIT                    (1U << 15)
#define BOARD_CONTROL_REG                           0x04
#define BOARD_RESET_BIT                             (1U << 0)
#define TX_FIFO_RESET_BIT                           (1U << 1)
#define RX_FIFO_RESET_BIT                           (1U << 2)
#define TX_ENABLE_BIT                               (1U << 4)
#define RX_ENABLE_BIT                               (1U << 5)
#define DEMAND_DMA_DIRECTION_TX_BIT                 (1U << 6)
#define LINE_VALID_ON_STATUS_VALID_BIT              (1U << 7)
#define START_TX_BIT                                (1U << 8)
#define CABLE_THROTTLE_ENABLE_BIT                   (1U << 9)
#define TEST_MODE_ENABLE_BIT                        (1U << 31)
#define BOARD_STATUS_REG                            0x08
#define COMMAND_LINE_STATUS_MASK                    (0x7f << 0)
#define TX_IN_PROGRESS_BIT                          (1U << 7)
#define TX_NOT_EMPTY_BIT                            (1U << 8)
#define TX_NOT_ALMOST_EMPTY_BIT                     (1U << 9)
#define TX_NOT_ALMOST_FULL_BIT                      (1U << 10)
#define TX_NOT_FULL_BIT                             (1U << 11)
#define RX_NOT_EMPTY_BIT                            (1U << 12)
#define RX_NOT_ALMOST_EMPTY_BIT                     (1U << 13)
#define RX_NOT_ALMOST_FULL_BIT                      (1U << 14)
#define RX_NOT_FULL_BIT                             (1U << 15)
#define BOARD_JUMPER0_INSTALLED_BIT                 (1U << 16)
#define BOARD_JUMPER1_INSTALLED_BIT                 (1U << 17)
#define TX_OVERRUN_BIT                              (1U << 21)
#define RX_UNDERRUN_BIT                             (1U << 22)
#define RX_OVERRUN_BIT                              (1U << 23)
#define TX_PROG_ALMOST_REG                          0x0c
#define RX_PROG_ALMOST_REG                          0x10
#define ALMOST_EMPTY_BITS(x)                        (((x) & 0xffff) << 0)
#define ALMOST_FULL_BITS(x)                         (((x) & 0xff) << 16)
#define FEATURES_REG                                0x14
#define FIFO_SIZE_PRESENT_BIT                       (1U << 0)
#define FIFO_WORDS_PRESENT_BIT                      (1U << 1)
#define LEVEL_EDGE_INTERRUPTS_PRESENT_BIT           (1U << 2)
#define GPIO_SUPPORTED_BIT                          (1U << 3)
#define PLX_DMA_CH1_SUPPORTED_BIT                   (1U << 4)
#define OVERRUN_UNDERRUN_SUPPORTED_BIT              (1U << 5)
#define FIFO_REG                                    0x18
#define TX_STATUS_COUNT_REG                         0x1c
#define TX_LINE_VALID_COUNT_REG                     0x20
#define TX_LINE_INVALID_COUNT_REG                   0x24
#define RX_STATUS_COUNT_REG                         0x28
#define RX_LINE_COUNT_REG                           0x2c
#define INTERRUPT_CONTROL_REG                       0x30
#define FRAME_VALID_START_INTR                      (1U << 0)
#define FRAME_VALID_END_INTR                        (1U << 1)
#define TX_FIFO_EMPTY_INTR                          (1U << 8)
#define TX_FIFO_ALMOST_EMPTY_INTR                   (1U << 9)
#define TX_FIFO_ALMOST_FULL_INTR                    (1U << 10)
#define TX_FIFO_FULL_INTR                           (1U << 11)
#define RX_EMPTY_INTR                               (1U << 12)
#define RX_ALMOST_EMPTY_INTR                        (1U << 13)
#define RX_ALMOST_FULL_INTR                         (1U << 14)
#define RX_FULL_INTR                                (1U << 15)
#define INTERRUPT_STATUS_REG                        0x34
#define TX_CLOCK_DIVIDER_REG                        0x38
#define TX_FIFO_SIZE_REG                            0x40
#define RX_FIFO_SIZE_REG                            0x44
#define FIFO_SIZE_MASK                              (0xfffff << 0)
#define TX_FIFO_WORDS_REG                           0x48
#define RX_FIFO_WORDS_REG                           0x4c
#define INTERRUPT_EDGE_LEVEL_REG                    0x50
#define INTERRUPT_POLARITY_REG                      0x54
#define TIMER_BASE                                  50
#define NUM_DMA_BUFFERS                             4
#define NUM_DMA_DESCRIPTORS                         256
#define PLX_REG_DMAPADR(n)                          ((n) ? PLX_REG_DMAPADR1 : PLX_REG_DMAPADR0)
#define PLX_DMACSR_ENABLE                           (1U << 0)
#define PLX_REG_DMACSR0                             0x00a8
#define PLX_REG_DMACSR1                             0x00a9
#define PLX_REG_INTCSR                              0x0068
#define PLX_INTCSR_DMA1IA                           (1U << 22)
#define PLX_DMACSR_CLEARINTR                        (1U << 3)
#define PLX_INTCSR_LDBIA                            (1U << 20)
#define PLX_INTCSR_PLIA                             (1U << 15)
#define PLX_INTCSR_DMA0IA                           (1U << 21)
#define PLX_REG_L2PDBELL                            0x0064
#define PLX_DMADPR_XFERL2P                          (1U << 3)
#define PLX_REG_DMADPR0                             0x0090
#define PLX_DMADPR_DESCPCI                          (1U << 0)
#define PLX_REG_DMAPADR0                            0x0084
#define PLX_REG_DMASIZ0                             0x008c
#define PLX_REG_DMALADR0                            0x0088
#define PLX_DMACSR_START                            (1U << 1)
#define PLX_DMADPR_TCINTR                           (1U << 2)
#define PLX_INTCSR_LSEPARITYEN                      (1U << 1)
#define PLX_INTCSR_DMA0IEN                          (1U << 18)
#define PLX_INTCSR_PIEN                             (1U << 8)
#define PLX_INTCSR_LIOEN                            (1U << 16)
#define PLX_INTCSR_LSEABORTEN                       (1U << 0)
#define PLX_INTCSR_PABORTIEN                        (1U << 10)
#define PLX_INTCSR_PLIEN                            (1U << 11)
#define PLX_DMAMODE_LACONST                         (1U << 11)
#define PLX_BIGEND_DMA1                             (1U << 6)
#define PLX_DMAMODE_INTRPCI                         (1U << 17)
#define PLX_DMAMODE_WIDTH_32                        ((1U << 0) * 2)
#define PLX_DMAMODE_BURSTEN                         (1U << 8)
#define PLX_REG_DMAMODE0                            0x0080
#define PLX_BIGEND_DMA0                             (1U << 7)
#define PLX_DMAMODE_CHAINEN                         (1U << 9)
#define PLX_DMAMODE_DEMAND                          (1U << 12)
#define PLX_REG_BIGEND                              0x000c
#define PLX_DMAMODE_DONEIEN                         (1U << 10)
#define PLX_DMAMODE_READYIEN                        (1U << 6)
#define PLX_REG_DMAPADR1                            0x0098
#define PLX_REG_DMACSR(n)                           ((n) ? PLX_REG_DMACSR1 : PLX_REG_DMACSR0)
#define PLX_DMACSR_DONE                             (1U << 4)
#define PLX_DMACSR_ABORT                            (1U << 2)

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

    /* HPDI register shadows (BAR2) */
    uint32_t hpdi_board_control;
    uint32_t hpdi_board_status;
    uint32_t hpdi_tx_prog_almost;
    uint32_t hpdi_rx_prog_almost;
    uint32_t hpdi_features;
    uint32_t hpdi_interrupt_control;
    uint32_t hpdi_interrupt_status;
    uint32_t hpdi_tx_clock_divider;
    uint32_t hpdi_tx_fifo_size;
    uint32_t hpdi_rx_fifo_size;
    uint32_t hpdi_tx_fifo_words;
    uint32_t hpdi_rx_fifo_words;
    uint32_t hpdi_int_edge_level;
    uint32_t hpdi_int_polarity;

    /* PLX 9080 register shadows (BAR0) */
    uint32_t plx_intcsr;
    uint32_t plx_l2pdbell;
    uint32_t plx_dmadpr0;
    uint32_t plx_dmamode0;
    uint32_t plx_dmasiz0;
    uint32_t plx_dmapadr0;
    uint32_t plx_dmaladr0;
    uint32_t plx_dmapadr1;
    uint8_t  plx_dmacsr0;
    uint8_t  plx_dmacsr1;
    uint32_t plx_bigend;

    /* simple DMA emulation state for channel 0 */
    dma_addr_t dma_desc_head0;
    bool dma0_active;
    bool dma0_irq_pending;
};

static bool pcibase_msi_enabled(PCIBaseState *s)
{
    return msi_enabled(PCI_DEVICE(s));
}

static void pcibase_set_irq(PCIBaseState *s, bool level)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    if (pcibase_msi_enabled(s)) {
        if (level) {
            msi_notify(pdev, 0);
        }
    } else {
        pci_set_irq(pdev, level ? 1 : 0);
    }
}

static void pcibase_update_irq(PCIBaseState *s)
{
    uint32_t hpdi_istat = s->hpdi_interrupt_status;
    uint32_t hpdi_ien   = s->hpdi_interrupt_control;
    uint32_t plx_intcsr = s->plx_intcsr;
    bool assert = false;

    if (hpdi_istat && (hpdi_ien & hpdi_istat)) {
        assert = true;
    }

    if ((plx_intcsr & PLX_INTCSR_DMA0IEN) && (plx_intcsr & PLX_INTCSR_DMA0IA)) {
        assert = true;
    }

    if ((plx_intcsr & PLX_INTCSR_DMA1IA)) {
        assert = true;
    }

    if ((plx_intcsr & (PLX_INTCSR_PIEN | PLX_INTCSR_PLIEN | PLX_INTCSR_LIOEN)) &&
        (plx_intcsr & (PLX_INTCSR_PLIA | PLX_INTCSR_LDBIA))) {
        assert = true;
    }

    pcibase_set_irq(s, assert);
}

static uint64_t pcibase_mmio_read_plx(PCIBaseState *s, hwaddr addr, unsigned size)
{
    uint64_t val = 0;

    switch (addr) {
    case PLX_REG_INTCSR:
        val = s->plx_intcsr;
        break;
    case PLX_REG_L2PDBELL:
        val = s->plx_l2pdbell;
        break;
    case PLX_REG_DMADPR0:
        val = s->plx_dmadpr0;
        break;
    case PLX_REG_DMAMODE0:
        val = s->plx_dmamode0;
        break;
    case PLX_REG_DMASIZ0:
        val = s->plx_dmasiz0;
        break;
    case PLX_REG_DMAPADR0:
        val = s->plx_dmapadr0;
        break;
    case PLX_REG_DMALADR0:
        val = s->plx_dmaladr0;
        break;
    case PLX_REG_DMAPADR1:
        val = s->plx_dmapadr1;
        break;
    case PLX_REG_DMACSR0:
        val = s->plx_dmacsr0;
        break;
    case PLX_REG_DMACSR1:
        val = s->plx_dmacsr1;
        break;
    case PLX_REG_BIGEND:
        val = s->plx_bigend;
        break;
    default:
        val = 0;
        break;
    }

    if (size == 1) {
        return (uint8_t)val;
    } else if (size == 2) {
        return (uint16_t)val;
    } else {
        return (uint32_t)val;
    }
}

static uint64_t pcibase_mmio_read_hpdi(PCIBaseState *s, hwaddr addr, unsigned size)
{
    uint64_t val = 0;

    switch (addr) {
    case FIRMWARE_REV_REG:
        val = 0x00010000 | FEATURES_REG_PRESENT_BIT;
        break;
    case BOARD_CONTROL_REG:
        val = s->hpdi_board_control;
        break;
    case BOARD_STATUS_REG:
        val = s->hpdi_board_status;
        break;
    case TX_PROG_ALMOST_REG:
        val = s->hpdi_tx_prog_almost;
        break;
    case RX_PROG_ALMOST_REG:
        val = s->hpdi_rx_prog_almost;
        break;
    case FEATURES_REG:
        val = s->hpdi_features;
        break;
    case FIFO_REG:
        val = 0;
        break;
    case TX_STATUS_COUNT_REG:
        val = 0;
        break;
    case TX_LINE_VALID_COUNT_REG:
        val = 0;
        break;
    case TX_LINE_INVALID_COUNT_REG:
        val = 0;
        break;
    case RX_STATUS_COUNT_REG:
        val = 0;
        break;
    case RX_LINE_COUNT_REG:
        val = 0;
        break;
    case INTERRUPT_CONTROL_REG:
        val = s->hpdi_interrupt_control;
        break;
    case INTERRUPT_STATUS_REG:
        val = s->hpdi_interrupt_status;
        break;
    case TX_CLOCK_DIVIDER_REG:
        val = s->hpdi_tx_clock_divider;
        break;
    case TX_FIFO_SIZE_REG:
        val = s->hpdi_tx_fifo_size;
        break;
    case RX_FIFO_SIZE_REG:
        val = s->hpdi_rx_fifo_size;
        break;
    case TX_FIFO_WORDS_REG:
        val = s->hpdi_tx_fifo_words;
        break;
    case RX_FIFO_WORDS_REG:
        val = s->hpdi_rx_fifo_words;
        break;
    case INTERRUPT_EDGE_LEVEL_REG:
        val = s->hpdi_int_edge_level;
        break;
    case INTERRUPT_POLARITY_REG:
        val = s->hpdi_int_polarity;
        break;
    default:
        val = 0;
        break;
    }

    if (size == 1) {
        return (uint8_t)val;
    } else if (size == 2) {
        return (uint16_t)val;
    } else {
        return (uint32_t)val;
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < 0x1000) {
        return pcibase_mmio_read_plx(s, addr, size);
    } else {
        hwaddr local = addr - 0x1000;
        return pcibase_mmio_read_hpdi(s, local, size);
    }
}

static void pcibase_mmio_write_plx(PCIBaseState *s, hwaddr addr, uint64_t val, unsigned size)
{
    uint32_t v32 = (uint32_t)val;
    uint8_t v8 = (uint8_t)val;

    switch (addr) {
    case PLX_REG_INTCSR:
        s->plx_intcsr = v32;
        break;
    case PLX_REG_L2PDBELL:
        s->plx_l2pdbell &= ~v32;
        s->plx_intcsr &= ~PLX_INTCSR_LDBIA;
        break;
    case PLX_REG_DMADPR0:
        s->plx_dmadpr0 = v32;
        s->dma_desc_head0 = (dma_addr_t)(v32 & ~0xF);
        break;
    case PLX_REG_DMAMODE0:
        s->plx_dmamode0 = v32;
        break;
    case PLX_REG_DMASIZ0:
        s->plx_dmasiz0 = v32;
        break;
    case PLX_REG_DMAPADR0:
        s->plx_dmapadr0 = v32;
        break;
    case PLX_REG_DMALADR0:
        s->plx_dmaladr0 = v32;
        break;
    case PLX_REG_DMAPADR1:
        s->plx_dmapadr1 = v32;
        break;
    case PLX_REG_DMACSR0:
        {
            uint8_t old = s->plx_dmacsr0;
            s->plx_dmacsr0 = (v8 & (PLX_DMACSR_ENABLE | PLX_DMACSR_ABORT | PLX_DMACSR_CLEARINTR));

            if (v8 & PLX_DMACSR_CLEARINTR) {
                s->plx_intcsr &= ~PLX_INTCSR_DMA0IA;
                s->plx_dmacsr0 &= ~(PLX_DMACSR_DONE);
            }

            if ((v8 & PLX_DMACSR_ABORT) && (old & PLX_DMACSR_ENABLE)) {
                s->plx_dmacsr0 &= ~PLX_DMACSR_ENABLE;
                s->plx_dmacsr0 |= PLX_DMACSR_DONE;
                s->plx_intcsr |= PLX_INTCSR_DMA0IA;
            }

            if ((v8 & PLX_DMACSR_ENABLE) && (v8 & PLX_DMACSR_START)) {
                s->plx_dmacsr0 |= PLX_DMACSR_ENABLE;
                s->plx_dmacsr0 |= PLX_DMACSR_DONE;
                s->plx_intcsr |= PLX_INTCSR_DMA0IA;
                s->dma0_active = true;
            }
        }
        break;
    case PLX_REG_DMACSR1:
        if (v8 & PLX_DMACSR_CLEARINTR) {
            s->plx_intcsr &= ~PLX_INTCSR_DMA1IA;
            s->plx_dmacsr1 &= ~(PLX_DMACSR_DONE);
        }
        if (v8 & PLX_DMACSR_ABORT) {
            s->plx_dmacsr1 &= ~PLX_DMACSR_ENABLE;
            s->plx_dmacsr1 |= PLX_DMACSR_DONE;
            s->plx_intcsr |= PLX_INTCSR_DMA1IA;
        }
        if (v8 & PLX_DMACSR_ENABLE) {
            s->plx_dmacsr1 |= PLX_DMACSR_ENABLE;
        }
        break;
    case PLX_REG_BIGEND:
        s->plx_bigend = v32;
        break;
    default:
        break;
    }

    pcibase_update_irq(s);
}

static void pcibase_mmio_write_hpdi(PCIBaseState *s, hwaddr addr, uint64_t val, unsigned size)
{
    uint32_t v32 = (uint32_t)val;

    switch (addr) {
    case BOARD_CONTROL_REG:
        s->hpdi_board_control = v32;
        if (v32 & BOARD_RESET_BIT) {
            s->hpdi_board_status &= ~(RX_OVERRUN_BIT | RX_UNDERRUN_BIT | TX_OVERRUN_BIT);
        }
        break;
    case TX_PROG_ALMOST_REG:
        s->hpdi_tx_prog_almost = v32;
        break;
    case RX_PROG_ALMOST_REG:
        s->hpdi_rx_prog_almost = v32;
        break;
    case INTERRUPT_CONTROL_REG:
        s->hpdi_interrupt_control = v32;
        break;
    case INTERRUPT_STATUS_REG:
        s->hpdi_interrupt_status &= ~v32;
        break;
    case BOARD_STATUS_REG:
        s->hpdi_board_status &= ~v32;
        break;
    case TX_CLOCK_DIVIDER_REG:
        s->hpdi_tx_clock_divider = v32;
        break;
    case TX_FIFO_SIZE_REG:
        s->hpdi_tx_fifo_size = v32 & FIFO_SIZE_MASK;
        break;
    case RX_FIFO_SIZE_REG:
        s->hpdi_rx_fifo_size = v32 & FIFO_SIZE_MASK;
        break;
    case TX_FIFO_WORDS_REG:
        s->hpdi_tx_fifo_words = v32;
        break;
    case RX_FIFO_WORDS_REG:
        s->hpdi_rx_fifo_words = v32;
        break;
    case INTERRUPT_EDGE_LEVEL_REG:
        s->hpdi_int_edge_level = v32;
        break;
    case INTERRUPT_POLARITY_REG:
        s->hpdi_int_polarity = v32;
        break;
    default:
        break;
    }

    pcibase_update_irq(s);
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < 0x1000) {
        pcibase_mmio_write_plx(s, addr, val, size);
    } else {
        hwaddr local = addr - 0x1000;
        pcibase_mmio_write_hpdi(s, local, val, size);
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

    s->hpdi_board_control = 0;
    s->hpdi_board_status = 0;
    s->hpdi_tx_prog_almost = 0;
    s->hpdi_rx_prog_almost = 0;
    s->hpdi_features = FIFO_SIZE_PRESENT_BIT | FIFO_WORDS_PRESENT_BIT |
                       LEVEL_EDGE_INTERRUPTS_PRESENT_BIT |
                       GPIO_SUPPORTED_BIT | PLX_DMA_CH1_SUPPORTED_BIT |
                       OVERRUN_UNDERRUN_SUPPORTED_BIT;
    s->hpdi_interrupt_control = 0;
    s->hpdi_interrupt_status = 0;
    s->hpdi_tx_clock_divider = 0;
    s->hpdi_tx_fifo_size = FIFO_SIZE_MASK & 0x1000;
    s->hpdi_rx_fifo_size = FIFO_SIZE_MASK & 0x1000;
    s->hpdi_tx_fifo_words = 0;
    s->hpdi_rx_fifo_words = 0;
    s->hpdi_int_edge_level = 0;
    s->hpdi_int_polarity = 0;

    s->plx_intcsr = 0;
    s->plx_l2pdbell = 0;
    s->plx_dmadpr0 = 0;
    s->plx_dmamode0 = 0;
    s->plx_dmasiz0 = 0;
    s->plx_dmapadr0 = 0;
    s->plx_dmaladr0 = 0;
    s->plx_dmapadr1 = 0;
    s->plx_dmacsr0 = 0;
    s->plx_dmacsr1 = 0;
    s->plx_bigend = 0;

    s->dma_desc_head0 = 0;
    s->dma0_active = false;
    s->dma0_irq_pending = false;

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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  GSC_HPDI_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  GSC_HPDI_DEVICE_ID);

    pci_set_word(pci_conf + PCI_SUBSYSTEM_VENDOR_ID, GSC_HPDI_VENDOR_ID);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_ID, 0x2400);

    pci_set_word(pci_conf + PCI_CLASS_DEVICE, GSC_HPDI_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    s->num_bars = 0;

    /* BAR0: PLX registers (at least up to offset 0xA9) */
    s->bar_info[s->num_bars].index = 0;
    s->bar_info[s->num_bars].type = BAR_TYPE_MMIO;
    s->bar_info[s->num_bars].size = 0x1000;
    s->bar_info[s->num_bars].name = "gsc_hpdi_plx";
    s->num_bars++;

    /* BAR2: HPDI board registers */
    s->bar_info[s->num_bars].index = 2;
    s->bar_info[s->num_bars].type = BAR_TYPE_MMIO;
    s->bar_info[s->num_bars].size = 0x1000;
    s->bar_info[s->num_bars].name = "gsc_hpdi_board";
    s->num_bars++;

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = false;
    s->has_msix = false;

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

static const VMStateDescription vmstate_pcibase = {
    .name = "gsc_hpdi_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(hpdi_board_control, PCIBaseState),
        VMSTATE_UINT32(hpdi_board_status, PCIBaseState),
        VMSTATE_UINT32(hpdi_tx_prog_almost, PCIBaseState),
        VMSTATE_UINT32(hpdi_rx_prog_almost, PCIBaseState),
        VMSTATE_UINT32(hpdi_features, PCIBaseState),
        VMSTATE_UINT32(hpdi_interrupt_control, PCIBaseState),
        VMSTATE_UINT32(hpdi_interrupt_status, PCIBaseState),
        VMSTATE_UINT32(hpdi_tx_clock_divider, PCIBaseState),
        VMSTATE_UINT32(hpdi_tx_fifo_size, PCIBaseState),
        VMSTATE_UINT32(hpdi_rx_fifo_size, PCIBaseState),
        VMSTATE_UINT32(hpdi_tx_fifo_words, PCIBaseState),
        VMSTATE_UINT32(hpdi_rx_fifo_words, PCIBaseState),
        VMSTATE_UINT32(hpdi_int_edge_level, PCIBaseState),
        VMSTATE_UINT32(hpdi_int_polarity, PCIBaseState),
        VMSTATE_UINT32(plx_intcsr, PCIBaseState),
        VMSTATE_UINT32(plx_l2pdbell, PCIBaseState),
        VMSTATE_UINT32(plx_dmadpr0, PCIBaseState),
        VMSTATE_UINT32(plx_dmamode0, PCIBaseState),
        VMSTATE_UINT32(plx_dmasiz0, PCIBaseState),
        VMSTATE_UINT32(plx_dmapadr0, PCIBaseState),
        VMSTATE_UINT32(plx_dmaladr0, PCIBaseState),
        VMSTATE_UINT32(plx_dmapadr1, PCIBaseState),
        VMSTATE_UINT8(plx_dmacsr0, PCIBaseState),
        VMSTATE_UINT8(plx_dmacsr1, PCIBaseState),
        VMSTATE_UINT32(plx_bigend, PCIBaseState),
        VMSTATE_UINT64(dma_desc_head0, PCIBaseState),
        VMSTATE_BOOL(dma0_active, PCIBaseState),
        VMSTATE_BOOL(dma0_irq_pending, PCIBaseState),
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

