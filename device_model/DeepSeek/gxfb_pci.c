/*
 * QEMU virtual device model for AMD Geode GX video framebuffer
 * Based on Linux driver gxfb_core.c
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

#define TYPE_PCIBASE_DEVICE "gxfb_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI IDs from Linux driver (first entry in pci_device_id table) */
#define VENDOR_ID 0x100b  /* PCI_VENDOR_ID_NS */
#define DEVICE_ID 0x0030  /* PCI_DEVICE_ID_NS_GX_VIDEO */
#define CLASS_ID PCI_CLASS_DISPLAY_VGA

/* Register counts (matching gxfb_core.c) */
#define DC_REG_COUNT   (0x90 / 4)
#define DC_PAL_COUNT   0x104
#define GP_REG_COUNT   (0x50 / 4)
#define FP_REG_COUNT   (0x68 / 8)
#define VP_REG_COUNT   (0x138 / 8)
#define VP_FP_START    0x400

/* Hardware register arrays (shadows) */
typedef struct {
    /* Shadow copies of all device registers */
    uint32_t dc_regs[DC_REG_COUNT];   /* 0x90 bytes, 32-bit each */
    uint32_t gp_regs[GP_REG_COUNT];   /* 0x50 bytes, 32-bit each */
    uint64_t vp_regs[VP_REG_COUNT];   /* 0x138 bytes, 64-bit each */
    uint64_t fp_regs[FP_REG_COUNT];   /* 0x68 bytes, 64-bit each */
    uint32_t pal[DC_PAL_COUNT];       /* 0x104 entries, 32-bit each */
} HardwareRegs;

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
    HardwareRegs regs;

    /* Power management state */
    uint32_t pm_state; /* 0 = D0, 3 = D3 */

    /* Palette index for DC PAL registers */
    uint32_t dc_pal_index;
};

/* Internal MMIO handlers (dispatched per BAR) */
static uint64_t pcibase_mmio_read_internal(PCIBaseState *s, int bar, hwaddr addr, unsigned size)
{
    uint64_t val = 0;

    switch (bar) {
    case 1: /* Graphics Processor (GP) */
        if (addr < 0x80 && size == 4) {
            int reg = addr >> 2;
            if (reg < GP_REG_COUNT) {
                val = s->regs.gp_regs[reg];
            }
        }
        break;
    case 2: /* Display Controller (DC) */
        if (size == 4) {
            if (addr < 0x100) {
                int reg = addr >> 2;
                if (reg < DC_REG_COUNT) {
                    val = s->regs.dc_regs[reg];
                }
            } else if (addr == 0x100) { /* DC_PAL_ADDRESS */
                val = s->dc_pal_index;
            } else if (addr == 0x104) { /* DC_PAL_DATA */
                val = s->regs.pal[s->dc_pal_index];
                /* auto-increment index */
                s->dc_pal_index = (s->dc_pal_index + 1) % DC_PAL_COUNT;
            }
        }
        break;
    case 3: /* Video Processor (VP and FP) */
        if (size == 4) {
            if (addr < VP_FP_START) {
                int reg = addr >> 3;  /* 8 bytes per VP register */
                if (reg < VP_REG_COUNT) {
                    val = (uint32_t)s->regs.vp_regs[reg];
                }
            } else if (addr >= VP_FP_START && addr < VP_FP_START + 0x68) {
                int reg = (addr - VP_FP_START) >> 3;
                if (reg < FP_REG_COUNT) {
                    val = (uint32_t)s->regs.fp_regs[reg];
                }
            }
        }
        break;
    default:
        break;
    }
    return val;
}

static void pcibase_mmio_write_internal(PCIBaseState *s, int bar, hwaddr addr,
                                        uint64_t val, unsigned size)
{
    switch (bar) {
    case 1: /* GP */
        if (addr < 0x80 && size == 4) {
            int reg = addr >> 2;
            if (reg < GP_REG_COUNT) {
                s->regs.gp_regs[reg] = val;
            }
        }
        break;
    case 2: /* DC */
        if (size == 4) {
            if (addr < 0x100) {
                int reg = addr >> 2;
                if (reg < DC_REG_COUNT) {
                    s->regs.dc_regs[reg] = val;
                }
            } else if (addr == 0x100) { /* DC_PAL_ADDRESS */
                s->dc_pal_index = val;
            } else if (addr == 0x104) { /* DC_PAL_DATA */
                s->regs.pal[s->dc_pal_index] = val;
                /* auto-increment index */
                s->dc_pal_index = (s->dc_pal_index + 1) % DC_PAL_COUNT;
            }
        }
        break;
    case 3: /* VID */
        if (size == 4) {
            if (addr < VP_FP_START) {
                int reg = addr >> 3;
                if (reg < VP_REG_COUNT) {
                    s->regs.vp_regs[reg] = (s->regs.vp_regs[reg] & 0xFFFFFFFF00000000ULL) | val;
                }
            } else if (addr >= VP_FP_START && addr < VP_FP_START + 0x68) {
                int reg = (addr - VP_FP_START) >> 3;
                if (reg < FP_REG_COUNT) {
                    s->regs.fp_regs[reg] = (s->regs.fp_regs[reg] & 0xFFFFFFFF00000000ULL) | val;
                }
            }
        }
        break;
    default:
        break;
    }
}

/* BAR-specific read/write stubs */
static uint64_t pcibase_gp_read(void *opaque, hwaddr addr, unsigned size)
{
    return pcibase_mmio_read_internal(PCIBASE_DEVICE(opaque), 1, addr, size);
}

static void pcibase_gp_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    pcibase_mmio_write_internal(PCIBASE_DEVICE(opaque), 1, addr, val, size);
}

static uint64_t pcibase_dc_read(void *opaque, hwaddr addr, unsigned size)
{
    return pcibase_mmio_read_internal(PCIBASE_DEVICE(opaque), 2, addr, size);
}

static void pcibase_dc_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    pcibase_mmio_write_internal(PCIBASE_DEVICE(opaque), 2, addr, val, size);
}

static uint64_t pcibase_vid_read(void *opaque, hwaddr addr, unsigned size)
{
    return pcibase_mmio_read_internal(PCIBASE_DEVICE(opaque), 3, addr, size);
}

static void pcibase_vid_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    pcibase_mmio_write_internal(PCIBASE_DEVICE(opaque), 3, addr, val, size);
}

/* Ops definitions for each BAR */
static const MemoryRegionOps pcibase_gp_ops = {
    .read = pcibase_gp_read,
    .write = pcibase_gp_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static const MemoryRegionOps pcibase_dc_ops = {
    .read = pcibase_dc_read,
    .write = pcibase_dc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static const MemoryRegionOps pcibase_vid_ops = {
    .read = pcibase_vid_read,
    .write = pcibase_vid_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Initialize all registers to zero */
    memset(&s->regs, 0, sizeof(s->regs));
    s->pm_state = 0;
    s->dc_pal_index = 0;
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi,
                                 const MemoryRegionOps *ops, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }
    
    /* CRITICAL: PCI requires BAR sizes to be a power of 2. Prevent QEMU assert crash. */
    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), ops, s, bi->name, aligned_size);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 0); /* no interrupt used */

    /* BAR Initialization: 4 BARs as used by driver */
    s->num_bars = 4;

    /* BAR0: Video memory (framebuffer RAM) */
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_RAM, .size = 16 * MiB, .name = "gxfb-framebuffer" };
    /* BAR1: Graphics processor registers (GP) */
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_MMIO, .size = 0x80, .name = "gxfb-gp" };
    /* BAR2: Display controller registers (DC), expanded to include palette */
    s->bar_info[2] = (BARInfo){ .index = 2, .type = BAR_TYPE_MMIO, .size = 0x200, .name = "gxfb-dc" };
    /* BAR3: Video processor registers (VP and FP) */
    s->bar_info[3] = (BARInfo){ .index = 3, .type = BAR_TYPE_MMIO, .size = 0x800, .name = "gxfb-vid" };

    for (int i = 0; i < s->num_bars; i++) {
        const MemoryRegionOps *ops = NULL;
        if (s->bar_info[i].type == BAR_TYPE_MMIO) {
            if (i == 1) {
                ops = &pcibase_gp_ops;
            } else if (i == 2) {
                ops = &pcibase_dc_ops;
            } else if (i == 3) {
                ops = &pcibase_vid_ops;
            }
        }
        pcibase_register_bar(pdev, s, &s->bar_info[i], ops, errp);
    }

    /* Final state initialization */
    pcibase_reset(DEVICE(pdev));
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

    /* No additional cleanup */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "gxfb_pci",
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