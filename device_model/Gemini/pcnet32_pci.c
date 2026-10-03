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


#define TYPE_PCIBASE_DEVICE "pcnet32_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_DEVICE_ID_AMD_LANCE_HOME	0x2001
#define PCNET32_WIO_RDP		0x10
#define PCNET32_WIO_RAP		0x12
#define PCNET32_WIO_RESET	0x14
#define PCNET32_WIO_BDP		0x16
#define PCNET32_DWIO_RDP	0x10
#define PCNET32_DWIO_RAP	0x14
#define PCNET32_DWIO_RESET	0x18
#define PCNET32_DWIO_BDP	0x1C
#define PCNET32_TOTAL_SIZE	0x20
#define CSR0		0
#define CSR0_INIT	0x1
#define CSR0_START	0x2
#define CSR0_STOP	0x4
#define CSR0_TXPOLL	0x8
#define CSR0_INTEN	0x40
#define CSR0_IDON	0x0100
#define CSR0_NORMAL	(CSR0_START | CSR0_INTEN)
#define CSR3		3
#define CSR4		4
#define CSR5		5
#define CSR5_SUSPEND	0x0001
#define CSR15		15

struct pcnet32_rx_head {
    uint32_t base;
    uint16_t buf_length;
    uint16_t status;
    uint32_t msg_length;
    uint32_t reserved;
};

struct pcnet32_tx_head {
    uint32_t base;
    uint16_t length;
    uint16_t status;
    uint32_t misc;
    uint32_t reserved;
};

struct pcnet32_init_block {
    uint16_t mode;
    uint16_t tlen_rlen;
    uint8_t phys_addr[6];
    uint16_t reserved;
    uint32_t filter[2];
    uint32_t rx_ring;
    uint32_t tx_ring;
};

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
    uint32_t csr0;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t rap;
    uint32_t csr[128];
    uint32_t bcr[32];

    /* DMA Context */
    uint32_t rx_ring_base;
    uint32_t tx_ring_base;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t isr = s->csr[0] & 0x8f00;
    uint32_t inten = s->csr[0] & 0x0040;
    if (isr && inten) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

static void pcnet32_soft_reset(PCIBaseState *s)
{
    memset(s->csr, 0, sizeof(s->csr));
    memset(s->bcr, 0, sizeof(s->bcr));
    s->rap = 0;
    
    s->csr[0] = 0x0004; /* STOP bit */
    s->csr[88] = 0x1003;
    s->csr[89] = 0x0262; /* Chip ID: 0x2621003 (PCnet/PCI II 79C970A) */
    
    /* MAC Address in CSR12-14 */
    s->csr[12] = 0x5452; /* 52:54 */
    s->csr[13] = 0x1200; /* 00:12 */
    s->csr[14] = 0x5634; /* 34:56 */
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    pcnet32_soft_reset(s);
}

static uint64_t pcibase_read_common(PCIBaseState *s, hwaddr addr, unsigned size)
{
    if (addr < 16) {
        uint8_t prom[16] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
        prom[14] = 0x57;
        prom[15] = 0x57;
        if (size == 1) return prom[addr];
        if (size == 2) return prom[addr] | (prom[addr+1] << 8);
        if (size == 4) return prom[addr] | (prom[addr+1] << 8) | (prom[addr+2] << 16) | (prom[addr+3] << 24);
        return 0;
    }
    if (size == 2) {
        switch (addr) {
            case 0x10: return s->csr[s->rap];
            case 0x12: return s->rap;
            case 0x14: pcnet32_soft_reset(s); return 0;
            case 0x16: return s->bcr[s->rap];
        }
    } else if (size == 4) {
        switch (addr) {
            case 0x10: return s->csr[s->rap];
            case 0x14: return s->rap;
            case 0x18: pcnet32_soft_reset(s); return 0;
            case 0x1C: return s->bcr[s->rap];
        }
    }
    return 0;
}

static void pcibase_write_common(PCIBaseState *s, hwaddr addr, uint64_t val, unsigned size)
{
    int is_csr = 0, is_bcr = 0, is_rap = 0, is_reset = 0;

    if (size == 2) {
        if (addr == 0x10) is_csr = 1;
        else if (addr == 0x12) is_rap = 1;
        else if (addr == 0x14) is_reset = 1;
        else if (addr == 0x16) is_bcr = 1;
    } else if (size == 4) {
        if (addr == 0x10) is_csr = 1;
        else if (addr == 0x14) is_rap = 1;
        else if (addr == 0x18) is_reset = 1;
        else if (addr == 0x1C) is_bcr = 1;
    }

    if (is_rap) {
        s->rap = val & 0x7f;
    } else if (is_reset) {
        pcnet32_soft_reset(s);
    } else if (is_csr) {
        if (s->rap == 0) {
            s->csr[0] &= ~(val & 0x8f00); /* W1C */
            s->csr[0] = (s->csr[0] & ~0x007f) | (val & 0x007f);
            if (val & 0x4) { /* STOP */
                s->csr[0] = 0x4;
            } else if (val & 0x1) { /* INIT */
                s->csr[0] |= 0x1;
                hwaddr init_addr = s->csr[1] | (s->csr[2] << 16);
                struct pcnet32_init_block ib;
                pci_dma_read(PCI_DEVICE(s), init_addr, &ib, sizeof(ib));
                s->rx_ring_base = le32_to_cpu(ib.rx_ring);
                s->tx_ring_base = le32_to_cpu(ib.tx_ring);
                s->csr[0] |= 0x0100; /* IDON */
                s->csr[0] &= ~0x1; /* Clear INIT */
            }
            if (val & 0x2) { /* START */
                s->csr[0] |= 0x2;
            }
            if (val & 0x8) { /* TXPOLL */
                s->csr[0] |= 0x0200; /* TINT */
            }
            pcibase_update_irq(s);
        } else {
            s->csr[s->rap] = val;
        }
    } else if (is_bcr) {
        if (s->rap < 32) {
            s->bcr[s->rap] = val;
        }
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    return pcibase_read_common(s, addr, size);
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    pcibase_write_common(s, addr, val, size);
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    return pcibase_read_common(s, addr, size);
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    pcibase_write_common(s, addr, val, size);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_AMD );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_AMD_LANCE_HOME );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_ETHERNET );
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
    s->bar_info[0] = (BARInfo){0, BAR_TYPE_PIO, PCNET32_TOTAL_SIZE, "pcnet32-pio"};
    s->bar_info[1] = (BARInfo){1, BAR_TYPE_MMIO, PCNET32_TOTAL_SIZE, "pcnet32-mmio"};  
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
    .name = "pcnet32_pci",
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
