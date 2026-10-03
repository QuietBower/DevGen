#include "qemu/osdep.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_device.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "sysemu/dma.h"

#define TYPE_VMW_PVRDMA_PCI "vmw_pvrdma_pci"
OBJECT_DECLARE_SIMPLE_TYPE(VMWPVRDMAPCIState, VMW_PVRDMA_PCI)

#define PCI_VENDOR_ID_VMWARE  0x15AD
#define PCI_DEVICE_ID_PVRDMA  0x0820

#define PVRDMA_MMIO_BAR       0
#define PVRDMA_MMIO_SIZE      (16 * 1024)  /* 16KB */

typedef struct {
    PCIDevice pdev;
    MemoryRegion mmio;
} VMWPVRDMAPCIState;

static uint64_t vmw_pvrdma_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* dummy read */
    return 0;
}

static void vmw_pvrdma_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* dummy write */
}

static const MemoryRegionOps vmw_pvrdma_mmio_ops = {
    .read = vmw_pvrdma_mmio_read,
    .write = vmw_pvrdma_mmio_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
};

static void vmw_pvrdma_pci_realize(PCIDevice *pdev, Error **errp)
{
    VMWPVRDMAPCIState *s = VMW_PVRDMA_PCI(pdev);

    pci_set_word(pdev->config + PCI_COMMAND, PCI_COMMAND_MEMORY);

    memory_region_init_io(&s->mmio, OBJECT(s), &vmw_pvrdma_mmio_ops, s,
                          "vmw_pvrdma-mmio", PVRDMA_MMIO_SIZE);
    pci_register_bar(pdev, PVRDMA_MMIO_BAR, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mmio);
}

static void vmw_pvrdma_pci_exit(PCIDevice *pdev)
{
    /* nothing to do */
}

static const VMStateDescription vmw_pvrdma_vmstate = {
    .name = TYPE_VMW_PVRDMA_PCI,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(pdev, VMWPVRDMAPCIState),
        VMSTATE_END_OF_LIST()
    }
};

static void vmw_pvrdma_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = vmw_pvrdma_pci_realize;
    k->exit = vmw_pvrdma_pci_exit;
    k->vendor_id = PCI_VENDOR_ID_VMWARE;
    k->device_id = PCI_DEVICE_ID_PVRDMA;
    k->revision = 0;
    k->class_id = 0x0c06; /* InfiniBand subclass */
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
    dc->desc = "VMware Paravirtual RDMA";
    dc->vmsd = &vmw_pvrdma_vmstate;
}

static const TypeInfo vmw_pvrdma_pci_info = {
    .name          = TYPE_VMW_PVRDMA_PCI,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(VMWPVRDMAPCIState),
    .class_init    = vmw_pvrdma_pci_class_init,
};

static void vmw_pvrdma_pci_register_types(void)
{
    type_register_static(&vmw_pvrdma_pci_info);
}

type_init(vmw_pvrdma_pci_register_types)
