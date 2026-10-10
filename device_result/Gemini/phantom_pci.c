/*
 * QEMU Phantom Haptic Device Model
 *
 * Based on Linux driver: drivers/misc/phantom.c
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/pci/pci.h"
#include "hw/hw.h"
#include "hw/pci/msi.h"
#include "qemu/timer.h"
#include "qom/object.h"
#include "qemu/main-loop.h"
#include "qemu/log.h"
#include "qapi/error.h"

#define TYPE_PCIBASE_DEVICE "phantom_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Related register/macros/constants from driver */
#define PCI_VENDOR_ID_PLX 0x10b5
#define PCI_DEVICE_ID_PLX_9050 0x9050

#define PHN_IRQCTL      0x4c

/* Definitions from driver source */
#define PHN_CONTROL     0x6
#define PHN_CTL_IRQ     0x10
#define PHN_CTL_AMP     0x1

/* ------------------------------------------------------------------ */
/* BAR metadata definition                                             */
/* ------------------------------------------------------------------ */
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

/* ------------------------------------------------------------------ */
/* Device State                                                        */
/* ------------------------------------------------------------------ */
struct PCIBaseState {
    PCIDevice parent_obj;

    /* BAR memory regions */
    MemoryRegion bar_regions[6];

    /* BAR table */
    BARInfo bar_info[6];
    int num_bars;

    /* Register shadows */
    uint32_t irqctl; /* PHN_IRQCTL at 0x4c in BAR0 */
    
    /* Input/Output space registers (BAR2/BAR3) */
    uint32_t iregs[8]; /* BAR2 */
    uint32_t oregs[8]; /* BAR3 */

    QEMUTimer *irq_timer;
    bool irq_asserted;
};

/* ------------------------------------------------------------------ */
/* Forward declarations                                                */
/* ------------------------------------------------------------------ */
static void pcibase_reset(DeviceState *dev);
static void pcibase_realize(PCIDevice *pdev, Error **errp);
static void pcibase_uninit(PCIDevice *pdev);

/* ------------------------------------------------------------------ */
/* Timer Logic                                                         */
/* ------------------------------------------------------------------ */
static void phantom_update_irq(PCIBaseState *s)
{
    /* 
     * Driver logic:
     * 1. Writes 0x43 to PHN_IRQCTL (BAR0 + 0x4c) to enable global IRQs.
     * 2. Writes PHN_CTL_IRQ to PHN_CONTROL (BAR2 + Reg6) to enable device IRQs.
     */
    bool global_en = (s->irqctl & 0x40); /* Bit 6 of 0x43 is Local Int Enable on PLX 9050 */
    bool local_en = (s->iregs[PHN_CONTROL] & PHN_CTL_IRQ);

    if (global_en && local_en) {
        timer_mod(s->irq_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 10); /* 100Hz */
    } else {
        timer_del(s->irq_timer);
        pci_set_irq(&s->parent_obj, 0);
        s->irq_asserted = false;
    }
}

static void phantom_timer_cb(void *opaque)
{
    PCIBaseState *s = opaque;

    if (!s->irq_asserted) {
        pci_set_irq(&s->parent_obj, 1);
        s->irq_asserted = true;
    }
    
    /* Reschedule */
    phantom_update_irq(s);
}

/* ------------------------------------------------------------------ */
/* BAR 0: Config / PLX Registers                                       */
/* ------------------------------------------------------------------ */
static uint64_t phantom_bar0_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case PHN_IRQCTL:
        val = s->irqctl;
        break;
    default:
        break;
    }
    return val;
}

static void phantom_bar0_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case PHN_IRQCTL:
        s->irqctl = val;
        phantom_update_irq(s);
        break;
    default:
        break;
    }
}

static const MemoryRegionOps phantom_bar0_ops = {
    .read = phantom_bar0_read,
    .write = phantom_bar0_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

/* ------------------------------------------------------------------ */
/* BAR 2: Input Registers                                              */
/* ------------------------------------------------------------------ */
static uint64_t phantom_bar2_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    unsigned idx = addr / 4;

    if (idx < 8) {
        return s->iregs[idx];
    }
    return 0;
}

static void phantom_bar2_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    unsigned idx = addr / 4;

    if (idx < 8) {
        s->iregs[idx] = val;
        
        /* Check for IRQ Enable/Disable via PHN_CONTROL */
        if (idx == PHN_CONTROL) {
            phantom_update_irq(s);
        }
        
        /* 
         * ISR ACK Sequence:
         * Driver writes 0 then 0xc0 to offset 0 to ACK.
         */
        if (idx == 0) {
            if (val == 0 || val == 0xc0) {
                if (s->irq_asserted) {
                    pci_set_irq(&s->parent_obj, 0);
                    s->irq_asserted = false;
                }
            }
        }
    }
}

static const MemoryRegionOps phantom_bar2_ops = {
    .read = phantom_bar2_read,
    .write = phantom_bar2_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

/* ------------------------------------------------------------------ */
/* BAR 3: Output Registers                                             */
/* ------------------------------------------------------------------ */
static uint64_t phantom_bar3_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    unsigned idx = addr / 4;

    if (idx < 8) {
        return s->oregs[idx];
    }
    return 0;
}

static void phantom_bar3_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    unsigned idx = addr / 4;

    if (idx < 8) {
        s->oregs[idx] = val;
    }
}

static const MemoryRegionOps phantom_bar3_ops = {
    .read = phantom_bar3_read,
    .write = phantom_bar3_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

/* ------------------------------------------------------------------ */
/* Helper: register a BAR                                              */
/* ------------------------------------------------------------------ */
static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE)
        return;

    MemoryRegion *mr = &s->bar_regions[bi->index];
    const MemoryRegionOps *ops = NULL;

    switch (bi->index) {
    case 0: ops = &phantom_bar0_ops; break;
    case 2: ops = &phantom_bar2_ops; break;
    case 3: ops = &phantom_bar3_ops; break;
    default: return;
    }

    memory_region_init_io(mr, OBJECT(s), ops, s, bi->name, bi->size);
    pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
}

/* ------------------------------------------------------------------ */
/* Reset                                                               */
/* ------------------------------------------------------------------ */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    
    s->irqctl = 0;
    memset(s->iregs, 0, sizeof(s->iregs));
    memset(s->oregs, 0, sizeof(s->oregs));
    
    timer_del(s->irq_timer);
    s->irq_asserted = false;
    pci_set_irq(PCI_DEVICE(dev), 0);
}

/* ------------------------------------------------------------------ */
/* Realize (device init)                                                */
/* ------------------------------------------------------------------ */
static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_PLX);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_PLX_9050);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_VENDOR_ID, PCI_VENDOR_ID_PLX);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_ID,        PCI_DEVICE_ID_PLX_9050);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0680);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);
    
    s->irq_timer = timer_new_ms(QEMU_CLOCK_VIRTUAL, phantom_timer_cb, s);

    s->num_bars = 3;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = 0x1000, .name = "phantom-conf" };
    s->bar_info[1] = (BARInfo){ .index = 2, .type = BAR_TYPE_MMIO, .size = 0x1000, .name = "phantom-input" };
    s->bar_info[2] = (BARInfo){ .index = 3, .type = BAR_TYPE_MMIO, .size = 0x1000, .name = "phantom-output" };

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
}

/* ------------------------------------------------------------------ */
/* Uninit/cleanup                                                      */
/* ------------------------------------------------------------------ */
static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    timer_free(s->irq_timer);
}

/* ------------------------------------------------------------------ */
/* Class init / type registration                                      */
/* ------------------------------------------------------------------ */
static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

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