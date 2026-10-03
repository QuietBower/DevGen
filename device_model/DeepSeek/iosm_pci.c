/*
 * QEMU PCI device model for Intel IOSM IPC (wwan) device.
 * Derived from driver: drivers/net/wwan/iosm/iosm_ipc_pcie.c
 * Phase 4: Debug & Update (Runtime Refinement) - Separated BAR 0 (doorbell) and BAR 2 (scratchpad).
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

#define TYPE_PCIBASE_DEVICE "iosm_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI Identification */
#define VENDOR_ID 0x8086
#define DEVICE_ID 0x7560
#define CLASS_ID  0x0d00

/* Doorbell bit definitions from driver (iosm_ipc_pcie.c) */
#define DOORBELL_WRITE_PTR_BIT_0   BIT(4)
#define DOORBELL_CAPTURE_PTR_BIT_0 BIT(3)
#define DOORBELL_CH_OFFSET_BIT     BIT(5)

/* Register Offsets for doorbell area (now in BAR 0) */
enum {
    REG_EXEC_STAGE      = 0x00,
    REG_CHIP_INFO       = 0x04,
    REG_ROM_EXIT_CODE   = 0x08,
    REG_PSI_ADDRESS     = 0x0C,
    REG_PSI_SIZE        = 0x10,
    REG_IPC_STATUS      = 0x14,
    REG_CONTEXT_INFO    = 0x18,
    REG_AP_WIN_BASE     = 0x1C,
    REG_AP_WIN_END      = 0x20,
    REG_CP_VERSION      = 0x24,
    REG_CP_CAPABILITY   = 0x28,
    REG_DOORBELL_WRITE  = 0x100,
    REG_DOORBELL_CAPTURE = 0x104,
};

/* Execution stages */
enum {
    IPC_MEM_EXEC_STAGE_RUN      = 0x600DF00D,
    IPC_MEM_EXEC_STAGE_CRASH    = 0x8BADF00D,
    IPC_MEM_EXEC_STAGE_CD_READY = 0xBADC0DED,
    IPC_MEM_EXEC_STAGE_BOOT     = 0xFEEDB007,
    IPC_MEM_EXEC_STAGE_PSI      = 0xFEEDBEEF,
    IPC_MEM_EXEC_STAGE_EBL      = 0xFEEDCAFE,
    IPC_MEM_EXEC_STAGE_INVALID  = 0xFFFFFFFF
};

/* IPC MSI vectors */
#define IPC_MSI_VECTORS 1

/* Shared memory layout constants */
#define IPC_MEM_MAX_PIPES 16
#define IPC_MEM_MSG_ENTRIES 128

/* Structures from driver for shared memory layout */
struct ipc_protocol_context_info {
    uint64_t device_info_addr;
    uint64_t head_array;
    uint64_t tail_array;
    uint64_t msg_head;
    uint64_t msg_tail;
    uint64_t msg_ring_addr;
    uint16_t msg_ring_entries;
    uint8_t msg_irq_vector;
    uint8_t device_info_irq_vector;
};

struct ipc_protocol_device_info {
    uint32_t execution_stage;
    uint32_t ipc_status;
    uint32_t device_sleep_notification;
};

struct ipc_mem_msg_open_pipe {
    uint64_t tdr_addr;
    uint16_t tdr_entries;
    uint8_t pipe_nr;
    uint8_t type_of_message;
    uint32_t irq_vector;
    uint32_t accumulation_backoff;
    uint32_t completion_status;
};

struct ipc_mem_msg_close_pipe {
    uint32_t reserved1[2];
    uint16_t reserved2;
    uint8_t pipe_nr;
    uint8_t type_of_message;
    uint32_t reserved3;
    uint32_t reserved4;
    uint32_t completion_status;
};

struct ipc_mem_msg_abort_pipe {
    uint32_t reserved1[2];
    uint16_t reserved2;
    uint8_t pipe_nr;
    uint8_t type_of_message;
    uint32_t reserved3;
    uint32_t reserved4;
    uint32_t completion_status;
};

struct ipc_mem_msg_host_sleep {
    uint32_t reserved1[2];
    uint8_t target;
    uint8_t state;
    uint8_t reserved2;
    uint8_t type_of_message;
    uint32_t reserved3;
    uint32_t reserved4;
    uint32_t completion_status;
};

struct ipc_mem_msg_feature_set {
    uint32_t reserved1[2];
    uint16_t reserved2;
    uint8_t reset_enable;
    uint8_t type_of_message;
    uint32_t reserved3;
    uint32_t reserved4;
    uint32_t completion_status;
};

struct ipc_mem_msg_common {
    uint32_t reserved1[2];
    uint8_t reserved2[3];
    uint8_t type_of_message;
    uint32_t reserved3;
    uint32_t reserved4;
    uint32_t completion_status;
};

union ipc_mem_msg_entry {
    struct ipc_mem_msg_open_pipe open_pipe;
    struct ipc_mem_msg_close_pipe close_pipe;
    struct ipc_mem_msg_abort_pipe abort_pipe;
    struct ipc_mem_msg_host_sleep host_sleep;
    struct ipc_mem_msg_feature_set feature_set;
    struct ipc_mem_msg_common common;
};

struct ipc_protocol_ap_shm {
    struct ipc_protocol_context_info ci;
    struct ipc_protocol_device_info device_info;
    uint32_t msg_head;
    uint32_t head_array[IPC_MEM_MAX_PIPES];
    uint32_t msg_tail;
    uint32_t tail_array[IPC_MEM_MAX_PIPES];
    union ipc_mem_msg_entry msg_ring[IPC_MEM_MSG_ENTRIES];
};

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion doorbell_mmio;   /* BAR 0: doorbell and control registers */
    MemoryRegion scratchpad_ram;  /* BAR 2: shared memory (scratchpad) */

    bool has_msi;
    bool has_msix;
    uint32_t irq_status;
    uint32_t irq_mask;

    struct {
        uint32_t exec_stage;
        uint32_t chip_info;
        uint32_t rom_exit_code;
        uint32_t psi_address;
        uint32_t psi_size;
        uint32_t ipc_status;
        uint32_t context_info;
        uint32_t ap_win_base;
        uint32_t ap_win_end;
        uint32_t cp_version;
        uint32_t cp_capability;
    } regs;
    
    uint32_t doorbell_write_val;
    uint32_t doorbell_capture_val;

    uint32_t power_state;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->irq_status) {
        if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!msi_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case REG_EXEC_STAGE:
        val = s->regs.exec_stage;
        break;
    case REG_CHIP_INFO:
        val = s->regs.chip_info;
        break;
    case REG_ROM_EXIT_CODE:
        val = s->regs.rom_exit_code;
        break;
    case REG_PSI_ADDRESS:
        val = s->regs.psi_address;
        break;
    case REG_PSI_SIZE:
        val = s->regs.psi_size;
        break;
    case REG_IPC_STATUS:
        val = s->regs.ipc_status;
        break;
    case REG_CONTEXT_INFO:
        val = s->regs.context_info;
        break;
    case REG_AP_WIN_BASE:
        val = s->regs.ap_win_base;
        break;
    case REG_AP_WIN_END:
        val = s->regs.ap_win_end;
        break;
    case REG_CP_VERSION:
        val = s->regs.cp_version;
        break;
    case REG_CP_CAPABILITY:
        val = s->regs.cp_capability;
        break;
    case REG_DOORBELL_CAPTURE:
        val = s->doorbell_capture_val;
        if (s->doorbell_capture_val & (DOORBELL_WRITE_PTR_BIT_0 | DOORBELL_CAPTURE_PTR_BIT_0)) {
            s->irq_status &= ~0x1;
            pcibase_update_irq(s);
        }
        s->doorbell_capture_val = 0;
        break;
    default:
        if (addr >= 0x100 && addr < 0x200) {
            val = 0;
        } else {
            qemu_log_mask(LOG_UNIMP, "iosm_pci: unimplemented MMIO read at 0x%"HWADDR_PRIx"\n", addr);
        }
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case REG_EXEC_STAGE:
        s->regs.exec_stage = val;
        break;
    case REG_CHIP_INFO:
        s->regs.chip_info = val;
        break;
    case REG_ROM_EXIT_CODE:
        s->regs.rom_exit_code = val;
        break;
    case REG_PSI_ADDRESS:
        s->regs.psi_address = val;
        break;
    case REG_PSI_SIZE:
        s->regs.psi_size = val;
        break;
    case REG_IPC_STATUS:
        s->regs.ipc_status = val;
        break;
    case REG_CONTEXT_INFO:
        s->regs.context_info = val;
        break;
    case REG_AP_WIN_BASE:
        s->regs.ap_win_base = val;
        break;
    case REG_AP_WIN_END:
        s->regs.ap_win_end = val;
        break;
    case REG_CP_VERSION:
        s->regs.cp_version = val;
        break;
    case REG_CP_CAPABILITY:
        s->regs.cp_capability = val;
        break;
    case REG_DOORBELL_WRITE:
        s->doorbell_write_val = val;
        s->doorbell_capture_val |= val;
        if (val & (DOORBELL_WRITE_PTR_BIT_0 | DOORBELL_CAPTURE_PTR_BIT_0)) {
            s->irq_status |= 0x1;
            pcibase_update_irq(s);
        }
        break;
    default:
        if (addr >= 0x100 && addr < 0x200) {
            /* Fallback for doorbell area writes */
            s->doorbell_write_val = val;
            s->doorbell_capture_val |= val;
            if (val & (DOORBELL_WRITE_PTR_BIT_0 | DOORBELL_CAPTURE_PTR_BIT_0)) {
                s->irq_status |= 0x1;
                pcibase_update_irq(s);
            }
        } else {
            qemu_log_mask(LOG_UNIMP, "iosm_pci: unimplemented MMIO write at 0x%"HWADDR_PRIx"\n", addr);
        }
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

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    memset(&s->regs, 0, sizeof(s->regs));
    s->regs.exec_stage = IPC_MEM_EXEC_STAGE_BOOT;
    s->doorbell_write_val = 0;
    s->doorbell_capture_val = 0;
    s->irq_status = 0;
    s->irq_mask = 0;
    s->power_state = 0;

    struct ipc_protocol_ap_shm *shm = memory_region_get_ram_ptr(&s->scratchpad_ram);
    memset(shm, 0, sizeof(*shm));
    shm->device_info.execution_stage = IPC_MEM_EXEC_STAGE_RUN;
    shm->device_info.ipc_status = 0;
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

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

    /* BAR 0: Doorbell and control registers */
    memory_region_init_io(&s->doorbell_mmio, OBJECT(s), &pcibase_mmio_ops, s,
                          "doorbell", 0x2000);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->doorbell_mmio);

    /* BAR 2: Shared memory (scratchpad) */
    memory_region_init_ram(&s->scratchpad_ram, OBJECT(s), "scratchpad",
                           0x10000, &error_fatal);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->scratchpad_ram);

    if (msi_init(pdev, 0, IPC_MSI_VECTORS, true, false, errp) < 0) {
        return;
    }
    s->has_msi = true;

    pcibase_reset(DEVICE(pdev));
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
    .name = "iosm_pci",
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
