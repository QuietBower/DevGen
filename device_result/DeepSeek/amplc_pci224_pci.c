/*
 * QEMU model for Amplicon PCI224/234
 * Generated from Linux driver amplc_pci224.c
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/module.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"

/* Driver-provided identifiers */
#define PCI_VENDOR_ID_AMPLICON 0x14dc
#define PCI224_DEVICE_ID 0x0007
#define PCI224_CLASS_ID   0x0000 /* unknown, to be provided */

/* Register offsets */
#define PCI224_DACDATA   0x00
#define PCI224_SOFTTRIG  0x00
#define PCI224_DACCON    0x02
#define PCI224_FIFOSIZ   0x04
#define PCI224_DACCEN    0x06
#define PCI224_Z2_BASE   0x14
#define PCI224_ZCLK_SCE  0x1A
#define PCI224_ZGAT_SCE  0x1D
#define PCI224_INT_SCE   0x1E

/* DAC control register bits */
#define PCI224_DACCON_TRIG(x)   (((x) & 0x7) << 0)
#define PCI224_DACCON_TRIG_MASK  PCI224_DACCON_TRIG(7)
#define PCI224_DACCON_TRIG_NONE  PCI224_DACCON_TRIG(0)
#define PCI224_DACCON_TRIG_SW    PCI224_DACCON_TRIG(1)
#define PCI224_DACCON_TRIG_EXTP  PCI224_DACCON_TRIG(2)
#define PCI224_DACCON_TRIG_EXTN  PCI224_DACCON_TRIG(3)
#define PCI224_DACCON_TRIG_Z2CT0 PCI224_DACCON_TRIG(4)
#define PCI224_DACCON_TRIG_Z2CT1 PCI224_DACCON_TRIG(5)
#define PCI224_DACCON_TRIG_Z2CT2 PCI224_DACCON_TRIG(6)
#define PCI224_DACCON_POLAR(x)   (((x) & 0x1) << 3)
#define PCI224_DACCON_POLAR_MASK  PCI224_DACCON_POLAR(1)
#define PCI224_DACCON_POLAR_UNI   PCI224_DACCON_POLAR(0)
#define PCI224_DACCON_POLAR_BI    PCI224_DACCON_POLAR(1)
#define PCI224_DACCON_VREF(x)    (((x) & 0x3) << 4)
#define PCI224_DACCON_VREF_MASK   PCI224_DACCON_VREF(3)
#define PCI224_DACCON_VREF_1_25   PCI224_DACCON_VREF(0)
#define PCI224_DACCON_VREF_2_5    PCI224_DACCON_VREF(1)
#define PCI224_DACCON_VREF_5      PCI224_DACCON_VREF(2)
#define PCI224_DACCON_VREF_10     PCI224_DACCON_VREF(3)
#define PCI224_DACCON_FIFOWRAP    BIT(7)
#define PCI224_DACCON_FIFOENAB    BIT(8)
#define PCI224_DACCON_FIFOINTR(x) (((x) & 0x7) << 9)
#define PCI224_DACCON_FIFOINTR_MASK PCI224_DACCON_FIFOINTR(7)
#define PCI224_DACCON_FIFOINTR_EMPTY  PCI224_DACCON_FIFOINTR(0)
#define PCI224_DACCON_FIFOINTR_NEMPTY PCI224_DACCON_FIFOINTR(1)
#define PCI224_DACCON_FIFOINTR_NHALF  PCI224_DACCON_FIFOINTR(2)
#define PCI224_DACCON_FIFOINTR_HALF   PCI224_DACCON_FIFOINTR(3)
#define PCI224_DACCON_FIFOINTR_NFULL  PCI224_DACCON_FIFOINTR(4)
#define PCI224_DACCON_FIFOINTR_FULL   PCI224_DACCON_FIFOINTR(5)
#define PCI224_DACCON_FIFOFL(x)   (((x) & 0x7) << 12)
#define PCI224_DACCON_FIFOFL_MASK  PCI224_DACCON_FIFOFL(7)
#define PCI224_DACCON_FIFOFL_EMPTY       PCI224_DACCON_FIFOFL(1)
#define PCI224_DACCON_FIFOFL_ONETOHALF   PCI224_DACCON_FIFOFL(0)
#define PCI224_DACCON_FIFOFL_HALFTOFULL  PCI224_DACCON_FIFOFL(4)
#define PCI224_DACCON_FIFOFL_FULL        PCI224_DACCON_FIFOFL(6)
#define PCI224_DACCON_BUSY           BIT(15)
#define PCI224_DACCON_FIFORESET      BIT(12)
#define PCI224_DACCON_GLOBALRESET    BIT(13)

/* FIFO constants */
#define PCI224_FIFO_SIZE 4096

/* Clock sources */
#define CLK_CLK     0
#define CLK_10MHZ   1
#define CLK_1MHZ    2
#define CLK_100KHZ  3
#define CLK_10KHZ   4
#define CLK_1KHZ    5
#define CLK_OUTNM1  6
#define CLK_EXT     7

/* Gate sources */
#define GAT_VCC     0
#define GAT_GND     1
#define GAT_EXT     2
#define GAT_NOUTNM2 3

/* Interrupt bits */
#define PCI224_INTR_EXT    0x01
#define PCI224_INTR_DAC    0x04
#define PCI224_INTR_Z2CT1  0x20
#define PCI224_INTR_EDGE_BITS  (PCI224_INTR_EXT | PCI224_INTR_Z2CT1)

#define TYPE_PCIBASE_DEVICE "amplc_pci224_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar2_mem;
    MemoryRegion bar3_mem;

    uint8_t bar2_regs[0x20];   // BAR2 i/o region: Z2, clock, gate, interrupt
    uint8_t bar3_regs[0x20];   // BAR3 i/o region: DAC data, control, etc.

    /* Internal state */
    uint16_t daccon;           // shadow of DACCONTROL (without strobe bits)
    uint16_t daccen;           // channel enable bits
    uint8_t int_enable;        // enables as written to INT_SCE
    uint8_t int_pending;       // latched interrupt status
    uint32_t fifo_count;       // samples currently in FIFO (0..4096)
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->int_enable & s->int_pending) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

static void pcibase_daccon_reset(PCIBaseState *s, bool global)
{
    if (global) {
        memset(s->bar2_regs, 0, sizeof(s->bar2_regs));
        memset(s->bar3_regs, 0, sizeof(s->bar3_regs));
        s->daccon = 0;
        s->daccen = 0;
        s->int_enable = 0;
        s->int_pending = 0;
        s->fifo_count = 0;
        pcibase_update_irq(s);
    } else {
        /* FIFO reset only */
        s->fifo_count = 0;
    }
}

static uint64_t pci224_bar2_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= sizeof(s->bar2_regs)) {
        return ~0ULL;
    }
    switch (addr) {
    case PCI224_INT_SCE:
        /* Return latched pending status */
        val = s->int_pending;
        break;
    default:
        val = s->bar2_regs[addr];
        break;
    }
    return val;
}

static void pci224_bar2_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= sizeof(s->bar2_regs)) {
        return;
    }
    switch (addr) {
    case PCI224_INT_SCE:
        s->int_enable = val & 0x3F;
        /* Any bit written as 0 clears the pending status */
        s->int_pending &= s->int_enable;
        s->bar2_regs[addr] = s->int_enable;
        pcibase_update_irq(s);
        break;
    default:
        s->bar2_regs[addr] = val;
        break;
    }
}

static const MemoryRegionOps pci224_bar2_ops = {
    .read = pci224_bar2_read,
    .write = pci224_bar2_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static uint64_t pci224_bar3_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= sizeof(s->bar3_regs)) {
        return ~0ULL;
    }
    switch (addr) {
    case PCI224_SOFTTRIG:
        /* Read triggers conversion; for now just ignore */
        break;
    case PCI224_DACCON:
        val = s->daccon;
        /* Set FIFOFL based on fifo_count */
        if (s->fifo_count == 0) {
            val |= PCI224_DACCON_FIFOFL_EMPTY;
        } else if (s->fifo_count <= PCI224_FIFO_SIZE / 4) {
            val |= PCI224_DACCON_FIFOFL_ONETOHALF;
        } else if (s->fifo_count <= PCI224_FIFO_SIZE / 2) {
            val |= PCI224_DACCON_FIFOFL_HALFTOFULL;
        } else {
            val |= PCI224_DACCON_FIFOFL_FULL;
        }
        if (s->fifo_count == PCI224_FIFO_SIZE) {
            val |= PCI224_DACCON_BUSY;
        }
        break;
    case PCI224_DACCEN:
        val = s->daccen;
        break;
    default:
        val = s->bar3_regs[addr];
        break;
    }
    return val;
}

static void pci224_bar3_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= sizeof(s->bar3_regs)) {
        return;
    }
    switch (addr) {
    case PCI224_DACDATA:
        if (s->fifo_count < PCI224_FIFO_SIZE) {
            s->bar3_regs[addr] = val;
            s->fifo_count++;
            /* Check if interrupt condition met */
            uint8_t intr_cond = (s->daccon & PCI224_DACCON_FIFOINTR_MASK) >> 9;
            bool trigger = false;
            switch (intr_cond) {
            case 0: // EMPTY
                trigger = (s->fifo_count == 0);
                break;
            case 1: // NEMPTY
                trigger = (s->fifo_count != 0);
                break;
            case 2: // NHALF
                trigger = (s->fifo_count < PCI224_FIFO_SIZE / 2);
                break;
            case 3: // HALF
                trigger = (s->fifo_count == PCI224_FIFO_SIZE / 2);
                break;
            case 4: // NFULL
                trigger = (s->fifo_count < PCI224_FIFO_SIZE);
                break;
            case 5: // FULL
                trigger = (s->fifo_count == PCI224_FIFO_SIZE);
                break;
            }
            if (trigger) {
                s->int_pending |= PCI224_INTR_DAC;
                pcibase_update_irq(s);
            }
        }
        break;
    case PCI224_DACCON:
        if (val & PCI224_DACCON_GLOBALRESET) {
            pcibase_daccon_reset(s, true);
        }
        if (val & PCI224_DACCON_FIFORESET) {
            pcibase_daccon_reset(s, false);
        }
        /* Store only the non-strobe bits */
        s->daccon = val & ~(PCI224_DACCON_FIFORESET | PCI224_DACCON_GLOBALRESET);
        break;
    case PCI224_DACCEN:
        s->daccen = val;
        s->bar3_regs[addr] = val;
        break;
    default:
        s->bar3_regs[addr] = val;
        break;
    }
}

static const MemoryRegionOps pci224_bar3_ops = {
    .read = pci224_bar3_read,
    .write = pci224_bar3_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    memset(s->bar2_regs, 0, sizeof(s->bar2_regs));
    memset(s->bar3_regs, 0, sizeof(s->bar3_regs));
    s->daccon = 0;
    s->daccen = 0;
    s->int_enable = 0;
    s->int_pending = 0;
    s->fifo_count = 0;
    pcibase_update_irq(s);
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_AMPLICON);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI224_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI224_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    memory_region_init_io(&s->bar2_mem, OBJECT(s), &pci224_bar2_ops, s, "amplc_pci224-bar2", 0x20);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_IO, &s->bar2_mem);

    memory_region_init_io(&s->bar3_mem, OBJECT(s), &pci224_bar3_ops, s, "amplc_pci224-bar3", 0x20);
    pci_register_bar(pdev, 3, PCI_BASE_ADDRESS_SPACE_IO, &s->bar3_mem);
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
    .name = "amplc_pci224_pci",
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
    k->exit = pcibase_uninit;
    dc->reset = pcibase_reset;
    dc->vmsd = &vmstate_pcibase;
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
