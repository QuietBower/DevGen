/*
 * QEMU PCI device model for ke_counter
 * Phase 2: Functional behavior implementation
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

#define TYPE_PCIBASE_DEVICE "ke_counter_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define KE_RESET_REG(x)            (0x00 + ((x) * 0x20))
#define KE_LATCH_REG(x)            (0x00 + ((x) * 0x20))
#define KE_LSB_REG(x)              (0x04 + ((x) * 0x20))
#define KE_MID_REG(x)              (0x08 + ((x) * 0x20))
#define KE_MSB_REG(x)              (0x0c + ((x) * 0x20))
#define KE_SIGN_REG(x)             (0x10 + ((x) * 0x20))
#define KE_OSC_SEL_REG             0xf8
#define KE_OSC_SEL_CLK(x)          (((x) & 0x3) << 0)
#define KE_OSC_SEL_EXT             KE_OSC_SEL_CLK(1)
#define KE_OSC_SEL_4MHZ            KE_OSC_SEL_CLK(2)
#define KE_OSC_SEL_20MHZ           KE_OSC_SEL_CLK(3)
#define KE_DO_REG                  0xfc

#define KE_COUNTER_VENDOR_ID       0x1001
#define KE_COUNTER_DEVICE_ID       0x0014
#define KE_COUNTER_CLASS_ID        0xff

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
    /* Counter channels 0-2, each 32-bit signed value */
    uint32_t counter[3];
    /* Latched values for read sequence */
    uint32_t latched_counter[3];
    /* Oscillator selection register */
    uint8_t osc_sel;
    /* Digital output register (3 bits used) */
    uint8_t do_reg;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    /* ke_counter driver does not use interrupts; no logic required */
    (void)s;
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /* No DMA usage in ke_counter driver */
    (void)s;
    (void)is_write;
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0xff;

    /* Driver uses byte I/O (inb/outb). Accept 1-byte accesses only. */
    if (size != 1) {
        return 0xff;
    }

    /* Counter channels at offsets 0x00..0x5f (3 channels * 0x20) */
    if (addr < 0x60) {
        unsigned int chan = addr / 0x20;
        hwaddr off = addr % 0x20;

        if (chan >= 3) {
            return 0xff;
        }

        switch (off) {
        case 0x00: /* LATCH / RESET read */
            /* Latch current counter value for this channel */
            s->latched_counter[chan] = s->counter[chan];
            /* The driver ignores the return value of the latch read, so any
             * value is fine; use LSB of counter for determinism.
             */
            val = (uint8_t)(s->latched_counter[chan] & 0xff);
            break;
        case 0x04: /* LSB */
            val = (uint8_t)(s->latched_counter[chan] & 0xff);
            break;
        case 0x08: /* MID */
            val = (uint8_t)((s->latched_counter[chan] >> 8) & 0xff);
            break;
        case 0x0c: /* MSB */
            val = (uint8_t)((s->latched_counter[chan] >> 16) & 0xff);
            break;
        case 0x10: /* SIGN */
            val = (uint8_t)((s->latched_counter[chan] >> 24) & 0xff);
            break;
        default:
            /* Unused offsets; return 0xff */
            val = 0xff;
            break;
        }
        return val;
    }

    /* Oscillator selection register */
    if (addr == KE_OSC_SEL_REG) {
        return s->osc_sel;
    }

    /* Digital output register */
    if (addr == KE_DO_REG) {
        return s->do_reg;
    }

    /* Default for unmapped addresses within BAR */
    return 0xff;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint8_t v8 = (uint8_t)val;

    /* Driver uses byte I/O (outb). Ignore non-1-byte accesses. */
    if (size != 1) {
        return;
    }

    /* Counter channels regions 0x00..0x5f */
    if (addr < 0x60) {
        unsigned int chan = addr / 0x20;
        hwaddr off = addr % 0x20;
        uint32_t new_val;

        if (chan >= 3) {
            return;
        }

        switch (off) {
        case 0x00: /* RESET */
            /* Driver writes 0 to reset. Any write should zero the counter. */
            s->counter[chan] = 0;
            s->latched_counter[chan] = 0;
            break;
        case 0x04: /* LSB */
        case 0x08: /* MID */
        case 0x0c: /* MSB */
        case 0x10: /* SIGN */
            /* ke_counter_insn_write writes SIGN, MSB, MID, LSB in that order
             * for a 32-bit value. Implement writes as a shadow 32-bit value
             * reconstructed from byte writes. We don't know the existing
             * hardware semantics, but a straightforward mapping is enough for
             * round-trip insn_write/insn_read to work.
             */
            new_val = s->counter[chan];
            switch (off) {
            case 0x04: /* LSB */
                new_val &= ~0x000000ffU;
                new_val |= (uint32_t)v8;
                break;
            case 0x08: /* MID */
                new_val &= ~0x0000ff00U;
                new_val |= ((uint32_t)v8 << 8);
                break;
            case 0x0c: /* MSB */
                new_val &= ~0x00ff0000U;
                new_val |= ((uint32_t)v8 << 16);
                break;
            case 0x10: /* SIGN */
                new_val &= ~0xff000000U;
                new_val |= ((uint32_t)v8 << 24);
                break;
            default:
                break;
            }
            s->counter[chan] = new_val;
            break;
        default:
            /* Unused offsets ignored */
            break;
        }
        return;
    }

    /* Oscillator selection register */
    if (addr == KE_OSC_SEL_REG) {
        /* Driver writes one of KE_OSC_SEL_* values and reads back the same
         * when querying config. We simply store the written value.
         */
        s->osc_sel = v8 & 0x03; /* Only lower 2 bits defined by KE_OSC_SEL_CLK */
        return;
    }

    /* Digital output register */
    if (addr == KE_DO_REG) {
        /* Driver uses 3 digital output channels; store full byte for
         * consistency but actual hardware likely only uses low bits.
         */
        s->do_reg = v8;
        return;
    }

    /* Writes to other addresses within BAR are ignored */
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* ke_counter driver does not use port I/O; keep stub returning 0xff */
    (void)opaque;
    (void)addr;
    (void)size;
    return 0xff;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* ke_counter driver does not use port I/O; ignore */
    (void)opaque;
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
    int i;

    pci_device_reset(PCI_DEVICE(dev));

    /* Initialize counters and related state as on device reset */
    for (i = 0; i < 3; i++) {
        s->counter[i] = 0;
        s->latched_counter[i] = 0;
    }
    /* According to ke_counter_auto_attach(), driver sets KE_OSC_SEL_20MHZ
     * after attach and then calls ke_counter_reset(), but hardware should
     * have a sensible default. Use 20MHz as a convenient default.
     */
    s->osc_sel = KE_OSC_SEL_20MHZ;
    s->do_reg = 0;
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
    int i;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  KE_COUNTER_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  KE_COUNTER_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, KE_COUNTER_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    s->bar_info[0].size  = 0x100; /* Covers 0x00-0xFC used by driver macros */
    s->bar_info[0].name  = "ke_counter-mmio";
    for (i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type  = BAR_TYPE_NONE;
        s->bar_info[i].size  = 0;
        s->bar_info[i].name  = NULL;
    }

    for (i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = false;
    s->has_msix = false;

    /* Initialize internal state to reset values */
    for (i = 0; i < 3; i++) {
        s->counter[i] = 0;
        s->latched_counter[i] = 0;
    }
    s->osc_sel = KE_OSC_SEL_20MHZ;
    s->do_reg = 0;
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

    /* No dynamic resources allocated beyond BARs */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "ke_counter_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32_ARRAY(counter, PCIBaseState, 3),
        VMSTATE_UINT32_ARRAY(latched_counter, PCIBaseState, 3),
        VMSTATE_UINT8(osc_sel, PCIBaseState),
        VMSTATE_UINT8(do_reg, PCIBaseState),
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
