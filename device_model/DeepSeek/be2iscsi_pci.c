/*
 * QEMU PCI device model for Emulex 10GbE iSCSI (be2iscsi)
 * Auto-generated from driver analysis.
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

#define TYPE_PCIBASE_DEVICE "be2iscsi_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI Vendor/Device IDs (first entry from beiscsi_pci_id_table) */
#define VENDOR_ID 0x19A2
#define DEVICE_ID 0x212
#define CLASS_ID PCI_CLASS_STORAGE_SCSI /* Emulex 10Gbe iSCSI */

/* Registers offsets (from driver defines) */
#define CEV_ISR0_OFFSET                 0xC18
#define DB_EQ_OFFSET                    0x120   /* Doorbell: EQ */
#define DB_CQ_OFFSET                    0x120
#define DB_RXULP0_OFFSET                0xA0
#define DB_TXULP0_OFFSET                0x40
#define DB_MCCQ_OFFSET                  0x140
#define PCICFG_MEMBAR_CTRL_INT_CTRL_OFFSET 0xfc
#define MPU_MAILBOX_DB_OFFSET           0x160
#define SLIPORT_SEMAPHORE_OFFSET_SH     0x94

/* Interrupt status/mask bits (from MEMBAR_CTRL_INT_CTRL) */
#define MEMBAR_CTRL_INT_CTRL_HOSTINTR_MASK (1 << 29)

/* Doorbell EQ bit shifts (from driver: hwi_ring_eq_db) */
#define DB_EQ_REARM_SHIFT  (29)
#define DB_EQ_CLR_SHIFT    (9)
#define DB_EQ_EVNT_SHIFT   (10)

/* CEV ISR size per function (from driver) */
#define CEV_ISR_SIZE 4

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
    uint32_t intr_status;   /* host interrupt status (not used yet) */
    uint32_t intr_mask;     /* host interrupt mask (not used yet) */

    /* Hardware Register Shadows */
    struct {
        uint32_t revision;
        uint32_t control;
        uint32_t isr;           /* at 0xC18 */
    } reg;

    /* PCICFG control register shadow */
    uint32_t pcicfg_ctrl;

    /* PCI function number (for ISR offset) */
    uint8_t func;

    uint32_t state;             /* Operational status flags */
    bool reset_in_progress;

    /* MSI-X specific */
    MemoryRegion msix_mr;
};

#define BEISCSI_HBA_ONLINE   0
#define BEISCSI_HBA_LINK_UP  1

/* IRQ update logic */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t enabled = s->pcicfg_ctrl & MEMBAR_CTRL_INT_CTRL_HOSTINTR_MASK;

    if (enabled && s->reg.isr) {
        if (msix_enabled(pdev)) {
            msix_notify(pdev, 0);  /* assume vector 0 */
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

/* MMIO handlers per BAR */
static uint64_t bar0_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr == PCICFG_MEMBAR_CTRL_INT_CTRL_OFFSET) {
        return s->pcicfg_ctrl;
    }
    return 0;
}

static void bar0_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr == PCICFG_MEMBAR_CTRL_INT_CTRL_OFFSET) {
        s->pcicfg_ctrl = val;
        pcibase_update_irq(s);
    }
}

static const MemoryRegionOps bar0_mmio_ops = {
    .read = bar0_mmio_read,
    .write = bar0_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static uint64_t bar2_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    /* ISR offset per function */
    if (addr == CEV_ISR0_OFFSET + s->func * CEV_ISR_SIZE) {
        return s->reg.isr;
    }
    return 0;
}

static void bar2_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* CSR region is read-only in driver usage */
}

static const MemoryRegionOps bar2_mmio_ops = {
    .read = bar2_mmio_read,
    .write = bar2_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static uint64_t bar4_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* DB bar not read */
    return 0;
}

static void bar4_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    switch (addr) {
    case DB_EQ_OFFSET:
        /* Doorbell for EQ ring: clear interrupt if CLR bit set */
        if (val & (1 << DB_EQ_CLR_SHIFT)) {
            s->reg.isr = 0;
            pcibase_update_irq(s);
        }
        break;
    default:
        /* other doorbells ignored */
        break;
    }
}

static const MemoryRegionOps bar4_mmio_ops = {
    .read = bar4_mmio_read,
    .write = bar4_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

/* These unused handlers are removed */

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    s->reg.isr = 0;
    s->pcicfg_ctrl = 0;
    s->func = 0;
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }
    
    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        const MemoryRegionOps *ops;
        switch (bi->index) {
        case 0: ops = &bar0_mmio_ops; break;
        case 2: ops = &bar2_mmio_ops; break;
        case 4: ops = &bar4_mmio_ops; break;
        default: ops = &bar0_mmio_ops; break;
        }
        memory_region_init_io(mr, OBJECT(s), ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        /* not used */
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
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

    /* Setup BARs: 0 (PCICFG), 2 (CSR), 4 (DB) */
    s->num_bars = 3;
    s->bar_info[0] = (BARInfo) { .index = 0, .type = BAR_TYPE_MMIO, .size = 4096, .name = "pcicfg" };
    s->bar_info[1] = (BARInfo) { .index = 2, .type = BAR_TYPE_MMIO, .size = 4096, .name = "csr" };
    s->bar_info[2] = (BARInfo) { .index = 4, .type = BAR_TYPE_MMIO, .size = 128 * 1024, .name = "db" };

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Add MSI-X BAR (BAR1) */
    memory_region_init(&s->msix_mr, OBJECT(s), "be2iscsi-msix", 8192);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->msix_mr);

    /* Interrupt setup: support MSI-X and MSI */
    if (msix_init(pdev, 16, &s->msix_mr, 1, 0, &s->msix_mr, 1, 0x1000, 0, errp) == 0) {
        s->has_msix = true;
    } else {
        s->has_msix = false;
        if (msi_init(pdev, 0, 1, true, false, errp) != 0) {
            /* fallback to INTx handled by pin */
        } else {
            s->has_msi = true;
        }
    }

    /* Final state initialization */
    s->func = PCI_FUNC(pdev->devfn);
    s->reg.isr = 0;
    s->pcicfg_ctrl = 0;
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
    .name = "be2iscsi_pci",
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
