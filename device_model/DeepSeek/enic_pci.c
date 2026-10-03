/*
 * QEMU emulation of Cisco VIC Ethernet (enic) device
 * Based on Linux enic driver.
 * Phase 4: Debug & Update - Runtime Repair: Made devcmd processing synchronous.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/module.h"
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

/* Static definitions extracted from enic driver */

#define TYPE_PCIBASE_DEVICE "enic_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Constants from enic driver */
#define VNIC_DEVCMD_NARGS 15
#define ENIC_BARS_MAX     6
#define ENIC_WQ_MAX       256
#define ENIC_RQ_MAX       256
#define ENIC_CQ_MAX       256
#define ENIC_MSIX_MIN_INTR 4   /* ENIC_MSIX_RESERVED_INTR (2) + 2 */
#define VNIC_RES_MAGIC    0x766E6963UL
#define VNIC_RES_VERSION  0x00000000UL

/* devcmd protocol constants from enic driver */
#define STAT_BUSY         0xFF
#define STAT_ERROR        7

/* Enums */
enum vnic_dev_intr_mode {
    VNIC_DEV_INTR_MODE_UNKNOWN,
    VNIC_DEV_INTR_MODE_INTX,
    VNIC_DEV_INTR_MODE_MSI,
    VNIC_DEV_INTR_MODE_MSIX,
};

enum vnic_res_type {
    RES_TYPE_EOL,
    RES_TYPE_WQ,
    RES_TYPE_RQ,
    RES_TYPE_CQ,
    RES_TYPE_RSVD1,
    RES_TYPE_NIC_CFG,
    RES_TYPE_RSVD2,
    RES_TYPE_RSVD3,
    RES_TYPE_RSVD4,
    RES_TYPE_RSVD5,
    RES_TYPE_INTR_CTRL,
    RES_TYPE_INTR_TABLE,
    RES_TYPE_INTR_PBA,
    RES_TYPE_INTR_PBA_LEGACY,
    RES_TYPE_RSVD6,
    RES_TYPE_RSVD7,
    RES_TYPE_DEVCMD,
    RES_TYPE_PASS_THRU_PAGE,
    RES_TYPE_SUBVNIC,
    RES_TYPE_MQ_WQ,
    RES_TYPE_MQ_RQ,
    RES_TYPE_MQ_CQ,
    RES_TYPE_DEPRECATED1,
    RES_TYPE_DEPRECATED2,
    RES_TYPE_DEVCMD2,
    RES_TYPE_SRIOV_INTR = 45,
    RES_TYPE_ADMIN_WQ = 49,
    RES_TYPE_ADMIN_RQ,
    RES_TYPE_ADMIN_CQ,
    RES_TYPE_MAX,
};

/* Hardware register structures (from enic driver, with kernel types removed) */
typedef struct vnic_devcmd {
    uint32_t status;
    uint32_t cmd;
    uint64_t args[VNIC_DEVCMD_NARGS];
} vnic_devcmd;

typedef struct vnic_devcmd_notify {
    uint32_t csum;
    uint32_t link_state;
    uint32_t port_speed;
    uint32_t mtu;
    uint32_t msglvl;
    uint32_t uif;
    uint32_t status;
    uint32_t error;
    uint32_t link_down_cnt;
    uint32_t perbi_rebuild_cnt;
} vnic_devcmd_notify;

typedef struct vnic_resource_header {
    uint32_t magic;
    uint32_t version;
} vnic_resource_header;

typedef struct vnic_resource {
    uint8_t type;
    uint8_t bar;
    uint8_t pad[2];
    uint32_t bar_offset;
    uint32_t count;
} vnic_resource;

typedef struct vnic_devcmd_fw_info {
    char fw_version[32];
    char fw_build[32];
    char hw_version[32];
    char hw_serial_number[32];
    uint16_t asic_type;
    uint16_t asic_rev;
} vnic_devcmd_fw_info;

typedef struct vnic_intr_ctrl {
    uint32_t coalescing_timer;
    uint32_t pad0;
    uint32_t coalescing_value;
    uint32_t pad1;
    uint32_t coalescing_type;
    uint32_t pad2;
    uint32_t mask_on_assertion;
    uint32_t pad3;
    uint32_t mask;
    uint32_t pad4;
    uint32_t int_credits;
    uint32_t pad5;
    uint32_t int_credit_return;
    uint32_t pad6;
} vnic_intr_ctrl;

typedef struct vnic_wq_ctrl {
    uint64_t ring_base;
    uint32_t ring_size;
    uint32_t pad0;
    uint32_t posted_index;
    uint32_t pad1;
    uint32_t cq_index;
    uint32_t pad2;
    uint32_t enable;
    uint32_t pad3;
    uint32_t running;
    uint32_t pad4;
    uint32_t fetch_index;
    uint32_t pad5;
    uint32_t dca_value;
    uint32_t pad6;
    uint32_t error_interrupt_enable;
    uint32_t pad7;
    uint32_t error_interrupt_offset;
    uint32_t pad8;
    uint32_t error_status;
    uint32_t pad9;
} vnic_wq_ctrl;

typedef struct vnic_rq_ctrl {
    uint64_t ring_base;
    uint32_t ring_size;
    uint32_t pad0;
    uint32_t posted_index;
    uint32_t pad1;
    uint32_t cq_index;
    uint32_t pad2;
    uint32_t enable;
    uint32_t pad3;
    uint32_t running;
    uint32_t pad4;
    uint32_t fetch_index;
    uint32_t pad5;
    uint32_t error_interrupt_enable;
    uint32_t pad6;
    uint32_t error_interrupt_offset;
    uint32_t pad7;
    uint32_t error_status;
    uint32_t pad8;
    uint32_t dropped_packet_count;
    uint32_t pad9;
    uint32_t dropped_packet_count_rc;
    uint32_t pad10;
} vnic_rq_ctrl;

typedef struct vnic_cq_ctrl {
    uint64_t ring_base;
    uint32_t ring_size;
    uint32_t pad0;
    uint32_t flow_control_enable;
    uint32_t pad1;
    uint32_t color_enable;
    uint32_t pad2;
    uint32_t cq_head;
    uint32_t pad3;
    uint32_t cq_tail;
    uint32_t pad4;
    uint32_t cq_tail_color;
    uint32_t pad5;
    uint32_t interrupt_enable;
    uint32_t pad6;
    uint32_t cq_entry_enable;
    uint32_t pad7;
    uint32_t cq_message_enable;
    uint32_t pad8;
    uint32_t interrupt_offset;
    uint32_t pad9;
    uint64_t cq_message_addr;
    uint32_t pad10;
} vnic_cq_ctrl;

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

/* Devcmd command enum from supplementary source */
enum vnic_devcmd_cmd {
    CMD_NONE                = 0,
    CMD_MCPU_FW_INFO_OLD    = 1,
    CMD_MCPU_FW_INFO        = 1, /* same enum for compatibility */
    CMD_DEV_SPEC            = 2,
    CMD_STATS_CLEAR         = 3,
    CMD_STATS_DUMP          = 4,
    CMD_PACKET_FILTER        = 7,
    CMD_PACKET_FILTER_ALL   = 7,
    CMD_HANG_NOTIFY         = 8,
    CMD_GET_MAC_ADDR        = 9,
    CMD_ADDR_ADD            = 12,
    CMD_ADDR_DEL            = 13,
    CMD_VLAN_ADD            = 14,
    CMD_VLAN_DEL            = 15,
    CMD_NIC_CFG             = 16,
    CMD_NIC_CFG_CHK          = 16,
    CMD_RSS_KEY             = 17,
    CMD_RSS_CPU             = 18,
    CMD_SOFT_RESET          = 19,
    CMD_SOFT_RESET_STATUS   = 20,
    CMD_NOTIFY              = 21,
    CMD_UNDI                = 22,
    CMD_OPEN                = 23,
    CMD_OPEN_STATUS          = 24,
    CMD_CLOSE                = 25,
    CMD_INIT_v1              = 26,
    CMD_INIT_PROV_INFO      = 27,
    CMD_ENABLE              = 28,
    CMD_ENABLE_WAIT          = 28,
    CMD_DISABLE             = 29,
    CMD_STATS_DUMP_ALL      = 30,
    CMD_INIT_STATUS          = 31,
    CMD_INT13               = 32,
    CMD_LOGICAL_UPLINK      = 33,
    CMD_DEINIT              = 34,
    CMD_INIT                = 35,
    CMD_CAPABILITY          = 36,
    CMD_PERBI               = 37,
    CMD_IAR                  = 38,
    CMD_HANG_RESET          = 39,
    CMD_HANG_RESET_STATUS   = 40,
    CMD_IG_VLAN_REWRITE_MODE = 41,
    CMD_PROXY_BY_BDF        = 42,
    CMD_PROXY_BY_INDEX      = 43,
    CMD_CONFIG_INFO_GET     = 44,
    CMD_INT13_ALL           = 45,
    CMD_SET_DEFAULT_VLAN    = 46,
    CMD_INIT_PROV_INFO2     = 47,
    CMD_ENABLE2             = 48,
    CMD_STATUS              = 49,
    CMD_INTR_COAL_CONVERT   = 50,
    CMD_SET_MAC_ADDR        = 55,
    CMD_PROV_INFO_UPDATE    = 56,
    CMD_INITIALIZE_DEVCMD2  = 57,
    CMD_ADD_FILTER          = 58,
    CMD_DEL_FILTER          = 59,
    CMD_QP_ENABLE           = 60,
    CMD_QP_DISABLE          = 61,
    CMD_QP_STATS_DUMP       = 62,
    CMD_QP_STATS_CLEAR      = 63,
    CMD_GET_SUPP_FEATURE_VER = 69,
    CMD_OVERLAY_OFFLOAD_CTRL = 72,
    CMD_OVERLAY_OFFLOAD_CFG  = 73,
    CMD_CQ_ENTRY_SIZE_SET   = 90,
};

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

    /* Hardware Register Shadows */
    vnic_devcmd devcmd;
    vnic_devcmd_notify notify;
    vnic_resource_header res_hdr;
    vnic_resource resources[RES_TYPE_MAX];
    vnic_devcmd_fw_info fw_info;

    /* Control area shadows */
    vnic_intr_ctrl intr_ctrl;
    vnic_wq_ctrl wq_ctrl;
    vnic_rq_ctrl rq_ctrl;
    vnic_cq_ctrl cq_ctrl;
    uint8_t nic_cfg[256];

    /* Status flags */
    uint32_t link_status;

    /* Prob/Reset state */
    bool reset_pending;

    /* Open/init state */
    enum { OPEN_INIT, OPEN_INPROGRESS, OPEN_DONE } open_status;
};

/* Synchronous command processing */
static void process_devcmd(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t cmd = s->devcmd.cmd;
    uint64_t a0 = s->devcmd.args[0];

    switch (cmd) {
    case CMD_NONE:
        break;
    case CMD_MCPU_FW_INFO:
    {
        dma_addr_t dma_addr = a0;
        vnic_devcmd_fw_info info;
        strcpy(info.fw_version, "1.0.0");
        strcpy(info.fw_build, "build");
        strcpy(info.hw_version, "hw");
        strcpy(info.hw_serial_number, "sn");
        info.asic_type = 0;
        info.asic_rev = 0;
        pci_dma_write(pdev, dma_addr, &info, sizeof(info));
        break;
    }
    case CMD_OPEN:
        s->open_status = OPEN_DONE;
        break;
    case CMD_OPEN_STATUS:
        s->devcmd.args[0] = (s->open_status == OPEN_INPROGRESS) ? 1 : 0;
        break;
    case CMD_CLOSE:
        s->open_status = OPEN_INIT;
        break;
    case CMD_SOFT_RESET:
        break;
    case CMD_SOFT_RESET_STATUS:
        s->devcmd.args[0] = 0;
        break;
    case CMD_NOTIFY:
        break;
    case CMD_INIT:
        break;
    case CMD_INIT_STATUS:
        s->devcmd.args[0] = 0;
        break;
    case CMD_CAPABILITY:
        s->devcmd.args[0] = 0;
        s->devcmd.args[1] = 0;
        break;
    case CMD_STATUS:
        s->devcmd.args[0] = 0;
        s->devcmd.args[1] = 0;
        break;
    default:
        break;
    }

    s->devcmd.status = 0;

    /* Signal completion interrupt if MSI-X enabled */
    if (msix_enabled(pdev)) {
        msix_notify(pdev, 0);
    }
}

/* MMIO/PIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr < 8) {
        switch (addr) {
        case 0:
            val = VNIC_RES_MAGIC;
            break;
        case 4:
            val = VNIC_RES_VERSION;
            break;
        }
    } else if (addr >= 8 && addr < (8 + RES_TYPE_MAX * sizeof(vnic_resource))) {
        int res_index = (addr - 8) / sizeof(vnic_resource);
        int offset = (addr - 8) % sizeof(vnic_resource);
        uint8_t *res_ptr = (uint8_t *)&s->resources[res_index];
        if (res_index < RES_TYPE_MAX) {
            memcpy(&val, res_ptr + offset, size);
        }
    } else if (addr >= 0x1000 && addr < 0x1080) {
        unsigned offset = addr - 0x1000;
        memcpy(&val, ((uint8_t *)&s->devcmd) + offset, MIN(size, sizeof(s->devcmd) - offset));
    } else if (addr >= 0x2000 && addr < 0x2000 + sizeof(vnic_intr_ctrl)) {
        unsigned offset = addr - 0x2000;
        memcpy(&val, ((uint8_t *)&s->intr_ctrl) + offset, MIN(size, sizeof(s->intr_ctrl) - offset));
    } else if (addr >= 0x3000 && addr < 0x3000 + sizeof(vnic_wq_ctrl)) {
        unsigned offset = addr - 0x3000;
        memcpy(&val, ((uint8_t *)&s->wq_ctrl) + offset, MIN(size, sizeof(s->wq_ctrl) - offset));
    } else if (addr >= 0x4000 && addr < 0x4000 + sizeof(vnic_rq_ctrl)) {
        unsigned offset = addr - 0x4000;
        memcpy(&val, ((uint8_t *)&s->rq_ctrl) + offset, MIN(size, sizeof(s->rq_ctrl) - offset));
    } else if (addr >= 0x5000 && addr < 0x5000 + sizeof(vnic_cq_ctrl)) {
        unsigned offset = addr - 0x5000;
        memcpy(&val, ((uint8_t *)&s->cq_ctrl) + offset, MIN(size, sizeof(s->cq_ctrl) - offset));
    } else if (addr >= 0x6000 && addr < 0x6000 + sizeof(s->nic_cfg)) {
        unsigned offset = addr - 0x6000;
        memcpy(&val, s->nic_cfg + offset, MIN(size, sizeof(s->nic_cfg) - offset));
    } else if (addr >= 0x7000 && addr < 0x7000 + 0x1000) {
        /* INTR_PBA_LEGACY placeholder */
        return 0;
    } else {
        return 0;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < 8) {
        return;
    } else if (addr >= 8 && addr < (8 + RES_TYPE_MAX * sizeof(vnic_resource))) {
        return;
    } else if (addr >= 0x1000 && addr < 0x1080) {
        unsigned offset = addr - 0x1000;
        memcpy(((uint8_t *)&s->devcmd) + offset, &val, MIN(size, sizeof(s->devcmd) - offset));
        /* Trigger command processing immediately if status is BUSY and cmd is non-zero */
        if (s->devcmd.status == STAT_BUSY && s->devcmd.cmd != 0) {
            process_devcmd(s);
        }
    } else if (addr >= 0x2000 && addr < 0x2000 + sizeof(vnic_intr_ctrl)) {
        unsigned offset = addr - 0x2000;
        memcpy(((uint8_t *)&s->intr_ctrl) + offset, &val, MIN(size, sizeof(s->intr_ctrl) - offset));
    } else if (addr >= 0x3000 && addr < 0x3000 + sizeof(vnic_wq_ctrl)) {
        unsigned offset = addr - 0x3000;
        memcpy(((uint8_t *)&s->wq_ctrl) + offset, &val, MIN(size, sizeof(s->wq_ctrl) - offset));
    } else if (addr >= 0x4000 && addr < 0x4000 + sizeof(vnic_rq_ctrl)) {
        unsigned offset = addr - 0x4000;
        memcpy(((uint8_t *)&s->rq_ctrl) + offset, &val, MIN(size, sizeof(s->rq_ctrl) - offset));
    } else if (addr >= 0x5000 && addr < 0x5000 + sizeof(vnic_cq_ctrl)) {
        unsigned offset = addr - 0x5000;
        memcpy(((uint8_t *)&s->cq_ctrl) + offset, &val, MIN(size, sizeof(s->cq_ctrl) - offset));
    } else if (addr >= 0x6000 && addr < 0x6000 + sizeof(s->nic_cfg)) {
        unsigned offset = addr - 0x6000;
        memcpy(s->nic_cfg + offset, &val, MIN(size, sizeof(s->nic_cfg) - offset));
    } else if (addr >= 0x7000 && addr < 0x7000 + 0x1000) {
        /* Ignore writes */
    } else {
        /* Unmapped */
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    memset(&s->devcmd, 0, sizeof(s->devcmd));
    memset(&s->notify, 0, sizeof(s->notify));
    memset(&s->fw_info, 0, sizeof(s->fw_info));
    s->link_status = 0;
    s->reset_pending = false;

    memset(&s->intr_ctrl, 0, sizeof(s->intr_ctrl));
    memset(&s->wq_ctrl, 0, sizeof(s->wq_ctrl));
    memset(&s->rq_ctrl, 0, sizeof(s->rq_ctrl));
    memset(&s->cq_ctrl, 0, sizeof(s->cq_ctrl));
    memset(s->nic_cfg, 0, sizeof(s->nic_cfg));

    s->res_hdr.magic = VNIC_RES_MAGIC;
    s->res_hdr.version = VNIC_RES_VERSION;

    memset(s->resources, 0, sizeof(s->resources));
    int idx = 0;
    s->resources[idx].type = RES_TYPE_WQ;
    s->resources[idx].bar = 0;
    s->resources[idx].bar_offset = 0x3000;
    s->resources[idx].count = 1;
    idx++;
    s->resources[idx].type = RES_TYPE_RQ;
    s->resources[idx].bar = 0;
    s->resources[idx].bar_offset = 0x4000;
    s->resources[idx].count = 1;
    idx++;
    s->resources[idx].type = RES_TYPE_CQ;
    s->resources[idx].bar = 0;
    s->resources[idx].bar_offset = 0x5000;
    s->resources[idx].count = 2;
    idx++;
    s->resources[idx].type = RES_TYPE_INTR_CTRL;
    s->resources[idx].bar = 0;
    s->resources[idx].bar_offset = 0x2000;
    s->resources[idx].count = 4;
    idx++;
    s->resources[idx].type = RES_TYPE_DEVCMD;
    s->resources[idx].bar = 0;
    s->resources[idx].bar_offset = 0x1000;
    s->resources[idx].count = 1;
    idx++;
    s->resources[idx].type = RES_TYPE_NIC_CFG;
    s->resources[idx].bar = 0;
    s->resources[idx].bar_offset = 0x6000;
    s->resources[idx].count = 1;
    idx++;
    s->resources[idx].type = RES_TYPE_INTR_PBA_LEGACY;
    s->resources[idx].bar = 0;
    s->resources[idx].bar_offset = 0x7000;
    s->resources[idx].count = 1;
    idx++;
    s->resources[idx].type = RES_TYPE_EOL;

    s->open_status = OPEN_INIT;
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
        /* Not used */
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, 0x1137);
    pci_set_word(pci_conf + PCI_DEVICE_ID, 0x0043);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0200);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x8000;
    s->bar_info[0].name = "enic-mmio";
    pcibase_register_bar(pdev, s, &s->bar_info[0], errp);

    if (msix_init_exclusive_bar(pdev, ENIC_MSIX_MIN_INTR, 4, errp)) {
        return;
    }

    pcibase_reset(DEVICE(pdev));
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, 0);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

static const VMStateDescription vmstate_pcibase = {
    .name = "enic_pci",
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
