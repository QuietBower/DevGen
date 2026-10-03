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

#define TYPE_PCIBASE_DEVICE "tw68_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID PCI_VENDOR_ID_TECHWELL
#define DEVICE_ID PCI_DEVICE_ID_TECHWELL_6800
#define CLASS_ID  0x0400

#define TW68_SHARPNESS      0x248
#define TW68_HDELAY_LO      0x228
#define TW68_GPOE       0x028
#define TW68_TESTREG        0x02C
#define TW68_HSYNC      0x210
#define TW68_SCALE_HI       0x238
#define TW68_IDCNTL     0x2D0
#define TW68_CLCNTL1        0x2D4
#define TW68_INFORM     0x208
#define TW68_CLMPL      0x290
#define TW68_DMAC       0x000
#define TW68_CNTRL2     0x268
#define TW68_CKILL      0x2A8
#define TW68_IAGC       0x284
#define TW68_CORING     0x260
#define tw_writel(reg, value)   writel((value), dev->lmmio + ((reg) >> 2))
#define TW68_CROP_HI        0x21C
#define TW68_CNTRL1     0x230
#define TW68_VCNTL2     0x2A4
#define TW68_VSCALE_LO      0x234
#define TW68_PCLAMP     0x29C
#define TW68_CLMD       0x2CC
#define TW68_SAT_U      0x24C
#define TW68_CAP_CTL        0x040
#define TW68_COMB       0x2AC
#define TW68_LDLY       0x2B0
#define TW68_INTSTAT        0x01C
#define TW68_BRIGHT     0x240
#define TW68_HUE        0x254
#define TW68_CLMPG      0x280
#define TW68_MISC2      0x2BC
#define TW68_SAT_V      0x250
#define TW68_INTMASK        0x020
#define TW68_VDELAY_LO      0x220
#define TW68_VCNTL1     0x2A0
#define TW68_MISSCNT        0x298
#define TW68_VBIC       0x010
#define TW68_GPIOC      0x024
#define TW68_HACTIVE_LO     0x22C
#define TW68_SDT        0x270
#define TW68_LOOP       0x2B8
#define tw_writeb(reg, value)   writeb((value), dev->bmmio + (reg))
#define TW68_VSHARP     0x25C
#define TW68_CONTRAST       0x244
#define TW68_AGCGAIN        0x288
#define TW68_ACNTL      0x218
#define TW68_MISC1      0x2B4
#define TW68_SYNCT      0x294
#define TW68_PEAKWT     0x28C
#define TW68_SHARP2     0x258
#define TW68_MVSN       0x2C0
#define TW68_VACTIVE_LO     0x224
#define TW68_OPFORM     0x20C
#define TW68_SDTR       0x274
#define TW68_GPDATA     0x100
#define TW68_HSCALE_LO      0x23C
#define tw_readl(reg)       readl(dev->lmmio + ((reg) >> 2))
#define tw_clearl(reg, bit) \
        writel((readl(dev->lmmio + ((reg) >> 2)) & ~(bit)), \
        dev->lmmio + ((reg) >> 2))
#define tw_setl(reg, bit)   tw_andorl((reg), (bit), (bit))
#define TW68_MAXBOARDS          16
#define UNSET   (-1U)
#define TW68_VID_INTS   (TW68_FFERR | TW68_PABORT | TW68_DMAPERR | \
             TW68_FFOF   | TW68_DMAPI)
#define TW68_VID_INTSX  (TW68_FDMIS | TW68_HLOCK | TW68_VLOCK)
#define TW68_DMAP_EN        (1 << 0)
#define TW68_FIFO_EN        (1 << 1)
#define TW68_FFOF       (1 << 3)
#define TW68_PABORT     (1 << 6)
#define TW68_FDMIS      (1 << 4)
#define TW68_DMAPERR        (1 << 5)
#define TW68_HLOCK      (1 << 22)
#define TW68_DMAPI      (1 << 1)
#define TW68_VLOCK      (1 << 19)
#define TW68_FFERR      (1 << 15)
#define tw_andorl(reg, mask, value) \
        writel((readl(dev->lmmio+((reg)>>2)) & ~(mask)) |\
        ((value) & (mask)), dev->lmmio+((reg)>>2))
#define TW68_DMAP_SA        0x004
#define tw_andorb(reg, mask, value) \
        writeb((readb(dev->bmmio + (reg)) & ~(mask)) |\
        ((value) & (mask)), dev->bmmio+(reg))
#define VideoFormatPALM      4
#define VideoFormatSECAM     2
#define VideoFormatNTSC      0
#define VideoFormatPAL60     6
#define VideoFormatPALNC     5
#define VideoFormatPALBDGHI  1
#define TW68_NORMS ( \
    V4L2_STD_NTSC        | V4L2_STD_PAL       | V4L2_STD_SECAM    | \
    V4L2_STD_PAL_M       | V4L2_STD_PAL_Nc    | V4L2_STD_PAL_60)
#define RISC_INT_BIT        0x08000000
#define RISC_JUMP       0xB0000000
#define ColorFormatYUY2      0x40
#define ColorFormatRGB24     0x10
#define ColorFormatRGB16     0x20
#define ColorFormatRGB32     0x00
#define ColorFormatRGB15     0x30
#define TW68_INPUT_MAX          4
#define TW68_STATUS1       0x204
#define tw_readb(reg)       readb(dev->bmmio + (reg))
#define RISC_INLINE     0xA0000000
#define RISC_SYNCO      0xC0000000
#define RISC_SYNCE      0xD0000000
#define RISC_LINESTART      0x9000000

/* Vendor/Device ID definitions missing from QEMU headers */
#define PCI_VENDOR_ID_TECHWELL      0x1797
#define PCI_DEVICE_ID_TECHWELL_6800 0x6800

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
    uint8_t regs[0x400];  /* MMIO register space, covers up to 0x2D4 */
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->intr_status & s->intr_mask) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= 0x400) {
        return ~0ULL;
    }

    if (addr == 0x01C && size == 4) {
        val = s->intr_status;
    } else if (addr == 0x020 && size == 4) {
        val = s->intr_mask;
    } else {
        /* General read from regs array */
        switch (size) {
        case 1:
            val = s->regs[addr];
            break;
        case 2:
            val = s->regs[addr] | (s->regs[addr + 1] << 8);
            break;
        case 4:
            val = s->regs[addr] | (s->regs[addr + 1] << 8) |
                  (s->regs[addr + 2] << 16) | (s->regs[addr + 3] << 24);
            break;
        default:
            val = 0;
            break;
        }
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= 0x400) {
        return;
    }

    if (addr == 0x01C && size == 4) {
        /* Write-1-to-clear: clear bits set in val */
        s->intr_status &= ~val;
        pcibase_update_irq(s);
    } else if (addr == 0x020 && size == 4) {
        s->intr_mask = val;
        pcibase_update_irq(s);
    } else {
        /* General write to regs array */
        switch (size) {
        case 1:
            s->regs[addr] = val & 0xff;
            break;
        case 2:
            s->regs[addr] = val & 0xff;
            s->regs[addr + 1] = (val >> 8) & 0xff;
            break;
        case 4:
            s->regs[addr] = val & 0xff;
            s->regs[addr + 1] = (val >> 8) & 0xff;
            s->regs[addr + 2] = (val >> 16) & 0xff;
            s->regs[addr + 3] = (val >> 24) & 0xff;
            break;
        }
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s G_GNUC_UNUSED = opaque;
    uint64_t val = 0;

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s G_GNUC_UNUSED = opaque;
    
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

    /* Clear all register shadows */
    memset(s->regs, 0, sizeof(s->regs));
    s->intr_status = 0;
    s->intr_mask = 0;
    /* Ensure IRQ line is lowered */
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
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
        .size = 0x400,   /* enough for all register offsets up to 0x2D4 */
        .name = "tw68-mmio"
    };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s G_GNUC_UNUSED = PCIBASE_DEVICE(pdev);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }

}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "tw68_pci",
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
