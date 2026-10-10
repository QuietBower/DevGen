/*
 * QEMU PCI device model for ENETC VF (fsl_enetc_vf_pci)
 * Phase 2: Functional behavior implementation based on driver access patterns.
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

/* Removed linux/pci_ids.h, which does not exist in QEMU build tree */

#define TYPE_PCIBASE_DEVICE "fsl_enetc_vf_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define ENETC_DEV_ID_VF       0xef00
#define ENETC_BAR_REGS        0

/* Vendor / Device / Class IDs corresponding to Freescale ENETC VF */
#define FSL_ENETC_VF_VENDOR_ID   0x1957
#define FSL_ENETC_VF_DEVICE_ID   ENETC_DEV_ID_VF
#define FSL_ENETC_VF_CLASS_ID    PCI_CLASS_NETWORK_ETHERNET

#define ENETC_VSIMSGSNDAR1   0x214
#define ENETC_VSIMSGSNDAR0   0x210
#define ENETC_VSIMSGSR       0x204
#define ENETC_VSIMSGSR_MS    (1U << 1)
#define ENETC_VSIMSGSR_MB    (1U << 0)

#define ENETC_SIMR           0x0000
#define ENETC_SIMR_EN        (1U << 31)
#define ENETC_SIMR_RSSE      (1U << 0)

#define ENETC_SIPCAPR0       0x0020
#define ENETC_SIRSSCAPR      0x1600
#define ENETC_SICAPR0        0x0900
#define ENETC_SICAPR1        0x0904
#define ENETC_SICAR0         0x0040
#define ENETC_SICAR1         0x0044
#define ENETC_SICAR2         0x0048
#define ENETC_SICTR0         0x0018
#define ENETC_SICTR1         0x001c

#define ENETC_SICBDRMR       0x0800
#define ENETC_SICBDRSR       0x0804
#define ENETC_SICBDRBAR0     0x0810
#define ENETC_SICBDRBAR1     0x0814
#define ENETC_SICBDRPIR      0x0818
#define ENETC_SICBDRCIR      0x081c
#define ENETC_SICBDRLENR     0x0820

#define ENETC_SIMSITRV(n)    (0x0B00 + (n) * 0x4)
#define ENETC_SIMSIRRV(n)    (0x0B80 + (n) * 0x4)

#define ENETC_MMCSR          0x1f00
#define ENETC_MMHCR          0x1f1c
#define ENETC_MMFAECR        0x1f08
#define ENETC_MMFAOCR        0x1f10
#define ENETC_MMFSECR        0x1f0c
#define ENETC_MMFCRXR        0x1f14
#define ENETC_MMFCTXR        0x1f18

#define ENETC_SIRMCA         0x0318
#define ENETC_SIRFRM         0x0308
#define ENETC_SITOCT         0x0320
#define ENETC_SITMCA         0x0338
#define ENETC_SITUCA         0x0330
#define ENETC_SIRUCA         0x0310
#define ENETC_SITFRM         0x0328
#define ENETC_SIROCT         0x0300

#define ENETC_MMCSR_ME       (1U << 16)
#define ENETC_MMCSR_VDIS     (1U << 17)
#define ENETC_MMCSR_LPE      (1U << 1)
#define ENETC_MMCSR_LINK_FAIL (1U << 31)


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
        uint32_t simr;
        uint32_t sipcapr0;
        uint32_t sirsscapr;
        uint32_t sicapr0;
        uint32_t sicapr1;
        uint32_t sicar0;
        uint32_t sicar1;
        uint32_t sicar2;
        uint32_t sictr0;
        uint32_t sictr1;
        uint32_t vsimsgsndar0;
        uint32_t vsimsgsndar1;
        uint32_t vsimsgsr;
        uint32_t sicbdrmr;
        uint32_t sicbdrsr;
        uint32_t sicbdrbar0;
        uint32_t sicbdrbar1;
        uint32_t sicbdrpir;
        uint32_t sicbdrcir;
        uint32_t sicbdrlengr;
        uint32_t mmcsr;
        uint32_t mmhcr;
        uint32_t mmfaecr;
        uint32_t mmfaocr;
        uint32_t mmfsecr;
        uint32_t mmfcrxr;
        uint32_t mmfctxr;
    } regs;

    uint32_t status_flags;
};


/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    if (s->intr_status & ~s->intr_mask) {
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
    PCIDevice *pdev = PCI_DEVICE(s);

    (void)pdev;
    (void)is_write;
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* The driver uses readl()/__raw_readl(), i.e. 32-bit little-endian. */
    if (size != 4) {
        return 0;
    }

    switch (addr) {
    case ENETC_SIMR:
        val = s->regs.simr;
        break;
    case ENETC_SIPCAPR0:
        val = s->regs.sipcapr0;
        break;
    case ENETC_SIRSSCAPR:
        val = s->regs.sirsscapr;
        break;
    case ENETC_SICAPR0:
        val = s->regs.sicapr0;
        break;
    case ENETC_SICAPR1:
        val = s->regs.sicapr1;
        break;
    case ENETC_SICAR0:
        val = s->regs.sicar0;
        break;
    case ENETC_SICAR1:
        val = s->regs.sicar1;
        break;
    case ENETC_SICAR2:
        val = s->regs.sicar2;
        break;
    case ENETC_SICTR0:
        val = s->regs.sictr0;
        break;
    case ENETC_SICTR1:
        val = s->regs.sictr1;
        break;
    case ENETC_VSIMSGSNDAR0:
        val = s->regs.vsimsgsndar0;
        break;
    case ENETC_VSIMSGSNDAR1:
        val = s->regs.vsimsgsndar1;
        break;
    case ENETC_VSIMSGSR:
        /* VSI message status register */
        val = s->regs.vsimsgsr;
        break;
    case ENETC_SICBDRMR:
        val = s->regs.sicbdrmr;
        break;
    case ENETC_SICBDRSR:
        val = s->regs.sicbdrsr;
        break;
    case ENETC_SICBDRBAR0:
        val = s->regs.sicbdrbar0;
        break;
    case ENETC_SICBDRBAR1:
        val = s->regs.sicbdrbar1;
        break;
    case ENETC_SICBDRPIR:
        val = s->regs.sicbdrpir;
        break;
    case ENETC_SICBDRCIR:
        val = s->regs.sicbdrcir;
        break;
    case ENETC_SICBDRLENR:
        val = s->regs.sicbdrlengr;
        break;
    case ENETC_MMCSR:
        val = s->regs.mmcsr;
        break;
    case ENETC_MMHCR:
        val = s->regs.mmhcr;
        break;
    case ENETC_MMFAECR:
        val = s->regs.mmfaecr;
        break;
    case ENETC_MMFAOCR:
        val = s->regs.mmfaocr;
        break;
    case ENETC_MMFSECR:
        val = s->regs.mmfsecr;
        break;
    case ENETC_MMFCRXR:
        val = s->regs.mmfcrxr;
        break;
    case ENETC_MMFCTXR:
        val = s->regs.mmfctxr;
        break;
    default:
        /* Unknown/unimplemented register; return 0 as a safe default. */
        val = 0;
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        return;
    }

    switch (addr) {
    case ENETC_SIMR:
        s->regs.simr = (uint32_t)val;
        break;
    case ENETC_SIPCAPR0:
        /* capability register, usually read-only; ignore writes */
        break;
    case ENETC_SIRSSCAPR:
        break;
    case ENETC_SICAPR0:
        break;
    case ENETC_SICAPR1:
        break;
    case ENETC_SICAR0:
        s->regs.sicar0 = (uint32_t)val;
        break;
    case ENETC_SICAR1:
        s->regs.sicar1 = (uint32_t)val;
        break;
    case ENETC_SICAR2:
        s->regs.sicar2 = (uint32_t)val;
        break;
    case ENETC_SICTR0:
        s->regs.sictr0 = (uint32_t)val;
        break;
    case ENETC_SICTR1:
        s->regs.sictr1 = (uint32_t)val;
        break;
    case ENETC_VSIMSGSNDAR1:
        /* upper 32 bits of message DMA address */
        s->regs.vsimsgsndar1 = (uint32_t)val;
        break;
    case ENETC_VSIMSGSNDAR0: {
        /* lower 32 bits plus size encoding */
        s->regs.vsimsgsndar0 = (uint32_t)val;

        /* emulate an immediate, successful completion */
        s->regs.vsimsgsr &= ~(ENETC_VSIMSGSR_MB | ENETC_VSIMSGSR_MS);
        break;
    }
    case ENETC_SICBDRMR:
        s->regs.sicbdrmr = (uint32_t)val;
        break;
    case ENETC_SICBDRSR:
        s->regs.sicbdrsr = (uint32_t)val;
        break;
    case ENETC_SICBDRBAR0:
        s->regs.sicbdrbar0 = (uint32_t)val;
        break;
    case ENETC_SICBDRBAR1:
        s->regs.sicbdrbar1 = (uint32_t)val;
        break;
    case ENETC_SICBDRPIR:
        s->regs.sicbdrpir = (uint32_t)val;
        break;
    case ENETC_SICBDRCIR:
        s->regs.sicbdrcir = (uint32_t)val;
        break;
    case ENETC_SICBDRLENR:
        s->regs.sicbdrlengr = (uint32_t)val;
        break;
    case ENETC_MMCSR:
        s->regs.mmcsr = (uint32_t)val;
        break;
    case ENETC_MMHCR:
        s->regs.mmhcr = (uint32_t)val;
        break;
    case ENETC_MMFAECR:
        s->regs.mmfaecr = (uint32_t)val;
        break;
    case ENETC_MMFAOCR:
        s->regs.mmfaocr = (uint32_t)val;
        break;
    case ENETC_MMFSECR:
        s->regs.mmfsecr = (uint32_t)val;
        break;
    case ENETC_MMFCRXR:
        s->regs.mmfcrxr = (uint32_t)val;
        break;
    case ENETC_MMFCTXR:
        s->regs.mmfctxr = (uint32_t)val;
        break;
    default:
        /* Ignore writes to unknown/unimplemented registers. */
        break;
    }

    /* Update interrupt line if any register write changed status/mask. */
    pcibase_update_irq(s);
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    (void)s;
    (void)addr;
    (void)size;

    /* The provided driver does not use PIO; return 0. */
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    (void)s;
    (void)addr;
    (void)val;
    (void)size;

    /* The driver does not use PIO; ignore writes. */
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

    /* Initialize all shadow registers to reset-safe defaults. */
    memset(&s->regs, 0, sizeof(s->regs));

    s->regs.simr = 0;          /* interrupts disabled */
    s->regs.mmcsr = 0;         /* MAC/monitor control reset */

    s->intr_status = 0;
    s->intr_mask = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  FSL_ENETC_VF_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  FSL_ENETC_VF_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, FSL_ENETC_VF_CLASS_ID);
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
    s->bar_info[0].index = ENETC_BAR_REGS;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 1 * MiB;
    s->bar_info[0].name = "enetc-vf-regs";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = NULL;
    }

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize MSI/MSI-X capabilities if the driver attempts to use them. */
    Error *local_err = NULL;

    /* The driver uses enetc_alloc_msix(), so expose MSI-X with at least 1 vector. */
    if (msix_init_exclusive_bar(pdev, 1, ENETC_BAR_REGS, &local_err) == 0) {
        s->has_msix = true;
    } else {
        s->has_msix = false;
        if (local_err) {
            error_propagate(errp, local_err);
            return;
        }
    }

    /* Also provide MSI as a fallback. */
    local_err = NULL;
    if (msi_init(pdev, 0, 1, true, false, &local_err) == 0) {
        s->has_msi = true;
    } else {
        s->has_msi = false;
        if (local_err) {
            error_propagate(errp, local_err);
            return;
        }
    }

    /* Reset internal state to known defaults. */
    pcibase_reset(DEVICE(pdev));
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, 0);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }

    (void)s;
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "fsl_enetc_vf_pci",
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
