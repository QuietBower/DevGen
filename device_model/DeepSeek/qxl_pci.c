/*
 * QEMU QXL PCI device model (functional implementation)
 * This is an incremental update based on the provided driver source.
 * Missing symbols are listed in needed_sources.
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

#define TYPE_PCIBASE_DEVICE "qxl_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x1b36
#define DEVICE_ID 0x0100
#define CLASS_ID 0x0300

/* QXL hardware constants from driver */
#define QXL_INTERRUPT_MASK (\
	QXL_INTERRUPT_DISPLAY |\
	QXL_INTERRUPT_CURSOR |\
	QXL_INTERRUPT_IO_CMD |\
	QXL_INTERRUPT_CLIENT_MONITORS_CONFIG)
#define QXL_INTERRUPT_DISPLAY (1 << 0)
#define QXL_INTERRUPT_CURSOR (1 << 1)
#define QXL_INTERRUPT_IO_CMD (1 << 2)
#define QXL_INTERRUPT_CLIENT_MONITORS_CONFIG  (1 << 5)

/* IO port offsets used by the driver - need exact values from driver source */
#define QXL_IO_RESET            0   /* needed_source: define:QXL_IO_RESET */
#define QXL_IO_NOTIFY_CMD       1   /* needed_source: define:QXL_IO_NOTIFY_CMD */
#define QXL_IO_NOTIFY_CURSOR    2   /* needed_source: define:QXL_IO_NOTIFY_CURSOR */
#define QXL_IO_UPDATE_IRQ       3   /* needed_source: define:QXL_IO_UPDATE_IRQ */

/* ROM and RAM constants */
#define QXL_ROM_SIZE            0x1000    /* 4KB */
#define QXL_VRAM_SIZE           (16 * MiB)
#define QXL_SURFACE0_AREA_SIZE  (4 * MiB) /* typical default */

/* QXL ROM magic from driver source */
#define QXL_ROM_MAGIC 0x4f525851

/* Type definitions from driver source */
typedef uint64_t QXLPHYSICAL;

struct qxl_ring_header {
	uint32_t num_items;
	uint32_t prod;
	uint32_t notify_on_prod;
	uint32_t cons;
	uint32_t notify_on_cons;
};

struct qxl_command {
	QXLPHYSICAL data;
	uint32_t type;
	uint32_t padding;
};

struct qxl_rect {
	int32_t top;
	int32_t left;
	int32_t bottom;
	int32_t right;
};

struct qxl_mem_slot {
	uint64_t mem_start;
	uint64_t mem_end;
};

struct qxl_surface_create {
	uint32_t width;
	uint32_t height;
	int32_t stride;
	uint32_t format;
	uint32_t position;
	uint32_t mouse_mode;
	uint32_t flags;
	uint32_t type;
	QXLPHYSICAL mem;
};

/* BAR types and struct definition (missing from previous phase) */
typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM
} BARType;

typedef struct BARInfo {
    int index;
    BARType type;
    hwaddr size;
    const char *name;
} BARInfo;

/* Structs from driver source (partially defined) */
typedef struct QXLURect {
    uint32_t x, y, width, height;
} QXLURect;

typedef struct __attribute__((packed)) QXLRom {
    uint32_t magic;
    uint32_t id;
    uint32_t update_id;
    uint32_t compression_level;
    uint32_t log_level;
    uint32_t mode;
    uint32_t modes_offset;
    uint32_t num_io_pages;
    uint32_t pages_offset;
    uint32_t draw_area_offset;
    uint32_t surface0_area_size;
    uint32_t ram_header_offset;
    uint32_t mm_clock;
    /* appended for qxl-2 */
    uint32_t n_surfaces;
    uint64_t flags;
    uint8_t slots_start;
    uint8_t slots_end;
    uint8_t slot_gen_bits;
    uint8_t slot_id_bits;
    uint8_t slot_generation;
    /* appended for qxl-4 */
    uint8_t client_present;
    uint8_t client_capabilities[58];
    uint32_t client_monitors_config_crc;
    struct {
        uint16_t count;
        uint16_t padding;
        QXLURect heads[64];
    } client_monitors_config;
} QXLRom;

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;

    /* Device-specific state */
    uint32_t int_mask;       /* shadow of int_mask in ram_header */
    uint32_t int_pending;    /* shadow of int_pending in ram_header */
    MemoryRegion io_region;  /* BAR3: IO ports */

    /* VRAM and ROM */
    MemoryRegion vram_mr;        /* BAR0 */
    uint8_t *vram_data;          /* pointer to VRAM buffer */
    MemoryRegion ram_header_mr;  /* overlay for ram_header registers */
    uint32_t ram_header_offset;  /* from ROM */
};

/* Internal helper for status-triggered signaling */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->int_pending & s->int_mask) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* IO Port write handler (BAR3) */
static void pcibase_io_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case QXL_IO_RESET:
        /* Reset the device state */
        s->int_pending = 0;
        s->int_mask = 0;
        pci_set_irq(PCI_DEVICE(s), 0);
        break;
    case QXL_IO_NOTIFY_CMD:
        /* The driver uses this to signal a new command; triggers DISPLAY irq */
        s->int_pending |= QXL_INTERRUPT_DISPLAY;
        pcibase_update_irq(s);
        break;
    case QXL_IO_NOTIFY_CURSOR:
        /* Cursor ring update triggers CURSOR irq */
        s->int_pending |= QXL_INTERRUPT_CURSOR;
        pcibase_update_irq(s);
        break;
    case QXL_IO_UPDATE_IRQ:
        /* Re-send IRQ based on current pending */
        pcibase_update_irq(s);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "qxl: unimplemented IO write at 0x%" HWADDR_PRIx ", val=0x%" PRIx64 "\n", addr, val);
        break;
    }
}

static uint64_t pcibase_io_read(void *opaque, hwaddr addr, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "qxl: IO read from 0x%" HWADDR_PRIx "\n", addr);
    return 0;
}

static const MemoryRegionOps pcibase_io_ops = {
    .read = pcibase_io_read,
    .write = pcibase_io_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 1 },
    .impl = { .min_access_size = 1, .max_access_size = 1 },
};

/* Ram header overlay read handler */
static uint64_t pcibase_ram_header_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    uint32_t local_offset = addr;
    hwaddr vram_addr = s->ram_header_offset + local_offset;

    /* Forward standard reads to VRAM for all offsets */
    memcpy(&val, s->vram_data + vram_addr, MIN(size, sizeof(val)));
    return val;
}

/* Ram header overlay write handler */
static void pcibase_ram_header_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t local_offset = addr;
    hwaddr vram_addr = s->ram_header_offset + local_offset;

    /* Write-through to VRAM */
    switch (size) {
    case 1:
        s->vram_data[vram_addr] = (uint8_t)val;
        break;
    case 2:
        stw_le_p(s->vram_data + vram_addr, (uint16_t)val);
        break;
    case 4:
        stl_le_p(s->vram_data + vram_addr, (uint32_t)val);
        break;
    case 8:
        stq_le_p(s->vram_data + vram_addr, val);
        break;
    default:
        memcpy(s->vram_data + vram_addr, &val, size);
        break;
    }

    /* Handle register side-effects */
    switch (local_offset) {
    case 4: /* int_pending: write-1-to-clear */
        s->int_pending &= ~((uint32_t)val);
        pcibase_update_irq(s);
        break;
    case 8: /* int_mask */
        s->int_mask = (uint32_t)val;
        pcibase_update_irq(s);
        break;
    default:
        break;
    }
}

static const MemoryRegionOps pcibase_ram_header_ops = {
    .read = pcibase_ram_header_read,
    .write = pcibase_ram_header_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Revert registers to power-on defaults */
    s->int_mask = 0;
    s->int_pending = 0;
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
        /* Not used currently */
        memory_region_init_io(mr, OBJECT(s), NULL, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_io_ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

static void pcibase_init_rom(PCIBaseState *s)
{
    MemoryRegion *rom = &s->bar_regions[2];
    uint8_t *rom_data = memory_region_get_ram_ptr(rom);
    QXLRom *rom_header = (QXLRom *)rom_data;

    /* Fill ROM with default values */
    memset(rom_data, 0, QXL_ROM_SIZE);

    /* Magic from driver source */
    rom_header->magic = QXL_ROM_MAGIC;
    rom_header->id = DEVICE_ID;
    rom_header->update_id = 0;
    rom_header->compression_level = 0;
    rom_header->log_level = 0;
    rom_header->mode = 0; /* VGA mode? */
    rom_header->modes_offset = 0;
    rom_header->num_io_pages = 1; /* one IO page */
    rom_header->pages_offset = 0;
    rom_header->draw_area_offset = 0x10000; /* beyond RAM header */
    rom_header->surface0_area_size = QXL_SURFACE0_AREA_SIZE;
    rom_header->ram_header_offset = 0x1000; /* 4K into VRAM */
    rom_header->mm_clock = 0;
    rom_header->n_surfaces = 1;
    rom_header->flags = 0;
    rom_header->slots_start = 0;
    rom_header->slots_end = 0;
    rom_header->slot_gen_bits = 0;
    rom_header->slot_id_bits = 0;
    rom_header->slot_generation = 0;
    rom_header->client_present = 0;
    rom_header->client_monitors_config_crc = 0;
    rom_header->client_monitors_config.count = 0;
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Configuration based on driver usage:
     * BAR0: VRAM (RAM)
     * BAR1: Surface RAM (32-bit)
     * BAR2: ROM (RAM)
     * BAR3: IO ports
     * BAR4: not implemented (64-bit surface bar optional), size 0 to disable
     */
    s->num_bars = 5;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_RAM, .size = QXL_VRAM_SIZE, .name = "qxl-vram" };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_RAM, .size = 16 * MiB, .name = "qxl-surface" };
    s->bar_info[2] = (BARInfo){ .index = 2, .type = BAR_TYPE_RAM, .size = QXL_ROM_SIZE, .name = "qxl-rom" };
    s->bar_info[3] = (BARInfo){ .index = 3, .type = BAR_TYPE_PIO, .size = 256, .name = "qxl-io" };
    s->bar_info[4] = (BARInfo){ .index = 4, .type = BAR_TYPE_NONE, .size = 0, .name = NULL };

    for (int i = 0; i < s->num_bars; i++) {
        if (s->bar_info[i].type != BAR_TYPE_NONE) {
            pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
        }
    }

    /* Initialize VRAM and ROM content */
    s->vram_data = memory_region_get_ram_ptr(&s->bar_regions[0]);
    memset(s->vram_data, 0, QXL_VRAM_SIZE);
    s->ram_header_offset = 0x1000; /* match ROM header */

    /* Install ram_header overlay region on top of VRAM */
    memory_region_init_io(&s->ram_header_mr, OBJECT(s), &pcibase_ram_header_ops, s,
                          "qxl-ram-header", 0x1000);
    memory_region_add_subregion(&s->bar_regions[0], s->ram_header_offset, &s->ram_header_mr);

    /* Set up ROM data */
    pcibase_init_rom(s);

    /* Initialize device state */
    s->int_mask = 0;
    s->int_pending = 0;
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
    .name = "qxl_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(int_mask, PCIBaseState),
        VMSTATE_UINT32(int_pending, PCIBaseState),
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
