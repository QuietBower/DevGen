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

#define TYPE_PCIBASE_DEVICE "isci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI_VENDOR_ID_INTEL 0x8086
#define ISCI_VENDOR_ID 0x8086
#define ISCI_DEVICE_ID 0x1D61
#define ISCI_CLASS_ID  0x0107   /* Serial Attached SCSI controller */

#define SCI_SMU_BAR 0
#define SCI_SCU_BAR 1
#define SCI_SMU_BAR_SIZE (32*1024)
#define SCI_SCU_BAR_SIZE (8*1024*1024)

/* SMU Register Offsets (BAR0) */
#define SMU_ISR          0x10
#define SMU_IMR          0x14
#define SMU_ICC          0x18
#define SMU_HTTLBAR      0x20
#define SMU_HTTUBAR      0x24
#define SMU_TCR          0x28
#define SMU_CQLBAR       0x30
#define SMU_CQUBAR       0x34
#define SMU_CQPR         0x40
#define SMU_CQGR         0x44
#define SMU_CQC          0x48
#define SMU_RNCLBAR      0x80
#define SMU_RNCUBAR      0x84
#define SMU_DCC          0x90
#define SMU_DFC          0x94
#define SMU_SMUCSR       0x98
#define SMU_SCUSRCR      0x9C
#define SMU_SMAW         0xA0
#define SMU_SMDW         0xA4
#define SMU_CGUCR        0xA8
#define SMU_CGUPC        0xAC
#define SMU_TCA_BASE     0x400

/* Device Context Capacity register fields (from driver) */
#define SMU_DEVICE_CONTEXT_CAPACITY_MAX_RNC_MASK    (0x07FF8000)
#define SMU_DEVICE_CONTEXT_CAPACITY_MAX_RNC_SHIFT   (15)
#define SMU_DEVICE_CONTEXT_CAPACITY_MAX_LP_MASK     (0x00007000)
#define SMU_DEVICE_CONTEXT_CAPACITY_MAX_LP_SHIFT    (12)
#define SMU_DEVICE_CONTEXT_CAPACITY_MAX_TC_MASK     (0x00000FFF)
#define SMU_DEVICE_CONTEXT_CAPACITY_MAX_TC_SHIFT    (0)

/* Control Status register bit shifts */
#define SMU_CONTROL_STATUS_CONTEXT_RAM_INIT_COMPLETED_SHIFT     (16)
#define SMU_CONTROL_STATUS_SCHEDULER_RAM_INIT_COMPLETED_SHIFT   (17)

/* Number of MSI-X vectors */
#define SCI_NUM_MSI_X_INT 2

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
    struct {
        uint32_t smu_dcc;
        uint32_t smu_csr;
        uint32_t smu_isr;
        uint32_t smu_imr;
        uint32_t smu_icc;
        uint32_t smu_tcr;
        uint32_t smu_httlbar;
        uint32_t smu_httubar;
        uint32_t smu_cqlbar;
        uint32_t smu_cqubar;
        uint32_t smu_cqpr;
        uint32_t smu_cqgr;
        uint32_t smu_cqc;
        uint32_t smu_rnclbar;
        uint32_t smu_rncubar;
        uint32_t smu_dfc;
        uint32_t smu_scusrcr;
        uint32_t smu_smaw;
        uint32_t smu_smdw;
        uint32_t smu_cgucr;
        uint32_t smu_cgupc;
    } regs;

    /* DMA Context */
    dma_addr_t tc_dma;          /* Task Context base */
    dma_addr_t rnc_dma;         /* Remote Node Context base */
    dma_addr_t cq_dma;          /* Completion Queue base */
    dma_addr_t ufi_dma;         /* Unsolicited Frame Info base */

    /* Operational status flags */
    uint32_t status;

    /* State used to handle reset sequences */
    bool soft_reset_pending;

    /* Power management state (D0-D3) */
    uint8_t power_state;

    /* IRQ raised flag */
    bool irq_raised;
};

/* Internal helper for status-triggered signaling */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t pending = s->regs.smu_isr & s->regs.smu_imr;
    if (pending && !s->irq_raised) {
        msix_notify(pdev, 0);
        s->irq_raised = true;
    } else if (!pending && s->irq_raised) {
        s->irq_raised = false;
    }
}

/* MMIO Handlers for SMU (BAR0) */
static uint64_t pcibase_smu_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case SMU_ISR:
        val = s->regs.smu_isr;
        break;
    case SMU_IMR:
        val = s->regs.smu_imr;
        break;
    case SMU_ICC:
        val = s->regs.smu_icc;
        break;
    case SMU_HTTLBAR:
        val = s->regs.smu_httlbar;
        break;
    case SMU_HTTUBAR:
        val = s->regs.smu_httubar;
        break;
    case SMU_TCR:
        val = s->regs.smu_tcr;
        break;
    case SMU_CQLBAR:
        val = s->regs.smu_cqlbar;
        break;
    case SMU_CQUBAR:
        val = s->regs.smu_cqubar;
        break;
    case SMU_CQPR:
        val = s->regs.smu_cqpr;
        break;
    case SMU_CQGR:
        val = s->regs.smu_cqgr;
        break;
    case SMU_CQC:
        val = s->regs.smu_cqc;
        break;
    case SMU_RNCLBAR:
        val = s->regs.smu_rnclbar;
        break;
    case SMU_RNCUBAR:
        val = s->regs.smu_rncubar;
        break;
    case SMU_DCC:
        val = s->regs.smu_dcc;
        break;
    case SMU_DFC:
        val = s->regs.smu_dfc;
        break;
    case SMU_SMUCSR:
        val = s->regs.smu_csr;
        break;
    case SMU_SCUSRCR:
        val = s->regs.smu_scusrcr;
        break;
    case SMU_SMAW:
        val = s->regs.smu_smaw;
        break;
    case SMU_SMDW:
        val = s->regs.smu_smdw;
        break;
    case SMU_CGUCR:
        val = s->regs.smu_cgucr;
        break;
    case SMU_CGUPC:
        val = s->regs.smu_cgupc;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented SMU read at 0x%"HWADDR_PRIx"\n", __func__, addr);
        val = 0;
    }
    return val;
}

static void pcibase_smu_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case SMU_ISR:
        s->regs.smu_isr &= ~val; /* W1C */
        pcibase_update_irq(s);
        break;
    case SMU_IMR:
        s->regs.smu_imr = val;
        pcibase_update_irq(s);
        break;
    case SMU_ICC:
        s->regs.smu_icc = val;
        break;
    case SMU_HTTLBAR:
        s->regs.smu_httlbar = val;
        break;
    case SMU_HTTUBAR:
        s->regs.smu_httubar = val;
        /* Combine with low part for full DMA address */
        s->tc_dma = ((uint64_t)s->regs.smu_httubar << 32) | s->regs.smu_httlbar;
        break;
    case SMU_TCR:
        s->regs.smu_tcr = val;
        break;
    case SMU_CQLBAR:
        s->regs.smu_cqlbar = val;
        break;
    case SMU_CQUBAR:
        s->regs.smu_cqubar = val;
        s->cq_dma = ((uint64_t)s->regs.smu_cqubar << 32) | s->regs.smu_cqlbar;
        break;
    case SMU_CQPR:
        s->regs.smu_cqpr = val;
        break;
    case SMU_CQGR:
        s->regs.smu_cqgr = val;
        break;
    case SMU_CQC:
        s->regs.smu_cqc = val;
        break;
    case SMU_RNCLBAR:
        s->regs.smu_rnclbar = val;
        break;
    case SMU_RNCUBAR:
        s->regs.smu_rncubar = val;
        s->rnc_dma = ((uint64_t)s->regs.smu_rncubar << 32) | s->regs.smu_rnclbar;
        break;
    case SMU_DCC:
        /* Possibly read-only; ignore */
        break;
    case SMU_DFC:
        s->regs.smu_dfc = val;
        break;
    case SMU_SMUCSR:
        s->regs.smu_csr = val;
        break;
    case SMU_SCUSRCR:
        s->regs.smu_scusrcr = val;
        break;
    case SMU_SMAW:
        s->regs.smu_smaw = val;
        break;
    case SMU_SMDW:
        s->regs.smu_smdw = val;
        break;
    case SMU_CGUCR:
        s->regs.smu_cgucr = val;
        break;
    case SMU_CGUPC:
        s->regs.smu_cgupc = val;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented SMU write at 0x%"HWADDR_PRIx" val=0x%"PRIx64"\n", __func__, addr, val);
    }
}

static const MemoryRegionOps pcibase_smu_mmio_ops = {
    .read = pcibase_smu_mmio_read,
    .write = pcibase_smu_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

/* MMIO Handlers for SCU (BAR2) - minimal implementation to return a signature */
static uint64_t pcibase_scu_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* Return a valid signature at offset 0 to satisfy driver checks */
    if (addr == 0) {
        return 0x494f5343; /* "ISCI" in little-endian, common for Intel SAS controllers */
    }
    qemu_log_mask(LOG_UNIMP, "%s: unimplemented SCU read at 0x%"HWADDR_PRIx"\n", __func__, addr);
    return 0;
}

static void pcibase_scu_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "%s: unimplemented SCU write at 0x%"HWADDR_PRIx" val=0x%"PRIx64"\n", __func__, addr, val);
}

static const MemoryRegionOps pcibase_scu_mmio_ops = {
    .read = pcibase_scu_mmio_read,
    .write = pcibase_scu_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    memset(&s->regs, 0, sizeof(s->regs));
    /* Set default values required for driver probing */
    s->regs.smu_dcc = (256 << SMU_DEVICE_CONTEXT_CAPACITY_MAX_RNC_SHIFT) |
                      (4 << SMU_DEVICE_CONTEXT_CAPACITY_MAX_LP_SHIFT) |
                      (256 << SMU_DEVICE_CONTEXT_CAPACITY_MAX_TC_SHIFT);
    s->regs.smu_csr = (1 << SMU_CONTROL_STATUS_CONTEXT_RAM_INIT_COMPLETED_SHIFT) |
                      (1 << SMU_CONTROL_STATUS_SCHEDULER_RAM_INIT_COMPLETED_SHIFT);
    s->irq_raised = false;
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  ISCI_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  ISCI_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, ISCI_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_VENDOR_ID, 0x8086);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_ID, 0x3513);
    pci_set_byte(pci_conf + PCI_INTERRUPT_PIN, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR0: SMU */
    memory_region_init_io(&s->bar_regions[0], OBJECT(s), &pcibase_smu_mmio_ops, s, "smu-mmio", SCI_SMU_BAR_SIZE);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_regions[0]);

    /* BAR1: MSI-X table and PBA (dedicated BAR, 2 entries) */
    if (msix_init_exclusive_bar(pdev, SCI_NUM_MSI_X_INT, 1, errp)) {
        return;
    }

    /* BAR2: SCU */
    memory_region_init_io(&s->bar_regions[2], OBJECT(s), &pcibase_scu_mmio_ops, s, "scu-mmio", SCI_SCU_BAR_SIZE);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_regions[2]);

    /* Set initial register values */
    s->regs.smu_dcc = (256 << SMU_DEVICE_CONTEXT_CAPACITY_MAX_RNC_SHIFT) |
                      (4 << SMU_DEVICE_CONTEXT_CAPACITY_MAX_LP_SHIFT) |
                      (256 << SMU_DEVICE_CONTEXT_CAPACITY_MAX_TC_SHIFT);
    s->regs.smu_csr = (1 << SMU_CONTROL_STATUS_CONTEXT_RAM_INIT_COMPLETED_SHIFT) |
                      (1 << SMU_CONTROL_STATUS_SCHEDULER_RAM_INIT_COMPLETED_SHIFT);
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
    .name = "isci_pci",
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
