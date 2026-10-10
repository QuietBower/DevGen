/*
 * QEMU TPCI200 PCI carrier device model (Phase 2 - Implementation)
 * Extracted from Linux driver: /home/eely/linux-7.1/drivers/ipack/carriers/tpci200.c
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

#define TYPE_PCIBASE_DEVICE "tpci200_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define TPCI200_VENDOR_ID       0x1498
#define TPCI200_DEVICE_ID       0x30C8
#define TPCI200_SUBVENDOR_ID    0x1498
#define TPCI200_SUBDEVICE_ID    0x300A
#define TPCI200_CLASS           0x0680

#define TPCI200_NB_SLOT         0x4

/* BAR indices */
#define TPCI200_CFG_MEM_BAR         0
#define TPCI200_IP_INTERFACE_BAR    2
#define TPCI200_IO_ID_INT_SPACES_BAR 3
#define TPCI200_MEM16_SPACE_BAR     4
#define TPCI200_MEM8_SPACE_BAR      5

/* BAR sizes */
#define TPCI200_CFG_MEM_SIZE            0x400000
#define TPCI200_IFACE_SIZE              0x100
#define TPCI200_IO_ID_INT_SPACES_SIZE   0x100
#define TPCI200_MEM16_SPACE_SIZE        0x00800000
#define TPCI200_MEM8_SPACE_SIZE         0x00400000
#define TPCI200_MEM16_SPACE_BAR_SIZE    (TPCI200_NB_SLOT * TPCI200_MEM16_SPACE_SIZE)
#define TPCI200_MEM8_SPACE_BAR_SIZE     (TPCI200_NB_SLOT * TPCI200_MEM8_SPACE_SIZE)

/* Interface register offsets (in BAR2) */
#define TPCI200_REG_REVISION    0x00
#define TPCI200_REG_CONTROL_A   0x02
#define TPCI200_REG_CONTROL_B   0x04
#define TPCI200_REG_CONTROL_C   0x06
#define TPCI200_REG_CONTROL_D   0x08
#define TPCI200_REG_RESET       0x0A
#define TPCI200_REG_STATUS      0x0C

/* Control/Status bits */
#define TPCI200_INT0_EN         0x0040
#define TPCI200_INT1_EN         0x0080
#define TPCI200_SLOT_INT_MASK   0x00FF
#define TPCI200_A_INT0          0x0001
#define TPCI200_A_INT1          0x0002
#define TPCI200_A_TIMEOUT       0x1000
#define TPCI200_A_ERROR         0x0100
#define TPCI200_B_TIMEOUT       0x2000
#define TPCI200_B_ERROR         0x0200
#define TPCI200_C_TIMEOUT       0x4000
#define TPCI200_C_ERROR         0x0400
#define TPCI200_D_TIMEOUT       0x8000
#define TPCI200_D_ERROR         0x0800
#define TPCI200_CLK32           0x0001

/* CFG MEM register offsets (in BAR0) */
#define LAS1_DESC               0x2C
#define LAS2_DESC               0x30

#define LAS_BIT_BIGENDIAN       0   /* assumed bit position for byte swap disable */

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

    /* Interface register storage (BAR2) */
    uint16_t revision;
    uint16_t control[4];
    uint16_t reset;
    uint16_t status;

    /* CFG MEM registers (BAR0) */
    uint32_t las1_desc;
    uint32_t las2_desc;
};

/* Forward declarations of ops structures */
static const MemoryRegionOps pcibase_bar0_ops;
static const MemoryRegionOps pcibase_bar2_ops;
static const MemoryRegionOps pcibase_bar_dummy_ops;

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    int i;
    bool raise = false;

    for (i = 0; i < TPCI200_NB_SLOT; i++) {
        uint16_t slot_int_mask = (TPCI200_A_INT0 | TPCI200_A_INT1) << (2 * i);
        if ((s->control[i] & (TPCI200_INT0_EN | TPCI200_INT1_EN)) &&
            (s->status & slot_int_mask)) {
            raise = true;
            break;
        }
    }

    pci_set_irq(pdev, raise ? 1 : 0);
}

/* ---------- BAR0 (CFG MEM) read/write ---------- */
static uint64_t pcibase_bar0_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case LAS1_DESC:
        if (size >= 4) val = s->las1_desc;
        break;
    case LAS2_DESC:
        if (size >= 4) val = s->las2_desc;
        break;
    default:
        break;
    }
    return val;
}

static void pcibase_bar0_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case LAS1_DESC:
        if (size >= 4) s->las1_desc = val;
        break;
    case LAS2_DESC:
        if (size >= 4) s->las2_desc = val;
        break;
    default:
        break;
    }
}

/* ---------- BAR2 (IP Interface) read/write ---------- */
static uint64_t pcibase_bar2_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case TPCI200_REG_REVISION:   /* 0x00 */
        val = s->revision;
        break;
    case TPCI200_REG_CONTROL_A:  /* 0x02 */
        val = s->control[0];
        break;
    case TPCI200_REG_CONTROL_B:  /* 0x04 */
        val = s->control[1];
        break;
    case TPCI200_REG_CONTROL_C:  /* 0x06 */
        val = s->control[2];
        break;
    case TPCI200_REG_CONTROL_D:  /* 0x08 */
        val = s->control[3];
        break;
    case TPCI200_REG_RESET:      /* 0x0A */
        val = s->reset;
        break;
    case TPCI200_REG_STATUS:     /* 0x0C */
        val = s->status;
        break;
    default:
        break;
    }
    return val;
}

static void pcibase_bar2_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case TPCI200_REG_REVISION:
        /* Read-only */
        break;
    case TPCI200_REG_CONTROL_A:
        s->control[0] = val;
        pcibase_update_irq(s);
        break;
    case TPCI200_REG_CONTROL_B:
        s->control[1] = val;
        pcibase_update_irq(s);
        break;
    case TPCI200_REG_CONTROL_C:
        s->control[2] = val;
        pcibase_update_irq(s);
        break;
    case TPCI200_REG_CONTROL_D:
        s->control[3] = val;
        pcibase_update_irq(s);
        break;
    case TPCI200_REG_RESET:
        s->reset = val;
        break;
    case TPCI200_REG_STATUS:
        /* W1C for error/timeout bits, others possibly read-only */
        s->status &= ~(val & 0xFF00);
        pcibase_update_irq(s);
        break;
    default:
        break;
    }
}

/* ---------- Dummy BARs (BAR3,4,5) read/write ---------- */
static uint64_t pcibase_bar_dummy_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_bar_dummy_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* No side effects */
}

/* ---------- MemoryRegionOps definitions ---------- */
static const MemoryRegionOps pcibase_bar0_ops = {
    .read = pcibase_bar0_read,
    .write = pcibase_bar0_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static const MemoryRegionOps pcibase_bar2_ops = {
    .read = pcibase_bar2_read,
    .write = pcibase_bar2_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static const MemoryRegionOps pcibase_bar_dummy_ops = {
    .read = pcibase_bar_dummy_read,
    .write = pcibase_bar_dummy_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* ---------- Reset ---------- */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    s->revision = 0x1;
    memset(s->control, 0, sizeof(s->control));
    s->reset = 0;
    s->status = 0;
    s->las1_desc = 0;
    s->las2_desc = 0;
}

/* ---------- BAR registration helper ---------- */
static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];
    const MemoryRegionOps *ops = NULL;

    switch (bi->index) {
    case TPCI200_CFG_MEM_BAR:
        ops = &pcibase_bar0_ops;
        break;
    case TPCI200_IP_INTERFACE_BAR:
        ops = &pcibase_bar2_ops;
        break;
    default:
        ops = &pcibase_bar_dummy_ops;
        break;
    }

    memory_region_init_io(mr, OBJECT(s), ops, s, bi->name, aligned_size);
    pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
}

/* ---------- Device realize ---------- */
static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  TPCI200_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  TPCI200_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, TPCI200_CLASS);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_VENDOR_ID, TPCI200_SUBVENDOR_ID);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_ID, TPCI200_SUBDEVICE_ID);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR Initialization */
    s->num_bars = 0;
    #define ADD_BAR(idx, bar_type, sz, nm) do { \
        s->bar_info[s->num_bars].index = (idx); \
        s->bar_info[s->num_bars].type = (bar_type); \
        s->bar_info[s->num_bars].size = (sz); \
        s->bar_info[s->num_bars].name = (nm); \
        s->num_bars++; \
    } while(0)

    ADD_BAR(TPCI200_CFG_MEM_BAR, BAR_TYPE_MMIO, TPCI200_CFG_MEM_SIZE, "tpci200-cfg-mem");
    ADD_BAR(TPCI200_IP_INTERFACE_BAR, BAR_TYPE_MMIO, TPCI200_IFACE_SIZE, "tpci200-ip-interface");
    ADD_BAR(TPCI200_IO_ID_INT_SPACES_BAR, BAR_TYPE_MMIO, TPCI200_IO_ID_INT_SPACES_SIZE, "tpci200-io-id-int");
    ADD_BAR(TPCI200_MEM16_SPACE_BAR, BAR_TYPE_MMIO, TPCI200_MEM16_SPACE_BAR_SIZE, "tpci200-mem16");
    ADD_BAR(TPCI200_MEM8_SPACE_BAR, BAR_TYPE_MMIO, TPCI200_MEM8_SPACE_BAR_SIZE, "tpci200-mem8");
    #undef ADD_BAR

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No MSI support: driver uses legacy IRQ */
    s->has_msi = false;
    s->has_msix = false;

    /* Final state initialization */
    s->revision = 0x1;
    memset(s->control, 0, sizeof(s->control));
    s->reset = 0;
    s->status = 0;
    s->las1_desc = 0;
    s->las2_desc = 0;
}

/* ---------- Device uninit ---------- */
static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

/* ---------- VMState ---------- */
static const VMStateDescription vmstate_pcibase = {
    .name = "tpci200_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT16_ARRAY(control, PCIBaseState, 4),
        VMSTATE_UINT16(reset, PCIBaseState),
        VMSTATE_UINT16(status, PCIBaseState),
        VMSTATE_UINT16(revision, PCIBaseState),
        VMSTATE_UINT32(las1_desc, PCIBaseState),
        VMSTATE_UINT32(las2_desc, PCIBaseState),
        VMSTATE_END_OF_LIST()
    }
};

/* ---------- Class Init ---------- */
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

/* ---------- Type Registration ---------- */
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
