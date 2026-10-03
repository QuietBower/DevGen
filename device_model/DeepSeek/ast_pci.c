/*
 * QEMU device model for ASPEED AST2000 graphics controller
 * Emulates necessary registers for Linux ast_drv probe success.
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

#define TYPE_PCIBASE_DEVICE "ast_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_ASPEED        0x1a03
#define PCI_DEVICE_ID_AST2000       0x2000
#define PCI_CLASS_DISPLAY_VGA       0x0300

#define AST_IO_VGAER                0x43
#define AST_IO_VGAER_VGA_ENABLE     BIT(0)
#define AST_IO_VGAMR_W              0x42
#define AST_IO_VGAMR_IOSEL          BIT(0)
#define AST_IO_VGACRI               0x54
#define AST_IO_VGACRA1_MMIO_ENABLED BIT(2)
#define AST_IO_VGACRA1_VGAIO_DISABLED BIT(1)
#define AST_IO_VGACR80_PASSWORD     0xa8
#define AST_IO_MM_LENGTH            128
#define AST_IO_MM_OFFSET            0x380

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

    /* AST device-specific shadow registers */
    uint8_t vgaer;            /* VGA Enable Register (0x43) */
    uint8_t vgamr_w;          /* VGA Miscellaneous Register Write (0x42) */
    uint8_t index_reg;        /* Index register for 0x54/0x55 indexed access */
    uint8_t idx_data[256];    /* Data for each index */
};

/* Internal helper for status-triggered signaling. Unused, but required by template. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    /* Interrupts not needed for probe, leave empty */
}

/* Device-initiated DMA logic based on driver access patterns - not used */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /* DMA not required for probe, leave empty */
}

/* MMIO Read Handler */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* VGA I/O space at MMIO offset AST_IO_MM_OFFSET */
    if (addr >= AST_IO_MM_OFFSET && addr < AST_IO_MM_OFFSET + AST_IO_MM_LENGTH) {
        uint8_t reg = addr - AST_IO_MM_OFFSET;
        if (size != 1) {
            qemu_log_mask(LOG_GUEST_ERROR, "ast: invalid MMIO read size %d at 0x%" HWADDR_PRIx "\n", size, addr);
            return ~0ULL;
        }
        switch (reg) {
        case 0x42:
            val = s->vgamr_w;
            break;
        case 0x43:
            val = s->vgaer;
            break;
        case 0x54:
            val = s->index_reg;
            break;
        case 0x55:
            val = s->idx_data[s->index_reg];
            break;
        default:
            val = 0xff;
            break;
        }
    } else {
        /* Other MMIO reads (e.g., P2A or framebuffer) return 0 */
        val = 0;
    }

    return val;
}

/* MMIO Write Handler */
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* VGA I/O space at MMIO offset AST_IO_MM_OFFSET */
    if (addr >= AST_IO_MM_OFFSET && addr < AST_IO_MM_OFFSET + AST_IO_MM_LENGTH) {
        uint8_t reg = addr - AST_IO_MM_OFFSET;
        if (size != 1) {
            qemu_log_mask(LOG_GUEST_ERROR, "ast: invalid MMIO write size %d at 0x%" HWADDR_PRIx "\n", size, addr);
            return;
        }
        switch (reg) {
        case 0x42:
            s->vgamr_w = val & 0xff;
            break;
        case 0x43:
            s->vgaer = val & 0xff;
            break;
        case 0x54:
            s->index_reg = val & 0xff;
            break;
        case 0x55:
            s->idx_data[s->index_reg] = val & 0xff;
            break;
        default:
            break;
        }
    }
    /* Other MMIO writes are ignored */
}

/* PIO Read Handler */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size != 1) {
        qemu_log_mask(LOG_GUEST_ERROR, "ast: invalid PIO read size %d at 0x%" HWADDR_PRIx "\n", size, addr);
        return ~0ULL;
    }

    switch (addr) {
    case 0x42:
        val = s->vgamr_w;
        break;
    case 0x43:
        val = s->vgaer;
        break;
    case 0x54:
        val = s->index_reg;
        break;
    case 0x55:
        val = s->idx_data[s->index_reg];
        break;
    default:
        val = 0xff;
        break;
    }

    return val;
}

/* PIO Write Handler */
static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 1) {
        qemu_log_mask(LOG_GUEST_ERROR, "ast: invalid PIO write size %d at 0x%" HWADDR_PRIx "\n", size, addr);
        return;
    }

    switch (addr) {
    case 0x42:
        s->vgamr_w = val & 0xff;
        break;
    case 0x43:
        s->vgaer = val & 0xff;
        break;
    case 0x54:
        s->index_reg = val & 0xff;
        break;
    case 0x55:
        s->idx_data[s->index_reg] = val & 0xff;
        break;
    default:
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

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Enable memory and I/O access early to aid BAR sizing */
    pci_set_word(PCI_DEVICE(dev)->config + PCI_COMMAND, PCI_COMMAND_MEMORY | PCI_COMMAND_IO);

    /* Initialize VGA registers to allow driver probe */
    s->vgaer = AST_IO_VGAER_VGA_ENABLE;
    s->vgamr_w = AST_IO_VGAMR_IOSEL;
    s->index_reg = 0;
    memset(s->idx_data, 0, sizeof(s->idx_data));

    /* Pre-populate indexed registers to bypass P2A detection */
    s->idx_data[0xd0] = 0x80;  /* vgacrd0: bit7 set -> P2A not detected */
    s->idx_data[0xd1] = 0x10;  /* vgacrd1: bit4 set -> P2A not detected */
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
        /* Changed to MMIO to avoid RAM allocation issues during probe */
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_ASPEED);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_AST2000);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_DISPLAY_VGA);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Power Management capability */
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Configuration:
     * BAR0: VRAM framebuffer (8MB) - now MMIO to ensure compatibility
     * BAR1: MMIO for control registers (256KB)
     * BAR2: I/O for legacy VGA registers (256 bytes)
     */
    s->bar_info[0].index = 0;
    s->bar_info[0].type  = BAR_TYPE_MMIO; /* Changed from RAM to MMIO */
    s->bar_info[0].size  = 0x800000; /* 8MB */
    s->bar_info[0].name  = "ast-vram";

    s->bar_info[1].index = 1;
    s->bar_info[1].type  = BAR_TYPE_MMIO;
    s->bar_info[1].size  = 0x40000;
    s->bar_info[1].name  = "ast-bar1";

    s->bar_info[2].index = 2;
    s->bar_info[2].type  = BAR_TYPE_PIO;
    s->bar_info[2].size  = 0x100;
    s->bar_info[2].name  = "ast-bar2";

    s->num_bars = 3;

    /* Register all configured BARs */
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = false;
    s->has_msix = false;
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
    .name = "ast_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT8(vgaer, PCIBaseState),
        VMSTATE_UINT8(vgamr_w, PCIBaseState),
        VMSTATE_UINT8(index_reg, PCIBaseState),
        VMSTATE_BUFFER(idx_data, PCIBaseState),
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
    set_bit(DEVICE_CATEGORY_DISPLAY, dc->categories);
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
