/*
 * QEMU PCI device model for NI 6527
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

#define TYPE_PCIBASE_DEVICE "ni_6527_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register offsets and bit definitions from linux-6.18/drivers/comedi/drivers/ni_6527.c */
#define NI6527_DI_REG(x)              (0x00 + (x))
#define NI6527_DO_REG(x)              (0x03 + (x))
#define NI6527_ID_REG                 0x06
#define NI6527_CLR_REG                0x07
#define NI6527_CLR_EDGE               (1U << 3)
#define NI6527_CLR_OVERFLOW           (1U << 2)
#define NI6527_CLR_FILT               (1U << 1)
#define NI6527_CLR_INTERVAL           (1U << 0)
#define NI6527_CLR_IRQS               (NI6527_CLR_EDGE | NI6527_CLR_OVERFLOW)
#define NI6527_CLR_RESET_FILT         (NI6527_CLR_FILT | NI6527_CLR_INTERVAL)
#define NI6527_FILT_INTERVAL_REG(x)   (0x08 + (x))
#define NI6527_FILT_ENA_REG(x)        (0x0c + (x))
#define NI6527_STATUS_REG             0x14
#define NI6527_STATUS_IRQ             (1U << 2)
#define NI6527_STATUS_OVERFLOW        (1U << 1)
#define NI6527_STATUS_EDGE            (1U << 0)
#define NI6527_CTRL_REG               0x15
#define NI6527_CTRL_FALLING           (1U << 4)
#define NI6527_CTRL_RISING            (1U << 3)
#define NI6527_CTRL_IRQ               (1U << 2)
#define NI6527_CTRL_OVERFLOW          (1U << 1)
#define NI6527_CTRL_EDGE              (1U << 0)
#define NI6527_CTRL_DISABLE_IRQS      0
#define NI6527_CTRL_ENABLE_IRQS       (NI6527_CTRL_FALLING | \
                                       NI6527_CTRL_RISING | \
                                       NI6527_CTRL_IRQ | NI6527_CTRL_EDGE)
#define NI6527_RISING_EDGE_REG(x)     (0x18 + (x))
#define NI6527_FALLING_EDGE_REG(x)    (0x20 + (x))

/* PCI IDs: from first entry of ni6527_pci_table: { PCI_VDEVICE(NI, 0x2b10), ... } */
#define NI6527_VENDOR_ID_FIRST  0x1093
#define NI6527_DEVICE_ID_FIRST  0x2b10

#define NI6527_PCI_CLASS_ID      PCI_CLASS_OTHERS

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
    uint8_t di_regs[3];            /* NI6527_DI_REG(x) */
    uint8_t do_regs[3];            /* NI6527_DO_REG(x) */
    uint8_t id_reg;                /* NI6527_ID_REG */
    uint8_t clr_reg;               /* NI6527_CLR_REG (last written value) */
    uint8_t filt_interval_regs[4]; /* NI6527_FILT_INTERVAL_REG(x) */
    uint8_t filt_ena_regs[4];      /* NI6527_FILT_ENA_REG(x) */
    uint8_t status_reg;            /* NI6527_STATUS_REG */
    uint8_t ctrl_reg;              /* NI6527_CTRL_REG */
    uint8_t rising_edge_regs[4];   /* NI6527_RISING_EDGE_REG(x) */
    uint8_t falling_edge_regs[4];  /* NI6527_FALLING_EDGE_REG(x) */
};

/* Internal helper for status-triggered signaling. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool irq_active = (s->status_reg & NI6527_STATUS_IRQ) &&
                      (s->ctrl_reg & NI6527_CTRL_IRQ);

    if (irq_active) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /* NI 6527 driver shows no explicit DMA usage; nothing to implement. */
    (void)s;
    (void)is_write;
}

/* MMIO/PIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0xff;

    /* Only 8-bit accesses are used by the driver (readb/writeb). */
    if (size != 1) {
        return 0xff;
    }

    switch (addr) {
    case NI6527_DI_REG(0):
        val = s->di_regs[0];
        break;
    case NI6527_DI_REG(1):
        val = s->di_regs[1];
        break;
    case NI6527_DI_REG(2):
        val = s->di_regs[2];
        break;

    case NI6527_DO_REG(0):
        val = s->do_regs[0];
        break;
    case NI6527_DO_REG(1):
        val = s->do_regs[1];
        break;
    case NI6527_DO_REG(2):
        val = s->do_regs[2];
        break;

    case NI6527_ID_REG:
        /* The driver checks this against 0x27. */
        val = s->id_reg;
        break;

    case NI6527_CLR_REG:
        /* Return last written clear value (not used by driver). */
        val = s->clr_reg;
        break;

    case NI6527_FILT_INTERVAL_REG(0):
        val = s->filt_interval_regs[0];
        break;
    case NI6527_FILT_INTERVAL_REG(1):
        val = s->filt_interval_regs[1];
        break;
    case NI6527_FILT_INTERVAL_REG(2):
        val = s->filt_interval_regs[2];
        break;
    case NI6527_FILT_INTERVAL_REG(3):
        val = s->filt_interval_regs[3];
        break;

    case NI6527_FILT_ENA_REG(0):
        val = s->filt_ena_regs[0];
        break;
    case NI6527_FILT_ENA_REG(1):
        val = s->filt_ena_regs[1];
        break;
    case NI6527_FILT_ENA_REG(2):
        val = s->filt_ena_regs[2];
        break;
    case NI6527_FILT_ENA_REG(3):
        val = s->filt_ena_regs[3];
        break;

    case NI6527_STATUS_REG:
        val = s->status_reg;
        break;

    case NI6527_CTRL_REG:
        val = s->ctrl_reg;
        break;

    case NI6527_RISING_EDGE_REG(0):
        val = s->rising_edge_regs[0];
        break;
    case NI6527_RISING_EDGE_REG(1):
        val = s->rising_edge_regs[1];
        break;
    case NI6527_RISING_EDGE_REG(2):
        val = s->rising_edge_regs[2];
        break;
    case NI6527_RISING_EDGE_REG(3):
        val = s->rising_edge_regs[3];
        break;

    case NI6527_FALLING_EDGE_REG(0):
        val = s->falling_edge_regs[0];
        break;
    case NI6527_FALLING_EDGE_REG(1):
        val = s->falling_edge_regs[1];
        break;
    case NI6527_FALLING_EDGE_REG(2):
        val = s->falling_edge_regs[2];
        break;
    case NI6527_FALLING_EDGE_REG(3):
        val = s->falling_edge_regs[3];
        break;

    default:
        /* Unknown offset: return 0xFF like typical unimplemented 8-bit reg. */
        val = 0xff;
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint8_t v8;

    if (size != 1) {
        return;
    }
    v8 = (uint8_t)(val & 0xff);

    switch (addr) {
    case NI6527_DI_REG(0):
    case NI6527_DI_REG(1):
    case NI6527_DI_REG(2):
        /* DI registers are read-only from the driver's perspective. */
        break;

    case NI6527_DO_REG(0):
        s->do_regs[0] = v8;
        break;
    case NI6527_DO_REG(1):
        s->do_regs[1] = v8;
        break;
    case NI6527_DO_REG(2):
        s->do_regs[2] = v8;
        break;

    case NI6527_ID_REG:
        /* Treat ID as read-only; ignore writes. */
        break;

    case NI6527_CLR_REG:
        /* Clear bits in status according to clear mask. */
        s->clr_reg = v8;
        if (v8 & NI6527_CLR_EDGE) {
            s->status_reg &= ~NI6527_STATUS_EDGE;
        }
        if (v8 & NI6527_CLR_OVERFLOW) {
            s->status_reg &= ~NI6527_STATUS_OVERFLOW;
        }
        if (v8 & NI6527_CLR_IRQS) {
            s->status_reg &= ~NI6527_STATUS_IRQ;
        }
        if (v8 & NI6527_CLR_FILT) {
            /* no dedicated status bit; nothing additional */
        }
        if (v8 & NI6527_CLR_INTERVAL) {
            /* likewise; ignore for now */
        }
        pcibase_update_irq(s);
        break;

    case NI6527_FILT_INTERVAL_REG(0):
        s->filt_interval_regs[0] = v8;
        break;
    case NI6527_FILT_INTERVAL_REG(1):
        s->filt_interval_regs[1] = v8;
        break;
    case NI6527_FILT_INTERVAL_REG(2):
        s->filt_interval_regs[2] = v8;
        break;
    case NI6527_FILT_INTERVAL_REG(3):
        s->filt_interval_regs[3] = v8;
        break;

    case NI6527_FILT_ENA_REG(0):
        s->filt_ena_regs[0] = v8;
        break;
    case NI6527_FILT_ENA_REG(1):
        s->filt_ena_regs[1] = v8;
        break;
    case NI6527_FILT_ENA_REG(2):
        s->filt_ena_regs[2] = v8;
        break;
    case NI6527_FILT_ENA_REG(3):
        s->filt_ena_regs[3] = v8;
        break;

    case NI6527_STATUS_REG:
        /* Status is read-only from driver; ignore writes. */
        break;

    case NI6527_CTRL_REG:
        /* Store control register; it gates IRQs. */
        s->ctrl_reg = v8;
        pcibase_update_irq(s);
        break;

    case NI6527_RISING_EDGE_REG(0):
        s->rising_edge_regs[0] = v8;
        break;
    case NI6527_RISING_EDGE_REG(1):
        s->rising_edge_regs[1] = v8;
        break;
    case NI6527_RISING_EDGE_REG(2):
        s->rising_edge_regs[2] = v8;
        break;
    case NI6527_RISING_EDGE_REG(3):
        s->rising_edge_regs[3] = v8;
        break;

    case NI6527_FALLING_EDGE_REG(0):
        s->falling_edge_regs[0] = v8;
        break;
    case NI6527_FALLING_EDGE_REG(1):
        s->falling_edge_regs[1] = v8;
        break;
    case NI6527_FALLING_EDGE_REG(2):
        s->falling_edge_regs[2] = v8;
        break;
    case NI6527_FALLING_EDGE_REG(3):
        s->falling_edge_regs[3] = v8;
        break;

    default:
        /* Ignore unknown writes. */
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* Device BAR used by driver is MMIO (pci_ioremap_bar); no PIO used. */
    (void)opaque;
    (void)addr;
    (void)size;
    return 0xff;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
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
    pci_device_reset(PCI_DEVICE(dev));

    memset(s->di_regs, 0, sizeof(s->di_regs));
    memset(s->do_regs, 0, sizeof(s->do_regs));
    /* Hardware ID expected by driver: 0x27 */
    s->id_reg = 0x27;
    s->clr_reg = 0;
    memset(s->filt_interval_regs, 0, sizeof(s->filt_interval_regs));
    memset(s->filt_ena_regs, 0, sizeof(s->filt_ena_regs));
    s->status_reg = 0;
    s->ctrl_reg = 0;
    memset(s->rising_edge_regs, 0, sizeof(s->rising_edge_regs));
    memset(s->falling_edge_regs, 0, sizeof(s->falling_edge_regs));

    pcibase_update_irq(s);
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  NI6527_VENDOR_ID_FIRST);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  NI6527_DEVICE_ID_FIRST);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, NI6527_PCI_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR used by driver is BAR 1; map a single MMIO BAR large enough
     * to cover all known registers (up to 0x23).
     */
    s->num_bars = 2;

    /* BAR 0: disabled */
    s->bar_info[0].index = 0;
    s->bar_info[0].type  = BAR_TYPE_NONE;
    s->bar_info[0].size  = 0;
    s->bar_info[0].name  = NULL;

    /* BAR 1: MMIO region */
    s->bar_info[1].index = 1;
    s->bar_info[1].type  = BAR_TYPE_MMIO;
    s->bar_info[1].size  = 0x40; /* covers all referenced registers */
    s->bar_info[1].name  = "ni_6527-mmio";

    for (int i = 2; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type  = BAR_TYPE_NONE;
        s->bar_info[i].size  = 0;
        s->bar_info[i].name  = NULL;
    }

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = false;
    s->has_msix = false;

    memset(s->di_regs, 0, sizeof(s->di_regs));
    memset(s->do_regs, 0, sizeof(s->do_regs));
    s->id_reg = 0x27;
    s->clr_reg = 0;
    memset(s->filt_interval_regs, 0, sizeof(s->filt_interval_regs));
    memset(s->filt_ena_regs, 0, sizeof(s->filt_ena_regs));
    s->status_reg = 0;
    s->ctrl_reg = 0;
    memset(s->rising_edge_regs, 0, sizeof(s->rising_edge_regs));
    memset(s->falling_edge_regs, 0, sizeof(s->falling_edge_regs));

    pcibase_update_irq(s);
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

static const VMStateDescription vmstate_pcibase = {
    .name = "ni_6527_pci",
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

