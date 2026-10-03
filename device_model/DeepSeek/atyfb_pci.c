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


#define TYPE_PCIBASE_DEVICE "atyfb_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define M64F_GX                 0x00000400
#define M64F_CT                 0x00000800
#define M64F_INTEGRATED         0x00000040
#define M64F_CT_BUS             0x00000080
#define M64F_MAGIC_FIFO         0x00000002
#define M64F_VT_BUS             0x00000100
#define M64F_VT                 0x00001000
#define M64F_GT                 0x00002000
#define M64F_EXTRA_BRIGHT       0x00020000
#define M64F_GTB_DSP            0x00000004
#define M64F_SDRAM_MAGIC_PLL    0x00000010
#define M64F_HW_TRIPLE          0x00200000
#define M64F_RESET_3D           0x00000001
#define M64F_FIFO_32            0x00000008
#define M64F_XL_DLL             0x00080000
#define M64F_MFB_FORCE_4        0x00100000
#define M64F_XL_MEM             0x00400000
#define M64F_MOBIL_BUS          0x00000200
#define M64F_MAGIC_POSTDIV      0x00000020
#define PCI_CHIP_MACH64GX       0x4758
#define PCI_CHIP_MACH64CX       0x4358
#define PCI_CHIP_MACH64GT       0x4754
#define PCI_CHIP_MACH64VT       0x5654
#define M64F_G3_PB_1_1          0x00008000
#define M64F_MAGIC_VRAM_SIZE    0x00004000
#define M64F_LT_LCD_REGS        0x00040000

/* Additional register offsets from atyfb_pci_probe context */
#define CNFG_PANEL_LG           0x0074
#define LCD_GEN_CNTL_LG         0x00D4
#define DSTN_CONTROL_LG         0x003C
#define HFB_PITCH_ADDR_LG       0x00A8
#define HORZ_STRETCHING_LG      0x00C8
#define VERT_STRETCHING_LG      0x00CC
#define LT_GIO_LG               0x00BC
#define POWER_MANAGEMENT_LG     0x00D8
#define CRTC_GEN_CNTL           0x001C
#define CRTC_H_SYNC_STRT_WID    0x0004
#define CRTC_V_TOTAL            0x0008
#define CRTC_V_SYNC_STRT_WID    0x000C
#define CRTC_VLINE_CRNT_VLINE   0x0010
#define CRTC_OFF_PITCH          0x0014
#define DP_PIX_WIDTH            0x02D0
#define DP_CHAIN_MASK           0x02CC
#define CLOCK_CNTL              0x0090
#define DAC_CNTL                0x00C4
#define MEM_CNTL                0x00B0
#define BUS_CNTL                0x00A0
#define CRTC_INT_CNTL           0x0018

/* New definitions from supplementary source */
#define CNFG_CHIP_ID		0x00E0
#define CNFG_STAT0		0x00E4
#define CRTC_VBLANK_INT		0x00000004
#define CRTC_VBLANK_INT_AK	CRTC_VBLANK_INT
#define CRTC_VBLANK_INT_EN	0x00000002
#define CRTC_VLINE_INT_EN	0x00000008
#define SNAPSHOT_INT_EN		0x00000080
#define I2C_INT_EN		0x00000200
#define CRTC2_VBLANK_INT_EN	0x00001000
#define CRTC2_VLINE_INT_EN	0x00004000
#define CAPBUF0_INT_EN		0x00010000
#define CAPBUF1_INT_EN		0x00040000
#define OVERLAY_EOF_INT_EN	0x00100000
#define ONESHOT_CAP_INT_EN	0x00400000
#define BUSMASTER_EOL_INT_EN	0x01000000
#define GP_INT_EN		0x04000000
#define SNAPSHOT2_INT_EN	0x20000000

#define CRTC_INT_EN_MASK	(CRTC_VBLANK_INT_EN |	\
				 CRTC_VLINE_INT_EN |	\
				 SNAPSHOT_INT_EN |	\
				 I2C_INT_EN |		\
				 CRTC2_VBLANK_INT_EN |	\
				 CRTC2_VLINE_INT_EN |	\
				 CAPBUF0_INT_EN |	\
				 CAPBUF1_INT_EN |	\
				 OVERLAY_EOF_INT_EN |	\
				 ONESHOT_CAP_INT_EN |	\
				 BUSMASTER_EOL_INT_EN |	\
				 GP_INT_EN |		\
				 SNAPSHOT2_INT_EN)


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
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint8_t mmio_regs[0x1000];  /* Shadow of MMIO registers, size matches BAR2 mapping */

    /* DMA Context */

};


/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t int_cntl = *(uint32_t *)(s->mmio_regs + CRTC_INT_CNTL);
    bool raise = false;

    if (int_cntl & CRTC_VBLANK_INT & CRTC_VBLANK_INT_EN) {
        raise = true;
    }

    if (raise) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* Device-initiated DMA logic based on driver access patterns - removed as driver does not use DMA */

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= 0x400) {
        memcpy(&val, s->mmio_regs + (addr - 0x400), size);
    } else {
        /* Addresses below 0x400 return 0 for GX chips */
        val = 0;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t old_val, new_val;
    uint32_t status_mask;

    if (addr < 0x400) {
        /* Ignore writes to addresses below 0x400 for GX */
        return;
    }

    /* Translate address to register offset */
    uint32_t reg_offset = addr - 0x400;

    /* Handle special registers with side effects */
    switch (reg_offset) {
    case CRTC_INT_CNTL:
        if (size != 4) {
            /* Unsupported access size; fall through to generic write */
            break;
        }
        old_val = *(uint32_t *)(s->mmio_regs + CRTC_INT_CNTL);
        status_mask = ~CRTC_INT_EN_MASK;
        /* Clear status bits that are written with '1' */
        new_val = old_val & ~(val & status_mask);
        /* Update enable bits to the written value */
        new_val = (new_val & ~CRTC_INT_EN_MASK) | (val & CRTC_INT_EN_MASK);
        *(uint32_t *)(s->mmio_regs + CRTC_INT_CNTL) = new_val;

        pcibase_update_irq(s);
        return;

    default:
        break;
    }

    /* Generic write to shadow registers */
    memcpy(s->mmio_regs + reg_offset, &val, size);
    
    /* Handle any other side-effects if needed */
    if (reg_offset == CRTC_INT_CNTL) {
        pcibase_update_irq(s);
    }
}

/* PIO handlers removed as driver does not reference PIO access */

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

    /* Zero all registers */
    memset(s->mmio_regs, 0, sizeof(s->mmio_regs));

    /* Set default chip identification: Rev 1, type 0x00d7 (Mach64 GX) */
    *(uint32_t *)(s->mmio_regs + CNFG_CHIP_ID) = 0x010000d7;
    /* Set CNFG_STAT0: GX chip only, bus type = 0 (PCI), ram type = 0 */
    *(uint32_t *)(s->mmio_regs + CNFG_STAT0) = M64F_GX;

    /* Ensure IRQ is deasserted */
    pcibase_update_irq(s);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x1002 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x4758 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x030000 );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    /* BAR0: Frame buffer memory, 8 MiB */
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_RAM;
    s->bar_info[0].size = 0x800000;
    s->bar_info[0].name = "atyfb-fb";

    /* BAR2: MMIO register aperture, 4 KiB */
    s->bar_info[2].index = 2;
    s->bar_info[2].type = BAR_TYPE_MMIO;
    s->bar_info[2].size = 0x1000;
    s->bar_info[2].name = "atyfb-regs";

    s->num_bars = 2;
    for (int i = 0; i < 6; i++) {
        /* Only register bars that have been initialized (index >= 0 and type != NONE) */
        if (s->bar_info[i].type != BAR_TYPE_NONE) {
            pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
        }
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
    .name = "atyfb_pci",
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
