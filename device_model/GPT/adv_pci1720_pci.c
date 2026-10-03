/*
 * QEMU PCI device model for Advantech PCI-1720 (for comedi adv_pci1720 driver)
 * Target QEMU: 8.2.10
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

#define TYPE_PCIBASE_DEVICE "adv_pci1720_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI1720_AO_LSB_REG(x)        (0x00 + ((x) * 2))
#define PCI1720_AO_MSB_REG(x)        (0x01 + ((x) * 2))
#define PCI1720_AO_RANGE_REG         0x08
#define PCI1720_AO_RANGE(c, r)       (((r) & 0x3) << ((c) * 2))
#define PCI1720_AO_RANGE_MASK(c)     PCI1720_AO_RANGE((c), 0x3)
#define PCI1720_SYNC_REG             0x09
#define PCI1720_SYNC_CTRL_REG        0x0f
#define PCI1720_BOARDID_REG          0x14

/* BAR metadata definition */
typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM
} BARType;

typedef struct {
    int index;      /* PCI BAR index (0-5) */
    BARType type;   /* MMIO/PIO/RAM */
    hwaddr size;    /* size of region */
    const char *name;
    bool sparse;
} BARInfo;

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];

    uint8_t *mmio_backing;
    size_t mmio_backing_size;

    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;

    /* Device-visible register state */
    uint8_t range_reg;          /* PCI1720_AO_RANGE_REG (0x08) */
    uint8_t sync_reg;           /* PCI1720_SYNC_REG (0x09) - unused by driver */
    uint8_t sync_ctrl_reg;      /* PCI1720_SYNC_CTRL_REG (0x0f) */
    uint8_t boardid_reg;        /* PCI1720_BOARDID_REG (0x14) */

    /* Analog output channel data (driver readback is in comedi, but we mirror) */
    uint16_t ao_data[4];        /* 4 AO channels, 12-bit values */
};

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

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    (void)opaque;

    qemu_log_mask(LOG_UNIMP, "[%s] unexpected mmio_read addr=%" PRIx64 " size=%u\n",
                  TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    (void)opaque;

    qemu_log_mask(LOG_UNIMP, "[%s] unexpected mmio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n",
                  TYPE_PCIBASE_DEVICE, (uint64_t)addr, (uint64_t)val, size);
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 1) {
        qemu_log_mask(LOG_GUEST_ERROR, "[%s] pio_read invalid size=%u addr=%" PRIx64 "\n",
                      TYPE_PCIBASE_DEVICE, size, (uint64_t)addr);
        return 0;
    }

    if (addr == PCI1720_AO_RANGE_REG) {
        return s->range_reg;
    } else if (addr == PCI1720_SYNC_REG) {
        return s->sync_reg;
    } else if (addr == PCI1720_SYNC_CTRL_REG) {
        return s->sync_ctrl_reg;
    } else if (addr == PCI1720_BOARDID_REG) {
        return s->boardid_reg;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] pio_read unmapped addr=%" PRIx64 "\n",
                  TYPE_PCIBASE_DEVICE, (uint64_t)addr);
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 1) {
        qemu_log_mask(LOG_GUEST_ERROR, "[%s] pio_write invalid size=%u addr=%" PRIx64 "\n",
                      TYPE_PCIBASE_DEVICE, size, (uint64_t)addr);
        return;
    }

    uint8_t v = (uint8_t)(val & 0xff);

    if (addr == PCI1720_AO_RANGE_REG) {
        s->range_reg = v;
        return;
    } else if (addr == PCI1720_SYNC_REG) {
        s->sync_reg = v;
        return;
    } else if (addr == PCI1720_SYNC_CTRL_REG) {
        s->sync_ctrl_reg = v;
        return;
    }

    if (addr <= PCI1720_AO_MSB_REG(3)) {
        int chan = (addr >> 1) & 0x3;
        bool is_msb = addr & 0x1;
        uint16_t cur = s->ao_data[chan];
        if (!is_msb) {
            cur = (cur & 0xff00u) | v;
        } else {
            cur = (cur & 0x00ffu) | ((uint16_t)v << 8);
        }
        s->ao_data[chan] = cur;
        return;
    }

    if (addr == PCI1720_BOARDID_REG) {
        s->boardid_reg = v;
        return;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] pio_write unmapped addr=%" PRIx64 " val=%02" PRIx64 "\n",
                  TYPE_PCIBASE_DEVICE, (uint64_t)addr, (uint64_t)v);
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    pci_device_reset(pdev);

    s->range_reg = 0;
    s->sync_reg = 0;
    s->sync_ctrl_reg = 0;
    s->boardid_reg = 0x0; /* arbitrary but consistent default */

    memset(s->ao_data, 0, sizeof(s->ao_data));

    if (s->mmio_backing && s->mmio_backing_size) {
        memset(s->mmio_backing, 0, s->mmio_backing_size);
    }
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
    /* Allow normal guest configuration space writes, including BARs */
    pci_default_write_config(pdev, addr, val, len);
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x13fe);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x1720);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_OTHERS);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* BAR layout strictly from driver usage: dev->iobase = pci_resource_start(pcidev, 2); */
    s->num_bars = 1;
    /* bar_info[0] is the first (and only) descriptor; index=2 means PCI BAR2 is used */
    s->bar_info[0].index = 2;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 0x20; /* minimal region covering used offsets up to 0x14 */
    s->bar_info[0].name = "adv_pci1720-io";
    s->bar_info[0].sparse = false;

    /* Initialize remaining BAR descriptors as unused to avoid any stale data */
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = NULL;
        s->bar_info[i].sparse = false;
    }

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) {
        return;
    }

    s->range_reg = 0;
    s->sync_reg = 0;
    s->sync_ctrl_reg = 0;
    s->boardid_reg = 0x0;
    memset(s->ao_data, 0, sizeof(s->ao_data));

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

