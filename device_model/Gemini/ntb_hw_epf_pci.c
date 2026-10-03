/*
 * Integrated QEMU PCI device template (QEMU 8.2.10).
 * Designed for register-level modeling and PCIe driver probing.
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

#define TYPE_PCIBASE_DEVICE "ntb_hw_epf_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#ifndef PCI_VENDOR_ID_TI
#define PCI_VENDOR_ID_TI 0x104c
#endif
#ifndef PCI_DEVICE_ID_TI_J721E
#define PCI_DEVICE_ID_TI_J721E 0xb00d
#endif

#define COMMAND_STATUS_OK	1
#define COMMAND_STATUS_ERROR	2
#define LINK_STATUS_UP		(1 << 0)
#define MSIX_ENABLE		(1 << 16)

#define NTB_EPF_COMMAND		0x0
#define CMD_CONFIGURE_DOORBELL	1
#define CMD_TEARDOWN_DOORBELL	2
#define CMD_CONFIGURE_MW	3
#define CMD_TEARDOWN_MW		4
#define CMD_LINK_UP		5
#define CMD_LINK_DOWN		6
#define NTB_EPF_ARGUMENT	0x4
#define NTB_EPF_CMD_STATUS	0x8
#define NTB_EPF_LINK_STATUS	0x0A
#define NTB_EPF_TOPOLOGY	0x0C
#define NTB_EPF_LOWER_ADDR	0x10
#define NTB_EPF_UPPER_ADDR	0x14
#define NTB_EPF_LOWER_SIZE	0x18
#define NTB_EPF_UPPER_SIZE	0x1C
#define NTB_EPF_MW_COUNT	0x20
#define NTB_EPF_MW1_OFFSET	0x24
#define NTB_EPF_SPAD_OFFSET	0x28
#define NTB_EPF_SPAD_COUNT	0x2C
#define NTB_EPF_DB_ENTRY_SIZE	0x30
#define NTB_EPF_DB_DATA(n)	(0x34 + (n) * 4)
#define NTB_EPF_DB_OFFSET(n)	(0xB4 + (n) * 4)
#define NTB_EPF_MIN_DB_COUNT	3
#define NTB_EPF_MAX_DB_COUNT	31
#define NTB_EPF_COMMAND_TIMEOUT	1000

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

    uint32_t command;
    uint32_t argument;
    uint32_t cmd_status;
    uint32_t link_status;
    uint32_t topology;
    uint32_t lower_addr;
    uint32_t upper_addr;
    uint32_t lower_size;
    uint32_t upper_size;
    uint32_t mw_count;
    uint32_t mw1_offset;
    uint32_t spad_offset;
    uint32_t spad_count;
    uint32_t db_entry_size;
    uint32_t db_data[32];
    uint32_t db_offset[32];
    
    uint32_t spad[32];
};

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    
    if (addr >= 0x34 && addr <= 0xB0) {
        int n = (addr - 0x34) / 4;
        return s->db_data[n];
    }
    if (addr >= 0xB4 && addr <= 0x130) {
        int n = (addr - 0xB4) / 4;
        return s->db_offset[n];
    }
    if (addr >= s->spad_offset && addr < s->spad_offset + s->spad_count * 4) {
        int n = (addr - s->spad_offset) / 4;
        return s->spad[n];
    }

    switch (addr) {
        case NTB_EPF_COMMAND: return s->command;
        case NTB_EPF_ARGUMENT: return s->argument;
        case NTB_EPF_CMD_STATUS: return s->cmd_status;
        case NTB_EPF_LINK_STATUS: return s->link_status;
        case NTB_EPF_TOPOLOGY: return s->topology;
        case NTB_EPF_LOWER_ADDR: return s->lower_addr;
        case NTB_EPF_UPPER_ADDR: return s->upper_addr;
        case NTB_EPF_LOWER_SIZE: return s->lower_size;
        case NTB_EPF_UPPER_SIZE: return s->upper_size;
        case NTB_EPF_MW_COUNT: return s->mw_count;
        case NTB_EPF_MW1_OFFSET: return s->mw1_offset;
        case NTB_EPF_SPAD_OFFSET: return s->spad_offset;
        case NTB_EPF_SPAD_COUNT: return s->spad_count;
        case NTB_EPF_DB_ENTRY_SIZE: return s->db_entry_size;
        default: return 0;
    }
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= 0x800 && addr < 0x800 + 32 * 4) {
        int n = (addr - 0x800) / 4;
        if (msix_enabled(&s->parent_obj)) {
            msix_notify(&s->parent_obj, n);
        } else if (msi_enabled(&s->parent_obj)) {
            msi_notify(&s->parent_obj, n);
        }
        return;
    }
    if (addr >= s->spad_offset && addr < s->spad_offset + s->spad_count * 4) {
        int n = (addr - s->spad_offset) / 4;
        s->spad[n] = val;
        return;
    }

    switch (addr) {
        case NTB_EPF_COMMAND:
            s->command = val;
            s->cmd_status = COMMAND_STATUS_OK;
            if (val == CMD_LINK_UP) {
                s->link_status |= LINK_STATUS_UP;
                if (msix_enabled(&s->parent_obj)) msix_notify(&s->parent_obj, 0);
                else if (msi_enabled(&s->parent_obj)) msi_notify(&s->parent_obj, 0);
            } else if (val == CMD_LINK_DOWN) {
                s->link_status &= ~LINK_STATUS_UP;
                if (msix_enabled(&s->parent_obj)) msix_notify(&s->parent_obj, 0);
                else if (msi_enabled(&s->parent_obj)) msi_notify(&s->parent_obj, 0);
            }
            break;
        case NTB_EPF_ARGUMENT: s->argument = val; break;
        case NTB_EPF_CMD_STATUS: s->cmd_status = val; break;
        case NTB_EPF_LOWER_ADDR: s->lower_addr = val; break;
        case NTB_EPF_UPPER_ADDR: s->upper_addr = val; break;
        case NTB_EPF_LOWER_SIZE: s->lower_size = val; break;
        case NTB_EPF_UPPER_SIZE: s->upper_size = val; break;
        default: break;
    }
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
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    s->cmd_status = 0;
    s->link_status = 0;
    s->mw_count = 1;
    s->spad_count = 32;
    s->spad_offset = 0x200;
    s->db_entry_size = 4;
    for (int i = 0; i < 32; i++) {
        s->db_data[i] = i;
        s->db_offset[i] = 0x800;
        s->spad[i] = 0;
    }
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_TI );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_TI_J721E );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_MEMORY_RAM );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    s->num_bars = 6;
    s->bar_info[0] = (BARInfo){0, BAR_TYPE_MMIO, 0x1000, "bar0"};
    s->bar_info[1] = (BARInfo){1, BAR_TYPE_MMIO, 0x1000, "bar1"};
    s->bar_info[2] = (BARInfo){2, BAR_TYPE_MMIO, 0x1000, "bar2"};
    s->bar_info[3] = (BARInfo){3, BAR_TYPE_MMIO, 0x1000, "bar3"};
    s->bar_info[4] = (BARInfo){4, BAR_TYPE_MMIO, 0x1000, "bar4"};
    s->bar_info[5] = (BARInfo){5, BAR_TYPE_MMIO, 0x1000, "bar5"};
  
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    msi_init(pdev, 0, 32, true, false, errp);
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
    .name = "ntb_hw_epf_pci",
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
