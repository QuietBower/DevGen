/*
 * QEMU model of GSC HP-DI PCI board with PLX 9080
 * Based on Linux driver gsc_hpdi.c
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

/* PCI IDs */
#define PCI_VENDOR_ID_PLX            0x10b5
#define PCI_DEVICE_ID_PLX_9080       0x9080
#define PCI_SUBSYSTEM_ID_HP_DI       0x2400
#define PCI_CLASS_ID_HP_DI           0x00000000

/* HP DI Register Offsets */
#define FIRMWARE_REV_REG              0x00
#define BOARD_CONTROL_REG             0x04
#define BOARD_STATUS_REG              0x08
#define TX_PROG_ALMOST_REG            0x0c
#define RX_PROG_ALMOST_REG            0x10
#define FEATURES_REG                  0x14
#define FIFO_REG                      0x18
#define TX_STATUS_COUNT_REG           0x1c
#define TX_LINE_VALID_COUNT_REG       0x20
#define TX_LINE_INVALID_COUNT_REG     0x24
#define RX_STATUS_COUNT_REG           0x28
#define RX_LINE_COUNT_REG             0x2c
#define INTERRUPT_CONTROL_REG         0x30
#define INTERRUPT_STATUS_REG          0x34
#define TX_CLOCK_DIVIDER_REG          0x38
#define TX_FIFO_SIZE_REG              0x40
#define RX_FIFO_SIZE_REG              0x44
#define TX_FIFO_WORDS_REG             0x48
#define RX_FIFO_WORDS_REG             0x4c
#define INTERRUPT_EDGE_LEVEL_REG       0x50
#define INTERRUPT_POLARITY_REG        0x54

/* HP DI Board Control Bits */
#define BOARD_RESET_BIT               BIT(0)
#define TX_FIFO_RESET_BIT             BIT(1)
#define RX_FIFO_RESET_BIT             BIT(2)
#define TX_ENABLE_BIT                 BIT(4)
#define RX_ENABLE_BIT                 BIT(5)
#define DEMAND_DMA_DIRECTION_TX_BIT   BIT(6)
#define LINE_VALID_ON_STATUS_VALID_BIT BIT(7)
#define START_TX_BIT                  BIT(8)
#define CABLE_THROTTLE_ENABLE_BIT     BIT(9)
#define TEST_MODE_ENABLE_BIT          BIT(31)

/* HP DI Board Status Bits */
#define COMMAND_LINE_STATUS_MASK      (0x7f << 0)
#define TX_IN_PROGRESS_BIT            BIT(7)
#define TX_NOT_EMPTY_BIT              BIT(8)
#define TX_NOT_ALMOST_EMPTY_BIT       BIT(9)
#define TX_NOT_ALMOST_FULL_BIT        BIT(10)
#define TX_NOT_FULL_BIT               BIT(11)
#define RX_NOT_EMPTY_BIT              BIT(12)
#define RX_NOT_ALMOST_EMPTY_BIT       BIT(13)
#define RX_NOT_ALMOST_FULL_BIT        BIT(14)
#define RX_NOT_FULL_BIT               BIT(15)
#define BOARD_JUMPER0_INSTALLED_BIT   BIT(16)
#define BOARD_JUMPER1_INSTALLED_BIT   BIT(17)
#define TX_OVERRUN_BIT                BIT(21)
#define RX_UNDERRUN_BIT               BIT(22)
#define RX_OVERRUN_BIT                BIT(23)

/* HP DI Features Bits */
#define FEATURES_REG_PRESENT_BIT      BIT(15)
#define FIFO_SIZE_PRESENT_BIT         BIT(0)
#define FIFO_WORDS_PRESENT_BIT        BIT(1)
#define LEVEL_EDGE_INTERRUPTS_PRESENT_BIT BIT(2)
#define GPIO_SUPPORTED_BIT            BIT(3)
#define PLX_DMA_CH1_SUPPORTED_BIT     BIT(4)
#define OVERRUN_UNDERRUN_SUPPORTED_BIT BIT(5)

/* HP DI Almost Registers */
#define ALMOST_EMPTY_BITS(x)          (((x) & 0xffff) << 0)
#define ALMOST_FULL_BITS(x)           (((x) & 0xff) << 16)

/* HP DI Interrupt Control Bits */
#define FRAME_VALID_START_INTR        BIT(0)
#define FRAME_VALID_END_INTR          BIT(1)
#define TX_FIFO_EMPTY_INTR            BIT(8)
#define TX_FIFO_ALMOST_EMPTY_INTR     BIT(9)
#define TX_FIFO_ALMOST_FULL_INTR      BIT(10)
#define TX_FIFO_FULL_INTR             BIT(11)
#define RX_EMPTY_INTR                 BIT(12)
#define RX_ALMOST_EMPTY_INTR          BIT(13)
#define RX_ALMOST_FULL_INTR           BIT(14)
#define RX_FULL_INTR                  BIT(15)

/* HP DI FIFO Sizes */
#define FIFO_SIZE_MASK                (0xfffff << 0)

/* PLX9080 Register Offsets */
#define PLX_REG_L2PDBELL              0x0064
#define PLX_REG_INTCSR                0x0068
#define PLX_REG_DMACSR0               0x00a8
#define PLX_REG_DMACSR1               0x00a9
#define PLX_REG_DMALADR0              0x0088
#define PLX_REG_DMADPR0               0x0090
#define PLX_REG_DMASIZ0               0x008c
#define PLX_REG_DMAPADR0              0x0084
#define PLX_REG_DMAPADR1              0x0098
#define PLX_REG_DMAMODE0              0x0080
#define PLX_REG_BIGEND                0x000c

/* PLX9080 Register Bits */
#define PLX_INTCSR_LDBIA              BIT(20)
#define PLX_INTCSR_DMA1IA             BIT(22)
#define PLX_INTCSR_PLIA               BIT(15)
#define PLX_INTCSR_DMA0IA             BIT(21)
#define PLX_INTCSR_LSEPARITYEN        BIT(1)
#define PLX_INTCSR_PABORTIEN          BIT(10)
#define PLX_INTCSR_LIOEN              BIT(16)
#define PLX_INTCSR_LSEABORTEN         BIT(0)
#define PLX_INTCSR_PLIEN              BIT(11)
#define PLX_INTCSR_PIEN               BIT(8)
#define PLX_INTCSR_DMA0IEN            BIT(18)
#define PLX_DMACSR_ENABLE             BIT(0)
#define PLX_DMACSR_START              BIT(1)
#define PLX_DMACSR_ABORT              BIT(2)
#define PLX_DMACSR_CLEARINTR          BIT(3)
#define PLX_DMACSR_DONE               BIT(4)
#define PLX_DMADPR_DESCPCI            BIT(0)
#define PLX_DMADPR_TCINTR             BIT(2)
#define PLX_DMADPR_XFERL2P            BIT(3)
#define PLX_DMAMODE_BURSTEN           BIT(8)
#define PLX_DMAMODE_CHAINEN           BIT(9)
#define PLX_DMAMODE_DONEIEN           BIT(10)
#define PLX_DMAMODE_LACONST           BIT(11)
#define PLX_DMAMODE_DEMAND            BIT(12)
#define PLX_DMAMODE_READYIEN          BIT(6)
#define PLX_DMAMODE_INTRPCI           BIT(17)
#define PLX_DMAMODE_WIDTH_32          (BIT(0) * 2)
#define PLX_BIGEND_DMA0               BIT(7)
#define PLX_BIGEND_DMA1               BIT(6)

/* DMA macros */
#define PLX_REG_DMAPADR(n)            ((n) ? PLX_REG_DMAPADR1 : PLX_REG_DMAPADR0)
#define PLX_REG_DMACSR(n)             ((n) ? PLX_REG_DMACSR1 : PLX_REG_DMACSR0)

/* DMA Buffer Sizes */
#define DMA_BUFFER_SIZE               0x10000
#define NUM_DMA_BUFFERS               4
#define NUM_DMA_DESCRIPTORS           256

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
    const MemoryRegionOps *ops;   /* added for separate MMIO handlers */
} BARInfo;

/* Struct definitions */
struct plx_dma_desc {
    __le32 pci_start_addr;
    __le32 local_start_addr;
    __le32 transfer_size;
    __le32 next;
};
typedef struct plx_dma_desc plx_dma_desc;

typedef struct {
    uint32_t firmware_rev;
    uint32_t board_control;
    uint32_t board_status;
    uint32_t tx_prog_almost;
    uint32_t rx_prog_almost;
    uint32_t features;
    uint32_t fifo_reg;           /* FIFO port: read/write data */
    uint32_t tx_status_count;
    uint32_t tx_line_valid_count;
    uint32_t tx_line_invalid_count;
    uint32_t rx_status_count;
    uint32_t rx_line_count;
    uint32_t interrupt_control;
    uint32_t interrupt_status;
    uint32_t tx_clock_divider;
    uint32_t reserved_3c[1];
    uint32_t tx_fifo_size;
    uint32_t rx_fifo_size;
    uint32_t tx_fifo_words;
    uint32_t rx_fifo_words;
    uint32_t interrupt_edge_level;
    uint32_t interrupt_polarity;
} HP_DI_Registers;

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
    HP_DI_Registers regs;

    /* PLX9080 registers */
    uint32_t plx_intcsr;        /* combined IA and IEN bits */
    uint8_t  dmacsr0;
    uint8_t  dmacsr1;
    uint32_t dmaladr0;
    uint32_t dmadpr0;
    uint32_t dmasiz0;
    uint32_t dmapadr0;
    uint32_t dmapadr1;
    uint32_t dmamode0;
    uint32_t bigend;
    uint32_t l2pdbell;

    /* DMA Context */
    plx_dma_desc *dma_desc_ring;
    hwaddr dma_desc_phys_addr;
    unsigned int num_dma_descriptors;
    unsigned int dma_desc_index;
    uint32_t *dma_buffers[NUM_DMA_BUFFERS];
    hwaddr dma_buffers_phys_addr[NUM_DMA_BUFFERS];
    uint32_t *desc_dio_buffer[NUM_DMA_DESCRIPTORS];
    unsigned int block_size;
    unsigned long dio_count;
    unsigned int tx_fifo_size;
    unsigned int rx_fifo_size;
    int dma_channel;
    bool dma_running;
    bool plx_intr_enabled;

    bool board_reset_done;
};

/* Prototypes */
static uint64_t plx_mmio_read(void *opaque, hwaddr addr, unsigned size);
static void plx_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint64_t hpdi_mmio_read(void *opaque, hwaddr addr, unsigned size);
static void hpdi_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);

static const MemoryRegionOps plx_mmio_ops = {
    .read = plx_mmio_read,
    .write = plx_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static const MemoryRegionOps hpdi_mmio_ops = {
    .read = hpdi_mmio_read,
    .write = hpdi_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t plx_ia = s->plx_intcsr & (PLX_INTCSR_DMA0IA | PLX_INTCSR_DMA1IA |
                                        PLX_INTCSR_PLIA | PLX_INTCSR_LDBIA);
    uint32_t plx_ien = s->plx_intcsr & (PLX_INTCSR_DMA0IEN |
                                         PLX_INTCSR_LSEABORTEN | PLX_INTCSR_LSEPARITYEN |
                                         PLX_INTCSR_PIEN | PLX_INTCSR_PABORTIEN |
                                         PLX_INTCSR_PLIEN | PLX_INTCSR_LIOEN);
    /* Simple mapping: raise if any IA bit is set and its corresponding IEN is set
     * or for those without explicit IEN, assume active anyway. We map PLIA to PLIEN,
     * DMA0 to DMA0IEN, etc. LDBIA has no IEN, ignore for now.
     */
    bool irq_active = false;
    if ((s->plx_intcsr & PLX_INTCSR_DMA0IA) && (s->plx_intcsr & PLX_INTCSR_DMA0IEN)) {
        irq_active = true;
    }
    if ((s->plx_intcsr & PLX_INTCSR_PLIA) && (s->plx_intcsr & PLX_INTCSR_PLIEN)) {
        irq_active = true;
    }
    /* Other sources omitted for simplicity */

    pci_set_irq(pdev, irq_active ? 1 : 0);
}

static uint64_t plx_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case PLX_REG_BIGEND:
        val = s->bigend;
        break;
    case PLX_REG_L2PDBELL:
        val = s->l2pdbell;
        break;
    case PLX_REG_INTCSR:
        val = s->plx_intcsr;
        break;
    case PLX_REG_DMACSR0:
        if (size == 1) {
            val = s->dmacsr0;
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "plx: bad read size %d at DMACSR0\n", size);
        }
        break;
    case PLX_REG_DMACSR1:
        if (size == 1) {
            val = s->dmacsr1;
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "plx: bad read size %d at DMACSR1\n", size);
        }
        break;
    case PLX_REG_DMALADR0:
        val = s->dmaladr0;
        break;
    case PLX_REG_DMADPR0:
        val = s->dmadpr0;
        break;
    case PLX_REG_DMASIZ0:
        val = s->dmasiz0;
        break;
    case PLX_REG_DMAPADR0:
        val = s->dmapadr0;
        break;
    case PLX_REG_DMAPADR1:
        val = s->dmapadr1;
        break;
    case PLX_REG_DMAMODE0:
        val = s->dmamode0;
        break;
    default:
        /* unimplemented, return 0 */
        break;
    }
    return val;
}

static void plx_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case PLX_REG_BIGEND:
        s->bigend = val;
        break;
    case PLX_REG_L2PDBELL:
        s->l2pdbell = val;
        /* Writing doorbell register clears corresponding interrupt?
         * Driver writes back read value to clear. No action needed here.
         */
        s->plx_intcsr &= ~PLX_INTCSR_LDBIA; /* simulate clearing LDBIA */
        pcibase_update_irq(s);
        break;
    case PLX_REG_INTCSR: {
        /* Only enable bits are writable (IEN bits). IA bits are read-only. */
        uint32_t ien_mask = PLX_INTCSR_LSEABORTEN | PLX_INTCSR_LSEPARITYEN |
                            PLX_INTCSR_PIEN | PLX_INTCSR_PABORTIEN |
                            PLX_INTCSR_PLIEN | PLX_INTCSR_LIOEN |
                            PLX_INTCSR_DMA0IEN;
        s->plx_intcsr = (s->plx_intcsr & ~ien_mask) | (val & ien_mask);
        pcibase_update_irq(s);
        break;
    }
    case PLX_REG_DMACSR0:
        if (size == 1) {
            uint8_t old = s->dmacsr0;
            s->dmacsr0 = val;
            if (val & PLX_DMACSR_CLEARINTR) {
                s->plx_intcsr &= ~PLX_INTCSR_DMA0IA;
                pcibase_update_irq(s);
            }
            if (val & PLX_DMACSR_ABORT) {
                s->dmacsr0 &= ~PLX_DMACSR_ENABLE;
                s->dmacsr0 |= PLX_DMACSR_DONE;
                /* If start and enable are set, we would start DMA; for now just set DONE */
                if ((s->dmacsr0 & PLX_DMACSR_ENABLE) && (s->dmacsr0 & PLX_DMACSR_START)) {
                    s->dmacsr0 |= PLX_DMACSR_DONE;
                    if (s->dmamode0 & PLX_DMAMODE_DONEIEN) {
                        s->plx_intcsr |= PLX_INTCSR_DMA0IA;
                        pcibase_update_irq(s);
                    }
                }
            }
            /* If writing ENABLE and START, simulate immediate completion */
            if ((s->dmacsr0 & PLX_DMACSR_ENABLE) && (s->dmacsr0 & PLX_DMACSR_START) &&
                !(old & PLX_DMACSR_START)) {
                s->dmacsr0 |= PLX_DMACSR_DONE;
                if (s->dmamode0 & PLX_DMAMODE_DONEIEN) {
                    s->plx_intcsr |= PLX_INTCSR_DMA0IA;
                    pcibase_update_irq(s);
                }
            }
        }
        break;
    case PLX_REG_DMACSR1:
        if (size == 1) {
            s->dmacsr1 = val;
            if (val & PLX_DMACSR_CLEARINTR) {
                s->plx_intcsr &= ~PLX_INTCSR_DMA1IA;
                pcibase_update_irq(s);
            }
        }
        break;
    case PLX_REG_DMALADR0:
        s->dmaladr0 = val;
        break;
    case PLX_REG_DMADPR0:
        s->dmadpr0 = val;
        break;
    case PLX_REG_DMASIZ0:
        s->dmasiz0 = val;
        break;
    case PLX_REG_DMAPADR0:
        s->dmapadr0 = val;
        break;
    case PLX_REG_DMAPADR1:
        s->dmapadr1 = val;
        break;
    case PLX_REG_DMAMODE0:
        s->dmamode0 = val;
        break;
    default:
        break;
    }
}

static uint64_t hpdi_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR, "hpdi: bad read size %d at addr 0x%" HWADDR_PRIx "\n", size, addr);
        return ~0ULL;
    }

    switch (addr) {
    case FIRMWARE_REV_REG:
        val = s->regs.firmware_rev;
        break;
    case BOARD_CONTROL_REG:
        val = s->regs.board_control;
        break;
    case BOARD_STATUS_REG:
        val = s->regs.board_status;
        break;
    case TX_PROG_ALMOST_REG:
        val = s->regs.tx_prog_almost;
        break;
    case RX_PROG_ALMOST_REG:
        val = s->regs.rx_prog_almost;
        break;
    case FEATURES_REG:
        val = s->regs.features;
        break;
    case FIFO_REG:
        val = s->regs.fifo_reg;
        break;
    case TX_STATUS_COUNT_REG:
        val = s->regs.tx_status_count;
        break;
    case TX_LINE_VALID_COUNT_REG:
        val = s->regs.tx_line_valid_count;
        break;
    case TX_LINE_INVALID_COUNT_REG:
        val = s->regs.tx_line_invalid_count;
        break;
    case RX_STATUS_COUNT_REG:
        val = s->regs.rx_status_count;
        break;
    case RX_LINE_COUNT_REG:
        val = s->regs.rx_line_count;
        break;
    case INTERRUPT_CONTROL_REG:
        val = s->regs.interrupt_control;
        break;
    case INTERRUPT_STATUS_REG:
        val = s->regs.interrupt_status;
        break;
    case TX_CLOCK_DIVIDER_REG:
        val = s->regs.tx_clock_divider;
        break;
    case 0x3c:
        val = s->regs.reserved_3c[0];
        break;
    case TX_FIFO_SIZE_REG:
        val = s->regs.tx_fifo_size;
        break;
    case RX_FIFO_SIZE_REG:
        val = s->regs.rx_fifo_size;
        break;
    case TX_FIFO_WORDS_REG:
        val = s->regs.tx_fifo_words;
        break;
    case RX_FIFO_WORDS_REG:
        val = s->regs.rx_fifo_words;
        break;
    case INTERRUPT_EDGE_LEVEL_REG:
        val = s->regs.interrupt_edge_level;
        break;
    case INTERRUPT_POLARITY_REG:
        val = s->regs.interrupt_polarity;
        break;
    default:
        /* unmapped, return 0 */
        break;
    }
    return val;
}

static void hpdi_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR, "hpdi: bad write size %d at addr 0x%" HWADDR_PRIx "\n", size, addr);
        return;
    }

    switch (addr) {
    case FIRMWARE_REV_REG:
        /* read-only */
        break;
    case BOARD_CONTROL_REG:
        s->regs.board_control = val;
        /* Handle reset if BOARD_RESET_BIT set */
        if (val & BOARD_RESET_BIT) {
            /* Reset HPDI logic: later calls to init will reconfigure; for now just clear FIFOs? */
            s->regs.board_status &= ~(RX_OVERRUN_BIT | RX_UNDERRUN_BIT | TX_OVERRUN_BIT);
            s->regs.interrupt_status = 0;
            /* Clear PLX PLIA? */
            s->plx_intcsr &= ~PLX_INTCSR_PLIA;
            pcibase_update_irq(s);
        }
        break;
    case BOARD_STATUS_REG:
        /* W1C for overrun/underrun bits */
        if (val & (RX_OVERRUN_BIT | RX_UNDERRUN_BIT | TX_OVERRUN_BIT)) {
            s->regs.board_status &= ~(val & (RX_OVERRUN_BIT | RX_UNDERRUN_BIT | TX_OVERRUN_BIT));
        }
        break;
    case TX_PROG_ALMOST_REG:
        s->regs.tx_prog_almost = val;
        break;
    case RX_PROG_ALMOST_REG:
        s->regs.rx_prog_almost = val;
        break;
    case FEATURES_REG:
        /* read-only */
        break;
    case FIFO_REG:
        s->regs.fifo_reg = val;
        break;
    case TX_STATUS_COUNT_REG:
        s->regs.tx_status_count = val;
        break;
    case TX_LINE_VALID_COUNT_REG:
        s->regs.tx_line_valid_count = val;
        break;
    case TX_LINE_INVALID_COUNT_REG:
        s->regs.tx_line_invalid_count = val;
        break;
    case RX_STATUS_COUNT_REG:
        s->regs.rx_status_count = val;
        break;
    case RX_LINE_COUNT_REG:
        s->regs.rx_line_count = val;
        break;
    case INTERRUPT_CONTROL_REG:
        s->regs.interrupt_control = val;
        /* Re-evaluate local interrupt PLIA */
        if (s->regs.interrupt_status & s->regs.interrupt_control) {
            s->plx_intcsr |= PLX_INTCSR_PLIA;
        } else {
            s->plx_intcsr &= ~PLX_INTCSR_PLIA;
        }
        pcibase_update_irq(s);
        break;
    case INTERRUPT_STATUS_REG:
        /* W1C */
        s->regs.interrupt_status &= ~val;
        if (!(s->regs.interrupt_status & s->regs.interrupt_control)) {
            s->plx_intcsr &= ~PLX_INTCSR_PLIA;
        }
        pcibase_update_irq(s);
        break;
    case TX_CLOCK_DIVIDER_REG:
        s->regs.tx_clock_divider = val;
        break;
    case 0x3c:
        s->regs.reserved_3c[0] = val;
        break;
    case TX_FIFO_SIZE_REG:
    case RX_FIFO_SIZE_REG:
        /* read-only */
        break;
    case TX_FIFO_WORDS_REG:
    case RX_FIFO_WORDS_REG:
        /* read-only */
        break;
    case INTERRUPT_EDGE_LEVEL_REG:
        s->regs.interrupt_edge_level = val;
        break;
    case INTERRUPT_POLARITY_REG:
        s->regs.interrupt_polarity = val;
        break;
    default:
        break;
    }
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }
    
    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];
    const MemoryRegionOps *ops = bi->ops;

    if (bi->type == BAR_TYPE_MMIO) {
        if (ops == NULL) {
            /* fallback */
            static const MemoryRegionOps default_mmio_ops = {
                .read = NULL, .write = NULL,
                .endianness = DEVICE_LITTLE_ENDIAN,
                .valid = { .min_access_size = 1, .max_access_size = 8 },
                .impl = { .min_access_size = 1, .max_access_size = 8 },
            };
            ops = &default_mmio_ops;
        }
        memory_region_init_io(mr, OBJECT(s), ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        /* not used */
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* HPDI registers */
    memset(&s->regs, 0, sizeof(s->regs));
    s->regs.firmware_rev = 0x00000001;          /* arbitrary version */
    s->regs.features     = 0x0000803F;          /* all features present */
    s->regs.tx_fifo_size = 0x00002000;          /* 8k FIFO */
    s->regs.rx_fifo_size = 0x00002000;

    /* PLX registers */
    s->plx_intcsr = 0;
    s->dmacsr0 = 0;
    s->dmacsr1 = 0;
    s->dmaladr0 = 0;
    s->dmadpr0 = 0;
    s->dmasiz0 = 0;
    s->dmapadr0 = 0;
    s->dmapadr1 = 0;
    s->dmamode0 = 0;
    s->bigend = 0;
    s->l2pdbell = 0;

    /* DMA related state */
    s->dma_desc_ring = NULL;
    s->dma_desc_phys_addr = 0;
    s->num_dma_descriptors = 0;
    s->dma_desc_index = 0;
    s->block_size = 0;
    s->dio_count = 0;
    s->tx_fifo_size = 0;
    s->rx_fifo_size = 0;
    s->dma_channel = 0;
    s->dma_running = false;
    s->plx_intr_enabled = false;
    s->board_reset_done = false;
    for (int i = 0; i < NUM_DMA_BUFFERS; i++) {
        s->dma_buffers[i] = NULL;
        s->dma_buffers_phys_addr[i] = 0;
    }
    for (int i = 0; i < NUM_DMA_DESCRIPTORS; i++) {
        s->desc_dio_buffer[i] = NULL;
    }
    /* Ensure IRQ line is low */
    pci_set_irq(PCI_DEVICE(dev), 0);
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_PLX);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_PLX_9080);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_ID_HP_DI);
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
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = 0x1000,
                                .name = "plx-mmio", .ops = &plx_mmio_ops };
    s->bar_info[1] = (BARInfo){ .index = 2, .type = BAR_TYPE_MMIO, .size = 0x100,
                                .name = "hpdi-mmio", .ops = &hpdi_mmio_ops };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = false;
    s->has_msix = false;

    /* Set subsystem IDs */
    pci_set_word(pci_conf + PCI_SUBSYSTEM_VENDOR_ID, PCI_VENDOR_ID_PLX);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_ID, 0x2400);

    /* Field initialization already done in reset, but call reset to apply defaults */
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
    /* Free DMA resources? Not needed for simulation yet. */
}

static const VMStateDescription vmstate_pcibase = {
    .name = "gsc_hpdi_pci",
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
