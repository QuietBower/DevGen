/*
 * QEMU PCnet32 PCI device model (AMD PCnet-PCI II 79C970A)
 * Generated from Linux driver pcnet32.c (DWIO mode)
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

#define TYPE_PCIBASE_DEVICE "pcnet32_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_PCNET32    0x1022
#define PCI_DEVICE_ID_PCNET32    0x2621
#define PCI_CLASS_PCNET32        0x0200

/* IO Port Offsets (DWIO mode) */
#define PCN32_IO_RDP       0x10
#define PCN32_IO_RAP       0x14
#define PCN32_IO_RESET     0x18
#define PCN32_IO_BDP       0x1C
#define PCN32_IO_TOTAL_SIZE 0x20

/* CSR Register Indices */
#define CSR0    0
#define CSR1    1
#define CSR2    2
#define CSR3    3
#define CSR4    4
#define CSR5    5
#define CSR15   15
#define CSR80   80
#define CSR88   88
#define CSR89   89
#define CSR112  112
#define CSR114  114
#define CSR124  124

/* CSR0 bits */
#define CSR0_INIT     0x0001
#define CSR0_START    0x0002
#define CSR0_STOP     0x0004
#define CSR0_TXPOLL   0x0008
#define CSR0_INTEN    0x0040
#define CSR0_IDON     0x0100
#define CSR0_NORMAL   (CSR0_START | CSR0_INTEN)

/* Interrupt bits in CSR0 */
#define CSR0_ERR      0x8000
#define CSR0_BABL     0x4000
#define CSR0_CERR     0x2000
#define CSR0_MISS     0x1000
#define CSR0_MERR     0x0800
#define CSR0_RINT     0x0400
#define CSR0_TINT     0x0200
#define CSR0_IDON_INTR 0x0080  /* IDON interrupt (unused by driver) */
#define CSR0_INTR_MASK 0x8f80  /* all interrupt sources */

enum {
    CSR_COUNT = 128,
    BCR_COUNT = 36,
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

    uint16_t csr[CSR_COUNT];
    uint16_t bcr[BCR_COUNT];
    uint16_t rap;

    hwaddr rx_ring_addr;
    hwaddr tx_ring_addr;
    uint16_t pmcsr;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    int level = 0;
    if (s->csr[CSR0] & CSR0_INTEN) {
        if (s->csr[CSR0] & CSR0_INTR_MASK) {
            level = 1;
        }
    }
    pci_set_irq(pdev, level);
}

static void pcibase_set_csr0_bits(PCIBaseState *s, uint16_t bits)
{
    s->csr[CSR0] |= bits;
    if (bits & CSR0_INTR_MASK) {
        pcibase_update_irq(s);
    }
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    s->rap = 0;
    memset(s->csr, 0, sizeof(s->csr));
    memset(s->bcr, 0, sizeof(s->bcr));
    /* CSR0 reset value is 4 (16-bit mode) */
    s->csr[CSR0] = 0x0004;
    /* CSR88/89: chip version for 79C970A: 0x26210003 */
    s->csr[CSR88] = 0x0003;
    s->csr[CSR89] = 0x2621;
    s->rx_ring_addr = 0;
    s->tx_ring_addr = 0;
    s->pmcsr = 0;
    s->intr_status = 0;
    s->intr_mask = 0;
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size != 4) {
        return val;
    }

    switch (addr) {
    case PCN32_IO_RDP:  /* CSR data port */
        if (s->rap < CSR_COUNT) {
            val = s->csr[s->rap];
            /* Reading CSR0 clears interrupt bits */
            if (s->rap == CSR0) {
                s->csr[CSR0] &= ~CSR0_INTR_MASK;
                pcibase_update_irq(s);
            }
        }
        break;
    case PCN32_IO_RAP:  /* Register address pointer */
        val = s->rap;
        break;
    case PCN32_IO_RESET:  /* Reset */
        pcibase_reset(DEVICE(s));
        val = 0;
        break;
    case PCN32_IO_BDP:  /* BCR data port */
        if (s->rap < BCR_COUNT) {
            val = s->bcr[s->rap];
        }
        break;
    default:
        break;
    }
    return val & 0xffff;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint16_t v = val & 0xffff;

    if (size != 4) {
        return;
    }

    switch (addr) {
    case PCN32_IO_RDP:  /* CSR data port */
        if (s->rap < CSR_COUNT) {
            if (s->rap == CSR0) {
                /* CSR0 write: process commands */
                /* Mask off interrupt status bits (they are set by HW) */
                uint16_t old = s->csr[CSR0];
                uint16_t wmask = v & ~CSR0_INTR_MASK;  /* only writable bits */
                s->csr[CSR0] = (old & CSR0_INTR_MASK) | wmask;
                if (v & CSR0_INIT) {
                    /* Simulate init done immediately */
                    pcibase_set_csr0_bits(s, CSR0_IDON);
                }
                if (v & CSR0_STOP) {
                    pcibase_set_csr0_bits(s, CSR0_STOP);
                }
                /* Writing 0 to INIT, START, STOP, TXPOLL, INTEN clears them */
                if (!(v & CSR0_INIT)) s->csr[CSR0] &= ~CSR0_INIT;
                if (!(v & CSR0_START)) s->csr[CSR0] &= ~CSR0_START;
                if (!(v & CSR0_STOP)) s->csr[CSR0] &= ~CSR0_STOP;
                if (!(v & CSR0_TXPOLL)) s->csr[CSR0] &= ~CSR0_TXPOLL;
                if (!(v & CSR0_INTEN)) s->csr[CSR0] &= ~CSR0_INTEN;
                pcibase_update_irq(s);
            } else {
                s->csr[s->rap] = v;
            }
        }
        break;
    case PCN32_IO_RAP:
        s->rap = v;
        break;
    case PCN32_IO_BDP:
        if (s->rap < BCR_COUNT) {
            s->bcr[s->rap] = v;
        }
        break;
    case PCN32_IO_RESET:
        /* Ignore writes to reset? Driver does outl? No, reset is read-only */
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

    pci_set_word(pci_conf + PCI_VENDOR_ID, 0x1022);
    pci_set_word(pci_conf + PCI_DEVICE_ID, 0x2621);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0200);
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
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = PCN32_IO_TOTAL_SIZE;
    s->bar_info[0].name = "pcnet32-io";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    pcibase_reset(DEVICE(s));
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
