/*
 * QEMU Intel QuickAssist Technology C62x PCI Device Model
 * Based on Linux driver adf_drv.c for probing support
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

#define TYPE_PCIBASE_DEVICE "c6xx_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Vendor and Device IDs */
#ifndef PCI_VENDOR_ID_INTEL
#define PCI_VENDOR_ID_INTEL 0x8086
#endif
#ifndef PCI_DEVICE_ID_INTEL_QAT_C62X
#define PCI_DEVICE_ID_INTEL_QAT_C62X 0x37c8
#endif
#ifndef PCI_CLASS_CRYPTO_OTHER
#define PCI_CLASS_CRYPTO_OTHER 0x1080
#endif

/* BAR indices from driver (adf_drv.c) */
#define ADF_C62X_SRAM_BAR   0
#define ADF_C62X_PMISC_BAR  1
#define ADF_C62X_ETR_BAR    2

/* Register offsets and masks from driver */
#define ADF_C62X_SOFTSTRAP_CSR_OFFSET         0x2EC
#define ADF_C62X_ACCELERATORS_REG_OFFSET      16
#define ADF_C62X_ACCELERATORS_MASK            0x1F
#define ADF_C62X_MAX_ACCELERATORS             5
#define ADF_C62X_ACCELENGINES_MASK            0x3FF
#define ADF_C62X_MAX_ACCELENGINES             10

/* BAR sizes (placeholder values) */
#ifndef BAR0_SIZE
#define BAR0_SIZE (4 * 1024 * 1024)  /* 4 MiB */
#endif
#ifndef BAR1_SIZE
#define BAR1_SIZE (4 * 1024 * 1024)  /* 4 MiB */
#endif
#ifndef BAR2_SIZE
#define BAR2_SIZE (64 * 1024)        /* 64 KiB */
#endif

/* Fuses control offset from driver */
#define ADF_DEVICE_FUSECTL_OFFSET  0x40

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

    /* PMISC BAR shadow registers (accel masks, etc.) */
    uint32_t reg_accel_count;
    uint32_t reg_engines_count;
};

/* MMIO handlers for BAR 0 (SRAM) - dummy */
static uint64_t pcibase_bar0_read(void *opaque, hwaddr addr, unsigned size)
{
    /* SRAM: returns 0, writes ignored */
    return 0;
}

static void pcibase_bar0_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* SRAM: ignore writes */
}

static const MemoryRegionOps pcibase_bar0_ops = {
    .read = pcibase_bar0_read,
    .write = pcibase_bar0_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

/* MMIO handlers for BAR 1 (PMISC) - control/status registers */
static uint64_t pcibase_bar1_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case ADF_C62X_ACCELERATORS_REG_OFFSET:
        val = s->reg_accel_count;
        break;
    case 0x14: /* Accelerator Engines count register (offset guessed) */
        val = s->reg_engines_count;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "QAT C62x: unknown BAR1 read at 0x%" HWADDR_PRIx "\n", addr);
        break;
    }
    return val;
}

static void pcibase_bar1_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* All useful registers are read-only; ignore writes */
    qemu_log_mask(LOG_UNIMP, "QAT C62x: unknown BAR1 write at 0x%" HWADDR_PRIx " val 0x%" PRIx64 "\n", addr, val);
}

static const MemoryRegionOps pcibase_bar1_ops = {
    .read = pcibase_bar1_read,
    .write = pcibase_bar1_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

/* MMIO handlers for BAR 2 (ETR) - dummy */
static uint64_t pcibase_bar2_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_bar2_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static const MemoryRegionOps pcibase_bar2_ops = {
    .read = pcibase_bar2_read,
    .write = pcibase_bar2_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    /* Reset any local state if necessary; currently nothing to reset */
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    const MemoryRegionOps *ops = NULL;
    switch (bi->index) {
    case ADF_C62X_SRAM_BAR:
        ops = &pcibase_bar0_ops;
        break;
    case ADF_C62X_PMISC_BAR:
        ops = &pcibase_bar1_ops;
        break;
    case ADF_C62X_ETR_BAR:
        ops = &pcibase_bar2_ops;
        break;
    default:
        error_setg(errp, "Invalid BAR index %d", bi->index);
        return;
    }

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), ops, s, bi->name, aligned_size);
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

    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_INTEL);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_INTEL_QAT_C62X);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_CRYPTO_OTHER);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* Set up config registers that the driver reads */
    pci_set_long(pci_conf + ADF_C62X_SOFTSTRAP_CSR_OFFSET, 0x00000000);
    pci_set_long(pci_conf + ADF_DEVICE_FUSECTL_OFFSET, 0x00000000);

    /* Initialize PMISC shadow registers */
    s->reg_accel_count = ADF_C62X_MAX_ACCELERATORS;  /* 5, gives mask 0x1F */
    s->reg_engines_count = ADF_C62X_MAX_ACCELENGINES; /* 10, gives mask 0x3FF */

    /* BAR Initialization */
    s->num_bars = 3;
    s->bar_info[0] = (BARInfo){ .index = ADF_C62X_SRAM_BAR, .type = BAR_TYPE_MMIO, .size = BAR0_SIZE, .name = "bar0-sram" };
    s->bar_info[1] = (BARInfo){ .index = ADF_C62X_PMISC_BAR, .type = BAR_TYPE_MMIO, .size = BAR1_SIZE, .name = "bar1-pmisc" };
    s->bar_info[2] = (BARInfo){ .index = ADF_C62X_ETR_BAR, .type = BAR_TYPE_MMIO, .size = BAR2_SIZE, .name = "bar2-etr" };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No MSI/MSI-X needed for probe */
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
    .name = "c6xx_pci",
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
