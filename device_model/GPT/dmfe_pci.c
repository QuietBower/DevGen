/*
 * QEMU PCI device model for Davicom DM910x as used by dmfe.c
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

#define TYPE_PCIBASE_DEVICE "dmfe_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define DMFE_PCI_VENDOR_ID 0x1282
#define DMFE_PCI_DEVICE_ID 0x9132

enum dmfe_offsets {
    DCR0  = 0x00,
    DCR1  = 0x08,
    DCR2  = 0x10,
    DCR3  = 0x18,
    DCR4  = 0x20,
    DCR5  = 0x28,
    DCR6  = 0x30,
    DCR7  = 0x38,
    DCR8  = 0x40,
    DCR9  = 0x48,
    DCR10 = 0x50,
    DCR11 = 0x58,
    DCR12 = 0x60,
    DCR13 = 0x68,
    DCR14 = 0x70,
    DCR15 = 0x78,
};

#define CR0_DEFAULT     0x00E00000
#define CR6_DEFAULT     0x00080000
#define CR7_DEFAULT     0x00180c1
#define CR15_DEFAULT    0x06

/* Bits actually referenced by the driver */
#define CR5_TX_INT      0x01
#define CR5_RX_INT      0x40
#define CR5_ERR_INT     0x2000
#define CR5_INT_SUMMARY 0xC1

#define CR6_RXSC      0x00000002
#define CR6_PBF       0x00000008
#define CR6_PM        0x00000040
#define CR6_PAM       0x00000080
#define CR6_FDM       0x00000200
#define CR6_TXSC      0x00002000
#define CR6_STI       0x00100000
#define CR6_SFT       0x00200000
#define CR6_RXA       0x40000000
#define CR6_NO_PURGE  0x20000000

#define DM910X_RESET  0x00000001

/* SROM / MII CR9 bits used by driver */
#define CR9_SROM_READ 0x00000001
#define CR9_SRCS      0x00000002
#define CR9_SRCLK     0x00000004
#define CR9_CRDOUT    0x00000008

#define PHY_DATA_0    0x00080000
#define PHY_DATA_1    0x000C0000
#define MDCLKH        0x00100000

#define DM9102_IO_SIZE  0x80
#define DM9102A_IO_SIZE 0x100

#define DMFE_PCI_CLASS_ID PCI_CLASS_NETWORK_ETHERNET

/* BAR metadata */
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

    /* BARs */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;

    struct {
        uint32_t cr[16];
        uint32_t tx_desc_base; /* DCR4 */
        uint32_t rx_desc_base; /* DCR3 */
    } regs;

    /* Interrupt state */
    uint32_t cr5_status;   /* shadow of DCR5 */
    uint32_t cr7_mask;     /* shadow of DCR7 */

    /* Simple emulated PHY / SROM state */
    uint16_t phy_reg[32];
    uint16_t srom[64];

    /* Link state flag for ethtool / carrier */
    bool link_up;
};

static bool dmfe_msi_enabled(PCIBaseState *s)
{
    return msi_enabled(PCI_DEVICE(s));
}

static void dmfe_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t pending = s->cr5_status & s->cr7_mask & CR5_INT_SUMMARY;

    if (pending) {
        if (dmfe_msi_enabled(s)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!dmfe_msi_enabled(s)) {
            pci_set_irq(pdev, 0);
        }
    }
}

/* MMIO handlers are unused; I/O space only */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    (void)opaque;
    (void)addr;
    (void)size;
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    (void)opaque;
    (void)addr;
    (void)val;
    (void)size;
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = 0;

    /* align to 8-byte spaced DCRn as in driver (DCR0=0, DCR1=8,...) */
    switch (addr & ~0x3ULL) {
    case DCR0:
        val = s->regs.cr[0];
        break;
    case DCR1:
        val = s->regs.cr[1];
        break;
    case DCR2:
        val = s->regs.cr[2];
        break;
    case DCR3:
        val = s->regs.rx_desc_base;
        break;
    case DCR4:
        val = s->regs.tx_desc_base;
        break;
    case DCR5:
        /* status register */
        val = s->cr5_status;
        break;
    case DCR6:
        val = s->regs.cr[6];
        break;
    case DCR7:
        val = s->cr7_mask;
        break;
    case DCR8:
        /* used by driver as error counter; return 0 so no timeout path */
        val = 0;
        break;
    case DCR9:
        val = s->regs.cr[9];
        /* CR9_CRDOUT bit is sampled by SROM/MII helpers; keep 0 */
        break;
    case DCR10:
        val = s->regs.cr[10];
        break;
    case DCR11:
        val = s->regs.cr[11];
        break;
    case DCR12:
        val = s->regs.cr[12];
        break;
    case DCR13:
        val = s->regs.cr[13];
        break;
    case DCR14:
        val = s->regs.cr[14];
        break;
    case DCR15:
        val = s->regs.cr[15];
        break;
    default:
        val = 0;
        break;
    }

    switch (size) {
    case 1:
        return (uint8_t)val;
    case 2:
        return (uint16_t)val;
    default:
        return val;
    }
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t data = (uint32_t)val;

    switch (addr & ~0x3ULL) {
    case DCR0:
        /* reset / control */
        s->regs.cr[0] = data;
        if (data & DM910X_RESET) {
            /* simple reset: clear status and desc pointers */
            s->cr5_status = 0;
            s->regs.tx_desc_base = 0;
            s->regs.rx_desc_base = 0;
        }
        break;
    case DCR1:
        /* Tx polling command: driver writes 0x1 to kick TX */
        s->regs.cr[1] = data;
        if (data & 0x1) {
            /* Immediately complete a TX and raise interrupt */
            s->cr5_status |= CR5_TX_INT;
            dmfe_update_irq(s);
        }
        break;
    case DCR2:
        s->regs.cr[2] = data;
        break;
    case DCR3:
        /* RX descriptor base */
        s->regs.rx_desc_base = data;
        break;
    case DCR4:
        /* TX descriptor base */
        s->regs.tx_desc_base = data;
        break;
    case DCR5:
        /* write back same value to clear IRQs */
        s->cr5_status &= ~((uint32_t)data);
        dmfe_update_irq(s);
        break;
    case DCR6:
        /* CR6: operating mode */
        s->regs.cr[6] = data;
        break;
    case DCR7:
        /* interrupt mask */
        s->cr7_mask = data;
        dmfe_update_irq(s);
        break;
    case DCR8:
        s->regs.cr[8] = data;
        break;
    case DCR9:
        /* SROM/MII access; we do not emulate protocol details */
        s->regs.cr[9] = data;
        break;
    case DCR10:
        s->regs.cr[10] = data;
        break;
    case DCR11:
        s->regs.cr[11] = data;
        break;
    case DCR12:
        s->regs.cr[12] = data;
        break;
    case DCR13:
        s->regs.cr[13] = data;
        break;
    case DCR14:
        s->regs.cr[14] = data;
        break;
    case DCR15:
        s->regs.cr[15] = data;
        break;
    default:
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

    for (int i = 0; i < 16; i++) {
        s->regs.cr[i] = 0;
    }
    s->regs.cr[0]  = CR0_DEFAULT;
    s->regs.cr[6]  = CR6_DEFAULT;
    s->regs.cr[7]  = CR7_DEFAULT;
    s->regs.cr[15] = CR15_DEFAULT;

    s->cr5_status = 0;
    s->cr7_mask = CR7_DEFAULT;
    s->regs.tx_desc_base = 0;
    s->regs.rx_desc_base = 0;

    /* Simple defaults for PHY and SROM: zeroed is fine for driver logic
     * since it never checks specific values apart from presence.
     */
    memset(s->phy_reg, 0, sizeof(s->phy_reg));
    memset(s->srom, 0, sizeof(s->srom));
    s->link_up = true;
    dmfe_update_irq(s);
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  DMFE_PCI_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DMFE_PCI_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, DMFE_PCI_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x31); /* a common dm9102a rev */
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type  = BAR_TYPE_PIO;
    s->bar_info[0].size  = DM9102A_IO_SIZE;
    s->bar_info[0].name  = "dmfe-io";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type  = BAR_TYPE_NONE;
        s->bar_info[i].size  = 0;
        s->bar_info[i].name  = "unused";
    }

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = false;
    s->has_msix = false;

    for (int i = 0; i < 16; i++) {
        s->regs.cr[i] = 0;
    }
    s->regs.cr[0]  = CR0_DEFAULT;
    s->regs.cr[6]  = CR6_DEFAULT;
    s->regs.cr[7]  = CR7_DEFAULT;
    s->regs.cr[15] = CR15_DEFAULT;

    s->cr5_status = 0;
    s->cr7_mask = CR7_DEFAULT;
    s->regs.tx_desc_base = 0;
    s->regs.rx_desc_base = 0;

    memset(s->phy_reg, 0, sizeof(s->phy_reg));
    memset(s->srom, 0, sizeof(s->srom));
    s->link_up = true;
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    (void)s;

    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

static const VMStateDescription vmstate_pcibase = {
    .name = "dmfe_pci",
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
