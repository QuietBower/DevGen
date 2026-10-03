/*
 * QEMU 8.2.10 virtual PCI device for bochs-drm driver.
 * This device model implements the MMIO interface expected by the bochs-drm
 * driver, including VBE DISPI registers, VGA register passthrough, EDID area,
 * and extension registers.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/module.h"
#include "qemu/log.h"
#include "qemu/host-utils.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/pci/pcie.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"

#define TYPE_PCIBASE_DEVICE "bochs_drm_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID      0x1234
#define DEVICE_ID      0x1111
#define CLASS_ID       0x0300  /* VGA compatible controller */

/* VBE DISPI index register definitions */
#define VBE_DISPI_INDEX_ID               0x0
#define VBE_DISPI_INDEX_XRES             0x1
#define VBE_DISPI_INDEX_YRES             0x2
#define VBE_DISPI_INDEX_BPP              0x3
#define VBE_DISPI_INDEX_ENABLE           0x4
#define VBE_DISPI_INDEX_BANK             0x5
#define VBE_DISPI_INDEX_VIRT_WIDTH       0x6
#define VBE_DISPI_INDEX_VIRT_HEIGHT      0x7
#define VBE_DISPI_INDEX_X_OFFSET         0x8
#define VBE_DISPI_INDEX_Y_OFFSET         0x9
#define VBE_DISPI_INDEX_VIDEO_MEMORY_64K 0xa

/* VBE DISPI ID values */
#define VBE_DISPI_ID0                    0xB0C0
#define VBE_DISPI_ID1                    0xB0C1
#define VBE_DISPI_ID2                    0xB0C2
#define VBE_DISPI_ID3                    0xB0C3
#define VBE_DISPI_ID4                    0xB0C4
#define VBE_DISPI_ID5                    0xB0C5

/* VBE DISPI enable flags */
#define VBE_DISPI_DISABLED               0x00
#define VBE_DISPI_ENABLED                0x01
#define VBE_DISPI_GETCAPS                0x02
#define VBE_DISPI_8BIT_DAC               0x20
#define VBE_DISPI_LFB_ENABLED            0x40
#define VBE_DISPI_NOCLEARMEM             0x80

/* I/O ports for VBE DISPI (kept for reference, not used in MMIO mode) */
#define VBE_DISPI_IOPORT_INDEX           0x01CE
#define VBE_DISPI_IOPORT_DATA            0x01CF

/* Framebuffer and register region sizes */
#define FB_SIZE       (16 * MiB)  /* Framebuffer size */
#define MMIO_BAR_SIZE 0x1000      /* Size of BAR2 covering registers */

/* MMIO register offsets (within BAR2) */
#define VBE_DISPI_MMIO_BASE  0x500  /* Start of VBE registers in MMIO bar */
#define QEXT_SIZE_OFFSET     0x600  /* Offset of qext_size register */

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

    /* Power management state (D0-D3) */
    uint32_t pm_state;

    /* Hardware Register Shadows */
    uint16_t vbe_index;               /* current VBE index register */
    uint16_t vbe_regs[0x10];         /* VBE indexed registers */
};

/* MMIO/PIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* EDID area: returns 0 (no EDID) */
    if (addr < 0x400) {
        return 0;
    }

    /* VGA register passthrough: just return 0 for reads */
    if (addr >= 0x400 && addr < 0x500) {
        return 0;
    }

    /* VBE DISPI registers */
    if (addr >= VBE_DISPI_MMIO_BASE && addr < 0x600) {
        if (size != 2 || (addr & 1)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: bad access at addr 0x%" HWADDR_PRIx " size %u\n",
                          __func__, addr, size);
            return ~0ULL;
        }
        unsigned reg = (addr - VBE_DISPI_MMIO_BASE) >> 1;
        if (reg >= 0x10) {
            return ~0ULL;
        }
        return s->vbe_regs[reg];
    }

    /* qext_size register */
    if (addr == QEXT_SIZE_OFFSET && size == 4) {
        return 8;  /* report 8 bytes of extension */
    }

    /* endian register (0x604) */
    if (addr == 0x604 && size == 4) {
        return 0;  /* writes only, read as 0 */
    }

    qemu_log_mask(LOG_UNIMP, "%s: unimplemented read addr 0x%" HWADDR_PRIx " size %u\n",
                  __func__, addr, size);
    return ~0ULL;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* EDID area: read-only, ignore writes */
    if (addr < 0x400) {
        return;
    }

    /* VGA register passthrough: ignore writes */
    if (addr >= 0x400 && addr < 0x500) {
        return;
    }

    /* VBE DISPI registers */
    if (addr >= VBE_DISPI_MMIO_BASE && addr < 0x600) {
        if (size != 2 || (addr & 1)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: bad access at addr 0x%" HWADDR_PRIx " size %u\n",
                          __func__, addr, size);
            return;
        }
        unsigned reg = (addr - VBE_DISPI_MMIO_BASE) >> 1;
        if (reg >= 0x10) {
            return;
        }
        s->vbe_regs[reg] = val;
        return;
    }

    /* qext_size register: read-only, ignore */
    if (addr == QEXT_SIZE_OFFSET) {
        return;
    }

    /* endian register (0x604) */
    if (addr == 0x604 && size == 4) {
        /* ignore writes; endianness handled internally by QEMU */
        return;
    }

    qemu_log_mask(LOG_UNIMP, "%s: unimplemented write addr 0x%" HWADDR_PRIx " val 0x%" PRIx64 " size %u\n",
                  __func__, addr, val, size);
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

    /* Reset VBE registers to power-on defaults */
    s->vbe_index = 0;
    memset(s->vbe_regs, 0, sizeof(s->vbe_regs));
    s->vbe_regs[VBE_DISPI_INDEX_ID] = VBE_DISPI_ID5;  /* Valid ID */
    s->vbe_regs[VBE_DISPI_INDEX_VIDEO_MEMORY_64K] = FB_SIZE / (64 * 1024);
    s->vbe_regs[VBE_DISPI_INDEX_ENABLE] = VBE_DISPI_DISABLED;
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
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, aligned_size);
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
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x02);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR Initialization */
    s->num_bars = 2;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_RAM, .size = FB_SIZE, .name = "bochs-fb" };
    s->bar_info[1] = (BARInfo){ .index = 2, .type = BAR_TYPE_MMIO, .size = MMIO_BAR_SIZE, .name = "bochs-mmio" };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    /* No special cleanup needed */
}

/* VMState: include VBE registers for migration */
static const VMStateDescription vmstate_pcibase = {
    .name = "bochs_drm_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT16(vbe_index, PCIBaseState),
        VMSTATE_UINT16_ARRAY(vbe_regs, PCIBaseState, 0x10),
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
