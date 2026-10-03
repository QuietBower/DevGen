/*
 * QEMU PCI device model for SM712 framebuffer
 * Based on smtcfb driver (sm712fb.c)
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

#define TYPE_PCIBASE_DEVICE "smtcfb_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI IDs from smtcfb_pci_table */
#define VENDOR_ID 0x126f
#define DEVICE_ID 0x710
#define CLASS_ID  0x030000

/* Known register block offsets within BAR0 (chip 710/712) */
#define LFB_OFFSET        0x00000000  /* Linear framebuffer */
#define DP_REGS_OFFSET    0x00408000  /* Display processor registers */
#define VP_REGS_OFFSET    0x0040c000  /* Video processor registers */
#define SMTC_REGS_OFFSET  0x00700000  /* SMTC sequence registers (mmio) */

/* Additional defines (unused) */
#define mmio_addr         0x00c00000
#define big_addr          0

struct PCIBaseState {
    PCIDevice parent_obj;

    /* BAR0 subregions */
    MemoryRegion bar_container;
    MemoryRegion lfb_ram;      /* Framebuffer RAM (4MB) */
    MemoryRegion vga_mmio;     /* VGA register aliases at 0x700000+port */
    MemoryRegion vp_mmio;      /* VP registers at 0x40c000 */

    /* VGA register emulation */
    uint8_t seq_index;
    uint8_t seq_regs[256];
    uint8_t gr_index;
    uint8_t gr_regs[256];
    uint8_t crtc_index;
    uint8_t crtc_regs[256];
    uint8_t ar_index;
    bool ar_flip_flop;         /* false=index, true=data */
    uint8_t ar_regs[21];
    uint8_t dac_mask;
    uint8_t dac_write_index;
    uint8_t dac_color;         /* 0=R,1=G,2=B */
    uint8_t palette[256][3];
    uint8_t misc_output;
    uint8_t vp_regs[0x100];    /* VP register space */
};

/* VGA MMIO handlers: offset range 0x3c0-0x3df */
static uint64_t vga_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint8_t port = addr + 0x3c0;
    uint64_t val = 0xff;

    switch (port) {
    case 0x3c1: /* Attribute data read */
        val = s->ar_regs[s->ar_index & 0x1f];
        break;
    case 0x3c5: /* Sequencer data */
        val = s->seq_regs[s->seq_index];
        break;
    case 0x3cf: /* Graphics data */
        val = s->gr_regs[s->gr_index];
        break;
    case 0x3d5: /* CRTC data */
        val = s->crtc_regs[s->crtc_index];
        break;
    case 0x3da: /* Input status, resets attribute flip-flop */
        s->ar_flip_flop = false;
        val = 0x00;  /* Not in retrace */
        break;
    /* Reads not explicitly used by driver return default */
    default:
        break;
    }
    return val;
}

static void vga_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint8_t port = addr + 0x3c0;
    uint8_t data = val & 0xff;

    switch (port) {
    case 0x3c0: /* Attribute index/data */
        if (!s->ar_flip_flop) {
            s->ar_index = data & 0x1f;
            s->ar_flip_flop = true;
        } else {
            s->ar_regs[s->ar_index] = data;
            s->ar_flip_flop = false;
        }
        break;
    case 0x3c2: /* Misc output */
        s->misc_output = data;
        break;
    case 0x3c4: /* Sequencer index */
        s->seq_index = data;
        break;
    case 0x3c5: /* Sequencer data */
        s->seq_regs[s->seq_index] = data;
        break;
    case 0x3c6: /* DAC mask */
        s->dac_mask = data;
        break;
    case 0x3c8: /* DAC write index */
        s->dac_write_index = data;
        s->dac_color = 0;
        break;
    case 0x3c9: /* DAC data */
        s->palette[s->dac_write_index & 0xff][s->dac_color] = data;
        if (++s->dac_color == 3) {
            s->dac_color = 0;
            s->dac_write_index++;
        }
        break;
    case 0x3ce: /* Graphics index */
        s->gr_index = data;
        break;
    case 0x3cf: /* Graphics data */
        s->gr_regs[s->gr_index] = data;
        break;
    case 0x3d4: /* CRTC index */
        s->crtc_index = data;
        break;
    case 0x3d5: /* CRTC data */
        s->crtc_regs[s->crtc_index] = data;
        break;
    /* Other writes ignored */
    default:
        break;
    }
}

static const MemoryRegionOps vga_mmio_ops = {
    .read = vga_mmio_read,
    .write = vga_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 1 },
    .impl  = { .min_access_size = 1, .max_access_size = 1 },
};

/* VP MMIO handlers: offset 0x0..0xff */
static uint64_t vp_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* Not read by driver */
    return 0;
}

static void vp_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr < 0x100) {
        /* Only support 32-bit writes as used by driver */
        if (size == 4) {
            *(uint32_t *)&s->vp_regs[addr] = val;
        } else if (size == 2) {
            *(uint16_t *)&s->vp_regs[addr] = val;
        } else {
            s->vp_regs[addr] = val;
        }
    }
}

static const MemoryRegionOps vp_mmio_ops = {
    .read = vp_mmio_read,
    .write = vp_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    memset(&s->seq_index, 0, sizeof(*s) - offsetof(PCIBaseState, seq_index));
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID, VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR0: 8MB MMIO region */
    memory_region_init(&s->bar_container, OBJECT(s), "bar0-container", 0x800000);

    /* 4MB framebuffer RAM at offset 0 */
    memory_region_init_ram(&s->lfb_ram, OBJECT(s), "lfb-ram", 4 * MiB, &error_fatal);
    memory_region_add_subregion(&s->bar_container, 0x00000000, &s->lfb_ram);

    /* VGA register aliases at 0x700000 + legacy port (0x3c0-0x3df) */
    memory_region_init_io(&s->vga_mmio, OBJECT(s), &vga_mmio_ops, s,
                          "vga-regs", 0x20);
    memory_region_add_subregion(&s->bar_container, 0x00703c0, &s->vga_mmio);

    /* VP registers at 0x40c000 - 0x40c0ff */
    memory_region_init_io(&s->vp_mmio, OBJECT(s), &vp_mmio_ops, s,
                          "vp-regs", 0x100);
    memory_region_add_subregion(&s->bar_container, 0x0040c000, &s->vp_mmio);

    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_container);
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

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "smtcfb_pci",
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
