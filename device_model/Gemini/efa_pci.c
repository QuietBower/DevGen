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
#include "qemu/bitops.h"
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

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

#define TYPE_PCIBASE_DEVICE "efa_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_AMAZON 0x1d0f
#define PCI_DEV_ID_EFA0_VF 0xefa0
#define EFA_REG_BAR 0
#define EFA_MEM_BAR 2

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

#define EFA_REGS_DEV_STS_READY_MASK                         0x1
#define EFA_REGS_CAPS_RESET_TIMEOUT_MASK                    0x3e
#define EFA_REGS_DEV_CTL_DEV_RESET_MASK                     0x1
#define EFA_REGS_DEV_CTL_RESET_REASON_MASK                  0xf0000000
#define EFA_REGS_CAPS_ADMIN_CMD_TO_MASK                     0xf0000
#define EFA_REGS_VERSION_MAJOR_VERSION_MASK                 0xff00
#define EFA_REGS_VERSION_MINOR_VERSION_MASK                 0xff
#define EFA_REGS_CONTROLLER_VERSION_MAJOR_VERSION_MASK      0xff0000
#define EFA_REGS_CONTROLLER_VERSION_MINOR_VERSION_MASK      0xff00
#define EFA_REGS_CONTROLLER_VERSION_SUBMINOR_VERSION_MASK   0xff
#define EFA_REGS_CONTROLLER_VERSION_IMPL_ID_MASK            0xff000000
#define EFA_REGS_CAPS_DMA_ADDR_WIDTH_MASK                   0xff00
#define EFA_ADMIN_API_VERSION_MAJOR          0
#define EFA_ADMIN_API_VERSION_MINOR          1
#define EFA_CTRL_MAJOR          0
#define EFA_CTRL_MINOR          0
#define EFA_CTRL_SUB_MINOR      1

#define EFA_ADMIN_AQ_COMMON_DESC_CTRL_DATA_MASK             BIT(1)
#define EFA_ADMIN_CREATE_EQ_CMD_COMPLETION_EVENTS_MASK      BIT(0)
#define EFA_ADMIN_EQE_PHASE_MASK                            BIT(0)
#define EFA_ADMIN_AENQ_COMMON_DESC_PHASE_MASK               BIT(0)

#define EFA_REGS_MMIO_REG_READ_REG_OFF_MASK                 0xffff0000
#define EFA_REGS_MMIO_REG_READ_REQ_ID_MASK                  0xffff

enum efa_admin_aq_feature_id {
    EFA_ADMIN_DEVICE_ATTR                       = 1,
    EFA_ADMIN_AENQ_CONFIG                       = 2,
    EFA_ADMIN_NETWORK_ATTR                      = 3,
    EFA_ADMIN_QUEUE_ATTR_1                      = 4,
    EFA_ADMIN_HW_HINTS                          = 5,
    EFA_ADMIN_HOST_INFO                         = 6,
    EFA_ADMIN_EVENT_QUEUE_ATTR                  = 7,
    EFA_ADMIN_QUEUE_ATTR_2                      = 9,
};

enum efa_admin_aq_opcode {
    EFA_ADMIN_CREATE_QP                         = 1,
    EFA_ADMIN_MODIFY_QP                         = 2,
    EFA_ADMIN_QUERY_QP                          = 3,
    EFA_ADMIN_DESTROY_QP                        = 4,
    EFA_ADMIN_CREATE_AH                         = 5,
    EFA_ADMIN_DESTROY_AH                        = 6,
    EFA_ADMIN_REG_MR                            = 7,
    EFA_ADMIN_DEREG_MR                          = 8,
    EFA_ADMIN_CREATE_CQ                         = 9,
    EFA_ADMIN_DESTROY_CQ                        = 10,
    EFA_ADMIN_GET_FEATURE                       = 11,
    EFA_ADMIN_SET_FEATURE                       = 12,
    EFA_ADMIN_GET_STATS                         = 13,
    EFA_ADMIN_ALLOC_PD                          = 14,
    EFA_ADMIN_DEALLOC_PD                        = 15,
    EFA_ADMIN_ALLOC_UAR                         = 16,
    EFA_ADMIN_DEALLOC_UAR                       = 17,
    EFA_ADMIN_CREATE_EQ                         = 18,
    EFA_ADMIN_DESTROY_EQ                        = 19,
    EFA_ADMIN_ALLOC_MR                          = 20,
    EFA_ADMIN_MAX_OPCODE                        = 20,
};

struct efa_common_mem_addr {
    u32 mem_addr_low;
    u32 mem_addr_high;
};

struct efa_admin_acq_common_desc {
    u16 command;
    u8 status;
    u8 flags;
    u16 extended_status;
    u16 sq_head_indx;
};

struct efa_admin_aq_common_desc {
    u16 command_id;
    u8 opcode;
    u8 flags;
};

struct efa_admin_aenq_common_desc {
    u16 group;
    u16 syndrome;
    u8 flags;
    u8 reserved1[3];
    u32 timestamp_low;
    u32 timestamp_high;
};

struct efa_admin_ctrl_buff_info {
    u32 length;
    struct efa_common_mem_addr address;
};

struct efa_admin_get_set_feature_common_desc {
    u8 reserved0;
    u8 feature_id;
    u16 reserved16;
};

struct efa_admin_feature_aenq_desc {
    u32 supported_groups;
    u32 enabled_groups;
};

struct efa_admin_feature_device_attr_desc {
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
};

struct efa_admin_feature_network_attr_desc {
    u8 addr[16];
    u32 mtu;
};

struct efa_admin_feature_queue_attr_desc_1 {
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
};

struct efa_admin_feature_queue_attr_desc_2 {
    u16 inline_buf_size_ex;
};

struct efa_admin_event_queue_attr_desc {
    u32 max_eq;
    u32 max_eq_depth;
    u32 event_bitmask;
};

struct efa_admin_hw_hints {
    u16 mmio_read_timeout;
    u16 driver_watchdog_timeout;
    u16 admin_completion_timeout;
    u16 poll_interval;
};

struct efa_admin_get_feature_resp {
    struct efa_admin_acq_common_desc acq_common_desc;
    union {
        u32 raw[14];
        struct efa_admin_feature_device_attr_desc device_attr;
        struct efa_admin_feature_aenq_desc aenq;
        struct efa_admin_feature_network_attr_desc network_attr;
        struct efa_admin_feature_queue_attr_desc_1 queue_attr_1;
        struct efa_admin_feature_queue_attr_desc_2 queue_attr_2;
        struct efa_admin_event_queue_attr_desc event_queue_attr;
        struct efa_admin_hw_hints hw_hints;
    } u;
};

struct efa_admin_set_feature_resp {
    struct efa_admin_acq_common_desc acq_common_desc;
    union {
        u32 raw[14];
    } u;
};

struct efa_admin_set_feature_cmd {
    struct efa_admin_aq_common_desc aq_common_descriptor;
    struct efa_admin_ctrl_buff_info control_buffer;
    struct efa_admin_get_set_feature_common_desc feature_common;
    union {
        u32 raw[11];
        struct efa_admin_feature_aenq_desc aenq;
    } u;
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

struct efa_com_create_eq_params {
    dma_addr_t dma_addr;
    u32 event_bitmask;
    u16 depth;
    u8 entry_size_in_bytes;
    u8 msix_vec;
};

struct efa_com_create_eq_result {
    u16 eqn;
};

struct efa_com_destroy_eq_params {
    u16 eqn;
};

struct efa_admin_comp_event {
    u16 cqn;
    u16 reserved;
    u32 reserved2;
};

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
    uint32_t intr_status;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t version;
    uint32_t controller_version;
    uint32_t caps;
    uint32_t dev_ctl;
    uint32_t dev_sts;
    uint32_t mmio_reg_read;
    uint32_t mmio_resp_lo;
    uint32_t mmio_resp_hi;

    /* DMA Context */
    uint32_t aq_base_lo;
    uint32_t aq_base_hi;
    uint32_t aq_caps;
    uint32_t aq_prod_db;
    uint32_t acq_base_lo;
    uint32_t acq_base_hi;
    uint32_t acq_caps;
    uint32_t aenq_caps;
    uint32_t aenq_base_lo;
    uint32_t aenq_base_hi;
    uint32_t aenq_cons_db;
    uint32_t eq_db;
};

static void pcibase_reset(DeviceState *dev);

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
    case EFA_REGS_VERSION_OFF:
        val = s->version;
        break;
    case EFA_REGS_CONTROLLER_VERSION_OFF:
        val = s->controller_version;
        break;
    case EFA_REGS_CAPS_OFF:
        val = s->caps;
        break;
    case EFA_REGS_AQ_BASE_LO_OFF:
        val = s->aq_base_lo;
        break;
    case EFA_REGS_AQ_BASE_HI_OFF:
        val = s->aq_base_hi;
        break;
    case EFA_REGS_AQ_CAPS_OFF:
        val = s->aq_caps;
        break;
    case EFA_REGS_ACQ_BASE_LO_OFF:
        val = s->acq_base_lo;
        break;
    case EFA_REGS_ACQ_BASE_HI_OFF:
        val = s->acq_base_hi;
        break;
    case EFA_REGS_ACQ_CAPS_OFF:
        val = s->acq_caps;
        break;
    case EFA_REGS_AQ_PROD_DB_OFF:
        val = s->aq_prod_db;
        break;
    case EFA_REGS_AENQ_CAPS_OFF:
        val = s->aenq_caps;
        break;
    case EFA_REGS_AENQ_BASE_LO_OFF:
        val = s->aenq_base_lo;
        break;
    case EFA_REGS_AENQ_BASE_HI_OFF:
        val = s->aenq_base_hi;
        break;
    case EFA_REGS_AENQ_CONS_DB_OFF:
        val = s->aenq_cons_db;
        break;
    case EFA_REGS_INTR_MASK_OFF:
        val = s->intr_mask;
        break;
    case EFA_REGS_DEV_CTL_OFF:
        val = s->dev_ctl;
        break;
    case EFA_REGS_DEV_STS_OFF:
        val = s->dev_sts;
        break;
    case EFA_REGS_MMIO_REG_READ_OFF:
        val = s->mmio_reg_read;
        break;
    case EFA_REGS_MMIO_RESP_LO_OFF:
        val = s->mmio_resp_lo;
        break;
    case EFA_REGS_MMIO_RESP_HI_OFF:
        val = s->mmio_resp_hi;
        break;
    case EFA_REGS_EQ_DB_OFF:
        val = s->eq_db;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset 0x%"HWADDR_PRIx"\n", __func__, addr);
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case EFA_REGS_AQ_BASE_LO_OFF:
        s->aq_base_lo = val;
        break;
    case EFA_REGS_AQ_BASE_HI_OFF:
        s->aq_base_hi = val;
        break;
    case EFA_REGS_AQ_CAPS_OFF:
        s->aq_caps = val;
        break;
    case EFA_REGS_ACQ_BASE_LO_OFF:
        s->acq_base_lo = val;
        break;
    case EFA_REGS_ACQ_BASE_HI_OFF:
        s->acq_base_hi = val;
        break;
    case EFA_REGS_ACQ_CAPS_OFF:
        s->acq_caps = val;
        break;
    case EFA_REGS_AQ_PROD_DB_OFF:
        s->aq_prod_db = val;
        break;
    case EFA_REGS_AENQ_CAPS_OFF:
        s->aenq_caps = val;
        break;
    case EFA_REGS_AENQ_BASE_LO_OFF:
        s->aenq_base_lo = val;
        break;
    case EFA_REGS_AENQ_BASE_HI_OFF:
        s->aenq_base_hi = val;
        break;
    case EFA_REGS_AENQ_CONS_DB_OFF:
        s->aenq_cons_db = val;
        break;
    case EFA_REGS_INTR_MASK_OFF:
        s->intr_mask = val;
        break;
    case EFA_REGS_DEV_CTL_OFF:
        s->dev_ctl = val;
        if (val & EFA_REGS_DEV_CTL_DEV_RESET_MASK) {
            pcibase_reset(DEVICE(s));
        }
        break;
    case EFA_REGS_MMIO_REG_READ_OFF:
        s->mmio_reg_read = val;
        {
            uint16_t req_id = val & EFA_REGS_MMIO_REG_READ_REQ_ID_MASK;
            uint16_t reg_off = (val & EFA_REGS_MMIO_REG_READ_REG_OFF_MASK) >> 16;
            uint32_t reg_val = pcibase_mmio_read(s, reg_off, 4);
            s->mmio_resp_lo = req_id | (reg_off << 16);
            s->mmio_resp_hi = reg_val;
        }
        break;
    case EFA_REGS_MMIO_RESP_LO_OFF:
        s->mmio_resp_lo = val;
        break;
    case EFA_REGS_MMIO_RESP_HI_OFF:
        s->mmio_resp_hi = val;
        break;
    case EFA_REGS_EQ_DB_OFF:
        s->eq_db = val;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset 0x%"HWADDR_PRIx"\n", __func__, addr);
        break;
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

    s->version = (EFA_ADMIN_API_VERSION_MAJOR << 8) | EFA_ADMIN_API_VERSION_MINOR;
    s->controller_version = (EFA_CTRL_MAJOR << 16) | (EFA_CTRL_MINOR << 8) | EFA_CTRL_SUB_MINOR;
    s->caps = 0;
    s->dev_ctl = 0;
    s->dev_sts = EFA_REGS_DEV_STS_READY_MASK;
    s->mmio_reg_read = 0;
    s->mmio_resp_lo = 0;
    s->mmio_resp_hi = 0;
    s->aq_base_lo = 0;
    s->aq_base_hi = 0;
    s->aq_caps = 0;
    s->aq_prod_db = 0;
    s->acq_base_lo = 0;
    s->acq_base_hi = 0;
    s->acq_caps = 0;
    s->aenq_caps = 0;
    s->aenq_base_lo = 0;
    s->aenq_base_hi = 0;
    s->aenq_cons_db = 0;
    s->eq_db = 0;
    s->intr_mask = 0;
    s->intr_status = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_AMAZON );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEV_ID_EFA0_VF );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0280 );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 2;
    s->bar_info[0].index = EFA_REG_BAR;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 8192; /* Increased size to fit MSI-X table and PBA */
    s->bar_info[0].name = "efa-reg";

    s->bar_info[1].index = EFA_MEM_BAR;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = 4096; /* Placeholder size */
    s->bar_info[1].name = "efa-mem";
  
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msix = true;
    msix_init_exclusive_bar(pdev, 256, 4, errp);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (s->has_msix) {
        msix_uninit_exclusive_bar(pdev);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "efa_pci",
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
