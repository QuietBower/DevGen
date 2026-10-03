/*
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

#define TYPE_PCIBASE_DEVICE "i2c_designware_pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */

#define VENDOR_ID 0x8086
#define DEVICE_ID 0x0817
#define CLASS_ID  0x0C05

/* Helper macros to match driver definitions */
#define BIT(x) (1UL << (x))
#define GENMASK(h, l) (((1UL << ((h) - (l) + 1)) - 1) << (l))

/* DesignWare I2C register offsets and constants */
#define DW_IC_SLAVE                    1
#define DW_IC_MASTER                   0
#define DW_IC_CON                      0x00
#define DW_IC_CON_SPEED_MASK           GENMASK(2, 1)
#define DW_IC_CON_SPEED_HIGH           (3 << 1)
#define DW_IC_CON_SPEED_FAST           (2 << 1)
#define DW_IC_CON_SPEED_STD            (1 << 1)
#define DW_IC_CON_SLAVE_DISABLE        BIT(6)
#define DW_IC_CON_MASTER               BIT(0)
#define DW_IC_CON_10BITADDR_MASTER     BIT(4)
#define DW_IC_CON_BUS_CLEAR_CTRL       BIT(11)
#define DW_IC_CON_RESTART_EN           BIT(5)
#define DW_IC_CON_RX_FIFO_FULL_HLD_CTRL BIT(9)
#define DW_IC_CON_STOP_DET_IFADDRESSED BIT(7)
#define DW_IC_TAR                      0x04
#define DW_IC_TAR_10BITADDR_MASTER     BIT(12)
#define DW_IC_SAR                      0x08
#define DW_IC_DATA_CMD                 0x10
#define DW_IC_SS_SCL_HCNT              0x14
#define DW_IC_SS_SCL_LCNT              0x18
#define DW_IC_FS_SCL_HCNT              0x1c
#define DW_IC_FS_SCL_LCNT              0x20
#define DW_IC_HS_SCL_HCNT              0x24
#define DW_IC_HS_SCL_LCNT              0x28
#define DW_IC_INTR_STAT                0x2c
#define DW_IC_INTR_MASK                0x30
#define DW_IC_INTR_DEFAULT_MASK        (DW_IC_INTR_RX_FULL | \
                                         DW_IC_INTR_TX_ABRT | \
                                         DW_IC_INTR_STOP_DET)
#define DW_IC_INTR_MASTER_MASK         (DW_IC_INTR_DEFAULT_MASK | \
                                         DW_IC_INTR_TX_EMPTY)
#define DW_IC_INTR_SLAVE_MASK          (DW_IC_INTR_DEFAULT_MASK | \
                                         DW_IC_INTR_RX_UNDER | \
                                         DW_IC_INTR_RD_REQ)
#define DW_IC_INTR_RX_FULL             BIT(2)
#define DW_IC_INTR_TX_ABRT             BIT(6)
#define DW_IC_INTR_STOP_DET            BIT(9)
#define DW_IC_INTR_TX_EMPTY            BIT(4)
#define DW_IC_INTR_RX_UNDER            BIT(0)
#define DW_IC_INTR_RD_REQ              BIT(5)
#define DW_IC_RX_TL                    0x38
#define DW_IC_TX_TL                    0x3c
#define DW_IC_CLR_INTR                 0x40
#define DW_IC_STATUS                   0x70
#define DW_IC_STATUS_ACTIVITY          BIT(0)
#define DW_IC_SDA_HOLD                 0x7c
#define DW_IC_SDA_HOLD_MIN_VERS        0x3131312A
#define DW_IC_SDA_HOLD_RX_MASK         GENMASK(23, 16)
#define DW_IC_SDA_HOLD_RX_SHIFT        16
#define DW_IC_ENABLE_STATUS            0x9c
#define DW_IC_COMP_PARAM_1             0xf4
#define DW_IC_COMP_PARAM_1_SPEED_MODE_HIGH (BIT(2) | BIT(3))
#define DW_IC_COMP_PARAM_1_SPEED_MODE_MASK GENMASK(3, 2)
#define DW_IC_COMP_VERSION             0xf8
#define DW_IC_COMP_TYPE                0xfc
#define DW_IC_COMP_TYPE_VALUE          0x44570140
#define DW_IC_SMBUS_INTR_MASK          0xcc
#define DW_IC_FIFO_TX_FIELD            GENMASK(23, 16)
#define DW_IC_FIFO_RX_FIELD            GENMASK(15, 8)
#define DW_IC_FIFO_MIN_DEPTH           2
#define TXGBE_TX_FIFO_DEPTH            4
#define TXGBE_RX_FIFO_DEPTH            1

#define DW_IC_ENABLE                   0x6c   /* Enable register, missing in original template */

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
    uint32_t intr_status;
    uint32_t intr_mask;
    bool irq_asserted;         /* MSI edge-triggered state */

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t regs[0x40];   /* Covers offsets 0x00-0xfc (64 x 32-bit) */
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool level = (s->intr_status & s->intr_mask) != 0;
    if (msi_enabled(pdev)) {
        if (level && !s->irq_asserted) {
            msi_notify(pdev, 0);
            s->irq_asserted = true;
        } else if (!level && s->irq_asserted) {
            s->irq_asserted = false;
        }
    } else {
        pci_set_irq(pdev, level);
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = 0;

    if (addr >= 0x100 || (addr & 3)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Unaligned or out-of-range read at 0x%"HWADDR_PRIx"\n", __func__, addr);
        return 0;
    }

    uint32_t reg_idx = addr >> 2;
    val = s->regs[reg_idx];

    switch (addr) {
    case DW_IC_COMP_TYPE:
        val = 0x44570140;
        break;
    case DW_IC_INTR_STAT:
        val = s->intr_status; /* raw interrupt status */
        break;
    case DW_IC_ENABLE_STATUS:
        /* Read-only; reflect enable state */
        if (s->regs[DW_IC_ENABLE >> 2] & 1) {
            val = 1;
        } else {
            val = 0;
        }
        break;
    default:
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= 0x100 || (addr & 3)) {
        return;
    }

    uint32_t reg_idx = addr >> 2;

    switch (addr) {
    case DW_IC_INTR_STAT:
        /* Read-only register, ignore writes */
        break;
    case DW_IC_INTR_MASK:
        s->intr_mask = val;
        s->regs[reg_idx] = val;
        pcibase_update_irq(s);
        break;
    case DW_IC_CLR_INTR:
        s->intr_status &= ~val; /* Write-1-to-clear */
        pcibase_update_irq(s);
        break;
    case DW_IC_ENABLE:
        s->regs[reg_idx] = val;
        if (val & 1) {
            s->regs[DW_IC_ENABLE_STATUS >> 2] = 1;
        } else {
            s->regs[DW_IC_ENABLE_STATUS >> 2] = 0;
        }
        break;
    default:
        s->regs[reg_idx] = val;
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

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Set power-on defaults for all registers */
    memset(s->regs, 0, sizeof(s->regs));

    /* Component identification registers */
    s->regs[DW_IC_COMP_TYPE >> 2] = 0x44570140;
    s->regs[DW_IC_COMP_PARAM_1 >> 2] = (TXGBE_TX_FIFO_DEPTH << 16) | (TXGBE_RX_FIFO_DEPTH << 8) | DW_IC_COMP_PARAM_1_SPEED_MODE_HIGH;
    s->regs[DW_IC_COMP_VERSION >> 2] = DW_IC_SDA_HOLD_MIN_VERS;

    /* Interrupt and status registers */
    s->intr_status = 0;
    s->intr_mask = 0;
    s->irq_asserted = false;
    s->regs[DW_IC_INTR_STAT >> 2] = 0; /* optional, but clear */
    s->regs[DW_IC_INTR_MASK >> 2] = 0;
    s->regs[DW_IC_ENABLE_STATUS >> 2] = 0; /* disabled */
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
        /* PIO is not used by this driver, thus no ops implemented */
        error_setg(errp, "PIO BAR not supported");
        return;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* MSI capability */
    if (msi_init(pdev, 0, 1, true, false, errp)) {
        return;
    }
    s->has_msi = true;

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x100; /* Covers register space 0x00-0xfc */
    s->bar_info[0].name = "bar0";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
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

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "i2c_designware_pci_pci",
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
