/*
 * QEMU PCI device model for mpi3mr driver (behavioral stub with minimal
 * sysif_regs emulation sufficient for basic driver bring-up paths that
 * touch a few system interface registers.
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

/* Additional include files retrieved from driver context */
#include "hw/hw.h"

#define TYPE_PCIBASE_DEVICE "mpi3mr_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCIBASE_VENDOR_ID 0x1000
#define PCIBASE_DEVICE_ID 0x00a5
#define PCIBASE_CLASS_ID  PCI_CLASS_STORAGE_SCSI

/* Minimal system interface register layout as visible in mpi3_sysif_registers.
 * We only model fields that the provided driver code actually reads/writes
 * via mrioc->sysif_regs.
 */

typedef struct SysIfRegs {
    uint32_t ioc_status;                 /* offset 0x18 in struct, used via readl */
    uint32_t admin_queue_num_entries;    /* offset 0x24 in struct, not yet used */
    uint64_t admin_request_queue_address;/* offset 0x28 in struct */
    uint64_t admin_reply_queue_address;  /* offset 0x30 in struct */
    uint32_t coalesce_control;           /* offset 0x40 in struct */
    uint16_t admin_request_queue_pi;     /* near offset 0x1000 in struct */
    uint16_t admin_reply_queue_ci;       /* near offset 0x1004 in struct */
    /* oper_queue_indexes array is referenced for producer_index writes */
    uint32_t oper_queue_prod_index[383]; /* producer_index for each op reply q */
    uint32_t write_sequence;             /* offset 0x1c04 in struct */
    uint32_t host_diagnostic;            /* offset 0x1c08 in struct */
    uint32_t fault;                      /* offset 0x1c10 in struct */
    uint32_t fault_info[3];              /* offset 0x1c14 in struct */
    uint64_t hcb_address;                /* offset 0x1c28 in struct */
    uint32_t hcb_size;                   /* offset 0x1c30 in struct */
    uint32_t reply_free_host_index;      /* offset 0x1c40 in struct */
    uint32_t sense_buffer_free_host_index; /* offset 0x1c44 in struct */
    uint64_t diag_rw_data;               /* offset 0x1c50 in struct */
    uint64_t diag_rw_address;            /* offset 0x1c58 in struct */
    uint16_t diag_rw_control;            /* offset 0x1c60 in struct */
    uint16_t diag_rw_status;             /* offset 0x1c62 in struct */
    uint32_t scratchpad[4];              /* offset 0x1c6c in struct */
} SysIfRegs;

/* IO status bits as consumed by mpi3mr_get_iocstate and related helpers */
#define MPI3_SYSIF_IOC_STATUS_READY           0x00000001u
#define MPI3_SYSIF_IOC_STATUS_RESET_HISTORY   0x00000002u
#define MPI3_SYSIF_IOC_STATUS_FAULT           0x00000004u

/* IOC configuration enable bit used in mpi3mr_get_iocstate */
#define MPI3_SYSIF_IOC_CONFIG_ENABLE_IOC      0x00000001u

/* Host diagnostic bit used in mpi3mr_check_rh_fault_ioc and soft_reset_handler */
#define MPI3_SYSIF_HOST_DIAG_SAVE_IN_PROGRESS 0x00000001u

/* Fault code mask used in mpi3mr_check_rh_fault_ioc and soft_reset_handler */
#define MPI3_SYSIF_FAULT_CODE_MASK            0x0000FFFFu

/* BAR description */

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

    /* System interface registers shadow backing MMIO region */
    SysIfRegs sysif;

    /* Simple IOC configuration shadow to satisfy iocstate helper */
    uint32_t ioc_configuration;
};


/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* No explicit interrupt status bits are visible in provided sources.
     * Leave interrupts quiescent for now.
     */
    (void)pdev;
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* No DMA engine registers are manipulated directly in the new snippets. */
    (void)pdev;
    (void)is_write;
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Single MMIO BAR will map the mpi3_sysif_registers structure at BAR0. */
    if (addr + size > sizeof(SysIfRegs)) {
        return 0;
    }

    switch (size) {
    case 1:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "mpi3mr_pci: 8-bit MMIO read at 0x%" HWADDR_PRIx "\n",
                      addr);
        break;
    case 2:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "mpi3mr_pci: 16-bit MMIO read at 0x%" HWADDR_PRIx "\n",
                      addr);
        break;
    case 4:
        break;
    case 8:
        break;
    default:
        return 0;
    }

    /* Map offsets into the SysIfRegs structure */
    if (addr == offsetof(SysIfRegs, ioc_status) && size == 4) {
        val = s->sysif.ioc_status;
    } else if (addr == offsetof(SysIfRegs, admin_queue_num_entries) && size == 4) {
        val = s->sysif.admin_queue_num_entries;
    } else if (addr == offsetof(SysIfRegs, admin_request_queue_address) && size == 8) {
        val = s->sysif.admin_request_queue_address;
    } else if (addr == offsetof(SysIfRegs, admin_reply_queue_address) && size == 8) {
        val = s->sysif.admin_reply_queue_address;
    } else if (addr == offsetof(SysIfRegs, coalesce_control) && size == 4) {
        val = s->sysif.coalesce_control;
    } else if (addr == offsetof(SysIfRegs, admin_request_queue_pi) && size == 2) {
        val = s->sysif.admin_request_queue_pi;
    } else if (addr == offsetof(SysIfRegs, admin_reply_queue_ci) && size == 2) {
        val = s->sysif.admin_reply_queue_ci;
    } else if (addr >= offsetof(SysIfRegs, oper_queue_prod_index) &&
               addr < offsetof(SysIfRegs, oper_queue_prod_index) + sizeof(s->sysif.oper_queue_prod_index) &&
               size == 4) {
        hwaddr index = (addr - offsetof(SysIfRegs, oper_queue_prod_index)) / 4;
        if (index < 383) {
            val = s->sysif.oper_queue_prod_index[index];
        }
    } else if (addr == offsetof(SysIfRegs, write_sequence) && size == 4) {
        val = s->sysif.write_sequence;
    } else if (addr == offsetof(SysIfRegs, host_diagnostic) && size == 4) {
        val = s->sysif.host_diagnostic;
    } else if (addr == offsetof(SysIfRegs, fault) && size == 4) {
        val = s->sysif.fault;
    } else if (addr >= offsetof(SysIfRegs, fault_info) &&
               addr < offsetof(SysIfRegs, fault_info) + sizeof(s->sysif.fault_info) &&
               size == 4) {
        hwaddr index = (addr - offsetof(SysIfRegs, fault_info)) / 4;
        if (index < 3) {
            val = s->sysif.fault_info[index];
        }
    } else if (addr == offsetof(SysIfRegs, hcb_address) && size == 8) {
        val = s->sysif.hcb_address;
    } else if (addr == offsetof(SysIfRegs, hcb_size) && size == 4) {
        val = s->sysif.hcb_size;
    } else if (addr == offsetof(SysIfRegs, reply_free_host_index) && size == 4) {
        val = s->sysif.reply_free_host_index;
    } else if (addr == offsetof(SysIfRegs, sense_buffer_free_host_index) && size == 4) {
        val = s->sysif.sense_buffer_free_host_index;
    } else if (addr == offsetof(SysIfRegs, diag_rw_data) && size == 8) {
        val = s->sysif.diag_rw_data;
    } else if (addr == offsetof(SysIfRegs, diag_rw_address) && size == 8) {
        val = s->sysif.diag_rw_address;
    } else if (addr == offsetof(SysIfRegs, diag_rw_control) && size == 2) {
        val = s->sysif.diag_rw_control;
    } else if (addr == offsetof(SysIfRegs, diag_rw_status) && size == 2) {
        val = s->sysif.diag_rw_status;
    } else if (addr >= offsetof(SysIfRegs, scratchpad) &&
               addr < offsetof(SysIfRegs, scratchpad) + sizeof(s->sysif.scratchpad) &&
               size == 4) {
        hwaddr index = (addr - offsetof(SysIfRegs, scratchpad)) / 4;
        if (index < 4) {
            val = s->sysif.scratchpad[index];
        }
    } else {
        /* For unmapped offsets inside the struct, return 0 */
        val = 0;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr + size > sizeof(SysIfRegs)) {
        return;
    }

    switch (size) {
    case 1:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "mpi3mr_pci: 8-bit MMIO write at 0x%" HWADDR_PRIx "\n",
                      addr);
        return;
    case 2:
    case 4:
    case 8:
        break;
    default:
        return;
    }

    if (addr == offsetof(SysIfRegs, ioc_status) && size == 4) {
        s->sysif.ioc_status = (uint32_t)val;
    } else if (addr == offsetof(SysIfRegs, admin_queue_num_entries) && size == 4) {
        s->sysif.admin_queue_num_entries = (uint32_t)val;
    } else if (addr == offsetof(SysIfRegs, admin_request_queue_address) && size == 8) {
        s->sysif.admin_request_queue_address = (uint64_t)val;
    } else if (addr == offsetof(SysIfRegs, admin_reply_queue_address) && size == 8) {
        s->sysif.admin_reply_queue_address = (uint64_t)val;
    } else if (addr == offsetof(SysIfRegs, coalesce_control) && size == 4) {
        s->sysif.coalesce_control = (uint32_t)val;
    } else if (addr == offsetof(SysIfRegs, admin_request_queue_pi) && size == 2) {
        s->sysif.admin_request_queue_pi = (uint16_t)val;
    } else if (addr == offsetof(SysIfRegs, admin_reply_queue_ci) && size == 2) {
        s->sysif.admin_reply_queue_ci = (uint16_t)val;
    } else if (addr >= offsetof(SysIfRegs, oper_queue_prod_index) &&
               addr < offsetof(SysIfRegs, oper_queue_prod_index) + sizeof(s->sysif.oper_queue_prod_index) &&
               size == 4) {
        hwaddr index = (addr - offsetof(SysIfRegs, oper_queue_prod_index)) / 4;
        if (index < 383) {
            s->sysif.oper_queue_prod_index[index] = (uint32_t)val;
        }
    } else if (addr == offsetof(SysIfRegs, write_sequence) && size == 4) {
        s->sysif.write_sequence = (uint32_t)val;
    } else if (addr == offsetof(SysIfRegs, host_diagnostic) && size == 4) {
        s->sysif.host_diagnostic = (uint32_t)val;
    } else if (addr == offsetof(SysIfRegs, fault) && size == 4) {
        s->sysif.fault = (uint32_t)val;
    } else if (addr >= offsetof(SysIfRegs, fault_info) &&
               addr < offsetof(SysIfRegs, fault_info) + sizeof(s->sysif.fault_info) &&
               size == 4) {
        hwaddr index = (addr - offsetof(SysIfRegs, fault_info)) / 4;
        if (index < 3) {
            s->sysif.fault_info[index] = (uint32_t)val;
        }
    } else if (addr == offsetof(SysIfRegs, hcb_address) && size == 8) {
        s->sysif.hcb_address = (uint64_t)val;
    } else if (addr == offsetof(SysIfRegs, hcb_size) && size == 4) {
        s->sysif.hcb_size = (uint32_t)val;
    } else if (addr == offsetof(SysIfRegs, reply_free_host_index) && size == 4) {
        s->sysif.reply_free_host_index = (uint32_t)val;
    } else if (addr == offsetof(SysIfRegs, sense_buffer_free_host_index) && size == 4) {
        s->sysif.sense_buffer_free_host_index = (uint32_t)val;
    } else if (addr == offsetof(SysIfRegs, diag_rw_data) && size == 8) {
        s->sysif.diag_rw_data = (uint64_t)val;
    } else if (addr == offsetof(SysIfRegs, diag_rw_address) && size == 8) {
        s->sysif.diag_rw_address = (uint64_t)val;
    } else if (addr == offsetof(SysIfRegs, diag_rw_control) && size == 2) {
        s->sysif.diag_rw_control = (uint16_t)val;
    } else if (addr == offsetof(SysIfRegs, diag_rw_status) && size == 2) {
        s->sysif.diag_rw_status = (uint16_t)val;
    } else if (addr >= offsetof(SysIfRegs, scratchpad) &&
               addr < offsetof(SysIfRegs, scratchpad) + sizeof(s->sysif.scratchpad) &&
               size == 4) {
        hwaddr index = (addr - offsetof(SysIfRegs, scratchpad)) / 4;
        if (index < 4) {
            s->sysif.scratchpad[index] = (uint32_t)val;
        }
    } else {
        /* Ignore writes to unmapped offsets within struct */
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    (void)s;
    (void)addr;
    (void)size;
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    (void)s;
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

    /* Power-on defaults for sysif/ioc state */
    memset(&s->sysif, 0, sizeof(s->sysif));
    s->ioc_configuration = 0;

    /* Present controller as READY+ENABLED so that mpi3mr_get_iocstate() will
     * see MRIOC_STATE_READY when called.
     */
    s->sysif.ioc_status = MPI3_SYSIF_IOC_STATUS_READY;
    s->ioc_configuration = MPI3_SYSIF_IOC_CONFIG_ENABLE_IOC;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCIBASE_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCIBASE_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCIBASE_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization: provide a single MMIO BAR for sysif registers */
    s->num_bars = 1;
    for (int i = 0; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = "mpi3mr-bar";
    }

    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = sizeof(SysIfRegs);
    s->bar_info[0].name = "mpi3mr-sysif";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize sysif/ioc state */
    memset(&s->sysif, 0, sizeof(s->sysif));
    s->ioc_configuration = 0;
    s->sysif.ioc_status = MPI3_SYSIF_IOC_STATUS_READY;
    s->ioc_configuration = MPI3_SYSIF_IOC_CONFIG_ENABLE_IOC;

    (void)errp;
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }

    (void)s;
}

/* Minimal VMState to satisfy QEMU migration subsystems */
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

