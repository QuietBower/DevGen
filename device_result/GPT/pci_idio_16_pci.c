/*
 * QEMU PCI device model for gpio-pci-idio-16
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

#define TYPE_PCIBASE_DEVICE "pci_idio_16_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define IDIO_16_VENDOR_ID 0x494F
#define IDIO_16_DEVICE_ID 0x0DC8
#define IDIO_16_PCI_CLASS_ID PCI_CLASS_OTHERS

/* Core register layout derived from regmap_config */
#define IDIO_16_REG_BITS        8
#define IDIO_16_VAL_BITS        8
#define IDIO_16_MAX_REGISTER    0x7

/* BAR/register addressing characteristics used by driver regmap */
#define IDIO_16_REG_STRIDE      4
#define IDIO_16_NGPIO           32
#define IDIO_16_NGPIO_PER_REG   8
#define IDIO_16_DAT_BASE        0x0

/* Interrupt-related register offsets and control bits */
#define IDIO_16_INTERRUPT_STATUS        0x6
#define IDIO_16_ENABLE_IRQ              0x2
#define IDIO_16_CLEAR_INTERRUPT         0x1
#define IDIO_16_DISABLE_IRQ             IDIO_16_ENABLE_IRQ
#define IDIO_16_DEACTIVATE_INPUT_FILTERS 0x3

/* Access tables are handled in Linux via regmap; here we only reflect ranges already extracted */
#define IDIO_16_WR_RANGE0_START 0x0
#define IDIO_16_WR_RANGE0_END   0x2
#define IDIO_16_WR_RANGE1_START 0x3
#define IDIO_16_WR_RANGE1_END   0x4

#define IDIO_16_RD_RANGE0_START 0x1
#define IDIO_16_RD_RANGE0_END   0x2
#define IDIO_16_RD_RANGE1_START 0x5
#define IDIO_16_RD_RANGE1_END   0x6

#define IDIO_16_PRECIOUS_RANGE_START 0x2
#define IDIO_16_PRECIOUS_RANGE_END   0x2

#define IDIO_16_NUM_IRQS        16
#define IDIO_16_IRQ_MASK_BIT    2


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

    /* Simple interrupt state */
    uint8_t irq_enable;      /* mirror of enable/disable bit */
    uint8_t irq_status;      /* status register */

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint8_t regs[IDIO_16_MAX_REGISTER + 1];

    /* No device-initiated DMA is described in the provided driver */
};


/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    if (s->irq_enable & IDIO_16_ENABLE_IRQ) {
        if (s->irq_status) {
            if (msi_enabled(pdev)) {
                msi_notify(pdev, 0);
            } else {
                pci_set_irq(pdev, 1);
            }
        } else {
            if (!msi_enabled(pdev)) {
                pci_set_irq(pdev, 0);
            }
        }
    } else {
        if (!msi_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

/* No device-initiated DMA logic in the provided driver; keep empty helper */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    (void)s;
    (void)is_write;
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* The Linux driver uses regmap with 8-bit registers and 8-bit values.
     * We mirror that by treating the low 8 bits of the address (divided
     * by IDIO_16_REG_STRIDE) as the register index.
     */
    unsigned int reg_index = (addr / IDIO_16_REG_STRIDE) & 0xFF;

    if (reg_index > IDIO_16_MAX_REGISTER) {
        return 0;
    }

    switch (reg_index) {
    case IDIO_16_INTERRUPT_STATUS:
        /* Return the IRQ status shadow register */
        val = s->irq_status;
        break;
    default:
        val = s->regs[reg_index];
        break;
    }

    /* Only 8-bit values are defined by the chip; replicate for larger sizes */
    if (size == 1) {
        return val & 0xFF;
    } else if (size == 2) {
        return (val & 0xFF) | ((val & 0xFF) << 8);
    } else if (size == 4) {
        return (val & 0xFF) * 0x01010101u;
    } else if (size == 8) {
        uint64_t b = val & 0xFF;
        return (b << 56) | (b << 48) | (b << 40) | (b << 32) |
               (b << 24) | (b << 16) | (b << 8) | b;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    unsigned int reg_index = (addr / IDIO_16_REG_STRIDE) & 0xFF;

    if (reg_index > IDIO_16_MAX_REGISTER) {
        return;
    }

    /* Only least significant byte is meaningful according to VAL_BITS */
    uint8_t v8 = (uint8_t)(val & 0xFF);

    switch (reg_index) {
    case IDIO_16_INTERRUPT_STATUS:
        /* Clear bits written as 1 (Write-1-to-Clear) */
        s->irq_status &= ~v8;
        break;
    default:
        s->regs[reg_index] = v8;
        break;
    }

    /* IRQ enable/disable semantics on the designated control register bit */
    if (reg_index == IDIO_16_ENABLE_IRQ) {
        if (v8 & IDIO_16_ENABLE_IRQ) {
            s->irq_enable |= IDIO_16_ENABLE_IRQ;
        } else {
            s->irq_enable &= ~IDIO_16_ENABLE_IRQ;
        }
    }

    /* A write of the CLEAR_INTERRUPT bit can be interpreted as an interrupt
     * acknowledge/clear; we already applied W1C above if it maps to the
     * status register. If CLEAR_INTERRUPT is a different register, mirror it
     * into regs[] only.
     */
    pcibase_update_irq(s);
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* For this model we treat PIO identically to MMIO if ever used. */
    val = pcibase_mmio_read(s, addr, size);
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* For this model we treat PIO identically to MMIO if ever used. */
    pcibase_mmio_write(s, addr, val, size);
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

    /* Revert registers to power-on defaults: all zeros */
    for (i = 0; i <= IDIO_16_MAX_REGISTER; i++) {
        s->regs[i] = 0;
    }
    s->irq_status = 0;
    s->irq_enable = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  IDIO_16_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  IDIO_16_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, IDIO_16_PCI_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization: the Linux driver maps BAR 2, so we create it. */
    s->num_bars = 1;
    s->bar_info[0].index = 2;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    /* Enough space for 8 registers spaced by REG_STRIDE */
    s->bar_info[0].size  = (IDIO_16_MAX_REGISTER + 1) * IDIO_16_REG_STRIDE;
    s->bar_info[0].name  = "pci-idio-16-bar2";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Optional MSI support; the real board is legacy IRQ, but MSI is harmless */
    Error *local_err = NULL;
    if (msi_init(pdev, 0, 1, true, false, &local_err) == 0) {
        s->has_msi = true;
    } else {
        s->has_msi = false;
        if (local_err) {
            error_free(local_err);
        }
    }

    s->has_msix = false;

    /* Initialize internal register state */
    for (int i = 0; i <= IDIO_16_MAX_REGISTER; i++) {
        s->regs[i] = 0;
    }
    s->irq_status = 0;
    s->irq_enable = 0;
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

    /* No dynamic allocations to free in this simple model */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "pci_idio_16_pci",
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
