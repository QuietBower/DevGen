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

#define TYPE_PCIBASE_DEVICE "ismt_smbus_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_DEVICE_ID_INTEL_S1200_SMT0  0x0c59
#define PCI_DEVICE_ID_INTEL_S1200_SMT1  0x0c5a
#define PCI_DEVICE_ID_INTEL_CDF_SMT     0x18ac
#define PCI_DEVICE_ID_INTEL_DNV_SMT     0x19ac
#define PCI_DEVICE_ID_INTEL_EBG_SMT     0x1bff
#define PCI_DEVICE_ID_INTEL_AVOTON_SMT  0x1f15

#define ISMT_DESC_ENTRIES       2
#define ISMT_MAX_RETRIES        3
#define ISMT_LOG_ENTRIES        3

#define ISMT_DESC_CWRL  0x01
#define ISMT_DESC_BLK   0X04
#define ISMT_DESC_FAIR  0x08
#define ISMT_DESC_PEC   0x10
#define ISMT_DESC_I2C   0x20
#define ISMT_DESC_INT   0x40
#define ISMT_DESC_SOE   0x80

#define ISMT_DESC_SCS   0x01
#define ISMT_DESC_DLTO  0x04
#define ISMT_DESC_NAK   0x08
#define ISMT_DESC_CRC   0x10
#define ISMT_DESC_CLTO  0x20
#define ISMT_DESC_COL   0x40
#define ISMT_DESC_LPR   0x80

#define ISMT_DESC_ADDR_RW(addr, rw) (((addr) << 1) | (rw))

#define ISMT_GR_GCTRL           0x000
#define ISMT_GR_SMTICL          0x008
#define ISMT_GR_ERRINTMSK       0x010
#define ISMT_GR_ERRAERMSK       0x014
#define ISMT_GR_ERRSTS          0x018
#define ISMT_GR_ERRINFO         0x01c

#define ISMT_MSTR_MDBA          0x100
#define ISMT_MSTR_MCTRL         0x108
#define ISMT_MSTR_MSTS          0x10c
#define ISMT_MSTR_MDS           0x110
#define ISMT_MSTR_RPOLICY       0x114

#define ISMT_SPGT               0x300

#define ISMT_GCTRL_TRST 0x04
#define ISMT_GCTRL_KILL 0x08
#define ISMT_GCTRL_SRST 0x40

#define ISMT_MCTRL_SS   0x01
#define ISMT_MCTRL_MEIE 0x10
#define ISMT_MCTRL_FMHP 0x00ff0000

#define ISMT_MSTS_HMTP  0xff0000
#define ISMT_MSTS_MIS   0x20
#define ISMT_MSTS_MEIS  0x10
#define ISMT_MSTS_IP    0x01

#define ISMT_MDS_MASK   0xff

#define ISMT_SPGT_SPD_MASK      0xc0000000
#define ISMT_SPGT_SPD_80K       0x00
#define ISMT_SPGT_SPD_100K      (0x1 << 30)
#define ISMT_SPGT_SPD_400K      (0x2U << 30)
#define ISMT_SPGT_SPD_1M        (0x3U << 30)

#define ISMT_MSICTL_MSIE        0x01

#define VENDOR_ID PCI_VENDOR_ID_INTEL
#define DEVICE_ID PCI_DEVICE_ID_INTEL_S1200_SMT0
#define CLASS_ID PCI_CLASS_SERIAL_SMBUS

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
        uint32_t gr_gctrl;
        uint64_t gr_smticl;       /* 64-bit register */
        uint32_t gr_errintmsk;
        uint32_t gr_erraermsk;
        uint32_t gr_errsts;
        uint32_t gr_errinfo;      /* read-only, writes ignored */
        uint64_t mstr_mdba;
        uint32_t mstr_mctrl;
        uint32_t mstr_msts;
        uint32_t mstr_mds;
        uint32_t mstr_rpolicy;
        uint32_t spgt;
    } regs;

    /* DMA Context */
    struct {
        uint64_t base;
        uint32_t control;
    } dma;
};

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case ISMT_GR_GCTRL:
        if (size == 4) val = s->regs.gr_gctrl;
        break;
    case ISMT_GR_SMTICL:
        if (size == 8) val = s->regs.gr_smticl;
        else if (size == 4) val = (uint32_t)s->regs.gr_smticl; /* low 32 bits */
        break;
    case ISMT_GR_ERRINTMSK:
        if (size == 4) val = s->regs.gr_errintmsk;
        break;
    case ISMT_GR_ERRAERMSK:
        if (size == 4) val = s->regs.gr_erraermsk;
        break;
    case ISMT_GR_ERRSTS:
        if (size == 4) val = s->regs.gr_errsts;
        break;
    case ISMT_GR_ERRINFO:
        if (size == 4) val = s->regs.gr_errinfo;
        break;
    case ISMT_MSTR_MDBA:
        if (size == 8) val = s->regs.mstr_mdba;
        else if (size == 4) val = (uint32_t)s->regs.mstr_mdba;
        break;
    case ISMT_MSTR_MCTRL:
        if (size == 4) val = s->regs.mstr_mctrl;
        break;
    case ISMT_MSTR_MSTS:
        if (size == 4) val = s->regs.mstr_msts;
        break;
    case ISMT_MSTR_MDS:
        if (size == 4) val = s->regs.mstr_mds;
        break;
    case ISMT_MSTR_RPOLICY:
        if (size == 4) val = s->regs.mstr_rpolicy;
        break;
    case ISMT_SPGT:
        if (size == 4) val = s->regs.spgt;
        break;
    default:
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case ISMT_GR_GCTRL:
        if (size == 4) s->regs.gr_gctrl = val;
        break;
    case ISMT_GR_SMTICL:
        if (size == 8) s->regs.gr_smticl = val;
        else if (size == 4) s->regs.gr_smticl = (s->regs.gr_smticl & 0xFFFFFFFF00000000ULL) | (val & 0xFFFFFFFF);
        break;
    case ISMT_GR_ERRINTMSK:
        if (size == 4) s->regs.gr_errintmsk = val;
        break;
    case ISMT_GR_ERRAERMSK:
        if (size == 4) s->regs.gr_erraermsk = val;
        break;
    case ISMT_GR_ERRSTS:
        /* W1C: clear bits where '1' is written */
        if (size == 4) s->regs.gr_errsts &= ~val;
        break;
    case ISMT_GR_ERRINFO:
        /* read-only, ignore writes */
        break;
    case ISMT_MSTR_MDBA:
        if (size == 8) s->regs.mstr_mdba = val;
        else if (size == 4) s->regs.mstr_mdba = (s->regs.mstr_mdba & 0xFFFFFFFF00000000ULL) | (val & 0xFFFFFFFF);
        break;
    case ISMT_MSTR_MCTRL:
        if (size == 4) {
            s->regs.mstr_mctrl = val;
            /* Start bit (SS) set would trigger DMA descriptor processing,
             * but for probe we do not implement actual DMA; ignore. */
        }
        break;
    case ISMT_MSTR_MSTS:
        if (size == 4) {
            /* Write-1-to-clear for MIS, MEIS, and possibly other bits.
             * The driver writes (read_val | MIS | MEIS).
             */
            s->regs.mstr_msts &= ~val;
        }
        break;
    case ISMT_MSTR_MDS:
        if (size == 4) s->regs.mstr_mds = val;
        break;
    case ISMT_MSTR_RPOLICY:
        if (size == 4) s->regs.mstr_rpolicy = val;
        break;
    case ISMT_SPGT:
        if (size == 4) s->regs.spgt = val;
        break;
    default:
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

    /* Reset all device-specific registers to power-on defaults (all zero) */
    memset(&s->regs, 0, sizeof(s->regs));
    memset(&s->dma, 0, sizeof(s->dma));
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
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = 0x1000, .name = "ismt-mmio" };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    msi_init(pdev, 0, 1, true, false, errp);

    /* Final state initialization: ensure all registers are zero */
    memset(&s->regs, 0, sizeof(s->regs));
    memset(&s->dma, 0, sizeof(s->dma));
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
    .name = "ismt_smbus_pci",
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