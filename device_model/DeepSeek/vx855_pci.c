#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/module.h"
#include "qemu/log.h"
#include "qemu/host-utils.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "sysemu/dma.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_device.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-core.h"
#include "migration/vmstate.h"

#define TYPE_PCIBASE_DEVICE "vx855_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VX855_CFG_PMIO_OFFSET 0x88
#define VX855_PMIO_ACPI        0x00
#define VX855_PMIO_ACPI_LEN    0x0b
#define VX855_PMIO_PPM         0x10
#define VX855_PMIO_PPM_LEN     0x08
#define VX855_PMIO_GPPM        0x20
#define VX855_PMIO_GPPM_LEN    0x33
#define VX855_PMIO_R_GPI       0x48
#define VX855_PMIO_R_GPO       0x4c
#define VSPIC_MMIO_SIZE        0x1000

#define PCI_VENDOR_ID_VIA      0x1106
#define PCI_DEVICE_ID_VIA_VX855 0x8409

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];
    int num_bars;
};

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
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
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_VIA);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_VIA_VX855);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0c05); /* SMBus class to allow vt596_smbus binding */
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);

    /* Set PMIO base to a non-zero value that, after masking lowest 7 bits, remains non-zero.
     * The driver expects (value & 0xff80) != 0. 0x80 satisfies this. */
    pci_set_word(pci_conf + VX855_CFG_PMIO_OFFSET, 0x80);

    /* Set SMBus I/O base at offset 0x80 with enable bit set (bit 0 = 1) and I/O address 0x100.
     * This allows the vt596_smbus driver to successfully configure the SMBus I/O base and bind. */
    pci_set_word(pci_conf + 0x80, 0x101);

    /* No BARs are explicitly used by the driver; skip registration. */
}

static void pcibase_uninit(PCIDevice *pdev)
{
    /* Intentionally left empty: no MSI/MSI-X or other resources to release. */
}

static const VMStateDescription vmstate_pcibase = {
    .name = "vx855_pci",
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