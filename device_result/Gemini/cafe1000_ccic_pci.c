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

#define TYPE_PCIBASE_DEVICE "cafe1000_ccic_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define CAFE_VERSION 0x000002
#define REG_GPR		0xb4
#define GPR_C1EN	  0x00000020
#define GPR_C0EN	  0x00000010
#define GPR_C1	  0x00000002
#define GPR_C0	  0x00000001
#define REG_TWSIC0	0xb8
#define TWSIC0_EN	  0x00000001
#define TWSIC0_MODE	  0x00000002
#define TWSIC0_SID	  0x000003fc
#define TWSIC0_SID_SHIFT 3
#define TWSIC0_CLKDIV	  0x0007fc00
#define TWSIC0_MASKACK  0x00400000
#define TWSIC0_OVMAGIC  0x00800000
#define REG_TWSIC1	0xbc
#define TWSIC1_DATA	  0x0000ffff
#define TWSIC1_ADDR	  0x00ff0000
#define TWSIC1_ADDR_SHIFT 16
#define TWSIC1_READ	  0x01000000
#define TWSIC1_WSTAT	  0x02000000
#define TWSIC1_RVALID	  0x04000000
#define TWSIC1_ERROR	  0x08000000
#define REG_GL_CSR     0x3004
#define GCSR_SRS	 0x00000001
#define GCSR_SRC	 0x00000002
#define GCSR_MRS	 0x00000004
#define GCSR_MRC	 0x00000008
#define GCSR_CCIC_EN	 0x00004000
#define REG_GL_IMASK   0x300c
#define GIMSK_CCIC_EN		 0x00000004
#define REG_GL_FCR	0x3038
#define GFCR_GPIO_ON	  0x08
#define REG_GL_GPIOR	0x315c
#define GGPIO_OUT		0x80000
#define GGPIO_VAL		0x00008
#define REG_LEN		       (REG_GL_IMASK + 4)
#define REG_IRQMASK	0x2c
#define REG_IRQSTAT	0x30
#define IRQ_TWSIR	  0x00020000
#define IRQ_TWSIW	  0x00010000
#define IRQ_TWSIE	  0x00040000
#define TWSIIRQS (IRQ_TWSIW|IRQ_TWSIR|IRQ_TWSIE)
#define IRQ_EOF0	  0x00000001
#define IRQ_SOF0	  0x00000008
#define IRQ_EOF2	  0x00000004
#define IRQ_SOF2	  0x00000020
#define IRQ_EOF1	  0x00000002
#define IRQ_SOF1	  0x00000010
#define FRAMEIRQS (IRQ_EOF0|IRQ_EOF1|IRQ_EOF2|IRQ_SOF0|IRQ_SOF1|IRQ_SOF2)
#define C0_ENABLE	  0x00000001
#define REG_CTRL0	0x3c
#define REG_CLKCTRL	0x88
#define REG_CSI2_DPHY6	0x138
#define REG_CSI2_CTRL0	0x100
#define CSI2_C0_MIPI_EN (0x1 << 0)
#define REG_CSI2_DPHY5	0x134
#define REG_CSI2_DPHY3	0x12c
#define C1_PWRDWN	  0x10000000
#define REG_CTRL1	0x40
#define REG_IMGSIZE	0x34
#define C0_RGB4_XBGR	  0x0000000c
#define C0_YUVE_NOSWAP  0x00000000
#define C0_DF_MASK	  0x00fffffc
#define IMGSZ_H_MASK	  0x00003fff
#define C0_DF_RGB	  0x000000a0
#define REG_IMGPITCH	0x24
#define C0_RGB5_BGGR	  0x0000000c
#define IMGP_UVP_MASK   0x3ffc0000
#define C0_SIFM_MASK	  0xc0000000
#define IMGSZ_V_SHIFT	  16
#define IMGP_YP_MASK	  0x00003ffc
#define C0_YUVE_SWAP24  0x00020000
#define C0_SIF_HVSYNC	  0x00000000
#define C0_RGB5_GRBG	  0x00000004
#define C0_YUV_420PL	  0x0000a000
#define REG_IMGOFFSET	0x38
#define C0_YUV_PACKED	  0x00008000
#define C0_DF_YUV	  0x00000000
#define C0_YUVE_VYUY	  0x00020000
#define C0_RGBF_565	  0x00000000
#define C0_RGBF_444	  0x00000800
#define IMGSZ_V_MASK	  0x1fff0000
#define C1_TWOBUFS	  0x08000000
#define REG_UBAR	0xc4
#define C1_DESC_3WORD   0x00000200
#define REG_U0BAR	0x0c
#define REG_V0BAR	0x18
#define REG_Y0BAR	0x00
#define REG_DMA_DESC_Y	0x200
#define REG_DESC_LEN_V	0x214
#define REG_DESC_LEN_U	0x210
#define C1_DESC_ENA	  0x00000100
#define REG_DESC_LEN_Y	0x20c

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
    uint32_t irqmask;
    uint32_t irqstat;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t gpr;
    uint32_t twsic0;
    uint32_t twsic1;
    uint32_t gl_csr;
    uint32_t gl_imask;
    uint32_t gl_fcr;
    uint32_t gl_gpior;
    uint32_t ctrl0;
    uint32_t ctrl1;
    uint32_t clkctrl;
    uint32_t csi2_dphy6;
    uint32_t csi2_ctrl0;
    uint32_t csi2_dphy5;
    uint32_t csi2_dphy3;
    uint32_t imgsize;
    uint32_t imgpitch;
    uint32_t imgoffset;

    /* DMA Context */
    uint32_t y0bar;
    uint32_t u0bar;
    uint32_t v0bar;
    uint32_t ubar;
    uint32_t dma_desc_y;
    uint32_t desc_len_y;
    uint32_t desc_len_u;
    uint32_t desc_len_v;

    uint32_t state;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool level = (s->irqstat & s->irqmask) != 0;
    pci_set_irq(pdev, level);
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case REG_GPR:
        val = s->gpr;
        break;
    case REG_TWSIC0:
        val = s->twsic0;
        break;
    case REG_TWSIC1:
        val = s->twsic1;
        break;
    case REG_GL_CSR:
        val = s->gl_csr;
        break;
    case REG_GL_IMASK:
        val = s->gl_imask;
        break;
    case REG_GL_FCR:
        val = s->gl_fcr;
        break;
    case REG_GL_GPIOR:
        val = s->gl_gpior;
        break;
    case REG_IRQMASK:
        val = s->irqmask;
        break;
    case REG_IRQSTAT:
        val = s->irqstat;
        break;
    case REG_CTRL0:
        val = s->ctrl0;
        break;
    case REG_CSI2_DPHY3:
        val = s->csi2_dphy3;
        break;
    case REG_CSI2_DPHY5:
        val = s->csi2_dphy5;
        break;
    case REG_CSI2_DPHY6:
        val = s->csi2_dphy6;
        break;
    case REG_CSI2_CTRL0:
        val = s->csi2_ctrl0;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: Unimplemented read at offset 0x%" HWADDR_PRIx "\n", __func__, addr);
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case REG_GPR:
        s->gpr = val;
        break;
    case REG_TWSIC0:
        s->twsic0 = val;
        break;
    case REG_TWSIC1:
        s->twsic1 = val;
        if (val & TWSIC1_READ) {
            /* Simulate immediate read completion */
            s->twsic1 |= TWSIC1_RVALID;
            s->twsic1 &= ~(TWSIC1_WSTAT | TWSIC1_ERROR);
            s->twsic1 |= 0x42; /* Dummy data */
        } else {
            /* Simulate immediate write completion */
            s->twsic1 &= ~(TWSIC1_WSTAT | TWSIC1_ERROR);
        }
        /* Trigger TWSI interrupt */
        s->irqstat |= TWSIIRQS;
        pcibase_update_irq(s);
        break;
    case REG_GL_CSR:
        s->gl_csr = val;
        break;
    case REG_GL_IMASK:
        s->gl_imask = val;
        break;
    case REG_GL_FCR:
        s->gl_fcr = val;
        break;
    case REG_GL_GPIOR:
        s->gl_gpior = val;
        break;
    case REG_IRQMASK:
        s->irqmask = val;
        pcibase_update_irq(s);
        break;
    case REG_IRQSTAT:
        s->irqstat &= ~val; /* W1C */
        pcibase_update_irq(s);
        break;
    case REG_CTRL0:
        s->ctrl0 = val;
        break;
    case REG_CSI2_DPHY3:
        s->csi2_dphy3 = val;
        break;
    case REG_CSI2_DPHY5:
        s->csi2_dphy5 = val;
        break;
    case REG_CSI2_DPHY6:
        s->csi2_dphy6 = val;
        break;
    case REG_CSI2_CTRL0:
        s->csi2_ctrl0 = val;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: Unimplemented write at offset 0x%" HWADDR_PRIx " val 0x%" PRIx64 "\n", __func__, addr, val);
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

    s->gpr = 0;
    s->twsic0 = 0;
    s->twsic1 = 0;
    s->gl_csr = 0;
    s->gl_imask = 0;
    s->gl_fcr = 0;
    s->gl_gpior = 0;
    s->irqmask = 0;
    s->irqstat = 0;
    s->ctrl0 = 0;
    s->ctrl1 = 0;
    s->clkctrl = 0;
    s->csi2_dphy6 = 0;
    s->csi2_ctrl0 = 0;
    s->csi2_dphy5 = 0;
    s->csi2_dphy3 = 0;
    s->imgsize = 0;
    s->imgpitch = 0;
    s->imgoffset = 0;
    s->y0bar = 0;
    s->u0bar = 0;
    s->v0bar = 0;
    s->ubar = 0;
    s->dma_desc_y = 0;
    s->desc_len_y = 0;
    s->desc_len_u = 0;
    s->desc_len_v = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x11ab );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x4102 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_OTHERS );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo){
        .index = 0,
        .type = BAR_TYPE_MMIO,
        .size = 0x4000,
        .name = "cafe-ccic-mmio"
    };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
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
