/*
 * QEMU PCI device model for adl_pci6208
 * Phase 2: Functional behavior implementation based on Linux driver.
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
#include "hw/pci/pci_ids.h"

#define TYPE_PCIBASE_DEVICE "adl_pci6208_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI6208_AO_CONTROL(x)        (0x00 + (2 * (x)))
#define PCI6208_AO_STATUS            0x00
#define PCI6208_AO_STATUS_DATA_SEND  (1u << 0)
#define PCI6208_DIO                  0x40
#define PCI6208_DIO_DO_MASK          (0x0f)
#define PCI6208_DIO_DO_SHIFT         (0)
#define PCI6208_DIO_DI_MASK          (0xf0)
#define PCI6208_DIO_DI_SHIFT         (4)

/* Vendor/device/class identifiers taken from driver pci_device_id table */
#ifndef PCI_VENDOR_ID_ADLINK
#define PCI_VENDOR_ID_ADLINK 0x144a
#endif

#define PCIBASE_VENDOR_ID            PCI_VENDOR_ID_ADLINK
#define PCIBASE_DEVICE_ID            0x6208
#define PCIBASE_CLASS_ID             PCI_CLASS_OTHERS

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
    uint16_t ao_control_shadow[4];
    uint8_t dio_shadow;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* No interrupt logic defined in driver source. */
    (void)pdev;
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* No DMA usage in driver source. */
    (void)s;
    (void)pdev;
    (void)is_write;
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Only 16-bit accesses are used by the driver (inw/outw). */
    if (size != 2) {
        return 0xffff;
    }

    switch (addr) {
    case PCI6208_AO_STATUS:
        /*
         * The driver polls this register using pci6208_ao_eoc():
         *   status = inw(iobase + PCI6208_AO_STATUS);
         *   if ((status & PCI6208_AO_STATUS_DATA_SEND) == 0) EOC.
         * It assumes the bit is 0 when hardware is ready to accept
         * a new value and 1 while a conversion is in progress.
         * We model the device as always ready, so return 0.
         */
        val = 0x0000;
        break;

    case PCI6208_DIO:
        /*
         * Combined digital I/O register.
         * DO: bits [3:0]  (output state)
         * DI: bits [7:4]  (input state)
         *
         * Driver behaviors:
         *  - DO write: outw(s->state, iobase + PCI6208_DIO);
         *  - DO readback: initial state from DO bits, later via s->state.
         *  - DI read: uses only DI bits.
         *
         * We simply mirror digital outputs into the DO bits and keep DI at 0.
         */
        val = (uint16_t)(s->dio_shadow & PCI6208_DIO_DO_MASK);
        /* DI bits remain zero (no inputs asserted). */
        break;

    default:
        /* Unused/undefined offsets: return all ones for 16-bit read. */
        val = 0xffff;
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* Only 16-bit accesses are used by the driver (inw/outw). */
    if (size != 2) {
        return;
    }

    switch (addr) {
    default:
        /* Check for AO control channel writes: PCI6208_AO_CONTROL(x) */
        if (addr < 0x40 && ((addr & 0x1) == 0)) {
            /*
             * AO_CONTROL(x) = 0x00 + 2*x
             * Driver writes:
             *   outw(comedi_offset_munge(s, val),
             *        iobase + PCI6208_AO_CONTROL(chan));
             * We store the raw 16-bit value into our shadow for channels 0-3.
             */
            unsigned int chan = addr / 2;
            if (chan < 4) {
                s->ao_control_shadow[chan] = (uint16_t)val;
            }
        } else if (addr == PCI6208_DIO) {
            /*
             * Digital output register write.
             * Driver writes s->state (0..0x0f) with outw().
             * Only lower 4 bits are used/defined for DO; mask accordingly.
             */
            s->dio_shadow = (uint8_t)(val & PCI6208_DIO_DO_MASK);
        }
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* No port I/O usage in driver source. */
    (void)s;
    (void)addr;
    (void)size;
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    /* No port I/O usage in driver source. */
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

    /* Reset register shadows to power-on defaults. */
    memset(s->ao_control_shadow, 0, sizeof(s->ao_control_shadow));
    s->dio_shadow = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCIBASE_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCIBASE_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCIBASE_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /*
     * BAR Initialization
     *
     * The Linux driver uses pci_resource_start(pcidev, 2) for dev->iobase
     * and then performs inw/outw on that base. We therefore expose our
     * MMIO/PIO window via BAR2 to match the index.
     */
    s->num_bars = 1;
    s->bar_info[0].index = 2;           /* BAR2 as used by the driver */
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x100;        /* Covers AO, status, and DIO */
    s->bar_info[0].name = "adl_pci6208-bar2";
    for (int i = 1; i < 6; i++) {
        if (i == 2) {
            continue; /* already assigned above */
        }
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = "unused";
    }

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No MSI/MSI-X usage in driver source. */
    s->has_msi = false;
    s->has_msix = false;

    /* No DMA configuration used in driver source. */

    /* No internal timers described in driver source. */

    /* Initialize register shadows. */
    memset(s->ao_control_shadow, 0, sizeof(s->ao_control_shadow));
    s->dio_shadow = 0;
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

    /* No dynamic resources to free in skeleton. */
    (void)s;
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "adl_pci6208_pci",
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

