/* Template with functional behavior from adl_pci8164.c */
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

/* Driver-specific macros */
#define PCI_VENDOR_ID_ADLINK 0x144A
#define PCI8164_AXIS(x)       ((x) * 0x08)
#define PCI8164_CMD_MSTS_REG  0x00
#define PCI8164_OTP_SSTS_REG  0x02
#define PCI8164_BUF0_REG      0x04
#define PCI8164_BUF1_REG      0x06
#define PCI8164_BAR2_SIZE     0x20

#define TYPE_PCIBASE_DEVICE "adl_pci8164_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI_CLASS_ID 0x118000

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
    /* Shadow registers: 4 axes, each with 4 16-bit registers */
    uint16_t regs[4][4];
};

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* No MMIO BAR registered */
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* No MMIO BAR registered */
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    unsigned axis, reg;

    if (addr >= PCI8164_BAR2_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: PIO read out of range at 0x%" HWADDR_PRIx "\n", __func__, addr);
        return 0;
    }
    if (size != 2) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: PIO read with invalid size %u at 0x%" HWADDR_PRIx "\n", __func__, size, addr);
        return 0;
    }

    axis = addr / 0x08;
    reg = (addr % 0x08) / 2;
    val = s->regs[axis][reg];
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    unsigned axis, reg;

    if (addr >= PCI8164_BAR2_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: PIO write out of range at 0x%" HWADDR_PRIx "\n", __func__, addr);
        return;
    }
    if (size != 2) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: PIO write with invalid size %u at 0x%" HWADDR_PRIx "\n", __func__, size, addr);
        return;
    }

    axis = addr / 0x08;
    reg = (addr % 0x08) / 2;
    s->regs[axis][reg] = (uint16_t)val;
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
    memset(s->regs, 0, sizeof(s->regs));
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

    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_ADLINK);
    pci_set_word(pci_conf + PCI_DEVICE_ID, 0x8164);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR2: I/O ports, size 0x20 */
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo){
        .index = 2,
        .type = BAR_TYPE_PIO,
        .size = PCI8164_BAR2_SIZE,
        .name = "adl_pci8164-pio"
    };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize shadow registers */
    memset(s->regs, 0, sizeof(s->regs));
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
    .name = "adl_pci8164_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT16_2DARRAY(regs, PCIBaseState, 4, 4),
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
