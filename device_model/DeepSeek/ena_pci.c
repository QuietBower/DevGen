/*
 * QEMU emulation of Amazon ENA PCI network device (simplified)
 * Generated for compile fix only.
 */

#include "qemu/osdep.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "qemu/log.h"
#include "hw/qdev-properties.h"

#define TYPE_PCIBASE_DEVICE "ena-pci"
#define PCIBASE_DEVICE(obj) OBJECT_CHECK(PCIBaseState, (obj), TYPE_PCIBASE_DEVICE)

/* Register offsets and bit definitions (approximate, to match driver) */
#define ENA_REGS_VERSION                    0x000
#define ENA_REGS_VERSION_MAJOR_VERSION_SHIFT        24
#define ENA_REGS_VERSION_MINOR_VERSION_SHIFT        16
#define ENA_REGS_VERSION_MINOR_VERSION_MASK         0xFF

#define ENA_REGS_CONTROLLER_VERSION         0x004
#define ENA_REGS_CONTROLLER_VERSION_MAJOR_VERSION_SHIFT    8
#define ENA_REGS_CONTROLLER_VERSION_MINOR_VERSION_SHIFT    0
#define ENA_REGS_CONTROLLER_VERSION_SUB_MINOR_VERSION_SHIFT 16

#define ENA_CTRL_MAJOR   0
#define ENA_CTRL_MINOR   0
#define ENA_CTRL_SUB_MINOR 1

#define ENA_MIN_MSIX_VEC 2

typedef struct PCIBaseState {
    PCIDevice parent_obj;
    MemoryRegion mmio;
    MemoryRegion pio;
    MemoryRegion msix_table_bar;
    MemoryRegion msix_pba_bar;
    /* other state */
} PCIBaseState;

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    uint64_t val = 0;

    switch (addr) {
    case ENA_REGS_VERSION:
        val = (0 << ENA_REGS_VERSION_MAJOR_VERSION_SHIFT) |
              (0 << ENA_REGS_VERSION_MINOR_VERSION_SHIFT);
        break;
    case ENA_REGS_CONTROLLER_VERSION:
        val = (ENA_CTRL_MAJOR << ENA_REGS_CONTROLLER_VERSION_MAJOR_VERSION_SHIFT) |
              (ENA_CTRL_MINOR << ENA_REGS_CONTROLLER_VERSION_MINOR_VERSION_SHIFT) |
              (ENA_CTRL_SUB_MINOR);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "ena: unimplemented read at 0x%" HWADDR_PRIx "\n", addr);
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* placeholder for future writes */
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* PCIBaseState *s = opaque; */
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* PCIBaseState *s = opaque; */
}

static const MemoryRegionOps ena_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static const MemoryRegionOps ena_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    Error *local_err = NULL;

    pci_config_set_interrupt_pin(pdev->config, 1);

    memory_region_init_io(&s->mmio, OBJECT(s), &ena_mmio_ops, s, "ena-mmio", 0x1000);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mmio);

    memory_region_init_io(&s->pio, OBJECT(s), &ena_pio_ops, s, "ena-pio", 0x100);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_IO, &s->pio);

    /* MSI-X: allocate table and PBA bars */
    memory_region_init(&s->msix_table_bar, OBJECT(s), "ena-msix-table", 4096);
    memory_region_init(&s->msix_pba_bar, OBJECT(s), "ena-msix-pba", 4096);
    pci_register_bar(pdev, 4, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->msix_table_bar);
    pci_register_bar(pdev, 5, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->msix_pba_bar);

    msix_init(pdev, ENA_MIN_MSIX_VEC,
              &s->msix_table_bar, 4, 0,
              &s->msix_pba_bar, 5, 0, 0, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    msix_vector_use(pdev, 0);
}

static void pcibase_uninit(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    msix_uninit(pdev, &s->msix_table_bar, &s->msix_pba_bar);
}

static void pcibase_reset(DeviceState *dev)
{
}

static Property pcibase_properties[] = {
    DEFINE_PROP_END_OF_LIST(),
};

static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pcibase_realize;
    k->unrealize = pcibase_uninit;
    k->vendor_id = 0x1d0f;
    k->device_id = 0xec20;
    k->revision = 0;
    k->class_id = PCI_CLASS_NETWORK_ETHERNET;
    dc->reset = pcibase_reset;
    device_class_set_props(dc, pcibase_properties);
}

static const TypeInfo pcibase_info = {
    .name          = TYPE_PCIBASE_DEVICE,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIBaseState),
    .class_init    = pcibase_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void pcibase_register_types(void)
{
    type_register_static(&pcibase_info);
}

type_init(pcibase_register_types)
