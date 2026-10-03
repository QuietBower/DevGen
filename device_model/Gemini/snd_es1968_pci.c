/*
 * Integrated QEMU PCI device template (QEMU 8.2.10).
 * Designed for register-level modeling and PCIe driver probing.
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

#define TYPE_PCIBASE_DEVICE "snd_es1968_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define DRIVER_NAME "ES1968"
#define CARD_NAME "ESS Maestro1/2"
#define GPIO_DATA	0x60
#define SUPPORT_JOYSTICK 1
#define DEBOUNCE		0x0040
#define NR_APUS			64
#define NR_APU_REGS		16
#define NEC_VERSA_SUBID1	0x80581033
#define NEC_VERSA_SUBID2	0x803c1033
#define ESS_FMT_STEREO     	0x01
#define ESS_FMT_16BIT      	0x02
#define DAC_RUNNING		1
#define ADC_RUNNING		2
#define ESS_DISABLE_AUDIO	0x8000
#define ESS_ENABLE_SERIAL_IRQ	0x4000
#define IO_ADRESS_ALIAS		0x0020
#define MPU401_IRQ_ENABLE	0x0010
#define MPU401_IO_ENABLE	0x0008
#define GAME_IO_ENABLE		0x0004
#define FM_IO_ENABLE		0x0002
#define SB_IO_ENABLE		0x0001
#define PIC_SNOOP1		0x4000
#define PIC_SNOOP2		0x2000
#define SAFEGUARD		0x0800
#define DMA_CLEAR		0x0700
#define DMA_DDMA		0x0000
#define DMA_TDMA		0x0100
#define DMA_PCPCI		0x0200
#define POST_WRITE		0x0080
#define PCI_TIMING		0x0040
#define SWAP_LR			0x0020
#define SUBTR_DECODE		0x0002
#define SPDIF_CONFB		0x0100
#define HWV_CONFB		0x0080
#define GPIO_CONFB		0x0020
#define CHI_CONFB		0x0010
#define IDMA_CONFB		0x0008
#define MIDI_FIX		0x0004
#define IRQ_TO_ISA		0x0001
#define RINGB_2CODEC_ID_MASK	0x0003
#define RINGB_DIS_VALIDATION	0x0008
#define RINGB_EN_SPDIF		0x0010
#define RINGB_EN_2CODEC		0x0020
#define RINGB_SING_BIT_DUAL	0x0040
#define ESM_INDEX		0x02
#define ESM_DATA		0x00
#define ESM_AC97_INDEX		0x30
#define ESM_AC97_DATA		0x32
#define ESM_RING_BUS_DEST	0x34
#define ESM_RING_BUS_CONTR_A	0x36
#define ESM_RING_BUS_CONTR_B	0x38
#define ESM_RING_BUS_SDO	0x3A
#define WC_INDEX		0x10
#define WC_DATA			0x12
#define WC_CONTROL		0x14
#define ASSP_INDEX		0x80
#define ASSP_MEMORY		0x82
#define ASSP_DATA		0x84
#define ASSP_CONTROL_A		0xA2
#define ASSP_CONTROL_B		0xA4
#define ASSP_CONTROL_C		0xA6
#define ASSP_HOSTW_INDEX	0xA8
#define ASSP_HOSTW_DATA		0xAA
#define ASSP_HOSTW_IRQ		0xAC
#define ESM_MPU401_PORT		0x98
#define ESM_PORT_HOST_IRQ	0x18
#define IDR0_DATA_PORT		0x00
#define IDR1_CRAM_POINTER	0x01
#define IDR2_CRAM_DATA		0x02
#define IDR3_WAVE_DATA		0x03
#define IDR4_WAVE_PTR_LOW	0x04
#define IDR5_WAVE_PTR_HI	0x05
#define IDR6_TIMER_CTRL		0x06
#define IDR7_WAVE_ROMRAM	0x07
#define WRITEABLE_MAP		0xEFFFFF
#define READABLE_MAP		0x64003F
#define ESM_LEGACY_AUDIO_CONTROL 0x40
#define ESM_ACPI_COMMAND	0x54
#define ESM_CONFIG_A		0x50
#define ESM_CONFIG_B		0x52
#define ESM_DDMA		0x60
#define ESM_BOB_ENABLE		0x0001
#define ESM_BOB_START		0x0001
#define ESM_RESET_MAESTRO	0x8000
#define ESM_RESET_DIRECTSOUND   0x4000
#define ESM_HIRQ_ClkRun		0x0100
#define ESM_HIRQ_HW_VOLUME	0x0040
#define ESM_HIRQ_HARPO		0x0030
#define ESM_HIRQ_ASSP		0x0010
#define ESM_HIRQ_DSIE		0x0004
#define ESM_HIRQ_MPU401		0x0002
#define ESM_HIRQ_SB		0x0001
#define ESM_MPU401_IRQ		0x02
#define ESM_SB_IRQ		0x01
#define ESM_SOUND_IRQ		0x04
#define ESM_ASSP_IRQ		0x10
#define ESM_HWVOL_IRQ		0x40
#define ESS_SYSCLK		50000000
#define ESM_BOB_FREQ 		200
#define ESM_BOB_FREQ_MAX	800
#define ESM_FREQ_ESM1  		(49152000L / 1024L)
#define ESM_FREQ_ESM2  		(50000000L / 1024L)
#define ESM_APU_MODE_SHIFT	4
#define ESM_APU_MODE_MASK	(0xf << 4)
#define ESM_APU_OFF		0x00
#define ESM_APU_16BITLINEAR	0x01
#define ESM_APU_16BITSTEREO	0x02
#define ESM_APU_8BITLINEAR	0x03
#define ESM_APU_8BITSTEREO	0x04
#define ESM_APU_8BITDIFF	0x05
#define ESM_APU_DIGITALDELAY	0x06
#define ESM_APU_DUALTAP		0x07
#define ESM_APU_CORRELATOR	0x08
#define ESM_APU_INPUTMIXER	0x09
#define ESM_APU_WAVETABLE	0x0A
#define ESM_APU_SRCONVERTOR	0x0B
#define ESM_APU_16BITPINGPONG	0x0C
#define ESM_APU_RESERVED1	0x0D
#define ESM_APU_RESERVED2	0x0E
#define ESM_APU_RESERVED3	0x0F
#define ESM_APU_FILTER_Q_SHIFT		0
#define ESM_APU_FILTER_Q_MASK		(3 << 0)
#define ESM_APU_FILTER_LESSQ	0x00
#define ESM_APU_FILTER_MOREQ	0x03
#define ESM_APU_FILTER_TYPE_SHIFT	2
#define ESM_APU_FILTER_TYPE_MASK	(3 << 2)
#define ESM_APU_ENV_TYPE_SHIFT		8
#define ESM_APU_ENV_TYPE_MASK		(3 << 8)
#define ESM_APU_ENV_STATE_SHIFT		10
#define ESM_APU_ENV_STATE_MASK		(3 << 10)
#define ESM_APU_END_CURVE		(1 << 12)
#define ESM_APU_INT_ON_LOOP		(1 << 13)
#define ESM_APU_DMA_ENABLE		(1 << 14)
#define ESM_APU_SUBMIX_GROUP_SHIRT	0
#define ESM_APU_SUBMIX_GROUP_MASK	(7 << 0)
#define ESM_APU_SUBMIX_MODE		(1 << 3)
#define ESM_APU_6dB			(1 << 4)
#define ESM_APU_DUAL_EFFECT		(1 << 5)
#define ESM_APU_EFFECT_CHANNELS_SHIFT	6
#define ESM_APU_EFFECT_CHANNELS_MASK	(3 << 6)
#define ESM_APU_STEP_SIZE_MASK		0x0fff
#define ESM_APU_PHASE_SHIFT		0
#define ESM_APU_PHASE_MASK		(0xff << 0)
#define ESM_APU_WAVE64K_PAGE_SHIFT	8
#define ESM_APU_WAVE64K_PAGE_MASK	(0xff << 8)
#define ESM_APU_EFFECT_GAIN_SHIFT	0
#define ESM_APU_EFFECT_GAIN_MASK	(0xff << 0)
#define ESM_APU_TREMOLO_DEPTH_SHIFT	8
#define ESM_APU_TREMOLO_DEPTH_MASK	(0xf << 8)
#define ESM_APU_TREMOLO_RATE_SHIFT	12
#define ESM_APU_TREMOLO_RATE_MASK	(0xf << 12)
#define ESM_APU_AMPLITUDE_NOW_SHIFT	8
#define ESM_APU_AMPLITUDE_NOW_MASK	(0xff << 8)
#define ESM_APU_POLAR_PAN_SHIFT		0
#define ESM_APU_POLAR_PAN_MASK		(0x3f << 0)
#define ESM_APU_PAN_CENTER_CIRCLE		0x00
#define ESM_APU_PAN_MIDDLE_RADIUS		0x01
#define ESM_APU_PAN_OUTSIDE_RADIUS		0x02
#define ESM_APU_FILTER_TUNING_SHIFT	8
#define ESM_APU_FILTER_TUNING_MASK	(0xff << 8)
#define ESM_APU_DATA_SRC_A_SHIFT	0
#define ESM_APU_DATA_SRC_A_MASK		(0x7f << 0)
#define ESM_APU_INV_POL_A		(1 << 7)
#define ESM_APU_DATA_SRC_B_SHIFT	8
#define ESM_APU_DATA_SRC_B_MASK		(0x7f << 8)
#define ESM_APU_INV_POL_B		(1 << 15)
#define ESM_APU_VIBRATO_RATE_SHIFT	0
#define ESM_APU_VIBRATO_RATE_MASK	(0xf << 0)
#define ESM_APU_VIBRATO_DEPTH_SHIFT	4
#define ESM_APU_VIBRATO_DEPTH_MASK	(0xf << 4)
#define ESM_APU_VIBRATO_PHASE_SHIFT	8
#define ESM_APU_VIBRATO_PHASE_MASK	(0xff << 8)
#define ESM_APU_RADIUS_SELECT		(1 << 6)
#define ESM_APU_FILTER_2POLE_LOPASS	0x00
#define ESM_APU_FILTER_2POLE_BANDPASS	0x01
#define ESM_APU_FILTER_2POLE_HIPASS	0x02
#define ESM_APU_FILTER_1POLE_LOPASS	0x03
#define ESM_APU_FILTER_1POLE_HIPASS	0x04
#define ESM_APU_FILTER_OFF		0x05
#define ESM_APU_ATFP_AMPLITUDE			0x00
#define ESM_APU_ATFP_TREMELO			0x01
#define ESM_APU_ATFP_FILTER			0x02
#define ESM_APU_ATFP_PAN			0x03
#define ESM_APU_ATFP_FLG_OFF			0x00
#define ESM_APU_ATFP_FLG_WAIT			0x01
#define ESM_APU_ATFP_FLG_DONE			0x02
#define ESM_APU_ATFP_FLG_INPROCESS		0x03
#define ESM_MEM_ALIGN		0x1000
#define ESM_MIXBUF_SIZE		0x400
#define ESM_MODE_PLAY		0
#define ESM_MODE_CAPTURE	1
#define CLOCK_MEASURE_BUFSIZE	16768
#define JOYSTICK_ADDR	0x200
#define IO_MASK		4
#define IO_DIR		8

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
    uint32_t intr_status;
    uint32_t intr_mask;

    uint8_t pio_regs[0x100];
    uint16_t maestro_regs[0x100];
    uint16_t wave_regs[0x200];
    uint16_t apu_regs[64][16];
    uint16_t ac97_regs[0x80];

    uint16_t maestro_index;
    uint16_t wave_index;
    uint16_t ac97_index;

    uint32_t dma_addr;
    uint32_t dma_count;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->intr_status) {
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

    switch (addr) {
    case ESM_INDEX:
        val = s->maestro_index;
        break;
    case ESM_DATA:
        if (s->maestro_index == IDR0_DATA_PORT) {
            uint16_t apu_idx = s->maestro_regs[IDR1_CRAM_POINTER];
            uint8_t ch = (apu_idx >> 4) & 0x3F;
            uint8_t reg = apu_idx & 0x0F;
            val = s->apu_regs[ch][reg];
        } else if (s->maestro_index < 0x100) {
            val = s->maestro_regs[s->maestro_index];
        }
        break;
    case ESM_AC97_INDEX:
        val = s->ac97_index & ~1;
        break;
    case ESM_AC97_DATA:
        val = s->ac97_regs[s->ac97_index & 0x7F];
        break;
    case WC_INDEX:
        val = s->wave_index;
        break;
    case WC_DATA:
        if (s->wave_index < 0x200) {
            val = s->wave_regs[s->wave_index];
        }
        break;
    case 0x1A:
        val = s->intr_status;
        break;
    default:
        if (addr < 0x100 && addr + size <= 0x100) {
            if (size == 1) val = s->pio_regs[addr];
            else if (size == 2) val = *(uint16_t*)(&s->pio_regs[addr]);
            else if (size == 4) val = *(uint32_t*)(&s->pio_regs[addr]);
        }
        break;
    }
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case ESM_INDEX:
        s->maestro_index = val;
        break;
    case ESM_DATA:
        if (s->maestro_index == IDR0_DATA_PORT) {
            uint16_t apu_idx = s->maestro_regs[IDR1_CRAM_POINTER];
            uint8_t ch = (apu_idx >> 4) & 0x3F;
            uint8_t reg = apu_idx & 0x0F;
            s->apu_regs[ch][reg] = val;
        } else if (s->maestro_index < 0x100) {
            s->maestro_regs[s->maestro_index] = val;
        }
        break;
    case ESM_AC97_INDEX:
        s->ac97_index = val;
        break;
    case ESM_AC97_DATA:
        s->ac97_regs[s->ac97_index & 0x7F] = val;
        break;
    case WC_INDEX:
        s->wave_index = val;
        break;
    case WC_DATA:
        if (s->wave_index < 0x200) {
            s->wave_regs[s->wave_index] = val;
        }
        break;
    case 0x1A:
        s->intr_status &= ~val;
        pcibase_update_irq(s);
        break;
    default:
        if (addr < 0x100 && addr + size <= 0x100) {
            if (size == 1) s->pio_regs[addr] = val;
            else if (size == 2) *(uint16_t*)(&s->pio_regs[addr]) = val;
            else if (size == 4) *(uint32_t*)(&s->pio_regs[addr]) = val;
        }
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

    memset(s->pio_regs, 0, sizeof(s->pio_regs));
    memset(s->maestro_regs, 0, sizeof(s->maestro_regs));
    memset(s->wave_regs, 0, sizeof(s->wave_regs));
    memset(s->apu_regs, 0, sizeof(s->apu_regs));
    memset(s->ac97_regs, 0, sizeof(s->ac97_regs));
    s->maestro_index = 0;
    s->wave_index = 0;
    s->ac97_index = 0;
    s->intr_status = 0;
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x1285 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x0100 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_MULTIMEDIA_AUDIO );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pci_set_word(pdev->wmask + ESM_CONFIG_A, 0xFFFF);
    pci_set_word(pdev->wmask + ESM_CONFIG_B, 0xFFFF);
    pci_set_word(pdev->wmask + ESM_LEGACY_AUDIO_CONTROL, 0xFFFF);
    pci_set_word(pdev->wmask + ESM_DDMA, 0xFFFF);
    pci_set_word(pdev->wmask + 0x58, 0xFFFF);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 0x100;
    s->bar_info[0].name = "es1968-pio";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

static const VMStateDescription vmstate_pcibase = {
    .name = "snd_es1968_pci",
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
