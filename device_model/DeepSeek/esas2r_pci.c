/*
 * QEMU device model for ATTO SAS2 RAID controller (esas2r)
 * Based on driver source and register definitions.
 * This file is auto-generated for Phase 2 implementation.
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
/* No additional includes needed */

#define TYPE_PCIBASE_DEVICE "esas2r_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
/* Driver-specific definitions */
#define ATTO_VENDOR_ID 0x117C
#define DEVICE_ID_ESAS2R 0x0049
#define PCI_CLASS_ESAS2R 0x0100  /* Mass storage: SCSI */

/* Register offsets */
#define MW_DATA_WINDOW_SIZE 0x00020000
#define MU_INT_STATUS_OUT       0x00010200
#define MU_INTSTAT_POST_OUT     0x00000010
#define MU_OUT_LIST_INT_STAT    0x00004088
#define MU_INTSTAT_DRBL         0x00001000
#define MU_DOORBELL_OUT         0x00010480
#define MU_DOORBELL_IN          0x00010460
#define MU_DOORBELL_OUT_ENB     0x00010484
#define MU_DOORBELL_IN_ENB      0x00010464
#define MU_INT_MASK_OUT         0x0001020C
#define MU_INTSTAT_MASK         (0x00001010)
#define MU_IN_LIST_CONFIG       0x0000402C
#define MU_OUT_LIST_CONFIG      0x0000407C
#define MU_IN_LIST_ADDR_LO      0x00004000
#define MU_IN_LIST_ADDR_HI      0x00004004
#define MU_OUT_LIST_ADDR_LO     0x00004050
#define MU_OUT_LIST_ADDR_HI     0x00004054
#define MU_OLC_WRT_PTR          0x00003FFF
#define MU_OLC_TOGGLE           0x00004000
#define MU_OLIS_INT             0x00000001
#define MU_OLIS_MASK            0x00000001
#define DRBL_RESET_BUS          0x00000002
#define DRBL_FORCE_INT          0x00000080
#define DRBL_FLASH_REQ          0x00000020
#define DRBL_FLASH_DONE         0x00000040
#define DRBL_POWER_DOWN         0x00000200
#define DRBL_MSG_IFC_INIT       0x00000100
#define DRBL_FW_RESET           0x00080000

/* Newly defined from driver iteration */
#define MVR_PCI_WIN1_REMAP      (0x00008438)
#define MVRPW1R_ENABLE          (0x00000001)
#define ESAS2R_INT_DIS_MASK     0
#define ESAS2R_INT_ENB_MASK     MU_INTSTAT_MASK

/* New registers from supplementary source */
#define MU_IN_LIST_WRITE        (0x00004018)
#define MU_ILW_TOGGLE           (0x00004000)

/* IRQ mask values used by driver */

/* Placeholder for missing register offsets: none now, replaced by correct defines above. */

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
    uint32_t intr_status;   /* MU_INT_STATUS_OUT shadow */
    uint32_t intr_mask;     /* MU_INT_MASK_OUT shadow */
    uint32_t out_list_int_stat; /* MU_OUT_LIST_INT_STAT */
    uint32_t doorbell_out;  /* MU_DOORBELL_OUT */
    uint32_t doorbell_in;   /* MU_DOORBELL_IN */
    uint32_t doorbell_out_enb; /* MU_DOORBELL_OUT_ENB */
    uint32_t doorbell_in_enb;  /* MU_DOORBELL_IN_ENB */

    /* Inbound/Outbound queue settings */
    uint64_t in_list_addr;
    uint64_t out_list_addr;
    uint32_t in_list_config;
    uint32_t out_list_config;
    uint32_t in_list_write;     /* MU_IN_LIST_WRITE (write pointer + toggle) */

    /* Data window remap */
    uint32_t data_window_remap; /* MVR_PCI_WIN1_REMAP */
    /* Device memory buffer (e.g., NVRAM, firmware) */
    uint8_t dev_mem[256 * 1024]; /* 256KB */

    struct {
        dma_addr_t src;
        dma_addr_t dst;
        dma_addr_t cnt;
        uint32_t cmd;
    } dma;

    uint32_t status;
    uint8_t reset_state;
    uint8_t pm_state;
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t active = s->intr_status & s->intr_mask;
    if (active) {
        if (msix_enabled(pdev)) {
            msix_notify(pdev, 0);
        } else if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (msix_enabled(pdev) || msi_enabled(pdev)) {
            /* No deassert for MSI/MSI-X */
        } else {
            pci_set_irq(pdev, 0);
        }
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case MU_INT_STATUS_OUT:
        val = s->intr_status;
        break;
    case MU_OUT_LIST_INT_STAT:
        val = s->out_list_int_stat;
        break;
    case MU_DOORBELL_IN:
        val = s->doorbell_in;
        break;
    case MU_DOORBELL_IN_ENB:
        val = s->doorbell_in_enb;
        break;
    case MU_INT_MASK_OUT:
        val = s->intr_mask;
        break;
    case MU_DOORBELL_OUT:
        /* Read-clear semantics, as driver reads then processes */
        val = s->doorbell_out;
        s->doorbell_out = 0;
        break;
    case MU_DOORBELL_OUT_ENB:
        val = s->doorbell_out_enb;
        break;
    case MU_IN_LIST_CONFIG:
        val = s->in_list_config;
        break;
    case MU_OUT_LIST_CONFIG:
        val = s->out_list_config;
        break;
    case MU_IN_LIST_ADDR_LO:
        val = s->in_list_addr & 0xFFFFFFFF;
        break;
    case MU_IN_LIST_ADDR_HI:
        val = (s->in_list_addr >> 32) & 0xFFFFFFFF;
        break;
    case MU_OUT_LIST_ADDR_LO:
        val = s->out_list_addr & 0xFFFFFFFF;
        break;
    case MU_OUT_LIST_ADDR_HI:
        val = (s->out_list_addr >> 32) & 0xFFFFFFFF;
        break;
    case MU_IN_LIST_WRITE:
        val = s->in_list_write;
        break;
    case MVR_PCI_WIN1_REMAP:
        val = s->data_window_remap;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "esas2r: unimplemented MMIO read at 0x%" HWADDR_PRIx "\n", addr);
        val = 0;
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case MU_INT_STATUS_OUT:
        /* W1C: write 1 to clear */
        s->intr_status &= ~(uint32_t)val;
        pcibase_update_irq(s);
        break;
    case MU_INT_MASK_OUT:
        s->intr_mask = val;
        pcibase_update_irq(s);
        break;
    case MU_DOORBELL_OUT:
        /* Doorbell bits are latched */
        s->doorbell_out |= (uint32_t)val;
        if (val & DRBL_FORCE_INT) {
            s->intr_status |= MU_INTSTAT_DRBL;
            pcibase_update_irq(s);
        }
        /* Other doorbell bits (reset, init) are ignored for now */
        break;
    case MU_DOORBELL_OUT_ENB:
        s->doorbell_out_enb = val;
        break;
    case MU_DOORBELL_IN_ENB:
        s->doorbell_in_enb = val;
        break;
    case MU_OUT_LIST_INT_STAT:
        /* W1C: write 1 to clear */
        s->out_list_int_stat &= ~(uint32_t)val;
        break;
    case MU_IN_LIST_CONFIG:
        s->in_list_config = val;
        break;
    case MU_OUT_LIST_CONFIG:
        s->out_list_config = val;
        break;
    case MU_IN_LIST_ADDR_LO:
        s->in_list_addr = (s->in_list_addr & 0xFFFFFFFF00000000ULL) | val;
        break;
    case MU_IN_LIST_ADDR_HI:
        s->in_list_addr = (s->in_list_addr & 0xFFFFFFFF) | ((uint64_t)val << 32);
        break;
    case MU_OUT_LIST_ADDR_LO:
        s->out_list_addr = (s->out_list_addr & 0xFFFFFFFF00000000ULL) | val;
        break;
    case MU_OUT_LIST_ADDR_HI:
        s->out_list_addr = (s->out_list_addr & 0xFFFFFFFF) | ((uint64_t)val << 32);
        break;
    case MU_IN_LIST_WRITE:
        s->in_list_write = val;
        break;
    case MVR_PCI_WIN1_REMAP:
        s->data_window_remap = val;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "esas2r: unimplemented MMIO write at 0x%" HWADDR_PRIx " value 0x%" PRIx64 "\n", addr, val);
        break;
    }
}

static uint64_t pcibase_data_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Check if window is enabled */
    if (!(s->data_window_remap & MVRPW1R_ENABLE)) {
        return 0;
    }

    uint32_t base = s->data_window_remap & ~(MW_DATA_WINDOW_SIZE - 1);
    uint32_t offset = base + (uint32_t)addr;
    if (offset < sizeof(s->dev_mem)) {
        /* Allow byte-sized reads */
        val = s->dev_mem[offset];
    }
    return val;
}

static void pcibase_data_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Data window is read-only for simplicity; writes ignored */
    PCIBaseState *s = opaque;
    if (s->data_window_remap & MVRPW1R_ENABLE) {
        /* Ignore writes for now, but could be implemented later */
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static const MemoryRegionOps pcibase_data_mmio_ops = {
    .read = pcibase_data_mmio_read,
    .write = pcibase_data_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    s->intr_status = 0;
    s->intr_mask = 0;
    s->out_list_int_stat = 0;
    s->doorbell_out = 0;
    s->doorbell_in = 0;
    s->doorbell_out_enb = 0;
    s->doorbell_in_enb = 0;
    s->in_list_addr = 0;
    s->out_list_addr = 0;
    s->in_list_config = 0;
    s->out_list_config = 0;
    s->in_list_write = 0;
    s->data_window_remap = 0;
    /* Initialize device memory with a minimal NVRAM structure */
    memset(s->dev_mem, 0, sizeof(s->dev_mem));
    /* NVRAM signature and defaults to satisfy driver init */
    /* Placeholder: real NVRAM layout will be filled once struct is known */
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        if (bi->index == 0) {
            memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, aligned_size);
        } else if (bi->index == 1) {
            memory_region_init_io(mr, OBJECT(s), &pcibase_data_mmio_ops, s, bi->name, aligned_size);
        } else {
            memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, aligned_size);
        }
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
    Error *local_err = NULL;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x117C );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x0049 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0100 );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);

    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos < 0) {
        /* Error already set by pci_add_capability */
        return;
    }
    pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);

    /* BAR Initialization */
    s->num_bars = 3;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = 0x20000, .name = "esas2r-mmio" };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_MMIO, .size = 0x20000, .name = "esas2r-data" };
    s->bar_info[2] = (BARInfo){ .index = 2, .type = BAR_TYPE_MMIO, .size = 0x1000, .name = "esas2r-msix" };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    if (msix_init(pdev, 2, &s->bar_regions[2], 2, 0, &s->bar_regions[2], 2, 0x1000, 0, &local_err)) {
        error_propagate(errp, local_err);
        return;
    }

    /* Initialize device memory with fake NVRAM etc. */
    memset(s->dev_mem, 0, sizeof(s->dev_mem));
    /* NVRAM default values to satisfy probe; will be refined when struct is known */
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, &s->bar_regions[2], &s->bar_regions[2]);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "esas2r_pci",
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
