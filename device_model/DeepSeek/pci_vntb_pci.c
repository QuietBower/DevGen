/*
 * QEMU PCI device model for NTB endpoint function (vntb).
 * Generated from pci-epf-vntb.c driver analysis.
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

#define TYPE_PCIBASE_DEVICE "pci_vntb_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0xffff
#define DEVICE_ID 0xffff
#define CLASS_ID 0x058000

/* Register offsets for BAR0 (Config BAR) based on struct epf_ntb_ctrl */
#define REG_COMMAND             0x00
#define REG_ARGUMENT            0x04
#define REG_COMMAND_STATUS      0x08
#define REG_LINK_STATUS         0x0a
#define REG_TOPOLOGY            0x0c
#define REG_ADDR                0x10
#define REG_SIZE                0x18
#define REG_NUM_MWS             0x20
#define REG_RESERVED            0x24
#define REG_SPAD_OFFSET         0x28
#define REG_SPAD_COUNT          0x2c
#define REG_DB_ENTRY_SIZE       0x30
#define REG_DB_DATA             0x34
#define REG_DB_OFFSET           0xb4
#define REG_CTRL_SIZE           0x200

#define MAX_DB_COUNT            32
#define BAR0_SIZE               0x1000
#define BAR1_SIZE               0x1000
#define MW_BAR_SIZE             0x100000

/* Hardware register structure from driver */
struct epf_ntb_ctrl {
    uint32_t command;
    uint32_t argument;
    uint16_t command_status;
    uint16_t link_status;
    uint32_t topology;
    uint64_t addr;
    uint64_t size;
    uint32_t num_mws;
    uint32_t reserved;
    uint32_t spad_offset;
    uint32_t spad_count;
    uint32_t db_entry_size;
    uint32_t db_data[MAX_DB_COUNT];
    uint32_t db_offset[MAX_DB_COUNT];
};

struct PCIBaseState {
    PCIDevice parent_obj;

    /* BAR memory regions */
    MemoryRegion bar0;
    MemoryRegion bar1;
    MemoryRegion bar2;
    MemoryRegion bar3;
    MemoryRegion bar4;
    MemoryRegion bar5;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware Register Shadows */
    struct epf_ntb_ctrl reg;

    /* Extra space after control struct for SPAD */
    uint8_t bar0_extra[BAR0_SIZE - sizeof(struct epf_ntb_ctrl)];

    /* Doorbell BAR memory buffer */
    uint8_t db_mem[BAR1_SIZE];

    /* Operational status flags */
    bool linkup;
};

/* BAR0 MMIO read handler */
static uint64_t pcibase_bar0_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr < sizeof(struct epf_ntb_ctrl)) {
        /* Register fields */
        switch (addr) {
        case REG_COMMAND:
            val = s->reg.command;
            break;
        case REG_ARGUMENT:
            val = s->reg.argument;
            break;
        case REG_COMMAND_STATUS:
            val = s->reg.command_status;
            break;
        case REG_LINK_STATUS:
            val = s->reg.link_status;
            break;
        case REG_TOPOLOGY:
            val = s->reg.topology;
            break;
        case REG_ADDR:
            val = s->reg.addr;
            break;
        case REG_SIZE:
            val = s->reg.size;
            break;
        case REG_NUM_MWS:
            val = s->reg.num_mws;
            break;
        case REG_RESERVED:
            val = s->reg.reserved;
            break;
        case REG_SPAD_OFFSET:
            val = s->reg.spad_offset;
            break;
        case REG_SPAD_COUNT:
            val = s->reg.spad_count;
            break;
        case REG_DB_ENTRY_SIZE:
            val = s->reg.db_entry_size;
            break;
        default:
            if (addr >= REG_DB_DATA && addr < REG_DB_DATA + MAX_DB_COUNT * sizeof(uint32_t)) {
                int idx = (addr - REG_DB_DATA) / sizeof(uint32_t);
                val = s->reg.db_data[idx];
            } else if (addr >= REG_DB_OFFSET && addr < REG_DB_OFFSET + MAX_DB_COUNT * sizeof(uint32_t)) {
                int idx = (addr - REG_DB_OFFSET) / sizeof(uint32_t);
                val = s->reg.db_offset[idx];
            } else {
                val = 0;
            }
        }
    } else {
        /* SPAD area */
        hwaddr offset = addr - sizeof(struct epf_ntb_ctrl);
        if (offset < sizeof(s->bar0_extra)) {
            /* Align to access size and extract bytes */
            memcpy(&val, &s->bar0_extra[offset], size > 8 ? 8 : size);
        }
    }
    return val;
}

/* BAR0 MMIO write handler */
static void pcibase_bar0_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < sizeof(struct epf_ntb_ctrl)) {
        switch (addr) {
        case REG_COMMAND:
            s->reg.command = val;
            break;
        case REG_ARGUMENT:
            s->reg.argument = val;
            break;
        case REG_COMMAND_STATUS:
            s->reg.command_status = val & 0xffff;
            break;
        case REG_LINK_STATUS:
            s->reg.link_status = val & 0xffff;
            break;
        case REG_TOPOLOGY:
            s->reg.topology = val;
            break;
        case REG_ADDR:
            s->reg.addr = val;
            break;
        case REG_SIZE:
            s->reg.size = val;
            break;
        case REG_NUM_MWS:
            s->reg.num_mws = val;
            break;
        case REG_RESERVED:
            s->reg.reserved = val;
            break;
        case REG_SPAD_OFFSET:
            s->reg.spad_offset = val;
            break;
        case REG_SPAD_COUNT:
            s->reg.spad_count = val;
            break;
        case REG_DB_ENTRY_SIZE:
            s->reg.db_entry_size = val;
            break;
        default:
            if (addr >= REG_DB_DATA && addr < REG_DB_DATA + MAX_DB_COUNT * sizeof(uint32_t)) {
                int idx = (addr - REG_DB_DATA) / sizeof(uint32_t);
                s->reg.db_data[idx] = val;
            } else if (addr >= REG_DB_OFFSET && addr < REG_DB_OFFSET + MAX_DB_COUNT * sizeof(uint32_t)) {
                int idx = (addr - REG_DB_OFFSET) / sizeof(uint32_t);
                s->reg.db_offset[idx] = val;
            }
        }
    } else {
        /* SPAD area */
        hwaddr offset = addr - sizeof(struct epf_ntb_ctrl);
        if (offset < sizeof(s->bar0_extra)) {
            memcpy(&s->bar0_extra[offset], &val, size > 8 ? 8 : size);
        }
    }
}

static const MemoryRegionOps pcibase_bar0_ops = {
    .read = pcibase_bar0_read,
    .write = pcibase_bar0_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

/* BAR1 (Doorbell) read handler */
static uint64_t pcibase_bar1_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr < BAR1_SIZE) {
        memcpy(&val, &s->db_mem[addr], size > 8 ? 8 : size);
    }
    return val;
}

/* BAR1 (Doorbell) write handler */
static void pcibase_bar1_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    PCIDevice *pdev = PCI_DEVICE(s);

    /* Store the value into doorbell memory */
    if (addr < BAR1_SIZE) {
        memcpy(&s->db_mem[addr], &val, size > 8 ? 8 : size);
    }

    /* If MSI enabled, check for doorbell match and raise interrupt */
    if (msi_enabled(pdev)) {
        for (int i = 0; i < MAX_DB_COUNT; i++) {
            if (s->reg.db_offset[i] == addr) {
                msi_notify(pdev, i);
                break;
            }
        }
    }
}

static const MemoryRegionOps pcibase_bar1_ops = {
    .read = pcibase_bar1_read,
    .write = pcibase_bar1_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    memset(&s->reg, 0, sizeof(s->reg));
    memset(s->bar0_extra, 0, sizeof(s->bar0_extra));
    memset(s->db_mem, 0, sizeof(s->db_mem));
    s->linkup = false;
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID, VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, DEVICE_ID);
    pci_set_byte(pci_conf + PCI_CLASS_PROG, 0x00);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0580);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR0: Configuration and SPAD */
    memory_region_init_io(&s->bar0, OBJECT(s), &pcibase_bar0_ops, s, "bar0", BAR0_SIZE);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar0);

    /* BAR1: Doorbell */
    memory_region_init_io(&s->bar1, OBJECT(s), &pcibase_bar1_ops, s, "bar1", BAR1_SIZE);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar1);

    /* BAR2-5: Memory windows */
    memory_region_init_ram(&s->bar2, OBJECT(s), "bar2", MW_BAR_SIZE, &error_fatal);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar2);
    memory_region_init_ram(&s->bar3, OBJECT(s), "bar3", MW_BAR_SIZE, &error_fatal);
    pci_register_bar(pdev, 3, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar3);
    memory_region_init_ram(&s->bar4, OBJECT(s), "bar4", MW_BAR_SIZE, &error_fatal);
    pci_register_bar(pdev, 4, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar4);
    memory_region_init_ram(&s->bar5, OBJECT(s), "bar5", MW_BAR_SIZE, &error_fatal);
    pci_register_bar(pdev, 5, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar5);

    /* MSI Initialization (16 vectors) */
    if (msi_init(pdev, 0, 16, true, false, errp) < 0) {
        return;
    }

    /* Initialize state */
    pcibase_reset(DEVICE(s));
}

static void pcibase_uninit(PCIDevice *pdev)
{
    msi_uninit(pdev);
}

static const VMStateDescription vmstate_pcibase = {
    .name = "pci_vntb_pci",
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
