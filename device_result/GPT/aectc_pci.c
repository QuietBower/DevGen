/*
 * QEMU PCI device for AEC VITCLTC (for linux/drivers/uio/uio_aec.c)
 * Target QEMU version: 8.2.x
 */

#include "qemu/osdep.h"
#include <inttypes.h>
#include <string.h>
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "sysemu/dma.h"
#include "sysemu/reset.h"
#include "hw/irq.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/pci/pcie.h"
#include "qom/object.h"
#include "qapi/visitor.h"
#include "hw/qdev-properties.h"

#define TYPE_PCIBASE_DEVICE "aectc_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register/macros/constants from driver */
#define INT_ENABLE              0x10
#define INT_DISABLE             0x0
#define INT_MASK_ALL            0x3F
#define PCI_VENDOR_ID_AEC       0xaecb
#define PCI_DEVICE_ID_AEC_VITCLTC 0x6250
#define INT_ENABLE_ADDR         0xFC
#define INT_MASK_ADDR           0x2E
#define INTA_DRVR_ADDR          0xFE
#define INTA_ENABLED_FLAG       0x08
#define INTA_FLAG               0x01
#define MAILBOX                 0x0F

/* BAR metadata definition */
typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM
} BARType;

typedef struct {
    int index;
    BARType type;
    hwaddr size;
    const char *name;
    bool sparse;
} BARInfo;

/* Device State */
struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];

    uint8_t *mmio_backing;
    size_t mmio_backing_size;

    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;

    /* Interrupt related state */
    qemu_irq irq;
    bool inta_enabled;      /* from INT_ENABLE register */
    uint8_t int_mask;       /* at INT_MASK_ADDR */
    uint8_t inta_status;    /* at INTA_DRVR_ADDR */
    uint8_t mailbox;        /* at MAILBOX */
};

/* Forward declarations */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len);
static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len);
static void pcibase_reset(DeviceState *dev);
static void pcibase_realize(PCIDevice *pdev, Error **errp);
static void pcibase_uninit(PCIDevice *pdev);

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
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

    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, bi->size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

/* Helper to update IRQ line based on internal registers */
static void pcibase_update_irq(PCIBaseState *s)
{
    bool want_irq = false;

    if (s->inta_enabled && !(s->int_mask & 0x01) && (s->inta_status & INTA_FLAG)) {
        want_irq = true;
    }

    if (want_irq) {
        qemu_irq_raise(s->irq);
    } else {
        qemu_irq_lower(s->irq);
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    /* All driver-visible accesses are byte or 32-bit; support generic sizes */
    switch (addr) {
    case 0x00:
        if (size == 1) {
            /* board vendor low byte */
            return 0x34; /* arbitrary constant */
        }
        break;
    case 0x01:
        if (size == 1) {
            /* board vendor high byte */
            return 0x12; /* arbitrary constant */
        }
        break;
    case 0x02:
        if (size == 1) {
            /* board number low byte */
            return 0x78; /* arbitrary constant */
        }
        break;
    case 0x03:
        if (size == 1) {
            /* board number high byte */
            return 0x56; /* arbitrary constant */
        }
        break;
    case 0x06:
        if (size == 1) {
            /* revision first char */
            return 'A';
        }
        break;
    case 0x07:
        if (size == 1) {
            /* revision second char */
            return '1';
        }
        break;
    case MAILBOX:
        if (size == 1) {
            return s->mailbox;
        }
        break;
    case INT_MASK_ADDR:
        if (size == 1) {
            return s->int_mask;
        }
        break;
    case INT_ENABLE_ADDR:
        if (size == 4) {
            return s->inta_enabled ? INT_ENABLE : INT_DISABLE;
        }
        break;
    case INTA_DRVR_ADDR:
        if (size == 1) {
            /* status visible to driver */
            return s->inta_status;
        }
        break;
    default:
        break;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] mmio_read addr=%" PRIx64 " size=%u\n",
                  TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case INT_ENABLE_ADDR:
        if (size == 4) {
            if (val == INT_ENABLE) {
                s->inta_enabled = true;
            } else if (val == INT_DISABLE) {
                s->inta_enabled = false;
            }
            pcibase_update_irq(s);
            return;
        }
        break;
    case INT_MASK_ADDR:
        if (size == 1) {
            s->int_mask = (uint8_t)val;
            pcibase_update_irq(s);
            return;
        }
        break;
    case MAILBOX:
        if (size == 1) {
            /* Application is documented to write 0x00 here to get next irq */
            s->mailbox = (uint8_t)val;
            if (s->mailbox == 0x00) {
                /* arm next interrupt: set INTA_FLAG */
                s->inta_status |= INTA_ENABLED_FLAG | INTA_FLAG;
            }
            pcibase_update_irq(s);
            return;
        }
        break;
    default:
        break;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] mmio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n",
                  TYPE_PCIBASE_DEVICE, (uint64_t)addr, (uint64_t)val, size);
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    (void)s;

    qemu_log_mask(LOG_UNIMP, "[%s] pio_read addr=%" PRIx64 " size=%u\n",
                  TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    (void)s;

    qemu_log_mask(LOG_UNIMP, "[%s] pio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n",
                  TYPE_PCIBASE_DEVICE, (uint64_t)addr, (uint64_t)val, size);
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    pci_device_reset(pdev);

    if (s->mmio_backing && s->mmio_backing_size) {
        memset(s->mmio_backing, 0, s->mmio_backing_size);
    }

    s->inta_enabled = false;
    s->int_mask = 0x00;
    s->inta_status = 0x00;
    s->mailbox = 0x00;

    pcibase_update_irq(s);
}

static void pcibase_dma_device_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    (void)s;
    (void)errp;
}

static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    uint32_t val = pci_default_read_config(pdev, addr, len);

    switch (len) {
    case 1:
        val &= 0xFF;
        break;
    case 2:
        val &= 0xFFFF;
        break;
    case 4:
    default:
        break;
    }
    return val;
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    if (addr >= PCI_BASE_ADDRESS_0 && addr <= PCI_BASE_ADDRESS_5) {
        pci_default_write_config(pdev, addr, val, len);
        return;
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* basic PCI config */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_AEC);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_AEC_VITCLTC);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_OTHERS);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* single MMIO BAR 0, at least large enough for highest used offset (0xFE) */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x100; /* 256 bytes, covers up to 0xFF */
    s->bar_info[0].name = "aectc-mmio";
    s->bar_info[0].sparse = false;

    pcibase_register_bar(pdev, s, &s->bar_info[0], errp);
    if (errp && *errp) {
        return;
    }

    /* allocate software backing for any unused space if needed */
    s->mmio_backing_size = s->bar_info[0].size;
    s->mmio_backing = g_malloc0(s->mmio_backing_size);

    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) {
        return;
    }

    /* legacy INTx line */
    s->irq = pci_allocate_irq(pdev);

    /* initial register values */
    s->inta_enabled = false;
    s->int_mask = 0x00;
    s->inta_status = 0x00;
    s->mailbox = 0x00;

    pcibase_update_irq(s);

    qemu_log_mask(LOG_UNIMP, "[%s] device realized\n", TYPE_PCIBASE_DEVICE);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (s->has_msix) {
        msix_uninit(pdev, NULL, 0);
        s->has_msix = false;
    }
    if (s->has_msi) {
        msi_uninit(pdev);
        s->has_msi = false;
    }

    if (s->mmio_backing) {
        g_free(s->mmio_backing);
        s->mmio_backing = NULL;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] device uninit\n", TYPE_PCIBASE_DEVICE);
}

static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->config_read  = pcibase_config_read;
    k->config_write = pcibase_config_write;

    k->realize = pcibase_realize;
    k->exit    = pcibase_uninit;
    dc->reset  = pcibase_reset;

    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void pcibase_register_types(void)
{
    static InterfaceInfo interfaces[] = {
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

