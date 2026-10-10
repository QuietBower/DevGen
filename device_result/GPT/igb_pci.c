/*
 * QEMU PCI device model for Intel igb - minimal functional model for driver probe/bind
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

#include "hw/net/igb_regs.h"

#define TYPE_PCIBASE_DEVICE "igb_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define IGB_VENDOR_ID_INTEL          0x8086
#define IGB_DEVICE_ID_I354_BACKPLANE_1GBPS 0x1F40
#define IGB_PCI_CLASS_NETWORK_ETHERNET 0x0200

#define E1000_DEV_ID_I354_BACKPLANE_1GBPS   0x1F40
#define E1000_DEV_ID_I354_SGMII             0x1F41
#define E1000_DEV_ID_I354_BACKPLANE_2_5GBPS 0x1F45
#define E1000_DEV_ID_I211_COPPER            0x1539
#define E1000_DEV_ID_I210_COPPER            0x1533
#define E1000_DEV_ID_I210_FIBER             0x1536
#define E1000_DEV_ID_I210_SERDES            0x1537
#define E1000_DEV_ID_I210_SGMII             0x1538
#define E1000_DEV_ID_I210_COPPER_FLASHLESS  0x157B
#define E1000_DEV_ID_I210_SERDES_FLASHLESS  0x157C
#define E1000_DEV_ID_I350_COPPER            0x1521
#define E1000_DEV_ID_I350_FIBER             0x1522
#define E1000_DEV_ID_I350_SERDES            0x1523
#define E1000_DEV_ID_I350_SGMII             0x1524
#define E1000_DEV_ID_82580_COPPER           0x150E
#define E1000_DEV_ID_82580_FIBER            0x150F
#define E1000_DEV_ID_82580_QUAD_FIBER       0x1527
#define E1000_DEV_ID_82580_SERDES           0x1510
#define E1000_DEV_ID_82580_SGMII            0x1511
#define E1000_DEV_ID_82580_COPPER_DUAL      0x1516
#define E1000_DEV_ID_DH89XXCC_SGMII         0x0438
#define E1000_DEV_ID_DH89XXCC_SERDES        0x043A
#define E1000_DEV_ID_DH89XXCC_BACKPLANE     0x043C
#define E1000_DEV_ID_DH89XXCC_SFP           0x0440
#define E1000_DEV_ID_82576                  0x10C9
#define E1000_DEV_ID_82576_NS               0x150A
#define E1000_DEV_ID_82576_NS_SERDES        0x1518
#define E1000_DEV_ID_82576_FIBER            0x10E6
#define E1000_DEV_ID_82576_SERDES           0x10E7
#define E1000_DEV_ID_82576_SERDES_QUAD      0x150D
#define E1000_DEV_ID_82576_QUAD_COPPER_ET2  0x1526
#define E1000_DEV_ID_82576_QUAD_COPPER      0x10E8
#define E1000_DEV_ID_82575EB_COPPER         0x10A7
#define E1000_DEV_ID_82575EB_FIBER_SERDES   0x10A9
#define E1000_DEV_ID_82575GB_QUAD_COPPER    0x10D6

#define E1000_CTRL     0x00000
#define E1000_STATUS   0x00008
#define E1000_CTRL_EXT 0x00018
#define E1000_ICR      0x000C0
#define E1000_RCTL     0x00100
#define E1000_TCTL     0x00400
#define E1000_TDFH     0x03410
#define E1000_TDFT     0x03418
#define E1000_TDFHS    0x03420
#define E1000_TDFPC    0x03430

#define E1000_RDLEN(_n)   ((_n) < 4 ? (0x02808 + ((_n) * 0x100)) \
                                    : (0x0C008 + ((_n) * 0x40)))
#define E1000_TDBAH(_n)   ((_n) < 4 ? (0x03804 + ((_n) * 0x100)) \
                                    : (0x0E004 + ((_n) * 0x40)))
#define E1000_RDBAL(_n)   ((_n) < 4 ? (0x02800 + ((_n) * 0x100)) \
                                    : (0x0C000 + ((_n) * 0x40)))
#define E1000_TDBAL(_n)   ((_n) < 4 ? (0x03800 + ((_n) * 0x100)) \
                                    : (0x0E000 + ((_n) * 0x40)))
#define E1000_TXDCTL(_n)  ((_n) < 4 ? (0x03828 + ((_n) * 0x100)) \
                                    : (0x0E028 + ((_n) * 0x40)))
#define E1000_RDT(_n)     ((_n) < 4 ? (0x02818 + ((_n) * 0x100)) \
                                    : (0x0C018 + ((_n) * 0x40)))
#define E1000_RDH(_n)     ((_n) < 4 ? (0x02810 + ((_n) * 0x100)) \
                                    : (0x0C010 + ((_n) * 0x40)))
#define E1000_TDH(_n)     ((_n) < 4 ? (0x03810 + ((_n) * 0x100)) \
                                    : (0x0E010 + ((_n) * 0x40)))
#define E1000_RDBAH(_n)   ((_n) < 4 ? (0x02804 + ((_n) * 0x100)) \
                                    : (0x0C004 + ((_n) * 0x40)))
#define E1000_TDT(_n)     ((_n) < 4 ? (0x03818 + ((_n) * 0x100)) \
                                    : (0x0E018 + ((_n) * 0x40)))
#define E1000_TDLEN(_n)   ((_n) < 4 ? (0x03808 + ((_n) * 0x100)) \
                                    : (0x0E008 + ((_n) * 0x40)))
#define E1000_RXDCTL(_n)  ((_n) < 4 ? (0x02828 + ((_n) * 0x100)) \
                                    : (0x0C028 + ((_n) * 0x40)))

#define IGB_FLAG_HAS_MSIX   (1U << 13)
#define IGB_FLAG_HAS_MSI    (1U << 0)

#define E1000_RXDCTL_QUEUE_ENABLE  0x02000000
#define E1000_TXDCTL_QUEUE_ENABLE  0x02000000

#define E1000_ICR_LSC 0x00000004
#define E1000_ICR_RXT0 0x00000080

#define E1000_IMS 0x000D0
#define E1000_IMC 0x000D8

#define E1000_CTRL_RST 0x04000000
#define E1000_CTRL_SLU 0x00000040

#define E1000_STATUS_LU 0x00000002
#define E1000_STATUS_SPEED_1000 0x00000080
#define E1000_STATUS_FD 0x00000001

#define E1000_RCTL_EN 0x00000002
#define E1000_TCTL_EN 0x00000002

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
    uint32_t intr_status;
    uint32_t intr_mask;

    struct {
        uint32_t ctrl;
        uint32_t status;
        uint32_t ctrl_ext;
        uint32_t icr;
        uint32_t rctl;
        uint32_t tctl;
        uint32_t rdlen0;
        uint32_t rdh0;
        uint32_t rdt0;
        uint32_t rxdctl0;
        uint32_t rdbal0;
        uint32_t rdbah0;
        uint32_t tdbal0;
        uint32_t tdbah0;
        uint32_t tdlen0;
        uint32_t tdh0;
        uint32_t tdt0;
        uint32_t txdctl0;
        uint32_t tdfh;
        uint32_t tdft;
        uint32_t tdfhs;
        uint32_t tdfpc;
    } regs;

    uint32_t status_flags;
    uint32_t reset_state;
    uint8_t power_state;
};

static void pcibase_raise_irq(PCIBaseState *s, uint32_t cause)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    s->regs.icr |= cause;
    if (!(s->intr_mask & cause)) {
        return;
    }
    if (msix_enabled(pdev)) {
        msix_notify(pdev, 0);
    } else if (msi_enabled(pdev)) {
        msi_notify(pdev, 0);
    } else {
        pci_set_irq(pdev, 1);
    }
}

static void pcibase_lower_legacy_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (!msix_enabled(pdev) && !msi_enabled(pdev)) {
        pci_set_irq(pdev, 0);
    }
}

static uint32_t pcibase_reg_read(PCIBaseState *s, uint32_t addr)
{
    switch (addr) {
    case E1000_CTRL:
        return s->regs.ctrl;
    case E1000_STATUS:
        return s->regs.status;
    case E1000_CTRL_EXT:
        return s->regs.ctrl_ext;
    case E1000_ICR:
        return s->regs.icr;
    case E1000_RCTL:
        return s->regs.rctl;
    case E1000_TCTL:
        return s->regs.tctl;
    case E1000_TDFH:
        return s->regs.tdfh;
    case E1000_TDFT:
        return s->regs.tdft;
    case E1000_TDFHS:
        return s->regs.tdfhs;
    case E1000_TDFPC:
        return s->regs.tdfpc;
    default:
        if (addr == E1000_RDLEN(0)) {
            return s->regs.rdlen0;
        } else if (addr == E1000_RDBAL(0)) {
            return s->regs.rdbal0;
        } else if (addr == E1000_RDBAH(0)) {
            return s->regs.rdbah0;
        } else if (addr == E1000_RDH(0)) {
            return s->regs.rdh0;
        } else if (addr == E1000_RDT(0)) {
            return s->regs.rdt0;
        } else if (addr == E1000_RXDCTL(0)) {
            return s->regs.rxdctl0;
        } else if (addr == E1000_TDBAL(0)) {
            return s->regs.tdbal0;
        } else if (addr == E1000_TDBAH(0)) {
            return s->regs.tdbah0;
        } else if (addr == E1000_TDLEN(0)) {
            return s->regs.tdlen0;
        } else if (addr == E1000_TDH(0)) {
            return s->regs.tdh0;
        } else if (addr == E1000_TDT(0)) {
            return s->regs.tdt0;
        } else if (addr == E1000_TXDCTL(0)) {
            return s->regs.txdctl0;
        }
        break;
    }
    return 0;
}

static void pcibase_reg_write(PCIBaseState *s, uint32_t addr, uint32_t val)
{
    switch (addr) {
    case E1000_CTRL:
        if (val & E1000_CTRL_RST) {
            memset(&s->regs, 0, sizeof(s->regs));
            s->regs.status = E1000_STATUS_LU | E1000_STATUS_SPEED_1000 | E1000_STATUS_FD;
            s->regs.ctrl = val & ~E1000_CTRL_RST;
        } else {
            s->regs.ctrl = val;
        }
        break;
    case E1000_CTRL_EXT:
        s->regs.ctrl_ext = val;
        break;
    case E1000_RCTL:
        s->regs.rctl = val;
        break;
    case E1000_TCTL:
        s->regs.tctl = val;
        break;
    case E1000_ICR:
        s->regs.icr &= ~val;
        if (s->regs.icr == 0) {
            pcibase_lower_legacy_irq(s);
        }
        break;
    case E1000_IMS:
        s->intr_mask |= val;
        break;
    case E1000_IMC:
        s->intr_mask &= ~val;
        if (s->intr_mask == 0) {
            pcibase_lower_legacy_irq(s);
        }
        break;
    default:
        if (addr == E1000_RDLEN(0)) {
            s->regs.rdlen0 = val;
        } else if (addr == E1000_RDBAL(0)) {
            s->regs.rdbal0 = val;
        } else if (addr == E1000_RDBAH(0)) {
            s->regs.rdbah0 = val;
        } else if (addr == E1000_RDH(0)) {
            s->regs.rdh0 = val;
        } else if (addr == E1000_RDT(0)) {
            s->regs.rdt0 = val;
        } else if (addr == E1000_RXDCTL(0)) {
            s->regs.rxdctl0 = val;
        } else if (addr == E1000_TDBAL(0)) {
            s->regs.tdbal0 = val;
        } else if (addr == E1000_TDBAH(0)) {
            s->regs.tdbah0 = val;
        } else if (addr == E1000_TDLEN(0)) {
            s->regs.tdlen0 = val;
        } else if (addr == E1000_TDH(0)) {
            s->regs.tdh0 = val;
        } else if (addr == E1000_TDT(0)) {
            s->regs.tdt0 = val;
            if (s->regs.tctl & E1000_TCTL_EN) {
                pcibase_raise_irq(s, E1000_ICR_RXT0);
            }
        } else if (addr == E1000_TXDCTL(0)) {
            s->regs.txdctl0 = val;
        }
        break;
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val32;

    if (size != 4) {
        return 0;
    }

    val32 = pcibase_reg_read(s, (uint32_t)addr);
    return val32;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        return;
    }

    pcibase_reg_write(s, (uint32_t)addr, (uint32_t)val);
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    (void)opaque;
    (void)addr;
    (void)size;
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    (void)opaque;
    (void)addr;
    (void)val;
    (void)size;
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
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

    memset(&s->regs, 0, sizeof(s->regs));
    s->regs.status = E1000_STATUS_LU | E1000_STATUS_SPEED_1000 | E1000_STATUS_FD;
    s->intr_status = 0;
    s->intr_mask = 0;
    s->status_flags = 0;
    s->reset_state = 0;
    s->power_state = 0;
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    hwaddr size = bi->size ? pow2ceil(bi->size) : 0;
    if (!size) {
        size = 128 * KiB;
    }

    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s, bi->name, size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  IGB_VENDOR_ID_INTEL);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  IGB_DEVICE_ID_I354_BACKPLANE_1GBPS);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, IGB_PCI_CLASS_NETWORK_ETHERNET);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    s->bar_info[0].size  = 128 * KiB;
    s->bar_info[0].name  = "igb-mmio-bar0";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type  = BAR_TYPE_NONE;
        s->bar_info[i].size  = 0;
        s->bar_info[i].name  = NULL;
    }

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = true;
    s->has_msix = true;

    if (s->has_msix) {
        msix_init_exclusive_bar(pdev, 1, 0, NULL);
    }
    if (s->has_msi) {
        msi_init(pdev, 0, 1, true, false, errp);
    }

    memset(&s->regs, 0, sizeof(s->regs));
    s->regs.status = E1000_STATUS_LU | E1000_STATUS_SPEED_1000 | E1000_STATUS_FD;
    s->intr_status = 0;
    s->intr_mask = 0;
    s->status_flags = 0;
    s->reset_state = 0;
    s->power_state = 0;
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
    .name = "igb_pci",
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
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
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
