/*
 * QEMU PCI device model for Amazon EFA (Elastic Fabric Adapter).
 * Derived from Linux driver efa_main.c and register offsets.
 * Implements minimal admin queue processing to allow driver probe.
 *
 * Updated with common descriptor structs from driver source.
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

/* ---------------------------------------------------------------------------
 * Temporary placeholder definitions for admin command opcodes and structures.
 * These will be replaced when actual driver header (efa_admin.h) is available.
 * --------------------------------------------------------------------------- */
#ifndef EFA_ADMIN_GET_DEVICE_ATTR_CMD
#define EFA_ADMIN_GET_DEVICE_ATTR_CMD   0x01
#endif
#ifndef EFA_ADMIN_GET_HW_HINTS_CMD
#define EFA_ADMIN_GET_HW_HINTS_CMD      0x02
#endif
#ifndef EFA_ADMIN_CREATE_EQ_CMD
#define EFA_ADMIN_CREATE_EQ_CMD         0x03
#endif
#ifndef EFA_ADMIN_SET_FEATURE_CMD
#define EFA_ADMIN_SET_FEATURE_CMD       0x04
#endif
#ifndef EFA_ADMIN_SET_AENQ_CONFIG_CMD
#define EFA_ADMIN_SET_AENQ_CONFIG_CMD   0x05
#endif

/* Submission queue common descriptor */
struct efa_admin_aq_common_desc {
    uint16_t command_id;
    uint8_t opcode;
    uint8_t flags;
};

/* Completion queue common descriptor */
struct efa_admin_acq_common_desc {
    uint16_t command;
    uint8_t status;
    uint8_t flags;
    uint16_t extended_status;
    uint16_t sq_head_indx;
};

/* Common memory address (low/high) */
struct efa_common_mem_addr {
    uint32_t mem_addr_low;
    uint32_t mem_addr_high;
};

/* Updated from driver source: efa_com_get_device_attr_result */
#define EFA_GID_SIZE 16   /* Standard InfiniBand GID size */

struct efa_com_get_device_attr_result {
    uint8_t addr[EFA_GID_SIZE];
    uint64_t page_size_cap;
    uint64_t max_mr_pages;
    uint64_t guid;
    uint32_t mtu;
    uint32_t fw_version;
    uint32_t admin_api_version;
    uint32_t device_version;
    uint32_t supported_features;
    uint32_t phys_addr_width;
    uint32_t virt_addr_width;
    uint32_t max_qp;
    uint32_t max_sq_depth; /* wqes */
    uint32_t max_rq_depth; /* wqes */
    uint32_t max_cq;
    uint32_t max_cq_depth; /* cqes */
    uint32_t inline_buf_size;
    uint32_t inline_buf_size_ex;
    uint32_t max_mr;
    uint32_t max_pd;
    uint32_t max_ah;
    uint32_t max_llq_size;
    uint32_t max_rdma_size;
    uint32_t device_caps;
    uint32_t max_eq;
    uint32_t max_eq_depth;
    uint32_t event_bitmask; /* EQ events bitmask */
    uint16_t sub_cqs_per_cq;
    uint16_t max_sq_sge;
    uint16_t max_rq_sge;
    uint16_t max_wr_rdma_sge;
    uint16_t max_tx_batch;
    uint16_t min_sq_depth;
    uint16_t max_link_speed_gbps;
    uint8_t db_bar;
} __attribute__((packed));

/*
 * Structure from driver source: efa_admin_create_eq_cmd
 */
struct efa_admin_create_eq_cmd {
    struct efa_admin_aq_common_desc aq_common_descriptor;

    /* Size of the EQ in entries, must be power of 2 */
    uint16_t depth;

    /* MSI-X table entry index */
    uint8_t msix_vec;

    /*
     * 4:0 : entry_size_words - size of EQ entry in
     *    32-bit words
     * 7:5 : reserved - MBZ
     */
    uint8_t caps;

    /* EQ ring base address */
    struct efa_common_mem_addr ba;

    /*
     * Enabled events on this EQ
     * 0 : completion_events - Enable completion events
     * 31:1 : reserved - MBZ
     */
    uint32_t event_bitmask;

    /* MBZ */
    uint32_t reserved;
};

/* Create EQ result (unchanged) */
struct efa_admin_create_eq_result {
    uint16_t eq_idx;
    uint16_t pad;
    uint32_t status;
};

/* End of placeholder definitions */

#define TYPE_PCIBASE_DEVICE "efa_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_AMAZON 0x1D0F
#define PCI_DEV_ID_EFA0_VF 0xefa0
#define CLASS_ID 0x0c06 /* InfiniBand */

/* Register Offsets */
#define EFA_REGS_VERSION_OFF            0x0
#define EFA_REGS_CONTROLLER_VERSION_OFF 0x4
#define EFA_REGS_CAPS_OFF               0x8
#define EFA_REGS_AQ_BASE_LO_OFF         0x10
#define EFA_REGS_AQ_BASE_HI_OFF         0x14
#define EFA_REGS_AQ_CAPS_OFF            0x18
#define EFA_REGS_ACQ_BASE_LO_OFF        0x20
#define EFA_REGS_ACQ_BASE_HI_OFF        0x24
#define EFA_REGS_ACQ_CAPS_OFF           0x28
#define EFA_REGS_AQ_PROD_DB_OFF         0x2c
#define EFA_REGS_AENQ_CAPS_OFF          0x34
#define EFA_REGS_AENQ_BASE_LO_OFF       0x38
#define EFA_REGS_AENQ_BASE_HI_OFF       0x3c
#define EFA_REGS_AENQ_CONS_DB_OFF       0x40
#define EFA_REGS_INTR_MASK_OFF          0x4c
#define EFA_REGS_DEV_CTL_OFF            0x54
#define EFA_REGS_DEV_STS_OFF            0x58
#define EFA_REGS_MMIO_REG_READ_OFF      0x5c
#define EFA_REGS_MMIO_RESP_LO_OFF       0x60
#define EFA_REGS_MMIO_RESP_HI_OFF       0x64
#define EFA_REGS_EQ_DB_OFF              0x68

/* BAR indices */
#define EFA_REG_BAR 0
#define EFA_MEM_BAR 2

/* BAR sizes (power of 2) */
#define EFA_REG_BAR_SIZE  0x1000
#define EFA_MEM_BAR_SIZE  0x1000

/* MSI-X: management vector index */
#define EFA_MGMNT_MSIX_VEC_IDX 0

/* Device status bits (fictional but needed for reset logic) */
#define DEV_STS_READY         0x00000001
#define DEV_STS_RESET_ACTIVE  0x00000002

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

/* Admin Queue state */
typedef struct AdminQueue {
    uint64_t base;
    uint32_t caps;
    uint16_t prod_idx;
    uint16_t cons_idx;
    bool active;
} AdminQueue;

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

    /* Hardware Register Shadows */
    uint32_t regs[64]; /* 32-bit registers */

    /* Admin Subsystem */
    AdminQueue asq;   /* Submission Queue */
    AdminQueue acq;   /* Completion Queue */
    AdminQueue aenq;  /* Async Event Queue */

    /* EQ created via admin command */
    struct {
        uint64_t base;
        uint32_t caps;
        uint16_t msix_vector;
        uint16_t eq_idx;
        bool valid;
    } eq[16];

    /* MMIO_REG_READ state */
    uint32_t mmio_read_req;
    uint64_t mmio_read_resp;

    uint32_t dev_sts;
    uint32_t dev_ctl;
};

/* IRQ update: raise MSI-X if unmasked and status indicates */
static void pcibase_update_irq(PCIBaseState *s, uint32_t vector)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (msix_enabled(pdev)) {
        msix_notify(pdev, vector);
    } else if (msi_enabled(pdev)) {
        msi_notify(pdev, 0);
    } else {
        pci_set_irq(pdev, 1);
    }
}

/* Process a single admin command entry */
static void pcibase_process_admin_cmd(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    AdminQueue *asq = &s->asq;
    AdminQueue *acq = &s->acq;
    uint32_t entry_size, depth;
    hwaddr cmd_addr;
    uint8_t cmd_buf[256]; /* safe max entry size */
    uint8_t opcode;
    uint16_t command_id;
    uint64_t ctrl_buf_addr = 0;
    uint32_t status = 0;

    /* Parse caps: bits 15:0 = entry size, bits 31:16 = log2(depth) */
    entry_size = asq->caps & 0xFFFF;
    depth = 1U << ((asq->caps >> 16) & 0xF);
    if (!entry_size || entry_size > 256 || !depth) {
        return;
    }

    /* Calculate address of next command */
    uint16_t idx = asq->cons_idx % depth;
    cmd_addr = asq->base + (uint64_t)idx * entry_size;

    /* Fetch command from guest DMA */
    pci_dma_read(pdev, cmd_addr, cmd_buf, entry_size);

    /* Parse submission common descriptor (first 4 bytes) */
    struct efa_admin_aq_common_desc sub_desc;
    memcpy(&sub_desc, cmd_buf, sizeof(sub_desc));
    opcode = sub_desc.opcode;
    command_id = sub_desc.command_id;

    /* Check for control buffer address if indicated (flag bit 1) */
    if (sub_desc.flags & 0x02) {
        if (entry_size >= sizeof(sub_desc) + sizeof(struct efa_common_mem_addr)) {
            struct efa_common_mem_addr addr;
            memcpy(&addr, cmd_buf + sizeof(sub_desc), sizeof(addr));
            ctrl_buf_addr = ((uint64_t)addr.mem_addr_high << 32) | addr.mem_addr_low;
        }
    }

    /* Process command by opcode, using ctrl_buf_addr as result buffer */
    switch (opcode) {
    case EFA_ADMIN_GET_DEVICE_ATTR_CMD:
        {
            struct efa_com_get_device_attr_result res = {0};
            res.guid = 0x123456789ABCDEF0ULL;
            res.max_eq = 8;
            res.max_eq_depth = 4096;
            res.db_bar = 0;    /* doorbell in BAR0 */
            res.page_size_cap = 0x1000;
            res.phys_addr_width = 48;
            res.virt_addr_width = 48;
            res.device_version = 0x00010000;
            res.admin_api_version = 0x00010000;
            if (ctrl_buf_addr) {
                pci_dma_write(pdev, ctrl_buf_addr, (uint8_t *)&res, sizeof(res));
            }
            status = 0;
        }
        break;

    case EFA_ADMIN_GET_HW_HINTS_CMD:
        /* Return zero hints (acceptable defaults) */
        if (ctrl_buf_addr) {
            uint8_t zeroes[64] = {0};
            pci_dma_write(pdev, ctrl_buf_addr, zeroes, sizeof(zeroes));
        }
        status = 0;
        break;

    case EFA_ADMIN_CREATE_EQ_CMD:
        {
            struct efa_admin_create_eq_cmd eq_cmd;
            if (entry_size >= sizeof(eq_cmd)) {
                memcpy(&eq_cmd, cmd_buf, sizeof(eq_cmd));
                /* Allocate a free EQ slot (very simplistic) */
                int eqn = -1;
                for (int i = 0; i < 16; i++) {
                    if (!s->eq[i].valid) {
                        eqn = i;
                        break;
                    }
                }
                if (eqn >= 0) {
                    /* Convert base address from efa_common_mem_addr to 64-bit */
                    uint64_t base = ((uint64_t)eq_cmd.ba.mem_addr_high << 32) | eq_cmd.ba.mem_addr_low;
                    s->eq[eqn].base = base;
                    /* Store capabilities: combine entry_size_words (from caps[4:0]) and maybe depth? 
                     * For now keep caps as a placeholder, driver might not use it. 
                     * We'll store depth and entry_size_words for future use. 
                     * Note: eq_cmd.caps contains entry_size_words, eq_cmd.depth has depth.
                     */
                    uint32_t entry_size_words = eq_cmd.caps & 0x1F;
                    s->eq[eqn].caps = entry_size_words; /* Simplification */
                    s->eq[eqn].msix_vector = eq_cmd.msix_vec;
                    s->eq[eqn].valid = true;
                    s->eq[eqn].eq_idx = eqn;

                    struct efa_admin_create_eq_result eq_res = {
                        .eq_idx = eqn,
                        .status = 0,
                    };
                    if (ctrl_buf_addr) {
                        pci_dma_write(pdev, ctrl_buf_addr, (uint8_t *)&eq_res, sizeof(eq_res));
                    }
                    status = 0;
                } else {
                    status = 1; /* no resource */
                }
            } else {
                status = 1;
            }
        }
        break;

    case EFA_ADMIN_SET_FEATURE_CMD:
        /* Just acknowledge success */
        status = 0;
        break;

    case EFA_ADMIN_SET_AENQ_CONFIG_CMD:
        /* Accept and ignore */
        status = 0;
        break;

    default:
        qemu_log_mask(LOG_UNIMP, "EFA: unknown admin opcode %u\n", opcode);
        status = 1;
        break;
    }

    /* Write completion entry to ACQ */
    if (acq->active) {
        uint32_t acq_entry_size = acq->caps & 0xFFFF;
        uint32_t acq_depth = 1U << ((acq->caps >> 16) & 0xF);
        if (acq_entry_size >= sizeof(struct efa_admin_acq_common_desc) && acq_depth > 0) {
            uint16_t comp_idx = acq->prod_idx % acq_depth;
            hwaddr comp_addr = acq->base + (uint64_t)comp_idx * acq_entry_size;
            struct efa_admin_acq_common_desc comp;
            comp.command = command_id;
            comp.status = status;
            /* Compute phase: toggle on wrap; phase = (prod_idx / depth) & 1 */
            uint32_t phase_bit = (acq->prod_idx / acq_depth) & 1;
            comp.flags = (phase_bit << 0);
            comp.extended_status = 0;
            comp.sq_head_indx = asq->cons_idx;  /* index of the consumed command */
            pci_dma_write(pdev, comp_addr, (uint8_t *)&comp, sizeof(comp));
            /* Zero out remaining bytes if entry is larger */
            if (acq_entry_size > sizeof(comp)) {
                uint8_t zero[1024] = {0};
                if (acq_entry_size - sizeof(comp) <= 1024) {
                    pci_dma_write(pdev, comp_addr + sizeof(comp), zero, acq_entry_size - sizeof(comp));
                }
            }
            acq->prod_idx++;
        }
    }

    /* Advance consumer index */
    asq->cons_idx++;

    /* Raise management interrupt */
    pcibase_update_irq(s, EFA_MGMNT_MSIX_VEC_IDX);
}

/* Doorbell write: AQ_PROD_DB */
static void pcibase_aq_doorbell(PCIBaseState *s, uint32_t val)
{
    /* The doorbell value is the new producer index */
    s->asq.prod_idx = (uint16_t)val;
    /* Process all pending commands */
    while (s->asq.cons_idx != s->asq.prod_idx) {
        pcibase_process_admin_cmd(s);
    }
}

/* Doorbell write: AENQ_CONS_DB */
static void pcibase_aenq_doorbell(PCIBaseState *s, uint32_t val)
{
    s->aenq.cons_idx = (uint16_t)val;
    /* No processing needed for async events during probe */
}

/* Doorbell write: EQ_DB (event queue producer index) */
static void pcibase_eq_doorbell(PCIBaseState *s, uint32_t val)
{
    /* Not implemented for probe; EQ completions not needed yet */
}

/* MMIO Read */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t offset = addr & 0x7F;
    uint64_t val = 0;

    if (size != 4 && size != 8) {
        return ~0ULL;
    }

    switch (offset) {
    case EFA_REGS_VERSION_OFF:
        val = 0x00010000;  /* major 1, minor 0 */
        break;
    case EFA_REGS_CONTROLLER_VERSION_OFF:
        val = 0x00010000;
        break;
    case EFA_REGS_CAPS_OFF:
        val = 0;  /* basic capabilities */
        break;
    case EFA_REGS_AQ_BASE_LO_OFF:
        val = (uint32_t)s->asq.base;
        break;
    case EFA_REGS_AQ_BASE_HI_OFF:
        val = (uint32_t)(s->asq.base >> 32);
        break;
    case EFA_REGS_AQ_CAPS_OFF:
        val = s->asq.caps;
        break;
    case EFA_REGS_ACQ_BASE_LO_OFF:
        val = (uint32_t)s->acq.base;
        break;
    case EFA_REGS_ACQ_BASE_HI_OFF:
        val = (uint32_t)(s->acq.base >> 32);
        break;
    case EFA_REGS_ACQ_CAPS_OFF:
        val = s->acq.caps;
        break;
    case EFA_REGS_AENQ_CAPS_OFF:
        val = s->aenq.caps;
        break;
    case EFA_REGS_AENQ_BASE_LO_OFF:
        val = (uint32_t)s->aenq.base;
        break;
    case EFA_REGS_AENQ_BASE_HI_OFF:
        val = (uint32_t)(s->aenq.base >> 32);
        break;
    case EFA_REGS_INTR_MASK_OFF:
        val = s->intr_mask;
        break;
    case EFA_REGS_DEV_STS_OFF:
        val = s->dev_sts;
        break;
    case EFA_REGS_MMIO_RESP_LO_OFF:
        val = (uint32_t)s->mmio_read_resp;
        break;
    case EFA_REGS_MMIO_RESP_HI_OFF:
        val = (uint32_t)(s->mmio_read_resp >> 32);
        break;
    default:
        val = s->regs[offset / 4];
        break;
    }
    return val;
}

/* MMIO Write */
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t offset = addr & 0x7F;

    if (size != 4 && size != 8) {
        return;
    }

    switch (offset) {
    case EFA_REGS_AQ_BASE_LO_OFF:
        s->asq.base = (s->asq.base & 0xFFFFFFFF00000000ULL) | val;
        break;
    case EFA_REGS_AQ_BASE_HI_OFF:
        s->asq.base = (s->asq.base & 0xFFFFFFFF) | ((uint64_t)val << 32);
        break;
    case EFA_REGS_AQ_CAPS_OFF:
        s->asq.caps = val;
        s->asq.active = true;
        break;
    case EFA_REGS_ACQ_BASE_LO_OFF:
        s->acq.base = (s->acq.base & 0xFFFFFFFF00000000ULL) | val;
        break;
    case EFA_REGS_ACQ_BASE_HI_OFF:
        s->acq.base = (s->acq.base & 0xFFFFFFFF) | ((uint64_t)val << 32);
        break;
    case EFA_REGS_ACQ_CAPS_OFF:
        s->acq.caps = val;
        s->acq.active = true;
        break;
    case EFA_REGS_AQ_PROD_DB_OFF:
        pcibase_aq_doorbell(s, val);
        break;
    case EFA_REGS_AENQ_CAPS_OFF:
        s->aenq.caps = val;
        break;
    case EFA_REGS_AENQ_BASE_LO_OFF:
        s->aenq.base = (s->aenq.base & 0xFFFFFFFF00000000ULL) | val;
        break;
    case EFA_REGS_AENQ_BASE_HI_OFF:
        s->aenq.base = (s->aenq.base & 0xFFFFFFFF) | ((uint64_t)val << 32);
        break;
    case EFA_REGS_AENQ_CONS_DB_OFF:
        pcibase_aenq_doorbell(s, val);
        break;
    case EFA_REGS_INTR_MASK_OFF:
        s->intr_mask = val;
        break;
    case EFA_REGS_DEV_CTL_OFF:
        s->dev_ctl = val;
        /* Any write triggers immediate reset completion */
        s->dev_sts = DEV_STS_READY;
        break;
    case EFA_REGS_MMIO_REG_READ_OFF:
        s->mmio_read_req = val;
        /* Perform read of requested register and store result */
        s->mmio_read_resp = pcibase_mmio_read(s, val, 4);
        break;
    case EFA_REGS_EQ_DB_OFF:
        pcibase_eq_doorbell(s, val);
        break;
    default:
        s->regs[offset / 4] = val;
        break;
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 8 },
    .impl  = { .min_access_size = 4, .max_access_size = 8 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Reset queue state */
    memset(&s->asq, 0, sizeof(s->asq));
    memset(&s->acq, 0, sizeof(s->acq));
    memset(&s->aenq, 0, sizeof(s->aenq));
    for (int i = 0; i < 16; i++) {
        s->eq[i].valid = false;
    }
    s->intr_mask = 0;
    s->dev_sts = 0;
    s->dev_ctl = 0;
    s->mmio_read_req = 0;
    s->mmio_read_resp = 0;
    memset(s->regs, 0, sizeof(s->regs));
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
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;
    Error *local_err = NULL;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_AMAZON);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEV_ID_EFA0_VF);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* PCI Express */
    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, &local_err);
    if (pm_pos < 0) {
        error_propagate(errp, local_err);
        return;
    }
    pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);

    /* BAR Initialization */
    s->num_bars = 2;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = EFA_REG_BAR_SIZE;
    s->bar_info[0].name = "efa-regs";
    s->bar_info[1].index = 2;
    s->bar_info[1].type = BAR_TYPE_RAM;
    s->bar_info[1].size = EFA_MEM_BAR_SIZE;
    s->bar_info[1].name = "efa-mem";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            return;
        }
    }

    /* MSI-X setup with 2 vectors (management + 1 completion) */
    if (msix_init(pdev, 2, &s->bar_regions[2], 2, 0, &s->bar_regions[2], 2, 0, 0, &local_err)) {
        error_propagate(errp, local_err);
        return;
    }

    /* Initialize admin and queue state */
    pcibase_reset(DEVICE(s));
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, &s->bar_regions[2], &s->bar_regions[2]);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

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
