/*
 * QEMU EFA (Elastic Fabric Adapter) PCI Device Model
 * 
 * Based on Linux driver: drivers/infiniband/hw/efa/efa_main.c
 * Target QEMU: 8.2.10
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include <inttypes.h>
#include <string.h>
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "sysemu/dma.h"
#include "sysemu/reset.h"
#include "hw/irq.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/pci/pcie.h"
#include "qom/object.h"
#include "qapi/visitor.h"
#include "hw/qdev-properties.h"
#include "qemu/bitops.h"

#define TYPE_PCIBASE_DEVICE "efa_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Related register/macros/constants from driver (Stage1) */
#define VENDOR_ID 0x1d0f
#define DEVICE_ID 0xefa0
#define CLASS_ID  PCI_CLASS_OTHERS

#define EFA_REG_BAR 0
#define EFA_MEM_BAR 2
#define EFA_DB_BAR  4 /* Inferred from driver logic requesting a DB BAR */
#define EFA_BASE_BAR_MASK (BIT(EFA_REG_BAR) | BIT(EFA_MEM_BAR))

#define EFA_REGS_VERSION_OFF                                0x0
#define EFA_REGS_CONTROLLER_VERSION_OFF                     0x4
#define EFA_REGS_CAPS_OFF                                   0x8
#define EFA_REGS_AQ_BASE_LO_OFF                             0x10
#define EFA_REGS_AQ_BASE_HI_OFF                             0x14
#define EFA_REGS_AQ_CAPS_OFF                                0x18
#define EFA_REGS_ACQ_BASE_LO_OFF                            0x20
#define EFA_REGS_ACQ_BASE_HI_OFF                            0x24
#define EFA_REGS_ACQ_CAPS_OFF                               0x28
#define EFA_REGS_AQ_PROD_DB_OFF                             0x2c
#define EFA_REGS_AENQ_CAPS_OFF                              0x34
#define EFA_REGS_AENQ_BASE_LO_OFF                           0x38
#define EFA_REGS_AENQ_BASE_HI_OFF                           0x3c
#define EFA_REGS_AENQ_CONS_DB_OFF                           0x40
#define EFA_REGS_INTR_MASK_OFF                              0x4c
#define EFA_REGS_DEV_CTL_OFF                                0x54
#define EFA_REGS_DEV_STS_OFF                                0x58
#define EFA_REGS_MMIO_REG_READ_OFF                          0x5c
#define EFA_REGS_MMIO_RESP_LO_OFF                           0x60
#define EFA_REGS_MMIO_RESP_HI_OFF                           0x64
#define EFA_REGS_EQ_DB_OFF                                  0x68

#define EFA_GID_SIZE 16

/* Inferred Opcodes (Not in Stage 1, required for functionality) */
#define EFA_ADMIN_CREATE_QP     0x10
#define EFA_ADMIN_CREATE_CQ     0x11
#define EFA_ADMIN_REG_MR        0x12
#define EFA_ADMIN_GET_FEATURE   0x16
#define EFA_ADMIN_SET_FEATURE   0x17
#define EFA_ADMIN_CREATE_EQ     0x18
#define EFA_ADMIN_HOST_INFO     0x19

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

enum efa_regs_reset_reason_types {
    EFA_REGS_RESET_NORMAL                       = 0,
    EFA_REGS_RESET_KEEP_ALIVE_TO                = 1,
    EFA_REGS_RESET_ADMIN_TO                     = 2,
    EFA_REGS_RESET_INIT_ERR                     = 3,
    EFA_REGS_RESET_DRIVER_INVALID_STATE         = 4,
    EFA_REGS_RESET_OS_TRIGGER                   = 5,
    EFA_REGS_RESET_SHUTDOWN                     = 6,
    EFA_REGS_RESET_USER_TRIGGER                 = 7,
    EFA_REGS_RESET_GENERIC                      = 8,
};

enum efa_admin_aq_feature_id {
    EFA_ADMIN_DEVICE_ATTR                       = 1,
    EFA_ADMIN_AENQ_CONFIG                       = 2,
    EFA_ADMIN_NETWORK_ATTR                      = 3,
    EFA_ADMIN_QUEUE_ATTR                        = 4,
    EFA_ADMIN_HW_HINTS                          = 5,
    EFA_ADMIN_HOST_INFO_ID                      = 6, /* Renamed to avoid conflict with macro */
    EFA_ADMIN_EVENT_QUEUE_ATTR                  = 7,
};

/* ------------------------------------------------------------------ */
/* BAR metadata definition                                             */
/* ------------------------------------------------------------------ */
typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM 
} BARType;

typedef struct {
    int index;
    BARType type;
    hwaddr size;
    const char *name;
    bool sparse;
} BARInfo;

/* ------------------------------------------------------------------ */
/* Device State                                                        */
/* ------------------------------------------------------------------ */
struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;
    
    /* Admin Queue State */
    dma_addr_t aq_base;
    dma_addr_t acq_base;
    uint32_t aq_prod;
    uint32_t acq_cons;
    uint16_t aq_depth;
    uint16_t acq_depth;

    /* Register shadows */
    struct {
        uint32_t version;
        uint32_t controller_version;
        uint32_t caps;
        uint32_t aq_base_lo;
        uint32_t aq_base_hi;
        uint32_t aq_caps;
        uint32_t acq_base_lo;
        uint32_t acq_base_hi;
        uint32_t acq_caps;
        uint32_t aq_prod_db;
        uint32_t aenq_caps;
        uint32_t aenq_base_lo;
        uint32_t aenq_base_hi;
        uint32_t aenq_cons_db;
        uint32_t intr_mask;
        uint32_t dev_ctl;
        uint32_t dev_sts;
        uint32_t mmio_reg_read;
        uint32_t mmio_resp_lo;
        uint32_t mmio_resp_hi;
        uint32_t eq_db;
    } regs;
};

struct efa_common_mem_addr {
    u32 mem_addr_low;
    u32 mem_addr_high;
};

struct efa_admin_aq_common_desc {
    u16 command_id;
    u8 opcode;
    u8 flags;
};

struct efa_admin_acq_common_desc {
    u16 command;
    u8 status;
    u8 flags;
    u16 extended_status;
    u16 sq_head_indx;
};

struct efa_admin_ctrl_buff_info {
    u32 length;
    struct efa_common_mem_addr address;
};

struct efa_admin_aq_entry {
    struct efa_admin_aq_common_desc aq_common_descriptor;
    union {
        u32 inline_data_w1[3];
        struct efa_admin_ctrl_buff_info control_buffer;
    } u;
    u32 inline_data_w4[12];
};

struct efa_admin_acq_entry {
    struct efa_admin_acq_common_desc acq_common_descriptor;
    u32 response_specific_data[14];
};

struct efa_admin_get_feature_cmd {
    struct efa_admin_aq_common_desc aq_common_descriptor;
    struct efa_admin_ctrl_buff_info control_buffer;
    struct {
        u8 reserved0;
        u8 feature_id;
        u16 reserved16;
    } feature_common;
    u32 raw[11];
};

struct efa_admin_get_feature_resp {
    struct efa_admin_acq_common_desc acq_common_desc;
    union {
        u32 raw[14];
        struct {
            u64 supported_features;
            u64 page_size_cap;
            u32 fw_version;
            u32 admin_api_version;
            u32 device_version;
            u16 db_bar;
            u8 phys_addr_width;
            u8 virt_addr_width;
            u32 device_caps;
            u32 max_rdma_size;
            u64 guid;
            u16 max_link_speed_gbps;
            u16 reserved0;
            u32 reserved1;
        } device_attr;
        struct {
            u32 supported_groups;
            u32 enabled_groups;
        } aenq;
        struct {
            u8 addr[16];
            u32 mtu;
        } network_attr;
        struct {
            u32 max_qp;
            u32 max_sq_depth;
            u32 inline_buf_size;
            u32 max_rq_depth;
            u32 max_cq;
            u32 max_cq_depth;
            u16 sub_cqs_per_cq;
            u16 min_sq_depth;
            u16 max_wr_send_sges;
            u16 max_wr_recv_sges;
            u32 max_mr;
            u32 max_mr_pages;
            u32 max_pd;
            u32 max_ah;
            u32 max_llq_size;
            u16 max_wr_rdma_sges;
            u16 max_tx_batch;
        } queue_attr;
        struct {
            u32 max_eq;
            u32 max_eq_depth;
            u32 event_bitmask;
        } event_queue_attr;
        struct {
            u16 mmio_read_timeout;
            u16 driver_watchdog_timeout;
            u16 admin_completion_timeout;
            u16 poll_interval;
        } hw_hints;
    } u;
};

/* ------------------------------------------------------------------ */
/* Forward declarations                                                */
/* ------------------------------------------------------------------ */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static void pcibase_reset(DeviceState *dev);
static void pcibase_realize(PCIDevice *pdev, Error **errp);
static void pcibase_uninit(PCIDevice *pdev);

/* ------------------------------------------------------------------ */
/* MemoryRegionOps                                                      */
/* ------------------------------------------------------------------ */
static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 8 },
    .impl  = { .min_access_size = 4, .max_access_size = 8 },
};

static const MemoryRegionOps pcibase_db_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 8 },
    .impl  = { .min_access_size = 4, .max_access_size = 8 },
};

/* ------------------------------------------------------------------ */
/* Helper: register a BAR (MMIO or PIO)                                 */
/* ------------------------------------------------------------------ */
static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE)
        return;

    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, bi->size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

/* ------------------------------------------------------------------ */
/* Admin Queue Processing                                              */
/* ------------------------------------------------------------------ */
static void efa_process_admin_command(PCIBaseState *s)
{
    struct efa_admin_aq_entry cmd;
    struct efa_admin_acq_entry cqe;
    dma_addr_t cmd_addr, cqe_addr;
    
    /* Simple ring processing: assume 1 command per doorbell for now */
    /* In reality, we should loop until head == tail */
    
    cmd_addr = s->aq_base + (s->aq_prod % 4096) * sizeof(cmd); /* Simplified masking */
    if (dma_memory_read(&address_space_memory, cmd_addr, &cmd, sizeof(cmd), MEMTXATTRS_UNSPECIFIED)) {
        return;
    }

    memset(&cqe, 0, sizeof(cqe));
    cqe.acq_common_descriptor.command = cmd.aq_common_descriptor.command_id;
    cqe.acq_common_descriptor.status = 0; /* Success */

    switch (cmd.aq_common_descriptor.opcode) {
    case EFA_ADMIN_GET_FEATURE: {
        struct efa_admin_get_feature_cmd *gf_cmd = (struct efa_admin_get_feature_cmd *)&cmd;
        struct efa_admin_get_feature_resp *gf_resp = (struct efa_admin_get_feature_resp *)&cqe;
        dma_addr_t buf_addr = ((u64)gf_cmd->control_buffer.address.mem_addr_high << 32) | 
                              gf_cmd->control_buffer.address.mem_addr_low;
        u32 buf_len = gf_cmd->control_buffer.length;

        switch (gf_cmd->feature_common.feature_id) {
        case EFA_ADMIN_DEVICE_ATTR:
            gf_resp->u.device_attr.db_bar = EFA_DB_BAR;
            gf_resp->u.device_attr.supported_features = -1ULL;
            gf_resp->u.device_attr.page_size_cap = 4096;
            gf_resp->u.device_attr.fw_version = 1;
            gf_resp->u.device_attr.device_version = 1;
            gf_resp->u.device_attr.phys_addr_width = 48;
            gf_resp->u.device_attr.virt_addr_width = 48;
            break;
        case EFA_ADMIN_HW_HINTS:
            gf_resp->u.hw_hints.mmio_read_timeout = 100;
            gf_resp->u.hw_hints.poll_interval = 10;
            gf_resp->u.hw_hints.admin_completion_timeout = 1000;
            break;
        default:
            break;
        }
        
        /* Write back response data if buffer provided */
        if (buf_len > 0) {
            /* We are writing the response structure into the buffer provided by the driver */
            /* Note: The driver expects the data in the buffer, not just in the ACQ entry */
            /* Actually, EFA returns data in the buffer pointed to by control_buffer */
            /* We need to construct the data and write it to buf_addr */
            /* For simplicity, we write the whole union u from resp to buffer */
             dma_memory_write(&address_space_memory, buf_addr, &gf_resp->u, 
                              MIN(buf_len, sizeof(gf_resp->u)), MEMTXATTRS_UNSPECIFIED);
        }
        break;
    }
    case EFA_ADMIN_CREATE_EQ:
    case EFA_ADMIN_CREATE_CQ:
    case EFA_ADMIN_CREATE_QP:
    case EFA_ADMIN_REG_MR:
    case EFA_ADMIN_SET_FEATURE:
    case EFA_ADMIN_HOST_INFO:
        /* Return success for all mandatory init commands */
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "EFA: Unknown Admin Opcode 0x%x\n", cmd.aq_common_descriptor.opcode);
        break;
    }

    /* Post completion */
    cqe_addr = s->acq_base + (s->acq_cons % 4096) * sizeof(cqe);
    dma_memory_write(&address_space_memory, cqe_addr, &cqe, sizeof(cqe), MEMTXATTRS_UNSPECIFIED);
    
    s->acq_cons++;
    
    /* Raise MSI-X interrupt (Vector 0 for Admin) */
    if (s->has_msix) {
        msix_notify(&s->parent_obj, 0);
    }
}

/* ------------------------------------------------------------------ */
/* MMIO handlers                                                       */
/* ------------------------------------------------------------------ */

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case EFA_REGS_VERSION_OFF:
        val = 1;
        break;
    case EFA_REGS_CONTROLLER_VERSION_OFF:
        val = 1;
        break;
    case EFA_REGS_CAPS_OFF:
        val = 0;
        break;
    case EFA_REGS_AQ_BASE_LO_OFF:
        val = s->regs.aq_base_lo;
        break;
    case EFA_REGS_AQ_BASE_HI_OFF:
        val = s->regs.aq_base_hi;
        break;
    case EFA_REGS_ACQ_BASE_LO_OFF:
        val = s->regs.acq_base_lo;
        break;
    case EFA_REGS_ACQ_BASE_HI_OFF:
        val = s->regs.acq_base_hi;
        break;
    case EFA_REGS_DEV_STS_OFF:
        val = s->regs.dev_sts | 1; /* Ready */
        break;
    case EFA_REGS_MMIO_REG_READ_OFF:
        val = s->regs.mmio_reg_read;
        break;
    case EFA_REGS_MMIO_RESP_LO_OFF:
        val = s->regs.mmio_resp_lo;
        break;
    case EFA_REGS_MMIO_RESP_HI_OFF:
        val = s->regs.mmio_resp_hi;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "[%s] mmio_read addr=0x%" PRIx64 "\n", TYPE_PCIBASE_DEVICE, addr);
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case EFA_REGS_DEV_CTL_OFF:
        s->regs.dev_ctl = val;
        if (val == EFA_REGS_RESET_NORMAL) {
            pcibase_reset(DEVICE(s));
        }
        break;
    case EFA_REGS_AQ_BASE_LO_OFF:
        s->regs.aq_base_lo = val;
        s->aq_base = (s->aq_base & 0xFFFFFFFF00000000ULL) | val;
        break;
    case EFA_REGS_AQ_BASE_HI_OFF:
        s->regs.aq_base_hi = val;
        s->aq_base = (s->aq_base & 0x00000000FFFFFFFFULL) | ((uint64_t)val << 32);
        break;
    case EFA_REGS_ACQ_BASE_LO_OFF:
        s->regs.acq_base_lo = val;
        s->acq_base = (s->acq_base & 0xFFFFFFFF00000000ULL) | val;
        break;
    case EFA_REGS_ACQ_BASE_HI_OFF:
        s->regs.acq_base_hi = val;
        s->acq_base = (s->acq_base & 0x00000000FFFFFFFFULL) | ((uint64_t)val << 32);
        break;
    case EFA_REGS_AQ_PROD_DB_OFF:
        s->regs.aq_prod_db = val;
        s->aq_prod = val;
        efa_process_admin_command(s);
        break;
    case EFA_REGS_INTR_MASK_OFF:
        s->regs.intr_mask = val;
        break;
    case EFA_REGS_MMIO_REG_READ_OFF:
        s->regs.mmio_reg_read = val;
        /* Echo back for simple test */
        s->regs.mmio_resp_lo = val;
        s->regs.mmio_resp_hi = 0;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "[%s] mmio_write addr=0x%" PRIx64 " val=0x%" PRIx64 "\n", TYPE_PCIBASE_DEVICE, addr, val);
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Reset       */
/* ------------------------------------------------------------------ */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    
    memset(&s->regs, 0, sizeof(s->regs));
    s->aq_base = 0;
    s->acq_base = 0;
    s->aq_prod = 0;
    s->acq_cons = 0;
    s->regs.version = 1;
    s->regs.controller_version = 1;
}

/* ------------------------------------------------------------------ */
/* Realize (device init)                                                */
/* ------------------------------------------------------------------ */
static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* BAR 0: Registers */
    s->bar_info[0].index = EFA_REG_BAR;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x4000; /* 16KB */
    s->bar_info[0].name = "efa-regs";
    pcibase_register_bar(pdev, s, &s->bar_info[0], errp);

    /* BAR 2: Memory */
    s->bar_info[2].index = EFA_MEM_BAR;
    s->bar_info[2].type = BAR_TYPE_RAM;
    s->bar_info[2].size = 1 * MiB;
    s->bar_info[2].name = "efa-mem";
    pcibase_register_bar(pdev, s, &s->bar_info[2], errp);

    /* BAR 4: Doorbell (Requested dynamically by driver) */
    /* We register it here so it exists when driver requests it */
    MemoryRegion *mr_db = g_new(MemoryRegion, 1);
    memory_region_init_io(mr_db, OBJECT(s), &pcibase_db_ops, s, "efa-db", 0x1000);
    pci_register_bar(pdev, EFA_DB_BAR, PCI_BASE_ADDRESS_SPACE_MEMORY, mr_db);

    /* MSI-X Init */
    if (msix_init(pdev, 16, &s->bar_regions[EFA_REG_BAR], EFA_REG_BAR, 0x2000, 
                  &s->bar_regions[EFA_REG_BAR], EFA_REG_BAR, 0x3000, 0x0, errp) == 0) {
        s->has_msix = true;
    }
}

/* ------------------------------------------------------------------ */
/* Uninit/cleanup                                                      */
/* ------------------------------------------------------------------ */
static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (s->has_msix) {
        msix_uninit(pdev, &s->bar_regions[0], &s->bar_regions[0]);
        s->has_msix = false;
    }
}

/* ------------------------------------------------------------------ */
/* Class init / type registration                                      */
/* ------------------------------------------------------------------ */
static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pcibase_realize;
    k->exit    = pcibase_uninit;
    dc->reset  = pcibase_reset;

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
