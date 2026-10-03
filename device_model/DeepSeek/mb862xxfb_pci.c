/* QEMU 8.2.10 virtual PCI device for Fujitsu MB862xx Coral-P/PA (fbdev) */

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
#include "hw/qdev-properties.h"
#include "hw/qdev-core.h"
#include "migration/vmstate.h"

/* Static hardware identifiers from the driver's first pci_device_id entry */
#define PCI_VENDOR_ID_MB862XX  0x10cf
#define PCI_DEVICE_ID_MB862XX  0x2019
#define PCI_CLASS_MB862XX      0x038000 /* Display controller */
#define CORALP_MEM_SIZE        0x2000000
#define BAR0_SIZE              0x4000000   /* 64 MiB, covers framebuffer + MMIO */

/* Register offsets and constants from mb862xxfbdrv.c */
#define GC_L0PAL0              0x00000400
#define GC_L0M_L0W_UNIT        64
#define GC_L0WY_L0WX           0x00000114
#define GC_DCM01_ESY           0x00000004
#define GC_L0EM                0x00000110
#define GC_WY_WX               0x00000018
#define GC_VTR                 0x00000010
#define GC_L0DY_L0DX           0x0000002c
#define GC_L0DA0               0x00000028
#define GC_DCM01_CKS           0x00008000
#define GC_CPM_CEN1            0x00200000
#define GC_L0WH_L0WW           0x00000118
#define GC_HTP                 0x00000004
#define GC_VSW_HSW_HSP         0x0000000c
#define GC_DCM01_L0E           0x00010000
#define GC_L0EM_L0EC_24        0x40000000
#define GC_DCM1                0x00000100
#define GC_L0M_L0C_16          0x80000000
#define GC_L0OA0               0x00000024
#define GC_CPM_CEN0            0x00100000
#define GC_DCM01_DEN           0x80000000
#define GC_CPM_CUTC            0x000000a0
#define GC_VDP_VSP             0x00000014
#define GC_HDB_HDP             0x00000008
#define GC_WH_WW               0x0000001c
#define GC_DCM01_SC            0x00003f00
#define GC_DCM01_RESV          0x00004000
#define GC_L0M                 0x00000020
#define GC_VCM_CM              0x03000000
#define GC_CAP_CBM             0x00000010
#define GC_L1WH_L1WW           0x00000128
#define GC_L1M                 0x00000030
#define GC_DCM1_L1E            0x00020000
#define GC_DCM1_DEN            0x80000000
#define GC_CBM_HRV             0x00000010
#define GC_VCM_VS_PAL          0x00000002
#define GC_CAP_IMG_END         0x00000020
#define GC_VCM_VIE             0x80000000
#define GC_L1M_CS              0x20000000
#define GC_CAP_IMG_START       0x0000001C
#define GC_L1DA                0x00000034
#define GC_CAP_CSC             0x00000004
#define GC_CAP_VCM             0x00000000
#define GC_L1EM                0x00000120
#define GC_L1EM_DM             0x02000000
#define GC_DLS                 0x00000180
#define GC_L1M_YC              0x40000000
#define GC_L1WY_L1WX           0x00000124
#define GC_CAP_CMSS            0x00000048
#define GC_CAP_CMDS            0x0000004C
#define GC_L1M_16              0x80000000
#define GC_CBM_OO              0x80000000
#define GC_CAP_CBOA            0x00000014
#define GC_CBM_CBST            0x00000001
#define GC_CAP_CBLA            0x00000018
#define GC_DCM0                0x00000000
#define GC_CUY1_CUX1           0x000000b0
#define GC_IST                 0x00000020
#define GC_CTRL_STATUS         0x00000000
#define GC_CTRL_INT_MASK       0x00000004
#define GC_IMASK               0x00000024
#define MB862XX_MMIO_BASE      0x01fc0000
#define MB862XX_MMIO_HIGH_BASE 0x03fc0000
#define MB862XX_MMIO_SIZE      0x40000
#define GC_CCF                 0x00000038
#define MB862XX_CAP_BASE       0x00018000
#define GC_CID_CNAME_MSK       0x0000ff00
#define GC_DISP_REFCLK_400     400
#define GC_CID                 0x000000f0
#define MB862XX_GEO_BASE       0x00038000
#define GC_RSW                 0x0000005c
#define GC_MMR                 0x0000fffc
#define GC_CCF_CGE_166         0x00080000
#define MB862XX_DRAW_BASE      0x00030000
#define GC_CCF_COT_133         0x00010000
#define MB862XX_DISP_BASE      0x00010000
#define GC_MMR_CORALP_EVB_VAL  0x11d7fa13
#define GC_CID_VERSION_MSK     0x000000ff
#define MB862XX_I2C_BASE       0x0000c000
#define GC_DCTL_RSV0_STATES    0x0000000C
#define GC_EVB_DCTL_SETTIME1_EMODE    0x47498000
#define GC_EVB_DCTL_MODE_ADD          0x012105c3
#define GC_DCTL_DDRIF2_DDRIF1 0x00000014
#define GC_EVB_DCTL_DDRIF2_DDRIF1    0x00556646
#define GC_EVB_DCTL_RSV0_STATES_AFT_RST 0x00200002
#define GC_DCTL_REFRESH_SETTIME2    0x00000008
#define GC_EVB_DCTL_IOCONT1_IOCONT0  0x05550555
#define GC_DCTL_RSV2_RSV1      0x00000010
#define GC_DCTL_MODE_ADD       0x00000000
#define GC_EVB_DCTL_REFRESH_SETTIME2  0x00422a22
#define GC_DCTL_INIT_WAIT_CNT  3000
#define GC_DCTL_IOCONT1_IOCONT0      0x00000024
#define GC_EVB_DCTL_RSV0_STATES       0x00200003
#define GC_EVB_DCTL_MODE_ADD_AFT_RST  0x002105c3
#define GC_DCTL_SETTIME1_EMODE        0x00000004
#define GC_DCTL_INIT_WAIT_INTERVAL    1
#define GC_DCTL_STATES_MSK   0x0000000f
#define GC_EVB_DCTL_RSV2_RSV1         0x0000000f
#define GC_CTRL_CLK_EN_DRAM   0x00000001
#define MB86297_CAP0_BASE      0x00200000
#define MB86297_DISP0_BASE     0x00100000
#define MB86297_DRAW_BASE      0x00020000
#define GC_CTRL_CLK_EN_2D3D    0x00000002
#define MB86297_CAP1_BASE       0x00280000
#define GC_2D3D_REV            0x000004b4
#define GC_RE_REVISION         0x24240200
#define GC_CTRL_CLK_EN_DISP0   0x00000020
#define MB86297_I2C_BASE        0x00500000
#define MB86297_CTRL_BASE       0x00400000
#define GC_DISP_REFCLK_533     533
#define MB86297_DRAMCTRL_BASE   0x00300000
#define MB86297_WRBACK_BASE     0x00180000
#define GC_CTRL_CLK_ENABLE      0x0000000c
#define MB86297_DISP1_BASE      0x00140000
#define GC_INT_EN               0x00000000
#define MB862XX_PIO_BASE        0x00028000
#define GC_CARMINE_INT_EN       0x00000004

#define TYPE_PCIBASE_DEVICE "mb862xxfb_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

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

    uint32_t mmio[MB862XX_MMIO_SIZE / 4];
    uint8_t fb_mem[CORALP_MEM_SIZE];
    
    bool regs_relocated; /* tracks if registers have been relocated to high base via GC_RSW */
};

/* Update IRQ line based on IST and IMASK (legacy IRQ only) */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t ist = s->mmio[GC_IST / 4];
    uint32_t imask = s->mmio[GC_IMASK / 4];
    if (ist & imask) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Handle high MMIO region (registers) */
    if (addr >= MB862XX_MMIO_HIGH_BASE && addr < MB862XX_MMIO_HIGH_BASE + MB862XX_MMIO_SIZE) {
        hwaddr reg_offset = addr - MB862XX_MMIO_HIGH_BASE;
        if (size == 4 && (reg_offset % 4) == 0) {
            uint32_t idx = reg_offset >> 2;
            if (idx < ARRAY_SIZE(s->mmio)) {
                if (reg_offset == GC_CID) {
                    val = 0x00000308; /* Coral-PA: CNAME=3, Rev.8 */
                } else if (reg_offset == GC_2D3D_REV) {
                    val = GC_RE_REVISION; /* 0x24240200 */
                } else {
                    val = s->mmio[idx];
                }
            } else {
                qemu_log_mask(LOG_GUEST_ERROR, "mb862xxfb: MMIO read out of bounds at offset 0x%lx\n", reg_offset);
            }
        } else {
            qemu_log_mask(LOG_UNIMP, "mb862xxfb: MMIO read with unsupported size %d at 0x%lx\n", size, addr);
            val = 0xFFFFFFFF;
        }
    }
    /* Handle low MMIO region (only used for GC_RSW relocation write, reads return 0 after relocation) */
    else if (addr >= MB862XX_MMIO_BASE && addr < MB862XX_MMIO_BASE + MB862XX_MMIO_SIZE) {
        hwaddr reg_offset = addr - MB862XX_MMIO_BASE;
        if (size == 4 && (reg_offset % 4) == 0) {
            /* After relocation, low MMIO registers are not accessible; return 0 */
            val = 0;
        } else {
            qemu_log_mask(LOG_UNIMP, "mb862xxfb: Unimplemented low MMIO read at 0x%lx\n", addr);
        }
    }
    /* Framebuffer memory region */
    else if (addr < CORALP_MEM_SIZE) {
        switch (size) {
        case 1:
            val = s->fb_mem[addr];
            break;
        case 2:
            val = lduw_le_p(&s->fb_mem[addr]);
            break;
        case 4:
            val = ldl_le_p(&s->fb_mem[addr]);
            break;
        case 8:
            val = ldq_le_p(&s->fb_mem[addr]);
            break;
        default:
            val = 0;
            break;
        }
    } else {
        val = 0;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= MB862XX_MMIO_HIGH_BASE && addr < MB862XX_MMIO_HIGH_BASE + MB862XX_MMIO_SIZE) {
        hwaddr reg_offset = addr - MB862XX_MMIO_HIGH_BASE;
        if (size == 4 && (reg_offset % 4) == 0) {
            uint32_t idx = reg_offset >> 2;
            if (idx < ARRAY_SIZE(s->mmio)) {
                if (reg_offset == GC_CID || reg_offset == GC_2D3D_REV) {
                    /* Read-only */
                    return;
                } else if (reg_offset == GC_IST) {
                    /* Write-0-to-clear */
                    s->mmio[idx] &= val;
                    pcibase_update_irq(s);
                } else if (reg_offset == GC_IMASK) {
                    s->mmio[idx] = val;
                    pcibase_update_irq(s);
                } else {
                    s->mmio[idx] = val;
                }
            } else {
                qemu_log_mask(LOG_GUEST_ERROR, "mb862xxfb: MMIO write out of bounds at offset 0x%lx\n", reg_offset);
            }
        } else {
            qemu_log_mask(LOG_UNIMP, "mb862xxfb: MMIO write with unsupported size %d at 0x%lx\n", size, addr);
        }
    }
    else if (addr >= MB862XX_MMIO_BASE && addr < MB862XX_MMIO_BASE + MB862XX_MMIO_SIZE) {
        /* Low MMIO region: used for GC_RSW write to relocate registers */
        hwaddr reg_offset = addr - MB862XX_MMIO_BASE;
        if (size == 4 && (reg_offset % 4) == 0) {
            if (reg_offset == GC_RSW) {
                if (val & 1) {
                    s->regs_relocated = true;
                } else {
                    s->regs_relocated = false;
                }
                qemu_log_mask(LOG_UNIMP, "mb862xxfb: write to low GC_RSW (0x%lx) with val 0x%lx, relocated=%d\n", addr, val, s->regs_relocated);
            } else {
                qemu_log_mask(LOG_GUEST_ERROR, "mb862xxfb: unexpected low MMIO write at offset 0x%lx\n", reg_offset);
            }
        } else {
            qemu_log_mask(LOG_UNIMP, "mb862xxfb: low MMIO write with unsupported size %d at 0x%lx\n", size, addr);
        }
    }
    else if (addr < CORALP_MEM_SIZE) {
        /* Framebuffer memory write */
        switch (size) {
        case 1:
            s->fb_mem[addr] = val;
            break;
        case 2:
            stw_le_p(&s->fb_mem[addr], val);
            break;
        case 4:
            stl_le_p(&s->fb_mem[addr], val);
            break;
        case 8:
            stq_le_p(&s->fb_mem[addr], val);
            break;
        default:
            break;
        }
    }
    /* Writes to unmapped region are ignored */
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
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

    memset(s->mmio, 0, sizeof(s->mmio));
    memset(s->fb_mem, 0, sizeof(s->fb_mem));

    /* Fixed chip identification values */
    s->mmio[GC_CID / 4] = 0x00000308;       /* Coral-PA: CNAME=3, Rev.8 */
    s->mmio[GC_2D3D_REV / 4] = GC_RE_REVISION;
    s->mmio[GC_MMR / 4] = GC_MMR_CORALP_EVB_VAL;
    
    s->regs_relocated = true;  /* registers start at high base as per our mapping */
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
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_MB862XX );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_MB862XX );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_MB862XX );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Remove PCIe capabilities; this is a conventional PCI device */
    /* int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    } */

    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = BAR0_SIZE;
    s->bar_info[0].name = "mb862xx-bar0";
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

static const VMStateDescription vmstate_pcibase = {
    .name = "mb862xxfb_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32_ARRAY(mmio, PCIBaseState, MB862XX_MMIO_SIZE / 4),
        VMSTATE_BUFFER(fb_mem, PCIBaseState),
        VMSTATE_BOOL(regs_relocated, PCIBaseState),
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
