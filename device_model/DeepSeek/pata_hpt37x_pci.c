/*
 * QEMU device model for HighPoint HPT370/372/374 ATA controller
 * Based on driver pata_hpt37x.c
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
#include "hw/qdev-properties.h"
#include "hw/qdev-core.h"
#include "migration/vmstate.h"

#define TYPE_PCIBASE_DEVICE "pata_hpt37x_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;
};

/* IDE command block read/write handlers */
static uint64_t pcibase_ide_cmd_read(void *opaque, hwaddr addr, unsigned size)
{
    uint64_t val = 0;
    switch (addr) {
    case 7: /* Status register */
        val = 0x50; /* DRDY | DSC, no BSY, no error */
        break;
    default:
        val = 0;
        break;
    }
    return val;
}

static void pcibase_ide_cmd_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Ignore writes to command block */
}

static const MemoryRegionOps pcibase_ide_cmd_ops = {
    .read = pcibase_ide_cmd_read,
    .write = pcibase_ide_cmd_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* IDE control block read/write handlers */
static uint64_t pcibase_ide_ctl_read(void *opaque, hwaddr addr, unsigned size)
{
    uint64_t val = 0;
    switch (addr) {
    case 0: /* Alternate status */
        val = 0x50; /* Mirror of status */
        break;
    default:
        val = 0;
        break;
    }
    return val;
}

static void pcibase_ide_ctl_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Ignore writes to control block */
}

static const MemoryRegionOps pcibase_ide_ctl_ops = {
    .read = pcibase_ide_ctl_read,
    .write = pcibase_ide_ctl_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* BAR4 PIO handlers (BMIDE and custom registers) */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    uint64_t val = 0;

    switch (addr) {
    case 0x90:
        /* Provide magic PCI clock value: fcnt=0x84, magic=0xABCDE */
        val = 0xABCDE084;
        break;
    default:
        /* For BMIDE registers and others, return 0 */
        val = 0;
        break;
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* No side effects needed; writes are ignored */
    switch (addr) {
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
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID, 0x1103);
    pci_set_word(pci_conf + PCI_DEVICE_ID, 0x0004);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0101); /* IDE controller */
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x03);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Initialize custom config registers to expected default values */
    pci_conf[0x50] = 0x04; /* Enable port 0 */
    pci_conf[0x54] = 0x04; /* Enable port 1 */

    /* BAR Initialization: standard IDE I/O ports (0-3) and BMIDE/custom (4) */
    /* BAR0: Primary command block (size rounded to 16 for memory BAR) */
    memory_region_init_io(&s->bar_regions[0], OBJECT(s), &pcibase_ide_cmd_ops, s, "hpt37x-ide-cmd0", 16);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_regions[0]);

    /* BAR1: Primary control block (size rounded to 16 for memory BAR) */
    memory_region_init_io(&s->bar_regions[1], OBJECT(s), &pcibase_ide_ctl_ops, s, "hpt37x-ide-ctl0", 16);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_regions[1]);

    /* BAR2: Secondary command block (size rounded to 16 for memory BAR) */
    memory_region_init_io(&s->bar_regions[2], OBJECT(s), &pcibase_ide_cmd_ops, s, "hpt37x-ide-cmd1", 16);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_regions[2]);

    /* BAR3: Secondary control block (size rounded to 16 for memory BAR) */
    memory_region_init_io(&s->bar_regions[3], OBJECT(s), &pcibase_ide_ctl_ops, s, "hpt37x-ide-ctl1", 16);
    pci_register_bar(pdev, 3, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_regions[3]);

    /* BAR4: BMIDE and custom registers */
    memory_region_init_io(&s->bar_regions[4], OBJECT(s), &pcibase_pio_ops, s, "hpt37x-io", 0x100);
    pci_register_bar(pdev, 4, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_regions[4]);
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
    .name = "pata_hpt37x_pci",
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
