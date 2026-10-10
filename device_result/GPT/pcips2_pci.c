/*
 * QEMU PCI device model for pcips2 (PCI PS/2 controller)
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

#define TYPE_PCIBASE_DEVICE "pcips2_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCIPS2_VENDOR_ID 0x14f2
#define PCIPS2_DEVICE_ID 0x0123
#define PCIPS2_CLASS_ID  PCI_CLASS_INPUT_KEYBOARD

#define PS2_CTRL        (0)
#define PS2_STATUS      (1)
#define PS2_DATA        (2)
#define PS2_CTRL_CLK        (1<<0)
#define PS2_CTRL_DAT        (1<<1)
#define PS2_CTRL_TXIRQ      (1<<2)
#define PS2_CTRL_ENABLE     (1<<3)
#define PS2_CTRL_RXIRQ      (1<<4)
#define PS2_STAT_CLK        (1<<0)
#define PS2_STAT_DAT        (1<<1)
#define PS2_STAT_PARITY     (1<<2)
#define PS2_STAT_RXFULL     (1<<5)
#define PS2_STAT_TXBUSY     (1<<6)
#define PS2_STAT_TXEMPTY    (1<<7)


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
    uint8_t regs[3]; /* 0: CTRL, 1: STATUS, 2: DATA */

    /* Internal IRQ line state */
    bool irq_asserted;
};


/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /*
     * Driver uses a shared legacy IRQ and enables RX interrupt via
     * PS2_CTRL_RXIRQ bit together with PS2_CTRL_ENABLE. It only ever
     * reads STATUS/DATA in the ISR and never writes an explicit IRQ
     * acknowledge register. We therefore assert an interrupt whenever
     * RXFULL is set and RXIRQ+ENABLE are set, and keep it asserted
     * while data remains in the RX buffer. Once the guest reads DATA
     * we clear RXFULL and drop the interrupt.
     */

    bool irq_should_assert = false;

    if ((s->regs[PS2_CTRL] & (PS2_CTRL_ENABLE | PS2_CTRL_RXIRQ)) ==
        (PS2_CTRL_ENABLE | PS2_CTRL_RXIRQ)) {
        if (s->regs[PS2_STATUS] & PS2_STAT_RXFULL) {
            irq_should_assert = true;
        }
    }

    if (irq_should_assert && !s->irq_asserted) {
        pci_set_irq(pdev, 1);
        s->irq_asserted = true;
    } else if (!irq_should_assert && s->irq_asserted) {
        pci_set_irq(pdev, 0);
        s->irq_asserted = false;
    }
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    (void)s;
    (void)pdev;
    (void)is_write;
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    (void)s;
    (void)addr;
    (void)size;
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    (void)s;
    (void)addr;
    (void)val;
    (void)size;
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint8_t ret = 0xff;

    /* Driver uses inb() on STATUS and DATA offsets (byte access). */
    if (size != 1) {
        return 0xff;
    }

    switch (addr) {
    case PS2_STATUS:
        /* Return current STATUS shadow register. */
        ret = s->regs[PS2_STATUS];
        break;
    case PS2_DATA:
        /*
         * Reading DATA consumes the current byte and clears RXFULL.
         * Driver's interrupt loop continues reading while RXFULL is set.
         */
        ret = s->regs[PS2_DATA];
        s->regs[PS2_STATUS] &= ~(PS2_STAT_RXFULL | PS2_STAT_PARITY);
        break;
    case PS2_CTRL:
        /* CTRL is normally written, but allow reads to return shadow. */
        ret = s->regs[PS2_CTRL];
        break;
    default:
        ret = 0xff;
        break;
    }

    /* Status or data consumption might change interrupt condition. */
    pcibase_update_irq(s);

    return ret;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint8_t v = (uint8_t)val;

    if (size != 1) {
        return;
    }

    switch (addr) {
    case PS2_CTRL:
        /*
         * Driver writes PS2_CTRL_ENABLE in open(), possibly ORed with
         * PS2_CTRL_RXIRQ when IRQ request succeeds. Writing 0 in close().
         * We simply store the value; no additional state machine.
         */
        s->regs[PS2_CTRL] = v;
        break;
    case PS2_STATUS:
        /*
         * Driver never writes STATUS in provided code; ignore writes.
         */
        break;
    case PS2_DATA:
        /*
         * Driver uses outb() to send host-to-device data. For now we
         * just record the last value and update TX status bits to look
         * immediately idle (TXEMPTY=1, TXBUSY=0).
         */
        s->regs[PS2_DATA] = v;
        s->regs[PS2_STATUS] &= ~PS2_STAT_TXBUSY;
        s->regs[PS2_STATUS] |= PS2_STAT_TXEMPTY;
        break;
    default:
        break;
    }

    /* CTRL or DATA changes may affect IRQ logic, recalc. */
    pcibase_update_irq(s);
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

    memset(s->regs, 0, sizeof(s->regs));

    /* Reset TX/RX status to idle: TXEMPTY=1, TXBUSY=0, RXFULL=0. */
    s->regs[PS2_STATUS] = PS2_STAT_TXEMPTY;

    s->irq_asserted = false;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCIPS2_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCIPS2_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCIPS2_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization - single I/O port BAR of 8 bytes (CTRL/STATUS/DATA + padding). */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 8;
    s->bar_info[0].name = "pcips2-io";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = "unused";
    }
  
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = false;
    s->has_msix = false;

    memset(s->regs, 0, sizeof(s->regs));
    /* Initialize STATUS to TXEMPTY as in reset, and clear IRQ state. */
    s->regs[PS2_STATUS] = PS2_STAT_TXEMPTY;
    s->irq_asserted = false;
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

    (void)s;
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "pcips2_pci",
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
