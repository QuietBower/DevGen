/* Tulip QEMU PCI device model - DEC 21140A */
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

#define TYPE_PCIBASE_DEVICE "tulip_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define VENDOR_ID 0x1011
#define DEVICE_ID 0x0009
#define CLASS_ID  0x0200

/* Register offsets */
#define CSR0    0x00
#define CSR1    0x08
#define CSR2    0x10
#define CSR3    0x18
#define CSR4    0x20
#define CSR5    0x28
#define CSR6    0x30
#define CSR7    0x38
#define CSR8    0x40
#define CSR9    0x48
#define CSR10   0x50
#define CSR11   0x58
#define CSR12   0x60
#define CSR13   0x68
#define CSR14   0x70
#define CSR15   0x78

/* Bit definitions */
#define DESC_RING_WRAP 0x02000000
#define CSR5_RS 0x000e0000
#define CSR5_TS 0x00700000
#define RxOn 0x0002
#define TxOn 0x2000
#define RxTx (RxOn | TxOn)

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

    uint32_t csr[16];            /* CSR0-15 */
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t active = s->csr[5] & s->csr[7];
    int irq_level = (active != 0) ? 1 : 0;
    if (msi_enabled(pdev)) {
        if (irq_level) {
            msi_notify(pdev, 0);
        }
        /* MSI does not require deassert */
    } else {
        pci_set_irq(pdev, irq_level);
    }
}

/* MMIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size != 4) {
        return ~0ULL;
    }

    if (addr >= 0x80) {
        return 0;
    }

    int reg = addr >> 3;
    if (reg >= 16) {
        return 0;
    }

    switch (addr) {
    case CSR5: /* 0x28 */
        val = s->csr[5];
        break;
    case CSR8: /* 0x40 */
        val = s->csr[8];
        s->csr[8] = 0;
        break;
    case CSR9: /* 0x48 */
        /* Fake EEPROM: always return done + data = 0xFFFF */
        val = 0x8000FFFF;
        break;
    default:
        val = s->csr[reg];
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        return;
    }

    if (addr >= 0x80) {
        return;
    }

    int reg = addr >> 3;
    if (reg >= 16) {
        return;
    }

    switch (addr) {
    case CSR0: /* 0x00 */
        if (val & 1) {  /* Reset command */
            /* Soft reset: clear all CSRs */
            memset(s->csr, 0, sizeof(s->csr));
            /* Update IRQ (low) */
            pcibase_update_irq(s);
        }
        s->csr[0] = val;
        break;

    case CSR1: /* 0x08 */
        /* Transmit poll demand: ignore for now */
        break;

    case CSR2: /* 0x10 */
        /* Receive poll demand: ignore */
        break;

    case CSR3: /* 0x18 */
        s->csr[3] = val;
        break;

    case CSR4: /* 0x20 */
        s->csr[4] = val;
        break;

    case CSR5: /* 0x28 */
        /* Write-1-to-clear */
        s->csr[5] &= ~val;
        pcibase_update_irq(s);
        break;

    case CSR6: /* 0x30 */
        s->csr[6] = val;
        if (!(val & RxTx)) {
            /* Stop transmit/receive: clear process state bits */
            s->csr[5] &= ~(CSR5_RS | CSR5_TS);
        }
        pcibase_update_irq(s);
        break;

    case CSR7: /* 0x38 */
        s->csr[7] = val;
        pcibase_update_irq(s);
        break;

    case CSR8: /* 0x40 */
        s->csr[8] = val;
        break;

    case CSR9: /* 0x48 */
        /* Write ignored */
        break;

    default:
        s->csr[reg] = val;
        break;
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    memset(s->csr, 0, sizeof(s->csr));
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
        /* Not used in this fix */
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Make device conventional PCI - do not enable PCIe capabilities */
    /* pdev->cap_present |= QEMU_PCI_CAP_EXPRESS; */
    /* pcie_endpoint_cap_init(pdev, 0x80); */

    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    s->num_bars = 1;
    /* Change BAR0 to memory-mapped, with size large enough to satisfy driver's io_size */
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x400;  /* 1024 bytes, > typical 128-byte register space */
    s->bar_info[0].name = "tulip-mmio";
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
    .name = "tulip_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32_ARRAY(csr, PCIBaseState, 16),
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
