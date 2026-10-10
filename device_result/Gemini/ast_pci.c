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


#define TYPE_PCIBASE_DEVICE "ast_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ASPEED 0x1a03
#define PCI_CHIP_AST2000 0x2000
#define AST_IO_VGAER 0x43
#define AST_IO_VGAMR_W 0x42
#define AST_IO_VGACRI 0x54
#define AST_IO_VGACR80_PASSWORD 0xa8
#define AST_IO_MM_OFFSET 0x380
#define AST_IO_MM_LENGTH 128

#define AST_REGS_0xF000 0xf000
#define AST_REGS_0xF004 0xf004
#define AST_REGS_0x1207C 0x1207c

#define AST_IO_VGAER_VGA_ENABLE      (1 << 0)
#define AST_IO_VGAMR_IOSEL           (1 << 0)
#define AST_IO_VGACRA1_MMIO_ENABLED  (1 << 2)
#define AST_IO_VGACRA1_VGAIO_DISABLED (1 << 1)
#define AST_IO_VGACRD0_VRAM_INIT_STATUS_MASK (0x3 << 6)

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
    

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint8_t vgaer;
    uint8_t vgamr_w;
    uint8_t vgacr_index;
    uint8_t vgacr_regs[256];
    uint32_t reg_f000;
    uint32_t reg_f004;
    uint32_t reg_1207c;

    /* DMA Context */
    
};

static uint64_t ast_ioregs_read(PCIBaseState *s, hwaddr addr, unsigned size)
{
    switch (addr) {
    case AST_IO_VGAER:
        return s->vgaer;
    case AST_IO_VGACRI:
        return s->vgacr_index;
    case AST_IO_VGACRI + 1:
        return s->vgacr_regs[s->vgacr_index];
    default:
        return 0;
    }
}

static void ast_ioregs_write(PCIBaseState *s, hwaddr addr, uint64_t val, unsigned size)
{
    switch (addr) {
    case AST_IO_VGAER:
        s->vgaer = val;
        break;
    case AST_IO_VGAMR_W:
        s->vgamr_w = val;
        break;
    case AST_IO_VGACRI:
        s->vgacr_index = val;
        break;
    case AST_IO_VGACRI + 1:
        s->vgacr_regs[s->vgacr_index] = val;
        break;
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= AST_IO_MM_OFFSET && addr < AST_IO_MM_OFFSET + AST_IO_MM_LENGTH) {
        return ast_ioregs_read(s, addr - AST_IO_MM_OFFSET, size);
    }

    switch (addr) {
    case AST_REGS_0xF004:
        val = s->reg_f004;
        break;
    case AST_REGS_0x1207C:
        val = s->reg_1207c;
        break;
    }
    
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= AST_IO_MM_OFFSET && addr < AST_IO_MM_OFFSET + AST_IO_MM_LENGTH) {
        ast_ioregs_write(s, addr - AST_IO_MM_OFFSET, val, size);
        return;
    }

    switch (addr) {
    case AST_REGS_0xF000:
        s->reg_f000 = val;
        break;
    case AST_REGS_0xF004:
        s->reg_f004 = val;
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    return ast_ioregs_read(s, addr, size);
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    ast_ioregs_write(s, addr, val, size);
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

    s->vgaer = AST_IO_VGAER_VGA_ENABLE;
    s->vgamr_w = AST_IO_VGAMR_IOSEL;
    s->vgacr_index = 0;
    memset(s->vgacr_regs, 0, sizeof(s->vgacr_regs));
    
    /* Default values to pass driver probe checks */
    s->vgacr_regs[0xa1] = AST_IO_VGACRA1_MMIO_ENABLED;
    s->vgacr_regs[0xd0] = 0x80; /* P2A bridge enabled, VRAM init status */
    s->vgacr_regs[0xd1] = 0x10;
    
    s->reg_f000 = 0;
    s->reg_f004 = 0x12345678; /* Non-zero and non-0xffffffff */
    s->reg_1207c = 0x0100; /* SCU revision */
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ASPEED );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_CHIP_AST2000 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0300 );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 3;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_RAM, .size = 16 * MiB, .name = "ast.vram" };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_MMIO, .size = 0x400, .name = "ast.regs" };
    s->bar_info[2] = (BARInfo){ .index = 2, .type = BAR_TYPE_PIO, .size = 128, .name = "ast.ioregs" };
    
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
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
