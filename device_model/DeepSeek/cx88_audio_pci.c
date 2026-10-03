/*
 * QEMU device model for Conexant CX2388x audio PCI interface
 * Generated from Linux driver cx88-alsa.c
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

#define TYPE_PCIBASE_DEVICE "cx88_audio_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define VENDOR_ID 0x14f1
#define DEVICE_ID 0x8801
#define CLASS_ID  PCI_CLASS_MULTIMEDIA_AUDIO

/* Register Offsets */
#define MO_AUD_INTMSK       0x200060
#define MO_DEV_CNTRL2       0x200034
#define MO_AUDD_GPCNTRL     0x32C030
#define MO_AUD_INTSTAT      0x200064
#define MO_AUDD_LNGTH       0x32C048
#define MO_AUD_DMACNTRL     0x32C040
#define MO_AUDD_GPCNT       0x32C020
#define MO_PCI_INTMSK       0x200040
#define MO_PCI_INTSTAT      0x200044
#define AUD_VOL_CTL         0x320594
#define AUD_BAL_CTL         0x320598

/* Bit definitions */
#define PCI_INT_AUDINT      (1 << 1)

/* BAR type identifiers */
#define BAR_TYPE_MMIO  1
#define BAR_TYPE_PIO   2
#define BAR_TYPE_RAM   3
#define BAR_TYPE_NONE  0

typedef struct BARInfo {
    int index;
    int type;
    hwaddr size;
    const char *name;
} BARInfo;

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Register shadows */
    uint32_t aud_intmsk;
    uint32_t aud_intstat;
    uint32_t aud_dmacntrl;
    uint32_t audd_gpcntrl;
    uint32_t audd_lngth;
    uint32_t audd_gpcnt;
    uint32_t pci_intmsk;
    uint32_t pci_intstat;
    uint32_t dev_cntrl2;
    uint32_t aud_vol_ctl;
    uint32_t aud_bal_ctl;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t pending;

    /* Compute pending interrupts: if any unmasked PCI interrupt bit is set */
    pending = s->pci_intstat & s->pci_intmsk;

    if (pending) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size != 4) {
        return ~0ULL;
    }

    switch (addr) {
    case MO_AUD_INTMSK:
        val = s->aud_intmsk;
        break;
    case MO_DEV_CNTRL2:
        val = s->dev_cntrl2;
        break;
    case MO_AUDD_GPCNTRL:
        val = s->audd_gpcntrl;
        break;
    case MO_AUD_INTSTAT:
        val = s->aud_intstat;
        break;
    case MO_AUDD_LNGTH:
        val = s->audd_lngth;
        break;
    case MO_AUD_DMACNTRL:
        val = s->aud_dmacntrl;
        break;
    case MO_AUDD_GPCNT:
        val = s->audd_gpcnt;
        break;
    case MO_PCI_INTMSK:
        val = s->pci_intmsk;
        break;
    case MO_PCI_INTSTAT:
        val = s->pci_intstat;
        break;
    case AUD_VOL_CTL:
        val = s->aud_vol_ctl;
        break;
    case AUD_BAL_CTL:
        val = s->aud_bal_ctl;
        break;
    default:
        val = 0;
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        return;
    }

    switch (addr) {
    case MO_AUD_INTMSK:
        s->aud_intmsk = val;
        pcibase_update_irq(s);
        break;
    case MO_DEV_CNTRL2:
        s->dev_cntrl2 = val;
        break;
    case MO_AUDD_GPCNTRL:
        s->audd_gpcntrl = val;
        break;
    case MO_AUD_INTSTAT:
        /* W1C: writing 1 to a bit clears it */
        s->aud_intstat &= ~val;
        pcibase_update_irq(s);
        break;
    case MO_AUDD_LNGTH:
        s->audd_lngth = val;
        break;
    case MO_AUD_DMACNTRL:
        s->aud_dmacntrl = val;
        break;
    case MO_AUDD_GPCNT:
        s->audd_gpcnt = val;
        break;
    case MO_PCI_INTMSK:
        s->pci_intmsk = val;
        pcibase_update_irq(s);
        break;
    case MO_PCI_INTSTAT:
        /* W1C */
        s->pci_intstat &= ~val;
        pcibase_update_irq(s);
        break;
    case AUD_VOL_CTL:
        s->aud_vol_ctl = val;
        break;
    case AUD_BAL_CTL:
        s->aud_bal_ctl = val;
        break;
    default:
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
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

    /* Clear all register shadows */
    s->aud_intmsk = 0;
    s->aud_intstat = 0;
    s->aud_dmacntrl = 0;
    s->audd_gpcntrl = 0;
    s->audd_lngth = 0;
    s->audd_gpcnt = 0;
    s->pci_intmsk = 0;
    s->pci_intstat = 0;
    s->dev_cntrl2 = 0;
    s->aud_vol_ctl = 0;
    s->aud_bal_ctl = 0;
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp);

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

    s->num_bars = 2;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x400000;
    s->bar_info[0].name = "bar0-mmio";
    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_PIO;
    s->bar_info[1].size = 0x100;
    s->bar_info[1].name = "bar1-io";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI/MSI-X not used */
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
    .name = "cx88_audio_pci",
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
