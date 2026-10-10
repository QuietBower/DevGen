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
/* (none) */

#define TYPE_PCIBASE_DEVICE "snd_ca0106_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x1102
#define DEVICE_ID 0x0007
#define CLASS_ID 0x0401   /* PCI_CLASS_MULTIMEDIA_AUDIO */

/* Register offsets from ca0106_main.c */
#define CA0106_DATA         0x04
#define CA0106_PTR          0x00
#define SPI                 0x7a
#define I2C_D1              0x7d
#define CA0106_INTE         0x0c
#define SPCS0               0x41
#define PLAYBACK_LIST_ADDR  0x00
#define PLAYBACK_DMA_ADDR   0x04
#define PLAYBACK_PERIOD_SIZE 0x05
#define PLAYBACK_LIST_PTR   0x02
#define PLAYBACK_LIST_SIZE  0x01
#define CA0106_HCFG         0x14
#define CAPTURE_BUFFER_SIZE 0x11
#define CAPTURE_DMA_ADDR    0x10
#define BASIC_INTERRUPT     0x40
#define EXTENDED_INT_MASK   0x75
#define PLAYBACK_POINTER    0x06
#define CAPTURE_POINTER     0x12
#define CA0106_AC97ADDRESS  0x1e
#define CA0106_AC97DATA     0x1c
#define CA0106_IPR          0x08
#define EXTENDED_INT        0x76
#define CAPTURE_ROUTING2    0x68
#define CAPTURE_VOLUME1     0x61
#define CAPTURE_SOURCE      0x60
#define SPCS1               0x42
#define PLAYBACK_VOLUME2    0x6a
#define PLAYBACK_MUTE       0x65
#define CAPTURE_MUTE        0x69
#define CAPTURE_VOLUME2     0x62
#define CAPTURE_CONTROL     0x71
#define CAPTURE_ROUTING1    0x67
#define PLAYBACK_ROUTING2   0x64
#define SPCS3               0x44
#define SPDIF_SELECT2       0x72
#define SPDIF_SELECT1       0x45
#define PLAYBACK_VOLUME1    0x66
#define PLAYBACK_ROUTING1   0x63
#define CA0106_GPIO         0x18
#define SPCS2               0x43
#define CA0106_MPU401_ACK   0xfe
#define MIDI_UART_B_DATA    0x6e
#define MIDI_UART_A_DATA    0x6c
#define CA0106_MPU401_ENTER_UART 0x3f
#define CA0106_MPU401_RESET 0xff

/* Bitfield and value defines */
#define HCFG_PLAYBACK_S32_LE  0x00000800
#define HCFG_CAPTURE_S32_LE   0x00000400
#define HCFG_AC97             0x00000008
#define HCFG_AUDIOENABLE      0x00000001
#define SPCS_GENERATIONSTATUS 0x00008000
#define SPCS_SAMPLERATE_48    0x02000000
#define SPCS_COPYRIGHT        0x00000004
#define SPCS_CLKACCY_1000PPM  0x00000000
#define SPCS_CHANNELNUM_LEFT  0x00100000
#define SPCS_EMPHASIS_NONE    0x00000000
#define SPCS_SOURCENUM_UNSPEC 0x00000000
#define ADC_MASTER            0x0000000c
#define ADC_MUX_LINEIN        0x00000004
#define ADC_ATTEN_ADCR        0x0000000f
#define ADC_ATTEN_ADCL        0x0000000e
#define ADC_MUX               0x00000015
#define IPR_MIDI_RX_B         0x00020000
#define IPR_MIDI_RX_A         0x00000004
#define IPR_MIDI_TX_B         0x00010000
#define IPR_MIDI_TX_A         0x00000002
#define INTE_MIDI_TX_A        0x00000002
#define INTE_MIDI_TX_B        0x00010000
#define INTE_MIDI_RX_B        0x00020000
#define CA0106_MIDI_INPUT_AVAIL   0x80
#define CA0106_MIDI_OUTPUT_READY  0x40
#define CA0106_MIDI_CHAN_A        0x1
#define CA0106_MIDI_CHAN_B        0x2

/* SPI DAC register indices, bits, etc. */
#define SPI_DACD2_REG    10
#define SPI_DACD4_REG    15
#define SPI_DACD0_REG    10
#define SPI_DACD1_REG    10
#define SPI_DACD4_BIT    (1<<0)
#define SPI_DACD0_BIT    (1<<1)
#define SPI_DACD2_BIT    (1<<3)
#define SPI_DACD1_BIT    (1<<2)
#define SPI_RDA3_REG     7
#define SPI_RDA1_REG     1
#define SPI_RDA4_REG     14
#define SPI_FMT_REG      3
#define SPI_RDA2_REG     5
#define SPI_LDA3_REG     6
#define SPI_MS_REG       10
#define SPI_MASTDA_REG   8
#define SPI_DA_BIT_UPDATE (1<<8)
#define SPI_LDA4_REG     13
#define SPI_LDA2_REG     4
#define SPI_IZD_BIT      (0<<4)
#define SPI_LDA1_REG     0
#define SPI_IWL_BIT_24   (2<<4)
#define SPI_FMT_BIT_I2S  (2<<0)
#define SPI_PL_BIT_L_L   (1<<5)
#define SPI_PL_BIT_R_R   (2<<7)
#define SPI_PL_REG       2
#define SPI_REG_MASK     0x1ff
#define SPI_DMUTE4_REG   15
#define SPI_DMUTE1_REG   9
#define SPI_DMUTE2_REG   9
#define SPI_DMUTE0_REG   9
#define SPI_DMUTE4_BIT   (1<<2)
#define SPI_DMUTE0_BIT   (1<<3)
#define SPI_DMUTE1_BIT   (1<<4)
#define SPI_DMUTE2_BIT   (1<<5)
#define SPI_REG(reg, value) (((reg) << SPI_REG_SHIFT) | (value))
#define SPI_REG_SHIFT 9

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

#define MAX_INDIR_REGS 256
#define MAX_INDIR_CHNS 4
#define AC97_SIZE 128

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
    uint32_t ptr_val;          /* PTR register (0x00), holds current indirect address */
    uint32_t ipr;              /* Interrupt Pending Register (0x08) */
    uint32_t inte;             /* Interrupt Enable Register (0x0c) */
    uint32_t hcfg;             /* Hardware Config Register (0x14) */
    uint32_t gpio;             /* GPIO output register (0x18) */
    uint8_t ac97_addr;         /* AC97 address register (0x1e) */
    uint16_t ac97_regs[AC97_SIZE]; /* AC97 codec registers */
    uint32_t indir[MAX_INDIR_REGS][MAX_INDIR_CHNS]; /* Indirect register space */
    uint16_t spi_dac_reg[16];  /* SPI DAC register shadows */

    /* MIDI UART state (two channels) */
    struct {
        uint8_t mode;          /* 0: reset, 1: UART */
        uint8_t data_byte;     /* last data byte for read */
        bool ack_pending;      /* ACK (0xFE) pending after reset */
    } midi[2];

    /* Power management state (D0-D3) */
    uint8_t pm_state; /* D0=0, D3=3 */
};

/* Helper: update IRQ line based on IPR and INTE */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->ipr & s->inte) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* PIO Handlers */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case CA0106_PTR:  /* 0x00 */
        val = s->ptr_val;
        break;
    case CA0106_DATA: /* 0x04 */
        {
            uint32_t p = s->ptr_val;
            unsigned reg = (p >> 16) & 0xff;
            unsigned chn = p & 0x3;
            val = s->indir[reg][chn];
            if (reg == SPI) {
                val &= ~0x10000;  /* clear busy bit */
            } else if (reg == MIDI_UART_A_DATA || reg == MIDI_UART_B_DATA) {
                int ch = (reg - MIDI_UART_A_DATA) / 2;
                if (chn == 0) {
                    if (s->midi[ch].ack_pending) {
                        val = 0xfe;
                        s->midi[ch].ack_pending = false;
                    } else {
                        val = s->midi[ch].data_byte;
                    }
                }
            } else if (reg == (MIDI_UART_A_DATA + 1) || reg == (MIDI_UART_B_DATA + 1)) {
                int ch = (reg - (MIDI_UART_A_DATA + 1)) / 2;
                if (chn == 0) {
                    if (s->midi[ch].mode == 1) {
                        val = CA0106_MIDI_OUTPUT_READY; /* 0x40 */
                    } else {
                        val = 0;
                    }
                }
            }
        }
        break;
    case CA0106_IPR:  /* 0x08 */
        val = s->ipr;
        break;
    case CA0106_INTE:  /* 0x0c */
        val = s->inte;
        break;
    case CA0106_HCFG:  /* 0x14 */
        val = s->hcfg;
        break;
    case CA0106_GPIO:  /* 0x18 */
        val = s->gpio;
        break;
    case CA0106_AC97DATA:  /* 0x1c */
        if (s->ac97_addr < AC97_SIZE) {
            val = s->ac97_regs[s->ac97_addr];
        }
        break;
    case CA0106_AC97ADDRESS:  /* 0x1e */
        val = s->ac97_addr;
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
    case CA0106_PTR:  /* 0x00 */
        s->ptr_val = val;
        break;
    case CA0106_DATA: /* 0x04 */
        {
            uint32_t p = s->ptr_val;
            unsigned reg = (p >> 16) & 0xff;
            unsigned chn = p & 0x3;
            if (reg == EXTENDED_INT) {
                s->indir[reg][chn] &= ~val;  /* W1C */
            } else if (reg == (MIDI_UART_A_DATA + 1) || reg == (MIDI_UART_B_DATA + 1)) {
                /* MIDI command register */
                int ch = (reg - (MIDI_UART_A_DATA + 1)) / 2;
                if (chn == 0) {
                    if (val == CA0106_MPU401_RESET) { /* 0xff */
                        s->midi[ch].mode = 0;
                        s->midi[ch].ack_pending = true;
                    } else if (val == CA0106_MPU401_ENTER_UART) { /* 0x3f */
                        s->midi[ch].mode = 1;
                        s->midi[ch].ack_pending = false;
                    }
                    s->indir[reg][chn] = val;
                } else {
                    s->indir[reg][chn] = val;
                }
            } else {
                s->indir[reg][chn] = val;
            }
        }
        break;
    case CA0106_IPR:  /* 0x08 */
        s->ipr &= ~val; /* W1C */
        pcibase_update_irq(s);
        break;
    case CA0106_INTE:  /* 0x0c */
        s->inte = val;
        pcibase_update_irq(s);
        break;
    case CA0106_HCFG:  /* 0x14 */
        s->hcfg = val;
        break;
    case CA0106_GPIO:  /* 0x18 */
        s->gpio = val;
        break;
    case CA0106_AC97DATA:  /* 0x1c */
        if (s->ac97_addr < AC97_SIZE) {
            s->ac97_regs[s->ac97_addr] = val;
        }
        break;
    case CA0106_AC97ADDRESS:  /* 0x1e */
        s->ac97_addr = val;
        break;
    default:
        break;
    }
}

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

    /* Reset direct registers */
    s->ptr_val = 0;
    s->ipr = 0;
    s->inte = 0;
    s->hcfg = 0;
    s->gpio = 0;
    s->ac97_addr = 0;
    memset(s->ac97_regs, 0, sizeof(s->ac97_regs));
    memset(s->indir, 0, sizeof(s->indir));
    memset(s->spi_dac_reg, 0, sizeof(s->spi_dac_reg));
    memset(s->midi, 0, sizeof(s->midi));

    /* Initialize AC97 registers to sane defaults for codec detection */
    /* SigmaTel STAC9700-compatible IDs */
    s->ac97_regs[0x7C] = 0x8384;  /* Vendor ID1 */
    s->ac97_regs[0x7E] = 0x7600;  /* Vendor ID2 */
    s->ac97_regs[0x00] = 0x0000;  /* Reset */
    s->ac97_regs[0x02] = 0x8000;  /* Master volume mute */
    s->ac97_regs[0x04] = 0x8000;  /* Headphone volume mute */
    s->ac97_regs[0x06] = 0x8000;  /* Mono volume mute */
    s->ac97_regs[0x0A] = 0x8000;  /* PC Beep volume mute */
    s->ac97_regs[0x0C] = 0x8008;  /* Phone volume */
    s->ac97_regs[0x0E] = 0x8008;  /* Mic volume */
    s->ac97_regs[0x10] = 0x8808;  /* Line In volume */
    s->ac97_regs[0x12] = 0x8808;  /* CD volume */
    s->ac97_regs[0x14] = 0x8808;  /* Video volume */
    s->ac97_regs[0x16] = 0x8808;  /* Aux volume */
    s->ac97_regs[0x18] = 0x8808;  /* PCM Out volume */
    s->ac97_regs[0x1A] = 0x0000;  /* Record select */
    s->ac97_regs[0x1C] = 0x8000;  /* Record gain */
    s->ac97_regs[0x1E] = 0x8000;  /* Record gain mic */
    s->ac97_regs[0x20] = 0x0000;  /* General purpose */
    s->ac97_regs[0x22] = 0x0000;  /* 3D control */
    s->ac97_regs[0x24] = 0x0000;  /* Audio interrupt and paging */
    s->ac97_regs[0x26] = 0x0000;  /* Page select */
    s->ac97_regs[0x28] = 0x0000;  /* Connect 1 */
    s->ac97_regs[0x2A] = 0x0000;  /* Connect 2 */
    s->ac97_regs[0x2C] = 0x000F;  /* Powerdown ctrl/stat */
    s->ac97_regs[0x2E] = 0x0000;  /* Extended audio ID */
    s->ac97_regs[0x30] = 0x0000;  /* Extended audio status/ctrl */
    s->ac97_regs[0x32] = 0xBB80;  /* PCM front DAC rate: 48000 Hz */

    /* Ensure IRQ is lowered */
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
        /* Not used */
        return;
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

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 0x100; /* covers all registers up to 0xff */
    s->bar_info[0].name = "ca0106-pio";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI or MSI-X not used, driver uses legacy IRQ */
    s->has_msi = false;
    s->has_msix = false;
    s->pm_state = 0; /* D0 */

    /* Bring device to reset state */
    pcibase_reset(DEVICE(s));
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }

    /* No timers or DMA buffers to clean up */
}

/* VMState for migration */
static const VMStateDescription vmstate_pcibase = {
    .name = "snd_ca0106_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(ptr_val, PCIBaseState),
        VMSTATE_UINT32(ipr, PCIBaseState),
        VMSTATE_UINT32(inte, PCIBaseState),
        VMSTATE_UINT32(hcfg, PCIBaseState),
        VMSTATE_UINT32(gpio, PCIBaseState),
        VMSTATE_UINT8(ac97_addr, PCIBaseState),
        VMSTATE_UINT16_ARRAY(ac97_regs, PCIBaseState, AC97_SIZE),
        VMSTATE_UINT32_2DARRAY(indir, PCIBaseState, MAX_INDIR_REGS, MAX_INDIR_CHNS),
        VMSTATE_UINT16_ARRAY(spi_dac_reg, PCIBaseState, 16),
        VMSTATE_UINT8(midi[0].mode, PCIBaseState),
        VMSTATE_UINT8(midi[0].data_byte, PCIBaseState),
        VMSTATE_BOOL(midi[0].ack_pending, PCIBaseState),
        VMSTATE_UINT8(midi[1].mode, PCIBaseState),
        VMSTATE_UINT8(midi[1].data_byte, PCIBaseState),
        VMSTATE_BOOL(midi[1].ack_pending, PCIBaseState),
        VMSTATE_UINT8(pm_state, PCIBaseState),
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