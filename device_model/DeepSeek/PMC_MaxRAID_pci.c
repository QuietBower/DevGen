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

/* Additional include files retrieved from driver context */

#define TYPE_PCIBASE_DEVICE "PMC_MaxRAID_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
/* PCI Vendor and Device IDs */
#define PCI_VENDOR_ID_PMC             0x11F8
#define PCI_DEVICE_ID_PMC_MAXRAID     0x5220
#define PCI_CLASS_ID_RAID             0x0104

/* Register offsets from pmcraid_chip_cfg */
#define IOA_STATUS_REG_OFFSET         0x0000
#define IOARRIN_REG_OFFSET            0x00040
#define MAILBOX_REG_OFFSET            0x7FC30
#define GLOBAL_INTR_MASK_REG_OFFSET    0x00034
#define IOA_HOST_INTR_REG_OFFSET      0x0009C
#define IOA_HOST_INTR_CLR_REG_OFFSET  0x000A0
#define IOA_HOST_MSIX_INTR_REG_OFFSET 0x7FC40
#define IOA_HOST_MASK_REG_OFFSET      0x7FC28
#define IOA_HOST_MASK_CLR_REG_OFFSET  0x7FC28
#define HOST_IOA_INTR_REG_OFFSET      0x00020
#define HOST_IOA_INTR_CLR_REG_OFFSET  0x00020

/* Bit manipulation macros (as in driver) */
#define PMC_BIT32(n)         (1 << (31-(n)))
#define PMC_BIT8(n)          (1 << (7-(n)))

/* Interrupt bits */
#define INTRS_TRANSITION_TO_OPERATIONAL  PMC_BIT32(0)
#define INTRS_IOARCB_TRANSFER_FAILED     PMC_BIT32(3)
#define INTRS_IOA_UNIT_CHECK             PMC_BIT32(4)
#define INTRS_NO_HRRQ_FOR_CMD_RESPONSE   PMC_BIT32(5)
#define INTRS_CRITICAL_OP_IN_PROGRESS    PMC_BIT32(6)
#define INTRS_HRRQ_VALID                 PMC_BIT32(30)
#define INTRS_IOARRIN_LOST               PMC_BIT32(27)
#define INTRS_SYSTEM_BUS_MMIO_ERROR      PMC_BIT32(28)
#define INTRS_IOA_PROCESSOR_ERROR        PMC_BIT32(29)
#define INTRS_ALLOW_MSIX_VECTOR0         PMC_BIT32(31)

/* Doorbell bits */
#define DOORBELL_IOA_RESET_ALERT         PMC_BIT32(7)
#define DOORBELL_IOA_START_BIST          PMC_BIT32(23)
#define DOORBELL_RUNTIME_RESET           PMC_BIT32(1)
#define DOORBELL_INTR_MODE_MSIX          PMC_BIT32(25)
#define DOORBELL_ENABLE_DESTRUCTIVE_DIAGS PMC_BIT32(8)
#define DOORBELL_INTR_MSIX_CLR           PMC_BIT32(26)

/* Number of MSI-X vectors */
#define PMCRAID_NUM_MSIX_VECTORS         16

/* Error interrupt mask used by driver */
#define PMCRAID_ERROR_INTERRUPTS \
    (INTRS_IOARCB_TRANSFER_FAILED | INTRS_IOA_UNIT_CHECK | \
     INTRS_NO_HRRQ_FOR_CMD_RESPONSE | INTRS_IOARRIN_LOST | \
     INTRS_SYSTEM_BUS_MMIO_ERROR | INTRS_IOA_PROCESSOR_ERROR)

#define PMCRAID_PCI_INTERRUPTS \
    (PMCRAID_ERROR_INTERRUPTS | INTRS_HRRQ_VALID | \
     INTRS_TRANSITION_TO_OPERATIONAL | INTRS_ALLOW_MSIX_VECTOR0)

/* Command request types (from driver, typically defined in header) */
#define REQ_TYPE_SCSI      0x00
#define REQ_TYPE_IOACMD    0x01
#define REQ_TYPE_HCAM      0x02

/* IOA resource handle */
#define PMCRAID_IOA_RES_HANDLE 0xFFFFFFFF

/* Command codes (from driver header) - UPDATED from new source */
#define PMCRAID_IDENTIFY_HRRQ           0xC4
#define PMCRAID_QUERY_IOA_CONFIG        0xC5
#define PMCRAID_SCSI_SET_TIMESTAMP      0xA4
#define PMCRAID_SET_SUPPORTED_DEVICES   0xFB
#define PMCRAID_HOST_CONTROLLED_ASYNC   0x85  /* keep for compatibility */

/* HCAM types */
#define PMCRAID_HCAM_CODE_CONFIG_CHANGE 0x01
#define PMCRAID_HCAM_CODE_LOG_DATA      0x02

/* IOADL flags */
#define IOADL_FLAGS_READ_LAST           0x01
#define IOADL_FLAGS_LAST_DESC           0x02

/* INQUIRY command */
#define INQUIRY                          0x12

/* HRRQ toggle bit definition */
#define HRRQ_TOGGLE_BIT                   0x2

/* Sizes and alignment */
#define PMCRAID_MAX_CMD                  256
#define PMCRAID_IOARCB_ALIGNMENT         32
#define HRRQ_ENTRY_SIZE                  4

/* Maximum number of HRRQs we support */
#define MAX_HRRQ                        16

/* Dummy firmware version for INQUIRY */
#define DUMMY_FW_VERSION                 0x0200

/* New macros from provided source */
#define ALL_DEVICES_SUPPORTED            PMC_BIT8(0)
#define PMCRAID_MAX_CDB_LEN              16  /* typical */
#define PMCRAID_SENSE_DATA_LEN           32  /* placeholder */
#define PMCRAID_MAX_RESOURCES            256 /* placeholder */

/* Structure definitions updated from new driver source */
#pragma pack(push, 1)
typedef struct {
    __le64 ioarcb_bus_addr;
    __le32 resource_handle;
    __le32 response_handle;
    __le64 ioadl_bus_addr;
    __le32 ioadl_length;
    __le32 data_transfer_length;
    __le64 ioasa_bus_addr;
    __le16 ioasa_len;
    __le16 cmd_timeout;
    __le16 add_cmd_param_offset;
    __le16 add_cmd_param_length;
    __le32 reserved1[2];
    __le32 reserved2;
    __u8  request_type;
    __u8  request_flags0;
    __u8  request_flags1;
    __u8  hrrq_id;
    __u8  cdb[PMCRAID_MAX_CDB_LEN];
    /* add_data follows, but we don't need to access it */
} pmcraid_ioarcb;

typedef struct {
    __le32 ioasc;
    __le16 returned_status_length;
    __le16 available_status_length;
    __le32 residual_data_length;
    __le32 ilid;
    __le32 fd_ioasc;
    __le32 fd_res_address;
    __le32 fd_res_handle;
    __le32 reserved;
    union {
        struct { __u8 vset[64]; } vset; /* approximate size */
    } u;
    __le16 auto_sense_length;
    __le16 error_data_length;
    __u8  sense_data[PMCRAID_SENSE_DATA_LEN];
} pmcraid_ioasa;

typedef struct {
    __u8    ph_dev_type;
    __u8    page_code;
    __u8    reserved1;
    __u8    add_page_len;
    __u8    length;
    __u8    reserved2;
    __be16  fw_version;
    __u8    reserved3[16];
} pmcraid_inquiry_data;

typedef struct {
    __le16 num_entries;
    __u8   table_format;
    __u8   reserved1;
    __u8   flags;
    __u8   reserved2[11];
    /* entries follow */
} pmcraid_config_table;

typedef struct {
    __le64 address;
    __le32 data_len;
    __u8   reserved[3];
    __u8   flags;
} pmcraid_ioadl_desc;
#pragma pack(pop)

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

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;
    uint32_t intr_status;   /* IOA host interrupt status */
    uint32_t intr_mask;     /* IOA host interrupt mask */

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t ioa_status;
    uint32_t host_ioa_doorbell;
    uint32_t global_intr_mask;

    /* MSI-X handling */
    MemoryRegion msix_mem;

    /* HRRQ context */
    int num_hrrq;
    dma_addr_t hrrq_base[MAX_HRRQ];
    uint32_t hrrq_size[MAX_HRRQ];
    int hrrq_curr_idx[MAX_HRRQ];
    int hrrq_device_toggle[MAX_HRRQ]; /* toggle bit device writes */

    /* DMA Context */
    /* ... */
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t pending = s->intr_status & ~s->intr_mask;
    if (pending) {
        if (s->has_msix && msix_enabled(pdev)) {
            msix_notify(pdev, 0); /* vector 0 for general interrupts */
        } else if (s->has_msi && msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!s->has_msix || !msix_enabled(pdev)) {
            if (!s->has_msi || !msi_enabled(pdev)) {
                pci_set_irq(pdev, 0);
            }
        }
    }
}

/* Write a response entry to the specified HRRQ.
 * response_handle should have toggle bit already set. */
static void hrrq_write_response(PCIBaseState *s, int hrrq_id, uint32_t response_val)
{
    if (hrrq_id >= s->num_hrrq || !s->hrrq_base[hrrq_id]) {
        qemu_log_mask(LOG_GUEST_ERROR, "pmcraid: invalid HRRQ id %d or not initialized\n", hrrq_id);
        return;
    }
    int idx = s->hrrq_curr_idx[hrrq_id];
    if (idx >= PMCRAID_MAX_CMD) {
        qemu_log_mask(LOG_GUEST_ERROR, "pmcraid: HRRQ %d index overflow\n", hrrq_id);
        return;
    }
    dma_addr_t entry_addr = s->hrrq_base[hrrq_id] + idx * HRRQ_ENTRY_SIZE;
    uint32_t val_le = cpu_to_le32(response_val);
    pci_dma_write(&s->parent_obj, entry_addr, &val_le, HRRQ_ENTRY_SIZE);
    idx++;
    if (idx >= PMCRAID_MAX_CMD) {
        idx = 0;
        s->hrrq_device_toggle[hrrq_id] ^= 1; /* flip toggle on wrap */
    }
    s->hrrq_curr_idx[hrrq_id] = idx;
}

/* Process a submitted IOARCB with updated struct and command codes. */
static void process_ioarcb(PCIBaseState *s, dma_addr_t ioarcb_addr)
{
    pmcraid_ioarcb ioarcb;
    pci_dma_read(&s->parent_obj, ioarcb_addr, &ioarcb, sizeof(ioarcb));

    uint32_t response_handle = le32_to_cpu(ioarcb.response_handle);
    uint8_t request_type = ioarcb.request_type;
    uint8_t cdb0 = ioarcb.cdb[0];
    int hrrq_id = ioarcb.hrrq_id;
    if (hrrq_id >= MAX_HRRQ) hrrq_id = 0;

    /* Prepare IOASA buffer with success status (ioasc = 0) */
    uint16_t ioasa_len = le16_to_cpu(ioarcb.ioasa_len);
    dma_addr_t ioasa_bus_addr = le64_to_cpu(ioarcb.ioasa_bus_addr);
    if (ioasa_bus_addr && ioasa_len > 0) {
        /* Zero out the IOASA buffer and set ioasc = 0 (first 4 bytes) */
        void *ioasa_buf = g_malloc0(ioasa_len);
        *(uint32_t *)ioasa_buf = cpu_to_le32(0); /* ioasc success */
        pci_dma_write(&s->parent_obj, ioasa_bus_addr, ioasa_buf, ioasa_len);
        g_free(ioasa_buf);
    }

    /* Handle specific commands */
    switch (request_type) {
    case REQ_TYPE_IOACMD:
        switch (cdb0) {
        case PMCRAID_IDENTIFY_HRRQ: {
            /* Extract HRRQ address (8 bytes, big-endian) from cdb[2-9] and size (4 bytes, big-endian) from cdb[10-13] */
            uint64_t hrrq_addr = 0;
            memcpy(&hrrq_addr, &ioarcb.cdb[2], 8);
            hrrq_addr = be64_to_cpu(hrrq_addr);
            uint32_t size = 0;
            memcpy(&size, &ioarcb.cdb[10], 4);
            size = be32_to_cpu(size);
            int index = ioarcb.cdb[1];
            if (index < MAX_HRRQ) {
                s->hrrq_base[index] = hrrq_addr;
                s->hrrq_size[index] = size;
                s->hrrq_curr_idx[index] = 0;
                s->hrrq_device_toggle[index] = 1;
                if (index >= s->num_hrrq) s->num_hrrq = index + 1;
            }
            break;
        }
        case PMCRAID_QUERY_IOA_CONFIG: {
            /* Write a dummy config table with zero entries */
            if (ioarcb.ioadl_length) {
                dma_addr_t ioadl_bus_addr = le64_to_cpu(ioarcb.ioadl_bus_addr);
                pmcraid_ioadl_desc ioadl;
                pci_dma_read(&s->parent_obj, ioadl_bus_addr, &ioadl, sizeof(ioadl));
                dma_addr_t cfg_buf = le64_to_cpu(ioadl.address);
                uint32_t cfg_len = le32_to_cpu(ioadl.data_len);
                if (cfg_len >= sizeof(pmcraid_config_table)) {
                    pmcraid_config_table cfg;
                    memset(&cfg, 0, sizeof(cfg));
                    cfg.num_entries = cpu_to_le16(0);
                    cfg.flags = 0;
                    pci_dma_write(&s->parent_obj, cfg_buf, &cfg, sizeof(cfg));
                }
            }
            break;
        }
        case PMCRAID_SET_SUPPORTED_DEVICES:
            /* No action needed; just acknowledge */
            break;
        default:
            break;
        }
        break;
    case REQ_TYPE_SCSI: {
        if (cdb0 == INQUIRY) {
            /* Write inquiry data */
            if (ioarcb.ioadl_length) {
                dma_addr_t ioadl_bus_addr = le64_to_cpu(ioarcb.ioadl_bus_addr);
                pmcraid_ioadl_desc ioadl;
                pci_dma_read(&s->parent_obj, ioadl_bus_addr, &ioadl, sizeof(ioadl));
                dma_addr_t inq_buf = le64_to_cpu(ioadl.address);
                uint32_t data_len = le32_to_cpu(ioadl.data_len);
                pmcraid_inquiry_data inq;
                memset(&inq, 0, sizeof(inq));
                inq.ph_dev_type = 0x00;  /* direct access block device */
                inq.page_code = 0x00;
                inq.fw_version = cpu_to_be16(DUMMY_FW_VERSION);
                /* Write up to data_len bytes */
                uint32_t write_len = MIN(data_len, (uint32_t)sizeof(inq));
                pci_dma_write(&s->parent_obj, inq_buf, &inq, write_len);
                /* If data_len is larger, zero-fill the rest (optional) */
                if (data_len > write_len) {
                    void *zero_buf = g_malloc0(data_len - write_len);
                    pci_dma_write(&s->parent_obj, inq_buf + write_len, zero_buf, data_len - write_len);
                    g_free(zero_buf);
                }
            }
        } else if (cdb0 == PMCRAID_SCSI_SET_TIMESTAMP) {
            /* Just acknowledge */
        }
        break;
    }
    case REQ_TYPE_HCAM:
        /* For HCAM registration, just acknowledge */
        break;
    default:
        break;
    }

    /* Write response to HRRQ */
    uint32_t toggle = s->hrrq_device_toggle[hrrq_id] ? HRRQ_TOGGLE_BIT : 0;
    uint32_t resp = response_handle | toggle;
    hrrq_write_response(s, hrrq_id, resp);

    /* Set interrupt status bit HRRQ_VALID */
    s->intr_status |= INTRS_HRRQ_VALID;
    pcibase_update_irq(s);
}

/* MMIO/PIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case IOA_STATUS_REG_OFFSET:
        val = s->ioa_status;
        break;
    case HOST_IOA_INTR_REG_OFFSET:
        val = s->host_ioa_doorbell;
        break;
    case GLOBAL_INTR_MASK_REG_OFFSET:
        val = s->global_intr_mask;
        break;
    case IOA_HOST_INTR_REG_OFFSET:
        val = s->intr_status;
        break;
    case IOA_HOST_MSIX_INTR_REG_OFFSET:
        val = s->intr_status;
        break;
    case IOA_HOST_MASK_REG_OFFSET:
        val = s->intr_mask;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "pmcraid: unknown MMIO read at 0x%" HWADDR_PRIx "\n", addr);
        val = 0;
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case HOST_IOA_INTR_REG_OFFSET:
        /* Doorbell write from host to IOA */
        s->host_ioa_doorbell = val;
        /* Handle specific doorbell bits */
        if (val & DOORBELL_RUNTIME_RESET) {
            /* Simulate soft reset: set transition to operational immediately */
            s->ioa_status |= INTRS_TRANSITION_TO_OPERATIONAL;
            s->intr_status |= INTRS_TRANSITION_TO_OPERATIONAL;
            pcibase_update_irq(s);
        }
        if (val & DOORBELL_INTR_MODE_MSIX) {
            s->has_msix = true; /* mark that driver switched to MSI-X mode */
        }
        break;
    case GLOBAL_INTR_MASK_REG_OFFSET:
        s->global_intr_mask = val;
        pcibase_update_irq(s);
        break;
    case IOA_HOST_INTR_CLR_REG_OFFSET:
        /* Clear specific interrupt bits */
        s->intr_status &= ~val;
        pcibase_update_irq(s);
        break;
    case IOA_HOST_MASK_REG_OFFSET:
        /* Set mask bits */
        s->intr_mask = val;
        pcibase_update_irq(s);
        break;
    case IOARRIN_REG_OFFSET:
        /* IOA request ring: command submission */
        process_ioarcb(s, (dma_addr_t)val);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "pmcraid: unknown MMIO write at 0x%" HWADDR_PRIx "\n", addr);
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* PIO not used by this driver */
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* PIO not used */
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
    /* Initialize registers to default values */
    s->ioa_status = 0;
    s->host_ioa_doorbell = 0;
    s->global_intr_mask = 1; /* start with interrupts masked? */
    s->intr_status = 0;
    s->intr_mask = 0;
    s->ioa_status |= INTRS_TRANSITION_TO_OPERATIONAL; /* ready for bringup */
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
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

static const MemoryRegionOps pmcraid_msix_ops = {
    .read = NULL, /* MSI-X table and PBA are handled internally by QEMU */
    .write = NULL,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_PMC );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_PMC_MAXRAID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_ID_RAID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR 0: MMIO registers */
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 1 * MiB;
    s->bar_info[0].name = "pmcraida-mmio0";
    pcibase_register_bar(pdev, s, &s->bar_info[0], errp);

    /* BAR 1: MSI-X */
    memory_region_init_io(&s->msix_mem, OBJECT(s), &pmcraid_msix_ops, s, "pmcraida-msix", 0x4000);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->msix_mem);

    /* Add MSI-X capability and initialize */
    int msix_cap = pci_add_capability(pdev, PCI_CAP_ID_MSIX, 0, 12, errp);
    if (msix_cap < 0) {
        error_setg(errp, "Failed to add MSI-X capability");
        return;
    }
    if (msix_init(pdev, PMCRAID_NUM_MSIX_VECTORS, &s->msix_mem, 1, 0, &s->msix_mem, 1, 0x800, msix_cap, errp)) {
        /* MSI-X init failed; driver may fall back to INTx */
        s->has_msix = false;
    } else {
        s->has_msix = true;
    }

    /* Optional MSI support (fallback) */
    s->has_msi = false;

    s->num_bars = 2;
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, &s->msix_mem, &s->msix_mem);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "PMC_MaxRAID_pci",
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
