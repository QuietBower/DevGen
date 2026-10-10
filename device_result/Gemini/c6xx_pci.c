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
#include "qemu/bitops.h"
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

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_DEVICE_ID_INTEL_QAT_C62X 0x37c8
#define ADF_C62X_SOFTSTRAP_CSR_OFFSET 0x2EC
#define ADF_C62X_ACCELERATORS_REG_OFFSET 16
#define ADF_C62X_ACCELERATORS_MASK 0x1F
#define ADF_C62X_MAX_ACCELERATORS 5
#define ADF_C62X_ACCELENGINES_MASK 0x3FF
#define ADF_C62X_MAX_ACCELENGINES 10
#define ADF_C62X_ETR_MAX_BANKS 16
#define ADF_C62X_SRAM_BAR 0
#define ADF_C62X_AE2FUNC_MAP_GRP_A_NUM_REGS 80
#define ADF_C62X_AE2FUNC_MAP_GRP_B_NUM_REGS 10
#define ADF_C62X_PMISC_BAR 1
#define ADF_C62X_ETR_BAR 2

#define ADF_DEVICE_FUSECTL_OFFSET 0x40
#define ADF_DEVICE_FUSECTL_MASK 0x80000000
#define ADF_PCI_MAX_BARS 3
#define ADF_C62X_DEVICE_NAME "c6xx"

#define ADF_ETR_MAX_RINGS_PER_BANK 16
#define ADF_GEN2_RX_RINGS_OFFSET 8
#define ADF_GEN2_TX_RINGS_MASK 0xFF
#define ADF_C62X_FW "qat_c62x.bin"
#define ADF_C62X_MMP "qat_c62x_mmp.bin"

/* Newly added definitions */
#define ADF_GEN2_SMIAPF0_MASK_OFFSET    (0x3A000 + 0x28)
#define ADF_GEN2_SMIAPF1_MASK_OFFSET    (0x3A000 + 0x30)
#define ADF_GEN2_SMIA1_MASK             0x1
#define ADF_MAILBOX_BASE_OFFSET         0x20970
#define ADF_ADMINMSGUR_OFFSET           (0x3A000 + 0x574)
#define ADF_ADMINMSGLR_OFFSET           (0x3A000 + 0x578)
#define ADF_ARB_CONFIG                  (BIT(31) | BIT(6) | BIT(0))
#define ADF_ARB_OFFSET                  0x30000
#define ADF_ARB_WRK_2_SER_MAP_OFFSET    0x180
#define ADF_ARB_NUM                     4

typedef enum {
    DEV_SKU_2,
    DEV_SKU_4,
    DEV_SKU_UNKNOWN
} dev_sku_info;

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
    
    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t fusectl;
    uint32_t smiapf0_mask;
    uint32_t smiapf1_mask;
    uint32_t adminmsgur;
    uint32_t adminmsglr;

    /* DMA Context */
};

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case ADF_GEN2_SMIAPF0_MASK_OFFSET:
        val = s->smiapf0_mask;
        break;
    case ADF_GEN2_SMIAPF1_MASK_OFFSET:
        val = s->smiapf1_mask;
        break;
    case ADF_ADMINMSGUR_OFFSET:
        val = s->adminmsgur;
        break;
    case ADF_ADMINMSGLR_OFFSET:
        val = s->adminmsglr;
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
    case ADF_GEN2_SMIAPF0_MASK_OFFSET:
        s->smiapf0_mask = val;
        break;
    case ADF_GEN2_SMIAPF1_MASK_OFFSET:
        s->smiapf1_mask = val;
        break;
    case ADF_ADMINMSGUR_OFFSET:
        s->adminmsgur = val;
        break;
    case ADF_ADMINMSGLR_OFFSET:
        s->adminmsglr = val;
        break;
    default:
        break;
    }
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

    /* Initialize FUSECTL and SOFTSTRAP_CSR to 0 to allow driver to proceed */
    pci_set_long(PCI_DEVICE(dev)->config + ADF_DEVICE_FUSECTL_OFFSET, 0x00000000);
    pci_set_long(PCI_DEVICE(dev)->config + ADF_C62X_SOFTSTRAP_CSR_OFFSET, 0x00000000);

    s->smiapf0_mask = 0;
    s->smiapf1_mask = 0;
    s->adminmsgur = 0;
    s->adminmsglr = 0;
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }
    
    /* CRITICAL: PCI requires BAR sizes to be a power of 2. Prevent QEMU assert crash. */
    hwaddr aligned_size = pow2ceil(bi->size);
    if (aligned_size == 0) {
        aligned_size = 4096;
    }
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_INTEL );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_INTEL_QAT_C62X );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_OTHERS );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    
    /* Enable PCIe FLR capability as required by adf_reset_flr */
    pcie_cap_flr_init(pdev);

    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 3;
    s->bar_info[0].index = ADF_C62X_SRAM_BAR;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x1000;
    s->bar_info[0].name = "sram";

    s->bar_info[1].index = ADF_C62X_PMISC_BAR;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = 0x40000; /* Increased to cover 0x3A000 offsets */
    s->bar_info[1].name = "pmisc";

    s->bar_info[2].index = ADF_C62X_ETR_BAR;
    s->bar_info[2].type = BAR_TYPE_MMIO;
    s->bar_info[2].size = 0x1000;
    s->bar_info[2].name = "etr";  
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* msi_init or msix_init calls */
    s->has_msix = true;
    /* The driver calls adf_enable_msix, so we initialize MSI-X. 
     * Using PMISC BAR (1) as a placeholder for MSI-X table and PBA until exact offsets are known. */
    if (msix_init(pdev, 1, &s->bar_regions[ADF_C62X_PMISC_BAR], ADF_C62X_PMISC_BAR, 0,
                  &s->bar_regions[ADF_C62X_PMISC_BAR], ADF_C62X_PMISC_BAR, 0x800, 0, errp)) {
        return;
    }

    /* Set DMA masks or ring buffer limits */
    /* Initialize internal hardware timers if used */
    /* Final state initialization before the device is 'live' */
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, &s->bar_regions[ADF_C62X_PMISC_BAR], &s->bar_regions[ADF_C62X_PMISC_BAR]);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }

     /* Free buffers, stop timers, etc. */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
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
