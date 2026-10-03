#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/ide/pci.h"
#include "hw/ide/internal.h"

#define TYPE_PCIBASE_DEVICE "pata_radisys_pci"
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI_VENDOR_ID_RADISYS 0x1331
#define DEVICE_ID_RADISYS 0x8201
#define PCI_CLASS_STORAGE_IDE 0x0101

struct PCIBaseState {
    PCIIDEState ide;

    /* Timing register shadows (PCI config offsets 0x40, 0x48, 0x4A) */
    uint16_t idetm;
    uint8_t udma_enable;
    uint8_t udma_mode;

    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;
};

/* Forward declarations for QEMU internal functions */
void ide_ctrl_read(void *opaque, uint32_t addr, uint32_t *val);
void bmdma_init(IDEBus *bus, BMDMAState *bm, PCIIDEState *d, int unit);

/* Primary command block I/O operations (BAR0) */
static uint64_t primary_cmd_read(void *opaque, hwaddr addr, unsigned size)
{
    IDEBus *bus = opaque;
    return ide_ioport_read(bus, addr);
}

static void primary_cmd_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    IDEBus *bus = opaque;
    ide_ioport_write(bus, addr, val);
}

static const MemoryRegionOps primary_cmd_ops = {
    .read = primary_cmd_read,
    .write = primary_cmd_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* Primary control block I/O operations (BAR1) */
static uint64_t primary_ctrl_read(void *opaque, hwaddr addr, unsigned size)
{
    IDEBus *bus = opaque;
    uint32_t val;
    ide_ctrl_read(bus, addr, &val);
    return val;
}

static void primary_ctrl_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    IDEBus *bus = opaque;
    ide_ctrl_write(bus, addr, (uint32_t)val);
}

static const MemoryRegionOps primary_ctrl_ops = {
    .read = primary_ctrl_read,
    .write = primary_ctrl_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* Secondary command block I/O operations (BAR2) */
static uint64_t secondary_cmd_read(void *opaque, hwaddr addr, unsigned size)
{
    IDEBus *bus = opaque;
    return ide_ioport_read(bus, addr);
}

static void secondary_cmd_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    IDEBus *bus = opaque;
    ide_ioport_write(bus, addr, val);
}

static const MemoryRegionOps secondary_cmd_ops = {
    .read = secondary_cmd_read,
    .write = secondary_cmd_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* Secondary control block I/O operations (BAR3) */
static uint64_t secondary_ctrl_read(void *opaque, hwaddr addr, unsigned size)
{
    IDEBus *bus = opaque;
    uint32_t val;
    ide_ctrl_read(bus, addr, &val);
    return val;
}

static void secondary_ctrl_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    IDEBus *bus = opaque;
    ide_ctrl_write(bus, addr, (uint32_t)val);
}

static const MemoryRegionOps secondary_ctrl_ops = {
    .read = secondary_ctrl_read,
    .write = secondary_ctrl_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* Custom config read to capture timing registers */
static uint32_t pata_radisys_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    switch (addr) {
    case 0x40:
        return s->idetm & 0xff;
    case 0x41:
        return (s->idetm >> 8) & 0xff;
    case 0x48:
        return s->udma_enable;
    case 0x4A:
        return s->udma_mode;
    default:
        return pci_default_read_config(pdev, addr, len);
    }
}

/* Custom config write to store timing registers */
static void pata_radisys_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    switch (addr) {
    case 0x40:
        s->idetm = (s->idetm & 0xff00) | (val & 0xff);
        break;
    case 0x41:
        s->idetm = (s->idetm & 0x00ff) | ((val & 0xff) << 8);
        break;
    case 0x48:
        s->udma_enable = val & 0xff;
        break;
    case 0x4A:
        s->udma_mode = val & 0xff;
        break;
    default:
        pci_default_write_config(pdev, addr, val, len);
        break;
    }
}

static void pata_radisys_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Set vendor and device IDs early, before parent realize tries to use them */
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_RADISYS);
    pci_set_word(pci_conf + PCI_DEVICE_ID, DEVICE_ID_RADISYS);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_STORAGE_IDE);
    pci_set_byte(pci_conf + PCI_CLASS_PROG, 0x80); /* Native mode, bus master */
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Install custom config handlers for timing registers */
    pdev->config_read = pata_radisys_config_read;
    pdev->config_write = pata_radisys_config_write;

    /* Create I/O memory regions for the IDE ports */
    MemoryRegion *primary_cmd = g_new(MemoryRegion, 1);
    memory_region_init_io(primary_cmd, OBJECT(s), &primary_cmd_ops,
                          &s->ide.bus[0], "primary-cmd", 8);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_IO, primary_cmd);

    MemoryRegion *primary_ctrl = g_new(MemoryRegion, 1);
    memory_region_init_io(primary_ctrl, OBJECT(s), &primary_ctrl_ops,
                          &s->ide.bus[0], "primary-ctrl", 4);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_IO, primary_ctrl);

    MemoryRegion *secondary_cmd = g_new(MemoryRegion, 1);
    memory_region_init_io(secondary_cmd, OBJECT(s), &secondary_cmd_ops,
                          &s->ide.bus[1], "secondary-cmd", 8);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_IO, secondary_cmd);

    MemoryRegion *secondary_ctrl = g_new(MemoryRegion, 1);
    memory_region_init_io(secondary_ctrl, OBJECT(s), &secondary_ctrl_ops,
                          &s->ide.bus[1], "secondary-ctrl", 4);
    pci_register_bar(pdev, 3, PCI_BASE_ADDRESS_SPACE_IO, secondary_ctrl);

    /* Initialize IDE buses and BMDMA */
    qemu_irq irq = pci_allocate_irq(pdev);
    ide_init2(&s->ide.bus[0], irq);
    ide_init2(&s->ide.bus[1], irq);
    bmdma_init(&s->ide.bus[0], &s->ide.bmdma[0], &s->ide, 0);
    bmdma_init(&s->ide.bus[1], &s->ide.bmdma[1], &s->ide, 1);

    /* Register BAR4 for bus master DMA */
    memory_region_init(&s->ide.bmdma_bar, OBJECT(s), "bmdma-bar", 16);
    memory_region_add_subregion(&s->ide.bmdma_bar, 0, &s->ide.bmdma[0].cmd_ioport);
    memory_region_add_subregion(&s->ide.bmdma_bar, 2, &s->ide.bmdma[0].extra_io);
    memory_region_add_subregion(&s->ide.bmdma_bar, 8, &s->ide.bmdma[1].cmd_ioport);
    memory_region_add_subregion(&s->ide.bmdma_bar, 10, &s->ide.bmdma[1].extra_io);
    pci_register_bar(pdev, 4, PCI_BASE_ADDRESS_SPACE_IO, &s->ide.bmdma_bar);

    /* Reset timing registers */
    s->idetm = 0xc0c0;
    s->udma_enable = 0x00;
    s->udma_mode = 0x00;
}

static void pata_radisys_uninit(PCIDevice *pdev)
{
    msix_uninit(pdev, NULL, NULL);
    msi_uninit(pdev);
}

static void pata_radisys_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);

    pci_device_reset(PCI_DEVICE(dev));
    s->idetm = 0xc0c0;
    s->udma_enable = 0x00;
    s->udma_mode = 0x00;
}

static void pata_radisys_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pata_radisys_realize;
    k->exit = pata_radisys_uninit;
    dc->reset = pata_radisys_reset;
    dc->vmsd = NULL;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
}

static void pata_radisys_register_types(void)
{
    static InterfaceInfo interfaces[] = {
        { INTERFACE_PCIE_DEVICE },
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    };

    static const TypeInfo pata_radisys_info = {
        .name          = TYPE_PCIBASE_DEVICE,
        .parent        = TYPE_PCI_IDE,
        .instance_size = sizeof(PCIBaseState),
        .class_init    = pata_radisys_class_init,
        .interfaces = interfaces,
    };

    type_register_static(&pata_radisys_info);
}
type_init(pata_radisys_register_types);
