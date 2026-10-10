#include "qemu/osdep.h"
#include <inttypes.h>
#include <string.h>
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "sysemu/dma.h"
#include "sysemu/reset.h"
#include "hw/irq.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/pci/pcie.h"
#include "qom/object.h"
#include "qapi/visitor.h"
#include "hw/qdev-properties.h"

#define TYPE_PCIBASE_DEVICE "phantom_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PHANTOM_VERSION "n0.9.8"
#define PHANTOM_MAX_MINORS 8
#define PHN_IRQCTL 0x4c
#define PHB_RUNNING 1
#define PHB_NOT_OH 2
#define PCI_VENDOR_ID_PLX 0x10b5
#define PCI_DEVICE_ID_PLX_9050 0x9050

/* Unknown exact values, but referenced by driver; keep as opaque indices */
#define PHN_CONTROL 0 /* control register offset within input BAR */
#define PHN_CTL_IRQ 0x1
#define PHN_CTL_AMP 0x2

#define PHANTOM_BAR_CONF 0
#define PHANTOM_BAR_IN   2
#define PHANTOM_BAR_OUT  3

#define PHANTOM_BAR_CONF_SIZE 0x100
#define PHANTOM_BAR_IN_SIZE   0x20
#define PHANTOM_BAR_OUT_SIZE  0x20

/* BAR metadata definition */
typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM
} BARType;

typedef struct {
    int index;
    BARType type;
    hwaddr size;
    const char *name;
    bool sparse;
} BARInfo;

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];

    uint8_t *mmio_backing;
    size_t mmio_backing_size;

    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;

    /* Phantom-specific state derived from driver */
    uint32_t status;
    uint32_t ctl_reg;

    /* simple register sets for in/out BARs (8 regs each, 32-bit) */
    uint32_t in_regs[8];
    uint32_t out_regs[8];

    /* config BAR registers including PHN_IRQCTL */
    uint32_t conf_regs[0x100 / 4];

    /* emulated interrupt line state and IRQ enable (from PHN_IRQCTL) */
    qemu_irq irq;
};

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len);
static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len);
static void pcibase_reset(DeviceState *dev);
static void pcibase_realize(PCIDevice *pdev, Error **errp);
static void pcibase_uninit(PCIDevice *pdev);

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
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

    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, bi->size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

/* Forward decl for local helper */
static void phantom_update_irq(PCIBaseState *s)
{
    /* PHN_IRQCTL at conf BAR controls IRQ enable; driver writes 0/0x43 */
    uint32_t irqctl = s->conf_regs[PHN_IRQCTL / 4];
    if (irqctl) {
        qemu_set_irq(s->irq, 1);
    } else {
        qemu_set_irq(s->irq, 0);
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    /* Determine which BAR this access belongs to by region size/layout */
    if (addr < PHANTOM_BAR_CONF_SIZE) {
        /* conf BAR: BAR0 */
        if (size != 4) {
            return 0;
        }
        if (addr % 4 != 0) {
            return 0;
        }
        uint32_t index = addr / 4;
        if (index >= (PHANTOM_BAR_CONF_SIZE / 4)) {
            return 0;
        }
        return s->conf_regs[index];
    }

    qemu_log_mask(LOG_UNIMP, "[phantom_pci] mmio_read addr=%" PRIx64 " size=%u\n", (uint64_t)addr, size);
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* Conf BAR (BAR0) handling */
    if (addr < PHANTOM_BAR_CONF_SIZE) {
        if (size != 4 || (addr % 4)) {
            return;
        }
        uint32_t index = addr / 4;
        if (index >= (PHANTOM_BAR_CONF_SIZE / 4)) {
            return;
        }
        s->conf_regs[index] = (uint32_t)val;
        if (addr == PHN_IRQCTL) {
            /* Driver uses PHN_IRQCTL to enable/disable interrupts */
            phantom_update_irq(s);
        }
        return;
    }

    qemu_log_mask(LOG_UNIMP, "[phantom_pci] mmio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n", (uint64_t)addr, (uint64_t)val, size);
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;

    qemu_log_mask(LOG_UNIMP, "[phantom_pci] pio_read addr=%" PRIx64 " size=%u\n", (uint64_t)addr, size);
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;

    qemu_log_mask(LOG_UNIMP, "[phantom_pci] pio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n", (uint64_t)addr, (uint64_t)val, size);
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    pci_device_reset(pdev);

    if (s->mmio_backing && s->mmio_backing_size) {
        memset(s->mmio_backing, 0, s->mmio_backing_size);
    }

    memset(s->in_regs, 0, sizeof(s->in_regs));
    memset(s->out_regs, 0, sizeof(s->out_regs));
    memset(s->conf_regs, 0, sizeof(s->conf_regs));
    s->status = 0;
    s->ctl_reg = 0;
    phantom_update_irq(s);
}

static void pcibase_dma_device_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    (void)s;
    (void)errp;
}

static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    uint32_t val = pci_default_read_config(pdev, addr, len);

    switch (len) {
    case 1:
        val &= 0xFF;
        break;
    case 2:
        val &= 0xFFFF;
        break;
    case 4:
        break;
    default:
        break;
    }
    return val;
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    if (addr >= PCI_BASE_ADDRESS_0 && addr <= PCI_BASE_ADDRESS_5) {
        pci_default_write_config(pdev, addr, val, len);
        return;
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_PLX);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_PLX_9050);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_VENDOR_ID, PCI_VENDOR_ID_PLX);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_ID,        PCI_DEVICE_ID_PLX_9050);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_BRIDGE_OTHER);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    s->num_bars = 0;

    s->bar_info[s->num_bars].index = PHANTOM_BAR_CONF;
    s->bar_info[s->num_bars].type = BAR_TYPE_MMIO;
    s->bar_info[s->num_bars].size = PHANTOM_BAR_CONF_SIZE;
    s->bar_info[s->num_bars].name = "phantom-conf";
    s->bar_info[s->num_bars].sparse = false;
    s->num_bars++;

    s->bar_info[s->num_bars].index = PHANTOM_BAR_IN;
    s->bar_info[s->num_bars].type = BAR_TYPE_MMIO;
    s->bar_info[s->num_bars].size = PHANTOM_BAR_IN_SIZE;
    s->bar_info[s->num_bars].name = "phantom-in";
    s->bar_info[s->num_bars].sparse = false;
    s->num_bars++;

    s->bar_info[s->num_bars].index = PHANTOM_BAR_OUT;
    s->bar_info[s->num_bars].type = BAR_TYPE_MMIO;
    s->bar_info[s->num_bars].size = PHANTOM_BAR_OUT_SIZE;
    s->bar_info[s->num_bars].name = "phantom-out";
    s->bar_info[s->num_bars].sparse = false;
    s->num_bars++;

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->irq = pci_allocate_irq(pdev);

    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) {
        return;
    }

    qemu_log_mask(LOG_UNIMP, "[phantom_pci] device realized\n");
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (s->has_msix) {
        msix_uninit(pdev, NULL, 0);
        s->has_msix = false;
    }
    if (s->has_msi) {
        msi_uninit(pdev);
        s->has_msi = false;
    }

    if (s->mmio_backing) {
        g_free(s->mmio_backing);
        s->mmio_backing = NULL;
    }

    qemu_log_mask(LOG_UNIMP, "[phantom_pci] device uninit\n");
}

static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->config_read  = pcibase_config_read;
    k->config_write = pcibase_config_write;

    k->realize = pcibase_realize;
    k->exit    = pcibase_uninit;
    dc->reset  = pcibase_reset;

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

