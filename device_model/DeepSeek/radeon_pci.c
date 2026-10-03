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

#define TYPE_PCIBASE_DEVICE "radeon_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define VENDOR_ID 0x1002                     /* ATI/AMD vendor ID */
#define DEVICE_ID 0x1304                     /* First entry from radeon_PCI_IDS */
#define CLASS_ID  0x0300                     /* PCI_CLASS_DISPLAY_VGA */

/* Register offsets extracted from radeon_drv.c */
#define RADEON_MM_INDEX                     0x0000
#define RADEON_MM_DATA                      0x0004
#define RADEON_CONFIG_MEMSIZE               0x00f8
#define RADEON_BIOS_0_SCRATCH               0x0010
#define RADEON_BIOS_2_SCRATCH               0x0018
#define RADEON_BIOS_6_SCRATCH               0x0028
#define RADEON_BIOS_7_SCRATCH               0x002c
#define RADEON_GEN_INT_CNTL                 0x0040
#define RADEON_GEN_INT_STATUS               0x0044
#define RADEON_CRTC_GEN_CNTL                0x0050
#define RADEON_CRTC_EXT_CNTL                0x0054
#define RADEON_CRTC_VLINE_CRNT_VLINE        0x0210
#define RADEON_CRTC_CRNT_FRAME              0x0214
#define RADEON_CRTC_OFFSET                  0x0224
#define RADEON_CRTC_PITCH                   0x022c
#define RADEON_CRTC_H_TOTAL_DISP            0x0200
#define RADEON_CRTC2_H_TOTAL_DISP           0x0300
#define RADEON_CRTC2_CRNT_FRAME             0x0314
#define RADEON_CRTC2_GEN_CNTL               0x03f8
#define RADEON_SURFACE_CNTL                 0x0b00
#define RADEON_SURFACE0_INFO                0x0b0c
#define RADEON_SURFACE0_LOWER_BOUND         0x0b04
#define RADEON_SURFACE0_UPPER_BOUND         0x0b08
#define RADEON_RBBM_STATUS                  0x0e40
#define RADEON_CONFIG_APER_SIZE             0x0108
#define RADEON_HOST_PATH_CNTL               0x0130
#define RADEON_MC_STATUS                    0x0150
#define RADEON_MEM_STR_CNTL                 0x0150
#define RADEON_MEM_CNTL                     0x0140
#define RADEON_MEM_TIMING_CNTL              0x0144
#define RADEON_NB_TOM                       0x15c
#define RADEON_AGP_CNTL                     0x0174
#define RADEON_AGP_BASE                 0x0170
#define RADEON_AGP_STATUS                   0x0f5c
#define RADEON_AIC_CNTL                     0x01d0
#define RADEON_AIC_PT_BASE                  0x01d8
#define RADEON_AIC_LO_ADDR                  0x01dc
#define RADEON_AIC_HI_ADDR                  0x01e0
#define RADEON_MSI_REARM_EN                 0x0160
#define RADEON_BUS_CNTL                     0x0030
#define RADEON_CLK_PIN_CNTL                 0x0001
#define RADEON_SCLK_CNTL                    0x000d
#define RADEON_SPLL_CNTL                    0x000c
#define RADEON_M_SPLL_REF_FB_DIV            0x000a
#define RADEON_MCLK_CNTL                    0x0012
#define RADEON_PPLL_REF_DIV                 0x0003
#define RADEON_PPLL_DIV_0                   0x0004
#define RADEON_DISP_PWR_MAN                 0x0d08
#define RADEON_DAC_CNTL2                    0x007c
#define RADEON_TV_DAC_CNTL                  0x088c
#define RADEON_DVI_I2C_CNTL_0              0x02e0
#define RADEON_DVI_I2C_CNTL_1              0x02e4
#define RADEON_I2C_CNTL_0                  0x0090
#define RADEON_I2C_CNTL_1                  0x0094
#define RADEON_I2C_DATA                     0x0098
#define RADEON_FP_GEN_CNTL                  0x0284
#define RADEON_FP2_GEN_CNTL                 0x0288
#define RADEON_LVDS_GEN_CNTL                0x02d0
#define RADEON_LVDS_PLL_CNTL                0x02d4
#define RADEON_GRPH_BUFFER_CNTL             0x02f0
#define RADEON_GRPH2_BUFFER_CNTL            0x03f0
#define RADEON_CP_RB_BASE                   0x0700
#define RADEON_CP_RB_CNTL                   0x0704
#define RADEON_CP_RB_RPTR_WR                0x071c
#define RADEON_CP_RB_WPTR                   0x0714
#define RADEON_CP_RB_WPTR_DELAY             0x0718
#define RADEON_CP_CSQ_CNTL                  0x0740
#define RADEON_CP_CSQ_MODE                  0x0744
#define RADEON_CP_ME_RAM_ADDR               0x07d4
#define RADEON_CP_ME_RAM_DATAL              0x07e0
#define RADEON_CP_ME_RAM_DATAH              0x07dc
#define RADEON_SCRATCH_REG0                 0x15e0
#define RADEON_SCRATCH_UMSK                 0x0770
#define RADEON_HDP_APER_CNTL         (1 << 23)
#define RADEON_MM_APER      (1 << 31)
#define RADEON_MIN_MMIO_SIZE 0x40000       /* 256 KB - typical register aperture for CIK */

#define RADEON_VRAM_SIZE     (128 * 1024 * 1024)  /* 128 MB VRAM */

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar0_container;
    MemoryRegion mmio_region;
    MemoryRegion vram_region;

    uint32_t mmio_regs[0x40000 / 4]; /* 256KB register space */
};

/* MMIO Handlers for the register region only */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t reg_idx = addr >> 2;
    if (size != 4 || (addr & 3)) {
        return 0xFFFFFFFF;
    }
    if (reg_idx >= ARRAY_SIZE(s->mmio_regs)) {
        return 0xFFFFFFFF;
    }
    return s->mmio_regs[reg_idx];
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t reg_idx = addr >> 2;
    if (size != 4 || (addr & 3)) {
        return;
    }
    if (reg_idx >= ARRAY_SIZE(s->mmio_regs)) {
        return;
    }
    s->mmio_regs[reg_idx] = (uint32_t)val;
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    memset(s->mmio_regs, 0, sizeof(s->mmio_regs));

    /* Set default VRAM size to 128MB */
    s->mmio_regs[RADEON_CONFIG_MEMSIZE >> 2] = RADEON_VRAM_SIZE;
    s->mmio_regs[RADEON_CONFIG_APER_SIZE >> 2] = RADEON_VRAM_SIZE;
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR0: VRAM container */
    memory_region_init(&s->bar0_container, OBJECT(s), "radeon-bar0-vram", RADEON_VRAM_SIZE);
    memory_region_init_ram(&s->vram_region, OBJECT(s), "radeon-vram", RADEON_VRAM_SIZE, &error_abort);
    memory_region_add_subregion(&s->bar0_container, 0, &s->vram_region);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar0_container);

    /* BAR2: MMIO register region */
    memory_region_init_io(&s->mmio_region, OBJECT(s), &pcibase_mmio_ops, s,
                         "radeon-mmio", RADEON_MIN_MMIO_SIZE);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mmio_region);
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
    .name = "radeon_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32_ARRAY(mmio_regs, PCIBaseState, 0x40000 / 4),
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
