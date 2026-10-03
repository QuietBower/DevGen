/*
 * QEMU PCI device model for Realtek r8169-compatible NIC (minimal behavioral emulation)
 * Target driver: drivers/net/ethernet/realtek/r8169_main.c
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

#include "hw/qdev-core.h"

#define TYPE_PCIBASE_DEVICE "r8169_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCIBASE_VENDOR_ID 0x10ec
#define PCIBASE_DEVICE_ID 0x2502
#define PCIBASE_CLASS_ID  PCI_CLASS_NETWORK_ETHERNET

enum rtl_registers_qemu {
    MAC0               = 0x00,
    MAC4               = 0x04,
    MAR0               = 0x08,
    CounterAddrLow     = 0x10,
    CounterAddrHigh    = 0x14,
    TxDescStartAddrLow = 0x20,
    TxDescStartAddrHigh= 0x24,
    TxHDescStartAddrLow= 0x28,
    TxHDescStartAddrHigh=0x2c,
    FLASH              = 0x30,
    ERSR               = 0x36,
    ChipCmd            = 0x37,
    TxPoll             = 0x38,
    IntrMask           = 0x3c,
    IntrStatus         = 0x3e,
    TxConfig           = 0x40,
    RxConfig           = 0x44,
    Cfg9346            = 0x50,
    Config0            = 0x51,
    Config1            = 0x52,
    Config2            = 0x53,
    Config3            = 0x54,
    Config4            = 0x55,
    Config5            = 0x56,
    PHYAR              = 0x60,
    PHYstatus          = 0x6c,
    RxMaxSize          = 0xda,
    CPlusCmd           = 0xe0,
    IntrMitigate       = 0xe2,
    RxDescAddrLow      = 0xe4,
    RxDescAddrHigh     = 0xe8,
    EarlyTxThres       = 0xec,
    MaxTxPacketSize    = 0xec,
};

enum rtl_interrupt_status_bits_qemu {
    SYSErr       = 0x8000,
    PCSTimeout   = 0x4000,
    SWInt        = 0x0100,
    TxDescUnavail= 0x0080,
    RxFIFOOver   = 0x0040,
    LinkChg      = 0x0020,
    RxOverflow   = 0x0010,
    TxErr        = 0x0008,
    TxOK         = 0x0004,
    RxErr        = 0x0002,
    RxOK         = 0x0001,
};

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

    uint8_t mac[6];
    uint32_t tx_desc_start_low;
    uint32_t tx_desc_start_high;
    uint32_t rx_desc_start_low;
    uint32_t rx_desc_start_high;
    uint8_t chip_cmd;
    uint32_t tx_config;
    uint32_t rx_config;
    uint8_t cfg9346;
    uint8_t config1;
    uint8_t config2;
    uint8_t config3;
    uint8_t config4;
    uint8_t config5;
    uint16_t cplus_cmd;

    dma_addr_t tx_ring_dma;
    dma_addr_t rx_ring_dma;

    /* Additional state reflecting driver-visible fields */
    uint32_t counter_addr_low;
    uint32_t counter_addr_high;
    uint16_t ersr;
    uint8_t tx_poll;
    uint16_t intr_mitigate;
    uint16_t rx_max_size;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool asserted = (s->intr_status & s->intr_mask) != 0;

    if (asserted) {
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

    switch (addr) {
    case MAC0 + 0:
        return s->mac[0] | ((uint32_t)s->mac[1] << 8) |
               ((uint32_t)s->mac[2] << 16) | ((uint32_t)s->mac[3] << 24);
    case MAC4:
        return s->mac[4] | ((uint32_t)s->mac[5] << 8);
    case MAR0:
        /* Multicast filter is not modeled; return 0 */
        return 0x00000000;
    case CounterAddrLow:
        return s->counter_addr_low;
    case CounterAddrHigh:
        return s->counter_addr_high;
    case TxDescStartAddrLow:
        return s->tx_desc_start_low;
    case TxDescStartAddrHigh:
        return s->tx_desc_start_high;
    case TxHDescStartAddrLow:
        return 0x00000000;
    case TxHDescStartAddrHigh:
        return 0x00000000;
    case FLASH:
        return 0x00000000;
    case ERSR:
        return s->ersr;
    case ChipCmd:
        return s->chip_cmd;
    case TxPoll:
        return s->tx_poll;
    case IntrMask:
        return s->intr_mask & 0xffff;
    case IntrStatus:
        return s->intr_status & 0xffff;
    case TxConfig:
        return s->tx_config;
    case RxConfig:
        return s->rx_config;
    case Cfg9346:
        return s->cfg9346;
    case Config0:
        return 0x00;
    case Config1:
        return s->config1;
    case Config2:
        return s->config2;
    case Config3:
        return s->config3;
    case Config4:
        return s->config4;
    case Config5:
        return s->config5;
    case PHYAR:
        return 0x00000000;
    case PHYstatus:
        /* Link up, basic PHY status */
        return 0x00000001;
    case RxMaxSize:
        return s->rx_max_size;
    case CPlusCmd:
        return s->cplus_cmd;
    case IntrMitigate:
        return s->intr_mitigate;
    case RxDescAddrLow:
        return s->rx_desc_start_low;
    case RxDescAddrHigh:
        return s->rx_desc_start_high;
    case EarlyTxThres:
        return 0x00000000;
    default:
        return 0;
    }
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case MAC0 + 0:
        s->mac[0] = val & 0xff;
        s->mac[1] = (val >> 8) & 0xff;
        s->mac[2] = (val >> 16) & 0xff;
        s->mac[3] = (val >> 24) & 0xff;
        break;
    case MAC4:
        s->mac[4] = val & 0xff;
        s->mac[5] = (val >> 8) & 0xff;
        break;
    case MAR0:
        /* Ignore multicast filter programming */
        break;
    case CounterAddrLow:
        s->counter_addr_low = (uint32_t)val;
        break;
    case CounterAddrHigh:
        s->counter_addr_high = (uint32_t)val;
        break;
    case TxDescStartAddrLow:
        s->tx_desc_start_low = (uint32_t)val;
        s->tx_ring_dma = ((uint64_t)s->tx_desc_start_high << 32) | s->tx_desc_start_low;
        break;
    case TxDescStartAddrHigh:
        s->tx_desc_start_high = (uint32_t)val;
        s->tx_ring_dma = ((uint64_t)s->tx_desc_start_high << 32) | s->tx_desc_start_low;
        break;
    case TxHDescStartAddrLow:
        /* High priority TX not modeled */
        break;
    case TxHDescStartAddrHigh:
        /* High priority TX not modeled */
        break;
    case FLASH:
        /* No flash emulation */
        break;
    case ERSR:
        s->ersr = (uint16_t)val;
        break;
    case ChipCmd:
        s->chip_cmd = (uint8_t)val;
        break;
    case TxPoll:
        s->tx_poll = (uint8_t)val;
        break;
    case IntrMask:
        s->intr_mask = (uint16_t)val;
        pcibase_update_irq(s);
        break;
    case IntrStatus:
        /* W1C semantics */
        s->intr_status &= ~((uint16_t)val);
        pcibase_update_irq(s);
        break;
    case TxConfig:
        s->tx_config = (uint32_t)val;
        break;
    case RxConfig:
        s->rx_config = (uint32_t)val;
        break;
    case Cfg9346:
        s->cfg9346 = (uint8_t)val;
        break;
    case Config0:
        /* Not modeled; ignore */
        break;
    case Config1:
        s->config1 = (uint8_t)val;
        break;
    case Config2:
        s->config2 = (uint8_t)val;
        break;
    case Config3:
        s->config3 = (uint8_t)val;
        break;
    case Config4:
        s->config4 = (uint8_t)val;
        break;
    case Config5:
        s->config5 = (uint8_t)val;
        break;
    case PHYAR:
        /* No MDIO emulation; ignore */
        break;
    case PHYstatus:
        /* Read-only in this model */
        break;
    case RxMaxSize:
        s->rx_max_size = (uint16_t)val;
        break;
    case CPlusCmd:
        s->cplus_cmd = (uint16_t)val;
        break;
    case IntrMitigate:
        s->intr_mitigate = (uint16_t)val;
        break;
    case RxDescAddrLow:
        s->rx_desc_start_low = (uint32_t)val;
        s->rx_ring_dma = ((uint64_t)s->rx_desc_start_high << 32) | s->rx_desc_start_low;
        break;
    case RxDescAddrHigh:
        s->rx_desc_start_high = (uint32_t)val;
        s->rx_ring_dma = ((uint64_t)s->rx_desc_start_high << 32) | s->rx_desc_start_low;
        break;
    case EarlyTxThres:
        /* Not modeled */
        break;
    default:
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;
    (void)addr;
    (void)size;
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;
    (void)addr;
    (void)val;
    (void)size;
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
    PCIDevice *pdev = PCI_DEVICE(dev);

    pci_device_reset(pdev);

    memset(s->mac, 0, sizeof(s->mac));
    s->tx_desc_start_low = 0;
    s->tx_desc_start_high = 0;
    s->rx_desc_start_low = 0;
    s->rx_desc_start_high = 0;
    s->chip_cmd = 0x00;
    s->tx_config = 0x00000000;
    s->rx_config = 0x00000000;
    s->cfg9346 = 0x00;
    s->config1 = 0x00;
    s->config2 = 0x00;
    s->config3 = 0x00;
    s->config4 = 0x00;
    s->config5 = 0x00;
    s->cplus_cmd = 0x0000;
    s->intr_status = 0x0000;
    s->intr_mask = 0x0000;
    s->tx_ring_dma = 0;
    s->rx_ring_dma = 0;
    s->counter_addr_low = 0;
    s->counter_addr_high = 0;
    s->ersr = 0;
    s->tx_poll = 0;
    s->intr_mitigate = 0;
    s->rx_max_size = 0;

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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCIBASE_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCIBASE_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCIBASE_CLASS_ID);
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
    s->bar_info[0].size  = 0x100;
    s->bar_info[0].name  = "r8169-mmio";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type  = BAR_TYPE_NONE;
        s->bar_info[i].size  = 0;
        s->bar_info[i].name  = "";
    }

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = true;
    s->has_msix = false;
    if (s->has_msi) {
        msi_init(pdev, 0, 1, true, false, errp);
    }

    memset(s->mac, 0, sizeof(s->mac));
    s->tx_desc_start_low = 0;
    s->tx_desc_start_high = 0;
    s->rx_desc_start_low = 0;
    s->rx_desc_start_high = 0;
    s->chip_cmd = 0x00;
    s->tx_config = 0x00000000;
    s->rx_config = 0x00000000;
    s->cfg9346 = 0x00;
    s->config1 = 0x00;
    s->config2 = 0x00;
    s->config3 = 0x00;
    s->config4 = 0x00;
    s->config5 = 0x00;
    s->cplus_cmd = 0x0000;
    s->intr_status = 0x0000;
    s->intr_mask = 0x0000;
    s->tx_ring_dma = 0;
    s->rx_ring_dma = 0;
    s->counter_addr_low = 0;
    s->counter_addr_high = 0;
    s->ersr = 0;
    s->tx_poll = 0;
    s->intr_mitigate = 0;
    s->rx_max_size = 0;
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    (void)s;

    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, 0);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

static const VMStateDescription vmstate_pcibase = {
    .name = "r8169_pci",
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
