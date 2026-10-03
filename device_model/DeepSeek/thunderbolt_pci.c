/*
 * QEMU Thunderbolt NHI Controller Emulation
 * Generated for Linux driver nhi.c
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

#define TYPE_PCIBASE_DEVICE "thunderbolt_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* NHI Register Offsets and Masks */
#define REG_RING_INTERRUPT_BASE          0x38200
#define REG_RING_INTERRUPT_MASK_CLEAR_BASE 0x38208
#define REG_RING_INT_CLEAR               0x37808
#define REG_RING_NOTIFY_BASE             0x37800
#define REG_DMA_MISC                     0x39864
#define REG_DMA_MISC_INT_AUTO_CLEAR      BIT(2)
#define REG_DMA_MISC_DISABLE_AUTO_CLEAR  BIT(17)
#define REG_INT_VEC_ALLOC_BASE           0x38c40
#define REG_INT_VEC_ALLOC_REGS           (32 / REG_INT_VEC_ALLOC_BITS)
#define REG_INT_VEC_ALLOC_BITS           4
#define REG_INT_VEC_ALLOC_MASK           GENMASK(3, 0)
#define REG_RX_RING_BASE                 0x08000
#define REG_TX_RING_BASE                 0x00000
#define REG_RX_OPTIONS_BASE              0x29800
#define REG_TX_OPTIONS_BASE              0x19800
#define REG_RX_OPTIONS_E2E_HOP_MASK      GENMASK(22, 12)
#define REG_RX_OPTIONS_E2E_HOP_SHIFT     12
#define REG_INMAIL_CMD                   0x39904
#define REG_INMAIL_CMD_MASK              GENMASK(7, 0)
#define REG_INMAIL_DATA                  0x39900
#define REG_INMAIL_ERROR                 BIT(30)
#define REG_INMAIL_OP_REQUEST            BIT(31)
#define REG_OUTMAIL_CMD                  0x3990c
#define REG_OUTMAIL_CMD_OPMODE_MASK      GENMASK(11, 8)
#define REG_OUTMAIL_CMD_OPMODE_SHIFT     8
#define REG_INT_THROTTLING_RATE          0x38c00
#define REG_RESET                        0x39898
#define REG_RESET_HRR                    BIT(0)
#define REG_CAPS                         0x39640
#define REG_CAPS_VERSION_MASK            GENMASK(23, 16)
#define REG_CAPS_VERSION_2               0x40
#define REG_FW_STS                       0x39944
#define REG_FW_STS_NVM_AUTH_DONE         BIT(31)
#define REG_FW_STS_ICM_EN                BIT(0)
#define REG_FW_STS_CIO_RESET_REQ         BIT(30)
#define REG_FW_STS_ICM_EN_CPU            BIT(2)
#define REG_FW_STS_ICM_EN_INVERT         BIT(1)

#define MSIX_MIN_VECS                    6
#define MSIX_MAX_VECS                    16

#define NHI_MMIO_SIZE                    0x40000

/* PCI Vendor/Device IDs */
#define PCI_VENDOR_ID_INTEL              0x8086
#define PCI_DEVICE_ID_INTEL_LIGHT_RIDGE  0x1513

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
    MemoryRegion mmio_io_region; /* IO region for MMIO BAR ops */
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware Register Shadows */
    uint32_t reg_reset;     /* REG_RESET */
    /* Add other registers as needed */
};

/* MMIO read handler */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case REG_CAPS:
        /* Version 2, hop_count = 32 */
        val = (REG_CAPS_VERSION_2 << 16) | 0x20;
        break;
    case REG_RESET:
        val = s->reg_reset;
        break;
    default:
        /* Return 0 for all other registers */
        val = 0;
        break;
    }

    return val;
}

/* MMIO write handler */
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case REG_RESET:
        /* Writing RESET_HRR triggers reset; clear the bit to indicate completion */
        if (val & REG_RESET_HRR) {
            s->reg_reset = 0; /* Auto-clear after write */
        } else {
            s->reg_reset = val;
        }
        break;
    default:
        /* Ignore writes to other registers */
        break;
    }
}

/* PIO handlers (unused) */
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
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Reset register state */
    s->reg_reset = 0;
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
        memory_region_init(mr, OBJECT(s), bi->name, aligned_size);
        memory_region_init_io(&s->mmio_io_region, OBJECT(s), &pcibase_mmio_ops, s,
                              "nhi-mmio-ops", aligned_size);
        memory_region_add_subregion_overlap(mr, 0, &s->mmio_io_region, 0);
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
    int ret;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_INTEL);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_INTEL_LIGHT_RIDGE);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_SYSTEM_OTHER);
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
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = NHI_MMIO_SIZE;
    s->bar_info[0].name = "nhi-mmio";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI-X initialization */
    ret = msix_init(pdev, MSIX_MAX_VECS,
                    &s->bar_regions[0], 0, 0x10000,
                    &s->bar_regions[0], 0, 0x20000,
                    0, errp);
    if (ret < 0) {
        return;
    }

    /* Initialize register state */
    s->reg_reset = 0;
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }

    /* No additional resources to free */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "thunderbolt_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(reg_reset, PCIBaseState),
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
