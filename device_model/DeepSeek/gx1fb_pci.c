/*
 * QEMU PCI device model for Geode GX1/CS5530 framebuffer
 * Based on driver gx1fb_core.c and register definitions.
 * Phase 2: Functional behavior stubs, separate MMIO regions for DC and MC.
 * Missing driver operations sources are listed in needed_sources.
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

#define TYPE_PCIBASE_DEVICE "gx1fb_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define CONFIG_GCR  0xb8
#define MC_BANK_CFG		0x08
#define MC_BCFG_DIMM0_PG_SZ_MASK	0x00000070
#define MC_GBASE_ADD		0x14
#define MC_BCFG_DIMM0_PG_SZ_NO_DIMM	0x00000070
#define MC_BCFG_DIMM0_SZ_MASK		0x00000700
#define MC_GADD_GBADD_MASK		0x000003ff
#define CONFIG_CCR3_MAPEN 0x10
#define CONFIG_CCR3 0xc3
#define DC_PAL_DATA		0x74
#define DC_PAL_ADDRESS		0x70
#define DC_TCFG_TGEN			0x00000020
#define DC_FP_V_TIMING		0x4C
#define DC_V_TIMING_1		0x40
#define DC_GCFG_CMPE			0x00000010
#define DC_GCFG_DCLK_MASK		0x000000C0
#define DC_OCFG_8BPP			0x00000001
#define DC_H_TIMING_3		0x38
#define DC_GCFG_DFHPSL_POS			 8
#define DC_OCFG_PCKE			0x00000004
#define DC_TCFG_FPPE			0x00000001
#define DC_OUTPUT_CFG		0x0C
#define DC_UNLOCK		0x00
#define DC_LINE_DELTA		0x24
#define DC_H_TIMING_1		0x30
#define DC_GCFG_DFLE			0x00000001
#define DC_OCFG_PDEH			0x00002000
#define DC_TCFG_VSYE			0x00000004
#define DC_GCFG_VRDY			0x20000000
#define DC_FP_H_TIMING		0x3C
#define DC_H_TIMING_2		0x34
#define DC_TCFG_HSYE			0x00000002
#define DC_GCFG_DECE			0x00000020
#define DC_TCFG_BLKE			0x00000008
#define DC_GCFG_DCLK_DIV_1		0x00000080
#define DC_V_TIMING_2		0x44
#define DC_GCFG_DFHPEL_POS			12
#define DC_V_TIMING_3		0x48
#define DC_GENERAL_CFG		0x04
#define DC_UNLOCK_CODE		0x00004758
#define DC_TIMING_CFG		0x08
#define DC_BUF_SIZE		0x28
#define DC_FB_ST_OFFSET		0x10
#define DC_OCFG_PDEL			0x00001000
#define CS5530_DCFG_FP_DATA_EN		0x00000080
#define CS5530_DCFG_HSYNC_EN			0x00000002
#define CS5530_DCFG_DAC_BL_EN			0x00000008
#define CS5530_DCFG_VSYNC_EN			0x00000004
#define CS5530_DCFG_FP_PWR_EN			0x00000040
#define CS5530_DISPLAY_CONFIG	0x0004
#define CS5530_DCFG_DAC_PWR_EN		0x00000020
#define CS5530_DCFG_PWR_SEQ_DLY_INIT		0x00080000
#define CS5530_DCFG_CRT_HSYNC_POL		0x00000100
#define CS5530_DCFG_CRT_VSYNC_POL		0x00000200
#define CS5530_DCFG_GV_PAL_BYP		0x00200000
#define CS5530_DCFG_CRT_SYNC_SKW_MASK		0x0001C000
#define CS5530_DCFG_PWR_SEQ_DLY_MASK		0x000E0000
#define CS5530_DCFG_CRT_SYNC_SKW_INIT		0x00010000
#define CS5530_DOT_CLK_CONFIG	0x0024

#define BAR0_SIZE 0x1000
#define FB_SIZE 0x800000

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

    MemoryRegion dc_regs;
    MemoryRegion mc_regs;
    MemoryRegion fbmem;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t mc_bank_cfg;
    uint32_t mc_gbase_add;
};

/* Separate MMIO handlers for each region */

/* Video (CS5530) BAR0 read/write */
static uint64_t vid_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case CS5530_DISPLAY_CONFIG:
        val = 0; /* Placeholder, actual value depends on driver ops */
        break;
    case CS5530_DOT_CLK_CONFIG:
        val = 0;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: vid read offset 0x%" HWADDR_PRIx " size %u\n",
                      __func__, addr, size);
        break;
    }
    return val;
}

static void vid_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case CS5530_DISPLAY_CONFIG:
        /* Store value? Driver writes configuration bits */
        break;
    case CS5530_DOT_CLK_CONFIG:
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: vid write offset 0x%" HWADDR_PRIx " val 0x%" PRIx64 " size %u\n",
                      __func__, addr, val, size);
        break;
    }
}

static const MemoryRegionOps vid_mmio_ops = {
    .read = vid_mmio_read,
    .write = vid_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

/* Display Controller (DC) register read/write */
static uint64_t dc_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case DC_UNLOCK:
        val = 0; /* Write-only, read returns 0 */
        break;
    case DC_GENERAL_CFG:
        val = 0; /* Placeholder */
        break;
    case DC_TIMING_CFG:
        val = 0;
        break;
    case DC_OUTPUT_CFG:
        val = 0;
        break;
    case DC_FB_ST_OFFSET:
        val = 0;
        break;
    case DC_LINE_DELTA:
        val = 0;
        break;
    case DC_BUF_SIZE:
        val = 0;
        break;
    case DC_H_TIMING_1:
        val = 0;
        break;
    case DC_H_TIMING_2:
        val = 0;
        break;
    case DC_H_TIMING_3:
        val = 0;
        break;
    case DC_FP_H_TIMING:
        val = 0;
        break;
    case DC_FP_V_TIMING:
        val = 0;
        break;
    case DC_V_TIMING_1:
        val = 0;
        break;
    case DC_V_TIMING_2:
        val = 0;
        break;
    case DC_V_TIMING_3:
        val = 0;
        break;
    case DC_PAL_ADDRESS:
        val = 0;
        break;
    case DC_PAL_DATA:
        val = 0;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: dc read offset 0x%" HWADDR_PRIx " size %u\n",
                      __func__, addr, size);
        break;
    }
    return val;
}

static void dc_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case DC_UNLOCK:
        /* Handle unlock sequence: write 0x4758 to enable configuration */
        break;
    case DC_GENERAL_CFG:
        break;
    case DC_TIMING_CFG:
        break;
    case DC_OUTPUT_CFG:
        break;
    case DC_FB_ST_OFFSET:
        break;
    case DC_LINE_DELTA:
        break;
    case DC_BUF_SIZE:
        break;
    case DC_H_TIMING_1:
        break;
    case DC_H_TIMING_2:
        break;
    case DC_H_TIMING_3:
        break;
    case DC_FP_H_TIMING:
        break;
    case DC_FP_V_TIMING:
        break;
    case DC_V_TIMING_1:
        break;
    case DC_V_TIMING_2:
        break;
    case DC_V_TIMING_3:
        break;
    case DC_PAL_ADDRESS:
        break;
    case DC_PAL_DATA:
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: dc write offset 0x%" HWADDR_PRIx " val 0x%" PRIx64 " size %u\n",
                      __func__, addr, val, size);
        break;
    }
}

static const MemoryRegionOps dc_mmio_ops = {
    .read = dc_mmio_read,
    .write = dc_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

/* Memory Controller (MC) register read/write */
static uint64_t mc_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case MC_BANK_CFG:
        val = s->mc_bank_cfg;
        break;
    case MC_GBASE_ADD:
        val = s->mc_gbase_add;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: mc read offset 0x%" HWADDR_PRIx " size %u\n",
                      __func__, addr, size);
        break;
    }
    return val;
}

static void mc_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case MC_BANK_CFG:
        s->mc_bank_cfg = val;
        break;
    case MC_GBASE_ADD:
        s->mc_gbase_add = val;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: mc write offset 0x%" HWADDR_PRIx " val 0x%" PRIx64 " size %u\n",
                      __func__, addr, val, size);
        break;
    }
}

static const MemoryRegionOps mc_mmio_ops = {
    .read = mc_mmio_read,
    .write = mc_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

/* Unused PIO handlers */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

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

    /* Reset registers to default values that match expected frame buffer size */
    s->mc_bank_cfg = 0x100; /* DIMM0 present, size 8MB (0x400000 << 1) */
    s->mc_gbase_add = 0;
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &vid_mmio_ops, s, bi->name, aligned_size);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x1078 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x0104 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0300 );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Set GX base address via config GCR */
    pci_set_byte(pci_conf + CONFIG_GCR, 0x03);
    uint32_t gx_base = ((pci_get_byte(pci_conf + CONFIG_GCR) & 0x03) << 30);

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo){
        .index = 0,
        .type = BAR_TYPE_MMIO,
        .size = BAR0_SIZE,
        .name = "gx1fb-vid"
    };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Map display controller registers at GX_BASE + 0x8300 */
    memory_region_init_io(&s->dc_regs, OBJECT(s), &dc_mmio_ops, s,
                          "gx1fb-dc", 0x100);
    memory_region_add_subregion(get_system_memory(),
                                gx_base + 0x8300, &s->dc_regs);

    /* Map memory controller registers at GX_BASE + 0x8400 */
    memory_region_init_io(&s->mc_regs, OBJECT(s), &mc_mmio_ops, s,
                          "gx1fb-mc", 0x100);
    memory_region_add_subregion(get_system_memory(),
                                gx_base + 0x8400, &s->mc_regs);

    /* Map frame buffer RAM at GX_BASE + 0x800000 */
    memory_region_init_ram(&s->fbmem, OBJECT(s), "gx1fb-framebuffer",
                           FB_SIZE, errp);
    if (*errp) {
        return;
    }
    memory_region_add_subregion(get_system_memory(),
                                gx_base + 0x800000, &s->fbmem);
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

    memory_region_del_subregion(get_system_memory(), &s->dc_regs);
    memory_region_del_subregion(get_system_memory(), &s->mc_regs);
    memory_region_del_subregion(get_system_memory(), &s->fbmem);
    object_unparent(OBJECT(&s->dc_regs));
    object_unparent(OBJECT(&s->mc_regs));
    object_unparent(OBJECT(&s->fbmem));
}

static const VMStateDescription vmstate_pcibase = {
    .name = "gx1fb_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(mc_bank_cfg, PCIBaseState),
        VMSTATE_UINT32(mc_gbase_add, PCIBaseState),
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
