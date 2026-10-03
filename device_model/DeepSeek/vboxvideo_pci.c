/*
 * This template provides a robust skeleton for hardware emulation.
 * Integrated QEMU PCI device template (QEMU 8.2.10).
 * Replace #PLACEHOLDER# blocks with driver-specific definitions.
 * Designed for register-level modeling and PCIe driver probing.
 * NOTE: Do not directly fill this file with the Linux kernel source code.
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

/* Additional include files retrieved from driver context */

#define TYPE_PCIBASE_DEVICE "vboxvideo_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x80ee
#define DEVICE_ID 0xbeef
#define CLASS_ID 0x0000 /* Unknown, needs definition */
#define VBE_DISPI_IOPORT_INDEX 0x01CE
#define VBE_DISPI_IOPORT_DATA 0x01CF
#define VGA_PORT_HGSMI_GUEST 0x3d0

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

    /* VBE I/O Port Registers */
    MemoryRegion vbe_io;         /* I/O region for 0x1CE-0x1CF */
    uint16_t dispi_index;        /* Index register (port 0x1CE) */
    uint16_t dispi_id;           /* ID register value (index 0) */
    uint32_t vram_size;          /* VRAM size reported via VBE */

    /* HGSMI port */
    MemoryRegion hgsmi_io;       /* HGSMI guest port at 0x3d0 */
};

#define VBE_DISPI_INDEX_ID               0x0
#define VBE_DISPI_ID_HGSMI               0xBE01
#define VBE_DISPI_ID_ANYX                0xBE02
#define VBE_DISPI_MAX_XRES               16384
#define VBE_DISPI_MAX_YRES               16384
#define VBVA_ADAPTER_INFORMATION_SIZE    65536
#define VBVA_MIN_BUFFER_SIZE             65536
#define VBVA_MAX_RECORDS                 64
#define VBVA_F_ENABLE                    0x00000001
#define VBVA_F_DISABLE                   0x00000002
#define VBVA_F_EXTENDED                  0x00000004
#define VBVA_F_ABSOFFSET                 0x00000008
#define VBVA_ENABLE                      7
#define VBVA_SCREEN_F_ACTIVE             0x0001
#define VBVA_SCREEN_F_BLANK              0x0004
#define VBVA_SCREEN_F_DISABLED           0x0002
#define VBVAMODEHINT_MAGIC               0x0801add9u
#define VBVA_QUERY_CONF32                1
#define VBVA_INFO_SCREEN                 6
#define VBVA_QUERY_MODE_HINTS            19
#define VBOX_MAX_SCREENS                 32
#define VBOX_MAX_CURSOR_WIDTH            64
#define VBOX_MAX_CURSOR_HEIGHT           64
#define CURSOR_PIXEL_COUNT               (VBOX_MAX_CURSOR_WIDTH * VBOX_MAX_CURSOR_HEIGHT)
#define CURSOR_DATA_SIZE                 (CURSOR_PIXEL_COUNT * 4 + CURSOR_PIXEL_COUNT / 8)
#define GUEST_HEAP_SIZE                  VBVA_ADAPTER_INFORMATION_SIZE
#define GUEST_HEAP_OFFSET(vbox)          ((vbox)->full_vram_size - VBVA_ADAPTER_INFORMATION_SIZE)
#define GUEST_HEAP_USABLE_SIZE           (VBVA_ADAPTER_INFORMATION_SIZE - sizeof(struct hgsmi_host_flags))
#define VBOX_VBVA_CONF32_MONITOR_COUNT   0
#define VBOX_VBVA_CONF32_MODE_HINT_REPORTING 2
#define VBOX_VBVA_CONF32_GUEST_CURSOR_REPORTING 3
#define VBOX_VBVA_CONF32_CURSOR_CAPABILITIES 4
#define VBOX_VBVA_CURSOR_CAPABILITY_HARDWARE BIT(1)

/* VBE I/O Port Handlers */
static uint64_t vbe_ioport_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = ~0ULL;

    switch (addr) {
    case 0: /* Index register (0x1CE) */
        val = s->dispi_index;
        break;
    case 1: /* Data register (0x1CF) */
        switch (s->dispi_index) {
        case VBE_DISPI_INDEX_ID:
            val = s->dispi_id;
            break;
        /* VRAM size register index undefined, handled by needed_sources */
        default:
            val = 0;
            break;
        }
        break;
    default:
        break;
    }

    return val;
}

static void vbe_ioport_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case 0: /* Index register (0x1CE) */
        s->dispi_index = (uint16_t)val;
        break;
    case 1: /* Data register (0x1CF) */
        if (s->dispi_index == VBE_DISPI_INDEX_ID) {
            s->dispi_id = (uint16_t)val;
        }
        /* Other registers may be writable but not required for basic probing */
        break;
    }
}

static const MemoryRegionOps vbe_ioport_ops = {
    .read = vbe_ioport_read,
    .write = vbe_ioport_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* HGSMI I/O Port Handlers (Placeholder) */
static uint64_t hgsmi_ioport_read(void *opaque, hwaddr addr, unsigned size)
{
    /* Placeholder: will implement HGSMI command processing after obtaining submit details */
    return ~0ULL;
}

static void hgsmi_ioport_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Placeholder: will implement HGSMI command processing after obtaining submit details */
}

static const MemoryRegionOps hgsmi_ioport_ops = {
    .read = hgsmi_ioport_read,
    .write = hgsmi_ioport_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* Placeholder MMIO/PIO handlers, currently unused */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
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

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Reset VBE state */
    s->dispi_index = 0;
    s->dispi_id = VBE_DISPI_ID_HGSMI; /* Default ID */
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

    /* Initialize BAR0 as VRAM (RAM type) */
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo){
        .index = 0,
        .type = BAR_TYPE_RAM,
        .size = s->vram_size,
        .name = "vram"
    };

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Register VBE I/O ports at fixed legacy addresses */
    memory_region_init_io(&s->vbe_io, OBJECT(s), &vbe_ioport_ops, s, "vbe-io", 2);
    memory_region_add_subregion(get_system_io(), VBE_DISPI_IOPORT_INDEX, &s->vbe_io);

    /* Register HGSMI port */
    memory_region_init_io(&s->hgsmi_io, OBJECT(s), &hgsmi_ioport_ops, s, "hgsmi-io", 4);
    memory_region_add_subregion(get_system_io(), VGA_PORT_HGSMI_GUEST, &s->hgsmi_io);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    memory_region_del_subregion(get_system_io(), &s->vbe_io);
    memory_region_unref(&s->vbe_io);

    memory_region_del_subregion(get_system_io(), &s->hgsmi_io);
    memory_region_unref(&s->hgsmi_io);

    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }

    /* No dynamic resources to free */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "vboxvideo_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_END_OF_LIST()
    }
};

static Property pcibase_properties[] = {
    DEFINE_PROP_UINT32("vram_size", PCIBaseState, vram_size, 32 * MiB),
    DEFINE_PROP_END_OF_LIST(),
};

static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pcibase_realize;
    k->exit    = pcibase_uninit;
    dc->reset  = pcibase_reset;
    dc->vmsd   = &vmstate_pcibase;
    dc->props  = pcibase_properties;
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
