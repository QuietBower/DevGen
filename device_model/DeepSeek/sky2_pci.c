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

/*
 * QEMU PCI device model for Marvell Yukon 2 (sky2 driver)
 * Phase 4: Runtime refinement to fix probe failure.
 * Based on linux driver /home/eely/linux-7.1/drivers/net/ethernet/marvell/sky2.c
 */

#define TYPE_PCIBASE_DEVICE "sky2_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Define missing vendor ID */
#define PCI_VENDOR_ID_SYSKONNECT 0x1148

/* -------------------------------------------------------------------------- */
/* Register offsets (from typical sky2.h) */
/* -------------------------------------------------------------------------- */
#define VENDOR_ID PCI_VENDOR_ID_SYSKONNECT
#define DEVICE_ID 0x9000
#define CLASS_ID  PCI_CLASS_NETWORK_ETHERNET

/* Block 0 registers */
#define B0_CTST          0x0100
#define B0_IMSK          0x000c
#define B0_ISRC          0x0008
#define B0_POWER_CTRL    0x0110
#define B0_Y2_SP_ICR     0x0120
#define B0_Y2_SP_ISRC2   0x0128

/* Block 2 registers */
#define B2_CHIP_ID       0x0274
#define B2_MAC_CFG       0x0276
#define B2_PMD_TYP       0x0109
#define B2_E_0           0x010c
#define B2_Y2_HW_RES     0x0288
#define B2_Y2_CLK_GATE   0x028c

/* Status unit registers */
#define STAT_LIST_ADDR_LO 0x0A00
#define STAT_LIST_ADDR_HI 0x0A04
#define STAT_CTRL         0x0A08
#define STAT_LAST_IDX     0x0A0C
#define STAT_TX_IDX_TH    0x0A10
#define STAT_FIFO_WM      0x0A14
#define STAT_FIFO_ISR_WM  0x0A18
#define STAT_TX_TIMER_INI 0x0A20
#define STAT_ISR_TIMER_INI 0x0A24
#define STAT_LEV_TIMER_INI 0x0A28
#define STAT_TX_TIMER_CTRL 0x0A30
#define STAT_LEV_TIMER_CTRL 0x0A34
#define STAT_ISR_TIMER_CTRL 0x0A38
#define STAT_PUT_IDX      0x0A40

/* PCI extended config space base (within MMIO) */
#define Y2_CFG_SPC        0x1C00

/* Control/Status bits */
#define CS_RST_SET         0x01
#define CS_RST_CLR         0x02
#define CS_MRST_CLR        0x04
#define CS_ST_SW_IRQ       0x10
#define CS_CL_SW_IRQ       0x20

/* Chip ID values */
#define CHIP_ID_YUKON_EC_U 0xB4
#define CFG_CHIP_R_MSK     0x0F

/* Interrupt bits */
#define Y2_IS_IRQ_SW       0x00000001 /* simplified, actual bit may differ */

/* PCI config space registers */
#define PCI_DEV_REG2       0x44

/* -------------------------------------------------------------------------- */
/* End of register definitions */
/* -------------------------------------------------------------------------- */

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
    uint32_t intr_status;  /* B0_Y2_SP_ISRC2 and other interrupt sources */
    uint32_t intr_mask;    /* B0_IMSK */
    uint32_t hw_flags;     /* sky2_hw.flags -- unused for now */
    bool reset_active;
    uint8_t power_state;

    /* Flat register array (16KB) */
    uint8_t regs[0x4000];
};

G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t active = s->intr_status & s->intr_mask;
    if (active) {
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
    PCIBaseState *s = opaque;
    uint32_t val = 0;

    /* Ensure we don't read past array */
    if (addr + size > sizeof(s->regs)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds MMIO read at 0x%" HWADDR_PRIx " size %d\n",
                      __func__, addr, size);
        return ~0ULL;
    }

    switch (addr) {
    /* Interrupt status register: dynamic, not stored in regs[] */
    case B0_Y2_SP_ISRC2:
        val = s->intr_status;
        break;
    default:
        /* For all other registers, return stored value from regs array
         * with proper access size handling. */
        memcpy(&val, s->regs + addr, MIN(size, sizeof(val)));
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr + size > sizeof(s->regs)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds MMIO write at 0x%" HWADDR_PRIx " size %d\n",
                      __func__, addr, size);
        return;
    }

    switch (addr) {
    case B0_CTST:
        /* Handle control/status register */
        /* Store the write value in regs for possible reads */
        memcpy(s->regs + addr, &val, MIN(size, sizeof(val)));
        if (val & CS_ST_SW_IRQ) {
            s->intr_status |= Y2_IS_IRQ_SW;
            pcibase_update_irq(s);
        }
        if (val & CS_CL_SW_IRQ) {
            s->intr_status &= ~Y2_IS_IRQ_SW;
            pcibase_update_irq(s);
        }
        break;
    case B0_IMSK:
        memcpy(s->regs + addr, &val, MIN(size, sizeof(val)));
        s->intr_mask = val;
        pcibase_update_irq(s);
        break;
    case B0_Y2_SP_ICR:
        /* Write to ICR: typically to acknowledge interrupts.
           The real driver writes 2 after handling SW interrupt.
           We'll treat it as Write-1-to-Clear for all bits. */
        s->intr_status &= ~val;
        pcibase_update_irq(s);
        break;
    /* Read-only registers: ignore writes */
    case B2_CHIP_ID:
    case B2_MAC_CFG:
    case B2_PMD_TYP:
    case B2_E_0:
    case B2_Y2_HW_RES:
    case B2_Y2_CLK_GATE:
        break;
    default:
        /* General register write: store to regs array */
        memcpy(s->regs + addr, &val, MIN(size, sizeof(val)));
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
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Clear all registers */
    memset(s->regs, 0, sizeof(s->regs));
    /* Set identity registers to default values */
    s->regs[B2_CHIP_ID] = CHIP_ID_YUKON_EC_U;
    s->regs[B2_MAC_CFG] = 0x00; /* rev 0 */
    s->regs[B2_PMD_TYP] = 'C';
    s->regs[B2_E_0] = 0;
    s->regs[B2_Y2_HW_RES] = 0; /* single port */
    s->regs[B2_Y2_CLK_GATE] = 0;

    s->intr_status = 0;
    s->intr_mask = 0;
    pcibase_update_irq(s);
}

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

    /* Set PCI_DEV_REG2 appropriately */
    pci_set_long(pci_conf + PCI_DEV_REG2, 0x00000000);

    /* BAR 0: MMIO, 16KB */
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x4000;
    s->bar_info[0].name = "sky2-mmio";
    s->num_bars = 1;
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    msi_init(pdev, 0, 1, true, false, errp);
    s->has_msi = true;
    s->has_msix = false;

    /* Initialize register array to zeros */
    memset(s->regs, 0, sizeof(s->regs));
    /* Set chip identity registers */
    s->regs[B2_CHIP_ID] = CHIP_ID_YUKON_EC_U;
    s->regs[B2_MAC_CFG] = 0x00;
    s->regs[B2_PMD_TYP] = 'C';
    s->regs[B2_E_0] = 0;
    s->regs[B2_Y2_HW_RES] = 0;
    s->regs[B2_Y2_CLK_GATE] = 0;

    s->power_state = 0;
    s->reset_active = false;
    s->intr_status = 0;
    s->intr_mask = 0;
    s->hw_flags = 0;
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
    .name = "sky2_pci",
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
