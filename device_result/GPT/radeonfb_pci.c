/*
 * QEMU PCI device model for radeonfb minimal MMIO stub
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

#include "hw/pci/pci_ids.h"

#define TYPE_PCIBASE_DEVICE "radeonfb_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCIBASE_VENDOR_ID   0x1002
#define PCIBASE_DEVICE_ID   0x5955 /* PCI_CHIP_RS480_5955 */
#define PCIBASE_CLASS_ID    PCI_CLASS_DISPLAY_VGA

/* Some register offsets seen in the driver */
#define MC_FB_LOCATION                        0x0148
#define CNFG_MEMSIZE                          0x00F8
#define CNFG_MEMSIZE_MASK                     0x1FFFFFFF
#define CNFG_CNTL                             0x00E0
#define NB_TOM                                0x15C
#define MC_FB_LOCATION_DEFAULT                0x1FFF0000
#define MC_FB_LOCATION_FB_START_MASK          0x0000FFFF
#define MC_FB_LOCATION_FB_START_SHIFT         0

/* Offsets from driver's snippet */
#define OVR_CLR                                0x0230
#define OVR_WID_LEFT_RIGHT                     0x0234
#define OVR_WID_TOP_BOTTOM                     0x0238
#define OV0_SCALE_CNTL                         0x0420
#define SUBPIC_CNTL                            0x0540
#define I2C_CNTL_1                             0x0094
#define GEN_INT_CNTL                           0x0040
#define VIPH_CONTROL                           0x0C40
#define CAP0_TRIG_CNTL                         0x0950
#define CAP1_TRIG_CNTL                         0x09C0

/* Very small subset of other registers that are read/written early */
#define RBBM_STATUS                            0x0E40
#define DSTCACHE_CTLSTAT                       0x1714
#define MC_REG_BASE                            0x0000


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

    /* Minimal register shadows we explicitly need */
    uint32_t mc_fb_location;
    uint32_t cnfg_memsize;
    uint32_t cnfg_cntl;
    uint32_t nb_tom;

    uint32_t ovr_clr;
    uint32_t ovr_wid_lr;
    uint32_t ovr_wid_tb;
    uint32_t ov0_scale_cntl;
    uint32_t subpic_cntl;
    uint32_t i2c_cntl_1;
    uint32_t gen_int_cntl;
    uint32_t viph_control;
    uint32_t cap0_trig_cntl;
    uint32_t cap1_trig_cntl;

    uint32_t rbbm_status;
    uint32_t dstcache_ctlstat;
};

G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    /* The provided driver source never installs an IRQ handler or
     * manipulates interrupt enable/status registers in a way we
     * must emulate for probe. Leave IRQ line inactive. */
    PCIDevice *pdev = PCI_DEVICE(s);
    (void)pdev;
}

G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /* The fbdev driver uses only CPU access to framebuffer MMIO/VRAM
     * in the provided snippets; no descriptor-based bus mastering is
     * visible. No emulated DMA needed. */
    (void)s;
    (void)is_write;
}

static uint32_t pcibase_mmio_readl(PCIBaseState *s, hwaddr addr)
{
    switch (addr) {
    case MC_FB_LOCATION:
        return s->mc_fb_location;
    case CNFG_MEMSIZE:
        return s->cnfg_memsize;
    case CNFG_CNTL:
        return s->cnfg_cntl;
    case NB_TOM:
        return s->nb_tom;

    case OVR_CLR:
        return s->ovr_clr;
    case OVR_WID_LEFT_RIGHT:
        return s->ovr_wid_lr;
    case OVR_WID_TOP_BOTTOM:
        return s->ovr_wid_tb;
    case OV0_SCALE_CNTL:
        return s->ov0_scale_cntl;
    case SUBPIC_CNTL:
        return s->subpic_cntl;
    case I2C_CNTL_1:
        return s->i2c_cntl_1;
    case GEN_INT_CNTL:
        return s->gen_int_cntl;
    case VIPH_CONTROL:
        return s->viph_control;
    case CAP0_TRIG_CNTL:
        return s->cap0_trig_cntl;
    case CAP1_TRIG_CNTL:
        return s->cap1_trig_cntl;

    case RBBM_STATUS:
        /* Engine idle; FIFO "full" enough. Driver polls GUI_ACTIVE and
         * FIFO bits here; we simply return 0 so GUI is idle and
         * treat low 7 bits as large FIFO. */
        return s->rbbm_status;
    case DSTCACHE_CTLSTAT:
        return s->dstcache_ctlstat;

    default:
        /* For unmapped registers, return 0. */
        return 0;
    }
}

static void pcibase_mmio_writel(PCIBaseState *s, hwaddr addr, uint32_t val)
{
    switch (addr) {
    case MC_FB_LOCATION:
        s->mc_fb_location = val;
        break;
    case CNFG_MEMSIZE:
        /* size is read-only from driver's PoV; ignore writes */
        break;
    case CNFG_CNTL:
        s->cnfg_cntl = val;
        break;
    case NB_TOM:
        s->nb_tom = val;
        break;

    case OVR_CLR:
        s->ovr_clr = val;
        break;
    case OVR_WID_LEFT_RIGHT:
        s->ovr_wid_lr = val;
        break;
    case OVR_WID_TOP_BOTTOM:
        s->ovr_wid_tb = val;
        break;
    case OV0_SCALE_CNTL:
        s->ov0_scale_cntl = val;
        break;
    case SUBPIC_CNTL:
        s->subpic_cntl = val;
        break;
    case I2C_CNTL_1:
        s->i2c_cntl_1 = val;
        break;
    case GEN_INT_CNTL:
        s->gen_int_cntl = val;
        pcibase_update_irq(s);
        break;
    case VIPH_CONTROL:
        s->viph_control = val;
        break;
    case CAP0_TRIG_CNTL:
        s->cap0_trig_cntl = val;
        break;
    case CAP1_TRIG_CNTL:
        s->cap1_trig_cntl = val;
        break;

    case RBBM_STATUS:
        s->rbbm_status = val;
        break;
    case DSTCACHE_CTLSTAT:
        /* Driver writes RB2D_DC_FLUSH_ALL, then polls BUSY bit cleared. */
        s->dstcache_ctlstat = 0;
        break;

    default:
        break;
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size == 4) {
        val = pcibase_mmio_readl(s, addr);
    } else if (size == 2) {
        uint32_t full = pcibase_mmio_readl(s, addr & ~0x3ULL);
        unsigned shift = (addr & 0x3) * 8;
        val = (full >> shift) & 0xFFFFu;
    } else if (size == 1) {
        uint32_t full = pcibase_mmio_readl(s, addr & ~0x3ULL);
        unsigned shift = (addr & 0x3) * 8;
        val = (full >> shift) & 0xFFu;
    } else if (size == 8) {
        uint32_t lo = pcibase_mmio_readl(s, addr);
        uint32_t hi = pcibase_mmio_readl(s, addr + 4);
        val = ((uint64_t)hi << 32) | lo;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size == 4) {
        pcibase_mmio_writel(s, addr, (uint32_t)val);
    } else if (size == 2 || size == 1) {
        uint32_t old = pcibase_mmio_readl(s, addr & ~0x3ULL);
        unsigned shift = (addr & 0x3) * 8;
        uint32_t mask = (size == 2) ? 0xFFFFu : 0xFFu;
        uint32_t n = (old & ~(mask << shift)) | (((uint32_t)val & mask) << shift);
        pcibase_mmio_writel(s, addr & ~0x3ULL, n);
    } else if (size == 8) {
        pcibase_mmio_writel(s, addr, (uint32_t)(val & 0xFFFFFFFFu));
        pcibase_mmio_writel(s, addr + 4, (uint32_t)(val >> 32));
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;
    (void)addr;
    (void)size;
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;
    (void)addr;
    (void)val;
    (void)size;
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

    /* Initialize register defaults consistent with driver's expectations */
    s->mc_fb_location = MC_FB_LOCATION_DEFAULT;
    /* Provide a non-zero VRAM size: e.g. 64 MiB */
    s->cnfg_memsize = (64 * MiB) & CNFG_MEMSIZE_MASK;
    s->cnfg_cntl = 0;

    /* * 修复：RS480 驱动通过 NB_TOM 计算 VRAM 大小。
     * 公式: ((tom >> 16) - (tom & 0xFFFF) + 1) << 16
     * 为了让驱动识别出 64MB (0x04000000) 显存，我们将 tom 设为 0x03FF0000
     */
    s->nb_tom = 0x03FF0000;

    s->ovr_clr = 0;
    s->ovr_wid_lr = 0;
    s->ovr_wid_tb = 0;
    s->ov0_scale_cntl = 0;
    s->subpic_cntl = 0;
    s->i2c_cntl_1 = 0;
    s->gen_int_cntl = 0;
    s->viph_control = 0;
    s->cap0_trig_cntl = 0;
    s->cap1_trig_cntl = 0;

    /* Indicate engine idle and FIFO sufficiently large */
    s->rbbm_status = 0x0000007F;
    s->dstcache_ctlstat = 0;
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
        /* * 修复：Framebuffer 必须加上 PCI_BASE_ADDRESS_MEM_PREFETCH，
         * 否则 Linux 内核的 ioremap_wc 会映射失败。
         */
        pci_register_bar(pdev, bi->index, 
                         PCI_BASE_ADDRESS_SPACE_MEMORY | PCI_BASE_ADDRESS_MEM_PREFETCH, 
                         mr);
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCIBASE_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCIBASE_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCIBASE_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Expose as PCIe endpoint, power management capability as in template */
    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* Define BARs so that driver sees:
     *  BAR0 : framebuffer aperture (64 MiB RAM)
     *  BAR2 : MMIO registers (64 KiB MMIO)
     */
    s->num_bars = 2;

    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_RAM;
    s->bar_info[0].size = 64 * MiB;
    s->bar_info[0].name = "radeonfb-fb";

    s->bar_info[1].index = 2;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = 64 * KiB;
    s->bar_info[1].name = "radeonfb-mmio";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No MSI/MSI-X exposed: driver does not depend on it in the snippet */

    /* Initialize internal state */
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

    for (int i = 0; i < 6; i++) {
        memory_region_del_subregion(pci_address_space(pdev), &s->bar_regions[i]);
        /* memory_region_destroy is not available or deprecated; rely on
         * MemoryRegion lifetime being managed with the device object. */
    }
}

static const VMStateDescription vmstate_pcibase = {
    .name = "radeonfb_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(mc_fb_location, PCIBaseState),
        VMSTATE_UINT32(cnfg_memsize, PCIBaseState),
        VMSTATE_UINT32(cnfg_cntl, PCIBaseState),
        VMSTATE_UINT32(nb_tom, PCIBaseState),
        VMSTATE_UINT32(ovr_clr, PCIBaseState),
        VMSTATE_UINT32(ovr_wid_lr, PCIBaseState),
        VMSTATE_UINT32(ovr_wid_tb, PCIBaseState),
        VMSTATE_UINT32(ov0_scale_cntl, PCIBaseState),
        VMSTATE_UINT32(subpic_cntl, PCIBaseState),
        VMSTATE_UINT32(i2c_cntl_1, PCIBaseState),
        VMSTATE_UINT32(gen_int_cntl, PCIBaseState),
        VMSTATE_UINT32(viph_control, PCIBaseState),
        VMSTATE_UINT32(cap0_trig_cntl, PCIBaseState),
        VMSTATE_UINT32(cap1_trig_cntl, PCIBaseState),
        VMSTATE_UINT32(rbbm_status, PCIBaseState),
        VMSTATE_UINT32(dstcache_ctlstat, PCIBaseState),
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

type_init(pcibase_register_types)

