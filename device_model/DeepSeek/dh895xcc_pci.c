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

#define TYPE_PCIBASE_DEVICE "dh895xcc_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define ADF_DH895XCC_FUSECTL_SKU_1 0x0
#define ADF_DH895XCC_FUSECTL_SKU_3 0x2
#define ADF_DH895XCC_FUSECTL_SKU_SHIFT 20
#define ADF_DH895XCC_FUSECTL_SKU_4 0x3
#define ADF_DH895XCC_FUSECTL_SKU_MASK 0x300000
#define ADF_DH895XCC_FUSECTL_SKU_2 0x1
#define ADF_DH895XCC_ACCELERATORS_MASK 0x3F
#define ADF_DH895XCC_ACCELERATORS_REG_OFFSET 13
#define ADF_DH895XCC_ACCELENGINES_MASK 0xFFF
#define ADF_DH895XCC_MAX_ACCELENGINES 12
#define ADF_DH895XCC_MAX_ACCELERATORS 6
#define ADF_DH895X_AE_FREQ (933 * HZ_PER_MHZ)
#define ADF_DH895XCC_MMP "qat_895xcc_mmp.bin"
#define ADF_DH895XCC_FW "qat_895xcc.bin"
#define ADF_DH895XCC_ETR_MAX_BANKS 32
#define ADF_DH895XCC_ERR_REG_VF2PF_L(vf_src)	(((vf_src) & 0x01FFFE00) >> 9)
#define ADF_DH895XCC_ERR_MSK_VF2PF_U(vf_mask)	((vf_mask) >> 16)
#define ADF_DH895XCC_ERR_REG_VF2PF_U(vf_src)	(((vf_src) & 0x0000FFFF) << 16)
#define ADF_DH895XCC_ERR_MSK_VF2PF_L(vf_mask)	(((vf_mask) & 0xFFFF) << 9)
#define ADF_DH895XCC_SRAM_BAR 0
#define ADF_DH895XCC_AE2FUNC_MAP_GRP_B_NUM_REGS 12
#define ADF_DH895XCC_AE2FUNC_MAP_GRP_A_NUM_REGS 96
#define ADF_DH895XCC_PMISC_BAR 1
#define ADF_DH895XCC_ETR_BAR 2
#define PCI_DEVICE_ID_INTEL_QAT_DH895XCC 0x0435
#define ADF_DH895XCC_DEVICE_NAME "dh895xcc"

/* Placeholder definitions for missing macros from driver headers. */
/* These will be refined when the actual values are provided. */
#define ADF_DH895XCC_SRAM_SIZE  0x1000      /* 4KB */
#define ADF_DH895XCC_PMISC_SIZE 0x40000     /* 256KB – to accommodate register offsets up to 0x3A0DC */
#define ADF_DH895XCC_ETR_SIZE   0x200000    /* 2MB – for ring buffers */
#define ADF_DH895XCC_MSIX_BAR_SIZE 0x1000   /* 4KB – for MSI-X table and PBA */
#define ADH895XCC_CLASS_ID      0x0b4000    /* PCI class code – placeholder */
#define ADF_DEVICE_FUSECTL_OFFSET 0x40      /* Configuration offset – confirmed from driver */

/* Number of bundles/banks – placeholder; real value needed from driver headers */
#define ADF_DH895XCC_NUM_BANKS 32  /* likely derived from ETR_MAX_BANKS or static config */

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
};

/* MMIO/PIO Handlers – currently not used as all BARs are RAM; preserved for future expansion */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x8086 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_INTEL_QAT_DH895XCC );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, ADH895XCC_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Provide valid fuses at config offset 0x40 to satisfy driver mask checks */
    pci_set_long(pci_conf + ADF_DEVICE_FUSECTL_OFFSET, 0x00002000);
    /* Set second fuse dword to provide non-zero accelerator engines mask */
    pci_set_long(pci_conf + ADF_DEVICE_FUSECTL_OFFSET + 4, 0x00000FFF);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 4;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_RAM, .size = ADF_DH895XCC_SRAM_SIZE, .name = "dh895xcc-sram" };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_RAM, .size = ADF_DH895XCC_PMISC_SIZE, .name = "dh895xcc-pmisc" };
    s->bar_info[2] = (BARInfo){ .index = 2, .type = BAR_TYPE_RAM, .size = ADF_DH895XCC_ETR_SIZE, .name = "dh895xcc-etr" };
    s->bar_info[3] = (BARInfo){ .index = 3, .type = BAR_TYPE_RAM, .size = ADF_DH895XCC_MSIX_BAR_SIZE, .name = "dh895xcc-msix" };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI-X initialization – using exclusive BAR to prevent table/PBA overlap */
    if (msix_init_exclusive_bar(pdev, ADF_DH895XCC_NUM_BANKS + 1, 3, errp)) {
        if (errp && !*errp) {
            error_setg(errp, "Failed to initialize MSI-X");
        }
        return;
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, &s->bar_regions[3], &s->bar_regions[3]);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "dh895xcc_pci",
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
