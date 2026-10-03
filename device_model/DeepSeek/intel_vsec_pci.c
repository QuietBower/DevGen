/*
 * QEMU Intel VSEC PCI device model (QEMU 8.2.10)
 * Emulates a PCI device with Intel DVSEC capabilities for the intel_vsec driver.
 * This device presents a telemetry DVSEC and a BAR0 discovery table region.
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

#define TYPE_PCIBASE_DEVICE "intel_vsec_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Vendor / Device IDs from intel_vsec_pci_ids (first entry assumed) */
#define PCI_VENDOR_ID_INTEL_VSEC 0x8086
#define PCI_DEVICE_ID_INTEL_VSEC_ADL 0x467d

/* Intel DVSEC register offsets (provided by driver) */
#define INTEL_DVSEC_ENTRIES         0xA
#define INTEL_DVSEC_SIZE            0xB
#define INTEL_DVSEC_TABLE           0xC

#define VSEC_ID_TELEMETRY           0x00   /* Placeholder: actual value unknown, set to 0 */

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
};

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }
    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), NULL, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), NULL, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    /* Intercept DVSEC extended capability at 0x100 */
    if (addr >= 0x100 && addr < 0x100 + 0x20) {
        uint32_t offset = addr - 0x100;
        uint32_t val = 0;

        switch (offset) {
        case 0x00: /* PCIe Extended Capability Header */
            val = 0x00230000; /* Cap ID = DVSEC (0x0023), Next = 0x0000 */
            break;
        case 0x04: /* Designated Vendor-Specific Header1 */
            val = (PCI_VENDOR_ID_INTEL_VSEC & 0xFFFF) |
                  (1 << 16) |          /* Revision 1 */
                  (0x20 << 20);        /* Length 0x20 bytes (covers Intel fields) */
            break;
        case 0x08: /* Designated Vendor-Specific Header2 */
            val = VSEC_ID_TELEMETRY;   /* Lower 16 bits */
            break;
        case INTEL_DVSEC_ENTRIES: /* 0xA */
            val = 1;                   /* num_entries = 1 */
            break;
        case INTEL_DVSEC_SIZE:    /* 0xB */
            val = 4;                   /* entry_size = 4 */
            break;
        case INTEL_DVSEC_TABLE:   /* 0xC */
            /* Table register [31:8] = offset, [7:0] = BAR index */
            /* Here: BAR0, offset 0 */
            val = (0 << 8) | 0;
            break;
        default:
            val = 0;
            break;
        }

        /* Extract requested bytes */
        uint32_t shift = (addr & 0x3) * 8;
        uint32_t mask = (len == 4) ? 0xFFFFFFFF : ((1 << (len * 8)) - 1);
        return (val >> shift) & mask;
    }

    return pci_default_read_config(pdev, addr, len);
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    /* Allow any writes to the DVSEC range (driver may not write) */
    if (addr >= 0x100 && addr < 0x100 + 0x20) {
        return;
    }
    pci_default_write_config(pdev, addr, val, len);
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_INTEL_VSEC);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_INTEL_VSEC_ADL);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0xff00); /* Others */
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    /* No interrupt needed */

    /* Set PCIe endpoint capability (standard, in regular config space) */
    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);

    /* BAR 0: 1MiB memory region for discovery tables */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_RAM;
    s->bar_info[0].size = 1 * MiB;
    s->bar_info[0].name = "vsec-mmio";
    pcibase_register_bar(pdev, s, &s->bar_info[0], errp);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    /* No MSI or other cleanup needed */
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
}

static const VMStateDescription vmstate_pcibase = {
    .name = "intel_vsec_pci",
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
    k->config_read = pcibase_config_read;
    k->config_write = pcibase_config_write;
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
