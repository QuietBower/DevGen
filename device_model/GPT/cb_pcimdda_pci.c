/*
 * QEMU PCI device model for cb_pcimdda
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

#define TYPE_PCIBASE_DEVICE "cb_pcimdda_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_CB            0x1307
#define PCIMDDA_PCI_VENDOR_ID   PCI_VENDOR_ID_CB
#define PCIMDDA_PCI_DEVICE_ID   0x0053

#define PCIMDDA_DA_CHAN(x)      (0x00 + (x) * 2)
#define PCIMDDA_8255_BASE_REG   0x0c

#define PCIMDDA_NUM_AO_CHAN     6

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

    /* Hardware Register Shadows */
    /* Analog output channels: 6 channels, 16-bit each, accessed as two 8-bit ports */
    uint16_t ao_readback[PCIMDDA_NUM_AO_CHAN];

    /* 8255 digital I/O: emulate 3 ports (A, B, C) as bytes plus a control register */
    uint8_t dio_port_a;
    uint8_t dio_port_b;
    uint8_t dio_port_c;
    uint8_t dio_ctrl;
};


/* Internal helper for status-triggered signaling. Currently no IRQ behavior
 * is used by the driver, so this is a no-op stub kept for completeness. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    (void)pdev;
}

/* Device-initiated DMA logic: not used by this driver, so omitted. */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    (void)pdev;
    (void)is_write;
}

/* MMIO/PIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* BAR interpreted as I/O-like register space but mapped as MMIO.
     * The driver uses inw()/outb() on I/O ports at dev->iobase + offsets,
     * and we map BAR3 as PIO in addition. MMIO path is kept simple and
     * mirrors the PIO layout for completeness. */

    if (size == 2) {
        /* 16-bit read, used only for "inw(dev->iobase + PCIMDDA_DA_CHAN(chan))"
         * to trigger simultaneous update. The actual read value is ignored
         * by the driver, but we can return the current readback value of
         * the channel for plausibility. */
        unsigned int chan = (addr / 2) & 0xF;
        if (chan < PCIMDDA_NUM_AO_CHAN) {
            val = s->ao_readback[chan];
        } else {
            val = 0xFFFF;
        }
        return val;
    }

    if (size != 1) {
        /* Only 8-bit and 16-bit are expected; others return 0. */
        return 0;
    }

    /* 8-bit register space */
    if (addr < PCIMDDA_8255_BASE_REG) {
        /* AO registers: 0x00..0x0B (6 channels * 2 bytes) */
        unsigned int chan = addr / 2;
        unsigned int lo = addr & 1;
        if (chan < PCIMDDA_NUM_AO_CHAN) {
            uint16_t v = s->ao_readback[chan];
            val = lo ? ((v >> 8) & 0xFF) : (v & 0xFF);
        } else {
            val = 0xFF;
        }
    } else {
        /* 8255 digital I/O block starting at PCIMDDA_8255_BASE_REG.
         * Map as: base+0: port A, base+1: port B, base+2: port C, base+3: control. */
        hwaddr off = addr - PCIMDDA_8255_BASE_REG;
        switch (off) {
        case 0:
            val = s->dio_port_a;
            break;
        case 1:
            val = s->dio_port_b;
            break;
        case 2:
            val = s->dio_port_c;
            break;
        case 3:
            val = s->dio_ctrl;
            break;
        default:
            val = 0xFF;
            break;
        }
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size == 2) {
        /* 16-bit writes are not used by the driver; ignore safely. */
        return;
    }

    if (size != 1) {
        return;
    }

    if (addr < PCIMDDA_8255_BASE_REG) {
        /* AO registers: 0x00..0x0B (6 channels * 2 bytes). Driver does two
         * outb() calls per sample: first LSB at offset, then MSB at offset+1.
         * We reconstruct the 16-bit value in the shadow readback array. */
        unsigned int chan = addr / 2;
        unsigned int lo = addr & 1; /* 0 -> LSB, 1 -> MSB */
        uint8_t b = (uint8_t)val;

        if (chan < PCIMDDA_NUM_AO_CHAN) {
            uint16_t old = s->ao_readback[chan];
            uint16_t newv;
            if (lo == 0) {
                /* Write LSB, keep MSB */
                newv = (old & 0xFF00) | b;
            } else {
                /* Write MSB, keep LSB */
                newv = (old & 0x00FF) | ((uint16_t)b << 8);
            }
            s->ao_readback[chan] = newv;
        }
    } else {
        /* 8255 digital I/O block */
        hwaddr off = addr - PCIMDDA_8255_BASE_REG;
        uint8_t b = (uint8_t)val;
        switch (off) {
        case 0:
            s->dio_port_a = b;
            break;
        case 1:
            s->dio_port_b = b;
            break;
        case 2:
            s->dio_port_c = b;
            break;
        case 3:
            /* Control register: store but we do not implement full 8255
             * mode semantics, as the driver core handles it generically. */
            s->dio_ctrl = b;
            break;
        default:
            break;
        }
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size == 2) {
        /* 16-bit I/O read: inw(dev->iobase + PCIMDDA_DA_CHAN(chan)) */
        unsigned int chan = (addr / 2) & 0xF;
        if (chan < PCIMDDA_NUM_AO_CHAN) {
            val = s->ao_readback[chan];
        } else {
            val = 0xFFFF;
        }
        return val;
    }

    if (size != 1) {
        return 0;
    }

    if (addr < PCIMDDA_8255_BASE_REG) {
        /* AO registers: 8-bit view */
        unsigned int chan = addr / 2;
        unsigned int lo = addr & 1;
        if (chan < PCIMDDA_NUM_AO_CHAN) {
            uint16_t v = s->ao_readback[chan];
            val = lo ? ((v >> 8) & 0xFF) : (v & 0xFF);
        } else {
            val = 0xFF;
        }
    } else {
        /* 8255 digital I/O */
        hwaddr off = addr - PCIMDDA_8255_BASE_REG;
        switch (off) {
        case 0:
            val = s->dio_port_a;
            break;
        case 1:
            val = s->dio_port_b;
            break;
        case 2:
            val = s->dio_port_c;
            break;
        case 3:
            val = s->dio_ctrl;
            break;
        default:
            val = 0xFF;
            break;
        }
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size == 2) {
        /* Driver only performs 8-bit writes for AO; ignore 16-bit. */
        return;
    }

    if (size != 1) {
        return;
    }

    if (addr < PCIMDDA_8255_BASE_REG) {
        /* AO registers */
        unsigned int chan = addr / 2;
        unsigned int lo = addr & 1;
        uint8_t b = (uint8_t)val;

        if (chan < PCIMDDA_NUM_AO_CHAN) {
            uint16_t old = s->ao_readback[chan];
            uint16_t newv;
            if (lo == 0) {
                newv = (old & 0xFF00) | b;
            } else {
                newv = (old & 0x00FF) | ((uint16_t)b << 8);
            }
            s->ao_readback[chan] = newv;
        }
    } else {
        /* 8255 digital I/O */
        hwaddr off = addr - PCIMDDA_8255_BASE_REG;
        uint8_t b = (uint8_t)val;
        switch (off) {
        case 0:
            s->dio_port_a = b;
            break;
        case 1:
            s->dio_port_b = b;
            break;
        case 2:
            s->dio_port_c = b;
            break;
        case 3:
            s->dio_ctrl = b;
            break;
        default:
            break;
        }
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

    pci_device_reset(PCI_DEVICE(dev));

    /* Reset AO readback values to 0, consistent with comedi expectations. */
    for (i = 0; i < PCIMDDA_NUM_AO_CHAN; ++i) {
        s->ao_readback[i] = 0;
    }

    /* Reset digital I/O state */
    s->dio_port_a = 0x00;
    s->dio_port_b = 0x00;
    s->dio_port_c = 0x00;
    s->dio_ctrl   = 0x9B; /* arbitrary 8255 power-up-like value (input mode), not used by driver */
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }
    
    /* PCI requires BAR sizes to be a power of 2. Prevent QEMU assert crash. */
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCIMDDA_PCI_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCIMDDA_PCI_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_OTHERS );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* Configure BAR3 as the I/O space used by the driver. The driver calls
     * pci_resource_start(pcidev, 3) and uses it with inw/outb. Use a small
     * size covering AO and 8255 region (e.g., 0x20 bytes). */
    s->num_bars = 1;
    s->bar_info[0].index = 3;
    s->bar_info[0].type  = BAR_TYPE_PIO;
    s->bar_info[0].size  = 0x20;
    s->bar_info[0].name  = "cb_pcimdda-io";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize internal state */
    pcibase_reset(DEVICE(pdev));
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    (void)s;
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "cb_pcimdda_pci",
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

