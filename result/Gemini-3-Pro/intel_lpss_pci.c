/*
 * QEMU model for intel-lpss-pci
 *
 * Based on Linux driver: drivers/mfd/intel-lpss-pci.c
 * and core driver: drivers/mfd/intel-lpss.c
 */

#include "qemu/osdep.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "qemu/log.h"
#include "qom/object.h"

#define TYPE_PCIBASE_DEVICE "intel_lpss_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI_VENDOR_ID_INTEL 0x8086
#define DEVICE_ID 0x02a8
#define CLASS_ID PCI_CLASS_OTHERS

#define LPSS_PRIV_OFFSET	0x200
#define LPSS_PRIV_SIZE		0x100
#define LPSS_PRIV_CAPS		0xfc

/* Newly added definitions from driver source */
#define LPSS_PRIV_RESETS		0x04
#define LPSS_PRIV_SSP_REG		0x20
#define LPSS_PRIV_SSP_REG_DIS_DMA_FIN	0x1   /* BIT(0) */
#define LPSS_PRIV_CAPS_TYPE_MASK	0xf0  /* GENMASK(7, 4) */
#define LPSS_PRIV_CAPS_TYPE_SHIFT	4
#define LPSS_PRIV_CAPS_NO_IDMA		0x100 /* BIT(8) */

struct PCIBaseState {
    PCIDevice parent_obj;
    MemoryRegion mmio;
    bool has_msi;

    /* Registers */
    uint32_t resets;
    uint32_t ssp_reg;
    uint32_t caps;
};

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    
    /*
     * Driver accesses are relative to lpss->priv, which is at LPSS_PRIV_OFFSET (0x200).
     * So absolute offsets are 0x200 + register_offset.
     */
    switch (addr) {
    case LPSS_PRIV_OFFSET + LPSS_PRIV_RESETS:
        return s->resets;
    case LPSS_PRIV_OFFSET + LPSS_PRIV_SSP_REG:
        return s->ssp_reg;
    case LPSS_PRIV_OFFSET + LPSS_PRIV_CAPS:
        /* 
         * Driver checks (caps & LPSS_PRIV_CAPS_TYPE_MASK) >> SHIFT.
         * Returning 0 implies type 0. 
         */
        return s->caps;
    default:
        break;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] mmio_read addr=0x%" HWADDR_PRIx " size=%u\n",
                  TYPE_PCIBASE_DEVICE, addr, size);
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case LPSS_PRIV_OFFSET + LPSS_PRIV_RESETS:
        /* 
         * Driver writes LPSS_PRIV_RESETS_FUNC | LPSS_PRIV_RESETS_IDMA 
         * to deassert reset.
         */
        s->resets = val;
        break;
    case LPSS_PRIV_OFFSET + LPSS_PRIV_SSP_REG:
        s->ssp_reg = val;
        break;
    /* 
     * LPSS_PRIV_REMAP_ADDR is written by intel_lpss_set_remap_addr,
     * but the offset definition is missing in the provided source.
     */
    default:
        qemu_log_mask(LOG_UNIMP, "[%s] mmio_write addr=0x%" HWADDR_PRIx " val=0x%" PRIx64 " size=%u\n",
                      TYPE_PCIBASE_DEVICE, addr, val, size);
        break;
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_INTEL);
    pci_set_word(pci_conf + PCI_DEVICE_ID, DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* 
     * Driver uses pci_resource_n(pdev, 0).
     * We allocate a 4KB MMIO region for BAR 0.
     */
    memory_region_init_io(&s->mmio, OBJECT(s), &pcibase_mmio_ops, s,
                          "lpss-mmio", 0x1000);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mmio);

    /* Driver requests PCI_IRQ_ALL_TYPES */
    if (msi_init(pdev, 0, 1, true, false, errp) == 0) {
        s->has_msi = true;
    }
    
    /* Initialize default register values */
    s->caps = 0x00000000;
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (s->has_msi) {
        msi_uninit(pdev);
        s->has_msi = false;
    }
}

static void pcibase_reset(DeviceState *dev)
{
    PCIDevice *pdev = PCI_DEVICE(dev);
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    
    pci_device_reset(pdev);
    
    s->resets = 0;
    s->ssp_reg = 0;
}

static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pcibase_realize;
    k->exit = pcibase_uninit;
    dc->reset = pcibase_reset;
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