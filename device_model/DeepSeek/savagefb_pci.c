#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/module.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/pci/pcie.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"

#define TYPE_PCIBASE_DEVICE "savagefb_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define VENDOR_ID 0x5333
#define DEVICE_ID 0x8c22
#define CLASS_ID  0x030000

#define FIFO_CONTROL_REG       0x8200
#define MIU_CONTROL_REG        0x8204
#define STREAMS_TIMEOUT_REG    0x8208
#define MISC_TIMEOUT_REG       0x820c

#define MMIO_BAR_SIZE 0x80000   /* 512 KB */
#define FB_BAR_SIZE   0x800000  /* 8 MB, matches driver-probed 8192k */

#define VGA_MMIO_OFFSET 0x8000

#define REG_48C00 0x48C00
#define REG_48C60 0x48C60

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_mmio;
    MemoryRegion bar_fb;

    uint8_t mmio_data[MMIO_BAR_SIZE];

    uint8_t crtc_index;
    uint8_t seq_index;
    uint8_t gr_index;
    bool attr_flipflop;
    uint8_t attr_index;
    uint8_t misc_reg;
    uint8_t dac_write_index, dac_read_index;

    uint8_t crtc_regs[256];
    uint8_t seq_regs[256];
    uint8_t gr_regs[256];
    uint8_t attr_regs[21];

    uint32_t reg_8200, reg_8204, reg_8208, reg_820c;
    uint32_t reg_8128, reg_812c;
    uint16_t reg_8134, reg_8136;
    uint32_t reg_48c00, reg_48c60;
};

static void savage_update_irq(PCIBaseState *s) __attribute__((unused));
static void savage_update_irq(PCIBaseState *s)
{
}

static uint64_t vga_mmio_read(PCIBaseState *s, hwaddr port, unsigned size)
{
    uint64_t val = 0;
    switch (port) {
    case 0x3c0:
        val = 0;
        break;
    case 0x3c1:
        val = s->attr_regs[s->attr_index & 0x1f];
        s->attr_flipflop = !s->attr_flipflop;
        break;
    case 0x3c2:
        val = 0x00;
        break;
    case 0x3c3:
        val = 0x01;
        break;
    case 0x3c4:
        val = s->seq_index;
        break;
    case 0x3c5:
        val = s->seq_regs[s->seq_index];
        break;
    case 0x3c6:
        val = s->mmio_data[VGA_MMIO_OFFSET + port];
        break;
    case 0x3c7:
        val = s->dac_read_index;
        break;
    case 0x3c8:
        val = s->dac_write_index;
        break;
    case 0x3c9:
        val = s->mmio_data[VGA_MMIO_OFFSET + port];
        break;
    case 0x3ce:
        val = s->gr_index;
        break;
    case 0x3cf:
        val = s->gr_regs[s->gr_index];
        break;
    case 0x3d4:
        val = s->crtc_index;
        break;
    case 0x3d5:
        val = s->crtc_regs[s->crtc_index];
        break;
    case 0x3da:
        val = 0x00;
        break;
    default:
        val = s->mmio_data[VGA_MMIO_OFFSET + port];
        break;
    }
    return val;
}

static void vga_mmio_write(PCIBaseState *s, hwaddr port, uint64_t val, unsigned size)
{
    switch (port) {
    case 0x3c0:
        if (size == 1) {
            if (s->attr_flipflop) {
                s->attr_regs[s->attr_index & 0x1f] = val;
                s->mmio_data[VGA_MMIO_OFFSET + port] = val;
            } else {
                s->attr_index = val;
            }
            s->attr_flipflop = !s->attr_flipflop;
        }
        break;
    case 0x3c2:
        s->misc_reg = val;
        s->mmio_data[VGA_MMIO_OFFSET + port] = val;
        break;
    case 0x3c3:
        s->mmio_data[VGA_MMIO_OFFSET + port] = val;
        break;
    case 0x3c4:
        s->seq_index = val;
        s->mmio_data[VGA_MMIO_OFFSET + port] = val;
        break;
    case 0x3c5:
        s->seq_regs[s->seq_index] = val;
        s->mmio_data[VGA_MMIO_OFFSET + port] = val;
        break;
    case 0x3c6:
        s->mmio_data[VGA_MMIO_OFFSET + port] = val;
        break;
    case 0x3c7:
        s->dac_read_index = val;
        break;
    case 0x3c8:
        s->dac_write_index = val;
        s->mmio_data[VGA_MMIO_OFFSET + port] = val;
        break;
    case 0x3c9:
        s->mmio_data[VGA_MMIO_OFFSET + port] = val;
        break;
    case 0x3ce:
        s->gr_index = val;
        s->mmio_data[VGA_MMIO_OFFSET + port] = val;
        break;
    case 0x3cf:
        s->gr_regs[s->gr_index] = val;
        s->mmio_data[VGA_MMIO_OFFSET + port] = val;
        break;
    case 0x3d4:
        s->crtc_index = val;
        s->mmio_data[VGA_MMIO_OFFSET + port] = val;
        break;
    case 0x3d5:
        s->crtc_regs[s->crtc_index] = val;
        s->mmio_data[VGA_MMIO_OFFSET + port] = val;
        break;
    default:
        s->mmio_data[VGA_MMIO_OFFSET + port] = val;
        break;
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = PCIBASE_DEVICE(opaque);
    uint64_t val = 0;

    if (addr >= VGA_MMIO_OFFSET && addr < VGA_MMIO_OFFSET + 0x100) {
        val = vga_mmio_read(s, addr - VGA_MMIO_OFFSET, size);
        return val;
    }

    switch (addr) {
    case REG_48C00:
        if (size == 4) return s->reg_48c00;
        break;
    case REG_48C60:
        if (size == 4) return s->reg_48c60;
        break;
    case FIFO_CONTROL_REG:
        if (size == 4) return s->reg_8200;
        break;
    case MIU_CONTROL_REG:
        if (size == 4) return s->reg_8204;
        break;
    case STREAMS_TIMEOUT_REG:
        if (size == 4) return s->reg_8208;
        break;
    case MISC_TIMEOUT_REG:
        if (size == 4) return s->reg_820c;
        break;
    case 0x8128:
        if (size == 4) return s->reg_8128;
        break;
    case 0x812C:
        if (size == 4) return s->reg_812c;
        break;
    case 0x8134:
        if (size == 2) return s->reg_8134;
        break;
    case 0x8136:
        if (size == 2) return s->reg_8136;
        break;
    default:
        if (addr < MMIO_BAR_SIZE) {
            memcpy(&val, s->mmio_data + addr, size);
            return val;
        }
        qemu_log_mask(LOG_UNIMP, "savagefb: mmio read out of range: 0x%" HWADDR_PRIx " size %d\n", addr, size);
        return 0xffffffff;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = PCIBASE_DEVICE(opaque);

    if (addr >= VGA_MMIO_OFFSET && addr < VGA_MMIO_OFFSET + 0x100) {
        vga_mmio_write(s, addr - VGA_MMIO_OFFSET, val, size);
        return;
    }

    switch (addr) {
    case REG_48C00:
        if (size == 4) s->reg_48c00 = val;
        break;
    case REG_48C60:
        if (size == 4) s->reg_48c60 = val;
        break;
    case FIFO_CONTROL_REG:
        if (size == 4) s->reg_8200 = val;
        break;
    case MIU_CONTROL_REG:
        if (size == 4) s->reg_8204 = val;
        break;
    case STREAMS_TIMEOUT_REG:
        if (size == 4) s->reg_8208 = val;
        break;
    case MISC_TIMEOUT_REG:
        if (size == 4) s->reg_820c = val;
        break;
    case 0x8128:
        if (size == 4) s->reg_8128 = val;
        break;
    case 0x812C:
        if (size == 4) s->reg_812c = val;
        break;
    case 0x8134:
        if (size == 2) s->reg_8134 = val;
        break;
    case 0x8136:
        if (size == 2) s->reg_8136 = val;
        break;
    default:
        if (addr < MMIO_BAR_SIZE) {
            memcpy(s->mmio_data + addr, &val, size);
            return;
        }
        qemu_log_mask(LOG_UNIMP, "savagefb: mmio write out of range: 0x%" HWADDR_PRIx " size %d\n", addr, size);
        return;
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    memset(s->mmio_data, 0, sizeof(s->mmio_data));

    s->crtc_index = 0;
    s->seq_index = 0;
    s->gr_index = 0;
    s->attr_flipflop = false;
    s->attr_index = 0;
    s->misc_reg = 0;
    s->dac_write_index = 0;
    s->dac_read_index = 0;
    memset(s->crtc_regs, 0, sizeof(s->crtc_regs));
    memset(s->seq_regs, 0, sizeof(s->seq_regs));
    memset(s->gr_regs, 0, sizeof(s->gr_regs));
    memset(s->attr_regs, 0, sizeof(s->attr_regs));

    /* Set CR36 to 0x20 (8 MB framebuffer) to match FB_BAR_SIZE and driver detection */
    s->crtc_regs[0x36] = 0x20;
    s->crtc_regs[0x68] = 0x00;
    s->seq_regs[0x08] = 0x00;
    s->seq_regs[0x10] = 0x00;
    s->seq_regs[0x11] = 0x00;

    s->reg_48c00 = 0x00080000;
    s->reg_48c60 = 0x00a00000;

    s->reg_8200 = 0;
    s->reg_8204 = 0;
    s->reg_8208 = 0;
    s->reg_820c = 0;
    s->reg_8128 = 0;
    s->reg_812c = 0;
    s->reg_8134 = 0;
    s->reg_8136 = 0;
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x5333);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x8c22);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x030000);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);

    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    memory_region_init_io(&s->bar_mmio, OBJECT(s), &pcibase_mmio_ops, s, "savagefb-mmio", MMIO_BAR_SIZE);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_mmio);

    memory_region_init_ram(&s->bar_fb, OBJECT(s), "savagefb-fb", FB_BAR_SIZE, errp);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY | PCI_BASE_ADDRESS_MEM_PREFETCH, &s->bar_fb);

    pcibase_reset(DEVICE(s));
}

static void pcibase_uninit(PCIDevice *pdev)
{
}

static const VMStateDescription vmstate_pcibase = {
    .name = "savagefb_pci",
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
