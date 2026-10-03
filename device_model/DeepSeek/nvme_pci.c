/*
 * QEMU NVMe PCI device emulation for Linux driver probe
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

/* NVMe register offsets */
#define NVME_REG_CAP    0x00
#define NVME_REG_VS     0x08
#define NVME_REG_INTMS  0x0C
#define NVME_REG_INTMC  0x10
#define NVME_REG_CC     0x14
#define NVME_REG_CSTS   0x1C
#define NVME_REG_NSSR   0x20
#define NVME_REG_AQA    0x24
#define NVME_REG_ASQ    0x28
#define NVME_REG_ACQ    0x30
#define NVME_REG_CMBLOC 0x38
#define NVME_REG_CMBSZ  0x3C
#define NVME_REG_CMBMSC 0x50
#define NVME_REG_DBS    0x1000

#define NVME_CAP_MQES(cap)  ((cap) & 0xFFFF)
#define NVME_CAP_STRIDE(cap) (((cap) >> 32) & 0xF)

#define NVME_MAX_QUEUES     256
#define NVME_CMDSZ          64
#define NVME_CQESZ          16

/* Completion status codes */
#define NVME_SC_SUCCESS     0x0

/* Admin command opcodes */
#define NVME_ADMIN_DELETE_SQ    0x00
#define NVME_ADMIN_CREATE_SQ    0x01
#define NVME_ADMIN_DELETE_CQ    0x04
#define NVME_ADMIN_CREATE_CQ    0x05
#define NVME_ADMIN_IDENTIFY     0x06
#define NVME_ADMIN_SET_FEATURES 0x09
#define NVME_ADMIN_ASYNC_EVENT  0x0C

/* Feature identifiers */
#define NVME_FEAT_NUM_QUEUES    0x07
#define NVME_FEAT_ASYNC_EVENT   0x0B

/* Identify CNS values */
#define NVME_ID_CNS_NS          0x0
#define NVME_ID_CNS_CTRL        0x1
#define NVME_ID_CNS_NS_ACTIVE_LIST 0x2

#define TYPE_PCIBASE_DEVICE "nvme_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

typedef struct NvmeQueue {
    bool present;
    uint16_t sq_head;
    uint16_t sq_tail;
    uint16_t sq_size;       /* number of entries - 1 */
    uint64_t sq_addr;
    uint16_t cq_head;       /* driver writes new head here */
    uint16_t cq_tail;       /* next completion to post */
    uint16_t cq_size;       /* number of entries - 1 */
    uint64_t cq_addr;
    uint8_t  cq_phase;      /* phase bit */
    int      vector;        /* interrupt vector index */
} NvmeQueue;

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar0;

    /* NVMe registers */
    uint64_t cap;
    uint32_t vs;
    uint32_t cc;
    uint32_t csts;
    uint32_t aqa;
    uint64_t asq;
    uint64_t acq;
    uint32_t asq_lo;        /* temporary for low part of ASQ */
    uint32_t acq_lo;

    /* Queue management */
    NvmeQueue queues[NVME_MAX_QUEUES];
    int      num_vectors;   /* property */

    /* Identify data */
    uint8_t  id_ctrl[4096];
    uint8_t  id_ns[4096];
};

/* Utility to read/write register fields */
static void strpadcpy(char *dest, size_t n, const char *src, char pad) {
    size_t len = strlen(src);
    if (len >= n) {
        memcpy(dest, src, n);
    } else {
        memcpy(dest, src, len);
        memset(dest + len, pad, n - len);
    }
}

static void nvme_init_state(PCIBaseState *s)
{
    /* Set up CAP register */
    /* MQES=1023 (1024 entries), TO=0x05, DSTRD=2 (4 bytes), CSS=1 (NVM) */
    s->cap = 0x22050003FFULL;
    s->vs  = 0x10300;   /* version 1.3.0 */
    s->cc  = 0;
    s->csts = 0;
    s->aqa = 0;
    s->asq = 0;
    s->acq = 0;
    s->asq_lo = 0;
    s->acq_lo = 0;

    /* Initialize queues */
    memset(s->queues, 0, sizeof(s->queues));
    /* Mark admin queue as present (id 0) when enabled */

    /* Build static Identify Controller data */
    memset(s->id_ctrl, 0, sizeof(s->id_ctrl));
    /* PCI VID/DID: 0x8086:0x0953 */
    stw_le_p(&s->id_ctrl[0], 0x8086);
    stw_le_p(&s->id_ctrl[2], 0x0953);
    /* Serial Number (20 bytes) */
    strpadcpy((char *)&s->id_ctrl[4], 20, "QEMU000000000000", ' ');
    /* Model Number (40 bytes) */
    strpadcpy((char *)&s->id_ctrl[24], 40, "QEMU NVMe Controller", ' ');
    /* Firmware Revision (8 bytes) */
    strpadcpy((char *)&s->id_ctrl[64], 8, "1.0", ' ');
    /* MDTS (Maximum Data Transfer Size) */
    s->id_ctrl[77] = 0;   /* 2^0 * 4KB = 4KB, but spec says 0 means no limit? We'll set 0. */
    /* Controller ID (CNTLID) */
    stw_le_p(&s->id_ctrl[78], 1);
    /* Version */
    s->id_ctrl[80] = 1;
    s->id_ctrl[81] = 3;
    s->id_ctrl[82] = 0;
    /* OACS: Optional Admin Command Support */
    stw_le_p(&s->id_ctrl[256], 0); /* no doorbell buffer */
    /* ONCS: Optional NVM Command Support */
    stw_le_p(&s->id_ctrl[516], 0);
    /* MAXCMD: maximum outstanding commands */
    s->id_ctrl[520] = 1;
    /* NN: number of namespaces */
    stl_le_p(&s->id_ctrl[512], 1);
    /* FNA */
    s->id_ctrl[524] = 0;
    /* SGL Support */
    stl_le_p(&s->id_ctrl[536], 0);

    /* Build static Identify Namespace data */
    memset(s->id_ns, 0, sizeof(s->id_ns));
    /* NSZE: namespace size (32MB in sectors) */
    stq_le_p(&s->id_ns[0], 0x10000);
    /* NCAP: capacity */
    stq_le_p(&s->id_ns[8], 0x10000);
    /* NUSE */
    stq_le_p(&s->id_ns[16], 0x10000);
    /* FLBAS: LBA format index 0 */
    s->id_ns[26] = 0;
    /* LBA Format 0: LBA data size = 9 (512 bytes) */
    s->id_ns[128] = 9;
}

static void nvme_post_cq(PCIBaseState *s, int qid, uint32_t sq_head_val)
{
    NvmeQueue *q = &s->queues[qid];
    PCIDevice *pdev = PCI_DEVICE(s);
    uint8_t entry[NVME_CQESZ];
    memset(entry, 0, sizeof(entry));
    stl_le_p(&entry[0], 0); /* DWORD0: command-specific */
    stl_le_p(&entry[4], 0); /* DWORD1: reserved */
    stw_le_p(&entry[8], sq_head_val); /* SQ head pointer */
    stw_le_p(&entry[10], 0); /* SQ identifier (unused) */
    stw_le_p(&entry[12], 0); /* Command ID (unused) */
    /* Status field: phase bit in bit0, status code in bits 1-7 */
    uint16_t status = (q->cq_phase & 1) | (NVME_SC_SUCCESS << 1);
    stw_le_p(&entry[14], status);

    dma_addr_t cq_addr = q->cq_addr + (q->cq_tail * NVME_CQESZ);
    pci_dma_write(&s->parent_obj, cq_addr, entry, NVME_CQESZ);

    q->cq_tail = (q->cq_tail + 1) & q->cq_size;
    if (q->cq_tail == 0) {
        q->cq_phase ^= 1;
    }

    /* Raise interrupt */
    if (msix_enabled(pdev)) {
        msix_notify(pdev, q->vector);
    } else if (msi_enabled(pdev)) {
        msi_notify(pdev, q->vector);
    } else {
        pci_set_irq(pdev, 1);
    }
}

static void nvme_process_sq(PCIBaseState *s, int qid)
{
    NvmeQueue *q = &s->queues[qid];
    uint8_t cmd[NVME_CMDSZ];

    while (q->sq_head != q->sq_tail) {
        dma_addr_t sq_addr = q->sq_addr + (q->sq_head * NVME_CMDSZ);
        pci_dma_read(&s->parent_obj, sq_addr, cmd, NVME_CMDSZ);

        uint8_t opcode = cmd[0];
        uint32_t nsid = ldl_le_p(&cmd[4]);
        uint32_t cdw10 = ldl_le_p(&cmd[40]);

        if (qid == 0) {
            /* Admin queue commands */
            switch (opcode) {
            case NVME_ADMIN_IDENTIFY:
                {
                    uint8_t cns = cdw10 & 0xFF;
                    if (cns == NVME_ID_CNS_CTRL) {
                        /* Identify Controller */
                        uint64_t prp1 = ldq_le_p(&cmd[24]);
                        pci_dma_write(&s->parent_obj, prp1, s->id_ctrl, 4096);
                    } else if (cns == NVME_ID_CNS_NS) {
                        /* Identify Namespace */
                        if (nsid == 1) {
                            uint64_t prp1 = ldq_le_p(&cmd[24]);
                            pci_dma_write(&s->parent_obj, prp1, s->id_ns, 4096);
                        }
                    } else if (cns == NVME_ID_CNS_NS_ACTIVE_LIST) {
                        /* Namespace list (CNS=2) */
                        uint64_t prp1 = ldq_le_p(&cmd[24]);
                        uint32_t list[1024];
                        memset(list, 0, sizeof(list));
                        list[0] = cpu_to_le32(1); /* only nsid 1 */
                        pci_dma_write(&s->parent_obj, prp1, (uint8_t*)list, 4096);
                    }
                }
                break;
            case NVME_ADMIN_SET_FEATURES:
                {
                    uint8_t fid = cdw10 & 0xFF;
                    if (fid == NVME_FEAT_NUM_QUEUES || fid == NVME_FEAT_ASYNC_EVENT) {
                        /* Accept and ignore */
                    }
                }
                break;
            case NVME_ADMIN_CREATE_CQ:
                {
                    uint16_t cqid = lduw_le_p(&cmd[10]);
                    uint16_t qsize = lduw_le_p(&cmd[14]);
                    uint16_t irq_vector = lduw_le_p(&cmd[18]);
                    uint64_t prp1 = ldq_le_p(&cmd[24]);
                    if (cqid < NVME_MAX_QUEUES) {
                        NvmeQueue *newq = &s->queues[cqid];
                        newq->present = true;
                        newq->cq_size = qsize;
                        newq->cq_addr = prp1;
                        newq->cq_head = 0;
                        newq->cq_tail = 0;
                        newq->cq_phase = 1;
                        newq->vector = irq_vector;
                    }
                }
                break;
            case NVME_ADMIN_CREATE_SQ:
                {
                    uint16_t sqid = lduw_le_p(&cmd[10]);
                    uint16_t cqid = lduw_le_p(&cmd[12]);
                    uint16_t qsize = lduw_le_p(&cmd[14]);
                    uint64_t prp1 = ldq_le_p(&cmd[24]);
                    if (sqid < NVME_MAX_QUEUES) {
                        NvmeQueue *newq = &s->queues[sqid];
                        newq->present = true;
                        newq->sq_size = qsize;
                        newq->sq_addr = prp1;
                        newq->sq_head = 0;
                        newq->sq_tail = 0;
                        /* vector is inherited from associated CQ? */
                        /* For simplicity, assume cqid already exists */
                        if (cqid < NVME_MAX_QUEUES && s->queues[cqid].present) {
                            newq->vector = s->queues[cqid].vector;
                        }
                    }
                }
                break;
            case NVME_ADMIN_DELETE_SQ:
            case NVME_ADMIN_DELETE_CQ:
                /* Mark queue as not present, but not needed */
                break;
            case NVME_ADMIN_ASYNC_EVENT:
                /* Ignore */
                break;
            default:
                break;
            }
        } else {
            /* I/O queue commands not processed during probe */
        }

        /* Post completion */
        nvme_post_cq(s, qid, q->sq_head);
        q->sq_head = (q->sq_head + 1) & q->sq_size;
    }
}

static void nvme_ctrl_enable(PCIBaseState *s)
{
    s->csts |= 1; /* RDY */
    /* Mark admin queue as present if ASQ/ACQ are set */
    if (s->asq && s->acq) {
        NvmeQueue *adminq = &s->queues[0];
        adminq->present = true;
        adminq->sq_size = (s->aqa >> 16) & 0xFFFF;
        adminq->cq_size = s->aqa & 0xFFFF;
        adminq->sq_addr = s->asq;
        adminq->cq_addr = s->acq;
        adminq->sq_head = 0;
        adminq->sq_tail = 0;
        adminq->cq_head = 0;
        adminq->cq_tail = 0;
        adminq->cq_phase = 1;
        adminq->vector = 0;
    }
}

static uint64_t nvme_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr < NVME_REG_DBS) {
        switch (addr) {
        case NVME_REG_CAP:
            if (size == 8) {
                val = s->cap;
            } else if (size == 4) {
                val = (addr == 0) ? (uint32_t)s->cap : (uint32_t)(s->cap >> 32);
            }
            break;
        case NVME_REG_VS:
            val = s->vs;
            break;
        case NVME_REG_INTMS:
        case NVME_REG_INTMC:
            val = 0;
            break;
        case NVME_REG_CC:
            val = s->cc;
            break;
        case NVME_REG_CSTS:
            val = s->csts;
            break;
        case NVME_REG_NSSR:
            val = 0;
            break;
        case NVME_REG_AQA:
            val = s->aqa;
            break;
        case NVME_REG_ASQ:
            if (size == 8) {
                val = s->asq;
            } else if (size == 4) {
                val = (addr == NVME_REG_ASQ) ? (uint32_t)s->asq : (uint32_t)(s->asq >> 32);
            }
            break;
        case NVME_REG_ACQ:
            if (size == 8) {
                val = s->acq;
            } else if (size == 4) {
                val = (addr == NVME_REG_ACQ) ? (uint32_t)s->acq : (uint32_t)(s->acq >> 32);
            }
            break;
        case NVME_REG_CMBLOC:
        case NVME_REG_CMBSZ:
        case NVME_REG_CMBMSC:
            val = 0;
            break;
        default:
            qemu_log_mask(LOG_UNIMP, "nvme: unimplemented MMIO read 0x%"HWADDR_PRIx"\n", addr);
            break;
        }
    } else {
        /* Doorbell region: reads return 0 */
        val = 0;
    }
    return val;
}

static void nvme_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < NVME_REG_DBS) {
        switch (addr) {
        case NVME_REG_CC:
            s->cc = val;
            if (val & 1) {
                nvme_ctrl_enable(s);
            } else {
                s->csts &= ~1; /* clear RDY */
            }
            break;
        case NVME_REG_AQA:
            s->aqa = val;
            break;
        case NVME_REG_ASQ:
            if (size == 8) {
                s->asq = val;
            } else if (size == 4) {
                if (addr == NVME_REG_ASQ) {
                    s->asq_lo = val;
                    s->asq = (s->asq & 0xFFFFFFFF00000000ULL) | val;
                } else { /* addr == NVME_REG_ASQ + 4 */
                    s->asq = (s->asq & 0xFFFFFFFFULL) | ((uint64_t)val << 32);
                }
            }
            break;
        case NVME_REG_ACQ:
            if (size == 8) {
                s->acq = val;
            } else if (size == 4) {
                if (addr == NVME_REG_ACQ) {
                    s->acq_lo = val;
                    s->acq = (s->acq & 0xFFFFFFFF00000000ULL) | val;
                } else { /* addr == NVME_REG_ACQ + 4 */
                    s->acq = (s->acq & 0xFFFFFFFFULL) | ((uint64_t)val << 32);
                }
            }
            break;
        case NVME_REG_NSSR:
            /* Subsystem reset: emulate reset */
            nvme_init_state(s);
            break;
        default:
            qemu_log_mask(LOG_UNIMP, "nvme: unimplemented MMIO write 0x%"HWADDR_PRIx" val 0x%"PRIx64"\n", addr, val);
            break;
        }
    } else {
        /* Doorbell write */
        if (size == 4) {
            uint32_t offset = addr - NVME_REG_DBS;
            uint32_t db_index = offset / 4; /* each doorbell is 4 bytes, stride 4 */
            if (db_index % 2 == 0) {
                /* SQ doorbell */
                int qid = db_index / 2;
                if (qid < NVME_MAX_QUEUES && s->queues[qid].present) {
                    uint16_t new_tail = val & 0xFFFF;
                    s->queues[qid].sq_tail = new_tail;
                    nvme_process_sq(s, qid);
                }
            } else {
                /* CQ doorbell */
                int qid = (db_index - 1) / 2;
                if (qid < NVME_MAX_QUEUES && s->queues[qid].present) {
                    uint16_t new_head = val & 0xFFFF;
                    s->queues[qid].cq_head = new_head;
                }
            }
        }
    }
}

static const MemoryRegionOps nvme_mmio_ops = {
    .read = nvme_mmio_read,
    .write = nvme_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
};

static void nvme_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    s->num_vectors = 2; /* admin + one I/O */

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID, 0x8086);
    pci_set_word(pci_conf + PCI_DEVICE_ID, 0x0953);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0108);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);

    /* BAR0: MMIO, 64KB */
    memory_region_init_io(&s->bar0, OBJECT(s), &nvme_mmio_ops, s,
                          "nvme-bar0", 0x10000);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar0);

    /* MSI-X support with 2 vectors */
    if (msix_init(pdev, s->num_vectors, &s->bar0, 0, 0x2000, NULL, 0, 0, 0, NULL) < 0) {
        return;
    }

    /* Initialize NVMe state */
    nvme_init_state(s);
}

static void nvme_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, &s->bar0, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

static void nvme_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    nvme_init_state(s);
}

static const VMStateDescription vmstate_nvme = {
    .name = "nvme_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_END_OF_LIST()
    }
};

static void nvme_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = nvme_realize;
    k->exit    = nvme_uninit;
    dc->reset  = nvme_reset;
    dc->vmsd   = &vmstate_nvme;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void nvme_register_types(void)
{
    static InterfaceInfo interfaces[] = {
        { INTERFACE_PCIE_DEVICE },
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    };

    static const TypeInfo nvme_info = {
        .name = TYPE_PCIBASE_DEVICE,
        .parent = TYPE_PCI_DEVICE,
        .instance_size = sizeof(PCIBaseState),
        .class_init = nvme_class_init,
        .interfaces = interfaces,
    };

    type_register_static(&nvme_info);
}

type_init(nvme_register_types);