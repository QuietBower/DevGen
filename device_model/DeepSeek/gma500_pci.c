/*
 * QEMU gma500 PCI device model
 * Based on driver analysis of psb_drv.c
 * Implements PCI configuration for stolen memory mapping.
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

#define TYPE_PCIBASE_DEVICE "gma500_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register offsets extracted from driver macros */
#define PSB_BSM                 0x5C   /* BackStolen Memory offset in PCI config */
#define PSB_HWSTAM              0x2098
#define PSB_INT_ENABLE_R        0x20A0
#define PSB_INT_IDENTITY_R      0x20A4
#define PSB_INT_MASK_R          0x20A8
#define PIPEASTAT               0x70024
#define PIPEBSTAT               0x71024
#define PIPECSTAT               0x72024
#define CURACNTR                0x70080
#define CURABASE                0x70084
#define CURBCNTR                0x700c0
#define CURBBASE                0x700c4
#define CURCCNTR                0x700e0
#define CURCBASE                0x700e4

#define VENDOR_ID 0x8086
#define DEVICE_ID 0x8108
#define CLASS_ID  0x0300

/* BAR layout: BAR0 MMIO (VDC+SGX), BAR2 GATT (stolen), BAR3 GTT (translation) */
#define BAR_MMIO  0
#define BAR_GATT  2
#define BAR_GTT   3

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];
    MemoryRegion rom_region;         /* Fake ROM BAR to prevent assignment errors */

    uint32_t intr_enable;
    uint32_t intr_mask;
    uint32_t intr_identity;
    uint32_t hwstam;
    uint32_t pipeastat;
    uint32_t pipebstat;
    uint32_t pipecstat;
    uint32_t cura_cntr;
    uint32_t cura_base;
    uint32_t curb_cntr;
    uint32_t curb_base;
    uint32_t curc_cntr;
    uint32_t curc_base;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t pending = s->intr_enable & s->intr_identity & ~s->intr_mask;
    if (pending) {
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
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr < 0x80000) { /* VDC region */
        switch (addr) {
        case PSB_HWSTAM:
            val = s->hwstam;
            break;
        case PSB_INT_ENABLE_R:
            val = s->intr_enable;
            break;
        case PSB_INT_IDENTITY_R:
            val = s->intr_identity;
            break;
        case PSB_INT_MASK_R:
            val = s->intr_mask;
            break;
        case PIPEASTAT:
            val = s->pipeastat;
            break;
        case PIPEBSTAT:
            val = s->pipebstat;
            break;
        case PIPECSTAT:
            val = s->pipecstat;
            break;
        case CURACNTR:
            val = s->cura_cntr;
            break;
        case CURABASE:
            val = s->cura_base;
            break;
        case CURBCNTR:
            val = s->curb_cntr;
            break;
        case CURBBASE:
            val = s->curb_base;
            break;
        case CURCCNTR:
            val = s->curc_cntr;
            break;
        case CURCBASE:
            val = s->curc_base;
            break;
        default:
            qemu_log_mask(LOG_UNIMP, "gma500: unimplemented VDC read at 0x%" HWADDR_PRIx "\n", addr);
            break;
        }
    } else { /* SGX region, not implemented */
        val = 0;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < 0x80000) {
        switch (addr) {
        case PSB_HWSTAM:
            s->hwstam = val;
            break;
        case PSB_INT_ENABLE_R:
            s->intr_enable = val;
            pcibase_update_irq(s);
            break;
        case PSB_INT_IDENTITY_R:
            s->intr_identity = val;
            pcibase_update_irq(s);
            break;
        case PSB_INT_MASK_R:
            s->intr_mask = val;
            pcibase_update_irq(s);
            break;
        case PIPEASTAT:
            s->pipeastat = val;
            break;
        case PIPEBSTAT:
            s->pipebstat = val;
            break;
        case PIPECSTAT:
            s->pipecstat = val;
            break;
        case CURACNTR:
            s->cura_cntr = val;
            break;
        case CURABASE:
            s->cura_base = val;
            break;
        case CURBCNTR:
            s->curb_cntr = val;
            break;
        case CURBBASE:
            s->curb_base = val;
            break;
        case CURCCNTR:
            s->curc_cntr = val;
            break;
        case CURCBASE:
            s->curc_base = val;
            break;
        default:
            qemu_log_mask(LOG_UNIMP, "gma500: unimplemented VDC write at 0x%" HWADDR_PRIx " = 0x%" PRIx64 "\n", addr, val);
            break;
        }
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t address, int len)
{
    uint32_t val = pci_default_read_config(pdev, address, len);
    if (address == PSB_BSM && len == 4) {
        /* BSM (BackStolen Memory): return base address of BAR2 (stolen memory region) */
        val = pci_get_long(pdev->config + PCI_BASE_ADDRESS_0 + BAR_GATT * 4) & ~0xf;
    }
    return val;
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t address, uint32_t val, int len)
{
    /* Allow writes to ROM BAR (enabled by fake ROM BAR) */
    pci_default_write_config(pdev, address, val, len);
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    s->intr_enable = 0;
    s->intr_mask = 0;
    s->intr_identity = 0;
    s->hwstam = 0;
    s->pipeastat = 0;
    s->pipebstat = 0;
    s->pipecstat = 0;
    s->cura_cntr = 0;
    s->cura_base = 0;
    s->curb_cntr = 0;
    s->curb_base = 0;
    s->curc_cntr = 0;
    s->curc_base = 0;
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    pdev->config_read = pcibase_config_read;
    pdev->config_write = pcibase_config_write;

    /* BAR0: 2MB MMIO region for VDC and SGX */
    memory_region_init_io(&s->bar_regions[BAR_MMIO], OBJECT(s), &pcibase_mmio_ops, s, "gma500-mmio", 0x200000);
    pci_register_bar(pdev, BAR_MMIO, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_regions[BAR_MMIO]);

    /* BAR2: 8MB GATT (Graphics Aperture Translation Table) - stolen memory */
    memory_region_init_ram(&s->bar_regions[BAR_GATT], OBJECT(s), "gma500-gatt", 0x800000, errp);
    pci_register_bar(pdev, BAR_GATT, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_regions[BAR_GATT]);

    /* BAR3: 2MB GTT (Graphics Translation Table) */
    memory_region_init_ram(&s->bar_regions[BAR_GTT], OBJECT(s), "gma500-gtt", 0x200000, errp);
    pci_register_bar(pdev, BAR_GTT, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_regions[BAR_GTT]);

    /* Fake ROM BAR: 64KB to prevent "bogus alignment" assignment error */
    memory_region_init_ram(&s->rom_region, OBJECT(s), "gma500-rom", 0x10000, errp);
    pci_register_bar(pdev, 6, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->rom_region);
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

static const VMStateDescription vmstate_pcibase = {
    .name = "gma500_pci",
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
