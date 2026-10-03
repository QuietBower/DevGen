/*
 * QEMU DEC 21041 (Tulip) Fast Ethernet Controller Emulation
 * for Linux driver de2104x.c
 * Phase 3 Repair: fix DEVICE_ID, struct conflict, unrealize->exit, CmdReset
 */

#include "qemu/osdep.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/units.h"

#define TYPE_PCIBASE_DEVICE "de2104x"
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Vendor and Device IDs from de2104x.c: */
#define PCI_VENDOR_ID_DEC 0x1011
#define PCI_DEVICE_ID_DEC_TULIP 0x0002

/* TODO: CmdReset is defined in driver source, request it */
/* #define CmdReset ... */

struct PCIBaseState {
    PCIDevice pdev;
    MemoryRegion mmio;
};

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* PCIBaseState *s = opaque; */ /* unused, removed */
    switch (addr) {
    case 0x00:   /* CSR0 (Bus Mode Register) example */
        /* if (val & CmdReset) {
            // reset logic
        } */
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented write to 0x%" HWADDR_PRIx " val 0x%" PRIx64 "\n",
                      __func__, addr, val);
        break;
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* PCIBaseState *s = opaque; */ /* unused, removed */
    qemu_log_mask(LOG_UNIMP, "%s: unimplemented read from 0x%" HWADDR_PRIx "\n",
                  __func__, addr);
    return 0;
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    /* Reset logic placeholder */
    (void)s; /* suppress warning if unused */
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_DEC);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_DEC_TULIP);

    memory_region_init_io(&s->mmio, OBJECT(s), &pcibase_mmio_ops, s,
                          "de2104x-mmio", 0x1000);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mmio);
}

static void pcibase_exit(PCIDevice *pdev)
{
    /* nothing to do */
}

static void pcibase_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(class);

    k->realize = pcibase_realize;
    k->exit = pcibase_exit;
    dc->reset = pcibase_reset;
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
}

static const TypeInfo pcibase_info = {
    .name = TYPE_PCIBASE_DEVICE,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIBaseState),
    .class_init = pcibase_class_init,
};

static void pcibase_register_types(void)
{
    type_register_static(&pcibase_info);
}
type_init(pcibase_register_types);
