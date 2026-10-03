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

#define TYPE_PCIBASE_DEVICE "pds_core_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/*
 * Device identification (extracted from driver pci_device_id table)
 */
#define PDS_CORE_VENDOR_ID 0x1dd8
#define PDS_CORE_DEVICE_ID 0x100c

/*
 * BAR sizes (actual hardware layout)
 */
#define PDS_CORE_BAR0_SIZE   0x8000
#define PDS_CORE_BAR1_SIZE   0x200000

/*
 * BAR0 register layout offsets
 */
#define PDS_CORE_BAR0_DEV_INFO_REGS_OFFSET  0x0000
#define PDS_CORE_BAR0_DEV_CMD_REGS_OFFSET   0x0100
#define PDS_CORE_BAR0_INTR_STATUS_OFFSET    0x0200
#define PDS_CORE_BAR0_INTR_CTRL_OFFSET      0x0204

/* Device command submission and completion offsets (deduced from driver behavior) */
#define PDS_CORE_BAR0_DEVCMD_STATUS_OFFSET 0x0100
#define PDS_CORE_BAR0_DEVCMD_CMD_OFFSET    0x0104
#define PDS_CORE_BAR0_DEVCMD_DATA_OFFSET   0x0108
#define PDS_CORE_BAR0_DEVCMD_COMP_OFFSET   0x0110
#define PDS_CORE_DEVCMD_COMP_SIZE          16

/* Signature value expected by driver */
#define PDS_CORE_DEV_INFO_SIGNATURE  0x44455649

/* Firmware status value to indicate running (1 = running) */
#define PDS_CORE_FW_STATUS_RUNNING   1

/* Command completion status: 0 = success */
#define PDS_CORE_DEVCMD_STATUS_SUCCESS 0

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

/* Supplementary union for completion layout (minimal representation) */
union pds_core_dev_comp {
    uint8_t status;
    uint8_t bytes[16];
    struct {
        uint8_t status;
        uint8_t ver;
    } identify;
    /* other completion types can be added here */
};

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

    /* Core device registers */
    uint32_t info_signature;
    uint32_t fw_status;               /* firmware status register at offset 0x0010 */
    uint32_t devcmd;                  /* write-only command register at 0x0104 */
    union pds_core_dev_comp devcmd_comp; /* completion at 0x0110 */
    uint32_t cmd_regs[64];            /* remaining command regs */

    /* Operational state */
    uint32_t status;
    bool reset_active;
    uint8_t pm_state;
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    if (s->intr_status & s->intr_mask) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* MMIO read handler for BAR0 */
static uint64_t pcibase_bar0_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Information registers (offset 0x0000..0x00FF) */
    if (addr >= PDS_CORE_BAR0_DEV_INFO_REGS_OFFSET &&
        addr < PDS_CORE_BAR0_DEV_INFO_REGS_OFFSET + 0x100) {
        switch (addr) {
        case PDS_CORE_BAR0_DEV_INFO_REGS_OFFSET:  /* signature */
            val = s->info_signature;
            break;
        case PDS_CORE_BAR0_DEV_INFO_REGS_OFFSET + 0x10: /* firmware status */
            val = s->fw_status;
            break;
        default:
            val = 0;
            break;
        }
        return val;
    }

    /* Command registers (offset 0x0100..0x01FF) */
    if (addr >= 0x0100 && addr < 0x0200) {
        if (addr == PDS_CORE_BAR0_DEVCMD_STATUS_OFFSET) {
            val = s->cmd_regs[0];
        } else if (addr == PDS_CORE_BAR0_DEVCMD_CMD_OFFSET) {
            val = s->cmd_regs[1];
        } else if (addr >= PDS_CORE_BAR0_DEVCMD_COMP_OFFSET &&
                   addr < PDS_CORE_BAR0_DEVCMD_COMP_OFFSET + PDS_CORE_DEVCMD_COMP_SIZE) {
            unsigned offset = addr - PDS_CORE_BAR0_DEVCMD_COMP_OFFSET;
            val = s->devcmd_comp.bytes[offset];
        } else {
            /* Generic command reg, including data area */
            unsigned offset = (addr - 0x0100) / 4;
            if (offset < ARRAY_SIZE(s->cmd_regs)) {
                val = s->cmd_regs[offset];
            }
        }
        return val;
    }

    /* Interrupt status/control */
    if (addr == PDS_CORE_BAR0_INTR_STATUS_OFFSET) {
        return s->intr_status;
    }
    if (addr == PDS_CORE_BAR0_INTR_CTRL_OFFSET) {
        return s->intr_mask;
    }

    /* Fallback: unhandled regions */
    qemu_log_mask(LOG_UNIMP, "pds_core: unimplemented BAR0 MMIO read at 0x%"HWADDR_PRIx"\n", addr);
    return 0;
}

static void pcibase_bar0_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* Information registers: read-only in real HW; ignore writes */
    if (addr >= PDS_CORE_BAR0_DEV_INFO_REGS_OFFSET &&
        addr < PDS_CORE_BAR0_DEV_INFO_REGS_OFFSET + 0x100) {
        return;
    }

    /* Command registers */
    if (addr >= 0x0100 && addr < 0x0200) {
        if (addr == PDS_CORE_BAR0_DEVCMD_STATUS_OFFSET) {
            /* Status register, typically read-only, ignore writes */
            return;
        } else if (addr == PDS_CORE_BAR0_DEVCMD_CMD_OFFSET) {
            /* Command submission */
            s->devcmd = val;
            if (val == 1) {  /* PDS_CORE_CMD_IDENTIFY */
                s->devcmd_comp.identify.status = PDS_CORE_DEVCMD_STATUS_SUCCESS;
                s->devcmd_comp.identify.ver = 1;
                /* Fill data area with mock device identity */
                s->cmd_regs[2] = 0x44455649; /* signature */
                s->cmd_regs[3] = 0x302e312e; /* "1.0." */
                s->cmd_regs[4] = 0x00000030; /* "0\0\0\0" */
            } else {
                memset(&s->devcmd_comp, 0, sizeof(s->devcmd_comp));
            }
            return;
        } else if (addr >= PDS_CORE_BAR0_DEVCMD_COMP_OFFSET &&
                   addr < PDS_CORE_BAR0_DEVCMD_COMP_OFFSET + PDS_CORE_DEVCMD_COMP_SIZE) {
            /* Ignore writes to completion area */
            return;
        } else {
            /* Other command regs, including data area */
            unsigned offset = (addr - 0x0100) / 4;
            if (offset < ARRAY_SIZE(s->cmd_regs)) {
                s->cmd_regs[offset] = val;
            }
            return;
        }
    }

    /* Interrupt status (W1C) */
    if (addr == PDS_CORE_BAR0_INTR_STATUS_OFFSET) {
        s->intr_status &= ~val;
        pcibase_update_irq(s);
        return;
    }

    /* Interrupt mask */
    if (addr == PDS_CORE_BAR0_INTR_CTRL_OFFSET) {
        s->intr_mask = val;
        pcibase_update_irq(s);
        return;
    }

    /* Fallback */
    qemu_log_mask(LOG_UNIMP, "pds_core: unimplemented BAR0 MMIO write at 0x%"HWADDR_PRIx" val 0x%"PRIx64"\n", addr, val);
}

/* MMIO read handler for BAR1: emulates firmware status */
static uint64_t pcibase_bar1_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* Return 1 to indicate firmware is running. */
    return 1;
}

static void pcibase_bar1_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Ignore writes to BAR1 for now */
}

/* PIO handlers (not used) */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static const MemoryRegionOps pcibase_bar0_mmio_ops = {
    .read = pcibase_bar0_mmio_read,
    .write = pcibase_bar0_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static const MemoryRegionOps pcibase_bar1_mmio_ops = {
    .read = pcibase_bar1_mmio_read,
    .write = pcibase_bar1_mmio_write,
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

    s->info_signature = PDS_CORE_DEV_INFO_SIGNATURE;
    s->fw_status = PDS_CORE_FW_STATUS_RUNNING;
    memset(s->cmd_regs, 0, sizeof(s->cmd_regs));
    s->cmd_regs[0] = 1;  /* devcmd status: FW running */
    s->devcmd = 0;
    memset(&s->devcmd_comp, 0, sizeof(s->devcmd_comp));
    s->intr_status = 0;
    s->intr_mask = 0;
    s->status = 0;
    s->reset_active = false;
    s->pm_state = 0;
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi,
                                 const MemoryRegionOps *mmio_ops_for_bar, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), mmio_ops_for_bar, s, bi->name, aligned_size);
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PDS_CORE_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PDS_CORE_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0200);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    s->num_bars = 2;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = PDS_CORE_BAR0_SIZE;
    s->bar_info[0].name = "bar0";

    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = PDS_CORE_BAR1_SIZE;
    s->bar_info[1].name = "bar1";

    for (int i = 2; i < 6; i++) {
        s->bar_info[i].type = BAR_TYPE_NONE;
    }

    for (int i = 0; i < s->num_bars; i++) {
        const MemoryRegionOps *mmio_ops_to_use = NULL;
        if (s->bar_info[i].type == BAR_TYPE_MMIO) {
            mmio_ops_to_use = (i == 0) ? &pcibase_bar0_mmio_ops : &pcibase_bar1_mmio_ops;
        }
        pcibase_register_bar(pdev, s, &s->bar_info[i], mmio_ops_to_use, errp);
    }

    /* Initialize register defaults (reset will do this again) */
    s->info_signature = PDS_CORE_DEV_INFO_SIGNATURE;
    s->fw_status = PDS_CORE_FW_STATUS_RUNNING;
    s->intr_status = 0;
    s->intr_mask = 0;
    memset(&s->devcmd_comp, 0, sizeof(s->devcmd_comp));
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

static const VMStateDescription vmstate_pcibase = {
    .name = "pds_core_pci",
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
