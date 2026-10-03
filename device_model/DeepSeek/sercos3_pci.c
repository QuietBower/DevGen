/*
 * QEMU 8.2.10 virtual PCI device emulating SERCOS III interface (PLX 9030)
 * Based on Linux driver: drivers/uio/uio_sercos3.c
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

#define TYPE_PCIBASE_DEVICE "sercos3_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_PLX      0x10b5
#define PCI_DEVICE_ID_PLX_9030 0x9030
#define SERCOS_SUB_VENDOR_ID   0x1971
#define SERCOS_SUB_SYSID_3530  0x3530
#define IER0_OFFSET 0x08
#define ISR0_OFFSET 0x18
#define SERCOS3_CLASS_ID 0x068000 /* Guess: PCI bridge device */

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

    struct Sercos3Regs {
        uint32_t ier0;   // Interrupt Enable Register at offset 0x08 in BAR4
        uint32_t isr0;   // Interrupt Status Register at offset 0x18 in BAR4
    } regs;
};

/* MMIO Read Handler for all MMIO BARs */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case IER0_OFFSET:
        val = s->regs.ier0;
        break;
    case ISR0_OFFSET:
        val = s->regs.isr0;
        break;
    default:
        /* Unused area: return 0 */
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case IER0_OFFSET:
        s->regs.ier0 = (uint32_t)val;
        break;
    case ISR0_OFFSET:
        /* ISR is read-only in the driver; writes ignored */
        break;
    default:
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* No PIO BARs are used; return 0 */
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* No PIO BARs are used; ignore writes */
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
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_PLX );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_PLX_9030 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, SERCOS3_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* BAR Initialization based on driver: uses BARs 0, 2, 3, 4, 5 */
    s->num_bars = 5;
    BARInfo bar_info[] = {
        { .index = 0, .type = BAR_TYPE_MMIO, .size = 0x1000, .name = "bar0" }, /* size placeholder, need BAR0_SIZE */
        { .index = 2, .type = BAR_TYPE_MMIO, .size = 0x1000, .name = "bar2" }, /* size placeholder, need BAR2_SIZE */
        { .index = 3, .type = BAR_TYPE_MMIO, .size = 0x1000, .name = "bar3" }, /* size placeholder, need BAR3_SIZE */
        { .index = 4, .type = BAR_TYPE_MMIO, .size = 0x1000, .name = "bar4" }, /* size placeholder, need BAR4_SIZE */
        { .index = 5, .type = BAR_TYPE_MMIO, .size = 0x1000, .name = "bar5" }, /* size placeholder, need BAR5_SIZE */
    };
    memcpy(s->bar_info, bar_info, sizeof(bar_info));

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Set subsystem IDs */
    pci_set_word(pci_conf + PCI_SUBSYSTEM_VENDOR_ID, SERCOS_SUB_VENDOR_ID);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_ID, SERCOS_SUB_SYSID_3530);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    /* No MSI/MSI-X cleanup required; driver uses legacy INTx */
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    s->regs.ier0 = 0;
    s->regs.isr0 = 0;
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "sercos3_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(regs.ier0, PCIBaseState),
        VMSTATE_UINT32(regs.isr0, PCIBaseState),
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