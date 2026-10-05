/* QEMU device model for pata_ninja32 driver */
#include "qemu/osdep.h"
#include "hw/pci/pci.h"
#include "hw/ide/pci.h"
#include "hw/qdev-properties.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qom/object.h"
#include "hw/irq.h"

/* Prototypes for IDE internal functions usually found in hw/ide/internal.h */
void ide_bus_init(IDEBus *idebus, size_t idebus_size, DeviceState *dev, int bus_id, int max_units);
void ide_bus_reset(IDEBus *bus);
void ide_bus_init_output_irq(IDEBus *bus, qemu_irq irq_out);
uint32_t ide_ioport_read(void *opaque, uint32_t addr);
void ide_ioport_write(void *opaque, uint32_t addr, uint32_t val);
uint32_t ide_status_read(void *opaque, uint32_t addr);
void ide_ctrl_write(void *opaque, uint32_t addr, uint32_t val);
uint32_t ide_data_readw(void *opaque, uint32_t addr);
void ide_data_writew(void *opaque, uint32_t addr, uint32_t val);
uint32_t ide_data_readl(void *opaque, uint32_t addr);
void ide_data_writel(void *opaque, uint32_t addr, uint32_t val);

#define TYPE_PCIBASE_DEVICE "pata_ninja32_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI IDs */
#define VENDOR_ID 0x10FC
#define DEVICE_ID 0x0003
#define CLASS_ID  PCI_CLASS_STORAGE_IDE

struct PCIBaseState {
    PCIIDEState ide; /* Parent class state */

    MemoryRegion bar_io;

    /* Registers */
    uint8_t reg_irq_enable;
    uint8_t reg_burst;
    uint8_t reg_unk3;
    uint8_t reg_wait0;
    uint8_t reg_unk5;
    uint8_t reg_unk1c;
    uint8_t reg_bmdma_ctl;
    uint8_t reg_pio_timing;
};

static void ninja32_set_irq(void *opaque, int n, int level)
{
    PCIBaseState *s = opaque;
    
    /* 
     * Driver writes 0x05 to 0x01 to enable interrupts.
     * We assume bit 0 or 2 enables it. 
     */
    if (s->reg_irq_enable & 0x05) {
        pci_set_irq(PCI_DEVICE(s), level);
    } else {
        pci_set_irq(PCI_DEVICE(s), 0);
    }
}

static uint64_t ninja32_io_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case 0x00: /* BMDMA Command */
        val = s->ide.bmdma[0].cmd;
        break;
    case 0x01: /* IRQ Enable */
        val = s->reg_irq_enable;
        break;
    case 0x02: /* BMDMA Status / Burst */
        val = s->ide.bmdma[0].status;
        break;
    case 0x03: /* Unknown */
        val = s->reg_unk3;
        break;
    case 0x04 ... 0x07: /* BMDMA PRDT */
        {
            uint32_t prdt = s->ide.bmdma[0].addr;
            unsigned shift = (addr - 0x04) * 8;
            if (size == 4) val = prdt;
            else val = (prdt >> shift) & ((1ULL << (size * 8)) - 1);
        }
        break;
    case 0x10: /* IDE Data */
        if (size == 4) val = ide_data_readl(&s->ide.bus[0], 0);
        else if (size == 2) val = ide_data_readw(&s->ide.bus[0], 0);
        else val = ide_ioport_read(&s->ide.bus[0], 0);
        break;
    case 0x11 ... 0x17: /* IDE Command Block */
        val = ide_ioport_read(&s->ide.bus[0], addr - 0x10);
        break;
    case 0x1C: /* Unknown */
        val = s->reg_unk1c;
        break;
    case 0x1D: /* BMDMA Control */
        val = s->reg_bmdma_ctl;
        break;
    case 0x1E: /* IDE Control Block (AltStatus) */
        val = ide_status_read(&s->ide.bus[0], 0);
        break;
    case 0x1F: /* PIO Timing */
        val = s->reg_pio_timing;
        break;
    default:
        break;
    }
    return val;
}

static void ninja32_io_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case 0x00: /* BMDMA Command */
        bmdma_cmd_writeb(&s->ide.bmdma[0], val);
        break;
    case 0x01: /* IRQ Enable */
        s->reg_irq_enable = val;
        break;
    case 0x02: /* BMDMA Status / Burst */
        bmdma_status_writeb(&s->ide.bmdma[0], val);
        s->reg_burst = val; /* Shadow */
        break;
    case 0x03: /* Unknown */
        s->reg_unk3 = val;
        break;
    case 0x04 ... 0x07: /* BMDMA PRDT */
        {
            if (size == 4) {
                s->ide.bmdma[0].addr = val;
            } else {
                uint32_t mask = ((1ULL << (size * 8)) - 1);
                unsigned shift = (addr - 0x04) * 8;
                s->ide.bmdma[0].addr &= ~(mask << shift);
                s->ide.bmdma[0].addr |= (val & mask) << shift;
            }
            if (addr == 0x04) s->reg_wait0 = val; /* Shadow */
        }
        break;
    case 0x10: /* IDE Data */
        if (size == 4) ide_data_writel(&s->ide.bus[0], 0, val);
        else if (size == 2) ide_data_writew(&s->ide.bus[0], 0, val);
        else ide_ioport_write(&s->ide.bus[0], 0, val);
        break;
    case 0x11 ... 0x17: /* IDE Command Block */
        ide_ioport_write(&s->ide.bus[0], addr - 0x10, val);
        break;
    case 0x1C: /* Unknown */
        s->reg_unk1c = val;
        break;
    case 0x1D: /* BMDMA Control */
        s->reg_bmdma_ctl = val;
        break;
    case 0x1E: /* IDE Control Block (Device Control) */
        ide_ctrl_write(&s->ide.bus[0], 0, val);
        break;
    case 0x1F: /* PIO Timing */
        s->reg_pio_timing = val;
        break;
    default:
        break;
    }
}

static const MemoryRegionOps ninja32_io_ops = {
    .read = ninja32_io_read,
    .write = ninja32_io_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    
    ide_bus_reset(&s->ide.bus[0]);
    
    s->reg_irq_enable = 0;
    s->reg_burst = 0;
    s->reg_unk3 = 0;
    s->reg_wait0 = 0;
    s->reg_unk5 = 0;
    s->reg_unk1c = 0;
    s->reg_bmdma_ctl = 0;
    s->reg_pio_timing = 0;
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    DeviceState *ds = DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    
    /* 
     * Set Programming Interface to 0x85 (Native Mode, Bus Master).
     * This prevents the kernel from assigning legacy IDE ports (0x1f0, etc.)
     * which conflict with the PIIX3 IDE controller.
     */
    pci_set_byte(pci_conf + PCI_CLASS_PROG, 0x85);

    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Init IDE Bus 0 */
    ide_bus_init(&s->ide.bus[0], sizeof(s->ide.bus[0]), ds, 0, 1);
    
    /* Init BMDMA */
    bmdma_init(&s->ide.bus[0], &s->ide.bmdma[0], &s->ide);
    
    /* Setup IRQ routing */
    qdev_init_gpio_in(ds, ninja32_set_irq, 1);
    ide_bus_init_output_irq(&s->ide.bus[0], qdev_get_gpio_in(ds, 0));

    /* Register BAR 0 */
    memory_region_init_io(&s->bar_io, OBJECT(s), &ninja32_io_ops, s, "ninja32-io", 0x20);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_IO, &s->bar_io);
}

static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pcibase_realize;
    dc->reset  = pcibase_reset;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
}

static const TypeInfo pcibase_info = {
    .name = TYPE_PCIBASE_DEVICE,
    .parent = TYPE_PCI_IDE,
    .instance_size = sizeof(PCIBaseState),
    .class_init = pcibase_class_init,
};

static void pcibase_register_types(void)
{
    type_register_static(&pcibase_info);
}

type_init(pcibase_register_types);
