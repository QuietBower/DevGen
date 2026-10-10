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


#define TYPE_PCIBASE_DEVICE "snd_sonicvibes_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#ifndef PCI_VENDOR_ID_S3
#define PCI_VENDOR_ID_S3 0x5333
#endif

#define SV_REG_CONTROL	0x00
#define SV_ENHANCED	  0x01
#define SV_TEST	  0x02
#define SV_REVERB	  0x04
#define SV_WAVETABLE	  0x08
#define SV_INTA	  0x20
#define SV_RESET	  0x80
#define SV_REG_IRQMASK	0x01
#define SV_DMAA_MASK	  0x01
#define SV_DMAC_MASK	  0x04
#define SV_SPEC_MASK	  0x08
#define SV_UD_MASK	  0x40
#define SV_MIDI_MASK	  0x80
#define SV_REG_STATUS	0x02
#define SV_DMAA_IRQ	  0x01
#define SV_DMAC_IRQ	  0x04
#define SV_SPEC_IRQ	  0x08
#define SV_UD_IRQ	  0x40
#define SV_MIDI_IRQ	  0x80
#define SV_REG_INDEX	0x04
#define SV_MCE          0x40
#define SV_TRD	  0x80
#define SV_REG_DATA	0x05
#define SV_IREG_LEFT_ADC	0x00
#define SV_IREG_RIGHT_ADC	0x01
#define SV_IREG_LEFT_AUX1	0x02
#define SV_IREG_RIGHT_AUX1	0x03
#define SV_IREG_LEFT_CD		0x04
#define SV_IREG_RIGHT_CD	0x05
#define SV_IREG_LEFT_LINE	0x06
#define SV_IREG_RIGHT_LINE	0x07
#define SV_IREG_MIC		0x08
#define SV_IREG_GAME_PORT	0x09
#define SV_IREG_LEFT_SYNTH	0x0a
#define SV_IREG_RIGHT_SYNTH	0x0b
#define SV_IREG_LEFT_AUX2	0x0c
#define SV_IREG_RIGHT_AUX2	0x0d
#define SV_IREG_LEFT_ANALOG	0x0e
#define SV_IREG_RIGHT_ANALOG	0x0f
#define SV_IREG_LEFT_PCM	0x10
#define SV_IREG_RIGHT_PCM	0x11
#define SV_IREG_DMA_DATA_FMT	0x12
#define SV_IREG_PC_ENABLE	0x13
#define SV_IREG_UD_BUTTON	0x14
#define SV_IREG_REVISION	0x15
#define SV_IREG_ADC_OUTPUT_CTRL	0x16
#define SV_IREG_DMA_A_UPPER	0x18
#define SV_IREG_DMA_A_LOWER	0x19
#define SV_IREG_DMA_C_UPPER	0x1c
#define SV_IREG_DMA_C_LOWER	0x1d
#define SV_IREG_PCM_RATE_LOW	0x1e
#define SV_IREG_PCM_RATE_HIGH	0x1f
#define SV_IREG_SYNTH_RATE_LOW	0x20
#define SV_IREG_SYNTH_RATE_HIGH 0x21
#define SV_IREG_ADC_CLOCK	0x22
#define SV_IREG_ADC_ALT_RATE	0x23
#define SV_IREG_ADC_PLL_M	0x24
#define SV_IREG_ADC_PLL_N	0x25
#define SV_IREG_SYNTH_PLL_M	0x26
#define SV_IREG_SYNTH_PLL_N	0x27
#define SV_IREG_MPU401		0x2a
#define SV_IREG_DRIVE_CTRL	0x2b
#define SV_IREG_SRS_SPACE	0x2c
#define SV_IREG_SRS_CENTER	0x2d
#define SV_IREG_WAVE_SOURCE	0x2e
#define SV_IREG_ANALOG_POWER	0x30
#define SV_IREG_DIGITAL_POWER	0x31
#define SV_DMA_ADDR0		0x00
#define SV_DMA_ADDR1		0x01
#define SV_DMA_ADDR2		0x02
#define SV_DMA_ADDR3		0x03
#define SV_DMA_COUNT0		0x04
#define SV_DMA_COUNT1		0x05
#define SV_DMA_COUNT2		0x06
#define SV_DMA_MODE		0x0b
#define SV_DMA_RESET		0x0d
#define SV_DMA_MASK		0x0f

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
    uint8_t irqmask;
    uint8_t status;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint8_t control;
    uint8_t index;
    uint8_t ireg[0x32];

    /* DMA Context */
    unsigned long dma1size;
    unsigned long dma2size;
    unsigned int dmaa_port;
    unsigned int dmac_port;
    unsigned int p_dma_size;
    unsigned int c_dma_size;
    uint8_t dma_regs[0x10];

    unsigned int mode;
    unsigned char enable;
    unsigned char revision;
    unsigned char format;
    unsigned char srs_space;
    unsigned char srs_center;
    unsigned char mpu_switch;
    unsigned char wave_source;
};



/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    uint8_t active_irqs = s->status & ~s->irqmask;
    pci_set_irq(pdev, active_irqs ? 1 : 0);
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0xffffffff;

    switch (addr) {
        case SV_REG_CONTROL:
            val = s->control;
            break;
        case SV_REG_IRQMASK:
            val = s->irqmask;
            break;
        case SV_REG_STATUS:
            val = s->status;
            s->status = 0; /* Read to clear IRQs */
            pcibase_update_irq(s);
            break;
        case SV_REG_INDEX:
            val = s->index;
            break;
        case SV_REG_DATA:
            if (s->index < sizeof(s->ireg)) {
                val = s->ireg[s->index];
            } else {
                val = 0xff;
            }
            break;
        default:
            val = 0;
            break;
    }
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    switch (addr) {
        case SV_REG_CONTROL:
            if (val & SV_RESET) {
                s->control = SV_RESET;
                s->irqmask = 0xff;
                s->status = 0;
                s->index = 0;
                memset(s->ireg, 0, sizeof(s->ireg));
                s->ireg[SV_IREG_REVISION] = 0x01;
            } else {
                s->control = val & 0xff;
            }
            break;
        case SV_REG_IRQMASK:
            s->irqmask = val & 0xff;
            pcibase_update_irq(s);
            break;
        case SV_REG_STATUS:
            /* Read-to-clear register, ignore writes */
            break;
        case SV_REG_INDEX:
            s->index = val & 0xff;
            break;
        case SV_REG_DATA:
            if (s->index < sizeof(s->ireg)) {
                s->ireg[s->index] = val & 0xff;
                if (s->index == SV_IREG_PC_ENABLE) {
                    s->enable = val & 0xff;
                }
            }
            break;
        default:
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

    /* Initialize DDMA ports to prevent driver from allocating port 0 and failing probe */
    pci_set_long(PCI_DEVICE(dev)->config + 0x40, 0x0800);
    pci_set_long(PCI_DEVICE(dev)->config + 0x48, 0x0810);

    s->control = 0;
    s->irqmask = 0xff;
    s->status = 0;
    s->index = 0;
    memset(s->ireg, 0, sizeof(s->ireg));
    s->ireg[SV_IREG_REVISION] = 0x01;
    s->enable = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_S3 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0xca00 );
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
    s->num_bars = 5;
    s->bar_info[0] = (BARInfo){0, BAR_TYPE_PIO, 0x10, "sb_port"};
    s->bar_info[1] = (BARInfo){1, BAR_TYPE_PIO, 0x10, "enh_port"};
    s->bar_info[2] = (BARInfo){2, BAR_TYPE_PIO, 0x04, "synth_port"};
    s->bar_info[3] = (BARInfo){3, BAR_TYPE_PIO, 0x02, "midi_port"};
    s->bar_info[4] = (BARInfo){4, BAR_TYPE_PIO, 0x08, "game_port"};
      
    for (int i = 0; i < s->num_bars; i++) {
        if (s->bar_info[i].size > 0) {
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
    .name = "snd_sonicvibes_pci",
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
