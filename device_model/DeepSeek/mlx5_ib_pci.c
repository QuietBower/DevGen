/*
 * QEMU PCI device model for ConnectX-8 Data Direct (based on mlx5_data_direct driver)
 * Phase 2: Functional implementation with VPD support.
 */

#include "qemu/osdep.h"
#include "qemu/host-utils.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "hw/pci/pci.h"
#include "hw/pci/pcie.h"
#include "hw/pci/pci_device.h"
#include "hw/qdev-core.h"
#include "migration/vmstate.h"

#define TYPE_PCIBASE_DEVICE "mlx5_ib_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI IDs and Class extracted from driver */
#define PCI_VENDOR_ID_MELLANOX 0x15b3
#define PCI_DEVICE_ID_CONNECTX8_DATA_DIRECT 0x2100

/* No device-specific registers are used by the driver; dummy BAR structures kept for compatibility. */
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

    /* Resource Management (unused by this driver, but kept as placeholder) */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;
    
    int vpd_cap_offset;
    uint16_t vpd_addr;
};

/*
 * VPD data containing the "VU" keyword with a unique ID.
 * This is required by the driver to extract the vendor unique identifier.
 */
static const uint8_t mlx5_data_direct_vpd[] = {
    0x82, 0x08, 0x00, /* VPD-R header (read-only resource, length 8) */
    'V', 'U', 0x04,   /* Keyword VU, data length 4 */
    '0', '0', '0', '0', /* Data: a dummy unique ID */
    0x07,             /* Checksum: sum of all bytes from 0x82 to this byte is 0x100 */
};

static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint32_t val = pci_default_read_config(pdev, addr, len);
    if (s->vpd_cap_offset && addr >= s->vpd_cap_offset && addr < s->vpd_cap_offset + 6) {
        switch (addr - s->vpd_cap_offset) {
        case 0:
        case 1:
            break;
        case 2: /* VPD address register */
            if (len == 2) {
                return s->vpd_addr;
            }
            break;
        case 4: /* VPD data register */
            if (len == 4) {
                uint32_t data = 0;
                uint16_t offset = s->vpd_addr & ~0x8000; /* clear flag */
                if (offset < sizeof(mlx5_data_direct_vpd)) {
                    memcpy(&data, mlx5_data_direct_vpd + offset,
                           MIN(sizeof(mlx5_data_direct_vpd) - offset, 4));
                }
                return data;
            }
            break;
        }
    }
    return val;
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    pci_default_write_config(pdev, addr, val, len);
    if (s->vpd_cap_offset && addr >= s->vpd_cap_offset && addr < s->vpd_cap_offset + 6) {
        switch (addr - s->vpd_cap_offset) {
        case 2:
            if (len == 2) {
                s->vpd_addr = val;
            }
            break;
        case 4:
            /* read-only, ignore writes */
            break;
        }
    }
}

/* MMIO/PIO handlers: not used, but defined to avoid compilation errors */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
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
    pci_device_reset(PCI_DEVICE(dev));
}

/* BAR registration function (kept but will not be used since num_bars=0) */
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

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_MELLANOX);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_CONNECTX8_DATA_DIRECT);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0c06);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* VPD capability: register manually to avoid missing VPD API */
    s->vpd_cap_offset = pci_add_capability(pdev, PCI_CAP_ID_VPD, 0, 6, errp);
    s->vpd_addr = 0;
    pdev->config_read = pcibase_config_read;
    pdev->config_write = pcibase_config_write;

    /* No BARs are registered: num_bars = 0 */
    s->num_bars = 0;
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    /* No MSI/MSI-X or other dynamic resources to clean up */
}

static const VMStateDescription vmstate_pcibase = {
    .name = "mlx5_ib_pci",
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
