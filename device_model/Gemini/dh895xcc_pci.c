/*
 * Integrated QEMU PCI device template (QEMU 8.2.10).
 * Designed for register-level modeling and PCIe driver probing.
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

#ifndef BIT
#define BIT(nr) (1UL << (nr))
#endif

#define TYPE_PCIBASE_DEVICE "dh895xcc_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_DEVICE_ID_INTEL_QAT_DH895XCC 0x0435
#define ADF_DH895XCC_SRAM_BAR 0
#define ADF_DH895XCC_PMISC_BAR 1
#define ADF_DH895XCC_ETR_BAR 2
#define ADF_DEVICE_FUSECTL_OFFSET 0x40
#define ADF_DEVICE_LEGFUSE_OFFSET 0x4C

#define ADF_MAILBOX_BASE_OFFSET	0x20970
#define ADF_ADMINMSGUR_OFFSET	(0x3A000 + 0x574)
#define ADF_ADMINMSGLR_OFFSET	(0x3A000 + 0x578)
#define ADF_ARB_CONFIG			(BIT(31) | BIT(6) | BIT(0))
#define ADF_ARB_OFFSET			0x30000
#define ADF_ARB_WRK_2_SER_MAP_OFFSET	0x180
#define ADF_GEN2_SMIAPF0_MASK_OFFSET    (0x3A000 + 0x28)
#define ADF_GEN2_SMIAPF1_MASK_OFFSET    (0x3A000 + 0x30)
#define ADF_GEN2_SMIA1_MASK             0x1
#define ADF_GEN2_AE_CTX_ENABLES(i)	((i) * 0x1000 + 0x20818)
#define ADF_GEN2_AE_MISC_CONTROL(i)	((i) * 0x1000 + 0x20960)
#define ADF_GEN2_UERRSSMSH(i)		((i) * 0x4000 + 0x18)
#define ADF_GEN2_CERRSSMSH(i)		((i) * 0x4000 + 0x10)
#define ADF_GEN2_ERRMSK3 (0x3A000 + 0x1C)
#define ADF_GEN2_ERRMSK5 (0x3A000 + 0xDC)
#define ADF_GEN2_ERRSOU3 (0x3A000 + 0x0C)
#define ADF_GEN2_ERRSOU5 (0x3A000 + 0xD8)

/* Inferred default sizes to ensure compilation */
#define ADF_DH895XCC_SRAM_BAR_SIZE 0x1000
#define ADF_DH895XCC_PMISC_BAR_SIZE 0x40000
#define ADF_DH895XCC_ETR_BAR_SIZE 0x1000

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

    /* PMISC BAR State */
    uint32_t adminmsg_ur;
    uint32_t adminmsg_lr;
    uint32_t smiapf0_mask;
    uint32_t smiapf1_mask;
    uint32_t errmsk3;
    uint32_t errmsk5;
    uint32_t errsou3;
    uint32_t errsou5;
};

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = PCIBASE_DEVICE(opaque);
    uint64_t val = 0;

    switch (addr) {
    case ADF_ADMINMSGUR_OFFSET:
        val = s->adminmsg_ur;
        break;
    case ADF_ADMINMSGLR_OFFSET:
        val = s->adminmsg_lr;
        break;
    case ADF_GEN2_SMIAPF0_MASK_OFFSET:
        val = s->smiapf0_mask;
        break;
    case ADF_GEN2_SMIAPF1_MASK_OFFSET:
        val = s->smiapf1_mask;
        break;
    case ADF_GEN2_ERRMSK3:
        val = s->errmsk3;
        break;
    case ADF_GEN2_ERRMSK5:
        val = s->errmsk5;
        break;
    case ADF_GEN2_ERRSOU3:
        val = s->errsou3;
        break;
    case ADF_GEN2_ERRSOU5:
        val = s->errsou5;
        break;
    default:
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = PCIBASE_DEVICE(opaque);

    switch (addr) {
    case ADF_ADMINMSGUR_OFFSET:
        s->adminmsg_ur = val;
        break;
    case ADF_ADMINMSGLR_OFFSET:
        s->adminmsg_lr = val;
        break;
    case ADF_GEN2_SMIAPF0_MASK_OFFSET:
        s->smiapf0_mask = val;
        break;
    case ADF_GEN2_SMIAPF1_MASK_OFFSET:
        s->smiapf1_mask = val;
        break;
    case ADF_GEN2_ERRMSK3:
        s->errmsk3 = val;
        break;
    case ADF_GEN2_ERRMSK5:
        s->errmsk5 = val;
        break;
    case ADF_GEN2_ERRSOU3:
        s->errsou3 = val;
        break;
    case ADF_GEN2_ERRSOU5:
        s->errsou5 = val;
        break;
    default:
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_INTEL);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_INTEL_QAT_DH895XCC);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_OTHERS);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    
    /* Allocate PM capability at 0x50 to avoid overwriting FUSECTL at 0x40 */
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0x50, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* Initialize FUSECTL and LEGFUSE registers to pass driver mask checks.
     * The driver uses ~fuses to calculate masks, so setting to 0 ensures non-zero masks. */
    pci_set_long(pci_conf + ADF_DEVICE_FUSECTL_OFFSET, 0x00000000);
    pci_set_long(pci_conf + ADF_DEVICE_LEGFUSE_OFFSET, 0x00000000);

    /* BAR Initialization */
    s->num_bars = 3;
    s->bar_info[0] = (BARInfo){ .index = ADF_DH895XCC_SRAM_BAR, .type = BAR_TYPE_MMIO, .size = ADF_DH895XCC_SRAM_BAR_SIZE, .name = "sram" };
    s->bar_info[1] = (BARInfo){ .index = ADF_DH895XCC_PMISC_BAR, .type = BAR_TYPE_MMIO, .size = ADF_DH895XCC_PMISC_BAR_SIZE, .name = "pmisc" };
    s->bar_info[2] = (BARInfo){ .index = ADF_DH895XCC_ETR_BAR, .type = BAR_TYPE_MMIO, .size = ADF_DH895XCC_ETR_BAR_SIZE, .name = "etr" };
    
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
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
        VMSTATE_UINT32(adminmsg_ur, PCIBaseState),
        VMSTATE_UINT32(adminmsg_lr, PCIBaseState),
        VMSTATE_UINT32(smiapf0_mask, PCIBaseState),
        VMSTATE_UINT32(smiapf1_mask, PCIBaseState),
        VMSTATE_UINT32(errmsk3, PCIBaseState),
        VMSTATE_UINT32(errmsk5, PCIBaseState),
        VMSTATE_UINT32(errsou3, PCIBaseState),
        VMSTATE_UINT32(errsou5, PCIBaseState),
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
