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

#define TYPE_PCIBASE_DEVICE "i3000_edac_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define VENDOR_ID 0x8086
#define DEVICE_ID 0x2778
#define CLASS_ID 0x0500  /* Memory Controller */
#define I3000_MMR_WINDOW_SIZE 16384
#define I3000_MCHBAR 0x44
#define I3000_MCHBAR_MASK 0xffffc000
#define I3000_EDEAP 0x70
#define I3000_DEAP 0x58
#define I3000_DEAP_GRAIN (1 << 7)
#define I3000_DERRSYN 0x5c
#define I3000_ERRSTS 0xc8
#define I3000_ERRSTS_BITS 0x0b03
#define I3000_ERRSTS_UE 0x0002
#define I3000_ERRSTS_CE 0x0001
#define I3000_ERRCMD 0xca
#define I3000_DRB_SHIFT 25
#define I3000_C0DRB 0x100
#define I3000_C1DRB 0x180
#define I3000_C0DRA 0x108
#define I3000_C1DRA 0x188
#define I3000_C0DRC0 0x120
#define I3000_C0DRC1 0x124

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

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint8_t regs[I3000_MMR_WINDOW_SIZE];
};

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= I3000_MMR_WINDOW_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: read out of bounds: 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        return 0;
    }

    switch (size) {
    case 1:
        val = s->regs[addr];
        break;
    case 2:
        if (addr + 1 < I3000_MMR_WINDOW_SIZE)
            val = lduw_le_p(&s->regs[addr]);
        break;
    case 4:
        if (addr + 3 < I3000_MMR_WINDOW_SIZE)
            val = ldl_le_p(&s->regs[addr]);
        break;
    case 8:
        if (addr + 7 < I3000_MMR_WINDOW_SIZE)
            val = ldq_le_p(&s->regs[addr]);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid size %u\n", __func__, size);
        return 0;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= I3000_MMR_WINDOW_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: write out of bounds: 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        return;
    }

    switch (size) {
    case 1:
        s->regs[addr] = val & 0xff;
        break;
    case 2:
        if (addr + 1 < I3000_MMR_WINDOW_SIZE)
            stw_le_p(&s->regs[addr], val);
        break;
    case 4:
        if (addr + 3 < I3000_MMR_WINDOW_SIZE)
            stl_le_p(&s->regs[addr], val);
        break;
    case 8:
        if (addr + 7 < I3000_MMR_WINDOW_SIZE)
            stq_le_p(&s->regs[addr], val);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid size %u\n", __func__, size);
        return;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo){
        .index = 0,
        .type = BAR_TYPE_MMIO,
        .size = I3000_MMR_WINDOW_SIZE,
        .name = "i3000-mmio"
    };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Set MCHBAR to match BAR0 address after it's allocated */
    pci_set_long(pdev->config + I3000_MCHBAR,
                 pci_get_long(pdev->config + PCI_BASE_ADDRESS_0) & I3000_MCHBAR_MASK);
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

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "i3000_edac_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
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
