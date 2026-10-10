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

#define TYPE_PCIBASE_DEVICE "fnic_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

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

#define PCI_VENDOR_ID_CISCO 0x1137
#define PCI_DEVICE_ID_CISCO_FNIC 0x0045
#define FNIC_WQ_MAX 1
#define FNIC_RQ_MAX 1
#define FNIC_WQ_COPY_MAX 64
#define FNIC_CQ_MAX (FNIC_WQ_COPY_MAX + FNIC_WQ_MAX + FNIC_RQ_MAX)
#define VNIC_DEVCMD_NARGS 15

#ifndef PCI_CLASS_STORAGE_FIBRE
#define PCI_CLASS_STORAGE_FIBRE 0x0104
#endif

struct vnic_wq_ctrl {
    uint64_t ring_base;         /* 0x00 */
    uint32_t ring_size;         /* 0x08 */
    uint32_t pad0;
    uint32_t posted_index;      /* 0x10 */
    uint32_t pad1;
    uint32_t cq_index;          /* 0x18 */
    uint32_t pad2;
    uint32_t enable;            /* 0x20 */
    uint32_t pad3;
    uint32_t running;           /* 0x28 */
    uint32_t pad4;
    uint32_t fetch_index;       /* 0x30 */
    uint32_t pad5;
    uint32_t dca_value;         /* 0x38 */
    uint32_t pad6;
    uint32_t error_interrupt_enable; /* 0x40 */
    uint32_t pad7;
    uint32_t error_interrupt_offset; /* 0x48 */
    uint32_t pad8;
    uint32_t error_status;      /* 0x50 */
    uint32_t pad9;
};

struct vnic_rq_ctrl {
    uint64_t ring_base;         /* 0x00 */
    uint32_t ring_size;         /* 0x08 */
    uint32_t pad0;
    uint32_t posted_index;      /* 0x10 */
    uint32_t pad1;
    uint32_t cq_index;          /* 0x18 */
    uint32_t pad2;
    uint32_t enable;            /* 0x20 */
    uint32_t pad3;
    uint32_t running;           /* 0x28 */
    uint32_t pad4;
    uint32_t fetch_index;       /* 0x30 */
    uint32_t pad5;
    uint32_t error_interrupt_enable; /* 0x38 */
    uint32_t pad6;
    uint32_t error_interrupt_offset; /* 0x40 */
    uint32_t pad7;
    uint32_t error_status;      /* 0x48 */
    uint32_t pad8;
    uint32_t dropped_packet_count;   /* 0x50 */
    uint32_t pad9;
    uint32_t dropped_packet_count_rc;/* 0x58 */
    uint32_t pad10;
};

struct vnic_cq_ctrl {
    uint64_t ring_base;         /* 0x00 */
    uint32_t ring_size;         /* 0x08 */
    uint32_t pad0;
    uint32_t flow_control_enable;/* 0x10 */
    uint32_t pad1;
    uint32_t color_enable;      /* 0x18 */
    uint32_t pad2;
    uint32_t cq_head;           /* 0x20 */
    uint32_t pad3;
    uint32_t cq_tail;           /* 0x28 */
    uint32_t pad4;
    uint32_t cq_tail_color;     /* 0x30 */
    uint32_t pad5;
    uint32_t interrupt_enable;  /* 0x38 */
    uint32_t pad6;
    uint32_t cq_entry_enable;   /* 0x40 */
    uint32_t pad7;
    uint32_t cq_message_enable; /* 0x48 */
    uint32_t pad8;
    uint32_t interrupt_offset;  /* 0x50 */
    uint32_t pad9;
    uint64_t cq_message_addr;   /* 0x58 */
    uint32_t pad10;
};

struct vnic_devcmd {
    uint32_t status;            /* RO */
    uint32_t cmd;               /* RW */
    uint64_t args[VNIC_DEVCMD_NARGS];   /* RW cmd args (little-endian) */
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

    /* Hardware Register Shadows (The 'Identity' of the device) */
    struct vnic_wq_ctrl wq_ctrl[1];
    struct vnic_rq_ctrl rq_ctrl[1];
    struct vnic_cq_ctrl cq_ctrl[66];
    struct vnic_devcmd devcmd;

    uint32_t state;
    uint32_t reset_in_progress;
};

enum vnic_dev_intr_mode {
    VNIC_DEV_INTR_MODE_UNKNOWN,
    VNIC_DEV_INTR_MODE_INTX,
    VNIC_DEV_INTR_MODE_MSI,
    VNIC_DEV_INTR_MODE_MSIX,
};

enum vnic_res_type {
    RES_TYPE_EOL,            /* End-of-list */
    RES_TYPE_WQ,             /* Work queues */
    RES_TYPE_RQ,             /* Receive queues */
    RES_TYPE_CQ,             /* Completion queues */
    RES_TYPE_RSVD1,
    RES_TYPE_NIC_CFG,        /* Enet NIC config registers */
    RES_TYPE_RSVD2,
    RES_TYPE_RSVD3,
    RES_TYPE_RSVD4,
    RES_TYPE_RSVD5,
    RES_TYPE_INTR_CTRL,      /* Interrupt ctrl table */
    RES_TYPE_INTR_TABLE,     /* MSI/MSI-X Interrupt table */
    RES_TYPE_INTR_PBA,       /* MSI/MSI-X PBA table */
    RES_TYPE_INTR_PBA_LEGACY,/* Legacy intr status */
    RES_TYPE_RSVD6,
    RES_TYPE_RSVD7,
    RES_TYPE_DEVCMD,         /* Device command region */
    RES_TYPE_PASS_THRU_PAGE, /* Pass-thru page */
    RES_TYPE_SUBVNIC,        /* subvnic resource type */
    RES_TYPE_MQ_WQ,          /* MQ Work queues */
    RES_TYPE_MQ_RQ,          /* MQ Receive queues */
    RES_TYPE_MQ_CQ,          /* MQ Completion queues */
    RES_TYPE_DEPRECATED1,    /* Old version of devcmd 2 */
    RES_TYPE_DEPRECATED2,    /* Old version of devcmd 2 */
    RES_TYPE_DEVCMD2,        /* Device control region */
    RES_TYPE_MAX,            /* Count of resource types */
};

#define VNIC_RES_STRIDE 128
#define _CMDC(dir, vtype, nr)    _CMDCF(dir, 0, vtype, nr)
#define _CMDCNW(dir, vtype, nr)  _CMDCF(dir, _CMD_FLAGS_NOWAIT, vtype, nr)
#define _CMD_DIR_NONE   0U
#define _CMD_VTYPE_NONE  0U
#define _CMD_DIR_WRITE  1U
#define _CMD_VTYPE_ALL   (_CMD_VTYPE_ENET | _CMD_VTYPE_FC | _CMD_VTYPE_SCSI)
#define _CMD_DIR_RW     (_CMD_DIR_WRITE | _CMD_DIR_READ)
#define _CMD_DIR_READ   2U
#define _CMD_VTYPE_ENET  1U
#define _CMD_VTYPE_FC    2U

struct vnic_intr_ctrl {
    uint32_t coalescing_timer;  /* 0x00 */
    uint32_t pad0;
    uint32_t coalescing_value;  /* 0x08 */
    uint32_t pad1;
    uint32_t coalescing_type;   /* 0x10 */
    uint32_t pad2;
    uint32_t mask_on_assertion; /* 0x18 */
    uint32_t pad3;
    uint32_t mask;              /* 0x20 */
    uint32_t pad4;
    uint32_t int_credits;       /* 0x28 */
    uint32_t pad5;
    uint32_t int_credit_return; /* 0x30 */
    uint32_t pad6;
};

struct vnic_fc_config {
    uint64_t node_wwn;
    uint64_t port_wwn;
    uint32_t flags;
    uint32_t wq_enet_desc_count;
    uint32_t wq_copy_desc_count;
    uint32_t rq_desc_count;
    uint32_t flogi_retries;
    uint32_t flogi_timeout;
    uint32_t plogi_retries;
    uint32_t plogi_timeout;
    uint32_t io_throttle_count;
    uint32_t link_down_timeout;
    uint32_t port_down_timeout;
    uint32_t port_down_io_retries;
    uint32_t luns_per_tgt;
    uint16_t maxdatafieldsize;
    uint16_t ed_tov;
    uint16_t ra_tov;
    uint16_t intr_timer;
    uint8_t intr_timer_type;
    uint8_t intr_mode;
    uint8_t lun_queue_depth;
    uint8_t io_timeout_retry;
    uint16_t wq_copy_count;
};

struct wq_enet_desc {
    uint64_t address;
    uint16_t length;
    uint16_t mss_loopback;
    uint16_t header_length_flags;
    uint16_t vlan_tag;
};

struct rq_enet_desc {
    uint64_t address;
    uint16_t length_type;
    uint8_t reserved[6];
};

struct cq_enet_rq_desc {
    uint16_t completed_index_flags;
    uint16_t q_number_rss_type_flags;
    uint32_t rss_hash;
    uint16_t bytes_written_flags;
    uint16_t vlan;
    uint16_t checksum_fcoe;
    uint8_t flags;
    uint8_t type_color;
};

struct cq_enet_wq_desc {
    uint16_t completed_index;
    uint16_t q_number;
    uint8_t reserved[11];
    uint8_t type_color;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->intr_status & s->intr_mask) {
        if (s->has_msix && msix_enabled(pdev)) {
            msix_notify(pdev, 0);
        } else if (s->has_msi && msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!s->has_msix && !s->has_msi) {
            pci_set_irq(pdev, 0);
        }
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    static const uint8_t res_table_bytes[] = {
        /* magic: 0x766e6963 */
        0x63, 0x69, 0x6e, 0x76,
        /* version: 0x0 */
        0x00, 0x00, 0x00, 0x00,
        
        /* WQ */
        RES_TYPE_WQ, 0, 0, 0,
        0x00, 0x10, 0x00, 0x00, /* 0x1000 */
        0x02, 0x00, 0x00, 0x00, /* 2 */

        /* RQ */
        RES_TYPE_RQ, 0, 0, 0,
        0x00, 0x20, 0x00, 0x00, /* 0x2000 */
        0x01, 0x00, 0x00, 0x00, /* 1 */

        /* CQ */
        RES_TYPE_CQ, 0, 0, 0,
        0x00, 0x30, 0x00, 0x00, /* 0x3000 */
        0x42, 0x00, 0x00, 0x00, /* 66 */

        /* INTR_CTRL */
        RES_TYPE_INTR_CTRL, 0, 0, 0,
        0x00, 0x40, 0x00, 0x00, /* 0x4000 */
        0x04, 0x00, 0x00, 0x00, /* 4 */

        /* INTR_TABLE */
        RES_TYPE_INTR_TABLE, 0, 0, 0,
        0x00, 0x50, 0x00, 0x00, /* 0x5000 */
        0x04, 0x00, 0x00, 0x00, /* 4 */

        /* INTR_PBA */
        RES_TYPE_INTR_PBA, 0, 0, 0,
        0x00, 0x60, 0x00, 0x00, /* 0x6000 */
        0x04, 0x00, 0x00, 0x00, /* 4 */

        /* DEVCMD */
        RES_TYPE_DEVCMD, 0, 0, 0,
        0x00, 0x70, 0x00, 0x00, /* 0x7000 */
        0x01, 0x00, 0x00, 0x00, /* 1 */

        /* NIC_CFG */
        RES_TYPE_NIC_CFG, 0, 0, 0,
        0x00, 0x80, 0x00, 0x00, /* 0x8000 */
        0x01, 0x00, 0x00, 0x00, /* 1 */

        /* EOL */
        RES_TYPE_EOL, 0, 0, 0,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00
    };

    if (addr < sizeof(res_table_bytes)) {
        const uint8_t *ptr = res_table_bytes;
        if (size == 1) val = ptr[addr];
        else if (size == 2) val = *(uint16_t *)(ptr + addr);
        else if (size == 4) val = *(uint32_t *)(ptr + addr);
        else if (size == 8) val = *(uint64_t *)(ptr + addr);
        return val;
    }

    if (addr >= 0x7000 && addr < 0x7000 + sizeof(struct vnic_devcmd)) {
        uint32_t offset = addr - 0x7000;
        uint8_t *ptr = (uint8_t *)&s->devcmd;
        if (size == 1) val = ptr[offset];
        else if (size == 2) val = *(uint16_t *)(ptr + offset);
        else if (size == 4) val = *(uint32_t *)(ptr + offset);
        else if (size == 8) val = *(uint64_t *)(ptr + offset);
        return val;
    }

    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= 0x7000 && addr < 0x7000 + sizeof(struct vnic_devcmd)) {
        uint32_t offset = addr - 0x7000;
        uint8_t *ptr = (uint8_t *)&s->devcmd;
        if (size == 1) ptr[offset] = val;
        else if (size == 2) *(uint16_t *)(ptr + offset) = val;
        else if (size == 4) *(uint32_t *)(ptr + offset) = val;
        else if (size == 8) *(uint64_t *)(ptr + offset) = val;

        if (offset == offsetof(struct vnic_devcmd, cmd)) {
            s->devcmd.status = 0;
            memset(s->devcmd.args, 0, sizeof(s->devcmd.args));
            
            s->devcmd.args[0] = 0x0000000000000000;
            
            struct vnic_fc_config *cfg = (struct vnic_fc_config *)s->devcmd.args;
            cfg->flags = 0x0040; 
            cfg->maxdatafieldsize = 2112;
        }
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    (void)opaque;
    (void)addr;
    (void)size;
    uint64_t val = 0;
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    (void)opaque;
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
    pci_device_reset(PCI_DEVICE(dev));
    s->intr_status = 0;
    s->intr_mask = 0;
    s->state = 0;
    s->reset_in_progress = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_CISCO );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_CISCO_FNIC );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_STORAGE_FIBRE );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 1;  
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x10000;
    s->bar_info[0].name = "fnic-bar0";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msix = true;  /* msi_init or msix_init calls */
    s->has_msi = true;
    
    if (msix_init_exclusive_bar(pdev, 64, 1, errp)) {
        return;
    }
    if (msi_init(pdev, 0, 1, true, false, errp)) {
        return;
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit_exclusive_bar(pdev);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "fnic_pci",
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
