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

#define TYPE_PCIBASE_DEVICE "me4000_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI_VENDOR_ID_MEILHAUS 0x1402
#define ME4000_DEVICE_ID 0x4650
#define ME4000_CLASS_ID 0xFF00

#define ME4000_AO_CHAN(x)           ((x) * 0x18)
#define ME4000_AO_CTRL_REG(x)       (0x00 + ME4000_AO_CHAN(x))
#define ME4000_AO_CTRL_MODE_0       BIT(0)
#define ME4000_AO_CTRL_MODE_1       BIT(1)
#define ME4000_AO_CTRL_STOP         BIT(2)
#define ME4000_AO_CTRL_ENABLE_FIFO      BIT(3)
#define ME4000_AO_CTRL_ENABLE_EX_TRIG   BIT(4)
#define ME4000_AO_CTRL_EX_TRIG_EDGE     BIT(5)
#define ME4000_AO_CTRL_IMMEDIATE_STOP   BIT(7)
#define ME4000_AO_CTRL_ENABLE_DO        BIT(8)
#define ME4000_AO_CTRL_ENABLE_IRQ       BIT(9)
#define ME4000_AO_CTRL_RESET_IRQ        BIT(10)
#define ME4000_AO_STATUS_REG(x)     (0x04 + ME4000_AO_CHAN(x))
#define ME4000_AO_STATUS_FSM        BIT(0)
#define ME4000_AO_STATUS_FF         BIT(1)
#define ME4000_AO_STATUS_HF         BIT(2)
#define ME4000_AO_STATUS_EF         BIT(3)
#define ME4000_AO_FIFO_REG(x)       (0x08 + ME4000_AO_CHAN(x))
#define ME4000_AO_SINGLE_REG(x)     (0x0c + ME4000_AO_CHAN(x))
#define ME4000_AO_TIMER_REG(x)      (0x10 + ME4000_AO_CHAN(x))

#define ME4000_AI_CTRL_REG          0x74
#define ME4000_AI_STATUS_REG        0x74
#define ME4000_AI_CTRL_MODE_0       BIT(0)
#define ME4000_AI_CTRL_MODE_1       BIT(1)
#define ME4000_AI_CTRL_MODE_2       BIT(2)
#define ME4000_AI_CTRL_SAMPLE_HOLD      BIT(3)
#define ME4000_AI_CTRL_IMMEDIATE_STOP   BIT(4)
#define ME4000_AI_CTRL_STOP         BIT(5)
#define ME4000_AI_CTRL_CHANNEL_FIFO     BIT(6)
#define ME4000_AI_CTRL_DATA_FIFO        BIT(7)
#define ME4000_AI_CTRL_FULLSCALE        BIT(8)
#define ME4000_AI_CTRL_OFFSET           BIT(9)
#define ME4000_AI_CTRL_EX_TRIG_ANALOG   BIT(10)
#define ME4000_AI_CTRL_EX_TRIG          BIT(11)
#define ME4000_AI_CTRL_EX_TRIG_FALLING  BIT(12)
#define ME4000_AI_CTRL_EX_IRQ           BIT(13)
#define ME4000_AI_CTRL_EX_IRQ_RESET     BIT(14)
#define ME4000_AI_CTRL_LE_IRQ           BIT(15)
#define ME4000_AI_CTRL_LE_IRQ_RESET     BIT(16)
#define ME4000_AI_CTRL_HF_IRQ           BIT(17)
#define ME4000_AI_CTRL_HF_IRQ_RESET     BIT(18)
#define ME4000_AI_CTRL_SC_IRQ           BIT(19)
#define ME4000_AI_CTRL_SC_IRQ_RESET     BIT(20)
#define ME4000_AI_CTRL_SC_RELOAD        BIT(21)
#define ME4000_AI_STATUS_EF_CHANNEL     BIT(22)
#define ME4000_AI_STATUS_HF_CHANNEL     BIT(23)
#define ME4000_AI_STATUS_FF_CHANNEL     BIT(24)
#define ME4000_AI_STATUS_EF_DATA        BIT(25)
#define ME4000_AI_STATUS_HF_DATA        BIT(26)
#define ME4000_AI_STATUS_FF_DATA        BIT(27)
#define ME4000_AI_STATUS_LE         BIT(28)
#define ME4000_AI_STATUS_FSM        BIT(29)
#define ME4000_AI_CTRL_EX_TRIG_BOTH     BIT(31)
#define ME4000_AI_CHANNEL_LIST_REG      0x78
#define ME4000_AI_LIST_INPUT_DIFFERENTIAL    BIT(5)
#define ME4000_AI_LIST_RANGE(x)          ((3 - ((x) & 3)) << 6)
#define ME4000_AI_LIST_LAST_ENTRY        BIT(8)
#define ME4000_AI_DATA_REG          0x7c
#define ME4000_AI_CHAN_TIMER_REG        0x80
#define ME4000_AI_CHAN_PRE_TIMER_REG    0x84
#define ME4000_AI_SCAN_TIMER_LOW_REG    0x88
#define ME4000_AI_SCAN_TIMER_HIGH_REG   0x8c
#define ME4000_AI_SCAN_PRE_TIMER_LOW_REG    0x90
#define ME4000_AI_SCAN_PRE_TIMER_HIGH_REG   0x94

#define ME4000_AI_START_REG         0x98
#define ME4000_IRQ_STATUS_REG       0x9c
#define ME4000_IRQ_STATUS_EX        BIT(0)
#define ME4000_IRQ_STATUS_LE        BIT(1)
#define ME4000_IRQ_STATUS_AI_HF     BIT(2)
#define ME4000_IRQ_STATUS_AO_0_HF   BIT(3)
#define ME4000_IRQ_STATUS_AO_1_HF   BIT(4)
#define ME4000_IRQ_STATUS_AO_2_HF   BIT(5)
#define ME4000_IRQ_STATUS_AO_3_HF   BIT(6)
#define ME4000_IRQ_STATUS_SC        BIT(7)

#define ME4000_DIO_PORT_0_REG       0xa0
#define ME4000_DIO_PORT_1_REG       0xa4
#define ME4000_DIO_PORT_2_REG       0xa8
#define ME4000_DIO_PORT_3_REG       0xac
#define ME4000_DIO_DIR_REG          0xb0
#define ME4000_AO_LOADSETREG_XX     0xb4
#define ME4000_DIO_CTRL_REG         0xb8
#define ME4000_DIO_CTRL_MODE_0      BIT(0)
#define ME4000_DIO_CTRL_MODE_1      BIT(1)
#define ME4000_DIO_CTRL_MODE_2      BIT(2)
#define ME4000_DIO_CTRL_MODE_3      BIT(3)
#define ME4000_DIO_CTRL_MODE_4      BIT(4)
#define ME4000_DIO_CTRL_MODE_5      BIT(5)
#define ME4000_DIO_CTRL_MODE_6      BIT(6)
#define ME4000_DIO_CTRL_MODE_7      BIT(7)
#define ME4000_DIO_CTRL_FUNCTION_0  BIT(8)
#define ME4000_DIO_CTRL_FUNCTION_1  BIT(9)
#define ME4000_DIO_CTRL_FIFO_HIGH_0 BIT(10)
#define ME4000_DIO_CTRL_FIFO_HIGH_1 BIT(11)
#define ME4000_DIO_CTRL_FIFO_HIGH_2 BIT(12)
#define ME4000_DIO_CTRL_FIFO_HIGH_3 BIT(13)

#define ME4000_AO_DEMUX_ADJUST_REG  0xbc
#define ME4000_AO_DEMUX_ADJUST_VALUE    0x4c
#define ME4000_AI_SAMPLE_COUNTER_REG    0xc0
#define ME4000_AI_FIFO_COUNT        2048
#define ME4000_AI_MIN_TICKS         66
#define ME4000_AI_MIN_SAMPLE_TIME   2000
#define ME4000_AI_CHANNEL_LIST_COUNT    1024

#define PLX9052_INTCSR          0x4c
#define PLX9052_INTCSR_LI2POL       BIT(4)
#define PLX9052_INTCSR_LI2STAT      BIT(5)
#define PLX9052_INTCSR_PCIENAB      BIT(6)
#define PLX9052_INTCSR_LI1POL       BIT(1)
#define PLX9052_INTCSR_LI1ENAB      BIT(0)
#define PLX9052_CNTRL          0x50
#define PLX9052_CNTRL_UIO1_DATA     BIT(5)
#define PLX9052_CNTRL_UIO2_DATA     BIT(8)
#define PLX9052_CNTRL_UIO0_DATA     BIT(2)
#define PLX9052_CNTRL_PCI_RESET     BIT(30)

#define ME4000_MMIO_SIZE 0x200

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
    uint32_t irq_status;

    uint8_t mmio[ME4000_MMIO_SIZE];

    uint32_t ai_ctrl;
    uint32_t ai_status;
    uint32_t plx_intcsr;
    uint32_t plx_cntrl;
    uint32_t dio_ctrl;
    uint32_t dio_dir_reg;

    bool reset_done;
};

G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool plx_int_enabled = (s->plx_intcsr & PLX9052_INTCSR_PCIENAB) &&
                          (s->plx_intcsr & PLX9052_INTCSR_LI1ENAB);
    if (s->irq_status && plx_int_enabled) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad read size %u\n", __func__, size);
        return ~0ULL;
    }

    if (addr == PLX9052_INTCSR) {
        val = s->plx_intcsr;
        if (s->plx_intcsr & PLX9052_INTCSR_LI2POL) {
            val |= PLX9052_INTCSR_LI2STAT;
        }
        return val;
    }
    if (addr == PLX9052_CNTRL) {
        return s->plx_cntrl | PLX9052_CNTRL_UIO0_DATA;
    }

    switch (addr) {
    case ME4000_AI_CTRL_REG:
        val = s->ai_status;
        break;
    case ME4000_AI_DATA_REG:
        val = ldl_le_p(s->mmio + addr);
        break;
    case ME4000_IRQ_STATUS_REG:
        val = s->irq_status;
        break;
    case ME4000_AO_CTRL_REG(0):
    case ME4000_AO_CTRL_REG(1):
    case ME4000_AO_CTRL_REG(2):
    case ME4000_AO_CTRL_REG(3):
        val = ldl_le_p(s->mmio + addr);
        break;
    case ME4000_AO_STATUS_REG(0):
    case ME4000_AO_STATUS_REG(1):
    case ME4000_AO_STATUS_REG(2):
    case ME4000_AO_STATUS_REG(3):
        val = 0;
        break;
    case ME4000_DIO_DIR_REG:
        val = s->dio_dir_reg;
        break;
    case ME4000_DIO_CTRL_REG:
        val = s->dio_ctrl;
        break;
    case ME4000_AO_SINGLE_REG(0):
    case ME4000_AO_SINGLE_REG(1):
    case ME4000_AO_SINGLE_REG(2):
    case ME4000_AO_SINGLE_REG(3):
    case ME4000_AO_FIFO_REG(0):
    case ME4000_AO_FIFO_REG(1):
    case ME4000_AO_FIFO_REG(2):
    case ME4000_AO_FIFO_REG(3):
    case ME4000_AO_TIMER_REG(0):
    case ME4000_AO_TIMER_REG(1):
    case ME4000_AO_TIMER_REG(2):
    case ME4000_AO_TIMER_REG(3):
    case ME4000_AI_CHANNEL_LIST_REG:
    case ME4000_AI_CHAN_TIMER_REG:
    case ME4000_AI_CHAN_PRE_TIMER_REG:
    case ME4000_AI_SCAN_TIMER_LOW_REG:
    case ME4000_AI_SCAN_TIMER_HIGH_REG:
    case ME4000_AI_SCAN_PRE_TIMER_LOW_REG:
    case ME4000_AI_SCAN_PRE_TIMER_HIGH_REG:
    case ME4000_AI_SAMPLE_COUNTER_REG:
    case ME4000_AO_DEMUX_ADJUST_REG:
    case ME4000_AO_LOADSETREG_XX:
    case ME4000_DIO_PORT_0_REG:
    case ME4000_DIO_PORT_1_REG:
    case ME4000_DIO_PORT_2_REG:
    case ME4000_DIO_PORT_3_REG:
        val = ldl_le_p(s->mmio + addr);
        break;
    case ME4000_AI_START_REG:
        stl_le_p(s->mmio + ME4000_AI_DATA_REG, 0x8000);
        s->ai_status |= ME4000_AI_STATUS_EF_DATA;
        val = 0;
        break;
    default:
        if (addr < ME4000_MMIO_SIZE) {
            val = ldl_le_p(s->mmio + addr);
        } else {
            val = ~0ULL;
        }
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad write size %u\n", __func__, size);
        return;
    }

    if (addr == PLX9052_INTCSR) {
        s->plx_intcsr = val & (PLX9052_INTCSR_LI2POL | PLX9052_INTCSR_PCIENAB |
                               PLX9052_INTCSR_LI1POL | PLX9052_INTCSR_LI1ENAB);
        pcibase_update_irq(s);
        return;
    }
    if (addr == PLX9052_CNTRL) {
        s->plx_cntrl = val & (PLX9052_CNTRL_UIO2_DATA | PLX9052_CNTRL_UIO1_DATA |
                              PLX9052_CNTRL_UIO0_DATA | PLX9052_CNTRL_PCI_RESET);
        return;
    }

    switch (addr) {
    case ME4000_AI_CTRL_REG:
        if (val & (ME4000_AI_CTRL_STOP | ME4000_AI_CTRL_IMMEDIATE_STOP)) {
            s->ai_status = 0;
        }
        if (val & ME4000_AI_CTRL_HF_IRQ_RESET) {
            s->irq_status &= ~ME4000_IRQ_STATUS_AI_HF;
        }
        if (val & ME4000_AI_CTRL_SC_IRQ_RESET) {
            s->irq_status &= ~ME4000_IRQ_STATUS_SC;
        }
        s->ai_ctrl = val & ~(ME4000_AI_CTRL_HF_IRQ_RESET | ME4000_AI_CTRL_SC_IRQ_RESET);
        pcibase_update_irq(s);
        break;
    case ME4000_AI_DATA_REG:
        break;
    case ME4000_IRQ_STATUS_REG:
        break;
    case ME4000_DIO_DIR_REG:
        break;
    case ME4000_DIO_CTRL_REG:
        s->dio_ctrl = val;
        break;
    case ME4000_AO_CTRL_REG(0):
    case ME4000_AO_CTRL_REG(1):
    case ME4000_AO_CTRL_REG(2):
    case ME4000_AO_CTRL_REG(3):
    case ME4000_AO_SINGLE_REG(0):
    case ME4000_AO_SINGLE_REG(1):
    case ME4000_AO_SINGLE_REG(2):
    case ME4000_AO_SINGLE_REG(3):
    case ME4000_AO_FIFO_REG(0):
    case ME4000_AO_FIFO_REG(1):
    case ME4000_AO_FIFO_REG(2):
    case ME4000_AO_FIFO_REG(3):
    case ME4000_AO_TIMER_REG(0):
    case ME4000_AO_TIMER_REG(1):
    case ME4000_AO_TIMER_REG(2):
    case ME4000_AO_TIMER_REG(3):
    case ME4000_AI_CHANNEL_LIST_REG:
    case ME4000_AI_CHAN_TIMER_REG:
    case ME4000_AI_CHAN_PRE_TIMER_REG:
    case ME4000_AI_SCAN_TIMER_LOW_REG:
    case ME4000_AI_SCAN_TIMER_HIGH_REG:
    case ME4000_AI_SCAN_PRE_TIMER_LOW_REG:
    case ME4000_AI_SCAN_PRE_TIMER_HIGH_REG:
    case ME4000_AI_SAMPLE_COUNTER_REG:
    case ME4000_AO_DEMUX_ADJUST_REG:
    case ME4000_AO_LOADSETREG_XX:
    case ME4000_DIO_PORT_0_REG:
    case ME4000_DIO_PORT_1_REG:
    case ME4000_DIO_PORT_2_REG:
    case ME4000_DIO_PORT_3_REG:
        stl_le_p(s->mmio + addr, val);
        break;
    default:
        if (addr < ME4000_MMIO_SIZE) {
            stl_le_p(s->mmio + addr, val);
        }
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
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

    memset(s->mmio, 0, sizeof(s->mmio));
    s->irq_status = 0;
    s->ai_ctrl = 0;
    s->ai_status = 0;
    s->plx_intcsr = 0;
    s->plx_cntrl = 0;
    s->dio_ctrl = 0;
    s->dio_dir_reg = 0;
    s->reset_done = true;

    for (int i = 0; i < 4; i++) {
        stl_le_p(s->mmio + ME4000_AO_SINGLE_REG(i), 0x8000);
    }
    stl_le_p(s->mmio + ME4000_AO_DEMUX_ADJUST_REG, ME4000_AO_DEMUX_ADJUST_VALUE);
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_MEILHAUS );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  ME4000_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, ME4000_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    s->num_bars = 4;
    s->bar_info[0].index = 1;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x100;
    s->bar_info[0].name = "me4000-plx";

    s->bar_info[1].index = 2;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = ME4000_MMIO_SIZE;
    s->bar_info[1].name = "me4000-mmio";

    s->bar_info[2].index = 3;
    s->bar_info[2].type = BAR_TYPE_PIO;
    s->bar_info[2].size = 4;
    s->bar_info[2].name = "me4000-timer";

    s->bar_info[3].index = 5;
    s->bar_info[3].type = BAR_TYPE_PIO;
    s->bar_info[3].size = 0x100;
    s->bar_info[3].name = "me4000-xilinx";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    memset(s->mmio, 0, sizeof(s->mmio));
    s->plx_intcsr = 0;
    s->plx_cntrl = 0;
    s->dio_dir_reg = 0;
    s->irq_status = 0;
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

static const VMStateDescription vmstate_pcibase = {
    .name = "me4000_pci",
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