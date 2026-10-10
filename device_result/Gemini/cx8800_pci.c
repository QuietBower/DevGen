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

#define TYPE_PCIBASE_DEVICE "cx8800_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define CX88_VENDOR_ID 0x14f1
#define CX88_DEVICE_ID 0x8800

#define MO_VID_DMACNTRL     0x31C040
#define VID_CAPTURE_CONTROL 0x310180
#define MO_DEVICE_STATUS    0x310100
#define MO_GP1_IO           0x350014
#define MO_GP3_IO           0x35001C
#define MO_GP2_IO           0x350018
#define MO_GP0_IO           0x350010
#define MO_FILTER_ODD       0x310160
#define MO_INPUT_FORMAT     0x310104
#define MO_AFECFG_IO        0x35C04C
#define AUD_CTL             0x32058c
#define AUD_I2SCNTL         0x3205ec
#define MO_FILTER_EVEN      0x31015c
#define MO_VID_INTMSK       0x200050
#define MO_VIDY_GPCNTRL     0x31C030
#define MO_DEV_CNTRL2       0x200034
#define MO_PCI_INTMSK       0x200040
#define MO_VIDY_GPCNT       0x31C020
#define MO_VID_INTSTAT      0x200054
#define MO_VBI_GPCNT        0x31C02C
#define MO_PCI_INTSTAT      0x200044
#define MO_CONTR_BRIGHT     0x310110
#define MO_HTOTAL           0x310120
#define MO_UV_SATURATION    0x310114
#define MO_HUE              0x310118
#define AUD_BAL_CTL         0x320598
#define AUD_VOL_CTL         0x320594
#define AUD_STATUS          0x320590
#define AUD_I2SINPUTCNTL    0x320120
#define MO_GPHST_INTMSK     0x200090
#define MO_AUD_INTMSK       0x200060
#define MO_VIP_INTMSK       0x200080
#define MO_GPHST_DMACNTRL   0x38C040
#define MO_AUD_DMACNTRL     0x32C040
#define MO_TS_DMACNTRL      0x33C040
#define MO_VIP_DMACNTRL     0x34C040
#define MO_TS_INTMSK        0x200070
#define MO_PLL_REG          0x310168
#define MO_SAMPLE_IO        0x35C058
#define MO_VBOS_CONTROL     0x3101a8
#define MO_VBI_GPCNTRL      0x31C03C
#define MO_AUD_INTSTAT      0x200064
#define MO_AUDR_LNGTH       0x32C04C
#define MO_AUDD_LNGTH       0x32C048
#define MO_SRST_IO          0x35C05C
#define MO_I2C              0x368000
#define CX88X_DEVCTRL       0x40

#define MO_INT1_STAT        0x35C064
#define MO_PDMA_STHRSH      0x200000
#define MO_PDMA_DTHRSH      0x200010
#define MO_AGC_SYNC_TIP1    0x310208
#define MO_AGC_BACK_VBI     0x310200
#define MO_HDELAY_EVEN      0x310124
#define MO_HDELAY_ODD       0x310128
#define MO_HSCALE_EVEN      0x31014c
#define MO_HSCALE_ODD       0x310150
#define MO_HACTIVE_EVEN     0x31013c
#define MO_HACTIVE_ODD      0x310140
#define MO_VDELAY_EVEN      0x310130
#define MO_VDELAY_ODD       0x31012c
#define MO_VSCALE_EVEN      0x310154
#define MO_VSCALE_ODD       0x310158
#define MO_VACTIVE_EVEN     0x310144
#define MO_VACTIVE_ODD      0x310148
#define MO_OUTPUT_FORMAT    0x310164
#define MO_SCONV_REG        0x310170
#define MO_SUB_STEP         0x310178
#define MO_SUB_STEP_DR      0x31017c
#define MO_AGC_BURST        0x31010c
#define MO_VBI_PACKET       0x310188

#define PCI_INT_VIDINT      (1 << 0)
#define PCI_INT_IR_SMPINT   (1 << 18)
#define PCI_INT_RISC_RD_BERRINT (1 << 10)
#define PCI_INT_RISC_WR_BERRINT (1 << 11)
#define PCI_INT_SRC_DMA_BERRINT (1 << 13)
#define PCI_INT_IPB_DMA_BERRINT (1 << 15)
#define PCI_INT_DST_DMA_BERRINT (1 << 14)
#define PCI_INT_BRDG_BERRINT    (1 << 12)

#define RISC_CNT_INC        0x00010000
#define RISC_IRQ1           0x01000000
#define RISC_JUMP           0x70000000
#define RISC_EOL            0x04000000
#define RISC_WRITE          0x10000000
#define RISC_RESYNC         0x80008000
#define RISC_SOL            0x08000000
#define RISC_WRITEC         0x50000000
#define RISC_SKIP           0x20000000
#define RISC_SYNC           0x80000000
#define RISC_WRITERM        0xB0000000
#define RISC_WRITECM        0xC0000000
#define RISC_READC          0xA0000000
#define RISC_READ           0x90000000
#define RISC_WRITECR        0xD0000000

#define MO_COLOR_CTRL       0x310184
#define ColorFormatGamma    0x1000
#define GP_COUNT_CONTROL_RESET 0x3
#define SRAM_CH21           0

#define VideoFormatNTSCJapan     0x2
#define VideoFormatNTSC443       0x3
#define VideoFormatPALM          0x5
#define VideoFormatPALN          0x6
#define VideoFormatPALNC         0x7
#define VideoFormatPAL60         0x8
#define VideoFormatNTSC          0x1
#define VideoFormatSECAM         0x9
#define VideoFormatPAL           0x4

/* Newly added definitions */
#define AUD_INIT                 0x320100
#define AUD_INIT_LD              0x320104
#define AUD_SOFT_RESET           0x320108
#define AUD_BAUDRATE             0x320124
#define AUD_I2SOUTPUTCNTL        0x320128
#define AUD_RATE_THRES_DMD       0x3205d0
#define EN_I2SOUT_ENABLE        0x00002000
#define EN_DAC_ENABLE           0x00001000
#define VBI_LINE_LENGTH           2048
#define NO_SYNC_LINE (-1U)
#define EN_FMRADIO_EN_RDS       0x00000200
#define SEL_SAP      0x08
#define SEL_BTSC     0x01
#define EN_DMTRX_LR             (2 << 7)
#define EN_DMTRX_BYPASS         (1 << 11)
#define SEL_NICAM    0x10
#define EN_DMTRX_SUMDIFF        (0 << 7)
#define SEL_A2       0x04
#define SEL_EIAJ     0x02
#define EN_EIAJ_AUTO_STEREO     20
#define SEL_FMRADIO  0x20
#define EN_FMRADIO_AUTO_STEREO  26
#define SHADOW_AUD_VOL_CTL           1

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
    uint32_t pci_intmsk;
    uint32_t pci_intstat;
    uint32_t vid_intmsk;
    uint32_t vid_intstat;
    uint32_t aud_intmsk;
    uint32_t aud_intstat;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t mmio_regs[0x400000 / 4];

    /* DMA Context */
    uint32_t vid_dmacntrl;
    uint32_t aud_dmacntrl;
    uint32_t ts_dmacntrl;
    uint32_t vip_dmacntrl;
    uint32_t gphst_dmacntrl;

    uint32_t device_status;
    uint32_t srst_io;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool raise = false;

    if (s->mmio_regs[MO_PCI_INTSTAT / 4] & s->mmio_regs[MO_PCI_INTMSK / 4]) {
        raise = true;
    }

    pci_set_irq(pdev, raise ? 1 : 0);
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /* Simulate DMA completion for video */
    s->mmio_regs[MO_VID_INTSTAT / 4] |= 0x01; /* risc1 y */
    s->mmio_regs[MO_VID_INTSTAT / 4] |= 0x08; /* risc1 vbi */
    
    /* Cascade to PCI INT STAT */
    if (s->mmio_regs[MO_VID_INTSTAT / 4] & s->mmio_regs[MO_VID_INTMSK / 4]) {
        s->mmio_regs[MO_PCI_INTSTAT / 4] |= PCI_INT_VIDINT;
    }
    
    pcibase_update_irq(s);
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr < 0x400000) {
        return s->mmio_regs[addr / 4];
    }
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr >= 0x400000) return;

    uint32_t reg = addr / 4;
    
    switch (addr) {
    case MO_PCI_INTSTAT:
    case MO_VID_INTSTAT:
    case MO_AUD_INTSTAT:
        s->mmio_regs[reg] &= ~val; /* W1C */
        pcibase_update_irq(s);
        break;
    case MO_PCI_INTMSK:
    case MO_VID_INTMSK:
    case MO_AUD_INTMSK:
    case MO_TS_INTMSK:
    case MO_VIP_INTMSK:
    case MO_GPHST_INTMSK:
        s->mmio_regs[reg] = val;
        pcibase_update_irq(s);
        break;
    case MO_VID_DMACNTRL:
        s->mmio_regs[reg] = val;
        if (val & 0x11) {
            pcibase_do_dma(s, false);
        } else if (val & 0x88) {
            /* VBI DMA start */
            pcibase_do_dma(s, false);
        }
        break;
    case MO_SRST_IO:
        s->mmio_regs[reg] = val;
        if (val == 0) {
            /* Driver asserts reset */
        }
        break;
    case MO_PLL_REG:
        s->mmio_regs[reg] = val;
        /* Simulate PLL lock */
        s->mmio_regs[MO_DEVICE_STATUS / 4] |= (1 << 2);
        break;
    default:
        s->mmio_regs[reg] = val;
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
    memset(s->mmio_regs, 0, sizeof(s->mmio_regs));
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x14f1 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x8800 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_MULTIMEDIA_VIDEO );
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
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x400000;
    s->bar_info[0].name = "cx88-mmio";

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
    .name = "cx8800_pci",
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
