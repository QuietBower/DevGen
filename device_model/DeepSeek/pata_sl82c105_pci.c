/*
 * QEMU PCI device model for Winbond SL82C105 IDE controller (pata_sl82c105).
 * Generated during Phase 4: Debug & Update.
 * Fix: Pre-assign BAR addresses to fixed I/O ports to ensure the kernel's
 * resource assignment sees them as already assigned, avoiding failure due
 * to unassigned BARs on PCIe buses.
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

#define TYPE_PCIBASE_DEVICE "pata_sl82c105_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_WINBOND             0x1050
#define PCI_DEVICE_ID_WINBOND_82C105      0x0105

#define BAR0 0
#define BAR1 1
#define BAR4 4  /* bus master DMA */

/* Bus master DMA registers */
#define BM_COMMAND  0x0588
#define BM_STATUS   0x058C

#define BAR0_SIZE 8      /* primary command block */
#define BAR1_SIZE 4      /* primary control block */
#define BAR4_SIZE 16     /* bus master DMA registers */

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

    /* Register storage for BAR regions (PIO) */
    uint8_t bar0_regs[8];
    uint8_t bar1_regs[4];
    uint8_t bar4_regs[16];
};

/* Helper to recover PCIBaseState from a BARInfo pointer used as opaque */
static inline PCIBaseState *get_pc_base_from_bi(BARInfo *bi)
{
    return (PCIBaseState *)((uintptr_t)bi - offsetof(PCIBaseState, bar_info) - bi->index * sizeof(BARInfo));
}

/* IRQ update logic: assert if any unmasked status bits are set */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    if (s->intr_status & s->intr_mask) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* Not used, all BARs are PIO */
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Not used */
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    BARInfo *bi = (BARInfo *)opaque;
    PCIBaseState *s = get_pc_base_from_bi(bi);
    uint8_t *data;
    hwaddr max_size;

    switch (bi->index) {
    case 0:
        data = s->bar0_regs;
        max_size = sizeof(s->bar0_regs);
        break;
    case 1:
        data = s->bar1_regs;
        max_size = sizeof(s->bar1_regs);
        break;
    case 4:
        data = s->bar4_regs;
        max_size = sizeof(s->bar4_regs);
        break;
    default:
        return 0;
    }

    if (addr + size > max_size) {
        return 0xFFFFFFFFFFFFFFFF;
    }

    uint64_t val = 0;
    memcpy(&val, data + addr, MIN(size, sizeof(val)));
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    BARInfo *bi = (BARInfo *)opaque;
    PCIBaseState *s = get_pc_base_from_bi(bi);
    uint8_t *data;
    hwaddr max_size;

    switch (bi->index) {
    case 0: data = s->bar0_regs; max_size = sizeof(s->bar0_regs); break;
    case 1: data = s->bar1_regs; max_size = sizeof(s->bar1_regs); break;
    case 4: data = s->bar4_regs; max_size = sizeof(s->bar4_regs); break;
    default: return;
    }

    if (addr + size > max_size) {
        return;
    }

    memcpy(data + addr, &val, size);
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

    /* Reset all register banks */
    memset(s->bar0_regs, 0, sizeof(s->bar0_regs));
    memset(s->bar1_regs, 0, sizeof(s->bar1_regs));
    memset(s->bar4_regs, 0, sizeof(s->bar4_regs));
    s->intr_status = 0;
    s->intr_mask = 0;
    pcibase_update_irq(s);
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
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, (void*)&s->bar_info[bi->index], bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, (void*)&s->bar_info[bi->index], bi->name, aligned_size);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_WINBOND );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_WINBOND_82C105 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0101 );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x06);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* REMOVED: PCI Express capability to allow assignment of I/O BARs on conventional PCI bus.
     * The device is not natively PCIe and I/O space is required for IDE registers.
     */

    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 3;
    s->bar_info[0].index = BAR0;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = BAR0_SIZE;
    s->bar_info[0].name = "bar0";
    s->bar_info[1].index = BAR1;
    s->bar_info[1].type = BAR_TYPE_PIO;
    s->bar_info[1].size = BAR1_SIZE;
    s->bar_info[1].name = "bar1";
    s->bar_info[2].index = BAR4;
    s->bar_info[2].type = BAR_TYPE_PIO;
    s->bar_info[2].size = BAR4_SIZE;
    s->bar_info[2].name = "bar4";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Pre-assign BAR addresses to fixed I/O ports to ensure they are seen as assigned
     * by the kernel. This avoids -EINVAL from pci_enable_device when the device is on
     * a bus that does not provide I/O resource allocation (e.g., PCIe root port). */
    pci_set_long(pdev->config + PCI_BASE_ADDRESS_0, 0x3001); /* BAR0: I/O 0x3000-0x3007 */
    pci_set_long(pdev->config + PCI_BASE_ADDRESS_1, 0x3009); /* BAR1: I/O 0x3008-0x300B */
    pci_set_long(pdev->config + PCI_BASE_ADDRESS_4, 0x3011); /* BAR4: I/O 0x3010-0x301F */

    /* Field Init Real: set programming interface to native mode (both channels) */
    pci_set_byte(pci_conf + PCI_CLASS_PROG, 0x8A);

    /* Ensure register arrays are zeroed */
    memset(s->bar0_regs, 0, sizeof(s->bar0_regs));
    memset(s->bar1_regs, 0, sizeof(s->bar1_regs));
    memset(s->bar4_regs, 0, sizeof(s->bar4_regs));
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

    /* Placeholder intentionally left empty (no additional cleanup) */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "pata_sl82c105_pci",
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
