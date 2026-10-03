/* QEMU device model for Intel 82975X EDAC memory controller */
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

#define TYPE_PCIBASE_DEVICE "i82975x_edac_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Hardware identifiers extracted from driver */
#define PCI_VENDOR_ID_INTEL 0x8086
#define PCI_DEVICE_ID_INTEL_82975_0 0x277c
#define VENDOR_ID PCI_VENDOR_ID_INTEL
#define DEVICE_ID PCI_DEVICE_ID_INTEL_82975_0
#define CLASS_ID 0x0500  /* Memory controller */

/* Register offsets from i82975x_edac.c */
#define I82975X_EAP       0x58
#define I82975X_DERRSYN   0x5c
#define I82975X_DES       0x5d
#define I82975X_ERRSTS    0xc8
#define I82975X_ERRCMD    0xca
#define I82975X_SMICMD    0xcc
#define I82975X_SCICMD    0xce
#define I82975X_XEAP      0xfc
#define I82975X_MCHBAR    0x44
#define I82975X_DRB_SHIFT 25
#define I82975X_DRB       0x100
#define I82975X_DRB_CH0R0 0x100
#define I82975X_DRB_CH0R1 0x101
#define I82975X_DRB_CH0R2 0x102
#define I82975X_DRB_CH0R3 0x103
#define I82975X_DRB_CH1R0 0x180
#define I82975X_DRB_CH1R1 0x181
#define I82975X_DRB_CH1R2 0x182
#define I82975X_DRB_CH1R3 0x183
#define I82975X_DRA       0x108
#define I82975X_DRA_CH0R01 0x108
#define I82975X_DRA_CH0R23 0x109
#define I82975X_DRA_CH1R01 0x188
#define I82975X_DRA_CH1R23 0x189
#define I82975X_BNKARC    0x10e
#define I82975X_C0BNKARC  0x10e
#define I82975X_C1BNKARC  0x18e
#define I82975X_DRC       0x120
#define I82975X_DRC_CH0M0 0x120
#define I82975X_DRC_CH1M0 0x1A0
#define I82975X_DRC_M1    0x124
#define I82975X_DRC_CH0M1 0x124
#define I82975X_DRC_CH1M1 0x1A4

/* BAR size: 16KB as indicated by driver */
#define I82975X_MCHBAR_SIZE 0x4000

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

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;

    /* Hardware Register Shadows */
    uint8_t regs[0x200];  /* MMIO register file */
};

/* MMIO Read/Write Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= sizeof(s->regs)) {
        qemu_log_mask(LOG_GUEST_ERROR, "i82975x_edac: MMIO read out of range, addr=0x%" HWADDR_PRIx "\n", addr);
        return 0;
    }

    switch (size) {
    case 1:
        val = s->regs[addr];
        break;
    case 2:
        val = lduw_le_p(&s->regs[addr]);
        break;
    case 4:
        val = ldl_le_p(&s->regs[addr]);
        break;
    case 8:
        val = ldq_le_p(&s->regs[addr]);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "i82975x_edac: unsupported MMIO read size %u at addr 0x%" HWADDR_PRIx "\n", size, addr);
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= sizeof(s->regs)) {
        qemu_log_mask(LOG_GUEST_ERROR, "i82975x_edac: MMIO write out of range, addr=0x%" HWADDR_PRIx "\n", addr);
        return;
    }

    switch (size) {
    case 1:
        s->regs[addr] = (uint8_t)val;
        break;
    case 2:
        stw_le_p(&s->regs[addr], (uint16_t)val);
        break;
    case 4:
        stl_le_p(&s->regs[addr], (uint32_t)val);
        break;
    case 8:
        stq_le_p(&s->regs[addr], val);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "i82975x_edac: unsupported MMIO write size %u at addr 0x%" HWADDR_PRIx "\n", size, addr);
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

/* Custom PCI config write to handle W1C for ERRSTS */
static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    /* Handle W1C for ERRSTS (0xc8, word) */
    if (addr == 0xc8 && len == 2) {
        uint16_t cur = pci_get_word(pdev->config + 0xc8);
        /* Clear bits that are written with 1 */
        uint16_t new_val = cur & ~(val & 0x0003);
        pci_set_word(pdev->config + 0xc8, new_val);
        return;
    }

    /* For all other registers, default behavior */
    pci_default_write_config(pdev, addr, val, len);
}

static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    /* Use default read; no special handling needed */
    return pci_default_read_config(pdev, addr, len);
}

/* Register a BAR with proper alignment */
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
    }
    /* Note: PIO and RAM BARs not used in this device */
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Reset MMIO registers to default values */
    memset(s->regs, 0, sizeof(s->regs));

    /* Set DRC CH0M0 and CH1M0 to enable ECC: bits 22:21 = 01 */
    stl_le_p(&s->regs[I82975X_DRC_CH0M0], 0x00200000);
    stl_le_p(&s->regs[I82975X_DRC_CH1M0], 0x00200000);

    /* Set DRB values for dual channel: ch0 = {0x08,0x10,0x18,0x20}, ch1 = same */
    s->regs[I82975X_DRB_CH0R0] = 0x08;
    s->regs[I82975X_DRB_CH0R1] = 0x10;
    s->regs[I82975X_DRB_CH0R2] = 0x18;
    s->regs[I82975X_DRB_CH0R3] = 0x20;
    s->regs[I82975X_DRB_CH1R0] = 0x08;
    s->regs[I82975X_DRB_CH1R1] = 0x10;
    s->regs[I82975X_DRB_CH1R2] = 0x18;
    s->regs[I82975X_DRB_CH1R3] = 0x20;

    /* Other registers remain zero (DRA, BNKARC, etc.) */
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID, VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* Set custom config read/write handlers */
    pdev->config_read = pcibase_config_read;
    pdev->config_write = pcibase_config_write;

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = I82975X_MCHBAR_SIZE;
    s->bar_info[0].name = "i82975x_mchbar";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No MSI/MSI-X, no DMA, no timers */
}

static void pcibase_uninit(PCIDevice *pdev)
{
    /* No special cleanup needed aside from possible MSI/MSI-X which we don't use */
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

static const VMStateDescription vmstate_pcibase = {
    .name = "i82975x_edac_pci",
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
