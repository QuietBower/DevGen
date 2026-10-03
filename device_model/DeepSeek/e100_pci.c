/*
 * QEMU PCI device model for Intel e100 Ethernet controller (e100.c driver)
 * This model provides minimal functional behavior to allow the Linux e100 driver
 * to successfully probe, initialize, and bind.
 * Implements MMIO register access, EEPROM bit-bang reads, MDIO for PHY discovery,
 * reset handling, and legacy INTx interrupt generation.
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
/* No additional headers required */

#define TYPE_PCIBASE_DEVICE "e100_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_INTEL 0x8086
#define E100_DEVICE_ID 0x1029
#define E100_CLASS_ID 0x0200 /* Network controller, Ethernet */

/* Register offsets derived from struct csr in e100.c */
#define CSR_SCB_STATUS   0x00
#define CSR_SCB_STAT_ACK 0x01
#define CSR_SCB_CMD_LO   0x02
#define CSR_SCB_CMD_HI   0x03
#define CSR_SCB_GEN_PTR  0x04
#define CSR_PORT         0x08
#define CSR_FLASH_CTRL   0x0C
#define CSR_EEPROM_CTRL_LO 0x0E
#define CSR_EEPROM_CTRL_HI 0x0F
#define CSR_MDI_CTRL     0x10
#define CSR_RX_DMA_COUNT 0x14

/* SCB status bits */
#define SCB_STATUS_RUS_NO_RES   0x08
#define SCB_STATUS_RUS_READY    0x10
#define SCB_STATUS_RUS_MASK     0x3C

/* SCB stat_ack bits */
#define STAT_ACK_NOT_OURS       0x00
#define STAT_ACK_SW_GEN         0x04
#define STAT_ACK_RNR            0x10
#define STAT_ACK_CU_IDLE        0x20
#define STAT_ACK_FRAME_RX       0x40
#define STAT_ACK_CU_CMD_DONE    0x80
#define STAT_ACK_NOT_PRESENT    0xFF

/* SCB cmd_hi bits */
#define IRQ_MASK_NONE           0x00
#define IRQ_MASK_ALL            0x01
#define IRQ_SW_GEN              0x02

/* SCB cmd_lo bits */
#define CUC_NOP                 0x00
#define RUC_START               0x01
#define RUC_LOAD_BASE           0x06
#define CUC_START               0x10
#define CUC_RESUME              0x20
#define CUC_DUMP_ADDR           0x40
#define CUC_DUMP_STATS          0x50
#define CUC_LOAD_BASE           0x60
#define CUC_DUMP_RESET          0x70

/* CUC dump return codes */
#define CUC_DUMP_COMPLETE       0x0000A005
#define CUC_DUMP_RESET_COMPLETE 0x0000A007

/* EEPROM control bits */
#define EESK 0x01
#define EECS 0x02
#define EEDI 0x04
#define EEDO 0x08

/* MDI control bits */
#define MDI_WRITE 0x04000000
#define MDI_READ  0x08000000
#define MDI_READY 0x10000000

/* Port commands */
#define PORT_SOFTWARE_RESET  0x0000
#define PORT_SELFTEST        0x0001
#define PORT_SELECTIVE_RESET 0x0002

/* Other constants */
#define UCODE_SIZE 134
#define E100_MAX_MULTICAST_ADDRS 64
#ifndef RFD_BUF_LEN
#define RFD_BUF_LEN (sizeof(struct rfd) + VLAN_ETH_FRAME_LEN + ETH_FCS_LEN)
#endif

/* MII Register Definitions */
#define MII_BMCR         0
#define MII_BMSR         1
#define MII_PHYSID1      2
#define MII_PHYSID2      3

/* BMCR bits */
#define BMCR_ANENABLE    0x1000
#define BMCR_FULLDPLX    0x0100
#define BMCR_SPEED100    0x2000

/* BMSR bits */
#define BMSR_LSTATUS     0x0004
#define BMSR_ANEGCAPABLE 0x0008

/* Default PHY ID (Intel 8255x) */
#define DEFAULT_PHYSID1  0x0282
#define DEFAULT_PHYSID2  0x1000

/* EEPROM size in words */
#define EEPROM_WC 64

/* Enums (non-conflicting, definitions that do not clash with macros) */
typedef enum {
    RUS_NO_RES       = 0x08,
    RUS_READY        = 0x10,
    RUS_MASK         = 0x3C
} scb_status;

typedef enum {
    RU_SUSPENDED = 0,
    RU_RUNNING   = 1,
    RU_UNINITIALIZED = -1
} ru_state;

/* Stat ack combination macros */
#define STAT_ACK_RX (STAT_ACK_SW_GEN | STAT_ACK_RNR | STAT_ACK_FRAME_RX)
#define STAT_ACK_TX (STAT_ACK_CU_IDLE | STAT_ACK_CU_CMD_DONE)

/* BAR definitions */
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
    uint32_t intr_status; /* Interrupt status register shadow */
    uint32_t intr_mask;   /* Interrupt mask register shadow */

    /* Hardware Register Shadows (The 'Identity' of the device) */
    /* CSR register shadows */
    uint8_t  scb_status;
    uint8_t  scb_stat_ack;
    uint8_t  scb_cmd_lo;
    uint8_t  scb_cmd_hi;
    uint32_t scb_gen_ptr;
    uint32_t port;
    uint16_t flash_ctrl;
    uint8_t  eeprom_ctrl_lo;
    uint8_t  eeprom_ctrl_hi;
    uint32_t mdi_ctrl;
    uint32_t rx_dma_count;

    /* DMA Context (TODO: filled in Phase 2) */
    /* Placeholder for DMA descriptors and ring state */
    uint8_t dma_placeholder[1];

    /* Operational status flags */
    uint32_t status_flags;

    /* State used to handle reset sequences */
    uint8_t reset_count;

    /* Power management state (D0-D3) */
    uint8_t pm_state;

    /* EEPROM state */
    uint16_t eeprom_data[EEPROM_WC];
    uint8_t last_eeprom_ctrl; /* previous value of eeprom_ctrl_lo to detect edges */
    uint32_t eeprom_shift_in; /* input shift register for command/address */
    int eeprom_clk_count;     /* clock cycles within transaction */
    uint8_t eeprom_output_bit; /* current EEDO bit to be read */
    uint16_t eeprom_output_data_word; /* current word for read */
    int eeprom_output_bit_index; /* current bit index in the word */

    /* EEPROM transaction state machine variables */
    int eeprom_state;         /* 0=IDLE, 1=COMMAND, 2=ADDRESS, 3=DATA_OUT */
    int eeprom_cmd_bit_count; /* bits shifted in during transaction */
    uint8_t eeprom_opcode; 
    int eeprom_addr_bit_count;
    uint16_t eeprom_address;
    int eeprom_addr_length;   /* current expected address length */
    bool eeprom_size_detected; /* true after first read of word0 */

    /* MDIO (PHY) state */
    uint16_t phy_regs[32]; /* register file for PHY at address 1 */
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    /* Level-triggered: raise if any stat_ack bits are set and IRQ not masked */
    if (s->scb_stat_ack && ((s->scb_cmd_hi & IRQ_MASK_ALL) == IRQ_MASK_NONE)) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /* TODO: Implement DMA logic in Phase 2 (not needed for probe) */
    (void)s;
    (void)is_write;
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case CSR_SCB_STATUS:
        val = s->scb_status;
        break;
    case CSR_SCB_STAT_ACK:
        val = s->scb_stat_ack;
        break;
    case CSR_SCB_CMD_LO:
        val = s->scb_cmd_lo;
        break;
    case CSR_SCB_CMD_HI:
        val = s->scb_cmd_hi;
        break;
    case CSR_SCB_GEN_PTR:
        val = s->scb_gen_ptr;
        break;
    case CSR_PORT:
        val = s->port;
        break;
    case CSR_FLASH_CTRL:
        val = s->flash_ctrl;
        break;
    case CSR_EEPROM_CTRL_LO:
        /* Return the last written value but with EEDO replaced by the current output bit */
        val = (s->eeprom_ctrl_lo & ~EEDO) | s->eeprom_output_bit;
        break;
    case CSR_EEPROM_CTRL_HI:
        val = s->eeprom_ctrl_hi;
        break;
    case CSR_MDI_CTRL:
        val = s->mdi_ctrl;
        break;
    case CSR_RX_DMA_COUNT:
        val = s->rx_dma_count;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad read at offset 0x%"HWADDR_PRIx"\n", __func__, addr);
        val = ~0ULL;
        break;
    }
    return val;
}

static void handle_port_write(PCIBaseState *s, uint32_t val)
{
    /* Port register is used for reset and self-test commands */
    if (val == PORT_SOFTWARE_RESET) {
        /* Software reset: reset all registers to defaults */
        s->scb_status = 0;
        s->scb_stat_ack = 0;
        s->scb_cmd_lo = 0;
        s->scb_cmd_hi = IRQ_MASK_NONE;
        s->scb_gen_ptr = 0;
        s->port = 0;
        s->flash_ctrl = 0;
        s->eeprom_ctrl_lo = 0;
        s->eeprom_ctrl_hi = 0;
        s->mdi_ctrl = 0;
        s->rx_dma_count = 0;
        s->intr_status = 0;
        s->intr_mask = 0;
        s->status_flags = 0;
        s->reset_count = 0;
        s->pm_state = 0;
        /* EEPROM state unchanged (retains its contents) */
        s->last_eeprom_ctrl = 0;
        s->eeprom_shift_in = 0;
        s->eeprom_clk_count = 0;
        s->eeprom_output_bit = 0;
        s->eeprom_output_data_word = 0;
        s->eeprom_output_bit_index = 0;
        s->eeprom_state = 0;
        s->eeprom_cmd_bit_count = 0;
        s->eeprom_opcode = 0;
        s->eeprom_addr_bit_count = 0;
        s->eeprom_address = 0;
        /* eeprom_addr_length and eeprom_size_detected persist? They should reset to defaults. */
        s->eeprom_addr_length = 8;
        s->eeprom_size_detected = false;
        /* PHY regs retained (persist across reset) */
        pcibase_update_irq(s);
    } else if (val == PORT_SELECTIVE_RESET) {
        /* Selective reset: put CU and RU into idle, clear command */
        s->scb_cmd_lo = 0;
        /* Optionally reset other state if needed */
    }
    /* Self-test and other commands not implemented (not needed for probe) */
    s->port = val;
}

static void handle_eeprom_ctrl_write(PCIBaseState *s, uint8_t val)
{
    uint8_t old = s->last_eeprom_ctrl;
    s->last_eeprom_ctrl = val;
    s->eeprom_ctrl_lo = val;  /* update shadow */

    bool cs_old = old & EECS;
    bool cs_new = val & EECS;
    bool clk_old = old & EESK;
    bool clk_new = val & EESK;

    if (!cs_new) {
        /* Chip select de-asserted: end of transaction, reset state */
        s->eeprom_state = 0; /* IDLE */
        s->eeprom_cmd_bit_count = 0;
        s->eeprom_shift_in = 0;
        s->eeprom_output_bit = EEDO; /* default high (pull-up) */
        return;
    }

    /* CS is high */
    if (!cs_old) {
        /* Just asserted: start new transaction */
        s->eeprom_state = 1; /* COMMAND */
        s->eeprom_cmd_bit_count = 0;
        s->eeprom_shift_in = 0;
        s->eeprom_output_bit = EEDO; /* default high */
        s->eeprom_opcode = 0;
        s->eeprom_addr_bit_count = 0;
        s->eeprom_address = 0;
        return;
    }

    /* Rising edge of EESK */
    if (!clk_old && clk_new) {
        /* Shift in EEDI */
        s->eeprom_shift_in = (s->eeprom_shift_in << 1) | ((val & EEDI) ? 1 : 0);
        s->eeprom_cmd_bit_count++;

        int state = s->eeprom_state;
        if (state == 1) { /* COMMAND state */
            if (s->eeprom_cmd_bit_count == 3) {
                /* Received start bit (must be 1) and two opcode bits */
                s->eeprom_opcode = s->eeprom_shift_in & 0x7;
                if (s->eeprom_opcode == 0x6) { /* READ opcode (start=1, opcode=10) */
                    s->eeprom_state = 2; /* ADDRESS */
                    s->eeprom_addr_bit_count = 0;
                    s->eeprom_address = 0;
                } else {
                    /* Unknown opcode, ignore and stay IDLE? We'll treat as error and reset on CS low. */
                    s->eeprom_state = 0; /* IDLE */
                }
            }
        } else if (state == 2) { /* ADDRESS state */
            /* Shift in address bits */
            s->eeprom_address = (s->eeprom_address << 1) | ((s->eeprom_shift_in & 1) ? 1 : 0);
            s->eeprom_addr_bit_count++;
            if (s->eeprom_addr_bit_count == s->eeprom_addr_length) {
                /* Done with address; prepare data output */
                uint16_t rd_addr = s->eeprom_address & (EEPROM_WC - 1);
                s->eeprom_output_data_word = s->eeprom_data[rd_addr];
                s->eeprom_output_bit_index = 15; /* MSB first */
                s->eeprom_state = 3; /* DATA_OUT */
                /* Current clock: output still default (high) */
            }
        } else if (state == 3) { /* DATA_OUT state */
            /* Output next data bit */
            if (s->eeprom_output_bit_index >= 0) {
                uint8_t bit = (s->eeprom_output_data_word >> s->eeprom_output_bit_index) & 1;
                s->eeprom_output_bit = bit ? EEDO : 0;
                s->eeprom_output_bit_index--;
            } else {
                s->eeprom_output_bit = 0;
            }

            /* After finishing output of all 16 bits, detect EEPROM size from word 0 */
            if (s->eeprom_output_bit_index < 0 && s->eeprom_address == 0 && !s->eeprom_size_detected) {
                uint8_t size_bits = (s->eeprom_output_data_word >> 14) & 0x3;
                switch (size_bits) {
                case 0: s->eeprom_addr_length = 6; break; /* 64 words */
                case 1: s->eeprom_addr_length = 7; break; /* 128 words */
                case 2: s->eeprom_addr_length = 8; break; /* 256 words */
                default: s->eeprom_addr_length = 8; break;
                }
                s->eeprom_size_detected = true;
            }
        }
    } else if (clk_old && !clk_new) {
        /* Falling edge: do nothing */
    }
}

static void handle_mdi_ctrl_write(PCIBaseState *s, uint32_t val)
{
    uint32_t addr = (val >> 21) & 0x1F;
    uint32_t reg = (val >> 16) & 0x1F;
    uint32_t dir = val & (MDI_READ | MDI_WRITE);
    uint16_t data = val & 0xFFFF;

    if (addr != 1) {
        /* Non-existent PHY: return 0xFFFF to markers for discovery */
        s->mdi_ctrl = MDI_READY | 0xFFFF;
    } else {
        if (dir & MDI_READ) {
            /* Read from PHY register */
            uint16_t regval = s->phy_regs[reg];
            s->mdi_ctrl = MDI_READY | regval;
        } else if (dir & MDI_WRITE) {
            /* Write to PHY register */
            s->phy_regs[reg] = data;
            s->mdi_ctrl = MDI_READY;
        } else {
            /* Invalid direction, return ready with dummy */
            s->mdi_ctrl = MDI_READY | 0xFFFF;
        }
    }
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case CSR_SCB_STATUS:
        /* Read-only? Driver reads only, but allow write to maybe clear? Ignore. */
        break;
    case CSR_SCB_STAT_ACK:
        /* Write-1-to-clear: clear bits that are set in val */
        s->scb_stat_ack &= ~((uint8_t)val);
        pcibase_update_irq(s);
        break;
    case CSR_SCB_CMD_LO:
        s->scb_cmd_lo = (uint8_t)val;
        /* Command acceptance: could trigger DMA, but not for probe */
        break;
    case CSR_SCB_CMD_HI:
        s->scb_cmd_hi = (uint8_t)val;
        if (val & IRQ_SW_GEN) {
            s->scb_stat_ack |= STAT_ACK_SW_GEN;
        }
        pcibase_update_irq(s);
        break;
    case CSR_SCB_GEN_PTR:
        s->scb_gen_ptr = (uint32_t)val;
        break;
    case CSR_PORT:
        handle_port_write(s, (uint32_t)val);
        break;
    case CSR_FLASH_CTRL:
        s->flash_ctrl = (uint16_t)val;
        break;
    case CSR_EEPROM_CTRL_LO:
        handle_eeprom_ctrl_write(s, (uint8_t)val);
        break;
    case CSR_EEPROM_CTRL_HI:
        s->eeprom_ctrl_hi = (uint8_t)val;
        break;
    case CSR_MDI_CTRL:
        s->mdi_ctrl = (uint32_t)val;
        handle_mdi_ctrl_write(s, (uint32_t)val);
        break;
    case CSR_RX_DMA_COUNT:
        s->rx_dma_count = (uint32_t)val;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad write at offset 0x%"HWADDR_PRIx" value 0x%"PRIx64"\n", __func__, addr, val);
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    (void)opaque;
    uint64_t val = 0;
    /* TODO: Implement PIO reads if needed in Phase 2 */
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    (void)opaque;
    /* TODO: Implement PIO writes if needed in Phase 2 */
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

    /* Reset registers to power-on defaults */
    s->scb_status = 0;
    s->scb_stat_ack = 0;
    s->scb_cmd_lo = 0;
    s->scb_cmd_hi = IRQ_MASK_NONE;  /* interrupts unmasked after reset */
    s->scb_gen_ptr = 0;
    s->port = 0;
    s->flash_ctrl = 0;
    s->eeprom_ctrl_lo = 0;
    s->eeprom_ctrl_hi = 0;
    s->mdi_ctrl = 0;
    s->rx_dma_count = 0;
    s->intr_status = 0;
    s->intr_mask = 0;
    s->status_flags = 0;
    s->reset_count = 0;
    s->pm_state = 0;

    /* Initialize EEPROM contents (valid MAC and checksum) */
    memset(s->eeprom_data, 0, sizeof(s->eeprom_data));
    /* Correct EEPROM layout:
     *   Word 0: ID (0x1452 indicates 64-word, bits 15:14=00)
     *   Words 1-3: MAC address 52:54:00:12:34:56 (little-endian words)
     */
    s->eeprom_data[0] = 0x1452;  /* ID, 64-word EEPROM */
    s->eeprom_data[1] = 0x5452;  /* MAC low word (bytes 0x52, 0x54) */
    s->eeprom_data[2] = 0x1200;  /* MAC mid word (bytes 0x00, 0x12) */
    s->eeprom_data[3] = 0x5634;  /* MAC high word (bytes 0x34, 0x56) */

    /* Compute checksum: sum of words 0..EEPROM_WC-2 equals (0xBABA - word[EEPROM_WC-1]) */
    uint16_t sum = 0;
    for (int i = 0; i < EEPROM_WC - 1; i++) {
        sum += s->eeprom_data[i];
    }
    s->eeprom_data[EEPROM_WC - 1] = 0xBABA - sum;

    /* Reset EEPROM state machine */
    s->last_eeprom_ctrl = 0;
    s->eeprom_shift_in = 0;
    s->eeprom_clk_count = 0;
    s->eeprom_output_bit = 0;
    s->eeprom_output_data_word = 0;
    s->eeprom_output_bit_index = 0;
    s->eeprom_state = 0; /* IDLE */
    s->eeprom_cmd_bit_count = 0;
    s->eeprom_opcode = 0;
    s->eeprom_addr_bit_count = 0;
    s->eeprom_address = 0;
    s->eeprom_addr_length = 8;  /* initially 8-bit addressing */
    s->eeprom_size_detected = false;

    /* Initialize PHY registers for phy_id 1 */
    memset(s->phy_regs, 0, sizeof(s->phy_regs));
    s->phy_regs[MII_BMCR] = BMCR_ANENABLE | BMCR_FULLDPLX; /* autoneg, full-duplex */
    s->phy_regs[MII_BMSR] = BMSR_LSTATUS | BMSR_ANEGCAPABLE; /* link up, autoneg capable */
    s->phy_regs[MII_PHYSID1] = DEFAULT_PHYSID1;
    s->phy_regs[MII_PHYSID2] = DEFAULT_PHYSID2;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_INTEL);
    pci_set_word(pci_conf + PCI_DEVICE_ID, E100_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, E100_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    /* BAR0: Memory-mapped CSR registers */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x1000; /* Typical 4KB MMIO region */
    s->bar_info[0].name = "csr";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No MSI or MSIX init needed, driver uses legacy IRQ */

    /* DMA configuration (TODO: Phase 2) */

    /* Timer configuration (No hardware timers needed) */

    /* Final state initialization - call reset to set defaults */
    pcibase_reset(DEVICE(pdev));
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }

    /* Free buffers, stop timers, etc. (None for now) */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "e100_pci",
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
