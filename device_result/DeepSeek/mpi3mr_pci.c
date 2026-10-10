/*
 * QEMU PCI device model for MPI3MR controller (mpi3mr driver)
 * Generated Phase 2: Functional Behavior (Iteration 2)
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

#define TYPE_PCIBASE_DEVICE "mpi3mr_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI Vendor/Device IDs from driver's pci_device_id table, first entry */
#define MPI3_MFGPAGE_VENDORID_BROADCOM  0x1000
#define MPI3_MFGPAGE_DEVID_SAS4116      0x00a5

#define PCI_VENDOR_ID_MPI3MR    MPI3_MFGPAGE_VENDORID_BROADCOM
#define PCI_DEVICE_ID_MPI3MR    MPI3_MFGPAGE_DEVID_SAS4116
#define PCI_CLASS_ID_MPI3MR     0x0107  /* SCSI storage controller (base class 01h, sub-class 07h) */

/* Register Offsets from struct mpi3_sysif_registers */
#define MPI3_SYSIF_REG_IOC_INFORMATION          0x00
#define MPI3_SYSIF_REG_VERSION                  0x08
#define MPI3_SYSIF_REG_IOC_CONFIGURATION        0x10
#define MPI3_SYSIF_REG_IOC_STATUS               0x18
#define MPI3_SYSIF_REG_ADMIN_Q_NUM_ENTRIES      0x20
#define MPI3_SYSIF_REG_ADMIN_REQUEST_Q_ADDR     0x24
#define MPI3_SYSIF_REG_ADMIN_REPLY_Q_ADDR       0x28
#define MPI3_SYSIF_REG_COALESCE_CONTROL         0x40
#define MPI3_SYSIF_REG_ADMIN_REQUEST_Q_PI       0x1000
#define MPI3_SYSIF_REG_ADMIN_REPLY_Q_CI         0x1004
#define MPI3_SYSIF_REG_OPER_Q_INDEXES_BASE      0x1008 /* oper_queue_indexes[0] */
#define MPI3_SYSIF_REG_WRITE_SEQUENCE           0x1C04
#define MPI3_SYSIF_REG_HOST_DIAGNOSTIC          0x1C08
#define MPI3_SYSIF_REG_FAULT                    0x1C10
#define MPI3_SYSIF_REG_FAULT_INFO0              0x1C14
#define MPI3_SYSIF_REG_FAULT_INFO1              0x1C18
#define MPI3_SYSIF_REG_FAULT_INFO2              0x1C1C
#define MPI3_SYSIF_REG_REPLY_FREE_HOST_INDEX    0x1C40
#define MPI3_SYSIF_REG_SENSE_BUF_FREE_HOST_INDEX 0x1C44
#define MPI3_SYSIF_REG_DIAG_RW_DATA             0x1C80
#define MPI3_SYSIF_REG_DIAG_RW_ADDRESS          0x1C88
#define MPI3_SYSIF_REG_DIAG_RW_CONTROL          0x1C90
#define MPI3_SYSIF_REG_DIAG_RW_STATUS           0x1C92
#define MPI3_SYSIF_REG_SCRATCHPAD_BASE          0x1D00
#define MPI3_SYSIF_REG_DEVICE_ASSIGNED_BASE     0x2000

/* BAR size: inferred from driver usage; increased to accommodate MSI-X */
#define MPI3MR_BAR0_SIZE                        0x10000

/* Capability flags */
#define MPI3_SYSIF_IOC_STATUS_READY             0x00000001
#define MPI3_SYSIF_IOC_STATUS_FAULT             0x00000002
#define MPI3_SYSIF_IOC_STATUS_SHUTDOWN_MASK     0x0000000c
#define MPI3_SYSIF_IOC_STATUS_RESET_HISTORY     0x00000010
#define MPI3_SYSIF_IOC_CONFIG_ENABLE_IOC        0x00000001

/* Write sequence key values */
#define MPI3_SYSIF_WRITE_SEQUENCE_KEY_VALUE_1ST  0xf
#define MPI3_SYSIF_WRITE_SEQUENCE_KEY_VALUE_2ND  0x4
#define MPI3_SYSIF_WRITE_SEQUENCE_KEY_VALUE_3RD  0xb
#define MPI3_SYSIF_WRITE_SEQUENCE_KEY_VALUE_4TH  0x2
#define MPI3_SYSIF_WRITE_SEQUENCE_KEY_VALUE_5TH  0x7
#define MPI3_SYSIF_WRITE_SEQUENCE_KEY_VALUE_6TH  0xd
#define MPI3_SYSIF_WRITE_SEQUENCE_KEY_VALUE_FLUSH 0x0

/* Interrupt definitions */
#define MPI3MR_IRQ_ADMIN_REPLY         0x00000001

/* MSI-X settings */
#define MPI3MR_MSIX_VECTORS             16

/* Admin queue frame sizes from driver */
#define MPI3MR_ADMIN_REQ_FRAME_SZ       128
#define MPI3MR_ADMIN_REPLY_FRAME_SZ     16

/* --------------------------------------------------------------
 * Admin request structures (from mpi3_ioc_facts_request, etc.)
 * Packed to match hardware layout.
 * --------------------------------------------------------------*/
typedef struct QEMU_PACKED MPI3AdminRequestCommon {
    uint16_t host_tag;       /* offset 0: le16 */
    uint8_t  ioc_use_only02; /* offset 2 */
    uint8_t  function;       /* offset 3 */
    uint16_t ioc_use_only04; /* offset 4 */
    uint8_t  ioc_use_only06; /* offset 6 */
    uint8_t  msg_flags;      /* offset 7 */
} MPI3AdminRequestCommon;

typedef struct QEMU_PACKED MPI3IOCFactsRequest {
    MPI3AdminRequestCommon common;
    uint16_t change_count;   /* offset 8 */
    uint16_t reserved0a;     /* offset 10 */
    uint32_t reserved0c;     /* offset 12 */
    /* union mpi3_sge_union sgl; */ /* placeholder: need definition */
} MPI3IOCFactsRequest;

typedef struct QEMU_PACKED MPI3PortEnableRequest {
    MPI3AdminRequestCommon common;
    uint16_t change_count;   /* offset 8 */
    uint16_t reserved0a;     /* offset 10 */
} MPI3PortEnableRequest;
/* End of admin request structures */

/* --------------------------------------------------------------
 * IOC Facts Data structure (from driver mpi3_ioc_facts_data)
 * Used when emulating IOC Facts command.
 * --------------------------------------------------------------*/
typedef struct QEMU_PACKED MPI3IOCFactsData {
    uint16_t ioc_facts_data_length;  /* offset 0: le16 */
    uint16_t reserved02;             /* offset 2 */
    /* union mpi3_version_union mpi_version; */ /* placeholder: need definition */
    /* struct mpi3_comp_image_version fw_version; */ /* placeholder: need definition */
    uint32_t ioc_capabilities;       /* offset 8?: le32, precise offset depends on previous */
    uint8_t  ioc_number;            /* offset 12 */
    uint8_t  who_init;              /* offset 13 */
    uint16_t max_msix_vectors;       /* offset 14: le16 */
    uint16_t max_outstanding_requests; /* offset 16: le16 */
    uint16_t product_id;            /* offset 18: le16 */
    uint16_t ioc_request_frame_size; /* offset 20: le16 */
    uint16_t reply_frame_size;      /* offset 22: le16 */
    uint16_t ioc_exceptions;        /* offset 24: le16 */
    uint16_t max_persistent_id;     /* offset 26: le16 */
    uint8_t  sge_modifier_mask;     /* offset 28 */
    uint8_t  sge_modifier_value;    /* offset 29 */
    uint8_t  sge_modifier_shift;    /* offset 30 */
    uint8_t  protocol_flags;        /* offset 31 */
    uint16_t max_sas_initiators;    /* offset 32: le16 */
    uint16_t max_data_length;       /* offset 34: le16 */
    uint16_t max_sas_expanders;     /* offset 36: le16 */
    uint16_t max_enclosures;        /* offset 38: le16 */
    uint16_t min_dev_handle;        /* offset 40: le16 */
    uint16_t max_dev_handle;        /* offset 42: le16 */
    uint16_t max_pcie_switches;     /* offset 44: le16 */
    uint16_t max_nvme;              /* offset 46: le16 */
    uint16_t reserved38;            /* offset 48: le16 */
    uint16_t max_vds;               /* offset 50: le16 */
    uint16_t max_host_pds;          /* offset 52: le16 */
    uint16_t max_adv_host_pds;      /* offset 54: le16 */
    uint16_t max_raid_pds;          /* offset 56: le16 */
    uint16_t max_posted_cmd_buffers;/* offset 58: le16 */
    uint32_t flags;                 /* offset 60: le32 */
    uint16_t max_operational_request_queues; /* offset 64: le16 */
    uint16_t max_operational_reply_queues;   /* offset 66: le16 */
    uint16_t shutdown_timeout;      /* offset 68: le16 */
    uint16_t reserved4e;            /* offset 70: le16 */
    uint32_t diag_trace_size;       /* offset 72: le32 */
    uint32_t diag_fw_size;          /* offset 76: le32 */
    uint32_t diag_driver_size;      /* offset 80: le32 */
    uint8_t  max_host_pd_ns_count;  /* offset 84 */
    uint8_t  max_adv_host_pd_ns_count; /* offset 85 */
    uint8_t  max_raidpd_ns_count;   /* offset 86 */
    uint8_t  max_devices_per_throttle_group; /* offset 87 */
    uint16_t io_throttle_data_length; /* offset 88: le16 */
    uint16_t max_io_throttle_group; /* offset 90: le16 */
    uint16_t io_throttle_low;       /* offset 92: le16 */
    uint16_t io_throttle_high;      /* offset 94: le16 */
    uint32_t diag_fdl_size;         /* offset 96: le32 */
    uint32_t diag_tty_size;         /* offset 100: le32 */
} MPI3IOCFactsData;

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

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;
    MemoryRegion mmio;          /* Register region within BAR0 */
    MemoryRegion mmio_container; /* Container for BAR0 */

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;
    uint32_t intr_mask;
    uint32_t intr_status;
    uint16_t msix_count;
    bool msi_enabled;

    /* Hardware Register Shadows */
    uint8_t regs[MPI3MR_BAR0_SIZE];  /* Sparse mirror, only used for some registers */
    uint32_t reg32[MPI3MR_BAR0_SIZE / 4]; /* More efficient 32-bit access */

    /* DMA Context */
    struct {
        dma_addr_t admin_req_base;
        dma_addr_t admin_reply_base;
        dma_addr_t reply_free_q_base;
        dma_addr_t sense_buf_q_base;
        uint32_t admin_req_q_sz;
        uint32_t admin_reply_q_sz;
        uint16_t reply_free_q_depth;
        uint16_t sense_buf_free_q_depth;
    } dma;

    /* Operational status flags */
    uint32_t ioc_status;
    uint32_t ioc_config;

    /* Write sequence state machine */
    uint8_t write_seq_state;  /* 0 = idle, 1-6 = expecting key n */

    /* Probe/Reset state */
    bool reset_in_progress;
    bool unrecoverable;
    int reset_reason;

    /* Power management state (D0-D3) */
    uint8_t pm_state;
};

/* Internal helper for status-triggered signaling */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->intr_status & s->intr_mask) {
        if (msix_enabled(pdev)) {
            msix_notify(pdev, 0);
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

/* Post a reply entry to the admin reply queue and signal interrupt.
 * NOTE: This uses a guessed reply layout. The actual layout requires
 * struct mpi3_admin_reply from the driver to be correct. */
static void pcibase_admin_reply_post(PCIBaseState *s, uint8_t function,
                                     uint16_t host_tag, uint8_t status)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    dma_addr_t reply_addr = s->dma.admin_reply_base +
        (s->reg32[MPI3_SYSIF_REG_ADMIN_REPLY_Q_CI / 4] % s->dma.admin_reply_q_sz)
        * MPI3MR_ADMIN_REPLY_FRAME_SZ;
    uint8_t reply_buf[MPI3MR_ADMIN_REPLY_FRAME_SZ];

    memset(reply_buf, 0, sizeof(reply_buf));
    /* Temporary: use generic offsets; will be replaced when reply struct known */
    stw_le_p(reply_buf + 0, host_tag);   /* assumed host_tag offset */
    reply_buf[2] = 0;                    /* reserved */
    reply_buf[3] = function;             /* function echoed */
    reply_buf[4] = status;               /* status byte */
    /* Additional fields zeroed */

    pci_dma_write(pdev, reply_addr, reply_buf, MPI3MR_ADMIN_REPLY_FRAME_SZ);

    /* Advance CI */
    uint32_t ci = s->reg32[MPI3_SYSIF_REG_ADMIN_REPLY_Q_CI / 4];
    s->reg32[MPI3_SYSIF_REG_ADMIN_REPLY_Q_CI / 4] = ci + 1;

    /* Signal interrupt */
    s->intr_status |= MPI3MR_IRQ_ADMIN_REPLY;
    pcibase_update_irq(s);
}

/* Device-initiated DMA logic based on driver access patterns */
static void pcibase_process_admin_command(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    dma_addr_t req_addr;
    uint32_t pi, ci;
    uint8_t req_buf[MPI3MR_ADMIN_REQ_FRAME_SZ];

    /* Read current PI and CI */
    pi = s->reg32[MPI3_SYSIF_REG_ADMIN_REQUEST_Q_PI / 4];
    ci = s->reg32[MPI3_SYSIF_REG_ADMIN_REPLY_Q_CI / 4];

    if (pi == ci) {
        return; /* No new command */
    }

    /* Compute request buffer address: base + (ci % queue_size) * frame_size */
    req_addr = s->dma.admin_req_base + (ci % s->dma.admin_req_q_sz) * MPI3MR_ADMIN_REQ_FRAME_SZ;
    pci_dma_read(pdev, req_addr, req_buf, MPI3MR_ADMIN_REQ_FRAME_SZ);

    /* Parse common header using driver-defined structure */
    const MPI3AdminRequestCommon *req = (const MPI3AdminRequestCommon *)req_buf;
    uint8_t function = req->function;
    uint16_t host_tag = le16_to_cpu(req->host_tag);

    /* Emulate basic admin commands based on driver functions */
    switch (function) {
    case 0x01: /* MPI3_FUNCTION_IOC_FACTS */
        /* The request contains an SGL describing a buffer for the facts data.
         * We need union mpi3_sge_union and struct mpi3_ioc_facts_data to
         * properly DMA the facts payload. For now, post success with empty facts. */
        pcibase_admin_reply_post(s, function, host_tag, 0x00);
        break;
    case 0x02: /* MPI3_FUNCTION_IOC_INIT */
        pcibase_admin_reply_post(s, function, host_tag, 0x00);
        break;
    case 0x03: /* MPI3_FUNCTION_PORT_ENABLE */
        pcibase_admin_reply_post(s, function, host_tag, 0x00);
        break;
    case 0x08: /* MPI3_FUNCTION_IO_UNIT_CONTROL */
        pcibase_admin_reply_post(s, function, host_tag, 0x00);
        break;
    default:
        printf("mpi3mr: unsupported admin command function 0x%x\n", function);
        pcibase_admin_reply_post(s, function, host_tag, 0xFF); /* Fail */
        break;
    }
}

/* MMIO Read Handler */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size > 4) {
        return 0;
    }

    switch (addr & ~0x3) {
    case MPI3_SYSIF_REG_IOC_INFORMATION:
        val = 0x00000000;
        break;
    case MPI3_SYSIF_REG_VERSION:
        val = 0x00000100;
        break;
    case MPI3_SYSIF_REG_IOC_CONFIGURATION:
        val = s->ioc_config;
        break;
    case MPI3_SYSIF_REG_IOC_STATUS:
        val = s->ioc_status;
        break;
    case MPI3_SYSIF_REG_ADMIN_Q_NUM_ENTRIES:
        val = s->dma.admin_req_q_sz;
        break;
    case MPI3_SYSIF_REG_ADMIN_REQUEST_Q_ADDR:
        val = (uint32_t)(s->dma.admin_req_base & 0xFFFFFFFF);
        break;
    case MPI3_SYSIF_REG_ADMIN_REPLY_Q_ADDR:
        val = (uint32_t)(s->dma.admin_reply_base & 0xFFFFFFFF);
        break;
    case MPI3_SYSIF_REG_COALESCE_CONTROL:
        val = 0;
        break;
    case MPI3_SYSIF_REG_ADMIN_REQUEST_Q_PI:
        val = s->reg32[MPI3_SYSIF_REG_ADMIN_REQUEST_Q_PI / 4];
        break;
    case MPI3_SYSIF_REG_ADMIN_REPLY_Q_CI:
        val = s->reg32[MPI3_SYSIF_REG_ADMIN_REPLY_Q_CI / 4];
        break;
    case MPI3_SYSIF_REG_OPER_Q_INDEXES_BASE:
        val = 0;
        break;
    case MPI3_SYSIF_REG_WRITE_SEQUENCE:
        val = 0;
        break;
    case MPI3_SYSIF_REG_HOST_DIAGNOSTIC:
        val = s->reg32[MPI3_SYSIF_REG_HOST_DIAGNOSTIC / 4];
        break;
    case MPI3_SYSIF_REG_FAULT:
        val = s->reg32[MPI3_SYSIF_REG_FAULT / 4];
        break;
    case MPI3_SYSIF_REG_FAULT_INFO0:
        val = s->reg32[MPI3_SYSIF_REG_FAULT_INFO0 / 4];
        break;
    case MPI3_SYSIF_REG_FAULT_INFO1:
        val = s->reg32[MPI3_SYSIF_REG_FAULT_INFO1 / 4];
        break;
    case MPI3_SYSIF_REG_FAULT_INFO2:
        val = s->reg32[MPI3_SYSIF_REG_FAULT_INFO2 / 4];
        break;
    case MPI3_SYSIF_REG_REPLY_FREE_HOST_INDEX:
        val = s->reg32[MPI3_SYSIF_REG_REPLY_FREE_HOST_INDEX / 4];
        break;
    case MPI3_SYSIF_REG_SENSE_BUF_FREE_HOST_INDEX:
        val = s->reg32[MPI3_SYSIF_REG_SENSE_BUF_FREE_HOST_INDEX / 4];
        break;
    case MPI3_SYSIF_REG_DIAG_RW_DATA:
        val = s->reg32[MPI3_SYSIF_REG_DIAG_RW_DATA / 4];
        break;
    case MPI3_SYSIF_REG_DIAG_RW_ADDRESS:
        val = s->reg32[MPI3_SYSIF_REG_DIAG_RW_ADDRESS / 4];
        break;
    case MPI3_SYSIF_REG_DIAG_RW_CONTROL:
        val = s->reg32[MPI3_SYSIF_REG_DIAG_RW_CONTROL / 4];
        break;
    case MPI3_SYSIF_REG_DIAG_RW_STATUS:
        val = s->reg32[MPI3_SYSIF_REG_DIAG_RW_STATUS / 4];
        break;
    case MPI3_SYSIF_REG_SCRATCHPAD_BASE:
        val = s->reg32[MPI3_SYSIF_REG_SCRATCHPAD_BASE / 4];
        break;
    case MPI3_SYSIF_REG_DEVICE_ASSIGNED_BASE:
        val = s->reg32[MPI3_SYSIF_REG_DEVICE_ASSIGNED_BASE / 4];
        break;
    default:
        val = 0;
        break;
    }
    return val;
}

/* MMIO Write Handler */
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size > 4) {
        return;
    }

    switch (addr & ~0x3) {
    case MPI3_SYSIF_REG_IOC_CONFIGURATION:
        s->ioc_config = val;
        break;
    case MPI3_SYSIF_REG_IOC_STATUS:
        s->ioc_status &= ~(val & MPI3_SYSIF_IOC_STATUS_FAULT);
        break;
    case MPI3_SYSIF_REG_ADMIN_Q_NUM_ENTRIES:
        s->dma.admin_req_q_sz = val;
        s->dma.admin_reply_q_sz = val;
        break;
    case MPI3_SYSIF_REG_ADMIN_REQUEST_Q_ADDR:
        s->dma.admin_req_base = (uint64_t)val;
        break;
    case MPI3_SYSIF_REG_ADMIN_REPLY_Q_ADDR:
        s->dma.admin_reply_base = (uint64_t)val;
        break;
    case MPI3_SYSIF_REG_COALESCE_CONTROL:
        break;
    case MPI3_SYSIF_REG_ADMIN_REQUEST_Q_PI:
        s->reg32[MPI3_SYSIF_REG_ADMIN_REQUEST_Q_PI / 4] = val;
        pcibase_process_admin_command(s);
        break;
    case MPI3_SYSIF_REG_ADMIN_REPLY_Q_CI:
        s->reg32[MPI3_SYSIF_REG_ADMIN_REPLY_Q_CI / 4] = val;
        break;
    case MPI3_SYSIF_REG_WRITE_SEQUENCE:
        {
            static const uint32_t sequence_keys[] = {
                MPI3_SYSIF_WRITE_SEQUENCE_KEY_VALUE_1ST,
                MPI3_SYSIF_WRITE_SEQUENCE_KEY_VALUE_2ND,
                MPI3_SYSIF_WRITE_SEQUENCE_KEY_VALUE_3RD,
                MPI3_SYSIF_WRITE_SEQUENCE_KEY_VALUE_4TH,
                MPI3_SYSIF_WRITE_SEQUENCE_KEY_VALUE_5TH,
                MPI3_SYSIF_WRITE_SEQUENCE_KEY_VALUE_6TH,
                MPI3_SYSIF_WRITE_SEQUENCE_KEY_VALUE_FLUSH
            };
            if (s->write_seq_state < sizeof(sequence_keys)/sizeof(sequence_keys[0])) {
                if (val == sequence_keys[s->write_seq_state]) {
                    s->write_seq_state++;
                    if (s->write_seq_state == sizeof(sequence_keys)/sizeof(sequence_keys[0])) {
                        s->ioc_status = MPI3_SYSIF_IOC_STATUS_RESET_HISTORY;
                        s->ioc_status |= MPI3_SYSIF_IOC_STATUS_READY;
                        s->write_seq_state = 0;
                    }
                } else {
                    s->write_seq_state = 0;
                }
            } else {
                s->write_seq_state = 0;
            }
        }
        break;
    case MPI3_SYSIF_REG_HOST_DIAGNOSTIC:
        s->reg32[MPI3_SYSIF_REG_HOST_DIAGNOSTIC / 4] = val;
        break;
    case MPI3_SYSIF_REG_FAULT:
        s->reg32[MPI3_SYSIF_REG_FAULT / 4] = val;
        break;
    case MPI3_SYSIF_REG_FAULT_INFO0:
    case MPI3_SYSIF_REG_FAULT_INFO1:
    case MPI3_SYSIF_REG_FAULT_INFO2:
        s->reg32[addr/4] = val;
        break;
    case MPI3_SYSIF_REG_REPLY_FREE_HOST_INDEX:
        s->reg32[MPI3_SYSIF_REG_REPLY_FREE_HOST_INDEX / 4] = val;
        break;
    case MPI3_SYSIF_REG_SENSE_BUF_FREE_HOST_INDEX:
        s->reg32[MPI3_SYSIF_REG_SENSE_BUF_FREE_HOST_INDEX / 4] = val;
        break;
    case MPI3_SYSIF_REG_DIAG_RW_DATA:
        s->reg32[MPI3_SYSIF_REG_DIAG_RW_DATA / 4] = val;
        break;
    case MPI3_SYSIF_REG_DIAG_RW_ADDRESS:
        s->reg32[MPI3_SYSIF_REG_DIAG_RW_ADDRESS / 4] = val;
        break;
    case MPI3_SYSIF_REG_DIAG_RW_CONTROL:
        s->reg32[MPI3_SYSIF_REG_DIAG_RW_CONTROL / 4] = val;
        break;
    case MPI3_SYSIF_REG_DIAG_RW_STATUS:
        s->reg32[MPI3_SYSIF_REG_DIAG_RW_STATUS / 4] = val;
        break;
    case MPI3_SYSIF_REG_SCRATCHPAD_BASE:
        s->reg32[MPI3_SYSIF_REG_SCRATCHPAD_BASE / 4] = val;
        break;
    case MPI3_SYSIF_REG_DEVICE_ASSIGNED_BASE:
        s->reg32[MPI3_SYSIF_REG_DEVICE_ASSIGNED_BASE / 4] = val;
        break;
    case MPI3_SYSIF_REG_OPER_Q_INDEXES_BASE:
        break;
    default:
        s->reg32[addr/4] = val;
        break;
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    memset(s->regs, 0, sizeof(s->regs));
    memset(s->reg32, 0, sizeof(s->reg32));
    s->ioc_status = 0;
    s->ioc_config = 0;
    s->intr_status = 0;
    s->intr_mask = 0;
    s->write_seq_state = 0;
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;
    Error *local_err = NULL;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_MPI3MR);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_MPI3MR);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_ID_MPI3MR);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    memory_region_init(&s->mmio_container, OBJECT(s), "mpi3mr-bar0", MPI3MR_BAR0_SIZE);
    memory_region_init_io(&s->mmio, OBJECT(s), &pcibase_mmio_ops, s, "mpi3mr-regs", 0x2000);
    memory_region_add_subregion(&s->mmio_container, 0, &s->mmio);

    s->msix_count = MPI3MR_MSIX_VECTORS;
    if (msix_init(pdev, s->msix_count,
                  &s->mmio_container, 0, 0x2000,
                  &s->mmio_container, 0, 0x2200,
                  0, &local_err)) {
        error_propagate(errp, local_err);
        return;
    }
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mmio_container);

    s->num_bars = 0;

    s->dma.admin_req_base = 0;
    s->dma.admin_reply_base = 0;
    s->dma.admin_req_q_sz = 256;
    s->dma.admin_reply_q_sz = 256;

    s->ioc_status = 0;
    s->write_seq_state = 0;
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
    .name = "mpi3mr_pci",
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
