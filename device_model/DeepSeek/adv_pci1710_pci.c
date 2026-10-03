/* Complete QEMU C source file */
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


#define TYPE_PCIBASE_DEVICE "adv_pci1710_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register offsets and bit definitions */
#define PCI171X_AD_DATA_REG     0x00
#define PCI171X_SOFTTRG_REG     0x00
#define PCI171X_RANGE_REG        0x02
#define PCI171X_RANGE_DIFF       BIT(5)
#define PCI171X_RANGE_UNI        BIT(4)
#define PCI171X_RANGE_GAIN(x)    (((x) & 0x7) << 0)
#define PCI171X_MUX_REG          0x04
#define PCI171X_MUX_CHANH(x)      (((x) & 0xff) << 8)
#define PCI171X_MUX_CHANL(x)      (((x) & 0xff) << 0)
#define PCI171X_MUX_CHAN(x)       (PCI171X_MUX_CHANH(x) | PCI171X_MUX_CHANL(x))
#define PCI171X_STATUS_REG       0x06
#define PCI171X_STATUS_IRQ       BIT(11)
#define PCI171X_STATUS_FF        BIT(10)
#define PCI171X_STATUS_FH        BIT(9)
#define PCI171X_STATUS_FE        BIT(8)
#define PCI171X_CTRL_REG         0x06
#define PCI171X_CTRL_CNT0        BIT(6)
#define PCI171X_CTRL_ONEFH       BIT(5)
#define PCI171X_CTRL_IRQEN       BIT(4)
#define PCI171X_CTRL_GATE        BIT(3)
#define PCI171X_CTRL_EXT         BIT(2)
#define PCI171X_CTRL_PACER       BIT(1)
#define PCI171X_CTRL_SW          BIT(0)
#define PCI171X_CLRINT_REG       0x08
#define PCI171X_CLRFIFO_REG      0x09
#define PCI171X_DA_REG(x)         (0x0a + ((x) * 2))
#define PCI171X_DAREF_REG        0x0e
#define PCI171X_DAREF(c, r)       (((r) & 0x3) << ((c) * 2))
#define PCI171X_DAREF_MASK(c)     PCI171X_DAREF((c), 0x3)
#define PCI171X_DI_REG           0x10
#define PCI171X_DO_REG           0x10
#define PCI171X_TIMER_BASE       0x18

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

    uint32_t intr_status;
    uint32_t intr_mask;

    struct {
        uint16_t ad_data;
        uint16_t range;
        uint16_t mux;
        uint16_t status;
        uint16_t ctrl;
        uint16_t da[4];
        uint16_t daref;
        uint16_t di;
        uint16_t do_;
        /* 16-bit timer registers at 0x18, 0x1a, 0x1c, 0x1e */
        uint16_t timer[4];
    } regs;
};


static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->intr_status & s->intr_mask) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}


static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case PCI171X_AD_DATA_REG:
        val = s->regs.ad_data;
        /* reading the A/D data pops the FIFO, so set FIFO empty status */
        s->regs.status |= PCI171X_STATUS_FE;
        break;
    case PCI171X_RANGE_REG:
        val = s->regs.range;
        break;
    case PCI171X_MUX_REG:
        val = s->regs.mux;
        break;
    case PCI171X_STATUS_REG:
        val = s->regs.status;
        break;
    case PCI171X_DI_REG:
        val = s->regs.di;
        break;
    case PCI171X_DAREF_REG:
        val = s->regs.daref;
        break;
    case 0x0a ... 0x0d:
        val = s->regs.da[(addr - 0x0a) / 2];
        break;
    case 0x18:
        val = s->regs.timer[0];
        break;
    case 0x1a:
        val = s->regs.timer[1];
        break;
    case 0x1c:
        val = s->regs.timer[2];
        break;
    case 0x1e:
        val = s->regs.timer[3];
        break;
    default:
        val = 0;
        break;
    }
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case PCI171X_AD_DATA_REG:
        /* software trigger: start conversion */
        {
            uint8_t channel = s->regs.mux & 0xFF;
            s->regs.ad_data = (channel << 12) | 0xAAA;
            s->regs.status &= ~PCI171X_STATUS_FE;  /* FIFO not empty */
        }
        break;
    case PCI171X_RANGE_REG:
        s->regs.range = val;
        break;
    case PCI171X_MUX_REG:
        s->regs.mux = val;
        break;
    case PCI171X_CTRL_REG:
        s->regs.ctrl = val;
        s->intr_mask = (val & PCI171X_CTRL_IRQEN) ? 1 : 0;
        pcibase_update_irq(s);
        break;
    case PCI171X_CLRINT_REG:
        s->intr_status = 0;
        s->regs.status &= ~PCI171X_STATUS_IRQ;
        pcibase_update_irq(s);
        break;
    case PCI171X_CLRFIFO_REG:
        /* clear FIFO: set FIFO empty */
        s->regs.status |= PCI171X_STATUS_FE;
        break;
    case PCI171X_DAREF_REG:
        s->regs.daref = val;
        break;
    case PCI171X_DO_REG:
        s->regs.do_ = val;
        break;
    case 0x0a ... 0x0d:
        s->regs.da[(addr - 0x0a) / 2] = val;
        break;
    case 0x18:
        s->regs.timer[0] = val;
        break;
    case 0x1a:
        s->regs.timer[1] = val;
        break;
    case 0x1c:
        s->regs.timer[2] = val;
        break;
    case 0x1e:
        s->regs.timer[3] = val;
        break;
    default:
        break;
    }
}

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

    memset(&s->regs, 0, sizeof(s->regs));
    s->intr_status = 0;
    s->intr_mask = 0;
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
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s, bi->name, aligned_size);
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

    pci_set_word(pci_conf + PCI_VENDOR_ID, 0x13fe);
    pci_set_word(pci_conf + PCI_DEVICE_ID, 0x1710);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0xff00);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* BAR configuration */
    s->bar_info[2].index = 2;
    s->bar_info[2].type = BAR_TYPE_PIO;
    s->bar_info[2].size = 0x20;
    s->bar_info[2].name = "bar2";
    s->num_bars = 1;

    for (int i = 0; i < 6; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    /* No MSI/MSI-X cleanup needed */
}

static const VMStateDescription vmstate_pcibase = {
    .name = "adv_pci1710_pci",
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
