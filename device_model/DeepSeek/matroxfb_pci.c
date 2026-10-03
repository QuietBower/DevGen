/*
 * Matroxfb Virtual PCI Device for QEMU 8.2.10
 * Based on matroxfb_base.c driver
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
#include "qemu/bswap.h"

#define TYPE_PCIBASE_DEVICE "matroxfb_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Vendor and Device IDs */
#define PCI_VENDOR_ID_MATROX        0x102B
#define PCI_DEVICE_ID_MATROX_MIL    0x0519
#define PCI_CLASS_DISPLAY_VGA       0x030000

/* Register Offsets */
#define M_DWGCTL                    0x1C00
#define M_MACCESS                   0x1C04
#define M_CTLWTST                   0x1C08
#define M_PLNWT                     0x1C1C
#define M_BCOL                      0x1C20
#define M_FCOL                      0x1C24
#define M_SGN                       0x1C58
#define M_LEN                       0x1C5C
#define M_AR0                       0x1C60
#define M_AR3                       0x1C6C
#define M_AR5                       0x1C74
#define M_CXBNDRY                   0x1C80
#define M_FXBNDRY                   0x1C84
#define M_YDSTLEN                   0x1C88
#define M_PITCH                     0x1C8C
#define M_YDST                      0x1C90
#define M_YDSTORG                   0x1C94
#define M_YTOP                      0x1C98
#define M_YBOT                      0x1C9C
#define M_EXEC                      0x0100
#define M_DWG_TRAP                  0x04
#define M_DWG_BITBLT                0x08
#define M_DWG_ILOAD                 0x09
#define M_DWG_LINEAR                0x0080
#define M_DWG_SOLID                 0x0800
#define M_DWG_ARZERO                0x1000
#define M_DWG_SGNZERO               0x2000
#define M_DWG_SHIFTZERO             0x4000
#define M_DWG_BMONOWF               0x08000000
#define M_DWG_BFCOL                 0x04000000
#define M_DWG_REPLACE               0x000C0000
#define M_DWG_REPLACE2              (M_DWG_REPLACE | 0x40)
#define M_DWG_TRANSC                0x40000000
#define M_FIFOSTATUS                0x1E10
#define M_STATUS                    0x1E14
#define M_ICLEAR                    0x1E18
#define M_IEN                       0x1E1C
#define M_VCOUNT                    0x1E20
#define M_RESET                     0x1E40
#define M_MEMRDBK                   0x1E44
#define M_OPMODE                    0x1E54
#define M_MISC_REG                  0x1FC2
#define M_SEQ_INDEX                 0x1FC4
#define M_SEQ_DATA                  0x1FC5
#define M_GRAPHICS_INDEX            0x1FCE
#define M_GRAPHICS_DATA             0x1FCF
#define M_ATTR_INDEX                0x1FC0
#define M_ATTR_RESET                0x1FDA
#define M_INSTS1                    0x1FDA
#define M_MISC_REG_READ             0x1FCC
#define M_EXTVGA_INDEX              0x1FDE
#define M_EXTVGA_DATA               0x1FDF
#define M_CACHEFLUSH                0x1FFF
#define M_RAMDAC_BASE               0x3C00
#define M_DAC_REG                   (M_RAMDAC_BASE+0)
#define M_DAC_VAL                   (M_RAMDAC_BASE+1)
#define M_PALETTE_MASK              (M_RAMDAC_BASE+2)

/* PCI Option Registers */
#define PCI_OPTION_REG              0x40
#define PCI_OPTION2_REG             0x50
#define PCI_OPTION3_REG             0x54
#define PCI_MEMMISC_REG             0x58
#define PCI_MGA_INDEX               0x44
#define PCI_ROM_ADDR                0x30

/* Chip types */
enum mga_chip {
    MGA_2064,
    MGA_2164,
    MGA_1064,
    MGA_1164,
    MGA_G100,
    MGA_G200,
    MGA_G400,
    MGA_G450,
    MGA_G550
};

/* Driver flags */
#define DEVF_TEXT4B                 0x0020
#define DEVF_SWAPS                  0x0002
#define DEVF_VIDEO64BIT             0x0001
#define DEVF_CROSS4MB               0x0010
#define DEVF_G100                   (DEVF_GCORE)
#define DEVF_G200                   (DEVF_G2CORE | DEVF_SUPPORT32MB | DEVF_TEXT16B | DEVF_CRTC2)
#define DEVF_G400                   (DEVF_G2CORE | DEVF_SUPPORT32MB | DEVF_TEXT16B | DEVF_CRTC2)
#define DEVF_G450                   (DEVF_GCORE | DEVF_ANY_VXRES | DEVF_SUPPORT32MB | DEVF_TEXT16B | DEVF_CRTC2 | DEVF_G450DAC | DEVF_SRCORG | DEVF_DUALHEAD)
#define DEVF_G550                   (DEVF_G450)


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

    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;

    uint8_t regs[0x4000];

    uint32_t status;
    int reset_counter;

    /* Custom PCI config registers */
    uint32_t option_reg;
    uint32_t option2_reg;
    uint32_t option3_reg;
    uint32_t memmisc_reg;
    uint32_t mga_index;
    uint32_t rom_address;

    /* EXTVGA indirect registers */
    uint8_t extvga_index;
    uint8_t extvga_regs[256];

    /* DAC palette */
    uint8_t dac_index;
    uint8_t dac_palette[256][3];

    /* Vsync timer */
    QEMUTimer vsync_timer;
};

static void pcibase_vsync_timer(void *opaque)
{
    PCIBaseState *s = opaque;
    s->intr_status |= 0x20; /* CRTC1 vsync */
    if (s->intr_status & s->intr_mask) {
        pci_set_irq(PCI_DEVICE(s), 1);
    }
    timer_mod(&s->vsync_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 16);
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case M_STATUS:
        if (size == 4) val = s->intr_status;
        break;
    case M_IEN:
        if (size == 4) val = s->intr_mask;
        break;
    case M_ICLEAR:
        /* Write-only, read returns 0 */
        break;
    case M_VCOUNT:
        if (size == 4) val = 0; /* Not implemented */
        break;
    case M_INSTS1:
        if (size == 1) val = 0;
        break;
    case M_CACHEFLUSH:
        /* Write-only */
        break;
    case M_EXTVGA_INDEX:
        if (size == 1) val = s->extvga_index;
        break;
    case M_EXTVGA_DATA:
        if (size == 1) val = s->extvga_regs[s->extvga_index];
        break;
    case M_DAC_REG:
        if (size == 1) val = s->dac_index;
        break;
    case M_DAC_VAL:
        if (size == 1) val = s->dac_palette[s->dac_index][0];
        break;
    case M_PALETTE_MASK:
        if (size == 1) val = 0; /* Not implemented */
        break;
    default:
        /* Generic access to flat register space */
        if (addr + size <= sizeof(s->regs)) {
            switch (size) {
            case 1: val = s->regs[addr]; break;
            case 2: val = lduw_le_p(&s->regs[addr]); break;
            case 4: val = ldl_le_p(&s->regs[addr]); break;
            case 8: val = ldq_le_p(&s->regs[addr]); break;
            }
        }
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    switch (addr) {
    case M_ICLEAR:
        if (size == 4) {
            s->intr_status &= ~val;
            if (!(s->intr_status & s->intr_mask)) {
                pci_set_irq(PCI_DEVICE(s), 0);
            }
        }
        break;
    case M_IEN:
        if (size == 4) {
            s->intr_mask = val;
            if (s->intr_status & s->intr_mask) {
                pci_set_irq(PCI_DEVICE(s), 1);
            } else {
                pci_set_irq(PCI_DEVICE(s), 0);
            }
        }
        break;
    case M_CACHEFLUSH:
        /* Ignore */
        break;
    case M_EXTVGA_INDEX:
        if (size == 1) s->extvga_index = val & 0xFF;
        break;
    case M_EXTVGA_DATA:
        if (size == 1) s->extvga_regs[s->extvga_index] = val & 0xFF;
        break;
    case M_DAC_REG:
        if (size == 1) s->dac_index = val & 0xFF;
        break;
    case M_DAC_VAL:
        if (size == 1) {
            s->dac_palette[s->dac_index][0] = val & 0xFF;
            s->dac_index = (s->dac_index + 1) % 256;
        }
        break;
    case M_PALETTE_MASK:
        /* Ignore */
        break;
    default:
        if (addr + size <= sizeof(s->regs)) {
            switch (size) {
            case 1: s->regs[addr] = val; break;
            case 2: stw_le_p(&s->regs[addr], val); break;
            case 4: stl_le_p(&s->regs[addr], val); break;
            case 8: stq_le_p(&s->regs[addr], val); break;
            }
        }
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0xFFFFFFFF;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
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

static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint32_t val = 0;

    /* Handle custom config registers */
    if (addr >= PCI_ROM_ADDR && addr < PCI_ROM_ADDR + 4) {
        val = s->rom_address;
    } else if (addr >= PCI_OPTION_REG && addr < PCI_OPTION_REG + 4) {
        val = s->option_reg;
    } else if (addr >= PCI_MGA_INDEX && addr < PCI_MGA_INDEX + 4) {
        val = s->mga_index;
    } else if (addr >= PCI_OPTION2_REG && addr < PCI_OPTION2_REG + 4) {
        val = s->option2_reg;
    } else if (addr >= PCI_OPTION3_REG && addr < PCI_OPTION3_REG + 4) {
        val = s->option3_reg;
    } else if (addr >= PCI_MEMMISC_REG && addr < PCI_MEMMISC_REG + 4) {
        val = s->memmisc_reg;
    } else {
        val = pci_default_read_config(pdev, addr, len);
    }
    return val;
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (addr >= PCI_ROM_ADDR && addr < PCI_ROM_ADDR + 4) {
        if (len == 4) {
            s->rom_address = val;
        } else {
            uint32_t mask = (1 << (8 * len)) - 1;
            s->rom_address = (s->rom_address & ~mask) | (val & mask);
        }
    } else if (addr >= PCI_OPTION_REG && addr < PCI_OPTION_REG + 4) {
        if (len == 4) {
            s->option_reg = val;
        } else {
            uint32_t mask = (1 << (8 * len)) - 1;
            s->option_reg = (s->option_reg & ~mask) | (val & mask);
        }
    } else if (addr >= PCI_MGA_INDEX && addr < PCI_MGA_INDEX + 4) {
        if (len == 4) {
            s->mga_index = val;
        } else {
            uint32_t mask = (1 << (8 * len)) - 1;
            s->mga_index = (s->mga_index & ~mask) | (val & mask);
        }
    } else if (addr >= PCI_OPTION2_REG && addr < PCI_OPTION2_REG + 4) {
        if (len == 4) {
            s->option2_reg = val;
        } else {
            uint32_t mask = (1 << (8 * len)) - 1;
            s->option2_reg = (s->option2_reg & ~mask) | (val & mask);
        }
    } else if (addr >= PCI_OPTION3_REG && addr < PCI_OPTION3_REG + 4) {
        if (len == 4) {
            s->option3_reg = val;
        } else {
            uint32_t mask = (1 << (8 * len)) - 1;
            s->option3_reg = (s->option3_reg & ~mask) | (val & mask);
        }
    } else if (addr >= PCI_MEMMISC_REG && addr < PCI_MEMMISC_REG + 4) {
        if (len == 4) {
            s->memmisc_reg = val;
        } else {
            uint32_t mask = (1 << (8 * len)) - 1;
            s->memmisc_reg = (s->memmisc_reg & ~mask) | (val & mask);
        }
    } else {
        pci_default_write_config(pdev, addr, val, len);
    }
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    memset(s->regs, 0, sizeof(s->regs));
    s->intr_status = 0;
    s->intr_mask = 0;
    s->option_reg = 0;
    s->option2_reg = 0;
    s->option3_reg = 0;
    s->memmisc_reg = 0;
    s->mga_index = 0;
    s->rom_address = 0;
    s->extvga_index = 0;
    memset(s->extvga_regs, 0, sizeof(s->extvga_regs));
    s->dac_index = 0;
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_MATROX);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_MATROX_MIL);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_DISPLAY_VGA);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    s->num_bars = 2;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = 0x4000, .name = "mmio" };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_RAM, .size = 0x800000, .name = "framebuffer" };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    timer_init_ms(&s->vsync_timer, QEMU_CLOCK_VIRTUAL, pcibase_vsync_timer, s);
    timer_mod(&s->vsync_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 16);

    memset(s->regs, 0, sizeof(s->regs));
    s->intr_status = 0;
    s->intr_mask = 0;
    s->option_reg = 0;
    s->option2_reg = 0;
    s->option3_reg = 0;
    s->memmisc_reg = 0;
    s->mga_index = 0;
    s->rom_address = 0;
    s->extvga_index = 0;
    memset(s->extvga_regs, 0, sizeof(s->extvga_regs));
    s->dac_index = 0;
    memset(s->dac_palette, 0, sizeof(s->dac_palette));
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    timer_del(&s->vsync_timer);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

static const VMStateDescription vmstate_pcibase = {
    .name = "matroxfb_pci",
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
