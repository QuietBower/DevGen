/*
 * QEMU PCI device model for Marvell CAFE1000 CCIC
 * Phase 2: Functional behavior for driver bring-up.
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

#define TYPE_PCIBASE_DEVICE "cafe1000_ccic_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Version */
#define CAFE_VERSION              0x000002

/* General-purpose register */
#define REG_GPR                   0x00b4
#define GPR_C1EN                  0x00000020
#define GPR_C0EN                  0x00000010
#define GPR_C1                    0x00000002
#define GPR_C0                    0x00000001

/* TWSI controller registers */
#define REG_TWSIC0                0x00b8
#define TWSIC0_EN                 0x00000001
#define TWSIC0_MODE               0x00000002
#define TWSIC0_SID                0x000003fc
#define TWSIC0_SID_SHIFT          3
#define TWSIC0_CLKDIV             0x0007fc00
#define TWSIC0_MASKACK            0x00400000
#define TWSIC0_OVMAGIC            0x00800000

#define REG_TWSIC1                0x00bc
#define TWSIC1_DATA               0x0000ffff
#define TWSIC1_ADDR               0x00ff0000
#define TWSIC1_ADDR_SHIFT         16
#define TWSIC1_READ               0x01000000
#define TWSIC1_WSTAT              0x02000000
#define TWSIC1_RVALID             0x04000000
#define TWSIC1_ERROR              0x08000000

/* Global control/status and GPIO */
#define REG_GL_CSR                0x3004
#define GCSR_SRS                  0x00000001
#define GCSR_SRC                  0x00000002
#define GCSR_MRS                  0x00000004
#define GCSR_MRC                  0x00000008
#define GCSR_CCIC_EN              0x00004000

#define REG_GL_IMASK              0x300c
#define GIMSK_CCIC_EN             0x00000004

#define REG_GL_FCR                0x3038
#define GFCR_GPIO_ON              0x00000008

#define REG_GL_GPIOR              0x315c
#define GGPIO_OUT                 0x00080000
#define GGPIO_VAL                 0x00000008

/* Length of register space used by driver */
#define REG_LEN                   (REG_GL_IMASK + 4)

/* Interrupt controller registers and bits */
#define REG_IRQMASK               0x002c
#define REG_IRQSTAT               0x0030

#define IRQ_TWSIE                 0x00040000
#define IRQ_TWSIR                 0x00020000
#define IRQ_TWSIW                 0x00010000
#define IRQ_SOF0                  0x00000008
#define IRQ_EOF0                  0x00000001
#define IRQ_SOF1                  0x00000010
#define IRQ_SOF2                  0x00000020
#define IRQ_EOF1                  0x00000002
#define IRQ_EOF2                  0x00000004

#define TWSIIRQS                  (IRQ_TWSIW | IRQ_TWSIR | IRQ_TWSIE)
#define FRAMEIRQS                 (IRQ_EOF0 | IRQ_EOF1 | IRQ_EOF2 | \
                                   IRQ_SOF0 | IRQ_SOF1 | IRQ_SOF2)

/* Core control and image registers */
#define REG_CTRL0                 0x003c
#define C0_ENABLE                 0x00000001

#define REG_CTRL1                 0x0040
#define C1_PWRDWN                 0x10000000
#define C1_TWOBUFS                0x08000000
#define C1_DESC_3WORD             0x00000200
#define C1_DESC_ENA               0x00000100

#define REG_IMGPITCH              0x0024
#define REG_IMGOFFSET             0x0038
#define REG_IMGSIZE               0x0034

#define C0_DF_YUV                 0x00000000
#define C0_RGBF_444               0x00000800
#define C0_SIF_HVSYNC             0x00000000
#define C0_YUVE_NOSWAP            0x00000000
#define C0_RGB5_BGGR              0x0000000c
#define C0_RGB5_GRBG              0x00000004
#define C0_RGBF_565               0x00000000
#define C0_DF_MASK                0x00fffffc
#define C0_YUVE_SWAP24            0x00020000
#define C0_YUV_PACKED             0x00008000
#define C0_DF_RGB                 0x000000a0
#define C0_YUVE_VYUY              0x00020000
#define C0_YUV_420PL              0x0000a000
#define C0_RGB4_XBGR              0x0000000c
#define C0_SIFM_MASK              0xc0000000

#define IMGSZ_H_MASK              0x00003fff
#define IMGSZ_V_MASK              0x1fff0000
#define IMGSZ_V_SHIFT             16

#define IMGP_YP_MASK              0x00003ffc
#define IMGP_UVP_MASK             0x3ffc0000

/* DMA and buffer registers */
#define REG_UBAR                  0x00c4
#define REG_DESC_LEN_Y            0x020c
#define REG_DESC_LEN_U            0x0210
#define REG_DESC_LEN_V            0x0214
#define REG_DMA_DESC_Y            0x0200

#define REG_V0BAR                 0x0018
#define REG_Y0BAR                 0x0000
#define REG_U0BAR                 0x000c

/* Clock and MIPI CSI2 registers */
#define REG_CLKCTRL               0x0088
#define REG_CSI2_CTRL0            0x0100
#define REG_CSI2_DPHY3            0x012c
#define REG_CSI2_DPHY5            0x0134
#define REG_CSI2_DPHY6            0x0138

#define CSI2_C0_MIPI_EN           (0x1 << 0)
#define CSI2_C0_ACT_LANE(n)       (((n) - 1) << 1)

/* Misc constants used by the driver */
#define NR_MCAM_CLK               3
#define MCAM_MODE_VMALLOC         1
#define MCAM_MODE_DMA_SG          1
#define MCAM_MODE_DMA_CONTIG      1
#define MAX_DMA_BUFS              3

#define VGA_WIDTH                 640
#define VGA_HEIGHT                480

#define CAFE_SMBUS_TIMEOUT        (HZ)

/* Static PCI vendor/device/class IDs */
#define CAFE_PCI_VENDOR_ID        0x11ab
#define CAFE_PCI_DEVICE_ID        0x4102
#define CAFE_PCI_CLASS_ID         0x0400

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

    /* Hardware Register Shadows */
    struct {
        uint32_t gpr;
        uint32_t twsic0;
        uint32_t twsic1;
        uint32_t gl_csr;
        uint32_t gl_imask;
        uint32_t gl_fcr;
        uint32_t gl_gpior;
        uint32_t irqmask;
        uint32_t irqstat;
        uint32_t ctrl0;
        uint32_t ctrl1;
        uint32_t imgpitch;
        uint32_t imgoffset;
        uint32_t imgsize;
        uint32_t ubar;
        uint32_t desc_len_y;
        uint32_t desc_len_u;
        uint32_t desc_len_v;
        uint32_t dma_desc_y;
        uint32_t v0bar;
        uint32_t y0bar;
        uint32_t u0bar;
        uint32_t clkctrl;
        uint32_t csi2_ctrl0;
        uint32_t csi2_dphy3;
        uint32_t csi2_dphy5;
        uint32_t csi2_dphy6;
    } regs;
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    if ((s->regs.irqstat & s->regs.irqmask) ||
        (s->regs.gl_imask & GIMSK_CCIC_EN)) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* MMIO/PIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = 0;

    if (size != 4) {
        /* Driver always uses 32-bit accesses. */
        return 0;
    }

    switch (addr) {
    case REG_GPR:
        val = s->regs.gpr;
        break;
    case REG_TWSIC0:
        val = s->regs.twsic0;
        break;
    case REG_TWSIC1:
        val = s->regs.twsic1;
        break;
    case REG_GL_CSR:
        val = s->regs.gl_csr;
        break;
    case REG_GL_IMASK:
        val = s->regs.gl_imask;
        break;
    case REG_GL_FCR:
        val = s->regs.gl_fcr;
        break;
    case REG_GL_GPIOR:
        val = s->regs.gl_gpior;
        break;
    case REG_IRQMASK:
        val = s->regs.irqmask;
        break;
    case REG_IRQSTAT:
        val = s->regs.irqstat;
        break;
    case REG_CTRL0:
        val = s->regs.ctrl0;
        break;
    case REG_CTRL1:
        val = s->regs.ctrl1;
        break;
    case REG_IMGPITCH:
        val = s->regs.imgpitch;
        break;
    case REG_IMGOFFSET:
        val = s->regs.imgoffset;
        break;
    case REG_IMGSIZE:
        val = s->regs.imgsize;
        break;
    case REG_UBAR:
        val = s->regs.ubar;
        break;
    case REG_DESC_LEN_Y:
        val = s->regs.desc_len_y;
        break;
    case REG_DESC_LEN_U:
        val = s->regs.desc_len_u;
        break;
    case REG_DESC_LEN_V:
        val = s->regs.desc_len_v;
        break;
    case REG_DMA_DESC_Y:
        val = s->regs.dma_desc_y;
        break;
    case REG_V0BAR:
        val = s->regs.v0bar;
        break;
    case REG_Y0BAR:
        val = s->regs.y0bar;
        break;
    case REG_U0BAR:
        val = s->regs.u0bar;
        break;
    case REG_CLKCTRL:
        val = s->regs.clkctrl;
        break;
    case REG_CSI2_CTRL0:
        val = s->regs.csi2_ctrl0;
        break;
    case REG_CSI2_DPHY3:
        val = s->regs.csi2_dphy3;
        break;
    case REG_CSI2_DPHY5:
        val = s->regs.csi2_dphy5;
        break;
    case REG_CSI2_DPHY6:
        val = s->regs.csi2_dphy6;
        break;
    default:
        /* Unused offsets return 0 */
        val = 0;
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                               unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t v = (uint32_t)val;

    if (size != 4) {
        return;
    }

    switch (addr) {
    case REG_GPR:
        s->regs.gpr = v;
        break;
    case REG_TWSIC0:
        s->regs.twsic0 = v;
        break;
    case REG_TWSIC1:
        /*
         * Model minimal SMBus behavior:
         * - Write stores value.
         * - For write ops (no TWSIC1_READ set), we immediately mark
         *   WSTAT and clear ERROR.
         * - For read ops (TWSIC1_READ set), we immediately provide a
         *   dummy data byte, set RVALID, clear ERROR.
         * - In both cases we assert the TWSI IRQ bits if unmasked.
         */
        s->regs.twsic1 = v;

        if (v & TWSIC1_READ) {
            /* Read operation: fabricate data and completion. */
            s->regs.twsic1 &= ~TWSIC1_ERROR;
            s->regs.twsic1 |= TWSIC1_RVALID;
            /* Keep command/address bits, data byte can be anything. */
            if (!(s->regs.irqstat & TWSIIRQS)) {
                s->regs.irqstat |= TWSIIRQS;
            }
        } else {
            /* Write operation completion */
            s->regs.twsic1 &= ~TWSIC1_ERROR;
            s->regs.twsic1 |= TWSIC1_WSTAT;
            if (!(s->regs.irqstat & TWSIIRQS)) {
                s->regs.irqstat |= TWSIIRQS;
            }
        }
        pcibase_update_irq(s);
        break;

    case REG_GL_CSR:
        /* Simple shadow; driver writes wake-up dance, we don't enforce. */
        s->regs.gl_csr = v;
        break;
    case REG_GL_IMASK:
        s->regs.gl_imask = v;
        pcibase_update_irq(s);
        break;
    case REG_GL_FCR:
        s->regs.gl_fcr = v;
        break;
    case REG_GL_GPIOR:
        s->regs.gl_gpior = v;
        break;
    case REG_IRQMASK:
        s->regs.irqmask = v;
        pcibase_update_irq(s);
        break;
    case REG_IRQSTAT:
        /*
         * IRQSTAT is handled as write-1-to-clear (W1C) by the ISR:
         *   mcam_reg_write(mcam, REG_IRQSTAT, TWSIIRQS);
         *   mcam_reg_write(mcam, REG_IRQSTAT, FRAMEIRQS);
         */
        s->regs.irqstat &= ~v;
        pcibase_update_irq(s);
        break;
    case REG_CTRL0:
        s->regs.ctrl0 = v;
        /*
         * When capture is enabled, the real hardware generates frame
         * start/end interrupts. The driver clears FRAMEIRQS and then
         * calls mccic_irq() with the snapshot of active bits.
         *
         * We model only that some frame IRQs become pending when
         * C0_ENABLE is set. Do not attempt to emulate DMA or timing.
         */
        if (v & C0_ENABLE) {
            /* Raise a single EOF0/SOF0 pair to trigger mccic_irq() path. */
            s->regs.irqstat |= (IRQ_EOF0 | IRQ_SOF0);
            pcibase_update_irq(s);
        }
        break;
    case REG_CTRL1:
        s->regs.ctrl1 = v;
        break;
    case REG_IMGPITCH:
        s->regs.imgpitch = v;
        break;
    case REG_IMGOFFSET:
        s->regs.imgoffset = v;
        break;
    case REG_IMGSIZE:
        s->regs.imgsize = v;
        break;
    case REG_UBAR:
        s->regs.ubar = v;
        break;
    case REG_DESC_LEN_Y:
        s->regs.desc_len_y = v;
        break;
    case REG_DESC_LEN_U:
        s->regs.desc_len_u = v;
        break;
    case REG_DESC_LEN_V:
        s->regs.desc_len_v = v;
        break;
    case REG_DMA_DESC_Y:
        s->regs.dma_desc_y = v;
        break;
    case REG_V0BAR:
        s->regs.v0bar = v;
        break;
    case REG_Y0BAR:
        s->regs.y0bar = v;
        break;
    case REG_U0BAR:
        s->regs.u0bar = v;
        break;
    case REG_CLKCTRL:
        s->regs.clkctrl = v;
        break;
    case REG_CSI2_CTRL0:
        s->regs.csi2_ctrl0 = v;
        break;
    case REG_CSI2_DPHY3:
        s->regs.csi2_dphy3 = v;
        break;
    case REG_CSI2_DPHY5:
        s->regs.csi2_dphy5 = v;
        break;
    case REG_CSI2_DPHY6:
        s->regs.csi2_dphy6 = v;
        break;
    default:
        /* Ignore unknown writes */
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    (void)opaque;
    (void)addr;
    (void)size;
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val,
                              unsigned size)
{
    (void)opaque;
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

    /* Clear all register shadows on reset */
    memset(&s->regs, 0, sizeof(s->regs));
    pcibase_update_irq(s);
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s,
                                 BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s,
                              bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s,
                              bi->name, aligned_size);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  CAFE_PCI_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  CAFE_PCI_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CAFE_PCI_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF,
                                    errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR Initialization: single MMIO BAR0 */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    s->bar_info[0].size  = REG_LEN;
    s->bar_info[0].name  = "cafe-ccic-mmio";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type  = BAR_TYPE_NONE;
        s->bar_info[i].size  = 0;
        s->bar_info[i].name  = NULL;
    }

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No MSI/MSI-X in driver path */
    s->has_msi = false;
    s->has_msix = false;

    memset(&s->regs, 0, sizeof(s->regs));
    pcibase_update_irq(s);
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

    (void)s;
}

static const VMStateDescription vmstate_pcibase = {
    .name = "cafe1000_ccic_pci",
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
