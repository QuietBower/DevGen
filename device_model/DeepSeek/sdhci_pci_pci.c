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

#define PCI_VENDOR_ID_RICOH            0x1180
#define PCI_DEVICE_ID_RICOH_R5C822     0x0822

#define TYPE_PCIBASE_DEVICE "sdhci_pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Standard SDHCI Register Offsets */
#define SDHCI_DMA_ADDRESS       0x00
#define SDHCI_BLOCK_SIZE        0x04
#define SDHCI_BLOCK_COUNT       0x06
#define SDHCI_ARGUMENT          0x08
#define SDHCI_TRANSFER_MODE     0x0C
#define SDHCI_COMMAND           0x0E
#define SDHCI_RESPONSE          0x10
#define SDHCI_BUFFER            0x20
#define SDHCI_PRESENT_STATE     0x24
#define SDHCI_HOST_CONTROL      0x28
#define SDHCI_POWER_CONTROL     0x29
#define SDHCI_BLOCK_GAP_CONTROL 0x2A
#define SDHCI_WAKE_UP_CONTROL   0x2B
#define SDHCI_CLOCK_CONTROL     0x2C
#define SDHCI_TIMEOUT_CONTROL   0x2E
#define SDHCI_SOFTWARE_RESET    0x2F
#define SDHCI_INT_STATUS        0x30
#define SDHCI_INT_ENABLE        0x34
#define SDHCI_SIGNAL_ENABLE     0x38
#define SDHCI_AUTO_CMD_STATUS   0x3C
#define SDHCI_HOST_CONTROL2     0x3E
#define SDHCI_CAPABILITIES      0x40
#define SDHCI_CAPABILITIES_1    0x44
#define SDHCI_MAX_CURRENT       0x48
#define SDHCI_ADMA_ADDRESS      0x58
#define SDHCI_ADMA_ADDRESS_HI   0x5C
#define SDHCI_SLOT_INT_STATUS   0xFC
#define SDHCI_HOST_VERSION      0xFE

/* Interrupt bits (raw hardware bits) */
#define SDHCI_INT_RESPONSE     0x00000001
#define SDHCI_INT_DATA_END     0x00000002
#define SDHCI_INT_DMA_END      0x00000008
#define SDHCI_INT_SPACE_AVAIL  0x00000010
#define SDHCI_INT_DATA_AVAIL   0x00000020
#define SDHCI_INT_CARD_INSERT  0x00000040
#define SDHCI_INT_CARD_REMOVE  0x00000080
#define SDHCI_INT_CARD_INT     0x00000100
#define SDHCI_INT_ERROR        0x00008000
#define SDHCI_INT_TIMEOUT      0x00010000
#define SDHCI_INT_CRC          0x00020000
#define SDHCI_INT_END_BIT      0x00040000
#define SDHCI_INT_INDEX        0x00080000
#define SDHCI_INT_DATA_TIMEOUT 0x00100000
#define SDHCI_INT_DATA_CRC     0x00200000
#define SDHCI_INT_DATA_END_BIT 0x00400000
#define SDHCI_INT_BUS_POWER    0x00800000
#define SDHCI_INT_AUTO_CMD_ERR 0x01000000
#define SDHCI_INT_ADMA_ERROR   0x02000000
#define SDHCI_INT_TUNING_ERROR 0x04000000

/* Present state bits */
#define SDHCI_CMD_INHIBIT      0x00000001
#define SDHCI_DATA_INHIBIT     0x00000002
#define SDHCI_CARD_PRESENT     0x00010000
#define SDHCI_CD_STABLE        0x00020000
#define SDHCI_WRITE_PROTECT    0x00080000

/* Power control bits */
#define SDHCI_POWER_ON         0x01
#define SDHCI_POWER_180        0x0A
#define SDHCI_POWER_300        0x0C
#define SDHCI_POWER_330        0x0E

/* Clock control bits */
#define SDHCI_CLOCK_CARD_EN    0x0004
#define SDHCI_CLOCK_INT_EN     0x0001
#define SDHCI_CLOCK_INT_STABLE 0x0002

/* Software reset bits */
#define SDHCI_RESET_ALL        0x01
#define SDHCI_RESET_CMD        0x02
#define SDHCI_RESET_DATA       0x04

/* Host control bits */
#define SDHCI_CTRL_4BITBUS     0x02

/* Host version */
#define SDHCI_SPEC_300         2

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

#define SDHCI_NUM_SLOTS 2

/* Per-slot opaque for MMIO handlers */
typedef struct {
    PCIBaseState *parent;
    int slot;
} SlotOpaque;

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[SDHCI_NUM_SLOTS];  /* one per slot */
    BARInfo bar_info[SDHCI_NUM_SLOTS];
    int num_bars;

    bool has_msi;
    bool has_msix;

    uint8_t regs[SDHCI_NUM_SLOTS][256];
    uint32_t intr_status[SDHCI_NUM_SLOTS];
    uint32_t intr_mask[SDHCI_NUM_SLOTS];

    uint8_t pm_state;
    bool in_reset;

    SlotOpaque slot_opaque[SDHCI_NUM_SLOTS];
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool irq_asserted = false;
    int i;

    for (i = 0; i < SDHCI_NUM_SLOTS; i++) {
        uint32_t enabled = ldl_le_p(&s->regs[i][SDHCI_INT_ENABLE]);
        enabled |= ldl_le_p(&s->regs[i][SDHCI_SIGNAL_ENABLE]);
        s->intr_mask[i] = enabled;
        if (s->intr_status[i] & s->intr_mask[i]) {
            irq_asserted = true;
            break;
        }
    }

    if (irq_asserted) {
        if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!msi_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    SlotOpaque *so = opaque;
    PCIBaseState *s = so->parent;
    int slot = so->slot;
    uint64_t val = 0;

    if (addr >= 256) {
        return 0xffffffffffffffffULL;
    }

    switch (size) {
    case 1:
        val = s->regs[slot][addr];
        break;
    case 2:
        if (addr + 1 >= 256) {
            val = 0xffff;
        } else {
            val = lduw_le_p(&s->regs[slot][addr]);
        }
        break;
    case 4:
        if (addr + 3 >= 256) {
            val = 0xffffffff;
        } else {
            val = ldl_le_p(&s->regs[slot][addr]);
        }
        break;
    default:
        val = 0;
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    SlotOpaque *so = opaque;
    PCIBaseState *s = so->parent;
    int slot = so->slot;
    uint32_t old_intr_status;

    if (addr >= 256) {
        return;
    }

    switch (size) {
    case 1:
        s->regs[slot][addr] = (uint8_t)val;
        break;
    case 2:
        if (addr + 1 < 256) {
            stw_le_p(&s->regs[slot][addr], (uint16_t)val);
        }
        break;
    case 4:
        if (addr + 3 < 256) {
            stl_le_p(&s->regs[slot][addr], (uint32_t)val);
        }
        break;
    default:
        break;
    }

    /* Special register handling */
    switch (addr) {
    case SDHCI_CLOCK_CONTROL:
        if (size == 2) {
            uint16_t clk = lduw_le_p(&s->regs[slot][SDHCI_CLOCK_CONTROL]);
            if (clk & SDHCI_CLOCK_INT_EN) {
                clk |= SDHCI_CLOCK_INT_STABLE;
                stw_le_p(&s->regs[slot][SDHCI_CLOCK_CONTROL], clk);
            }
        }
        break;
    case SDHCI_INT_STATUS:
        old_intr_status = s->intr_status[slot];
        s->intr_status[slot] &= ~((uint32_t)val);
        stl_le_p(&s->regs[slot][SDHCI_INT_STATUS], s->intr_status[slot]);
        if (old_intr_status != s->intr_status[slot]) {
            pcibase_update_irq(s);
        }
        break;
    case SDHCI_INT_ENABLE:
    case SDHCI_SIGNAL_ENABLE:
        if (size == 4) {
            s->intr_mask[slot] = ldl_le_p(&s->regs[slot][SDHCI_INT_ENABLE]) |
                                 ldl_le_p(&s->regs[slot][SDHCI_SIGNAL_ENABLE]);
            pcibase_update_irq(s);
        }
        break;
    case SDHCI_SOFTWARE_RESET:
        if (val & SDHCI_RESET_ALL) {
            uint32_t caps = ldl_le_p(&s->regs[slot][SDHCI_CAPABILITIES]);
            uint32_t caps1 = ldl_le_p(&s->regs[slot][SDHCI_CAPABILITIES_1]);
            uint32_t max_curr = ldl_le_p(&s->regs[slot][SDHCI_MAX_CURRENT]);
            uint16_t version = lduw_le_p(&s->regs[slot][SDHCI_HOST_VERSION]);
            memset(s->regs[slot], 0, sizeof(s->regs[slot]));
            stl_le_p(&s->regs[slot][SDHCI_CAPABILITIES], caps);
            stl_le_p(&s->regs[slot][SDHCI_CAPABILITIES_1], caps1);
            stl_le_p(&s->regs[slot][SDHCI_MAX_CURRENT], max_curr);
            stw_le_p(&s->regs[slot][SDHCI_HOST_VERSION], version);
            s->intr_status[slot] = 0;
            pcibase_update_irq(s);
        } else if (val & SDHCI_RESET_CMD) {
        } else if (val & SDHCI_RESET_DATA) {
        }
        break;
    default:
        break;
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    int i;
    for (i = 0; i < SDHCI_NUM_SLOTS; i++) {
        uint32_t caps = 0x006421A1;
        uint32_t caps1 = 0;
        uint32_t max_current = 0;
        uint16_t version = SDHCI_SPEC_300;
        memset(s->regs[i], 0, 256);
        stl_le_p(&s->regs[i][SDHCI_CAPABILITIES], caps);
        stl_le_p(&s->regs[i][SDHCI_CAPABILITIES_1], caps1);
        stl_le_p(&s->regs[i][SDHCI_MAX_CURRENT], max_current);
        stw_le_p(&s->regs[i][SDHCI_HOST_VERSION], version);
        stl_le_p(&s->regs[i][SDHCI_PRESENT_STATE], SDHCI_CARD_PRESENT | SDHCI_CD_STABLE);
        s->intr_status[i] = 0;
        s->intr_mask[i] = 0;
    }
    pcibase_update_irq(s);
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_RICOH);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_RICOH_R5C822);
    pci_config_set_class(pci_conf, 0x0805);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    s->num_bars = 2;

    /* Slot 0 BAR0: 256 bytes */
    s->bar_info[0] = (BARInfo) {
        .index = 0,
        .type = BAR_TYPE_MMIO,
        .size = 0x100,
        .name = "sdhci-mmio0"
    };
    /* Slot 1 BAR1: 256 bytes */
    s->bar_info[1] = (BARInfo) {
        .index = 1,
        .type = BAR_TYPE_MMIO,
        .size = 0x100,
        .name = "sdhci-mmio1"
    };

    /* Initialize slot opaques */
    s->slot_opaque[0].parent = s;
    s->slot_opaque[0].slot = 0;
    s->slot_opaque[1].parent = s;
    s->slot_opaque[1].slot = 1;

    hwaddr aligned_size0 = pow2ceil(s->bar_info[0].size);
    memory_region_init_io(&s->bar_regions[0], OBJECT(s), &pcibase_mmio_ops,
                          &s->slot_opaque[0], s->bar_info[0].name, aligned_size0);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_regions[0]);

    hwaddr aligned_size1 = pow2ceil(s->bar_info[1].size);
    memory_region_init_io(&s->bar_regions[1], OBJECT(s), &pcibase_mmio_ops,
                          &s->slot_opaque[1], s->bar_info[1].name, aligned_size1);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_regions[1]);

    if (msi_init(pdev, 0, 1, true, false, errp) < 0) {
        error_setg(errp, "Failed to initialize MSI");
        return;
    }
    s->has_msi = true;
    s->has_msix = false;

    s->pm_state = 0;
    s->in_reset = false;

    /* Initialize registers for both slots */
    int i;
    for (i = 0; i < SDHCI_NUM_SLOTS; i++) {
        uint32_t caps = 0x006421A1;
        uint32_t caps1 = 0;
        uint32_t max_current = 0;
        uint16_t version = SDHCI_SPEC_300;
        memset(s->regs[i], 0, 256);
        stl_le_p(&s->regs[i][SDHCI_CAPABILITIES], caps);
        stl_le_p(&s->regs[i][SDHCI_CAPABILITIES_1], caps1);
        stl_le_p(&s->regs[i][SDHCI_MAX_CURRENT], max_current);
        stw_le_p(&s->regs[i][SDHCI_HOST_VERSION], version);
        stl_le_p(&s->regs[i][SDHCI_PRESENT_STATE], SDHCI_CARD_PRESENT | SDHCI_CD_STABLE);
        s->intr_status[i] = 0;
        s->intr_mask[i] = 0;
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

static const VMStateDescription vmstate_pcibase = {
    .name = "sdhci_pci_pci",
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
