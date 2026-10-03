/*
 * QEMU 8.2.10 virtual PCI device model for VIA VT82C586B SMBus/I2C controller
 * Based on Linux driver i2c-via.c
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

#define TYPE_PCIBASE_DEVICE "vt586b_smbus_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI Vendor & Device IDs from driver */
#define PCI_VENDOR_ID_VIA               0x1106
#define PCI_DEVICE_ID_VIA_82C586_3      0x3040

/* PCI configuration space registers */
#define PM_CFG_REVID                    0x08
#define PM_CFG_IOBASE0                  0x20
#define PM_CFG_IOBASE1                  0x48

/* I/O offsets relative to BAR0 */
#define I2C_DIR_OFFSET                  0x40
#define I2C_OUT_OFFSET                  0x42
#define I2C_IN_OFFSET                   0x44

/* I2C pin definitions */
#define I2C_SCL                         0x02
#define I2C_SDA                         0x04
#define IOSPACE                         0x06

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion io;
    bool has_msi;
    bool has_msix;

    uint8_t i2c_dir;
    uint8_t i2c_out;
    uint8_t i2c_in;
};

/* PIO read/write handlers */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case I2C_DIR_OFFSET:
        return s->i2c_dir;
    case I2C_OUT_OFFSET:
        return s->i2c_out;
    case I2C_IN_OFFSET:
        return s->i2c_in;
    default:
        return 0xff;
    }
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case I2C_DIR_OFFSET:
        s->i2c_dir = val & 0xff;
        /* Update input state: pins are high (1) when dir bit is 0 (output high),
         * low (0) when dir bit is 1 (output low) */
        s->i2c_in = (~s->i2c_dir) & (I2C_SCL | I2C_SDA);
        break;
    case I2C_OUT_OFFSET:
        s->i2c_out = val & 0xff;
        break;
    /* I2C_IN is read-only, ignore writes */
    default:
        break;
    }
}

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 1 },
    .impl  = { .min_access_size = 1, .max_access_size = 1 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    s->i2c_dir = 0x00;
    s->i2c_out = 0x00;
    s->i2c_in = I2C_SCL | I2C_SDA;  /* both lines idle high */
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_VIA);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_VIA_82C586_3);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0C05);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x00);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Set up I/O base address in config space */
    pci_set_word(pci_conf + PM_CFG_IOBASE0, 0x5000);
    pci_set_word(pci_conf + PM_CFG_IOBASE1, 0x5000);

    /* Create PIO region and register as BAR0 */
    memory_region_init_io(&s->io, OBJECT(s), &pcibase_pio_ops, s, "vt586b-pio", 0x100);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_IO, &s->io);
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
    .name = "vt586b_smbus_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT8(i2c_dir, PCIBaseState),
        VMSTATE_UINT8(i2c_out, PCIBaseState),
        VMSTATE_UINT8(i2c_in, PCIBaseState),
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
    dc->categories |= (1u << DEVICE_CATEGORY_MISC);
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
