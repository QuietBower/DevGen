/*
 * This template provides a robust skeleton for hardware emulation.
 * Integrated QEMU PCI device template (QEMU 8.2.10).
 * Replace #PLACEHOLDER# blocks with driver-specific definitions.
 * Designed for register-level modeling and PCIe driver probing.
 * NOTE: Do not directly fill this file with the Linux kernel source code.
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

#define TYPE_PCIBASE_DEVICE "pata_ninja32_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define NINJA32_VENDOR_ID 0x10FC
#define NINJA32_DEVICE_ID 0x0003

#define NINJA32_REG_IRQ_ENABLE 0x01
#define NINJA32_REG_BURST      0x02
#define NINJA32_REG_UNK3       0x03
#define NINJA32_REG_WAIT0      0x04
#define NINJA32_REG_UNK5       0x05
#define NINJA32_REG_CMD        0x10
#define NINJA32_REG_UNK1C      0x1c
#define NINJA32_REG_BMDMA_CTRL 0x1d
#define NINJA32_REG_CTL        0x1E

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
    uint8_t reg_irq_enable;
    uint8_t reg_burst;
    uint8_t reg_unk3;
    uint8_t reg_wait0;
    uint8_t reg_unk5;
    uint8_t reg_unk1c;
    uint8_t reg_bmdma_ctrl;
    uint8_t reg_1f;
};

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case NINJA32_REG_IRQ_ENABLE:
        val = s->reg_irq_enable;
        break;
    case NINJA32_REG_BURST:
        val = s->reg_burst;
        break;
    case NINJA32_REG_UNK3:
        val = s->reg_unk3;
        break;
    case NINJA32_REG_WAIT0:
        val = s->reg_wait0;
        break;
    case NINJA32_REG_UNK5:
        val = s->reg_unk5;
        break;
    case NINJA32_REG_UNK1C:
        val = s->reg_unk1c;
        break;
    case NINJA32_REG_BMDMA_CTRL:
        val = s->reg_bmdma_ctrl;
        break;
    case 0x1f:
        val = s->reg_1f;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: Unimplemented read at offset 0x%" HWADDR_PRIx "\n", __func__, addr);
        break;
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case NINJA32_REG_IRQ_ENABLE:
        s->reg_irq_enable = val;
        break;
    case NINJA32_REG_BURST:
        s->reg_burst = val;
        break;
    case NINJA32_REG_UNK3:
        s->reg_unk3 = val;
        break;
    case NINJA32_REG_WAIT0:
        s->reg_wait0 = val;
        break;
    case NINJA32_REG_UNK5:
        s->reg_unk5 = val;
        break;
    case NINJA32_REG_UNK1C:
        s->reg_unk1c = val;
        break;
    case NINJA32_REG_BMDMA_CTRL:
        s->reg_bmdma_ctrl = val;
        break;
    case 0x1f:
        s->reg_1f = val;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: Unimplemented write at offset 0x%" HWADDR_PRIx "\n", __func__, addr);
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

    s->reg_irq_enable = 0;
    s->reg_burst = 0;
    s->reg_unk3 = 0;
    s->reg_wait0 = 0;
    s->reg_unk5 = 0;
    s->reg_unk1c = 0;
    s->reg_bmdma_ctrl = 0;
    s->reg_1f = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  NINJA32_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  NINJA32_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_STORAGE_IDE );
    pci_set_byte(pci_conf + PCI_CLASS_PROG, 0x85); /* Native IDE mode */
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
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_PIO, .size = 0x20, .name = "ninja32-pio" };
    
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "pata_ninja32_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT8(reg_irq_enable, PCIBaseState),
        VMSTATE_UINT8(reg_burst, PCIBaseState),
        VMSTATE_UINT8(reg_unk3, PCIBaseState),
        VMSTATE_UINT8(reg_wait0, PCIBaseState),
        VMSTATE_UINT8(reg_unk5, PCIBaseState),
        VMSTATE_UINT8(reg_unk1c, PCIBaseState),
        VMSTATE_UINT8(reg_bmdma_ctrl, PCIBaseState),
        VMSTATE_UINT8(reg_1f, PCIBaseState),
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
