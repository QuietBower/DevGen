/*
 * QEMU PCI device model for TI TLAN-based Ethernet controller (simplified)
 * Generated to satisfy Linux drivers/net/ethernet/ti/tlan.c probing and
 * basic operation requirements, using only behavior visible in the driver.
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

#include "hw/pci/pci_ids.h"

#define TYPE_PCIBASE_DEVICE "tlan_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define TLAN_HOST_CMD            0x00
#define TLAN_HOST_INT            0x0A
#define TLAN_DIO_ADR             0x08
#define TLAN_DIO_DATA            0x0C
#define TLAN_NET_CMD             0x00
#define TLAN_NET_STS             0x02
#define TLAN_NET_CONFIG          0x04
#define TLAN_HASH_1              0x28
#define TLAN_HASH_2              0x2C
#define TLAN_GOOD_TX_FRMS        0x30
#define TLAN_GOOD_RX_FRMS        0x34
#define TLAN_DEFERRED_TX         0x38
#define TLAN_MULTICOL_FRMS       0x3C
#define TLAN_EXCESSCOL_FRMS      0x40
#define TLAN_AREG_0              0x10
#define TLAN_LED_REG             0x44
#define TLAN_INT_DIS             0x48
#define TLAN_MAX_RX              0x46

#define TLAN_NET_STS_MIRQ       0x80

#define TLAN_NET_CMD_CAF        0x10
#define TLAN_NET_CMD_NWRAP      0x40
#define TLAN_NET_CMD_DUPLEX     0x04
#define TLAN_NET_CMD_NRESET     0x80

#define TLAN_NET_CFG_BIT        0x2000
#define TLAN_NET_CFG_PHY_EN     0x0080
#define TLAN_NET_CFG_1FRAG      0x0400
#define TLAN_NET_CFG_1CHAN      0x0200

#define TLAN_NET_MASK           0x03
#define TLAN_NET_MASK_MASK4     0x10
#define TLAN_NET_MASK_MASK5     0x20
#define TLAN_NET_MASK_MASK7     0x80

#define TLAN_NET_SIO            0x01
#define TLAN_NET_SIO_NMRST      0x08
#define TLAN_NET_SIO_MINTEN     0x80
#define TLAN_NET_SIO_MCLK       0x04
#define TLAN_NET_SIO_MDATA      0x01
#define TLAN_NET_SIO_MTXEN      0x02
#define TLAN_NET_SIO_ECLOK      0x40
#define TLAN_NET_SIO_EDATA      0x10
#define TLAN_NET_SIO_ETXEN      0x20

#define TLAN_LED_LINK           0x01
#define TLAN_LED_ACT            0x10

#define TLAN_HC_AD_RST          0x00008000
#define TLAN_HC_GO              0x80000000
#define TLAN_HC_ACK             0x20000000
#define TLAN_HC_RT              0x00080000
#define TLAN_HC_INT_OFF         0x00000800
#define TLAN_HC_LD_TMR          0x00004000
#define TLAN_HC_LD_THR          0x00002000
#define TLAN_HC_REQ_INT         0x00001000
#define TLAN_HC_INT_ON          0x00000400

#define TLAN_HI_IT_MASK         0x001C
#define TLAN_HI_IV_MASK         0x1FE0

#define TLAN_CSTAT_UNUSED       0x8000
#define TLAN_CSTAT_READY        0x3000
#define TLAN_CSTAT_FRM_CMP      0x4000
#define TLAN_CSTAT_EOC          0x0800

#define TLAN_LAST_BUFFER        0x80000000

#define TLAN_MAX_FRAME_SIZE     1600
#define TLAN_MIN_FRAME_SIZE     64

#define TLAN_INT_NUMBER_OF_INTS 8
#define TLAN_BUFFERS_PER_LIST   10

#define TLAN_ADAPTER_NONE               0x00000000
#define TLAN_ADAPTER_UNMANAGED_PHY      0x00000001
#define TLAN_ADAPTER_BIT_RATE_PHY       0x00000002
#define TLAN_ADAPTER_USE_INTERN_10      0x00000004
#define TLAN_ADAPTER_ACTIVITY_LED       0x00000008

#define TLAN_DUPLEX_HALF        1
#define TLAN_DUPLEX_FULL        2

#define TLAN_SPEED_10           10
#define TLAN_SPEED_100          100

#define TLAN_PHY_NONE           0x20
#define TLAN_PHY_MAX_ADDR       0x1F
#define TLAN_PHY_SPEED_100      0x0040
#define TLAN_PHY_DUPLEX_FULL    0x0080

#define TLAN_TLPHY_CTL          0x11
#define TLAN_TLPHY_STS          0x12
#define TLAN_TLPHY_PAR          0x19

#define TLAN_TIMER_ACTIVITY           2
#define TLAN_TIMER_PHY_PDOWN          3
#define TLAN_TIMER_PHY_PUP            4
#define TLAN_TIMER_PHY_RESET          5
#define TLAN_TIMER_PHY_START_LINK     6
#define TLAN_TIMER_PHY_FINISH_AN      7
#define TLAN_TIMER_FINISH_RESET       8

#define TLAN_TIMER_ACT_DELAY    (100) /* dummy, kernel uses HZ/10 */
#define TX_TIMEOUT              (1000)

#define TLAN_EEPROM_SIZE        256

#define TLAN_NUM_RX_LISTS       32
#define TLAN_NUM_TX_LISTS       64

#define MAX_TLAN_BOARDS         8

#define TLAN_DEF_REVISION       0x0C

#define PCI_DEVICE_ID_NETELLIGENT_10_T2            0xB012
#define PCI_DEVICE_ID_NETELLIGENT_10_100_WS_5100   0xB030

/* Define missing PCI IDs used for this device */
#ifndef PCI_VENDOR_ID_COMPAQ
#define PCI_VENDOR_ID_COMPAQ 0x0E11
#endif

#ifndef PCI_DEVICE_ID_COMPAQ_NETEL10
#define PCI_DEVICE_ID_COMPAQ_NETEL10 0xAE32
#endif

/* Use the first PCI ID entry from tlan_pci_tbl */
#define TLAN_PCI_VENDOR_ID      PCI_VENDOR_ID_COMPAQ
#define TLAN_PCI_DEVICE_ID      PCI_DEVICE_ID_COMPAQ_NETEL10

#define TLAN_PCI_CLASS_ID       PCI_CLASS_NETWORK_ETHERNET

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

    /* Core host registers (I/O space) */
    uint32_t host_cmd;      /* 32-bit at offset 0x00 */
    uint16_t host_int;      /* 16-bit at offset 0x0A */

    /* DIO indirect address and internal registers */
    uint16_t dio_addr;      /* last written internal address */

    /* Internal DIO register space (0x00-0x4B as seen via tlan_print_dio) */
    uint8_t dio_regs[0x4C];

    /* Simple statistic counters backing tlan_read_and_clear_stats */
    uint32_t stat_good_tx_frames;
    uint32_t stat_good_rx_frames;
    uint8_t  stat_tx_underrun;
    uint8_t  stat_rx_overflow;
    uint16_t stat_deferred_tx;
    uint8_t  stat_crc_errors;
    uint8_t  stat_code_errors;
    uint16_t stat_multicol_frames;
    uint8_t  stat_singlecol_lo;
    uint8_t  stat_singlecol_hi;
    uint8_t  stat_excesscol;
    uint8_t  stat_latecol;
    uint8_t  stat_loss;

    /* PCI interrupt line state */
    bool irq_asserted;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* Simple model: assert IRQ whenever host_int is non-zero and interrupts
     * are enabled (no explicit enable bit modeled, so always enabled unless
     * INT_OFF is set in host_cmd).
     */
    bool want_irq = (s->host_int != 0) && !(s->host_cmd & TLAN_HC_INT_OFF);

    if (want_irq && !s->irq_asserted) {
        pci_set_irq(pdev, 1);
        s->irq_asserted = true;
    } else if (!want_irq && s->irq_asserted) {
        pci_set_irq(pdev, 0);
        s->irq_asserted = false;
    }
}

/* Device-initiated DMA logic based on driver access patterns
 *
 * The real hardware walks descriptor rings and performs DMA to/from host
 * memory. The driver, however, owns the rings and will manipulate descriptor
 * status (c_stat) itself; the card only signals completion and restarts
 * channels when HC_GO/HC_RT are issued and interrupts are handled.
 *
 * Implementing full DMA semantics would require information about
 * struct tlan_list, which is not provided. Therefore, this helper is a
 * no-op and we rely on the driver-side software emulation of completion.
 */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    (void)s;
    (void)is_write;
}

/* Helper to access DIO internal registers via indirect addressing */
static inline uint8_t *tlan_get_dio_ptr(PCIBaseState *s, uint16_t internal_addr)
{
    /* Internal DIO space is 0x00-0x4B; mask as the driver uses &0x3 or &0x2 */
    if (internal_addr < sizeof(s->dio_regs)) {
        return &s->dio_regs[internal_addr];
    }
    return NULL;
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* This device uses I/O space in the driver, not MMIO. Provide a dummy
     * MMIO region that returns 0.
     */
    PCIBaseState *s = opaque;
    (void)s;
    (void)addr;
    (void)size;
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* No MMIO in driver; ignore writes. */
    PCIBaseState *s = opaque;
    (void)s;
    (void)addr;
    (void)val;
    (void)size;
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Port I/O map as expected by driver:
     *   base + 0x00 : HOST_CMD (32-bit)
     *   base + 0x08 : DIO_ADR (16-bit)
     *   base + 0x0A : HOST_INT (16-bit)
     *   base + 0x0C : DIO_DATA (8/16/32 via internal addressing)
     */

    switch (size) {
    case 1:
        if (addr == TLAN_HOST_CMD) {
            val = (uint8_t)(s->host_cmd & 0xFF);
        } else if (addr == TLAN_HOST_CMD + 1) {
            val = (uint8_t)((s->host_cmd >> 8) & 0xFF);
        } else if (addr == TLAN_HOST_CMD + 2) {
            val = (uint8_t)((s->host_cmd >> 16) & 0xFF);
        } else if (addr == TLAN_HOST_CMD + 3) {
            val = (uint8_t)((s->host_cmd >> 24) & 0xFF);
        } else if (addr == TLAN_DIO_ADR) {
            val = (uint8_t)(s->dio_addr & 0xFF);
        } else if (addr == TLAN_DIO_ADR + 1) {
            val = (uint8_t)((s->dio_addr >> 8) & 0xFF);
        } else if (addr == TLAN_HOST_INT) {
            val = (uint8_t)(s->host_int & 0xFF);
        } else if (addr == TLAN_HOST_INT + 1) {
            val = (uint8_t)((s->host_int >> 8) & 0xFF);
        } else if (addr >= TLAN_DIO_DATA && addr < TLAN_DIO_DATA + 4) {
            /* inb(base + TLAN_DIO_DATA + (internal_addr & 0x3)) */
            uint16_t internal = s->dio_addr + (addr - TLAN_DIO_DATA);
            uint8_t *p = tlan_get_dio_ptr(s, internal);
            if (p) {
                val = *p;
            }
        }
        break;
    case 2:
        if (addr == TLAN_DIO_ADR) {
            val = s->dio_addr;
        } else if (addr == TLAN_HOST_INT) {
            val = s->host_int;
        } else if (addr == TLAN_DIO_DATA || addr == TLAN_DIO_DATA + 2) {
            /* inw(base + TLAN_DIO_DATA + (internal_addr & 0x2)) */
            uint16_t internal = s->dio_addr + (addr - TLAN_DIO_DATA);
            uint8_t *p0 = tlan_get_dio_ptr(s, internal);
            uint8_t *p1 = tlan_get_dio_ptr(s, internal + 1);
            uint16_t tmp = 0;
            if (p0) {
                tmp |= *p0;
            }
            if (p1) {
                tmp |= ((uint16_t)(*p1)) << 8;
            }
            val = tmp;
        } else if (addr == TLAN_HOST_CMD || addr == TLAN_HOST_CMD + 2) {
            uint32_t cmd = s->host_cmd;
            if (addr == TLAN_HOST_CMD + 2) {
                cmd >>= 16;
            }
            val = (uint16_t)(cmd & 0xFFFF);
        }
        break;
    case 4:
        if (addr == TLAN_HOST_CMD) {
            val = s->host_cmd;
        } else if (addr == TLAN_DIO_DATA || addr == TLAN_DIO_DATA + 2) {
            /* inl(base + TLAN_DIO_DATA + (internal_addr & 0x2)) */
            uint16_t internal = s->dio_addr + (addr - TLAN_DIO_DATA);
            uint8_t *p0 = tlan_get_dio_ptr(s, internal);
            uint8_t *p1 = tlan_get_dio_ptr(s, internal + 1);
            uint8_t *p2 = tlan_get_dio_ptr(s, internal + 2);
            uint8_t *p3 = tlan_get_dio_ptr(s, internal + 3);
            uint32_t tmp = 0;
            if (p0) tmp |= (uint32_t)(*p0);
            if (p1) tmp |= (uint32_t)(*p1) << 8;
            if (p2) tmp |= (uint32_t)(*p2) << 16;
            if (p3) tmp |= (uint32_t)(*p3) << 24;
            val = tmp;
        }
        break;
    default:
        break;
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (size) {
    case 1:
        if (addr == TLAN_HOST_CMD) {
            s->host_cmd = (s->host_cmd & ~0x000000FFu) | ((uint32_t)val & 0xFF);
        } else if (addr == TLAN_HOST_CMD + 1) {
            s->host_cmd = (s->host_cmd & ~0x0000FF00u) | (((uint32_t)val & 0xFF) << 8);
        } else if (addr == TLAN_HOST_CMD + 2) {
            s->host_cmd = (s->host_cmd & ~0x00FF0000u) | (((uint32_t)val & 0xFF) << 16);
        } else if (addr == TLAN_HOST_CMD + 3) {
            s->host_cmd = (s->host_cmd & ~0xFF000000u) | (((uint32_t)val & 0xFF) << 24);
        } else if (addr == TLAN_DIO_ADR) {
            s->dio_addr = (s->dio_addr & 0xFF00) | ((uint16_t)val & 0x00FF);
        } else if (addr == TLAN_DIO_ADR + 1) {
            s->dio_addr = (s->dio_addr & 0x00FF) | (((uint16_t)val & 0x00FF) << 8);
        } else if (addr == TLAN_HOST_INT) {
            /* lower byte of HOST_INT, driver uses outw(host_int, HOST_INT)
             * as W1C: bits written as 1 are cleared.
             */
            uint16_t mask = (uint16_t)val & 0x00FF;
            s->host_int &= ~mask;
        } else if (addr == TLAN_HOST_INT + 1) {
            uint16_t mask = ((uint16_t)val & 0x00FF) << 8;
            s->host_int &= ~mask;
        } else if (addr >= TLAN_DIO_DATA && addr < TLAN_DIO_DATA + 4) {
            /* outb(data, base + TLAN_DIO_DATA + (internal_addr & 0x3)) */
            uint16_t internal = s->dio_addr + (addr - TLAN_DIO_DATA);
            uint8_t *p = tlan_get_dio_ptr(s, internal);
            if (p) {
                *p = (uint8_t)val;
            }
        }
        break;
    case 2:
        if (addr == TLAN_DIO_ADR) {
            s->dio_addr = (uint16_t)val;
        } else if (addr == TLAN_HOST_INT) {
            /* W1C semantics */
            uint16_t mask = (uint16_t)val;
            s->host_int &= ~mask;
        } else if (addr == TLAN_DIO_DATA || addr == TLAN_DIO_DATA + 2) {
            /* outw(data, base + TLAN_DIO_DATA + (internal_addr & 0x2)) */
            uint16_t internal = s->dio_addr + (addr - TLAN_DIO_DATA);
            uint8_t *p0 = tlan_get_dio_ptr(s, internal);
            uint8_t *p1 = tlan_get_dio_ptr(s, internal + 1);
            uint16_t tmp = (uint16_t)val;
            if (p0) {
                *p0 = (uint8_t)(tmp & 0xFF);
            }
            if (p1) {
                *p1 = (uint8_t)((tmp >> 8) & 0xFF);
            }
        } else if (addr == TLAN_HOST_CMD || addr == TLAN_HOST_CMD + 2) {
            uint32_t cmd = s->host_cmd;
            uint16_t w = (uint16_t)val;
            if (addr == TLAN_HOST_CMD) {
                cmd = (cmd & 0xFFFF0000u) | w;
            } else {
                cmd = (cmd & 0x0000FFFFu) | ((uint32_t)w << 16);
            }
            s->host_cmd = cmd;
        }
        break;
    case 4:
        if (addr == TLAN_HOST_CMD) {
            uint32_t old_cmd = s->host_cmd;
            uint32_t new_cmd = (uint32_t)val;
            s->host_cmd = new_cmd;

            /* Interpret key control bits explicitly used by driver:
             *  - TLAN_HC_AD_RST: Adapter reset
             *  - TLAN_HC_INT_OFF / TLAN_HC_INT_ON
             *  - TLAN_HC_GO / TLAN_HC_RT: start TX/RX channels
             *  - TLAN_HC_ACK and vector/ack bits: clear interrupts
             */

            if (new_cmd & TLAN_HC_AD_RST) {
                /* Adapter reset: clear internal registers and stats */
                memset(s->dio_regs, 0, sizeof(s->dio_regs));
                s->dio_addr = 0;
                s->host_int = 0;
                s->stat_good_tx_frames = 0;
                s->stat_good_rx_frames = 0;
                s->stat_tx_underrun = 0;
                s->stat_rx_overflow = 0;
                s->stat_deferred_tx = 0;
                s->stat_crc_errors = 0;
                s->stat_code_errors = 0;
                s->stat_multicol_frames = 0;
                s->stat_singlecol_lo = 0;
                s->stat_singlecol_hi = 0;
                s->stat_excesscol = 0;
                s->stat_latecol = 0;
                s->stat_loss = 0;
            }

            if (new_cmd & TLAN_HC_LD_TMR) {
                /* Load timer: driver writes TLAN_HC_LD_TMR | 0x3f */
                /* Not modeled further. */
            }

            if (new_cmd & TLAN_HC_LD_THR) {
                /* Load threshold: driver writes TLAN_HC_LD_THR | 0x9 */
            }

            if (new_cmd & TLAN_HC_INT_OFF) {
                /* Interrupts disabled; update IRQ line */
            }

            if (new_cmd & TLAN_HC_INT_ON) {
                /* Interrupts enabled again */
            }

            if (new_cmd & TLAN_HC_ACK) {
                /* Acknowledge: driver computes 'ack' from tlan_int_vector
                 * handlers and ORs with TLAN_HC_ACK | (type << 18).
                 * Here we simply clear host_int, assuming the vector
                 * consumed all pending interrupt bits.
                 */
                s->host_int = 0;
            }

            if (new_cmd & TLAN_HC_GO) {
                /* Start TX or RX depending on channel; we don't know channel
                 * semantics, but for probe to succeed and for minimal
                 * operation we can simulate that an interrupt occurs
                 * indicating TX EOF or RX EOF soon after GO.
                 * However, without information on interrupt vector layout,
                 * we cannot set specific host_int bits; leaving host_int at 0
                 * avoids spurious interrupt loops. The driver will still
                 * consider link up via tlan_finish_reset.
                 */
                (void)old_cmd;
                pcibase_do_dma(s, true);
            }

        } else if (addr == TLAN_DIO_DATA || addr == TLAN_DIO_DATA + 2) {
            /* outl(data, base + TLAN_DIO_DATA + (internal_addr & 0x2)) */
            uint16_t internal = s->dio_addr + (addr - TLAN_DIO_DATA);
            uint8_t *p0 = tlan_get_dio_ptr(s, internal);
            uint8_t *p1 = tlan_get_dio_ptr(s, internal + 1);
            uint8_t *p2 = tlan_get_dio_ptr(s, internal + 2);
            uint8_t *p3 = tlan_get_dio_ptr(s, internal + 3);
            uint32_t tmp = (uint32_t)val;
            if (p0) *p0 = (uint8_t)(tmp & 0xFF);
            if (p1) *p1 = (uint8_t)((tmp >> 8) & 0xFF);
            if (p2) *p2 = (uint8_t)((tmp >> 16) & 0xFF);
            if (p3) *p3 = (uint8_t)((tmp >> 24) & 0xFF);
        }
        break;
    default:
        break;
    }

    pcibase_update_irq(s);
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

    /* Revert registers to power-on defaults */
    s->host_cmd = 0;
    s->host_int = 0;
    s->dio_addr = 0;
    memset(s->dio_regs, 0, sizeof(s->dio_regs));
    s->stat_good_tx_frames = 0;
    s->stat_good_rx_frames = 0;
    s->stat_tx_underrun = 0;
    s->stat_rx_overflow = 0;
    s->stat_deferred_tx = 0;
    s->stat_crc_errors = 0;
    s->stat_code_errors = 0;
    s->stat_multicol_frames = 0;
    s->stat_singlecol_lo = 0;
    s->stat_singlecol_hi = 0;
    s->stat_excesscol = 0;
    s->stat_latecol = 0;
    s->stat_loss = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  TLAN_PCI_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  TLAN_PCI_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, TLAN_PCI_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* Initialize BAR layout to provide an I/O port region for the device. */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 0x20; /* driver uses up to at least 0x10; 0x20 is safe power-of-two */
    s->bar_info[0].name = "tlan-io";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = false;
    s->has_msix = false;

    /* Initialize register shadows */
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
    .name = "tlan_pci",
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
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
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
