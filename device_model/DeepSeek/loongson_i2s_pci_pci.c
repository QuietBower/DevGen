/*
 * Integrated QEMU PCI device template (QEMU 8.2.10) for loongson_i2s_pci.
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
#include "hw/pci/pci_device.h"
#include "hw/pci/pci_bus.h"
#include "hw/pci/pcie.h"
#include "hw/pci/msix.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-core.h"
#include "migration/vmstate.h"
#include "hw/acpi/aml-build.h"

#define PCI_VENDOR_ID_LOONGSON 0x0014

#define TYPE_PCIBASE_DEVICE "loongson_i2s_pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define LS_I2S_VER        0x00
#define LS_I2S_CFG        0x04
#define LS_I2S_CTRL       0x08
#define LS_I2S_RX_DATA    0x0C
#define LS_I2S_TX_DATA    0x10
#define LS_I2S_CFG1       0x14
#define LS_I2S_TX_ORDER   0x100
#define LS_I2S_RX_ORDER   0x110
#define I2S_CTRL_RESET    BIT(4)

#define VENDOR_ID PCI_VENDOR_ID_LOONGSON
#define DEVICE_ID 0x7a27
#define CLASS_ID PCI_CLASS_MULTIMEDIA_AUDIO

/* Shadow register structure */
typedef struct {
    uint32_t ver;
    uint32_t cfg;
    uint32_t ctrl;
    uint32_t rx_data;
    uint32_t tx_data;
    uint32_t cfg1;
    uint32_t tx_order;
    uint32_t rx_order;
} LoongsonI2SRegs;

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

    /* Hardware Register Shadows */
    LoongsonI2SRegs regs;
};

/* MMIO Read Handler */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case LS_I2S_VER:
        val = s->regs.ver;
        break;
    case LS_I2S_CFG:
        val = s->regs.cfg;
        break;
    case LS_I2S_CTRL:
        val = s->regs.ctrl;
        break;
    case LS_I2S_RX_DATA:
        val = s->regs.rx_data;
        break;
    case LS_I2S_TX_DATA:
        val = s->regs.tx_data;
        break;
    case LS_I2S_CFG1:
        val = s->regs.cfg1;
        break;
    case LS_I2S_TX_ORDER:
        val = s->regs.tx_order;
        break;
    case LS_I2S_RX_ORDER:
        val = s->regs.rx_order;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad read offset 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        break;
    }
    return val;
}

/* MMIO Write Handler */
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case LS_I2S_VER:
        /* Read-only register, ignore writes */
        break;
    case LS_I2S_CFG:
        s->regs.cfg = val;
        break;
    case LS_I2S_CTRL:
        s->regs.ctrl = val;
        break;
    case LS_I2S_RX_DATA:
        s->regs.rx_data = val;
        break;
    case LS_I2S_TX_DATA:
        s->regs.tx_data = val;
        break;
    case LS_I2S_CFG1:
        s->regs.cfg1 = val;
        break;
    case LS_I2S_TX_ORDER:
        s->regs.tx_order = val;
        break;
    case LS_I2S_RX_ORDER:
        s->regs.rx_order = val;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad write offset 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        break;
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
    /* Reset all hardware registers to defaults */
    memset(&s->regs, 0, sizeof(s->regs));
    /* Set a non-zero version to match revision 1 */
    s->regs.ver = 0x1;
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
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

/* Removed ACPI AML callback entirely to fix compile errors */
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

    /* Set subsystem vendor and device IDs to match platform firmware expectations */
    pci_set_word(pci_conf + PCI_SUBSYSTEM_VENDOR_ID, 0x0014); /* Loongson */
    pci_set_word(pci_conf + PCI_SUBSYSTEM_ID, 0x7a27);

    /* PCIe Capabilities */
    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* No MSI-X initialization, rely on legacy INTx interrupts */

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo){
        .index = 0,
        .type = BAR_TYPE_MMIO,
        .size = 0x200,
        .name = "i2s-mmio"
    };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    /* No cleanup needed as MSI-X was removed */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "loongson_i2s_pci_pci",
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