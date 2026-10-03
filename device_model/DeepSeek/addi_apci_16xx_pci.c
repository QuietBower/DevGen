/*
 * QEMU model for ADDI-DATA APCI-16xx digital I/O board (QEMU 8.2.10)
 * Based on Linux driver: addi_apci_16xx.c
 * Implements BAR0 as PIO region with DIR/OUT/IN registers.
 * Vendor/Device ID and channel count depend on board type (TBD).
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
#include "hw/qdev-properties.h"
#include "hw/qdev-core.h"
#include "migration/vmstate.h"

/* Register offsets from driver */
#define APCI16XX_IN_REG(x)     (((x) * 4) + 0x08)
#define APCI16XX_OUT_REG(x)    (((x) * 4) + 0x14)
#define APCI16XX_DIR_REG(x)    (((x) * 4) + 0x20)

#define TYPE_PCIBASE_DEVICE "addi_apci_16xx_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

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

/* Maximum number of subdevices (each handles 32 channels) */
#define MAX_SUBDEVS 4

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Device state: per-subdevice registers */
    uint32_t dir[MAX_SUBDEVS];   /* Direction: 0=input, 1=output */
    uint32_t out[MAX_SUBDEVS];   /* Output latch */
    uint32_t in[MAX_SUBDEVS];    /* Input pin state (external, read-only) */
};

/* MMIO/PIO Handlers (PIO used by this device) */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    int subdev = (addr - 0x08) / 4;

    if (addr < 0x08) {
        /* Reserved region, return 0 */
        return 0;
    }

    if (subdev < 0 || subdev >= MAX_SUBDEVS) {
        return 0;
    }

    switch ((addr - 0x08) % 0x0C) {
    case 0x00: /* IN_REG */
        val = s->in[subdev];
        break;
    /* OUT_REG and DIR_REG are not readable per hardware? Driver only reads IN. */
    /* Return 0 for others to avoid reads from write-only registers. */
    default:
        val = 0;
        break;
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    int subdev = (addr - 0x08) / 4;

    if (addr < 0x08 || subdev < 0 || subdev >= MAX_SUBDEVS) {
        /* Ignore reserved or out-of-range writes */
        return;
    }

    switch ((addr - 0x08) % 0x0C) {
    case 0x00: /* IN_REG: read-only, ignore writes */
        break;
    case 0x0C: /* OUT_REG (0x14+4*x) */
        s->out[subdev] = (uint32_t)val;
        break;
    case 0x18: /* DIR_REG (0x20+4*x) */
        s->dir[subdev] = (uint32_t)val;
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
    int i;

    /* Default all channels to inputs (dir=0) */
    memset(s->dir, 0, sizeof(s->dir));
    memset(s->out, 0, sizeof(s->out));
    /* External inputs are undetermined, set to 0 */
    memset(s->in, 0, sizeof(s->in));

    pci_device_reset(PCI_DEVICE(dev));
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, 0x15B8);
    pci_set_word(pci_conf + PCI_DEVICE_ID, 0x0000);  /* PLACEHOLDER: to be replaced with actual device ID from apci16xx_pci_table */
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0xFF00);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Set up BAR0: PIO for digital I/O registers */
    /* BAR size must cover all subdevices: max offset = DIR_REG(MAX_SUBDEVS-1) = 0x20+4*(MAX_SUBDEVS-1).
       We'll use 0x80 for simplicity. */
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 0x80;
    s->bar_info[0].name = "apci16xx-io";
    s->num_bars = 1;

    /* BAR Initialization */
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    /* Nothing to clean up */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "addi_apci_16xx_pci",
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
