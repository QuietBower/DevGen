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

#define TYPE_PCIBASE_DEVICE "epic100_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI_VENDOR_ID_SMSC 0x10B8
#define PCI_DEVICE_ID_EPIC100 0x0005
#define PCI_CLASS_NET_ETHER 0x0200

/* Register Offsets */
#define EPIC_REG_COMMAND      0x00
#define EPIC_REG_INTSTAT      0x04
#define EPIC_REG_INTMASK      0x08
#define EPIC_REG_GENCTL       0x0C
#define EPIC_REG_NVCTL        0x10
#define EPIC_REG_EECTL        0x14
#define EPIC_REG_PCIBurstCnt  0x18
#define EPIC_REG_TEST1        0x1C
#define EPIC_REG_CRCCNT       0x20
#define EPIC_REG_ALICNT       0x24
#define EPIC_REG_MPCNT        0x28
#define EPIC_REG_MIICtrl      0x30
#define EPIC_REG_MIIData      0x34
#define EPIC_REG_MIICfg       0x38
#define EPIC_REG_LAN0         0x40
#define EPIC_REG_MC0          0x50
#define EPIC_REG_RxCtrl       0x60
#define EPIC_REG_TxCtrl       0x70
#define EPIC_REG_TxSTAT       0x74
#define EPIC_REG_PRxCDAR      0x84
#define EPIC_REG_RxSTAT       0xA4
#define EPIC_REG_EarlyRx      0xB0
#define EPIC_REG_PTxCDAR      0xC4
#define EPIC_REG_TxThresh     0xDC

/* Command bits */
#define EPIC_CMD_STOP_RX     0x00000001
#define EPIC_CMD_START_RX    0x00000002
#define EPIC_CMD_TX_QUEUED   0x00000004
#define EPIC_CMD_RX_QUEUED   0x00000008
#define EPIC_CMD_STOP_TX_DMA 0x00000020
#define EPIC_CMD_STOP_RX_DMA 0x00000040
#define EPIC_CMD_RESTART_TX  0x00000080

/* MII operation bits */
#define MII_READOP  1
#define MII_WRITEOP 2

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

    uint8_t regs[0x100];

    dma_addr_t tx_ring_dma;
    dma_addr_t rx_ring_dma;
};

/* Utility: read register bytes as little-endian integer */
static uint64_t reg_read(PCIBaseState *s, hwaddr addr, unsigned size)
{
    uint64_t val = 0;
    if (addr + size > sizeof(s->regs)) {
        return 0;
    }
    for (int i = 0; i < size; i++) {
        val |= (uint64_t)s->regs[addr + i] << (i * 8);
    }
    return val;
}

/* Utility: write register bytes from little-endian integer */
static void reg_write(PCIBaseState *s, hwaddr addr, uint64_t val, unsigned size)
{
    if (addr + size > sizeof(s->regs)) {
        return;
    }
    for (int i = 0; i < size; i++) {
        s->regs[addr + i] = (val >> (i * 8)) & 0xFF;
    }
}

/* Internal helper for status-triggered signaling. */
static void epic100_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t active = s->intr_status & s->intr_mask;
    if (active) {
        if (msix_enabled(pdev)) {
            msix_notify(pdev, 0);
        } else if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!msix_enabled(pdev) && !msi_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

/* MMIO read */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case EPIC_REG_COMMAND ... EPIC_REG_COMMAND + 3:
        val = reg_read(s, EPIC_REG_COMMAND, size);
        break;
    case EPIC_REG_INTSTAT ... EPIC_REG_INTSTAT + 3:
        val = s->intr_status;
        break;
    case EPIC_REG_INTMASK ... EPIC_REG_INTMASK + 3:
        val = s->intr_mask;
        break;
    case EPIC_REG_GENCTL ... EPIC_REG_GENCTL + 3:
        val = reg_read(s, EPIC_REG_GENCTL, size);
        break;
    case EPIC_REG_NVCTL ... EPIC_REG_NVCTL + 3:
        val = reg_read(s, EPIC_REG_NVCTL, size);
        break;
    case EPIC_REG_EECTL ... EPIC_REG_EECTL + 3:
        val = reg_read(s, EPIC_REG_EECTL, size);
        break;
    case EPIC_REG_PCIBurstCnt ... EPIC_REG_PCIBurstCnt + 3:
        val = reg_read(s, EPIC_REG_PCIBurstCnt, size);
        break;
    case EPIC_REG_TEST1 ... EPIC_REG_TEST1 + 3:
        val = reg_read(s, EPIC_REG_TEST1, size);
        break;
    case EPIC_REG_CRCCNT:
        val = s->regs[EPIC_REG_CRCCNT];
        break;
    case EPIC_REG_ALICNT:
        val = s->regs[EPIC_REG_ALICNT];
        break;
    case EPIC_REG_MPCNT:
        val = s->regs[EPIC_REG_MPCNT];
        break;
    case EPIC_REG_MIICtrl ... EPIC_REG_MIICtrl + 3:
        val = reg_read(s, EPIC_REG_MIICtrl, size);
        break;
    case EPIC_REG_MIIData ... EPIC_REG_MIIData + 1:
        val = reg_read(s, EPIC_REG_MIIData, size);
        break;
    case EPIC_REG_MIICfg ... EPIC_REG_MIICfg + 3:
        val = reg_read(s, EPIC_REG_MIICfg, size);
        break;
    case EPIC_REG_LAN0 ... EPIC_REG_LAN0 + 1:
        val = reg_read(s, EPIC_REG_LAN0, size);
        break;
    case EPIC_REG_LAN0 + 4 ... EPIC_REG_LAN0 + 5:
        val = reg_read(s, EPIC_REG_LAN0 + 4, size);
        break;
    case EPIC_REG_LAN0 + 8 ... EPIC_REG_LAN0 + 9:
        val = reg_read(s, EPIC_REG_LAN0 + 8, size);
        break;
    case EPIC_REG_MC0 ... EPIC_REG_MC0 + 1:
        val = reg_read(s, EPIC_REG_MC0, size);
        break;
    case EPIC_REG_MC0 + 4 ... EPIC_REG_MC0 + 5:
        val = reg_read(s, EPIC_REG_MC0 + 4, size);
        break;
    case EPIC_REG_MC0 + 8 ... EPIC_REG_MC0 + 9:
        val = reg_read(s, EPIC_REG_MC0 + 8, size);
        break;
    case EPIC_REG_MC0 + 12 ... EPIC_REG_MC0 + 13:
        val = reg_read(s, EPIC_REG_MC0 + 12, size);
        break;
    case EPIC_REG_RxCtrl ... EPIC_REG_RxCtrl + 3:
        val = reg_read(s, EPIC_REG_RxCtrl, size);
        break;
    case EPIC_REG_TxCtrl ... EPIC_REG_TxCtrl + 3:
        val = reg_read(s, EPIC_REG_TxCtrl, size);
        break;
    case EPIC_REG_TxSTAT ... EPIC_REG_TxSTAT + 3:
        /* Provide Tx status; driver checks for underflow (0x10) */
        val = reg_read(s, EPIC_REG_TxSTAT, size);
        break;
    case EPIC_REG_PRxCDAR ... EPIC_REG_PRxCDAR + 3:
        val = reg_read(s, EPIC_REG_PRxCDAR, size);
        break;
    case EPIC_REG_RxSTAT ... EPIC_REG_RxSTAT + 3:
        val = reg_read(s, EPIC_REG_RxSTAT, size);
        break;
    case EPIC_REG_EarlyRx ... EPIC_REG_EarlyRx + 3:
        val = reg_read(s, EPIC_REG_EarlyRx, size);
        break;
    case EPIC_REG_PTxCDAR ... EPIC_REG_PTxCDAR + 3:
        val = reg_read(s, EPIC_REG_PTxCDAR, size);
        break;
    case EPIC_REG_TxThresh ... EPIC_REG_TxThresh + 3:
        val = reg_read(s, EPIC_REG_TxThresh, size);
        break;
    default:
        /* Unmapped area: return 0 for reads */
        val = 0;
        break;
    }
    return val;
}

/* MMIO write */
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t old_val;

    switch (addr) {
    case EPIC_REG_COMMAND ... EPIC_REG_COMMAND + 3:
        reg_write(s, EPIC_REG_COMMAND, val, size);
        /* Check for soft reset request? Not needed for probe. */
        break;
    case EPIC_REG_INTSTAT ... EPIC_REG_INTSTAT + 3:
        /* Write-1-to-clear: clear bits that are 1 in val */
        s->intr_status &= ~(val & 0xFFFFFFFF);
        epic100_update_irq(s);
        break;
    case EPIC_REG_INTMASK ... EPIC_REG_INTMASK + 3:
        old_val = s->intr_mask;
        s->intr_mask = val & 0xFFFFFFFF;
        if (old_val != s->intr_mask) {
            epic100_update_irq(s);
        }
        break;
    case EPIC_REG_GENCTL ... EPIC_REG_GENCTL + 3:
        reg_write(s, EPIC_REG_GENCTL, val, size);
        /* Soft reset: when bit 14 is set (0x4000) and bit 0 (0x0001) maybe? */
        /* Driver writes 0x4001 for reset. We'll trigger a device reset. */
        if ((val & 0x4001) == 0x4001) {
            /* Perform soft reset: not fully implemented yet */
        }
        break;
    case EPIC_REG_NVCTL ... EPIC_REG_NVCTL + 3:
        reg_write(s, EPIC_REG_NVCTL, val, size);
        break;
    case EPIC_REG_EECTL ... EPIC_REG_EECTL + 3:
        reg_write(s, EPIC_REG_EECTL, val, size);
        /* EEPROM bit-banging: we simply store writes. No real emulation needed
           for probe; read_eeprom will complete with zero data. */
        break;
    case EPIC_REG_PCIBurstCnt ... EPIC_REG_PCIBurstCnt + 3:
        reg_write(s, EPIC_REG_PCIBurstCnt, val, size);
        break;
    case EPIC_REG_TEST1 ... EPIC_REG_TEST1 + 3:
        reg_write(s, EPIC_REG_TEST1, val, size);
        break;
    case EPIC_REG_CRCCNT:
        s->regs[EPIC_REG_CRCCNT] = val & 0xFF;
        break;
    case EPIC_REG_ALICNT:
        s->regs[EPIC_REG_ALICNT] = val & 0xFF;
        break;
    case EPIC_REG_MPCNT:
        s->regs[EPIC_REG_MPCNT] = val & 0xFF;
        break;
    case EPIC_REG_MIICtrl ... EPIC_REG_MIICtrl + 3: {
        uint32_t cmd = val & 0xFFFFFFFF;
        reg_write(s, EPIC_REG_MIICtrl, cmd, size);
        if (cmd & MII_READOP) {
            /* MDIO Read operation */
            int phy = (cmd >> 9) & 0x1F;
            int reg = (cmd >> 4) & 0x1F;
            uint16_t data = 0xFFFF;
            if (phy == 1) {
                /* Emulate a simple PHY at address 1 with basic regs */
                switch (reg) {
                case 0: /* BMCR */
                    data = 0x1000; /* basic power-up value */
                    break;
                case 1: /* BMSR */
                    data = 0x7809; /* link up, 100Mbps full duplex */
                    break;
                case 2: /* PHYIDR1 */
                    data = 0x0181;
                    break;
                case 3: /* PHYIDR2 */
                    data = 0xb8b0;
                    break;
                case 4: /* ANAR */
                    data = 0x01e1;
                    break;
                case 5: /* ANLPAR */
                    data = 0x41e1; /* partner capabilities */
                    break;
                default:
                    data = 0x0000;
                    break;
                }
            }
            /* Place data into MIIData */
            s->regs[EPIC_REG_MIIData] = data & 0xFF;
            s->regs[EPIC_REG_MIIData + 1] = (data >> 8) & 0xFF;
            /* Clear the read operation bit to indicate completion */
            s->regs[EPIC_REG_MIICtrl] &= ~MII_READOP;
            s->regs[EPIC_REG_MIICtrl + 1] &= ~(MII_READOP >> 8);
            s->regs[EPIC_REG_MIICtrl + 2] &= ~(MII_READOP >> 16);
            s->regs[EPIC_REG_MIICtrl + 3] &= ~(MII_READOP >> 24);
        } else if (cmd & MII_WRITEOP) {
            /* MDIO Write operation: just clear the write op bit */
            s->regs[EPIC_REG_MIICtrl] &= ~MII_WRITEOP;
            s->regs[EPIC_REG_MIICtrl + 1] &= ~(MII_WRITEOP >> 8);
            s->regs[EPIC_REG_MIICtrl + 2] &= ~(MII_WRITEOP >> 16);
            s->regs[EPIC_REG_MIICtrl + 3] &= ~(MII_WRITEOP >> 24);
        }
        break;
    }
    case EPIC_REG_MIIData ... EPIC_REG_MIIData + 1:
        reg_write(s, EPIC_REG_MIIData, val, size);
        break;
    case EPIC_REG_MIICfg ... EPIC_REG_MIICfg + 3:
        reg_write(s, EPIC_REG_MIICfg, val, size);
        break;
    case EPIC_REG_LAN0 ... EPIC_REG_LAN0 + 1:
        reg_write(s, EPIC_REG_LAN0, val, size);
        break;
    case EPIC_REG_LAN0 + 4 ... EPIC_REG_LAN0 + 5:
        reg_write(s, EPIC_REG_LAN0 + 4, val, size);
        break;
    case EPIC_REG_LAN0 + 8 ... EPIC_REG_LAN0 + 9:
        reg_write(s, EPIC_REG_LAN0 + 8, val, size);
        break;
    case EPIC_REG_MC0 ... EPIC_REG_MC0 + 1:
        reg_write(s, EPIC_REG_MC0, val, size);
        break;
    case EPIC_REG_MC0 + 4 ... EPIC_REG_MC0 + 5:
        reg_write(s, EPIC_REG_MC0 + 4, val, size);
        break;
    case EPIC_REG_MC0 + 8 ... EPIC_REG_MC0 + 9:
        reg_write(s, EPIC_REG_MC0 + 8, val, size);
        break;
    case EPIC_REG_MC0 + 12 ... EPIC_REG_MC0 + 13:
        reg_write(s, EPIC_REG_MC0 + 12, val, size);
        break;
    case EPIC_REG_RxCtrl ... EPIC_REG_RxCtrl + 3:
        reg_write(s, EPIC_REG_RxCtrl, val, size);
        break;
    case EPIC_REG_TxCtrl ... EPIC_REG_TxCtrl + 3:
        reg_write(s, EPIC_REG_TxCtrl, val, size);
        break;
    case EPIC_REG_TxSTAT ... EPIC_REG_TxSTAT + 3:
        /* TxSTAT is read-only; ignore writes */
        break;
    case EPIC_REG_PRxCDAR ... EPIC_REG_PRxCDAR + 3:
        reg_write(s, EPIC_REG_PRxCDAR, val, size);
        s->rx_ring_dma = (dma_addr_t)val;
        break;
    case EPIC_REG_RxSTAT ... EPIC_REG_RxSTAT + 3:
        reg_write(s, EPIC_REG_RxSTAT, val, size);
        break;
    case EPIC_REG_EarlyRx ... EPIC_REG_EarlyRx + 3:
        reg_write(s, EPIC_REG_EarlyRx, val, size);
        break;
    case EPIC_REG_PTxCDAR ... EPIC_REG_PTxCDAR + 3:
        reg_write(s, EPIC_REG_PTxCDAR, val, size);
        s->tx_ring_dma = (dma_addr_t)val;
        break;
    case EPIC_REG_TxThresh ... EPIC_REG_TxThresh + 3:
        reg_write(s, EPIC_REG_TxThresh, val, size);
        break;
    default:
        /* Ignore writes to unmapped areas */
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

    /* Reset register defaults */
    memset(s->regs, 0, sizeof(s->regs));
    s->intr_status = 0;
    s->intr_mask = 0;

    /* Set default MAC in LAN0 registers (52:54:00:12:34:56) */
    s->regs[EPIC_REG_LAN0]     = 0x52;
    s->regs[EPIC_REG_LAN0 + 1] = 0x54;
    s->regs[EPIC_REG_LAN0 + 4] = 0x00;
    s->regs[EPIC_REG_LAN0 + 5] = 0x12;
    s->regs[EPIC_REG_LAN0 + 8] = 0x34;
    s->regs[EPIC_REG_LAN0 + 9] = 0x56;

    /* Initialize MII control to idle */
    s->regs[EPIC_REG_MIICtrl] = 0;
    s->regs[EPIC_REG_MIICtrl+1] = 0;
    s->regs[EPIC_REG_MIICtrl+2] = 0;
    s->regs[EPIC_REG_MIICtrl+3] = 0;
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_SMSC );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_EPIC100 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NET_ETHER );
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
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x100;
    s->bar_info[0].name = "epic-mmio";
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

static const VMStateDescription vmstate_pcibase = {
    .name = "epic100_pci",
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
