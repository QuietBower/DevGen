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

/* Additional include files retrieved from driver context */

#define TYPE_PCIBASE_DEVICE "pata_atp867x_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_ARTOP             0x1191
#define PCI_DEVICE_ID_ARTOP_ATP867A     0x000A
#define PCI_DEVICE_ID_ARTOP_ATP867B     0x000B

#define ATP867X_SYS_INFO_OFFSET         0x3F
#define ATP867X_IO_PORTBASE_OFFSET      0x00
#define ATP867X_IO_DMABASE_OFFSET       0x40

/* Port 0 offsets */
#define ATP867X_PORT0_MSTRPIOSPD_OFFSET (ATP867X_IO_DMABASE_OFFSET + 0x08)
#define ATP867X_PORT0_SLAVPIOSPD_OFFSET (ATP867X_IO_DMABASE_OFFSET + 0x09)
#define ATP867X_PORT0_8BPIOSPD_OFFSET   (ATP867X_IO_DMABASE_OFFSET + 0x0A) /* Also PORTSPD */
#define ATP867X_PORT0_DMAMODE_OFFSET    (ATP867X_IO_DMABASE_OFFSET + 0x0B)
#define ATP867X_PORT0_PREREAD_OFFSET    (ATP867X_IO_PORTBASE_OFFSET + 0x4C)

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
    uint8_t dma_mode;
    uint8_t mstr_piospd;
    uint8_t slave_piospd;
    uint8_t eightb_piospd;
    uint8_t pre_read;
    int pci66mhz;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case ATP867X_PORT0_DMAMODE_OFFSET:
        val = s->dma_mode;
        break;
    case ATP867X_PORT0_MSTRPIOSPD_OFFSET:
        val = s->mstr_piospd;
        break;
    case ATP867X_PORT0_SLAVPIOSPD_OFFSET:
        val = s->slave_piospd;
        break;
    case ATP867X_PORT0_8BPIOSPD_OFFSET:
        val = s->eightb_piospd;
        break;
    case ATP867X_PORT0_PREREAD_OFFSET:
        val = s->pre_read;
        break;
    case ATP867X_SYS_INFO_OFFSET:
        /* Requires ATP867X_IO_SYS_INFO_66MHZ to populate correctly */
        val = 0;
        break;
    default:
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case ATP867X_PORT0_DMAMODE_OFFSET:
        s->dma_mode = val;
        break;
    case ATP867X_PORT0_MSTRPIOSPD_OFFSET:
        s->mstr_piospd = val;
        break;
    case ATP867X_PORT0_SLAVPIOSPD_OFFSET:
        s->slave_piospd = val;
        break;
    case ATP867X_PORT0_8BPIOSPD_OFFSET:
        s->eightb_piospd = val;
        break;
    case ATP867X_PORT0_PREREAD_OFFSET:
        s->pre_read = val;
        break;
    default:
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

    s->dma_mode = 0;
    s->mstr_piospd = 0;
    s->slave_piospd = 0;
    s->eightb_piospd = 0;
    s->pre_read = 0;
    s->pci66mhz = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_ARTOP );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_ARTOP_ATP867A );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_STORAGE_IDE );
    pci_set_byte(pci_conf + PCI_CLASS_PROG, 0x8f);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    s->num_bars = 5;
    s->bar_info[0] = (BARInfo){0, BAR_TYPE_PIO, 8, "atp867x-bar0"};
    s->bar_info[1] = (BARInfo){1, BAR_TYPE_PIO, 4, "atp867x-bar1"};
    s->bar_info[2] = (BARInfo){2, BAR_TYPE_PIO, 8, "atp867x-bar2"};
    s->bar_info[3] = (BARInfo){3, BAR_TYPE_PIO, 4, "atp867x-bar3"};
    s->bar_info[4] = (BARInfo){4, BAR_TYPE_PIO, 256, "atp867x-bar4"};

    /* BAR Initialization */
    for (int i = 0; i < s->num_bars; i++) {
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

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "pata_atp867x_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT8(dma_mode, PCIBaseState),
        VMSTATE_UINT8(mstr_piospd, PCIBaseState),
        VMSTATE_UINT8(slave_piospd, PCIBaseState),
        VMSTATE_UINT8(eightb_piospd, PCIBaseState),
        VMSTATE_UINT8(pre_read, PCIBaseState),
        VMSTATE_INT32(pci66mhz, PCIBaseState),
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
