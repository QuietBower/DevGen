/*
 * This template provides a robust skeleton for hardware emulation.
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

/* Additional include files retrieved from driver context */

#define TYPE_PCIBASE_DEVICE "tulip_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define TULIP_VENDOR_ID 0x1011
#define TULIP_DEVICE_ID 0x0009
#define TULIP_CLASS_ID  0x0200

#define EEPROM_SIZE 512
#define CSR5_RS 0x000e0000
#define CSR5_TS 0x00700000

#define CSR0 0x00
#define CSR1 0x08
#define CSR5 0x28
#define CSR6 0x30
#define CSR7 0x38
#define CSR8 0x40
#define CSR9 0x48
#define CSR11 0x58
#define CSR12 0x60
#define CSR13 0x68
#define CSR14 0x70

#define RX_RING_SIZE 128
#define TX_RING_SIZE 32

#define RxPollInt (RxIntr|RxNoBuf|RxDied|RxJabber)
#define DESC_RING_WRAP 0x02000000

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
    uint32_t csr5;
    uint32_t csr7;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t csr0;
    uint32_t csr6;
    uint32_t csr11;
    uint32_t csr12_shadow;

    /* DMA Context */
    dma_addr_t rx_ring_dma;
    dma_addr_t tx_ring_dma;
    uint32_t cur_rx;
    uint32_t cur_tx;

    int chip_id;
    int revision;
    int flags;

    int pm_state;

    uint8_t eeprom[EEPROM_SIZE];
};

struct tulip_tx_desc {
    uint32_t status;
    uint32_t length;
    uint32_t buffer1;
    uint32_t buffer2;
};

struct tulip_rx_desc {
    uint32_t status;
    uint32_t length;
    uint32_t buffer1;
    uint32_t buffer2;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool level = (s->csr5 & s->csr7) != 0;
    pci_set_irq(pdev, level);
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (is_write) {
        struct tulip_tx_desc desc;
        dma_addr_t desc_addr = s->tx_ring_dma + s->cur_tx * sizeof(struct tulip_tx_desc);
        pci_dma_read(pdev, desc_addr, &desc, sizeof(desc));
        
        /* Emulate Tx completion by clearing ownership */
        desc.status = 0;
        pci_dma_write(pdev, desc_addr, &desc, sizeof(desc));
        
        s->cur_tx = (s->cur_tx + 1) % TX_RING_SIZE;
        
        /* Set generic Tx interrupt bit in CSR5 */
        s->csr5 |= 0x01;
        pcibase_update_irq(s);
    } else {
        struct tulip_rx_desc desc;
        dma_addr_t desc_addr = s->rx_ring_dma + s->cur_rx * sizeof(struct tulip_rx_desc);
        pci_dma_read(pdev, desc_addr, &desc, sizeof(desc));
        
        /* Emulate Rx completion */
        desc.status = 0;
        pci_dma_write(pdev, desc_addr, &desc, sizeof(desc));
        
        s->cur_rx = (s->cur_rx + 1) % RX_RING_SIZE;
        
        /* Set generic Rx interrupt bit in CSR5 */
        s->csr5 |= 0x40;
        pcibase_update_irq(s);
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case CSR0:
        val = s->csr0;
        break;
    case CSR5:
        val = s->csr5;
        break;
    case CSR6:
        val = s->csr6;
        break;
    case CSR7:
        val = s->csr7;
        break;
    case CSR8:
        val = 0; /* Missed frames */
        break;
    case CSR11:
        val = s->csr11;
        break;
    case CSR12:
        val = s->csr12_shadow;
        break;
    case 0x18: /* CSR3 */
        val = s->rx_ring_dma;
        break;
    case 0x20: /* CSR4 */
        val = s->tx_ring_dma;
        break;
    default:
        val = 0;
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case CSR0:
        s->csr0 = val;
        if (val & 0x01) {
            /* Software reset */
            s->csr5 = 0;
            s->csr6 = 0;
            s->csr7 = 0;
            s->csr11 = 0;
            s->csr12_shadow = 0;
            s->rx_ring_dma = 0;
            s->tx_ring_dma = 0;
            s->cur_rx = 0;
            s->cur_tx = 0;
            pcibase_update_irq(s);
        }
        break;
    case CSR1:
        pcibase_do_dma(s, true);
        break;
    case 0x10: /* CSR2 */
        pcibase_do_dma(s, false);
        break;
    case 0x18: /* CSR3 */
        s->rx_ring_dma = val;
        break;
    case 0x20: /* CSR4 */
        s->tx_ring_dma = val;
        break;
    case CSR5:
        s->csr5 &= ~val; /* W1C */
        pcibase_update_irq(s);
        break;
    case CSR6:
        s->csr6 = val;
        break;
    case CSR7:
        s->csr7 = val;
        pcibase_update_irq(s);
        break;
    case CSR11:
        s->csr11 = val;
        break;
    case CSR12:
        s->csr12_shadow = val;
        break;
    case 0xAC:
        /* Multicast filter 0 */
        break;
    case 0xB0:
        /* Multicast filter 1 */
        break;
    default:
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return pcibase_mmio_read(opaque, addr, size);
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    pcibase_mmio_write(opaque, addr, val, size);
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

    s->csr0 = 0;
    s->csr5 = 0;
    s->csr6 = 0;
    s->csr7 = 0;
    s->csr11 = 0;
    s->csr12_shadow = 0;
    s->rx_ring_dma = 0;
    s->tx_ring_dma = 0;
    s->cur_rx = 0;
    s->cur_tx = 0;
    pcibase_update_irq(s);
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

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  TULIP_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  TULIP_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, TULIP_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 2;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 256;
    s->bar_info[0].name = "tulip-pio";

    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = 256;
    s->bar_info[1].name = "tulip-mmio";
  
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
    .name = "tulip_pci",
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
