/*
 * QEMU PCI Device Model for ADDI APCI-2200 (Comedi driver)
 *
 * Generated for minimal behavior required by
 * linux/drivers/comedi/drivers/addi_apci_2200.c
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

#define TYPE_PCIBASE_DEVICE "addi_apci_2200_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define APCI2200_DI_REG   0x00
#define APCI2200_DO_REG   0x04
#define APCI2200_WDOG_REG 0x08

#define PCI_VENDOR_ID_ADDIDATA 0x15B8
#define APCI2200_VENDOR_ID PCI_VENDOR_ID_ADDIDATA
#define APCI2200_DEVICE_ID 0x1005

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
    uint16_t di_reg;    /* Digital input register (read-only to driver) */
    uint16_t do_reg;    /* Digital output register (read/write) */
    uint16_t wdog_reg;  /* Watchdog base - behavior handled in helper driver */
};


/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    /* The provided driver code does not reference any IRQs for this board.
     * No interrupt generation is required or implemented.
     */
    (void)s;
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /* The driver does not perform any DMA operations for this board. */
    (void)s;
    (void)is_write;
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* The Linux driver uses inw/outw on dev->iobase + APCI2200_*_REG.
     * That means 16-bit I/O port accesses. Our BAR is mapped as IO space
     * and sized to at least 0x10 bytes, so we implement 16-bit semantics
     * here for completeness if MMIO is ever used, but primary accesses go
     * through pcibase_pio_read.
     */

    switch (addr) {
    case APCI2200_DI_REG:
        if (size == 2) {
            /* Digital input register: return shadow value. */
            val = s->di_reg;
        }
        break;
    case APCI2200_DO_REG:
        if (size == 2) {
            /* Digital output register: return last written state. */
            val = s->do_reg;
        }
        break;
    case APCI2200_WDOG_REG:
        if (size == 2) {
            /* Watchdog register: the main apci2200 driver only passes this
             * base address to addi_watchdog_init/reset; actual behavior is
             * implemented in that helper driver and not required here.
             * Return a stable value (0) to avoid surprises.
             */
            val = s->wdog_reg;
        }
        break;
    default:
        /* Unused/undefined offsets return 0. */
        val = 0;
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case APCI2200_DO_REG:
        if (size == 2) {
            /* The driver writes 16-bit values via outw to DO_REG.
             * Store the lower 16 bits in the shadow register.
             */
            s->do_reg = (uint16_t)val;
        }
        break;
    case APCI2200_WDOG_REG:
        if (size == 2) {
            /* Watchdog control. The apci2200 driver only calls
             * addi_watchdog_reset(dev->iobase + APCI2200_WDOG_REG).
             * Without the helper driver's code we cannot model exact
             * semantics, so we simply store the last written value.
             */
            s->wdog_reg = (uint16_t)val;
        }
        break;
    default:
        /* DI_REG is read-only in the driver; ignore writes to it.
         * Writes to other undefined offsets are ignored.
         */
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* The driver uses inw() which is a 16-bit read. */
    if (size != 2) {
        return 0;
    }

    switch (addr) {
    case APCI2200_DI_REG:
        /* Digital input register: return shadow value. */
        val = s->di_reg;
        break;
    case APCI2200_DO_REG:
        /* apci2200_do_insn_bits() first reads back the DO register via inw,
         * stores to s->state, then possibly writes a new value via outw.
         * So this must return our last DO write.
         */
        val = s->do_reg;
        break;
    case APCI2200_WDOG_REG:
        /* No direct read from watchdog in this driver, but return a
         * stable value in case other code inspects it.
         */
        val = s->wdog_reg;
        break;
    default:
        val = 0;
        break;
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* The driver uses outw() which is a 16-bit write. */
    if (size != 2) {
        return;
    }

    switch (addr) {
    case APCI2200_DO_REG:
        /* Digital output register. Store the 16-bit state.
         * This is read back by subsequent inw() calls.
         */
        s->do_reg = (uint16_t)val;
        break;
    case APCI2200_WDOG_REG:
        /* Watchdog control; store last written value only. */
        s->wdog_reg = (uint16_t)val;
        break;
    default:
        /* Ignore writes to DI_REG and undefined offsets. */
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

    /* Reset PCI core state. */
    pci_device_reset(PCI_DEVICE(dev));

    /* The apci2200_reset() function in the driver performs:
     *   outw(0x0, dev->iobase + APCI2200_DO_REG);
     *   addi_watchdog_reset(dev->iobase + APCI2200_WDOG_REG);
     * We mirror that logical effect here by resetting the shadow
     * DO and watchdog registers to 0. DI defaults to 0 as well.
     */
    s->di_reg = 0;
    s->do_reg = 0;
    s->wdog_reg = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  APCI2200_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  APCI2200_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_OTHERS );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization
     * The driver calls: dev->iobase = pci_resource_start(pcidev, 1);
     * so BAR1 must be an IO region that is large enough to cover
     * APCI2200_DI_REG/DO_REG/WDOG_REG (offsets 0x00-0x08).
     */
    s->num_bars = 1;
    s->bar_info[0].index = 1;              /* BAR1 as used by driver */
    s->bar_info[0].type = BAR_TYPE_PIO;    /* inw/outw => I/O space */
    s->bar_info[0].size = 0x10;            /* covers at least 0x0-0x8 */
    s->bar_info[0].name = "addi_apci_2200-bar1";

    for (int i = 0; i < 6; i++) {
        if (i != s->bar_info[0].index) {
            s->bar_info[i].index = i;
            s->bar_info[i].type = BAR_TYPE_NONE;
            s->bar_info[i].size = 0;
            s->bar_info[i].name = "";
        }
    }

    /* Register active BAR(s). */
    for (int i = 0; i < 6; i++) {
        if (s->bar_info[i].type != BAR_TYPE_NONE) {
            pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
        }
    }

    s->has_msi = false;
    s->has_msix = false;

    /* Initialize register shadows to reset state. */
    s->di_reg = 0;
    s->do_reg = 0;
    s->wdog_reg = 0;
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
    .name = "addi_apci_2200_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT16(di_reg, PCIBaseState),
        VMSTATE_UINT16(do_reg, PCIBaseState),
        VMSTATE_UINT16(wdog_reg, PCIBaseState),
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
