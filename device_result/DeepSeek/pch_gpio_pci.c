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

#define TYPE_PCIBASE_DEVICE "pch_gpio_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
/* Vendor and Device IDs from first entry of pch_gpio_pcidev_id[] */
#define PCI_VENDOR_ID_INTEL 0x8086
#define PCI_DEVICE_ID_INTEL_EG20T_PCH 0x8803
/* Register offsets derived from struct pch_regs layout */
#define PCH_IEN       0x00
#define PCH_ISTATUS   0x04
#define PCH_IDISP     0x08
#define PCH_ICLR      0x0C
#define PCH_IMASK     0x10
#define PCH_IMASKCLR  0x14
#define PCH_PO        0x18
#define PCH_PI        0x1C
#define PCH_PM        0x20
#define PCH_IM0       0x24
#define PCH_IM1       0x28
#define PCH_RESERVED0 0x2C
#define PCH_RESERVED1 0x30
#define PCH_RESERVED2 0x34
#define PCH_GPIO_USE_SEL 0x38
#define PCH_RESET     0x3C
/* BAR size and CLASS_ID are unknown; placeholders to be filled */
#define PCH_BAR1_SIZE 0x1000
#define PCH_DEVICE_CLASS PCI_CLASS_OTHERS

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

    /* Hardware Register Shadows (The 'Identity' of the device) */
    struct pch_regs_shadow {
        uint32_t ien;
        uint32_t istatus;
        uint32_t idisp;
        uint32_t iclr;
        uint32_t imask;
        uint32_t imaskclr;
        uint32_t po;
        uint32_t pi;
        uint32_t pm;
        uint32_t im0;
        uint32_t im1;
        uint32_t reserved[3];
        uint32_t gpio_use_sel;
        uint32_t reset;
    } regs;
};

/* Forward declarations */
static void pcibase_reset(DeviceState *dev);

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t pending = s->regs.istatus & s->regs.ien & ~s->regs.imask;
    int level = (pending != 0) ? 1 : 0;
    pci_set_irq(pdev, level);
}

/* MMIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad read size %d\n", __func__, size);
        return 0;
    }

    switch (addr) {
    case PCH_IEN:       val = s->regs.ien;        break;
    case PCH_ISTATUS:   val = s->regs.istatus;    break;
    case PCH_IDISP:     val = s->regs.idisp;      break;
    case PCH_ICLR:      val = 0; /* write-only, read as 0 */ break;
    case PCH_IMASK:     val = s->regs.imask;      break;
    case PCH_IMASKCLR:  val = 0; /* write-only */ break;
    case PCH_PO:        val = s->regs.po;         break;
    case PCH_PI:        val = s->regs.pi;         break;
    case PCH_PM:        val = s->regs.pm;         break;
    case PCH_IM0:       val = s->regs.im0;        break;
    case PCH_IM1:       val = s->regs.im1;        break;
    case PCH_RESERVED0: val = s->regs.reserved[0]; break;
    case PCH_RESERVED1: val = s->regs.reserved[1]; break;
    case PCH_RESERVED2: val = s->regs.reserved[2]; break;
    case PCH_GPIO_USE_SEL: val = s->regs.gpio_use_sel; break;
    case PCH_RESET:     val = s->regs.reset;      break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad read addr 0x%"HWADDR_PRIx"\n", __func__, addr);
        val = 0;
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad write size %d\n", __func__, size);
        return;
    }

    switch (addr) {
    case PCH_IEN:
        s->regs.ien = val;
        pcibase_update_irq(s);
        break;
    case PCH_ISTATUS:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: istatus is read-only\n", __func__);
        break;
    case PCH_IDISP:
        s->regs.idisp = val;
        break;
    case PCH_ICLR:
        /* Write-1-to-clear: bits set in 'val' clear corresponding bits in istatus */
        s->regs.istatus &= ~val;
        pcibase_update_irq(s);
        break;
    case PCH_IMASK:
        s->regs.imask |= val;
        pcibase_update_irq(s);
        break;
    case PCH_IMASKCLR:
        s->regs.imask &= ~val;
        pcibase_update_irq(s);
        break;
    case PCH_PO:
        s->regs.po = val;
        break;
    case PCH_PI:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: pi is read-only\n", __func__);
        break;
    case PCH_PM:
        s->regs.pm = val;
        break;
    case PCH_IM0:
        s->regs.im0 = val;
        break;
    case PCH_IM1:
        s->regs.im1 = val;
        break;
    case PCH_RESERVED0:
    case PCH_RESERVED1:
    case PCH_RESERVED2:
        /* ignored */
        break;
    case PCH_GPIO_USE_SEL:
        s->regs.gpio_use_sel = val;
        break;
    case PCH_RESET:
        if (val & 1) {
            s->regs.reset = 1;
        } else if (s->regs.reset == 1) {
            /* when 0 is written after 1, perform reset */
            pcibase_reset(DEVICE(s));
        }
        /* else ignore */
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad write addr 0x%"HWADDR_PRIx" val=0x%"PRIx64"\n",
                      __func__, addr, val);
        break;
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Reset all hardware registers to default (all zeros) */
    memset(&s->regs, 0, sizeof(s->regs));

    /* Update IRQ after reset */
    pcibase_update_irq(s);
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
        /* Not used */
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_INTEL );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_INTEL_EG20T_PCH );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCH_DEVICE_CLASS );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization: only BAR1 is used by the driver as MMIO */
    s->num_bars = 1;
    s->bar_info[0].index = 1;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = PCH_BAR1_SIZE;
    s->bar_info[0].name = "pch-gpio-bar1";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI/MSI-X not used: driver relies on legacy INTx */
    /* DMA not used */
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "pch_gpio_pci",
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
