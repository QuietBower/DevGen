/*
 * QEMU PCI device model for Ralink RT2500PCI wireless adapter.
 * Based on Linux driver rt2500pci.c register definitions and EEPROM bitbanging.
 * QEMU 8.2.10 compatible.
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

#define TYPE_PCIBASE_DEVICE "rt2500pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Vendor/Device IDs from driver */
#define VENDOR_ID 0x1814
#define DEVICE_ID 0x0201
#define CLASS_ID 0x0280

/* Register offsets from driver */
#define CSR0                    0x0000
#define CSR1                    0x0004
#define CSR3                    0x000c
#define CSR5                    0x0014
#define CSR7                    0x001c
#define CSR8                    0x0020
#define CSR9                    0x0024
#define CSR11                   0x002c
#define CSR12                   0x0030
#define CSR14                   0x0038
#define CSR15                   0x003c
#define CSR16                   0x0040
#define CSR17                   0x0044
#define CSR18                   0x0048
#define CSR19                   0x004c
#define CSR20                   0x0050
#define CSR21                   0x0054
#define BBPCSR                  0x00f0
#define RFCSR                   0x00f4
#define LEDCSR                  0x00f8
#define GPIOCSR                 0x0120
#define CNT0                    0x00a0
#define CNT3                    0x00b8
#define CNT4                    0x00bc
#define RXCSR0                  0x0080
#define RXCSR1                  0x0084
#define RXCSR2                  0x0088
#define RXCSR3                  0x0090
#define TXCSR0                  0x0060
#define TXCSR1                  0x0064
#define TXCSR2                  0x0068
#define TXCSR3                  0x006c
#define TXCSR4                  0x0070
#define TXCSR5                  0x0074
#define TXCSR6                  0x0078
#define TXCSR8                  0x0098
#define PCICSR                  0x008c
#define MACCSR0                 0x00e0
#define MACCSR1                 0x00e4
#define MACCSR2                 0x0134
#define RALINKCSR               0x00e8
#define PWRCSR0                 0x00c4
#define PWRCSR1                 0x00d8
#define PSCSR0                  0x00c8
#define PSCSR1                  0x00cc
#define PSCSR2                  0x00d0
#define PSCSR3                  0x00d4
#define TIMECSR                 0x00dc
#define TESTCSR                 0x0138
#define ARCSR1                  0x009c
#define ARCSR2                  0x013c
#define ARCSR3                  0x0140
#define ARCSR4                  0x0144
#define ARCSR5                  0x0148
#define ARTCSR0                 0x014c
#define ARTCSR1                 0x0150
#define ARTCSR2                 0x0154
#define BBPCSR1                 0x015c
#define TXACKCSR0               0x0110
#define BCNCSR1                 0x0130

/* Register space sizes */
#define CSR_REG_SIZE            0x0174
#define CSR_REG_BASE            0x0000
#define BBP_SIZE                0x0040
#define RF_SIZE                 0x0010
#define EEPROM_SIZE             0x0200

/* Number of 32-bit registers in CSR space */
#define CSR_NUM_REGS            (CSR_REG_SIZE / sizeof(uint32_t))  /* 93 */

/* Register field definitions from driver */
#define CSR0_REVISION           0x0000ffff
#define CSR21_EEPROM_DATA_IN     0x00000008
#define CSR21_EEPROM_DATA_OUT    0x00000010
#define CSR21_EEPROM_DATA_CLOCK  0x00000002
#define CSR21_EEPROM_CHIP_SELECT 0x00000004
#define CSR21_TYPE_93C46         0x00000020

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

typedef enum { EEPROM_IDLE, EEPROM_CMD, EEPROM_ADDR, EEPROM_DATA } EEPROMState;

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
    uint32_t csr[CSR_NUM_REGS];

    /* EEPROM state for bitbanging */
    uint8_t eeprom_data[EEPROM_SIZE];
    EEPROMState eeprom_state;
    uint16_t eeprom_cmd;
    uint16_t eeprom_addr;
    int eeprom_phase_bits;
    uint16_t eeprom_read_data;
    int eeprom_data_bits;
    uint32_t eeprom_old_csr21;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    /* Placeholder for IRQ logic */
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    /* Placeholder for DMA logic */
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= CSR_REG_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: read out of bounds: addr=0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        return 0xFFFFFFFF;
    }

    uint32_t reg_index = addr >> 2;

    /* Default: read from shadow array */
    if (reg_index < CSR_NUM_REGS) {
        val = s->csr[reg_index];
    }

    /* Special handling for CSR21 EEPROM interface */
    if (addr == CSR21) {
        uint32_t csr21 = s->csr[reg_index];
        /* Clear the EEPROM_DATA_OUT bit, we will set it from our emulated state */
        csr21 &= ~CSR21_EEPROM_DATA_OUT;
        if (s->eeprom_state == EEPROM_DATA) {
            /* Output the current MSB of eeprom_read_data */
            uint16_t bit = (s->eeprom_read_data >> 15) & 1;
            if (bit) {
                csr21 |= CSR21_EEPROM_DATA_OUT;
            }
        }
        val = csr21;
    }

    /* W1C register: CSR7 (interrupt status) reading does not clear */
    if (addr == CSR7) {
        /* on read, just return value (no auto-clear) */
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= CSR_REG_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: write out of bounds: addr=0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        return;
    }

    uint32_t reg_index = addr >> 2;

    /* Special handling for CSR21: EEPROM bitbanging */
    if (addr == CSR21) {
        uint32_t old_val = s->eeprom_old_csr21;
        uint32_t new_val = val;
        s->eeprom_old_csr21 = new_val;

        uint32_t old_cs = old_val & CSR21_EEPROM_CHIP_SELECT;
        uint32_t new_cs = new_val & CSR21_EEPROM_CHIP_SELECT;
        uint32_t old_clk = old_val & CSR21_EEPROM_DATA_CLOCK;
        uint32_t new_clk = new_val & CSR21_EEPROM_DATA_CLOCK;
        uint32_t di = (new_val & CSR21_EEPROM_DATA_IN) ? 1 : 0;

        if (!new_cs) {
            /* CS low: reset EEPROM state machine */
            s->eeprom_state = EEPROM_IDLE;
        } else if (old_cs) {
            /* CS was and remains high, check clock edges */
            if (!old_clk && new_clk) {
                /* Rising clock edge: sample input, data already output */
                switch (s->eeprom_state) {
                case EEPROM_IDLE:
                    /* Start bit expected (DI=1) */
                    if (di) {
                        s->eeprom_state = EEPROM_CMD;
                        s->eeprom_cmd = 0;
                        s->eeprom_phase_bits = 0;
                    }
                    break;
                case EEPROM_CMD:
                    /* Shift in command bit */
                    s->eeprom_cmd = (s->eeprom_cmd << 1) | di;
                    s->eeprom_phase_bits++;
                    if (s->eeprom_phase_bits == 2) {
                        /* 2-bit opcode received */
                        if (s->eeprom_cmd == 2) { /* READ command (10) */
                            s->eeprom_state = EEPROM_ADDR;
                            s->eeprom_addr = 0;
                            s->eeprom_phase_bits = 0;
                        } else {
                            /* Unsupported command, go idle */
                            s->eeprom_state = EEPROM_IDLE;
                        }
                    }
                    break;
                case EEPROM_ADDR:
                    /* Shift in address bit */
                    s->eeprom_addr = (s->eeprom_addr << 1) | di;
                    s->eeprom_phase_bits++;
                    {
                        /* Determine address bits based on EEPROM type */
                        unsigned addr_bits = (s->csr[21] & CSR21_TYPE_93C46) ? 6 : 8;
                        if (s->eeprom_phase_bits == addr_bits) {
                            /* Transition to data output phase */
                            s->eeprom_state = EEPROM_DATA;
                            /* Load the 16-bit word from eeprom_data array (little-endian byte order) */
                            s->eeprom_read_data = s->eeprom_data[s->eeprom_addr * 2] |
                                                  (s->eeprom_data[s->eeprom_addr * 2 + 1] << 8);
                            s->eeprom_data_bits = 16;
                        }
                    }
                    break;
                case EEPROM_DATA:
                    /* On rising edge, data output is stable (presented on DO line) */
                    break;
                }
            } else if (old_clk && !new_clk) {
                /* Falling clock edge: shift data for next bit */
                if (s->eeprom_state == EEPROM_DATA) {
                    s->eeprom_read_data <<= 1;
                    s->eeprom_data_bits--;
                    if (s->eeprom_data_bits == 0) {
                        /* Word done, return to idle (driver will deassert CS) */
                        s->eeprom_state = EEPROM_IDLE;
                    }
                }
            }
        }
        /* Store the written value (output bits only, DO bit will be overwritten on read) */
        s->csr[reg_index] = new_val;
        return;
    }

    /* CSR7 interrupt status write: clear bits by writing 1 */
    if (addr == CSR7) {
        s->csr[reg_index] &= ~((uint32_t)val);
        return;
    }

    /* Default: store value */
    if (reg_index < CSR_NUM_REGS) {
        s->csr[reg_index] = val;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* PIO not used by this device */
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    /* PIO not used by this device */
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
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

    /* Reset all CSR registers to zero */
    memset(s->csr, 0, sizeof(s->csr));

    /* Set CSR0 with a valid revision in the lower 16 bits */
    s->csr[0] = 0x00000100;

    /* Initialize EEPROM data: set first word (offset 0) to signature 0x2500 */
    memset(s->eeprom_data, 0x00, EEPROM_SIZE);
    s->eeprom_data[0] = 0x00; /* Low byte */
    s->eeprom_data[1] = 0x25; /* High byte -> 0x2500 signature */

    /* Set EEPROM word at antenna offset (0x10) to a valid RF type (RF2522 = 0x0000) */
    s->eeprom_data[0x20] = 0x00; /* Low byte of word 0x10 */
    s->eeprom_data[0x21] = 0x00; /* High byte */

    /* Reset EEPROM interface state */
    s->eeprom_state = EEPROM_IDLE;
    s->eeprom_cmd = 0;
    s->eeprom_addr = 0;
    s->eeprom_phase_bits = 0;
    s->eeprom_read_data = 0;
    s->eeprom_data_bits = 0;
    s->eeprom_old_csr21 = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x1814 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x0201 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0280 );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization: only one MMIO BAR at index 0 */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = CSR_REG_SIZE;
    s->bar_info[0].name = "rt2500pci-mmio";
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
    .name = "rt2500pci_pci",
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
