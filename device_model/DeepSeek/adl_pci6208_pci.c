/*
 * QEMU virtual PCI device model for ADLINK PCI-6208
 * Based on driver: /home/eely/linux-7.1/drivers/comedi/drivers/adl_pci6208.c
 * Phase 2: Functional behavior (PIO handlers)
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/module.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_device.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "qemu/host-utils.h"
#include "qemu/bitops.h"

#define TYPE_PCIBASE_DEVICE "adl_pci6208_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_ADLINK 0x13fe
#define PCI_DEVICE_ID_ADL_PCI6208 0x6208

/* Register offsets (driver macros) */
#define PCI6208_AO_CONTROL(x)	(0x00 + (2 * (x)))	/* for x=0..7 */
#define PCI6208_AO_STATUS	0x00
#define PCI6208_AO_STATUS_DATA_SEND BIT(0)
#define PCI6208_DIO		0x40
#define PCI6208_DIO_DO_MASK	(0x0f)
#define PCI6208_DIO_DO_SHIFT	(0)
#define PCI6208_DIO_DI_MASK	(0xf0)
#define PCI6208_DIO_DI_SHIFT	(4)

/* BAR sizing: driver accesses I/O ports up to 0x41, so 0x80 is safe. */
#define PCI6208_IOSIZE 0x80

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

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Hardware register shadow */
    uint8_t regs[PCI6208_IOSIZE];
};

/* Minimal VMState to satisfy QEMU migration */
static const VMStateDescription vmstate_pcibase = {
    .name = "adl_pci6208_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT8_ARRAY(regs, PCIBaseState, PCI6208_IOSIZE),
        VMSTATE_END_OF_LIST()
    }
};

/* Forward declarations */
static void pcibase_realize(PCIDevice *pdev, Error **errp);
static void pcibase_uninit(PCIDevice *pdev);
static void pcibase_reset(DeviceState *dev);

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 2,
        .max_access_size = 2,
    },
    .impl = {
        .min_access_size = 2,
        .max_access_size = 2,
    },
};

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    }
    /* other BAR types omitted as only PIO is used */
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_ADLINK);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_ADL_PCI6208);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0xff00);  /* any class */
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 0);          /* no IRQ used */

    /* BAR Initialization: single I/O BAR at index 2 */
    s->bar_info[0].index = 2;
    s->bar_info[0].type  = BAR_TYPE_PIO;
    s->bar_info[0].size  = PCI6208_IOSIZE;
    s->bar_info[0].name  = "adl_pci6208-io";
    s->num_bars = 1;

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    /* No MSI/MSIX cleanup needed */
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    memset(s->regs, 0, sizeof(s->regs));
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size != 2) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad read size %u at 0x%"HWADDR_PRIx"\n",
                      __func__, size, addr);
        return ~0ULL;
    }

    switch (addr) {
    case PCI6208_AO_STATUS: /* 0x00 */
        /* Return status register; DATA_SEND always 0 to indicate ready */
        val = s->regs[addr] | (s->regs[addr+1] << 8);
        break;
    default:
        if (addr < PCI6208_DIO) {
            /* AO control region (0x00, 0x02, 0x04, 0x06, 0x08, 0x0A, 0x0C, 0x0E) */
            val = s->regs[addr] | (s->regs[addr+1] << 8);
        } else if (addr == PCI6208_DIO) {
            /* Digital I/O register: return stored DO value in lower nibble, 0 in upper nibble */
            val = s->regs[PCI6208_DIO] | (s->regs[PCI6208_DIO+1] << 8);
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: unknown read at 0x%"HWADDR_PRIx"\n",
                          __func__, addr);
        }
        break;
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 2) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad write size %u at 0x%"HWADDR_PRIx"\n",
                      __func__, size, addr);
        return;
    }

    switch (addr) {
    default:
        if (addr < PCI6208_DIO) {
            /* AO control write: store value */
            s->regs[addr] = val & 0xff;
            s->regs[addr+1] = (val >> 8) & 0xff;
        } else if (addr == PCI6208_DIO) {
            /* Digital output write: store full 16-bit value */
            s->regs[PCI6208_DIO] = val & 0xff;
            s->regs[PCI6208_DIO+1] = (val >> 8) & 0xff;
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: unknown write at 0x%"HWADDR_PRIx" = 0x%"PRIx64"\n",
                          __func__, addr, val);
        }
        break;
    }
}

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
        .name          = TYPE_PCIBASE_DEVICE,
        .parent        = TYPE_PCI_DEVICE,
        .instance_size = sizeof(PCIBaseState),
        .class_init    = pcibase_class_init,
        .interfaces    = interfaces,
    };

    type_register_static(&pcibase_info);
}

type_init(pcibase_register_types);
