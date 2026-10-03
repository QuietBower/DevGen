/*
 * QEMU ARKfb PCI device model
 * Based on Linux arkfb driver from /home/eely/linux-7.1/drivers/video/fbdev/arkfb.c
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "qemu/host-utils.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "exec/ioport.h"
#include "sysemu/dma.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/pci/pcie.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-core.h"
#include "migration/vmstate.h"

#define TYPE_PCIBASE_DEVICE "arkfb_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0xEDD8
#define DEVICE_ID 0xA099
#define CLASS_ID  0x0300

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

    /* VGA state */
    uint8_t seq_index;
    uint8_t seq_regs[256];
    uint8_t crt_index;
    uint8_t crt_regs[256];
    uint8_t gfx_index;
    uint8_t gfx_regs[256];
    uint8_t attr_index;
    uint8_t attr_regs[32];
    uint8_t attr_flipflop;
    uint8_t misc_reg;
    uint8_t dac_mask;
    uint8_t dac_read_index;
    uint8_t dac_write_index;
    uint32_t dac_palette[256];
    uint8_t dac_read_cycle;
    uint8_t dac_write_cycle;
    uint8_t vsync_toggle;
    MemoryRegion vga_io;
};

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static uint64_t pcibase_vga_io_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    addr += 0x3c0;

    switch (addr) {
    case 0x3c0:
        if (s->attr_flipflop) {
            val = s->attr_regs[s->attr_index & 0x1f];
        } else {
            val = s->attr_index;
        }
        break;
    case 0x3c1:
        val = s->attr_regs[s->attr_index & 0x1f];
        break;
    case 0x3c2:
        val = 0x00;
        break;
    case 0x3c4:
        val = s->seq_index;
        break;
    case 0x3c5:
        val = s->seq_regs[s->seq_index];
        break;
    case 0x3c6:
        val = s->dac_mask;
        break;
    case 0x3c7:
        val = s->dac_read_index;
        break;
    case 0x3c8:
        val = s->dac_write_index;
        break;
    case 0x3c9:
        switch (s->dac_read_cycle) {
        case 0:
            val = (uint8_t)(s->dac_palette[s->dac_read_index] >> 16);
            break;
        case 1:
            val = (uint8_t)(s->dac_palette[s->dac_read_index] >> 8);
            break;
        case 2:
            val = (uint8_t)(s->dac_palette[s->dac_read_index]);
            s->dac_read_index++;
            break;
        }
        s->dac_read_cycle = (s->dac_read_cycle + 1) % 3;
        break;
    case 0x3cc:
        val = s->misc_reg;
        break;
    case 0x3ce:
        val = s->gfx_index;
        break;
    case 0x3cf:
        val = s->gfx_regs[s->gfx_index & 0x0f];
        break;
    case 0x3d4:
        val = s->crt_index;
        break;
    case 0x3d5:
        val = s->crt_regs[s->crt_index];
        break;
    case 0x3da:
        /* Input Status Register 1: toggle bit 3 (vertical retrace) on each read */
        val = (s->vsync_toggle << 3);
        s->vsync_toggle ^= 1;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "arkfb: unhandled VGA I/O read addr %x\n", (int)addr);
        break;
    }
    return val;
}

static void pcibase_vga_io_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    addr += 0x3c0;

    switch (addr) {
    case 0x3c0:
        if (s->attr_flipflop) {
            s->attr_regs[s->attr_index & 0x1f] = val;
        } else {
            s->attr_index = val & 0x1f;
        }
        s->attr_flipflop = !s->attr_flipflop;
        break;
    case 0x3c2:
        s->misc_reg = val;
        break;
    case 0x3c4:
        s->seq_index = val;
        break;
    case 0x3c5:
        s->seq_regs[s->seq_index] = val;
        break;
    case 0x3c6:
        s->dac_mask = val;
        break;
    case 0x3c7:
        s->dac_read_index = val;
        s->dac_read_cycle = 0;
        break;
    case 0x3c8:
        s->dac_write_index = val;
        s->dac_write_cycle = 0;
        break;
    case 0x3c9:
        switch (s->dac_write_cycle) {
        case 0:
            s->dac_palette[s->dac_write_index] = (s->dac_palette[s->dac_write_index] & 0x00ffff) | ((val & 0xff) << 16);
            break;
        case 1:
            s->dac_palette[s->dac_write_index] = (s->dac_palette[s->dac_write_index] & 0xff00ff) | ((val & 0xff) << 8);
            break;
        case 2:
            s->dac_palette[s->dac_write_index] = (s->dac_palette[s->dac_write_index] & 0xffff00) | (val & 0xff);
            s->dac_write_index++;
            break;
        }
        s->dac_write_cycle = (s->dac_write_cycle + 1) % 3;
        break;
    case 0x3ce:
        s->gfx_index = val;
        break;
    case 0x3cf:
        s->gfx_regs[s->gfx_index & 0x0f] = val;
        break;
    case 0x3d4:
        s->crt_index = val;
        break;
    case 0x3d5:
        s->crt_regs[s->crt_index] = val;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "arkfb: unhandled VGA I/O write addr %x val %"PRIx64"\n", (int)addr, val);
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
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl = { .min_access_size = 1, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    memset(s->seq_regs, 0, sizeof(s->seq_regs));
    memset(s->crt_regs, 0, sizeof(s->crt_regs));
    memset(s->gfx_regs, 0, sizeof(s->gfx_regs));
    memset(s->attr_regs, 0, sizeof(s->attr_regs));
    s->seq_index = 0;
    s->crt_index = 0;
    s->gfx_index = 0;
    s->attr_index = 0;
    s->attr_flipflop = 0;
    s->misc_reg = 0;
    s->dac_mask = 0xff;
    s->dac_read_index = 0;
    s->dac_write_index = 0;
    memset(s->dac_palette, 0, sizeof(s->dac_palette));
    s->dac_read_cycle = 0;
    s->dac_write_cycle = 0;
    s->vsync_toggle = 0;

    /* Report 4MB framebuffer size via sequencer register 0x10 */
    s->seq_regs[0x10] = 0x80;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization: BAR0 as 4MB framebuffer RAM */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_RAM;
    s->bar_info[0].size = 4 * MiB;
    s->bar_info[0].name = "arkfb-fb";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Map legacy VGA I/O ports */
    memory_region_init_io(&s->vga_io, OBJECT(s), &pcibase_vga_io_ops, s, "vga-legacy", 0x20);
    memory_region_add_subregion(get_system_io(), 0x3c0, &s->vga_io);
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
    .name = "arkfb_pci",
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
