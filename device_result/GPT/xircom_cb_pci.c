/*
 * QEMU 8.2.10 PCI device model for Xircom CardBus (xircom_cb)
 * Phase 2: Behavioral implementation based strictly on xircom_cb.c
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
/* Removed: #include "hw/net/ethernet.h" - not available and not used */

#define TYPE_PCIBASE_DEVICE "xircom_cb_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* CSR register offsets from xircom_cb.c */
#define CSR0   0x00
#define CSR1   0x08
#define CSR2   0x10
#define CSR3   0x18
#define CSR4   0x20
#define CSR5   0x28
#define CSR6   0x30
#define CSR7   0x38
#define CSR8   0x40
#define CSR9   0x48
#define CSR10  0x50
#define CSR11  0x58
#define CSR12  0x60
#define CSR13  0x68
#define CSR14  0x70
#define CSR15  0x78
#define CSR16  0x80

#define PCI_POWERMGMT 0x40
#define NUMDESCRIPTORS 4

#define XIRCOM_VENDOR_ID 0x115d
#define XIRCOM_DEVICE_ID 0x0003

#define XIRCOM_PCI_CLASS_ID PCI_CLASS_NETWORK_ETHERNET


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

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t csr[17]; /* CSR0..CSR16 (indices 0..16) */

    /* Operational status flags */
    int open;

    /* DMA descriptor ring base addresses from driver */
    hwaddr rx_ring_base; /* CSR3 */
    hwaddr tx_ring_base; /* CSR4 */

    /* Cached DMA buffer physical addresses mentioned in driver comments */
    hwaddr rx_dma_handle;
    hwaddr tx_dma_handle;

    /* Internal runtime state */
    bool receiver_enabled;
    bool transmitter_enabled;

    /* Interrupt lines */
    uint32_t int_status;   /* mirrors parts of CSR5 used by driver */
    uint32_t int_enable;   /* mirrors CSR7 */

    /* Simple link status state for CSR12 */
    uint8_t link_status;   /* lower bits returned by CSR12 reads */
};

/* Bit definitions used in driver (approximated to match usage) */
#define CSR5_LINKCHANGE_BIT   (1u << 27)
#define CSR5_TX_ACTIVE_MASK   (7u << 20)
#define CSR5_RX_ACTIVE_MASK   (7u << 17)

#define CSR7_INT_TX           (1u << 0)
#define CSR7_INT_TX_STOP      (1u << 1)
#define CSR7_INT_TX_UNAVAIL   (1u << 2)
#define CSR7_INT_RX_UNAVAIL   (1u << 7)
#define CSR7_INT_RX_STOP      (1u << 8)
#define CSR7_INT_FATAL        (1u << 13)
#define CSR7_INT_ABNORMAL     (1u << 15)
#define CSR7_INT_NORMAL       (1u << 16)
#define CSR7_INT_LINK         (1u << 27)

/* Internal helper for IRQ signaling based on CSR5/CSR7 */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t pending = s->int_status & s->int_enable;

    if (pending) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /*
     * The driver programs CSR3/CSR4 with descriptor ring base addresses
     * and then directly accesses the coherent rx_buffer/tx_buffer memory
     * from the CPU. There are no explicit pci_dma_read/pci_dma_write
     * operations in the driver code that we must emulate for probe/open
     * to succeed. Therefore we keep DMA logic minimal and do not perform
     * any transfers here.
     */
    (void)s;
    (void)is_write;
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Only 32-bit accesses are used by this driver for CSR reads */
    if (size != 4 && size != 1) {
        return 0xffffffffu;
    }

    switch (addr) {
    case CSR0:
        /* CSR0: PCI configuration / reset; no specific bits inspected */
        val = s->csr[0];
        break;
    case CSR1:
        val = s->csr[1];
        break;
    case CSR2:
        val = s->csr[2];
        break;
    case CSR3:
        val = s->rx_ring_base;
        break;
    case CSR4:
        val = s->tx_ring_base;
        break;
    case CSR5:
        /* Status register */
        val = s->int_status;
        /* driver also checks activity bits for TX/RX */
        if (s->transmitter_enabled) {
            val |= CSR5_TX_ACTIVE_MASK;
        }
        if (s->receiver_enabled) {
            val |= CSR5_RX_ACTIVE_MASK;
        }
        s->csr[5] = (uint32_t)val;
        break;
    case CSR6:
        val = s->csr[6];
        break;
    case CSR7:
        val = s->int_enable;
        break;
    case CSR8:
        val = s->csr[8];
        break;
    case CSR9:
        /* Boot ROM data window used in read_mac_address() */
        val = s->csr[9];
        break;
    case CSR10:
        val = s->csr[10];
        break;
    case CSR11:
        val = s->csr[11];
        break;
    case CSR12:
        /* Link status: driver inspects bits 1 and 2 to determine speed */
        val = s->link_status;
        break;
    case CSR13:
        val = s->csr[13];
        break;
    case CSR14:
        val = s->csr[14];
        break;
    case CSR15:
        val = s->csr[15];
        break;
    case CSR16:
        val = s->csr[16];
        break;
    default:
        val = 0;
        break;
    }

    /* For the byte-sized read in link_status(), return only low byte */
    if (size == 1) {
        val &= 0xffu;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4 && size != 1) {
        return;
    }

    switch (addr) {
    case CSR0:
        /* CSR0: reset and configuration */
        if (val & 0x1) {
            /* software reset requested; clear internal state */
            memset(s->csr, 0, sizeof(s->csr));
            s->rx_ring_base = 0;
            s->tx_ring_base = 0;
            s->receiver_enabled = false;
            s->transmitter_enabled = false;
            s->int_status = 0;
            s->int_enable = 0;
        }
        s->csr[0] = (uint32_t)val;
        break;
    case CSR1:
        /* Transmit poll demand: driver writes 0 in trigger_transmit() */
        s->csr[1] = (uint32_t)val;
        break;
    case CSR2:
        /* Receive poll demand: driver writes 0 in trigger_receive() */
        s->csr[2] = (uint32_t)val;
        break;
    case CSR3:
        /* Receive descriptor list address */
        s->rx_ring_base = (hwaddr)val;
        s->csr[3] = (uint32_t)val;
        break;
    case CSR4:
        /* Transmit descriptor list address */
        s->tx_ring_base = (hwaddr)val;
        s->csr[4] = (uint32_t)val;
        break;
    case CSR5:
        /* Status register: write-1-to-clear semantics used by driver */
        {
            uint32_t w = (uint32_t)val;
            /* link_status_changed() writes bit 27 to clear; ISR writes
             * 0xffffffff to clear all pending bits.
             */
            s->int_status &= ~w;
            s->csr[5] = s->int_status;
            pcibase_update_irq(s);
        }
        break;
    case CSR6:
        /* Operation mode: bits used in activate/deactivate helpers */
        s->csr[6] = (uint32_t)val;
        /* bit 1: receiver enable */
        s->receiver_enabled = !!(s->csr[6] & (1u << 1));
        /* bit 13: transmitter enable */
        s->transmitter_enabled = !!(s->csr[6] & (1u << 13));
        break;
    case CSR7:
        /* Interrupt enable register */
        s->int_enable = (uint32_t)val;
        s->csr[7] = s->int_enable;
        pcibase_update_irq(s);
        break;
    case CSR8:
        s->csr[8] = (uint32_t)val;
        break;
    case CSR9:
        /* Boot ROM control; driver uses bit 12 to enable boot ROM access */
        s->csr[9] = (uint32_t)val;
        break;
    case CSR10:
        s->csr[10] = (uint32_t)val;
        break;
    case CSR11:
        s->csr[11] = (uint32_t)val;
        break;
    case CSR12:
        s->csr[12] = (uint32_t)val;
        break;
    case CSR13:
        s->csr[13] = (uint32_t)val;
        break;
    case CSR14:
        s->csr[14] = (uint32_t)val;
        break;
    case CSR15:
        /* Used in transceiver_voodoo() */
        s->csr[15] = (uint32_t)val;
        break;
    case CSR16:
        s->csr[16] = (uint32_t)val;
        break;
    default:
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    (void)s;
    (void)addr;
    (void)size;
    return val;
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
    pci_device_reset(PCI_DEVICE(dev));

    memset(s->csr, 0, sizeof(s->csr));
    s->open = 0;

    s->rx_ring_base = 0;
    s->tx_ring_base = 0;
    s->rx_dma_handle = 0;
    s->tx_dma_handle = 0;

    s->receiver_enabled = false;
    s->transmitter_enabled = false;

    s->int_status = 0;
    s->int_enable = 0;

    /* Default link status: let's report 100 Mbit link present so
     * that link_status() returns 100.
     * bit1 = 0 (100M), bit2 = 1 (not 10M)
     */
    s->link_status = (1u << 2); /* bit2=1, bit1=0 => 100Mbit */
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  XIRCOM_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  XIRCOM_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, XIRCOM_PCI_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* BAR0 MMIO: covers CSR0..CSR16 at offsets up to 0x80 */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    s->bar_info[0].size  = 0x100;
    s->bar_info[0].name  = "xircom_cb-mmio";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type  = BAR_TYPE_NONE;
        s->bar_info[i].size  = 0;
        s->bar_info[i].name  = NULL;
    }
    
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    memset(s->csr, 0, sizeof(s->csr));
    s->open = 0;
    s->has_msi = false;
    s->has_msix = false;

    s->rx_ring_base = 0;
    s->tx_ring_base = 0;
    s->rx_dma_handle = 0;
    s->tx_dma_handle = 0;

    s->receiver_enabled = false;
    s->transmitter_enabled = false;

    s->int_status = 0;
    s->int_enable = 0;

    /* Default link status: 100Mbit */
    s->link_status = (1u << 2);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, 0);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }

    (void)s;
}

static const VMStateDescription vmstate_pcibase = {
    .name = "xircom_cb_pci",
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
