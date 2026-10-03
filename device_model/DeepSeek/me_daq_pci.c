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

#define TYPE_PCIBASE_DEVICE "me_daq_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI_VENDOR_ID_MEILHAUS 0x1402
#define PCI_DEVICE_ID_ME2600 0x2600

#define ME_CTRL1_REG            0x00
#define ME_CTRL1_INT_ENA        BIT(15)
#define ME_CTRL1_COUNTER_B_IRQ  BIT(12)
#define ME_CTRL1_COUNTER_A_IRQ  BIT(11)
#define ME_CTRL1_CHANLIST_READY_IRQ BIT(10)
#define ME_CTRL1_EXT_IRQ        BIT(9)
#define ME_CTRL1_ADFIFO_HALFFULL_IRQ BIT(8)
#define ME_CTRL1_SCAN_COUNT_ENA BIT(5)
#define ME_CTRL1_SIMULTANEOUS_ENA BIT(4)
#define ME_CTRL1_TRIGGER_FALLING_EDGE BIT(3)
#define ME_CTRL1_CONTINUOUS_MODE BIT(2)
#define ME_CTRL1_ADC_MODE(x)    (((x) & 0x3) << 0)
#define ME_CTRL1_ADC_MODE_DISABLE  ME_CTRL1_ADC_MODE(0)
#define ME_CTRL1_ADC_MODE_SOFT_TRIG ME_CTRL1_ADC_MODE(1)
#define ME_CTRL1_ADC_MODE_SCAN_TRIG ME_CTRL1_ADC_MODE(2)
#define ME_CTRL1_ADC_MODE_EXT_TRIG ME_CTRL1_ADC_MODE(3)
#define ME_CTRL1_ADC_MODE_MASK  ME_CTRL1_ADC_MODE(3)

#define ME_CTRL2_REG            0x02
#define ME_CTRL2_ADFIFO_ENA     BIT(10)
#define ME_CTRL2_CHANLIST_ENA   BIT(9)
#define ME_CTRL2_PORT_B_ENA     BIT(7)
#define ME_CTRL2_PORT_A_ENA     BIT(6)
#define ME_CTRL2_COUNTER_B_ENA  BIT(4)
#define ME_CTRL2_COUNTER_A_ENA  BIT(3)
#define ME_CTRL2_DAC_ENA        BIT(1)
#define ME_CTRL2_BUFFERED_DAC   BIT(0)

#define ME_STATUS_REG           0x04
#define ME_STATUS_COUNTER_B_IRQ BIT(12)
#define ME_STATUS_COUNTER_A_IRQ BIT(11)
#define ME_STATUS_CHANLIST_READY_IRQ BIT(10)
#define ME_STATUS_EXT_IRQ       BIT(9)
#define ME_STATUS_ADFIFO_HALFFULL_IRQ BIT(8)
#define ME_STATUS_ADFIFO_FULL   BIT(4)
#define ME_STATUS_ADFIFO_HALFFULL BIT(3)
#define ME_STATUS_ADFIFO_EMPTY  BIT(2)
#define ME_STATUS_CHANLIST_FULL BIT(1)
#define ME_STATUS_FST_ACTIVE    BIT(0)

#define ME_DIO_PORT_A_REG       0x06
#define ME_DIO_PORT_B_REG       0x08
#define ME_TIMER_DATA_REG(x)    (0x0a + ((x) * 2))
#define ME_AI_FIFO_REG          0x10
#define ME_AI_FIFO_CHANLIST_DIFF BIT(7)
#define ME_AI_FIFO_CHANLIST_UNIPOLAR BIT(6)
#define ME_AI_FIFO_CHANLIST_GAIN(x) (((x) & 0x3) << 4)
#define ME_AI_FIFO_CHANLIST_CHAN(x) (((x) & 0xf) << 0)

#define ME_DAC_CTRL_REG         0x12
#define ME_DAC_CTRL_BIPOLAR(x)  BIT(7 - ((x) & 0x3))
#define ME_DAC_CTRL_GAIN(x)     BIT(11 - ((x) & 0x3))
#define ME_DAC_CTRL_MASK(x)     (ME_DAC_CTRL_BIPOLAR(x) | ME_DAC_CTRL_GAIN(x))

#define ME_AO_DATA_REG(x)       (0x14 + ((x) * 2))
#define ME_COUNTER_ENDDATA_REG(x) (0x1c + ((x) * 2))
#define ME_COUNTER_STARTDATA_REG(x) (0x20 + ((x) * 2))
#define ME_COUNTER_VALUE_REG(x)  (0x20 + ((x) * 2))

#define PLX9052_INTCSR          0x4c
#define PLX9052_INTCSR_PCIENAB  BIT(6)
#define PLX9052_INTCSR_LI2STAT  BIT(5)
#define PLX9052_INTCSR_LI1ENAB  BIT(0)
#define PLX9052_INTCSR_LI1POL   BIT(1)

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
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint16_t ctrl1;
    uint16_t ctrl2;
    uint16_t dac_ctrl;
    uint16_t status;
    uint16_t dio_port_a;
    uint16_t dio_port_b;
    uint16_t timer_data[2];
    uint16_t ai_fifo;
    uint16_t ao_data[4];
    uint16_t counter_enddata[2];
    uint16_t counter_startdata[2];

    // PLX 9052 registers (in BAR0)
    uint32_t plx_intcsr;
};

/* MMIO/PIO Handlers generated during Behavioral Modeling */

static uint64_t pcibase_plx_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr == PLX9052_INTCSR && size == 4) {
        val = s->plx_intcsr;
    } else {
        qemu_log_mask(LOG_UNIMP, "me_daq: PLX MMIO read at 0x%" HWADDR_PRIx "\n", addr);
    }
    return val;
}

static void pcibase_plx_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr == PLX9052_INTCSR && size == 4) {
        s->plx_intcsr = val;
    } else {
        qemu_log_mask(LOG_UNIMP, "me_daq: PLX MMIO write at 0x%" HWADDR_PRIx "\n", addr);
    }
}

static uint64_t pcibase_daq_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case 0x00:
        if (size == 2) val = s->ctrl1;
        break;
    case 0x02:
        if (size == 2) val = s->ctrl2;
        break;
    case 0x04:
        if (size == 2) val = s->status;
        break;
    case 0x06:
        if (size == 2) val = s->dio_port_a;
        break;
    case 0x08:
        if (size == 2) val = s->dio_port_b;
        break;
    case 0x0a:
        if (size == 2) val = s->timer_data[0];
        break;
    case 0x0c:
        if (size == 2) val = s->timer_data[1];
        break;
    case 0x10:
        if (size == 2) {
            val = s->ai_fifo;
            // After reading AI_FIFO, set EMPTY flag
            s->status |= ME_STATUS_ADFIFO_EMPTY;
        }
        break;
    case 0x12:
        if (size == 2) val = s->dac_ctrl;
        break;
    case 0x14: case 0x16: case 0x18: case 0x1a:
        if (size == 2) {
            int idx = (addr - 0x14) / 2;
            if (idx < 4) val = s->ao_data[idx];
        }
        break;
    case 0x1c: case 0x1e:
        if (size == 2) {
            int idx = (addr - 0x1c) / 2;
            if (idx < 2) val = s->counter_enddata[idx];
        }
        break;
    case 0x20: case 0x22:
        if (size == 2) {
            int idx = (addr - 0x20) / 2;
            if (idx < 2) val = s->counter_startdata[idx];
        }
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "me_daq: DAQ MMIO read at 0x%" HWADDR_PRIx "\n", addr);
        break;
    }
    return val;
}

static void pcibase_daq_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case 0x00:
        if (size == 2) {
            s->ctrl1 = val;
        } else if (size == 1) {
            // Firmware download: ignore
            qemu_log_mask(LOG_UNIMP, "me_daq: firmware byte write ignored\n");
        } else {
            qemu_log_mask(LOG_UNIMP, "me_daq: DAQ MMIO write at 0x00 with size %u\n", size);
        }
        break;
    case 0x02:
        if (size == 2) s->ctrl2 = val;
        break;
    case 0x04:
        if (size == 2) {
            // Writing to status register: clear interrupt bits that are set in val
            s->status &= ~(val & (ME_STATUS_COUNTER_B_IRQ | ME_STATUS_COUNTER_A_IRQ |
                                  ME_STATUS_CHANLIST_READY_IRQ | ME_STATUS_EXT_IRQ |
                                  ME_STATUS_ADFIFO_HALFFULL_IRQ));
        }
        break;
    case 0x06:
        if (size == 2) s->dio_port_a = val;
        break;
    case 0x08:
        if (size == 2) s->dio_port_b = val;
        break;
    case 0x0a:
        if (size == 2) s->timer_data[0] = val;
        break;
    case 0x0c:
        if (size == 2) s->timer_data[1] = val;
        break;
    case 0x10:
        if (size == 2) {
            s->ai_fifo = val;
        }
        break;
    case 0x12:
        if (size == 2) s->dac_ctrl = val;
        break;
    case 0x14: case 0x16: case 0x18: case 0x1a:
        if (size == 2) {
            int idx = (addr - 0x14) / 2;
            if (idx < 4) s->ao_data[idx] = val;
        }
        break;
    case 0x1c: case 0x1e:
        if (size == 2) {
            int idx = (addr - 0x1c) / 2;
            if (idx < 2) s->counter_enddata[idx] = val;
        }
        break;
    case 0x20: case 0x22:
        if (size == 2) {
            int idx = (addr - 0x20) / 2;
            if (idx < 2) s->counter_startdata[idx] = val;
        }
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "me_daq: DAQ MMIO write at 0x%" HWADDR_PRIx "\n", addr);
        break;
    }
}

static const MemoryRegionOps pcibase_plx_mmio_ops = {
    .read = pcibase_plx_mmio_read,
    .write = pcibase_plx_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

static const MemoryRegionOps pcibase_daq_mmio_ops = {
    .read = pcibase_daq_mmio_read,
    .write = pcibase_daq_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Revert registers to power-on defaults */
    s->ctrl1 = 0;
    s->ctrl2 = 0;
    s->dac_ctrl = 0;
    s->status = ME_STATUS_ADFIFO_EMPTY;
    s->dio_port_a = 0;
    s->dio_port_b = 0;
    s->timer_data[0] = 0;
    s->timer_data[1] = 0;
    s->ai_fifo = 0;
    s->ao_data[0] = 0;
    s->ao_data[1] = 0;
    s->ao_data[2] = 0;
    s->ao_data[3] = 0;
    s->counter_enddata[0] = 0;
    s->counter_enddata[1] = 0;
    s->counter_startdata[0] = 0;
    s->counter_startdata[1] = 0;
    s->plx_intcsr = 0x60;  // PLX9052 Interrupt Control/Status: set PCIENAB and LI2STAT to indicate device is ready, bypass firmware load
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_MEILHAUS);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_ME2600);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0800);  // PCI class: other (typical for DAQ devices)
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    // BAR0: PLX registers
    memory_region_init_io(&s->bar_regions[0], OBJECT(s), &pcibase_plx_mmio_ops, s, "plx_regs", 0x100);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_regions[0]);
    // BAR1: unused
    // BAR2: DAQ registers
    memory_region_init_io(&s->bar_regions[2], OBJECT(s), &pcibase_daq_mmio_ops, s, "me_daq_regs", 0x1000);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_regions[2]);

    /* MSI/MSI-X not used by driver */
    /* DMA not used */
    /* Timer not used */
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

    /* Free buffers, stop timers, etc. - not used */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "me_daq_pci",
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
