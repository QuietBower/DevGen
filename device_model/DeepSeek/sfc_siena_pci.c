/*
 * Integrated QEMU PCI device template for Solarflare Siena NIC.
 * Derived from driver at /home/eely/linux-7.1/drivers/net/ethernet/sfc/siena/efx.c
 * Emulates a minimal hardware interface to allow driver probe and binding.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/module.h"
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

#define TYPE_PCIBASE_DEVICE "sfc_siena_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI_VENDOR_ID_SOLARFLARE 0x1924
#define DEVICE_ID 0x0803
#define CLASS_ID PCI_CLASS_NETWORK_ETHERNET

/* Register offsets from the driver supplementary sources */
#define FR_BZ_TX_DESC_PTR_TBL 0x00f50000
#define FR_BZ_RX_DESC_PTR_TBL 0x00f40000
#define FR_BZ_BUF_FULL_TBL 0x00800000
#define FR_BZ_EVQ_PTR_TBL 0x00f60000
#define FR_BZ_EVQ_RPTR 0x00fa0000
#define FR_CZ_MC_TREG_SMEM 0x00ff0000
#define FR_CZ_MC_TREG_SMEM_STEP 4
#define FR_CZ_MC_TREG_SMEM_ROWS 512
#define MMIO_BAR_SIZE (FR_CZ_MC_TREG_SMEM + FR_CZ_MC_TREG_SMEM_STEP * FR_CZ_MC_TREG_SMEM_ROWS)

/* MSI-X configuration: allow up to 32 vectors as typical for this NIC family */
#define SFC_MSIX_BAR_IDX 4
#define SFC_MSIX_BAR_SIZE 0x2000
#define SFC_MSIX_MAX_VECTORS 32

typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM
} BARType;

typedef struct {
    int index;
    BARType type;
    hwaddr size;
    const char *name;
} BARInfo;

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* MSI-X support */
    MemoryRegion msix_bar;
    bool has_msi;
    bool has_msix;

    /* Interrupt state (minimal) */
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware Register Shadows (dummy, no specific registers defined) */
};

/* Internal helper for status-triggered signaling. Unused but kept for future. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    /* No IRQ logic needed; IRQs are handled via MSI-X and explicit writes */
}

/* MMIO Handlers: simple read-as-zero, write-ignored to allow probe */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = PCIBASE_DEVICE(opaque);
    uint64_t val = 0;

    /* For now, all reads return 0. This is sufficient for probe to succeed. */
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = PCIBASE_DEVICE(opaque);
    /* Writes are ignored. Hardware state is not emulated yet. */
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
    s->intr_status = 0;
    s->intr_mask = 0;
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
        error_setg(errp, "PIO BAR not supported");
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;
    int ret;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_SOLARFLARE);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR0: MSI-X table + PBA (separate BAR, required for MSI-X) */
    memory_region_init_io(&s->msix_bar, OBJECT(s), &pcibase_mmio_ops, s,
                          "sfc-siena-msix", SFC_MSIX_BAR_SIZE);
    pci_register_bar(pdev, SFC_MSIX_BAR_IDX, PCI_BASE_ADDRESS_SPACE_MEMORY,
                     &s->msix_bar);

    /* BAR2: Main MMIO region */
    s->bar_info[0] = (BARInfo){
        .index = 2,
        .type = BAR_TYPE_MMIO,
        .size = MMIO_BAR_SIZE,
        .name = "sfc-siena-mmio"
    };
    s->num_bars = 1;
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI-X initialization */
    ret = msix_init(pdev, SFC_MSIX_MAX_VECTORS,
                    &s->msix_bar, SFC_MSIX_BAR_IDX, 0,
                    &s->msix_bar, SFC_MSIX_BAR_IDX, 0x1000,
                    0, errp);
    if (ret < 0) {
        error_setg(errp, "Failed to initialize MSI-X");
        return;
    }
    s->has_msix = true;
    s->has_msi = false;  /* MSI-X is preferred, disable MSI */
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
    .name = "sfc_siena_pci",
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
