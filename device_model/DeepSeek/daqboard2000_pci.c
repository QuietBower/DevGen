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

#define TYPE_PCIBASE_DEVICE "daqboard2000_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_DAQBOARD2000 0x1616
#define PCI_DEVICE_ID_DAQBOARD2000 0x0409
#define PCI_CLASS_DAQBOARD2000 0x1180

/* Register offsets */
#define DB2K_REG_ACQ_CONTROL            0x00
#define DB2K_REG_ACQ_STATUS             0x00
#define DB2K_REG_ACQ_SCAN_LIST_FIFO     0x02
#define DB2K_REG_ACQ_PACER_CLOCK_DIV_LOW 0x04
#define DB2K_REG_ACQ_SCAN_COUNTER       0x08
#define DB2K_REG_ACQ_PACER_CLOCK_DIV_HIGH 0x0a
#define DB2K_REG_ACQ_TRIGGER_COUNT      0x0c
#define DB2K_REG_ACQ_RESULTS_FIFO       0x10
#define DB2K_REG_ACQ_RESULTS_SHADOW     0x14
#define DB2K_REG_ACQ_ADC_RESULT         0x18
#define DB2K_REG_DAC_SCAN_COUNTER       0x1c
#define DB2K_REG_DAC_CONTROL            0x20
#define DB2K_REG_DAC_STATUS             0x20
#define DB2K_REG_DAC_FIFO               0x24
#define DB2K_REG_DAC_PACER_CLOCK_DIV    0x2a
#define DB2K_REG_REF_DACS               0x2c
#define DB2K_REG_DIO_CONTROL            0x30
#define DB2K_REG_P3_HSIO_DATA           0x32
#define DB2K_REG_P3_CONTROL             0x34
#define DB2K_REG_CAL_EEPROM_CONTROL     0x36
#define DB2K_REG_DAC_SETTING(x)         (0x38 + (x) * 2)
#define DB2K_REG_DIO_P2_EXP_IO_8_BIT    0x40
#define DB2K_REG_COUNTER_TIMER_CONTROL  0x80
#define DB2K_REG_COUNTER_INPUT(x)       (0x88 + (x) * 2)
#define DB2K_REG_TIMER_DIV(x)           (0xa0 + (x) * 2)
#define DB2K_REG_DMA_CONTROL            0xb0
#define DB2K_REG_TRIG_CONTROL           0xb2
#define DB2K_REG_CAL_EEPROM             0xb8
#define DB2K_REG_ACQ_DIGITAL_MARK       0xba
#define DB2K_REG_TRIG_DACS              0xbc
#define DB2K_REG_DIO_P2_EXP_IO_16_BIT(x) (0xc0 + (x) * 2)
#define DB2K_REG_CPLD_STATUS            0x1000
#define DB2K_REG_CPLD_WDATA             0x1000

/* PLX chip register */
#define PLX_REG_CNTRL                   0x006c
#define PLX_CNTRL_RESET                 (1U << 30)
#define PLX_CNTRL_EERELOAD              (1U << 29)
#define PLX_CNTRL_USERO                 (1U << 16)
#define PLX_CNTRL_USERI                 (1U << 17)
#define PLX_CNTRL_EEPRESENT             (1U << 28)

/* Register bit definitions inferred from driver usage */
#define DB2K_CPLD_STATUS_INIT           0x0002
#define DB2K_CPLD_STATUS_TXREADY        0x0001
#define DB2K_CPLD_VERSION_MASK          0xF000
#define DB2K_CPLD_VERSION_NEW           0x1000

#define DB2K_REF_DACS_SET               0x8000
#define DB2K_REF_DACS_SELECT_POS_REF    0x0100
#define DB2K_REF_DACS_SELECT_NEG_REF    0x0000

#define DB2K_DAC_STATUS_REF_BUSY        0x0080
#define DB2K_DAC_STATUS_DAC_BUSY(chan)  (1 << (chan))

#define DB2K_ACQ_CONTROL_RESET_SCAN_LIST_FIFO  0x0001
#define DB2K_ACQ_CONTROL_RESET_RESULTS_FIFO    0x0002
#define DB2K_ACQ_CONTROL_RESET_CONFIG_PIPE      0x0004
#define DB2K_ACQ_CONTROL_SEQ_START_SCAN_LIST   0x0010
#define DB2K_ACQ_CONTROL_SEQ_STOP_SCAN_LIST    0x0020
#define DB2K_ACQ_CONTROL_ADC_PACER_ENABLE      0x0100
#define DB2K_ACQ_CONTROL_ADC_PACER_DISABLE     0x0200

#define DB2K_ACQ_STATUS_CONFIG_PIPE_FULL         0x0001
#define DB2K_ACQ_STATUS_LOGIC_SCANNING           0x0004
#define DB2K_ACQ_STATUS_RESULTS_FIFO_HAS_DATA    0x0008

#define DB2K_TRIG_CONTROL_TYPE_ANALOG   0x0000
#define DB2K_TRIG_CONTROL_TYPE_TTL      0x0010
#define DB2K_TRIG_CONTROL_DISABLE       0x0060

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
    uint32_t plx_cntrl;
    uint16_t regs[0x2000 / 2]; /* 16-bit register file */

    /* DMA Context */
    struct {
        dma_addr_t src;
        dma_addr_t dst;
        dma_addr_t cnt;
        uint32_t cmd;
    } dma;

    uint32_t status;
};

/* MMIO/PIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* PLX register (BAR 0) */
    if (addr == PLX_REG_CNTRL) {
        /* Return stored writable bits, force read-only bits */
        val = s->plx_cntrl | PLX_CNTRL_EEPRESENT | PLX_CNTRL_USERI;
        return val;
    }

    /* CPLD status (BAR 2) */
    if (addr == DB2K_REG_CPLD_STATUS) {
        val = DB2K_CPLD_STATUS_INIT | DB2K_CPLD_STATUS_TXREADY | DB2K_CPLD_VERSION_NEW;
        return val;
    }

    /* All other registers are handled by the 16-bit shadow array */
    if (addr < (0x2000) && (addr & 1) == 0) {
        uint16_t reg_val = s->regs[addr >> 1];
        switch (size) {
        case 1:
            val = (addr & 1) ? (reg_val >> 8) : (reg_val & 0xff);
            break;
        case 2:
            val = reg_val;
            break;
        case 4:
            /* 32-bit read: combine two consecutive 16-bit registers */
            if (addr + 2 < 0x2000) {
                val = s->regs[(addr + 2) >> 1];
                val = (val << 16) | reg_val;
            } else {
                val = reg_val;
            }
            break;
        default:
            val = reg_val;
            break;
        }
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t writable_mask;

    /* PLX register (BAR 0) */
    if (addr == PLX_REG_CNTRL) {
        /* Only writable bits: RESET(30), EERELOAD(29), USERO(16) */
        writable_mask = PLX_CNTRL_RESET | PLX_CNTRL_EERELOAD | PLX_CNTRL_USERO;
        s->plx_cntrl = (s->plx_cntrl & ~writable_mask) | (val & writable_mask);
        return;
    }

    /* CPLD write data (BAR 2), no side effect needed */
    if (addr == DB2K_REG_CPLD_WDATA) {
        /* Just consume the write; hardware would send to CPLD */
        return;
    }

    /* All other registers: store in the 16-bit shadow array */
    if (addr < 0x2000 && (addr & 1) == 0) {
        uint16_t reg_val;
        uint32_t index = addr >> 1;

        switch (size) {
        case 1:
            reg_val = s->regs[index];
            if (addr & 1)
                reg_val = (reg_val & 0x00ff) | ((val & 0xff) << 8);
            else
                reg_val = (reg_val & 0xff00) | (val & 0xff);
            s->regs[index] = reg_val;
            break;
        case 2:
            s->regs[index] = (uint16_t)val;
            break;
        case 4:
            s->regs[index] = (uint16_t)(val & 0xffff);
            if (index + 1 < (0x2000 / 2))
                s->regs[index + 1] = (uint16_t)((val >> 16) & 0xffff);
            break;
        default:
            break;
        }
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* No PIO */
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* No PIO */
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

    /* Reset all state */
    s->plx_cntrl = 0; /* writable bits cleared; read-only forced in reads */
    memset(s->regs, 0, sizeof(s->regs));
    s->status = 0;
    s->intr_status = 0;
    s->intr_mask = 0;
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

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_DAQBOARD2000 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_DAQBOARD2000 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_DAQBOARD2000 );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = 0x100, .name = "plx" };
    s->bar_info[1] = (BARInfo){ .index = 2, .type = BAR_TYPE_MMIO, .size = 0x2000, .name = "daq" };
    s->num_bars = 2;
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No DMA or IRQ initialization needed */
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

    /* No DMA or other resources to uninit */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "daqboard2000_pci",
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