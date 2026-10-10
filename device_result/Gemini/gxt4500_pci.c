/*
 * Integrated QEMU PCI device template (QEMU 8.2.10).
 * Designed for register-level modeling and PCIe driver probing.
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

enum gxt_cards {
	GXT4500P,
	GXT6500P,
	GXT4000P,
	GXT6000P
};

struct cardinfo {
	int	refclk_ps;	/* period of PLL reference clock in ps */
	const char *cardname;
};

#define TYPE_PCIBASE_DEVICE "gxt4500_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_IBM 0x1014
#define PCI_DEVICE_ID_IBM_GXT4500P 0x021c

#define SYNC_CTL		0x4034
#define STATUS			0x1000
#define CFG_ENDIAN0		0x40
#define CTRL_REG0		0x1004
#define CR0_HALT_DMA			0x4
#define CR0_RASTER_RESET		0x8
#define CR0_GEOM_RESET		0x10
#define CR0_MEM_CTRLER_RESET		0x20
#define FB_AB_CTRL		0x1100
#define FB_CD_CTRL		0x1104
#define FB_WID_CTRL		0x1108
#define FB_Z_CTRL		0x110c
#define FB_VGA_CTRL		0x1110
#define REFRESH_AB_CTRL		0x1114
#define REFRESH_CD_CTRL		0x1118
#define FB_OVL_CTRL		0x111c
#define FB_CTRL_TYPE			0x80000000
#define FB_CTRL_WIDTH_MASK		0x007f0000
#define FB_CTRL_WIDTH_SHIFT		16
#define FB_CTRL_START_SEG_MASK	0x00003fff
#define REFRESH_START		0x1098
#define REFRESH_SIZE		0x109c
#define DFA_FB_A		0x11e0
#define DFA_FB_B		0x11e4
#define DFA_FB_C		0x11e8
#define DFA_FB_D		0x11ec
#define DFA_FB_ENABLE			0x80000000
#define DFA_FB_BASE_MASK		0x03f00000
#define DFA_FB_STRIDE_1k		0x00000000
#define DFA_FB_STRIDE_2k		0x00000010
#define DFA_FB_STRIDE_4k		0x00000020
#define DFA_PIX_8BIT			0x00000000
#define DFA_PIX_16BIT_565		0x00000001
#define DFA_PIX_16BIT_1555		0x00000002
#define DFA_PIX_24BIT			0x00000004
#define DFA_PIX_32BIT			0x00000005
#define DTG_CONTROL		0x1900
#define DTG_CTL_SCREEN_REFRESH	2
#define DTG_CTL_ENABLE		1
#define DTG_HORIZ_EXTENT	0x1904
#define DTG_HORIZ_DISPLAY	0x1908
#define DTG_HSYNC_START		0x190c
#define DTG_HSYNC_END		0x1910
#define DTG_HSYNC_END_COMP	0x1914
#define DTG_VERT_EXTENT		0x1918
#define DTG_VERT_DISPLAY	0x191c
#define DTG_VSYNC_START		0x1920
#define DTG_VSYNC_END		0x1924
#define DTG_VERT_SHORT		0x1928
#define DISP_CTL		0x402c
#define DISP_CTL_OFF			2
#define SYNC_CTL_SYNC_ON_RGB		1
#define SYNC_CTL_SYNC_OFF		2
#define SYNC_CTL_HSYNC_INV		8
#define SYNC_CTL_VSYNC_INV		0x10
#define SYNC_CTL_HSYNC_OFF		0x20
#define SYNC_CTL_VSYNC_OFF		0x40
#define PLL_M			0x4040
#define PLL_N			0x4044
#define PLL_POSTDIV		0x4048
#define PLL_C			0x404c
#define CURSOR_X		0x4078
#define CURSOR_Y		0x407c
#define CURSOR_HOTSPOT		0x4080
#define CURSOR_MODE_OFF		0
#define CURSOR_MODE_4BPP		1
#define CURSOR_PIXMAP		0x5000
#define CURSOR_CMAP		0x7400
#define WAT_FMT			0x4100
#define WAT_FMT_24BIT			0
#define WAT_FMT_16BIT_565		1
#define WAT_FMT_16BIT_1555		2
#define WAT_FMT_32BIT			3
#define WAT_FMT_8BIT_332		9
#define WAT_FMT_8BIT			0xa
#define WAT_FMT_NO_CMAP		4
#define WAT_CMAP_OFFSET		0x4104
#define WAT_CTRL		0x4108
#define WAT_CTRL_SEL_B		1
#define WAT_CTRL_NO_INC		2
#define WAT_GAMMA_CTRL		0x410c
#define WAT_GAMMA_DISABLE		1
#define WAT_OVL_CTRL		0x430c
#define CMAP			0x6000
#define CURSOR_MODE		0x4084

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

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t regs[0x20000 / 4];
};

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr < 0x20000) {
        val = s->regs[addr / 4];
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < 0x20000) {
        s->regs[addr / 4] = val;
    }
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
    memset(s->regs, 0, sizeof(s->regs));
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x1014 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x021c );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0380 );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);

    /* Allow driver to write to CFG_ENDIAN0 registers in PCI config space */
    pci_set_long(pdev->wmask + CFG_ENDIAN0, 0xffffffff);
    pci_set_long(pdev->wmask + CFG_ENDIAN0 + 4, 0xffffffff);
    pci_set_long(pdev->wmask + CFG_ENDIAN0 + 8, 0xffffffff);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 2;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x20000;
    s->bar_info[0].name = "gxt4500-mmio";
  
    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_RAM;
    s->bar_info[1].size = 0x800000; /* 8MB placeholder for FB */
    s->bar_info[1].name = "gxt4500-fb";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    (void)s;
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "gxt4500_pci",
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
