/*
 * QEMU model for Ralink RT2400 PCI Wireless Adapter
 * Based on driver analysis from rt2400pci.c
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

#define TYPE_PCIBASE_DEVICE "rt2400pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI Identifiers from driver */
#define PCI_VENDOR_ID_RALINK 0x1814
#define PCI_DEVICE_ID_RT2400 0x0101
#define PCI_CLASS_WIRELESS   0x0280

/* Register offsets (from driver source) */
#define CSR0                 0x0000
#define CSR1                 0x0004
#define CSR3                 0x000c
#define CSR5                 0x0014
#define CSR7                 0x001c
#define CSR8                 0x0020
#define CSR9                 0x0024
#define CSR11                0x002c
#define CSR12                0x0030
#define CSR14                0x0038
#define CSR15                0x003c
#define CSR16                0x0040
#define CSR17                0x0044
#define CSR18                0x0048
#define CSR19                0x004c
#define CSR20                0x0050
#define CSR21                0x0054
#define TXCSR0               0x0060
#define TXCSR1               0x0064
#define TXCSR2               0x0068
#define TXCSR3               0x006c
#define TXCSR4               0x0070
#define TXCSR5               0x0074
#define TXCSR6               0x0078
#define RXCSR0               0x0080
#define RXCSR1               0x0084
#define RXCSR2               0x0088
#define RXCSR3               0x0090
#define ARCSR0               0x0098
#define ARCSR1               0x009c
#define ARCSR2               0x013c
#define ARCSR3               0x0140
#define ARCSR4               0x0144
#define ARCSR5               0x0148
#define CNT0                 0x00a0
#define CNT3                 0x00b8
#define CNT4                 0x00bc
#define PWRCSR0              0x00c4
#define PWRCSR1              0x00d8
#define PSCSR0               0x00c8
#define PSCSR1               0x00cc
#define PSCSR2               0x00d0
#define PSCSR3               0x00d4
#define MACCSR0              0x00e0
#define MACCSR1              0x00e4
#define MACCSR2              0x0134
#define RALINKCSR            0x00e8
#define BBPCSR               0x00f0
#define RFCSR                0x00f4
#define LEDCSR               0x00f8
#define GPIOCSR              0x0120
#define BCNCSR1              0x0130
#define TIMECSR              0x00dc

#define CSR_REG_SIZE         0x014c
#define EEPROM_SIZE            0x0100
#define NUM_TX_QUEUES          2
#define DATA_FRAME_SIZE       2432
#define MGMT_FRAME_SIZE        256
#define PREAMBLE               144
#define ACK_SIZE               14
#define DEFAULT_RSSI_OFFSET    100

/* Enum definitions from driver */
enum led_type {
	LED_TYPE_RADIO,
	LED_TYPE_ASSOC,
	LED_TYPE_ACTIVITY,
	LED_TYPE_QUALITY,
};

enum dev_state {
	STATE_DEEP_SLEEP = 0,
	STATE_SLEEP = 1,
	STATE_STANDBY = 2,
	STATE_AWAKE = 3,
	STATE_RADIO_ON,
	STATE_RADIO_OFF,
	STATE_RADIO_IRQ_ON,
	STATE_RADIO_IRQ_OFF,
};

/* Not all used in device, but needed for completeness */
enum data_queue_qid {
	QID_AC_VO = 0,
	QID_AC_VI = 1,
	QID_AC_BE = 2,
	QID_AC_BK = 3,
	QID_HCCA = 4,
	QID_MGMT = 13,
	QID_RX = 14,
	QID_OTHER = 15,
	QID_BEACON,
	QID_ATIM,
};

/* EEPROM 93cx6 state machine */
typedef enum {
    EEPROM_CS_LOW,
    EEPROM_CS_HIGH_START,
    EEPROM_CS_HIGH_OPCODE,
    EEPROM_CS_HIGH_ADDRESS,
    EEPROM_CS_HIGH_DATA
} EEPROMState;

/* BBP register count */
#define BBP_SIZE 32

/* RF register count */
#define RF_SIZE 3

/* Hardware register field definitions (guessed from common Ralink patterns) */
/* CSR7 interrupt sources (guessed) */
#define CSR7_TBCN_EXPIRE      (1 << 0)
#define CSR7_RXDONE           (1 << 1)
#define CSR7_TXDONE_ATIMRING  (1 << 2)
#define CSR7_TXDONE_PRIORING  (1 << 3)
#define CSR7_TXDONE_TXRING    (1 << 4)

/* CSR8 interrupt mask (bits same as CSR7) */
#define CSR8_TBCN_EXPIRE      CSR7_TBCN_EXPIRE
#define CSR8_RXDONE           CSR7_RXDONE
#define CSR8_TXDONE_ATIMRING  CSR7_TXDONE_ATIMRING
#define CSR8_TXDONE_PRIORING  CSR7_TXDONE_PRIORING
#define CSR8_TXDONE_TXRING    CSR7_TXDONE_TXRING

/* BBPCSR (0x00f0) field positions (guess) */
#define BBPCSR_VALUE          0x000000FF
#define BBPCSR_REGNUM         0x0000FF00
#define BBPCSR_BUSY           0x00010000
#define BBPCSR_WRITE_CONTROL  0x00020000

/* RFCSR (0x00f4) field positions (guess) */
#define RFCSR_VALUE           0x000FFFFF
#define RFCSR_NUMBER_OF_BITS  0x1F000000
#define RFCSR_IF_SELECT       0x20000000
#define RFCSR_BUSY            0x40000000

/* CSR21 (EEPROM) field positions (guess) */
#define CSR21_EEPROM_DATA_IN      (1 << 0)
#define CSR21_EEPROM_DATA_OUT     (1 << 1)
#define CSR21_EEPROM_DATA_CLOCK   (1 << 2)
#define CSR21_EEPROM_CHIP_SELECT  (1 << 3)
#define CSR21_TYPE_93C46         (1 << 4)

/* BAR types */
typedef enum {
    BAR_TYPE_NONE,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM
} BARType;

typedef struct {
    int index;
    BARType type;
    hwaddr size;
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
    uint32_t intr_status;   /* CSR7 */
    uint32_t intr_mask;     /* CSR8 */

    /* Hardware Register Shadows */
    uint32_t regs[CSR_REG_SIZE / sizeof(uint32_t)];

    /* BBP registers */
    uint8_t bbp[BBP_SIZE];
    /* RF registers */
    uint32_t rf[RF_SIZE];

    /* EEPROM data (256 x 16-bit) */
    uint16_t eeprom[EEPROM_SIZE];

    /* EEPROM state machine */
    EEPROMState eeprom_state;
    uint32_t eeprom_shift_reg;
    int eeprom_bit_count;
    uint8_t eeprom_word_address;
    bool eeprom_is_read;
    bool eeprom_last_clock;

    /* DMA ring base addresses (not used in probe, but stored) */
    dma_addr_t tx_ring_dma;
    dma_addr_t rx_ring_dma;
    dma_addr_t beacon_ring_dma;
    dma_addr_t atim_ring_dma;
    dma_addr_t prio_ring_dma;
};

/* Internal helper: update IRQ according to CSR7 and CSR8 */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t pending = s->intr_status & ~s->intr_mask;
    if (pending) {
        if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!msi_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

/* EEPROM state machine helper: handle clock edge */
static void eeprom_process(PCIBaseState *s)
{
    bool cs = s->regs[CSR21 / 4] & CSR21_EEPROM_CHIP_SELECT;
    bool clk = s->regs[CSR21 / 4] & CSR21_EEPROM_DATA_CLOCK;
    bool di = s->regs[CSR21 / 4] & CSR21_EEPROM_DATA_IN;

    if (!cs) {
        /* Chip select low, reset state */
        s->eeprom_state = EEPROM_CS_LOW;
        s->eeprom_bit_count = 0;
        /* data_out should be high impedance? We'll set to 0 */
        s->regs[CSR21 / 4] &= ~CSR21_EEPROM_DATA_OUT;
        return;
    }

    /* Rising edge detection */
    if (clk && !s->eeprom_last_clock) {
        switch (s->eeprom_state) {
        case EEPROM_CS_LOW:
        case EEPROM_CS_HIGH_START:
            /* First bit after CS high is start bit, must be 1 */
            if (di == 1) {
                s->eeprom_state = EEPROM_CS_HIGH_OPCODE;
                s->eeprom_bit_count = 0;
                s->eeprom_shift_reg = 0;
            } else {
                /* Stay in start? */
            }
            break;
        case EEPROM_CS_HIGH_OPCODE:
            s->eeprom_shift_reg = (s->eeprom_shift_reg << 1) | di;
            s->eeprom_bit_count++;
            if (s->eeprom_bit_count == 2) {
                /* Opcode received: 10 = READ, 01 = WRITE, 00 = EWDS, 11 = ERAL etc. */
                if (s->eeprom_shift_reg == 2) { /* 10 = READ */
                    s->eeprom_is_read = true;
                } else {
                    s->eeprom_is_read = false;
                    s->eeprom_state = EEPROM_CS_HIGH_DATA; /* skip address */
                    s->eeprom_bit_count = 0;
                    s->eeprom_shift_reg = 0;
                    break;
                }
                s->eeprom_state = EEPROM_CS_HIGH_ADDRESS;
                s->eeprom_bit_count = 0;
                s->eeprom_shift_reg = 0;
            }
            break;
        case EEPROM_CS_HIGH_ADDRESS:
            s->eeprom_shift_reg = (s->eeprom_shift_reg << 1) | di;
            s->eeprom_bit_count++;
            if (s->eeprom_bit_count == 6 ||
                (s->eeprom_bit_count == 8 && !(s->regs[CSR21 / 4] & CSR21_TYPE_93C46))) {
                s->eeprom_word_address = s->eeprom_shift_reg;
                if (s->eeprom_is_read) {
                    s->eeprom_state = EEPROM_CS_HIGH_DATA;
                    s->eeprom_bit_count = 0;
                    /* Preload shift register with the first data bit? We'll output MSB first after dummy bit? */
                    s->eeprom_shift_reg = s->eeprom[s->eeprom_word_address];
                } else {
                    s->eeprom_state = EEPROM_CS_HIGH_DATA;
                    s->eeprom_bit_count = 0;
                    s->eeprom_shift_reg = 0;
                }
            }
            break;
        case EEPROM_CS_HIGH_DATA:
            if (s->eeprom_is_read) {
                /* Clock in don't care, but we need to shift out data? Actually, on read, data_out changes on falling edge? */
                /* We'll handle data_out on falling edge. */
                s->eeprom_bit_count++;
                if (s->eeprom_bit_count == 17) { /* 1 start+dummy? Actually 93cx6 read: after address, a dummy 0 bit then 16 data bits MSB first. */
                    /* Done reading, go back to start? */
                    s->eeprom_state = EEPROM_CS_HIGH_START;
                    s->eeprom_bit_count = 0;
                }
            } else {
                s->eeprom_shift_reg = (s->eeprom_shift_reg << 1) | di;
                s->eeprom_bit_count++;
                if (s->eeprom_bit_count == 16) {
                    /* Write completed, store to eeprom */
                    s->eeprom[s->eeprom_word_address] = s->eeprom_shift_reg & 0xFFFF;
                    s->eeprom_state = EEPROM_CS_HIGH_START;
                    s->eeprom_bit_count = 0;
                }
            }
            break;
        }
    }

    /* Falling edge detection */
    if (!clk && s->eeprom_last_clock) {
        /* On falling edge, output data bit for READ */
        if (s->eeprom_state == EEPROM_CS_HIGH_DATA && s->eeprom_is_read) {
            /* The data output should be the MSB of the current word, shifted out. */
            /* We'll output the bit based on current position */
            if (s->eeprom_bit_count >= 1 && s->eeprom_bit_count <= 16) {
                int shift = 16 - s->eeprom_bit_count;
                uint32_t bit = (s->eeprom_shift_reg >> shift) & 1;
                if (bit) {
                    s->regs[CSR21 / 4] |= CSR21_EEPROM_DATA_OUT;
                } else {
                    s->regs[CSR21 / 4] &= ~CSR21_EEPROM_DATA_OUT;
                }
            } else {
                s->regs[CSR21 / 4] &= ~CSR21_EEPROM_DATA_OUT;
            }
        }
    }

    s->eeprom_last_clock = clk;
}

/* MMIO read handler */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    uint32_t reg_offset = addr & 0xFFFF;

    if (reg_offset >= CSR_REG_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: read out of bounds: addr=0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        return 0xFFFFFFFF;
    }

    if (reg_offset == CSR7) {
        val = s->intr_status;
    } else if (reg_offset == CSR8) {
        val = s->intr_mask;
    } else if (reg_offset == CSR21) {
        /* On read, return the current state of CSR21 lines */
        val = s->regs[CSR21 / 4];
    } else if (reg_offset == BBPCSR) {
        /* Return current BBPCSR value */
        val = s->regs[BBPCSR / 4];
    } else if (reg_offset == RFCSR) {
        val = s->regs[RFCSR / 4];
    } else {
        val = s->regs[reg_offset / 4];
    }

    return val;
}

/* MMIO write handler */
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t reg_offset = addr & 0xFFFF;
    uint32_t old_val;

    if (reg_offset >= CSR_REG_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: write out of bounds: addr=0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        return;
    }

    if (reg_offset == CSR7) {
        /* Write-1-to-clear: clear bits set in value */
        s->intr_status &= ~((uint32_t)val);
        pcibase_update_irq(s);
        return;
    } else if (reg_offset == CSR8) {
        s->intr_mask = (uint32_t)val;
        pcibase_update_irq(s);
        return;
    } else if (reg_offset == CSR21) {
        old_val = s->regs[CSR21 / 4];
        s->regs[CSR21 / 4] = (uint32_t)val;
        /* Process EEPROM state machine if clock or CS changed */
        if ((old_val ^ val) & (CSR21_EEPROM_DATA_CLOCK | CSR21_EEPROM_CHIP_SELECT)) {
            eeprom_process(s);
        }
        return;
    } else if (reg_offset == BBPCSR) {
        /* BBP command */
        uint32_t bbp_reg = val;
        s->regs[BBPCSR / 4] = bbp_reg;
        if (bbp_reg & BBPCSR_BUSY) {
            uint8_t regnum = (bbp_reg & BBPCSR_REGNUM) >> 8;
            regnum &= (BBP_SIZE - 1); /* Ensure index within bounds */
            if (bbp_reg & BBPCSR_WRITE_CONTROL) {
                /* Write BBP register */
                s->bbp[regnum] = bbp_reg & BBPCSR_VALUE;
            } else {
                /* Read BBP register */
                s->regs[BBPCSR / 4] &= ~BBPCSR_VALUE;
                s->regs[BBPCSR / 4] |= s->bbp[regnum];
            }
            /* Clear busy bit immediately */
            s->regs[BBPCSR / 4] &= ~BBPCSR_BUSY;
        }
        return;
    } else if (reg_offset == RFCSR) {
        /* RF command */
        uint32_t rf_reg = val;
        s->regs[RFCSR / 4] = rf_reg;
        if (rf_reg & RFCSR_BUSY) {
            /* Immediately clear busy for now; actual RF write not implemented */
            s->regs[RFCSR / 4] &= ~RFCSR_BUSY;
        }
        return;
    }

    /* Default: just store the value in shadow register */
    s->regs[reg_offset / 4] = (uint32_t)val;
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

/* BAR registration helper – simplified for MMIO only */
static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type != BAR_TYPE_MMIO) {
        /* For now, only MMIO BARs are implemented */
        return;
    }
    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];
    memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, aligned_size);
    pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);

    pci_device_reset(PCI_DEVICE(dev));

    /* Reset registers to default values (mimic init_registers) */
    memset(s->regs, 0, sizeof(s->regs));
    s->intr_status = 0;
    s->intr_mask = 0xFFFFFFFF; /* All masked initially */

    /* Initialize CSR1 to indicate host ready after reset? */
    /* Driver writes CSR1 during init, we'll just let shadow reflect writes */

    /* Initialize EEPROM data with values that satisfy probe (pre-loaded) */
    memset(s->eeprom, 0, sizeof(s->eeprom));
    /* MAC address starting at word 2 */
    s->eeprom[2] = 0xaaaa;
    s->eeprom[3] = 0xbbbb;
    s->eeprom[4] = 0xcccc;
    /* Antenna word at offset 0x0b (11) */
    s->eeprom[11] = 0x0000; /* RF_TYPE=0 (RF2420), TX=0, RX=0, LED_MODE=0, HARDWARE_RADIO=0 */
    /* EEPROM BBP values starting at 0x0c (12) (7 words) */
    /* Set to zero for now; driver will write them to BBP registers */
    /* Tx power calibration for channels 1-14, start at 0x13 (19) */
    for (int i = 0; i < 14; i++) {
        s->eeprom[19 + i] = 0x2020; /* MAX_TXPOWER scale */
    }

    /* Reset BBP array with init values from driver (rt2400pci_init_bbp) */
    memset(s->bbp, 0, sizeof(s->bbp));
    s->bbp[1] = 0x00;
    s->bbp[3] = 0x27;
    s->bbp[4] = 0x08;
    s->bbp[10] = 0x0f;
    s->bbp[15] = 0x72;
    s->bbp[16] = 0x74;
    s->bbp[17] = 0x20;
    s->bbp[18] = 0x72;
    s->bbp[19] = 0x0b;
    s->bbp[20] = 0x00;
    s->bbp[28] = 0x11;
    s->bbp[29] = 0x04;
    s->bbp[30] = 0x21;
    s->bbp[31] = 0x00;

    /* Reset RF array */
    memset(s->rf, 0, sizeof(s->rf));

    /* Reset EEPROM state */
    s->eeprom_state = EEPROM_CS_LOW;
    s->eeprom_shift_reg = 0;
    s->eeprom_bit_count = 0;
    s->eeprom_last_clock = 0;
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_RALINK);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_RT2400);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_WIRELESS);
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
    s->bar_info[0].size = CSR_REG_SIZE;
    s->bar_info[0].name = "rt2400pci-mmio";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize shadow regs */
    memset(s->regs, 0, sizeof(s->regs));
    s->intr_status = 0;
    s->intr_mask = 0xFFFFFFFF;

    /* Initialize EEPROM data with valid values */
    memset(s->eeprom, 0, sizeof(s->eeprom));
    /* MAC address starting at word 2 */
    s->eeprom[2] = 0xaaaa;
    s->eeprom[3] = 0xbbbb;
    s->eeprom[4] = 0xcccc;
    /* Antenna word at offset 0x0b (11) */
    s->eeprom[11] = 0x0000; /* RF_TYPE=0 (RF2420), TX=0, RX=0, LED_MODE=0, HARDWARE_RADIO=0 */
    /* EEPROM BBP values starting at 0x0c (12) (7 words) */
    /* Set to zero for now; driver will write them to BBP registers */
    /* Tx power calibration for channels 1-14, start at 0x13 (19) */
    for (int i = 0; i < 14; i++) {
        s->eeprom[19 + i] = 0x2020; /* MAX_TXPOWER scale */
    }

    /* Reset state (trigger initial reset logic) */
    pcibase_reset(DEVICE(s));
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, 0);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

static const VMStateDescription vmstate_pcibase = {
    .name = "rt2400pci_pci",
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
