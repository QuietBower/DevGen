#include "qemu/osdep.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_device.h"
#include "hw/pci/msix.h"
#include "exec/memory.h"

#define TYPE_PCIBASE_DEVICE "pcibase"

/* Register offsets from driver */
#define VK_BAR0_REG_START  0x0000
#define VK_BAR1_REG_START  0x0000
#define VK_BAR1_MSGQ_NR    0x0000
#define VK_MSIX_MSGQ_MAX   16
#define VK_MSIX_IRQ_MAX    16
#define MSIX_TABLE_OFFSET  0x2000

/* BAR indices */
#define VK_BAR0            0
#define VK_BAR1            1
#define MSIX_TABLE_BAR     2

#define PCIBASE_DEVICE(obj) OBJECT_CHECK(PCIBaseState, (obj), TYPE_PCIBASE_DEVICE)

typedef struct {
    PCIDevice parent_obj;
    MemoryRegion bar_regions[PCI_NUM_BARS];
    /* internal register storage */
    uint32_t regs[0x1000/4]; /* simple 4K register file for BAR0 */
    uint32_t bar1_regs[0x1000/4]; /* for BAR1 */
} PCIBaseState;

static void bar1_write_val(PCIDevice *pdev, unsigned offset, uint32_t val, unsigned size)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (offset < sizeof(s->regs)) {
        /* simple write */
        memcpy(&s->bar1_regs[offset/4], &val, size);
    }
}

static uint64_t pcibase_bar0_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_bar0_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static const MemoryRegionOps pcibase_bar0_ops = {
    .read = pcibase_bar0_read,
    .write = pcibase_bar0_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static uint64_t pcibase_bar1_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_bar1_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static const MemoryRegionOps pcibase_bar1_ops = {
    .read = pcibase_bar1_read,
    .write = pcibase_bar1_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static uint64_t pcibase_msix_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_msix_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static const MemoryRegionOps pcibase_msix_ops = {
    .read = pcibase_msix_read,
    .write = pcibase_msix_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void pcibase_reset(DeviceState *dev)
{
    PCIDevice *pdev = PCI_DEVICE(dev);
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    memset(s->regs, 0, sizeof(s->regs));
    memset(s->bar1_regs, 0, sizeof(s->bar1_regs));

    /* Initialize BAR1 MSGQ NR to max */
    bar1_write_val(pdev, VK_BAR1_MSGQ_NR, VK_MSIX_MSGQ_MAX, 4);
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_conf[PCI_INTERRUPT_PIN] = 1;

    /* Set up BAR0: MMIO, 4K */
    memory_region_init_io(&s->bar_regions[VK_BAR0], OBJECT(s),
                          &pcibase_bar0_ops, s, "bcm-vk-bar0", 0x1000);
    pci_register_bar(pdev, VK_BAR0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_regions[VK_BAR0]);

    /* Set up BAR1: MMIO, 4K */
    memory_region_init_io(&s->bar_regions[VK_BAR1], OBJECT(s),
                          &pcibase_bar1_ops, s, "bcm-vk-bar1", 0x1000);
    pci_register_bar(pdev, VK_BAR1, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_regions[VK_BAR1]);

    /* Set up MSI-X table BAR: MMIO, 8K to fit table and PBA */
    memory_region_init_io(&s->bar_regions[MSIX_TABLE_BAR], OBJECT(s),
                          &pcibase_msix_ops, s, "bcm-vk-msix", 0x8000);
    pci_register_bar(pdev, MSIX_TABLE_BAR, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_regions[MSIX_TABLE_BAR]);

    /* Initialize MSI-X */
    msix_init(pdev, VK_MSIX_IRQ_MAX,
              &s->bar_regions[MSIX_TABLE_BAR], MSIX_TABLE_BAR,
              MSIX_TABLE_OFFSET,
              &s->bar_regions[MSIX_TABLE_BAR], MSIX_TABLE_BAR,
              MSIX_TABLE_OFFSET + VK_MSIX_IRQ_MAX * 16,
              0, errp);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    msix_uninit(pdev, NULL, 0);
}

static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pcibase_realize;
    k->exit = pcibase_uninit;
    dc->reset = pcibase_reset;
    k->vendor_id = 0x14e4;  /* Broadcom */
    k->device_id = 0xd83c;  /* VK device ID from driver */
    k->revision = 0x01;
}

static const TypeInfo pcibase_info = {
    .name          = TYPE_PCIBASE_DEVICE,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIBaseState),
    .class_init    = pcibase_class_init,
};

static void pcibase_register_types(void)
{
    type_register_static(&pcibase_info);
}

type_init(pcibase_register_types)
