/*
 * QEMU PCI device model for QLogic FastLinQ QL4xxxx Ethernet (qede driver)
 * Based on driver analysis and provided mailbox structure.
 * Phase 4: Runtime fixes for probe failure.
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
#include "hw/pci/pci_ids.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-core.h"
#include "migration/vmstate.h"

#define TYPE_PCIBASE_DEVICE "qede_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI Identification */
#define PCI_VENDOR_ID_QLOGIC 0x1077
#define DEVICE_ID 0x1634
#define MSIX_NUM_VECTORS 65

/* Register Offsets */
#define IGU_REG_LEADING_EDGE_LATCH         0x130134
#define IGU_REG_TRAILING_EDGE_LATCH        0x130104
#define NIG_REG_RX_LLH_BRB_GATE_DNTFWD_PERPF 0x5011f4UL
#define PRS_REG_SEARCH_TCP                 0x1f0400UL
#define PRS_REG_SEARCH_UDP                 0x1f0404UL
#define PRS_REG_SEARCH_FCOE                0x1f0408UL
#define PRS_REG_SEARCH_ROCE                0x1f040cUL
#define PRS_REG_SEARCH_OPENFLOW            0x1f0434UL
#define DORQ_REG_PF_DB_ENABLE              0x100508UL
#define QM_REG_PF_EN                       0x16e70c
#define PRS_REG_TAG_ETHERTYPE_0_RT_OFFSET  5928
#define NIG_REG_TAG_ETHERTYPE_0_RT_OFFSET  34788
#define PBF_REG_TAG_ETHERTYPE_0_RT_OFFSET  34921
#define DORQ_REG_TAG1_ETHERTYPE_RT_OFFSET  18

/* Supplementary register: MCP scratchpad address */
#define MISC_REG_SHARED_MEM_ADDR 0xa2b4
#define MISCS_REG_GENERIC_POR_0   0x0096d4UL
#define MISC_REG_GEN_PURP_CR0     0x008c80UL

/* Shared memory base offset within BAR0, now MCP_REG_SCRATCH */
#define MCP_REG_SCRATCH   0xe20000UL
#define SHARED_MEM_BASE   MCP_REG_SCRATCH
#define SHARED_MEM_SIZE   0x1000  /* 4 KB */

/* Mailbox offsets within shared memory, based on struct public_drv_mb */
#define MAILBOX_OFFSET 0

/* Mailbox definitions from supplementary source */
#define DRV_MSG_SEQ_NUMBER_MASK      0x0000ffff
#define DRV_MSG_SEQ_NUMBER_OFFSET    0
#define DRV_MSG_CODE_MASK            0xffff0000
#define DRV_MSG_CODE_OFFSET          16

#define FW_MSG_SEQ_NUMBER_MASK       0x0000ffff
#define FW_MSG_SEQ_NUMBER_OFFSET     0
#define FW_MSG_CODE_MASK             0xffff0000
#define FW_MSG_CODE_OFFSET           16

#define DRV_PULSE_SEQ_MASK           0x00007fff
#define DRV_PULSE_SYSTEM_TIME_MASK   0xffff0000
#define DRV_PULSE_ALWAYS_ALIVE       0x00008000

#define MCP_PULSE_SEQ_MASK           0x00007fff
#define MCP_PULSE_ALWAYS_ALIVE       0x00008000
#define MCP_EVENT_MASK               0xffff0000
#define MCP_EVENT_OTHER_DRIVER_RESET_REQ 0x00010000

/* IGU CAM: approximate base and size */
#define IGU_CAM_BASE 0x130000
#define IGU_CAM_SIZE 0x1000
#define IGU_CAM_NUM_ENTRIES 128

/* IGU block status bits */
#define QED_IGU_STATUS_FREE     0x01
#define QED_IGU_STATUS_VALID    0x02
#define QED_IGU_STATUS_PF       0x04
#define QED_IGU_STATUS_DSB      0x08
#define QED_SB_INVALID_IDX      0xffff

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

    uint8_t shared_mem[SHARED_MEM_SIZE];
    uint8_t igu_cam[IGU_CAM_SIZE];
};

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Handle MCP scratchpad address register */
    if (addr == MISC_REG_SHARED_MEM_ADDR) {
        return MCP_REG_SCRATCH;
    }

    /* Handle MISCS_REG_GENERIC_POR_0 */
    if (addr == MISCS_REG_GENERIC_POR_0) {
        return 0;
    }

    /* Handle IGU CAM region */
    if (addr >= IGU_CAM_BASE && addr < IGU_CAM_BASE + IGU_CAM_SIZE) {
        hwaddr offset = addr - IGU_CAM_BASE;
        if (offset + size > IGU_CAM_SIZE) {
            return 0;
        }
        switch (size) {
        case 1:
            return s->igu_cam[offset];
        case 2:
            return lduw_le_p(&s->igu_cam[offset]);
        case 4:
            return ldl_le_p(&s->igu_cam[offset]);
        case 8:
            return ldq_le_p(&s->igu_cam[offset]);
        default:
            return 0;
        }
    }

    /* Handle shared memory region (MCP mailbox etc.) */
    if (addr >= SHARED_MEM_BASE && addr < SHARED_MEM_BASE + SHARED_MEM_SIZE) {
        hwaddr offset = addr - SHARED_MEM_BASE;
        if (size == 1) {
            val = s->shared_mem[offset];
        } else if (size == 2) {
            val = lduw_le_p(&s->shared_mem[offset]);
        } else if (size == 4) {
            val = ldl_le_p(&s->shared_mem[offset]);
        } else if (size == 8) {
            val = ldq_le_p(&s->shared_mem[offset]);
        }
        return val;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* MISC_REG_SHARED_MEM_ADDR is read-only to the driver; firmware sets it */
    if (addr == MISC_REG_SHARED_MEM_ADDR) {
        return;
    }

    /* Handle IGU CAM region writes */
    if (addr >= IGU_CAM_BASE && addr < IGU_CAM_BASE + IGU_CAM_SIZE) {
        hwaddr offset = addr - IGU_CAM_BASE;
        if (offset + size > IGU_CAM_SIZE) {
            return;
        }
        switch (size) {
        case 1:
            s->igu_cam[offset] = (uint8_t)val;
            break;
        case 2:
            stw_le_p(&s->igu_cam[offset], (uint16_t)val);
            break;
        case 4:
            stl_le_p(&s->igu_cam[offset], (uint32_t)val);
            break;
        case 8:
            stq_le_p(&s->igu_cam[offset], val);
            break;
        }
        return;
    }

    /* Handle shared memory region writes */
    if (addr >= SHARED_MEM_BASE && addr < SHARED_MEM_BASE + SHARED_MEM_SIZE) {
        hwaddr offset = addr - SHARED_MEM_BASE;
        if (size == 1) {
            s->shared_mem[offset] = (uint8_t)val;
        } else if (size == 2) {
            stw_le_p(&s->shared_mem[offset], (uint16_t)val);
        } else if (size == 4) {
            stl_le_p(&s->shared_mem[offset], (uint32_t)val);
        } else if (size == 8) {
            stq_le_p(&s->shared_mem[offset], val);
        }

        /* Mailbox handshake: when driver writes drv_mb_header, acknowledge */
        if (offset == (MAILBOX_OFFSET + 0)) { /* drv_mb_header at offset 0 */
            uint32_t drv_header = ldl_le_p(&s->shared_mem[MAILBOX_OFFSET + 0]);
            uint32_t seq = (drv_header & DRV_MSG_SEQ_NUMBER_MASK) >> DRV_MSG_SEQ_NUMBER_OFFSET;
            /* Respond with same sequence and code 1 (success) */
            uint32_t fw_response = (seq << FW_MSG_SEQ_NUMBER_OFFSET) & FW_MSG_SEQ_NUMBER_MASK;
            fw_response |= 0x00010000; /* code 1 */
            stl_le_p(&s->shared_mem[MAILBOX_OFFSET + 8], fw_response);  /* fw_mb_header at offset 8 */
            stl_le_p(&s->shared_mem[MAILBOX_OFFSET + 12], 0);           /* fw_mb_param at offset 12 */
            stl_le_p(&s->shared_mem[MAILBOX_OFFSET + 20], MCP_PULSE_ALWAYS_ALIVE); /* mcp_pulse_mb at offset 20 */
        }
        return;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;
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
    (void)s;
    pci_device_reset(PCI_DEVICE(dev));
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

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;
    Error *local_err = NULL;
    int i;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_QLOGIC);
    pci_set_word(pci_conf + PCI_DEVICE_ID, DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_ETHERNET);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, &local_err);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* Define BAR layout */
    s->num_bars = 0;

    /* BAR0: Control/Status Registers (MMIO), now 16 MiB to cover MCP_REG_SCRATCH */
    s->bar_info[s->num_bars].index = 0;
    s->bar_info[s->num_bars].type = BAR_TYPE_MMIO;
    s->bar_info[s->num_bars].size = 16 * MiB;
    s->bar_info[s->num_bars].name = "qede-mmio";
    s->num_bars++;

    /* BAR2: Doorbell (MMIO) */
    s->bar_info[s->num_bars].index = 2;
    s->bar_info[s->num_bars].type = BAR_TYPE_MMIO;
    s->bar_info[s->num_bars].size = 4 * KiB;
    s->bar_info[s->num_bars].name = "qede-doorbell";
    s->num_bars++;

    for (i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            return;
        }
    }

    /* Initialize shared memory with mailbox readiness */
    memset(s->shared_mem, 0, SHARED_MEM_SIZE);
    /* Pre-set fw_mb_header to indicate MCP is ready (code 1, seq 0) */
    stl_le_p(&s->shared_mem[MAILBOX_OFFSET + 8], 0x00010000);  /* fw_mb_header */
    /* Set mcp_pulse_mb with always alive bit */
    stl_le_p(&s->shared_mem[MAILBOX_OFFSET + 20], MCP_PULSE_ALWAYS_ALIVE);
    /* Set sup_msgs in MFW mailbox to indicate MFW is initialized (guess offset 0x100) */
    stl_le_p(&s->shared_mem[0x100], 0xFFFFFFFF);

    /* Initialize IGU CAM */
    memset(s->igu_cam, 0, IGU_CAM_SIZE);
    /* Set all entries to invalid: status=FREE, igu_sb_id=INVALID_IDX */
    for (i = 0; i < IGU_CAM_NUM_ENTRIES; i++) {
        uint32_t word = (QED_SB_INVALID_IDX << 16) | QED_IGU_STATUS_FREE;
        stl_le_p(&s->igu_cam[i * 8], word);
    }
    /* Set entry 0 as valid PF SB: status=VALID|PF, igu_sb_id=0 */
    stl_le_p(&s->igu_cam[0], (0 << 16) | (QED_IGU_STATUS_VALID | QED_IGU_STATUS_PF));

    /* MSI-X initialization */
    s->has_msi = false;
    s->has_msix = true;
    if (msix_init_exclusive_bar(pdev, MSIX_NUM_VECTORS, 4, &local_err)) {
        error_propagate(errp, local_err);
        return;
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    (void)s;
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

static const VMStateDescription vmstate_pcibase = {
    .name = "qede_pci",
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

type_init(pcibase_register_types)
