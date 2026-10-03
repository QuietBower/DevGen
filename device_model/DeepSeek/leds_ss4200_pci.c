/*
 * QEMU device model for Intel ICH7 LPC (NAS GPIO LED)
 * Based on driver leds-ss4200.c
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/module.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "migration/vmstate.h"

#define TYPE_PCIBASE_DEVICE "leds_ss4200_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x8086
#define DEVICE_ID 0x27b8
#define CLASS_ID 0x0601

/* PM and GPIO config space offsets */
#define PMBASE       0x040
#define GPIO_BASE    0x048
#define GPIO_CTRL    0x04c

/* GPIO I/O space offsets (relative to GPIO base) */
#define GPIO_USE_SEL  0x00
#define GP_IO_SEL     0x04
#define GP_LVL        0x0c
#define GPO_BLINK     0x18
#define GPI_INV       0x30
#define GPIO_USE_SEL2 0x34
#define GP_IO_SEL2    0x38
#define GP_LVL2       0x3c
#define GPIO_EN       0x10
#define ICH7_GPIO_SIZE 64

/* Bitmask */
#define NAS_RECOVERY 0x00000400

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;

    /* Hardware Register Shadows (config) */
    uint32_t pmbase;     /* config offset 0x40 */
    uint32_t gpiobase;   /* config offset 0x48 */
    uint32_t gpio_ctrl;  /* config offset 0x4c */

    /* GPIO I/O region */
    MemoryRegion gpio_io;

    /* GPIO registers */
    uint32_t gpio_use_sel;
    uint32_t gp_io_sel;
    uint32_t gp_lvl;
    uint32_t gpo_blink;
    uint32_t gpi_inv;
};

/* GPIO PIO handlers */
static uint64_t gpio_io_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    switch (addr) {
    case GPIO_USE_SEL:
        return s->gpio_use_sel;
    case GP_IO_SEL:
        return s->gp_io_sel;
    case GP_LVL:
        return s->gp_lvl;
    case GPO_BLINK:
        return s->gpo_blink;
    case GPI_INV:
        return s->gpi_inv;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "gpio: unimplemented read @0x%"HWADDR_PRIx"\n", addr);
        return 0;
    }
}

static void gpio_io_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    switch (addr) {
    case GPIO_USE_SEL:
        s->gpio_use_sel = val;
        break;
    case GP_IO_SEL:
        s->gp_io_sel = val;
        break;
    case GP_LVL:
        s->gp_lvl = val;
        break;
    case GPO_BLINK:
        s->gpo_blink = val;
        break;
    case GPI_INV:
        s->gpi_inv = val;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "gpio: unimplemented write @0x%"HWADDR_PRIx" = 0x%"PRIx64"\n", addr, val);
        break;
    }
}

static const MemoryRegionOps gpio_io_ops = {
    .read = gpio_io_read,
    .write = gpio_io_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl = { .min_access_size = 4, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    s->pmbase = 0;
    s->gpiobase = 0;
    s->gpio_ctrl = 0;
    s->gpio_use_sel = 0;
    s->gp_io_sel = 0;
    s->gp_lvl = 0;
    s->gpo_blink = 0;
    s->gpi_inv = 0;
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID, VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Set PMBASE, GPIOBASE, GPIO_CTRL */
    pci_set_long(pci_conf + PMBASE, 0x00000180);    /* arbitrary, driver doesn't use */
    pci_set_long(pci_conf + GPIO_BASE, 0x00002000); /* GPIO I/O base */
    pci_set_long(pci_conf + GPIO_CTRL, 0x10);       /* GPIO_EN bit set */

    /* No standard BARs; we manually map GPIO I/O region */
    memory_region_init_io(&s->gpio_io, OBJECT(s), &gpio_io_ops, s, "gpio-io", ICH7_GPIO_SIZE);
    memory_region_add_subregion(pci_address_space_io(pdev), 0x2000, &s->gpio_io);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    memory_region_del_subregion(pci_address_space_io(pdev), &s->gpio_io);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

/* VMState */
static const VMStateDescription vmstate_pcibase = {
    .name = "leds_ss4200_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(gpio_use_sel, PCIBaseState),
        VMSTATE_UINT32(gp_io_sel, PCIBaseState),
        VMSTATE_UINT32(gp_lvl, PCIBaseState),
        VMSTATE_UINT32(gpo_blink, PCIBaseState),
        VMSTATE_UINT32(gpi_inv, PCIBaseState),
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
