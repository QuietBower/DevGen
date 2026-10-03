/*
 * QEMU PCI device model for 3Com Typhoon (3CR990)
 * Based on driver: drivers/net/ethernet/3com/typhoon.c
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

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_3COM               0x10b7
#define PCI_DEVICE_ID_3COM_3CR990        0x990a

#define PCI_CLASS_NETWORK_ETHERNET       0x0200

/* Register offsets */
#define TYPHOON_REG_SOFT_RESET           0x00
#define TYPHOON_REG_INTR_STATUS          0x04
#define TYPHOON_REG_INTR_ENABLE          0x08
#define TYPHOON_REG_INTR_MASK            0x0c
#define TYPHOON_REG_SELF_INTERRUPT       0x10
#define TYPHOON_REG_HOST2ARM6            0x18
#define TYPHOON_REG_HOST2ARM5            0x1c
#define TYPHOON_REG_HOST2ARM4            0x20
#define TYPHOON_REG_HOST2ARM3            0x24
#define TYPHOON_REG_HOST2ARM2            0x28
#define TYPHOON_REG_HOST2ARM1            0x2c
#define TYPHOON_REG_HOST2ARM0            0x30
#define TYPHOON_REG_ARM2HOST3            0x34
#define TYPHOON_REG_ARM2HOST0            0x40
#define TYPHOON_REG_STATUS               TYPHOON_REG_ARM2HOST0

#define TYPHOON_INTR_HOST_INT            0x00000001
#define TYPHOON_INTR_ARM2HOST0           0x00000002
#define TYPHOON_INTR_SELF                0x00000800
#define TYPHOON_INTR_ALL                 0xffffffff
#define TYPHOON_INTR_ENABLE_ALL          0xffffffef
#define TYPHOON_INTR_NONE                0x00000000

#define TYPHOON_STATUS_WAITING_FOR_BOOT  0x07
#define TYPHOON_STATUS_WAITING_FOR_HOST  0x0d
#define TYPHOON_STATUS_WAITING_FOR_SEGMENT 0x10
#define TYPHOON_STATUS_RUNNING           0x09
#define TYPHOON_STATUS_HALTED            0x14
#define TYPHOON_STATUS_SLEEPING          0x11

#define TYPHOON_RESET_ALL    0x7f
#define TYPHOON_RESET_NONE   0x00

struct TyphoonRegs {
    uint32_t soft_reset;
    uint32_t intr_status;
    uint32_t intr_enable;
    uint32_t intr_mask;
    uint32_t self_interrupt;
    uint32_t pad1[2];
    uint32_t host2arm6;
    uint32_t host2arm5;
    uint32_t host2arm4;
    uint32_t host2arm3;
    uint32_t host2arm2;
    uint32_t host2arm1;
    uint32_t host2arm0;
    uint32_t arm2host3;
    uint32_t pad2[2];
    uint32_t arm2host0;
};

#define TYPE_PCIBASE_DEVICE "typhoon_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

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

    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;

    struct TyphoonRegs regs;

    uint32_t status;
    bool reset_needed;
    uint8_t pm_state;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t enabled = s->regs.intr_status & s->regs.intr_enable & ~s->regs.intr_mask;
    if (enabled) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

static void pcibase_reset(DeviceState *dev);

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case TYPHOON_REG_SOFT_RESET:
        val = s->regs.soft_reset;
        break;
    case TYPHOON_REG_INTR_STATUS:
        val = s->regs.intr_status;
        break;
    case TYPHOON_REG_INTR_ENABLE:
        val = s->regs.intr_enable;
        break;
    case TYPHOON_REG_INTR_MASK:
        val = s->regs.intr_mask;
        break;
    case TYPHOON_REG_SELF_INTERRUPT:
        val = s->regs.self_interrupt;
        break;
    case TYPHOON_REG_HOST2ARM6:
        val = s->regs.host2arm6;
        break;
    case TYPHOON_REG_HOST2ARM5:
        val = s->regs.host2arm5;
        break;
    case TYPHOON_REG_HOST2ARM4:
        val = s->regs.host2arm4;
        break;
    case TYPHOON_REG_HOST2ARM3:
        val = s->regs.host2arm3;
        break;
    case TYPHOON_REG_HOST2ARM2:
        val = s->regs.host2arm2;
        break;
    case TYPHOON_REG_HOST2ARM1:
        val = s->regs.host2arm1;
        break;
    case TYPHOON_REG_HOST2ARM0:
        val = s->regs.host2arm0;
        break;
    case TYPHOON_REG_ARM2HOST3:
        val = s->regs.arm2host3;
        break;
    case TYPHOON_REG_ARM2HOST0:
        val = s->status;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: unimplemented MMIO read from 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        val = 0;
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case TYPHOON_REG_SOFT_RESET:
        s->regs.soft_reset = val;
        if (val & TYPHOON_RESET_ALL) {
            pcibase_reset(DEVICE(s));
        }
        break;
    case TYPHOON_REG_INTR_STATUS:
        s->regs.intr_status &= ~val;
        pcibase_update_irq(s);
        break;
    case TYPHOON_REG_INTR_ENABLE:
        s->regs.intr_enable = val;
        pcibase_update_irq(s);
        break;
    case TYPHOON_REG_INTR_MASK:
        s->regs.intr_mask = val;
        pcibase_update_irq(s);
        break;
    case TYPHOON_REG_SELF_INTERRUPT:
        s->regs.self_interrupt = val;
        if (val & 1) {
            s->regs.intr_status |= TYPHOON_INTR_SELF;
            pcibase_update_irq(s);
        }
        break;
    case TYPHOON_REG_HOST2ARM6:
        s->regs.host2arm6 = val;
        break;
    case TYPHOON_REG_HOST2ARM5:
        s->regs.host2arm5 = val;
        break;
    case TYPHOON_REG_HOST2ARM4:
        s->regs.host2arm4 = val;
        break;
    case TYPHOON_REG_HOST2ARM3:
        s->regs.host2arm3 = val;
        break;
    case TYPHOON_REG_HOST2ARM2:
        s->regs.host2arm2 = val;
        break;
    case TYPHOON_REG_HOST2ARM1:
        s->regs.host2arm1 = val;
        break;
    case TYPHOON_REG_HOST2ARM0:
        s->regs.host2arm0 = val;
        break;
    case TYPHOON_REG_ARM2HOST3:
        s->regs.arm2host3 = val;
        break;
    case TYPHOON_REG_ARM2HOST0:
        s->regs.arm2host0 = val;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: unimplemented MMIO write to 0x%" HWADDR_PRIx " val 0x%" PRIx64 "\n",
                      __func__, addr, val);
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return pcibase_mmio_read(opaque, addr, size);
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    pcibase_mmio_write(opaque, addr, val, size);
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
    memset(&s->regs, 0, sizeof(s->regs));
    s->intr_status = 0;
    s->intr_mask = 0;
    s->status = TYPHOON_STATUS_WAITING_FOR_BOOT;
    s->reset_needed = false;
    s->pm_state = 0;
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_3COM );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_3COM_3CR990 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_ETHERNET );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    s->num_bars = 2;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_PIO, .size = 128, .name = "typhoon-bar0" };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_MMIO, .size = 128, .name = "typhoon-bar1" };
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

static const VMStateDescription vmstate_pcibase = {
    .name = "typhoon_pci",
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
