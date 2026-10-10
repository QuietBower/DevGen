/*
 * QEMU PCI device model for i6300ESB timer (watchdog)
 * Phase 2: Functional behavior implementation based on Linux driver
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

#define TYPE_PCIBASE_DEVICE "i6300ESB_timer_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define ESB_CONFIG_REG  0x60
#define ESB_LOCK_REG    0x68
#define ESB_TIMER1_OFFSET 0x00
#define ESB_TIMER2_OFFSET 0x04
#define ESB_GINTSR_OFFSET 0x08
#define ESB_RELOAD_OFFSET 0x0c

#define ESB_WDT_FUNC    (0x01 << 2)
#define ESB_WDT_ENABLE  (0x01 << 1)
#define ESB_WDT_LOCK    (0x01 << 0)
#define ESB_WDT_REBOOT  (0x01 << 5)
#define ESB_WDT_FREQ    (0x01 << 2)
#define ESB_WDT_INTTYPE (0x03 << 0)
#define ESB_WDT_TIMEOUT (0x01 << 9)
#define ESB_WDT_RELOAD  (0x01 << 8)

#define ESB_BAR_INDEX   0
#define ESB_BAR_SIZE    0x10

/* Use first entry from esb_pci_tbl: PCI_VENDOR_ID_INTEL / PCI_DEVICE_ID_INTEL_ESB_9 */
#define ESB_PCI_VENDOR_ID PCI_VENDOR_ID_INTEL
#define ESB_PCI_DEVICE_ID PCI_DEVICE_ID_INTEL_ESB_9

#define ESB_PCI_CLASS_ID PCI_CLASS_OTHERS

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

    /* No interrupt-specific shadow registers defined/used in the driver */

    /* Hardware Register Shadows (The 'Identity' of the device) */
    struct {
        uint32_t timer1;
        uint32_t timer2;
        uint32_t gintsr;
        uint32_t reload;
    } regs;

    /* No DMA used by this watchdog driver */

    /* Minimal operational status flags */
    bool running;

    /* Emulated config space fields used by driver */
    uint16_t config_reg;   /* ESB_CONFIG_REG 0x60 */
    uint8_t  lock_reg;     /* ESB_LOCK_REG   0x68 */

    /* State for reload / timeout reporting */
    bool timeout_flag;     /* represents ESB_WDT_TIMEOUT bit in reload reg */
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    /* i6300esb watchdog driver does not use interrupts in this configuration */
    (void)s;
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /* No DMA paths are used by this watchdog driver */
    (void)s;
    (void)is_write;
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case ESB_TIMER1_OFFSET: /* 0x00 */
        if (size == 4) {
            val = s->regs.timer1;
        }
        break;
    case ESB_TIMER2_OFFSET: /* 0x04 */
        if (size == 4) {
            val = s->regs.timer2;
        }
        break;
    case ESB_GINTSR_OFFSET: /* 0x08 */
        if (size == 4) {
            val = s->regs.gintsr;
        }
        break;
    case ESB_RELOAD_OFFSET: /* 0x0c */
        if (size == 2) {
            /* The driver reads ESB_RELOAD_REG as 16-bit to check ESB_WDT_TIMEOUT */
            uint16_t r = (uint16_t)(s->regs.reload & 0xffff);
            if (s->timeout_flag) {
                r |= ESB_WDT_TIMEOUT;
            } else {
                r &= ~ESB_WDT_TIMEOUT;
            }
            val = r;
        } else if (size == 4) {
            uint32_t r = (uint32_t)s->regs.reload;
            if (s->timeout_flag) {
                r |= ESB_WDT_TIMEOUT;
            } else {
                r &= ~ESB_WDT_TIMEOUT;
            }
            val = r;
        }
        break;
    default:
        /* unmapped / unused offset, return 0 */
        val = 0;
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case ESB_TIMER1_OFFSET: /* 0x00 */
        if (size == 4) {
            s->regs.timer1 = (uint32_t)val;
        }
        break;
    case ESB_TIMER2_OFFSET: /* 0x04 */
        if (size == 4) {
            s->regs.timer2 = (uint32_t)val;
        }
        break;
    case ESB_GINTSR_OFFSET: /* 0x08 */
        if (size == 4) {
            s->regs.gintsr = (uint32_t)val;
        }
        break;
    case ESB_RELOAD_OFFSET: /* 0x0c */
        /* Driver accesses as 16-bit via writew() with various patterns */
        if (size == 2 || size == 4) {
            uint32_t w = (size == 2) ? (uint32_t)(val & 0xffff) : (uint32_t)val;

            /* The Linux driver sequences:
             *   - Unlock via magic writes (handled entirely on host side; we do nothing here)
             *   - writew(ESB_WDT_RELOAD, ESB_RELOAD_REG) to reload / clear timeout
             *   - later, writew(ESB_WDT_TIMEOUT | ESB_WDT_RELOAD, ...) to clear timeout flag
             */

            /* Preserve raw shadow */
            s->regs.reload = w;

            /* Handle ESB_WDT_TIMEOUT flag semantics as seen by driver */
            if (w & ESB_WDT_RELOAD) {
                /* Any write with RELOAD bit set clears the timeout flag */
                s->timeout_flag = false;
            }
            /* If driver writes ESB_WDT_TIMEOUT | ESB_WDT_RELOAD, we
             * interpret it as "clear timeout flag" which is what it expects.
             * We do not simulate real timeout generation.
             */
        }
        break;
    default:
        /* ignore writes to unknown offsets */
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* The driver only uses MMIO via pci_ioremap_bar(); no PIO used. */
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    (void)s;
    (void)addr;
    (void)size;
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* The driver only uses MMIO via pci_ioremap_bar(); no PIO used. */
    PCIBaseState *s = opaque;
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

    /* Revert registers to power-on defaults (all zeros for now) */
    s->regs.timer1 = 0;
    s->regs.timer2 = 0;
    s->regs.gintsr = 0;
    s->regs.reload = 0;
    s->running = false;

    s->config_reg = 0;
    s->lock_reg = 0;
    s->timeout_flag = false;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  ESB_PCI_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  ESB_PCI_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, ESB_PCI_CLASS_ID );
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
    s->bar_info[0].index = ESB_BAR_INDEX;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = ESB_BAR_SIZE;
    s->bar_info[0].name = "i6300esb-mmio";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = "unused";
    }
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No MSI or MSI-X usage in driver; leave disabled */

    /* No DMA configuration required for this driver */

    /* Initial state */
    s->regs.timer1 = 0;
    s->regs.timer2 = 0;
    s->regs.gintsr = 0;
    s->regs.reload = 0;
    s->running = false;
    s->config_reg = 0;
    s->lock_reg = 0;
    s->timeout_flag = false;

    /* Mirror of config_reg and lock_reg is maintained by config read/write helpers below. */
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

/* Override PCI config read/write to track ESB_CONFIG_REG and ESB_LOCK_REG */
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t address, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    /* Use default implementation first */
    uint32_t val = pci_default_read_config(pdev, address, len);

    /* Reflect our internal shadows for specific registers the driver uses */
    if (address == ESB_CONFIG_REG && len == 2) {
        val = s->config_reg;
    } else if (address == ESB_LOCK_REG && len == 1) {
        val = s->lock_reg;
    }

    return val;
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t address, uint32_t val, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (address == ESB_CONFIG_REG && len == 2) {
        /* Driver writes 0x0003 during init; just store value */
        s->config_reg = (uint16_t)(val & 0xffff);
    } else if (address == ESB_LOCK_REG && len == 1) {
        /* Driver writes lock/enable bits and later reads them back */
        s->lock_reg = (uint8_t)(val & 0xff);

        /* Reflect running state based on ESB_WDT_ENABLE bit */
        if (s->lock_reg & ESB_WDT_ENABLE) {
            s->running = true;
        } else {
            s->running = false;
        }
    }

    /* Always write through to underlying config space so guest sees changes */
    pci_default_write_config(pdev, address, val, len);
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "i6300ESB_timer_pci",
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

    /* Hook custom config accessors so that config_reg / lock_reg mirrors work */
    k->config_read = pcibase_config_read;
    k->config_write = pcibase_config_write;

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
