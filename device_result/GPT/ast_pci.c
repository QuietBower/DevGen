/*
 * QEMU AST VGA-compatible PCI device (minimal model for ast_drv.c)
 *
 * This model only implements behavior that is explicitly visible in
 * drivers/gpu/drm/ast/ast_drv.c. All other hardware behavior is left
 * unimplemented.
 */

#include "qemu/osdep.h"
#include <inttypes.h>
#include <string.h>
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "sysemu/dma.h"
#include "sysemu/reset.h"
#include "hw/irq.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/pci/pcie.h"
#include "qom/object.h"
#include "qapi/visitor.h"
#include "hw/qdev-properties.h"

#define TYPE_PCIBASE_DEVICE "ast_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI_VENDOR_ASPEED 0x1a03
#define PCI_CHIP_AST2000 0x2000
#define PCI_CHIP_AST2100 0x2010

#define AST_IO_VGAER_VGA_ENABLE 0x1
#define AST_IO_VGAER            0x43
#define AST_IO_VGAMR_W          0x42
#define AST_IO_VGAMR_IOSEL      0x1
#define AST_IO_VGACRI           0x54
#define AST_IO_VGACRA1_MMIO_ENABLED 0x4
#define AST_IO_VGACRA1_VGAIO_DISABLED 0x2
#define AST_IO_VGACR80_PASSWORD 0xa8
#define AST_IO_VGACRD0_VRAM_INIT_STATUS_MASK 0xc0
#define AST_IO_MM_LENGTH        128
#define AST_IO_MM_OFFSET        0x380
#define AST_IO_VGACRD1_TX_ITE66121_VBIOS 0x02
#define AST_IO_VGACRD1_TX_CH7003_VBIOS  0x06
#define AST_IO_VGACRD1_TX_ASTDP         0x0e
#define AST_IO_VGACRD1_TX_TYPE_MASK     0x0e
#define AST_IO_VGACRD1_TX_SIL164_VBIOS  0x04
#define AST_IO_VGACRD1_TX_DP501_VBIOS   0x08
#define AST_IO_VGACRD1_TX_ANX9807_VBIOS 0x0a
#define AST_IO_VGACRA3_DVO_ENABLED      0x80
#define AST_IO_VGACRD1_TX_FW_EMBEDDED_FW 0x0c
#define AST_IO_VGACRAA_VGAMEM_SIZE_MASK 0x3
#define AST_IO_VGACR99_VGAMEM_RSRV_MASK 0x3
#define AST_MAX_HWC_HEIGHT      64
#define AST_MAX_HWC_WIDTH       64
#define AST_HWC_PITCH           (AST_MAX_HWC_WIDTH * 2)
#define AST_HWC_SIZE            (AST_MAX_HWC_HEIGHT * AST_HWC_PITCH)
#define AST_IO_VGACRD1_MCU_FW_EXECUTING 0x20
#define AST_IO_VGACRE5_EDID_READ_DONE   0x1
#define DRIVER_DESC             "AST"
#define DRIVER_MAJOR            0
#define DRIVER_PATCHLEVEL       0
#define DRIVER_NAME             "ast"
#define DRIVER_MINOR            1

enum ast_config_mode {
    ast_use_p2a,
    ast_use_dt,
    ast_use_defaults
};

enum ast_chip {
    AST1000 = ((1 << 16) | 0),
    AST2000 = ((1 << 16) | 1),
    AST1100 = ((2 << 16) | 0),
    AST2100 = ((2 << 16) | 1),
    AST2050 = ((2 << 16) | 2),
    AST2200 = ((3 << 16) | 0),
    AST2150 = ((3 << 16) | 1),
    AST2300 = ((4 << 16) | 0),
    AST1300 = ((4 << 16) | 1),
    AST1050 = ((4 << 16) | 2),
    AST2400 = ((5 << 16) | 0),
    AST1400 = ((5 << 16) | 1),
    AST1250 = ((5 << 16) | 2),
    AST2500 = ((6 << 16) | 0),
    AST2510 = ((6 << 16) | 1),
    AST2520 = ((6 << 16) | 2),
    AST2600 = ((7 << 16) | 0),
    AST2620 = ((7 << 16) | 1),
};

enum ast_tx_chip {
    AST_TX_NONE,
    AST_TX_SIL164,
    AST_TX_DP501,
    AST_TX_ASTDP,
};

enum ast_dram_layout {
    AST_DRAM_512Mx16 = 0,
    AST_DRAM_1Gx16   = 1,
    AST_DRAM_512Mx32 = 2,
    AST_DRAM_1Gx32   = 3,
    AST_DRAM_2Gx16   = 6,
    AST_DRAM_4Gx16   = 7,
    AST_DRAM_8Gx16   = 8,
};

struct ast_plane {
    uint64_t offset;
    unsigned long size;
};

struct ast_cursor_plane {
    struct ast_plane base;
    uint8_t argb4444[AST_HWC_SIZE];
};

struct ast_connector {
    int physical_status;
};

/* Minimal stub for struct mutex to satisfy the compiler. */
struct mutex {
    int dummy;
};

struct ast_device_state_stub {
    void *regs;
    void *ioregs;
    void *dp501_fw_buf;

    enum ast_config_mode config_mode;
    enum ast_chip chip;

    void *vram;
    unsigned long vram_base;
    unsigned long vram_size;

    struct mutex modeset_lock;

    enum ast_tx_chip tx_chip;

    struct ast_plane primary_plane;
    struct ast_cursor_plane cursor_plane;

    bool support_wsxga_p;
    bool support_fullhd;
    bool support_wuxga;

    uint8_t *dp501_fw_addr;
    const void *dp501_fw;
};

typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM
} BARType;

typedef struct {
    int index;
    BARType type;
    hwaddr size;
    const char *name;
    bool sparse;
} BARInfo;

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];

    uint8_t *mmio_backing;
    size_t mmio_backing_size;

    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;

    /* Minimal internal register state used in ast_drv.c */
    uint8_t io_regs[AST_IO_MM_LENGTH];   /* I/O / MMIO VGA regs window */
    uint32_t sys_regs_space[0x2000 / 4]; /* BAR1 system/mmio window used at 0xf000/0xf004/0x1207c */
};

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len);
static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len);
static void pcibase_reset(DeviceState *dev);
static void pcibase_realize(PCIDevice *pdev, Error **errp);
static void pcibase_uninit(PCIDevice *pdev);

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, bi->size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

/* MMIO / PIO handlers */

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    /* BAR1: system/mmio regs; BAR1 size defined in realize as 0x2000 */
    /* ast_drv.c uses __ast_read32(regs, 0xf004), 0xf000, 0x1207c */

    /* We model a simple 32-bit array; addr is offset within BAR1 */
    if (addr < sizeof(s->sys_regs_space)) {
        uint32_t val32 = s->sys_regs_space[addr >> 2];
        switch (size) {
        case 1:
            return (val32 >> ((addr & 3) * 8)) & 0xffu;
        case 2:
            if (addr & 1) {
                qemu_log_mask(LOG_UNIMP, "[%s] unaligned mmio_read16 @0x%"PRIx64"\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr);
            }
            return (val32 >> ((addr & 2) * 8)) & 0xffffu;
        case 4:
            return val32;
        default:
            break;
        }
    }

    qemu_log_mask(LOG_UNIMP, "[%s] mmio_read addr=%"PRIx64" size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < sizeof(s->sys_regs_space)) {
        uint32_t *p = &s->sys_regs_space[addr >> 2];
        uint32_t cur = *p;
        uint32_t newv = cur;
        switch (size) {
        case 1: {
            unsigned shift = (addr & 3) * 8;
            uint32_t mask = 0xffu << shift;
            newv = (cur & ~mask) | (((uint32_t)val & 0xffu) << shift);
            break;
        }
        case 2: {
            if (addr & 1) {
                qemu_log_mask(LOG_UNIMP, "[%s] unaligned mmio_write16 @0x%"PRIx64"\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr);
            }
            unsigned shift = (addr & 2) * 8;
            uint32_t mask = 0xffffu << shift;
            newv = (cur & ~mask) | (((uint32_t)val & 0xffffu) << shift);
            break;
        }
        case 4:
            newv = (uint32_t)val;
            break;
        default:
            break;
        }
        *p = newv;
        return;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] mmio_write addr=%"PRIx64" val=%"PRIx64" size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, (uint64_t)val, size);
}

static uint8_t *ast_get_ioregs_ptr(PCIBaseState *s, hwaddr addr, unsigned size)
{
    /* I/O/Mmio side: window of AST_IO_MM_LENGTH bytes starting at AST_IO_MM_OFFSET */
    if (addr < AST_IO_MM_LENGTH && (addr + size) <= AST_IO_MM_LENGTH) {
        return &s->io_regs[addr];
    }
    return NULL;
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint8_t *p;

    /* For I/O BAR2, Linux maps whole BAR and then accesses offsets AST_IO_* within it. */
    p = ast_get_ioregs_ptr(s, addr, size);
    if (p) {
        switch (size) {
        case 1:
            return p[0];
        case 2:
            return p[0] | ((uint16_t)p[1] << 8);
        case 4:
            return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
        default:
            break;
        }
    }

    qemu_log_mask(LOG_UNIMP, "[%s] pio_read addr=%"PRIx64" size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint8_t *p;

    p = ast_get_ioregs_ptr(s, addr, size);
    if (p) {
        switch (size) {
        case 1:
            p[0] = (uint8_t)val;
            break;
        case 2:
            p[0] = (uint8_t)(val & 0xff);
            p[1] = (uint8_t)((val >> 8) & 0xff);
            break;
        case 4:
            p[0] = (uint8_t)(val & 0xff);
            p[1] = (uint8_t)((val >> 8) & 0xff);
            p[2] = (uint8_t)((val >> 16) & 0xff);
            p[3] = (uint8_t)((val >> 24) & 0xff);
            break;
        default:
            break;
        }
        return;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] pio_write addr=%"PRIx64" val=%"PRIx64" size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, (uint64_t)val, size);
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    pci_device_reset(pdev);

    if (s->mmio_backing && s->mmio_backing_size) {
        memset(s->mmio_backing, 0, s->mmio_backing_size);
    }

    memset(s->io_regs, 0, sizeof(s->io_regs));
    memset(s->sys_regs_space, 0, sizeof(s->sys_regs_space));

    /* Set VGA enabled bit so ast_is_vga_enabled() sees it */
    s->io_regs[AST_IO_VGAER] |= AST_IO_VGAER_VGA_ENABLE;
}

static void pcibase_dma_device_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    (void)s;
    (void)errp;
}

static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    uint32_t val = pci_default_read_config(pdev, addr, len);

    switch (len) {
    case 1:
        val &= 0xFF;
        break;
    case 2:
        val &= 0xFFFF;
        break;
    case 4:
    default:
        break;
    }
    return val;
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    if (addr >= PCI_BASE_ADDRESS_0 && addr <= PCI_BASE_ADDRESS_5) {
        pci_default_write_config(pdev, addr, val, len);
        return;
    }

    pci_default_write_config(pdev, addr, val, len);
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ASPEED);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_CHIP_AST2000);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_DISPLAY_VGA);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x40); /* >= 0x40 so driver uses MMIO path */
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Define BARs according to how the driver uses them:
     * BAR1: MMIO regs space, large enough to include offsets 0xf000, 0xf004, 0x1207c.
     * BAR2: I/O space, at least AST_IO_MM_LENGTH bytes, used as I/O alternative.
     */

    s->num_bars = 0;

    s->bar_info[s->num_bars].index = 1;
    s->bar_info[s->num_bars].type = BAR_TYPE_MMIO;
    s->bar_info[s->num_bars].size = 0x2000; /* minimal window */
    s->bar_info[s->num_bars].name = "ast-mmio-bar1";
    s->bar_info[s->num_bars].sparse = false;
    s->num_bars++;

    s->bar_info[s->num_bars].index = 2;
    s->bar_info[s->num_bars].type = BAR_TYPE_PIO;
    s->bar_info[s->num_bars].size = AST_IO_MM_LENGTH;
    s->bar_info[s->num_bars].name = "ast-io-bar2";
    s->bar_info[s->num_bars].sparse = false;
    s->num_bars++;

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
        if (errp && *errp) {
            return;
        }
    }

    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) {
        return;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] device realized\n", TYPE_PCIBASE_DEVICE);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (s->has_msix) {
        msix_uninit(pdev, NULL, 0);
        s->has_msix = false;
    }
    if (s->has_msi) {
        msi_uninit(pdev);
        s->has_msi = false;
    }

    if (s->mmio_backing) {
        g_free(s->mmio_backing);
        s->mmio_backing = NULL;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] device uninit\n", TYPE_PCIBASE_DEVICE);
}

static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->config_read  = pcibase_config_read;
    k->config_write = pcibase_config_write;

    k->realize = pcibase_realize;
    k->exit    = pcibase_uninit;
    dc->reset  = pcibase_reset;

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
