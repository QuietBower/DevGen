#include "qemu/osdep.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_ids.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "exec/memory.h"

#define TYPE_PCIBASE_DEVICE "pcibase"
DECLARE_INSTANCE_CHECKER(PCibaseState, PCIBASE,
                         TYPE_PCIBASE_DEVICE)

#define PCI_VENDOR_ID_REALTEK 0x10ec
#define PCI_DEVICE_ID_RTW8814AE 0x881a

#define RTK_PCI_TXBD_DESA_BKQ 0x320
#define RTK_PCI_TXBD_DESA_BEQ 0x328

typedef struct {
    PCIDevice pdev;
    MemoryRegion mmio;
} PCibaseState;

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    switch (addr) {
    case RTK_PCI_TXBD_DESA_BKQ:
    case RTK_PCI_TXBD_DESA_BEQ:
        return 0;
    default:
        return 0;
    }
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    switch (addr) {
    case RTK_PCI_TXBD_DESA_BKQ:
    case RTK_PCI_TXBD_DESA_BEQ:
        break;
    default:
        break;
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCibaseState *s = PCIBASE(pdev);
    memory_region_init_io(&s->mmio, OBJECT(s), &pcibase_mmio_ops, s, "pcibase-mmio", 0x1000);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mmio);
}

static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    k->realize = pcibase_realize;
    k->vendor_id = PCI_VENDOR_ID_REALTEK;
    k->device_id = PCI_DEVICE_ID_RTW8814AE;
    k->revision = 0;
    dc->desc = "Realtek RTL8814AE PCI Device";
}

static const TypeInfo pcibase_info = {
    .name = TYPE_PCIBASE_DEVICE,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCibaseState),
    .class_init = pcibase_class_init,
};

static void pcibase_register_types(void)
{
    type_register_static(&pcibase_info);
}
type_init(pcibase_register_types);