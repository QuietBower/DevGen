/*
 * This template provides a robust skeleton for hardware emulation.
 * Integrated QEMU PCI device template (QEMU 8.2.10).
 * Replace #PLACEHOLDER# blocks with driver-specific definitions.
 * Designed for register-level modeling and PCIe driver probing.
 * NOTE: Do not directly fill this file with the Linux kernel source code.
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


#define TYPE_PCIBASE_DEVICE "ke_counter_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_KOLTER 0x1001
#define PCI_DEVICE_ID_KE_COUNTER 0x0014
#define PCI_CLASS_ID_KE_COUNTER PCI_CLASS_OTHERS  /* TODO: verify class */

#define KE_RESET_REG(x)         (0x00 + ((x) * 0x20))
#define KE_LATCH_REG(x)         (0x00 + ((x) * 0x20))
#define KE_LSB_REG(x)           (0x04 + ((x) * 0x20))
#define KE_MID_REG(x)           (0x08 + ((x) * 0x20))
#define KE_MSB_REG(x)           (0x0c + ((x) * 0x20))
#define KE_SIGN_REG(x)          (0x10 + ((x) * 0x20))
#define KE_OSC_SEL_REG          0xf8
#define KE_OSC_SEL_CLK(x)       (((x) & 0x3) << 0)
#define KE_OSC_SEL_EXT          KE_OSC_SEL_CLK(1)
#define KE_OSC_SEL_4MHZ         KE_OSC_SEL_CLK(2)
#define KE_OSC_SEL_20MHZ        KE_OSC_SEL_CLK(3)
#define KE_DO_REG               0xfc

#define KE_COUNTER_STRIDE 0x20
#define KE_NUM_COUNTERS 3


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
    uint8_t regs[0x100]; /* MMIO register space for BAR0 */

    /* Counter values */
    uint32_t counter[3];

    /* DMA Context --- removed, not used */
    /* Status flags --- removed */
    /* Reset sequence state --- removed */
    /* Power management state --- removed */
};


/* Internal helper for status-triggered signaling. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* No interrupt logic in driver, placeholder removed */
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* DMA not referenced in driver, placeholder removed */
}

/* Perform latch operation: copy counter value to byte registers */
static void ke_counter_latch(PCIBaseState *s, int chan)
{
    uint32_t val = s->counter[chan];
    s->regs[KE_LSB_REG(chan)] = val & 0xFF;
    s->regs[KE_MID_REG(chan)] = (val >> 8) & 0xFF;
    s->regs[KE_MSB_REG(chan)] = (val >> 16) & 0xFF;
    s->regs[KE_SIGN_REG(chan)] = (val >> 24) & 0xFF;
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Not used, device uses PIO */
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* Not used, device uses PIO */
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size != 1) {
        return 0;
    }

    if (addr >= 0x00 && addr < 0x60) {
        int chan = addr / KE_COUNTER_STRIDE;
        int offset = addr % KE_COUNTER_STRIDE;

        switch (offset) {
        case 0x00: /* LATCH_REG (read triggers latch) */
            ke_counter_latch(s, chan);
            /* Return value not used by driver */
            val = 0;
            break;
        case 0x04: /* LSB_REG */
            val = s->regs[addr];
            break;
        case 0x08: /* MID_REG */
            val = s->regs[addr];
            break;
        case 0x0c: /* MSB_REG */
            val = s->regs[addr];
            break;
        case 0x10: /* SIGN_REG */
            val = s->regs[addr];
            break;
        default:
            val = s->regs[addr];
            break;
        }
    } else if (addr == KE_OSC_SEL_REG) {
        val = s->regs[addr];
    } else if (addr == KE_DO_REG) {
        val = s->regs[addr];
    } else {
        val = 0;
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 1) {
        return;
    }

    uint8_t byte_val = val & 0xFF;

    if (addr >= 0x00 && addr < 0x60) {
        int chan = addr / KE_COUNTER_STRIDE;
        int offset = addr % KE_COUNTER_STRIDE;

        switch (offset) {
        case 0x00: /* RESET_REG (write performs reset) */
            if (byte_val == 0) {
                s->counter[chan] = 0;
                s->regs[KE_LSB_REG(chan)] = 0;
                s->regs[KE_MID_REG(chan)] = 0;
                s->regs[KE_MSB_REG(chan)] = 0;
                s->regs[KE_SIGN_REG(chan)] = 0;
            }
            break;
        case 0x04: /* LSB_REG */
            s->regs[addr] = byte_val;
            s->counter[chan] = (s->counter[chan] & ~0xFF) | byte_val;
            break;
        case 0x08: /* MID_REG */
            s->regs[addr] = byte_val;
            s->counter[chan] = (s->counter[chan] & ~(0xFF << 8)) | (byte_val << 8);
            break;
        case 0x0c: /* MSB_REG */
            s->regs[addr] = byte_val;
            s->counter[chan] = (s->counter[chan] & ~(0xFF << 16)) | (byte_val << 16);
            break;
        case 0x10: /* SIGN_REG */
            s->regs[addr] = byte_val;
            s->counter[chan] = (s->counter[chan] & ~(0xFF << 24)) | (byte_val << 24);
            break;
        default:
            s->regs[addr] = byte_val;
            break;
        }
    } else if (addr == KE_OSC_SEL_REG) {
        s->regs[addr] = byte_val;
    } else if (addr == KE_DO_REG) {
        s->regs[addr] = byte_val;
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
    pci_device_reset(PCI_DEVICE(dev));

    memset(s->regs, 0, sizeof(s->regs));
    memset(s->counter, 0, sizeof(s->counter));
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_KOLTER);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_KE_COUNTER);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_ID_KE_COUNTER);
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
    s->bar_info[0] = (BARInfo) {
        .index = 0,
        .type = BAR_TYPE_PIO,
        .size = 0x100,
        .name = "ke_counter-pio"
    };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI/MSI-X not used, placeholder removed */
    /* DMA configuration not used, placeholder removed */
    /* Timer configuration not used, placeholder removed */
    /* Final state initialization */
    memset(s->regs, 0, sizeof(s->regs));
    memset(s->counter, 0, sizeof(s->counter));
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

    /* Free buffers, stop timers, etc. */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "ke_counter_pci",
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
