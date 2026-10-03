/*
 * QEMU PCI device model for Intel iAVF Virtual Function.
 * Version for QEMU 8.2.10
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

#define TYPE_PCIBASE_DEVICE "iavf_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Hardware Identifiers */
#define VENDOR_ID 0x8086
#define DEVICE_ID 0x154C

/* Register Layout */
#define IAVF_VFINT_DYN_CTL01 0x00005C00
#define IAVF_VFINT_ICR0_ENA1_ADMINQ_MASK IAVF_MASK(0x1, IAVF_VFINT_ICR0_ENA1_ADMINQ_SHIFT)
#define IAVF_VFINT_DYN_CTL01_INTENA_MASK IAVF_MASK(0x1, IAVF_VFINT_DYN_CTL01_INTENA_SHIFT)
#define IAVF_VFINT_DYN_CTL01_ITR_INDX_MASK IAVF_MASK(0x3, IAVF_VFINT_DYN_CTL01_ITR_INDX_SHIFT)
#define IAVF_VFINT_ICR0_ENA1 0x00005000
#define IAVF_VFINT_DYN_CTLN1(_INTVF) (0x00003800 + ((_INTVF) * 4))
#define IAVF_VFINT_DYN_CTLN1_INTENA_MASK IAVF_MASK(0x1, IAVF_VFINT_DYN_CTLN1_INTENA_SHIFT)
#define IAVF_VFINT_DYN_CTLN1_ITR_INDX_MASK IAVF_MASK(0x3, IAVF_VFINT_DYN_CTLN1_ITR_INDX_SHIFT)
#define IAVF_VFINT_ICR01 0x00004800
#define IAVF_VFINT_ITRN1(_i, _INTVF) (0x00002800 + ((_i) * 64 + (_INTVF) * 4))
#define IAVF_QTX_TAIL1(_Q) (0x00000000 + ((_Q) * 4))
#define IAVF_QRX_TAIL1(_Q) (0x00002000 + ((_Q) * 4))
#define IAVF_VFQF_HKEY(_i) (0x0000CC00 + ((_i) * 4))
#define IAVF_VFQF_HLUT(_i) (0x0000D000 + ((_i) * 4))
#define IAVF_VFQF_HENA(_i) (0x0000C400 + ((_i) * 4))
#define IAVF_VFGEN_RSTAT 0x00008800
#define IAVF_VFGEN_RSTAT_VFR_STATE_MASK IAVF_MASK(0x3, IAVF_VFGEN_RSTAT_VFR_STATE_SHIFT)
#define IAVF_VF_ARQLEN1 0x00008000
#define IAVF_VF_ARQLEN1_ARQENABLE_MASK IAVF_MASK(0x1, IAVF_VF_ARQLEN1_ARQENABLE_SHIFT)
#define IAVF_VF_ATQLEN1 0x00006800
#define IAVF_VF_ATQLEN1_ATQVFE_MASK IAVF_MASK(0x1, IAVF_VF_ATQLEN1_ATQVFE_SHIFT)
#define IAVF_VF_ATQLEN1_ATQOVFL_MASK IAVF_MASK(0x1, IAVF_VF_ATQLEN1_ATQOVFL_SHIFT)
#define IAVF_VF_ARQLEN1_ARQOVFL_MASK IAVF_MASK(0x1, IAVF_VF_ARQLEN1_ARQOVFL_SHIFT)
#define IAVF_VF_ATQLEN1_ATQCRIT_MASK IAVF_MASK(0x1, IAVF_VF_ATQLEN1_ATQCRIT_SHIFT)
#define IAVF_VF_ARQLEN1_ARQCRIT_MASK IAVF_MASK(0x1, IAVF_VF_ARQLEN1_ARQCRIT_SHIFT)
#define IAVF_VF_ARQLEN1_ARQVFE_MASK IAVF_MASK(0x1, IAVF_VF_ARQLEN1_ARQVFE_SHIFT)
#define IAVF_VF_ATQH1 0x00006400
#define IAVF_VF_ARQH1 0x00007400
#define IAVF_VF_ARQT1 0x00007000
#define IAVF_VF_ARQH1_ARQH_MASK IAVF_MASK(0x3FF, IAVF_VF_ARQH1_ARQH_SHIFT)
#define IAVF_VF_ATQBAL1 0x00007C00
#define IAVF_VF_ATQBAH1 0x00007800
#define IAVF_VF_ATQT1 0x00008400
#define IAVF_VF_ARQBAL1 0x00006C00
#define IAVF_VF_ARQBAH1 0x00006000
#define IAVF_VF_ATQLEN1_ATQENABLE_MASK IAVF_MASK(0x1, IAVF_VF_ATQLEN1_ATQENABLE_SHIFT)
#define IAVF_VFINT_DYN_CTLN1_SW_ITR_INDX_ENA_MASK IAVF_MASK(0x1, IAVF_VFINT_DYN_CTLN1_SW_ITR_INDX_ENA_SHIFT)
#define IAVF_VFINT_DYN_CTLN1_SWINT_TRIG_MASK IAVF_MASK(0x1, IAVF_VFINT_DYN_CTLN1_SWINT_TRIG_SHIFT)

/* Shift constants */
#define IAVF_VFINT_ICR0_ENA1_ADMINQ_SHIFT 30
#define IAVF_VFINT_DYN_CTL01_INTENA_SHIFT 0
#define IAVF_VFINT_DYN_CTL01_ITR_INDX_SHIFT 3
#define IAVF_VFINT_DYN_CTLN1_INTENA_SHIFT 0
#define IAVF_VFINT_DYN_CTLN1_ITR_INDX_SHIFT 3
#define IAVF_VFGEN_RSTAT_VFR_STATE_SHIFT 0
#define IAVF_VF_ARQLEN1_ARQENABLE_SHIFT 31
#define IAVF_VF_ATQLEN1_ATQVFE_SHIFT 28
#define IAVF_VF_ATQLEN1_ATQOVFL_SHIFT 29
#define IAVF_VF_ARQLEN1_ARQOVFL_SHIFT 29
#define IAVF_VF_ATQLEN1_ATQCRIT_SHIFT 30
#define IAVF_VF_ARQLEN1_ARQCRIT_SHIFT 30
#define IAVF_VF_ARQLEN1_ARQVFE_SHIFT 28
#define IAVF_VF_ARQH1_ARQH_SHIFT 0
#define IAVF_VF_ATQLEN1_ATQENABLE_SHIFT 31
#define IAVF_VFINT_DYN_CTLN1_SW_ITR_INDX_ENA_SHIFT 24
#define IAVF_VFINT_DYN_CTLN1_SWINT_TRIG_SHIFT 2

/* Helper macro */
#define IAVF_MASK(mask, shift) ((uint32_t)(mask) << (shift))

/* BAR0 size: enough to cover all used registers (up to 0xD000) */
#define BAR0_SIZE (64 * 1024)  /* 64KB */

/* VFR state constants (based on driver usage) */
#define VIRTCHNL_VFR_VFACTIVE  0
#define VIRTCHNL_VFR_COMPLETED 1

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
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Shadow registers for MMIO */
    uint32_t regs[BAR0_SIZE / 4];

    /* MSI-X BAR */
    MemoryRegion msix_bar;
};

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t index = addr / 4;
    if (index < (BAR0_SIZE / 4)) {
        return s->regs[index];
    }
    qemu_log_mask(LOG_GUEST_ERROR, "%s: read out-of-range address: %" HWADDR_PRIx "\n",
                  __func__, addr);
    return 0xFFFFFFFF;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t index = addr / 4;
    if (index < (BAR0_SIZE / 4)) {
        if (addr == IAVF_VF_ARQLEN1) {
            /* Handle W1C for error bits */
            uint32_t error_mask = IAVF_VF_ARQLEN1_ARQVFE_MASK |
                                 IAVF_VF_ARQLEN1_ARQOVFL_MASK |
                                 IAVF_VF_ARQLEN1_ARQCRIT_MASK;
            s->regs[index] = (s->regs[index] & ~(val & error_mask)) | (val & ~error_mask);
        } else if (addr == IAVF_VF_ATQLEN1) {
            uint32_t error_mask = IAVF_VF_ATQLEN1_ATQVFE_MASK |
                                 IAVF_VF_ATQLEN1_ATQOVFL_MASK |
                                 IAVF_VF_ATQLEN1_ATQCRIT_MASK;
            s->regs[index] = (s->regs[index] & ~(val & error_mask)) | (val & ~error_mask);
        } else {
            s->regs[index] = val;
        }
    } else {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: write out-of-range address: %" HWADDR_PRIx "\n",
                      __func__, addr);
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Reset shadow registers to initial values */
    memset(s->regs, 0, sizeof(s->regs));
    s->regs[IAVF_VFGEN_RSTAT / 4] = VIRTCHNL_VFR_VFACTIVE;
    s->regs[IAVF_VF_ARQLEN1 / 4] = IAVF_VF_ARQLEN1_ARQENABLE_MASK;
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
        /* Not used */
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

    /* Set PCI class code: Network controller (Ethernet) */
    pci_set_byte(pci_conf + PCI_CLASS_PROG, 0x02);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0000);

    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR 0: MMIO registers */
    s->bar_info[0] = (BARInfo){
        .index = 0,
        .type = BAR_TYPE_MMIO,
        .size = BAR0_SIZE,
        .name = "bar0"
    };
    pcibase_register_bar(pdev, s, &s->bar_info[0], errp);

    /* MSI-X initialization */
    memory_region_init(&s->msix_bar, OBJECT(s), "msix-bar", 4096);
    if (msix_init(pdev, 3, &s->msix_bar, 1, 0, &s->msix_bar, 1, 0x1000, 0, errp)) {
        return;
    }
    /* Register MSI-X BAR at index 1 */
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->msix_bar);

    /* Initial register state */
    memset(s->regs, 0, sizeof(s->regs));
    s->regs[IAVF_VFGEN_RSTAT / 4] = VIRTCHNL_VFR_VFACTIVE;
    s->regs[IAVF_VF_ARQLEN1 / 4] = IAVF_VF_ARQLEN1_ARQENABLE_MASK;
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, &s->msix_bar, &s->msix_bar);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

static const VMStateDescription vmstate_pcibase = {
    .name = "iavf_pci",
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
