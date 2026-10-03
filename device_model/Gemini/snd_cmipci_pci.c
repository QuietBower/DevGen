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

#define TYPE_PCIBASE_DEVICE "snd_cmipci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_CMEDIA		0x13f6
#define PCI_DEVICE_ID_CMEDIA_CM8338A	0x0100

#define CM_REG_FUNCTRL0		0x00
#define CM_RST_CH1		0x00080000
#define CM_RST_CH0		0x00040000
#define CM_CHEN1		0x00020000
#define CM_CHEN0		0x00010000
#define CM_PAUSE1		0x00000008
#define CM_PAUSE0		0x00000004
#define CM_CHADC1		0x00000002
#define CM_CHADC0		0x00000001
#define CM_REG_FUNCTRL1		0x04
#define CM_DSFC_MASK		0x0000E000
#define CM_DSFC_SHIFT		13
#define CM_ASFC_MASK		0x00001C00
#define CM_ASFC_SHIFT		10
#define CM_SPDF_1		0x00000200
#define CM_SPDF_0		0x00000100
#define CM_SPDFLOOP		0x00000080
#define CM_SPDO2DAC		0x00000040
#define CM_INTRM		0x00000020
#define CM_BREQ			0x00000010
#define CM_VOICE_EN		0x00000008
#define CM_UART_EN		0x00000004
#define CM_JYSTK_EN		0x00000002
#define CM_ZVPORT		0x00000001
#define CM_REG_CHFORMAT		0x08
#define CM_CHB3D5C		0x80000000
#define CM_FMOFFSET2		0x40000000
#define CM_CHB3D		0x20000000
#define CM_CHIP_MASK1		0x1f000000
#define CM_CHIP_037		0x01000000
#define CM_SETLAT48		0x00800000
#define CM_EDGEIRQ		0x00400000
#define CM_SPD24SEL39		0x00200000
#define CM_AC3EN1		0x00100000
#define CM_SPDIF_SELECT1	0x00080000
#define CM_SPD24SEL		0x00020000
#define CM_ADCBITLEN_MASK	0x0000C000
#define CM_ADCBITLEN_16		0x00000000
#define CM_ADCBITLEN_15		0x00004000
#define CM_ADCBITLEN_14		0x00008000
#define CM_ADCBITLEN_13		0x0000C000
#define CM_ADCDACLEN_MASK	0x00003000
#define CM_ADCDACLEN_060	0x00000000
#define CM_ADCDACLEN_066	0x00001000
#define CM_ADCDACLEN_130	0x00002000
#define CM_ADCDACLEN_280	0x00003000
#define CM_ADCDLEN_MASK		0x00003000
#define CM_ADCDLEN_ORIGINAL	0x00000000
#define CM_ADCDLEN_EXTRA	0x00001000
#define CM_ADCDLEN_24K		0x00002000
#define CM_ADCDLEN_WEIGHT	0x00003000
#define CM_CH1_SRATE_176K	0x00000800
#define CM_CH1_SRATE_96K	0x00000800
#define CM_CH1_SRATE_88K	0x00000400
#define CM_CH0_SRATE_176K	0x00000200
#define CM_CH0_SRATE_96K	0x00000200
#define CM_CH0_SRATE_88K	0x00000100
#define CM_CH0_SRATE_128K	0x00000300
#define CM_CH0_SRATE_MASK	0x00000300
#define CM_SPDIF_INVERSE2	0x00000080
#define CM_DBLSPDS		0x00000040
#define CM_POLVALID		0x00000020
#define CM_SPDLOCKED		0x00000010
#define CM_CH1FMT_MASK		0x0000000C
#define CM_CH1FMT_SHIFT		2
#define CM_CH0FMT_MASK		0x00000003
#define CM_CH0FMT_SHIFT		0
#define CM_REG_INT_HLDCLR	0x0C
#define CM_CHIP_MASK2		0xff000000
#define CM_CHIP_8768		0x20000000
#define CM_CHIP_055		0x08000000
#define CM_CHIP_039		0x04000000
#define CM_CHIP_039_6CH		0x01000000
#define CM_UNKNOWN_INT_EN	0x00080000
#define CM_TDMA_INT_EN		0x00040000
#define CM_CH1_INT_EN		0x00020000
#define CM_CH0_INT_EN		0x00010000
#define CM_REG_INT_STATUS	0x10
#define CM_INTR			0x80000000
#define CM_VCO			0x08000000
#define CM_MCBINT		0x04000000
#define CM_UARTINT		0x00010000
#define CM_LTDMAINT		0x00008000
#define CM_HTDMAINT		0x00004000
#define CM_XDO46		0x00000080
#define CM_LHBTOG		0x00000040
#define CM_LEG_HDMA		0x00000020
#define CM_LEG_STEREO		0x00000010
#define CM_CH1BUSY		0x00000008
#define CM_CH0BUSY		0x00000004
#define CM_CHINT1		0x00000002
#define CM_CHINT0		0x00000001
#define CM_REG_LEGACY_CTRL	0x14
#define CM_NXCHG		0x80000000
#define CM_VMPU_MASK		0x60000000
#define CM_VMPU_330		0x00000000
#define CM_VMPU_320		0x20000000
#define CM_VMPU_310		0x40000000
#define CM_VMPU_300		0x60000000
#define CM_ENWR8237		0x10000000
#define CM_VSBSEL_MASK		0x0C000000
#define CM_VSBSEL_220		0x00000000
#define CM_VSBSEL_240		0x04000000
#define CM_VSBSEL_260		0x08000000
#define CM_VSBSEL_280		0x0C000000
#define CM_FMSEL_MASK		0x03000000
#define CM_FMSEL_388		0x00000000
#define CM_FMSEL_3C8		0x01000000
#define CM_FMSEL_3E0		0x02000000
#define CM_FMSEL_3E8		0x03000000
#define CM_ENSPDOUT		0x00800000
#define CM_SPDCOPYRHT		0x00400000
#define CM_DAC2SPDO		0x00200000
#define CM_INVIDWEN		0x00100000
#define CM_SETRETRY		0x00100000
#define CM_C_EEACCESS		0x00080000
#define CM_C_EECS		0x00040000
#define CM_C_EEDI46		0x00020000
#define CM_C_EECK46		0x00010000
#define CM_CHB3D6C		0x00008000
#define CM_CENTR2LIN		0x00004000
#define CM_BASE2LIN		0x00002000
#define CM_EXBASEN		0x00001000
#define CM_REG_MISC_CTRL	0x18
#define CM_PWD			0x80000000
#define CM_RESET		0x40000000
#define CM_SFIL_MASK		0x30000000
#define CM_VMGAIN		0x10000000
#define CM_TXVX			0x08000000
#define CM_N4SPK3D		0x04000000
#define CM_SPDO5V		0x02000000
#define CM_SPDIF48K		0x01000000
#define CM_SPATUS48K		0x01000000
#define CM_ENDBDAC		0x00800000
#define CM_XCHGDAC		0x00400000
#define CM_SPD32SEL		0x00200000
#define CM_SPDFLOOPI		0x00100000
#define CM_FM_EN		0x00080000
#define CM_AC3EN2		0x00040000
#define CM_ENWRASID		0x00010000
#define CM_VIDWPDSB		0x00010000
#define CM_SPDF_AC97		0x00008000
#define CM_MASK_EN		0x00004000
#define CM_ENWRMSID		0x00002000
#define CM_VIDWPPRT		0x00002000
#define CM_SFILENB		0x00001000
#define CM_MMODE_MASK		0x00000E00
#define CM_SPDIF_SELECT2	0x00000100
#define CM_ENCENTER		0x00000080
#define CM_FLINKON		0x00000040
#define CM_MUTECH1		0x00000040
#define CM_FLINKOFF		0x00000020
#define CM_MIDSMP		0x00000010
#define CM_UPDDMA_MASK		0x0000000C
#define CM_UPDDMA_2048		0x00000000
#define CM_UPDDMA_1024		0x00000004
#define CM_UPDDMA_512		0x00000008
#define CM_UPDDMA_256		0x0000000C
#define CM_TWAIT_MASK		0x00000003
#define CM_TWAIT1		0x00000002
#define CM_TWAIT0		0x00000001
#define CM_REG_TDMA_POSITION	0x1C
#define CM_TDMA_CNT_MASK	0xFFFF0000
#define CM_TDMA_ADR_MASK	0x0000FFFF
#define CM_REG_MIXER0		0x20
#define CM_REG_SBVR		0x20
#define CM_REG_DEV		0x20
#define CM_REG_MIXER21		0x21
#define CM_UNKNOWN_21_MASK	0x78
#define CM_X_ADPCM		0x04
#define CM_PROINV		0x02
#define CM_X_SB16		0x01
#define CM_REG_SB16_DATA	0x22
#define CM_REG_SB16_ADDR	0x23
#define CM_REG_MIXER1		0x24
#define CM_FMMUTE		0x80
#define CM_FMMUTE_SHIFT		7
#define CM_WSMUTE		0x40
#define CM_WSMUTE_SHIFT		6
#define CM_REAR2LIN		0x20
#define CM_REAR2LIN_SHIFT	5
#define CM_REAR2FRONT		0x10
#define CM_REAR2FRONT_SHIFT	4
#define CM_WAVEINL		0x08
#define CM_WAVEINL_SHIFT	3
#define CM_WAVEINR		0x04
#define CM_WAVEINR_SHIFT	2
#define CM_X3DEN		0x02
#define CM_X3DEN_SHIFT		1
#define CM_CDPLAY		0x01
#define CM_CDPLAY_SHIFT		0
#define CM_REG_MIXER2		0x25
#define CM_RAUXREN		0x80
#define CM_RAUXREN_SHIFT	7
#define CM_RAUXLEN		0x40
#define CM_RAUXLEN_SHIFT	6
#define CM_VAUXRM		0x20
#define CM_VAUXRM_SHIFT		5
#define CM_VAUXLM		0x10
#define CM_VAUXLM_SHIFT		4
#define CM_VADMIC_MASK		0x0e
#define CM_VADMIC_SHIFT		1
#define CM_MICGAINZ		0x01
#define CM_MICGAINZ_SHIFT	0
#define CM_REG_AUX_VOL		0x26
#define CM_VAUXL_MASK		0xf0
#define CM_VAUXR_MASK		0x0f
#define CM_REG_MISC		0x27
#define CM_UNKNOWN_27_MASK	0xd8
#define CM_XGPO1		0x20
#define CM_MIC_CENTER_LFE	0x04
#define CM_SPDIF_INVERSE	0x04
#define CM_SPDVALID		0x02
#define CM_DMAUTO		0x01
#define CM_REG_AC97		0x28
#define CM_REG_EXTERN_CODEC	CM_REG_AC97
#define CM_REG_MPU_PCI		0x40
#define CM_REG_FM_PCI		0x50
#define CM_REG_EXTENT_IND	0xf0
#define CM_VPHONE_MASK		0xe0
#define CM_VPHONE_SHIFT		5
#define CM_VPHOM		0x10
#define CM_VSPKM		0x08
#define CM_RLOOPREN		0x04
#define CM_RLOOPLEN		0x02
#define CM_VADMIC3		0x01
#define CM_REG_PLL		0xf8
#define CM_REG_CH0_FRAME1	0x80
#define CM_REG_CH0_FRAME2	0x84
#define CM_REG_CH1_FRAME1	0x88
#define CM_REG_CH1_FRAME2	0x8C
#define CM_REG_EXT_MISC		0x90
#define CM_ADC48K44K		0x10000000
#define CM_CHB3D8C		0x00200000
#define CM_SPD32FMT		0x00100000
#define CM_ADC2SPDIF		0x00080000
#define CM_SHAREADC		0x00040000
#define CM_REALTCMP		0x00020000
#define CM_INVLRCK		0x00010000
#define CM_UNKNOWN_90_MASK	0x0000FFFF
#define CM_EXTENT_CODEC	  0x100
#define CM_EXTENT_MIDI	  0x2
#define CM_EXTENT_SYNTH	  0x4

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
    uint8_t regs[256];
    uint8_t mixer_regs[256];
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool raise = false;

    if (s->intr_status & (CM_CHINT0 | CM_CHINT1)) {
        s->intr_status |= CM_INTR;
        raise = true;
    } else {
        s->intr_status &= ~CM_INTR;
    }

    if (raise) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= 256) return 0;

    if (addr == CM_REG_SB16_DATA && size == 1) {
        uint8_t idx = s->regs[CM_REG_SB16_ADDR];
        return s->mixer_regs[idx];
    }

    if (addr == CM_REG_INT_STATUS && size == 4) {
        return s->intr_status;
    }

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
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= 256) return;

    if (addr == CM_REG_SB16_DATA && size == 1) {
        uint8_t idx = s->regs[CM_REG_SB16_ADDR];
        s->mixer_regs[idx] = val;
        return;
    }

    if (addr == CM_REG_INT_HLDCLR && size == 4) {
        uint32_t old_val = s->regs[addr] | (s->regs[addr+1]<<8) | (s->regs[addr+2]<<16) | (s->regs[addr+3]<<24);
        uint32_t new_val = val;
        
        if ((old_val & CM_CH0_INT_EN) && !(new_val & CM_CH0_INT_EN)) {
            s->intr_status &= ~CM_CHINT0;
        }
        if ((old_val & CM_CH1_INT_EN) && !(new_val & CM_CH1_INT_EN)) {
            s->intr_status &= ~CM_CHINT1;
        }
        pcibase_update_irq(s);
    }

    switch (size) {
    case 1:
        s->regs[addr] = val;
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

    memset(s->regs, 0, sizeof(s->regs));
    memset(s->mixer_regs, 0, sizeof(s->mixer_regs));
    s->intr_status = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_CMEDIA );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_CMEDIA_CM8338A );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_MULTIMEDIA_AUDIO );
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
        .type = BAR_TYPE_PIO,
        .size = CM_EXTENT_CODEC,
        .name = "cmipci-io"
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
    .name = "snd_cmipci_pci",
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
