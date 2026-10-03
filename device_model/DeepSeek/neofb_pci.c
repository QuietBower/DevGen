/*
 * QEMU model for NeoMagic NM2070 framebuffer (neofb)
 * Generated from Linux driver drivers/video/fbdev/neofb.c
 * Phase 4: Debug & Update - Fixed probe failure by splitting BARs:
 *   BAR0: video RAM (2 MB)
 *   BAR1: MMIO registers (4 KB)
 *   Removed unnecessary VGA I/O ports to avoid resource conflicts.
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
#include "hw/pci/pci_ids.h"

#define TYPE_PCIBASE_DEVICE "neofb_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Hardware identifiers from driver source */
#define PCI_VENDOR_ID_NEOMAGIC  0x10c8
#define PCI_DEVICE_ID_NM2070    0x0001  /* PCI_CHIP_NM2070 */
#define CLASS_ID                0x030000  /* VGA compatible display controller */
#define BAR0_SIZE               0x200000  /* 2 MB video RAM */
#define BAR1_SIZE               0x1000    /* 4 KB MMIO registers */

struct PCIBaseState {
    PCIDevice parent_obj;

    /* BAR0: video RAM */
    MemoryRegion bar0_ram;

    /* BAR1: MMIO registers */
    MemoryRegion bar1_mmio;

    /* MMIO register state */
    uint8_t mmio_data[BAR1_SIZE];
};

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr < BAR1_SIZE) {
        switch (addr) {
        case 0x20:
            val = 0x12;  /* bits 3:4 = 10 => 1024x768 panel, bit1 = LCD */
            break;
        case 0x21:
            val = 0x12;  /* color TFT */
            break;
        default:
            val = s->mmio_data[addr];
            break;
        }
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < BAR1_SIZE) {
        s->mmio_data[addr] = (uint8_t)val;
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    memset(s->mmio_data, 0, sizeof(s->mmio_data));

    /* Set default panel detection values in MMIO */
    s->mmio_data[0x20] = 0x12;  /* 1024x768 panel, LCD */
    s->mmio_data[0x21] = 0x12;  /* color TFT */
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_NEOMAGIC);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_NM2070);
    /* Set class code: base 0x03, sub 0x00, prog-if 0x00 */
    pci_set_byte(pci_conf + PCI_CLASS_PROG, 0x00);
    pci_set_byte(pci_conf + PCI_CLASS_DEVICE, 0x00);
    pci_set_byte(pci_conf + PCI_CLASS_DEVICE + 1, 0x03);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR0: video RAM */
    memory_region_init_ram(&s->bar0_ram, OBJECT(s), "neofb-vram", BAR0_SIZE, errp);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar0_ram);

    /* BAR1: MMIO registers */
    memory_region_init_io(&s->bar1_mmio, OBJECT(s), &pcibase_mmio_ops, s, "neofb-mmio", BAR1_SIZE);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar1_mmio);
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
    .name = "neofb_pci",
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
    set_bit(DEVICE_CATEGORY_DISPLAY, dc->categories);
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
