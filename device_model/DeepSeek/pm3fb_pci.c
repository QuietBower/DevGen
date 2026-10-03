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
#include "hw/qdev-properties.h"
#include "hw/qdev-core.h"
#include "migration/vmstate.h"

/* Additional include files retrieved from driver context */
#include "hw/pci/pci_ids.h"

#define TYPE_PCIBASE_DEVICE "pm3fb_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PM3_VENDOR_ID       0x3D3D
#define PM3_DEVICE_ID       0x000a
#define PM3_CLASS_ID        0x0300

/* BAR sizes - extracted from driver usage */
#define PM3_REGS_SIZE 0x10000
#define PM3_FB_SIZE   (64 * 1024 * 1024)

/* Register offsets observed in driver source */
#define PM3VideoControl         0x3058
#define PM3MemBypassWriteMask   0x1008

/* Newly provided register definitions from supplementary source */
#define PM3InFIFOSpace          0x0018
#define PM3OutFIFOWords         0x0020
#define PM3OutputFifo           0x2000
#define PM3Sync_Tag             0x188
#define PM3FilterMode           0x8c00
#define PM3FilterModeSync       0x400
#define PM3Sync                 0x8c40
#define PM3StatisticMode        0x8c08
#define PM3DeltaMode            0x9300
#define PM3RasterizerMode       0x80a0
#define PM3ScissorMode          0x8180
#define PM3LineStippleMode      0x81a8
#define PM3AreaStippleMode      0x81a0
#define PM3GIDMode              0xb538
#define PM3DepthMode            0x89a0
#define PM3StencilMode          0x8988
#define PM3StencilData          0x8990
#define PM3ColorDDAMode         0x87e0
#define PM3TextureCoordMode     0x8380
#define PM3TextureIndexMode0    0xb338
#define PM3TextureIndexMode1    0xb340
#define PM3TextureReadMode      0x8670
#define PM3LUTMode              0xb378
#define PM3TextureFilterMode    0x84e0
#define PM3TextureCompositeMode 0xb300
#define PM3TextureApplicationMode 0x8680
#define PM3TextureCompositeColorMode1 0xb318
#define PM3TextureCompositeAlphaMode1 0xb320
#define PM3TextureCompositeColorMode0 0xb308
#define PM3TextureCompositeAlphaMode0 0xb310
#define PM3FogMode              0x8690
#define PM3ChromaTestMode       0x8f18
#define PM3AlphaTestMode        0x8800
#define PM3AntialiasMode        0x8808
#define PM3YUVMode              0x8f00
#define PM3AlphaBlendColorMode  0xafa0
#define PM3AlphaBlendAlphaMode  0xafa8
#define PM3DitherMode           0x8818
#define PM3LogicalOpMode        0x8828
#define PM3RouterMode           0x8840
#define PM3Window               0x8980
#define PM3Config2D             0xb618
#define PM3SpanColorMask        0x8168
#define PM3XBias                0x9480
#define PM3YBias                0x9488
#define PM3DeltaControl         0x9350
#define PM3BitMaskPattern       0x8068
#define PM3FBDestReadEnables    0xaee8
#define PM3FBDestReadBufferAddr0 0xae80
#define PM3FBDestReadBufferOffset0 0xaea0
#define PM3FBDestReadBufferWidth0 0xaec0
#define PM3FBDestReadMode       0xaee0
#define PM3FBSourceReadBufferAddr 0xaf08
#define PM3FBSourceReadBufferOffset 0xaf10
#define PM3FBSourceReadBufferWidth 0xaf18
#define PM3FBSourceReadMode     0xaf00
#define PM3PixelSize            0x80c0
#define PM3FBSoftwareWriteMask  0x8820
#define PM3FBHardwareWriteMask  0x8ac0
#define PM3FBWriteMode          0x8ab8
#define PM3FBWriteBufferAddr0   0xb000
#define PM3FBWriteBufferOffset0 0xb020
#define PM3FBWriteBufferWidth0  0xb040
#define PM3SizeOfFramebuffer    0xb0a8
#define PM3dXDom                0x8008
#define PM3dXSub                0x8018
#define PM3dY                   0x8028
#define PM3StartXDom            0x8000
#define PM3StartXSub            0x8010
#define PM3StartY               0x8020
#define PM3Count                0x8030
#define PM3LBDestReadMode       0xb500
#define PM3LBDestReadEnables    0xb508
#define PM3LBSourceReadMode     0xb520
#define PM3LBWriteMode          0x88c0
#define PM3ForegroundColor      0xb0c0
#define PM3BackgroundColor      0xb0c8
#define PM3RectanglePosition    0xb600
#define PM3Render2D             0xb640
#define PM3ScissorMinXY         0x8188
#define PM3ScissorMaxXY         0x8190
#define PM3Aperture0            0x0050
#define PM3Aperture1            0x0058
#define PM3FIFODis              0x0068
#define PM3HTotal               0x3010
#define PM3HsEnd                0x3030
#define PM3HsStart              0x3028
#define PM3HbEnd                0x3020
#define PM3HgEnd                0x3018
#define PM3ScreenStride         0x3008
#define PM3VTotal               0x3038
#define PM3VsEnd                0x3050
#define PM3VsStart              0x3048
#define PM3VbEnd                0x3040
#define PM3ByAperture1Mode      0x0300
#define PM3ByAperture2Mode      0x0328
#define PM3VClkCtl              0x0040
#define PM3ScreenBase           0x3000
#define PM3ChipConfig           0x0070
#define PM3RD_IndexHigh         0x4028
#define PM3RD_IndexLow          0x4020
#define PM3RD_IndexedData       0x4030
#define PM3RD_PaletteWriteAddress 0x4000
#define PM3RD_PaletteData       0x4008
#define PM3RD_CursorMode        0x005
#define PM3RD_CursorXLow        0x007
#define PM3RD_CursorXHigh       0x008
#define PM3RD_CursorYLow        0x009
#define PM3RD_CursorYHigh       0x00a
#define PM3RD_CursorHotSpotX    0x00b
#define PM3RD_CursorHotSpotY    0x00c
#define PM3RD_CursorPalette(p)  (0x303 + (p))
#define PM3RD_DClk0PreScale     0x201
#define PM3RD_DClk0FeedbackScale 0x202
#define PM3RD_DClk0PostScale    0x203
#define PM3RD_IndexControl      0x4038
#define PM3RD_SyncControl       0x001
#define PM3RD_DACControl        0x002
#define PM3RD_PixelSize         0x003
#define PM3RD_ColorFormat       0x004
#define PM3RD_MiscControl       0x000

/* Bit masks and constants from supplementary source */
#define PM3Config2D_UseConstantSource            (1 << 16)
#define PM3Config2D_ForegroundROPEnable          (1 << 6)
#define PM3Config2D_ForegroundROP(rop)          (((rop) & 0xf) << 7)
#define PM3Config2D_FBDestReadEnable            (1 << 3)
#define PM3Config2D_FBWriteEnable               (1 << 17)
#define PM3Config2D_UserScissorEnable           (1 << 2)
#define PM3Config2D_Blocking                    (1 << 18)
#define PM3Config2D_OpaqueSpan                  (1 << 0)
#define PM3Render2D_XPositive                   (1 << 28)
#define PM3Render2D_YPositive                   (1 << 29)
#define PM3Render2D_Operation_Normal            (0 << 12)
#define PM3Render2D_SpanOperation               (1 << 15)
#define PM3Render2D_Width(w)                    ((w) & 0x0fff)
#define PM3Render2D_Height(h)                   (((h) & 0x0fff) << 16)
#define PM3Render2D_FBSourceReadEnable          (1 << 14)
#define PM3Render2D_Operation_SyncOnBitMask     (2 << 12)
#define PM3RectanglePosition_XOffset(x)         ((x) & 0xffff)
#define PM3RectanglePosition_YOffset(y)         (((y) & 0xffff) << 16)
#define PM3FBDestReadEnables_E(e)               ((e) & 0xff)
#define PM3FBDestReadEnables_R(r)               (((r) & 0xff) << 8)
#define PM3FBDestReadEnables_ReferenceAlpha(a)  (((a) & 0xff) << 24)
#define PM3FBDestReadBufferWidth_Width(w)       ((w) & 0x0fff)
#define PM3FBDestReadMode_ReadEnable            (1 << 0)
#define PM3FBDestReadMode_Enable0               (1 << 8)
#define PM3FBSourceReadMode_Blocking            (1 << 11)
#define PM3FBSourceReadMode_ReadEnable          (1 << 0)
#define PM3FBSourceReadBufferOffset_XOffset(x)  ((x) & 0xffff)
#define PM3FBSourceReadBufferOffset_YOffset(y)  (((y) & 0xffff) << 16)
#define PM3PixelSize_GLOBAL_8BIT                (2 << 0)
#define PM3PixelSize_GLOBAL_16BIT               (1 << 0)
#define PM3PixelSize_GLOBAL_32BIT               (0 << 0)
#define PM3FBWriteMode_WriteEnable              (1 << 0)
#define PM3FBWriteMode_OpaqueSpan               (1 << 5)
#define PM3FBWriteMode_Enable0                  (1 << 12)
#define PM3FBWriteBufferWidth_Width(w)          ((w) & 0x0fff)
#define PM3VideoControl_HSYNC_MASK              (3 << 3)
#define PM3VideoControl_VSYNC_MASK              (3 << 5)
#define PM3VideoControl_HSYNC_ACTIVE_HIGH       (1 << 3)
#define PM3VideoControl_HSYNC_ACTIVE_LOW        (3 << 3)
#define PM3VideoControl_VSYNC_ACTIVE_HIGH       (1 << 5)
#define PM3VideoControl_VSYNC_ACTIVE_LOW        (3 << 5)
#define PM3VideoControl_ENABLE                  (1 << 0)
#define PM3VideoControl_PIXELSIZE_8BIT          (0 << 19)
#define PM3VideoControl_PIXELSIZE_16BIT         (1 << 19)
#define PM3VideoControl_PIXELSIZE_32BIT         (2 << 19)
#define PM3VideoControl_LINE_DOUBLE_ON          (1 << 2)
#define PM3VideoControl_BLANK_ACTIVE_LOW        (1 << 1)
#define PM3ByApertureMode_PIXELSIZE_8BIT        (0 << 5)
#define PM3ByApertureMode_PIXELSIZE_16BIT       (1 << 5)
#define PM3ByApertureMode_PIXELSIZE_32BIT       (2 << 5)
#define PM3ByApertureMode_BYTESWAP_BADC         (1 << 0)
#define PM3ByApertureMode_BYTESWAP_DCBA         (3 << 0)
#define PM3RD_SyncControl_HSYNC_ACTIVE_HIGH     (1 << 0)
#define PM3RD_SyncControl_VSYNC_ACTIVE_HIGH     (1 << 3)
#define PM3RD_PixelSize_8_BIT_PIXELS            (0 << 0)
#define PM3RD_PixelSize_16_BIT_PIXELS           (1 << 0)
#define PM3RD_PixelSize_32_BIT_PIXELS           (2 << 0)
#define PM3RD_ColorFormat_CI8_COLOR             (14 << 0)
#define PM3RD_ColorFormat_4444_COLOR            (2 << 0)
#define PM3RD_ColorFormat_5551_FRONT_COLOR      (1 << 0)
#define PM3RD_ColorFormat_565_FRONT_COLOR       (16 << 0)
#define PM3RD_ColorFormat_8888_COLOR            (0 << 0)
#define PM3RD_ColorFormat_COLOR_ORDER_BLUE_LOW  (1 << 5)
#define PM3RD_ColorFormat_LINEAR_COLOR_EXT_ENABLE (1 << 6)
#define PM3RD_MiscControl_HIGHCOLOR_RES_ENABLE  (1 << 0)
#define PM3RD_MiscControl_DIRECTCOLOR_ENABLE    (1 << 3)
#define PM3RD_CursorMode_TYPE_X                 (1 << 4)
#define PM3RD_CursorMode_CURSOR_ENABLE          (1 << 0)
#define PM3_REF_CLOCK                           14318
#define PM3_MAX_PIXCLOCK                        300000
#define PM3_FIFO_SIZE                           120
#define PM3_PIXMAP_SIZE                         (2048 * 4)


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
    uint32_t regs[PM3_REGS_SIZE / sizeof(uint32_t)];
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size != 4) {
        return ~0ULL;
    }
    if (addr >= PM3_REGS_SIZE) {
        return ~0ULL;
    }
    val = s->regs[addr / 4];
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        return;
    }
    if (addr < PM3_REGS_SIZE) {
        s->regs[addr / 4] = (uint32_t)val;
    }
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
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, aligned_size);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PM3_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PM3_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PM3_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* BAR Initialization */
    s->num_bars = 2;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = PM3_REGS_SIZE, .name = "pm3fb-mmio" };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_RAM, .size = PM3_FB_SIZE, .name = "pm3fb-framebuffer" };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Final state initialization before the device is 'live' */
    /* Nothing special needed yet; register array already zeroed by reset */
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
    .name = "pm3fb_pci",
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
