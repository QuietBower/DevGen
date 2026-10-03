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

#define TYPE_PCIBASE_DEVICE "tridentfb_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers */
#define BIT(x) (1 << (x))
#define DDC_SCL_OUT BIT(1)
#define DDC_SDA_OUT BIT(3)
#define DDC_SCL_IN  BIT(6)
#define DDC_SDA_IN  BIT(0)
#define DDC_SDA_TGUI BIT(0)
#define DDC_SCL_TGUI BIT(1)
#define DDC_SCL_DRIVE_TGUI BIT(2)
#define DDC_SDA_DRIVE_TGUI BIT(3)
#define DDC_MASK_TGUI (DDC_SCL_DRIVE_TGUI | DDC_SDA_DRIVE_TGUI)
#define DDC_MASK (DDC_SCL_OUT | DDC_SDA_OUT)

#define I2C_INDEX  0x2E
#define SPR_INDEX  0x1F

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

    /* VGA register state */
    uint8_t crtc_index;
    uint8_t crtc_regs[256];
    uint8_t seq_index;
    uint8_t seq_regs[64];
    uint8_t gfx_index;
    uint8_t gfx_regs[64];
    uint8_t attr_index;
    uint8_t attr_regs[21]; /* 0x00-0x14 */
    bool attr_flipflop; /* false: index, true: data */

    /* MMIO data for other regions (acceleration regs) */
    uint8_t mmio_data[0x100000]; /* 1 MB */

    /* VGA I/O region */
    MemoryRegion vga_io_region;
};

/* Helper to compute I2C register input bits based on output drive */
static uint8_t i2c_reg_read_val(PCIBaseState *s, uint8_t val)
{
    bool sda_drive = (val & DDC_SDA_OUT) || (val & DDC_SDA_DRIVE_TGUI);
    bool scl_drive = (val & DDC_SCL_OUT) || (val & DDC_SCL_DRIVE_TGUI);
    val &= ~(DDC_SDA_IN | DDC_SCL_IN);
    if (!sda_drive) {
        val |= DDC_SDA_IN;
    }
    if (!scl_drive) {
        val |= DDC_SCL_IN;
    }
    return val;
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size < 1 || size > 4) {
        return ~0ULL;
    }

    /* VGA register access */
    switch (addr) {
    case 0x3C0:
        /* Attribute write/address */
        if (size != 1) return ~0ULL;
        /* writes are handled in write; reads return data? */
        val = s->attr_regs[s->attr_index];
        break;
    case 0x3C1:
        if (size != 1) return ~0ULL;
        val = s->attr_regs[s->attr_index];
        s->attr_flipflop = false; /* reset flipflop on read */
        break;

    case 0x3C4:
        if (size != 1) return ~0ULL;
        val = s->seq_index;
        break;
    case 0x3C5:
        if (size != 1) return ~0ULL;
        val = s->seq_regs[s->seq_index];
        break;

    case 0x3CE:
        if (size != 1) return ~0ULL;
        val = s->gfx_index;
        break;
    case 0x3CF:
        if (size != 1) return ~0ULL;
        val = s->gfx_regs[s->gfx_index];
        break;

    case 0x3D4:
        if (size != 1) return ~0ULL;
        val = s->crtc_index;
        break;
    case 0x3D5:
        if (size != 1) return ~0ULL;
        val = s->crtc_regs[s->crtc_index];
        /* Apply I2C input emulation if this is the I2C register */
        if (s->crtc_index == I2C_INDEX) {
            val = i2c_reg_read_val(s, val);
        }
        break;

    default:
        /* Generic MMIO - acceleration registers, etc. */
        if (addr + size <= sizeof(s->mmio_data)) {
            memcpy(&val, s->mmio_data + addr, size);
        }
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size < 1 || size > 4) {
        return;
    }

    switch (addr) {
    case 0x3C0:
        /* Attribute write: flip-flop toggles */
        if (size != 1) return;
        if (!s->attr_flipflop) {
            s->attr_index = val & 0x1F;
            s->attr_flipflop = true;
        } else {
            s->attr_regs[s->attr_index] = val;
            s->attr_flipflop = false;
        }
        break;

    case 0x3C1:
        /* Read-only port; ignore writes */
        break;

    case 0x3C4:
        if (size != 1) return;
        s->seq_index = val & 0x3F;
        break;

    case 0x3C5:
        if (size != 1) return;
        s->seq_regs[s->seq_index] = val;
        break;

    case 0x3CE:
        if (size != 1) return;
        s->gfx_index = val & 0x3F;
        break;

    case 0x3CF:
        if (size != 1) return;
        s->gfx_regs[s->gfx_index] = val;
        break;

    case 0x3D4:
        if (size != 1) return;
        s->crtc_index = val & 0xFF;
        break;

    case 0x3D5:
        if (size != 1) return;
        /* Prevent overwriting memory size register */
        if (s->crtc_index != SPR_INDEX) {
            s->crtc_regs[s->crtc_index] = val;
        }
        break;

    default:
        /* Generic MMIO */
        if (addr + size <= sizeof(s->mmio_data)) {
            memcpy(s->mmio_data + addr, &val, size);
        }
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* VGA I/O region handlers (legacy ports 0x3C0-0x3DF) */
static uint64_t pcibase_vga_io_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    if (size != 1) return 0;

    switch (addr) {
    case 0x00: /* 0x3C0 - write-only attribute port, reading returns data from 0x3C1? */
        val = 0;
        break;
    case 0x01: /* 0x3C1 */
        val = s->attr_regs[s->attr_index];
        s->attr_flipflop = false;
        break;
    case 0x04: /* 0x3C4 */
        val = s->seq_index;
        break;
    case 0x05: /* 0x3C5 */
        val = s->seq_regs[s->seq_index];
        break;
    case 0x0E: /* 0x3CE */
        val = s->gfx_index;
        break;
    case 0x0F: /* 0x3CF */
        val = s->gfx_regs[s->gfx_index];
        break;
    case 0x14: /* 0x3D4 */
        val = s->crtc_index;
        break;
    case 0x15: /* 0x3D5 */
        val = s->crtc_regs[s->crtc_index];
        if (s->crtc_index == I2C_INDEX) {
            val = i2c_reg_read_val(s, val);
        }
        break;
    default:
        val = 0;
        break;
    }
    return val;
}

static void pcibase_vga_io_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    if (size != 1) return;

    switch (addr) {
    case 0x00: /* 0x3C0 */
        if (!s->attr_flipflop) {
            s->attr_index = val & 0x1F;
            s->attr_flipflop = true;
        } else {
            s->attr_regs[s->attr_index] = val;
            s->attr_flipflop = false;
        }
        break;
    case 0x04: /* 0x3C4 */
        s->seq_index = val & 0x3F;
        break;
    case 0x05: /* 0x3C5 */
        s->seq_regs[s->seq_index] = val;
        break;
    case 0x0E: /* 0x3CE */
        s->gfx_index = val & 0x3F;
        break;
    case 0x0F: /* 0x3CF */
        s->gfx_regs[s->gfx_index] = val;
        break;
    case 0x14: /* 0x3D4 */
        s->crtc_index = val & 0xFF;
        break;
    case 0x15: /* 0x3D5 */
        /* Prevent overwriting memory size register */
        if (s->crtc_index != SPR_INDEX) {
            s->crtc_regs[s->crtc_index] = val;
        }
        break;
    default:
        break;
    }
}

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

    /* Reset VGA state */
    s->crtc_index = 0;
    memset(s->crtc_regs, 0, sizeof(s->crtc_regs));
    s->seq_index = 0;
    memset(s->seq_regs, 0, sizeof(s->seq_regs));
    s->gfx_index = 0;
    memset(s->gfx_regs, 0, sizeof(s->gfx_regs));
    s->attr_index = 0;
    memset(s->attr_regs, 0, sizeof(s->attr_regs));
    s->attr_flipflop = false;

    /* Set default memory size: SPR = 0x0F -> 4 MB */
    s->crtc_regs[SPR_INDEX] = 0x0F;

    /* Clear MMIO RAM */
    memset(s->mmio_data, 0, sizeof(s->mmio_data));
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x1023);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x9880);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0300);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_VENDOR_ID, 0x1023);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_ID, 0x9880);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* BAR0: Framebuffer RAM, 8 MB */
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_RAM;
    s->bar_info[0].size = 8 * MiB;
    s->bar_info[0].name = "tridentfb-fb";

    /* BAR1: MMIO registers, 1 MB */
    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = 1 * MiB;
    s->bar_info[1].name = "tridentfb-mmio";

    s->num_bars = 2;
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* VGA legacy I/O region (ports 0x3C0-0x3DF) */
    memory_region_init_io(&s->vga_io_region, OBJECT(s), &pcibase_vga_io_ops, s,
                          "tridentfb-vga-io", 0x20);
    memory_region_add_subregion(pci_address_space_io(pdev), 0x3C0, &s->vga_io_region);

    /* Initialize the MMIO RAM to zero */
    memset(s->mmio_data, 0, sizeof(s->mmio_data));
    /* Initialize VGA registers to zero (reset will set SPR) */
    memset(s->crtc_regs, 0, sizeof(s->crtc_regs));
    memset(s->seq_regs, 0, sizeof(s->seq_regs));
    memset(s->gfx_regs, 0, sizeof(s->gfx_regs));
    memset(s->attr_regs, 0, sizeof(s->attr_regs));
    s->crtc_index = 0;
    s->seq_index = 0;
    s->gfx_index = 0;
    s->attr_index = 0;
    s->attr_flipflop = false;
    s->crtc_regs[SPR_INDEX] = 0x0F; /* 4 MB */
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    memory_region_del_subregion(pci_address_space_io(pdev), &s->vga_io_region);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

static const VMStateDescription vmstate_pcibase = {
    .name = "tridentfb_pci",
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
