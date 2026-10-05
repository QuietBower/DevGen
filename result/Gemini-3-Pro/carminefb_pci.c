/*
 * Generic QEMU PCI device template (QEMU 8.2.x)
 *
 * Phase 1: fill register macros, BAR list, BAR sizes, structs, enums.
 * Phase 2: implement MMIO/PIO read/write, DMA init, IRQ init, reset, config hooks
 * Phase 3 (Repair Phase)：correct syntax, types, includes, missing symbols, remove dead code
 * Phase 4 (Debug & Update Phase)：Based on actual kernel logs from QEMU boot, repair the virtual hardware behavior
 *
 * Replace #PLACEHOLDER# blocks with device-specific code/data.
 * NOTE:
 * - Only essential fixes added (TYPE macro, PIO BAR handling, MMIO impl, DMA init, IRQ).
 * - No optional or advanced features added.
 * - Do not directly fill this file with the Linux kernel source code
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
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

/* ------------------------------------------------------------------ */
/* Additional include files (Stage1 optional)                          */
/* ------------------------------------------------------------------ */

#define TYPE_PCIBASE_DEVICE "carminefb_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Related register/macros/constants from driver (Stage1) */
#define PCI_VENDOR_ID_FUJITU_LIMITED 0x10cf
#define CARMINE_PCI_DEVICE_ID        0x202b

#define CARMINEFB_DEFAULT_VIDEO_MODE	1

/* Register Offsets and Constants */
#define CARMINE_DISP_REG_L5_WIN_POS		(0x0164)
#define CARMINE_DISP_REG_L1_TRANS		(0x01A4)
#define CARMINE_DISP_REG_L2_MODE_W_H		(0x0040)
#define CARMINE_DISP_REG_L6_DISP_ADR0		(0x1908)
#define CARMINE_DISP_REG_L3_MODE_W_H		(0x0058)
#define CARMINE_DISP_REG_L3_DISP_ADR1		(0x0060)
#define CARMINE_DISP_REG_L4PX			(0x18C4)
#define CARMINE_DISP_REG_L5RM			(0x18D0)
#define CARMINE_DISP_REG_L4_WIN_POS		(0x0154)
#define CARMINE_DISP_REG_L3RM			(0x18B0)
#define CARMINE_DISP_REG_L6RM			(0x1924)
#define CARMINE_DISP_REG_L4_DISP_POS		(0x0084)
#define CARMINE_DISP_REG_MLMR_TRANS		(0x00C0)
#define CARMINE_DISP_REG_L5_WIN_SIZE		(0x0168)
#define CARMINE_DISP_REG_L6PX			(0x1928)
#define CARMINE_DISP_REG_L1_WIN_SIZE		(0x0128)
#define CARMINE_DISP_REG_C_TRANS		(0x00BC)
#define CARMINE_DISP_REG_L2_TRANS		(0x01A8)
#define CARMINE_DISP_REG_L3PX			(0x18B4)
#define CARMINE_DISP_REG_L7_DISP_POS		(0x1954)
#define CARMINE_DISP_REG_L3PY			(0x18B8)
#define CARMINE_DISP_REG_L3_EXT_MODE		(0x0140)
#define CARMINE_DISP_REG_BLEND_MODE_L0		(0x00B4)
#define CARMINE_CURSOR_CUTZ_MASK		(0x00000100)
#define CARMINE_DISP_REG_L6_ORG_ADR1		(0x1904)
#define CARMINE_DISP_REG_BLEND_MODE_L1		(0x0188)
#define CARMINE_DISP_REG_L5_MODE_W_H		(0x0088)
#define CARMINE_DISP_REG_L7_DISP_ADR0		(0x1948)
#define CARMINE_DISP_REG_L0_MODE_W_H		(0x0020)
#define CARMINE_DISP_REG_BLEND_MODE_L2		(0x018C)
#define CARMINE_DISP_REG_L3_DISP_POS		(0x006C)
#define CARMINE_DISP_REG_L7PY			(0x196C)
#define CARMINE_DISP_REG_L2_WIN_SIZE		(0x0138)
#define CARMINE_CURSOR1_PRIORITY_MASK		(0x00020000)
#define CARMINE_DISP_REG_L2_DISP_POS		(0x0054)
#define CARMINE_DISP_REG_L4_TRANS		(0x01B0)
#define CARMINE_DISP_REG_L7_TRANS		(0x199c)
#define CARMINE_DISP_WIDTH_SHIFT		(16)
#define CARMINE_DISP_REG_L7_MODE_W_H		(0x1940)
#define CARMINE_DISP_REG_CUR1_POS		(0x00A8)
#define CARMINE_WINDOW_MODE		(0x00000001)
#define CARMINE_OVERLAY_EXT_MODE	(0x00000002)
#define CARMINE_EXTEND_MODE		(CARMINE_WINDOW_MODE | \
					CARMINE_OVERLAY_EXT_MODE)
#define CARMINE_DISP_REG_L6_WIN_POS		(0x191c)
#define CARMINE_DISP_REG_L6PY			(0x192C)
#define CARMINE_DISP_REG_L0_EXT_MODE		(0x0110)
#define CARMINE_DISP_REG_L3_WIN_POS		(0x0144)
#define CARMINE_DISP_REG_L0_TRANS		(0x01A0)
#define CARMINE_DISP_REG_L5_DISP_ADR1		(0x0090)
#define CARMINE_DISP_REG_L0_WIN_SIZE		(0x0118)
#define CARMINE_DISP_REG_L0PX			(0x1884)
#define CARMINE_DISP_REG_L2PY			(0x18A8)
#define CARMINE_DISP_REG_L0_DISP_ADR		(0x0028)
#define CARMINE_DISP_REG_L1_WIDTH		(0x0030)
#define CARMINE_DISP_REG_L5_EXT_MODE		(0x0160)
#define CARMINE_DISP_REG_BLEND_MODE_L4		(0x0194)
#define CARMINE_DISP_REG_BLEND_MODE_L3		(0x0190)
#define CARMINE_DISP_REG_L3_ORG_ADR1		(0x005C)
#define CARMINE_DISP_REG_L6_EXT_MODE		(0x1918)
#define CARMINE_DISP_REG_L2_ORG_ADR1		(0x0044)
#define CARMINE_DISP_WIDTH_UNIT			(64)
#define CARMINE_DISP_REG_BLEND_MODE_L6		(0x1990)
#define CARMINE_DISP_REG_L6_DISP_POS		(0x1914)
#define CARMINE_DISP_REG_L7_EXT_MODE		(0x1958)
#define CARMINE_DISP_REG_L4_ORG_ADR1		(0x0074)
#define CARMINE_DISP_REG_CUR2_POS		(0x00B0)
#define CARMINE_DISP_REG_L2_WIN_POS		(0x0134)
#define CARMINE_DISP_REG_L3_WIN_SIZE		(0x0148)
#define CARMINE_DISP_REG_L7_ORG_ADR1		(0x1944)
#define CARMINE_DISP_REG_BLEND_MODE_L5		(0x0198)
#define CARMINE_DISP_REG_L5_TRANS		(0x01B4)
#define CARMINE_DISP_REG_BLEND_MODE_L7		(0x1994)
#define CARMINE_DISP_REG_L0_DISP_POS		(0x002C)
#define CARMINE_DISP_REG_L7PX			(0x1968)
#define CARMINE_DISP_REG_L1_ORG_ADR		(0x0034)
#define CARMINE_DISP_REG_L2_DISP_ADR1		(0x0048)
#define CARMINE_DISP_REG_L4_WIN_SIZE		(0x0158)
#define CARMINE_DISP_REG_L2RM			(0x18A0)
#define CARMINE_DISP_REG_CURSOR_MODE		(0x00A0)
#define CARMINE_DISP_REG_L0_WIN_POS		(0x0114)
#define CARMINE_DISP_REG_L4_DISP_ADR1		(0x0078)
#define CARMINE_DISP_REG_L2_EXT_MODE		(0x0130)
#define CARMINE_DISP_REG_L5PX			(0x18D4)
#define CARMINE_DISP_REG_L6_TRANS		(0x1998)
#define CARMINE_DISP_REG_L7RM			(0x1964)
#define CARMINE_DISP_REG_L1_WIN_POS		(0x0124)
#define CARMINE_DISP_REG_L0_ORG_ADR		(0x0024)
#define CARMINE_DISP_REG_L5PY			(0x18D8)
#define CARMINE_DISP_REG_L4_MODE_W_H		(0x0070)
#define CARMINE_DISP_REG_L3_TRANS		(0x01AC)
#define CARMINE_DISP_REG_L0PY			(0x1888)
#define CARMINE_DISP_REG_L5_ORG_ADR1		(0x008C)
#define CARMINE_DISP_REG_L1_EXT_MODE		(0x0120)
#define CARMINE_CURSOR0_PRIORITY_MASK		(0x00010000)
#define CARMINE_DISP_REG_L2PX			(0x18A4)
#define CARMINE_DISP_WIN_H_SHIFT		(16)
#define CARMINE_DISP_REG_L4PY			(0x18C8)
#define CARMINE_DISP_REG_L5_DISP_POS		(0x009C)
#define CARMINE_DISP_REG_L4_EXT_MODE		(0x0150)
#define CARMINE_DISP_REG_L7_WIN_SIZE		(0x1960)
#define CARMINE_DISP_REG_L4RM			(0x18C0)
#define CARMINE_DISP_REG_L0RM			(0x1880)
#define CARMINE_DISP_REG_L7_WIN_POS		(0x195c)
#define CARMINE_DISP_REG_L6_MODE_W_H		(0x1900)
#define CARMINE_DISP_REG_L6_WIN_SIZE		(0x1920)
#define CARMINE_EXT_CMODE_DIRECT24_RGBA		(0xC0000000)
#define CARMINE_DISP_REG_H_TOTAL		(0x0004)
#define CARMINE_DISP_VDP_SHIFT			(16)
#define CARMINE_DISP_HTP_SHIFT			(16)
#define CARMINE_DISP_VSW_SHIFT			(24)
#define CARMINE_DISP_REG_V_TOTAL		(0x0010)
#define CARMINE_DISP_DCM_MASK			(0x0000FFFF)
#define CARMINE_DISP_REG_DCM1			(0x0100)
#define CARMINE_DISP_REG_H_PERIOD		(0x0008)
#define CARMINE_DISP_REG_V_PERIOD_POS		(0x0014)
#define CARMINE_L0E			(1 << 16)
#define CARMINE_DISP_REG_V_H_W_H_POS		(0x000C)
#define CARMINE_DEN			(1 << 31)
#define CARMINE_DISP_HDB_SHIFT			(16)
#define CARMINE_DISP_HSW_SHIFT			(16)
#define CARMINE_DISP_VTR_SHIFT			(16)
#define CARMINE_DFLT_IP_CLOCK_ENABLE		(0x03ff)
#define CARMINE_DISP1_REG		(0x00140000)
#define CARMINE_DCTL_REG		(0x00300000)
#define CARMINE_DCTL_REG_SETTIME1_EMODE		(0x04)
#define CARMINE_DFLT_IP_DCTL_MODE		(0x0121)
#define CARMINE_DCTL_REG_REFRESH_SETTIME2	(0x08)
#define CARMINE_WB_REG			(0x00180000)
#define CARMINE_GRAPH_REG		(0x00000000)
#define CARMINE_DCTL_REG_MODE_ADD		(0x00)
#define CARMINE_DFLT_IP_DCTL_IO_CONT0		(0x0555)
#define CARMINE_DFLT_IP_DCTL_ADD		(0x05c3)
#define CARMINE_DFLT_IP_DCTL_DDRIF2		(0x0055)
#define CARMINE_GRAPH_REG_VRINTM		(0x00028064)
#define CARMINE_GRAPH_REG_DC_OFFSET_TX		(0x0004006C)
#define CARMINE_DFLT_IP_DCTL_SET_TIME2		(0x2a22)
#define CARMINE_GRAPH_REG_DC_OFFSET_PY		(0x00040060)
#define CARMINE_GRAPH_REG_VRERRM		(0x0002806C)
#define CARMINE_DCTL_REG_RSV2_RSV1		(0x10)
#define CARMINE_GRAPH_REG_DC_OFFSET_LY		(0x00040068)
#define CARMINE_DISP0_REG		(0x00100000)
#define CARMINE_WB_REG_WBM_DEFAULT		(0x0001c020)
#define CARMINE_CTL_REG			(0x00400000)
#define CARMINE_DFLT_IP_DCTL_FIFO_DEPTH		(0x000f)
#define CARMINE_CTL_REG_SOFTWARE_RESET		(0x0010)
#define CARMINE_DFLT_IP_DCTL_STATES_AFT_RST	(0x0002)
#define CARMINE_DFLT_IP_DCTL_IO_CONT1		(0x0555)
#define CARMINE_DFLT_IP_DCTL_REFRESH		(0x0042)
#define CARMINE_DCTL_REG_DDRIF2_DDRIF1		(0x14)
#define CARMINE_DCTL_INIT_WAIT_LIMIT		(5000)
#define CARMINE_DCTL_REG_RSV0_STATES		(0x0C)
#define CARMINE_CTL_REG_CLOCK_ENABLE		(0x000C)
#define CARMINE_DCTL_REG_STATES_MASK		(0x000F)
#define CARMINE_GRAPH_REG_DC_OFFSET_PX		(0x0004005C)
#define CARMINE_DCTL_DLL_RESET			(1)
#define CARMINE_DFLT_IP_DCTL_SET_TIME1		(0x4749)
#define CARMINE_DFLT_IP_DCTL_RESERVE2		(0x0000)
#define CARMINE_DFLT_IP_DCTL_RESERVE0		(0x0020)
#define CARMINE_DCTL_INIT_WAIT_INTERVAL		(1)
#define CARMINE_DFLT_IP_DCTL_STATES		(0x0003)
#define CARMINE_WB_REG_WBM			(0x0004)
#define CARMINE_DCTL_REG_IOCONT1_IOCONT0	(0x24)
#define CARMINE_GRAPH_REG_DC_OFFSET_LX		(0x00040064)
#define CARMINE_DFLT_IP_DCTL_MODE_AFT_RST	(0x0021)
#define CARMINE_GRAPH_REG_DC_OFFSET_TY		(0x00040070)
#define CARMINE_DFLT_IP_DCTL_EMODE		(0x8000)
#define CARMINE_DFLT_IP_DCTL_DDRIF1		(0x6646)

#define CARMINE_DISPLAY_MEM	(800 * 600 * 4)
#define CARMINE_USE_DISPLAY0	(1 << 0)
#define MAX_DISPLAY	2
#define CARMINE_TOTAL_DISPLAY_MEM	(CARMINE_DISPLAY_MEM * MAX_DISPLAY)
#define CARMINE_MEMORY_BAR	2
#define CARMINE_CONFIG_BAR	3
#define CARMINE_USE_DISPLAY1	(1 << 1)
#define CARMINE_MEMORY_BAR_SIZE  (0x400000)   /* 4 MiB, power-of-2 */

/* ------------------------------------------------------------------ */
/* BAR metadata definition                                             */
/* ------------------------------------------------------------------ */
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

/* ------------------------------------------------------------------ */
/* Device State                                                        */
/* ------------------------------------------------------------------ */
struct PCIBaseState {
    PCIDevice parent_obj;

    /* BAR memory regions (MMIO/PIO unified handling fix) */
    MemoryRegion bar_regions[6];

    /* optional linear backing */
    uint8_t *mmio_backing;
    size_t mmio_backing_size;

    /* BAR table */
    BARInfo bar_info[6];
    int num_bars;
};

struct carmine_resolution {
	uint32_t htp;
	uint32_t hsp;
	uint32_t hsw;
	uint32_t hdp;
	uint32_t vtr;
	uint32_t vsp;
	uint32_t vsw;
	uint32_t vdp;
	uint32_t disp_mode;
};

/* ------------------------------------------------------------------ */
/* Forward declarations                                                */
/* ------------------------------------------------------------------ */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len);
static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len);
static void pcibase_reset(DeviceState *dev);
static void pcibase_realize(PCIDevice *pdev, Error **errp);
static void pcibase_uninit(PCIDevice *pdev);

/* ------------------------------------------------------------------ */
/* MemoryRegionOps                                                      */
/* ------------------------------------------------------------------ */
static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* ------------------------------------------------------------------ */
/* Helper: register a BAR (MMIO or PIO)                                 */
/* ------------------------------------------------------------------ */
static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE)
        return;

    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, bi->size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }

}

/* ------------------------------------------------------------------ */
/* MMIO / PIO handlers (device-specific code goes into placeholders)   */
/* ------------------------------------------------------------------ */

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    if (s->mmio_backing && addr + size <= s->mmio_backing_size) {
        uint64_t val = 0;
        memcpy(&val, s->mmio_backing + addr, size);

        /* 
         * The driver waits for CARMINE_DCTL_REG_STATES_MASK bits to clear 
         * in CARMINE_DCTL_REG_RSV0_STATES during initialization.
         * We simulate immediate completion by masking these bits on read.
         */
        if (addr == (CARMINE_DCTL_REG + CARMINE_DCTL_REG_RSV0_STATES) && size == 4) {
             val &= ~CARMINE_DCTL_REG_STATES_MASK;
        }
        return val;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] mmio_read addr=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (s->mmio_backing && addr + size <= s->mmio_backing_size) {
        memcpy(s->mmio_backing + addr, &val, size);
        return;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] mmio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, (uint64_t)val, size);
}

/* ------------------------------------------------------------------ */
/* Reset       */
/* ------------------------------------------------------------------ */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    /* core resets */
    pci_device_reset(pdev);

    if (s->mmio_backing && s->mmio_backing_size)
        memset(s->mmio_backing, 0, s->mmio_backing_size);
}

/* ------------------------------------------------------------------ */
/* PCI config space access (class-level hooks)                         */
/* ------------------------------------------------------------------ */
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    uint32_t val = pci_default_read_config(pdev, addr, len);

    switch (len) {
        case 1: val &= 0xFF; break;
        case 2: val &= 0xFFFF; break;
        case 4: break;
        default: break;
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

/* ------------------------------------------------------------------ */
/* Realize (device init)                                                */
/* ------------------------------------------------------------------ */
static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_FUJITU_LIMITED );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  CARMINE_PCI_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_DISPLAY_OTHER );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);
    
    s->num_bars = 4; /* Covering up to BAR 3 */
    
    /* BAR 2: Framebuffer Memory */
    s->bar_info[CARMINE_MEMORY_BAR].index = CARMINE_MEMORY_BAR;
    s->bar_info[CARMINE_MEMORY_BAR].type = BAR_TYPE_RAM;
    s->bar_info[CARMINE_MEMORY_BAR].size = CARMINE_MEMORY_BAR_SIZE;
    s->bar_info[CARMINE_MEMORY_BAR].name = "carminefb-mem";

    /* BAR 3: MMIO Registers */
    s->bar_info[CARMINE_CONFIG_BAR].index = CARMINE_CONFIG_BAR;
    s->bar_info[CARMINE_CONFIG_BAR].type = BAR_TYPE_MMIO;
    /* Max offset seen is 0x400000 (CARMINE_CTL_REG), so 8MB is safe */
    s->bar_info[CARMINE_CONFIG_BAR].size = 8 * MiB; 
    s->bar_info[CARMINE_CONFIG_BAR].name = "carminefb-config";

    /* Allocate backing store for MMIO registers */
    s->mmio_backing_size = s->bar_info[CARMINE_CONFIG_BAR].size;
    s->mmio_backing = g_malloc0(s->mmio_backing_size);

    /* register BARs */
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    qemu_log_mask(LOG_UNIMP, "[%s] device realized\n", TYPE_PCIBASE_DEVICE);
}

/* ------------------------------------------------------------------ */
/* Uninit/cleanup                                                      */
/* ------------------------------------------------------------------ */
static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (s->mmio_backing) {
        g_free(s->mmio_backing);
        s->mmio_backing = NULL;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] device uninit\n", TYPE_PCIBASE_DEVICE);
}

/* ------------------------------------------------------------------ */
/* Class init / type registration                                      */
/* ------------------------------------------------------------------ */
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
