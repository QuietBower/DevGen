/*
 * QEMU VIA686A Hardware Monitor PCI Device Model
 * Generated via Phase 2: Implementation
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

#define TYPE_PCIBASE_DEVICE "via686a_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI Vendor / Device / Class IDs and Register Offsets */
#define PCI_VENDOR_ID_VIA                0x1106
#define PCI_DEVICE_ID_VIA_82C686_4       0x3057
#define CLASS_ID                         0x0880

#define VIA686A_EXTENT                   0x80
#define VIA686A_BASE_REG                 0x70
#define VIA686A_ENABLE_REG               0x74
#define VIA686A_REG_IN_MAX(nr)           (0x2b + ((nr) * 2))
#define VIA686A_REG_IN_MIN(nr)           (0x2c + ((nr) * 2))
#define VIA686A_REG_IN(nr)               (0x22 + (nr))
#define VIA686A_REG_FAN_MIN(nr)          (0x3a + (nr))
#define VIA686A_REG_FAN(nr)              (0x28 + (nr))
#define VIA686A_REG_TEMP_LOW1            0x4b
#define VIA686A_REG_TEMP_LOW23            0x49
#define VIA686A_REG_ALARM1               0x41
#define VIA686A_REG_ALARM2               0x42
#define VIA686A_REG_FANDIV               0x47
#define VIA686A_REG_CONFIG               0x40
#define VIA686A_REG_TEMP_MODE            0x4b
#define VIA686A_TEMP_MODE_MASK           0x3F
#define VIA686A_TEMP_MODE_CONTINUOUS     0x00

#define VIA686A_REG_TEMP_OVER(nr)        (0x1d - (nr) * 0x1c)  /* derived from static array: 0x39, 0x3d, 0x1d */
#define VIA686A_REG_TEMP_HYST(nr)        (0x1e - (nr) * 0x1c)  /* derived from static array: 0x3a, 0x3e, 0x1e */

/* BAR types */
typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM 
} BARType;

typedef struct {
    int     index;
    BARType type;
    hwaddr  size;
    const char *name;
} BARInfo;

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* I/O register file */
    uint8_t regs[VIA686A_EXTENT];
};

/* MMIO Handlers (unused but required for template compatibility) */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

}

/* PIO Handlers */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr < VIA686A_EXTENT) {
        val = s->regs[addr];
    }
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < VIA686A_EXTENT) {
        s->regs[addr] = (uint8_t)val;
    }
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

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    memset(s->regs, 0, VIA686A_EXTENT);
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }
    
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

    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_VIA);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_VIA_82C686_4);
    pci_config_set_class(pci_conf, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x20);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_VENDOR_ID, 0x1106);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_ID, 0x3057);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Set default I/O base address for hardware monitor (offset 0x70) and SMBus (offset 0x74) */
    pci_set_word(pci_conf + 0x70, 0x2e1);  /* Hardware monitor I/O base: port 0x2e0, enabled */
    pci_set_word(pci_conf + 0x74, 0x5001); /* SMBus I/O base: port 0x5000, enabled */

    /* Make PCI config space writable for BASE and ENABLE registers */
    pdev->wmask[0x70] = 0xff;
    pdev->wmask[0x71] = 0xff;
    pdev->wmask[0x74] = 0xff;
    pdev->wmask[0x75] = 0xff;

    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR0: I/O ports covering the hardware monitor registers */
    s->num_bars = 2;
    s->bar_info[0] = (BARInfo){
        .index = 0,
        .type = BAR_TYPE_PIO,
        .size = VIA686A_EXTENT,
        .name = "via686a-io"
    };
    /* BAR4: I/O ports for SMBus host */
    s->bar_info[4] = (BARInfo){
        .index = 4,
        .type = BAR_TYPE_PIO,
        .size = 0x10,
        .name = "via686a-smbus"
    };
    for (int i = 0; i < 6; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

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
    .name = "via686a_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT8_ARRAY(regs, PCIBaseState, VIA686A_EXTENT),
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

type_init(pcibase_register_types);