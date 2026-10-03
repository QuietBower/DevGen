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

/* Additional include files retrieved from driver context */

#define TYPE_PCIBASE_DEVICE "vmw_pvscsi_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
/* PCI_VENDOR_ID_VMWARE already defined in hw/pci/pci.h, use that */
#define PCI_DEVICE_ID_VMWARE_PVSCSI 0x07C0
#define CLASS_ID 0x0100 /* PCI_CLASS_STORAGE_SCSI */

#define MASK(n) ((1U << (n)) - 1)
#define PVSCSI_SETUP_RINGS_MAX_NUM_PAGES        32
#define PVSCSI_SETUP_MSG_RING_MAX_NUM_PAGES  16

/* Command definitions */
#define PVSCSI_CMD_ADAPTER_RESET            1
#define PVSCSI_CMD_SETUP_RINGS              3
#define PVSCSI_CMD_RESET_BUS                4
#define PVSCSI_CMD_RESET_DEVICE             5
#define PVSCSI_CMD_ABORT_CMD                6
#define PVSCSI_CMD_CONFIG                   7
#define PVSCSI_CMD_SETUP_MSG_RING           8
#define PVSCSI_CMD_SETUP_REQCALLTHRESHOLD   10

/* Register offsets */
#define PVSCSI_REG_OFFSET_COMMAND           0x0
#define PVSCSI_REG_OFFSET_COMMAND_DATA      0x4
#define PVSCSI_REG_OFFSET_COMMAND_STATUS    0x8
#define PVSCSI_REG_OFFSET_INTR_STATUS       0x100c
#define PVSCSI_REG_OFFSET_INTR_MASK         0x2010
#define PVSCSI_REG_OFFSET_KICK_RW_IO        0x40
#define PVSCSI_REG_OFFSET_KICK_NON_RW_IO    0x44

/* Config page constants */
#define PVSCSI_CONFIG_PAGE_CONTROLLER        0x1e
#define PVSCSI_CONFIG_CONTROLLER_ADDRESS     0x1e
#define BTSTAT_SUCCESS                       0x00
#define SDSTAT_GOOD                          0x00

/* Max entries per page */
#define PVSCSI_MAX_NUM_REQ_ENTRIES_PER_PAGE \
                (4096 / sizeof(struct PVSCSIRingReqDesc))
#define PVSCSI_MAX_NUM_CMP_ENTRIES_PER_PAGE \
                (4096 / sizeof(struct PVSCSIRingCmpDesc))

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
    /* Registers will be defined once offsets are available */
    uint8_t rev; /* device revision */

    /* DMA Context */
    dma_addr_t rings_state_pa;
    dma_addr_t req_ring_pa;
    dma_addr_t cmp_ring_pa;
    dma_addr_t msg_ring_pa;
    bool rings_initialized;

    /* Operational status flags */
    /* None defined in driver */

    /* State used to handle reset sequences */
    /* Reset state placeholder */

    /* Power management state (D0-D3) */
    /* Not referenced in driver */

    /* Additional fields */
    uint32_t command;                    /* last command written */
    uint32_t command_status;            /* command status register */
    uint32_t cmd_data[1024];            /* buffer to accumulate command descriptor */
    int cmd_data_idx;                   /* number of words written so far */
    bool cmd_active;                    /* whether command is being processed */
};

/* Additional defines or structures */
/* Interrupt masks */
#define PVSCSI_INTR_CMPL_MASK              MASK(2)
#define PVSCSI_INTR_MSG_MASK               (MASK(2) << 2)
#define PVSCSI_INTR_ALL_SUPPORTED          MASK(4)

/* Ring descriptor structures (hardware interface) */
struct PVSCSIRingReqDesc {
    uint64_t context;
    uint64_t dataAddr;
    uint64_t dataLen;
    uint64_t senseAddr;
    uint32_t senseLen;
    uint32_t flags;
    uint8_t cdb[16];
    uint8_t cdbLen;
    uint8_t lun[8];
    uint8_t tag;
    uint8_t bus;
    uint8_t target;
    uint16_t vcpuHint;
    uint8_t unused[58];
} QEMU_PACKED;

struct PVSCSIRingCmpDesc {
    uint64_t context;
    uint64_t dataLen;
    uint32_t senseLen;
    uint16_t hostStatus;
    uint16_t scsiStatus;
    uint32_t _pad[2];
} QEMU_PACKED;

struct PVSCSIRingMsgDesc {
    uint32_t type;
    uint32_t args[31];
} QEMU_PACKED;

struct PVSCSIRingsState {
    uint32_t reqProdIdx;
    uint32_t reqConsIdx;
    uint32_t reqNumEntriesLog2;

    uint32_t cmpProdIdx;
    uint32_t cmpConsIdx;
    uint32_t cmpNumEntriesLog2;

    uint32_t reqCallThreshold;

    uint8_t _pad[100];

    uint32_t msgProdIdx;
    uint32_t msgConsIdx;
    uint32_t msgNumEntriesLog2;
} QEMU_PACKED;

struct PVSCSICmdDescAbortCmd {
    uint64_t context;
    uint32_t target;
    uint32_t _pad;
} QEMU_PACKED;

struct PVSCSICmdDescResetDevice {
    uint32_t target;
    uint8_t lun[8];
} QEMU_PACKED;

struct PVSCSICmdDescSetupRings {
    uint32_t reqRingNumPages;
    uint32_t cmpRingNumPages;
    uint64_t ringsStatePPN;
    uint64_t reqRingPPNs[PVSCSI_SETUP_RINGS_MAX_NUM_PAGES];
    uint64_t cmpRingPPNs[PVSCSI_SETUP_RINGS_MAX_NUM_PAGES];
} QEMU_PACKED;

struct PVSCSICmdDescSetupMsgRing {
    uint32_t numPages;
    uint32_t _pad;
    uint64_t ringPPNs[PVSCSI_SETUP_MSG_RING_MAX_NUM_PAGES];
} QEMU_PACKED;

struct PVSCSICmdDescSetupReqCall {
    uint32_t enable;
} QEMU_PACKED;

struct PVSCSICmdDescConfigCmd {
    uint64_t cmpAddr;
    uint64_t configPageAddress;
    uint32_t configPageNum;
    uint32_t _pad;
} QEMU_PACKED;

struct PVSCSIConfigPageHeader {
    uint32_t pageNum;
    uint16_t numDwords;
    uint16_t hostStatus;
    uint16_t scsiStatus;
    uint16_t reserved[3];
} QEMU_PACKED;

struct PVSCSIConfigPageController {
    struct PVSCSIConfigPageHeader header;
    uint64_t nodeWWN; /* Device name as defined in the SAS spec. */
    uint16_t manufacturer[64];
    uint16_t serialNumber[64];
    uint16_t opromVersion[32];
    uint16_t hwVersion[32];
    uint16_t firmwareVersion[32];
    uint32_t numPhys;
    uint8_t useConsecutivePhyWWNs;
    uint8_t reserved[3];
} QEMU_PACKED;

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
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

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /* No DMA logic needed for probe; placeholders removed. */
}

static int cmd_data_size(uint32_t cmd)
{
    switch (cmd) {
    case PVSCSI_CMD_SETUP_RINGS:
        return sizeof(struct PVSCSICmdDescSetupRings);
    case PVSCSI_CMD_SETUP_MSG_RING:
        return sizeof(struct PVSCSICmdDescSetupMsgRing);
    case PVSCSI_CMD_RESET_DEVICE:
        return sizeof(struct PVSCSICmdDescResetDevice);
    case PVSCSI_CMD_ABORT_CMD:
        return sizeof(struct PVSCSICmdDescAbortCmd);
    case PVSCSI_CMD_CONFIG:
        return sizeof(struct PVSCSICmdDescConfigCmd);
    case PVSCSI_CMD_SETUP_REQCALLTHRESHOLD:
        return sizeof(struct PVSCSICmdDescSetupReqCall);
    case PVSCSI_CMD_ADAPTER_RESET:
    case PVSCSI_CMD_RESET_BUS:
        return 0;
    default:
        return -1;
    }
}

static void pcibase_execute_command(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    switch (s->command) {
    case PVSCSI_CMD_SETUP_RINGS:
    {
        struct PVSCSICmdDescSetupRings *desc = (struct PVSCSICmdDescSetupRings *)s->cmd_data;
        uint64_t state_pa = desc->ringsStatePPN << 12;
        uint32_t req_entries, cmp_entries;
        struct PVSCSIRingsState rings_state = {0};

        s->rings_state_pa = state_pa;
        s->req_ring_pa = desc->reqRingPPNs[0] << 12;
        s->cmp_ring_pa = desc->cmpRingPPNs[0] << 12;

        req_entries = desc->reqRingNumPages * PVSCSI_MAX_NUM_REQ_ENTRIES_PER_PAGE;
        cmp_entries = desc->cmpRingNumPages * PVSCSI_MAX_NUM_CMP_ENTRIES_PER_PAGE;

        rings_state.reqNumEntriesLog2 = ctz32(req_entries);
        rings_state.cmpNumEntriesLog2 = ctz32(cmp_entries);
        rings_state.msgNumEntriesLog2 = 0;

        pci_dma_write(pdev, state_pa, &rings_state, sizeof(rings_state));

        s->command_status = 0; /* success */
        break;
    }
    case PVSCSI_CMD_SETUP_MSG_RING:
        /* unsupported */
        s->command_status = -1;
        break;
    case PVSCSI_CMD_ADAPTER_RESET:
        s->rings_state_pa = 0;
        s->req_ring_pa = 0;
        s->cmp_ring_pa = 0;
        s->msg_ring_pa = 0;
        s->rings_initialized = false;
        s->command_status = 0;
        break;
    case PVSCSI_CMD_RESET_BUS:
        /* no-op for now */
        s->command_status = 0;
        break;
    case PVSCSI_CMD_RESET_DEVICE:
        /* no-op */
        s->command_status = 0;
        break;
    case PVSCSI_CMD_ABORT_CMD:
        /* not needed for probe */
        s->command_status = -1;
        break;
    case PVSCSI_CMD_CONFIG:
    {
        struct PVSCSICmdDescConfigCmd *desc = (struct PVSCSICmdDescConfigCmd *)s->cmd_data;
        uint64_t cmp_addr = desc->cmpAddr;
        uint64_t config_page_address = desc->configPageAddress;
        uint32_t config_page_num = desc->configPageNum;
        struct PVSCSIConfigPageController config_page = {0};

        if (config_page_num == PVSCSI_CONFIG_PAGE_CONTROLLER &&
            (config_page_address >> 32) == PVSCSI_CONFIG_CONTROLLER_ADDRESS) {
            config_page.header.pageNum = PVSCSI_CONFIG_PAGE_CONTROLLER;
            config_page.header.numDwords = (sizeof(config_page) - sizeof(config_page.header)) / 4;
            config_page.header.hostStatus = BTSTAT_SUCCESS;
            config_page.header.scsiStatus = SDSTAT_GOOD;
            config_page.numPhys = 16;
            pci_dma_write(pdev, cmp_addr, &config_page, sizeof(config_page));
            s->command_status = 0;
        } else {
            s->command_status = -1;
        }
        break;
    }
    case PVSCSI_CMD_SETUP_REQCALLTHRESHOLD:
        /* unsupported for now */
        s->command_status = -1;
        break;
    default:
        s->command_status = -1;
        break;
    }
    s->cmd_active = false;
    s->cmd_data_idx = 0;
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case PVSCSI_REG_OFFSET_INTR_STATUS:
        val = s->intr_status;
        break;
    case PVSCSI_REG_OFFSET_INTR_MASK:
        val = s->intr_mask;
        break;
    case PVSCSI_REG_OFFSET_COMMAND_STATUS:
        val = s->command_status;
        break;
    default:
        /* read returning 0 for other registers */
        val = 0;
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case PVSCSI_REG_OFFSET_COMMAND:
        s->command = val;
        s->cmd_active = true;
        s->cmd_data_idx = 0;
        if (cmd_data_size(val) == 0) {
            pcibase_execute_command(s);
        }
        break;
    case PVSCSI_REG_OFFSET_COMMAND_DATA:
        if (s->cmd_active && s->cmd_data_idx < ARRAY_SIZE(s->cmd_data)) {
            s->cmd_data[s->cmd_data_idx++] = val;
            if (s->cmd_data_idx * sizeof(uint32_t) >= cmd_data_size(s->command)) {
                pcibase_execute_command(s);
            }
        }
        break;
    case PVSCSI_REG_OFFSET_INTR_STATUS:
        s->intr_status &= ~val; /* W1C */
        pcibase_update_irq(s);
        break;
    case PVSCSI_REG_OFFSET_INTR_MASK:
        s->intr_mask = val;
        pcibase_update_irq(s);
        break;
    case PVSCSI_REG_OFFSET_KICK_RW_IO:
    case PVSCSI_REG_OFFSET_KICK_NON_RW_IO:
        /* Ignoring kicks for now; probe proceeds without processing requests */
        break;
    default:
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* No PIO used by driver; return zero */
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* No PIO used */
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

    s->intr_status = 0;
    s->intr_mask = 0;
    s->rings_state_pa = 0;
    s->req_ring_pa = 0;
    s->cmp_ring_pa = 0;
    s->msg_ring_pa = 0;
    s->rings_initialized = false;
    s->command = 0;
    s->command_status = 0;
    s->cmd_active = false;
    s->cmd_data_idx = 0;
    pcibase_update_irq(s);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x15AD );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x07C0 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0100 );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x8000; /* 8 pages * PAGE_SIZE (4096) */
    s->bar_info[0].name = "pvscsi-mmio";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI-X initialization; offsets for Table and PBA unknown, use defaults */
    /* 1 MSI-X vector, table offset 0, PBA offset 0x800 */
    msix_init(pdev, 1,
              &s->bar_regions[0], 0, 0x800,
              &s->bar_regions[0], 0, 0x1000,
              0x80, errp);

    /* DMA config not needed */
    /* Timer config not needed */
    /* Final state initialization */
    s->rev = 0x01;
    s->intr_status = 0;
    s->intr_mask = 0;
    s->rings_state_pa = 0;
    s->req_ring_pa = 0;
    s->cmp_ring_pa = 0;
    s->msg_ring_pa = 0;
    s->rings_initialized = false;
    s->command = 0;
    s->command_status = 0;
    s->cmd_active = false;
    s->cmd_data_idx = 0;
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }

    /* Nothing else to free */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "vmw_pvscsi_pci",
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
