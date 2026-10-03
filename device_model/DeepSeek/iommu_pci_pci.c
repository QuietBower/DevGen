/*
 * QEMU RISC-V IOMMU PCI device model for driver iommu-pci.c
 * Based on Stage-1 template, Phase 2 implementation.
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

/* Provide missing bit macros from Linux */
#define BIT_ULL(n) (1ULL << (n))
#define GENMASK_ULL(high, low) ((((1ULL << ((high) - (low) + 1)) - 1)) << (low))

/* Missing driver constants inferred from probe checks */
#define RISCV_IOMMU_CAPABILITIES_IGS_MSI    1
#define RISCV_IOMMU_CAPABILITIES_IGS_BOTH   3

#define TYPE_PCIBASE_DEVICE "iommu_pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
/* Register and bit definitions */
#define PCI_VENDOR_ID_REDHAT 0x1b36
#define PCI_DEVICE_ID_REDHAT_RISCV_IOMMU 0x0014
#define PCI_CLASS_RISCV_IOMMU PCI_CLASS_OTHERS

#define RISCV_IOMMU_REG_CAPABILITIES    0x0000
#define RISCV_IOMMU_REG_FCTL            0x0008
#define RISCV_IOMMU_REG_DDTP            0x0010
#define RISCV_IOMMU_REG_CQB             0x0018
#define RISCV_IOMMU_REG_CQH             0x0020
#define RISCV_IOMMU_REG_CQT             0x0024
#define RISCV_IOMMU_REG_CQCSR           0x0048
#define RISCV_IOMMU_REG_FQCSR           0x004C
#define RISCV_IOMMU_REG_PQCSR           0x0050
#define RISCV_IOMMU_REG_IPSR            0x0054
#define RISCV_IOMMU_REG_ICVEC           0x02F8

#define RISCV_IOMMU_REG_SIZE            0x1000

/* Bitfields */
#define RISCV_IOMMU_CAPABILITIES_IGS    GENMASK_ULL(29, 28)
#define RISCV_IOMMU_CAPABILITIES_END    BIT_ULL(27)

#define RISCV_IOMMU_FCTL_BE             BIT(0)
#define RISCV_IOMMU_FCTL_WSI            BIT(1)

#define RISCV_IOMMU_DDTP_IOMMU_MODE     GENMASK_ULL(3, 0)
#define RISCV_IOMMU_DDTP_BUSY           BIT_ULL(4)

#define RISCV_IOMMU_QUEUE_ENABLE        BIT(0)
#define RISCV_IOMMU_QUEUE_INTR_ENABLE   BIT(1)
#define RISCV_IOMMU_QUEUE_MEM_FAULT     BIT(8)
#define RISCV_IOMMU_QUEUE_ACTIVE        BIT(16)
#define RISCV_IOMMU_QUEUE_BUSY          BIT(17)
#define RISCV_IOMMU_QUEUE_LOG2SZ_FIELD  GENMASK_ULL(4, 0)
#define RISCV_IOMMU_PPN_FIELD           GENMASK_ULL(53, 10)

#define RISCV_IOMMU_CQCSR_CMD_TO        BIT(9)
#define RISCV_IOMMU_CQCSR_CMD_ILL       BIT(10)
#define RISCV_IOMMU_CQCSR_CQMF          RISCV_IOMMU_QUEUE_MEM_FAULT

#define RISCV_IOMMU_CMD_OPCODE          GENMASK_ULL(6, 0)
#define RISCV_IOMMU_CMD_FUNC            GENMASK_ULL(9, 7)
#define RISCV_IOMMU_CMD_IOTINVAL_OPCODE 1
#define RISCV_IOMMU_CMD_IOTINVAL_FUNC_VMA 0
#define RISCV_IOMMU_CMD_IODIR_OPCODE    3
#define RISCV_IOMMU_CMD_IODIR_FUNC_INVAL_DDT 0
#define RISCV_IOMMU_CMD_IOFENCE_OPCODE  2
#define RISCV_IOMMU_CMD_IOFENCE_FUNC_C  0
#define RISCV_IOMMU_CMD_IOFENCE_PR      BIT_ULL(12)
#define RISCV_IOMMU_CMD_IOFENCE_PW      BIT_ULL(13)

#define RISCV_IOMMU_INTR_CQ             0
#define RISCV_IOMMU_INTR_COUNT          4

#define RISCV_IOMMU_ICVEC_CIV           GENMASK_ULL(3, 0)
#define RISCV_IOMMU_ICVEC_FIV           GENMASK_ULL(7, 4)
#define RISCV_IOMMU_ICVEC_PMIV          GENMASK_ULL(11, 8)
#define RISCV_IOMMU_ICVEC_PIV           GENMASK_ULL(15, 12)

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
        uint64_t capabilities;  /* 0x0000 */
        uint32_t fctl;          /* 0x0008 */
        uint64_t ddtp;          /* 0x0010 */
        uint64_t cqb;           /* 0x0018 */
        uint64_t cqh;           /* 0x0020 */
        uint64_t cqt;           /* 0x0024 */
        uint32_t cqcsr;         /* 0x0048 */
        uint32_t fqcsr;         /* 0x004C */
        uint32_t pqcsr;         /* 0x0050 */
        uint32_t ipsr;          /* 0x0054 */
        uint64_t icvec;         /* 0x02F8 */
    } reg;

    /* DMA Context */
    /* Pointers for DMA base addresses, count, and status */

    /* Operational status flags */
    
    /* State used to handle reset sequences */
    
    /* Power management state (D0-D3) */
    uint8_t pm_state;
};

/* MMIO Handlers implemented based on driver access patterns */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case RISCV_IOMMU_REG_CAPABILITIES:
        val = s->reg.capabilities;
        break;
    case RISCV_IOMMU_REG_FCTL:
        val = s->reg.fctl;
        break;
    case RISCV_IOMMU_REG_DDTP:
        val = s->reg.ddtp;
        break;
    case RISCV_IOMMU_REG_CQB:
        val = s->reg.cqb;
        break;
    case RISCV_IOMMU_REG_CQH:
        val = s->reg.cqh;
        break;
    case RISCV_IOMMU_REG_CQT:
        val = s->reg.cqt;
        break;
    case RISCV_IOMMU_REG_CQCSR:
        val = s->reg.cqcsr;
        break;
    case RISCV_IOMMU_REG_FQCSR:
        val = s->reg.fqcsr;
        break;
    case RISCV_IOMMU_REG_PQCSR:
        val = s->reg.pqcsr;
        break;
    case RISCV_IOMMU_REG_IPSR:
        val = s->reg.ipsr;
        break;
    case RISCV_IOMMU_REG_ICVEC:
        val = s->reg.icvec;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "iommu_pci: unimplemented read at 0x%" PRIx64 "\n", addr);
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case RISCV_IOMMU_REG_FCTL:
        s->reg.fctl = (uint32_t)val;
        break;
    case RISCV_IOMMU_REG_DDTP:
        s->reg.ddtp = val;
        break;
    case RISCV_IOMMU_REG_CQB:
        s->reg.cqb = val;
        break;
    case RISCV_IOMMU_REG_CQH:
        s->reg.cqh = val;
        break;
    case RISCV_IOMMU_REG_CQT:
        s->reg.cqt = val;
        break;
    case RISCV_IOMMU_REG_CQCSR:
        s->reg.cqcsr = (uint32_t)val;
        break;
    case RISCV_IOMMU_REG_FQCSR:
        s->reg.fqcsr = (uint32_t)val;
        break;
    case RISCV_IOMMU_REG_PQCSR:
        s->reg.pqcsr = (uint32_t)val;
        break;
    case RISCV_IOMMU_REG_IPSR:
        s->reg.ipsr = (uint32_t)val;
        break;
    case RISCV_IOMMU_REG_ICVEC:
        s->reg.icvec = val;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "iommu_pci: unimplemented write at 0x%" PRIx64 "\n", addr);
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

    /* Set power-on defaults visible to probe() */
    s->reg.capabilities = (3ULL << 28) & GENMASK_ULL(29, 28); /* IGS = BOTH */
    s->reg.fctl = RISCV_IOMMU_FCTL_WSI; /* indicate MSI support ready */
    s->reg.ddtp = 0;
    s->reg.cqb = 0;
    s->reg.cqh = 0;
    s->reg.cqt = 0;
    s->reg.cqcsr = 0;
    s->reg.fqcsr = 0;
    s->reg.pqcsr = 0;
    s->reg.ipsr = 0;
    s->reg.icvec = 0;
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
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, aligned_size);
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_REDHAT);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_REDHAT_RISCV_IOMMU);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_RISCV_IOMMU);
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
    s->bar_info[0] = (BARInfo){
        .index = 0,
        .type = BAR_TYPE_MMIO,
        .size = RISCV_IOMMU_REG_SIZE,
        .name = "iommu-mmio",
    };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    if (msi_init(pdev, 0, RISCV_IOMMU_INTR_COUNT, true, false, errp)) {
        return;
    }

    /* Initialize device state to post-reset values */
    s->reg.capabilities = (3ULL << 28) & GENMASK_ULL(29, 28);
    s->reg.fctl = RISCV_IOMMU_FCTL_WSI;
    s->reg.ddtp = 0;
    s->reg.cqb = 0;
    s->reg.cqh = 0;
    s->reg.cqt = 0;
    s->reg.cqcsr = 0;
    s->reg.fqcsr = 0;
    s->reg.pqcsr = 0;
    s->reg.ipsr = 0;
    s->reg.icvec = 0;
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
    .name = "iommu_pci_pci",
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
