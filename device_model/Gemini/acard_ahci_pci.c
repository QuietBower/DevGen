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

#define TYPE_PCIBASE_DEVICE "acard_ahci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_ARTOP 0x1191
#define ACARD_AHCI_RX_FIS_SZ 128
#define EM_CTRL_MSG_TYPE              0x000f0000
#define EM_MSG_LED_VALUE_ACTIVITY     0x00070000
#define EM_MSG_LED_PMP_SLOT           0x0000ff00
#define EM_MSG_LED_HBA_PORT           0x0000000f
#define EM_MSG_LED_VALUE_ON           0x00010000
#define EM_MSG_LED_VALUE_OFF          0xfff80000

#define AHCI_PCI_BAR 5
#define PORT_CMD 0x6c

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
    uint32_t intr_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t cap;
    uint32_t cap2;
    uint32_t version;
    uint32_t port_map;

    /* DMA Context */
    dma_addr_t cmd_slot_dma;
    dma_addr_t cmd_tbl_dma;
    dma_addr_t rx_fis_dma;

    uint32_t em_loc;
    uint32_t em_buf_sz;
    uint32_t em_msg_type;
    bool fbs_supported;
    bool fbs_enabled;
};

struct acard_sg {
    uint32_t addr;
    uint32_t addr_hi;
    uint32_t reserved;
    uint32_t size;
};

struct ahci_cmd_hdr {
    uint32_t opts;
    uint32_t status;
    uint32_t tbl_addr;
    uint32_t tbl_addr_hi;
    uint32_t reserved[4];
};

struct ahci_sg {
    uint32_t addr;
    uint32_t addr_hi;
    uint32_t reserved;
    uint32_t flags_size;
};

struct mtip_cmd_hdr {
    uint32_t opts;
    union {
        uint32_t byte_count;
        uint32_t status;
    };
    uint32_t ctba;
    uint32_t ctbau;
    uint32_t res[4];
};

static inline int ahci_nr_ports(uint32_t cap)
{
    return (cap & 0x1f) + 1;
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;
    (void)size;
    uint64_t val = 0;

    if (addr >= 0x100) {
        unsigned int port_offset = (addr - 0x100) % 0x80;
        switch (port_offset) {
        case PORT_CMD:
            val = 0;
            break;
        default:
            break;
        }
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;
    (void)size;

    if (addr >= 0x100) {
        unsigned int port_offset = (addr - 0x100) % 0x80;
        switch (port_offset) {
        case PORT_CMD:
            break;
        default:
            break;
        }
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;
    (void)addr;
    (void)size;
    uint64_t val = 0;

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;
    (void)addr;
    (void)val;
    (void)size;
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
    (void)s;
    pci_device_reset(PCI_DEVICE(dev));
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_ARTOP );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x000d );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_STORAGE_SATA );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    s->num_bars = 6;
    for (int i = 0; i < s->num_bars; i++) {
        s->bar_info[i].type = BAR_TYPE_NONE;
    }
    
    s->bar_info[AHCI_PCI_BAR].index = AHCI_PCI_BAR;
    s->bar_info[AHCI_PCI_BAR].type = BAR_TYPE_MMIO;
    s->bar_info[AHCI_PCI_BAR].size = 0x1000;
    s->bar_info[AHCI_PCI_BAR].name = "ahci-bar5";
      
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    if (msi_init(pdev, 0, 1, true, false, errp)) {
        return;
    }
    s->has_msi = true;
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    (void)s;
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

static const VMStateDescription vmstate_pcibase = {
    .name = "acard_ahci_pci",
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
