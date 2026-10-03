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

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID PCI_VENDOR_ID_INTEL
#define DEVICE_ID 0x7560
#define CLASS_ID PCI_CLASS_OTHERS

#define IPC_DOORBELL_BAR0 0
#define IPC_SCRATCHPAD_BAR2 2

#define IPC_DOORBELL_CH_OFFSET BIT(5)
#define IPC_WRITE_PTR_REG_0 BIT(4)
#define IPC_CAPTURE_PTR_REG_0 BIT(3)
#define IPC_MEM_DL_ETH_OFFSET 16

#define IPC_MSI_VECTORS 1

#define IPC_DOORBELL_IRQ_SLEEP 2
#define IPC_HOST_SLEEP_ENTER_SLEEP 0

#define MMIO_OFFSET_EXECUTION_STAGE 0x00
#define MMIO_OFFSET_CHIP_INFO 0x04
#define MMIO_CHIP_INFO_SIZE 60
#define MMIO_OFFSET_ROM_EXIT_CODE 0x40
#define MMIO_OFFSET_PSI_ADDRESS 0x54
#define MMIO_OFFSET_PSI_SIZE 0x5C
#define MMIO_OFFSET_IPC_STATUS 0x60
#define MMIO_OFFSET_CONTEXT_INFO 0x64
#define MMIO_OFFSET_BASE_ADDR 0x6C
#define MMIO_OFFSET_END_ADDR 0x74
#define MMIO_OFFSET_CP_VERSION 0xF0
#define MMIO_OFFSET_CP_CAPABILITIES 0xF4

#define IPC_MEM_MSG_ENTRIES 128
#define IPC_MSG_IRQ_VECTOR 0
#define IPC_DEVICE_IRQ_VECTOR 0

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
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t doorbell_reg;
    uint32_t scratchpad_reg;

    /* DMA Context */
    uint64_t phy_ap_shm;

    uint32_t ipc_status;
    uint32_t execution_stage;
    uint8_t reset_enable;
    uint32_t device_sleep_notification;
    uint32_t cp_version;
};

enum ipc_mem_exec_stage {
    IPC_MEM_EXEC_STAGE_RUN = 0x600DF00D,
    IPC_MEM_EXEC_STAGE_CRASH = 0x8BADF00D,
    IPC_MEM_EXEC_STAGE_CD_READY = 0xBADC0DED,
    IPC_MEM_EXEC_STAGE_BOOT = 0xFEEDB007,
    IPC_MEM_EXEC_STAGE_PSI = 0xFEEDBEEF,
    IPC_MEM_EXEC_STAGE_EBL = 0xFEEDCAFE,
    IPC_MEM_EXEC_STAGE_INVALID = 0xFFFFFFFF
};

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

struct ipc_protocol_td {
    union {
        uint64_t address;
        struct {
            uint32_t address;
            uint32_t desc;
        } __attribute__((packed)) shm;
    } buffer;
    uint32_t scs;
    uint32_t next;
};

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t doorbell_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void doorbell_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    if (s->has_msi) {
        msi_notify(&s->parent_obj, 0);
    }
}

static const MemoryRegionOps doorbell_mmio_ops = {
    .read = doorbell_mmio_read,
    .write = doorbell_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static uint64_t scratchpad_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    switch (addr) {
    case MMIO_OFFSET_EXECUTION_STAGE:
        return s->execution_stage;
    case MMIO_OFFSET_CHIP_INFO:
        if (size == 1) return 1; /* version */
        return (58 << 8) | 1;
    case MMIO_OFFSET_CHIP_INFO + 1:
        return 58; /* size - 2 */
    case MMIO_OFFSET_IPC_STATUS:
        return s->ipc_status;
    case MMIO_OFFSET_CP_VERSION:
        return s->cp_version;
    case MMIO_OFFSET_CONTEXT_INFO:
        if (size == 8) return s->phy_ap_shm;
        return s->phy_ap_shm & 0xFFFFFFFF;
    case MMIO_OFFSET_CONTEXT_INFO + 4:
        return s->phy_ap_shm >> 32;
    }
    return 0;
}

static void scratchpad_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    switch (addr) {
    case MMIO_OFFSET_CONTEXT_INFO:
        if (size == 8) {
            s->phy_ap_shm = val;
        } else {
            s->phy_ap_shm = (s->phy_ap_shm & 0xFFFFFFFF00000000ULL) | val;
        }
        break;
    case MMIO_OFFSET_CONTEXT_INFO + 4:
        s->phy_ap_shm = (s->phy_ap_shm & 0xFFFFFFFFULL) | (val << 32);
        break;
    }
}

static const MemoryRegionOps scratchpad_mmio_ops = {
    .read = scratchpad_mmio_read,
    .write = scratchpad_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Initialize execution stage to a valid state to pass driver probe */
    s->execution_stage = IPC_MEM_EXEC_STAGE_BOOT;
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
        const MemoryRegionOps *ops = NULL;
        if (bi->index == IPC_DOORBELL_BAR0) {
            ops = &doorbell_mmio_ops;
        } else if (bi->index == IPC_SCRATCHPAD_BAR2) {
            ops = &scratchpad_mmio_ops;
        } else {
            ops = &scratchpad_mmio_ops;
        }
        memory_region_init_io(mr, OBJECT(s), ops, s, bi->name, aligned_size);
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

    /* Static PCI configuration */
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

    /* BAR Initialization */
    s->num_bars = 2;
    s->bar_info[0].index = IPC_DOORBELL_BAR0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x1000; /* Missing from source, requested in needed_sources */
    s->bar_info[0].name = "ipc_doorbell";

    s->bar_info[1].index = IPC_SCRATCHPAD_BAR2;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = 0x1000; /* Missing from source, requested in needed_sources */
    s->bar_info[1].name = "ipc_scratchpad";
  
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = true;
    if (msi_init(pdev, 0, IPC_MSI_VECTORS, true, false, errp)) {
        return;
    }
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
}

/* Minimal VMState to satisfy QEMU migration subsystems */
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
