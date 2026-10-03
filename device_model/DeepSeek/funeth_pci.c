/*
 * QEMU model of Fungible Ethernet PCI device (funeth)
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
#include "qemu/error-report.h"

#define TYPE_PCIBASE_DEVICE "funeth_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define VENDOR_ID 0x1dad
#define DEVICE_ID 0x0101
#define CLASS_ID 0x0200 /* PCI_CLASS_NETWORK_ETHERNET */

#define BAR0_SIZE (2 * MiB)

#define NVME_REG_DBS 0x1000
#define DB_OFFSET NVME_REG_DBS

#ifndef ADMIN_SQE_SIZE
#define ADMIN_SQE_SIZE 64
#endif
#ifndef ADMIN_CQE_SIZE
#define ADMIN_CQE_SIZE 64
#endif

#ifndef FUN_ADMIN_OP_PORT
#define FUN_ADMIN_OP_PORT 0
#endif
#ifndef FUN_ADMIN_OP_ADI
#define FUN_ADMIN_OP_ADI 1
#endif
#ifndef FUN_ADMIN_OP_RSS
#define FUN_ADMIN_OP_RSS 2
#endif
#ifndef FUN_ADMIN_OP_ETH
#define FUN_ADMIN_OP_ETH 3
#endif
#ifndef FUN_ADMIN_OP_VI
#define FUN_ADMIN_OP_VI 4
#endif
#ifndef FUN_ADMIN_SUBOP_CREATE
#define FUN_ADMIN_SUBOP_CREATE 0
#endif
#ifndef FUN_ADMIN_SUBOP_READ
#define FUN_ADMIN_SUBOP_READ 1
#endif
#ifndef FUN_ADMIN_SUBOP_WRITE
#define FUN_ADMIN_SUBOP_WRITE 2
#endif
#ifndef FUN_ADMIN_SUBOP_MODIFY
#define FUN_ADMIN_SUBOP_MODIFY 3
#endif
#ifndef FUN_ADMIN_PORT_KEY_MACADDR
#define FUN_ADMIN_PORT_KEY_MACADDR 100
#endif
#ifndef FUN_ADMIN_PORT_KEY_CAPABILITIES
#define FUN_ADMIN_PORT_KEY_CAPABILITIES 101
#endif
#ifndef FUN_ADMIN_PORT_KEY_MTU
#define FUN_ADMIN_PORT_KEY_MTU 102
#endif
#ifndef FUN_PORT_CAP_VPORT
#define FUN_PORT_CAP_VPORT 0x1
#endif

#pragma pack(push, 1)
struct fun_cqe_info {
    uint16_t sqhd;   /* big-endian */
    uint16_t sqid;   /* big-endian */
    uint16_t cid;    /* big-endian */
    uint16_t sf_p;   /* big-endian */
};

struct fun_admin_req_common {
    uint8_t op;
    uint8_t len8;
    uint16_t flags;   /* big-endian */
    uint8_t suboff8;
    uint8_t rsvd0;
    uint16_t cid;     /* big-endian */
};

typedef struct {
    uint8_t subop;
    uint8_t rsvd0[3];
    uint32_t id;   /* big-endian */
    uint16_t lport; /* big-endian */
    uint8_t rsvd1[6];
} fun_admin_port_create_rsp;

typedef struct {
    uint8_t subop;
    uint8_t rsvd0[3];
    uint32_t id; /* big-endian */
} fun_admin_port_read_rsp_hdr;

struct fun_admin_port_create_req {
    uint8_t subop;
    uint8_t rsvd0;
    uint16_t flags;  /* big-endian */
    uint32_t id;     /* big-endian */
};

struct fun_admin_read48_req {
    uint64_t key_pack; /* big-endian */
};

struct fun_admin_port_read_req {
    uint8_t subop;
    uint8_t rsvd0;
    uint16_t flags;  /* big-endian */
    uint32_t id;     /* big-endian */
    struct fun_admin_read48_req read48[];
};
#pragma pack(pop)

#define FUN_CQE_INFO_SIZE sizeof(struct fun_cqe_info)

#define FUN_ADMIN_READ48_RSP_DATA_S 0U
#define FUN_ADMIN_READ48_RSP_DATA_M 0xffffffffffff
#define FUN_ADMIN_READ48_RSP_RET_S 48U
#define FUN_ADMIN_READ48_RSP_RET_M 0xff

typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
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

    uint64_t cap;          /* Controller Capabilities, offset 0x00 */
    uint32_t vs;           /* Version, offset 0x08 */
    uint32_t cc;           /* Controller Configuration, offset 0x14 */
    uint32_t csts;         /* Controller Status, offset 0x1C */

    struct {
        dma_addr_t admin_cq_dma;
        dma_addr_t admin_sq_dma;
        uint16_t admin_sq_aqa;  /* raw value from AQA (0-based) */
        uint16_t admin_cq_aqa;
        uint32_t admin_sq_head;
        uint32_t admin_sq_tail;
        uint32_t admin_cq_head;
        uint32_t admin_cq_tail;
        bool admin_cq_phase;
    } dma;

    uint32_t status;
    uint8_t mac[6];
};

static void pcibase_process_admin_sq(PCIBaseState *s);

G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->intr_status & ~s->intr_mask) {
        if (msix_enabled(pdev)) {
            msix_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!msix_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case 0x00: /* CAP */
        if (size == 8 || size == 4) {
            val = s->cap;
        }
        break;
    case 0x08: /* VS */
        val = s->vs;
        break;
    case 0x14: /* CC */
        val = s->cc;
        break;
    case 0x1C: /* CSTS */
        val = s->csts;
        break;
    case 0x24: /* AQA */
        val = (s->dma.admin_sq_aqa & 0xFFF) | ((s->dma.admin_cq_aqa & 0xFFF) << 16);
        break;
    case 0x28: /* ASQ low */
        val = s->dma.admin_sq_dma & 0xFFFFFFFF;
        break;
    case 0x2c: /* ASQ high */
        val = (s->dma.admin_sq_dma >> 32) & 0xFFFFFFFF;
        break;
    case 0x30: /* ACQ low */
        val = s->dma.admin_cq_dma & 0xFFFFFFFF;
        break;
    case 0x34: /* ACQ high */
        val = (s->dma.admin_cq_dma >> 32) & 0xFFFFFFFF;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "funeth: unknown MMIO read at 0x%"HWADDR_PRIx"\n", addr);
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= DB_OFFSET) {
        uint32_t db_index = (addr - DB_OFFSET) / 4;
        if (db_index == 0) { /* Admin SQ doorbell */
            s->dma.admin_sq_tail = val;
            pcibase_process_admin_sq(s);
        } else if (db_index == 1) { /* Admin CQ doorbell */
            s->dma.admin_cq_head = val;
        }
        return;
    }

    switch (addr) {
    case 0x14: /* CC */
        s->cc = val;
        if (val & 1) {
            s->csts |= 1; /* CSTS.RDY */
        } else {
            s->csts &= ~1;
        }
        break;
    case 0x24: /* AQA */
        {
            uint16_t sq_size = val & 0xFFF;
            uint16_t cq_size = (val >> 16) & 0xFFF;
            s->dma.admin_sq_aqa = sq_size;
            s->dma.admin_cq_aqa = cq_size;
        }
        break;
    case 0x28: /* ASQ low */
        s->dma.admin_sq_dma = (s->dma.admin_sq_dma & 0xFFFFFFFF00000000ULL) | (val & 0xFFFFFFFF);
        break;
    case 0x2c: /* ASQ high */
        s->dma.admin_sq_dma = (s->dma.admin_sq_dma & 0xFFFFFFFF) | ((uint64_t)(val & 0xFFFFFFFF) << 32);
        break;
    case 0x30: /* ACQ low */
        s->dma.admin_cq_dma = (s->dma.admin_cq_dma & 0xFFFFFFFF00000000ULL) | (val & 0xFFFFFFFF);
        break;
    case 0x34: /* ACQ high */
        s->dma.admin_cq_dma = (s->dma.admin_cq_dma & 0xFFFFFFFF) | ((uint64_t)(val & 0xFFFFFFFF) << 32);
        break;
    case 0x20: /* NSSR (optional reset) */
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "funeth: unknown MMIO write at 0x%"HWADDR_PRIx" val 0x%"PRIx64"\n", addr, val);
        break;
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static void pcibase_process_admin_sq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint8_t cmd_buf[ADMIN_SQE_SIZE];
    uint8_t cqe_buf[ADMIN_CQE_SIZE];
    dma_addr_t sq_addr, cq_addr;
    uint32_t head = s->dma.admin_sq_head;
    uint32_t tail = s->dma.admin_sq_tail;
    uint32_t sq_entries = s->dma.admin_sq_aqa + 1;
    uint32_t cq_entries = s->dma.admin_cq_aqa + 1;

    if (!sq_entries || !cq_entries) {
        return;
    }

    while (head != tail) {
        sq_addr = s->dma.admin_sq_dma + head * ADMIN_SQE_SIZE;
        pci_dma_read(pdev, sq_addr, cmd_buf, sizeof(cmd_buf));

        struct fun_admin_req_common *common = (struct fun_admin_req_common *)cmd_buf;
        uint8_t op = common->op;
        uint8_t len8 = common->len8;
        uint16_t cid = be16_to_cpu(common->cid);

        memset(cqe_buf, 0, sizeof(cqe_buf));
        struct fun_cqe_info *cqe_info = (struct fun_cqe_info *)cqe_buf;
        cqe_info->sqid = 0;
        cqe_info->cid = cpu_to_be16(cid);
        cqe_info->sf_p = cpu_to_be16(s->dma.admin_cq_phase ? 0x1 : 0);

        /* Default: advance head, set SQHD to new head */
        head = (head + 1) % sq_entries;
        cqe_info->sqhd = cpu_to_be16(head);

        switch (op) {
        case FUN_ADMIN_OP_PORT: {
            uint8_t subop = cmd_buf[sizeof(struct fun_admin_req_common)];
            switch (subop) {
            case FUN_ADMIN_SUBOP_CREATE: {
                fun_admin_port_create_rsp *rsp = (fun_admin_port_create_rsp *)(cqe_buf + FUN_CQE_INFO_SIZE);
                rsp->subop = subop;
                rsp->id = cpu_to_be32(cid);
                rsp->lport = 0;
                memset(rsp->rsvd0, 0, sizeof(rsp->rsvd0));
                memset(rsp->rsvd1, 0, sizeof(rsp->rsvd1));
                break;
            }
            case FUN_ADMIN_SUBOP_READ: {
                struct fun_admin_port_read_req *read_req = (struct fun_admin_port_read_req *)(cmd_buf + sizeof(struct fun_admin_req_common));
                int num_entries = (len8 * 8 - (sizeof(struct fun_admin_req_common) + sizeof(struct fun_admin_port_read_req))) / sizeof(struct fun_admin_read48_req);
                if (num_entries < 0) num_entries = 0;
                fun_admin_port_read_rsp_hdr *rsp_hdr = (fun_admin_port_read_rsp_hdr *)(cqe_buf + FUN_CQE_INFO_SIZE);
                rsp_hdr->subop = subop;
                rsp_hdr->id = cpu_to_be32(cid);
                memset(rsp_hdr->rsvd0, 0, sizeof(rsp_hdr->rsvd0));
                uint64_t *read48_rsp = (uint64_t *)(cqe_buf + FUN_CQE_INFO_SIZE + sizeof(fun_admin_port_read_rsp_hdr));
                for (int i = 0; i < num_entries; i++) {
                    uint64_t key_pack = be64_to_cpu(read_req->read48[i].key_pack);
                    uint16_t key = key_pack & 0xFFFF;
                    uint64_t data = 0;
                    if (key == FUN_ADMIN_PORT_KEY_MACADDR) {
                        data = ((uint64_t)s->mac[0] << 40) |
                               ((uint64_t)s->mac[1] << 32) |
                               ((uint64_t)s->mac[2] << 24) |
                               ((uint64_t)s->mac[3] << 16) |
                               ((uint64_t)s->mac[4] << 8)  |
                               (uint64_t)s->mac[5];
                    } else if (key == FUN_ADMIN_PORT_KEY_CAPABILITIES) {
                        data = FUN_PORT_CAP_VPORT;
                    } else if (key == FUN_ADMIN_PORT_KEY_MTU) {
                        data = 9024;
                    }
                    uint64_t rsp_val = (data & 0xFFFFFFFFFFFFULL) | (0ULL << 48);
                    read48_rsp[i] = cpu_to_be64(rsp_val);
                }
                break;
            }
            case FUN_ADMIN_SUBOP_WRITE:
                break;
            default:
                qemu_log_mask(LOG_UNIMP, "funeth: unknown port subop %u\n", subop);
                cqe_info->sqhd = cpu_to_be16(head); /* keep head as updated */
                break;
            }
            break;
        }
        case FUN_ADMIN_OP_ADI:
        case FUN_ADMIN_OP_ETH:
        case FUN_ADMIN_OP_VI:
        case FUN_ADMIN_OP_RSS:
            break;
        default:
            qemu_log_mask(LOG_UNIMP, "funeth: unknown admin op %u\n", op);
            break;
        }

        uint32_t cq_tail = s->dma.admin_cq_tail;
        cq_addr = s->dma.admin_cq_dma + cq_tail * ADMIN_CQE_SIZE;
        pci_dma_write(pdev, cq_addr, cqe_buf, sizeof(cqe_buf));
        s->dma.admin_cq_tail = (cq_tail + 1) % cq_entries;
        if (s->dma.admin_cq_tail == 0) {
            s->dma.admin_cq_phase = !s->dma.admin_cq_phase;
        }

        if (msix_enabled(pdev)) {
            msix_notify(pdev, 0);
        }
    }

    s->dma.admin_sq_head = head;
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    s->cap = 0x3FFULL;
    s->vs = 0x0100;
    s->cc = 0;
    s->csts = 0;
    s->dma.admin_cq_dma = 0;
    s->dma.admin_sq_dma = 0;
    s->dma.admin_sq_aqa = 0;
    s->dma.admin_cq_aqa = 0;
    s->dma.admin_sq_head = 0;
    s->dma.admin_sq_tail = 0;
    s->dma.admin_cq_head = 0;
    s->dma.admin_cq_tail = 0;
    s->dma.admin_cq_phase = false;  /* Start phase at 0 */
    memset(s->mac, 0, sizeof(s->mac));
    s->intr_status = 0;
    s->intr_mask = 0;
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    s->bar_info[0] = (BARInfo) {
        .index = 0,
        .type = BAR_TYPE_MMIO,
        .size = BAR0_SIZE,
        .name = "bar0"
    };
    s->num_bars = 1;
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    if (msix_init_exclusive_bar(pdev, 4, 2, errp)) {
        error_report("MSI-X initialization failed");
        return;
    }
    s->has_msix = true;

    s->cap = 0x3FFULL;
    s->vs = 0x0100;
    s->cc = 0;
    s->csts = 0;

    s->mac[0] = 0x00; s->mac[1] = 0x11; s->mac[2] = 0x22;
    s->mac[3] = 0x33; s->mac[4] = 0x44; s->mac[5] = 0x55;

    s->dma.admin_cq_phase = false;  /* Start phase at 0 */
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
    .name = "funeth_pci",
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
