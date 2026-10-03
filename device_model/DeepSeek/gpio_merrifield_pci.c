#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "qemu/host-utils.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "hw/pci/pci.h"
#include "hw/pci/pcie.h"
#include "hw/pci/msi.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-core.h"
#include "migration/vmstate.h"
#include "qemu/bswap.h"

#define TYPE_PCIBASE_DEVICE "gpio_merrifield_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x8086
#define DEVICE_ID 0x1199
#define CLASS_ID 0x0c8000

#define GWMR_MRFLD 0x400
#define GWSR_MRFLD 0x418
#define GSIR_MRFLD 0xc00
#define MRFLD_GPIO_BAR0_SIZE 0x1000
#define MRFLD_GPIO_BAR1_SIZE 0x1000

typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM
} BARType;

typedef struct {
    int     index;    /* BAR index 0-5 */
    BARType type;
    hwaddr  size;
    const char *name;
} BARInfo;

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t gwmr;
    uint32_t gwsr;
    uint32_t gsir;

    /* Values read from BAR1 (irq_base, gpio_base) */
    uint32_t irq_base;
    uint32_t gpio_base;
};

/* MMIO Handlers for BAR0 */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = 0;

    switch (addr) {
    case GWMR_MRFLD:
        val = s->gwmr;
        break;
    case GWSR_MRFLD:
        val = s->gwsr;
        break;
    case GSIR_MRFLD:
        val = s->gsir;
        break;
    default:
        /* Return 0 for unknown registers */
        val = 0;
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case GWMR_MRFLD:
        s->gwmr = val;
        break;
    case GWSR_MRFLD:
        s->gwsr = val;
        break;
    case GSIR_MRFLD:
        s->gsir = val;
        break;
    default:
        /* Ignore writes to unknown registers */
        break;
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

/* MMIO Handlers for BAR1 (firmware information - read-only) */
static uint64_t pcibase_bar1_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val;

    switch (addr) {
    case 0:
        val = s->irq_base;
        break;
    case 4:
        val = s->gpio_base;
        break;
    default:
        val = 0;
        break;
    }
    return val;
}

static void pcibase_bar1_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* BAR1 is read-only for firmware values; ignore writes */
}

static const MemoryRegionOps pcibase_bar1_ops = {
    .read = pcibase_bar1_read,
    .write = pcibase_bar1_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* reset device registers to defaults */
    s->gwmr = 0;
    s->gwsr = 0;
    s->gsir = 0;
    /* Set valid GPIO and IRQ base to avoid pin-range creation failure */
    s->irq_base = 0;
    s->gpio_base = 0;
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    /* CRITICAL: PCI requires BAR sizes to be a power of 2. Prevent QEMU assert crash. */
    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
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

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);

    /* Enable MSI to support pci_alloc_irq_vectors with PCI_IRQ_ALL_TYPES */
    if (msi_init(pdev, 0, 1, false, false, errp) < 0) {
        return;
    }

    /* BAR Initialization */
    s->num_bars = 2;
    s->bar_info[0] = (BARInfo){0, BAR_TYPE_MMIO, MRFLD_GPIO_BAR0_SIZE, "mrfl-gpio-bar0"};
    s->bar_info[1] = (BARInfo){1, BAR_TYPE_MMIO, MRFLD_GPIO_BAR1_SIZE, "mrfl-gpio-bar1"};

    /* Register BAR0 with existing MMIO ops */
    MemoryRegion *bar0 = &s->bar_regions[0];
    memory_region_init_io(bar0, OBJECT(s), &pcibase_mmio_ops, s, "mrfl-gpio-bar0",
                          pow2ceil(MRFLD_GPIO_BAR0_SIZE));
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, bar0);

    /* Register BAR1 with special firmware-data ops */
    MemoryRegion *bar1 = &s->bar_regions[1];
    memory_region_init_io(bar1, OBJECT(s), &pcibase_bar1_ops, s, "mrfl-gpio-bar1",
                          pow2ceil(MRFLD_GPIO_BAR1_SIZE));
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, bar1);

    /* Ensure firmware values are set to valid defaults */
    s->irq_base = 0;
    s->gpio_base = 0;
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    msi_uninit(pdev);
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "gpio_merrifield_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(gwmr, PCIBaseState),
        VMSTATE_UINT32(gwsr, PCIBaseState),
        VMSTATE_UINT32(gsir, PCIBaseState),
        VMSTATE_UINT32(irq_base, PCIBaseState),
        VMSTATE_UINT32(gpio_base, PCIBaseState),
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
type_init(pcibase_register_types)
