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

#define CMD_SYS_EN              0x0001
#define CMD_QUERY_FW            0x0002
#define CMD_QUERY_DDR           0x0003
#define CMD_QUERY_DEV_LIM       0x0004
#define CMD_INIT_HCA            0x0005
#define CMD_CLOSE_HCA           0x0006
#define CMD_NOP                 0x0007

/* Updated mthca_dev_lim structure matching the driver's definition */
typedef struct {
    int max_srq_sz;
    int max_qp_sz;
    int reserved_qps;
    int max_qps;
    int reserved_srqs;
    int max_srqs;
    int reserved_eecs;
    int max_eecs;
    int max_cq_sz;
    int reserved_cqs;
    int max_cqs;
    int max_mpts;
    int reserved_eqs;
    int max_eqs;
    int reserved_mtts;
    int max_mrw_sz;
    int reserved_mrws;
    int max_mtt_seg;
    int max_requester_per_qp;
    int max_responder_per_qp;
    int max_rdma_global;
    int local_ca_ack_delay;
    int max_mtu;
    int max_port_width;
    int max_vl;
    int num_ports;
    int max_gids;
    uint16_t stat_rate_support;
    int max_pkeys;
    uint32_t flags;
    int reserved_uars;
    int uar_size;
    int min_page_sz;
    int max_sg;
    int max_desc_sz;
    int max_qp_per_mcg;
    int reserved_mgms;
    int max_mcgs;
    int reserved_pds;
    int max_pds;
    int reserved_rdds;
    int max_rdds;
    int eec_entry_sz;
    int qpc_entry_sz;
    int eeec_entry_sz;
    int eqpc_entry_sz;
    int eqc_entry_sz;
    int cqc_entry_sz;
    int srq_entry_sz;
    int uar_scratch_entry_sz;
    int mpt_entry_sz;
    union {
        struct {
            int max_avs;
        } tavor;
        struct {
            int resize_srq;
            int max_pbl_sz;
            uint8_t  bmme_flags;
            uint32_t reserved_lkey;
            int lam_required;
            uint64_t max_icm_sz;
        } arbel;
    } hca;
} __attribute__((packed)) mthca_dev_lim;

#define DEV_LIM_FLAG_BAD_PKEY_CNTR   (1 << 0)
#define DEV_LIM_FLAG_BAD_QKEY_CNTR   (1 << 1)
#define DEV_LIM_FLAG_RAW_MULTI       (1 << 2)
#define DEV_LIM_FLAG_AUTO_PATH_MIG   (1 << 3)
#define DEV_LIM_FLAG_UD_AV_PORT_ENFORCE (1 << 4)
#define DEV_LIM_FLAG_SRQ             (1 << 5)

#define TYPE_PCIBASE_DEVICE "ib_mthca_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define VENDOR_ID 0x15b3
#define DEVICE_ID 0x5a44
#define CLASS_ID  0x0c06

#define MTHCA_HCR_BASE         0x80680
#define MTHCA_HCR_SIZE         0x0001c
#define MTHCA_ECR_BASE         0x80700
#define MTHCA_ECR_SIZE         0x00008
#define MTHCA_ECR_CLR_SIZE     0x00008
#define MTHCA_CLR_INT_BASE     0xf00d8
#define MTHCA_CLR_INT_SIZE     0x00008
#define MTHCA_EQ_SET_CI_SIZE   (8 * 32)
#define MTHCA_EQ_DOORBELL      0x28

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

    /* HCR shadow registers */
    uint32_t hcr_cmd;
    uint32_t hcr_status;
    uint64_t hcr_in_param;
    uint64_t hcr_out_param;
    uint32_t hcr_go;

    /* Event/clear registers */
    uint32_t ecr;
    uint32_t clr_int;

    /* Operational status */
    uint32_t status;
    bool reset_done;
    uint8_t pm_state;

    /* Command completion timer */
    QEMUTimer cmd_timer;
    bool cmd_pending;

    /* DMA helper */
    uint32_t fw_ver;
};

G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->intr_status) {
        if (msix_enabled(pdev)) {
            msix_notify(pdev, 2); /* CMD EQ vector */
        } else if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!msix_enabled(pdev) && !msi_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

static void mthca_cmd_complete(PCIBaseState *s)
{
    if (!s->cmd_pending)
        return;

    s->cmd_pending = false;
    s->hcr_go = 0;
    s->hcr_status = 0; /* success */

    /* Simulate completion event on CMD EQ */
    s->intr_status |= 1 << 2;
    pcibase_update_irq(s);
}

static void mthca_cmd_timer_cb(void *opaque)
{
    PCIBaseState *s = opaque;
    mthca_cmd_complete(s);
}

static void mthca_submit_command(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint64_t out_addr = s->hcr_out_param;
    uint32_t op = s->hcr_cmd & 0xffff;

    s->cmd_pending = true;
    s->hcr_go = 1;
    s->hcr_status = 0xffffffff; /* busy */

    /* Process known commands */
    switch (op) {
    case CMD_SYS_EN:
        break;
    case CMD_QUERY_FW: {
        uint32_t fw_ver[4] = { 5, 0, 0, 1 }; /* major, minor, submin, reserved */
        pci_dma_write(pdev, out_addr, fw_ver, sizeof(fw_ver));
        break;
    }
    case CMD_QUERY_DDR:
        break;
    case CMD_QUERY_DEV_LIM: {
        mthca_dev_lim lim = {0};
        /* Fill in all fields, matching real hardware capabilities */
        lim.max_srq_sz = 256;
        lim.max_qp_sz = 256;
        lim.reserved_qps = 1;
        lim.max_qps = 256;
        lim.reserved_srqs = 1;
        lim.max_srqs = 256;
        lim.reserved_eecs = 0;
        lim.max_eecs = 0;
        lim.max_cq_sz = 256;
        lim.reserved_cqs = 1;
        lim.max_cqs = 256;
        lim.max_mpts = 256;
        lim.reserved_eqs = 1;
        lim.max_eqs = 3;
        lim.reserved_mtts = 1;
        lim.max_mrw_sz = 0x4000000;
        lim.reserved_mrws = 1;
        lim.max_mtt_seg = 512;
        lim.max_requester_per_qp = 16;
        lim.max_responder_per_qp = 16;
        lim.max_rdma_global = 256;
        lim.local_ca_ack_delay = 0;
        lim.max_mtu = 4;
        lim.max_port_width = 4;
        lim.max_vl = 1;
        lim.num_ports = 1;
        lim.max_gids = 16;
        lim.stat_rate_support = 0x3;
        lim.max_pkeys = 16;
        lim.flags = DEV_LIM_FLAG_BAD_PKEY_CNTR | DEV_LIM_FLAG_BAD_QKEY_CNTR
                    | DEV_LIM_FLAG_RAW_MULTI | DEV_LIM_FLAG_AUTO_PATH_MIG
                    | DEV_LIM_FLAG_UD_AV_PORT_ENFORCE | DEV_LIM_FLAG_SRQ;
        lim.reserved_uars = 1;
        lim.uar_size = 0x100000; /* 1MB */
        lim.min_page_sz = 4096;
        lim.max_sg = 32;
        lim.max_desc_sz = 256;
        lim.max_qp_per_mcg = 256;
        lim.reserved_mgms = 0;
        lim.max_mcgs = 512;
        lim.reserved_pds = 1;
        lim.max_pds = 256;
        lim.reserved_rdds = 0;
        lim.max_rdds = 0;
        lim.eec_entry_sz = 0;
        lim.qpc_entry_sz = 256;
        lim.eeec_entry_sz = 0;
        lim.eqpc_entry_sz = 256;
        lim.eqc_entry_sz = 64;
        lim.cqc_entry_sz = 64;
        lim.srq_entry_sz = 64;
        lim.uar_scratch_entry_sz = 64;
        lim.mpt_entry_sz = 64;
        /* Arbel-specific fields */
        lim.hca.arbel.resize_srq = 1;
        lim.hca.arbel.max_pbl_sz = 512 * 1024; /* 512KB */
        lim.hca.arbel.bmme_flags = 0;
        lim.hca.arbel.reserved_lkey = 0;
        lim.hca.arbel.lam_required = 0;
        lim.hca.arbel.max_icm_sz = 1ULL << 40; /* 1TB */
        pci_dma_write(pdev, out_addr, &lim, sizeof(lim));
        break;
    }
    case CMD_INIT_HCA:
        break;
    case CMD_CLOSE_HCA:
        break;
    case CMD_NOP:
        break;
    default:
        break;
    }

    /* Schedule completion after a short delay */
    timer_mod(&s->cmd_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 1);
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= MTHCA_HCR_BASE && addr < MTHCA_HCR_BASE + MTHCA_HCR_SIZE) {
        /* HCR region */
        hwaddr reg = addr - MTHCA_HCR_BASE;
        switch (reg) {
        case 0x00: val = s->hcr_cmd; break;
        case 0x04: val = s->hcr_status; break;
        case 0x08: val = (uint32_t)(s->hcr_in_param); break;
        case 0x0c: val = (uint32_t)(s->hcr_in_param >> 32); break;
        case 0x10: val = (uint32_t)(s->hcr_out_param); break;
        case 0x14: val = (uint32_t)(s->hcr_out_param >> 32); break;
        case 0x18: val = s->hcr_go; break;
        default: val = 0; break;
        }
    } else if (addr >= MTHCA_ECR_BASE && addr < MTHCA_ECR_BASE + MTHCA_ECR_SIZE) {
        val = s->ecr;
    } else if (addr >= MTHCA_CLR_INT_BASE && addr < MTHCA_CLR_INT_BASE + MTHCA_CLR_INT_SIZE) {
        val = s->clr_int;
    } else {
        val = 0;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= MTHCA_HCR_BASE && addr < MTHCA_HCR_BASE + MTHCA_HCR_SIZE) {
        hwaddr reg = addr - MTHCA_HCR_BASE;
        switch (reg) {
        case 0x00: s->hcr_cmd = val; break;
        case 0x04: s->hcr_status = val; break;
        case 0x08: s->hcr_in_param = (s->hcr_in_param & 0xFFFFFFFF00000000ULL) | val; break;
        case 0x0c: s->hcr_in_param = (s->hcr_in_param & 0xFFFFFFFF) | ((uint64_t)val << 32); break;
        case 0x10: s->hcr_out_param = (s->hcr_out_param & 0xFFFFFFFF00000000ULL) | val; break;
        case 0x14: s->hcr_out_param = (s->hcr_out_param & 0xFFFFFFFF) | ((uint64_t)val << 32); break;
        case 0x18:
            s->hcr_go = val ? 1 : 0;
            if (s->hcr_go) {
                mthca_submit_command(s);
            }
            break;
        default: break;
        }
    } else if (addr >= MTHCA_ECR_BASE && addr < MTHCA_ECR_BASE + MTHCA_ECR_SIZE) {
        s->ecr = val;
        /* Writing to ECR may clear pending events */
        s->intr_status &= ~val;
        pcibase_update_irq(s);
    } else if (addr >= MTHCA_CLR_INT_BASE && addr < MTHCA_CLR_INT_BASE + MTHCA_CLR_INT_SIZE) {
        s->clr_int = val;
        s->intr_status &= ~val;
        pcibase_update_irq(s);
    }
}

static uint64_t pcibase_bar2_read(void *opaque, hwaddr addr, unsigned size)
{
    /* UAR region reads return 0 */
    return 0;
}

static void pcibase_bar2_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    /* Doorbell write: 64-bit write triggers command submission */
    if (size == 8) {
        /* The driver writes hi, lo; opcode is in high 32 bits */
        s->hcr_cmd = (uint32_t)(val >> 32);
        s->hcr_go = 1;
        mthca_submit_command(s);
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static const MemoryRegionOps pcibase_bar2_ops = {
    .read = pcibase_bar2_read,
    .write = pcibase_bar2_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 8 },
    .impl  = { .min_access_size = 4, .max_access_size = 8 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    s->hcr_cmd = 0;
    s->hcr_status = 0;
    s->hcr_in_param = 0;
    s->hcr_out_param = 0;
    s->hcr_go = 0;
    s->ecr = 0;
    s->clr_int = 0;
    s->intr_status = 0;
    s->cmd_pending = false;
    s->fw_ver = 0x05000001;
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }
    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        if (bi->index == 0) {
            memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, aligned_size);
        } else if (bi->index == 2) {
            memory_region_init_io(mr, OBJECT(s), &pcibase_bar2_ops, s, bi->name, aligned_size);
        }
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, aligned_size);
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

    /* Initialize MSI-X with 3 vectors, table in BAR0 at offset 0x1000, PBA at 0x1800 */
    if (msix_init(pdev, 3, &s->bar_regions[0], 0, 0x1000, &s->bar_regions[0], 0, 0x1800, 0, errp)) {
        /* If MSI-X init fails, we can fallback to MSI, but for now just return error */
        return;
    }

    /* BAR configuration: BAR0 1MB MMIO, BAR2 8MB MMIO (UAR), BAR4 optional (hidden) */
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = 1 * MiB, .name = "bar0" };
    s->bar_info[2] = (BARInfo){ .index = 2, .type = BAR_TYPE_MMIO, .size = 8 * MiB, .name = "bar2" };
    s->num_bars = 2;
    for (int i = 0; i < 6; i++) {
        if (s->bar_info[i].type != BAR_TYPE_NONE) {
            pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
        }
    }

    /* Initialize command timer */
    timer_init_ms(&s->cmd_timer, QEMU_CLOCK_VIRTUAL, mthca_cmd_timer_cb, s);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    timer_del(&s->cmd_timer);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

static const VMStateDescription vmstate_pcibase = {
    .name = "ib_mthca_pci",
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
