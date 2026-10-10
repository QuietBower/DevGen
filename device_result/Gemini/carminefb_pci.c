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


#define TYPE_PCIBASE_DEVICE "carminefb_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define CARMINEFB_DEFAULT_VIDEO_MODE	1
#define PCI_VENDOR_ID_FUJITU_LIMITED 0x10cf
#define CARMINE_DISP_REG_L4_EXT_MODE		(0x0150)
#define CARMINE_DISP_REG_L5_TRANS		(0x01B4)
#define CARMINE_DISP_REG_L4PY			(0x18C8)
#define CARMINE_DISP_REG_L7_EXT_MODE		(0x1958)
#define CARMINE_DISP_REG_L2_DISP_ADR1		(0x0048)
#define CARMINE_DISP_REG_L3RM			(0x18B0)
#define CARMINE_DISP_REG_L0PX			(0x1884)
#define CARMINE_DISP_REG_BLEND_MODE_L5		(0x0198)
#define CARMINE_DISP_REG_L2RM			(0x18A0)
#define CARMINE_DISP_REG_L2_EXT_MODE		(0x0130)
#define CARMINE_DISP_REG_L7_WIN_POS		(0x195c)
#define CARMINE_DISP_REG_L0_MODE_W_H		(0x0020)
#define CARMINE_DISP_REG_L1_WIDTH		(0x0030)
#define CARMINE_DISP_REG_L5_DISP_ADR1		(0x0090)
#define CARMINE_DISP_REG_L0PY			(0x1888)
#define CARMINE_DISP_REG_L0RM			(0x1880)
#define CARMINE_DISP_REG_L3_ORG_ADR1		(0x005C)
#define CARMINE_DISP_REG_C_TRANS		(0x00BC)
#define CARMINE_DISP_REG_L3_TRANS		(0x01AC)
#define CARMINE_DISP_REG_L0_EXT_MODE		(0x0110)
#define CARMINE_DISP_REG_L5_MODE_W_H		(0x0088)
#define CARMINE_DISP_REG_L6_WIN_SIZE		(0x1920)
#define CARMINE_DISP_REG_L2_WIN_POS		(0x0134)
#define CARMINE_DISP_REG_L3PX			(0x18B4)
#define CARMINE_CURSOR1_PRIORITY_MASK		(0x00020000)
#define CARMINE_DISP_REG_BLEND_MODE_L1		(0x0188)
#define CARMINE_WINDOW_MODE		(0x00000001)
#define CARMINE_DISP_REG_L2_MODE_W_H		(0x0040)
#define CARMINE_DISP_REG_L7_TRANS		(0x199c)
#define CARMINE_DISP_REG_L2_DISP_POS		(0x0054)
#define CARMINE_DISP_REG_CUR1_POS		(0x00A8)
#define CARMINE_DISP_REG_L0_DISP_ADR		(0x0028)
#define CARMINE_DISP_REG_L4RM			(0x18C0)
#define CARMINE_DISP_REG_L3_MODE_W_H		(0x0058)
#define CARMINE_DISP_REG_L3PY			(0x18B8)
#define CARMINE_DISP_REG_MLMR_TRANS		(0x00C0)
#define CARMINE_OVERLAY_EXT_MODE	(0x00000002)
#define CARMINE_EXTEND_MODE		(CARMINE_WINDOW_MODE | CARMINE_OVERLAY_EXT_MODE)
#define CARMINE_DISP_REG_L4_ORG_ADR1		(0x0074)
#define CARMINE_DISP_REG_L6_EXT_MODE		(0x1918)
#define CARMINE_DISP_WIDTH_SHIFT		(16)
#define CARMINE_DISP_REG_BLEND_MODE_L6		(0x1990)
#define CARMINE_DISP_REG_L6_TRANS		(0x1998)
#define CARMINE_DISP_REG_L3_EXT_MODE		(0x0140)
#define CARMINE_DISP_REG_L0_TRANS		(0x01A0)
#define CARMINE_DISP_REG_L0_WIN_SIZE		(0x0118)
#define CARMINE_DISP_REG_L2PY			(0x18A8)
#define CARMINE_DISP_REG_L7_WIN_SIZE		(0x1960)
#define CARMINE_DISP_REG_L4_WIN_POS		(0x0154)
#define CARMINE_DISP_REG_L3_DISP_POS		(0x006C)
#define CARMINE_DISP_REG_BLEND_MODE_L2		(0x018C)
#define CARMINE_DISP_REG_L7RM			(0x1964)
#define CARMINE_DISP_REG_L3_WIN_POS		(0x0144)
#define CARMINE_DISP_REG_L5PY			(0x18D8)
#define CARMINE_DISP_REG_BLEND_MODE_L3		(0x0190)
#define CARMINE_DISP_REG_L0_WIN_POS		(0x0114)
#define CARMINE_DISP_REG_L6_MODE_W_H		(0x1900)
#define CARMINE_DISP_REG_L4PX			(0x18C4)
#define CARMINE_DISP_REG_L5RM			(0x18D0)
#define CARMINE_DISP_REG_L6PX			(0x1928)
#define CARMINE_DISP_REG_L2PX			(0x18A4)
#define CARMINE_DISP_REG_L2_WIN_SIZE		(0x0138)
#define CARMINE_DISP_REG_L4_DISP_ADR1		(0x0078)
#define CARMINE_DISP_REG_L4_MODE_W_H		(0x0070)
#define CARMINE_DISP_REG_L7_ORG_ADR1		(0x1944)
#define CARMINE_CURSOR_CUTZ_MASK		(0x00000100)
#define CARMINE_CURSOR0_PRIORITY_MASK		(0x00010000)
#define CARMINE_DISP_REG_L5_WIN_POS		(0x0164)
#define CARMINE_DISP_WIDTH_UNIT			(64)
#define CARMINE_DISP_REG_L7_DISP_ADR0		(0x1948)
#define CARMINE_DISP_REG_L7_MODE_W_H		(0x1940)
#define CARMINE_DISP_REG_L7_DISP_POS		(0x1954)
#define CARMINE_EXT_CMODE_DIRECT24_RGBA		(0xC0000000)
#define CARMINE_DISP_REG_L4_WIN_SIZE		(0x0158)
#define CARMINE_DISP_REG_L6_WIN_POS		(0x191c)
#define CARMINE_DISP_REG_L3_DISP_ADR1		(0x0060)
#define CARMINE_DISP_REG_BLEND_MODE_L0		(0x00B4)
#define CARMINE_DISP_REG_L4_TRANS		(0x01B0)
#define CARMINE_DISP_REG_L7PX			(0x1968)
#define CARMINE_DISP_REG_L1_ORG_ADR		(0x0034)
#define CARMINE_DISP_REG_L1_EXT_MODE		(0x0120)
#define CARMINE_DISP_REG_L5_DISP_POS		(0x009C)
#define CARMINE_DISP_REG_L0_DISP_POS		(0x002C)
#define CARMINE_DISP_REG_L0_ORG_ADR		(0x0024)
#define CARMINE_DISP_REG_L2_TRANS		(0x01A8)
#define CARMINE_DISP_REG_CURSOR_MODE		(0x00A0)
#define CARMINE_DISP_REG_BLEND_MODE_L4		(0x0194)
#define CARMINE_DISP_REG_L6PY			(0x192C)
#define CARMINE_DISP_REG_L5_ORG_ADR1		(0x008C)
#define CARMINE_DISP_REG_L2_ORG_ADR1		(0x0044)
#define CARMINE_DISP_REG_L5_WIN_SIZE		(0x0168)
#define CARMINE_DISP_WIN_H_SHIFT		(16)
#define CARMINE_DISP_REG_L5_EXT_MODE		(0x0160)
#define CARMINE_DISP_REG_L6_ORG_ADR1		(0x1904)
#define CARMINE_DISP_REG_L1_WIN_POS		(0x0124)
#define CARMINE_DISP_REG_L1_TRANS		(0x01A4)
#define CARMINE_DISP_REG_L6_DISP_ADR0		(0x1908)
#define CARMINE_DISP_REG_L7PY			(0x196C)
#define CARMINE_DISP_REG_L6RM			(0x1924)
#define CARMINE_DISP_REG_BLEND_MODE_L7		(0x1994)
#define CARMINE_DISP_REG_L3_WIN_SIZE		(0x0148)
#define CARMINE_DISP_REG_L4_DISP_POS		(0x0084)
#define CARMINE_DISP_REG_CUR2_POS		(0x00B0)
#define CARMINE_DISP_REG_L6_DISP_POS		(0x1914)
#define CARMINE_DISP_REG_L1_WIN_SIZE		(0x0128)
#define CARMINE_DISP_REG_L5PX			(0x18D4)
#define CARMINE_DISP_REG_H_TOTAL		(0x0004)
#define CARMINE_DISP_REG_V_PERIOD_POS		(0x0014)
#define CARMINE_DISP_HDB_SHIFT			(16)
#define CARMINE_DISP_REG_V_TOTAL		(0x0010)
#define CARMINE_DISP_HSW_SHIFT			(16)
#define CARMINE_DEN			(1 << 31)
#define CARMINE_DISP_REG_H_PERIOD		(0x0008)
#define CARMINE_DISP_VTR_SHIFT			(16)
#define CARMINE_DISP_REG_DCM1			(0x0100)
#define CARMINE_DISP_DCM_MASK			(0x0000FFFF)
#define CARMINE_DISP_VDP_SHIFT			(16)
#define CARMINE_L0E			(1 << 16)
#define CARMINE_DISP_HTP_SHIFT			(16)
#define CARMINE_DISP_REG_V_H_W_H_POS		(0x000C)
#define CARMINE_DISP_VSW_SHIFT			(24)
#define CARMINE_CTL_REG			(0x00400000)
#define CARMINE_DCTL_REG		(0x00300000)
#define CARMINE_DCTL_REG_MODE_ADD		(0x00)
#define CARMINE_DFLT_IP_DCTL_MODE		(0x0121)
#define CARMINE_DCTL_INIT_WAIT_LIMIT		(5000)
#define CARMINE_GRAPH_REG_VRINTM		(0x00028064)
#define CARMINE_DFLT_IP_DCTL_IO_CONT1		(0x0555)
#define CARMINE_DFLT_IP_DCTL_SET_TIME2		(0x2a22)
#define CARMINE_CTL_REG_CLOCK_ENABLE		(0x000C)
#define CARMINE_DFLT_IP_DCTL_DDRIF2		(0x0055)
#define CARMINE_DFLT_IP_DCTL_STATES_AFT_RST	(0x0002)
#define CARMINE_CTL_REG_SOFTWARE_RESET		(0x0010)
#define CARMINE_DFLT_IP_DCTL_EMODE		(0x8000)
#define CARMINE_GRAPH_REG_DC_OFFSET_PY		(0x00040060)
#define CARMINE_DFLT_IP_DCTL_REFRESH		(0x0042)
#define CARMINE_GRAPH_REG_DC_OFFSET_TX		(0x0004006C)
#define CARMINE_GRAPH_REG_DC_OFFSET_LY		(0x00040068)
#define CARMINE_DCTL_REG_STATES_MASK		(0x000F)
#define CARMINE_DFLT_IP_DCTL_DDRIF1		(0x6646)
#define CARMINE_WB_REG_WBM_DEFAULT		(0x0001c020)
#define CARMINE_DCTL_REG_REFRESH_SETTIME2	(0x08)
#define CARMINE_DFLT_IP_DCTL_STATES		(0x0003)
#define CARMINE_WB_REG			(0x00180000)
#define CARMINE_GRAPH_REG_DC_OFFSET_PX		(0x0004005C)
#define CARMINE_DFLT_IP_CLOCK_ENABLE		(0x03ff)
#define CARMINE_DCTL_REG_SETTIME1_EMODE		(0x04)
#define CARMINE_DFLT_IP_DCTL_SET_TIME1		(0x4749)
#define CARMINE_DCTL_REG_DDRIF2_DDRIF1		(0x14)
#define CARMINE_DISP1_REG		(0x00140000)
#define CARMINE_GRAPH_REG		(0x00000000)
#define CARMINE_GRAPH_REG_DC_OFFSET_TY		(0x00040070)
#define CARMINE_DISP0_REG		(0x00100000)
#define CARMINE_DCTL_REG_IOCONT1_IOCONT0	(0x24)
#define CARMINE_DCTL_INIT_WAIT_INTERVAL		(1)
#define CARMINE_DFLT_IP_DCTL_IO_CONT0		(0x0555)
#define CARMINE_DFLT_IP_DCTL_ADD		(0x05c3)
#define CARMINE_WB_REG_WBM			(0x0004)
#define CARMINE_DCTL_DLL_RESET			(1)
#define CARMINE_GRAPH_REG_VRERRM		(0x0002806C)
#define CARMINE_GRAPH_REG_DC_OFFSET_LX		(0x00040064)
#define CARMINE_DFLT_IP_DCTL_RESERVE0		(0x0020)
#define CARMINE_DCTL_REG_RSV2_RSV1		(0x10)
#define CARMINE_DCTL_REG_RSV0_STATES		(0x0C)
#define CARMINE_DFLT_IP_DCTL_FIFO_DEPTH		(0x000f)
#define CARMINE_DFLT_IP_DCTL_MODE_AFT_RST	(0x0021)
#define CARMINE_DFLT_IP_DCTL_RESERVE2		(0x0000)
#define MAX_DISPLAY	2
#define CARMINE_DISPLAY_MEM	(800 * 600 * 4)
#define CARMINE_TOTAL_DISPLAY_MEM	(CARMINE_DISPLAY_MEM * MAX_DISPLAY)
#define CARMINE_CONFIG_BAR	3
#define CARMINE_USE_DISPLAY0	(1 << 0)
#define CARMINE_MEMORY_BAR	2
#define CARMINE_USE_DISPLAY1	(1 << 1)

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
    uint32_t ctl_clock_enable;
    uint32_t disp0_dcm1;
    uint32_t disp1_dcm1;
    uint32_t ctl_software_reset;
    uint32_t dctl_iocont1_iocont0;
    uint32_t dctl_mode_add;
    uint32_t dctl_settime1_emode;
    uint32_t dctl_refresh_settime2;
    uint32_t dctl_rsv2_rsv1;
    uint32_t dctl_ddrif2_ddrif1;
    uint32_t dctl_rsv0_states;
    uint32_t wb_wbm;
    uint32_t graph_vrintm;
    uint32_t graph_vrerrm;
    uint32_t graph_dc_offset_px;
    uint32_t graph_dc_offset_py;
    uint32_t graph_dc_offset_lx;
    uint32_t graph_dc_offset_ly;
    uint32_t graph_dc_offset_tx;
    uint32_t graph_dc_offset_ty;
};

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case CARMINE_CTL_REG + CARMINE_CTL_REG_CLOCK_ENABLE:
        val = s->ctl_clock_enable;
        break;
    case CARMINE_CTL_REG + CARMINE_CTL_REG_SOFTWARE_RESET:
        val = s->ctl_software_reset;
        break;
    case CARMINE_DISP0_REG + CARMINE_DISP_REG_DCM1:
        val = s->disp0_dcm1;
        break;
    case CARMINE_DISP1_REG + CARMINE_DISP_REG_DCM1:
        val = s->disp1_dcm1;
        break;
    case CARMINE_DCTL_REG + CARMINE_DCTL_REG_IOCONT1_IOCONT0:
        val = s->dctl_iocont1_iocont0;
        break;
    case CARMINE_DCTL_REG + CARMINE_DCTL_REG_MODE_ADD:
        val = s->dctl_mode_add;
        break;
    case CARMINE_DCTL_REG + CARMINE_DCTL_REG_SETTIME1_EMODE:
        val = s->dctl_settime1_emode;
        break;
    case CARMINE_DCTL_REG + CARMINE_DCTL_REG_REFRESH_SETTIME2:
        val = s->dctl_refresh_settime2;
        break;
    case CARMINE_DCTL_REG + CARMINE_DCTL_REG_RSV2_RSV1:
        val = s->dctl_rsv2_rsv1;
        break;
    case CARMINE_DCTL_REG + CARMINE_DCTL_REG_DDRIF2_DDRIF1:
        val = s->dctl_ddrif2_ddrif1;
        break;
    case CARMINE_DCTL_REG + CARMINE_DCTL_REG_RSV0_STATES:
        val = s->dctl_rsv0_states;
        break;
    case CARMINE_WB_REG + CARMINE_WB_REG_WBM:
        val = s->wb_wbm;
        break;
    case CARMINE_GRAPH_REG + CARMINE_GRAPH_REG_VRINTM:
        val = s->graph_vrintm;
        break;
    case CARMINE_GRAPH_REG + CARMINE_GRAPH_REG_VRERRM:
        val = s->graph_vrerrm;
        break;
    case CARMINE_GRAPH_REG + CARMINE_GRAPH_REG_DC_OFFSET_PX:
        val = s->graph_dc_offset_px;
        break;
    case CARMINE_GRAPH_REG + CARMINE_GRAPH_REG_DC_OFFSET_PY:
        val = s->graph_dc_offset_py;
        break;
    case CARMINE_GRAPH_REG + CARMINE_GRAPH_REG_DC_OFFSET_LX:
        val = s->graph_dc_offset_lx;
        break;
    case CARMINE_GRAPH_REG + CARMINE_GRAPH_REG_DC_OFFSET_LY:
        val = s->graph_dc_offset_ly;
        break;
    case CARMINE_GRAPH_REG + CARMINE_GRAPH_REG_DC_OFFSET_TX:
        val = s->graph_dc_offset_tx;
        break;
    case CARMINE_GRAPH_REG + CARMINE_GRAPH_REG_DC_OFFSET_TY:
        val = s->graph_dc_offset_ty;
        break;
    default:
        val = 0;
        break;
    }
    
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case CARMINE_CTL_REG + CARMINE_CTL_REG_CLOCK_ENABLE:
        s->ctl_clock_enable = val;
        break;
    case CARMINE_CTL_REG + CARMINE_CTL_REG_SOFTWARE_RESET:
        s->ctl_software_reset = val;
        break;
    case CARMINE_DISP0_REG + CARMINE_DISP_REG_DCM1:
        s->disp0_dcm1 = val;
        break;
    case CARMINE_DISP1_REG + CARMINE_DISP_REG_DCM1:
        s->disp1_dcm1 = val;
        break;
    case CARMINE_DCTL_REG + CARMINE_DCTL_REG_IOCONT1_IOCONT0:
        s->dctl_iocont1_iocont0 = val;
        break;
    case CARMINE_DCTL_REG + CARMINE_DCTL_REG_MODE_ADD:
        s->dctl_mode_add = val;
        break;
    case CARMINE_DCTL_REG + CARMINE_DCTL_REG_SETTIME1_EMODE:
        s->dctl_settime1_emode = val;
        break;
    case CARMINE_DCTL_REG + CARMINE_DCTL_REG_REFRESH_SETTIME2:
        s->dctl_refresh_settime2 = val;
        break;
    case CARMINE_DCTL_REG + CARMINE_DCTL_REG_RSV2_RSV1:
        s->dctl_rsv2_rsv1 = val;
        break;
    case CARMINE_DCTL_REG + CARMINE_DCTL_REG_DDRIF2_DDRIF1:
        s->dctl_ddrif2_ddrif1 = val;
        break;
    case CARMINE_DCTL_REG + CARMINE_DCTL_REG_RSV0_STATES:
        /* The driver waits for (val & CARMINE_DCTL_REG_STATES_MASK) == 0 */
        s->dctl_rsv0_states = val & ~CARMINE_DCTL_REG_STATES_MASK;
        break;
    case CARMINE_WB_REG + CARMINE_WB_REG_WBM:
        s->wb_wbm = val;
        break;
    case CARMINE_GRAPH_REG + CARMINE_GRAPH_REG_VRINTM:
        s->graph_vrintm = val;
        break;
    case CARMINE_GRAPH_REG + CARMINE_GRAPH_REG_VRERRM:
        s->graph_vrerrm = val;
        break;
    case CARMINE_GRAPH_REG + CARMINE_GRAPH_REG_DC_OFFSET_PX:
        s->graph_dc_offset_px = val;
        break;
    case CARMINE_GRAPH_REG + CARMINE_GRAPH_REG_DC_OFFSET_PY:
        s->graph_dc_offset_py = val;
        break;
    case CARMINE_GRAPH_REG + CARMINE_GRAPH_REG_DC_OFFSET_LX:
        s->graph_dc_offset_lx = val;
        break;
    case CARMINE_GRAPH_REG + CARMINE_GRAPH_REG_DC_OFFSET_LY:
        s->graph_dc_offset_ly = val;
        break;
    case CARMINE_GRAPH_REG + CARMINE_GRAPH_REG_DC_OFFSET_TX:
        s->graph_dc_offset_tx = val;
        break;
    case CARMINE_GRAPH_REG + CARMINE_GRAPH_REG_DC_OFFSET_TY:
        s->graph_dc_offset_ty = val;
        break;
    default:
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
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

    s->ctl_clock_enable = 0;
    s->disp0_dcm1 = 0;
    s->disp1_dcm1 = 0;
    s->ctl_software_reset = 0;
    s->dctl_iocont1_iocont0 = 0;
    s->dctl_mode_add = 0;
    s->dctl_settime1_emode = 0;
    s->dctl_refresh_settime2 = 0;
    s->dctl_rsv2_rsv1 = 0;
    s->dctl_ddrif2_ddrif1 = 0;
    s->dctl_rsv0_states = 0;
    s->wb_wbm = 0;
    s->graph_vrintm = 0;
    s->graph_vrerrm = 0;
    s->graph_dc_offset_px = 0;
    s->graph_dc_offset_py = 0;
    s->graph_dc_offset_lx = 0;
    s->graph_dc_offset_ly = 0;
    s->graph_dc_offset_tx = 0;
    s->graph_dc_offset_ty = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_FUJITU_LIMITED );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x202b );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_DISPLAY_VGA );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 2;
    s->bar_info[0].index = CARMINE_MEMORY_BAR;
    s->bar_info[0].type = BAR_TYPE_RAM;
    s->bar_info[0].size = 256 * MiB;
    s->bar_info[0].name = "carmine-mem";
  
    s->bar_info[1].index = CARMINE_CONFIG_BAR;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = 8 * MiB;
    s->bar_info[1].name = "carmine-config";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "carminefb_pci",
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
