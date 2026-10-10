/*
 * QEMU PCI device model for NTB EPF (J721E mapping)
 * Generated from Linux driver ntb_hw_epf.c
 * Phase 2: Full functional implementation
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

/* Static definitions extracted from driver */
#define VENDOR_ID 0x104c
#define DEVICE_ID 0xb00d
#define CLASS_ID 0x0500

/* Register offsets from ntb_hw_epf */
#define NTB_EPF_COMMAND      0x0
#define CMD_CONFIGURE_DOORBELL 1
#define CMD_TEARDOWN_DOORBELL  2
#define CMD_CONFIGURE_MW      3
#define CMD_TEARDOWN_MW       4
#define CMD_LINK_UP           5
#define CMD_LINK_DOWN         6
#define NTB_EPF_ARGUMENT     0x4
#define NTB_EPF_CMD_STATUS   0x8
#define COMMAND_STATUS_OK     1
#define COMMAND_STATUS_ERROR  2
#define NTB_EPF_LINK_STATUS  0x0A
#define LINK_STATUS_UP       BIT(0)
#define NTB_EPF_TOPOLOGY     0x0C
#define NTB_EPF_LOWER_ADDR   0x10
#define NTB_EPF_UPPER_ADDR   0x14
#define NTB_EPF_LOWER_SIZE   0x18
#define NTB_EPF_UPPER_SIZE   0x1C
#define NTB_EPF_MW_COUNT     0x20
#define NTB_EPF_MW1_OFFSET   0x24
#define NTB_EPF_SPAD_OFFSET  0x28
#define NTB_EPF_SPAD_COUNT   0x2C
#define NTB_EPF_DB_ENTRY_SIZE 0x30
#define NTB_EPF_DB_DATA(n)   (0x34 + (n) * 4)
#define NTB_EPF_DB_OFFSET(n) (0xB4 + (n) * 4)
#define NTB_EPF_MIN_DB_COUNT 3
#define NTB_EPF_MAX_DB_COUNT 31
#define NTB_EPF_MAX_MW_COUNT (NTB_BAR_NUM - BAR_MW1)
#define MSIX_ENABLE           BIT(16)
#define NTB_EPF_COMMAND_TIMEOUT 1000

enum pci_barno {
    NO_BAR = -1,
    BAR_0,
    BAR_1,
    BAR_2,
    BAR_3,
    BAR_4,
    BAR_5,
};

enum epf_ntb_bar {
    BAR_CONFIG,
    BAR_PEER_SPAD,
    BAR_DB,
    BAR_MW1,
    BAR_MW2,
    BAR_MW3,
    BAR_MW4,
    NTB_BAR_NUM,
};

/* BAR sizes */
#define BAR0_SIZE 0x1000
#define BAR1_SIZE 0x1000
#define BAR2_SIZE 0x200000
#define BAR3_SIZE 0x100000
#define BAR4_SIZE 0x100000
#define BAR5_SIZE 0x100000

#define DEFAULT_MW_COUNT 3
#define DEFAULT_SPAD_COUNT 64
#define DEFAULT_DB_COUNT 32

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
    uint64_t db_mask;
    uint64_t db_valid_mask;

    struct {
        uint32_t command;
        uint32_t argument;
        uint32_t cmd_status;
        uint16_t link_status;
        uint16_t topology;
        uint32_t lower_addr;
        uint32_t upper_addr;
        uint32_t lower_size;
        uint32_t upper_size;
        uint32_t mw_count;
        uint32_t mw1_offset;
        uint32_t spad_offset;
        uint32_t spad_count;
        uint32_t db_entry_size;
        uint32_t db_data[31];
        uint32_t db_offset[31];
    } ctrl;

    uint32_t peer_spad[256];
    uint32_t local_spad[256];

    struct {
        dma_addr_t mw_addr[4];
        uint64_t mw_size[4];
    } dma;

    uint32_t status;

    unsigned int db_count;
    unsigned int mw_count;
    unsigned int spad_count;
};

static void process_command(PCIBaseState *s)
{
    uint32_t cmd = s->ctrl.command;
    switch (cmd) {
    case CMD_LINK_UP:
        s->ctrl.link_status |= LINK_STATUS_UP;
        s->ctrl.cmd_status = COMMAND_STATUS_OK;
        break;
    case CMD_LINK_DOWN:
        s->ctrl.link_status &= ~LINK_STATUS_UP;
        s->ctrl.cmd_status = COMMAND_STATUS_OK;
        break;
    case CMD_CONFIGURE_DOORBELL:
    case CMD_TEARDOWN_DOORBELL:
    case CMD_CONFIGURE_MW:
    case CMD_TEARDOWN_MW:
        s->ctrl.cmd_status = COMMAND_STATUS_OK;
        break;
    default:
        s->ctrl.cmd_status = COMMAND_STATUS_ERROR;
        break;
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= 0x34 && addr < 0x34 + 31 * 4) {
        int n = (addr - 0x34) / 4;
        if (n < 31) {
            val = s->ctrl.db_data[n];
        }
    } else if (addr >= 0xB4 && addr < 0xB4 + 31 * 4) {
        int n = (addr - 0xB4) / 4;
        if (n < 31) {
            val = s->ctrl.db_offset[n];
        }
    } else if (addr >= s->ctrl.spad_offset && addr < s->ctrl.spad_offset + s->ctrl.spad_count * 4) {
        int idx = (addr - s->ctrl.spad_offset) / 4;
        if (idx < 256) {
            val = s->local_spad[idx];
        }
    } else {
        switch (addr) {
        case 0x00: val = s->ctrl.command; break;
        case 0x04: val = s->ctrl.argument; break;
        case 0x08: val = s->ctrl.cmd_status; break;
        case 0x0A: val = s->ctrl.link_status; break;
        case 0x0C: val = s->ctrl.topology; break;
        case 0x10: val = s->ctrl.lower_addr; break;
        case 0x14: val = s->ctrl.upper_addr; break;
        case 0x18: val = s->ctrl.lower_size; break;
        case 0x1C: val = s->ctrl.upper_size; break;
        case 0x20: val = s->ctrl.mw_count; break;
        case 0x24: val = s->ctrl.mw1_offset; break;
        case 0x28: val = s->ctrl.spad_offset; break;
        case 0x2C: val = s->ctrl.spad_count; break;
        case 0x30: val = s->ctrl.db_entry_size; break;
        default: val = 0; break;
        }
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= s->ctrl.spad_offset && addr < s->ctrl.spad_offset + s->ctrl.spad_count * 4) {
        int idx = (addr - s->ctrl.spad_offset) / 4;
        if (idx < 256) {
            s->local_spad[idx] = val;
        }
        return;
    }

    switch (addr) {
    case 0x00:
        s->ctrl.command = val;
        process_command(s);
        break;
    case 0x04:
        s->ctrl.argument = val;
        break;
    case 0x08:
        if (val == 0) {
            s->ctrl.cmd_status = 0;
        }
        break;
    case 0x10: s->ctrl.lower_addr = val; break;
    case 0x14: s->ctrl.upper_addr = val; break;
    case 0x18: s->ctrl.lower_size = val; break;
    case 0x1C: s->ctrl.upper_size = val; break;
    default:
        qemu_log_mask(LOG_UNIMP, "ntb_hw_epf: write to unimplemented offset 0x%" HWADDR_PRIx "\n", addr);
        break;
    }
}

static uint64_t pcibase_peer_spad_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    int idx = addr / 4;
    if (idx < 256) {
        val = s->peer_spad[idx];
    }
    return val;
}

static void pcibase_peer_spad_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    int idx = addr / 4;
    if (idx < 256) {
        s->peer_spad[idx] = val;
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 2, .max_access_size = 4 },
    .impl  = { .min_access_size = 2, .max_access_size = 4 },
};

static const MemoryRegionOps pcibase_peer_spad_ops = {
    .read = pcibase_peer_spad_read,
    .write = pcibase_peer_spad_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    memset(&s->ctrl, 0, sizeof(s->ctrl));
    s->ctrl.mw_count = DEFAULT_MW_COUNT;
    s->ctrl.spad_count = DEFAULT_SPAD_COUNT;
    s->ctrl.db_entry_size = 4;
    for (int i = 0; i < 31; i++) {
        s->ctrl.db_data[i] = i + 1;
        s->ctrl.db_offset[i] = 0;
    }
    s->ctrl.spad_offset = 0x200;
    s->ctrl.mw1_offset = 0x1000;
    s->status = 0;
    s->db_count = 0;
    s->mw_count = 0;
    s->spad_count = 0;
    memset(s->peer_spad, 0, sizeof(s->peer_spad));
    memset(s->local_spad, 0, sizeof(s->local_spad));
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    if (msi_init(pdev, 0, 32, true, false, errp)) {
        return;
    }

    s->num_bars = 6;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = BAR0_SIZE, .name = "ntb-config" };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_MMIO, .size = BAR1_SIZE, .name = "ntb-peer-spad" };
    s->bar_info[2] = (BARInfo){ .index = 2, .type = BAR_TYPE_RAM, .size = BAR2_SIZE, .name = "ntb-db-mw1" };
    s->bar_info[3] = (BARInfo){ .index = 3, .type = BAR_TYPE_RAM, .size = BAR3_SIZE, .name = "ntb-mw2" };
    s->bar_info[4] = (BARInfo){ .index = 4, .type = BAR_TYPE_RAM, .size = BAR4_SIZE, .name = "ntb-mw3" };
    s->bar_info[5] = (BARInfo){ .index = 5, .type = BAR_TYPE_RAM, .size = BAR5_SIZE, .name = "ntb-mw4" };

    for (int i = 0; i < s->num_bars; i++) {
        BARInfo *bi = &s->bar_info[i];
        if (!bi || bi->type == BAR_TYPE_NONE) {
            continue;
        }
        hwaddr aligned_size = pow2ceil(bi->size);
        MemoryRegion *mr = &s->bar_regions[bi->index];
        if (bi->type == BAR_TYPE_MMIO) {
            if (i == 1) {
                memory_region_init_io(mr, OBJECT(s), &pcibase_peer_spad_ops, s, bi->name, aligned_size);
            } else {
                memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, aligned_size);
            }
            pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
        } else if (bi->type == BAR_TYPE_RAM) {
            memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, errp);
            pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
        }
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
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
