/* Integrated QEMU PCI device model for NVIDIA Riva framebuffer (rivafb) */
#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "qemu/host-utils.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "exec/address-spaces.h"
#include "sysemu/dma.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/pci/pcie.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-core.h"
#include "migration/vmstate.h"

#define TYPE_PCIBASE_DEVICE "rivafb_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI_VENDOR_ID_NVIDIA_SGS 0x12d2
#define PCI_DEVICE_ID_NVIDIA_SGS_RIVA128 0x0018
#define PCI_CLASS_DISPLAY_VGA 0x0300

#define NUM_SEQ_REGS 0x05
#define NUM_GRC_REGS 0x09
#define NUM_CRT_REGS 0x41
#define NUM_ATC_REGS 0x15

#define BAR0_SIZE 0x1000000   /* MMIO control region size */
#define BAR1_SIZE 0x1000000   /* Framebuffer size */

typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM 
} BARType;

typedef struct {
    int     index;
    BARType type;
    hwaddr  size;
    const char *name;
} BARInfo;

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* VGA register shadows */
    uint8_t attr[NUM_ATC_REGS];
    uint8_t crtc[NUM_CRT_REGS];
    uint8_t gra[NUM_GRC_REGS];
    uint8_t seq[NUM_SEQ_REGS];
    uint8_t misc_output;

    /* VGA I/O port state */
    uint8_t seq_index;
    uint8_t crtc_index;
    uint8_t gra_index;
    uint8_t attr_index;
    bool attr_flip_flop;     /* true = data, false = address */
    uint8_t dac_read_index;
    uint8_t dac_write_index;
    uint8_t dac_palette[256 * 3]; /* 256 entries of RGB */
    /* VGA I/O region */
    MemoryRegion vga_io;
};

static const uint8_t initial_attr[NUM_ATC_REGS] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x41, 0x01, 0x0F, 0x00, 0x00
};
static const uint8_t initial_crtc[NUM_CRT_REGS] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xE3,
    0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00
};
static const uint8_t initial_gra[NUM_GRC_REGS] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x05, 0x0F, 0xFF
};
static const uint8_t initial_seq[NUM_SEQ_REGS] = {
    0x03, 0x01, 0x0F, 0x00, 0x0E
};
static const uint8_t initial_misc = 0xEB;

G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    /* The driver reads MMIO registers, but we lack detailed register layout.
     * Returning 0 is safe for basic probing; advanced registers are not emulated. */
    qemu_log_mask(LOG_UNIMP, "riva: MMIO read at 0x%" HWADDR_PRIx " size %u\n", addr, size);
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    qemu_log_mask(LOG_UNIMP, "riva: MMIO write at 0x%" HWADDR_PRIx " size %u val 0x%" PRIx64 "\n", addr, size, val);
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    /* Not used; VGA ports handled separately */
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
}

/* VGA legacy I/O handler (0x3c0-0x3df) */
static uint64_t pcibase_vga_io_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = PCIBASE_DEVICE(opaque);
    uint32_t val = 0xff;

    switch (addr + 0x3c0) {
    case 0x3c1: /* Attribute data read */
        val = s->attr[s->attr_index];
        break;
    case 0x3cc: /* Miscellaneous output read */
        val = s->misc_output;
        break;
    case 0x3c5: /* Sequencer data read */
        if (s->seq_index < NUM_SEQ_REGS) {
            val = s->seq[s->seq_index];
        }
        break;
    case 0x3c9: /* DAC data read */
        val = s->dac_palette[s->dac_read_index * 3 + 0];
        s->dac_read_index = (s->dac_read_index + 1) % 256;
        break;
    case 0x3cf: /* Graphics controller data read */
        if (s->gra_index < NUM_GRC_REGS) {
            val = s->gra[s->gra_index];
        }
        break;
    case 0x3d5: /* CRTC data read */
        if (s->crtc_index < NUM_CRT_REGS) {
            val = s->crtc[s->crtc_index];
        }
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "riva: VGA read port 0x%x\n", addr + 0x3c0);
        val = 0xff;
        break;
    }
    return val;
}

static void pcibase_vga_io_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = PCIBASE_DEVICE(opaque);
    uint32_t port = addr + 0x3c0;

    switch (port) {
    case 0x3c0: /* Attribute address/data write */
        if (!s->attr_flip_flop) {
            s->attr_index = val & 0x1f;
            s->attr_flip_flop = true;
        } else {
            if (s->attr_index < NUM_ATC_REGS) {
                s->attr[s->attr_index] = val;
            }
            s->attr_flip_flop = false;
        }
        break;
    case 0x3c2: /* Miscellaneous output write */
        s->misc_output = val;
        break;
    case 0x3c4: /* Sequencer index */
        s->seq_index = val & 0x1f;
        break;
    case 0x3c5: /* Sequencer data */
        if (s->seq_index < NUM_SEQ_REGS) {
            s->seq[s->seq_index] = val;
        }
        break;
    case 0x3c7: /* DAC read index */
        s->dac_read_index = val;
        break;
    case 0x3c8: /* DAC write index */
        s->dac_write_index = val;
        break;
    case 0x3c9: /* DAC data write */
        s->dac_palette[s->dac_write_index * 3 + 0] = val;
        s->dac_write_index = (s->dac_write_index + 1) % 256;
        break;
    case 0x3ce: /* Graphics controller index */
        s->gra_index = val & 0x0f;
        break;
    case 0x3cf: /* Graphics controller data */
        if (s->gra_index < NUM_GRC_REGS) {
            s->gra[s->gra_index] = val;
        }
        break;
    case 0x3d4: /* CRTC index */
        s->crtc_index = val;
        break;
    case 0x3d5: /* CRTC data */
        if (s->crtc_index < NUM_CRT_REGS) {
            s->crtc[s->crtc_index] = val;
        }
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "riva: VGA write port 0x%x val 0x%x\n", port, (unsigned)val);
        break;
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

static const MemoryRegionOps pcibase_vga_io_ops = {
    .read = pcibase_vga_io_read,
    .write = pcibase_vga_io_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 1 },
    .impl  = { .min_access_size = 1, .max_access_size = 1 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    memcpy(s->attr, initial_attr, sizeof(initial_attr));
    memcpy(s->crtc, initial_crtc, sizeof(initial_crtc));
    memcpy(s->gra, initial_gra, sizeof(initial_gra));
    memcpy(s->seq, initial_seq, sizeof(initial_seq));
    s->misc_output = initial_misc;

    s->seq_index = 0;
    s->crtc_index = 0;
    s->gra_index = 0;
    s->attr_index = 0;
    s->attr_flip_flop = false;
    s->dac_read_index = 0;
    s->dac_write_index = 0;
    memset(s->dac_palette, 0, sizeof(s->dac_palette));
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x12d2);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x0018);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0300);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);
    /* Ensure IO and memory space are enabled (driver expects them on) */
    pci_set_word(pci_conf + PCI_COMMAND, pci_get_word(pci_conf + PCI_COMMAND) | PCI_COMMAND_IO | PCI_COMMAND_MEMORY);

    s->num_bars = 2;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = BAR0_SIZE, .name = "bar0-mmio" };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_RAM, .size = BAR1_SIZE, .name = "bar1-fb" };

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    memcpy(s->attr, initial_attr, sizeof(initial_attr));
    memcpy(s->crtc, initial_crtc, sizeof(initial_crtc));
    memcpy(s->gra, initial_gra, sizeof(initial_gra));
    memcpy(s->seq, initial_seq, sizeof(initial_seq));
    s->misc_output = initial_misc;
    s->seq_index = 0;
    s->crtc_index = 0;
    s->gra_index = 0;
    s->attr_index = 0;
    s->attr_flip_flop = false;
    s->dac_read_index = 0;
    s->dac_write_index = 0;
    memset(s->dac_palette, 0, sizeof(s->dac_palette));

    /* Map VGA legacy I/O ports at 0x3c0-0x3df */
    memory_region_init_io(&s->vga_io, OBJECT(s), &pcibase_vga_io_ops, s, "riva-vga-io", 0x20);
    memory_region_add_subregion(get_system_io(), 0x3c0, &s->vga_io);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    memory_region_del_subregion(get_system_io(), &s->vga_io);
    memory_region_unref(&s->vga_io);
}

static const VMStateDescription vmstate_pcibase = {
    .name = "rivafb_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT8_ARRAY(attr, PCIBaseState, NUM_ATC_REGS),
        VMSTATE_UINT8_ARRAY(crtc, PCIBaseState, NUM_CRT_REGS),
        VMSTATE_UINT8_ARRAY(gra, PCIBaseState, NUM_GRC_REGS),
        VMSTATE_UINT8_ARRAY(seq, PCIBaseState, NUM_SEQ_REGS),
        VMSTATE_UINT8(misc_output, PCIBaseState),
        VMSTATE_UINT8(seq_index, PCIBaseState),
        VMSTATE_UINT8(crtc_index, PCIBaseState),
        VMSTATE_UINT8(gra_index, PCIBaseState),
        VMSTATE_UINT8(attr_index, PCIBaseState),
        VMSTATE_BOOL(attr_flip_flop, PCIBaseState),
        VMSTATE_UINT8(dac_read_index, PCIBaseState),
        VMSTATE_UINT8(dac_write_index, PCIBaseState),
        VMSTATE_UINT8_ARRAY(dac_palette, PCIBaseState, 256 * 3),
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
