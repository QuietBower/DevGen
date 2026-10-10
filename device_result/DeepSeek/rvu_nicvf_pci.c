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
/* No additional headers needed */

#define TYPE_PCIBASE_DEVICE "rvu_nicvf_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
/* PCI IDs */
#define PCI_VENDOR_ID_CAVIUM  0x177d
#define PCI_DEVID_OCTEONTX2_RVU_AFVF  0xA0F8
#define PCI_CLASS_ID  PCI_CLASS_NETWORK_ETHERNET

/* BAR indices */
#define PCI_CFG_REG_BAR_NUM  2
#define PCI_MBOX_BAR_NUM     4

/* Register offsets (immediate) */
#define RVU_VF_INT                      0x20
#define RVU_VF_VFPF_MBOX0               0x00000
#define RVU_VF_INT_ENA_W1C              0x38
#define RVU_VF_INT_ENA_W1S              0x30
#define RVU_VF_MBOX_REGION              0xC0000
#define RVU_FUNC_BLKADDR_SHIFT          20
#define NIX_LF_CINT_VEC_START           0x40
#define NIX_LF_QINT_VEC_START           0x00
#define RVU_PF_INT                      0xc20
#define RVU_PF_PFAF_MBOX0               0xC00
/* Add other immediate offsets as encountered; offsets dependent on BLKTYPE_NIX etc will be added later */

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

/* Derived constants from driver usage */
#define MBOX_DOWN_MSG  BIT_ULL(0)
#define MBOX_UP_MSG    BIT_ULL(1)

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
    uint32_t vf_int;          /* RVU_VF_INT */
    uint32_t vf_int_ena;      /* RVU_VF_INT_ENA_W1S / _W1C */
    uint64_t vf_vfpf_mbox0;   /* RVU_VF_VFPF_MBOX0 */

    /* DMA Context */
    /* No DMA state needed for probe phase */
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    if (msix_enabled(pdev)) {
        if (s->vf_int & s->vf_int_ena) {
            msix_notify(pdev, 0);
        }
        /* No explicit deassert; driver clears via W1C */
    }
}

/* Device-initiated DMA logic based on driver access patterns - not used */

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case RVU_VF_INT:
        val = s->vf_int;
        break;
    case RVU_VF_INT_ENA_W1S:
    case RVU_VF_INT_ENA_W1C:
        val = s->vf_int_ena;
        break;
    case RVU_VF_VFPF_MBOX0:
        val = s->vf_vfpf_mbox0;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented read @0x%"HWADDR_PRIx" size=%u\n",
                      __func__, addr, size);
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case RVU_VF_INT:
        s->vf_int &= ~val;  /* Write-1-to-clear */
        pcibase_update_irq(s);
        break;
    case RVU_VF_INT_ENA_W1S:
        s->vf_int_ena |= val;
        pcibase_update_irq(s);
        break;
    case RVU_VF_INT_ENA_W1C:
        s->vf_int_ena &= ~val;
        pcibase_update_irq(s);
        break;
    case RVU_VF_VFPF_MBOX0:
        s->vf_vfpf_mbox0 = val;
        /* In future, process outgoing mailbox messages here */
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented write @0x%"HWADDR_PRIx" size=%u val=0x%"PRIx64"\n",
                      __func__, addr, size, val);
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* No PIO used, return 0 */
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* No PIO used, nothing to do */
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 8 },
    .impl  = { .min_access_size = 4, .max_access_size = 8 },
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

    s->vf_int = 0;
    s->vf_int_ena = 0;
    s->vf_vfpf_mbox0 = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_CAVIUM);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVID_OCTEONTX2_RVU_AFVF);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR Initialization */
    s->num_bars = 3; /* BAR1, BAR2, BAR4 */

    /* BAR1: MSI-X table */
    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_RAM;
    s->bar_info[1].size = 0x1000;  /* 4KB, power of 2 */
    s->bar_info[1].name = "msix-table";

    /* BAR2: Configuration registers */
    s->bar_info[2].index = 2;
    s->bar_info[2].type = BAR_TYPE_MMIO;
    s->bar_info[2].size = 0x200000; /* 2MB, covers register space with block offsets */
    s->bar_info[2].name = "regs";

    /* BAR4: Mailbox shared memory */
    s->bar_info[4].index = 4;
    s->bar_info[4].type = BAR_TYPE_RAM;
    s->bar_info[4].size = 0x1000;  /* 4KB */
    s->bar_info[4].name = "mailbox";

    for (int i = 0; i < ARRAY_SIZE(s->bar_info); i++) {
        if (s->bar_info[i].type != BAR_TYPE_NONE) {
            pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
            if (errp && *errp) {
                return;
            }
        }
    }

    /* MSI-X initialization with 128 vectors */
    if (msix_init(pdev, 128,
                  &s->bar_regions[1], 1, 0,
                  &s->bar_regions[1], 1, 0x800,
                  0, errp)) {
        if (errp && !*errp) {
            error_setg(errp, "MSI-X initialization failed");
        }
        return;
    }

    /* DMA config placeholder - not needed */
    /* Field init placeholder - no special actions */
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

    /* Uninit placeholder - nothing to clean */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "rvu_nicvf_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(vf_int, PCIBaseState),
        VMSTATE_UINT32(vf_int_ena, PCIBaseState),
        VMSTATE_UINT64(vf_vfpf_mbox0, PCIBaseState),
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
