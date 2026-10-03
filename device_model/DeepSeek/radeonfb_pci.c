/*
 * QEMU 8.2.10 PCI Device Model for radeonfb
 * Generated from Linux driver radeon_base.c
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
#include <string.h> /* for memcpy */

/* Provided register defines from supplementary driver source */
#define OVR_CLR                 0x0040
#define OVR_WID_LEFT_RIGHT      0x0044
#define OVR_WID_TOP_BOTTOM      0x0048
#define OV0_SCALE_CNTL          0x0420
#define SUBPIC_CNTL             0x0540
#define VIPH_CONTROL            0x0C40
#define I2C_CNTL_1              0x00BC
#define GEN_INT_CNTL            0x0040
#define CAP0_TRIG_CNTL          0x0950
#define CAP1_TRIG_CNTL          0x09c0

/* New register defines from supplementary driver source */
#define CLOCK_CNTL_DATA         0x000A /* CLOCK_CNTL + 2, with CLOCK_CNTL_INDEX=0x0008 */
#define CRTC_GEN_CNTL           0x001C
#define CLOCK_CNTL_INDEX        0x0008
#define RBBM_STATUS             0x0E40
#define DSTCACHE_CTLSTAT        0x1714
#define MPP_TB_CONFIG           0x01c0
/* #define DEVICE_ID 0x01  // omitted to avoid conflict with PCI device ID */
#define CRTC_VLINE_CRNT_VLINE   0x0010
#define PPLL_REF_DIV            0x0003
#define M_SPLL_REF_FB_DIV       0x000a
#define PPLL_DIV_0              0x0004
#define PPLL_DIV_1              0x0005
#define PPLL_DIV_2              0x0006
#define PPLL_DIV_3              0x0007
#define VCLK_ECP_CNTL           0x0008
#define HTOTAL_CNTL             0x001D
#define PPLL_CNTL               0x0002
#define LVDS_GEN_CNTL           0x02d0
#define CRTC_EXT_CNTL           0x0054
#define FP_GEN_CNTL             0x0284
#define DAC_CNTL2               0x007c
#define PALETTE_INDEX           0x00B0
#define PALETTE_DATA            0x00B4
#define DAC_CNTL                0x00C4
#define CRTC_H_TOTAL_DISP       0x0000
#define CRTC_H_SYNC_STRT_WID    0x0004
#define CRTC_V_TOTAL_DISP       0x0008
#define CRTC_V_SYNC_STRT_WID    0x000C
#define SURFACE_CNTL            0x0B00
#define CRTC_PITCH              0x0016
#define FP_CRTC_H_TOTAL_DISP    0x0250
#define FP_CRTC_V_TOTAL_DISP    0x0254
#define FP_H_SYNC_STRT_WID      0x02C4
#define FP_HORZ_STRETCH         0x028C
#define FP_V_SYNC_STRT_WID      0x02C8
#define FP_VERT_STRETCH         0x0290
#define LVDS_PLL_CNTL           0x02d4
#define TMDS_CRC                0x02a0
#define TMDS_TRANSMITTER_CNTL   0x02a4
#define SURFACE0_LOWER_BOUND    0x0B04
#define SURFACE0_UPPER_BOUND    0x0B08
#define SURFACE0_INFO           0x0B0C
#define NB_TOM                  0x015C
#define MC_FB_LOCATION          0x0148
#define DISPLAY_BASE_ADDR       0x023C
#define CRTC2_DISPLAY_BASE_ADDR 0x033c
#define OV0_BASE_ADDR           0x043C
#define GRPH2_BUFFER_CNTL      0x03F0
#define CRTC_MORE_CNTL          0x027C
#define CNFG_MEMSIZE            0x00F8
#define MEM_SDRAM_MODE_REG      0x0158
#define MEM_CNTL                0x00B0
#define CNFG_CNTL               0x00DC
#define CRTC_OFFSET             0x0014
#define CRTC_OFFSET_CNTL        0x0228
#define FP2_GEN_CNTL            0x0288
#define DISP_OUTPUT_CNTL        0x0D64
#define RADEON_REGSIZE          0x4000

/* Placeholder defines for BAR sizes – actual values from driver source */
#define RADEON_MMIO_BAR_SIZE    RADEON_REGSIZE   /* 0x4000, exact from RADEON_REGSIZE */
#define RADEON_FB_BAR_SIZE      0x10000000      /* placeholder, still unknown */
#define TYPE_PCIBASE_DEVICE "radeonfb_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
/* PCI IDs from first entry in radeonfb_pci_table: CHIP_DEF(PCI_CHIP_RS480_5955, ...) */
#define VENDOR_ID  0x1002   /* PCI_VENDOR_ID_ATI */
#define DEVICE_ID  0x5955   /* PCI_CHIP_RS480_5955 */
#define CLASS_ID   0x0300   /* PCI_CLASS_DISPLAY_VGA */

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

    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    uint8_t mmio_data[RADEON_MMIO_BAR_SIZE];  /* MMIO register state */

    QEMUTimer *lvds_timer;
    /* pending lvds gen cntl value for timer callback */
    uint32_t pending_lvds_gen_cntl;
};

/* LVDS timer callback stub – just apply pending register */
static void radeon_lvds_timer_cb(void *opaque)
{
    PCIBaseState *s = opaque;
    /* If there's a pending LVDS_GEN_CNTL value, write it to MMIO */
    /* For now, just acknowledge; actual offset requires define LVDS_GEN_CNTL */
    /* We'll implement after receiving register offsets */
    // OUTREG(LVDS_GEN_CNTL, s->pending_lvds_gen_cntl);
    /* Log it */
    qemu_log_mask(LOG_UNIMP, "radeonfb: lvds_timer triggered, pending=0x%x\n",
                  s->pending_lvds_gen_cntl);
}

/* MMIO read/write handlers: simple passthrough to byte array */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr + size > RADEON_MMIO_BAR_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR, "radeonfb: MMIO read out of bounds:"
                      " addr=0x%" HWADDR_PRIx " size=%u\n", addr, size);
        return ~0ULL;
    }

    memcpy(&val, s->mmio_data + addr, size);
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr + size > RADEON_MMIO_BAR_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR, "radeonfb: MMIO write out of bounds:"
                      " addr=0x%" HWADDR_PRIx " size=%u val=0x%" PRIx64 "\n",
                      addr, size, val);
        return;
    }

    memcpy(s->mmio_data + addr, &val, size);
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

    /* Clear all MMIO registers to zero */
    memset(s->mmio_data, 0, RADEON_MMIO_BAR_SIZE);
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
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, errp);
        pci_register_bar(pdev, bi->index,
                         PCI_BASE_ADDRESS_SPACE_MEMORY |
                         PCI_BASE_ADDRESS_MEM_PREFETCH,
                         mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        /* PIO not used */
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 0); /* No interrupt pin used by driver probe */
    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR Initialization: BAR0 = framebuffer (RAM), BAR2 = MMIO register space */
    s->num_bars = 2;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_RAM;   /* framebuffer, prefetchable */
    s->bar_info[0].size = RADEON_FB_BAR_SIZE;
    s->bar_info[0].name = "radeonfb-fb";

    s->bar_info[1].index = 2;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = RADEON_MMIO_BAR_SIZE;
    s->bar_info[1].name = "radeonfb-mmio";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Timer initialization – LVDS timer for panel power sequencing */
    s->lvds_timer = timer_new_ms(QEMU_CLOCK_VIRTUAL, radeon_lvds_timer_cb, s);
    s->pending_lvds_gen_cntl = 0;

    /* Clear MMIO state */
    memset(s->mmio_data, 0, RADEON_MMIO_BAR_SIZE);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    /* No MSI/MSI-X */
    /* Free timer */
    timer_free(s->lvds_timer);
}

static const VMStateDescription vmstate_pcibase = {
    .name = "radeonfb_pci",
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
